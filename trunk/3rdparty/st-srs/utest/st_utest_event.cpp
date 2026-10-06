/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <vector>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define ST_UTIME_MILLISECONDS 1000
#define ST_UTEST_TIMEOUT (100 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for descriptors numbered above the event system's initial table, such as a server that already holds more
// than 4096 connections. The event system keeps a per-descriptor table of waiter counts, sized at st_init: epoll for
// up to 4096 descriptors, kqueue for 1024. A higher descriptor grows the table, while other coroutines may be waiting
// on descriptors already in it.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef _WIN32 // POSIX only: pipes at chosen descriptor numbers
// A pipe whose read end is moved to a chosen descriptor number, and whose write end stays plain.
struct EventTestHighPipe {
    int reader_;
    int writer_;
    EventTestHighPipe() : reader_(-1), writer_(-1) {
    }
    ~EventTestHighPipe() {
        if (reader_ >= 0) ::close(reader_);
        if (writer_ >= 0) ::close(writer_);
    }
};

// Whether the process may open a descriptor numbered fd, which must be free.
static bool event_test_fd_allowed(int fd)
{
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return false;
    return rlim.rlim_cur == RLIM_INFINITY || (rlim_t)fd < rlim.rlim_cur;
}

static bool event_test_high_pipe(EventTestHighPipe& p, int fd)
{
    // The descriptor must be free, or dup2 would silently close whatever uses it.
    if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) return false;

    int fds[2];
    if (pipe(fds) < 0) return false;
    p.writer_ = fds[1];

    if (dup2(fds[0], fd) != fd) {
        ::close(fds[0]);
        return false;
    }
    ::close(fds[0]);
    p.reader_ = fd;
    return true;
}
#endif

struct EventTestReader {
    st_netfd_t stfd_;
    int r0_;
    int errno_;
    char data_;
    bool done_;
    EventTestReader() : stfd_(NULL), r0_(0), errno_(0), data_(0), done_(false) {
    }
};

static void* event_test_reader_coroutine(void* arg)
{
    EventTestReader* r = (EventTestReader*)arg;
    errno = 0;
    r->r0_ = (int)st_read(r->stfd_, &r->data_, 1, ST_UTEST_TIMEOUT);
    r->errno_ = errno;
    r->done_ = true;
    return NULL;
}

#ifndef _WIN32 // POSIX only: pipes at chosen descriptor numbers
// The server already holds more descriptors than the initial table, so the next connection gets descriptor 5000.
// st_netfd_open grows the table for it, a coroutine reading it waits and wakes on its data, and the descriptor then
// closes, since no waiter is left counted on it. Locks in current behavior.
VOID TEST(HighFdTest, ConnectionAboveInitialTable)
{
    const int fd = 5000;
    if (!event_test_fd_allowed(fd)) GTEST_SKIP() << "RLIMIT_NOFILE too low for fd " << fd;

    EventTestHighPipe p;
    ASSERT_TRUE(event_test_high_pipe(p, fd));

    EventTestReader r;
    ASSERT_TRUE((r.stfd_ = st_netfd_open(p.reader_)) != NULL);
    st_thread_t trd = st_thread_create(event_test_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The reader waits for data.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);

    // Data arrives; the reader wakes and gets it.
    ASSERT_EQ(1, ::write(p.writer_, "a", 1));
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(1, r.r0_);
    EXPECT_EQ('a', r.data_);

    EXPECT_EQ(0, st_netfd_close(r.stfd_));
    p.reader_ = -1;
}

// A connection on a low descriptor waits to read, then the server waits on descriptor 9000 with st_poll, above the
// table even after it grew for descriptor 5000, so the table grows while the reader waits. The high descriptor is
// reported ready, the reader is still waiting and wakes on its own data, and its descriptor then closes: the growth
// kept the reader's count, so it is dropped back to 0, not below. Locks in current behavior.
VOID TEST(HighFdTest, WaiterKeepsWaitingWhenTableGrows)
{
    const int fd = 9000;
    if (!event_test_fd_allowed(fd)) GTEST_SKIP() << "RLIMIT_NOFILE too low for fd " << fd;

    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    EventTestReader r;
    ASSERT_TRUE((r.stfd_ = st_netfd_open(fds[0])) != NULL);
    st_thread_t trd = st_thread_create(event_test_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The reader waits on the low descriptor.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);

    // Wait on the high descriptor, which already has data, so st_poll returns at once.
    EventTestHighPipe p;
    ASSERT_TRUE(event_test_high_pipe(p, fd));
    ASSERT_EQ(1, ::write(p.writer_, "b", 1));

    struct pollfd pd;
    pd.fd = fd;
    pd.events = POLLIN;
    EXPECT_EQ(1, st_poll(&pd, 1, ST_UTEST_TIMEOUT));
    EXPECT_EQ(POLLIN, pd.revents);
    EXPECT_FALSE(r.done_);

    // Data arrives for the reader; it wakes and gets it.
    ASSERT_EQ(1, ::write(fds[1], "a", 1));
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(1, r.r0_);
    EXPECT_EQ('a', r.data_);

    EXPECT_EQ(0, st_netfd_close(r.stfd_));
    ::close(fds[1]);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for a pollset the event system refuses part of, such as a tool that waits with st_poll on its server
// connection and on stdin, run with stdin redirected from a file (tool < commands.txt). epoll refuses a regular file
// with EPERM, after it already registered the descriptors before it in the set, so ST undoes those again: st_poll fails
// at once and leaves every descriptor as it was. kqueue accepts a regular file and reports it ready.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef _WIN32 // POSIX only: regular files and pipes
// A regular file holding one command, read from the start, like stdin redirected from a file.
static int event_test_command_file()
{
    char path[] = "/tmp/st-utest-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    ::unlink(path);

    if (::write(fd, "quit\n", 5) != 5 || lseek(fd, 0, SEEK_SET) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// The tool waits on its quiet server connection and on stdin, a file. On Linux st_poll fails at once with EPERM,
// instead of waiting for the connection, and the connection is left with no waiter counted, so it closes without
// EBUSY. On macOS the file is ready to read and the connection is not. Locks in current behavior.
VOID TEST(PollRefusedTest, StdinFromFileFailsAtOnce)
{
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    int file = event_test_command_file();
    ASSERT_GE(file, 0);

    struct pollfd pds[2];
    pds[0].fd = fds[0];
    pds[0].events = POLLIN;
    pds[0].revents = 0;
    pds[1].fd = file;
    pds[1].events = POLLIN;
    pds[1].revents = 0;

    st_utime_t starttime = st_utime();
    errno = 0;
    int r0 = st_poll(pds, 2, ST_UTEST_TIMEOUT);
#if defined(__linux__)
    int err = errno;
#endif
    EXPECT_LT(st_utime() - starttime, ST_UTEST_TIMEOUT);
#if defined(__linux__)
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EPERM, err);
#else
    EXPECT_EQ(1, r0);
    EXPECT_EQ(0, pds[0].revents);
    EXPECT_EQ(POLLIN, pds[1].revents);
#endif

    st_netfd_t conn = st_netfd_open(fds[0]);
    ASSERT_TRUE(conn != NULL);
    EXPECT_EQ(0, st_netfd_close(conn));
    ::close(fds[1]);
    ::close(file);
}

// After the refused st_poll, the tool closes the file, and the next connection gets the same descriptor number. A
// coroutine reading it waits and wakes on its data, so the refused file left no waiter counted on that number either.
// Locks in current behavior.
VOID TEST(PollRefusedTest, FileNumberReusedByNextConnection)
{
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    int file = event_test_command_file();
    ASSERT_GE(file, 0);

    struct pollfd pds[2];
    pds[0].fd = fds[0];
    pds[0].events = POLLIN;
    pds[1].fd = file;
    pds[1].events = POLLIN;
#if defined(__linux__)
    EXPECT_EQ(-1, st_poll(pds, 2, ST_UTEST_TIMEOUT));
#else
    EXPECT_EQ(1, st_poll(pds, 2, ST_UTEST_TIMEOUT));
#endif
    ::close(fds[0]);
    ::close(fds[1]);

    // The next connection reuses the file's descriptor number.
    int conn[2];
    ASSERT_EQ(0, pipe(conn));
    ASSERT_EQ(file, dup2(conn[0], file));
    ::close(conn[0]);

    EventTestReader r;
    ASSERT_TRUE((r.stfd_ = st_netfd_open(file)) != NULL);
    st_thread_t trd = st_thread_create(event_test_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The reader waits for data.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);

    // Data arrives; the reader wakes and gets it.
    ASSERT_EQ(1, ::write(conn[1], "a", 1));
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(1, r.r0_);
    EXPECT_EQ('a', r.data_);

    EXPECT_EQ(0, st_netfd_close(r.stfd_));
    ::close(conn[1]);
}

// A coroutine waits to read the server connection, while the tool waits with st_poll to write it and to read stdin, a
// file. On Linux the connection is already registered for reading, so ST adds writing to it, then undoes that when
// the file is refused. The reader keeps waiting, wakes on its data, and the connection then closes: its counts are
// back to 0. On macOS st_poll reports both ready, the connection writable and the file readable. Locks in current
// behavior.
VOID TEST(PollRefusedTest, ReaderOnSameConnectionKeepsWaiting)
{
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    int file = event_test_command_file();
    ASSERT_GE(file, 0);

    EventTestReader r;
    ASSERT_TRUE((r.stfd_ = st_netfd_open_socket(fds[0])) != NULL);
    st_thread_t trd = st_thread_create(event_test_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The reader waits on the connection.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);

    struct pollfd pds[2];
    pds[0].fd = fds[0];
    pds[0].events = POLLOUT;
    pds[0].revents = 0;
    pds[1].fd = file;
    pds[1].events = POLLIN;
    pds[1].revents = 0;
    errno = 0;
    int r0 = st_poll(pds, 2, ST_UTEST_TIMEOUT);
#if defined(__linux__)
    int err = errno;
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EPERM, err);
#else
    EXPECT_EQ(2, r0);
    EXPECT_EQ(POLLOUT, pds[0].revents);
    EXPECT_EQ(POLLIN, pds[1].revents);
#endif

    // The reader is still waiting, and wakes on its data.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);
    ASSERT_EQ(1, ::write(fds[1], "a", 1));
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(1, r.r0_);
    EXPECT_EQ('a', r.data_);

    EXPECT_EQ(0, st_netfd_close(r.stfd_));
    ::close(fds[1]);
    ::close(file);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for waiting for TCP urgent data with POLLPRI, such as an FTP server whose client aborts a transfer, or a
// telnet server whose user presses interrupt: the client sends one urgent byte with send(MSG_OOB) on the connection
// that also carries its normal commands. epoll counts POLLPRI waiters per descriptor next to the reading and writing
// waiters, so a coroutine waiting for urgent data ignores normal data, and one reading normal data ignores urgent
// data. kqueue has no urgent filter, so it rejects POLLPRI at once with EINVAL.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A connected TCP pair on loopback, as plain descriptors: the server side ST waits on, and the client side the test
// sends from.
struct EventTestTcpPair {
    int server_;
    int client_;
    EventTestTcpPair() : server_(-1), client_(-1) {
    }
    ~EventTestTcpPair() {
        if (server_ >= 0) st_utest_close(server_);
        if (client_ >= 0) st_utest_close(client_);
    }
};

// Accept the connection from client cfd, closing any other one queued on the listener, such as a stray connection from
// another program to a port it used before.
static int event_test_accept_from(int lfd, int cfd)
{
    struct sockaddr_in local, peer;
    socklen_t locallen = sizeof(local);
    if (getsockname(cfd, (sockaddr*)&local, &locallen) < 0) return -1;

    for (;;) {
        socklen_t peerlen = sizeof(peer);
        int sfd = ::accept(lfd, (sockaddr*)&peer, &peerlen);
        if (sfd < 0 || (peer.sin_port == local.sin_port && peer.sin_addr.s_addr == local.sin_addr.s_addr)) {
            return sfd;
        }
        st_utest_close(sfd);
    }
}

static bool event_test_tcp_pair(EventTestTcpPair& pair)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    socklen_t addrlen = sizeof(addr);
    if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, 1) < 0
        || getsockname(lfd, (sockaddr*)&addr, &addrlen) < 0) {
        st_utest_close(lfd);
        return false;
    }

    pair.client_ = socket(AF_INET, SOCK_STREAM, 0);
    if (pair.client_ < 0 || ::connect(pair.client_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        st_utest_close(lfd);
        return false;
    }

    pair.server_ = event_test_accept_from(lfd, pair.client_);
    st_utest_close(lfd);
    return pair.server_ >= 0;
}

struct EventTestAbortWaiter {
    int fd_;
    int r0_;
    int errno_;
    short revents_;
    bool done_;
    EventTestAbortWaiter() : fd_(-1), r0_(0), errno_(0), revents_(0), done_(false) {
    }
};

static void* event_test_abort_waiter_coroutine(void* arg)
{
    EventTestAbortWaiter* w = (EventTestAbortWaiter*)arg;
    struct pollfd pd;
    pd.fd = w->fd_;
    pd.events = POLLPRI;
    pd.revents = 0;
    errno = 0;
    w->r0_ = st_poll(&pd, 1, ST_UTEST_TIMEOUT);
    w->errno_ = errno;
    w->revents_ = pd.revents;
    w->done_ = true;
    return NULL;
}

// The server waits for a command or an abort in one st_poll, like the select loop of a telnet server with the
// connection in both the read and the exception set. A command wakes it with only POLLIN, and it reads the command.
// Then the client sends an urgent byte, which wakes it with only POLLPRI, and recv with MSG_OOB gets the byte. The
// connection then closes without EBUSY. On macOS st_poll fails at once with EINVAL. Locks in current behavior.
VOID TEST(PollPriTest, CommandOrAbortInOneWait)
{
    EventTestTcpPair pair;
    ASSERT_TRUE(event_test_tcp_pair(pair));
    st_netfd_t conn = st_netfd_open_socket(pair.server_);
    ASSERT_TRUE(conn != NULL);

    struct pollfd pd;
    pd.fd = pair.server_;
    pd.events = POLLIN | POLLPRI;
    pd.revents = 0;

#if defined(__linux__)
    // A command arrives; the server wakes with only POLLIN and reads it.
    ASSERT_EQ(1, ::send(pair.client_, "a", 1, 0));
    EXPECT_EQ(1, st_poll(&pd, 1, ST_UTEST_TIMEOUT));
    EXPECT_EQ(POLLIN, pd.revents);
    char data = 0;
    EXPECT_EQ(1, st_read(conn, &data, 1, ST_UTEST_TIMEOUT));
    EXPECT_EQ('a', data);

    // The client aborts; the server wakes with only POLLPRI and reads the urgent byte.
    ASSERT_EQ(1, ::send(pair.client_, "!", 1, MSG_OOB));
    pd.revents = 0;
    EXPECT_EQ(1, st_poll(&pd, 1, ST_UTEST_TIMEOUT));
    EXPECT_EQ(POLLPRI, pd.revents);
    data = 0;
    EXPECT_EQ(1, ::recv(pair.server_, &data, 1, MSG_OOB));
    EXPECT_EQ('!', data);
#else
    st_utime_t starttime = st_utime();
    errno = 0;
    EXPECT_EQ(-1, st_poll(&pd, 1, ST_UTEST_TIMEOUT));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_LT(st_utime() - starttime, ST_UTEST_TIMEOUT);
#endif

    EXPECT_EQ(0, st_netfd_close(conn));
    pair.server_ = -1;
}

// One coroutine reads commands from the connection while another waits for an abort on it, so the connection is
// registered for both. A command wakes only the reader, and the abort waiter keeps waiting. Then the client sends an
// urgent byte, which wakes the abort waiter with POLLPRI. The connection then closes without EBUSY, so both counts are
// back to 0. On macOS the abort waiter fails at once with EINVAL, and the reader still waits and wakes on its command.
// Locks in current behavior.
VOID TEST(PollPriTest, ReaderAndAbortWaiterShareConnection)
{
    EventTestTcpPair pair;
    ASSERT_TRUE(event_test_tcp_pair(pair));

    EventTestReader r;
    ASSERT_TRUE((r.stfd_ = st_netfd_open_socket(pair.server_)) != NULL);
    st_thread_t reader = st_thread_create(event_test_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(reader != NULL);

    EventTestAbortWaiter w;
    w.fd_ = pair.server_;
    st_thread_t waiter = st_thread_create(event_test_abort_waiter_coroutine, &w, 1, 0);
    ASSERT_TRUE(waiter != NULL);

    // Both wait on the connection.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(r.done_);
#if defined(__linux__)
    EXPECT_FALSE(w.done_);
#else
    EXPECT_TRUE(w.done_);
    EXPECT_EQ(-1, w.r0_);
    EXPECT_EQ(EINVAL, w.errno_);
#endif

    // A command arrives; only the reader wakes and gets it.
    ASSERT_EQ(1, ::send(pair.client_, "a", 1, 0));
    EXPECT_EQ(0, st_thread_join(reader, NULL));
    EXPECT_EQ(1, r.r0_);
    EXPECT_EQ('a', r.data_);

#if defined(__linux__)
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(w.done_);

    // The client aborts; the abort waiter wakes and reads the urgent byte.
    ASSERT_EQ(1, ::send(pair.client_, "!", 1, MSG_OOB));
    EXPECT_EQ(0, st_thread_join(waiter, NULL));
    EXPECT_EQ(1, w.r0_);
    EXPECT_EQ(POLLPRI, w.revents_);
    char data = 0;
    EXPECT_EQ(1, ::recv(pair.server_, &data, 1, MSG_OOB));
    EXPECT_EQ('!', data);
#else
    EXPECT_EQ(0, st_thread_join(waiter, NULL));
#endif

    EXPECT_EQ(0, st_netfd_close(r.stfd_));
    pair.server_ = -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for more connections waiting at once than the event system's result list holds, such as a busy server
// with more than 4096 connections waiting to read. epoll sizes its result list at st_init, for up to 4096 descriptors,
// and grows it as more descriptors are registered, so a single wait reports every one that is ready. kqueue grows its
// list for a large pollset in the same way.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef _WIN32 // POSIX only: dup of a pipe
// A server waits on 4097 connections in one st_poll, one more than the initial result list, and data arrives on all of
// them at once. Each connection is a dup of one pipe's read end, so one byte makes them all readable. st_poll reports
// every connection ready, and the descriptors then close. Locks in current behavior.
VOID TEST(ManyWaitsTest, EveryReadyConnectionReported)
{
    const int n = 4097;
    if (st_getfdlimit() < n + 128) GTEST_SKIP() << "descriptor limit too low for " << n << " connections";

    int fds[2];
    ASSERT_EQ(0, pipe(fds));

    std::vector<struct pollfd> pds(n);
    for (int i = 0; i < n; i++) {
        pds[i].fd = dup(fds[0]);
        pds[i].events = POLLIN;
        pds[i].revents = 0;
        ASSERT_GE(pds[i].fd, 0);
    }

    // Data arrives; every connection is reported ready in one wait.
    ASSERT_EQ(1, ::write(fds[1], "a", 1));
    EXPECT_EQ(n, st_poll(&pds[0], n, ST_UTEST_TIMEOUT));

    int ready = 0;
    for (int i = 0; i < n; i++) {
        if (pds[i].revents == POLLIN) ready++;
        ::close(pds[i].fd);
    }
    EXPECT_EQ(n, ready);

    ::close(fds[0]);
    ::close(fds[1]);
}
#endif

#if defined(__linux__)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_destroy on epoll, such as SRS calling srs_st_destroy at exit, so ASAN reports no leaks. st_destroy
// closes the epoll descriptor and frees the event system's memory; it is the last ST call on its OS thread. Each test
// runs a new OS thread with its own epoll-based ST in a forked child, like SelectTest, and the child reports what it
// saw through shared memory. kqueue's st_destroy frees nothing yet, so these tests are for Linux only.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct DestroyTestChild {
    void (*body_)(void*);
    void* arg_;
    int done_[2];
};

static void* destroy_test_thread(void* arg)
{
    DestroyTestChild* c = (DestroyTestChild*)arg;
    c->body_(c->arg_);

    char done = 1;
    if (::write(c->done_[1], &done, 1) != 1) _exit(1);

    // st_destroy frees only the event system, so keep the thread and the rest of its ST alive until the child exits.
    for (;;) pause();
    return NULL;
}

// Runs body on a new OS thread in a forked child, and returns the child's exit status, or -1 if it didn't exit.
static int destroy_test_run(void (*body)(void*), void* arg)
{
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // A hang kills the child and fails the test, instead of hanging the suite.
        alarm(5);

        DestroyTestChild c;
        c.body_ = body;
        c.arg_ = arg;
        if (pipe(c.done_) < 0) _exit(1);

        pthread_t trd;
        if (pthread_create(&trd, NULL, destroy_test_thread, &c) != 0) _exit(1);

        char done = 0;
        if (::read(c.done_[0], &done, 1) != 1) _exit(1);

        // Not _exit, so a coverage build writes the child's counters.
        exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Whether descriptor fd is an epoll instance.
static bool destroy_test_is_epoll(int fd)
{
    char path[64], link[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    ssize_t nn = readlink(path, link, sizeof(link) - 1);
    if (nn < 0) return false;
    link[nn] = 0;
    return strcmp(link, "anon_inode:[eventpoll]") == 0;
}

struct DestroyTestServe {
    int init_r0_;
    int name_is_epoll_;
    int epfd_open_;
    int read_r0_;
    char data_;
    int close_r0_;
    int epfd_closed_;
    int epfd_errno_;
};

static void* destroy_test_writer_coroutine(void* arg)
{
    int fd = *(int*)arg;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    if (::write(fd, "a", 1) != 1) return (void*)-1;
    return NULL;
}

static void destroy_test_serve(void* arg)
{
    DestroyTestServe* r = (DestroyTestServe*)arg;

    // Nothing else opens descriptors in the child now, so epoll gets the lowest free number.
    int epfd = open("/dev/null", O_RDONLY);
    if (epfd < 0) return;
    ::close(epfd);

    st_set_eventsys(ST_EVENTSYS_ALT);
    r->init_r0_ = st_init();
    r->name_is_epoll_ = strcmp(st_get_eventsys_name(), "epoll") == 0;
    r->epfd_open_ = destroy_test_is_epoll(epfd);

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return;
    st_netfd_t stfd = st_netfd_open_socket(fds[0]);
    if (!stfd) return;

    // The connection waits for a request, which the client sends a little later.
    st_thread_t trd = st_thread_create(destroy_test_writer_coroutine, &fds[1], 1, 0);
    if (!trd) return;
    r->read_r0_ = (int)st_read(stfd, &r->data_, 1, ST_UTEST_TIMEOUT);
    st_thread_join(trd, NULL);

    r->close_r0_ = st_netfd_close(stfd);
    ::close(fds[1]);

    st_destroy();
    errno = 0;
    r->epfd_closed_ = fcntl(epfd, F_GETFD) < 0;
    r->epfd_errno_ = errno;
}

// A worker thread starts ST on epoll, serves one connection that waits for a request, closes it, then calls
// st_destroy as its last ST call, like SRS at exit. The epoll descriptor is open while ST runs, and st_destroy closes
// it. Locks in current behavior.
VOID TEST(DestroyTest, ClosesEpollDescriptor)
{
    // A result shared with the forked child; plain data, since the child's heap is its own.
    void* m = mmap(NULL, sizeof(DestroyTestServe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    ASSERT_TRUE(m != MAP_FAILED);
    DestroyTestServe* r = (DestroyTestServe*)m;
    memset(r, 0, sizeof(*r));

    EXPECT_EQ(0, destroy_test_run(destroy_test_serve, r));
    EXPECT_EQ(0, r->init_r0_);
    EXPECT_TRUE(r->name_is_epoll_);
    EXPECT_TRUE(r->epfd_open_);

    // The connection got its request, and closed without EBUSY.
    EXPECT_EQ(1, r->read_r0_);
    EXPECT_EQ('a', r->data_);
    EXPECT_EQ(0, r->close_r0_);

    // st_destroy closed the epoll descriptor.
    EXPECT_TRUE(r->epfd_closed_);
    EXPECT_EQ(EBADF, r->epfd_errno_);

    munmap(m, sizeof(DestroyTestServe));
}
#endif
