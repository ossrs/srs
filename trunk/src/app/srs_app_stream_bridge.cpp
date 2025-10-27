//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_stream_bridge.hpp>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_rtc_source.hpp>

using namespace std;

ISrsFrameTarget::ISrsFrameTarget()
{
}

ISrsFrameTarget::~ISrsFrameTarget()
{
}

ISrsRtpTarget::ISrsRtpTarget()
{
}

ISrsRtpTarget::~ISrsRtpTarget()
{
}

ISrsSrtTarget::ISrsSrtTarget()
{
}

ISrsSrtTarget::~ISrsSrtTarget()
{
}

ISrsRtmpBridge::ISrsRtmpBridge()
{
}

ISrsRtmpBridge::~ISrsRtmpBridge()
{
}

SrsRtmpBridge::SrsRtmpBridge(ISrsAppFactory *factory)
{
#ifdef SRS_FFMPEG_FIT
    rtp_builder_ = NULL;
#endif
#ifdef SRS_RTSP
    rtsp_builder_ = NULL;
    rtsp_target_ = NULL;
#endif
    rtc_target_ = NULL;

    app_factory_ = factory;
}

SrsRtmpBridge::~SrsRtmpBridge()
{
#ifdef SRS_FFMPEG_FIT
    srs_freep(rtp_builder_);
#endif
#ifdef SRS_RTSP
    srs_freep(rtsp_builder_);
    rtsp_target_ = NULL;
#endif
    rtc_target_ = NULL;

    app_factory_ = NULL;
}

bool SrsRtmpBridge::empty()
{
    if (rtc_target_.get()) {
        return false;
    }

#ifdef SRS_RTSP
    if (rtsp_target_.get()) {
        return false;
    }
#endif

    return true;
}

void SrsRtmpBridge::enable_rtmp2rtc(SrsSharedPtr<SrsRtcSource> rtc_source)
{
    rtc_target_ = rtc_source;
}

#ifdef SRS_RTSP
void SrsRtmpBridge::enable_rtmp2rtsp(SrsSharedPtr<SrsRtspSource> rtsp_source)
{
    rtsp_target_ = rtsp_source;
}
#endif

srs_error_t SrsRtmpBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    if (rtc_target_.get()) {
        srs_freep(rtp_builder_);
        rtp_builder_ = new SrsRtcRtpBuilder(app_factory_, rtc_target_.get(), rtc_target_);
        if ((err = rtp_builder_->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtp builder initialize");
        }
    }
#endif

#ifdef SRS_RTSP
    if (rtsp_target_.get()) {
        srs_freep(rtsp_builder_);
        rtsp_builder_ = new SrsRtspRtpBuilder(rtsp_target_.get(), rtsp_target_);
        if ((err = rtsp_builder_->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtsp builder initialize");
        }
    }
#endif

    return err;
}

srs_error_t SrsRtmpBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if (rtc_target_.get()) {
        if ((err = rtc_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtc target publish");
        }

#ifdef SRS_FFMPEG_FIT
        if ((err = rtp_builder_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtp builder publish");
        }
#endif
    }

#ifdef SRS_RTSP
    // TODO: FIXME: Should sync with bridge?
    if (rtsp_target_.get()) {
        if ((err = rtsp_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtsp target publish");
        }

        if ((err = rtsp_builder_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtsp builder publish");
        }
    }
#endif

    return err;
}

void SrsRtmpBridge::on_unpublish()
{
    if (rtc_target_.get()) {
#ifdef SRS_FFMPEG_FIT
        rtp_builder_->on_unpublish();
#endif
        rtc_target_->on_unpublish();
    }

#ifdef SRS_RTSP
    if (rtsp_target_.get()) {
        rtsp_builder_->on_unpublish();
        rtsp_target_->on_unpublish();
    }
#endif

    // Note that RTMP live source free this rtmp bridge, after on_unpublish() is called.
    // So there is no need to free its components here.
}

srs_error_t SrsRtmpBridge::on_frame(SrsMediaPacket *frame)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    if (rtp_builder_ && (err = rtp_builder_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtp builder on frame");
    }
#endif

#ifdef SRS_RTSP
    if (rtsp_builder_ && (err = rtsp_builder_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtsp builder on frame");
    }
#endif

    return err;
}

ISrsSrtBridge::ISrsSrtBridge()
{
}

ISrsSrtBridge::~ISrsSrtBridge()
{
}

SrsSrtBridge::SrsSrtBridge(ISrsAppFactory *factory)
{
    frame_builder_ = new SrsSrtFrameBuilder(this);
    rtmp_target_ = NULL;

#ifdef SRS_FFMPEG_FIT
    rtp_builder_ = NULL;
#endif
    rtc_target_ = NULL;

    app_factory_ = factory;
}

SrsSrtBridge::~SrsSrtBridge()
{
    rtmp_target_ = NULL;
    srs_freep(frame_builder_);

    rtc_target_ = NULL;
#ifdef SRS_FFMPEG_FIT
    srs_freep(rtp_builder_);
#endif

    app_factory_ = NULL;
}

bool SrsSrtBridge::empty()
{
    return !rtmp_target_.get() && !rtc_target_.get();
}

void SrsSrtBridge::enable_srt2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_source)
{
    rtmp_target_ = rtmp_source;
}

void SrsSrtBridge::enable_srt2rtc(SrsSharedPtr<SrsRtcSource> rtc_source)
{
    rtc_target_ = rtc_source;
}

srs_error_t SrsSrtBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = frame_builder_->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "frame builder initialize");
    }

#ifdef SRS_FFMPEG_FIT
    if (rtc_target_.get()) {
        srs_freep(rtp_builder_);
        rtp_builder_ = new SrsRtcRtpBuilder(app_factory_, rtc_target_.get(), rtc_target_);
        if ((err = rtp_builder_->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtp builder initialize");
        }
    }
#endif

    return err;
}

srs_error_t SrsSrtBridge::on_publish()
{
    srs_error_t err = srs_success;

    if ((err = frame_builder_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "frame builder publish");
    }

    // TODO: FIXME: Should sync with bridge?
    if (rtmp_target_.get()) {
        if ((err = rtmp_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtmp target publish");
        }
    }

    if (rtc_target_.get()) {
        if ((err = rtc_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtc target publish");
        }

#ifdef SRS_FFMPEG_FIT
        if ((err = rtp_builder_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtp builder publish");
        }
#endif
    }

    return err;
}

void SrsSrtBridge::on_unpublish()
{
    frame_builder_->on_unpublish();

    if (rtmp_target_.get()) {
        rtmp_target_->on_unpublish();
    }

    if (rtc_target_.get()) {
#ifdef SRS_FFMPEG_FIT
        rtp_builder_->on_unpublish();
#endif
        rtc_target_->on_unpublish();
    }

    // Note that SRT source free this srt bridge, after on_unpublish() is called.
    // So there is no need to free its components here.
}

srs_error_t SrsSrtBridge::on_packet(SrsSrtPacket *pkt)
{
    srs_error_t err = srs_success;

    if ((err = frame_builder_->on_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "frame builder on packet");
    }

    return err;
}

srs_error_t SrsSrtBridge::on_frame(SrsMediaPacket *frame)
{
    srs_error_t err = srs_success;

    // Deliver frame to RTMP target
    if (rtmp_target_.get() && (err = rtmp_target_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtmp target on frame");
    }

    // Deliver frame to RTP builder, which delivers to RTC target
#ifdef SRS_FFMPEG_FIT
    if (rtp_builder_ && (err = rtp_builder_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtp builder on frame");
    }
#endif

    return err;
}

ISrsRtcBridge::ISrsRtcBridge()
{
}

ISrsRtcBridge::~ISrsRtcBridge()
{
}

SrsRtcBridge::SrsRtcBridge(ISrsAppFactory *factory)
{
    req_ = NULL;
#ifdef SRS_FFMPEG_FIT
    frame_builder_ = NULL;
#endif
    rtmp_target_ = NULL;

    app_factory_ = factory;
}

SrsRtcBridge::~SrsRtcBridge()
{
    srs_freep(req_);
#ifdef SRS_FFMPEG_FIT
    srs_freep(frame_builder_);
#endif
    rtmp_target_ = NULL;

    app_factory_ = NULL;
}

void SrsRtcBridge::enable_rtc2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_target)
{
    rtmp_target_ = rtmp_target;
}

bool SrsRtcBridge::empty()
{
    return !rtmp_target_.get();
}

srs_error_t SrsRtcBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    srs_freep(req_);
    req_ = r->copy();

#ifdef SRS_FFMPEG_FIT
    srs_assert(rtmp_target_.get());
    srs_freep(frame_builder_);
    frame_builder_ = app_factory_->create_rtc_frame_builder(rtmp_target_.get());
#endif

    return err;
}

srs_error_t SrsRtcBridge::setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    if ((err = frame_builder_->initialize(req_, acodec, vcodec)) != srs_success) {
        return srs_error_wrap(err, "frame builder initialize");
    }
#endif

    return err;
}

srs_error_t SrsRtcBridge::on_publish()
{
    srs_error_t err = srs_success;

    srs_assert(rtmp_target_.get());
    if ((err = rtmp_target_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "rtmp target publish");
    }

#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    if ((err = frame_builder_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "frame builder on publish");
    }
#endif

    return err;
}

void SrsRtcBridge::on_unpublish()
{
#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    frame_builder_->on_unpublish();
#endif

    srs_assert(rtmp_target_.get());
    rtmp_target_->on_unpublish();

    // Note that RTC source free this rtc bridge, after on_unpublish() is called.
    // So there is no need to free its components here.
}

srs_error_t SrsRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    if (frame_builder_ && (err = frame_builder_->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "frame builder on rtp");
    }
#endif

    return err;
}
