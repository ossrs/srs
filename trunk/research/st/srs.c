#include <unistd.h>
#include <stdio.h>

#include "public.h"

#define srs_trace(msg, ...)   printf(msg, ##__VA_ARGS__);printf("\n")

st_cond_t cond = NULL;
st_mutex_t mutex = NULL;

void* pfn(void* arg)
{
    st_usleep(100 * 1000);
    st_cond_signal(cond);
    
    st_mutex_lock(mutex);
    srs_trace("2. st mutex is ok");
    st_mutex_unlock(mutex);
    
    st_usleep(100 * 1000);
    srs_trace("3. st thread is ok");
    st_cond_signal(cond);
    
    return NULL;
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

    if ((cond = st_cond_new()) == NULL) {
        srs_trace("st_cond_new failed");
        return -1;
    }
    
    if ((mutex = st_mutex_new()) == NULL) {
        srs_trace("st_mutex_new failed");
        return -1;
    }
    
    if (!st_thread_create(pfn, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    // lock mutex to control thread.
    st_mutex_lock(mutex);
    
    // wait thread to ready.
    st_cond_wait(cond);
    srs_trace("1. st cond is ok");
    
    // release mutex to control thread
    st_usleep(100 * 1000);
    st_mutex_unlock(mutex);
    
    // wait thread to exit.
    st_cond_wait(cond);
    srs_trace("4. st is ok");
    
    // cleanup.
    st_cond_destroy(cond);
    st_mutex_destroy(mutex);
    st_thread_exit(NULL);
    
    return 0;
}

