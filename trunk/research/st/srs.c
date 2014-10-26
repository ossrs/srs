#include <unistd.h>
#include <stdio.h>

#include "public.h"

#define srs_trace(msg, ...)   printf(msg, ##__VA_ARGS__);printf("\n")

void* pfn(void* arg)
{
    st_sleep(1);
    srs_trace("st thread is ok");
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
    
    if (!st_thread_create(pfn, NULL, 0, 0)) {
        srs_trace("st_thread_create failed");
        return -1;
    }
    
    srs_trace("st is ok");
    
    st_thread_exit(NULL);
    
    return 0;
}

