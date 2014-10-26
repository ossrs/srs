#include <unistd.h>
#include <stdio.h>

#include "public.h"

#define srs_trace(msg, ...)   printf(msg, ##__VA_ARGS__);printf("\n")

st_mutex_t sync_start = NULL;
st_cond_t sync_cond = NULL;
st_mutex_t sync_mutex = NULL;

void* sync_master(void* arg)
{
    // wait for main to sync_start this thread.
    st_mutex_lock(sync_start);
    st_mutex_unlock(sync_start);
    
    st_usleep(100 * 1000);
    st_cond_signal(sync_cond);
    
    st_mutex_lock(sync_mutex);
    srs_trace("2. st mutex is ok");
    st_mutex_unlock(sync_mutex);
    
    st_usleep(100 * 1000);
    srs_trace("3. st thread is ok");
    st_cond_signal(sync_cond);
    
    return NULL;
}

void* sync_slave(void* arg)
{
    // lock mutex to control thread.
    st_mutex_lock(sync_mutex);
    
    // wait for main to sync_start this thread.
    st_mutex_lock(sync_start);
    st_mutex_unlock(sync_start);
    
    // wait thread to ready.
    st_cond_wait(sync_cond);
    srs_trace("1. st cond is ok");
    
    // release mutex to control thread
    st_usleep(100 * 1000);
    st_mutex_unlock(sync_mutex);
    
    // wait thread to exit.
    st_cond_wait(sync_cond);
    srs_trace("4. st is ok");
    
    return NULL;
}

int sync_test()
{
    if ((sync_start = st_mutex_new()) == NULL) {
        srs_trace("st_mutex_new sync_start failed");
        return -1;
    }
    st_mutex_lock(sync_start);

    if ((sync_cond = st_cond_new()) == NULL) {
        srs_trace("st_cond_new cond failed");
        return -1;
    }
    
    if ((sync_mutex = st_mutex_new()) == NULL) {
        srs_trace("st_mutex_new mutex failed");
        return -1;
    }
    
    if (!st_thread_create(sync_master, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    if (!st_thread_create(sync_slave, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    // run all threads.
    st_mutex_unlock(sync_start);
    
    return 0;
}

int main(int argc, char** argv)
{
    if (st_set_eventsys(ST_EVENTSYS_ALT) < 0) {
        srs_trace("st_set_eventsys failed");
        return -1;
    }
    
    if (st_init() < 0) {
        srs_trace("st_init failed");
        return -1;
    }
    
    if (sync_test() < 0) {
        srs_trace("sync_test failed");
        return -1;
    }
    
    // cleanup.
    st_thread_exit(NULL);
    
    return 0;
}

