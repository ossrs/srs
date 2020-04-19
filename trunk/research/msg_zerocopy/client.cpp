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
    if (argc < 3) {
        printf("Usage: %s <host> <port>\n", argv[0]);
        printf("For example:\n");
        printf("        %s 127.0.0.1 8000\n", argv[0]);
        exit(-1);
    }

    assert(!st_set_eventsys(ST_EVENTSYS_ALT));
    assert(!st_init());

    int fd = socket(PF_INET, SOCK_DGRAM, 0);
    assert(fd > 0);

    st_netfd_t stfd = st_netfd_open_socket(fd);
    assert(stfd);

    sockaddr_in peer;
    memset(&peer, 0, sizeof(sockaddr_in));

    int port = 8000;
    const char* host = "127.0.0.1";
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(host);

    char buf[1500];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "Hello", 5);

    iovec iov;
    iov.iov_base = buf;
    iov.iov_len = strlen(buf);

    msghdr msg;
    memset(&msg, 0, sizeof(msghdr));
    msg.msg_name = (sockaddr_in*)&peer;
    msg.msg_namelen = sizeof(sockaddr_in);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int r0 = st_sendmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
    printf("Ping %s:%d %d bytes, r0=%d, %s\n", host, port, iov.iov_len, r0, msg.msg_iov->iov_base);

    r0 = st_recvmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
    assert(r0 > 0);
    printf("From %s:%d %d bytes, flags %#x, %s\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), r0,
        msg.msg_flags, msg.msg_iov->iov_base);

    return 0;
}
