//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_SOURCE_HPP
#define SRS_APP_RTSP_SOURCE_HPP

#include <srs_core.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_app_source.hpp>
#include <srs_kernel_rtc_rtp.hpp>

#include <map>
#include <string>
#include <vector>

class SrsRequest;
class SrsRtpPacket;
class SrsRtspSource;
class SrsRtspConsumer;
class SrsRtcTrackDescription;
class SrsRtcSourceDescription;
class SrsResourceManager;
class SrsRtspConnection;
class SrsRtpVideoBuilder;

// The RTSP stream consumer, consume packets from RTSP stream source.
class SrsRtspConsumer
{
private:
    // Because source references to this object, so we should directly use the source ptr.
    SrsRtspSource *source_;

private:
    std::vector<SrsRtpPacket *> queue;
    // when source id changed, notice all consumers
    bool should_update_source_id;
    // The cond wait for mw.
    srs_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;

private:
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

class SrsRtspSourceManager : public ISrsHourGlass
{
private:
    srs_mutex_t lock;
    std::map<std::string, SrsSharedPtr<SrsRtspSource> > pool;
    SrsHourGlass *timer_;

public:
    SrsRtspSourceManager();
    virtual ~SrsRtspSourceManager();

public:
    virtual srs_error_t initialize();
    // interface ISrsHourGlass
private:
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

public:
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param pps the matched source, if success never be NULL.
    virtual srs_error_t fetch_or_create(SrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps);

public:
    // Get the exists source, NULL when not exists.
    virtual SrsSharedPtr<SrsRtspSource> fetch(SrsRequest *r);
};

// The global RTSP source manager.
extern SrsRtspSourceManager *_srs_rtsp_sources;

extern SrsResourceManager *_srs_rtsp_manager;

// A Source is a stream, to publish and to play with, binding to SrsRtspPlayStream.
class SrsRtspSource
{
private:
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_changed() to let all clients know.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    SrsRequest *req;
    // Steam description for this steam.
    SrsRtcTrackDescription *audio_desc_;
    SrsRtcTrackDescription *video_desc_;

private:
    // To delivery stream to clients.
    std::vector<SrsRtspConsumer *> consumers;
    // Whether stream is created, that is, SDP is done.
    bool is_created_;
    // Whether stream is delivering data, that is, DTLS is done.
    bool is_delivering_packets_;

private:
    // The last die time, while die means neither publishers nor players.
    srs_utime_t stream_die_at_;

public:
    SrsRtspSource();
    virtual ~SrsRtspSource();

public:
    virtual srs_error_t initialize(SrsRequest *r);

public:
    // Whether stream is dead, which is no publisher or player.
    virtual bool stream_is_dead();

public:
    // Update the authentication information in request.
    virtual void update_auth(SrsRequest *r);

private:
    // The stream source changed.
    virtual srs_error_t on_source_changed();

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
    srs_error_t on_rtp(SrsRtpPacket *pkt);

public:
    SrsRtcTrackDescription *audio_desc();
    void set_audio_desc(SrsRtcTrackDescription *audio_desc);
    SrsRtcTrackDescription *video_desc();
    void set_video_desc(SrsRtcTrackDescription *video_desc);
};

// Convert AV frame to RTSP RTP packets.
class SrsRtspRtpBuilder
{
private:
    SrsRequest *req;
    SrsFrameToRtspBridge *bridge_;
    // The format, codec information.
    SrsRtmpFormat *format;
    // The metadata cache.
    SrsMetaCache *meta;
    // The video builder, convert frame to RTP packets.
    SrsRtpVideoBuilder *video_builder_;

private:
    uint16_t audio_sequence;
    uint32_t audio_ssrc_;
    uint8_t audio_payload_type_;
    int audio_sample_rate_;

private:
    SrsSharedPtr<SrsRtspSource> source_;
    // Lazy initialization flags
    bool audio_initialized_;
    bool video_initialized_;

public:
    SrsRtspRtpBuilder(SrsFrameToRtspBridge *bridge, SrsSharedPtr<SrsRtspSource> source);
    virtual ~SrsRtspRtpBuilder();

private:
    // Lazy initialization methods
    srs_error_t initialize_audio_track(SrsAudioCodecId codec);
    srs_error_t initialize_video_track(SrsVideoCodecId codec);

public:
    virtual srs_error_t initialize(SrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsSharedPtrMessage *frame);

private:
    virtual srs_error_t on_audio(SrsSharedPtrMessage *msg);

private:
    srs_error_t package_aac(SrsAudioFrame *audio, SrsRtpPacket *pkt);

private:
    virtual srs_error_t on_video(SrsSharedPtrMessage *msg);

private:
    srs_error_t filter(SrsSharedPtrMessage *msg, SrsFormat *format, bool &has_idr, std::vector<SrsSample *> &samples);
    srs_error_t package_stap_a(SrsSharedPtrMessage *msg, SrsRtpPacket *pkt);
    srs_error_t package_nalus(SrsSharedPtrMessage *msg, const std::vector<SrsSample *> &samples, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_single_nalu(SrsSharedPtrMessage *msg, SrsSample *sample, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t package_fu_a(SrsSharedPtrMessage *msg, SrsSample *sample, int fu_payload_size, std::vector<SrsRtpPacket *> &pkts);
    srs_error_t consume_packets(std::vector<SrsRtpPacket *> &pkts);
};

class SrsRtspSendTrack
{
public:
    // send track description
    SrsRtcTrackDescription *track_desc_;

protected:
    // The owner connection for this track.
    SrsRtspConnection *session_;

public:
    SrsRtspSendTrack(SrsRtspConnection *session, SrsRtcTrackDescription *track_desc, bool is_audio);
    virtual ~SrsRtspSendTrack();

public:
    // SrsRtspSendTrack::set_nack_no_copy
    bool has_ssrc(uint32_t ssrc);
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt) = 0;
};

class SrsRtspAudioSendTrack : public SrsRtspSendTrack
{
public:
    SrsRtspAudioSendTrack(SrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ~SrsRtspAudioSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

class SrsRtspVideoSendTrack : public SrsRtspSendTrack
{
public:
    SrsRtspVideoSendTrack(SrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ~SrsRtspVideoSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

#endif
