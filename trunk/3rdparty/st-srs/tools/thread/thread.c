/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Coroutines, as SRS runs them: joinable coroutines that return a value to
 * st_thread_join, detached ones, the default and a custom stack size,
 * st_thread_self, the st_thread_yield order, and the stop of SrsSTCoroutine,
 * which interrupts a coroutine blocked in st_usleep and joins it. Also
 * st_randomize_stacks, the switch callbacks, and the DEBUG functions
 * _st_iterate_threads and _st_show_thread_stack.
 */

#include "tool.h"

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A coroutine that returns its argument plus one. */
static void *do_return(void *arg)
{
    return (void *)((long)arg + 1);
}

/* Exit from a nested call, so st_thread_exit never returns to the start function. */
static void exit_nested(long v)
{
    st_thread_exit((void *)(v * 2));
}

static void *do_exit(void *arg)
{
    exit_nested((long)arg);
    return (void *)-1;
}

/* The handle st_thread_self returns inside a coroutine. */
static void *do_self(void *arg)
{
    *(st_thread_t *)arg = st_thread_self();
    return NULL;
}

/* A joinable coroutine joining itself fails with EDEADLK. */
static void *do_join_self(void *arg)
{
    errno = 0;
    int r0 = st_thread_join(st_thread_self(), NULL);
    return (void *)(long)(r0 == -1 && errno == EDEADLK);
}

static int detached_done;

static void *do_detached(void *arg)
{
    if (st_usleep(100) == 0) {
        detached_done++;
    }
    return NULL;
}

/* Use most of the stack, and return a sum of it so nothing is optimized away. */
static void *do_stack(void *arg)
{
    long size = (long)arg;
    volatile char buf[64 * 1024];
    long sum = 0;
    for (long i = 0; i < size; i++) {
        buf[i] = (char)i;
    }
    for (long i = 0; i < size; i++) {
        sum += buf[i];
    }
    return (void *)sum;
}

static long stack_sum(long size)
{
    long sum = 0;
    for (long i = 0; i < size; i++) {
        sum += (char)i;
    }
    return sum;
}

static int join_and_exit(void)
{
    st_thread_t trd = st_thread_create(do_return, (void *)41, 1, 0);
    CHECK(trd);
    void *retval = NULL;
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 42);

    trd = st_thread_create(do_exit, (void *)21, 1, 0);
    CHECK(trd);
    retval = NULL;
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 42);

    /* A join without a place for the value still waits for the coroutine. */
    trd = st_thread_create(do_return, NULL, 1, 0);
    CHECK(trd);
    CHECK(st_thread_join(trd, NULL) == 0);

    trd = st_thread_create(do_join_self, NULL, 1, 0);
    CHECK(trd);
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 1);

    /* Many joinable coroutines at once, joined in creation order. */
    st_thread_t trds[64];
    for (long i = 0; i < 64; i++) {
        trds[i] = st_thread_create(do_return, (void *)i, 1, 0);
        CHECK(trds[i]);
    }
    for (long i = 0; i < 64; i++) {
        CHECK(st_thread_join(trds[i], &retval) == 0);
        CHECK((long)retval == i + 1);
    }
    return 0;
}

static int detached(void)
{
    detached_done = 0;
    st_thread_t trds[8];
    for (int i = 0; i < 8; i++) {
        trds[i] = st_thread_create(do_detached, NULL, 0, 0);
        CHECK(trds[i]);
    }

    /* A detached coroutine cannot be joined. */
    errno = 0;
    CHECK(st_thread_join(trds[0], NULL) == -1 && errno == EINVAL);

    /* They run and exit by themselves. */
    for (int i = 0; i < 100 && detached_done < 8; i++) {
        CHECK(st_usleep(1000) == 0);
    }
    CHECK(detached_done == 8);
    return 0;
}

static int self(void)
{
    st_thread_t primordial = st_thread_self();
    CHECK(primordial);
    CHECK(st_thread_self() == primordial);

    st_thread_t seen = NULL;
    st_thread_t trd = st_thread_create(do_self, &seen, 1, 0);
    CHECK(trd);
    CHECK(st_thread_join(trd, NULL) == 0);
    CHECK(seen == trd);
    CHECK(seen != primordial);
    CHECK(st_thread_self() == primordial);
    return 0;
}

static int stacks(void)
{
    /* The default stack holds 16 KB of locals. */
    void *retval = NULL;
    st_thread_t trd = st_thread_create(do_stack, (void *)(16 * 1024L), 1, 0);
    CHECK(trd);
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == stack_sum(16 * 1024));

    /* A custom stack, not a page multiple, holds 64 KB of locals. */
    trd = st_thread_create(do_stack, (void *)(64 * 1024L), 1, 256 * 1024 + 100);
    CHECK(trd);
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == stack_sum(64 * 1024));
    return 0;
}

#define ROUNDS 3
#define YIELDERS 3

static int order[ROUNDS * YIELDERS];
static int norder;

static void *do_yield(void *arg)
{
    for (int i = 0; i < ROUNDS; i++) {
        order[norder++] = (int)(long)arg;
        st_thread_yield();
    }
    return NULL;
}

/* Ready coroutines run in turn: each yield moves the caller to the end of the queue. */
static int yield_order(void)
{
    norder = 0;
    st_thread_t trds[YIELDERS];
    for (long i = 0; i < YIELDERS; i++) {
        trds[i] = st_thread_create(do_yield, (void *)i, 1, 0);
        CHECK(trds[i]);
    }
    for (int i = 0; i < YIELDERS; i++) {
        CHECK(st_thread_join(trds[i], NULL) == 0);
    }

    CHECK(norder == ROUNDS * YIELDERS);
    for (int i = 0; i < ROUNDS * YIELDERS; i++) {
        CHECK(order[i] == i % YIELDERS);
    }

    /* A yield with nothing else ready returns at once. */
    st_thread_yield();
    return 0;
}

/* A worker loop like an SrsSTCoroutine cycle: work, then sleep, until interrupted. */
static void *do_worker(void *arg)
{
    int *loops = (int *)arg;
    while (1) {
        (*loops)++;
        if (st_usleep(BLOCK_US) < 0) {
            return (void *)(long)errno;
        }
    }
}

/* An interrupt that comes before the sleep is kept, and only the first sleep sees it. */
static void *do_pending(void *arg)
{
    if (st_usleep(BLOCK_US) == 0 || errno != EINTR) {
        return (void *)1;
    }
    if (st_usleep(100) != 0) {
        return (void *)2;
    }
    return NULL;
}

static int interrupt(void)
{
    /* The SrsSTCoroutine stop: interrupt the sleeping worker, then join it. */
    int loops = 0;
    st_thread_t trd = st_thread_create(do_worker, &loops, 1, 0);
    CHECK(trd);
    CHECK(st_usleep(1000) == 0);
    CHECK(loops == 1);

    st_utime_t starttime = st_utime();
    st_thread_interrupt(trd);
    void *retval = NULL;
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == EINTR);
    CHECK(loops == 1);
    CHECK(st_utime() - starttime < BLOCK_US / 10);

    /* Interrupt a coroutine before it runs. */
    trd = st_thread_create(do_pending, NULL, 1, 0);
    CHECK(trd);
    st_thread_interrupt(trd);
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 0);

    /* Interrupting a coroutine that already finished does nothing, and it still joins. */
    trd = st_thread_create(do_return, (void *)1, 1, 0);
    CHECK(trd);
    CHECK(st_usleep(100) == 0);
    st_thread_interrupt(trd);
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 2);
    return 0;
}

static int randomize(void)
{
    CHECK(st_randomize_stacks(1) == 0);
    CHECK(st_randomize_stacks(1) == 1);

    void *retval = NULL;
    st_thread_t trds[8];
    for (long i = 0; i < 8; i++) {
        trds[i] = st_thread_create(do_stack, (void *)(16 * 1024L), 1, 0);
        CHECK(trds[i]);
    }
    for (int i = 0; i < 8; i++) {
        CHECK(st_thread_join(trds[i], &retval) == 0);
        CHECK((long)retval == stack_sum(16 * 1024));
    }

    /* Stacks made while it was on are reused after it is off. */
    CHECK(st_randomize_stacks(0) == 1);
    for (long i = 0; i < 8; i++) {
        trds[i] = st_thread_create(do_stack, (void *)(16 * 1024L), 1, 0);
        CHECK(trds[i]);
    }
    for (int i = 0; i < 8; i++) {
        CHECK(st_thread_join(trds[i], &retval) == 0);
        CHECK((long)retval == stack_sum(16 * 1024));
    }
    CHECK(st_randomize_stacks(0) == 0);
    return 0;
}

/* Switches into and out of each counted coroutine, seen by the callbacks. */
static st_thread_t counted[2];
static int ins[2], outs[2], prim_in, prim_out;

static void count_in(void)
{
    st_thread_t me = st_thread_self();
    if (me == counted[0] || me == counted[1]) {
        ins[me == counted[1]]++;
    } else {
        prim_in++;
    }
}

static void count_out(void)
{
    st_thread_t me = st_thread_self();
    if (me == counted[0] || me == counted[1]) {
        outs[me == counted[1]]++;
    } else {
        prim_out++;
    }
}

#define SWITCH_YIELDS 5

static void *do_switch(void *arg)
{
    for (int i = 0; i < SWITCH_YIELDS; i++) {
        st_thread_yield();
    }
    return NULL;
}

/* Run two coroutines that yield in turn, with the primordial thread waiting in a join. */
static int run_counted(void)
{
    counted[0] = counted[1] = NULL;
    memset(ins, 0, sizeof(ins));
    memset(outs, 0, sizeof(outs));
    prim_in = prim_out = 0;

    st_thread_t a = st_thread_create(do_switch, NULL, 1, 0);
    CHECK(a);
    st_thread_t b = st_thread_create(do_switch, NULL, 1, 0);
    CHECK(b);
    counted[0] = a;
    counted[1] = b;
    CHECK(st_thread_join(a, NULL) == 0);
    CHECK(st_thread_join(b, NULL) == 0);
    counted[0] = counted[1] = NULL;
    return 0;
}

static int switch_callbacks(void)
{
    CHECK(st_set_switch_in_cb(count_in) == NULL);
    CHECK(st_set_switch_out_cb(count_out) == NULL);

    CHECK(run_counted() == 0);
    for (int i = 0; i < 2; i++) {
        /* At least the first run and once after each yield, in and out. */
        CHECK(ins[i] >= SWITCH_YIELDS + 1);
        CHECK(outs[i] >= SWITCH_YIELDS + 1);
    }
    /* The primordial thread switches out to the join and back in after it. */
    CHECK(prim_in > 0 && prim_out > 0);
    printf("ST: switches in=%d,%d out=%d,%d primordial in=%d out=%d\n",
        ins[0], ins[1], outs[0], outs[1], prim_in, prim_out);

    /* Setting NULL returns the callback, and turns it off. */
    CHECK(st_set_switch_in_cb(NULL) == count_in);
    CHECK(st_set_switch_out_cb(NULL) == count_out);

    CHECK(run_counted() == 0);
    CHECK(ins[0] == 0 && ins[1] == 0 && outs[0] == 0 && outs[1] == 0);
    CHECK(prim_in == 0 && prim_out == 0);
    return 0;
}

static void *do_block(void *arg)
{
    return (void *)(long)(st_cond_timedwait((st_cond_t)arg, BLOCK_US) == 0);
}

/* The DEBUG functions run on live coroutines, blocked, ready and running. */
static int debug(void)
{
    st_cond_t cond = st_cond_new();
    CHECK(cond);

    st_thread_t blocked = st_thread_create(do_block, cond, 1, 0);
    CHECK(blocked);
    CHECK(st_usleep(100) == 0);
    st_thread_t ready = st_thread_create(do_return, (void *)1, 1, 0);
    CHECK(ready);

    _st_show_thread_stack(blocked, "blocked");
    _st_show_thread_stack(ready, NULL);
    _st_show_thread_stack(st_thread_self(), "self");
    _st_iterate_threads();

    void *retval = NULL;
    CHECK(st_cond_signal(cond) == 0);
    CHECK(st_thread_join(blocked, &retval) == 0);
    CHECK((long)retval == 1);
    CHECK(st_thread_join(ready, &retval) == 0);
    CHECK((long)retval == 2);

    CHECK(st_cond_destroy(cond) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

    CHECK(join_and_exit() == 0);
    CHECK(detached() == 0);
    CHECK(self() == 0);
    CHECK(stacks() == 0);
    CHECK(yield_order() == 0);
    CHECK(interrupt() == 0);
    CHECK(randomize() == 0);
    CHECK(switch_callbacks() == 0);
    CHECK(debug() == 0);

    printf("thread OK\n");
    return 0;
}
