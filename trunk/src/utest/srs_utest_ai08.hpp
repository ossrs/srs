//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI08_HPP
#define SRS_UTEST_AI08_HPP

/*
#include <srs_utest_ai08.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_rtmp_source.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#include <srs_utest_ai09.hpp>

// Forward declarations
class SrsMediaPacket;
class SrsRtpPacket;
class SrsSrtPacket;

// Mock request class for testing stream bridges
class MockStreamBridgeRequest : public ISrsRequest
{
public:
    MockStreamBridgeRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockStreamBridgeRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Mock frame target for testing bridges
class MockFrameTarget : public ISrsFrameTarget
{
public:
    int on_frame_count_;
    SrsMediaPacket *last_frame_;
    srs_error_t frame_error_;

public:
    MockFrameTarget();
    virtual ~MockFrameTarget();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
    void set_frame_error(srs_error_t err);
};

// Mock RTP target for testing bridges
class MockRtpTarget : public ISrsRtpTarget
{
public:
    int on_rtp_count_;
    SrsRtpPacket *last_rtp_;
    srs_error_t rtp_error_;

public:
    MockRtpTarget();
    virtual ~MockRtpTarget();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    void set_rtp_error(srs_error_t err);
};

// Mock SRT target for testing bridges
class MockSrtTarget : public ISrsSrtTarget
{
public:
    int on_packet_count_;
    SrsSrtPacket *last_packet_;
    srs_error_t packet_error_;

public:
    MockSrtTarget();
    virtual ~MockSrtTarget();
    virtual srs_error_t on_srt_packet(SrsSrtPacket *pkt);
    void set_packet_error(srs_error_t err);
};

// Mock live source handler for testing
class MockLiveSourceHandler : public ISrsLiveSourceHandler
{
public:
    int on_publish_count_;
    int on_unpublish_count_;

public:
    MockLiveSourceHandler();
    virtual ~MockLiveSourceHandler();
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish(ISrsRequest *r);
};

#endif
