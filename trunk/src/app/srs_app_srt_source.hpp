//
// Copyright (c) 2013-2025 The SRS Authors
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

// The SRT packet with shared message.
class SrsSrtPacket
{
public:
    SrsSrtPacket();
    virtual ~SrsSrtPacket();

public:
    // Wrap buffer to shared_message, which is managed by us.
    char *wrap(int size);
    char *wrap(char *data, int size);
    // Wrap the shared message, we copy it.
    char *wrap(SrsMediaPacket *msg);
    // Copy the SRT packet.
    virtual SrsSrtPacket *copy();

public:
    char *data();
    int size();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsMediaPacket *shared_buffer_;
    // The size of SRT packet or SRT payload.
    int actual_buffer_size_;
};

// The SRT source manager interface.
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

// The SRT source manager.
class SrsSrtSourceManager : public ISrsHourGlassHandler, public ISrsSrtSourceManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsSrtSource> > pool_;
    SrsHourGlass *timer_;

public:
    SrsSrtSourceManager();
    virtual ~SrsSrtSourceManager();

public:
    virtual srs_error_t initialize();
    // interface ISrsHourGlassHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

public:
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param pps the matched source, if success never be NULL.
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps);

public:
    // Get the exists source, NULL when not exists.
    virtual SrsSharedPtr<SrsSrtSource> fetch(ISrsRequest *r);
};

// Global singleton instance.
extern SrsSrtSourceManager *_srs_srt_sources;

// The SRT consumer interface.
class ISrsSrtConsumer
{
public:
    ISrsSrtConsumer();
    virtual ~ISrsSrtConsumer();

public:
    virtual srs_error_t enqueue(SrsSrtPacket *packet) = 0;
    virtual srs_error_t dump_packet(SrsSrtPacket **ppkt) = 0;
    virtual void wait(int nb_msgs, srs_utime_t timeout) = 0;
};

// The SRT consumer, consume packets from SRT stream source.
class SrsSrtConsumer : public ISrsSrtConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Because source references to this object, so we should directly use the source ptr.
    ISrsSrtSource *source_;

public:
    SrsSrtConsumer(ISrsSrtSource *source);
    virtual ~SrsSrtConsumer();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsSrtPacket *> queue_;
    // when source id changed, notice all consumers
    bool should_update_source_id_;
    // The cond wait for mw.
    srs_cond_t mw_wait_;
    bool mw_waiting_;
    int mw_min_msgs_;

public:
    // When source id changed, notice client to print.
    void update_source_id();
    // Put SRT packet into queue.
    srs_error_t enqueue(SrsSrtPacket *packet);
    // For SRT, we only got one packet, because there is not many packets in queue.
    virtual srs_error_t dump_packet(SrsSrtPacket **ppkt);
    // Wait for at-least some messages incoming in queue.
    virtual void wait(int nb_msgs, srs_utime_t timeout);
};

// Collect and build SRT TS packet to AV frames.
class SrsSrtFrameBuilder : public ISrsTsHandler
{
public:
    SrsSrtFrameBuilder(ISrsFrameTarget *target);
    virtual ~SrsSrtFrameBuilder();

public:
    srs_error_t initialize(ISrsRequest *r);

public:
    virtual srs_error_t on_publish();
    virtual srs_error_t on_packet(SrsSrtPacket *pkt);
    virtual void on_unpublish();
    // Interface ISrsTsHandler
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
    // Record sps/pps had changed, if change, need to generate new video sh frame.
    bool sps_pps_change_;
    std::string sps_;
    std::string pps_;
    bool vps_sps_pps_change_;
    std::string hevc_vps_;
    std::string hevc_sps_;
    std::vector<std::string> hevc_pps_;
    // Record audio sepcific config had changed, if change, need to generate new audio sh frame.
    bool audio_sh_change_;
    std::string audio_sh_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // SRT to rtmp, video stream id.
    int video_streamid_;
    // SRT to rtmp, audio stream id.
    int audio_streamid_;
    // Cycle print when audio duration too large because mpegts may merge multi audio frame in one pes packet.
    SrsAlonePithyPrint *pp_audio_duration_;
};

// The SRT source interface.
class ISrsSrtSource : public ISrsSrtTarget
{
public:
    ISrsSrtSource();
    virtual ~ISrsSrtSource();

public:
    virtual SrsContextId source_id() = 0;
    virtual SrsContextId pre_source_id() = 0;
    virtual void on_consumer_destroy(ISrsSrtConsumer *consumer) = 0;
};

// A SRT source is a stream, to publish and to play with.
class SrsSrtSource : public ISrsSrtSource
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;

public:
    SrsSrtSource();
    virtual ~SrsSrtSource();

public:
    virtual srs_error_t initialize(ISrsRequest *r);

public:
    // Whether stream is dead, which is no publisher or player.
    virtual bool stream_is_dead();

public:
    // The source id changed.
    virtual srs_error_t on_source_id_changed(SrsContextId id);
    // Get current source id.
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
    // Update the authentication information in request.
    virtual void update_auth(ISrsRequest *r);

public:
    void set_bridge(ISrsSrtBridge *bridge);

public:
    // Create consumer
    // @param consumer, output the create consumer.
    virtual srs_error_t create_consumer(ISrsSrtConsumer *&consumer);
    // Dumps packets in cache to consumer.
    virtual srs_error_t consumer_dumps(ISrsSrtConsumer *consumer);
    virtual void on_consumer_destroy(ISrsSrtConsumer *consumer);
    // Whether we can publish stream to the source, return false if it exists.
    virtual bool can_publish();
    // When start publish stream.
    virtual srs_error_t on_publish();
    // When stop publish stream.
    virtual void on_unpublish();

public:
    srs_error_t on_packet(SrsSrtPacket *packet);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Source id.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    ISrsRequest *req_;
    // To delivery packets to clients.
    std::vector<ISrsSrtConsumer *> consumers_;
    bool can_publish_;
    // The last die time, while die means neither publishers nor players.
    srs_utime_t stream_die_at_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtBridge *srt_bridge_;
};

#endif
