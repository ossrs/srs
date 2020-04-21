#include <st.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

struct message {
    st_netfd_t stfd;
    sockaddr_in peer;
    int delay;
};

void* sender(void* arg)
{
    message* p = (message*)arg;

    int delay = p->delay;
    if (delay > 0) {
        st_usleep(delay * 1000);
    }

    msghdr msg;
    memset(&msg, 0, sizeof(msghdr));

    sockaddr_in peer = p->peer;
    msg.msg_name = (sockaddr_in*)&peer;
    msg.msg_namelen = sizeof(sockaddr_in);

    char buf[] = "World";

    iovec iov;
    memset(&iov, 0, sizeof(iovec));
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    st_netfd_t stfd = p->stfd;
    int r0 = st_sendmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
    assert(r0 > 0);
    printf("Pong %s:%d %d bytes, flags %#x, %s\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), r0,
        msg.msg_flags, msg.msg_iov->iov_base);

    return NULL;
}

void usage(int argc, char** argv)
{
    printf("Usage: %s <options>\n", argv[0]);
    printf("Options:\n");
    printf("    --help          Print this help and exit.\n");
    printf("    --host=string   The host to send to.\n");
    printf("    --port=int      The port to send to.\n");
    printf("    --pong=bool     Whether response pong, true|false\n");
    printf("    --delay=int     The delay in ms to response pong.\n");
    printf("For example:\n");
    printf("        %s --host=0.0.0.0 --port=8000 --pong --delay=100\n", argv[0]);
}

int main(int argc, char** argv)
{
    option longopts[] = {
        { "host",       required_argument,      NULL,       'o' },
        { "port",       required_argument,      NULL,       'p' },
        { "pong",       required_argument,      NULL,       'n' },
        { "delay",      required_argument,      NULL,       'd' },
        { "help",       no_argument,            NULL,       'h' },
        { NULL,         0,                      NULL,       0 }
    };

    char* host = NULL; char ch;
    int port = 0; int delay = 0; bool pong = false;
    while ((ch = getopt_long(argc, argv, "o:p:n:d:h", longopts, NULL)) != -1) {
        switch (ch) {
            case 'o': host = (char*)optarg; break;
            case 'p': port = atoi(optarg); break;
            case 'n': pong = !strcmp(optarg,"true"); break;
            case 'd': delay = atoi(optarg); break;
            case 'h': usage(argc, argv); exit(0);
            default: usage(argc, argv); exit(-1);
        }
    }

    printf("Server listen %s:%d, pong %d, delay: %dms\n", host, port, pong, delay);
    if (!host || !port) {
        usage(argc, argv);
        exit(-1);
    }

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

    printf("Listen at udp://%s:%d, fd=%d\n", host, port, fd);

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

    int nn_msgs = 0;
    while (true) {
        r0 = st_recvmsg(stfd, &msg, 0, ST_UTIME_NO_TIMEOUT);
        assert(r0 > 0);
        printf("#%d, From %s:%d %d bytes, flags %#x, %s\n", nn_msgs++, inet_ntoa(peer.sin_addr), ntohs(peer.sin_port),
            r0, msg.msg_flags, msg.msg_iov->iov_base);

        if (pong) {
            message* msg = new message();
            msg->stfd = stfd;
            msg->peer = peer;
            msg->delay = delay;
            st_thread_t r0 = st_thread_create(sender, msg, 0, 0);
            assert(r0);
        }
    }

    return 0;
}
