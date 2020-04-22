#include <st.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

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

#include <netinet/udp.h>
// Define macro for UDP GSO.
// @see https://github.com/torvalds/linux/blob/master/tools/testing/selftests/net/udpgso.c
#ifndef UDP_SEGMENT
#define UDP_SEGMENT             103
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

void parse_reception(st_netfd_t stfd, int nn_confirm)
{
    int left = nn_confirm;
    while (left > 0) {
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
        left -= range;
        printf("Reception %d bytes, flags %#x, cmsg(level %#x, type %#x), serr(errno %#x, origin %#x, code %#x), range %d [%d, %d]\n",
            msg.msg_controllen, msg.msg_flags, cm->cmsg_level, cm->cmsg_type, serr->ee_errno, serr->ee_origin, serr->ee_code, range, lo, hi);

        // Defered Copies, @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#deferred-copies
        if (serr->ee_code == SO_EE_CODE_ZEROCOPY_COPIED) {
            printf("Warning: Defered copies, should stop zerocopy\n");
        }
    }
}

void usage(int argc, char** argv)
{
    printf("Usage: %s <options>\n", argv[0]);
    printf("Options:\n");
    printf("    --help          Print this help and exit.\n");
    printf("    --host=string   The host to send to.\n");
    printf("    --port=int      The port to send to.\n");
    printf("    --pong=bool     Whether response pong, true|false\n");
    printf("    --zerocopy=bool Whether use zerocopy to sendmsg, true|false\n");
    printf("    --copy=int      The copies of message, 1 means sendmmsg(msg+msg)\n");
    printf("    --loop=int      The number of loop to send out messages\n");
    printf("    --batch=bool    Whether read reception by batch, true|false\n");
    printf("    --mix=bool      Whether mix msg with zerocopy and those without, true|false\n");
    printf("    --size=int      Each message size in bytes.\n");
    printf("    --gso=int       The GSO size in bytes, 0 to disable it.\n");
    printf("    --iovs=int      The number of iovs to send, at least 1.\n");
    printf("    --sndbuf=int    The SO_SNDBUF size in bytes, 0 to ignore.\n");
    printf("For example:\n");
    printf("        %s --host=127.0.0.1 --port=8000 --pong=true --zerocopy=true --copy=0 --loop=1 --batch=true --mix=true --size=1400 --gso=0 --iovs=1 --sndbuf=0\n", argv[0]);
}

int main(int argc, char** argv)
{
    option longopts[] = {
        { "host",       required_argument,      NULL,       'o' },
        { "port",       required_argument,      NULL,       'p' },
        { "pong",       required_argument,      NULL,       'n' },
        { "zerocopy",   required_argument,      NULL,       'z' },
        { "copy",       required_argument,      NULL,       'c' },
        { "loop",       required_argument,      NULL,       'l' },
        { "batch",      required_argument,      NULL,       'b' },
        { "mix",        required_argument,      NULL,       'm' },
        { "size",       required_argument,      NULL,       's' },
        { "gso",        required_argument,      NULL,       'g' },
        { "iovs",       required_argument,      NULL,       'i' },
        { "sndbuf",     required_argument,      NULL,       'u' },
        { "help",       no_argument,            NULL,       'h' },
        { NULL,         0,                      NULL,       0 }
    };

    char* host = NULL; char ch;
    int port = 0; int nn_copies = 0; int loop = 1; int size = 1500; int gso = 0; int nn_iovs = 0; int sndbuf = 0;
    bool pong = false; bool zerocopy = false; bool batch = false; bool mix = false;
    while ((ch = getopt_long(argc, argv, "o:p:n:z:c:l:b:m:s:g:u:h", longopts, NULL)) != -1) {
        switch (ch) {
            case 'o': host = (char*)optarg; break;
            case 'p': port = atoi(optarg); break;
            case 'n': pong = !strcmp(optarg,"true"); break;
            case 'z': zerocopy = !strcmp(optarg,"true"); break;
            case 'c': nn_copies = atoi(optarg); break;
            case 'l': loop = atoi(optarg); break;
            case 'b': batch = !strcmp(optarg,"true"); break;
            case 'm': mix = !strcmp(optarg,"true"); break;
            case 's': size = atoi(optarg); break;
            case 'g': gso = atoi(optarg); break;
            case 'i': nn_iovs = atoi(optarg); break;
            case 'u': sndbuf = atoi(optarg); break;
            case 'h': usage(argc, argv); exit(0);
            default: usage(argc, argv); exit(-1);
        }
    }

    printf("Server listen %s:%d, pong %d, zerocopy %d, copies %d, loop %d, batch %d, mix %d, size %d, gso %d, iovs %d, sndbuf %d\n",
        host, port, pong, zerocopy, nn_copies, loop, batch, mix, size, gso, nn_iovs, sndbuf);
    if (!host || !port || !nn_iovs) {
        usage(argc, argv);
        exit(-1);
    }

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

    if (true) {
        int dv = 0;
        socklen_t len = sizeof(dv);
        int r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &dv, &len);

        int r1 = 0;
        if (sndbuf > 0) {
            r1 = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        }

        int nv = 0;
        int r2 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &nv, &len);
        printf("socket SO_SNDBUF default=%d, user=%d, now=%d, r0=%d, r1=%d, r2=%d\n", dv, sndbuf, nv, r0, r1, r2);
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

    char* buf = new char[size];
    memset(buf, 0, size);
    memcpy(buf, "Hello", size < 5? size : 5);

    iovec iov;
    iov.iov_base = buf;
    iov.iov_len = size;

    int nn_confirm = 0;
    for (int k = 0; k < loop; k++) {
        msghdr msg;
        memset(&msg, 0, sizeof(msghdr));
        msg.msg_name = (sockaddr_in*)&peer;
        msg.msg_namelen = sizeof(sockaddr_in);
        msg.msg_iov = new iovec[nn_iovs];
        msg.msg_iovlen = nn_iovs;

        for (int i = 0; i < nn_iovs; i++) {
            iovec* p = msg.msg_iov + i;
            memcpy(p, &iov, sizeof(iovec));
        }

        if (gso > 0) {
            msg.msg_controllen = CMSG_SPACE(sizeof(uint16_t));
            if (!msg.msg_control) {
                msg.msg_control = new char[msg.msg_controllen];
            }

            cmsghdr* cm = CMSG_FIRSTHDR(&msg);
            cm->cmsg_level = SOL_UDP;
            cm->cmsg_type = UDP_SEGMENT;
            cm->cmsg_len = CMSG_LEN(sizeof(uint16_t));
            *((uint16_t*)CMSG_DATA(cm)) = gso;
        }

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
        if (r0 > 0) {
            printf("Ping %s:%d %d bytes, control %d, copies=%d, r0=%d, %s\n", host, port, iov.iov_len * nn_iovs,
                msg.msg_controllen, nn_copies, r0, msg.msg_iov->iov_base);
        } else {
            printf("Ping %d bytes, error r0=%d, errno=%d\n", iov.iov_len * nn_iovs, r0, errno); exit(1);
        }

        if (zerocopy && !batch) {
            parse_reception(stfd, r0);
        } else {
            nn_confirm += r0;
        }

        if (mix) {
            r0 = st_sendmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
            assert(r0 > 0);
            printf("Mix %s:%d %d bytes, r0=%d, %s\n", host, port, iov.iov_len * nn_iovs, r0, msg.msg_iov->iov_base);
        }
    }

    // @see https://www.kernel.org/doc/html/latest/networking/msg_zerocopy.html#notification-batching
    if (batch) {
        parse_reception(stfd, nn_confirm);
    }

    st_sleep(-1);
    return 0;
}
