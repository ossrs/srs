/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Condition variables and mutexes, as SRS uses them: a task queue where a
 * producer signals consumers that wait in st_cond_wait, like
 * SrsAsyncCallWorker; a broadcast that wakes every waiting consumer, like a
 * live stream fan-out; a timed wait that times out; a mutex that guards a
 * shared counter while its owner yields; and st_mutex_trylock, interrupts
 * and destroying a cond or mutex that is still in use.
 */

#include "tool.h"

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* Let every other coroutine run until it blocks, up to n times, until *done. */
static void wait_for(int *done, int n)
{
    for (int i = 0; i < n && !*done; i++) {
        st_usleep(0);
    }
}

/*
 * A task queue, like SrsAsyncCallWorker: the producer appends a task and
 * signals, consumers wait in st_cond_wait while the queue is empty.
 */
#define QUEUE_CONSUMERS 3
#define QUEUE_TASKS 300

static struct {
    st_cond_t cond;
    int tasks[QUEUE_TASKS];
    int head;
    int tail;
    int quit;
    int done;
    long sum;
    int count[QUEUE_CONSUMERS];
} q;

static void *queue_consumer(void *arg)
{
    int id = (int)(long)arg;
    for (;;) {
        while (q.head == q.tail && !q.quit) {
            if (st_cond_wait(q.cond) != 0) {
                return (void *)1;
            }
        }
        if (q.head == q.tail) {
            break;
        }
        q.sum += q.tasks[q.head++];
        q.count[id]++;
        /* A task does some I/O, so the next one may go to another consumer. */
        if (q.count[id] % 7 == 0) {
            st_usleep(0);
        }
    }
    q.done++;
    return NULL;
}

static int queue(void)
{
    memset(&q, 0, sizeof(q));
    CHECK((q.cond = st_cond_new()) != NULL);

    st_thread_t consumers[QUEUE_CONSUMERS];
    for (int i = 0; i < QUEUE_CONSUMERS; i++) {
        CHECK((consumers[i] = st_thread_create(queue_consumer, (void *)(long)i, 1, 0)) != NULL);
    }

    /* Every consumer is waiting on the empty queue. */
    st_usleep(0);
    CHECK(q.done == 0);

    long want = 0;
    for (int i = 0; i < QUEUE_TASKS; i++) {
        q.tasks[q.tail++] = i + 1;
        want += i + 1;
        CHECK(st_cond_signal(q.cond) == 0);
        /* Sometimes push a burst before the consumers run. */
        if (i % 5 == 0) {
            st_usleep(0);
        }
    }

    /* Drain, then wake every consumer to quit. */
    for (int i = 0; i < 100 && q.head != q.tail; i++) {
        st_usleep(0);
    }
    CHECK(q.head == q.tail);
    q.quit = 1;
    CHECK(st_cond_broadcast(q.cond) == 0);

    int total = 0;
    for (int i = 0; i < QUEUE_CONSUMERS; i++) {
        void *ret = (void *)-1;
        CHECK(st_thread_join(consumers[i], &ret) == 0);
        CHECK(ret == NULL);
        total += q.count[i];
    }
    CHECK(q.done == QUEUE_CONSUMERS);
    CHECK(total == QUEUE_TASKS);
    CHECK(q.sum == want);
    CHECK(st_cond_destroy(q.cond) == 0);
    return 0;
}

/* st_cond_signal wakes one waiter, and a signal with no waiter is lost. */
#define ONE_WAITERS 3

static st_cond_t one_cond;
static int one_woken;

static void *one_waiter(void *arg)
{
    if (st_cond_timedwait(one_cond, BLOCK_US) != 0) {
        return (void *)1;
    }
    one_woken++;
    return NULL;
}

static int signal_one(void)
{
    CHECK((one_cond = st_cond_new()) != NULL);
    one_woken = 0;

    /* With no waiter, signal and broadcast do nothing, and are not kept. */
    CHECK(st_cond_signal(one_cond) == 0);
    CHECK(st_cond_broadcast(one_cond) == 0);
    errno = 0;
    CHECK(st_cond_timedwait(one_cond, 1000) == -1 && errno == ETIME);

    st_thread_t waiters[ONE_WAITERS];
    for (int i = 0; i < ONE_WAITERS; i++) {
        CHECK((waiters[i] = st_thread_create(one_waiter, NULL, 1, 0)) != NULL);
    }
    st_usleep(0);
    CHECK(one_woken == 0);

    /* Each signal wakes exactly one waiter. */
    for (int i = 0; i < ONE_WAITERS; i++) {
        CHECK(st_cond_signal(one_cond) == 0);
        st_usleep(0);
        st_usleep(0);
        CHECK(one_woken == i + 1);
    }

    for (int i = 0; i < ONE_WAITERS; i++) {
        void *ret = (void *)-1;
        CHECK(st_thread_join(waiters[i], &ret) == 0);
        CHECK(ret == NULL);
    }
    CHECK(st_cond_destroy(one_cond) == 0);
    return 0;
}

/*
 * A live stream fan-out: consumers wait on one cond, and each message the
 * source publishes is broadcast to all of them.
 */
#define FANOUT_CONSUMERS 16
#define FANOUT_MESSAGES 10

static struct {
    st_cond_t cond;
    int seq;
    int got[FANOUT_CONSUMERS];
    int all;
} fan;

static void *fanout_consumer(void *arg)
{
    int id = (int)(long)arg;
    int seen = 0;
    while (seen < FANOUT_MESSAGES) {
        while (fan.seq == seen) {
            if (st_cond_timedwait(fan.cond, BLOCK_US) != 0) {
                return (void *)1;
            }
        }
        /* One wake per message: no message is skipped or seen twice. */
        if (fan.seq != seen + 1) {
            return (void *)2;
        }
        seen = fan.seq;
        fan.got[id]++;
        fan.all++;
    }
    return NULL;
}

static int broadcast(void)
{
    memset(&fan, 0, sizeof(fan));
    CHECK((fan.cond = st_cond_new()) != NULL);

    st_thread_t consumers[FANOUT_CONSUMERS];
    for (int i = 0; i < FANOUT_CONSUMERS; i++) {
        CHECK((consumers[i] = st_thread_create(fanout_consumer, (void *)(long)i, 1, 0)) != NULL);
    }
    st_usleep(0);
    CHECK(fan.all == 0);

    for (int m = 1; m <= FANOUT_MESSAGES; m++) {
        fan.seq = m;
        CHECK(st_cond_broadcast(fan.cond) == 0);
        int want = m * FANOUT_CONSUMERS;
        for (int i = 0; i < 100 && fan.all != want; i++) {
            st_usleep(0);
        }
        CHECK(fan.all == want);
    }

    for (int i = 0; i < FANOUT_CONSUMERS; i++) {
        void *ret = (void *)-1;
        CHECK(st_thread_join(consumers[i], &ret) == 0);
        CHECK(ret == NULL);
        CHECK(fan.got[i] == FANOUT_MESSAGES);
    }
    CHECK(st_cond_destroy(fan.cond) == 0);
    return 0;
}

/* A timed wait times out with ETIME, or returns 0 when signaled in time. */
#define TIMEOUT_US (10 * 1000)

static st_cond_t timed_cond;
static int timed_rv = -2;

static void *timed_waiter(void *arg)
{
    timed_rv = st_cond_timedwait(timed_cond, BLOCK_US);
    return NULL;
}

static int timedwait(void)
{
    CHECK((timed_cond = st_cond_new()) != NULL);

    st_utime_t start = st_utime();
    errno = 0;
    CHECK(st_cond_timedwait(timed_cond, TIMEOUT_US) == -1 && errno == ETIME);
    CHECK(st_utime() - start >= TIMEOUT_US);

    /* A zero timeout also times out. */
    errno = 0;
    CHECK(st_cond_timedwait(timed_cond, 0) == -1 && errno == ETIME);

    /* Signaled before the timeout, the wait returns 0 at once. */
    st_thread_t t;
    CHECK((t = st_thread_create(timed_waiter, NULL, 1, 0)) != NULL);
    st_usleep(1000);
    CHECK(timed_rv == -2);
    start = st_utime();
    CHECK(st_cond_signal(timed_cond) == 0);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(timed_rv == 0);
    CHECK(st_utime() - start < BLOCK_US);

    CHECK(st_cond_destroy(timed_cond) == 0);
    return 0;
}

/*
 * A mutex guards a counter that each coroutine reads, then yields, then
 * writes back. Without the lock, the yields would lose updates.
 */
#define LOCK_WORKERS 8
#define LOCK_ROUNDS 50

static st_mutex_t lock;
static int counter;
static int inside;
static int max_inside;

static void *lock_worker(void *arg)
{
    for (int i = 0; i < LOCK_ROUNDS; i++) {
        if (st_mutex_lock(lock) != 0) {
            return (void *)1;
        }
        if (++inside > max_inside) {
            max_inside = inside;
        }

        int v = counter;
        if (i % 2) {
            st_thread_yield();
        } else {
            st_usleep(0);
        }
        counter = v + 1;

        inside--;
        if (st_mutex_unlock(lock) != 0) {
            return (void *)2;
        }
        st_thread_yield();
    }
    return NULL;
}

static int mutex(void)
{
    CHECK((lock = st_mutex_new()) != NULL);
    counter = inside = max_inside = 0;

    st_thread_t workers[LOCK_WORKERS];
    for (int i = 0; i < LOCK_WORKERS; i++) {
        CHECK((workers[i] = st_thread_create(lock_worker, NULL, 1, 0)) != NULL);
    }
    for (int i = 0; i < LOCK_WORKERS; i++) {
        void *ret = (void *)-1;
        CHECK(st_thread_join(workers[i], &ret) == 0);
        CHECK(ret == NULL);
    }
    CHECK(counter == LOCK_WORKERS * LOCK_ROUNDS);
    CHECK(max_inside == 1);

    CHECK(st_mutex_destroy(lock) == 0);
    return 0;
}

/* Only the owner unlocks, and the owner cannot lock again. */
static st_mutex_t owned;
static int other_rv;
static int other_errno;

static void *unlock_other(void *arg)
{
    errno = 0;
    other_rv = st_mutex_unlock(owned);
    other_errno = errno;
    return NULL;
}

static int ownership(void)
{
    CHECK((owned = st_mutex_new()) != NULL);

    errno = 0;
    CHECK(st_mutex_unlock(owned) == -1 && errno == EPERM);

    CHECK(st_mutex_lock(owned) == 0);
    errno = 0;
    CHECK(st_mutex_lock(owned) == -1 && errno == EDEADLK);

    st_thread_t t;
    CHECK((t = st_thread_create(unlock_other, NULL, 1, 0)) != NULL);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(other_rv == -1 && other_errno == EPERM);

    CHECK(st_mutex_unlock(owned) == 0);
    CHECK(st_mutex_destroy(owned) == 0);
    return 0;
}

/* st_mutex_trylock fails with EBUSY while another coroutine holds the mutex. */
static st_mutex_t trylocked;
static int try_rv;
static int try_errno;

static void *try_other(void *arg)
{
    errno = 0;
    try_rv = st_mutex_trylock(trylocked);
    try_errno = errno;
    if (try_rv == 0 && st_mutex_unlock(trylocked) != 0) {
        return (void *)1;
    }
    return NULL;
}

static int trylock(void)
{
    CHECK((trylocked = st_mutex_new()) != NULL);

    /* A free mutex is taken, and the owner unlocks it. */
    CHECK(st_mutex_trylock(trylocked) == 0);

    st_thread_t t;
    void *ret = (void *)-1;
    CHECK((t = st_thread_create(try_other, NULL, 1, 0)) != NULL);
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL);
    CHECK(try_rv == -1 && try_errno == EBUSY);

    CHECK(st_mutex_unlock(trylocked) == 0);

    /* Free again, another coroutine takes it. */
    ret = (void *)-1;
    CHECK((t = st_thread_create(try_other, NULL, 1, 0)) != NULL);
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL);
    CHECK(try_rv == 0);

    CHECK(st_mutex_destroy(trylocked) == 0);
    return 0;
}

/*
 * st_thread_interrupt wakes a coroutine waiting in st_cond_wait or
 * st_mutex_lock with EINTR, and the interrupted one does not get the mutex.
 */
static st_cond_t intr_cond;
static st_mutex_t intr_lock;
static int intr_rv;
static int intr_errno;

static void *intr_cond_waiter(void *arg)
{
    errno = 0;
    intr_rv = st_cond_wait(intr_cond);
    intr_errno = errno;
    return NULL;
}

static void *intr_lock_waiter(void *arg)
{
    errno = 0;
    intr_rv = st_mutex_lock(intr_lock);
    intr_errno = errno;
    return NULL;
}

static int interrupt(void)
{
    CHECK((intr_cond = st_cond_new()) != NULL);
    CHECK((intr_lock = st_mutex_new()) != NULL);

    st_thread_t t;
    intr_rv = -2;
    CHECK((t = st_thread_create(intr_cond_waiter, NULL, 1, 0)) != NULL);
    st_usleep(0);
    CHECK(intr_rv == -2);
    st_thread_interrupt(t);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(intr_rv == -1 && intr_errno == EINTR);
    CHECK(st_cond_destroy(intr_cond) == 0);

    CHECK(st_mutex_lock(intr_lock) == 0);
    intr_rv = -2;
    CHECK((t = st_thread_create(intr_lock_waiter, NULL, 1, 0)) != NULL);
    st_usleep(0);
    CHECK(intr_rv == -2);
    st_thread_interrupt(t);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(intr_rv == -1 && intr_errno == EINTR);

    /* The main thread still owns it, and no one waits. */
    CHECK(st_mutex_unlock(intr_lock) == 0);
    CHECK(st_mutex_trylock(intr_lock) == 0);
    CHECK(st_mutex_unlock(intr_lock) == 0);
    CHECK(st_mutex_destroy(intr_lock) == 0);
    return 0;
}

/* Destroying a cond or mutex still in use fails with EBUSY. */
static st_cond_t busy_cond;
static st_mutex_t busy_lock;
static int busy_done;

static void *busy_cond_waiter(void *arg)
{
    if (st_cond_timedwait(busy_cond, BLOCK_US) != 0) {
        return (void *)1;
    }
    busy_done = 1;
    return NULL;
}

static void *busy_lock_waiter(void *arg)
{
    if (st_mutex_lock(busy_lock) != 0) {
        return (void *)1;
    }
    /* Own it for a while, so destroying it is still busy. */
    st_usleep(0);
    if (st_mutex_unlock(busy_lock) != 0) {
        return (void *)2;
    }
    busy_done = 1;
    return NULL;
}

static int destroy(void)
{
    /* Free ones are destroyed at once. */
    st_cond_t c;
    st_mutex_t m;
    CHECK((c = st_cond_new()) != NULL);
    CHECK(st_cond_destroy(c) == 0);
    CHECK((m = st_mutex_new()) != NULL);
    CHECK(st_mutex_destroy(m) == 0);

    /* A cond with a waiter. */
    CHECK((busy_cond = st_cond_new()) != NULL);
    st_thread_t t;
    void *ret = (void *)-1;
    busy_done = 0;
    CHECK((t = st_thread_create(busy_cond_waiter, NULL, 1, 0)) != NULL);
    st_usleep(0);
    errno = 0;
    CHECK(st_cond_destroy(busy_cond) == -1 && errno == EBUSY);
    CHECK(st_cond_signal(busy_cond) == 0);
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL && busy_done);
    CHECK(st_cond_destroy(busy_cond) == 0);

    /* A mutex that is held, then held with a waiter, then held by the waiter. */
    CHECK((busy_lock = st_mutex_new()) != NULL);
    CHECK(st_mutex_lock(busy_lock) == 0);
    errno = 0;
    CHECK(st_mutex_destroy(busy_lock) == -1 && errno == EBUSY);

    ret = (void *)-1;
    busy_done = 0;
    CHECK((t = st_thread_create(busy_lock_waiter, NULL, 1, 0)) != NULL);
    st_usleep(0);
    errno = 0;
    CHECK(st_mutex_destroy(busy_lock) == -1 && errno == EBUSY);

    CHECK(st_mutex_unlock(busy_lock) == 0);
    errno = 0;
    CHECK(st_mutex_destroy(busy_lock) == -1 && errno == EBUSY);

    wait_for(&busy_done, 10);
    CHECK(st_thread_join(t, &ret) == 0);
    CHECK(ret == NULL && busy_done);
    CHECK(st_mutex_destroy(busy_lock) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

    CHECK(queue() == 0);
    CHECK(signal_one() == 0);
    CHECK(broadcast() == 0);
    CHECK(timedwait() == 0);
    CHECK(mutex() == 0);
    CHECK(ownership() == 0);
    CHECK(trylock() == 0);
    CHECK(interrupt() == 0);
    CHECK(destroy() == 0);

    printf("sync OK\n");
    return 0;
}
