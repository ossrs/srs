/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_HOURGLASS_HPP
#define SRS_APP_HOURGLASS_HPP

#include <srs_core.hpp>

#include <srs_app_st.hpp>

#include <map>

class SrsCoroutine;

// The handler for the tick.
class ISrsHourGlass
{
public:
    ISrsHourGlass();
    virtual ~ISrsHourGlass();
public:
    // When time is ticked, this function is called.
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick) = 0;
};

// The hourglass(timer or SrsTimer) for special tasks,
// while these tasks are attached to some intervals, for example,
// there are N=3 tasks bellow:
//          1. A heartbeat every 3s.
//          2. A print message every 5s.
//          3. A notify backend every 7s.
// The hourglass will call back when ticks:
//          1. Got notify(event=1, time=3)
//          2. Got notify(event=2, time=5)
//          3. Got notify(event=1, time=6)
//          4. Got notify(event=3, time=7)
//          5. Got notify(event=1, time=9)
//          6. Got notify(event=2, time=10)
// It's a complex and high-performance timer.
//
// Usage:
//      SrsHourGlass* hg = new SrsHourGlass(handler, 1 * SRS_UTIME_MILLISECONDS);
//
//      hg->tick(1, 3 * SRS_UTIME_MILLISECONDS);
//      hg->tick(2, 5 * SRS_UTIME_MILLISECONDS);
//      hg->tick(3, 7 * SRS_UTIME_MILLISECONDS);
//
//      // The hg will create a thread for timer.
//      hg->start();
class SrsHourGlass : virtual public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd;
    ISrsHourGlass* handler;
    srs_utime_t _resolution;
    // The ticks:
    //      key: the event of tick.
    //      value: the interval of tick.
    std::map<int, srs_utime_t> ticks;
    // The total elapsed time,
    // for each cycle, we increase it with a resolution.
    srs_utime_t total_elapse;
public:
    SrsHourGlass(ISrsHourGlass* h, srs_utime_t resolution);
    virtual ~SrsHourGlass();
public:
    // Start the hourglass.
    virtual srs_error_t start();
public:
    // Add a pair of tick(event, interval).
    // @param event the event of tick, default is 0.
    // @param interval the interval in srs_utime_t of tick.
    virtual srs_error_t tick(srs_utime_t interval);
    virtual srs_error_t tick(int event, srs_utime_t interval);
public:
    // Cycle the hourglass, which will sleep resolution every time.
    // and call handler when ticked.
    virtual srs_error_t cycle();
};

#endif
