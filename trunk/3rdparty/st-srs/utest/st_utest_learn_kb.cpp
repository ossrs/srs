/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#define ST_UTIME_MILLISECONDS 1000
#define ST_UTEST_TIMEOUT (100 * ST_UTIME_MILLISECONDS)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests for context switching: verify that _st_md_cxt_save/_st_md_cxt_restore
// and st_thread_create's save-then-patch-SP trick actually work.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Test: a coroutine runs on a different stack than the primordial thread.
// This proves the SP-patching trick in st_thread_create works.
static void* coroutine_stack_addr(void* arg)
{
    int local_var = 42;
    // Write our stack variable address back to the caller.
    *(uintptr_t*)arg = (uintptr_t)&local_var;
    return NULL;
}

VOID TEST(LearnKB, CoroutineRunsOnSeparateStack)
{
    uintptr_t primordial_stack_addr = 0;
    uintptr_t coroutine_stack = 0;

    // Capture primordial thread's stack address.
    int local = 0;
    primordial_stack_addr = (uintptr_t)&local;

    st_thread_t trd = st_thread_create(coroutine_stack_addr, &coroutine_stack, 1, 0);
    ASSERT_TRUE(trd != NULL);
    st_thread_join(trd, NULL);

    ASSERT_NE((uintptr_t)0, coroutine_stack);

    // The coroutine's stack should be far from the primordial stack.
    // Default stack is 128KB, so the difference should be at least that much.
    uintptr_t diff = (primordial_stack_addr > coroutine_stack)
        ? (primordial_stack_addr - coroutine_stack)
        : (coroutine_stack - primordial_stack_addr);
    EXPECT_GT(diff, (uintptr_t)4096) << "Coroutine stack should be on a separate heap-allocated stack";
}

// Test: context switches preserve local state across yields.
// Each coroutine writes to its own local variable, yields, then checks the value.
// This proves save/restore of registers and stack is correct.
static void* coroutine_preserve_local(void* arg)
{
    int id = *(int*)arg;
    int local_state = id * 1000;

    // Yield to let other coroutines run.
    st_usleep(0);

    // After resume, local_state should be intact — it lives on our own stack.
    local_state += 1;

    // Write result back.
    *(int*)arg = local_state;
    return NULL;
}

VOID TEST(LearnKB, LocalStatePreservedAcrossYield)
{
    int a = 1, b = 2, c = 3;
    st_thread_t t1 = st_thread_create(coroutine_preserve_local, &a, 1, 0);
    st_thread_t t2 = st_thread_create(coroutine_preserve_local, &b, 1, 0);
    st_thread_t t3 = st_thread_create(coroutine_preserve_local, &c, 1, 0);
    ASSERT_TRUE(t1 && t2 && t3);

    st_thread_join(t1, NULL);
    st_thread_join(t2, NULL);
    st_thread_join(t3, NULL);

    // Each coroutine computed: id * 1000 + 1
    EXPECT_EQ(1001, a);
    EXPECT_EQ(2001, b);
    EXPECT_EQ(3001, c);
}

// Test: context switching via st_thread_yield works correctly.
// We create coroutines that increment a shared counter in a deterministic order
// using yields. This verifies the scheduler + context switch round-trips.
static int g_counter = 0;

static void* coroutine_yield_order(void* arg)
{
    int my_order = *(int*)arg;

    // Wait until it's our turn.
    while (g_counter < my_order) {
        st_usleep(0);
    }

    // It's our turn — increment.
    g_counter++;
    return NULL;
}

VOID TEST(LearnKB, YieldOrderPreserved)
{
    g_counter = 0;

    int order0 = 0, order1 = 1, order2 = 2;
    st_thread_t t0 = st_thread_create(coroutine_yield_order, &order0, 1, 0);
    st_thread_t t1 = st_thread_create(coroutine_yield_order, &order1, 1, 0);
    st_thread_t t2 = st_thread_create(coroutine_yield_order, &order2, 1, 0);
    ASSERT_TRUE(t0 && t1 && t2);

    st_thread_join(t0, NULL);
    st_thread_join(t1, NULL);
    st_thread_join(t2, NULL);

    EXPECT_EQ(3, g_counter);
}

// Test: return value from coroutine is correctly passed through st_thread_join.
// This proves the full lifecycle: create (save+patch SP) → schedule (restore) →
// run → exit (save retval) → join (read retval).
static void* coroutine_retval(void* arg)
{
    int input = *(int*)arg;
    st_usleep(0);
    // Return a computed value as void*.
    return (void*)(intptr_t)(input * input);
}

VOID TEST(LearnKB, ReturnValueThroughJoin)
{
    int input = 7;
    st_thread_t trd = st_thread_create(coroutine_retval, &input, 1, 0);
    ASSERT_TRUE(trd != NULL);

    void* retval = NULL;
    st_thread_join(trd, &retval);

    EXPECT_EQ(49, (int)(intptr_t)retval);
}

// Test: start routine must NOT execute inline in st_thread_create.
// It should run only after scheduler handoff.
static int g_create_started = 0;

static void* coroutine_mark_started(void* /*arg*/)
{
    g_create_started++;
    return NULL;
}

VOID TEST(LearnKB, StartRoutineNotExecutedInline)
{
    g_create_started = 0;

    st_thread_t trd = st_thread_create(coroutine_mark_started, NULL, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Creator path: _st_md_cxt_save returns 0, so _st_thread_main is not run inline.
    EXPECT_EQ(0, g_create_started) << "Coroutine must not run inline inside st_thread_create";

    // After yielding, scheduler can run the created coroutine.
    st_usleep(0);
    EXPECT_EQ(1, g_create_started) << "Coroutine should run after scheduler handoff";

    st_thread_join(trd, NULL);
}

static void* coroutine_run_once(void* arg)
{
    int* runs = (int*)arg;
    (*runs)++;
    return NULL;
}

VOID TEST(LearnKB, JoinDrivesFirstRunWhenNoManualYield)
{
    int runs = 0;

    st_thread_t trd = st_thread_create(coroutine_run_once, &runs, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Still not run yet, because creator hasn't yielded.
    EXPECT_EQ(0, runs);

    // Join blocks current coroutine and hands control to scheduler.
    st_thread_join(trd, NULL);
    EXPECT_EQ(1, runs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_mutex cooperative lock/wakeup workflow.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct MutexLearnCtx {
    st_mutex_t lock;
    int* order;
    int* index;
    int id;
};

static void* coroutine_mutex_fifo_waiter(void* arg)
{
    MutexLearnCtx* ctx = (MutexLearnCtx*)arg;

    int r0 = st_mutex_lock(ctx->lock);
    ST_ASSERT_ERROR(r0 != 0, r0, "waiter failed to lock mutex");

    ctx->order[*ctx->index] = ctx->id;
    (*ctx->index)++;

    r0 = st_mutex_unlock(ctx->lock);
    ST_ASSERT_ERROR(r0 != 0, r0, "waiter failed to unlock mutex");

    return NULL;
}

VOID TEST(LearnKB, MutexCooperativeWorkflow)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);

    int r0 = st_mutex_lock(lock);
    ASSERT_EQ(0, r0);

    // Same-owner re-lock should fail with EDEADLK.
    errno = 0;
    r0 = st_mutex_lock(lock);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EDEADLK, errno);

    int order[2] = {0, 0};
    int index = 0;

    MutexLearnCtx w1 = {lock, order, &index, 1};
    MutexLearnCtx w2 = {lock, order, &index, 2};

    st_thread_t t1 = st_thread_create(coroutine_mutex_fifo_waiter, &w1, 1, 0);
    st_thread_t t2 = st_thread_create(coroutine_mutex_fifo_waiter, &w2, 1, 0);
    ASSERT_TRUE(t1 && t2);

    // Let both waiters run and block on mutex wait_q in FIFO order.
    st_usleep(0);

    // Owner unlock should hand off to the first waiter without preemption.
    r0 = st_mutex_unlock(lock);
    ASSERT_EQ(0, r0);

    // Yield to allow waiter1 then waiter2 to run.
    st_usleep(0);
    st_usleep(0);

    ST_COROUTINE_JOIN(t1, t1_err);
    ST_COROUTINE_JOIN(t2, t2_err);
    ST_EXPECT_SUCCESS(t1_err);
    ST_EXPECT_SUCCESS(t2_err);

    EXPECT_EQ(2, index);
    EXPECT_EQ(1, order[0]);
    EXPECT_EQ(2, order[1]);

    r0 = st_mutex_destroy(lock);
    EXPECT_EQ(0, r0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for epoll/kqueue driven I/O sleep/wakeup behavior.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct WakeCtx {
    st_netfd_t reader;
    st_netfd_t writer;
    char value;
};

static void* delayed_writer(void* arg)
{
    WakeCtx* ctx = (WakeCtx*)arg;

    // Give reader coroutine time to enter st_read() and block in st_poll().
    st_usleep(10 * ST_UTIME_MILLISECONDS);

    ssize_t n = st_write(ctx->writer, &ctx->value, 1, ST_UTEST_TIMEOUT);
    ST_ASSERT_ERROR(n != 1, (int)n, "Writer failed");

    return NULL;
}

static void* waiting_reader(void* arg)
{
    WakeCtx* ctx = (WakeCtx*)arg;

    char ch = 0;
    ssize_t n = st_read(ctx->reader, &ch, 1, ST_UTEST_TIMEOUT);
    ST_ASSERT_ERROR(n != 1, (int)n, "Reader failed");
    ST_ASSERT_ERROR(ch != ctx->value, (int)ch, "Unexpected byte");

    return NULL;
}

VOID TEST(EpollWorkflowTest, ReaderSleepsAndWakesOnWriteReady)
{
    int fds[2] = {-1, -1};
    int r0 = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    EXPECT_EQ(0, r0);

    st_netfd_t reader = st_netfd_open_socket(fds[0]);
    st_netfd_t writer = st_netfd_open_socket(fds[1]);
    ASSERT_TRUE(reader != NULL);
    ASSERT_TRUE(writer != NULL);

    WakeCtx ctx;
    ctx.reader = reader;
    ctx.writer = writer;
    ctx.value = 'S';

    st_thread_t rd = st_thread_create(waiting_reader, &ctx, 1, 0);
    st_thread_t wr = st_thread_create(delayed_writer, &ctx, 1, 0);

    ASSERT_TRUE(rd != NULL);
    ASSERT_TRUE(wr != NULL);

    ST_COROUTINE_JOIN(rd, rd_err);
    ST_COROUTINE_JOIN(wr, wr_err);

    ST_EXPECT_SUCCESS(rd_err);
    ST_EXPECT_SUCCESS(wr_err);

    st_netfd_close(reader);
    st_netfd_close(writer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_cond condition variable workflow.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct CondLearnCtx {
    st_cond_t cond;
    int* wake_order;
    int* wake_index;
    int id;
    st_utime_t timeout;
};

static void* coroutine_cond_wait_and_record(void* arg)
{
    CondLearnCtx* ctx = (CondLearnCtx*)arg;

    int r0 = st_cond_timedwait(ctx->cond, ctx->timeout);
    ST_ASSERT_ERROR(r0 != 0, r0, "cond wait expected success");

    ctx->wake_order[*ctx->wake_index] = ctx->id;
    (*ctx->wake_index)++;

    return NULL;
}

static void* coroutine_cond_wait_timeout(void* arg)
{
    CondLearnCtx* ctx = (CondLearnCtx*)arg;

    errno = 0;
    int r0 = st_cond_timedwait(ctx->cond, ctx->timeout);
    ST_ASSERT_ERROR(r0 != -1, r0, "cond wait should timeout");
    ST_ASSERT_ERROR(errno != ETIME, errno, "cond wait should set ETIME");

    return NULL;
}

VOID TEST(LearnKB, CondSignalWakeOne)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    int signal_order[2] = {0, 0};
    int signal_index = 0;

    CondLearnCtx s1 = {cond, signal_order, &signal_index, 1, ST_UTEST_TIMEOUT};
    CondLearnCtx s2 = {cond, signal_order, &signal_index, 2, ST_UTEST_TIMEOUT};

    st_thread_t ts1 = st_thread_create(coroutine_cond_wait_and_record, &s1, 1, 0);
    st_thread_t ts2 = st_thread_create(coroutine_cond_wait_and_record, &s2, 1, 0);
    ASSERT_TRUE(ts1 && ts2);

    st_usleep(0);  // Let both waiters enter cond wait_q.

    int r0 = st_cond_signal(cond);
    ASSERT_EQ(0, r0);

    st_usleep(0);  // Allow one waiter to resume.
    EXPECT_EQ(1, signal_index);

    // Wake remaining waiter to end the case cleanly.
    r0 = st_cond_signal(cond);
    ASSERT_EQ(0, r0);

    ST_COROUTINE_JOIN(ts1, ts1_err);
    ST_COROUTINE_JOIN(ts2, ts2_err);
    ST_EXPECT_SUCCESS(ts1_err);
    ST_EXPECT_SUCCESS(ts2_err);
    EXPECT_EQ(2, signal_index);

    r0 = st_cond_destroy(cond);
    EXPECT_EQ(0, r0);
}

VOID TEST(LearnKB, CondBroadcastWakeAll)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    int broadcast_order[2] = {0, 0};
    int broadcast_index = 0;

    CondLearnCtx b1 = {cond, broadcast_order, &broadcast_index, 1, ST_UTEST_TIMEOUT};
    CondLearnCtx b2 = {cond, broadcast_order, &broadcast_index, 2, ST_UTEST_TIMEOUT};

    st_thread_t tb1 = st_thread_create(coroutine_cond_wait_and_record, &b1, 1, 0);
    st_thread_t tb2 = st_thread_create(coroutine_cond_wait_and_record, &b2, 1, 0);
    ASSERT_TRUE(tb1 && tb2);

    st_usleep(0);  // Let both waiters enter cond wait_q.

    int r0 = st_cond_broadcast(cond);
    ASSERT_EQ(0, r0);

    ST_COROUTINE_JOIN(tb1, tb1_err);
    ST_COROUTINE_JOIN(tb2, tb2_err);
    ST_EXPECT_SUCCESS(tb1_err);
    ST_EXPECT_SUCCESS(tb2_err);
    EXPECT_EQ(2, broadcast_index);

    r0 = st_cond_destroy(cond);
    EXPECT_EQ(0, r0);
}

VOID TEST(LearnKB, CondTimedwaitTimeout)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    CondLearnCtx t1 = {cond, NULL, NULL, 0, 10 * ST_UTIME_MILLISECONDS};
    st_thread_t tt1 = st_thread_create(coroutine_cond_wait_timeout, &t1, 1, 0);
    ASSERT_TRUE(tt1 != NULL);

    ST_COROUTINE_JOIN(tt1, tt1_err);
    ST_EXPECT_SUCCESS(tt1_err);

    int r0 = st_cond_destroy(cond);
    EXPECT_EQ(0, r0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_thread_exit coroutine termination workflow.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* coroutine_explicit_exit_with_retval(void* /*arg*/)
{
    st_thread_exit((void*)(intptr_t)123);
    return NULL;
}

VOID TEST(LearnKB, ThreadExitExplicitRetvalThroughJoin)
{
    st_thread_t trd = st_thread_create(coroutine_explicit_exit_with_retval, NULL, 1, 0);
    ASSERT_TRUE(trd != NULL);

    void* retval = NULL;
    int r0 = st_thread_join(trd, &retval);
    ASSERT_EQ(0, r0);
    EXPECT_EQ(123, (int)(intptr_t)retval);
}

static void* coroutine_nonjoinable_exit(void* arg)
{
    int* finished = (int*)arg;
    *finished = 1;
    return NULL;
}

VOID TEST(LearnKB, ThreadExitNonJoinableCannotJoin)
{
    int finished = 0;

    st_thread_t trd = st_thread_create(coroutine_nonjoinable_exit, &finished, 0, 0);
    ASSERT_TRUE(trd != NULL);

    errno = 0;
    int r0 = st_thread_join(trd, NULL);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EINVAL, errno);

    // Non-joinable thread still runs and exits on its own.
    st_usleep(0);
    EXPECT_EQ(1, finished);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for st_thread_interrupt wakeup workflow.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct InterruptSleepCtx {
    volatile int ready;
    int r0;
    int err;
};

static void* coroutine_interruptible_sleep(void* arg)
{
    InterruptSleepCtx* ctx = (InterruptSleepCtx*)arg;

    ctx->ready = 1;
    errno = 0;
    ctx->r0 = st_usleep(ST_UTEST_TIMEOUT);
    ctx->err = errno;

    return NULL;
}

VOID TEST(LearnKB, ThreadInterruptWakeupFromUsleep)
{
    InterruptSleepCtx ctx = {0, 0, 0};

    st_thread_t trd = st_thread_create(coroutine_interruptible_sleep, &ctx, 1, 0);
    ASSERT_TRUE(trd != NULL);

    while (!ctx.ready) {
        st_usleep(0);
    }
    st_usleep(0);  // Let target enter st_usleep wait state.

    st_thread_interrupt(trd);

    ST_COROUTINE_JOIN(trd, trd_err);
    ST_EXPECT_SUCCESS(trd_err);
    EXPECT_EQ(-1, ctx.r0);
    EXPECT_EQ(EINTR, ctx.err);
}

struct InterruptCondCtx {
    st_cond_t cond;
    volatile int ready;
    int r0;
    int err;
};

static void* coroutine_interruptible_condwait(void* arg)
{
    InterruptCondCtx* ctx = (InterruptCondCtx*)arg;

    ctx->ready = 1;
    errno = 0;
    ctx->r0 = st_cond_timedwait(ctx->cond, ST_UTIME_NO_TIMEOUT);
    ctx->err = errno;

    return NULL;
}

VOID TEST(LearnKB, ThreadInterruptWakeupFromCondWait)
{
    st_cond_t cond = st_cond_new();
    ASSERT_TRUE(cond != NULL);

    InterruptCondCtx ctx = {cond, 0, 0, 0};
    st_thread_t trd = st_thread_create(coroutine_interruptible_condwait, &ctx, 1, 0);
    ASSERT_TRUE(trd != NULL);

    while (!ctx.ready) {
        st_usleep(0);
    }
    st_usleep(0);  // Let target enter cond wait_q.

    st_thread_interrupt(trd);

    ST_COROUTINE_JOIN(trd, trd_err);
    ST_EXPECT_SUCCESS(trd_err);
    EXPECT_EQ(-1, ctx.r0);
    EXPECT_EQ(EINTR, ctx.err);

    int r0 = st_cond_destroy(cond);
    EXPECT_EQ(0, r0);
}

struct InterruptMutexCtx {
    st_mutex_t lock;
    volatile int ready;
    int r0;
    int err;
};

static void* coroutine_interruptible_mutex_lock(void* arg)
{
    InterruptMutexCtx* ctx = (InterruptMutexCtx*)arg;

    ctx->ready = 1;
    errno = 0;
    ctx->r0 = st_mutex_lock(ctx->lock);
    ctx->err = errno;

    return NULL;
}

VOID TEST(LearnKB, ThreadInterruptWakeupFromMutexWait)
{
    st_mutex_t lock = st_mutex_new();
    ASSERT_TRUE(lock != NULL);

    int r0 = st_mutex_lock(lock);
    ASSERT_EQ(0, r0);

    InterruptMutexCtx ctx = {lock, 0, 0, 0};
    st_thread_t trd = st_thread_create(coroutine_interruptible_mutex_lock, &ctx, 1, 0);
    ASSERT_TRUE(trd != NULL);

    while (!ctx.ready) {
        st_usleep(0);
    }
    st_usleep(0);  // Let target block in mutex wait_q.

    st_thread_interrupt(trd);

    ST_COROUTINE_JOIN(trd, trd_err);
    ST_EXPECT_SUCCESS(trd_err);
    EXPECT_EQ(-1, ctx.r0);
    EXPECT_EQ(EINTR, ctx.err);

    r0 = st_mutex_unlock(lock);
    EXPECT_EQ(0, r0);
    r0 = st_mutex_destroy(lock);
    EXPECT_EQ(0, r0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for _st_netfd_t abstraction workflow.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int g_netfd_destructor_calls = 0;

static void netfd_specific_destructor(void* arg)
{
    g_netfd_destructor_calls++;
    free(arg);
}

VOID TEST(LearnKB, NetfdSpecificAndDestructorOnClose)
{
    int fds[2] = {-1, -1};
    int r0 = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(0, r0);

    st_netfd_t stfd = st_netfd_open_socket(fds[0]);
    ASSERT_TRUE(stfd != NULL);

    // st_netfd_open_socket takes ownership of fds[0], keep peer as raw fd.
    int peer = fds[1];

    g_netfd_destructor_calls = 0;
    int* payload = (int*)malloc(sizeof(int));
    ASSERT_TRUE(payload != NULL);
    *payload = 2026;

    st_netfd_setspecific(stfd, payload, netfd_specific_destructor);
    void* got = st_netfd_getspecific(stfd);
    EXPECT_EQ(payload, got);

    r0 = st_netfd_close(stfd);
    EXPECT_EQ(0, r0);
    EXPECT_EQ(1, g_netfd_destructor_calls);

    ::close(peer);
}

VOID TEST(LearnKB, NetfdFreeKeepsOsfdOpen)
{
    int fds[2] = {-1, -1};
    int r0 = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(0, r0);

    st_netfd_t stfd = st_netfd_open_socket(fds[0]);
    ASSERT_TRUE(stfd != NULL);

    int osfd = fds[0];
    int peer = fds[1];

    g_netfd_destructor_calls = 0;
    int* payload = (int*)malloc(sizeof(int));
    ASSERT_TRUE(payload != NULL);
    *payload = 7;

    st_netfd_setspecific(stfd, payload, netfd_specific_destructor);

    // Free wrapper only: should trigger destructor but keep underlying osfd open.
    st_netfd_free(stfd);
    EXPECT_EQ(1, g_netfd_destructor_calls);

    errno = 0;
    int flags = fcntl(osfd, F_GETFD);
    EXPECT_NE(-1, flags);

    // Raw fd should still be usable.
    char ch = 'N';
    ssize_t n = ::write(peer, &ch, 1);
    EXPECT_EQ(1, n);

    char got = 0;
    n = ::read(osfd, &got, 1);
    EXPECT_EQ(1, n);
    EXPECT_EQ('N', got);

    ::close(osfd);
    ::close(peer);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for basic ST netfd read/write workflow.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VOID TEST(LearnKB, BasicNetfdWriteThenRead)
{
    int fds[2] = {-1, -1};
    int r0 = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(0, r0);

    st_netfd_t reader = st_netfd_open_socket(fds[0]);
    st_netfd_t writer = st_netfd_open_socket(fds[1]);
    ASSERT_TRUE(reader != NULL);
    ASSERT_TRUE(writer != NULL);

    const char* msg = "hello-st";
    ssize_t wn = st_write(writer, msg, 8, ST_UTEST_TIMEOUT);
    ASSERT_EQ(8, wn);

    char buf[16] = {0};
    ssize_t rn = st_read(reader, buf, 8, ST_UTEST_TIMEOUT);
    ASSERT_EQ(8, rn);
    EXPECT_STREQ("hello-st", buf);

    r0 = st_netfd_close(reader);
    EXPECT_EQ(0, r0);
    r0 = st_netfd_close(writer);
    EXPECT_EQ(0, r0);
}

VOID TEST(LearnKB, BasicNetfdReadTimeout)
{
    int fds[2] = {-1, -1};
    int r0 = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    ASSERT_EQ(0, r0);

    st_netfd_t reader = st_netfd_open_socket(fds[0]);
    st_netfd_t writer = st_netfd_open_socket(fds[1]);
    ASSERT_TRUE(reader != NULL);
    ASSERT_TRUE(writer != NULL);

    char ch = 0;
    errno = 0;
    ssize_t rn = st_read(reader, &ch, 1, 10 * ST_UTIME_MILLISECONDS);
    EXPECT_EQ(-1, rn);
    EXPECT_EQ(ETIME, errno);

    r0 = st_netfd_close(reader);
    EXPECT_EQ(0, r0);
    r0 = st_netfd_close(writer);
    EXPECT_EQ(0, r0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for _st_eventsys_t abstraction behavior (selected backend, immutable after st_init).
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VOID TEST(LearnKB, EventSysSelectedAndLockedAfterInit)
{
    // st_utest main already called st_set_eventsys(...) then st_init().
    // Here we verify what this abstraction guarantees at runtime.
    int active = st_get_eventsys();
    const char* name = st_get_eventsys_name();

    EXPECT_TRUE(active == ST_EVENTSYS_SELECT || active == ST_EVENTSYS_ALT);
    EXPECT_TRUE(name != NULL);
    EXPECT_GT((int)strlen(name), 0);

    // Once selected, eventsys cannot be changed again.
    errno = 0;
    int r0 = st_set_eventsys(ST_EVENTSYS_SELECT);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EBUSY, errno);

    errno = 0;
    r0 = st_set_eventsys(ST_EVENTSYS_ALT);
    EXPECT_EQ(-1, r0);
    EXPECT_EQ(EBUSY, errno);
}
