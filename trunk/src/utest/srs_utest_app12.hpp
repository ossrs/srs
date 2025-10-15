//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP12_HPP
#define SRS_UTEST_APP12_HPP

/*
#include <srs_utest_app12.hpp>
*/
#include <srs_utest.hpp>

#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#endif
#include <srs_app_stream_bridge.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_app6.hpp>

// Mock frame target for testing SrsSrtFrameBuilder
class MockSrtFrameTarget : public ISrsFrameTarget
{
public:
    int on_frame_count_;
    SrsMediaPacket *last_frame_;
    srs_error_t frame_error_;

public:
    MockSrtFrameTarget();
    virtual ~MockSrtFrameTarget();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
    void reset();
    void set_frame_error(srs_error_t err);
};

// Mock statistic for testing SrsSrtSource publish/unpublish
class MockSrtStatistic : public ISrsStatistic
{
public:
    int on_stream_publish_count_;
    int on_stream_close_count_;
    std::string last_publisher_id_;
    ISrsRequest *last_publish_req_;
    ISrsRequest *last_close_req_;

public:
    MockSrtStatistic();
    virtual ~MockSrtStatistic();
    virtual void on_disconnect(std::string id, srs_error_t err);
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id);
    virtual void on_stream_close(ISrsRequest *req);
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta);
    virtual void kbps_sample();
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames);
    virtual std::string server_id();
    virtual std::string service_id();
    virtual std::string service_pid();
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid);
    virtual SrsStatisticStream *find_stream(std::string sid);
    virtual SrsStatisticStream *find_stream_by_url(std::string url);
    virtual SrsStatisticClient *find_client(std::string client_id);
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr);
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs);
    void reset();
};

// Mock SRT bridge for testing SrsSrtSource publish/unpublish
class MockSrtBridge : public ISrsSrtBridge
{
public:
    int on_publish_count_;
    int on_unpublish_count_;
    int on_packet_count_;
    srs_error_t on_publish_error_;
    srs_error_t on_packet_error_;

public:
    MockSrtBridge();
    virtual ~MockSrtBridge();
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_packet(SrsSrtPacket *packet);
    void set_on_publish_error(srs_error_t err);
    void set_on_packet_error(srs_error_t err);
    void reset();
};

// Mock SRT consumer for testing SrsSrtSource on_packet
class MockSrtConsumer : public ISrsSrtConsumer
{
public:
    int enqueue_count_;
    srs_error_t enqueue_error_;
    std::vector<SrsSrtPacket *> packets_;

public:
    MockSrtConsumer();
    virtual ~MockSrtConsumer();
    virtual srs_error_t enqueue(SrsSrtPacket *packet);
    virtual srs_error_t dump_packet(SrsSrtPacket **ppkt);
    virtual void wait(int nb_msgs, srs_utime_t timeout);
    void set_enqueue_error(srs_error_t err);
    void reset();
};

// Forward declaration
class SrsRtspConsumer;

// Mock RTSP source for testing SrsRtspConsumer
class MockRtspSource
{
public:
    int on_consumer_destroy_count_;

public:
    MockRtspSource();
    virtual ~MockRtspSource();
    void on_consumer_destroy(SrsRtspConsumer *consumer);
    void reset();
};

// Mock RTP target for testing SrsRtspRtpBuilder
class MockRtspRtpTarget : public ISrsRtpTarget
{
public:
    int on_rtp_count_;
    SrsRtpPacket *last_rtp_;
    srs_error_t rtp_error_;

public:
    MockRtspRtpTarget();
    virtual ~MockRtspRtpTarget();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    void set_rtp_error(srs_error_t err);
    void reset();
};

// Mock RTSP connection for testing SrsRtspSendTrack
class MockRtspConnection : public ISrsRtspConnection
{
public:
    int do_send_packet_count_;
    SrsRtpPacket *last_packet_;
    srs_error_t send_error_;

public:
    MockRtspConnection();
    virtual ~MockRtspConnection();
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);
    virtual void expire();
    void set_send_error(srs_error_t err);
    void reset();
};

#endif
