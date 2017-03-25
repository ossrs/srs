/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 SRS(ossrs)
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

/**
 * the handler for the tick.
 */
class ISrsHourGlass
{
public:
    ISrsHourGlass();
    virtual ~ISrsHourGlass();
public:
    /**
     * notify the handler, the type and tick.
     */
    virtual int notify(int type, int interval, int64_t tick) = 0;
};

/**
 * the hourglass used to do some specieal task,
 * while these task is cycle when some interval, for example,
 * there are N=3 tasks to do:
 *          1. heartbeat every 3s.
 *          2. print message every 5s.
 *          3. notify backend every 7s.
 * the hourglass will call back when ticks:
 *          1. notify(type=1, time=3)
 *          2. notify(type=2, time=5)
 *          3. notify(type=1, time=6)
 *          4. notify(type=3, time=7)
 *          5. notify(type=1, time=9)
 *          6. notify(type=2, time=10)
 * this is used for server and bocar server and other manager.
 *
 * Usage:
 *      SrsHourGlass* hg = new SrsHourGlass(handler, 1000);
 *      hg->tick(1, 3000);
 *      hg->tick(2, 5000);
 *      hg->tick(3, 7000);
 *      // create a thread to cycle, which will call handerl when ticked.
 *      while (true) {
 *          hg->cycle();
 *      }
 */
class SrsHourGlass
{
private:
    ISrsHourGlass* handler;
    int resolution;
    // key: the type of tick.
    // value: the interval of tick.
    std::map<int, int> ticks;
    // the total elapsed time,
    // for each cycle, we increase it with a resolution.
    int64_t total_elapse;
public:
    SrsHourGlass(ISrsHourGlass* h, int resolution_ms);
    virtual ~SrsHourGlass();
public:
    // add a pair of tick(type, interval).
    // @param type the type of tick.
    // @param interval the interval in ms of tick.
    virtual int tick(int type, int interval);
public:
    // cycle the hourglass, which will sleep resolution every time.
    // and call handler when ticked.
    virtual int cycle();
};

#endif
