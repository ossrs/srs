//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_FMP4_HPP
#define SRS_UTEST_FMP4_HPP

/*
#include <srs_utest_manual_fmp4.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_stack.hpp>

// Mock classes for testing
class MockFmp4SrsRequest : public SrsRequest
{
public:
    MockFmp4SrsRequest();
    virtual ~MockFmp4SrsRequest();
};

class MockSrsFormat : public SrsFormat
{
public:
    // The format's raw_ points into the last tag it parsed, so the tags live as long as it.
    uint8_t video_raw_[46];
    uint8_t audio_raw_[4];

public:
    MockSrsFormat();
    virtual ~MockSrsFormat();
};

class MockSrsMediaPacket : public SrsMediaPacket
{
public:
    MockSrsMediaPacket(bool is_video_msg, uint32_t ts);
    virtual ~MockSrsMediaPacket();
};

#endif
