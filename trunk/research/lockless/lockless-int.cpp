/*
g++ lockless-int.cpp -g -O0 -o lockless-int && ./lockless-int 1 0 1
*/

// For int64_t print using PRId64 format.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#define SRS_UTIME_MILLISECONDS 1000
#define srsu2i(us) ((int)(us))
#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)

int64_t srs_update_system_time()
{
    timeval now;
    ::gettimeofday(&now, NULL);
    return ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
}

int nn_loops = 0;
int use_lock = 0;

int64_t loops_done = 0;

void* pfn(void* args) {
    int id = (int)(long long)args;
    printf("thread #%d: start\n", id);

    int64_t start = srs_update_system_time();
    int64_t loop = ((int64_t)nn_loops) * 1000 * 1000;
    for (int64_t i = 0; i < loop; i++) {
        if (!use_lock) {
            loops_done++;
        } else {
            __sync_fetch_and_add(&loops_done, 1);
        }
    }

    int64_t now = srs_update_system_time();
    printf("thread #%d: done, cost=%dms\n", id, srsu2msi(now - start));
    return NULL;
}

int main(int argc, char** argv) {
    if (argc <= 3) {
        printf("Usage: %s threads lock m_loops\n", argv[0]);
        printf("    threads     The number of threads to start\n");
        printf("    lock        Whether use lock(CAS) to sync threads\n");
        printf("    m_loops     The number of loops to run, in m(1000*1000)\n");
        exit(-1);
    }

    int64_t start = srs_update_system_time();

    int nn_threads = ::atoi(argv[1]);
    use_lock = ::atoi(argv[2]);
    nn_loops = ::atoi(argv[3]);
    printf("lockless-int threads=%d, use_lock=%d, loops=%d(m)\n", nn_threads, use_lock, nn_loops);

    pthread_t trds[nn_threads];
    for (int i = 0; i < nn_threads; i++) {
        pthread_create(&trds[i], NULL, pfn, (void*)(long long)i);
    }

    for (int i = 0; i < nn_threads; i++) {
        pthread_join(trds[i], NULL);
    }

    int64_t now = srs_update_system_time();
    printf("done: threads=%d, loops_done=%" PRId64 ", cost=%dms\n", nn_threads, loops_done, srsu2msi(now - start));

    return 0;
}

