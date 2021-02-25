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

#ifndef SRS_KERNEL_KBPS_HPP
#define SRS_KERNEL_KBPS_HPP

#include <srs_core.hpp>

#include <srs_kernel_kbps.hpp>

class SrsWallClock;

// A sample for rate-based stat, such as kbps or kps.
class SrsRateSample
{
public:
    int64_t total;
    srs_utime_t time;
    // kbps or kps
    int rate;
public:
    SrsRateSample();
    virtual ~SrsRateSample();
public:
    virtual SrsRateSample* update(int64_t nn, srs_utime_t t, int k);
};

// A pps manager every some duration.
class SrsPps
{
private:
    SrsWallClock* clk_;
private:
    // samples
    SrsRateSample sample_10s_;
    SrsRateSample sample_30s_;
    SrsRateSample sample_1m_;
    SrsRateSample sample_5m_;
    SrsRateSample sample_60m_;
public:
    // Sugar for target to stat.
    int64_t sugar;
public:
    SrsPps();
    virtual ~SrsPps();
public:
    // Setup the clock, use global clock if not set.
    void set_clock(SrsWallClock* clk);
    // Update with the nn which is target.
    void update();
    // Update with the nn.
    void update(int64_t nn);
    // Get the 10s average stat.
    int r10s();
};

/**
 * A time source to provide wall clock.
 */
class SrsWallClock
{
public:
    SrsWallClock();
    virtual ~SrsWallClock();
public:
    /**
     * Current time in srs_utime_t.
     */
    virtual srs_utime_t now();
};

// The global clock.
extern SrsWallClock* _srs_clock;

#endif
