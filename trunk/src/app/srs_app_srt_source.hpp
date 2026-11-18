//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SRT_SOURCE_HPP
#define SRS_APP_SRT_SOURCE_HPP

#include <srs_core.hpp>

#include <map>
#include <vector>

#include <srs_app_stream_bridge.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_protocol_st.hpp>

class SrsMediaPacket;
class ISrsRequest;
class SrsLiveSource;
class SrsSrtSource;
class SrsAlonePithyPrint;
class SrsSrtFrameBuilder;
class ISrsStatistic;
class ISrsSrtConsumer;
class ISrsSrtSource;
class ISrsClock;
class ISrsAppFactory;

class SrsSrtPacket
{
public:
    SrsSrtPacket();
    virtual ~SrsSrtPacket();

public:
    char *wrap(int size);
    char *wrap(char *data, int size);
    char *wrap(SrsMediaPacket *msg);
    virtual SrsSrtPacket *copy();

public:
    char *data();
    int size();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsMediaPacket *shared_buffer_;
    int actual_buffer_size_;
};

class ISrsSrtSourceManager
{
public:
    ISrsSrtSourceManager();
    virtual ~ISrsSrtSourceManager();

public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps) = 0;
    virtual SrsSharedPtr<SrsSrtSource> fetch(ISrsRequest *r) = 0;
};

class SrsSrtSourceManager : public ISrsHourGlassHandler, public ISrsSrtSourceManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsSrtSource> > pool_;
    ISrsHourGlass *timer_;

public:
    SrsSrtSourceManager();
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsSrtSourceManager();

public:
    virtual srs_error_t initialize();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

public:
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps);

public:
    virtual SrsSharedPtr<SrsSrtSource> fetch(ISrsRequest *r);
};

extern SrsSrtSourceManager *_srs_srt_sources;

class ISrsSrtConsumer
{
public:
    ISrsSrtConsumer();
    virtual ~ISrsSrtConsumer();

public:
    virtual srs_error_t enqueue(SrsSrtPacket *packet) = 0;
    virtual srs_error_t dump_packet(SrsSrtPacket **ppkt) = 0;
    virtual bool wait(int nb_msgs, srs_utime_t timeout) = 0;
};

class SrsSrtConsumer : public ISrsSrtConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The source holds this consumer, so this is a raw pointer to the source.
    ISrsSrtSource *source_;

public:
    SrsSrtConsumer(ISrsSrtSource *source);
    virtual ~SrsSrtConsumer();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsSrtPacket *> queue_;
    bool should_update_source_id_;
    srs_cond_t mw_wait_;
    bool mw_waiting_;
    int mw_min_msgs_;

public:
    void update_source_id();
    srs_error_t enqueue(SrsSrtPacket *packet);
    // One packet at a time, since the SRT queue holds few packets.
    virtual srs_error_t dump_packet(SrsSrtPacket **ppkt);
    virtual bool wait(int nb_msgs, srs_utime_t timeout);
};

class ISrsSrtFormat
{
public:
    ISrsSrtFormat();
    virtual ~ISrsSrtFormat();

public:
    virtual srs_error_t initialize(ISrsRequest *req) = 0;
    virtual srs_error_t on_srt_packet(SrsSrtPacket *pkt) = 0;
};

class SrsSrtFormat : public ISrsSrtFormat, public ISrsTsHandler
{
public:
    SrsSrtFormat();
    virtual ~SrsSrtFormat();

public:
    srs_error_t initialize(ISrsRequest *req);
    srs_error_t on_srt_packet(SrsSrtPacket *pkt);

public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void count_ts_frame(char *p);
    void update_ts_message_stats(bool is_audio);
    srs_error_t parse_video_codec(SrsTsMessage *msg, bool &parsed);
    srs_error_t parse_audio_codec(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsStatistic *stat_;
    SrsTsContext *ts_ctx_;
    SrsRtmpFormat *format_;
    bool video_codec_reported_;
    bool audio_codec_reported_;
    int nn_video_frames_;
    int nn_audio_frames_;
};

class SrsSrtFrameBuilder : public ISrsTsHandler
{
public:
    SrsSrtFrameBuilder(ISrsFrameTarget *target);
    virtual ~SrsSrtFrameBuilder();
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    srs_error_t initialize(ISrsRequest *r);

public:
    virtual srs_error_t on_publish();
    virtual srs_error_t on_srt_packet(SrsSrtPacket *pkt);
    virtual void on_unpublish();

public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_ts_video_avc(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t check_sps_pps_change(SrsTsMessage *msg);
    srs_error_t on_h264_frame(SrsTsMessage *msg, std::vector<std::pair<char *, int> > &ipb_frames);
    srs_error_t check_audio_sh_change(SrsTsMessage *msg, uint32_t pts);
    srs_error_t on_aac_frame(SrsTsMessage *msg, uint32_t pts, char *frame, int frame_size);
    srs_error_t on_ts_video_hevc(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t check_vps_sps_pps_change(SrsTsMessage *msg);
    srs_error_t on_hevc_frame(SrsTsMessage *msg, std::vector<std::pair<char *, int> > &ipb_frames);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFrameTarget *frame_target_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTsContext *ts_ctx_;
    bool sps_pps_change_;
    std::string sps_;
    std::string pps_;
    bool vps_sps_pps_change_;
    std::string hevc_vps_;
    std::string hevc_sps_;
    std::vector<std::string> hevc_pps_;
    bool audio_sh_change_;
    std::string audio_sh_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int video_streamid_;
    int audio_streamid_;
    SrsAlonePithyPrint *pp_audio_duration_;
};

class ISrsSrtSource : public ISrsSrtTarget
{
public:
    ISrsSrtSource();
    virtual ~ISrsSrtSource();

public:
    virtual SrsContextId source_id() = 0;
    virtual SrsContextId pre_source_id() = 0;
    virtual void on_consumer_destroy(ISrsSrtConsumer *consumer) = 0;
    virtual bool can_publish() = 0;
};

class SrsSrtSource : public ISrsSrtSource
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsClock *clk_;

public:
    SrsSrtSource();
    virtual ~SrsSrtSource();
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    virtual srs_error_t initialize(ISrsRequest *r);

public:
    virtual bool stream_is_dead();

public:
    virtual srs_error_t on_source_id_changed(SrsContextId id);
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
    virtual void update_auth(ISrsRequest *r);

public:
    void set_bridge(ISrsSrtBridge *bridge);

public:
    virtual srs_error_t create_consumer(ISrsSrtConsumer *&consumer);
    virtual srs_error_t consumer_dumps(ISrsSrtConsumer *consumer);
    virtual void on_consumer_destroy(ISrsSrtConsumer *consumer);
    virtual bool can_publish();
    virtual srs_error_t on_publish();
    virtual void on_unpublish();

public:
    srs_error_t on_srt_packet(SrsSrtPacket *packet);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId source_id_;
    SrsContextId pre_source_id_;
    ISrsRequest *req_;
    std::vector<ISrsSrtConsumer *> consumers_;
    bool can_publish_;
    // When the source last had neither publishers nor players.
    srs_utime_t stream_die_at_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtBridge *srt_bridge_;
    ISrsSrtFormat *format_;
};

#endif
