#include <st.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    if (argc < 5) {
        printf("Usage: %s <host> <port> <pong> <delay>\n", argv[0]);
        printf("    pong        Whether response pong, true|false\n");
        printf("    delay       The delay in ms to response pong.\n");
        printf("For example:\n");
        printf("        %s 0.0.0.0 8000 true 100\n", argv[0]);
        exit(-1);
    }

    char* host = argv[1];
    int port = atoi(argv[2]);
    bool pong = !strcmp(argv[3], "true");
    int delay = ::atoi(argv[4]);
    printf("Server listen %s:%d, pong %d, delay: %dms\n", host, port, pong, delay);

    assert(!st_set_eventsys(ST_EVENTSYS_ALT));
    assert(!st_init());

    int fd = socket(PF_INET, SOCK_DGRAM, 0);
    assert(fd > 0);

    int n = 1;
    int r0 = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&n, sizeof(n));
    assert(!r0);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(sockaddr_in));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    r0 = bind(fd, (sockaddr *)&addr, sizeof(sockaddr_in));
    assert(!r0);

    st_netfd_t stfd = st_netfd_open_socket(fd);
    assert(stfd);

    printf("Listen at udp://%s:%d\n", host, port);

    msghdr msg;
    memset(&msg, 0, sizeof(msghdr));

    sockaddr_in peer;
    memset(&peer, 0, sizeof(sockaddr_in));
    msg.msg_name = (sockaddr_in*)&peer;
    msg.msg_namelen = sizeof(sockaddr_in);

    char buf[1500];
    memset(buf, 0, sizeof(buf));

    iovec iov;
    memset(&iov, 0, sizeof(iovec));
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    while (true) {
        r0 = st_recvmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
        assert(r0 > 0);
        printf("From %s:%d %d bytes, flags %#x, %s\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), r0,
            msg.msg_flags, msg.msg_iov->iov_base);

        memcpy(msg.msg_iov->iov_base, "World", 5);
        msg.msg_iov->iov_len = 5;

        if (pong) {
            if (delay > 0) {
                st_usleep(delay * 1000);
            }

            r0 = st_sendmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
            assert(r0 > 0);
            printf("Pong %s:%d %d bytes, flags %#x, %s\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), r0,
                msg.msg_flags, msg.msg_iov->iov_base);
        }
    }

    return 0;
}
