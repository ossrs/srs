//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_thread_pool.hpp>
#include <pthread.h>

static srs_error_t dummy_loop(void*) {
    return srs_success;
}

#ifndef SRS_CYGWIN64

VOID TEST(ThreadPoolTest, tid) {
#if 0
    srs_error_t err;
    SrsThreadPool* thread_pool_1 = new SrsThreadPool();
    SrsThreadPool* thread_pool_2 = new SrsThreadPool();

    EXPECT_TRUE((err = thread_pool_1->initialize()) == srs_success);
    srs_freep(err);
    
    EXPECT_TRUE((err = thread_pool_2->initialize()) == srs_success);
    srs_freep(err);
    
    EXPECT_TRUE((err = thread_pool_1->execute("hybrid", dummy_loop, (void*)NULL)) == srs_success);
    srs_freep(err);
    
    EXPECT_TRUE((err = thread_pool_2->execute("hybrid", dummy_loop, (void*)NULL)) == srs_success);
    srs_freep(err);

    err = thread_pool_1->run();
    srs_freep(err);
    err = thread_pool_2->run();
    srs_freep(err);

    EXPECT_GT(thread_pool_1->hybrid()->tid, 0);
    EXPECT_GT(thread_pool_2->hybrid()->tid, 0);

    EXPECT_NE(thread_pool_1->hybrid()->tid, thread_pool_2->hybrid()->tid);

    srs_freep(thread_pool_1);
    srs_freep(thread_pool_2);
#endif

    srs_error_t err;
    EXPECT_TRUE((err = _srs_thread_pool->initialize()) == srs_success);
    srs_freep(err);

    //EXPECT_TRUE((err = _srs_thread_pool->execute("hybrid", dummy_loop, (void*)NULL)) == srs_success);
    //srs_freep(err);

    //err = _srs_thread_pool->run();
    srs_freep(err);
}

#endif
