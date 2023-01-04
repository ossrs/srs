/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef INC_SRT_SYNC_ATOMIC_CLOCK_H
#define INC_SRT_SYNC_ATOMIC_CLOCK_H

#include "sync.h"
#include "atomic.h"

namespace srt
{
namespace sync
{

template <class Clock>
class AtomicDuration
{
    atomic<int64_t> dur;
    typedef typename Clock::duration duration_type;
    typedef typename Clock::time_point time_point_type;
public:

    AtomicDuration() ATR_NOEXCEPT : dur(0) {}

    duration_type load()
    {
        int64_t val = dur.load();
        return duration_type(val);
    }

    void store(const duration_type& d)
    {
        dur.store(d.count());
    }

    AtomicDuration<Clock>& operator=(const duration_type& s)
    {
        dur = s.count();
        return *this;
    }

    operator duration_type() const
    {
        return duration_type(dur);
    }
};

template <class Clock>
class AtomicClock
{
    atomic<uint64_t> dur;
    typedef typename Clock::duration duration_type;
    typedef typename Clock::time_point time_point_type;
public:

    AtomicClock() ATR_NOEXCEPT : dur(0) {}

    time_point_type load() const
    {
        int64_t val = dur.load();
        return time_point_type(duration_type(val));
    }

    void store(const time_point_type& d)
    {
        dur.store(uint64_t(d.time_since_epoch().count()));
    }

    AtomicClock& operator=(const time_point_type& s)
    {
        dur = s.time_since_epoch().count();
        return *this;
    }

    operator time_point_type() const
    {
        return time_point_type(duration_type(dur.load()));
    }
};

} // namespace sync
} // namespace srt

#endif // INC_SRT_SYNC_ATOMIC_CLOCK_H
