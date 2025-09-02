//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_DVR_HPP
#define SRS_APP_DVR_HPP

#include <srs_core.hpp>

#include <sstream>
#include <string>

class SrsLiveSource;
class SrsOriginHub;
class ISrsRequest;
class SrsBuffer;
class SrsRtmpJitter;
class SrsMediaPacket;
class SrsFileWriter;
class SrsFlvTransmuxer;
class SrsDvrPlan;
class SrsJsonAny;
class SrsJsonObject;
class SrsThread;
class SrsMp4Encoder;
class SrsFragment;
class SrsFormat;

#include <srs_app_async_call.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_source.hpp>

// The segmenter for DVR, to write a segment file in flv/mp4.
class SrsDvrSegmenter : public ISrsReloadHandler
{
protected:
    // The underlayer file object.
    SrsFileWriter *fs;
    // Whether wait keyframe to reap segment.
    bool wait_keyframe;
    // The FLV/MP4 fragment file.
    SrsFragment *fragment;

private:
    ISrsRequest *req;
    SrsDvrPlan *plan;

private:
    SrsRtmpJitter *jitter;
    SrsRtmpJitterAlgorithm jitter_algorithm;

public:
    SrsDvrSegmenter();
    virtual ~SrsDvrSegmenter();

public:
    // Initialize the segment.
    virtual srs_error_t initialize(SrsDvrPlan *p, ISrsRequest *r);
    // Get the current framgnet.
    virtual SrsFragment *current();
    // Open new segment file.
    // @param use_tmp_file Whether use tmp file for DVR, and rename when close.
    // @remark Ignore when file is already open.
    virtual srs_error_t open();
    // Write the metadata.
    virtual srs_error_t write_metadata(SrsMediaPacket *metadata);
    // Write audio packet.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    // Write video packet.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);
    // Refresh the metadata. For example, there is duration in flv metadata,
    // when DVR in append mode, the duration must be update every some seconds.
    // @remark Maybe ignored by concreate segmenter.
    virtual srs_error_t refresh_metadata() = 0;
    // Close current segment.
    // @remark ignore when already closed.
    virtual srs_error_t close();

protected:
    virtual srs_error_t open_encoder() = 0;
    virtual srs_error_t encode_metadata(SrsMediaPacket *metadata) = 0;
    virtual srs_error_t encode_audio(SrsMediaPacket *audio, SrsFormat *format) = 0;
    virtual srs_error_t encode_video(SrsMediaPacket *video, SrsFormat *format) = 0;
    virtual srs_error_t close_encoder() = 0;

private:
    // Generate the flv segment path.
    virtual std::string generate_path();
    // When update the duration of segment by rtmp msg.
    virtual srs_error_t on_update_duration(SrsMediaPacket *msg);
};

// The FLV segmenter to use FLV encoder to write file.
class SrsDvrFlvSegmenter : public SrsDvrSegmenter
{
private:
    // The FLV encoder, for FLV target.
    SrsFlvTransmuxer *enc;

private:
    // The offset of file for duration value.
    // The next 8 bytes is the double value.
    int64_t duration_offset;
    // The offset of file for filesize value.
    // The next 8 bytes is the double value.
    int64_t filesize_offset;
    // Whether current segment has keyframe.
    bool has_keyframe;

public:
    SrsDvrFlvSegmenter();
    virtual ~SrsDvrFlvSegmenter();

public:
    virtual srs_error_t refresh_metadata();

protected:
    virtual srs_error_t open_encoder();
    virtual srs_error_t encode_metadata(SrsMediaPacket *metadata);
    virtual srs_error_t encode_audio(SrsMediaPacket *audio, SrsFormat *format);
    virtual srs_error_t encode_video(SrsMediaPacket *video, SrsFormat *format);
    virtual srs_error_t close_encoder();
};

// The MP4 segmenter to use MP4 encoder to write file.
class SrsDvrMp4Segmenter : public SrsDvrSegmenter
{
private:
    // The MP4 encoder, for MP4 target.
    SrsMp4Encoder *enc;

public:
    SrsDvrMp4Segmenter();
    virtual ~SrsDvrMp4Segmenter();

public:
    virtual srs_error_t refresh_metadata();

protected:
    virtual srs_error_t open_encoder();
    virtual srs_error_t encode_metadata(SrsMediaPacket *metadata);
    virtual srs_error_t encode_audio(SrsMediaPacket *audio, SrsFormat *format);
    virtual srs_error_t encode_video(SrsMediaPacket *video, SrsFormat *format);
    virtual srs_error_t close_encoder();
};

// the dvr async call.
class SrsDvrAsyncCallOnDvr : public ISrsAsyncCallTask
{
private:
    SrsContextId cid;
    std::string path;
    ISrsRequest *req;

public:
    SrsDvrAsyncCallOnDvr(SrsContextId c, ISrsRequest *r, std::string p);
    virtual ~SrsDvrAsyncCallOnDvr();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// The DVR plan, when and how to reap segment.
class SrsDvrPlan : public ISrsReloadHandler
{
public:
    ISrsRequest *req;

protected:
    SrsOriginHub *hub;
    SrsDvrSegmenter *segment;
    bool dvr_enabled;

public:
    SrsDvrPlan();
    virtual ~SrsDvrPlan();

public:
    virtual srs_error_t initialize(SrsOriginHub *h, SrsDvrSegmenter *s, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
    // Internal interface for segmenter.
public:
    // When segmenter close a segment.
    virtual srs_error_t on_reap_segment();

public:
    static srs_error_t create_plan(std::string vhost, SrsDvrPlan **pplan);
};

// The DVR session plan: reap flv when session complete(unpublish)
class SrsDvrSessionPlan : public SrsDvrPlan
{
public:
    SrsDvrSessionPlan();
    virtual ~SrsDvrSessionPlan();

public:
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
};

// The DVR segment plan: reap flv when duration exceed.
class SrsDvrSegmentPlan : public SrsDvrPlan
{
private:
    // in config, in srs_utime_t
    srs_utime_t cduration;
    bool wait_keyframe;
    // Whether reopening the DVR file.
    bool reopening_segment_;

public:
    SrsDvrSegmentPlan();
    virtual ~SrsDvrSegmentPlan();

public:
    virtual srs_error_t initialize(SrsOriginHub *h, SrsDvrSegmenter *s, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);

private:
    virtual srs_error_t update_duration(SrsMediaPacket *msg);
};

// DVR(Digital Video Recorder) to record RTMP stream to flv/mp4 file.
class SrsDvr : public ISrsReloadHandler
{
private:
    SrsOriginHub *hub;
    SrsDvrPlan *plan;
    ISrsRequest *req;

private:
    // whether the dvr is actived by filter, which is specified by dvr_apply.
    // we always initialize the dvr, which crote plan and segment object,
    // but they never create actual piece of file util the apply active it.
    bool actived;

public:
    SrsDvr();
    virtual ~SrsDvr();

public:
    // initialize dvr, create dvr plan.
    // when system initialize(encoder publish at first time, or reload),
    // initialize the dvr will reinitialize the plan, the whole dvr framework.
    virtual srs_error_t initialize(SrsOriginHub *h, ISrsRequest *r);
    // publish stream event,
    // when encoder start to publish RTMP stream.
    // @param fetch_sequence_header whether fetch sequence from source.
    virtual srs_error_t on_publish(ISrsRequest *r);
    // the unpublish event.,
    // when encoder stop(unpublish) to publish RTMP stream.
    virtual void on_unpublish();
    // get some information from metadata, it's optinal.
    virtual srs_error_t on_meta_data(SrsMediaPacket *metadata);
    // mux the audio packets to dvr.
    // @param shared_audio, directly ptr, copy it if need to save it.
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *foramt);
    // mux the video packets to dvr.
    // @param shared_video, directly ptr, copy it if need to save it.
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
};

extern SrsAsyncCallWorker *_srs_dvr_async;

#endif
