/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#include <new>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/wait.h>
#endif

#define ST_UTIME_MILLISECONDS 1000

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the mutex, the way SRS guards a critical section that may switch coroutines: SrsLocker locks a mutex
// for a scope and asserts the lock succeeds, such as the source managers guarding their pool while a source is created
// (srs#1230), and srs_mutex_destroy asserts the destroy succeeds. Only the owner can unlock, and unlocking hands the
// mutex straight to the first coroutine still waiting for it. A mutex that is held or has waiters can't be destroyed.
// st_mutex_trylock takes a free mutex without ever waiting.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// What one mutex call returned.
struct MutexTestCall {
    int r0_;
    int errno_;
    MutexTestCall() : r0_(0), errno_(0) {
    }
};

static void mutex_test_trylock(MutexTestCall& call, st_mutex_t lock)
{
    errno = 0;
    call.r0_ = st_mutex_trylock(lock);
    call.errno_ = errno;
}

static void mutex_test_unlock(MutexTestCall& call, st_mutex_t lock)
{
    errno = 0;
    call.r0_ = st_mutex_unlock(lock);
    call.errno_ = errno;
}

// A coroutine that waits for the mutex. When it gets it, it works until it is given the work_ signal, if any, then
// unlocks.
struct MutexTestWaiter {
    st_mutex_t lock_;
    st_cond_t work_;
    MutexTestCall lock_call_;
    bool owned_;
    MutexTestCall unlock_call_;
    MutexTestWaiter(st_mutex_t lock) : lock_(lock), work_(NULL), owned_(false) {
    }
};

static void* mutex_waiter_coroutine(void* arg)
{
    MutexTestWaiter* w = (MutexTestWaiter*)arg;

    errno = 0;
    w->lock_call_.r0_ = st_mutex_lock(w->lock_);
    w->lock_call_.errno_ = errno;
    if (w->lock_call_.r0_ != 0) return NULL;

    w->owned_ = true;
    if (w->work_) st_cond_wait(w->work_);
    mutex_test_unlock(w->unlock_call_, w->lock_);
    return NULL;
}

static void* mutex_trylock_coroutine(void* arg)
{
    MutexTestWaiter* w = (MutexTestWaiter*)arg;

    mutex_test_trylock(w->lock_call_, w->lock_);
    if (w->lock_call_.r0_ != 0) return NULL;

    w->owned_ = true;
    mutex_test_unlock(w->unlock_call_, w->lock_);
    return NULL;
}

// A coroutine that has other work to do takes the mutex only if it is free, such as a timer that skips a refresh while
// another coroutine is doing it. st_mutex_trylock takes a free mutex, and fails at once with EBUSY when the mutex is
// held, by another coroutine or by the caller itself, where st_mutex_lock would wait or fail with EDEADLK. It never
// waits, so it never yields to another coroutine. No SRS caller. Locks in current behavior.
VOID TEST(MutexTest, TrylockTakesOnlyFreeMutex)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);

    MutexTestCall first;
    mutex_test_trylock(first, lock);
    EXPECT_EQ(0, first.r0_);

    MutexTestCall again;
    mutex_test_trylock(again, lock);
    EXPECT_EQ(-1, again.r0_);
    EXPECT_EQ(EBUSY, again.errno_);

    // Another coroutine tries while we hold it, and gives up at once.
    MutexTestWaiter busy(lock);
    st_thread_t trd = st_thread_create(mutex_trylock_coroutine, &busy, 1, 0);
    ASSERT_TRUE(trd != NULL);
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(-1, busy.lock_call_.r0_);
    EXPECT_EQ(EBUSY, busy.lock_call_.errno_);
    EXPECT_FALSE(busy.owned_);

    EXPECT_EQ(0, st_mutex_unlock(lock));

    // Once it is free, the other coroutine takes it.
    MutexTestWaiter later(lock);
    trd = st_thread_create(mutex_trylock_coroutine, &later, 1, 0);
    ASSERT_TRUE(trd != NULL);
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(0, later.lock_call_.r0_);
    EXPECT_TRUE(later.owned_);
    EXPECT_EQ(0, later.unlock_call_.r0_);

    EXPECT_EQ(0, st_mutex_destroy(lock));
}

struct MutexTestStopped {
    st_mutex_t lock_;
    MutexTestCall trylock_call_;
    int sleep_r0_;
    int sleep_errno_;
};

static void* mutex_stopped_trylock_coroutine(void* arg)
{
    MutexTestStopped* s = (MutexTestStopped*)arg;

    st_thread_interrupt(st_thread_self());

    mutex_test_trylock(s->trylock_call_, s->lock_);

    errno = 0;
    s->sleep_r0_ = st_usleep(1 * ST_UTIME_MILLISECONDS);
    s->sleep_errno_ = errno;

    if (s->trylock_call_.r0_ == 0) st_mutex_unlock(s->lock_);
    return NULL;
}

// A coroutine that has been stopped still takes a free mutex with st_mutex_trylock, unlike st_mutex_lock, which fails
// with EINTR. The interrupt is checked only by calls that can wait, and trylock never waits, so the interrupt stays
// pending: the next sleep fails at once with EINTR. Locks in current behavior.
VOID TEST(MutexTest, TrylockIgnoresPendingStop)
{
    MutexTestStopped s;
    s.lock_ = st_mutex_new();
    ASSERT_TRUE(s.lock_ != NULL);
    s.sleep_r0_ = 0;
    s.sleep_errno_ = 0;

    st_thread_t trd = st_thread_create(mutex_stopped_trylock_coroutine, &s, 1, 0);
    ASSERT_TRUE(trd != NULL);
    EXPECT_EQ(0, st_thread_join(trd, NULL));

    EXPECT_EQ(0, s.trylock_call_.r0_);

    EXPECT_EQ(-1, s.sleep_r0_);
    EXPECT_EQ(EINTR, s.sleep_errno_);

    EXPECT_EQ(0, st_mutex_destroy(s.lock_));
}

// A bug unlocks a mutex it doesn't hold: a free mutex, a mutex the caller already handed to a waiter by unlocking it
// once, or a mutex another coroutine is holding. Every such unlock fails with EPERM and changes nothing, so it can't
// take the mutex from the coroutine that owns it, and that owner still unlocks it normally. Locks in current behavior.
VOID TEST(MutexTest, UnlockByNonOwnerFails)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);
    st_cond_t work = st_cond_new();
    ASSERT_TRUE(work != NULL);

    MutexTestCall unlock_free;
    mutex_test_unlock(unlock_free, lock);
    EXPECT_EQ(-1, unlock_free.r0_);
    EXPECT_EQ(EPERM, unlock_free.errno_);

    ASSERT_EQ(0, st_mutex_lock(lock));

    MutexTestWaiter w(lock);
    w.work_ = work;
    st_thread_t trd = st_thread_create(mutex_waiter_coroutine, &w, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let the waiter block on the mutex we hold.
    st_usleep(1 * ST_UTIME_MILLISECONDS);
    EXPECT_FALSE(w.owned_);

    // The first unlock hands the mutex to the waiter, so the second one is not ours to make.
    EXPECT_EQ(0, st_mutex_unlock(lock));
    MutexTestCall unlock_twice;
    mutex_test_unlock(unlock_twice, lock);
    EXPECT_EQ(-1, unlock_twice.r0_);
    EXPECT_EQ(EPERM, unlock_twice.errno_);

    // Let the waiter take the mutex and work while it holds it.
    st_usleep(1 * ST_UTIME_MILLISECONDS);
    EXPECT_TRUE(w.owned_);

    MutexTestCall unlock_other;
    mutex_test_unlock(unlock_other, lock);
    EXPECT_EQ(-1, unlock_other.r0_);
    EXPECT_EQ(EPERM, unlock_other.errno_);

    MutexTestCall still_held;
    mutex_test_trylock(still_held, lock);
    EXPECT_EQ(-1, still_held.r0_);
    EXPECT_EQ(EBUSY, still_held.errno_);

    EXPECT_EQ(0, st_cond_signal(work));
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(0, w.lock_call_.r0_);
    EXPECT_EQ(0, w.unlock_call_.r0_);

    EXPECT_EQ(0, st_mutex_destroy(lock));
    EXPECT_EQ(0, st_cond_destroy(work));
}

// SRS destroys a mutex when the object it guards is freed, and asserts the destroy succeeds. A mutex that is held, or
// that a coroutine still waits for, fails to destroy with EBUSY and stays usable, so a coroutine still in its critical
// section, or about to enter it, never finds the mutex freed under it. Once the last owner unlocks it, it is destroyed.
// Locks in current behavior.
VOID TEST(MutexTest, DestroyBusyMutexFails)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);

    ASSERT_EQ(0, st_mutex_lock(lock));

    errno = 0;
    EXPECT_EQ(-1, st_mutex_destroy(lock));
    EXPECT_EQ(EBUSY, errno);

    MutexTestWaiter w(lock);
    st_thread_t trd = st_thread_create(mutex_waiter_coroutine, &w, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let the waiter block on the mutex we hold.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    errno = 0;
    EXPECT_EQ(-1, st_mutex_destroy(lock));
    EXPECT_EQ(EBUSY, errno);

    // Unlocking hands the mutex to the waiter, which hasn't run yet, so the mutex is still held.
    EXPECT_EQ(0, st_mutex_unlock(lock));
    errno = 0;
    EXPECT_EQ(-1, st_mutex_destroy(lock));
    EXPECT_EQ(EBUSY, errno);

    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(0, w.lock_call_.r0_);
    EXPECT_EQ(0, w.unlock_call_.r0_);

    EXPECT_EQ(0, st_mutex_destroy(lock));
}

// Two coroutines wait for a mutex, and SRS stops the first one, such as a connection closed while it waits for the
// source lock. The stopped waiter stays in the wait queue until it runs, so the owner's unlock skips it and hands the
// mutex to the next coroutine still waiting. The stopped waiter fails with EINTR without the mutex, and the other one
// gets it, so the mutex is neither lost nor held twice. Locks in current behavior.
VOID TEST(MutexTest, UnlockSkipsStoppedWaiter)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);

    ASSERT_EQ(0, st_mutex_lock(lock));

    MutexTestWaiter stopped(lock);
    st_thread_t stopped_trd = st_thread_create(mutex_waiter_coroutine, &stopped, 1, 0);
    ASSERT_TRUE(stopped_trd != NULL);
    MutexTestWaiter next(lock);
    st_thread_t next_trd = st_thread_create(mutex_waiter_coroutine, &next, 1, 0);
    ASSERT_TRUE(next_trd != NULL);

    // Let both waiters block on the mutex we hold, the stopped one first.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    st_thread_interrupt(stopped_trd);
    EXPECT_EQ(0, st_mutex_unlock(lock));

    EXPECT_EQ(0, st_thread_join(stopped_trd, NULL));
    EXPECT_EQ(-1, stopped.lock_call_.r0_);
    EXPECT_EQ(EINTR, stopped.lock_call_.errno_);
    EXPECT_FALSE(stopped.owned_);

    EXPECT_EQ(0, st_thread_join(next_trd, NULL));
    EXPECT_EQ(0, next.lock_call_.r0_);
    EXPECT_TRUE(next.owned_);
    EXPECT_EQ(0, next.unlock_call_.r0_);

    EXPECT_EQ(0, st_mutex_destroy(lock));
}

// The only coroutine waiting for a mutex is stopped, and then the owner unlocks it. The unlock skips the stopped waiter,
// so the mutex becomes free instead of going to a coroutine that won't take it. Until the stopped waiter runs and
// leaves the wait queue, the mutex can't be destroyed (EBUSY), though it is free and anyone can take it. Once the waiter
// has failed with EINTR, the mutex is destroyed. Locks in current behavior.
VOID TEST(MutexTest, UnlockWithOnlyStoppedWaiterFreesMutex)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);

    ASSERT_EQ(0, st_mutex_lock(lock));

    MutexTestWaiter stopped(lock);
    st_thread_t trd = st_thread_create(mutex_waiter_coroutine, &stopped, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let the waiter block on the mutex we hold.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_mutex_unlock(lock));

    errno = 0;
    EXPECT_EQ(-1, st_mutex_destroy(lock));
    EXPECT_EQ(EBUSY, errno);

    MutexTestCall take;
    mutex_test_trylock(take, lock);
    EXPECT_EQ(0, take.r0_);
    EXPECT_EQ(0, st_mutex_unlock(lock));

    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(-1, stopped.lock_call_.r0_);
    EXPECT_EQ(EINTR, stopped.lock_call_.errno_);
    EXPECT_FALSE(stopped.owned_);

    EXPECT_EQ(0, st_mutex_destroy(lock));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for the condition variable, the way SRS wakes a coroutine that waits for work: a consumer waits until the
// source enqueues a message (SrsLiveConsumer, SrsRtcConsumer), the async worker waits for a task (SrsAsyncCallWorker),
// and a wait group waits until its last coroutine is done (SrsWaitGroup). A signal wakes the first coroutine still
// waiting, and a broadcast wakes them all. A coroutine that is woken or stopped stays in the wait queue until it runs.
// srs_cond_destroy asserts the destroy succeeds, and a condition variable with a coroutine in its wait queue can't be
// destroyed.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The order in which the waiters of one test woke up.
struct CondTestOrder {
    int ids_[8];
    int n_;
    CondTestOrder() : n_(0) {
    }
};

// A coroutine that waits for work without a timeout, like every SRS caller, and records what the wait returned.
struct CondTestWaiter {
    st_cond_t cond_;
    int id_;
    CondTestOrder* order_;
    int r0_;
    int errno_;
    bool woken_;
    CondTestWaiter(st_cond_t cond, int id, CondTestOrder* order) : cond_(cond), id_(id), order_(order), r0_(0), errno_(0), woken_(false) {
    }
};

static void* cond_waiter_coroutine(void* arg)
{
    CondTestWaiter* w = (CondTestWaiter*)arg;

    errno = 0;
    w->r0_ = st_cond_wait(w->cond_);
    w->errno_ = errno;
    w->woken_ = true;
    if (w->order_) w->order_->ids_[w->order_->n_++] = w->id_;
    return NULL;
}

// Three workers wait for jobs on one condition variable, and the producer queues two jobs and signals once per job
// without yielding in between, such as a burst of tasks for a pool of async workers. The first worker is woken but
// hasn't run, so it is still in the wait queue: the second signal skips it and wakes the second worker, instead of
// waking the first one twice and losing a job. The third worker waits on until the next signal. Workers run in the order
// they were woken. Locks in current behavior.
VOID TEST(CondTest, EachSignalWakesAnotherWaiter)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    CondTestOrder order;
    CondTestWaiter w1(cond, 1, &order), w2(cond, 2, &order), w3(cond, 3, &order);
    st_thread_t t1 = st_thread_create(cond_waiter_coroutine, &w1, 1, 0);
    ASSERT_TRUE(t1 != NULL);
    st_thread_t t2 = st_thread_create(cond_waiter_coroutine, &w2, 1, 0);
    ASSERT_TRUE(t2 != NULL);
    st_thread_t t3 = st_thread_create(cond_waiter_coroutine, &w3, 1, 0);
    ASSERT_TRUE(t3 != NULL);

    // Let all three workers wait, in the order they were created.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    EXPECT_EQ(0, st_cond_signal(cond));
    EXPECT_EQ(0, st_cond_signal(cond));
    EXPECT_FALSE(w1.woken_);

    st_usleep(1 * ST_UTIME_MILLISECONDS);
    ASSERT_EQ(2, order.n_);
    EXPECT_EQ(1, order.ids_[0]);
    EXPECT_EQ(2, order.ids_[1]);
    EXPECT_EQ(0, w1.r0_);
    EXPECT_EQ(0, w2.r0_);
    EXPECT_FALSE(w3.woken_);

    EXPECT_EQ(0, st_cond_signal(cond));
    EXPECT_EQ(0, st_thread_join(t1, NULL));
    EXPECT_EQ(0, st_thread_join(t2, NULL));
    EXPECT_EQ(0, st_thread_join(t3, NULL));
    ASSERT_EQ(3, order.n_);
    EXPECT_EQ(3, order.ids_[2]);
    EXPECT_EQ(0, w3.r0_);

    EXPECT_EQ(0, st_cond_destroy(cond));
}

// The producer signals one waiter, then broadcasts before that waiter runs, such as a source that wakes a consumer for a
// message and then wakes everyone because it unpublished. The broadcast skips the waiter the signal already woke and
// wakes the others, so every waiter runs exactly once, in the order it was woken. Locks in current behavior.
VOID TEST(CondTest, BroadcastAfterSignalWakesEachWaiterOnce)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    CondTestOrder order;
    CondTestWaiter w1(cond, 1, &order), w2(cond, 2, &order), w3(cond, 3, &order);
    st_thread_t t1 = st_thread_create(cond_waiter_coroutine, &w1, 1, 0);
    ASSERT_TRUE(t1 != NULL);
    st_thread_t t2 = st_thread_create(cond_waiter_coroutine, &w2, 1, 0);
    ASSERT_TRUE(t2 != NULL);
    st_thread_t t3 = st_thread_create(cond_waiter_coroutine, &w3, 1, 0);
    ASSERT_TRUE(t3 != NULL);

    // Let all three waiters wait, in the order they were created.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    EXPECT_EQ(0, st_cond_signal(cond));
    EXPECT_EQ(0, st_cond_broadcast(cond));

    EXPECT_EQ(0, st_thread_join(t1, NULL));
    EXPECT_EQ(0, st_thread_join(t2, NULL));
    EXPECT_EQ(0, st_thread_join(t3, NULL));
    ASSERT_EQ(3, order.n_);
    EXPECT_EQ(1, order.ids_[0]);
    EXPECT_EQ(2, order.ids_[1]);
    EXPECT_EQ(3, order.ids_[2]);
    EXPECT_EQ(0, w1.r0_);
    EXPECT_EQ(0, w2.r0_);
    EXPECT_EQ(0, w3.r0_);

    EXPECT_EQ(0, st_cond_destroy(cond));
}

// Two coroutines wait for work, and SRS stops the first one, such as a connection closed while it waits. The stopped
// waiter stays in the wait queue until it runs, so the next signal skips it and wakes the second waiter, instead of
// being lost on a coroutine that is leaving. The stopped waiter fails with EINTR, and the other one gets the work.
// Locks in current behavior.
VOID TEST(CondTest, SignalSkipsStoppedWaiter)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    CondTestOrder order;
    CondTestWaiter stopped(cond, 1, &order), next(cond, 2, &order);
    st_thread_t stopped_trd = st_thread_create(cond_waiter_coroutine, &stopped, 1, 0);
    ASSERT_TRUE(stopped_trd != NULL);
    st_thread_t next_trd = st_thread_create(cond_waiter_coroutine, &next, 1, 0);
    ASSERT_TRUE(next_trd != NULL);

    // Let both waiters wait, the stopped one first.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    st_thread_interrupt(stopped_trd);
    EXPECT_EQ(0, st_cond_signal(cond));

    EXPECT_EQ(0, st_thread_join(stopped_trd, NULL));
    EXPECT_EQ(-1, stopped.r0_);
    EXPECT_EQ(EINTR, stopped.errno_);

    EXPECT_EQ(0, st_thread_join(next_trd, NULL));
    EXPECT_EQ(0, next.r0_);

    ASSERT_EQ(2, order.n_);
    EXPECT_EQ(1, order.ids_[0]);
    EXPECT_EQ(2, order.ids_[1]);

    EXPECT_EQ(0, st_cond_destroy(cond));
}

// SRS destroys a condition variable when the object that owns it is freed, and asserts the destroy succeeds. While a
// coroutine is in the wait queue, the destroy fails with EBUSY and the condition variable stays usable: when the
// coroutine waits, and also after a signal or a stop woke it but before it ran, because it leaves the wait queue only
// when it runs. So the owner must let the woken coroutine run, such as by joining it, before freeing the condition
// variable. Locks in current behavior.
VOID TEST(CondTest, DestroyWithWaiterFails)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    CondTestWaiter signaled(cond, 1, NULL);
    st_thread_t trd = st_thread_create(cond_waiter_coroutine, &signaled, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Let the waiter wait.
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    errno = 0;
    EXPECT_EQ(-1, st_cond_destroy(cond));
    EXPECT_EQ(EBUSY, errno);

    EXPECT_EQ(0, st_cond_signal(cond));
    errno = 0;
    EXPECT_EQ(-1, st_cond_destroy(cond));
    EXPECT_EQ(EBUSY, errno);

    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(0, signaled.r0_);

    // The same holds for a waiter that is stopped.
    CondTestWaiter stopped(cond, 2, NULL);
    trd = st_thread_create(cond_waiter_coroutine, &stopped, 1, 0);
    ASSERT_TRUE(trd != NULL);
    st_usleep(1 * ST_UTIME_MILLISECONDS);

    st_thread_interrupt(trd);
    errno = 0;
    EXPECT_EQ(-1, st_cond_destroy(cond));
    EXPECT_EQ(EBUSY, errno);

    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_EQ(-1, stopped.r0_);
    EXPECT_EQ(EINTR, stopped.errno_);

    EXPECT_EQ(0, st_cond_destroy(cond));
}

// A signal or broadcast with no coroutine waiting succeeds and is not remembered, so a coroutine that starts waiting
// afterwards waits for the next one. That is why SRS checks its own state before waiting, such as SrsAsyncCallWorker
// waiting only when it has no task, and SrsWaitGroup only while a coroutine isn't done: a wakeup sent before the wait
// would be lost. Locks in current behavior.
VOID TEST(CondTest, SignalWithoutWaiterIsLost)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    EXPECT_EQ(0, st_cond_signal(cond));
    EXPECT_EQ(0, st_cond_broadcast(cond));

    errno = 0;
    EXPECT_EQ(-1, st_cond_timedwait(cond, 10 * ST_UTIME_MILLISECONDS));
    EXPECT_EQ(ETIME, errno);

    EXPECT_EQ(0, st_cond_destroy(cond));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for reading the time without a system call. st_utime_last_clock returns the time the scheduler last read
// the monotonic clock, and with the time cache on, st_time returns a wall-clock second that the scheduler refreshes at
// most once a second. The scheduler reads the clock after it waits for I/O or timers, and in st_thread_yield, so both
// stand still while a coroutine works without switching. SRS doesn't call them: it keeps its own cached clock,
// srs_time_now_cached.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Work for usecs without switching to another coroutine, such as a coroutine busy with a batch of packets.
static void clock_test_busy(st_utime_t usecs)
{
    st_utime_t starttime = st_utime();
    while (st_utime() - starttime < usecs) {
    }
}

// A coroutine works without switching, so the scheduler's last clock reading stands still and falls behind the real
// clock. A yield reads the clock again, then returns at once because no other coroutine is runnable. So a coroutine
// that only yields between batches, such as an SRS UDP receive loop, still moves the clock that due timers are checked
// against. Locks in current behavior.
VOID TEST(ClockTest, YieldReadsClock)
{
    st_utime_t last = st_utime_last_clock();
    EXPECT_LE(last, st_utime());

    clock_test_busy(2 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(last, st_utime_last_clock());

    st_utime_t before = st_utime();
    st_thread_yield();
    st_utime_t after = st_utime();
    EXPECT_GE(st_utime_last_clock(), before);
    EXPECT_LE(st_utime_last_clock(), after);
}

// A timed wait counts its timeout from the scheduler's last clock reading, and the scheduler reads the clock again
// when it wakes the coroutine. So after the wait, the last clock reading is at least the timeout later than before it,
// and a coroutine can tell how long it waited without a system call. Locks in current behavior.
VOID TEST(ClockTest, SleepMovesLastClockByTimeout)
{
    st_utime_t last = st_utime_last_clock();
    EXPECT_EQ(0, st_usleep(10 * ST_UTIME_MILLISECONDS));
    EXPECT_GE(st_utime_last_clock(), last + 10 * ST_UTIME_MILLISECONDS);
    EXPECT_LE(st_utime_last_clock(), st_utime());
}

// st_timecache_set turns the time cache on or off and returns whether it was on, so a caller can put it back. It is
// off by default. Locks in current behavior.
VOID TEST(TimeCacheTest, SetReturnsWhetherItWasOn)
{
    EXPECT_EQ(0, st_timecache_set(1));
    EXPECT_EQ(1, st_timecache_set(1));
    EXPECT_EQ(1, st_timecache_set(0));
    EXPECT_EQ(0, st_timecache_set(0));
}

// The time cache's state, private to ST: the cached second, and when it was last read from time(). A test ages them
// to stand for a second that has passed, instead of waiting for it.
extern "C" {
extern __thread time_t _st_curr_time;
extern __thread st_utime_t _st_last_tset;
}

// A server that stamps every request with the wall-clock time turns the time cache on, so st_time reads a cached
// second instead of calling time(). Turning it on reads the time at once. The cached second stands still between
// refreshes, even when it is behind the real one, and the scheduler refreshes it only when its clock reading is more
// than a second after the last refresh, so a stamp may lag the real time by about a second. With the cache off,
// st_time calls time() again. Locks in current behavior.
VOID TEST(TimeCacheTest, CachedSecondRefreshedByScheduler)
{
    time_t starttime = time(NULL);
    EXPECT_EQ(0, st_timecache_set(1));

    time_t cached = st_time();
    EXPECT_GE(cached, starttime);
    EXPECT_LE(cached, time(NULL));

    // Age the cached second, so it is behind the real one, as if a second has passed since it was read.
    time_t stale = cached - 10;
    _st_curr_time = stale;
    EXPECT_EQ(stale, st_time());

    // The scheduler reads its clock less than a second after the last refresh, so it keeps the cached second.
    _st_last_tset = st_utime() - 900 * ST_UTIME_MILLISECONDS;
    st_thread_yield();
    EXPECT_EQ(stale, st_time());

    // More than a second after the last refresh, the scheduler reads time() again, and counts the next second from
    // its clock reading.
    st_utime_t before = st_utime();
    _st_last_tset = before - 1000 * ST_UTIME_MILLISECONDS;
    starttime = time(NULL);
    st_thread_yield();
    EXPECT_GE(st_time(), starttime);
    EXPECT_LE(st_time(), time(NULL));
    EXPECT_GE(_st_last_tset, before);
    EXPECT_LE(_st_last_tset, st_utime());

    EXPECT_EQ(1, st_timecache_set(0));
    starttime = time(NULL);
    time_t now = st_time();
    EXPECT_GE(now, starttime);
    EXPECT_LE(now, time(NULL));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for sleeping in whole seconds with st_sleep, such as a coroutine that checks something once a second. It
// is st_usleep counted in seconds, and a negative number of seconds sleeps until the coroutine is stopped. Like any
// sleep, a stop wakes it at once with EINTR. SRS doesn't call it: it sleeps with srs_usleep, which calls st_usleep.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// What one st_sleep returned, and how long it took.
struct SleepTestCall {
    bool started_;
    bool done_;
    int secs_;
    int r0_;
    int errno_;
    st_utime_t elapsed_;
    SleepTestCall() : started_(false), done_(false), secs_(0), r0_(0), errno_(0), elapsed_(0) {
    }
};

static void* sleep_test_coroutine(void* arg)
{
    SleepTestCall* call = (SleepTestCall*)arg;
    call->started_ = true;

    st_utime_t starttime = st_utime();
    errno = 0;
    call->r0_ = st_sleep(call->secs_);
    call->errno_ = errno;
    call->elapsed_ = st_utime() - starttime;

    call->done_ = true;
    return NULL;
}

// st_sleep(0) doesn't wait for any time, but it still switches: a coroutine that is already runnable runs first, then
// the sleep returns 0. Locks in current behavior.
VOID TEST(SleepTest, ZeroSecondsLetsOthersRun)
{
    SleepTestCall other;
    other.secs_ = 0;
    st_thread_t trd = st_thread_create(sleep_test_coroutine, &other, 1, 0);
    ASSERT_TRUE(trd != NULL);

    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, st_sleep(0));
    EXPECT_LT(st_utime() - starttime, 10 * ST_UTIME_MILLISECONDS);
    EXPECT_TRUE(other.started_);

    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_TRUE(other.done_);
    EXPECT_EQ(0, other.r0_);
}

// A coroutine that checks something once a second sleeps with st_sleep(1). The sleep counts in seconds, not
// milliseconds or microseconds, so the coroutine is still asleep after 20 ms. It is stopped while it sleeps, so the
// sleep fails at once with EINTR instead of waiting out the second. Locks in current behavior.
VOID TEST(SleepTest, OneSecondSleepStopped)
{
    SleepTestCall sleeper;
    sleeper.secs_ = 1;
    st_thread_t trd = st_thread_create(sleep_test_coroutine, &sleeper, 1, 0);
    ASSERT_TRUE(trd != NULL);

    EXPECT_EQ(0, st_usleep(20 * ST_UTIME_MILLISECONDS));
    EXPECT_TRUE(sleeper.started_);
    EXPECT_FALSE(sleeper.done_);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_TRUE(sleeper.done_);
    EXPECT_EQ(-1, sleeper.r0_);
    EXPECT_EQ(EINTR, sleeper.errno_);
    EXPECT_GE(sleeper.elapsed_, 20 * ST_UTIME_MILLISECONDS);
    EXPECT_LT(sleeper.elapsed_, 500 * ST_UTIME_MILLISECONDS);
}

// A negative number of seconds sleeps with no timeout, the same as st_usleep(ST_UTIME_NO_TIMEOUT): a coroutine parks
// until it is stopped, and the sleep fails with EINTR. Locks in current behavior.
VOID TEST(SleepTest, NegativeSecondsSleepsUntilStopped)
{
    SleepTestCall sleeper;
    sleeper.secs_ = -1;
    st_thread_t trd = st_thread_create(sleep_test_coroutine, &sleeper, 1, 0);
    ASSERT_TRUE(trd != NULL);

    EXPECT_EQ(0, st_usleep(20 * ST_UTIME_MILLISECONDS));
    EXPECT_TRUE(sleeper.started_);
    EXPECT_FALSE(sleeper.done_);

    st_thread_interrupt(trd);
    EXPECT_EQ(0, st_thread_join(trd, NULL));
    EXPECT_TRUE(sleeper.done_);
    EXPECT_EQ(-1, sleeper.r0_);
    EXPECT_EQ(EINTR, sleeper.errno_);
}

#ifndef _WIN32 // POSIX only: fork, shared memory, pipes and pthread
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for a custom clock, such as a simulation or a test tool that drives time itself instead of waiting for it.
// st_set_utime_function replaces the monotonic clock behind st_utime, and every timeout counts on it: sleeps, condition
// variable waits and I/O. It must be set before st_init. The clock is one for the whole process, so each test runs a
// new OS thread in a forked child, and the child reports what it saw through shared memory. When no coroutine can run,
// ST waits for the earliest timer in real time, so a test never lets that happen with a timer hours away on the custom
// clock: it moves the clock, then yields, and the yield checks the timers first. SRS doesn't call it.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define ST_UTIME_SECONDS ((st_utime_t)1000 * ST_UTIME_MILLISECONDS)

// The time the test tool says it is, in microseconds. Each forked child has its own copy.
static st_utime_t custom_clock_test_now;

static st_utime_t custom_clock_test_read(void)
{
    return custom_clock_test_now;
}

// A second clock that a test tries to install after the first one.
static st_utime_t custom_clock_test_other(void)
{
    return 7;
}

// A result shared with the forked child; T must be plain data, since the child's heap is its own.
template <typename T>
struct CustomClockTestShared {
    T* p_;
    CustomClockTestShared() : p_(NULL) {
        void* m = mmap(NULL, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
        if (m != MAP_FAILED) p_ = new (m) T();
    }
    ~CustomClockTestShared() {
        if (p_) munmap(p_, sizeof(T));
    }
};

struct CustomClockTestChild {
    void (*body_)(void*);
    void* arg_;
    int done_[2];
};

static void* custom_clock_test_thread(void* arg)
{
    CustomClockTestChild* c = (CustomClockTestChild*)arg;
    c->body_(c->arg_);

    char done = 1;
    if (::write(c->done_[1], &done, 1) != 1) _exit(1);

    // ST can't free an instance, so keep the thread and its instance alive until the child exits.
    for (;;) pause();
    return NULL;
}

// Runs body on a new OS thread in a forked child, and returns the child's exit status, or -1 if it didn't exit. The
// thread has no ST yet: body sets the clock and calls st_init itself.
static int custom_clock_test_run(void (*body)(void*), void* arg)
{
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // A hang kills the child and fails the test, instead of hanging the suite. It also catches a wait in real
        // time for a timer that is hours away on the custom clock.
        alarm(5);

        CustomClockTestChild c;
        c.body_ = body;
        c.arg_ = arg;
        if (pipe(c.done_) < 0) _exit(1);

        pthread_t trd;
        if (pthread_create(&trd, NULL, custom_clock_test_thread, &c) != 0) _exit(1);

        char done = 0;
        if (::read(c.done_[0], &done, 1) != 1) _exit(1);

        // Not _exit, so a coverage build writes the child's counters.
        exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct CustomClockTestSleep {
    int set_r0_;
    int init_r0_;
    st_utime_t utime_;
    st_utime_t last_clock_;
    int started_;
    int done_after_59m_;
    int done_after_60m_;
    int sleep_r0_;
    st_utime_t utime_after_;
    st_utime_t last_clock_after_;
};

static void* custom_clock_test_sleeper(void* arg)
{
    CustomClockTestSleep* r = (CustomClockTestSleep*)arg;
    r->started_ = 1;
    r->sleep_r0_ = st_usleep(3600 * ST_UTIME_SECONDS);
    r->done_after_60m_ = 1;
    return NULL;
}

static void custom_clock_test_sleep(void* arg)
{
    CustomClockTestSleep* r = (CustomClockTestSleep*)arg;

    custom_clock_test_now = 1000 * ST_UTIME_SECONDS;
    r->set_r0_ = st_set_utime_function(custom_clock_test_read);
    if (st_set_eventsys(ST_EVENTSYS_ALT) < 0) return;
    r->init_r0_ = st_init();
    r->utime_ = st_utime();
    r->last_clock_ = st_utime_last_clock();

    if (!st_thread_create(custom_clock_test_sleeper, r, 0, 0)) return;

    // The sleeper starts, and sleeps for an hour from the time the tool set.
    st_thread_yield();

    custom_clock_test_now += 59 * 60 * ST_UTIME_SECONDS;
    st_thread_yield();
    r->done_after_59m_ = r->done_after_60m_;

    custom_clock_test_now += 60 * ST_UTIME_SECONDS;
    st_thread_yield();

    r->utime_after_ = st_utime();
    r->last_clock_after_ = st_utime_last_clock();
}

// A test tool sets its own clock before st_init, and st_utime reads it. A coroutine sleeps for an hour. The tool moves
// its clock 59 minutes ahead: the coroutine still sleeps. One more minute, and the coroutine wakes and its sleep
// returns 0, with no real time spent waiting. The scheduler's last clock reading is the tool's time too. Locks in
// current behavior.
VOID TEST(CustomClockTest, SimulatedHourPassesAtOnce)
{
    CustomClockTestShared<CustomClockTestSleep> shared;
    ASSERT_TRUE(shared.p_ != NULL);
    CustomClockTestSleep* r = shared.p_;
    r->sleep_r0_ = -2;

    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, custom_clock_test_run(custom_clock_test_sleep, r));
    EXPECT_LT(st_utime() - starttime, 1 * ST_UTIME_SECONDS);

    EXPECT_EQ(0, r->set_r0_);
    EXPECT_EQ(0, r->init_r0_);
    EXPECT_EQ(1000 * ST_UTIME_SECONDS, r->utime_);
    EXPECT_EQ(1000 * ST_UTIME_SECONDS, r->last_clock_);

    EXPECT_EQ(1, r->started_);
    EXPECT_EQ(0, r->done_after_59m_);
    EXPECT_EQ(1, r->done_after_60m_);
    EXPECT_EQ(0, r->sleep_r0_);

    EXPECT_EQ(4600 * ST_UTIME_SECONDS, r->utime_after_);
    EXPECT_EQ(4600 * ST_UTIME_SECONDS, r->last_clock_after_);
}

struct CustomClockTestRead {
    int init_r0_;
    int started_;
    int done_after_29s_;
    int done_after_30s_;
    int read_r0_;
    int read_errno_;
    int close_r0_;
};

struct CustomClockTestReader {
    CustomClockTestRead* r_;
    st_netfd_t fd_;
};

static void* custom_clock_test_reader(void* arg)
{
    CustomClockTestReader* c = (CustomClockTestReader*)arg;
    c->r_->started_ = 1;

    char buf[16];
    errno = 0;
    c->r_->read_r0_ = (int)st_read(c->fd_, buf, sizeof(buf), 30 * ST_UTIME_SECONDS);
    c->r_->read_errno_ = errno;
    c->r_->done_after_30s_ = 1;
    return NULL;
}

static void custom_clock_test_read_timeout(void* arg)
{
    CustomClockTestRead* r = (CustomClockTestRead*)arg;

    custom_clock_test_now = 1000 * ST_UTIME_SECONDS;
    if (st_set_utime_function(custom_clock_test_read) < 0) return;
    if (st_set_eventsys(ST_EVENTSYS_ALT) < 0) return;
    r->init_r0_ = st_init();

    int fds[2];
    if (pipe(fds) < 0) return;
    CustomClockTestReader c;
    c.r_ = r;
    c.fd_ = st_netfd_open(fds[0]);
    if (!c.fd_) return;

    if (!st_thread_create(custom_clock_test_reader, &c, 0, 0)) return;

    // The reader starts and waits on the quiet pipe, with a 30 s timeout from the time the tool set.
    st_thread_yield();

    custom_clock_test_now += 29 * ST_UTIME_SECONDS;
    st_thread_yield();
    r->done_after_29s_ = r->done_after_30s_;

    custom_clock_test_now += 1 * ST_UTIME_SECONDS;
    st_thread_yield();

    r->close_r0_ = st_netfd_close(c.fd_);
    ::close(fds[1]);
}

// I/O timeouts count on the custom clock too. A reader waits on a quiet pipe with a 30 s timeout. The tool moves its
// clock 29 s ahead: the reader still waits. One more second, and the read fails with ETIME, with no real time spent
// waiting. The reader left no wait behind, so the pipe then closes without EBUSY. Locks in current behavior.
VOID TEST(CustomClockTest, ReadTimesOutOnSimulatedClock)
{
    CustomClockTestShared<CustomClockTestRead> shared;
    ASSERT_TRUE(shared.p_ != NULL);
    CustomClockTestRead* r = shared.p_;
    r->close_r0_ = -2;

    st_utime_t starttime = st_utime();
    EXPECT_EQ(0, custom_clock_test_run(custom_clock_test_read_timeout, r));
    EXPECT_LT(st_utime() - starttime, 1 * ST_UTIME_SECONDS);

    EXPECT_EQ(0, r->init_r0_);
    EXPECT_EQ(1, r->started_);
    EXPECT_EQ(0, r->done_after_29s_);
    EXPECT_EQ(1, r->done_after_30s_);
    EXPECT_EQ(-1, r->read_r0_);
    EXPECT_EQ(ETIME, r->read_errno_);
    EXPECT_EQ(0, r->close_r0_);
}

struct CustomClockTestRefused {
    int set_r0_;
    st_utime_t utime_set_;
    int reset_r0_;
    st_utime_t system_before_;
    st_utime_t utime_reset_;
    st_utime_t system_after_;
    int set_again_r0_;
    int init_r0_;
    int other_r0_;
    int other_errno_;
    int null_r0_;
    int null_errno_;
    st_utime_t utime_after_;
};

static st_utime_t custom_clock_test_monotonic(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static void custom_clock_test_refused(void* arg)
{
    CustomClockTestRefused* r = (CustomClockTestRefused*)arg;

    custom_clock_test_now = 1000 * ST_UTIME_SECONDS;
    r->set_r0_ = st_set_utime_function(custom_clock_test_read);
    r->utime_set_ = st_utime();

    r->reset_r0_ = st_set_utime_function(NULL);
    r->system_before_ = custom_clock_test_monotonic();
    r->utime_reset_ = st_utime();
    r->system_after_ = custom_clock_test_monotonic();

    r->set_again_r0_ = st_set_utime_function(custom_clock_test_read);
    if (st_set_eventsys(ST_EVENTSYS_ALT) < 0) return;
    r->init_r0_ = st_init();

    errno = 0;
    r->other_r0_ = st_set_utime_function(custom_clock_test_other);
    r->other_errno_ = errno;

    errno = 0;
    r->null_r0_ = st_set_utime_function(NULL);
    r->null_errno_ = errno;

    r->utime_after_ = st_utime();
}

// Before st_init, a program can set a clock, and set NULL to go back to the system's monotonic clock. Once ST runs on
// the thread, its timers already count on the clock it has, so changing it, to another clock or back to the system's,
// fails with EINVAL and st_utime keeps reading the same clock. Locks in current behavior.
VOID TEST(CustomClockTest, RefusedOnceStRuns)
{
    CustomClockTestShared<CustomClockTestRefused> shared;
    ASSERT_TRUE(shared.p_ != NULL);
    CustomClockTestRefused* r = shared.p_;
    r->null_r0_ = -2;

    EXPECT_EQ(0, custom_clock_test_run(custom_clock_test_refused, r));

    EXPECT_EQ(0, r->set_r0_);
    EXPECT_EQ(1000 * ST_UTIME_SECONDS, r->utime_set_);

    EXPECT_EQ(0, r->reset_r0_);
    EXPECT_GE(r->utime_reset_, r->system_before_);
    EXPECT_LE(r->utime_reset_, r->system_after_);

    EXPECT_EQ(0, r->set_again_r0_);
    EXPECT_EQ(0, r->init_r0_);
    EXPECT_EQ(-1, r->other_r0_);
    EXPECT_EQ(EINVAL, r->other_errno_);
    EXPECT_EQ(-1, r->null_r0_);
    EXPECT_EQ(EINVAL, r->null_errno_);
    EXPECT_EQ(1000 * ST_UTIME_SECONDS, r->utime_after_);
}
#endif
