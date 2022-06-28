/*
g++ mutex-queue.cpp -g -O0 -o mutex-queue && ./mutex-queue 1 1 1
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
#include<string.h>

#define SRS_UTIME_MILLISECONDS 1000
#define srsu2i(us) ((int)(us))
#define srsu2ms(us) ((us) / SRS_UTIME_MILLISECONDS)
#define srsu2msi(us) int((us) / SRS_UTIME_MILLISECONDS)
#define srs_freep(p) delete p; (void)0
#define srs_freepa(pa) delete[] pa; (void)0

int64_t srs_update_system_time()
{
    timeval now;
    ::gettimeofday(&now, NULL);
    return ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
}

// The default capacity of queue.
#define SRS_CONST_MAX_QUEUE_SIZE 1 * 1024 * 1024

// The metadata for circle queue.
#pragma pack(1)
struct SrsCircleQueueMetadata
{
    // The number of capacity of queue, the max of elements.
    uint32_t nn_capacity;
    // The number of elems in queue.
    volatile uint32_t nn_elems;

    // The position we're reading, like a token we got to read.
    volatile uint32_t reading_token;
    // The position already read, ok for writting.
    volatile uint32_t read_already;

    // The position we're writing, like a token we got to write.
    volatile uint32_t writing_token;
    // The position already written, ready for reading.
    volatile uint32_t written_already;

    SrsCircleQueueMetadata() : nn_capacity(0), nn_elems(0), reading_token(0), read_already(0), writing_token(0), written_already(0) {
    }
};
#pragma pack()

// A lockless queue using CAS(Compare and Swap).
template <typename T>
class SrsLocklessQueue
{
public:
    SrsLocklessQueue(uint32_t capacity = 0) {
        info = new SrsCircleQueueMetadata();
        data = NULL;
        initialize(capacity);
        pthread_mutex_init(&lock_, NULL);
    }

    ~SrsLocklessQueue() {
        srs_freep(info);
        srs_freepa(data);
        pthread_mutex_destroy(&lock_);
    }

private:
    void initialize(uint32_t capacity) {
        uint32_t count = (capacity > 0) ? capacity : SRS_CONST_MAX_QUEUE_SIZE;

        // We increase an element for reserved.
        count += 1;

        data = new T[count];
        info->nn_capacity = count;
        info->nn_elems = 0;
    }

public:
    // Push the elem to the end of queue.
    int push(const T& elem) {
        if (!info || !data) {
            return -1;
        }

        pthread_mutex_lock(&lock_);

        uint32_t current = info->writing_token;
        uint32_t next = (current + 1) % info->nn_capacity;
        if (next == info->read_already) {
            pthread_mutex_unlock(&lock_);
            return -1;
        }

        data[current] = elem;
        info->writing_token = next;
        info->written_already = next;
        info->nn_elems++;
        pthread_mutex_unlock(&lock_);
        return 0;
    }

    // Remove and return the message from the font of queue.
    // Error if empty. Please use size() to check before shift().
    int shift(T& elem) {
        if (!info || !data) {
            return -1;
        }

        pthread_mutex_lock(&lock_);

        uint32_t current = info->reading_token;
        uint32_t next = (current + 1) % info->nn_capacity;
        if (current == info->written_already) {
            pthread_mutex_unlock(&lock_);
            return -1;
        }

        elem = data[current];
        info->reading_token = next;
        info->read_already = next;
        info->nn_elems--;
        pthread_mutex_unlock(&lock_);
        return 0;
    }

    unsigned int size() const {
        return (!info || !data) ? 0 : info->nn_elems;
    }

private:
    SrsLocklessQueue(const SrsLocklessQueue&);
    const SrsLocklessQueue& operator=(const SrsLocklessQueue&);

private:
    pthread_mutex_t lock_;
    SrsCircleQueueMetadata* info;
    T* data;
};

struct StMsg {
    char data[8];
    void operator=(const StMsg& src) {
        memcpy(data, src.data, sizeof(data));
    }
};

int nn_loops = 0;
int r_wait = 0;
SrsLocklessQueue<StMsg> queue;

struct ThreadInfo {
    pthread_t trd;
    int id;
    bool is_write;
    int64_t  errs;
    ThreadInfo(int tid, bool write) : errs(0) {
        id = tid;
        is_write = write;
    }
};

void* pfn(void* args) {
    ThreadInfo* info = (ThreadInfo*)args;
    //printf("thread #%d: start write=%d\n", info->id, info->is_write);

    int64_t start = srs_update_system_time();
    int64_t loop = ((int64_t)nn_loops) * 1000;
    StMsg src;
    for (int64_t i = 0; i < loop; ) {
        int r0;
        if (info->is_write) {
            r0 = queue.push(src);
        } else {
            while (r_wait && !queue.size()) {
                timespec tv = {0};
                tv.tv_nsec = r_wait;
                nanosleep(&tv, NULL);
            }
            StMsg elem;
            r0 = queue.shift(elem);
        }

        if (r0) {
            info->errs++;
        } else {
            i++;
        }
    }

    int64_t now = srs_update_system_time();
    printf("thread #%d: done, write=%d, errs=%" PRId64 ", cost=%dms\n", info->id, info->is_write, info->errs, srsu2msi(now - start));
    return NULL;
}

int main(int argc, char** argv) {
    if (argc <= 4) {
        printf("Usage: %s w_threads r_threads m_loops r_wait\n", argv[0]);
        printf("    w_threads    The number of threads to write to queue\n");
        printf("    r_threads    The number of threads to read from queue\n");
        printf("    m_loops      The number of loops to run, in m(1000*1000)\n");
        printf("    r_wait       If no message, wait in ns. 0: disable wait.\n");
        exit(-1);
    }

    int64_t start = srs_update_system_time();

    int nn_w_threads = ::atoi(argv[1]);
    int nn_r_threads = ::atoi(argv[2]);
    nn_loops = ::atoi(argv[3]);
    r_wait = ::atoi(argv[4]);
    printf("mutex-queue w_threads=%d, r_threads=%d, loops=%d(m), r_wait=%d(ns)\n", nn_w_threads, nn_r_threads, nn_loops, r_wait);

    ThreadInfo** trds = new ThreadInfo*[nn_w_threads + nn_r_threads];
    int r = 0, w = 0;
    while (w < nn_w_threads || r < nn_r_threads) {
        if (w < nn_w_threads) {
            ThreadInfo* info = new ThreadInfo(w, true);
            trds[w] = info;
            pthread_create(&info->trd, NULL, pfn, (void*)info);
            w++;
        }
        if (r < nn_r_threads) {
            ThreadInfo* info = new ThreadInfo(r, false);
            trds[nn_w_threads + r] = info;
            pthread_create(&info->trd, NULL, pfn, (void*)info);
            r++;
        }
    }

    int64_t errs = 0;
    for (int i = 0; i < nn_w_threads + nn_r_threads; i++) {
        ThreadInfo* info = trds[i];
        pthread_join(info->trd, NULL);
        errs += info->errs;
    }

    int64_t now = srs_update_system_time();
    printf("done: w_threads=%d, r_threads=%d, errs=%" PRId64 " cost=%dms\n", nn_w_threads, nn_r_threads, errs, srsu2msi(now - start));

    return 0;
}

