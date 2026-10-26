//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI40_HPP
#define SRS_UTEST_AI40_HPP

/*
#include <srs_utest_ai40.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_kbps.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <vector>

// The server's stats lines are built every 10s from global counters that every other test shares.
// This scope gives each listed counter a test clock and an empty history, as in a server that just
// started, and puts the counter back as it was when the test ends.
class KbpsGlobalPpsScope
{
public:
    MockWallClock clock_;
    std::vector<SrsPps *> pps_;
    std::vector<SrsPps> saved_;

public:
    KbpsGlobalPpsScope(const std::vector<SrsPps *> &pps)
    {
        pps_ = pps;
        for (int i = 0; i < (int)pps.size(); i++) {
            SrsPps *p = pps[i];
            saved_.push_back(*p);

            SrsPps fresh;
            fresh.clk_ = &clock_;
            *p = fresh;
        }
    }
    ~KbpsGlobalPpsScope()
    {
        for (int i = 0; i < (int)pps_.size(); i++) {
            *pps_[i] = saved_[i];
        }
    }
};

// The counters srs_global_rtc_update reads for the RTC server's stats line.
extern std::vector<SrsPps *> kbps_rtc_line_pps();

#endif
