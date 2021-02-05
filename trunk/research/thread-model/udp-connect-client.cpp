/*
g++ udp-connect-client.cpp ../../objs/st/libst.a -g -O0 -o udp-connect-client &&
./udp-connect-client 127.0.0.1 8000 1
*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../../objs/st/st.h"

#include <sys/socket.h>
#include <arpa/inet.h>

void* pfn(void* arg) {
    sockaddr_in addr = *(sockaddr_in*)arg;
    char* ip = inet_ntoa(addr.sin_addr);
    int port = ntohs(addr.sin_port);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    st_netfd_t stfd = st_netfd_open_socket(fd);
    printf("connect to %s:%d, fd=%d ok\n", ip, port, fd);

    while (true) {
        char data[] = "Hello world!";
        int r0 = st_sendto(stfd, data, sizeof(data), (sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT);
        printf("fd #%d, send %dB %s, r0=%d\n", fd, (int)sizeof(data), data, r0);
        if (r0 != (int)sizeof(data)) {
            break;
        }
        st_usleep(800 * 1000);
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
    printf("Start %d workers, to %s:%d\n", workers, ip, port);

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

