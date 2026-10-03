/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * Coroutine specific data, as SRS keeps its context id: one key created
 * with a destructor, a heap value per coroutine set by the coroutine with
 * st_thread_setspecific or by another one with st_thread_setspecific2, read
 * with st_thread_getspecific, and freed by the destructor when it is
 * replaced or when the coroutine exits. Also the key limit.
 */

#include "tool.h"

/* Long enough that a coroutine still blocked here is a failure, not a slow run. */
#define BLOCK_US (5 * 1000 * 1000)

/* A heap value, like SRS's SrsContextId. */
typedef struct {
    int id;
} ctx_t;

static int freed;
static int freed_ids;

static ctx_t *ctx_new(int id)
{
    ctx_t *ctx = (ctx_t *)malloc(sizeof(ctx_t));
    if (ctx) {
        ctx->id = id;
    }
    return ctx;
}

/* The key destructor, like _srs_context_destructor. */
static void ctx_free(void *arg)
{
    ctx_t *ctx = (ctx_t *)arg;
    freed++;
    freed_ids += ctx->id;
    free(ctx);
}

/* The key with ctx_free, and one without a destructor. */
static int key = -1;
static int plain = -1;

/* Before any key exists, every key is invalid. */
static int no_keys(void)
{
    int v = 0;
    CHECK(st_thread_getspecific(0) == NULL);
    CHECK(st_thread_getspecific(-1) == NULL);
    errno = 0;
    CHECK(st_thread_setspecific(0, &v) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(st_thread_setspecific2(st_thread_self(), 0, &v) == -1 && errno == EINVAL);
    return 0;
}

static int create(void)
{
    CHECK(st_key_create(&key, ctx_free) == 0);
    CHECK(st_key_create(&plain, NULL) == 0);
    CHECK(key >= 0 && plain >= 0 && key != plain);
    CHECK(key < st_key_getlimit() && plain < st_key_getlimit());

    /* A key past the created ones is still invalid. */
    int next = key > plain ? key + 1 : plain + 1;
    int v = 0;
    CHECK(st_thread_getspecific(next) == NULL);
    errno = 0;
    CHECK(st_thread_setspecific(next, &v) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(st_thread_setspecific(-1, &v) == -1 && errno == EINVAL);
    return 0;
}

/* Replacing a value frees the old one, setting the same one does not. */
static int replace(void)
{
    CHECK(st_thread_getspecific(key) == NULL);

    ctx_t *a = ctx_new(1);
    ctx_t *b = ctx_new(2);
    CHECK(a && b);

    freed = freed_ids = 0;
    CHECK(st_thread_setspecific(key, a) == 0);
    CHECK(st_thread_getspecific(key) == a);
    CHECK(st_thread_setspecific(key, a) == 0);
    CHECK(freed == 0);

    CHECK(st_thread_setspecific(key, b) == 0);
    CHECK(st_thread_getspecific(key) == b);
    CHECK(freed == 1 && freed_ids == 1);

    CHECK(st_thread_setspecific(key, NULL) == 0);
    CHECK(st_thread_getspecific(key) == NULL);
    CHECK(freed == 2 && freed_ids == 3);

    /* Without a destructor, a replaced value is only dropped. */
    int x = 1, y = 2;
    CHECK(st_thread_setspecific(plain, &x) == 0);
    CHECK(st_thread_setspecific(plain, &y) == 0);
    CHECK(st_thread_getspecific(plain) == &y);
    CHECK(st_thread_setspecific(plain, NULL) == 0);
    CHECK(st_thread_getspecific(plain) == NULL);
    CHECK(freed == 2);
    return 0;
}

#define NN_WORKERS 8
#define NN_YIELDS 5

/*
 * Set its own value, then yield between the other workers, and still read
 * its own value each time. Returns 1 when every read matched.
 */
static void *do_own(void *arg)
{
    int id = (int)(long)arg;
    if (st_thread_getspecific(key) != NULL || st_thread_getspecific(plain) != NULL) {
        return NULL;
    }

    ctx_t *ctx = ctx_new(id);
    if (!ctx || st_thread_setspecific(key, ctx) != 0) {
        return NULL;
    }
    if (st_thread_setspecific(plain, arg) != 0) {
        return NULL;
    }

    for (int i = 0; i < NN_YIELDS; i++) {
        st_thread_yield();
        ctx_t *v = (ctx_t *)st_thread_getspecific(key);
        if (v != ctx || v->id != id || st_thread_getspecific(plain) != arg) {
            return NULL;
        }
    }
    return (void *)1;
}

/* Values stay per coroutine, and each one is freed when its coroutine exits. */
static int per_coroutine(void)
{
    ctx_t *mine = ctx_new(100);
    CHECK(mine);
    CHECK(st_thread_setspecific(key, mine) == 0);

    st_thread_t trds[NN_WORKERS];
    for (int i = 0; i < NN_WORKERS; i++) {
        trds[i] = st_thread_create(do_own, (void *)(long)(i + 1), 1, 0);
        CHECK(trds[i]);
    }

    freed = freed_ids = 0;
    int ids = 0;
    for (int i = 0; i < NN_WORKERS; i++) {
        void *retval = NULL;
        CHECK(st_thread_join(trds[i], &retval) == 0);
        CHECK(retval == (void *)1);
        ids += i + 1;
    }
    CHECK(freed == NN_WORKERS && freed_ids == ids);

    /* The workers did not touch the primordial thread's value. */
    CHECK(st_thread_getspecific(key) == mine);
    CHECK(st_thread_setspecific(key, NULL) == 0);
    CHECK(freed == NN_WORKERS + 1);
    return 0;
}

/* Return the id of the value another coroutine set on this one. */
static void *do_read(void *arg)
{
    ctx_t *v = (ctx_t *)st_thread_getspecific(key);
    return (void *)(long)(v ? v->id : 0);
}

/* Wait on the cond, then return the id of the value set while it waited. */
static void *do_wait(void *arg)
{
    ctx_t *before = (ctx_t *)st_thread_getspecific(key);
    if (!before || before->id != 1) {
        return NULL;
    }
    if (st_cond_timedwait((st_cond_t)arg, BLOCK_US) != 0) {
        return NULL;
    }
    ctx_t *after = (ctx_t *)st_thread_getspecific(key);
    return (void *)(long)(after ? after->id : 0);
}

/* st_thread_setspecific2 sets a value on another coroutine, as SrsFastCoroutine::set_cid does. */
static int set_other(void)
{
    /* Set on a new coroutine before it runs. */
    ctx_t *a = ctx_new(7);
    CHECK(a);
    st_thread_t trd = st_thread_create(do_read, NULL, 1, 0);
    CHECK(trd);
    CHECK(st_thread_setspecific2(trd, key, a) == 0);
    CHECK(st_thread_getspecific(key) == NULL);

    freed = freed_ids = 0;
    void *retval = NULL;
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 7);
    CHECK(freed == 1 && freed_ids == 7);

    /* Replace the value of a blocked coroutine, which frees the old one. */
    st_cond_t cond = st_cond_new();
    CHECK(cond);
    ctx_t *b = ctx_new(1);
    ctx_t *c = ctx_new(2);
    CHECK(b && c);
    trd = st_thread_create(do_wait, cond, 1, 0);
    CHECK(trd);
    CHECK(st_thread_setspecific2(trd, key, b) == 0);
    CHECK(st_usleep(100) == 0);

    freed = freed_ids = 0;
    CHECK(st_thread_setspecific2(trd, key, c) == 0);
    CHECK(freed == 1 && freed_ids == 1);
    CHECK(st_cond_signal(cond) == 0);
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK((long)retval == 2);
    CHECK(freed == 2 && freed_ids == 3);

    CHECK(st_cond_destroy(cond) == 0);
    return 0;
}

/* Set a value, then leave in the way arg says. */
static void exit_nested(void)
{
    st_thread_exit(NULL);
}

static void *do_leave(void *arg)
{
    long how = (long)arg;
    ctx_t *ctx = ctx_new(1);
    if (!ctx || st_thread_setspecific(key, ctx) != 0) {
        return NULL;
    }
    if (how == 1) {
        exit_nested();
    } else if (how == 2) {
        st_usleep(BLOCK_US);
    } else if (how == 3) {
        st_usleep(100);
    }
    return NULL;
}

/* Destructors run on every way a coroutine exits, before st_thread_join returns. */
static int destructors(void)
{
    freed = freed_ids = 0;

    /* Return from the start function. */
    st_thread_t trd = st_thread_create(do_leave, (void *)0, 1, 0);
    CHECK(trd);
    CHECK(st_thread_join(trd, NULL) == 0);
    CHECK(freed == 1);

    /* st_thread_exit from a nested call. */
    trd = st_thread_create(do_leave, (void *)1, 1, 0);
    CHECK(trd);
    CHECK(st_thread_join(trd, NULL) == 0);
    CHECK(freed == 2);

    /* Interrupted in st_usleep, as SrsFastCoroutine::stop does. */
    trd = st_thread_create(do_leave, (void *)2, 1, 0);
    CHECK(trd);
    CHECK(st_usleep(100) == 0);
    CHECK(freed == 2);
    st_thread_interrupt(trd);
    CHECK(st_thread_join(trd, NULL) == 0);
    CHECK(freed == 3);

    /* A detached coroutine. */
    trd = st_thread_create(do_leave, (void *)3, 0, 0);
    CHECK(trd);
    for (int i = 0; i < 100 && freed < 4; i++) {
        CHECK(st_usleep(1000) == 0);
    }
    CHECK(freed == 4);

    /* A new coroutine, which may reuse a freed stack, starts with no values. */
    trd = st_thread_create(do_read, NULL, 1, 0);
    CHECK(trd);
    void *retval = (void *)-1;
    CHECK(st_thread_join(trd, &retval) == 0);
    CHECK(retval == NULL);
    CHECK(freed == 4 && freed_ids == 4);
    return 0;
}

/* Create keys up to st_key_getlimit(); the next one fails with EAGAIN. */
static int limit(void)
{
    int limit = st_key_getlimit();
    CHECK(limit > 2);

    /* The keys made so far are key and plain. */
    int keys[limit];
    keys[0] = key;
    keys[1] = plain;
    for (int i = 2; i < limit; i++) {
        CHECK(st_key_create(&keys[i], NULL) == 0);
    }

    int k = -1;
    errno = 0;
    CHECK(st_key_create(&k, NULL) == -1 && errno == EAGAIN);
    CHECK(k == -1);

    /* Every key is distinct, in range, and holds its own value. */
    for (int i = 0; i < limit; i++) {
        CHECK(keys[i] >= 0 && keys[i] < limit);
        for (int j = 0; j < i; j++) {
            CHECK(keys[i] != keys[j]);
        }
    }
    for (int i = 2; i < limit; i++) {
        CHECK(st_thread_setspecific(keys[i], &keys[i]) == 0);
    }
    for (int i = 2; i < limit; i++) {
        CHECK(st_thread_getspecific(keys[i]) == &keys[i]);
        CHECK(st_thread_setspecific(keys[i], NULL) == 0);
    }
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(tool_init() == 0);

    CHECK(no_keys() == 0);
    CHECK(create() == 0);
    CHECK(replace() == 0);
    CHECK(per_coroutine() == 0);
    CHECK(set_other() == 0);
    CHECK(destructors() == 0);
    CHECK(limit() == 0);

    printf("key OK\n");
    return 0;
}
