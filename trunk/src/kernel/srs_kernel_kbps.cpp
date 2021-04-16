/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2021 Winlin
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

#include <srs_kernel_kbps.hpp>

#include <srs_kernel_utility.hpp>

SrsRateSample::SrsRateSample()
{
    total = time = -1;
    rate = 0;
}

SrsRateSample::~SrsRateSample()
{
}

SrsRateSample* SrsRateSample::update(int64_t nn, srs_utime_t t, int k)
{
    total = nn;
    time = t;
    rate = k;
    return this;
}

void srs_pps_init(SrsRateSample& sample, int64_t nn, srs_utime_t now)
{
    if (sample.time < 0 || nn < sample.total) {
        sample.update(nn, now, 0);
    }
}

void srs_pps_update(SrsRateSample& sample, int64_t nn, srs_utime_t now)
{
    int pps = (int)((nn - sample.total) * 1000 / srsu2ms(now - sample.time));
    if (pps == 0 && nn > sample.total) {
        pps = 1; // For pps in (0, 1), we set to 1.
    }
    sample.update(nn, now, pps);
}

SrsPps::SrsPps()
{
    clk_ = NULL;
    sugar = 0;
}

SrsPps::~SrsPps()
{
}

void SrsPps::set_clock(SrsWallClock* clk)
{
    clk_ = clk;
}

void SrsPps::update()
{
    update(sugar);
}

void SrsPps::update(int64_t nn)
{
    // Lazy setup the clock.
    if (!clk_) {
        clk_ = _srs_clock;
    }

    srs_utime_t now = clk_->now();

    srs_pps_init(sample_10s_, nn, now);
    srs_pps_init(sample_30s_, nn, now);
    srs_pps_init(sample_1m_, nn, now);
    srs_pps_init(sample_5m_, nn, now);
    srs_pps_init(sample_60m_, nn, now);

    if (now - sample_10s_.time >= 10 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_10s_, nn, now);
    }
    if (now - sample_30s_.time >= 30 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_30s_, nn, now);
    }
    if (now - sample_1m_.time >= 60 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_1m_, nn, now);
    }
    if (now - sample_5m_.time >= 300 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_5m_, nn, now);
    }
    if (now - sample_60m_.time >= 3600 * SRS_UTIME_SECONDS) {
        srs_pps_update(sample_60m_, nn, now);
    }
}

int SrsPps::r10s()
{
    return sample_10s_.rate;
}

SrsWallClock::SrsWallClock()
{
}

SrsWallClock::~SrsWallClock()
{
}

srs_utime_t SrsWallClock::now()
{
    return srs_get_system_time();
}

SrsWallClock* _srs_clock = new SrsWallClock();

