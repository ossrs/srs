/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2022 Winlin */

#include <stdio.h>

#include <st.h>
#include <assert.h>

st_mutex_t lock;
st_cond_t cond;

void* start(void* arg)
{
    printf("ST: thread run\n");

    printf("ST: thread wait for a while\n");
    st_usleep(1.5 * 1000 * 1000);
    printf("ST: thread wait done\n");

    int r0 = st_cond_signal(cond);
    printf("ST: thread cond signal, r0=%d\n", r0);

    printf("ST: thread lock\n");
    r0 = st_mutex_lock(lock);
    assert(r0 == 0);

    r0 = st_mutex_unlock(lock);
    printf("ST: thread unlock\n");

    return NULL;
}

int main(int argc, char** argv)
{
    int r0 = st_init();
    assert(r0 == 0);
    printf("ST: main init ok\n");

    lock = st_mutex_new();
    cond = st_cond_new();

    st_thread_t trd = st_thread_create(start, NULL, 1, 0);
    printf("ST: main create ok\n");

    printf("ST: main lock\n");
    r0 = st_mutex_lock(lock);
    assert(r0 == 0);

    printf("ST: main cond waiting\n");
    r0 = st_cond_wait(cond);
    printf("ST: main cond wait ok, r0=%d\n", r0);

    r0 = st_mutex_unlock(lock);
    printf("ST: main unlock\n");

    st_thread_join(trd, NULL);
    printf("ST: main done\n");

    st_mutex_destroy(lock);
    st_cond_destroy(cond);

    return 0;
}

