//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

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
    // Update with the nn which is target.
    void update();
    // Update with the nn.
    void update(int64_t nn);
    // Get the 10s average stat.
    int r10s();
    // Get the 30s average stat.
    int r30s();
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
