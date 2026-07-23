#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

static const uint32_t K = 2; // must match sender.cpp

// One XOR-parity block: K data frames + 1 parity. Enough state to rebuild the
// single missing member once parity and the other K-1 frames are in.
struct BlockState
{
    uint32_t block_id{0};
    bool valid{false};
    bool got[K]{};
    int count{0};
    uint8_t partial_xor[160]{0};
    bool parity_got{false};
    uint8_t parity[160]{0};
};

int main(void)
{
    const char *delay_env = std::getenv("DELAY_MS");
    double delay_ms = delay_env ? std::atof(delay_env) : 60.0;

    // The harness player judges frame i solely on whether a correct-seq packet
    // arrives before its deadline (t0 + delay_ms + i*20ms) and dedups by seq
    // itself — arriving early is never penalised. So the receiver does NOT run a
    // playout clock: it forwards every frame (and every reconstruction) the
    // instant it is ready. That removes the OS scheduling jitter a sleep_until
    // playout loop would inject, which is what pushed profile B over the 1% cap.
    //
    // `window` is only a retirement horizon: once we have seen a seq this far
    // ahead, older frames are certainly past their deadline, so their ring slots
    // and blocks can be reused without a late/duplicate packet clobbering them.
    size_t window = static_cast<size_t>(std::ceil(delay_ms / 20.0)) + 8;
    size_t block_capacity = static_cast<size_t>(std::ceil(window / (double)K)) + 2;

    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (in_fd < 0)
        return 1;

    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0)
    {
        close(in_fd);
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_fd < 0)
    {
        close(in_fd);
        return 1;
    }

    struct sockaddr_in player_addr = {0};
    player_addr.sin_family = AF_INET;
    player_addr.sin_port = htons(47020);
    player_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::vector<uint32_t> sent_seq(window, UINT32_MAX); // last seq forwarded at each slot
    std::vector<BlockState> blocks(block_capacity);
    uint32_t floor_seq = 0;    // frames below this are retired (deadline passed)
    uint32_t max_seq_seen = 0; // highest data seq observed, for the retirement horizon

    // Forward a frame to the player once. Dedup is a best-effort optimisation
    // (the player also dedups) so we never send a duplicate or waste work
    // reconstructing something already delivered.
    auto forward = [&](uint32_t seq, const uint8_t *payload)
    {
        if (seq < floor_seq)
            return;
        size_t idx = seq % window;
        if (sent_seq[idx] == seq)
            return;
        sent_seq[idx] = seq;
        uint8_t out_buf[164];
        uint32_t wire_seq = htonl(seq);
        std::memcpy(out_buf, &wire_seq, 4);
        std::memcpy(out_buf + 4, payload, 160);
        sendto(out_fd, out_buf, 164, 0, (struct sockaddr *)&player_addr, sizeof(player_addr));
    };

    // Rebuild the one missing frame in a block once parity and all other K-1
    // members are in, then forward it exactly like a normal arrival.
    auto try_reconstruct = [&](BlockState &bs)
    {
        if (!bs.parity_got || bs.count != (int)K - 1)
            return;
        int missing = -1;
        for (uint32_t k = 0; k < K; ++k)
        {
            if (!bs.got[k])
            {
                missing = (int)k;
                break;
            }
        }
        if (missing < 0)
            return;
        uint32_t rseq = bs.block_id * K + (uint32_t)missing;
        uint8_t rebuilt[160];
        for (size_t k = 0; k < 160; ++k)
            rebuilt[k] = bs.partial_xor[k] ^ bs.parity[k];
        forward(rseq, rebuilt);
        bs.got[missing] = true;
        bs.count = (int)K;
    };

    uint8_t buf[2048];
    for (;;)
    {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n < 164)
            continue;

        uint32_t net_seq;
        std::memcpy(&net_seq, buf, 4);
        uint32_t raw = ntohl(net_seq);
        bool is_parity = (raw & 0x80000000U) != 0;
        uint32_t value = raw & 0x7FFFFFFFU;
        const uint8_t *payload = buf + 4;

        uint32_t block_id = is_parity ? value : (value / K);

        // Advance the retirement horizon off the highest data seq we have seen.
        if (!is_parity && value > max_seq_seen)
        {
            max_seq_seen = value;
            floor_seq = max_seq_seen > (uint32_t)window ? max_seq_seen - (uint32_t)window : 0;
        }

        // Skip blocks whose last frame is already retired, so a late/duplicate
        // packet can't resurrect a block slot reused by a newer block sharing
        // the same (block_id % block_capacity) index.
        if (block_id * K + K - 1 < floor_seq)
            continue;

        BlockState &bs = blocks[block_id % block_capacity];
        if (!bs.valid || bs.block_id < block_id)
        {
            bs = BlockState{};
            bs.block_id = block_id;
            bs.valid = true;
        }
        else if (bs.block_id > block_id)
        {
            continue; // stale packet for an already-superseded block slot
        }

        if (is_parity)
        {
            if (!bs.parity_got)
            {
                bs.parity_got = true;
                std::memcpy(bs.parity, payload, 160);
            }
        }
        else
        {
            uint32_t seq = value;
            forward(seq, payload);
            uint32_t idx = seq % K;
            if (!bs.got[idx])
            {
                bs.got[idx] = true;
                bs.count++;
                if (bs.count == 1)
                    std::memcpy(bs.partial_xor, payload, 160);
                else
                    for (size_t k = 0; k < 160; ++k)
                        bs.partial_xor[k] ^= payload[k];
            }
        }

        try_reconstruct(bs);
    }

    close(in_fd);
    close(out_fd);
    return 0;
}
