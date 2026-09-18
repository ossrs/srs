//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI07_HPP
#define SRS_UTEST_AI07_HPP

/*
#include <srs_utest_ai07.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_protocol_rtmp_stack.hpp>

// Mock implementation of ISrsRtcSourceForConsumer for testing SrsRtcConsumer
class MockRtcSourceForConsumer : public ISrsRtcSourceForConsumer
{
public:
    SrsContextId source_id_;
    SrsContextId pre_source_id_;
    int consumer_destroy_count_;
    bool can_publish_;
    bool is_created_;

public:
    MockRtcSourceForConsumer();
    virtual ~MockRtcSourceForConsumer();
    virtual SrsContextId get_id();
    virtual SrsContextId get_pre_source_id();
    virtual void on_consumer_destroy(ISrsRtcConsumer *consumer);
    virtual bool can_publish();
    virtual bool is_created();
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
};

// Mock implementation of ISrsRtcSourceChangeCallback for testing
class MockRtcSourceChangeCallback : public ISrsRtcSourceChangeCallback
{
public:
    int stream_change_count_;
    SrsRtcSourceDescription *last_stream_desc_;

public:
    MockRtcSourceChangeCallback();
    virtual ~MockRtcSourceChangeCallback();
    virtual void on_stream_change(SrsRtcSourceDescription *desc);
};

// Mock implementation of ISrsRtcConsumer for testing SrsRtcSource
class MockRtcConsumer : public ISrsRtcConsumer
{
public:
    int update_source_id_count_;
    int stream_change_count_;
    int enqueue_count_;
    SrsRtcSourceDescription *last_stream_desc_;
    SrsContextId source_id_;
    SrsContextId consumer_id_;
    bool should_update_source_id_;
    srs_error_t enqueue_error_;

public:
    MockRtcConsumer();
    virtual ~MockRtcConsumer();
    virtual SrsContextId get_id();
    virtual void update_source_id();
    virtual void on_stream_change(SrsRtcSourceDescription *desc);
    virtual srs_error_t enqueue(SrsRtpPacket *pkt);
    void set_enqueue_error(srs_error_t err);
};

// Mock implementation of ISrsRtcSourceEventHandler for testing
class MockRtcSourceEventHandler : public ISrsRtcSourceEventHandler
{
public:
    int on_unpublish_count_;
    int on_consumers_finished_count_;

public:
    MockRtcSourceEventHandler();
    virtual ~MockRtcSourceEventHandler();
    virtual void on_unpublish();
    virtual void on_consumers_finished();
};

// Mock implementation of ISrsRtcPublishStream for testing
class MockRtcPublishStream : public SrsRtcPublishStream
{
public:
    int request_keyframe_count_;
    uint32_t last_keyframe_ssrc_;
    SrsContextId last_keyframe_cid_;
    SrsContextId context_id_;

public:
    MockRtcPublishStream();
    virtual ~MockRtcPublishStream();
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid);
    virtual const SrsContextId &context_id();
    void set_context_id(const SrsContextId &cid);
};

// Mock implementation of ISrsCircuitBreaker for testing
class MockCircuitBreaker : public ISrsCircuitBreaker
{
public:
    bool hybrid_high_water_level_;
    bool hybrid_critical_water_level_;
    bool hybrid_dying_water_level_;

public:
    MockCircuitBreaker();
    virtual ~MockCircuitBreaker();
    virtual srs_error_t initialize();
    virtual bool hybrid_high_water_level();
    virtual bool hybrid_critical_water_level();
    virtual bool hybrid_dying_water_level();
    void set_hybrid_dying_water_level(bool dying);
};

// Mock implementation of ISrsRequest that can simulate copy() failure
class MockFailingRequest : public ISrsRequest
{
public:
    bool should_fail_copy_;
    std::string stream_url_;

public:
    MockFailingRequest();
    MockFailingRequest(const std::string &stream_url, bool should_fail_copy = false);
    virtual ~MockFailingRequest();
    virtual ISrsRequest *copy();
    virtual void update_auth(ISrsRequest *req);
    virtual std::string get_stream_url();
    virtual void strip();
    virtual ISrsRequest *as_http();
    void set_should_fail_copy(bool should_fail);
};

// Mock implementation of SrsRtcSource that can simulate initialize() failure
class MockFailingRtcSource : public SrsRtcSource
{
public:
    bool should_fail_initialize_;
    srs_error_t initialize_error_;

public:
    MockFailingRtcSource();
    virtual ~MockFailingRtcSource();
    virtual srs_error_t initialize(ISrsRequest *req);
    void set_initialize_error(srs_error_t err);
};

// Mock implementation of ISrsRtcBridge for testing
class MockRtcBridge : public ISrsRtcBridge
{
public:
    int initialize_count_;
    int setup_codec_count_;
    int on_publish_count_;
    int on_unpublish_count_;
    int on_rtp_count_;
    srs_error_t initialize_error_;
    srs_error_t setup_codec_error_;
    srs_error_t on_publish_error_;
    srs_error_t on_rtp_error_;
    ISrsRequest *last_initialize_req_;
    SrsAudioCodecId last_audio_codec_;
    SrsVideoCodecId last_video_codec_;
    SrsRtpPacket *last_rtp_packet_;

public:
    MockRtcBridge();
    virtual ~MockRtcBridge();
    virtual srs_error_t initialize(ISrsRequest *req);
    virtual srs_error_t setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    void set_initialize_error(srs_error_t err);
    void set_setup_codec_error(srs_error_t err);
    void set_on_publish_error(srs_error_t err);
    void set_on_rtp_error(srs_error_t err);
};

#endif
