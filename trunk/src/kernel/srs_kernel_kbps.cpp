//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_kbps.hpp>

#include <srs_kernel_utility.hpp>
#include <srs_kernel_error.hpp>

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
    clk_ = _srs_clock;
    sugar = 0;
}

SrsPps::~SrsPps()
{
}

void SrsPps::update()
{
    update(sugar);
}

void SrsPps::update(int64_t nn)
{
    srs_assert(clk_);

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

int SrsPps::r30s()
{
    return sample_30s_.rate;
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

SrsWallClock* _srs_clock = NULL;

