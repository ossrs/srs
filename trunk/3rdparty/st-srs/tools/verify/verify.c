/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <stdio.h>

#include <st.h>

#define WAIT_US (100 * 1000)

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("ST: FAILED %s at line %d\n", #cond, __LINE__); \
        return 1; \
    } \
} while (0)

st_mutex_t lock;
st_cond_t cond;
st_utime_t signaled_at;

void* start(void* arg)
{
    printf("ST: thread run\n");

    printf("ST: thread wait for a while\n");
    st_usleep(WAIT_US);
    printf("ST: thread wait done\n");

    signaled_at = st_utime();
    int r0 = st_cond_signal(cond);
    printf("ST: thread cond signal, r0=%d\n", r0);

    printf("ST: thread lock\n");
    r0 = st_mutex_lock(lock);
    if (r0 != 0) return (void*)1;

    r0 = st_mutex_unlock(lock);
    printf("ST: thread unlock\n");

    return (void*)(long)r0;
}

int main(int argc, char** argv)
{
    int r0 = st_init();
    CHECK(r0 == 0);
    printf("ST: main init ok\n");

    lock = st_mutex_new();
    cond = st_cond_new();
    CHECK(lock && cond);

    st_utime_t created_at = st_utime();
    st_thread_t trd = st_thread_create(start, NULL, 1, 0);
    CHECK(trd);
    printf("ST: main create ok\n");

    printf("ST: main lock\n");
    r0 = st_mutex_lock(lock);
    CHECK(r0 == 0);

    printf("ST: main cond waiting\n");
    r0 = st_cond_wait(cond);
    printf("ST: main cond wait ok, r0=%d\n", r0);
    CHECK(r0 == 0);
    CHECK(signaled_at - created_at >= WAIT_US);

    r0 = st_mutex_unlock(lock);
    CHECK(r0 == 0);
    printf("ST: main unlock\n");

    void* res = (void*)-1;
    r0 = st_thread_join(trd, &res);
    CHECK(r0 == 0);
    CHECK(res == NULL);
    printf("ST: main done\n");

    CHECK(st_mutex_destroy(lock) == 0);
    CHECK(st_cond_destroy(cond) == 0);

    printf("ST: verify OK\n");
    return 0;
}
