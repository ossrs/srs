/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

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

    // A timeout counts from ST's last clock reading, which only changes when coroutines switch. An earlier test may
    // block outside ST for a long time, such as in fork and waitpid, so refresh the clock, or the 100 ms timeouts below
    // are already due.
    st_thread_yield();

    st_thread_t svr = st_thread_create(tcp_server, &port, 1, 0);
    EXPECT_TRUE(svr != NULL);

    st_thread_t client = st_thread_create(tcp_client, &port, 1, 0);
    EXPECT_TRUE(client != NULL);

    ST_COROUTINE_JOIN(svr, r0);
    ST_COROUTINE_JOIN(client, r1);

    ST_EXPECT_SUCCESS(r0);
    ST_EXPECT_SUCCESS(r1);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for an echo server with many concurrent connections, for correctness, not speed.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define TCP_ECHO_CLIENTS 1000
#define TCP_ECHO_ROUNDS 3
#define TCP_ECHO_SIZE 100
// Generous: connecting 1000 clients at once may overflow the listen backlog, and the OS then retries the SYN after
// about a second (Linux) or two (Windows). A correct run takes far less.
#define TCP_ECHO_TIMEOUT (30 * 1000 * ST_UTIME_MILLISECONDS)
// Small stacks, since there is one coroutine per client and one per accepted connection.
#define TCP_ECHO_STACK (64 * 1024)

struct TcpEchoTest {
    int nn_clients_;
    st_netfd_t listener_;
    sockaddr_in addr_;

    // The barrier: the server sends each client a go byte only when every client is connected and waits for it, and
    // every echo coroutine waits for the client's data, so all the sockets are polled at the same time.
    int connected_;
    int accepted_;
    int nn_ready_;
    st_cond_t all_connected_;

    // The results of the echo coroutines.
    int nn_echoed_;
    int64_t bytes_echoed_;
    int echo_errors_;

    TcpEchoTest() : nn_clients_(0), listener_(NULL), connected_(0), accepted_(0), nn_ready_(0), all_connected_(NULL),
        nn_echoed_(0), bytes_echoed_(0), echo_errors_(0) {
        memset(&addr_, 0, sizeof(addr_));
    }
};

struct TcpEchoConn {
    TcpEchoTest* t_;
    st_netfd_t stfd_;
    int id_;
};

// The message of one client in one round: its id, the round, and a pattern that depends on both.
static void tcp_echo_message(char* buf, int id, int round)
{
    snprintf(buf, TCP_ECHO_SIZE, "client=%d, round=%d, ", id, round);
    for (int i = (int)strlen(buf); i < TCP_ECHO_SIZE; i++) {
        buf[i] = (char)('a' + (id * 7 + round * 3 + i) % 26);
    }
}

// Echoes whatever arrives on one accepted connection until the client closes it.
static void* tcp_echo_session(void* arg)
{
    TcpEchoConn* c = (TcpEchoConn*)arg;
    TcpEchoTest* t = c->t_;

    char buf[TCP_ECHO_SIZE * 2];
    int64_t bytes = 0;
    bool ok = true;
    for (;;) {
        ssize_t nn = st_read(c->stfd_, buf, sizeof(buf), TCP_ECHO_TIMEOUT);
        if (nn == 0) break;
        if (nn < 0 || st_write(c->stfd_, buf, nn, TCP_ECHO_TIMEOUT) != nn) {
            ok = false;
            break;
        }
        bytes += nn;
    }

    if (ok) t->nn_echoed_++;
    else t->echo_errors_++;
    t->bytes_echoed_ += bytes;

    st_netfd_close(c->stfd_);
    delete c;
    return NULL;
}

// Accepts every client and starts an echo coroutine for each, then sends every client the go byte once all are
// connected, and waits for the echo coroutines.
static void* tcp_echo_server(void* arg)
{
    TcpEchoTest* t = (TcpEchoTest*)arg;
    std::vector<st_thread_t> sessions;
    std::vector<st_netfd_t> conns;

    for (int i = 0; i < t->nn_clients_; i++) {
        st_netfd_t client = st_accept(t->listener_, NULL, NULL, TCP_ECHO_TIMEOUT);
        if (!client) break;

        TcpEchoConn* c = new TcpEchoConn();
        c->t_ = t;
        c->stfd_ = client;
        c->id_ = i;
        st_thread_t trd = st_thread_create(tcp_echo_session, c, 1, TCP_ECHO_STACK);
        if (!trd) {
            st_netfd_close(client);
            delete c;
            break;
        }
        sessions.push_back(trd);
        conns.push_back(client);
        t->accepted_++;
    }
    int accepted = t->accepted_;

    // Each echo coroutine closes its connection only after its client got the go byte and closed, so a connection
    // here is still open when its go byte is sent. Without the go byte, the clients time out, and so do the echo
    // coroutines.
    int r0 = 0;
    while (accepted == t->nn_clients_ && t->connected_ < t->nn_clients_ && r0 == 0) {
        r0 = st_cond_timedwait(t->all_connected_, TCP_ECHO_TIMEOUT);
    }
    int nn_go = 0;
    for (size_t i = 0; r0 == 0 && accepted == t->nn_clients_ && i < conns.size(); i++) {
        if (st_write(conns[i], "G", 1, TCP_ECHO_TIMEOUT) == 1) nn_go++;
    }

    for (size_t i = 0; i < sessions.size(); i++) {
        st_thread_join(sessions[i], NULL);
    }

    ST_ASSERT_ERROR(accepted != t->nn_clients_, accepted, "Accept every client");
    ST_ASSERT_ERROR(r0, r0, "Wait for every client to connect");
    ST_ASSERT_ERROR(nn_go != t->nn_clients_, nn_go, "Send every go byte");
    return NULL;
}

// Connects, waits in a read for the go byte, then sends its own messages and checks each echo.
static void* tcp_echo_client(void* arg)
{
    TcpEchoConn* c = (TcpEchoConn*)arg;
    TcpEchoTest* t = c->t_;
    int id = c->id_;
    delete c;

    int fd = -1;
    st_netfd_t stfd = NULL;
    StFdCleanup(fd, stfd);

    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    ST_ASSERT_ERROR(fd < 0, fd, "Create socket");

    stfd = st_netfd_open_socket(fd);
    ST_ASSERT_ERROR(!stfd, fd, "Open ST socket");

    int r0 = st_connect(stfd, (const sockaddr*)&t->addr_, sizeof(t->addr_), TCP_ECHO_TIMEOUT);
    ST_ASSERT_ERROR(r0, r0, "Connect to server");

    t->connected_++;
    if (t->connected_ == t->nn_clients_) st_cond_signal(t->all_connected_);

    char go = 0;
    ssize_t nn = st_read_fully(stfd, &go, 1, TCP_ECHO_TIMEOUT);
    ST_ASSERT_ERROR(nn != 1 || go != 'G', (int)nn, "Read the go byte");
    t->nn_ready_++;

    char msg[TCP_ECHO_SIZE], echo[TCP_ECHO_SIZE];
    for (int round = 0; round < TCP_ECHO_ROUNDS; round++) {
        tcp_echo_message(msg, id, round);

        // Send the message in two parts, so the server may read it split, and check the echo as a whole.
        int half = TCP_ECHO_SIZE / 2;
        ST_ASSERT_ERROR(st_write(stfd, msg, half, TCP_ECHO_TIMEOUT) != half, round, "Write first half");
        st_usleep(0);
        ST_ASSERT_ERROR(st_write(stfd, msg + half, TCP_ECHO_SIZE - half, TCP_ECHO_TIMEOUT) != TCP_ECHO_SIZE - half,
            round, "Write second half");

        nn = st_read_fully(stfd, echo, TCP_ECHO_SIZE, TCP_ECHO_TIMEOUT);
        ST_ASSERT_ERROR(nn != TCP_ECHO_SIZE, (int)nn, "Read the echo");
        ST_ASSERT_ERROR(memcmp(msg, echo, TCP_ECHO_SIZE) != 0, round, "Echo matches the message");
    }

    return NULL;
}

// How many clients the descriptor limit allows, with room for the other descriptors of the process: each client
// takes two descriptors, its own and the server's end. On POSIX, raise the soft limit toward the hard limit first.
static int tcp_echo_max_clients()
{
#ifndef _WIN32
    rlim_t want = 2 * TCP_ECHO_CLIENTS + 256;
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return 0;

    if (rlim.rlim_cur != RLIM_INFINITY && rlim.rlim_cur < want) {
        rlim.rlim_cur = (rlim.rlim_max == RLIM_INFINITY || rlim.rlim_max > want) ? want : rlim.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rlim);
        if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return 0;
    }
    if (rlim.rlim_cur != RLIM_INFINITY && rlim.rlim_cur < want) {
        return rlim.rlim_cur > 256 ? (int)((rlim.rlim_cur - 256) / 2) : 0;
    }
#endif
    return TCP_ECHO_CLIENTS;
}

VOID TEST(TcpTest, EchoManyConcurrentConnections)
{
    TcpEchoTest t;
    t.nn_clients_ = tcp_echo_max_clients();
    // Only a system with a very low hard descriptor limit runs fewer clients; Windows, Linux, and macOS run them all.
    ASSERT_GT(t.nn_clients_, 0);
    if (t.nn_clients_ < TCP_ECHO_CLIENTS) {
        printf("The descriptor limit allows only %d of %d echo clients\n", t.nn_clients_, TCP_ECHO_CLIENTS);
    }

    // The listener, on a loopback port the OS picks, with the largest backlog.
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    t.addr_.sin_family = AF_INET;
    t.addr_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    t.addr_.sin_port = 0;
    socklen_t addrlen = sizeof(t.addr_);
    if (::bind(fd, (const sockaddr*)&t.addr_, addrlen) != 0 || ::listen(fd, SOMAXCONN) != 0 ||
        getsockname(fd, (sockaddr*)&t.addr_, &addrlen) != 0) {
        st_utest_close(fd);
        FAIL() << "Listen on loopback, errno=" << errno;
    }
    t.listener_ = st_netfd_open_socket(fd);
    if (!t.listener_) {
        st_utest_close(fd);
        FAIL() << "Open ST listener, errno=" << errno;
    }
    t.all_connected_ = st_cond_new();
    ASSERT_TRUE(t.all_connected_ != NULL);

    // Refresh ST's clock, which an earlier test may leave stale by blocking outside ST.
    st_thread_yield();

    st_thread_t svr = st_thread_create(tcp_echo_server, &t, 1, TCP_ECHO_STACK);
    ASSERT_TRUE(svr != NULL);

    std::vector<st_thread_t> clients;
    for (int i = 0; i < t.nn_clients_; i++) {
        TcpEchoConn* c = new TcpEchoConn();
        c->t_ = &t;
        c->stfd_ = NULL;
        c->id_ = i;
        st_thread_t trd = st_thread_create(tcp_echo_client, c, 1, TCP_ECHO_STACK);
        if (!trd) {
            delete c;
            break;
        }
        clients.push_back(trd);
    }
    EXPECT_EQ(t.nn_clients_, (int)clients.size());

    int nn_failed = 0;
    for (size_t i = 0; i < clients.size(); i++) {
        ST_COROUTINE_JOIN(clients[i], r0);
        // Print only the first few failures.
        if (r0 && nn_failed++ < 5) ADD_FAILURE() << "client " << i << ": " << r0;
    }
    EXPECT_EQ(0, nn_failed);

    // The server waits for its echo coroutines, which end when their clients close.
    ST_COROUTINE_JOIN(svr, r1);
    ST_EXPECT_SUCCESS(r1);

    // Every connection was open at the same time, and every byte came back.
    EXPECT_EQ(t.nn_clients_, t.connected_);
    EXPECT_EQ(t.nn_clients_, t.accepted_);
    EXPECT_EQ(t.nn_clients_, t.nn_ready_);
    EXPECT_EQ(t.nn_clients_, t.nn_echoed_);
    EXPECT_EQ(0, t.echo_errors_);
    EXPECT_EQ((int64_t)t.nn_clients_ * TCP_ECHO_ROUNDS * TCP_ECHO_SIZE, t.bytes_echoed_);

    st_cond_destroy(t.all_connected_);
    st_netfd_close(t.listener_);
}

