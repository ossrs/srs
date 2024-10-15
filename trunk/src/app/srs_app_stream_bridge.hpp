//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_STREAM_BRIDGE_HPP
#define SRS_APP_STREAM_BRIDGE_HPP

#include <srs_core.hpp>

#include <srs_kernel_codec.hpp>
#include <srs_core_autofree.hpp>

#include <vector>

class SrsRequest;
class SrsSharedPtrMessage;
class SrsLiveSource;
class SrsRtcSource;
class SrsRtmpFormat;
class SrsMetaCache;
class SrsRtpPacket;
class SrsRtcRtpBuilder;

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
    SrsSharedPtr<SrsLiveSource> source_;
public:
    SrsFrameToRtmpBridge(SrsSharedPtr<SrsLiveSource> source);
    virtual ~SrsFrameToRtmpBridge();
public:
    srs_error_t initialize(SrsRequest* r);
public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
public:
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame);
};

#ifdef SRS_RTC
// A bridge to covert AV frame to WebRTC stream.
class SrsFrameToRtcBridge : public ISrsStreamBridge
{
private:
    SrsSharedPtr<SrsRtcSource> source_;
private:
#if defined(SRS_FFMPEG_FIT)
    SrsRtcRtpBuilder* rtp_builder_;
#endif
public:
    SrsFrameToRtcBridge(SrsSharedPtr<SrsRtcSource> source);
    virtual ~SrsFrameToRtcBridge();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame);
    srs_error_t on_rtp(SrsRtpPacket* pkt);
};
#endif

// A bridge chain, a set of bridges.
class SrsCompositeBridge : public ISrsStreamBridge
{
public:
    SrsCompositeBridge();
    virtual ~SrsCompositeBridge();
public:
    srs_error_t initialize(SrsRequest* r);
public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
public:
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame);
public:
    SrsCompositeBridge* append(ISrsStreamBridge* bridge);
private:
    std::vector<ISrsStreamBridge*> bridges_;
};

#endif

