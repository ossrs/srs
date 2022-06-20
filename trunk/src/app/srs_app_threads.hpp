//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_THREADS_HPP
#define SRS_APP_THREADS_HPP

#include <srs_core.hpp>

#include <srs_app_hourglass.hpp>
#include <srs_kernel_error.hpp>

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <string>

class SrsThreadPool;
class SrsProcSelfStat;

// Protect server in high load.
class SrsCircuitBreaker : public ISrsFastTimer
{
private:
    // The config for high/critical water level.
    bool enabled_;
    int high_threshold_;
    int high_pulse_;
    int critical_threshold_;
    int critical_pulse_;
    int dying_threshold_;
    int dying_pulse_;
private:
    // Reset the water-level when CPU is low for N times.
    // @note To avoid the CPU change rapidly.
    int hybrid_high_water_level_;
    int hybrid_critical_water_level_;
    int hybrid_dying_water_level_;
public:
    SrsCircuitBreaker();
    virtual ~SrsCircuitBreaker();
public:
    srs_error_t initialize();
public:
    // Whether hybrid server water-level is high.
    bool hybrid_high_water_level();
    bool hybrid_critical_water_level();
    bool hybrid_dying_water_level();
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

extern SrsCircuitBreaker* _srs_circuit_breaker;

// Initialize global shared variables cross all threads.
extern srs_error_t srs_global_initialize();

// The thread mutex wrapper, without error.
class SrsThreadMutex
{
private:
    pthread_mutex_t lock_;
    pthread_mutexattr_t attr_;
public:
    SrsThreadMutex();
    virtual ~SrsThreadMutex();
public:
    void lock();
    void unlock();
};

// The thread mutex locker.
// TODO: FIXME: Rename _SRS to _srs
#define SrsThreadLocker(instance) \
    impl__SrsThreadLocker _SRS_free_##instance(instance)

class impl__SrsThreadLocker
{
private:
    SrsThreadMutex* lock;
public:
    impl__SrsThreadLocker(SrsThreadMutex* l) {
        lock = l;
        lock->lock();
    }
    virtual ~impl__SrsThreadLocker() {
        lock->unlock();
    }
};

// The information for a thread.
class SrsThreadEntry
{
public:
    SrsThreadPool* pool;
    std::string label;
    std::string name;
    srs_error_t (*start)(void* arg);
    void* arg;
    int num;
    // @see https://man7.org/linux/man-pages/man2/gettid.2.html
    pid_t tid;
public:
    // The thread object.
    pthread_t trd;
    // The exit error of thread.
    srs_error_t err;
public:
    SrsThreadEntry();
    virtual ~SrsThreadEntry();
};

// Allocate a(or almost) fixed thread poll to execute tasks,
// so that we can take the advantage of multiple CPUs.
class SrsThreadPool
{
private:
    SrsThreadEntry* entry_;
    srs_utime_t interval_;
private:
    SrsThreadMutex* lock_;
    std::vector<SrsThreadEntry*> threads_;
private:
    // The hybrid server entry, the cpu percent used for circuit breaker.
    SrsThreadEntry* hybrid_;
    std::vector<SrsThreadEntry*> hybrids_;
private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int pid_fd;
public:
    SrsThreadPool();
    virtual ~SrsThreadPool();
public:
    // Setup the thread-local variables.
    static srs_error_t setup_thread_locals();
    // Initialize the thread pool.
    srs_error_t initialize();
private:
    // Require the PID file for the whole process.
    virtual srs_error_t acquire_pid_file();
public:
    // Execute start function with label in thread.
    srs_error_t execute(std::string label, srs_error_t (*start)(void* arg), void* arg);
    // Run in the primordial thread, util stop or quit.
    srs_error_t run();
    // Stop the thread pool and quit the primordial thread.
    void stop();
public:
    SrsThreadEntry* self();
    SrsThreadEntry* hybrid();
    std::vector<SrsThreadEntry*> hybrids();
private:
    static void* start(void* arg);
};

// It MUST be thread-safe, global and shared object.
extern SrsThreadPool* _srs_thread_pool;

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
    SrsLocklessQueue() {
        info = new SrsCircleQueueMetadata();
        data = NULL;
    }

    ~SrsLocklessQueue() {
        srs_freep(info);
        srs_freepa(data);
    }

public:
    srs_error_t initialize(uint32_t max_count = 0) {
        srs_error_t err = srs_success;

        if (data) {
            return err;
        }
        if (max_count > SRS_CONST_MAX_QUEUE_SIZE) {
            return srs_error_new(ERROR_QUEUE_INIT, "size=%" PRIu64 " is too large", max_count);
        }

        uint32_t count = (max_count > 0) ? max_count : SRS_CONST_MAX_QUEUE_SIZE;
        // We increase an element for reserved.
        count += 1;

        data = new T[count];
        info->nn_capacity = count;
        info->nn_elems = 0;

        return err;
    }

    // Push the elem to the end of queue.
    srs_error_t push(const T& elem) {
        srs_error_t err = srs_success;

        if (!info || !data) {
            return srs_error_new(ERROR_QUEUE_PUSH, "queue init failed");
        }

        // Find out a position to write at current, using CAS to ensure we got a dedicated one.
        uint32_t current, next;
        do {
            current = info->writing_token;
            next = (current + 1) % info->nn_capacity;

            if (next == info->read_already) {
                return srs_error_new(ERROR_QUEUE_PUSH, "queue is full");
            }
        } while (!__sync_bool_compare_and_swap(&info->writing_token, current, next));

        // Then, we write the elem at current position, note that it's not readable.
        data[current] = elem;

        // Set the written elem to be ready to read, here CAS false means we are not the "minimum" position, so we
        // switch to other threads to finish the written. For example:
        //      Thread #1, current=10, next=11, written_already=10, switch to #2
        //      Thread #2, current=11, next=12, written_already=10, CAS is false and switch to #1
        //      Thread #1, current=10, written_already=10, CAS is true and update the written_already=11, switch to #2
        //      Thread #2, current=11, written_already=11, CAS is true, and update the written_already=12.
        // It keep the written_already like increasing by 1, similar to in a queue.
        while (!__sync_bool_compare_and_swap(&info->written_already, current, next)) {
            sched_yield();
        }

        // Done, we have already written an elem, increase the size of queue.
        __sync_add_and_fetch(&info->nn_elems, 1);

        return err;
    }

    // Remove and return the message from the font of queue.
    // Error if empty. Please use size() to check before shift().
    srs_error_t shift(T& elem) {
        srs_error_t err = srs_success;

        if (!info || !data) {
            return srs_error_new(ERROR_QUEUE_POP, "queue init failed");
        }

        // Find out a position to read at current, using CAS to ensure we got a dedicated one.
        uint32_t current, next;
        do {
            current = info->reading_token;
            next = (current + 1) % info->nn_capacity;

            if (current == info->written_already) {
                return srs_error_new(ERROR_QUEUE_POP, "queue is empty");
            }
        } while (!__sync_bool_compare_and_swap(&info->reading_token, current, next));

        // Then, we read the elem out at the current position, note that it's not writable.
        elem = data[current];

        // Set the read elem to be ready to write, see written_already of push.
        while (!__sync_bool_compare_and_swap(&info->read_already, current, next)) {
            sched_yield();
        }

        // Done, we have already read an elem, decrease the size of queue.
        __sync_sub_and_fetch(&info->nn_elems, 1);

        return err;
    }

    unsigned int size() const {
        return (!info || !data) ? 0 : info->nn_elems;
    }

private:
    SrsLocklessQueue(const SrsLocklessQueue&);
    const SrsLocklessQueue& operator=(const SrsLocklessQueue&);

private:
    SrsCircleQueueMetadata* info;
    T* data;
};

#endif

