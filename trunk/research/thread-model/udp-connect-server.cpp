/*
g++ udp-connect-server.cpp ../../objs/st/libst.a -g -O0 -o udp-connect-server &&
./udp-connect-server 127.0.0.1 8000 2
*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../../objs/st/st.h"

#include <sys/socket.h>
#include <arpa/inet.h>

#include <vector>
using namespace std;

vector<int> lfds;

void* pfn(void* arg) {
    sockaddr_in addr = *(sockaddr_in*)arg;
    char* ip = inet_ntoa(addr.sin_addr);
    int port = ntohs(addr.sin_port);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd > 0);

    lfds.push_back(fd);

    // Find another thread to recv packet.
    int migrate_fd = 0;
    while (!migrate_fd) {
        for (int i = 0; i < (int)lfds.size(); i++) {
            if (lfds.at(i) != fd) {
                migrate_fd = lfds.at(i);
                break;
            }
        }
        st_usleep(10 * 1000);
    }

    if (true) {
        int v = 1;
        int r0 = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int));
        assert(r0 != -1);
    }
    if (true) {
        int v = 1;
        int r0 = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(int));
        assert(r0 != -1);
    }
    if (true) {
        int r0 = bind(fd, (sockaddr*)&addr, sizeof(sockaddr_in));
        assert(!r0);
    }

    st_netfd_t stfd = st_netfd_open_socket(fd);
    printf("listen at %s:%d, fd=%d, migrate_fd=%d ok\n", ip, port, fd, migrate_fd);

    while (true) {
        char buf[1600] = {0};
        sockaddr_in from;
        int nb_from = sizeof(from);
        int r0 = st_recvfrom(stfd, buf, sizeof(buf), (sockaddr*)&from, &nb_from, ST_UTIME_NO_TIMEOUT);
        printf("fd #%d, peer %s:%d, got %dB, %s\n", fd, inet_ntoa(from.sin_addr), ntohs(from.sin_port), r0, buf);
        if (r0 <= 0) {
            break;
        }

        // When we got the packet, connect to another thread.
        // If it is of type SOCK_DGRAM, this call specifies the peer with which the socket
        // is to be associated; this address is that to which datagrams are to be sent, and
        // the only address from which datagrams are to be received.
        errno = 0;
        r0 = connect(migrate_fd, (const sockaddr*)&from, (socklen_t)nb_from);
        printf("Transfer %s:%d from #%d to #%d, r0=%d, errno=%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port),
            fd, migrate_fd, r0, errno);

        st_usleep(10 * 1000);
    }

    st_netfd_close(stfd);
    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("Usage: %s ip port workers\n", argv[0]);
        exit(-1);
    }

    st_init();

    const char* ip = argv[1];
    int port = ::atoi(argv[2]);
    int workers = ::atoi(argv[3]);
    printf("Start %d workers, at %s:%d\n", workers, ip, port);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);

    for (int i = 0; i < workers; i++) {
        st_thread_t thread = st_thread_create(pfn, &addr, 1, 0);
        assert(thread);
    }

    st_thread_exit(NULL);
    return 0;
}

