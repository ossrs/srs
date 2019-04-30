/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#include <map>

// The handler for the tick.
class ISrsHourGlass
{
public:
    ISrsHourGlass();
    virtual ~ISrsHourGlass();
public:
    // When time is ticked, this function is called.
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick) = 0;
};

// he hourglass used to do some specieal task,
// while these task is cycle when some interval, for example,
// there are N=3 tasks to do:
//          1. heartbeat every 3s.
//          2. print message every 5s.
//          3. notify backend every 7s.
// The hourglass will call back when ticks:
//          1. notify(type=1, time=3)
//          2. notify(type=2, time=5)
//          3. notify(type=1, time=6)
//          4. notify(type=3, time=7)
//          5. notify(type=1, time=9)
//          6. notify(type=2, time=10)
// This is used for server and bocar server and other manager.
//
// Usage:
//      SrsHourGlass* hg = new SrsHourGlass(handler, 1 * SRS_UTIME_MILLISECONDS);
//      hg->tick(1, 3 * SRS_UTIME_MILLISECONDS);
//      hg->tick(2, 5 * SRS_UTIME_MILLISECONDS);
//      hg->tick(3, 7 * SRS_UTIME_MILLISECONDS);
//      // create a thread to cycle, which will call handerl when ticked.
//      while (true) {
//          hg->cycle();
//      }
class SrsHourGlass
{
private:
    ISrsHourGlass* handler;
    srs_utime_t _resolution;
    // The ticks:
    //      key: the type of tick.
    //      value: the interval of tick.
    std::map<int, srs_utime_t> ticks;
    // The total elapsed time,
    // for each cycle, we increase it with a resolution.
    srs_utime_t total_elapse;
public:
    SrsHourGlass(ISrsHourGlass* h, srs_utime_t resolution);
    virtual ~SrsHourGlass();
public:
    // Add a pair of tick(type, interval).
    // @param type the type of tick.
    // @param interval the interval in srs_utime_t of tick.
    virtual srs_error_t tick(int type, srs_utime_t interval);
public:
    // Cycle the hourglass, which will sleep resolution every time.
    // and call handler when ticked.
    virtual srs_error_t cycle();
};

#endif
