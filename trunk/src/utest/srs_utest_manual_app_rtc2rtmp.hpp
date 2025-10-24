//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP_RTC2RTMP_HPP
#define SRS_UTEST_APP_RTC2RTMP_HPP

/*
#include <srs_utest_app_rtc2rtmp.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_stream_bridge.hpp>
#include <srs_protocol_rtmp_stack.hpp>

// Mock frame target for testing SrsRtcFrameBuilder
class MockRtc2RtmpFrameTarget : public ISrsFrameTarget
{
public:
    int on_frame_count_;
    SrsMediaPacket *last_frame_;
    srs_error_t frame_error_;

public:
    MockRtc2RtmpFrameTarget();
    virtual ~MockRtc2RtmpFrameTarget();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
    void reset();
    void set_frame_error(srs_error_t err);
};

// Mock request class for testing
class MockRtc2RtmpRequest : public ISrsRequest
{
public:
    MockRtc2RtmpRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockRtc2RtmpRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

extern SrsNaluSample *mock_create_nalu_sample(const uint8_t *data, int size);
extern SrsRtpPacket *mock_create_stap_packet_with_sps_pps(uint16_t seq, uint32_t ts);
extern SrsRtpPacket *mock_create_idr_packet(uint16_t seq, uint32_t ts, bool marker);
extern SrsRtpPacket *mock_create_p_frame_packet(uint16_t seq, uint32_t ts, bool marker);
extern SrsRtpPacket *mock_create_audio_packet(uint16_t seq, uint32_t ts, uint32_t avsync_time);

#endif
