#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    uint8_t payload_history[8][160];
    uint8_t recv_buf[2048];
    uint8_t send_buf[324];

    for (;;)
    {
        ssize_t n = recvfrom(in_fd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n < 164)
            continue;

        uint32_t net_seq;
        std::memcpy(&net_seq, recv_buf, 4);
        uint32_t seq = ntohl(net_seq);

        std::memcpy(payload_history[seq % 8], recv_buf + 4, 160);

        // FEC Parity P_N = Payload(N) XOR Payload(N-1) for N >= 1, skipping 1 in 35 to stay strictly under 2.0x overhead
        bool send_fec = (seq > 0) && (seq % 35 != 0);

        uint32_t wire_seq = htonl(seq | (send_fec ? 0x80000000U : 0U));
        std::memcpy(send_buf, &wire_seq, 4);
        std::memcpy(send_buf + 4, recv_buf + 4, 160);

        size_t total_len = 164;
        if (send_fec)
        {
            uint32_t prev_seq = seq - 1;
            const uint8_t *curr_p = recv_buf + 4;
            const uint8_t *prev_p = payload_history[prev_seq % 8];
            uint8_t *fec_p = send_buf + 164;
            for (size_t k = 0; k < 160; ++k)
            {
                fec_p[k] = curr_p[k] ^ prev_p[k];
            }
            total_len += 160;
        }

        sendto(out_fd, send_buf, total_len, 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));
    }

    close(in_fd);
    close(out_fd);
    return 0;
}
