//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTMP_SOURCE_HPP
#define SRS_APP_RTMP_SOURCE_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_protocol_st.hpp>

class SrsFormat;
class SrsRtmpFormat;
class SrsLiveConsumer;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsLiveSource;
class SrsRtmpCommonMessage;
class SrsOnMetaDataPacket;
class SrsMediaPacket;
class SrsForwarder;
class ISrsRequest;
class SrsStSocket;
class SrsRtmpServer;
class SrsEdgeProxyContext;
class SrsMessageArray;
class SrsNgExec;
class SrsMessageHeader;
class SrsHls;
class SrsRtc;
class SrsDvr;
class SrsDash;
class SrsEncoder;
class SrsBuffer;
class ISrsStatistic;
class ISrsHttpHooks;
class ISrsAppConfig;
class ISrsLiveSource;
class ISrsHls;
class ISrsDash;
class ISrsDvr;
class ISrsMediaEncoder;
class ISrsNgExec;
class ISrsForwarder;
class ISrsAppFactory;
class ISrsStreamPublishTokenManager;
class ISrsLiveConsumer;
class ISrsClock;

// The time_jitter config:
// full: the stream starts at zero and its timestamps always increase.
// zero: the stream starts at zero; jitter is kept.
// off: timestamps are kept, as for atc.
enum SrsRtmpJitterAlgorithm {
    SrsRtmpJitterAlgorithmFULL = 0x01,
    SrsRtmpJitterAlgorithmZERO,
    SrsRtmpJitterAlgorithmOFF
};
int srs_time_jitter_string2int(std::string time_jitter);

class SrsRtmpJitter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int64_t last_pkt_time_;
    int64_t last_pkt_correct_time_;

public:
    SrsRtmpJitter();
    virtual ~SrsRtmpJitter();

public:
    virtual srs_error_t correct(SrsMediaPacket *msg, SrsRtmpJitterAlgorithm ag);
    virtual int64_t get_time();
};

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
class SrsFastVector
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsMediaPacket **msgs_;
    int nb_msgs_;
    int count_;

public:
    SrsFastVector();
    virtual ~SrsFastVector();

public:
    virtual int size();
    virtual int begin();
    virtual int end();
    virtual SrsMediaPacket **data();
    virtual SrsMediaPacket *at(int index);
    virtual void clear();
    virtual void erase(int begin, int end);
    virtual void push_back(SrsMediaPacket *msg);
    virtual void free();
};
#endif

class ISrsMessageQueue
{
public:
    ISrsMessageQueue();
    virtual ~ISrsMessageQueue();

public:
    virtual int size() = 0;
    virtual srs_utime_t duration() = 0;
    virtual void set_queue_size(srs_utime_t queue_size) = 0;
    virtual srs_error_t enqueue(SrsMediaPacket *msg, bool *is_overflow = NULL) = 0;
    // virtual srs_error_t dump_packets(int max_count, SrsMediaPacket **pmsgs, int &count) = 0;
    virtual srs_error_t dump_packets(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm ag) = 0;
    // virtual void clear() = 0;
};

class SrsMessageQueue : public ISrsMessageQueue
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t av_start_time_;
    srs_utime_t av_end_time_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Whether to skip the log on shrink.
    bool ignore_shrink_;
    srs_utime_t max_queue_size_;
#ifdef SRS_PERF_QUEUE_FAST_VECTOR
    SrsFastVector msgs_;
#else
    std::vector<SrsMediaPacket *> msgs_;
#endif
public:
    SrsMessageQueue(bool ignore_shrink = false);
    virtual ~SrsMessageQueue();

public:
    virtual int size();
    virtual srs_utime_t duration();
    virtual void set_queue_size(srs_utime_t queue_size);

public:
    // The queue owns msg, even on error.
    virtual srs_error_t enqueue(SrsMediaPacket *msg, bool *is_overflow = NULL);
    // The caller allocates pmsgs with room for max_count messages.
    virtual srs_error_t dump_packets(int max_count, SrsMediaPacket **pmsgs, int &count);
    virtual srs_error_t dump_packets(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm ag);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void shrink();

public:
    virtual void clear();
};

class ISrsWakable
{
public:
    ISrsWakable();
    virtual ~ISrsWakable();

public:
    // A message from the play client may be a close, so the recv thread wakes the
    // consumer that waits on its cond to handle it.
    virtual void wakeup() = 0;
};

class ISrsLiveConsumer
{
public:
    ISrsLiveConsumer();
    virtual ~ISrsLiveConsumer();

public:
    virtual srs_error_t enqueue(SrsMediaPacket *shared_msg, bool atc, SrsRtmpJitterAlgorithm ag) = 0;
    virtual srs_error_t dump_packets(SrsMessageArray *msgs, int &count) = 0;
    virtual void set_queue_size(srs_utime_t queue_size) = 0;
    virtual int64_t get_time() = 0;
};

class SrsLiveConsumer : public ISrsWakable, public ISrsLiveConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A raw pointer, since the source holds this consumer.
    ISrsLiveSource *source_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtmpJitter *jitter_;
    SrsMessageQueue *queue_;
    bool paused_;
    bool should_update_source_id_;
#ifdef SRS_PERF_QUEUE_COND_WAIT
    // mw is merged write.
    srs_cond_t mw_wait_;
    bool mw_waiting_;
    int mw_min_msgs_;
    srs_utime_t mw_duration_;
#endif
public:
    SrsLiveConsumer(ISrsLiveSource *s);
    virtual ~SrsLiveConsumer();

public:
    virtual void set_queue_size(srs_utime_t queue_size);
    virtual void update_source_id();

public:
    virtual int64_t get_time();
    // The consumer copies shared_msg; the caller keeps it.
    virtual srs_error_t enqueue(SrsMediaPacket *shared_msg, bool atc, SrsRtmpJitterAlgorithm ag);
    // On input, a positive count caps the messages to dump, and 0 means no cap. On output, it
    // is the number dumped.
    virtual srs_error_t dump_packets(SrsMessageArray *msgs, int &count);
#ifdef SRS_PERF_QUEUE_COND_WAIT
    virtual void wait(int nb_msgs, srs_utime_t msgs_duration);
#endif
    virtual srs_error_t on_play_client_pause(bool is_pause);

public:
    virtual void wakeup();
};

// A new player gets the cached GOP, so it starts fast.
class SrsGopCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // When disabled, a new player waits for the next keyframe, with a black screen until then.
    bool enable_gop_cache_;
    // A stream with no IDR frame would grow the cache until SRS runs out of memory.
    int gop_cache_max_frames_;
    // Whether the max frames cleared the cache mid-GOP, so it caches nothing
    // until the next keyframe, since a player can't decode the frames before it.
    bool wait_keyframe_;
    int cached_video_count_;
    // When the publisher disables video, no keyframe comes to clear the cache, so it is
    // cleared after SRS_PURE_AUDIO_GUESS_COUNT audio packets with no video, about 3s, and
    // stays empty until the next video.
    // @see: https://github.com/ossrs/srs/issues/124
    int audio_after_last_video_count_;
    std::vector<SrsMediaPacket *> gop_cache_;

public:
    SrsGopCache();
    virtual ~SrsGopCache();

public:
    virtual void dispose();
    virtual void set(bool v);
    virtual void set_gop_cache_max_frames(int v);
    virtual bool enabled();
    // The cache copies shared_msg; the caller keeps it.
    virtual srs_error_t cache(SrsMediaPacket *shared_msg);
    virtual void clear();
    virtual srs_error_t dump(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm jitter_algorithm);
    // For atc, the sequence headers take the timestamp of the cached GOP.
    virtual bool empty();
    virtual srs_utime_t start_time();
    virtual bool pure_audio();
};

// The server implements it, to mount the HTTP stream when RTMP publishing starts.
class ISrsLiveSourceHandler
{
public:
    ISrsLiveSourceHandler();
    virtual ~ISrsLiveSourceHandler();

public:
    virtual srs_error_t on_publish(ISrsRequest *r) = 0;
    virtual void on_unpublish(ISrsRequest *r) = 0;
};

// For the mix_correct config.
class SrsMixQueue
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t nb_videos_;
    uint32_t nb_audios_;
    std::multimap<int64_t, SrsMediaPacket *> msgs_;

public:
    SrsMixQueue();
    virtual ~SrsMixQueue();

public:
    virtual void clear();
    virtual void push(SrsMediaPacket *msg);
    virtual SrsMediaPacket *pop();
};

class ISrsOriginHub
{
public:
    ISrsOriginHub();
    virtual ~ISrsOriginHub();

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r) = 0;
    virtual void dispose() = 0;
    virtual srs_error_t cycle() = 0;
    virtual bool active() = 0;
    virtual srs_utime_t cleanup_delay() = 0;

public:
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata, SrsOnMetaDataPacket *packet) = 0;
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio) = 0;
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, bool is_sequence_header) = 0;

public:
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t on_dvr_request_sh() = 0;
    virtual srs_error_t on_hls_request_sh() = 0;
    virtual srs_error_t on_dash_request_sh() = 0;
    virtual srs_error_t on_forwarder_start(SrsForwarder *forwarder) = 0;
};

// The features that run only on an origin, such as DVR, HLS, forward and transcode; an
// edge does not use them.
class SrsOriginHub : public ISrsReloadHandler, public ISrsOriginHub
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsHttpHooks *hooks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A raw pointer, since the source holds this hub.
    ISrsLiveSource *source_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    bool is_active_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHls *hls_;
    ISrsDash *dash_;
    ISrsDvr *dvr_;
    ISrsMediaEncoder *encoder_;
    // The exec feature of nginx-rtmp.
    ISrsNgExec *ng_exec_;
    std::vector<ISrsForwarder *> forwarders_;

public:
    SrsOriginHub();
    void assemble();
    virtual ~SrsOriginHub();

public:
    // The source owns r.
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r);
    virtual void dispose();
    virtual srs_error_t cycle();
    virtual bool active();
    srs_utime_t cleanup_delay();

public:
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata, SrsOnMetaDataPacket *packet);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, bool is_sequence_header);

public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();

public:
    virtual srs_error_t on_forwarder_start(SrsForwarder *forwarder);
    virtual srs_error_t on_dvr_request_sh();
    virtual srs_error_t on_hls_request_sh();
    virtual srs_error_t on_dash_request_sh();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t create_forwarders();
    virtual srs_error_t create_backend_forwarders(bool &applied);
    virtual void destroy_forwarders();
};

class SrsMetaCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The FLV script data tag.
    SrsMediaPacket *meta_;
    SrsMediaPacket *previous_meta_;
    // For example, the SPS and PPS of H.264.
    SrsMediaPacket *video_;
    SrsMediaPacket *previous_video_;
    // For example, the AudioSpecificConfig of AAC.
    SrsMediaPacket *audio_;
    SrsMediaPacket *previous_audio_;
    SrsRtmpFormat *vformat_;
    SrsRtmpFormat *aformat_;

public:
    SrsMetaCache();
    virtual ~SrsMetaCache();

public:
    virtual void dispose();
    virtual void clear();

public:
    virtual SrsMediaPacket *data();
    // vsh is the video sequence header.
    virtual SrsMediaPacket *vsh();
    virtual SrsFormat *vsh_format();
    // ash is the audio sequence header.
    virtual SrsMediaPacket *ash();
    virtual SrsFormat *ash_format();
    // dm dumps the metadata, and ds the sequence headers.
    virtual srs_error_t dumps(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm ag, bool dm, bool ds);

public:
    virtual SrsMediaPacket *previous_data();
    virtual void update_previous_data();
    virtual SrsMediaPacket *previous_vsh();
    virtual SrsMediaPacket *previous_ash();
    virtual void update_previous_vsh();
    virtual void update_previous_ash();

public:
    virtual srs_error_t update_data(SrsMessageHeader *header, SrsOnMetaDataPacket *metadata, bool &updated);
    virtual srs_error_t update_ash(SrsMediaPacket *msg);
    virtual srs_error_t update_vsh(SrsMediaPacket *msg);
};

class ISrsLiveSourceManager
{
public:
    ISrsLiveSourceManager();
    virtual ~ISrsLiveSourceManager();

public:
    // On success, pps is never NULL.
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps) = 0;
    // Returns NULL when the stream has no source.
    virtual SrsSharedPtr<SrsLiveSource> fetch(ISrsRequest *r) = 0;

public:
    virtual void dispose() = 0;
    virtual srs_error_t initialize() = 0;
};

class SrsLiveSourceManager : public ISrsHourGlassHandler, public ISrsLiveSourceManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsStreamPublishTokenManager *stream_publish_tokens_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsLiveSource> > pool_;
    ISrsHourGlass *timer_;

public:
    SrsLiveSourceManager();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsLiveSourceManager();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps);

public:
    virtual SrsSharedPtr<SrsLiveSource> fetch(ISrsRequest *r);

public:
    virtual void dispose();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

public:
    // At exit, so gmc can find memory leaks.
    virtual void destroy();
};

extern SrsLiveSourceManager *_srs_sources;

class ISrsLiveSource
{
public:
    ISrsLiveSource();
    virtual ~ISrsLiveSource();

public:
    virtual void on_consumer_destroy(SrsLiveConsumer *consumer) = 0;
    virtual SrsContextId source_id() = 0;
    virtual SrsContextId pre_source_id() = 0;
    virtual SrsMetaCache *meta() = 0;
    virtual SrsRtmpFormat *format() = 0;
    virtual srs_error_t on_source_id_changed(SrsContextId id) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t on_audio(SrsRtmpCommonMessage *audio) = 0;
    virtual srs_error_t on_video(SrsRtmpCommonMessage *video) = 0;
    virtual srs_error_t on_aggregate(SrsRtmpCommonMessage *msg) = 0;
    virtual srs_error_t on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata) = 0;
};

class SrsLiveSource : public ISrsReloadHandler, public ISrsFrameTarget, public ISrsLiveSource
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsLiveSourceHandler *handler_;
    ISrsAppFactory *app_factory_;
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The publisher's context id, or on an edge, the edge ingest's. When it changes, such as
    // on an edge reconnect, on_source_id_changed tells the consumers.
    SrsContextId source_id_;
    SrsContextId pre_source_id_;
    ISrsRequest *req_;
    std::vector<SrsLiveConsumer *> consumers_;
    SrsRtmpJitterAlgorithm jitter_algorithm_;
    bool mix_correct_;
    SrsMixQueue *mix_queue_;
    // ATC keeps the absolute time of each message. Without it, timestamps start from 0, as
    // Flash needs.
    bool atc_;
    bool is_monotonically_increase_;
    int64_t last_packet_time_;
    ISrsRtmpBridge *rtmp_bridge_;
    SrsPlayEdge *play_edge_;
    SrsPublishEdge *publish_edge_;
    SrsGopCache *gop_cache_;
    ISrsOriginHub *hub_;
    SrsMetaCache *meta_;
    SrsRtmpFormat *format_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool can_publish_;
    // Dead means no publisher and no player.
    srs_utime_t stream_die_at_;
    // Idle means no player.
    srs_utime_t publisher_idle_at_;

public:
    SrsLiveSource();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsLiveSource();

public:
    virtual void dispose();
    virtual srs_error_t cycle();
    virtual bool stream_is_dead();
    bool publisher_is_idle_for(srs_utime_t timeout);
    virtual SrsMetaCache *meta();
    virtual SrsRtmpFormat *format();

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> wrapper, ISrsRequest *r);
    void set_bridge(ISrsRtmpBridge *v);

public:
    virtual srs_error_t on_source_id_changed(SrsContextId id);
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
    // On an edge, the source stays inactive until the stream is pulled from the origin.
    virtual bool inactive();
    virtual void update_auth(ISrsRequest *r);

public:
    virtual bool can_publish(bool is_edge);
    virtual srs_error_t on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata);

public:
    // TODO: FIXME: Use SrsMediaPacket instead.
    virtual srs_error_t on_audio(SrsRtmpCommonMessage *audio);
    srs_error_t on_frame(SrsMediaPacket *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_audio_imp(SrsMediaPacket *audio);

public:
    // TODO: FIXME: Use SrsMediaPacket instead.
    virtual srs_error_t on_video(SrsRtmpCommonMessage *video);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_video_imp(SrsMediaPacket *video);

public:
    virtual srs_error_t on_aggregate(SrsRtmpCommonMessage *msg);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();

public:
    virtual srs_error_t create_consumer(SrsLiveConsumer *&consumer);
    // ds dumps the sequence headers, dm the metadata, and dg the GOP cache.
    virtual srs_error_t consumer_dumps(ISrsLiveConsumer *consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(SrsLiveConsumer *consumer);
    virtual void set_cache(bool enabled);
    virtual void set_gop_cache_max_frames(int v);
    virtual SrsRtmpJitterAlgorithm jitter();

public:
    virtual srs_error_t on_edge_start_publish();
    virtual srs_error_t on_edge_proxy_publish(SrsRtmpCommonMessage *msg);
    virtual void on_edge_proxy_unpublish();
};

#endif
