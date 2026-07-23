#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const uint32_t K = 2; // block size: K data frames share 1 XOR parity frame

int main(void)
{
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (in_fd < 0)
        return 1;

    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_port = htons(47010);
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

    struct sockaddr_in relay_addr = {0};
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(47001);
    relay_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint8_t recv_buf[2048];
    uint8_t send_buf[164];
    uint8_t block_xor[160] = {0};

    for (;;)
    {
        ssize_t n = recvfrom(in_fd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n < 164)
            continue;

        uint32_t net_seq;
        std::memcpy(&net_seq, recv_buf, 4);
        uint32_t seq = ntohl(net_seq);
        const uint8_t *payload = recv_buf + 4;

        // Data packet: 4B header (bit31=0, seq) + 160B primary payload.
        uint32_t wire_seq = htonl(seq);
        std::memcpy(send_buf, &wire_seq, 4);
        std::memcpy(send_buf + 4, payload, 160);
        sendto(out_fd, send_buf, 164, 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));

        uint32_t idx_in_block = seq % K;
        if (idx_in_block == 0)
        {
            std::memcpy(block_xor, payload, 160);
        }
        else
        {
            for (size_t k = 0; k < 160; ++k)
                block_xor[k] ^= payload[k];
        }

        if (idx_in_block == K - 1)
        {
            // Block complete: send parity packet (bit31=1, block_id) + XOR payload.
            uint32_t block_id = seq / K;
            uint32_t wire_block = htonl(block_id | 0x80000000U);
            std::memcpy(send_buf, &wire_block, 4);
            std::memcpy(send_buf + 4, block_xor, 160);
            sendto(out_fd, send_buf, 164, 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));
        }
    }

    close(in_fd);
    close(out_fd);
    return 0;
}
