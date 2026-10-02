/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <assert.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ST_UTIME_MILLISECONDS 1000
#define ST_UTEST_TIMEOUT (100 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for ping-pong TCP server coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* tcp_server(void* arg)
{
    int* port = (int*)arg;
    int fd = -1;
    st_netfd_t stfd = NULL;
    StFdCleanup(fd, stfd);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    ST_ASSERT_ERROR(fd == -1, fd, "Create socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;

    int v = 1;
    int r0 = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int));
    ST_ASSERT_ERROR(r0, r0, "Set SO_REUSEADDR");

    r0 = ::bind(fd, (const sockaddr*)&addr, sizeof(addr));
    ST_ASSERT_ERROR(r0, r0, "Bind socket");

    r0 = ::listen(fd, 10);
    ST_ASSERT_ERROR(r0, r0, "Listen socket");

    socklen_t addrlen = sizeof(addr);
    r0 = getsockname(fd, (sockaddr*)&addr, &addrlen);
    ST_ASSERT_ERROR(r0, r0, "Get listen port");
    *port = ntohs(addr.sin_port);

    stfd = st_netfd_open_socket(fd);
    ST_ASSERT_ERROR(!stfd, fd, "Open ST socket");

    st_netfd_t client = NULL;
    StStfdCleanup(client);

    client = st_accept(stfd, NULL, NULL, ST_UTEST_TIMEOUT);
    ST_ASSERT_ERROR(!client, fd, "Accept client");

    return NULL;
}

void* tcp_client(void* arg)
{
    int* port = (int*)arg;
    int fd = -1;
    st_netfd_t stfd = NULL;
    StFdCleanup(fd, stfd);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    ST_ASSERT_ERROR(fd == -1, fd, "Create socket");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(*port);

    stfd = st_netfd_open_socket(fd);
    ST_ASSERT_ERROR(!stfd, fd, "Open ST socket");

    int r0 = st_connect(stfd, (const sockaddr*)&addr, sizeof(addr), ST_UTEST_TIMEOUT);
    ST_ASSERT_ERROR(r0, r0, "Connect to server");

    return NULL;
}

VOID TEST(TcpTest, TcpConnection)
{
    // The server listens on a port the OS picks, and runs first, so the port is known before the client connects.
    int port = 0;
    st_thread_t svr = st_thread_create(tcp_server, &port, 1, 0);
    EXPECT_TRUE(svr != NULL);

    st_thread_t client = st_thread_create(tcp_client, &port, 1, 0);
    EXPECT_TRUE(client != NULL);

    ST_COROUTINE_JOIN(svr, r0);
    ST_COROUTINE_JOIN(client, r1);

    ST_EXPECT_SUCCESS(r0);
    ST_EXPECT_SUCCESS(r1);
}

