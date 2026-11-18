//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsFlvTransmuxer;
class SrsDvrPlan;
class SrsJsonAny;
class SrsJsonObject;
class SrsThread;
class SrsMp4Encoder;
class SrsFragment;
class SrsFormat;
class ISrsFileWriter;
class ISrsDvrPlan;
class ISrsMp4Encoder;
class ISrsOriginHub;
class ISrsAppConfig;
class ISrsAppFactory;
class ISrsAsyncCallWorker;
class ISrsStatistic;

#include <srs_app_async_call.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtmp_source.hpp>

class ISrsDvrSegmenter
{
public:
    ISrsDvrSegmenter();
    virtual void assemble() = 0;
    virtual ~ISrsDvrSegmenter();

public:
    virtual srs_error_t initialize(ISrsDvrPlan *p, ISrsRequest *r) = 0;
    virtual SrsFragment *current() = 0;
    virtual srs_error_t open() = 0;
    virtual srs_error_t write_metadata(SrsMediaPacket *metadata) = 0;
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;
    virtual srs_error_t close() = 0;
};

class SrsDvrSegmenter : public ISrsReloadHandler, public ISrsDvrSegmenter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsFileWriter *fs_;
    bool wait_keyframe_;
    SrsFragment *fragment_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsDvrPlan *plan_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtmpJitter *jitter_;
    SrsRtmpJitterAlgorithm jitter_algorithm_;

public:
    SrsDvrSegmenter();
    void assemble();
    virtual ~SrsDvrSegmenter();

public:
    virtual srs_error_t initialize(ISrsDvrPlan *p, ISrsRequest *r);
    virtual SrsFragment *current();
    virtual srs_error_t open();
    virtual srs_error_t write_metadata(SrsMediaPacket *metadata);
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual srs_error_t refresh_metadata() = 0;
    virtual srs_error_t close();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t open_encoder() = 0;
    virtual srs_error_t encode_metadata(SrsMediaPacket *metadata) = 0;
    virtual srs_error_t encode_audio(SrsMediaPacket *audio, SrsFormat *format) = 0;
    virtual srs_error_t encode_video(SrsMediaPacket *video, SrsFormat *format) = 0;
    virtual srs_error_t close_encoder() = 0;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual std::string
    generate_path();
    virtual srs_error_t on_update_duration(SrsMediaPacket *msg);
};

class SrsDvrFlvSegmenter : public SrsDvrSegmenter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFlvTransmuxer *enc_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int64_t duration_offset_;
    int64_t filesize_offset_;
    bool has_keyframe_;

public:
    SrsDvrFlvSegmenter();
    virtual ~SrsDvrFlvSegmenter();

public:
    virtual srs_error_t refresh_metadata();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t open_encoder();
    virtual srs_error_t encode_metadata(SrsMediaPacket *metadata);
    virtual srs_error_t encode_audio(SrsMediaPacket *audio, SrsFormat *format);
    virtual srs_error_t encode_video(SrsMediaPacket *video, SrsFormat *format);
    virtual srs_error_t close_encoder();
};

class SrsDvrMp4Segmenter : public SrsDvrSegmenter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsMp4Encoder *enc_;

public:
    SrsDvrMp4Segmenter();
    virtual ~SrsDvrMp4Segmenter();

public:
    virtual srs_error_t refresh_metadata();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t open_encoder();
    virtual srs_error_t encode_metadata(SrsMediaPacket *metadata);
    virtual srs_error_t encode_audio(SrsMediaPacket *audio, SrsFormat *format);
    virtual srs_error_t encode_video(SrsMediaPacket *video, SrsFormat *format);
    virtual srs_error_t close_encoder();
};

class SrsDvrAsyncCallOnDvr : public ISrsAsyncCallTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpHooks *hooks_;
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    std::string path_;
    ISrsRequest *req_;
    // The stream when the file was closed, which the stats may drop before the hook runs.
    SrsStatisticStreamSnapshot stream_;

public:
    SrsDvrAsyncCallOnDvr(SrsContextId c, ISrsRequest *r, std::string p, const SrsStatisticStreamSnapshot &stream);
    virtual ~SrsDvrAsyncCallOnDvr();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

class ISrsDvrPlan
{
public:
    ISrsDvrPlan();
    virtual ~ISrsDvrPlan();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r) = 0;
    virtual srs_error_t on_publish(ISrsRequest *r) = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata) = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;
    virtual srs_error_t on_reap_segment() = 0;
};

class SrsDvrPlan : public ISrsReloadHandler, public ISrsDvrPlan
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsAsyncCallWorker *async_;
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;

public:
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsOriginHub *hub_;
    ISrsDvrSegmenter *segment_;
    bool dvr_enabled_;

public:
    SrsDvrPlan();
    virtual ~SrsDvrPlan();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
public:
    virtual srs_error_t on_reap_segment();

public:
    static srs_error_t create_plan(ISrsAppConfig *config, std::string vhost, ISrsDvrPlan **pplan);
};

class SrsDvrSessionPlan : public SrsDvrPlan
{
public:
    SrsDvrSessionPlan();
    virtual ~SrsDvrSessionPlan();

public:
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
};

class SrsDvrSegmentPlan : public SrsDvrPlan
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t cduration_;
    bool wait_keyframe_;
    bool reopening_segment_;
    // In ms.
    int64_t last_timestamp_;
    bool has_video_;

public:
    SrsDvrSegmentPlan();
    virtual ~SrsDvrSegmentPlan();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t update_duration(SrsMediaPacket *msg);
};

class ISrsDvr
{
public:
    ISrsDvr();
    virtual void assemble() = 0;
    virtual ~ISrsDvr();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r) = 0;
    virtual srs_error_t on_publish(ISrsRequest *r) = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t on_meta_data(SrsMediaPacket *metadata) = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format) = 0;
};

// DVR is Digital Video Recorder.
class SrsDvr : public ISrsReloadHandler, public ISrsDvr
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsOriginHub *hub_;
    ISrsDvrPlan *plan_;
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool actived_;

public:
    SrsDvr();
    void assemble();
    virtual ~SrsDvr();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *foramt);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
};

extern SrsAsyncCallWorker *_srs_dvr_async;

#endif
