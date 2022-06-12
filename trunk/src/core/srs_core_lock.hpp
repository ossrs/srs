//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_CORE_LOCK_HPP
#define SRS_CORE_LOCK_HPP

#include <pthread.h>

#include <srs_core.hpp>

#include <stdlib.h>

class SrsScopeLock
{
private:
    pthread_mutex_t* mutex_;
    bool locked_;

public:
    explicit SrsScopeLock(pthread_mutex_t* mutex)
    {
        mutex_ = mutex;
        locked_ = false;

        lock();
    }

    ~SrsScopeLock()
    {
        unlock();
    }

    void lock()
    {
        if (locked_) {
            return;
        }

        locked_ = true;
        pthread_mutex_lock(mutex_);
    }

    void unlock()
    {
        if (! locked_) {
            return;
        }

        locked_ = false;
        pthread_mutex_unlock(mutex_);
    }
};

#endif
