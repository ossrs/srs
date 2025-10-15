//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_SOURCE_HPP
#define SRS_APP_RTSP_SOURCE_HPP

#include <srs_core.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_kernel_rtc_rtp.hpp>

#include <map>
#include <string>
#include <vector>

class ISrsRequest;
class SrsRtpPacket;
class SrsRtspSource;
class SrsRtspConsumer;
class SrsRtcTrackDescription;
class SrsRtcSourceDescription;
class SrsResourceManager;
class SrsRtspConnection;
class SrsRtpVideoBuilder;
class ISrsStatistic;
class ISrsCircuitBreaker;
class ISrsAppConfig;
class ISrsRtspConnection;

// The RTSP stream consumer, consume packets from RTSP stream source.
class SrsRtspConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Because source references to this object, so we should directly use the source ptr.
    SrsRtspSource *source_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsRtpPacket *> queue_;
    // when source id changed, notice all consumers
    bool should_update_source_id_;
    // The cond wait for mw.
    srs_cond_t mw_wait_;
    bool mw_waiting_;
    int mw_min_msgs_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The callback for stream change event.
    ISrsRtcSourceChangeCallback *handler_;

public:
    SrsRtspConsumer(SrsRtspSource *s);
    virtual ~SrsRtspConsumer();

public:
    // When source id changed, notice client to print.
    virtual void update_source_id();
    // Put RTP packet into queue.
    // @note We do not drop packet here, but drop it in sender.
    srs_error_t enqueue(SrsRtpPacket *pkt);
    // For RTSP, we only got one packet, because there is not many packets in queue.
    virtual srs_error_t dump_packet(SrsRtpPacket **ppkt);
    // Wait for at-least some messages incoming in queue.
    virtual void wait(int nb_msgs);

public:
    void set_handler(ISrsRtcSourceChangeCallback *h) { handler_ = h; } // SrsRtspConsumer::set_handler()
    void on_stream_change(SrsRtcSourceDescription *desc);
};

// The RTSP source manager interface.
class ISrsRtspSourceManager
{
public:
    ISrsRtspSourceManager();
    virtual ~ISrsRtspSourceManager();

public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps) = 0;
    virtual SrsSharedPtr<SrsRtspSource> fetch(ISrsRequest *r) = 0;
};

// The RTSP source manager.
class SrsRtspSourceManager : public ISrsHourGlassHandler, public ISrsRtspSourceManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsRtspSource> > pool_;
    SrsHourGlass *timer_;

public:
    SrsRtspSourceManager();
    virtual ~SrsRtspSourceManager();

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
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps);

public:
    // Get the exists source, NULL when not exists.
    virtual SrsSharedPtr<SrsRtspSource> fetch(ISrsRequest *r);
};

// The global RTSP source manager.
extern SrsRtspSourceManager *_srs_rtsp_sources;

extern SrsResourceManager *_srs_rtsp_manager;

// A Source is a stream, to publish and to play with, binding to SrsRtspPlayStream.
class SrsRtspSource : public ISrsRtpTarget
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_changed() to let all clients know.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    ISrsRequest *req_;
    // Steam description for this steam.
    SrsRtcTrackDescription *audio_desc_;
    SrsRtcTrackDescription *video_desc_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // To delivery stream to clients.
    std::vector<SrsRtspConsumer *>
        consumers_;
    // Whether stream is created, that is, SDP is done.
    bool is_created_;
    // Whether stream is delivering data, that is, DTLS is done.
    bool is_delivering_packets_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The last die time, while die means neither publishers nor players.
    srs_utime_t stream_die_at_;

public:
    SrsRtspSource();
    virtual ~SrsRtspSource();

public:
    virtual srs_error_t initialize(ISrsRequest *r);

public:
    // Whether stream is dead, which is no publisher or player.
    virtual bool stream_is_dead();

public:
    // Update the authentication information in request.
    virtual void update_auth(ISrsRequest *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The stream source changed.
    virtual srs_error_t
    on_source_changed();

public:
    // Get current source id.
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();

public:
    // Create consumer
    // @param consumer, output the create consumer.
    virtual srs_error_t create_consumer(SrsRtspConsumer *&consumer);
    // Dumps packets in cache to consumer.
    // @param ds, whether dumps the sequence header.
    // @param dm, whether dumps the metadata.
    // @param dg, whether dumps the gop cache.
    virtual srs_error_t consumer_dumps(SrsRtspConsumer *consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(SrsRtspConsumer *consumer);
    // Whether we can publish stream to the source, return false if it exists.
    // @remark Note that when SDP is done, we set the stream is not able to publish.
    virtual bool can_publish();
    // For RTSP, the stream is created when SDP is done, and then do DTLS
    virtual void set_stream_created();
    // When start publish stream.
    virtual srs_error_t on_publish();
    // When stop publish stream.
    virtual void on_unpublish();

public:
    // Consume the shared RTP packet, user must free it.
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);

public:
    SrsRtcTrackDescription *audio_desc();
    void set_audio_desc(SrsRtcTrackDescription *audio_desc);
    SrsRtcTrackDescription *video_desc();
    void set_video_desc(SrsRtcTrackDescription *video_desc);
};

// Convert AV frame to RTSP RTP packets.
class SrsRtspRtpBuilder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsRtpTarget *rtp_target_;
    // The format, codec information.
    SrsRtmpFormat *format_;
    // The metadata cache.
    SrsMetaCache *meta_;
    // The video builder, convert frame to RTP packets.
    SrsRtpVideoBuilder *video_builder_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint16_t audio_sequence_;
    uint32_t audio_ssrc_;
    uint8_t audio_payload_type_;
    int audio_sample_rate_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSharedPtr<SrsRtspSource> source_;
    // Lazy initialization flags
    bool audio_initialized_;
    bool video_initialized_;

public:
    SrsRtspRtpBuilder(ISrsRtpTarget *target, SrsSharedPtr<SrsRtspSource> source);
    virtual ~SrsRtspRtpBuilder();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Lazy initialization methods
    srs_error_t
    initialize_audio_track(SrsAudioCodecId codec);
    srs_error_t initialize_video_track(SrsVideoCodecId codec);

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_audio(SrsMediaPacket *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t package_aac(SrsParsedAudioPacket *audio, SrsRtpPacket *pkt);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_video(SrsMediaPacket *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t filter(SrsMediaPacket *msg, SrsFormat *format, bool &has_idr, std::vector<SrsNaluSample *> &samples);
    srs_error_t package_stap_a(SrsMediaPacket *msg, SrsRtpPacket *pkt);
    srs_error_t package_nalus(SrsMediaPacket *msg, const std::vector<SrsNaluSample *> &samples, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_single_nalu(SrsMediaPacket *msg, SrsNaluSample *sample, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_fu_a(SrsMediaPacket *msg, SrsNaluSample *sample, int fu_payload_size, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t consume_packets(std::vector<SrsRtpPacket *> &pkts);
};

class ISrsRtspSendTrack
{
public:
    ISrsRtspSendTrack();
    virtual ~ISrsRtspSendTrack();

public:
    virtual bool set_track_status(bool active) = 0;
    virtual std::string get_track_id() = 0;
    virtual SrsRtcTrackDescription *track_desc() = 0;
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt) = 0;
};

class SrsRtspSendTrack : public ISrsRtspSendTrack
{
public:
    // send track description
    SrsRtcTrackDescription *track_desc_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The owner connection for this track.
    ISrsRtspConnection *session_;

public:
    SrsRtspSendTrack(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc, bool is_audio);
    virtual ~SrsRtspSendTrack();

public:
    // SrsRtspSendTrack::set_nack_no_copy
    bool has_ssrc(uint32_t ssrc);
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();
    virtual SrsRtcTrackDescription *track_desc();
};

class SrsRtspAudioSendTrack : public SrsRtspSendTrack
{
public:
    SrsRtspAudioSendTrack(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ~SrsRtspAudioSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

class SrsRtspVideoSendTrack : public SrsRtspSendTrack
{
public:
    SrsRtspVideoSendTrack(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ~SrsRtspVideoSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

#endif
