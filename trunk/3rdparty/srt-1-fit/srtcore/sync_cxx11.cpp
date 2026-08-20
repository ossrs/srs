/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#include "platform_sys.h"

#include <iomanip>
#include <math.h>
#include <stdexcept>
#include "sync.h"
#include "srt_compat.h"
#include "common.h"

////////////////////////////////////////////////////////////////////////////////
//
// Clock frequency helpers
//
////////////////////////////////////////////////////////////////////////////////

namespace {
template <int val>
int pow10();

template <>
int pow10<10>()
{
    return 1;
}

template <int val>
int pow10()
{
    return 1 + pow10<val / 10>();
}
}

int srt::sync::clockSubsecondPrecision()
{
    const int64_t ticks_per_sec = (srt::sync::steady_clock::period::den / srt::sync::steady_clock::period::num);
    const int     decimals      = pow10<ticks_per_sec>();
    return decimals;
}

////////////////////////////////////////////////////////////////////////////////
//
// SyncCond (based on stl chrono C++11)
//
////////////////////////////////////////////////////////////////////////////////

srt::sync::Condition::Condition() {}

srt::sync::Condition::~Condition() {}

void srt::sync::Condition::init() {}

void srt::sync::Condition::reset()
{
    // SRT attempts to safely handle `fork()` in multithreaded environments,
    // even though using `fork()` in such contexts is strongly discouraged.
    // This is because `fork()` only duplicates the calling thread, leaving
    // synchronization primitives (like condition variables) in an
    // undefined or inconsistent state in the child process.
    //
    // To mitigate this, SRT forcefully reinitializes these synchronization
    // primitives post-fork. In POSIX, this is done by overwriting the object
    // with its default-initialized state. In C++11, we achieve the same effect
    // using *placement new* to reconstruct the object in place. This ensures
    // the condition variable is returned to a fresh, "neutral" state,
    // as if it was just created.

    new (&m_cv) std::condition_variable;
}

void srt::sync::Condition::destroy() {}

void srt::sync::Condition::wait(UniqueLock& lock)
{
    m_cv.wait(lock);
}

bool srt::sync::Condition::wait_for(UniqueLock& lock, const steady_clock::duration& rel_time)
{
    // Another possible implementation is wait_until(steady_clock::now() + timeout);
    return m_cv.wait_for(lock, rel_time) != std::cv_status::timeout;
}

bool srt::sync::Condition::wait_until(UniqueLock& lock, const steady_clock::time_point& timeout_time)
{
    return m_cv.wait_until(lock, timeout_time) != std::cv_status::timeout;
}

void srt::sync::Condition::notify_one()
{
    m_cv.notify_one();
}

void srt::sync::Condition::notify_all()
{
    m_cv.notify_all();
}

////////////////////////////////////////////////////////////////////////////////
//
// CThreadError class - thread local storage error wrapper
//
////////////////////////////////////////////////////////////////////////////////

// Threal local error will be used by CUDTUnited
// with a static scope, therefore static thread_local
static thread_local srt::CUDTException s_thErr;

void srt::sync::SetThreadLocalError(const srt::CUDTException& e)
{
    s_thErr = e;
}

srt::CUDTException& srt::sync::GetThreadLocalError()
{
    return s_thErr;
}

