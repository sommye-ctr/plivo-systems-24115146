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
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_fd < 0)
        return 1;

    struct sockaddr_in relay_addr = {0};
    relay_addr.sin_family = AF_INET;
    relay_addr.sin_port = htons(47001);
    relay_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    uint8_t payload_history[8][160];
    uint8_t recv_buf[2048];
    uint8_t send_buf[325];

    for (;;)
    {
        ssize_t n = recvfrom(in_fd, recv_buf, sizeof(recv_buf), 0, NULL, NULL);
        if (n < 164)
            continue;

        uint32_t net_seq;
        std::memcpy(&net_seq, recv_buf, 4);
        uint32_t seq = ntohl(net_seq);

        std::memcpy(payload_history[seq % 8], recv_buf + 4, 160);

        bool send_redundant = (seq >= 3) && (seq % 4 != 0);

        uint32_t wire_seq = htonl(seq);
        std::memcpy(send_buf, &wire_seq, 4);
        send_buf[4] = send_redundant ? 1 : 0;
        std::memcpy(send_buf + 5, recv_buf + 4, 160);

        size_t total_len = 165;
        if (send_redundant)
        {
            uint32_t rseq = seq - 3;
            std::memcpy(send_buf + 165, payload_history[rseq % 8], 160);
            total_len += 160;
        }

        sendto(out_fd, send_buf, total_len, 0, (struct sockaddr *)&relay_addr, sizeof(relay_addr));
    }

    close(in_fd);
    close(out_fd);
    return 0;
}
