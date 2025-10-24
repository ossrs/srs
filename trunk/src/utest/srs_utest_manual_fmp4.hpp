//
// Copyright (c) 2013-2025 The SRS Authors
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
