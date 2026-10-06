/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#endif

#define ST_UTIME_MILLISECONDS 1000
#define ST_UTEST_TIMEOUT (100 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for stopping a coroutine with st_thread_interrupt, the way SRS stops every coroutine: SrsFastCoroutine::stop
// interrupts it, then joins it. A coroutine blocked in a wait wakes at once. A coroutine that is not blocked, because it
// has not run yet, is runnable, or is the one calling, keeps a pending interrupt, and its next wait fails with EINTR
// instead of blocking. A coroutine that has already exited ignores it.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// What one blocking call returned, and how long it took.
struct InterruptTestCall {
    int r0_;
    int errno_;
    st_utime_t elapsed_;
    InterruptTestCall() : r0_(0), errno_(0), elapsed_(0) {
    }
};

static void interrupt_test_usleep(InterruptTestCall& call, st_utime_t usecs)
{
    st_utime_t starttime = st_utime();
    errno = 0;
    call.r0_ = st_usleep(usecs);
    call.errno_ = errno;
    call.elapsed_ = st_utime() - starttime;
}

struct InterruptTestSleeper {
    InterruptTestCall first_;
    InterruptTestCall second_;
};

static void* interrupt_sleep_twice_coroutine(void* arg)
{
    InterruptTestSleeper* s = (InterruptTestSleeper*)arg;
    interrupt_test_usleep(s->first_, ST_UTEST_TIMEOUT);
    interrupt_test_usleep(s->second_, 1 * ST_UTIME_MILLISECONDS);
    return NULL;
}

// SRS starts a coroutine and stops it before it ever runs, such as a connection closed right after it was accepted.
// The interrupt is kept until the coroutine runs, and its first wait fails at once with EINTR instead of sleeping. The
// interrupt is consumed by that one call, so the next sleep completes. Locks in current behavior.
VOID TEST(InterruptTest, StopBeforeFirstRun)
{
    InterruptTestSleeper s;
    st_thread_t trd = st_thread_create(interrupt_sleep_twice_coroutine, &s, 1, 0);
    ASSERT_TRUE(trd != NULL);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(-1, s.first_.r0_);
    EXPECT_EQ(EINTR, s.first_.errno_);
    EXPECT_LT(s.first_.elapsed_, ST_UTEST_TIMEOUT / 2);

    EXPECT_EQ(0, s.second_.r0_);
}

struct InterruptTestConsumer {
    st_cond_t cond_;
    bool yielded_;
    InterruptTestCall wait_;
};

static void* interrupt_busy_consumer_coroutine(void* arg)
{
    InterruptTestConsumer* c = (InterruptTestConsumer*)arg;

    // Busy with a batch of messages, it gives others a turn but stays runnable.
    c->yielded_ = true;
    st_thread_yield();

    // Then it waits for the next message.
    st_utime_t starttime = st_utime();
    errno = 0;
    c->wait_.r0_ = st_cond_wait(c->cond_);
    c->wait_.errno_ = errno;
    c->wait_.elapsed_ = st_utime() - starttime;
    return NULL;
}

// An SRS consumer coroutine is busy, not waiting, when SRS stops it, the way a play loop yields between batches of
// messages. The interrupt finds it runnable and is kept; its next st_cond_wait for a new message fails at once with
// EINTR, instead of waiting forever for a message nobody will send. It never joins the wait queue, so the condition
// variable has no waiter left and can be destroyed. Locks in current behavior.
VOID TEST(InterruptTest, StopBusyConsumerBeforeCondWait)
{
    InterruptTestConsumer c;
    c.cond_ = st_cond_new();
    ASSERT_TRUE(c.cond_ != NULL);
    c.yielded_ = false;

    st_thread_t trd = st_thread_create(interrupt_busy_consumer_coroutine, &c, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The consumer runs and yields back to us, so it is runnable, not waiting.
    st_thread_yield();
    EXPECT_TRUE(c.yielded_);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(-1, c.wait_.r0_);
    EXPECT_EQ(EINTR, c.wait_.errno_);
    EXPECT_LT(c.wait_.elapsed_, ST_UTEST_TIMEOUT / 2);

    EXPECT_EQ(0, st_cond_destroy(c.cond_));
}

struct InterruptTestLocker {
    st_mutex_t lock_;
    InterruptTestCall first_;
    InterruptTestCall second_;
};

static void* interrupt_self_then_lock_coroutine(void* arg)
{
    InterruptTestLocker* l = (InterruptTestLocker*)arg;

    st_thread_interrupt(st_thread_self());

    errno = 0;
    l->first_.r0_ = st_mutex_lock(l->lock_);
    l->first_.errno_ = errno;

    errno = 0;
    l->second_.r0_ = st_mutex_lock(l->lock_);
    l->second_.errno_ = errno;
    if (l->second_.r0_ == 0) st_mutex_unlock(l->lock_);
    return NULL;
}

// A coroutine stops itself while it is running. Its next st_mutex_lock fails with EINTR even though the mutex is free,
// and does not take it: a stopped coroutine must not start a new critical section. The interrupt is consumed by that
// call, so the next lock succeeds. Locks in current behavior.
VOID TEST(InterruptTest, StopSelfBeforeMutexLock)
{
    InterruptTestLocker l;
    l.lock_ = st_mutex_new();
    ASSERT_TRUE(l.lock_ != NULL);

    st_thread_t trd = st_thread_create(interrupt_self_then_lock_coroutine, &l, 1, 0);
    ASSERT_TRUE(trd != NULL);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(-1, l.first_.r0_);
    EXPECT_EQ(EINTR, l.first_.errno_);

    EXPECT_EQ(0, l.second_.r0_);

    EXPECT_EQ(0, st_mutex_destroy(l.lock_));
}

struct InterruptTestReader {
    st_netfd_t stfd_;
    char buf_[64];
    ssize_t first_;
    InterruptTestCall second_;
    InterruptTestCall third_;
};

static void interrupt_test_read(InterruptTestReader* r, InterruptTestCall& call)
{
    st_utime_t starttime = st_utime();
    errno = 0;
    call.r0_ = (int)st_read(r->stfd_, r->buf_, sizeof(r->buf_), ST_UTEST_TIMEOUT);
    call.errno_ = errno;
    call.elapsed_ = st_utime() - starttime;
}

static void* interrupt_reader_coroutine(void* arg)
{
    InterruptTestReader* r = (InterruptTestReader*)arg;

    memset(r->buf_, 0, sizeof(r->buf_));
    r->first_ = st_read(r->stfd_, r->buf_, sizeof(r->buf_), ST_UTEST_TIMEOUT);

    interrupt_test_read(r, r->second_);
    interrupt_test_read(r, r->third_);
    return NULL;
}

// SRS stops a connection coroutine that still has data queued on its socket. The interrupt is only checked when a call
// has to wait: st_read returns the queued data first, because the syscall succeeds without waiting. The next st_read
// finds the socket empty and fails at once with EINTR in st_poll, before it waits. The read after that waits for its
// full timeout again. So a stopped coroutine may still finish a read or a write that needs no wait, and SRS checks
// pull() in its loops to stop as well. Locks in current behavior.
VOID TEST(InterruptTest, StopReaderWithQueuedData)
{
    int fds[2];
    ASSERT_EQ(0, st_utest_stream_pair(fds));
    st_netfd_t reader = st_netfd_open_socket(fds[0]);
    ASSERT_TRUE(reader != NULL);
    StStfdCleanup(reader);
    int peer = fds[1];
    st_netfd_t peer_stfd = NULL;
    StFdCleanup(peer, peer_stfd);

    ASSERT_EQ(5, st_utest_send(peer, "hello", 5));

    InterruptTestReader r;
    r.stfd_ = reader;
    st_thread_t trd = st_thread_create(interrupt_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(trd != NULL);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(5, r.first_);
    EXPECT_STREQ("hello", r.buf_);

    EXPECT_EQ(-1, r.second_.r0_);
    EXPECT_EQ(EINTR, r.second_.errno_);
    EXPECT_LT(r.second_.elapsed_, ST_UTEST_TIMEOUT / 2);

    EXPECT_EQ(-1, r.third_.r0_);
    EXPECT_EQ(ETIME, r.third_.errno_);
    EXPECT_GE(r.third_.elapsed_, ST_UTEST_TIMEOUT / 2);
}

static void* interrupt_exit_at_once_coroutine(void* arg)
{
    return arg;
}

// The handler of an SRS coroutine fails and returns, so the coroutine has exited but nobody has joined it yet; then
// SRS stops it, which interrupts it and joins it. Interrupting a coroutine that has already exited is ignored, and the
// join still returns its result. Locks in current behavior.
VOID TEST(InterruptTest, StopCoroutineThatAlreadyExited)
{
    int result = 0;
    st_thread_t trd = st_thread_create(interrupt_exit_at_once_coroutine, &result, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let it run to its end, so it waits as a zombie for the join.
    st_usleep(0);

    st_thread_interrupt(trd);

    void* retval = NULL;
    EXPECT_EQ(0, st_thread_join(trd, &retval));
    EXPECT_EQ(&result, retval);
}

struct InterruptTestParked {
    bool parked_;
    InterruptTestCall call_;
};

static void* interrupt_parked_coroutine(void* arg)
{
    InterruptTestParked* p = (InterruptTestParked*)arg;
    p->parked_ = true;
    interrupt_test_usleep(p->call_, ST_UTIME_NO_TIMEOUT);
    return NULL;
}

// A coroutine parks with st_usleep(ST_UTIME_NO_TIMEOUT) and has nothing to wake it but a stop. It is suspended with no
// timer, so only the interrupt makes it runnable again, and the sleep returns EINTR. Locks in current behavior.
VOID TEST(InterruptTest, StopParkedCoroutine)
{
    InterruptTestParked p;
    p.parked_ = false;
    st_thread_t trd = st_thread_create(interrupt_parked_coroutine, &p, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let it run and park.
    st_usleep(1 * ST_UTIME_MILLISECONDS);
    EXPECT_TRUE(p.parked_);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(-1, p.call_.r0_);
    EXPECT_EQ(EINTR, p.call_.errno_);
}

struct InterruptTestWaiter {
    st_mutex_t lock_;
    InterruptTestCall lock_call_;
    InterruptTestCall sleep_call_;
    int unlock_r0_;
};

static void* interrupt_lock_waiter_coroutine(void* arg)
{
    InterruptTestWaiter* w = (InterruptTestWaiter*)arg;

    errno = 0;
    w->lock_call_.r0_ = st_mutex_lock(w->lock_);
    w->lock_call_.errno_ = errno;

    interrupt_test_usleep(w->sleep_call_, ST_UTEST_TIMEOUT);

    w->unlock_r0_ = st_mutex_unlock(w->lock_);
    return NULL;
}

// A coroutine waits for a mutex; the owner unlocks it, which hands the mutex to the waiter, and then stops the waiter
// before it runs. The lock succeeds and the waiter owns the mutex, so it is not lost with nobody to unlock it. The
// interrupt stays pending, and the waiter's next wait fails at once with EINTR. The waiter can still unlock the mutex.
// Locks in current behavior.
VOID TEST(InterruptTest, StopWaiterAfterMutexHandover)
{
    InterruptTestWaiter w;
    w.lock_ = st_mutex_new();
    ASSERT_TRUE(w.lock_ != NULL);
    w.unlock_r0_ = -1;

    ASSERT_EQ(0, st_mutex_lock(w.lock_));

    st_thread_t trd = st_thread_create(interrupt_lock_waiter_coroutine, &w, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let the waiter block on the mutex we hold.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    EXPECT_EQ(0, st_mutex_unlock(w.lock_));
    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(0, w.lock_call_.r0_);

    EXPECT_EQ(-1, w.sleep_call_.r0_);
    EXPECT_EQ(EINTR, w.sleep_call_.errno_);
    EXPECT_LT(w.sleep_call_.elapsed_, ST_UTEST_TIMEOUT / 2);

    EXPECT_EQ(0, w.unlock_r0_);
    EXPECT_EQ(0, st_mutex_destroy(w.lock_));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for joining a coroutine with st_thread_join, the second half of how SRS stops a coroutine:
// SrsFastCoroutine::stop interrupts it, then joins it to wait for its exit and take its result. A join that could
// never return is refused: a coroutine joining itself, or a second joiner of the same coroutine. A joiner that is
// stopped while it waits gives up, and the coroutine it waited for is joined later by someone else.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void join_test_join(InterruptTestCall& call, st_thread_t trd, void** retvalp)
{
    st_utime_t starttime = st_utime();
    errno = 0;
    call.r0_ = st_thread_join(trd, retvalp);
    call.errno_ = errno;
    call.elapsed_ = st_utime() - starttime;
}

static void* join_self_coroutine(void* arg)
{
    InterruptTestCall* call = (InterruptTestCall*)arg;
    join_test_join(*call, st_thread_self(), NULL);
    return arg;
}

// The handler of an SRS coroutine stops its own coroutine, so SrsFastCoroutine::stop joins the coroutine that is
// calling it. That join would wait for its own exit forever, so it fails at once with EDEADLK, which SRS turns into an
// assert. The coroutine is not changed: it goes on, exits, and is joined normally. Locks in current behavior.
VOID TEST(JoinTest, JoinSelfFailsWithDeadlock)
{
    InterruptTestCall call;
    st_thread_t trd = st_thread_create(join_self_coroutine, &call, 1, 0);
    ASSERT_TRUE(trd != NULL);

    void* retval = NULL;
    EXPECT_EQ(0, st_thread_join(trd, &retval));
    EXPECT_EQ(&call, retval);

    EXPECT_EQ(-1, call.r0_);
    EXPECT_EQ(EDEADLK, call.errno_);
}

// A worker coroutine that waits for work, then returns its result.
struct JoinTestWorker {
    st_cond_t work_;
    bool done_;
    int result_;
};

static void* join_worker_coroutine(void* arg)
{
    JoinTestWorker* w = (JoinTestWorker*)arg;
    st_cond_wait(w->work_);
    w->done_ = true;
    return &w->result_;
}

// A coroutine that stops the worker: it joins it and keeps what the join returned.
struct JoinTestJoiner {
    st_thread_t worker_;
    void* retval_;
    InterruptTestCall call_;
};

static void* join_joiner_coroutine(void* arg)
{
    JoinTestJoiner* j = (JoinTestJoiner*)arg;
    j->retval_ = NULL;
    join_test_join(j->call_, j->worker_, &j->retval_);
    return NULL;
}

// Two coroutines stop the same worker. The first joiner waits for the worker to exit; the second join fails at once
// with EINVAL, because a coroutine has only one joiner to hand its result to. The refused join does not disturb the
// first: when the worker exits, the first joiner gets its result. SRS never joins twice, because SrsFastCoroutine::stop
// returns early once the coroutine is disposed. Locks in current behavior.
VOID TEST(JoinTest, SecondJoinerIsRefused)
{
    JoinTestWorker w;
    w.work_ = st_cond_new();
    ASSERT_TRUE(w.work_ != NULL);
    w.done_ = false;
    st_thread_t worker = st_thread_create(join_worker_coroutine, &w, 1, 0);
    ASSERT_TRUE(worker != NULL);

    JoinTestJoiner j;
    j.worker_ = worker;
    st_thread_t joiner = st_thread_create(join_joiner_coroutine, &j, 1, 0);
    ASSERT_TRUE(joiner != NULL);

    // Let the worker wait for work, and the first joiner wait for the worker.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    InterruptTestCall second;
    join_test_join(second, worker, NULL);
    EXPECT_EQ(-1, second.r0_);
    EXPECT_EQ(EINVAL, second.errno_);

    EXPECT_EQ(0, st_cond_signal(w.work_));
    EXPECT_EQ(0, st_thread_join(joiner, NULL));

    EXPECT_TRUE(w.done_);
    EXPECT_EQ(0, j.call_.r0_);
    EXPECT_EQ(&w.result_, j.retval_);

    EXPECT_EQ(0, st_cond_destroy(w.work_));
}

// Coroutine A is stopping worker B, so A waits in the join, and then SRS stops A too. A's join fails at once with
// EINTR, and B is not stopped: it goes on until it exits, then waits for a join that nobody makes. A second join
// succeeds and takes B's result. A's interrupted join leaves no joiner behind, so the second join is not refused.
// SRS treats a failed join in SrsFastCoroutine::stop as fatal, so it never leaves a coroutine unjoined this way. Locks
// in current behavior.
VOID TEST(JoinTest, JoinerStoppedWhileWaiting)
{
    JoinTestWorker w;
    w.work_ = st_cond_new();
    ASSERT_TRUE(w.work_ != NULL);
    w.done_ = false;
    st_thread_t worker = st_thread_create(join_worker_coroutine, &w, 1, 0);
    ASSERT_TRUE(worker != NULL);

    JoinTestJoiner j;
    j.worker_ = worker;
    st_thread_t joiner = st_thread_create(join_joiner_coroutine, &j, 1, 0);
    ASSERT_TRUE(joiner != NULL);

    // Let the worker wait for work, and the joiner wait for the worker.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    st_thread_interrupt(joiner);
    EXPECT_EQ(0, st_thread_join(joiner, NULL));

    EXPECT_EQ(-1, j.call_.r0_);
    EXPECT_EQ(EINTR, j.call_.errno_);
    EXPECT_FALSE(w.done_);

    // The worker gets its work and exits, then waits as a zombie for the second join.
    EXPECT_EQ(0, st_cond_signal(w.work_));
    st_usleep(0);
    EXPECT_TRUE(w.done_);

    InterruptTestCall second;
    void* retval = NULL;
    join_test_join(second, worker, &retval);
    EXPECT_EQ(0, second.r0_);
    EXPECT_EQ(&w.result_, retval);

    EXPECT_EQ(0, st_cond_destroy(w.work_));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the timeout heap, the queue of every coroutine that waits with a timeout: a sleep, or a read, write,
// accept or condition wait with a timeout. SRS has one entry per connection waiting for its next packet. The scheduler
// wakes the coroutine with the earliest deadline first, whatever order the coroutines started waiting in. A coroutine
// that wakes early, because its I/O is ready or it is stopped, leaves the middle of the queue, and the others must
// still wake on time and in order.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A coroutine that waits with a timeout, and when, in which scheduler pass and in which order it woke.
struct TimeoutHeapTestWaiter {
    st_utime_t timeout_;
    int* woken_;
    int order_;
    st_utime_t woke_at_;
    st_utime_t woke_pass_;
    st_netfd_t stfd_;
    InterruptTestCall call_;
    TimeoutHeapTestWaiter() : timeout_(0), woken_(NULL), order_(-1), woke_at_(0), woke_pass_(0), stfd_(NULL) {
    }
};

static void timeout_heap_test_woke(TimeoutHeapTestWaiter* w)
{
    w->woke_at_ = st_utime();
    w->woke_pass_ = st_utime_last_clock();
    w->order_ = (*w->woken_)++;
}

static void* timeout_heap_sleeper_coroutine(void* arg)
{
    TimeoutHeapTestWaiter* w = (TimeoutHeapTestWaiter*)arg;
    errno = 0;
    w->call_.r0_ = st_usleep(w->timeout_);
    w->call_.errno_ = errno;
    timeout_heap_test_woke(w);
    return NULL;
}

// Let the created coroutines run until each one waits, and return a time that is no later than the start of every
// timeout. A wait counts its timeout from the scheduler's last clock reading, not from the call: st_thread_yield takes
// that reading, then runs the coroutines, so we read the clock just before it. A yield doesn't wait itself, so it adds
// nothing to the queue.
static st_utime_t timeout_heap_test_start_waiting()
{
    st_utime_t starttime = st_utime();
    st_thread_yield();
    return starttime;
}

// Waiter a has an earlier deadline than waiter b, so a never wakes in a later scheduler pass than b. When the program
// stalls past both deadlines, both expire in the same pass, and b runs first, because each coroutine whose timer
// expires goes to the head of the run queue.
static void timeout_heap_test_expect_order(const TimeoutHeapTestWaiter& a, const TimeoutHeapTestWaiter& b)
{
    EXPECT_LE(a.woke_pass_, b.woke_pass_) << "timeouts " << a.timeout_ << "us and " << b.timeout_ << "us";
    if (a.woke_pass_ == b.woke_pass_) {
        EXPECT_GT(a.order_, b.order_) << "timeouts " << a.timeout_ << "us and " << b.timeout_ << "us";
    } else {
        EXPECT_LT(a.order_, b.order_) << "timeouts " << a.timeout_ << "us and " << b.timeout_ << "us";
    }
}

// Coroutines sleep with deadlines given in no particular order, the way SRS timers and retries start at any time. Each
// one wakes after its own timeout, never before it, and they wake in deadline order. Locks in current behavior.
VOID TEST(TimeoutHeapTest, SleepersWakeInDeadlineOrder)
{
    // Timeouts in milliseconds, in the order the coroutines start sleeping.
    const int timeouts[] = {30, 70, 10, 50, 20, 60, 40};
    const int nn_waiters = (int)(sizeof(timeouts) / sizeof(timeouts[0]));

    int woken = 0;
    TimeoutHeapTestWaiter waiters[nn_waiters];
    st_thread_t trds[nn_waiters];
    for (int i = 0; i < nn_waiters; i++) {
        waiters[i].timeout_ = timeouts[i] * ST_UTIME_MILLISECONDS;
        waiters[i].woken_ = &woken;
        trds[i] = st_thread_create(timeout_heap_sleeper_coroutine, &waiters[i], 1, 0);
        ASSERT_TRUE(trds[i] != NULL);
    }

    st_utime_t starttime = timeout_heap_test_start_waiting();

    for (int i = 0; i < nn_waiters; i++) {
        EXPECT_EQ(0, st_thread_join(trds[i], NULL));
    }

    for (int i = 0; i < nn_waiters; i++) {
        EXPECT_EQ(0, waiters[i].call_.r0_);
        EXPECT_GE(waiters[i].woke_at_ - starttime, waiters[i].timeout_);
        for (int j = 0; j < nn_waiters; j++) {
            if (timeouts[i] < timeouts[j]) timeout_heap_test_expect_order(waiters[i], waiters[j]);
        }
    }
}

static void* timeout_heap_reader_coroutine(void* arg)
{
    TimeoutHeapTestWaiter* w = (TimeoutHeapTestWaiter*)arg;
    char buf[64];
    errno = 0;
    w->call_.r0_ = (int)st_read(w->stfd_, buf, sizeof(buf), w->timeout_);
    w->call_.errno_ = errno;
    timeout_heap_test_woke(w);
    return NULL;
}

// SRS accepts connections one after another and each waits for its next packet with the same receive timeout, so
// their deadlines follow the order they started waiting. One connection, in the middle of that queue, gets a packet
// first and wakes at once. Every other connection still times out with ETIME after its own timeout, never before it,
// and in deadline order. Locks in current behavior.
VOID TEST(TimeoutHeapTest, EarlyWakeKeepsOthersOnTime)
{
    const int nn_waiters = 7;
    const int early = 1;

    int fds[nn_waiters][2];
    st_netfd_t readers[nn_waiters];
    for (int i = 0; i < nn_waiters; i++) {
        ASSERT_EQ(0, st_utest_stream_pair(fds[i]));
        readers[i] = st_netfd_open_socket(fds[i][0]);
        ASSERT_TRUE(readers[i] != NULL);
    }

    int woken = 0;
    TimeoutHeapTestWaiter waiters[nn_waiters];
    st_thread_t trds[nn_waiters];
    for (int i = 0; i < nn_waiters; i++) {
        waiters[i].timeout_ = (i + 2) * 10 * ST_UTIME_MILLISECONDS;
        waiters[i].woken_ = &woken;
        waiters[i].stfd_ = readers[i];
        trds[i] = st_thread_create(timeout_heap_reader_coroutine, &waiters[i], 1, 0);
        ASSERT_TRUE(trds[i] != NULL);
    }

    st_utime_t starttime = timeout_heap_test_start_waiting();

    ASSERT_EQ(5, st_utest_send(fds[early][1], "hello", 5));

    for (int i = 0; i < nn_waiters; i++) {
        EXPECT_EQ(0, st_thread_join(trds[i], NULL));
    }

    EXPECT_EQ(5, waiters[early].call_.r0_);
    EXPECT_LT(waiters[early].woke_at_ - starttime, waiters[early].timeout_);
    EXPECT_EQ(0, waiters[early].order_);

    for (int i = 0; i < nn_waiters; i++) {
        if (i == early) continue;
        EXPECT_EQ(-1, waiters[i].call_.r0_);
        EXPECT_EQ(ETIME, waiters[i].call_.errno_);
        EXPECT_GE(waiters[i].woke_at_ - starttime, waiters[i].timeout_);
        for (int j = i + 1; j < nn_waiters; j++) {
            if (j != early) timeout_heap_test_expect_order(waiters[i], waiters[j]);
        }
    }

    for (int i = 0; i < nn_waiters; i++) {
        st_netfd_close(readers[i]);
        st_utest_close(fds[i][1]);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the switch callbacks, st_set_switch_in_cb and st_set_switch_out_cb: hooks that run on every switch into
// and out of a coroutine, in that coroutine, so st_thread_self tells which one it is. A profiler uses them to measure
// the CPU time of each coroutine, and a tracer to keep per-coroutine state such as the current trace ID in a global
// that follows the running coroutine. The idle coroutine, which waits for I/O and timers, is never reported, so the gap
// between a switch-out and the next switch-in is idle time. SRS doesn't use them.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One switch the callbacks saw: in or out, and the coroutine switched.
struct SwitchCbTestEvent {
    bool in_;
    st_thread_t trd_;
    SwitchCbTestEvent(bool in, st_thread_t trd) : in_(in), trd_(trd) {
    }
    bool operator==(const SwitchCbTestEvent& o) const {
        return in_ == o.in_ && trd_ == o.trd_;
    }
};

static std::ostream& operator<<(std::ostream& out, const SwitchCbTestEvent& e)
{
    return out << (e.in_ ? "in(" : "out(") << (void*)e.trd_ << ")";
}

static std::vector<SwitchCbTestEvent> switch_cb_test_events;

static void switch_cb_test_in()
{
    switch_cb_test_events.push_back(SwitchCbTestEvent(true, st_thread_self()));
}

static void switch_cb_test_out()
{
    switch_cb_test_events.push_back(SwitchCbTestEvent(false, st_thread_self()));
}

// Install the callbacks for a scope, and restore the previous ones when it ends, so no other test sees them.
class SwitchCbTestInstall {
    st_switch_cb_t in_;
    st_switch_cb_t out_;
public:
    SwitchCbTestInstall(st_switch_cb_t in, st_switch_cb_t out) {
        in_ = st_set_switch_in_cb(in);
        out_ = st_set_switch_out_cb(out);
    }
    virtual ~SwitchCbTestInstall() {
        st_set_switch_in_cb(in_);
        st_set_switch_out_cb(out_);
    }
};

// A coroutine that waits for work on a condition variable, until it is told to quit.
struct SwitchCbTestWorker {
    st_cond_t work_;
    bool quit_;
    int nn_works_;
    SwitchCbTestWorker() : quit_(false), nn_works_(0) {
        work_ = st_cond_new();
    }
    virtual ~SwitchCbTestWorker() {
        st_cond_destroy(work_);
    }
};

static void* switch_cb_test_worker_coroutine(void* arg)
{
    SwitchCbTestWorker* w = (SwitchCbTestWorker*)arg;
    while (true) {
        st_cond_wait(w->work_);
        if (w->quit_) break;
        w->nn_works_++;
    }
    return NULL;
}

static void switch_cb_test_quit(SwitchCbTestWorker& w, st_thread_t trd)
{
    w.quit_ = true;
    st_cond_signal(w.work_);
    if (trd) st_thread_join(trd, NULL);
}

// The callbacks are off by default. Setting one returns the previous one, so a library can chain to it or restore it,
// and setting NULL turns it off again, after which a switch calls nothing. Locks in current behavior.
VOID TEST(SwitchCbTest, SetReturnsPreviousCallback)
{
    EXPECT_TRUE(st_set_switch_in_cb(switch_cb_test_in) == NULL);
    EXPECT_TRUE(st_set_switch_out_cb(switch_cb_test_out) == NULL);

    EXPECT_TRUE(st_set_switch_in_cb(switch_cb_test_out) == switch_cb_test_in);
    EXPECT_TRUE(st_set_switch_out_cb(switch_cb_test_in) == switch_cb_test_out);

    EXPECT_TRUE(st_set_switch_in_cb(NULL) == switch_cb_test_out);
    EXPECT_TRUE(st_set_switch_out_cb(NULL) == switch_cb_test_in);

    SwitchCbTestWorker w;
    st_thread_t trd = st_thread_create(switch_cb_test_worker_coroutine, &w, 1, 0);
    ASSERT_TRUE(trd != NULL);
    // Let the worker start and wait for work.
    st_thread_yield();

    switch_cb_test_events.clear();
    st_cond_signal(w.work_);
    st_usleep(1 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(1, w.nn_works_);
    EXPECT_TRUE(switch_cb_test_events.empty());

    switch_cb_test_quit(w, trd);
}

// A profiler sees each slice a coroutine runs: the main coroutine sleeps, the worker it gave work to is switched in,
// does it, waits for more and is switched out, then the main coroutine is switched in when its sleep ends. Both
// callbacks run in the coroutine being switched. The idle coroutine waits for the timer in between and is not
// reported. Locks in current behavior.
VOID TEST(SwitchCbTest, ProfilerSeesEachSlice)
{
    SwitchCbTestWorker w;
    st_thread_t trd = st_thread_create(switch_cb_test_worker_coroutine, &w, 1, 0);
    ASSERT_TRUE(trd != NULL);
    // Let the worker start and wait for work.
    st_thread_yield();

    st_thread_t me = st_thread_self();
    switch_cb_test_events.clear();
    if (true) {
        SwitchCbTestInstall install(switch_cb_test_in, switch_cb_test_out);
        st_cond_signal(w.work_);
        st_usleep(1 * ST_UTIME_MILLISECONDS);
    }
    EXPECT_EQ(1, w.nn_works_);

    std::vector<SwitchCbTestEvent> expected;
    expected.push_back(SwitchCbTestEvent(false, me));
    expected.push_back(SwitchCbTestEvent(true, trd));
    expected.push_back(SwitchCbTestEvent(false, trd));
    expected.push_back(SwitchCbTestEvent(true, me));
    EXPECT_EQ(expected, switch_cb_test_events);

    switch_cb_test_quit(w, trd);
}

// The trace ID of the running coroutine, kept in a global for fast access, and saved per coroutine while it is
// switched out.
static int switch_cb_test_trace_id = 0;
static std::map<st_thread_t, int> switch_cb_test_trace_ids;

static void switch_cb_test_trace_in()
{
    switch_cb_test_trace_id = switch_cb_test_trace_ids[st_thread_self()];
}

static void switch_cb_test_trace_out()
{
    switch_cb_test_trace_ids[st_thread_self()] = switch_cb_test_trace_id;
}

struct SwitchCbTestTracer {
    int id_;
    int nn_wrong_;
    SwitchCbTestTracer(int id) : id_(id), nn_wrong_(0) {
    }
};

static void* switch_cb_test_tracer_coroutine(void* arg)
{
    SwitchCbTestTracer* t = (SwitchCbTestTracer*)arg;
    switch_cb_test_trace_id = t->id_;
    for (int i = 0; i < 3; i++) {
        st_thread_yield();
        if (switch_cb_test_trace_id != t->id_) t->nn_wrong_++;
    }
    return NULL;
}

// A tracer keeps the trace ID of the running coroutine in one global, the way a logger reads the current context ID on
// every line. The out callback saves the global for the coroutine leaving, and the in callback restores the one of the
// coroutine coming back, so two coroutines that take turns each keep their own ID, and the main coroutine gets its ID
// back after joining them. Locks in current behavior.
VOID TEST(SwitchCbTest, TraceIdFollowsCoroutine)
{
    switch_cb_test_trace_ids.clear();
    SwitchCbTestTracer t1(1), t2(2);
    if (true) {
        SwitchCbTestInstall install(switch_cb_test_trace_in, switch_cb_test_trace_out);
        switch_cb_test_trace_id = 100;

        st_thread_t trd1 = st_thread_create(switch_cb_test_tracer_coroutine, &t1, 1, 0);
        ASSERT_TRUE(trd1 != NULL);
        st_thread_t trd2 = st_thread_create(switch_cb_test_tracer_coroutine, &t2, 1, 0);
        ASSERT_TRUE(trd2 != NULL);

        EXPECT_EQ(0, st_thread_join(trd1, NULL));
        EXPECT_EQ(0, st_thread_join(trd2, NULL));
        EXPECT_EQ(100, switch_cb_test_trace_id);
    }
    EXPECT_EQ(0, t1.nn_wrong_);
    EXPECT_EQ(0, t2.nn_wrong_);
}

static void* switch_cb_test_sleeper_coroutine(void* arg)
{
    st_usleep(1 * ST_UTIME_MILLISECONDS);
    return NULL;
}

// A coroutine created while the callbacks are on is switched in when it first runs, so a profiler starts its first
// slice and a tracer gives it its own state, instead of leaving it with the state of the coroutine that ran before.
// The documentation says that after the callbacks are set, any thread switch calls them.
VOID TEST(SwitchCbTest, NewCoroutineIsSwitchedIn)
{
    // Let the coroutines joined by other tests free their resources first.
    st_thread_yield();

    st_thread_t me = st_thread_self();
    st_thread_t trd = NULL;
    switch_cb_test_events.clear();
    if (true) {
        SwitchCbTestInstall install(switch_cb_test_in, switch_cb_test_out);
        trd = st_thread_create(switch_cb_test_sleeper_coroutine, NULL, 1, 0);
        ASSERT_TRUE(trd != NULL);
        // The new coroutine runs first, then sleeps.
        st_thread_yield();
    }

    std::vector<SwitchCbTestEvent> expected;
    expected.push_back(SwitchCbTestEvent(false, me));
    expected.push_back(SwitchCbTestEvent(true, trd));
    expected.push_back(SwitchCbTestEvent(false, trd));
    expected.push_back(SwitchCbTestEvent(true, me));
    EXPECT_EQ(expected, switch_cb_test_events);

    EXPECT_EQ(0, st_thread_join(trd, NULL));
}

// A coroutine that exits is switched out, so a profiler ends its last slice and every switch-in has a switch-out. A
// detached one is switched out after it exits. A joinable one is switched out when it waits as a zombie for its
// joiner. When it is joined, it is switched in once more to free its resources, then switched out for good. The
// documentation says that after the callbacks are set, any thread switch calls them.
VOID TEST(SwitchCbTest, ExitingCoroutineSwitchedOut)
{
    st_thread_t me = st_thread_self();

    SwitchCbTestWorker detached;
    st_thread_t dtrd = st_thread_create(switch_cb_test_worker_coroutine, &detached, 0, 0);
    ASSERT_TRUE(dtrd != NULL);
    SwitchCbTestWorker joinable;
    st_thread_t jtrd = st_thread_create(switch_cb_test_worker_coroutine, &joinable, 1, 0);
    ASSERT_TRUE(jtrd != NULL);
    // Let both start and wait for work.
    st_thread_yield();

    std::vector<SwitchCbTestEvent> expected;
    switch_cb_test_events.clear();
    if (true) {
        SwitchCbTestInstall install(switch_cb_test_in, switch_cb_test_out);

        switch_cb_test_quit(detached, NULL);
        st_thread_yield();
        expected.push_back(SwitchCbTestEvent(false, me));
        expected.push_back(SwitchCbTestEvent(true, dtrd));
        expected.push_back(SwitchCbTestEvent(false, dtrd));
        expected.push_back(SwitchCbTestEvent(true, me));

        switch_cb_test_quit(joinable, jtrd);
        expected.push_back(SwitchCbTestEvent(false, me));
        expected.push_back(SwitchCbTestEvent(true, jtrd));
        expected.push_back(SwitchCbTestEvent(false, jtrd));
        expected.push_back(SwitchCbTestEvent(true, me));

        // The joined coroutine runs once more to free its resources.
        st_thread_yield();
        expected.push_back(SwitchCbTestEvent(false, me));
        expected.push_back(SwitchCbTestEvent(true, jtrd));
        expected.push_back(SwitchCbTestEvent(false, jtrd));
        expected.push_back(SwitchCbTestEvent(true, me));
    }
    EXPECT_EQ(expected, switch_cb_test_events);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for waiting on several descriptors at once with st_poll, such as a worker that waits for either a job or
// a stop request, whichever comes first. The I/O calls wait on one descriptor through st_poll; a program calls it
// directly to wait on more than one.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// What one st_poll returned, with the revents of each descriptor.
struct PollTestCall {
    int r0_;
    int errno_;
    short revents_[2];
    st_utime_t elapsed_;
    PollTestCall() : r0_(0), errno_(0), elapsed_(0) {
        revents_[0] = revents_[1] = 0;
    }
};

struct PollTestWorker {
    StUtestPair* jobs_;
    StUtestPair* stop_;
    std::vector<PollTestCall> calls_;
    std::vector<char> done_;
};

static void* poll_test_worker_coroutine(void* arg)
{
    PollTestWorker* w = (PollTestWorker*)arg;

    // One array for the whole loop, so the revents of the last pass are still in it.
    struct pollfd pds[2];
    pds[0].fd = st_netfd_fileno(w->jobs_->stfd_);
    pds[0].events = POLLIN;
    pds[1].fd = st_netfd_fileno(w->stop_->stfd_);
    pds[1].events = POLLIN;

    for (;;) {
        PollTestCall call;
        errno = 0;
        call.r0_ = st_poll(pds, 2, ST_UTIME_NO_TIMEOUT);
        call.errno_ = errno;
        call.revents_[0] = pds[0].revents;
        call.revents_[1] = pds[1].revents;
        w->calls_.push_back(call);

        if (call.r0_ <= 0 || (pds[1].revents & POLLIN)) {
            break;
        }

        char job = 0;
        if (st_utest_recv(pds[0].fd, &job, 1) == 1) {
            w->done_.push_back(job);
        }
    }
    return NULL;
}

// A worker waits, with no timeout, for a job or a stop request, whichever comes first, and keeps one pollfd array for
// its whole loop. Each st_poll returns how many descriptors are ready and sets revents on every descriptor in the
// array: the one that fired gets POLLIN, and the quiet one gets 0, so the job that woke the last pass is not reported
// again when the stop request wakes the next one. Locks in current behavior.
VOID TEST(PollTest, WorkerWakesOnJobOrStop)
{
    StUtestPair jobs, stop;
    ASSERT_TRUE(st_utest_pair_open(jobs));
    ASSERT_TRUE(st_utest_pair_open(stop));

    PollTestWorker w;
    w.jobs_ = &jobs;
    w.stop_ = &stop;
    st_thread_t trd = st_thread_create(poll_test_worker_coroutine, &w, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // The worker waits for something to do.
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(0, (int)w.calls_.size());

    // A job arrives; the worker does it and waits again.
    ASSERT_EQ(1, st_utest_send(jobs.peer_, "a", 1));
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    ASSERT_EQ(1, (int)w.calls_.size());
    EXPECT_EQ(1, w.calls_[0].r0_);
    EXPECT_EQ(POLLIN, w.calls_[0].revents_[0]);
    EXPECT_EQ(0, w.calls_[0].revents_[1]);
    ASSERT_EQ(1, (int)w.done_.size());
    EXPECT_EQ('a', w.done_[0]);

    // A stop request arrives; the worker sees no job and quits.
    ASSERT_EQ(1, st_utest_send(stop.peer_, "q", 1));
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    ASSERT_EQ(2, (int)w.calls_.size());
    EXPECT_EQ(1, w.calls_[1].r0_);
    EXPECT_EQ(0, w.calls_[1].revents_[0]);
    EXPECT_EQ(POLLIN, w.calls_[1].revents_[1]);
    EXPECT_EQ(1, (int)w.done_.size());
}

// A relay waits until it can read from one side or write to the other. When both are already ready, st_poll returns
// at once and reports both: 2, with POLLIN on the read side and POLLOUT on the write side. Locks in current behavior.
VOID TEST(PollTest, ReportsEveryReadyDescriptor)
{
    StUtestPair in, out;
    ASSERT_TRUE(st_utest_pair_open(in));
    ASSERT_TRUE(st_utest_pair_open(out));

    // Data waits on the read side, and the write side has room.
    ASSERT_EQ(1, st_utest_send(in.peer_, "a", 1));

    struct pollfd pds[2];
    pds[0].fd = st_netfd_fileno(in.stfd_);
    pds[0].events = POLLIN;
    pds[1].fd = out.peer_;
    pds[1].events = POLLOUT;

    st_utime_t starttime = st_utime();
    EXPECT_EQ(2, st_poll(pds, 2, ST_UTEST_TIMEOUT));
    EXPECT_LT(st_utime() - starttime, ST_UTEST_TIMEOUT / 2);
    EXPECT_EQ(POLLIN, pds[0].revents);
    EXPECT_EQ(POLLOUT, pds[1].revents);
}

// When nothing becomes ready before the timeout, st_poll returns 0 and leaves errno alone, the same as poll(2). The I/O
// calls built on it, such as st_read, turn that 0 into -1 with ETIME. Locks in current behavior.
VOID TEST(PollTest, TimeoutReturnsZero)
{
    StUtestPair a, b;
    ASSERT_TRUE(st_utest_pair_open(a));
    ASSERT_TRUE(st_utest_pair_open(b));

    struct pollfd pds[2];
    pds[0].fd = st_netfd_fileno(a.stfd_);
    pds[0].events = POLLIN;
    pds[1].fd = st_netfd_fileno(b.stfd_);
    pds[1].events = POLLIN;

    // The timeout counts from the clock reading this yield takes.
    st_utime_t starttime = st_utime();
    st_thread_yield();
    errno = 0;
    EXPECT_EQ(0, st_poll(pds, 2, 10 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(0, errno);
    EXPECT_GE(st_utime() - starttime, 10 * ST_UTIME_MILLISECONDS);

    // Nothing is left registered, so both descriptors close.
    EXPECT_EQ(0, st_netfd_close(a.stfd_));
    a.stfd_ = NULL;
    EXPECT_EQ(0, st_netfd_close(b.stfd_));
    b.stfd_ = NULL;
}

static void poll_test_rejected(st_netfd_t valid, int fd, short events)
{
    struct pollfd pds[2];
    pds[0].fd = st_netfd_fileno(valid);
    pds[0].events = POLLIN;
    pds[1].fd = fd;
    pds[1].events = events;

    st_utime_t starttime = st_utime();
    errno = 0;
    EXPECT_EQ(-1, st_poll(pds, 2, ST_UTEST_TIMEOUT));
    EXPECT_EQ(EINVAL, errno);
    EXPECT_LT(st_utime() - starttime, ST_UTEST_TIMEOUT / 2);
}

// st_poll refuses a pollset it can't wait on, at once and as a whole, with EINVAL: a negative descriptor, which poll(2)
// would skip; no events; or an event other than POLLIN or POLLOUT, such as POLLRDNORM from a program written for
// poll(2). The valid descriptor earlier in the same set is not left registered, so it still closes instead of failing
// with EBUSY as if a coroutine still waited on it. Locks in current behavior.
VOID TEST(PollTest, InvalidPollsetFailsAtOnce)
{
    StUtestPair valid, other;
    ASSERT_TRUE(st_utest_pair_open(valid));
    ASSERT_TRUE(st_utest_pair_open(other));

    poll_test_rejected(valid.stfd_, -1, POLLIN);
    poll_test_rejected(valid.stfd_, st_netfd_fileno(other.stfd_), 0);
    poll_test_rejected(valid.stfd_, st_netfd_fileno(other.stfd_), POLLRDNORM);

    EXPECT_EQ(0, st_netfd_close(valid.stfd_));
    valid.stfd_ = NULL;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for calling st_init again on an OS thread that already runs ST, such as library code that initializes ST
// in case its caller hasn't. The second call returns 0 at once and changes nothing: the calling coroutine, the event
// system and every waiting coroutine stay as they were.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct InitTestLibrary {
    st_thread_t self_;
    int r0_;
    int errno_;
    st_thread_t self_after_;
    int eventsys_after_;
    InitTestLibrary() : self_(NULL), r0_(-1), errno_(0), self_after_(NULL), eventsys_after_(-1) {
    }
};

static void* init_test_library_coroutine(void* arg)
{
    InitTestLibrary* lib = (InitTestLibrary*)arg;
    lib->self_ = st_thread_self();

    errno = 0;
    lib->r0_ = st_init();
    lib->errno_ = errno;
    lib->self_after_ = st_thread_self();
    lib->eventsys_after_ = st_get_eventsys();

    // Still a normal coroutine: it can wait and be joined.
    st_usleep(1 * ST_UTIME_MILLISECONDS);
    return arg;
}

// A library started from a coroutine calls st_init to be safe. It succeeds at once without touching errno, and the
// coroutine stays itself, with the same event system, so it goes on waiting and is joined as before. Locks in current
// behavior.
VOID TEST(InitTest, SecondInitFromCoroutineIsNoOp)
{
    int eventsys = st_get_eventsys();

    InitTestLibrary lib;
    st_thread_t trd = st_thread_create(init_test_library_coroutine, &lib, 1, 0);
    ASSERT_TRUE(trd != NULL);

    void* retval = NULL;
    EXPECT_EQ(0, st_thread_join(trd, &retval));
    EXPECT_EQ(&lib, retval);

    EXPECT_EQ(0, lib.r0_);
    EXPECT_EQ(0, lib.errno_);
    EXPECT_EQ(trd, lib.self_);
    EXPECT_EQ(trd, lib.self_after_);
    EXPECT_EQ(eventsys, lib.eventsys_after_);
}

struct InitTestReader {
    StUtestPair* pair_;
    bool returned_;
    ssize_t nread_;
    char data_;
    InitTestReader() : pair_(NULL), returned_(false), nread_(-1), data_(0) {
    }
};

static void* init_test_reader_coroutine(void* arg)
{
    InitTestReader* r = (InitTestReader*)arg;
    r->nread_ = st_read(r->pair_->stfd_, &r->data_, 1, ST_UTEST_TIMEOUT);
    r->returned_ = true;
    return NULL;
}

static void* init_test_sleeper_coroutine(void* arg)
{
    st_usleep(10 * ST_UTIME_MILLISECONDS);
    return arg;
}

// A server already has a coroutine asleep on a timer and another blocked in a read when a library calls st_init. The
// second call leaves the scheduler's queues alone, so the sleeper still wakes on its timer and the reader still wakes
// when its data arrives. Locks in current behavior.
VOID TEST(InitTest, SecondInitKeepsWaitingCoroutines)
{
    StUtestPair p;
    ASSERT_TRUE(st_utest_pair_open(p));

    InitTestReader r;
    r.pair_ = &p;
    st_thread_t reader = st_thread_create(init_test_reader_coroutine, &r, 1, 0);
    ASSERT_TRUE(reader != NULL);

    // The sleep counts from the clock reading this yield takes.
    int marker = 0;
    st_thread_t sleeper = st_thread_create(init_test_sleeper_coroutine, &marker, 1, 0);
    ASSERT_TRUE(sleeper != NULL);
    st_utime_t starttime = st_utime();
    st_thread_yield();

    st_thread_t me = st_thread_self();
    EXPECT_EQ(0, st_init());
    EXPECT_EQ(me, st_thread_self());

    // The sleeper wakes on its timer, no earlier.
    void* retval = NULL;
    EXPECT_EQ(0, st_thread_join(sleeper, &retval));
    EXPECT_EQ(&marker, retval);
    EXPECT_GE(st_utime() - starttime, 10 * ST_UTIME_MILLISECONDS);

    // The reader is still waiting, and wakes on its data.
    EXPECT_FALSE(r.returned_);
    ASSERT_EQ(1, (int)st_utest_send(p.peer_, "x", 1));
    EXPECT_EQ(0, st_thread_join(reader, NULL));
    EXPECT_EQ(1, r.nread_);
    EXPECT_EQ('x', r.data_);
}

#ifndef _WIN32 // POSIX only: fork, pipes and pthread
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for a program that ends when its last coroutine ends, the classic ST server: main starts the workers, then
// calls st_thread_exit instead of returning, and the process exits with status 0 once the last coroutine terminates.
// The exit ends the process, so each test runs the program on a new OS thread with its own ST in a forked child, and
// reads the child's exit status and what it printed.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__SANITIZE_ADDRESS__)
#define EXIT_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define EXIT_TEST_ASAN 1
#endif
#endif

#ifdef EXIT_TEST_ASAN
#include <sanitizer/lsan_interface.h>
#endif

static void* exit_test_thread(void* arg)
{
    void (*program)() = (void (*)())arg;

#ifdef EXIT_TEST_ASAN
    // ST never frees the primordial thread that st_init allocates, and the program's main exits it with st_thread_exit,
    // so nothing points to it when ST ends the process with exit(). LeakSanitizer would report it and fail the exit
    // status the tests check.
    __lsan_disable();
#endif

    if (st_set_eventsys(ST_EVENTSYS_ALT) < 0) _exit(1);
    if (st_init() < 0) _exit(1);

    // The program's main ends with st_thread_exit, so it never returns here.
    program();
    _exit(2);
    return NULL;
}

// Runs program as the main of a new OS thread in a forked child, and returns the child's exit status, or -1 if it
// didn't exit, with what the child printed to stdout.
static int exit_test_run(void (*program)(), std::string& out)
{
    fflush(stdout);
    fflush(stderr);

    int fds[2];
    if (pipe(fds) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // A hang kills the child and fails the test, instead of hanging the suite.
        alarm(5);

        // The printed lines go to the parent, and stay buffered in the child until it flushes or exits.
        ::close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(1);
        ::close(fds[1]);

        pthread_t trd;
        if (pthread_create(&trd, NULL, exit_test_thread, (void*)program) != 0) _exit(1);

        // Only the program's exit ends the child.
        pthread_join(trd, NULL);
        _exit(3);
    }

    ::close(fds[1]);
    char buf[256];
    ssize_t nn;
    while ((nn = ::read(fds[0], buf, sizeof(buf))) > 0) {
        out.append(buf, nn);
    }
    ::close(fds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void* exit_test_worker_coroutine(void* arg)
{
    int id = (int)(long)arg;
    st_usleep(id * 10 * ST_UTIME_MILLISECONDS);
    printf("served %d\n", id);
    return NULL;
}

static void exit_test_server()
{
    for (int id = 1; id <= 3; id++) {
        if (!st_thread_create(exit_test_worker_coroutine, (void*)(long)id, 0, 0)) _exit(1);
    }
    st_thread_exit(NULL);
}

// A server's main starts three workers, which serve for 10, 20 and 30 ms, then calls st_thread_exit. Main never runs
// again, the workers go on, and the process exits with status 0 after the last one. Because ST exits with exit(), not
// _exit(), the lines the workers printed reach stdout. Locks in current behavior.
VOID TEST(ExitTest, ProcessExitsAfterLastWorker)
{
    std::string out;
    EXPECT_EQ(0, exit_test_run(exit_test_server, out));
    EXPECT_EQ("served 1\nserved 2\nserved 3\n", out);
}

static void* exit_test_finished_coroutine(void* arg)
{
    printf("finished\n");
    return NULL;
}

static void exit_test_unjoined()
{
    if (!st_thread_create(exit_test_finished_coroutine, NULL, 1, 0)) _exit(1);
    if (!st_thread_create(exit_test_worker_coroutine, (void*)(long)1, 0, 0)) _exit(1);
    st_thread_exit(NULL);
}

// Main starts a joinable coroutine that finishes at once, and a worker that serves for 10 ms, then exits without
// joining the first. The finished coroutine waits as a zombie for a join that never comes, but it has terminated, so
// the process exits with status 0 when the worker ends. Locks in current behavior.
VOID TEST(ExitTest, UnjoinedCoroutineDoesNotKeepProcessAlive)
{
    std::string out;
    EXPECT_EQ(0, exit_test_run(exit_test_unjoined, out));
    EXPECT_EQ("finished\nserved 1\n", out);
}

static void* exit_test_reader_coroutine(void* arg)
{
    st_netfd_t stfd = (st_netfd_t)arg;
    char data = 0;
    if (st_read(stfd, &data, 1, ST_UTIME_NO_TIMEOUT) != 1) return NULL;
    printf("got %c\n", data);
    return NULL;
}

static void* exit_test_client_thread(void* arg)
{
    int fd = (int)(long)arg;
    usleep(20 * 1000);
    if (::write(fd, "a", 1) != 1) _exit(1);
    return NULL;
}

static void exit_test_waiting_reader()
{
    int fds[2];
    if (pipe(fds) < 0) _exit(1);
    st_netfd_t stfd = st_netfd_open(fds[0]);
    if (!stfd) _exit(1);
    if (!st_thread_create(exit_test_reader_coroutine, stfd, 0, 0)) _exit(1);

    // The request comes from another OS thread, so no coroutine is on a timer while the reader waits.
    pthread_t client;
    if (pthread_create(&client, NULL, exit_test_client_thread, (void*)(long)fds[1]) != 0) _exit(1);

    st_thread_exit(NULL);
}

// Main starts a reader that waits for a request with no timeout, then exits. With no coroutine runnable or on a timer,
// ST waits for I/O with no timeout instead of exiting, since the reader hasn't terminated. The request arrives 20 ms
// later from another OS thread, the reader prints it and ends, and the process exits with status 0. Locks in current
// behavior.
VOID TEST(ExitTest, WaitingReaderKeepsProcessAlive)
{
    std::string out;
    EXPECT_EQ(0, exit_test_run(exit_test_waiting_reader, out));
    EXPECT_EQ("got a\n", out);
}
#endif
