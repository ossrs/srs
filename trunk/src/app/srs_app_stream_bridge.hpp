//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_STREAM_BRIDGE_HPP
#define SRS_APP_STREAM_BRIDGE_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>

#include <vector>

class SrsRequest;
class SrsSharedPtrMessage;
class SrsLiveSource;
class SrsRtcSource;
class SrsRtmpFormat;
class SrsMetaCache;
class SrsAudioTranscoder;
class SrsRtpPacket;

// A stream bridge is used to convert stream via different protocols, such as bridge for RTMP and RTC. Generally, we use
// frame as message for bridge. A frame is a audio or video frame, such as an I/B/P frame, a general frame for decoder.
// So you must assemble RTP or TS packets to a video frame if WebRTC or SRT.
class ISrsStreamBridge
{
public:
    ISrsStreamBridge();
    virtual ~ISrsStreamBridge();
public:
    virtual srs_error_t initialize(SrsRequest* r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame) = 0;
    virtual void on_unpublish() = 0;
};

// A bridge to feed AV frame to RTMP stream.
class SrsFrameToRtmpBridge : public ISrsStreamBridge
{
private:
    SrsLiveSource *source_;
public:
    SrsFrameToRtmpBridge(SrsLiveSource *src);
    virtual ~SrsFrameToRtmpBridge();
public:
    srs_error_t initialize(SrsRequest* r);
public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
public:
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame);
};

#ifdef SRS_FFMPEG_FIT

// A bridge to covert AV frame to WebRTC stream.
class SrsFrameToRtcBridge : public ISrsStreamBridge
{
private:
    SrsRequest* req;
    SrsRtcSource* source_;
    // The format, codec information.
    SrsRtmpFormat* format;
    // The metadata cache.
    SrsMetaCache* meta;
private:
    SrsAudioCodecId latest_codec_;
    SrsAudioTranscoder* codec_;
    bool keep_bframe;
    bool merge_nalus;
    uint16_t audio_sequence;
    uint16_t video_sequence;
    uint32_t audio_ssrc;
    uint32_t video_ssrc;
    uint8_t audio_payload_type_;
    uint8_t video_payload_type_;
public:
    SrsFrameToRtcBridge(SrsRtcSource* source);
    virtual ~SrsFrameToRtcBridge();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame);
private:
    virtual srs_error_t on_audio(SrsSharedPtrMessage* msg);
private:
    srs_error_t init_codec(SrsAudioCodecId codec);
    srs_error_t transcode(SrsAudioFrame* audio);
    srs_error_t package_opus(SrsAudioFrame* audio, SrsRtpPacket* pkt);
private:
    virtual srs_error_t on_video(SrsSharedPtrMessage* msg);
private:
    srs_error_t filter(SrsSharedPtrMessage* msg, SrsFormat* format, bool& has_idr, std::vector<SrsSample*>& samples);
    srs_error_t package_stap_a(SrsRtcSource* source, SrsSharedPtrMessage* msg, SrsRtpPacket* pkt);
    srs_error_t package_nalus(SrsSharedPtrMessage* msg, const std::vector<SrsSample*>& samples, std::vector<SrsRtpPacket*>& pkts);
    srs_error_t package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, std::vector<SrsRtpPacket*>& pkts);
    srs_error_t package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, std::vector<SrsRtpPacket*>& pkts);
    srs_error_t consume_packets(std::vector<SrsRtpPacket*>& pkts);
};
#endif

#endif

