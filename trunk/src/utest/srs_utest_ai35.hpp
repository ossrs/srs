//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI35_HPP
#define SRS_UTEST_AI35_HPP

/*
#include <srs_utest_ai35.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_statistic.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_manual_mock.hpp>

// The SRT connection of a client, which dumps its SRT stats for the statistic.
class MockStatisticSrtForClient : public ISrsStatisticSrt
{
public:
    int dumps_count_;
    double rtt_ms_;
    srs_error_t dumps_error_;

public:
    MockStatisticSrtForClient();
    virtual ~MockStatisticSrtForClient();

public:
    virtual srs_error_t dumps_srt(SrsJsonObject *obj);
};

// The SRT connection, which returns the stats like libsrt: clear resets the interval counters, never the
// totals or the current values.
class MockSrtConnectionForStats : public MockSrtConnection
{
public:
    // The stats libsrt has now, set by the test.
    SrsSrtStat stats_;
    int clear_count_;
    int peek_count_;

public:
    MockSrtConnectionForStats();
    virtual ~MockSrtConnectionForStats();

public:
    virtual srs_error_t get_stats(SrsSrtStat &stat, bool clear);
};

#endif
