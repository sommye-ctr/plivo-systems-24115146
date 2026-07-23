#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

struct Slot
{
    uint32_t seq{0};
    bool present{false};
    uint8_t payload[160]{0};
};

int main(void)
{
    const char *delay_env = std::getenv("DELAY_MS");
    double delay_ms = delay_env ? std::atof(delay_env) : 60.0;

    const char *t0_env = std::getenv("T0");
    double t0 = t0_env ? std::atof(t0_env) : 0.0;

    size_t capacity = static_cast<size_t>(std::ceil(delay_ms / 20.0)) + 3 + 5;

    std::vector<Slot> ring(capacity);
    std::mutex ring_mutex;
    uint32_t next_to_play = 0;

    std::thread network_thread([&]()
                               {
        int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (in_fd < 0) return;

        struct sockaddr_in in_addr = {0};
        in_addr.sin_family = AF_INET;
        in_addr.sin_port = htons(47002);
        in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
            close(in_fd);
            return;
        }

        uint8_t buf[2048];
        for (;;) {
            ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
            if (n < 164) continue;

            uint32_t net_seq;
            std::memcpy(&net_seq, buf, 4);
            uint32_t raw_seq = ntohl(net_seq);
            bool has_fec = (raw_seq & 0x80000000U) != 0;
            uint32_t seq = raw_seq & 0x7FFFFFFFU;

            if (has_fec && n < 324) continue;

            std::lock_guard<std::mutex> lock(ring_mutex);

            // 1. Store primary payload for frame seq
            if (seq >= next_to_play) {
                size_t idx = seq % capacity;
                if (!ring[idx].present || ring[idx].seq != seq) {
                    ring[idx].seq = seq;
                    ring[idx].present = true;
                    std::memcpy(ring[idx].payload, buf + 4, 160);
                }
            }

            // 2. Reconstruct frame seq - 1 using FEC parity if missing
            if (has_fec && seq >= 1) {
                uint32_t rseq = seq - 1;
                if (rseq >= next_to_play) {
                    size_t ridx = rseq % capacity;
                    if (!ring[ridx].present || ring[ridx].seq != rseq) {
                        for (size_t k = 0; k < 160; ++k) {
                            ring[ridx].payload[k] = buf[4 + k] ^ buf[164 + k];
                        }
                        ring[ridx].present = true;
                        ring[ridx].seq = rseq;
                    }
                }
            }
        }
        close(in_fd); });

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_fd < 0)
    {
        network_thread.join();
        return 1;
    }

    struct sockaddr_in player_addr = {0};
    player_addr.sin_family = AF_INET;
    player_addr.sin_port = htons(47020);
    player_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Playout clock target: 10ms before deadline to absorb OS thread sleep scheduling jitter
    double start_sec = t0 + (delay_ms / 1000.0) - 0.010;
    using Clock = std::chrono::system_clock;
    auto start_tp = Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(start_sec)));

    for (uint32_t i = 0;; ++i)
    {
        auto deadline = start_tp + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(i * 0.020));
        std::this_thread::sleep_until(deadline);

        bool found = false;
        uint8_t out_buf[164];

        {
            std::lock_guard<std::mutex> lock(ring_mutex);
            size_t idx = i % capacity;
            if (ring[idx].present && ring[idx].seq == i)
            {
                found = true;
                uint32_t wire_seq = htonl(i);
                std::memcpy(out_buf, &wire_seq, 4);
                std::memcpy(out_buf + 4, ring[idx].payload, 160);
                ring[idx].present = false;
            }
            next_to_play = i + 1;
        }

        if (found)
        {
            sendto(out_fd, out_buf, 164, 0, (struct sockaddr *)&player_addr, sizeof(player_addr));
        }
    }

    network_thread.join();
    close(out_fd);
    return 0;
}
