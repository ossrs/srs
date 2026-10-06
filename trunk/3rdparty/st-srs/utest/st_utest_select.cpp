/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#include <fcntl.h>
#include <new>
#ifndef _WIN32
#include <poll.h>
#include <pthread.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#endif

#define ST_UTIME_MILLISECONDS 1000
#define SELECT_TEST_TIMEOUT (1000 * ST_UTIME_MILLISECONDS)

#ifndef _WIN32 // POSIX only: each test runs its own ST in a forked child, with shared memory, pipes and pthread
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the select event system, the portable one: the only one on Cygwin, and the one an OS thread gets when
// it calls st_init without choosing. It watches at most FD_SETSIZE descriptors, so its st_init lowers the descriptor
// limit of the whole process to FD_SETSIZE, for good. The test process keeps epoll or kqueue, so each test runs a new
// OS thread with its own select-based ST in a forked child, and the child reports what it saw through shared memory.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A result shared with the forked child; T must be plain data, since the child's heap is its own.
template <typename T>
struct SelectTestShared {
    T* p_;
    SelectTestShared() : p_(NULL) {
        void* m = mmap(NULL, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
        if (m != MAP_FAILED) p_ = new (m) T();
    }
    ~SelectTestShared() {
        if (p_) munmap(p_, sizeof(T));
    }
};

struct SelectTestChild {
    void (*body_)(void*);
    void* arg_;
    int done_[2];
};

static void* select_test_thread(void* arg)
{
    SelectTestChild* c = (SelectTestChild*)arg;
    c->body_(c->arg_);

    char done = 1;
    if (::write(c->done_[1], &done, 1) != 1) _exit(1);

    // ST can't free an instance, so keep the thread and its instance alive until the child exits.
    for (;;) pause();
    return NULL;
}

// Runs body on a new OS thread in a forked child, and returns the child's exit status, or -1 if it didn't exit. The
// thread has no ST yet: body chooses the event system and calls st_init itself.
static int select_test_run(void (*body)(void*), void* arg)
{
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // A hang kills the child and fails the test, instead of hanging the suite.
        alarm(5);

        SelectTestChild c;
        c.body_ = body;
        c.arg_ = arg;
        if (pipe(c.done_) < 0) _exit(1);

        pthread_t trd;
        if (pthread_create(&trd, NULL, select_test_thread, &c) != 0) _exit(1);

        char done = 0;
        if (::read(c.done_[0], &done, 1) != 1) _exit(1);

        // Not _exit, so a coverage build writes the child's counters.
        exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct SelectTestChoice {
    int before_;
    char before_name_[16];
    int unknown_r0_;
    int unknown_errno_;
    int after_unknown_;
    int init_r0_;
    int chosen_;
    char chosen_name_[16];
    int alt_r0_;
    int alt_errno_;
};

static void select_test_choice(void* arg)
{
    SelectTestChoice* r = (SelectTestChoice*)arg;

    r->before_ = st_get_eventsys();
    strncpy(r->before_name_, st_get_eventsys_name(), sizeof(r->before_name_) - 1);

    errno = 0;
    r->unknown_r0_ = st_set_eventsys(100);
    r->unknown_errno_ = errno;
    r->after_unknown_ = st_get_eventsys();

    r->init_r0_ = st_init();
    r->chosen_ = st_get_eventsys();
    strncpy(r->chosen_name_, st_get_eventsys_name(), sizeof(r->chosen_name_) - 1);

    errno = 0;
    r->alt_r0_ = st_set_eventsys(ST_EVENTSYS_ALT);
    r->alt_errno_ = errno;
}

// A new OS thread has no event system until it chooses one: st_get_eventsys returns -1 and the name is empty. A number
// ST doesn't know fails with EINVAL and chooses nothing. A thread that calls st_init without choosing, such as a library
// that starts ST on its own thread, runs on select. After st_init the choice is fixed, and asking for the other one
// fails with EBUSY. Locks in current behavior.
VOID TEST(SelectTest, ThreadWithoutChoiceRunsOnSelect)
{
    SelectTestShared<SelectTestChoice> r;
    ASSERT_TRUE(r.p_ != NULL);
    ASSERT_EQ(0, select_test_run(select_test_choice, r.p_));

    EXPECT_EQ(-1, r.p_->before_);
    EXPECT_STREQ("", r.p_->before_name_);

    EXPECT_EQ(-1, r.p_->unknown_r0_);
    EXPECT_EQ(EINVAL, r.p_->unknown_errno_);
    EXPECT_EQ(-1, r.p_->after_unknown_);

    EXPECT_EQ(0, r.p_->init_r0_);
    EXPECT_EQ(ST_EVENTSYS_SELECT, r.p_->chosen_);
    EXPECT_STREQ("select", r.p_->chosen_name_);

    EXPECT_EQ(-1, r.p_->alt_r0_);
    EXPECT_EQ(EBUSY, r.p_->alt_errno_);
}

struct SelectTestLimit {
    rlim_t hard_before_;
    int set_r0_;
    int init_r0_;
    rlim_t soft_after_;
    rlim_t hard_after_;
    int fdlimit_;
};

static void select_test_limit(void* arg)
{
    SelectTestLimit* r = (SelectTestLimit*)arg;

    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return;
    r->hard_before_ = rlim.rlim_max;

    r->set_r0_ = st_set_eventsys(ST_EVENTSYS_SELECT);
    r->init_r0_ = st_init();

    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return;
    r->soft_after_ = rlim.rlim_cur;
    r->hard_after_ = rlim.rlim_max;
    r->fdlimit_ = st_getfdlimit();
}

// A server that chooses select, as SRS does on Cygwin, can't watch a descriptor numbered FD_SETSIZE or above, so
// st_init lowers the process's hard limit of open descriptors to FD_SETSIZE when it is higher, raises the soft limit to
// the hard one, and st_getfdlimit reports it. The process can't raise the hard limit again, which is why these tests
// run in a child. Locks in current behavior.
VOID TEST(SelectTest, SelectLimitsProcessDescriptors)
{
    SelectTestShared<SelectTestLimit> r;
    ASSERT_TRUE(r.p_ != NULL);
    ASSERT_EQ(0, select_test_run(select_test_limit, r.p_));

    EXPECT_EQ(0, r.p_->set_r0_);
    EXPECT_EQ(0, r.p_->init_r0_);

    rlim_t expected = r.p_->hard_before_ > (rlim_t)FD_SETSIZE ? (rlim_t)FD_SETSIZE : r.p_->hard_before_;
    EXPECT_EQ(expected, r.p_->hard_after_);
    EXPECT_EQ(expected, r.p_->soft_after_);
    EXPECT_EQ((int)expected, r.p_->fdlimit_);
}

struct SelectTestRead {
    int r0_;
    int errno_;
    char data_;
    bool done_;
};

struct SelectTestReader {
    st_netfd_t stfd_;
    st_utime_t timeout_;
    SelectTestRead* result_;
};

static void* select_test_reader_coroutine(void* arg)
{
    SelectTestReader* r = (SelectTestReader*)arg;
    errno = 0;
    r->result_->r0_ = (int)st_read(r->stfd_, &r->result_->data_, 1, r->timeout_);
    r->result_->errno_ = errno;
    r->result_->done_ = true;
    return NULL;
}

struct SelectTestJobs {
    int init_r0_;
    SelectTestRead patient_;
    SelectTestRead impatient_;
    // Whether the patient worker was still waiting when the impatient one gave up.
    bool patient_waited_;
    int busy_r0_;
    int busy_errno_;
    int close_r0_;
};

static void* select_test_feeder(void* arg)
{
    // Long enough that the scheduler is waiting in select by then, with nothing on a timer.
    usleep(20 * 1000);
    int fd = *(int*)arg;
    if (::write(fd, "a", 1) != 1) return NULL;
    return NULL;
}

static void select_test_jobs(void* arg)
{
    SelectTestJobs* r = (SelectTestJobs*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (pipe(fds) < 0) return;
    st_netfd_t jobs = st_netfd_open(fds[0]);
    if (!jobs) return;

    SelectTestReader patient = {jobs, ST_UTIME_NO_TIMEOUT, &r->patient_};
    SelectTestReader impatient = {jobs, 10 * ST_UTIME_MILLISECONDS, &r->impatient_};
    st_thread_t patient_trd = st_thread_create(select_test_reader_coroutine, &patient, 1, 0);
    st_thread_t impatient_trd = st_thread_create(select_test_reader_coroutine, &impatient, 1, 0);
    if (!patient_trd || !impatient_trd) return;

    // The impatient worker gives up, and the patient one keeps waiting on the same pipe.
    st_thread_join(impatient_trd, NULL);
    r->patient_waited_ = !r->patient_.done_;

    // The pipe can't be closed while a worker waits on it.
    errno = 0;
    r->busy_r0_ = st_netfd_close(jobs);
    r->busy_errno_ = errno;

    // A job arrives from another OS thread while nothing sleeps on a timer.
    pthread_t feeder;
    if (pthread_create(&feeder, NULL, select_test_feeder, &fds[1]) != 0) return;
    st_thread_join(patient_trd, NULL);
    pthread_join(feeder, NULL);

    r->close_r0_ = st_netfd_close(jobs);
    ::close(fds[1]);
}

// Two workers wait for jobs on one pipe, one with no timeout and one for 10 ms. Select counts the waiters of each
// descriptor, so when the impatient worker times out with ETIME and leaves, the pipe stays registered for the patient
// one. While it waits, closing the pipe fails with EBUSY. With every coroutine waiting on I/O and none on a timer,
// select waits with no timeout, and a job written by another OS thread wakes the patient worker with it. Then the pipe
// closes. Locks in current behavior.
VOID TEST(SelectTest, TimedOutWorkerLeavesOtherWaiting)
{
    SelectTestShared<SelectTestJobs> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->busy_r0_ = r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_jobs, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_TRUE(r.p_->impatient_.done_);
    EXPECT_EQ(-1, r.p_->impatient_.r0_);
    EXPECT_EQ(ETIME, r.p_->impatient_.errno_);
    EXPECT_TRUE(r.p_->patient_waited_);

    EXPECT_EQ(-1, r.p_->busy_r0_);
    EXPECT_EQ(EBUSY, r.p_->busy_errno_);

    EXPECT_TRUE(r.p_->patient_.done_);
    EXPECT_EQ(1, r.p_->patient_.r0_);
    EXPECT_EQ('a', r.p_->patient_.data_);
    EXPECT_EQ(0, r.p_->close_r0_);
}

struct SelectTestConn {
    int init_r0_;
    bool filled_;
    // Whether each side was done after 10 ms, after the peer sent a byte, and after the peer read everything.
    bool received_[3];
    bool sent_[3];
    SelectTestRead receive_;
    SelectTestRead send_;
    int close_r0_;
};

struct SelectTestSender {
    st_netfd_t stfd_;
    SelectTestRead* result_;
};

static void* select_test_sender_coroutine(void* arg)
{
    SelectTestSender* s = (SelectTestSender*)arg;
    errno = 0;
    s->result_->r0_ = (int)st_write(s->stfd_, "z", 1, SELECT_TEST_TIMEOUT);
    s->result_->errno_ = errno;
    s->result_->done_ = true;
    return NULL;
}

static void select_test_conn(void* arg)
{
    SelectTestConn* r = (SelectTestConn*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return;
    int sndbuf = 4096;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    // The peer reads nothing, so the connection's send buffer fills up.
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    while (::write(fds[0], buf, sizeof(buf)) > 0) {
    }
    r->filled_ = (errno == EAGAIN || errno == EWOULDBLOCK);

    SelectTestReader receiver = {conn, SELECT_TEST_TIMEOUT, &r->receive_};
    SelectTestSender sender = {conn, &r->send_};
    st_thread_t receive_trd = st_thread_create(select_test_reader_coroutine, &receiver, 1, 0);
    st_thread_t send_trd = st_thread_create(select_test_sender_coroutine, &sender, 1, 0);
    if (!receive_trd || !send_trd) return;

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[0] = r->receive_.done_;
    r->sent_[0] = r->send_.done_;

    // The peer sends a byte, and only the receiver wakes.
    if (::write(fds[1], "x", 1) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[1] = r->receive_.done_;
    r->sent_[1] = r->send_.done_;

    // The peer reads everything, and the sender wakes.
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);
    while (::read(fds[1], buf, sizeof(buf)) > 0) {
    }
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[2] = r->receive_.done_;
    r->sent_[2] = r->send_.done_;

    st_thread_join(receive_trd, NULL);
    st_thread_join(send_trd, NULL);
    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);
}

// A connection has a receive coroutine and a send coroutine, the way SRS serves a client, and the peer reads nothing,
// so the send buffer is full. The receiver waits to read and the sender waits to write the same descriptor. When the
// peer sends a byte, only the receiver wakes, with it, and the sender keeps waiting. When the peer reads everything,
// the sender wakes and writes its byte. Then the connection closes. Locks in current behavior.
VOID TEST(SelectTest, ReceiverAndSenderShareConnection)
{
    SelectTestShared<SelectTestConn> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_conn, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);
    ASSERT_TRUE(r.p_->filled_);

    EXPECT_FALSE(r.p_->received_[0]);
    EXPECT_FALSE(r.p_->sent_[0]);

    EXPECT_TRUE(r.p_->received_[1]);
    EXPECT_FALSE(r.p_->sent_[1]);
    EXPECT_EQ(1, r.p_->receive_.r0_);
    EXPECT_EQ('x', r.p_->receive_.data_);

    EXPECT_TRUE(r.p_->sent_[2]);
    EXPECT_EQ(1, r.p_->send_.r0_);
    EXPECT_EQ(0, r.p_->close_r0_);
}

struct SelectTestRejects {
    int init_r0_;
    // st_poll with a descriptor of FD_SETSIZE, a negative one, no events, and POLLRDNORM.
    int r0_[4];
    int errno_[4];
    int close_r0_;
};

static void select_test_rejects(void* arg)
{
    SelectTestRejects* r = (SelectTestRejects*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (pipe(fds) < 0) return;
    st_netfd_t reader = st_netfd_open(fds[0]);
    if (!reader) return;

    struct pollfd bad[4];
    bad[0].fd = FD_SETSIZE;
    bad[0].events = POLLIN;
    bad[1].fd = -1;
    bad[1].events = POLLIN;
    bad[2].fd = fds[1];
    bad[2].events = 0;
    bad[3].fd = fds[1];
    bad[3].events = POLLRDNORM;

    for (int i = 0; i < 4; i++) {
        // A valid descriptor first, so a half-registered set would leave it counted.
        struct pollfd pds[2];
        pds[0].fd = fds[0];
        pds[0].events = POLLIN;
        pds[1] = bad[i];

        errno = 0;
        r->r0_[i] = st_poll(pds, 2, SELECT_TEST_TIMEOUT);
        r->errno_[i] = errno;
    }

    r->close_r0_ = st_netfd_close(reader);
    ::close(fds[1]);
}

// Select can only watch descriptors below FD_SETSIZE for reading, writing or priority data. A st_poll set with a
// descriptor of FD_SETSIZE or above, a negative one, no events, or another event such as POLLRDNORM, fails at once with
// EINVAL, and the valid descriptor before it is left unregistered, so it then closes without EBUSY. Locks in current
// behavior.
VOID TEST(SelectTest, PollRejectsWhatSelectCannotWatch)
{
    SelectTestShared<SelectTestRejects> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_rejects, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(-1, r.p_->r0_[i]) << "case " << i;
        EXPECT_EQ(EINVAL, r.p_->errno_[i]) << "case " << i;
    }
    EXPECT_EQ(0, r.p_->close_r0_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for waiting for TCP urgent data with POLLPRI on select, such as a telnet server whose user presses
// interrupt: the client sends one urgent byte with send(MSG_OOB) on the connection that also carries its commands.
// Select watches the connection in its exception set and counts those waiters apart from the reading and writing ones.
// Unlike kqueue, which rejects POLLPRI, select runs these on macOS too.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Connects a TCP pair on loopback with plain blocking calls: fds[0] is the server side, fds[1] the client side.
static bool select_test_tcp_pair(int fds[2])
{
    fds[0] = fds[1] = -1;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addrlen = sizeof(addr);
    if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, 1) < 0
        || getsockname(lfd, (sockaddr*)&addr, &addrlen) < 0) {
        ::close(lfd);
        return false;
    }

    fds[1] = socket(AF_INET, SOCK_STREAM, 0);
    if (fds[1] >= 0 && ::connect(fds[1], (sockaddr*)&addr, sizeof(addr)) == 0) {
        fds[0] = ::accept(lfd, NULL, NULL);
    }
    ::close(lfd);
    return fds[0] >= 0;
}

struct SelectTestTelnet {
    int init_r0_;
    // st_poll for a command or an abort: while the client is idle, after a command, and after an urgent byte.
    int r0_[3];
    short revents_[3];
    char command_;
    char urgent_;
    int close_r0_;
};

static void select_test_telnet(void* arg)
{
    SelectTestTelnet* r = (SelectTestTelnet*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (!select_test_tcp_pair(fds)) return;
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    struct pollfd pd;
    pd.fd = fds[0];
    pd.events = POLLIN | POLLPRI;

    // The client is idle, so the wait times out.
    pd.revents = 0;
    r->r0_[0] = st_poll(&pd, 1, 10 * ST_UTIME_MILLISECONDS);
    r->revents_[0] = pd.revents;

    // A command arrives, and the server reads it.
    if (::send(fds[1], "a", 1, 0) != 1) return;
    pd.revents = 0;
    r->r0_[1] = st_poll(&pd, 1, SELECT_TEST_TIMEOUT);
    r->revents_[1] = pd.revents;
    if (st_read(conn, &r->command_, 1, SELECT_TEST_TIMEOUT) != 1) return;

    // The client aborts, and the server reads the urgent byte.
    if (::send(fds[1], "!", 1, MSG_OOB) != 1) return;
    pd.revents = 0;
    r->r0_[2] = st_poll(&pd, 1, SELECT_TEST_TIMEOUT);
    r->revents_[2] = pd.revents;
    if (::recv(fds[0], &r->urgent_, 1, MSG_OOB) != 1) return;

    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);
}

// The server waits for a command or an abort in one st_poll, like the select loop of a telnet server with the
// connection in both the read and the exception set, and an idle timeout. While the client is idle, the wait returns 0.
// A command wakes it with only POLLIN, and it reads the command. Then the client sends an urgent byte, which wakes it
// with only POLLPRI, and recv with MSG_OOB gets the byte. The connection then closes without EBUSY, so neither the
// timeout nor the wakeups left it registered. Locks in current behavior.
VOID TEST(SelectTest, CommandOrAbortInOneWait)
{
    SelectTestShared<SelectTestTelnet> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_telnet, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_EQ(0, r.p_->r0_[0]);
    EXPECT_EQ(0, r.p_->revents_[0]);

    EXPECT_EQ(1, r.p_->r0_[1]);
    EXPECT_EQ(POLLIN, r.p_->revents_[1]);
    EXPECT_EQ('a', r.p_->command_);

    EXPECT_EQ(1, r.p_->r0_[2]);
    EXPECT_EQ(POLLPRI, r.p_->revents_[2]);
    EXPECT_EQ('!', r.p_->urgent_);

    EXPECT_EQ(0, r.p_->close_r0_);
}

struct SelectTestAbortWaiter {
    int fd_;
    int r0_;
    short revents_;
    bool done_;
};

static void* select_test_abort_waiter_coroutine(void* arg)
{
    SelectTestAbortWaiter* w = (SelectTestAbortWaiter*)arg;
    struct pollfd pd;
    pd.fd = w->fd_;
    pd.events = POLLPRI;
    pd.revents = 0;
    w->r0_ = st_poll(&pd, 1, SELECT_TEST_TIMEOUT);
    w->revents_ = pd.revents;
    w->done_ = true;
    return NULL;
}

struct SelectTestShare {
    int init_r0_;
    // Whether each one was done after 10 ms, after a command, and after an urgent byte.
    bool read_[3];
    bool aborted_[3];
    SelectTestRead command_;
    SelectTestAbortWaiter abort_;
    char urgent_;
    int close_r0_;
};

static void select_test_share(void* arg)
{
    SelectTestShare* r = (SelectTestShare*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (!select_test_tcp_pair(fds)) return;
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    SelectTestReader reader = {conn, SELECT_TEST_TIMEOUT, &r->command_};
    r->abort_.fd_ = fds[0];
    st_thread_t reader_trd = st_thread_create(select_test_reader_coroutine, &reader, 1, 0);
    st_thread_t abort_trd = st_thread_create(select_test_abort_waiter_coroutine, &r->abort_, 1, 0);
    if (!reader_trd || !abort_trd) return;

    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[0] = r->command_.done_;
    r->aborted_[0] = r->abort_.done_;

    // A command arrives, and only the reader wakes.
    if (::send(fds[1], "a", 1, 0) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[1] = r->command_.done_;
    r->aborted_[1] = r->abort_.done_;

    // The client aborts, and the abort waiter wakes.
    if (::send(fds[1], "!", 1, MSG_OOB) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[2] = r->command_.done_;
    r->aborted_[2] = r->abort_.done_;

    st_thread_join(reader_trd, NULL);
    st_thread_join(abort_trd, NULL);
    if (::recv(fds[0], &r->urgent_, 1, MSG_OOB) != 1) return;

    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);
}

// One coroutine reads commands from the connection while another waits for an abort on it, so select watches the
// connection in both the read and the exception set. A command wakes only the reader, with the command, and the abort
// waiter keeps waiting. Then the client sends an urgent byte, which wakes the abort waiter with only POLLPRI, and recv
// with MSG_OOB gets it. The connection then closes without EBUSY, so both counts are back to 0. Locks in current
// behavior.
VOID TEST(SelectTest, ReaderAndAbortWaiterShareConnection)
{
    SelectTestShared<SelectTestShare> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_share, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_FALSE(r.p_->read_[0]);
    EXPECT_FALSE(r.p_->aborted_[0]);

    EXPECT_TRUE(r.p_->read_[1]);
    EXPECT_FALSE(r.p_->aborted_[1]);
    EXPECT_EQ(1, r.p_->command_.r0_);
    EXPECT_EQ('a', r.p_->command_.data_);

    EXPECT_TRUE(r.p_->aborted_[2]);
    EXPECT_EQ(1, r.p_->abort_.r0_);
    EXPECT_EQ(POLLPRI, r.p_->abort_.revents_);
    EXPECT_EQ('!', r.p_->urgent_);

    EXPECT_EQ(0, r.p_->close_r0_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for a descriptor closed under a waiter on select, a program bug: a coroutine closes a connection with
// close() while another coroutine waits on it, instead of st_netfd_close, which would refuse with EBUSY. Select then
// fails with EBADF on every call, so ST probes each waiting descriptor, wakes the waiters of the closed one with
// POLLNVAL, which st_read and st_write report as EBADF, and keeps the others waiting.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SelectTestClosed {
    int init_r0_;
    bool filled_;
    // Whether each coroutine was done after the close, and after the second client sent a byte.
    bool received_[2];
    bool sent_[2];
    bool other_done_[2];
    SelectTestRead receive_;
    SelectTestRead send_;
    SelectTestRead other_;
    int close_r0_;
    int close_errno_;
    int other_close_r0_;
};

static void select_test_closed(void* arg)
{
    SelectTestClosed* r = (SelectTestClosed*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    // The second connection has the higher descriptor, so select must keep watching it after the first one is gone.
    int a[2], b[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, a) < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, b) < 0) return;
    int sndbuf = 4096;
    setsockopt(a[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    st_netfd_t conn_a = st_netfd_open_socket(a[0]);
    st_netfd_t conn_b = st_netfd_open_socket(b[0]);
    if (!conn_a || !conn_b) return;

    // The first client reads nothing, so its connection's send buffer fills up.
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    while (::write(a[0], buf, sizeof(buf)) > 0) {
    }
    r->filled_ = (errno == EAGAIN || errno == EWOULDBLOCK);

    SelectTestReader receiver = {conn_a, SELECT_TEST_TIMEOUT, &r->receive_};
    SelectTestSender sender = {conn_a, &r->send_};
    SelectTestReader other = {conn_b, SELECT_TEST_TIMEOUT, &r->other_};
    st_thread_t receive_trd = st_thread_create(select_test_reader_coroutine, &receiver, 1, 0);
    st_thread_t send_trd = st_thread_create(select_test_sender_coroutine, &sender, 1, 0);
    st_thread_t other_trd = st_thread_create(select_test_reader_coroutine, &other, 1, 0);
    if (!receive_trd || !send_trd || !other_trd) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    // The bug: the first connection is closed while its coroutines wait.
    ::close(a[0]);
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[0] = r->receive_.done_;
    r->sent_[0] = r->send_.done_;
    r->other_done_[0] = r->other_.done_;

    // The second client sends a byte, and its reader wakes with it.
    if (::write(b[1], "b", 1) != 1) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->received_[1] = r->receive_.done_;
    r->sent_[1] = r->send_.done_;
    r->other_done_[1] = r->other_.done_;

    st_thread_join(receive_trd, NULL);
    st_thread_join(send_trd, NULL);
    st_thread_join(other_trd, NULL);

    // Nothing waits on the first connection any more, so ST frees it, and only close() fails, on the closed descriptor.
    errno = 0;
    r->close_r0_ = st_netfd_close(conn_a);
    r->close_errno_ = errno;
    r->other_close_r0_ = st_netfd_close(conn_b);
    ::close(a[1]);
    ::close(b[1]);
}

// Two clients are connected. The first has a receive and a send coroutine, the way SRS serves a client, and reads
// nothing, so its sender waits for room to write. The second has a reader waiting. By mistake, the first connection is
// closed with close() while its coroutines wait. Both wake at once with EBADF, instead of waiting for their 1 s timeout
// while select fails over and over. The second reader keeps waiting, and wakes with the byte its client sends. Then
// st_netfd_close on the first connection doesn't fail with EBUSY, since its coroutines are gone, but with EBADF from
// close(). Locks in current behavior.
VOID TEST(SelectTest, ClosedConnectionWakesItsCoroutines)
{
    SelectTestShared<SelectTestClosed> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = r.p_->other_close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_closed, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);
    ASSERT_TRUE(r.p_->filled_);

    EXPECT_TRUE(r.p_->received_[0]);
    EXPECT_EQ(-1, r.p_->receive_.r0_);
    EXPECT_EQ(EBADF, r.p_->receive_.errno_);
    EXPECT_TRUE(r.p_->sent_[0]);
    EXPECT_EQ(-1, r.p_->send_.r0_);
    EXPECT_EQ(EBADF, r.p_->send_.errno_);
    EXPECT_FALSE(r.p_->other_done_[0]);

    EXPECT_TRUE(r.p_->other_done_[1]);
    EXPECT_EQ(1, r.p_->other_.r0_);
    EXPECT_EQ('b', r.p_->other_.data_);

    EXPECT_EQ(-1, r.p_->close_r0_);
    EXPECT_EQ(EBADF, r.p_->close_errno_);
    EXPECT_EQ(0, r.p_->other_close_r0_);
}

struct SelectTestAfterBadFd {
    st_netfd_t stfd_;
    st_cond_t stop_;
    SelectTestRead read_;
    int stop_r0_;
    int stop_errno_;
    bool stopped_;
};

static void* select_test_after_bad_fd_coroutine(void* arg)
{
    SelectTestAfterBadFd* c = (SelectTestAfterBadFd*)arg;
    errno = 0;
    c->read_.r0_ = (int)st_read(c->stfd_, &c->read_.data_, 1, 200 * ST_UTIME_MILLISECONDS);
    c->read_.errno_ = errno;
    c->read_.done_ = true;

    // After the error, the connection waits to be stopped, with no timeout.
    errno = 0;
    c->stop_r0_ = st_cond_wait(c->stop_);
    c->stop_errno_ = errno;
    c->stopped_ = true;
    return NULL;
}

struct SelectTestTimer {
    int init_r0_;
    // Whether the connection had read, and had stopped, after the close and after its read timeout would have expired.
    bool read_[2];
    bool stopped_[2];
    SelectTestAfterBadFd conn_;
};

static void select_test_timer(void* arg)
{
    SelectTestTimer* r = (SelectTestTimer*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return;
    r->conn_.stfd_ = st_netfd_open_socket(fds[0]);
    r->conn_.stop_ = st_cond_new();
    if (!r->conn_.stfd_ || !r->conn_.stop_) return;

    st_thread_t trd = st_thread_create(select_test_after_bad_fd_coroutine, &r->conn_, 1, 0);
    if (!trd) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    // The bug: the connection is closed while it waits to read.
    ::close(fds[0]);
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->read_[0] = r->conn_.read_.done_;
    r->stopped_[0] = r->conn_.stopped_;

    // 20 ms past the 200 ms the read would have timed out at.
    st_usleep(200 * ST_UTIME_MILLISECONDS);
    r->read_[1] = r->conn_.read_.done_;
    r->stopped_[1] = r->conn_.stopped_;

    st_cond_signal(r->conn_.stop_);
    st_thread_join(trd, NULL);

    st_cond_destroy(r->conn_.stop_);
    st_netfd_free(r->conn_.stfd_);
    ::close(fds[1]);
}

// A connection reads with a 200 ms timeout, and by mistake it is closed with close() after 10 ms while it waits. It
// wakes at once with EBADF, and then waits to be stopped, with no timeout. Its read timeout is cancelled when it wakes:
// 20 ms after the read would have timed out, it is still waiting, where a stale timer would wake it with ETIME. It is stopped with a signal, and its wait returns 0.
// Locks in current behavior.
VOID TEST(SelectTest, ClosedUnderTimedReaderCancelsTimeout)
{
    SelectTestShared<SelectTestTimer> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->conn_.stop_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_timer, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_TRUE(r.p_->read_[0]);
    EXPECT_EQ(-1, r.p_->conn_.read_.r0_);
    EXPECT_EQ(EBADF, r.p_->conn_.read_.errno_);
    EXPECT_FALSE(r.p_->stopped_[0]);

    EXPECT_FALSE(r.p_->stopped_[1]);

    EXPECT_EQ(0, r.p_->conn_.stop_r0_);
    EXPECT_EQ(0, r.p_->conn_.stop_errno_);
}

struct SelectTestPollWaiter {
    struct pollfd pds_[2];
    int r0_;
    bool done_;
};

static void* select_test_poll_waiter_coroutine(void* arg)
{
    SelectTestPollWaiter* w = (SelectTestPollWaiter*)arg;
    w->r0_ = st_poll(w->pds_, 2, SELECT_TEST_TIMEOUT);
    w->done_ = true;
    return NULL;
}

struct SelectTestPollClosed {
    int init_r0_;
    int conn_fd_;
    bool done_;
    SelectTestPollWaiter waiter_;
    int stop_close_r0_;
    int next_fd_;
    int next_close_r0_;
};

static void select_test_poll_closed(void* arg)
{
    SelectTestPollClosed* r = (SelectTestPollClosed*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int stop[2], conn[2];
    if (pipe(stop) < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, conn) < 0) return;
    st_netfd_t stop_stfd = st_netfd_open(stop[0]);
    if (!stop_stfd) return;
    r->conn_fd_ = conn[0];

    // The server waits for a command or an abort on its connection, or for a stop request, in one st_poll.
    SelectTestPollWaiter* w = &r->waiter_;
    w->pds_[0].fd = conn[0];
    w->pds_[0].events = POLLIN | POLLPRI;
    w->pds_[1].fd = stop[0];
    w->pds_[1].events = POLLIN;
    st_thread_t trd = st_thread_create(select_test_poll_waiter_coroutine, w, 1, 0);
    if (!trd) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    // The bug: the connection is closed while the server waits on it.
    ::close(conn[0]);
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    r->done_ = w->done_;
    st_thread_join(trd, NULL);

    // The next connection gets the closed descriptor's number, with no waiter left counted on it.
    int next[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, next) < 0) return;
    r->next_fd_ = next[0];
    st_netfd_t next_stfd = st_netfd_open_socket(next[0]);
    if (!next_stfd) return;
    r->next_close_r0_ = st_netfd_close(next_stfd);

    // The stop pipe was left unregistered too.
    r->stop_close_r0_ = st_netfd_close(stop_stfd);

    ::close(next[1]);
    ::close(conn[1]);
    ::close(stop[1]);
}

// A server waits in one st_poll for a command or an urgent abort on its connection, or for a stop request on a pipe,
// and by mistake the connection is closed with close() while it waits. The wait returns 1 at once, with POLLNVAL on the
// connection and nothing on the pipe. The whole set is unregistered: the pipe closes without EBUSY, and the next
// connection, which gets the closed descriptor's number, closes without EBUSY too, so no read or priority waiter is
// left counted on it. Locks in current behavior.
VOID TEST(SelectTest, ClosedInPollSetIsReported)
{
    SelectTestShared<SelectTestPollClosed> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->stop_close_r0_ = r.p_->next_close_r0_ = 1;
    r.p_->next_fd_ = -1;
    ASSERT_EQ(0, select_test_run(select_test_poll_closed, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_TRUE(r.p_->done_);
    EXPECT_EQ(1, r.p_->waiter_.r0_);
    EXPECT_EQ(POLLNVAL, r.p_->waiter_.pds_[0].revents);
    EXPECT_EQ(0, r.p_->waiter_.pds_[1].revents);

    EXPECT_EQ(0, r.p_->stop_close_r0_);
    EXPECT_EQ(r.p_->conn_fd_, r.p_->next_fd_);
    EXPECT_EQ(0, r.p_->next_close_r0_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the descriptors a select server can serve: only numbers below FD_SETSIZE, the size of select's fd
// sets. Its st_init lowers the process's descriptor limit to FD_SETSIZE, so the kernel itself refuses the descriptor
// past the limit, and ST refuses one numbered FD_SETSIZE or above that was opened before st_init.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SelectTestHighFd {
    bool skipped_;
    int init_r0_;
    // The descriptor opened before st_init at FD_SETSIZE: st_netfd_open refuses it, and it stays open.
    bool refused_;
    int refused_errno_;
    bool still_open_;
    // The one at FD_SETSIZE - 1 is served: a reader waits on it and wakes on its data.
    SelectTestRead last_;
    int last_close_r0_;
    // The high one moved below FD_SETSIZE with dup is served too.
    int moved_fd_;
    SelectTestRead moved_;
    int moved_close_r0_;
};

// Serves the read end of a pipe with st_netfd_open: a reader waits for one byte, which arrives 10 ms later.
static int select_test_serve_pipe(int osfd, int wfd, SelectTestRead* result)
{
    st_netfd_t stfd = st_netfd_open(osfd);
    if (!stfd) return -1;

    SelectTestReader reader = {stfd, SELECT_TEST_TIMEOUT, result};
    st_thread_t trd = st_thread_create(select_test_reader_coroutine, &reader, 1, 0);
    if (!trd) return -1;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    if (::write(wfd, "a", 1) != 1) return -1;
    st_thread_join(trd, NULL);

    return st_netfd_close(stfd);
}

static void select_test_high_fd(void* arg)
{
    SelectTestHighFd* r = (SelectTestHighFd*)arg;

    // The program may open FD_SETSIZE + 1 descriptors before it starts ST.
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) return;
    if (rlim.rlim_max != RLIM_INFINITY && rlim.rlim_max < (rlim_t)FD_SETSIZE + 1) {
        r->skipped_ = true;
        return;
    }
    rlim.rlim_cur = FD_SETSIZE + 1;
    if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) return;

    int high[2], last[2];
    if (pipe(high) < 0 || pipe(last) < 0) return;
    if (dup2(high[0], FD_SETSIZE) != FD_SETSIZE || dup2(last[0], FD_SETSIZE - 1) != FD_SETSIZE - 1) return;
    ::close(high[0]);
    ::close(last[0]);

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    errno = 0;
    r->refused_ = st_netfd_open(FD_SETSIZE) == NULL;
    r->refused_errno_ = errno;
    r->still_open_ = fcntl(FD_SETSIZE, F_GETFD) >= 0;

    r->last_close_r0_ = select_test_serve_pipe(FD_SETSIZE - 1, last[1], &r->last_);

    r->moved_fd_ = dup(FD_SETSIZE);
    ::close(FD_SETSIZE);
    r->moved_close_r0_ = select_test_serve_pipe(r->moved_fd_, high[1], &r->moved_);

    ::close(high[1]);
    ::close(last[1]);
}

// A program opens a descriptor numbered FD_SETSIZE before it starts ST on select, such as a listener inherited from
// its parent. Select can't watch it, so st_netfd_open fails with EMFILE, and leaves the descriptor open. The descriptor
// just below, FD_SETSIZE - 1, is served: a reader waits on it and wakes on its data, then it closes. The program moves
// the high descriptor below FD_SETSIZE with dup, since st_init lowered the limit, and serves it the same way. Locks in
// current behavior.
VOID TEST(SelectTest, DescriptorAboveSetSizeRefused)
{
    SelectTestShared<SelectTestHighFd> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->last_close_r0_ = r.p_->moved_close_r0_ = 1;
    r.p_->moved_fd_ = -1;
    ASSERT_EQ(0, select_test_run(select_test_high_fd, r.p_));
    if (r.p_->skipped_) GTEST_SKIP() << "descriptor limit too low for descriptor " << FD_SETSIZE;
    ASSERT_EQ(0, r.p_->init_r0_);

    EXPECT_TRUE(r.p_->refused_);
    EXPECT_EQ(EMFILE, r.p_->refused_errno_);
    EXPECT_TRUE(r.p_->still_open_);

    EXPECT_TRUE(r.p_->last_.done_);
    EXPECT_EQ(1, r.p_->last_.r0_);
    EXPECT_EQ('a', r.p_->last_.data_);
    EXPECT_EQ(0, r.p_->last_close_r0_);

    EXPECT_GE(r.p_->moved_fd_, 0);
    EXPECT_LT(r.p_->moved_fd_, FD_SETSIZE);
    EXPECT_TRUE(r.p_->moved_.done_);
    EXPECT_EQ(1, r.p_->moved_.r0_);
    EXPECT_EQ('a', r.p_->moved_.data_);
    EXPECT_EQ(0, r.p_->moved_close_r0_);
}

struct SelectTestFull {
    int init_r0_;
    int fdlimit_;
    bool filled_;
    // The accept with every descriptor in use, and how long it took.
    bool refused_;
    int refused_errno_;
    st_utime_t refused_us_;
    // What the refused client sees afterwards.
    int refused_client_r0_;
    int refused_client_errno_;
    // The accept after two descriptors were freed and a late client connected.
    int accepted_fd_;
    int read_r0_;
    char data_;
    int close_r0_;
    int listener_close_r0_;
};

static void select_test_full(void* arg)
{
    SelectTestFull* r = (SelectTestFull*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;
    r->fdlimit_ = st_getfdlimit();

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) return;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addrlen = sizeof(addr);
    if (::bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0 || ::listen(lfd, 8) < 0
        || getsockname(lfd, (sockaddr*)&addr, &addrlen) < 0) {
        return;
    }
    st_netfd_t listener = st_netfd_open_socket(lfd);
    if (!listener) return;

    // A client connects and sends its request; it waits in the accept queue.
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0 || ::connect(client, (sockaddr*)&addr, sizeof(addr)) < 0) return;
    if (::write(client, "a", 1) != 1) return;

    // Every other descriptor is in use, by earlier connections and files.
    int fillers[FD_SETSIZE];
    int nn = 0;
    while (nn < FD_SETSIZE && (fillers[nn] = dup(client)) >= 0) nn++;
    r->filled_ = nn < FD_SETSIZE && errno == EMFILE;

    st_utime_t starttime = st_utime();
    errno = 0;
    r->refused_ = st_accept(listener, NULL, NULL, SELECT_TEST_TIMEOUT) == NULL;
    r->refused_errno_ = errno;
    r->refused_us_ = st_utime() - starttime;

    // A reset reaches the client asynchronously, so it waits a little for one.
    struct pollfd pd = {client, POLLIN, 0};
    ::poll(&pd, 1, 100);
    char c = 0;
    errno = 0;
    r->refused_client_r0_ = (int)recv(client, &c, 1, MSG_DONTWAIT);
    r->refused_client_errno_ = errno;

    // Two connections close, and a late client connects on one of their descriptors and sends its request.
    for (int i = 0; i < 2 && nn > 0; i++) ::close(fillers[--nn]);
    int late = socket(AF_INET, SOCK_STREAM, 0);
    if (late < 0 || ::connect(late, (sockaddr*)&addr, sizeof(addr)) < 0) return;
    if (::write(late, "b", 1) != 1) return;

    st_netfd_t conn = st_accept(listener, NULL, NULL, SELECT_TEST_TIMEOUT);
    if (conn) {
        r->accepted_fd_ = st_netfd_fileno(conn);
        r->read_r0_ = (int)st_read(conn, &r->data_, 1, SELECT_TEST_TIMEOUT);
        r->close_r0_ = st_netfd_close(conn);
    }

    while (nn > 0) ::close(fillers[--nn]);
    r->listener_close_r0_ = st_netfd_close(listener);
    ::close(client);
    ::close(late);
}

// A select server already has a descriptor open for every number below its limit, FD_SETSIZE, when one more client
// connects. The kernel refuses the new descriptor, so st_accept fails at once with EMFILE instead of waiting. On Linux
// the client stays in the accept queue. On macOS the kernel takes it off the queue and resets it, so the client gets
// ECONNRESET. After two connections close and a late client connects, the next st_accept gets the oldest waiting
// client with its request: the refused one on Linux, the late one on macOS. Locks in current behavior.
VOID TEST(SelectTest, ConnectionPastLimitRefused)
{
    SelectTestShared<SelectTestFull> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = r.p_->listener_close_r0_ = 1;
    r.p_->accepted_fd_ = -1;
    ASSERT_EQ(0, select_test_run(select_test_full, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);
    ASSERT_LE(r.p_->fdlimit_, FD_SETSIZE);
    ASSERT_TRUE(r.p_->filled_);

    EXPECT_TRUE(r.p_->refused_);
    EXPECT_EQ(EMFILE, r.p_->refused_errno_);
    EXPECT_LT(r.p_->refused_us_, 100 * ST_UTIME_MILLISECONDS);

    EXPECT_EQ(-1, r.p_->refused_client_r0_);
#if defined(__APPLE__)
    EXPECT_EQ(ECONNRESET, r.p_->refused_client_errno_);
#else
    EXPECT_EQ(EAGAIN, r.p_->refused_client_errno_);
#endif

    EXPECT_GE(r.p_->accepted_fd_, 0);
    EXPECT_LT(r.p_->accepted_fd_, FD_SETSIZE);
    EXPECT_EQ(1, r.p_->read_r0_);
#if defined(__APPLE__)
    EXPECT_EQ('b', r.p_->data_);
#else
    EXPECT_EQ('a', r.p_->data_);
#endif
    EXPECT_EQ(0, r.p_->close_r0_);
    EXPECT_EQ(0, r.p_->listener_close_r0_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for a writer that leaves select without being woken, such as SRS sending to a player that stopped reading:
// the send coroutine gives up on its send timeout, or the server stops it when it kicks the player. ST takes the writer
// out of the write set itself, while the descriptor stays open and other coroutines may still wait on it.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct SelectTestStuck {
    int init_r0_;
    bool filled_;
    // Whether the other sender was still waiting after the timed sender gave up.
    bool stopped_waiting_;
    SelectTestRead timed_;
    SelectTestRead stopped_;
    SelectTestRead receive_;
    int close_r0_;
    SelectTestRead next_;
};

struct SelectTestTimedSender {
    st_netfd_t stfd_;
    st_utime_t timeout_;
    SelectTestRead* result_;
};

static void* select_test_timed_sender_coroutine(void* arg)
{
    SelectTestTimedSender* s = (SelectTestTimedSender*)arg;
    errno = 0;
    s->result_->r0_ = (int)st_write(s->stfd_, "z", 1, s->timeout_);
    s->result_->errno_ = errno;
    s->result_->done_ = true;
    return NULL;
}

static void select_test_stuck(void* arg)
{
    SelectTestStuck* r = (SelectTestStuck*)arg;

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return;
    int sndbuf = 4096;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    st_netfd_t conn = st_netfd_open_socket(fds[0]);
    if (!conn) return;

    // The next client's descriptors are higher than the player's.
    int next_fds[2];
    if (pipe(next_fds) < 0) return;
    st_netfd_t next = st_netfd_open(next_fds[0]);
    if (!next) return;

    // The player reads nothing, so the connection's send buffer fills up.
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    while (::write(fds[0], buf, sizeof(buf)) > 0) {
    }
    r->filled_ = (errno == EAGAIN || errno == EWOULDBLOCK);

    SelectTestReader receiver = {conn, SELECT_TEST_TIMEOUT, &r->receive_};
    SelectTestTimedSender timed = {conn, 10 * ST_UTIME_MILLISECONDS, &r->timed_};
    SelectTestTimedSender stopped = {conn, SELECT_TEST_TIMEOUT, &r->stopped_};
    st_thread_t receive_trd = st_thread_create(select_test_reader_coroutine, &receiver, 1, 0);
    st_thread_t timed_trd = st_thread_create(select_test_timed_sender_coroutine, &timed, 1, 0);
    st_thread_t stopped_trd = st_thread_create(select_test_timed_sender_coroutine, &stopped, 1, 0);
    if (!receive_trd || !timed_trd || !stopped_trd) return;

    // One sender gives up on its send timeout, and the other keeps waiting.
    st_thread_join(timed_trd, NULL);
    r->stopped_waiting_ = !r->stopped_.done_;

    // The server stops the other sender, the last writer on the connection.
    st_thread_interrupt(stopped_trd);
    st_thread_join(stopped_trd, NULL);

    // The player still sends, and the receiver wakes with it.
    if (::write(fds[1], "x", 1) != 1) return;
    st_thread_join(receive_trd, NULL);
    r->close_r0_ = st_netfd_close(conn);
    ::close(fds[1]);

    // The next client is served after the player's connection closes.
    SelectTestReader next_reader = {next, SELECT_TEST_TIMEOUT, &r->next_};
    st_thread_t next_trd = st_thread_create(select_test_reader_coroutine, &next_reader, 1, 0);
    if (!next_trd) return;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    if (::write(next_fds[1], "y", 1) != 1) return;
    st_thread_join(next_trd, NULL);
    st_netfd_close(next);
    ::close(next_fds[1]);
}

// A player stops reading, so the send buffer of its connection is full, with a receive coroutine and two send
// coroutines waiting on it. One sender gives up with ETIME on its 10 ms send timeout while the other keeps waiting, then
// the server stops the other with EINTR. The receiver still wakes on a byte from the player, and the connection closes
// without EBUSY. Then a reader on another descriptor wakes on its byte, so neither sender left its descriptor in the
// write set, which select would fail on with EBADF once the connection is closed. Locks in current behavior.
VOID TEST(SelectTest, StuckSendersLeaveConnection)
{
    SelectTestShared<SelectTestStuck> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_stuck, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);
    ASSERT_TRUE(r.p_->filled_);

    EXPECT_EQ(-1, r.p_->timed_.r0_);
    EXPECT_EQ(ETIME, r.p_->timed_.errno_);

    EXPECT_TRUE(r.p_->stopped_waiting_);
    EXPECT_EQ(-1, r.p_->stopped_.r0_);
    EXPECT_EQ(EINTR, r.p_->stopped_.errno_);

    EXPECT_EQ(1, r.p_->receive_.r0_);
    EXPECT_EQ('x', r.p_->receive_.data_);
    EXPECT_EQ(0, r.p_->close_r0_);

    EXPECT_EQ(1, r.p_->next_.r0_);
    EXPECT_EQ('y', r.p_->next_.data_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_destroy on select, such as SRS calling srs_st_destroy at exit on Cygwin, where select is the only
// event system. select has no descriptor to close, so st_destroy only frees the event system's memory: two descriptor
// sets and the waiter counts of every descriptor. The test reads the heap in use from the allocator before and after.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__SANITIZE_ADDRESS__)
#define SELECT_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SELECT_TEST_ASAN 1
#endif
#endif

// The bytes the allocator has handed out and not got back.
static size_t select_test_heap_in_use()
{
#if defined(__APPLE__)
    malloc_statistics_t stats;
    malloc_zone_statistics(NULL, &stats);
    return stats.size_in_use;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return mallinfo2().uordblks;
#else
    return (size_t)(unsigned int)mallinfo().uordblks;
#endif
}

struct SelectTestDestroy {
    int init_r0_;
    bool name_is_select_;
    int read_r0_;
    char data_;
    int close_r0_;
    size_t heap_before_;
    size_t heap_after_;
};

static void* select_test_writer_coroutine(void* arg)
{
    int fd = *(int*)arg;
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    if (::write(fd, "a", 1) != 1) return (void*)-1;
    return NULL;
}

static void select_test_destroy(void* arg)
{
    SelectTestDestroy* r = (SelectTestDestroy*)arg;

#if defined(M_ARENA_MAX)
    // glibc counts only its main arena, so this thread allocates there too.
    mallopt(M_ARENA_MAX, 1);
#endif

    st_set_eventsys(ST_EVENTSYS_SELECT);
    if ((r->init_r0_ = st_init()) != 0) return;
    r->name_is_select_ = strcmp(st_get_eventsys_name(), "select") == 0;

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) return;
    st_netfd_t stfd = st_netfd_open_socket(fds[0]);
    if (!stfd) return;

    // The connection waits for a request, which the client sends a little later.
    st_thread_t trd = st_thread_create(select_test_writer_coroutine, &fds[1], 1, 0);
    if (!trd) return;
    r->read_r0_ = (int)st_read(stfd, &r->data_, 1, SELECT_TEST_TIMEOUT);
    st_thread_join(trd, NULL);

    r->close_r0_ = st_netfd_close(stfd);
    ::close(fds[1]);

    r->heap_before_ = select_test_heap_in_use();
    st_destroy();
    r->heap_after_ = select_test_heap_in_use();
}

// A worker thread starts ST on select, serves one connection that waits for a request, closes it, then calls
// st_destroy as its last ST call, like SRS at exit. st_destroy returns, and the heap in use drops by at least the
// three descriptor sets select kept. ASAN's allocator keeps no such count, so an ASAN build checks only that
// st_destroy returns. Locks in current behavior.
VOID TEST(SelectTest, DestroyFreesEventSystem)
{
    SelectTestShared<SelectTestDestroy> r;
    ASSERT_TRUE(r.p_ != NULL);
    r.p_->close_r0_ = 1;
    ASSERT_EQ(0, select_test_run(select_test_destroy, r.p_));
    ASSERT_EQ(0, r.p_->init_r0_);
    EXPECT_TRUE(r.p_->name_is_select_);

    // The connection got its request, and closed without EBUSY.
    EXPECT_EQ(1, r.p_->read_r0_);
    EXPECT_EQ('a', r.p_->data_);
    EXPECT_EQ(0, r.p_->close_r0_);

#if !defined(SELECT_TEST_ASAN)
    EXPECT_GE(r.p_->heap_before_, r.p_->heap_after_ + 3 * sizeof(fd_set));
#endif
}
#endif
