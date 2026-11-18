//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_STREAM_BRIDGE_HPP
#define SRS_APP_STREAM_BRIDGE_HPP

#include <srs_core.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>

class ISrsRequest;
class SrsMediaPacket;
class SrsLiveSource;
class SrsRtcSource;
class SrsRtmpFormat;
class SrsMetaCache;
class SrsRtpPacket;
class SrsRtcRtpBuilder;
#ifdef SRS_RTSP
class SrsRtspSource;
class SrsRtspRtpBuilder;
#endif
class SrsRtcFrameBuilder;
class ISrsStreamBridge;
class SrsSrtFrameBuilder;
class SrsSrtPacket;
class ISrsAppFactory;

class ISrsFrameTarget
{
public:
    ISrsFrameTarget();
    virtual ~ISrsFrameTarget();

public:
    virtual srs_error_t on_frame(SrsMediaPacket *frame) = 0;
};

class ISrsRtpTarget
{
public:
    ISrsRtpTarget();
    virtual ~ISrsRtpTarget();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt) = 0;
};

class ISrsSrtTarget
{
public:
    ISrsSrtTarget();
    virtual ~ISrsSrtTarget();

public:
    virtual srs_error_t on_srt_packet(SrsSrtPacket *pkt) = 0;
};

class ISrsRtmpBridge : public ISrsFrameTarget
{
public:
    ISrsRtmpBridge();
    virtual ~ISrsRtmpBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
};

class SrsRtmpBridge : public ISrsRtmpBridge
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
#ifdef SRS_FFMPEG_FIT
    SrsRtcRtpBuilder *rtp_builder_;
#endif
#ifdef SRS_RTSP
    SrsRtspRtpBuilder *rtsp_builder_;
#endif
    SrsSharedPtr<SrsRtcSource> rtc_target_;
#ifdef SRS_RTSP
    SrsSharedPtr<SrsRtspSource> rtsp_target_;
#endif

public:
    SrsRtmpBridge(ISrsAppFactory *factory);
    virtual ~SrsRtmpBridge();

public:
    bool empty();
    void enable_rtmp2rtc(SrsSharedPtr<SrsRtcSource> rtc_source);
#ifdef SRS_RTSP
    void enable_rtmp2rtsp(SrsSharedPtr<SrsRtspSource> rtsp_source);
#endif

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
};

class ISrsSrtBridge : public ISrsSrtTarget
{
public:
    ISrsSrtBridge();
    virtual ~ISrsSrtBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
};

class SrsSrtBridge : public ISrsSrtBridge, public ISrsFrameTarget
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSrtFrameBuilder *frame_builder_;
    SrsSharedPtr<SrsLiveSource> rtmp_target_;
#ifdef SRS_FFMPEG_FIT
    SrsRtcRtpBuilder *rtp_builder_;
#endif
    SrsSharedPtr<SrsRtcSource> rtc_target_;

public:
    SrsSrtBridge(ISrsAppFactory *factory);
    virtual ~SrsSrtBridge();
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    bool empty();
    void enable_srt2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_source);
    void enable_srt2rtc(SrsSharedPtr<SrsRtcSource> rtc_source);

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_srt_packet(SrsSrtPacket *pkt);
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
};

class ISrsRtcBridge : public ISrsRtpTarget
{
public:
    ISrsRtcBridge();
    virtual ~ISrsRtcBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
};

class SrsRtcBridge : public ISrsRtcBridge
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
#ifdef SRS_FFMPEG_FIT
    SrsRtcFrameBuilder *frame_builder_;
#endif
    SrsSharedPtr<SrsLiveSource> rtmp_target_;

public:
    SrsRtcBridge(ISrsAppFactory *factory);
    virtual ~SrsRtcBridge();

public:
    bool empty();
    void enable_rtc2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_target);

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

#endif
