//
// Copyright (c) 2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_CIRQUEUE_HPP
#define SRS_KERNEL_CIRQUEUE_HPP

#include <srs_core.hpp>

#include <srs_kernel_error.hpp>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define SRS_CONST_MAX_QUEUE_SIZE     1 * 1024 * 1024

#pragma pack(1)
struct SrsCirmemHeader
{
    uint32_t max_nr;
    
    volatile uint32_t curr_nr;
    
    volatile uint32_t read_pos;
    volatile uint32_t read_pos_r;
    
    volatile uint32_t write_pos;
    volatile uint32_t write_pos_r;
};
#pragma pack()

template <typename T>
class SrsCircleQueue
{
public:
    SrsCircleQueue() : buffer(NULL), info(NULL), data(NULL)
    {

    }

    ~SrsCircleQueue()
    {
        if (buffer) {
            free(buffer);
            buffer = NULL;
        }
    }

public:
    srs_error_t init(uint32_t max_count = 0)
    {
        srs_error_t err = srs_success;

        if (data) {
            return err;
        }
        if (max_count > SRS_CONST_MAX_QUEUE_SIZE) {
            return srs_error_new(ERROR_QUEUE_INIT, "size=%" PRIu64 " is too large", max_count);
        }

        uint32_t count = (max_count > 0) ? max_count : SRS_CONST_MAX_QUEUE_SIZE;
        uint32_t buffer_size = count * sizeof(T) + sizeof(SrsCirmemHeader);
        
        buffer = malloc(buffer_size);
        memset(buffer, 0, buffer_size);

        info = (SrsCirmemHeader *)buffer; 
        data = (T *)((char*)buffer + sizeof(SrsCirmemHeader));
        info->max_nr = count;
        info->curr_nr = 0;

        return err;
    }
    
    srs_error_t push(const T& item)
    {
        srs_error_t err = srs_success;

        if (!info || !data) {
            return srs_error_new(ERROR_QUEUE_PUSH, "queue init failed");
        }
        
        uint32_t current_pos, next_pos;
        
        do {
            current_pos = info->write_pos;
            
            next_pos = (current_pos + 1) % info->max_nr;  // head is length

            if (next_pos == info->read_pos_r) {
                return srs_error_new(ERROR_QUEUE_PUSH, "queue is full");
            }
        } while (!__sync_bool_compare_and_swap(&info->write_pos, current_pos, next_pos));

        data[current_pos] = item;

        while (!__sync_bool_compare_and_swap(&info->write_pos_r, current_pos, next_pos)) {
            sched_yield();
        }
        __sync_add_and_fetch(&info->curr_nr, 1);    // message num add 1
        
        return err;
    }

    // Get the message from the queue
    // Error if empty. Please use size() to check before pop().
    srs_error_t pop(T& out)
    {
        srs_error_t err = srs_success;

        if (!info || !data) {
            return srs_error_new(ERROR_QUEUE_POP, "queue init failed");
        }
        
        uint32_t current_pos, next_pos;
        do {
            current_pos = info->read_pos;
            if (current_pos == info->write_pos_r) {
                return srs_error_new(ERROR_QUEUE_POP, "queue is empty");
            }
            
            next_pos = (current_pos + 1) % info->max_nr;
        } while (!__sync_bool_compare_and_swap(&info->read_pos, current_pos, next_pos));

        out = data[current_pos];

        while (!__sync_bool_compare_and_swap(&info->read_pos_r, current_pos, next_pos)) {
            sched_yield();
        }
        __sync_sub_and_fetch(&info->curr_nr, 1);    // message num sub 1

        return err;
    }

    unsigned size() const
    {
        if (!info || !data) {
            return 0;
        }
        return info->curr_nr;
    }

private:
    SrsCircleQueue(const SrsCircleQueue&);
    const SrsCircleQueue& operator=(const SrsCircleQueue&);
private:
    void* buffer;
    SrsCirmemHeader *info;
    T* data;
    
};
#endif

