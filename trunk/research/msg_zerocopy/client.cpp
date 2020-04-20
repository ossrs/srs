#include <st.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/version.h>

// @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#notification-reception
#include <sys/epoll.h>

// @see https://github.com/torvalds/linux/blob/master/tools/testing/selftests/net/msg_zerocopy.c
#include <linux/errqueue.h>
#ifndef SO_EE_ORIGIN_ZEROCOPY
#define SO_EE_ORIGIN_ZEROCOPY		5
#endif

#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY	60
#endif

#ifndef SO_EE_CODE_ZEROCOPY_COPIED
#define SO_EE_CODE_ZEROCOPY_COPIED	1
#endif

#ifndef MSG_ZEROCOPY
#define MSG_ZEROCOPY	0x4000000
#endif

void* receiver(void* arg)
{
    st_netfd_t stfd = (st_netfd_t)arg;

    for (;;) {
        sockaddr_in peer;
        memset(&peer, 0, sizeof(sockaddr_in));

        char buf[1500];
        memset(buf, 0, sizeof(buf));

        iovec iov;
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);

        msghdr msg;
        memset(&msg, 0, sizeof(msghdr));
        msg.msg_name = (sockaddr_in*)&peer;
        msg.msg_namelen = sizeof(sockaddr_in);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        int r0 = st_recvmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
        assert(r0 > 0);
        printf("Pong %s:%d %d bytes, flags %#x, %s\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), r0,
            msg.msg_flags, msg.msg_iov->iov_base);
    }

    return NULL;
}

void parse_reception(st_netfd_t stfd)
{
    msghdr msg;
    memset(&msg, 0, sizeof(msghdr));

    // Reception from kernel, @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#notification-reception
    // See do_recv_completion at https://github.com/torvalds/linux/blob/master/tools/testing/selftests/net/msg_zerocopy.c#L393
    char control[100];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    // Note that the r0 is 0, the reception is in the control.
    int r0 = st_recvmsg(stfd, &msg, MSG_ERRQUEUE, ST_UTIME_NO_TIMEOUT);
    assert(r0 >= 0);
    assert(msg.msg_flags == MSG_ERRQUEUE);

    // Notification parsing, @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#notification-parsing
    cmsghdr* cm = CMSG_FIRSTHDR(&msg);
    assert(cm->cmsg_level == SOL_IP || cm->cmsg_type == IP_RECVERR);

    sock_extended_err* serr = (sock_extended_err*)(void*)CMSG_DATA(cm);
    assert(serr->ee_errno == 0 && serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY);

    uint32_t hi = serr->ee_data;
    uint32_t lo = serr->ee_info;
    uint32_t range = hi - lo + 1;
    printf("Reception %d bytes, flags %#x, cmsg(level %#x, type %#x), serr(errno %#x, origin %#x, code %#x), range %d [%d, %d]\n",
        msg.msg_controllen, msg.msg_flags, cm->cmsg_level, cm->cmsg_type, serr->ee_errno, serr->ee_origin, serr->ee_code, range, lo, hi);

    // Defered Copies, @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#deferred-copies
    if (serr->ee_code == SO_EE_CODE_ZEROCOPY_COPIED) {
        printf("Warning: Defered copies, should stop zerocopy\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 8) {
        printf("Usage: %s <host> <port> <pong> <zerocopy> <sendmmsg> <loop> <batch>\n", argv[0]);
        printf("    pong        Whether response pong, true|false\n");
        printf("    zerocopy    Whether use zerocopy to sendmsg, true|false\n");
        printf("    sendmmsg    The copies of message, 1 means sendmmsg(msg+msg)\n");
        printf("    loop        The number of loop to send out messages\n");
        printf("    batch       Whether read reception by batch, true|false\n");
        printf("For example:\n");
        printf("        %s 127.0.0.1 8000 true true 0 1 true\n", argv[0]);
        exit(-1);
    }

    char* host = argv[1];
    int port = atoi(argv[2]);
    bool pong = !strcmp(argv[3], "true");
    bool zerocopy = !strcmp(argv[4], "true");
    int nn_copies = atoi(argv[5]);
    int loop = atoi(argv[6]);
    bool batch = !strcmp(argv[7], "true");
    printf("Server listen %s:%d, pong %d, zerocopy %d, copies %d, loop %d, batch %d\n",
        host, port, pong, zerocopy, nn_copies, loop, batch);

    assert(!st_set_eventsys(ST_EVENTSYS_ALT));
    assert(!st_init());

    int fd = socket(PF_INET, SOCK_DGRAM, 0);
    assert(fd > 0);

    // @see https://github.com/torvalds/linux/blob/master/tools/testing/selftests/net/msg_zerocopy.c
    if (zerocopy) {
        int one = 1;
        int r0 = setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one));
// MSG_ZEROCOPY for UDP was added in commit b5947e5d1e71 ("udp: msg_zerocopy") in Linux 5.0.
// @see https://lore.kernel.org/netdev/CA+FuTSfBFqRViKfG5crEv8xLMgAkp3cZ+yeuELK5TVv61xT=Yw@mail.gmail.com/
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
        if (r0 == -1) {
            printf("MSG_ZEROCOPY should be kernel 5.0+, kernel %#x, errno=%d\n", LINUX_VERSION_CODE, 524);
            exit(-1);
        }
#endif
        assert(!r0);

        printf("epoll events EPOLLERR=%#x, EPOLLHUP=%#x\n", EPOLLERR, EPOLLHUP);
    }

    st_netfd_t stfd = st_netfd_open_socket(fd);
    assert(stfd);
    printf("Client fd=%d\n", fd);

    if (pong) {
        st_thread_t r0 = st_thread_create(receiver, stfd, 0, 0);
        assert(r0);
    }

    sockaddr_in peer;
    memset(&peer, 0, sizeof(sockaddr_in));

    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = inet_addr(host);

    char buf[1500];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "Hello", 5);

    iovec iov;
    iov.iov_base = buf;
    iov.iov_len = strlen(buf);

    for (int k = 0; k < loop; k++) {
        msghdr msg;
        memset(&msg, 0, sizeof(msghdr));
        msg.msg_name = (sockaddr_in*)&peer;
        msg.msg_namelen = sizeof(sockaddr_in);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        int r0;
        if (nn_copies == 0) {
            if (zerocopy) {
                r0 = st_sendmsg(stfd, &msg, MSG_ZEROCOPY, ST_UTIME_NO_TIMEOUT);
            } else {
                r0 = st_sendmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
            }
        } else {
            mmsghdr* hdrs = new mmsghdr[nn_copies + 1];
            for (int i = 0; i < nn_copies + 1; i++) {
                mmsghdr* p = hdrs + i;
                memcpy(&p->msg_hdr, &msg, sizeof(msghdr));
                p->msg_len = 0;
            }
            if (zerocopy) {
                r0 = st_sendmmsg(stfd, hdrs, nn_copies + 1, MSG_ZEROCOPY, ST_UTIME_NO_TIMEOUT);
            } else {
                r0 = st_sendmmsg(stfd, hdrs, nn_copies + 1, 0, ST_UTIME_NO_TIMEOUT);
            }
        }
        assert(r0 > 0);
        printf("Ping %s:%d %d bytes, copies=%d, r0=%d, %s\n", host, port, iov.iov_len, nn_copies, r0, msg.msg_iov->iov_base);

        if (!zerocopy) {
            continue;
        }

        if (!batch) {
            parse_reception(stfd);
        }
    }

    // @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#notification-batching
    if (batch) {
        st_usleep(100 * 1000);
        parse_reception(stfd);
    }

    st_sleep(-1);
    return 0;
}
