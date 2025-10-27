//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI09_HPP
#define SRS_UTEST_AI09_HPP

/*
#include <srs_utest_ai09.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>

// Forward declarations
class SrsMediaPacket;
class SrsRtpPacket;

// Mock frame target for testing SrsRtcFrameBuilder
class MockRtcFrameTarget : public ISrsFrameTarget
{
public:
    int on_frame_count_;
    SrsMediaPacket *last_frame_;
    srs_error_t frame_error_;

public:
    MockRtcFrameTarget();
    virtual ~MockRtcFrameTarget();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
    void reset();
};

// Mock audio transcoder for testing SrsRtcFrameBuilder::transcode_audio
class MockAudioTranscoderForUtest : public ISrsAudioTranscoder
{
public:
    // Control behavior
    srs_error_t transcode_error_;
    std::vector<SrsParsedAudioPacket *> output_packets_;
    bool should_output_packets_;
    uint8_t *aac_header_data_;
    int aac_header_len_;

public:
    MockAudioTranscoderForUtest();
    virtual ~MockAudioTranscoderForUtest();

public:
    // ISrsAudioTranscoder interface
    virtual srs_error_t initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate);
    virtual srs_error_t transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs);
    virtual void free_frames(std::vector<SrsParsedAudioPacket *> &frames);
    virtual void aac_codec_header(uint8_t **data, int *len);

public:
    // Test helpers
    void reset();
    void set_output_packets(int count, const char *sample_data = NULL, int sample_size = 64);
    void set_transcode_error(srs_error_t err);
};

// Mock request class for testing - implement ISrsRequest interface
class MockRtcRequest : public ISrsRequest
{
public:
    MockRtcRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockRtcRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

#endif
