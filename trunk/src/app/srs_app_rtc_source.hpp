//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_SOURCE_HPP
#define SRS_APP_RTC_SOURCE_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_app_stream_bridge.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_st.hpp>

class ISrsRequest;
class SrsMetaCache;
class SrsMediaPacket;
class SrsRtmpCommonMessage;
class SrsMessageArray;
class SrsRtcSource;
class ISrsAudioTranscoder;
class SrsRtpPacket;
class SrsNaluSample;
class SrsRtcSourceDescription;
class SrsRtcTrackDescription;
class SrsRtcConnection;
class SrsRtpRingBuffer;
class SrsRtpNackForReceiver;
class SrsJsonObject;
class SrsErrorPithyPrint;
class SrsRtcFrameBuilder;
class SrsLiveSource;
class SrsRtpVideoBuilder;
class ISrsRtcConsumer;
class ISrsCircuitBreaker;
class ISrsRtcPublishStream;
class ISrsAppFactory;

// Firefox defaults as 109, Chrome is 111.
const int kAudioPayloadType = 111;
// Firefox defaults as 126, Chrome is 102.
const int kVideoPayloadType = 102;
// Chrome HEVC defaults as 49.
const int KVideoPayloadTypeHevc = 49;

// Audio jitter buffer size (in packets)
const int AUDIO_JITTER_BUFFER_SIZE = 100;
// Sliding window size for continuous processing
const int SLIDING_WINDOW_SIZE = 10;
// Maximum waiting time for out-of-order packets (in ms)
const int MAX_AUDIO_WAIT_MS = 100;

// NTP time for RTC.
class SrsNtp
{
public:
    uint64_t system_ms_;
    uint64_t ntp_;
    uint32_t ntp_second_;
    uint32_t ntp_fractions_;

public:
    SrsNtp();
    virtual ~SrsNtp();

public:
    static SrsNtp from_time_ms(uint64_t ms);
    static SrsNtp to_time_ms(uint64_t ntp);

public:
    static uint64_t kMagicNtpFractionalUnit;
};

// When RTC stream publish and re-publish.
class ISrsRtcSourceChangeCallback
{
public:
    ISrsRtcSourceChangeCallback();
    virtual ~ISrsRtcSourceChangeCallback();

public:
    virtual void on_stream_change(SrsRtcSourceDescription *desc) = 0;
};

// The RTC source for consumer.
class ISrsRtcSourceForConsumer
{
public:
    ISrsRtcSourceForConsumer();
    virtual ~ISrsRtcSourceForConsumer();

public:
    virtual void on_consumer_destroy(ISrsRtcConsumer *consumer) = 0;
    virtual SrsContextId source_id() = 0;
    virtual SrsContextId pre_source_id() = 0;
};

// The RTC consumer interface.
class ISrsRtcConsumer
{
public:
    ISrsRtcConsumer();
    virtual ~ISrsRtcConsumer();

public:
    virtual void update_source_id() = 0;
    virtual void on_stream_change(SrsRtcSourceDescription *desc) = 0;
    virtual srs_error_t enqueue(SrsRtpPacket *pkt) = 0;
};

// The RTC stream consumer, consume packets from RTC stream source.
class SrsRtcConsumer : public ISrsRtcConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Because source references to this object, so we should directly use the source ptr.
    ISrsRtcSourceForConsumer *source_;

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
    SrsRtcConsumer(ISrsRtcSourceForConsumer *s);
    virtual ~SrsRtcConsumer();

public:
    // When source id changed, notice client to print.
    virtual void update_source_id();
    // Put RTP packet into queue.
    // @note We do not drop packet here, but drop it in sender.
    srs_error_t enqueue(SrsRtpPacket *pkt);
    // For RTC, we only got one packet, because there is not many packets in queue.
    virtual srs_error_t dump_packet(SrsRtpPacket **ppkt);
    // Wait for at-least some messages incoming in queue.
    virtual void wait(int nb_msgs);

public:
    void set_handler(ISrsRtcSourceChangeCallback *h) { handler_ = h; } // SrsRtcConsumer::set_handler()
    void on_stream_change(SrsRtcSourceDescription *desc);
};

// The RTC source manager interface.
class ISrsRtcSourceManager
{
public:
    ISrsRtcSourceManager();
    virtual ~ISrsRtcSourceManager();

public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps) = 0;
    virtual SrsSharedPtr<SrsRtcSource> fetch(ISrsRequest *r) = 0;
};

// The RTC source manager.
class SrsRtcSourceManager : public ISrsRtcSourceManager, public ISrsHourGlassHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsRtcSource> > pool_;
    SrsHourGlass *timer_;

public:
    SrsRtcSourceManager();
    virtual ~SrsRtcSourceManager();

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
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps);

public:
    // Get the exists source, NULL when not exists.
    virtual SrsSharedPtr<SrsRtcSource> fetch(ISrsRequest *r);
};

// Global singleton instance.
extern SrsRtcSourceManager *_srs_rtc_sources;

// The event handler for RTC source.
class ISrsRtcSourceEventHandler
{
public:
    ISrsRtcSourceEventHandler();
    virtual ~ISrsRtcSourceEventHandler();

public:
    // stream unpublish, sync API.
    virtual void on_unpublish() = 0;
    // no player subscribe this stream, sync API
    virtual void on_consumers_finished() = 0;
};

// A Source is a stream, to publish and to play with, binding to SrsRtcPublishStream and SrsRtcPlayStream.
class SrsRtcSource : public ISrsRtpTarget, public ISrsFastTimerHandler, public ISrsRtcSourceForConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The RTP bridge, convert RTP packets to other protocols.
    ISrsRtcBridge *rtc_bridge_;
    // Circuit breaker for protecting server resources.
    ISrsCircuitBreaker *circuit_breaker_;
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_changed() to let all clients know.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    ISrsRequest *req_;
    ISrsRtcPublishStream *publish_stream_;
    // Steam description for this steam.
    SrsRtcSourceDescription *stream_desc_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // To delivery stream to clients.
    std::vector<ISrsRtcConsumer *>
        consumers_;
    // Whether stream is created, that is, SDP is done.
    bool is_created_;
    // Whether stream is delivering data, that is, DTLS is done.
    bool is_delivering_packets_;
    // Notify stream event to event handler
    std::vector<ISrsRtcSourceEventHandler *> event_handlers_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The PLI for RTC2RTMP.
    srs_utime_t pli_for_rtmp_;
    srs_utime_t pli_elapsed_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The last die time, while die means neither publishers nor players.
    srs_utime_t stream_die_at_;

public:
    SrsRtcSource();
    virtual ~SrsRtcSource();

public:
    virtual srs_error_t initialize(ISrsRequest *r);

public:
    // Whether stream is dead, which is no publisher or player.
    virtual bool stream_is_dead();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void init_for_play_before_publishing();

public:
    // Update the authentication information in request.
    virtual void update_auth(ISrsRequest *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The stream source changed.
    virtual srs_error_t on_source_changed();

public:
    // Get current source id.
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();

public:
    virtual void set_bridge(ISrsRtcBridge *bridge);

public:
    // Create consumer
    // @param consumer, output the create consumer.
    virtual srs_error_t create_consumer(ISrsRtcConsumer *&consumer);
    // Dumps packets in cache to consumer.
    // @param ds, whether dumps the sequence header.
    // @param dm, whether dumps the metadata.
    // @param dg, whether dumps the gop cache.
    virtual srs_error_t consumer_dumps(ISrsRtcConsumer *consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(ISrsRtcConsumer *consumer);
    // Whether we can publish stream to the source, return false if it exists.
    // @remark Note that when SDP is done, we set the stream is not able to publish.
    virtual bool can_publish();
    // For RTC, the stream is created when SDP is done, and then do DTLS
    virtual void set_stream_created();
    // When start publish stream.
    virtual srs_error_t on_publish();
    // When stop publish stream.
    virtual void on_unpublish();

public:
    // For event handler
    virtual void rtc_source_subscribe(ISrsRtcSourceEventHandler *h);
    virtual void rtc_source_unsubscribe(ISrsRtcSourceEventHandler *h);

public:
    // Get and set the publisher, passed to consumer to process requests such as PLI.
    virtual ISrsRtcPublishStream *publish_stream();
    virtual void set_publish_stream(ISrsRtcPublishStream *v);
    // Consume the shared RTP packet, user must free it.
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    // Set and get stream description for source
    virtual bool has_stream_desc();
    virtual void set_stream_desc(SrsRtcSourceDescription *stream_desc);
    virtual std::vector<SrsRtcTrackDescription *> get_track_desc(std::string type, std::string media_type);
    // interface ISrsFastTimerHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

#ifdef SRS_FFMPEG_FIT

// Convert AV frame to RTC RTP packets.
class SrsRtcRtpBuilder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

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
    SrsAudioCodecId latest_codec_;
    ISrsAudioTranscoder *codec_;
    bool keep_bframe_;
    bool keep_avc_nalu_sei_;
    bool merge_nalus_;
    uint16_t audio_sequence_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t audio_ssrc_;
    uint8_t audio_payload_type_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSharedPtr<SrsRtcSource> source_;
    // Lazy initialization flags
    bool audio_initialized_;
    bool video_initialized_;

public:
    SrsRtcRtpBuilder(ISrsAppFactory *factory, ISrsRtpTarget *target, SrsSharedPtr<SrsRtcSource> source);
    virtual ~SrsRtcRtpBuilder();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Lazy initialization methods
    srs_error_t initialize_audio_track(SrsAudioCodecId codec);
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
    srs_error_t init_codec(SrsAudioCodecId codec);
    srs_error_t transcode(SrsParsedAudioPacket *audio);
    srs_error_t package_opus(SrsParsedAudioPacket *audio, SrsRtpPacket *pkt);

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

// Video packet cache interface
class ISrsRtcFrameBuilderVideoPacketCache
{
public:
    ISrsRtcFrameBuilderVideoPacketCache();
    virtual ~ISrsRtcFrameBuilderVideoPacketCache();

public:
    virtual SrsRtpPacket *get_packet(uint16_t sequence_number) = 0;
    virtual void store_packet(SrsRtpPacket *pkt) = 0;
    virtual void clear_all() = 0;
    virtual SrsRtpPacket *take_packet(uint16_t sequence_number) = 0;
    virtual int32_t find_next_lost_sn(uint16_t current_sn, uint16_t header_sn, uint16_t &end_sn) = 0;
    virtual bool check_frame_complete(const uint16_t start, const uint16_t end) = 0;
};

// Video packet cache for RTP packet management
// TODO: Maybe should use SrsRtpRingBuffer?
class SrsRtcFrameBuilderVideoPacketCache : public ISrsRtcFrameBuilderVideoPacketCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    const static uint16_t cache_size_ = 512;
    struct RtcPacketCache {
        bool in_use_;
        uint16_t sn_;
        uint32_t ts_;
        uint32_t rtp_ts_;
        SrsRtpPacket *pkt_;
    };
    RtcPacketCache cache_pkts_[cache_size_];

public:
    SrsRtcFrameBuilderVideoPacketCache();
    virtual ~SrsRtcFrameBuilderVideoPacketCache();

public:
    SrsRtpPacket *get_packet(uint16_t sequence_number);
    void store_packet(SrsRtpPacket *pkt);
    void clear_all();
    SrsRtpPacket *take_packet(uint16_t sequence_number);

public:
    // Find next lost sequence number starting from current_sn
    // Returns: lost_sn if found, -1 if complete frame found (sets end_sn), -2 if cache overflow
    int32_t find_next_lost_sn(uint16_t current_sn, uint16_t header_sn, uint16_t &end_sn);
    // Check if frame is complete by verifying FU-A start/end fragment counts match
    bool check_frame_complete(const uint16_t start, const uint16_t end);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_slot_in_use(uint16_t sequence_number);
    uint32_t get_rtp_timestamp(uint16_t sequence_number);
    inline uint16_t cache_index(uint16_t sequence_number)
    {
        return sequence_number % cache_size_;
    }
};

// Video frame detector interface
class ISrsRtcFrameBuilderVideoFrameDetector
{
public:
    ISrsRtcFrameBuilderVideoFrameDetector();
    virtual ~ISrsRtcFrameBuilderVideoFrameDetector();

public:
    virtual void on_keyframe_start(SrsRtpPacket *pkt) = 0;
    virtual srs_error_t detect_frame(uint16_t received, uint16_t &frame_start, uint16_t &frame_end, bool &frame_ready) = 0;
    virtual srs_error_t detect_next_frame(uint16_t next_head, uint16_t &next_start, uint16_t &next_end, bool &next_ready) = 0;
    virtual void on_keyframe_detached() = 0;
    virtual bool is_lost_sn(uint16_t received) = 0;
};

// Video frame detector for managing frame boundaries and packet loss detection
class SrsRtcFrameBuilderVideoFrameDetector : public ISrsRtcFrameBuilderVideoFrameDetector
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcFrameBuilderVideoPacketCache *video_cache_;
    uint16_t header_sn_;
    uint16_t lost_sn_;
    int64_t rtp_key_frame_ts_;

public:
    SrsRtcFrameBuilderVideoFrameDetector(ISrsRtcFrameBuilderVideoPacketCache *cache);
    virtual ~SrsRtcFrameBuilderVideoFrameDetector();

public:
    void on_keyframe_start(SrsRtpPacket *pkt);
    srs_error_t detect_frame(uint16_t received, uint16_t &frame_start, uint16_t &frame_end, bool &frame_ready);
    srs_error_t detect_next_frame(uint16_t next_head, uint16_t &next_start, uint16_t &next_end, bool &next_ready);
    void on_keyframe_detached();
    bool is_lost_sn(uint16_t received);
};

// Audio packet cache interface
class ISrsRtcFrameBuilderAudioPacketCache
{
public:
    ISrsRtcFrameBuilderAudioPacketCache();
    virtual ~ISrsRtcFrameBuilderAudioPacketCache();

public:
    virtual srs_error_t process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets) = 0;
    virtual void clear_all() = 0;
};

// Audio packet cache for RTP packet jitter buffer management
class SrsRtcFrameBuilderAudioPacketCache : public ISrsRtcFrameBuilderAudioPacketCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Audio jitter buffer, map sequence number to packet
    std::map<uint16_t, SrsRtpPacket *>
        audio_buffer_;
    // Last processed sequence number
    uint16_t last_audio_seq_num_;
    // Last time we processed the jitter buffer
    srs_utime_t last_audio_process_time_;
    // Whether the cache has been initialized
    bool initialized_;
    // Timeout for waiting out-of-order packets (in microseconds)
    srs_utime_t timeout_;

public:
    SrsRtcFrameBuilderAudioPacketCache();
    virtual ~SrsRtcFrameBuilderAudioPacketCache();

public:
    // Set timeout for waiting out-of-order packets (in microseconds)
    void set_timeout(srs_utime_t timeout);
    // Process audio packet through jitter buffer
    // Returns packets ready for transcoding in order
    srs_error_t process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets);
    // Clear all cached packets
    void clear_all();
};

// Collect and build WebRTC RTP packets to AV frames.
class SrsRtcFrameBuilder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFrameTarget *frame_target_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_first_audio_;
    ISrsAudioTranscoder *audio_transcoder_;
    SrsVideoCodecId video_codec_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcFrameBuilderAudioPacketCache *audio_cache_;
    ISrsRtcFrameBuilderVideoPacketCache *video_cache_;
    ISrsRtcFrameBuilderVideoFrameDetector *frame_detector_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The state for timestamp sync state. -1 for init. 0 not sync. 1 sync.
    int sync_state_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // For OBS WHIP, send (VPS/)SPS/PPS in dedicated RTP packet.
    SrsRtpPacket *obs_whip_vps_;
    SrsRtpPacket *obs_whip_sps_;
    SrsRtpPacket *obs_whip_pps_;

public:
    SrsRtcFrameBuilder(ISrsAppFactory *factory, ISrsFrameTarget *target);
    virtual ~SrsRtcFrameBuilder();

public:
    srs_error_t initialize(ISrsRequest *r, SrsAudioCodecId audio_codec, SrsVideoCodecId video_codec);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t packet_audio(SrsRtpPacket *pkt);
    srs_error_t transcode_audio(SrsRtpPacket *pkt);
    void packet_aac(SrsRtmpCommonMessage *audio, char *data, int len, uint32_t pts, bool is_header);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t packet_video(SrsRtpPacket *pkt);
    srs_error_t packet_video_key_frame(SrsRtpPacket *pkt);
    srs_error_t packet_sequence_header_avc(SrsRtpPacket *pkt);
    srs_error_t do_packet_sequence_header_avc(SrsRtpPacket *pkt, SrsNaluSample *sps, SrsNaluSample *pps);
    srs_error_t packet_sequence_header_hevc(SrsRtpPacket *pkt);
    srs_error_t do_packet_sequence_header_hevc(SrsRtpPacket *pkt, SrsNaluSample *vps, SrsNaluSample *sps, SrsNaluSample *pps);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t packet_video_rtmp(const uint16_t start, const uint16_t end);
    int calculate_packet_payload_size(SrsRtpPacket *pkt);
    void write_packet_payload_to_buffer(SrsRtpPacket *pkt, SrsBuffer &payload, int &nalu_len);
};

#endif

// TODO: FIXME: Rename it.
class SrsCodecPayload
{
public:
    std::string type_;
    uint8_t pt_;
    // for publish, equals to PT of itself;
    // for subscribe, is the PT of publisher;
    uint8_t pt_of_publisher_;
    std::string name_;
    int sample_;

    std::vector<std::string> rtcp_fbs_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The cached codec ID, corresponding to name_.
    // For video, you can convert it to type SrsVideoCodecId
    // For audio, you can convert it to type SrsAudioCodecId
    // Note: Set up to -1, which means not initialized/cached yet
    // Note: Won't copy codec_, it will be recalculated when codec(bool) is called
    int8_t codec_;

public:
    SrsCodecPayload();
    SrsCodecPayload(uint8_t pt, std::string encode_name, int sample);
    virtual ~SrsCodecPayload();

public:
    // Get codec ID with context information about whether it's video or audio
    // Returns the numeric codec ID, with caching for performance
    int8_t codec(bool video);
    virtual SrsCodecPayload *copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
};

// TODO: FIXME: Rename it.
class SrsVideoPayload : public SrsCodecPayload
{
public:
    H264SpecificParam h264_param_;
    H265SpecificParam h265_param_;

public:
    SrsVideoPayload();
    SrsVideoPayload(uint8_t pt, std::string encode_name, int sample);
    virtual ~SrsVideoPayload();

public:
    virtual SrsVideoPayload *copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
    virtual SrsMediaPayloadType generate_media_payload_type_h265();

public:
    srs_error_t set_h264_param_desc(std::string fmtp);
    srs_error_t set_h265_param_desc(std::string fmtp);
};

// TODO: FIXME: Rename it.
class SrsAudioPayload : public SrsCodecPayload
{
    struct SrsOpusParameter {
        int minptime_;
        bool use_inband_fec_;
        bool stereo_;
        bool usedtx_;

        SrsOpusParameter()
        {
            minptime_ = 0;
            use_inband_fec_ = false;
            stereo_ = false;
            usedtx_ = false;
        }
    };

public:
    int channel_;
    SrsOpusParameter opus_param_;
    // AAC configuration hex string for SDP fmtp line
    std::string aac_config_hex_;

public:
    SrsAudioPayload();
    SrsAudioPayload(uint8_t pt, std::string encode_name, int sample, int channel);
    virtual ~SrsAudioPayload();

public:
    virtual SrsAudioPayload *copy();
    virtual SrsMediaPayloadType generate_media_payload_type();

public:
    srs_error_t set_opus_param_desc(std::string fmtp);
};

// TODO: FIXME: Rename it.
class SrsRedPayload : public SrsCodecPayload
{
public:
    int channel_;

public:
    SrsRedPayload();
    SrsRedPayload(uint8_t pt, std::string encode_name, int sample, int channel);
    virtual ~SrsRedPayload();

public:
    virtual SrsRedPayload *copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
};

class SrsRtxPayloadDes : public SrsCodecPayload
{
public:
    uint8_t apt_;

public:
    SrsRtxPayloadDes();
    SrsRtxPayloadDes(uint8_t pt, uint8_t apt);
    virtual ~SrsRtxPayloadDes();

public:
    virtual SrsRtxPayloadDes *copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
};

class SrsRtcTrackDescription
{
public:
    // type: audio, video
    std::string type_;
    // track_id
    std::string id_;
    // ssrc is the primary ssrc for this track,
    // if sdp has ssrc-group, it is the first ssrc of the ssrc-group
    uint32_t ssrc_;
    // rtx ssrc is the second ssrc of "FEC" src-group,
    // if no rtx ssrc, rtx_ssrc_ = 0.
    uint32_t fec_ssrc_;
    // rtx ssrc is the second ssrc of "FID" src-group,
    // if no rtx ssrc, rtx_ssrc_ = 0.
    uint32_t rtx_ssrc_;
    // key: rtp header extension id, value: rtp header extension uri.
    std::map<int, std::string> extmaps_;
    // Whether this track active. default: active.
    bool is_active_;
    // direction
    std::string direction_;
    // mid is used in BOUNDLE
    std::string mid_;
    // msid_: track stream id
    std::string msid_;

    // meida payload, such as opus, h264.
    SrsCodecPayload *media_;
    SrsCodecPayload *red_;
    SrsCodecPayload *rtx_;
    SrsCodecPayload *ulpfec_;

public:
    SrsRtcTrackDescription();
    virtual ~SrsRtcTrackDescription();

public:
    // whether or not the track has ssrc.
    // for example:
    //    we need check track has the ssrc in the ssrc_group, then add ssrc_group to the track,
    bool has_ssrc(uint32_t ssrc);

public:
    void add_rtp_extension_desc(int id, std::string uri);
    void del_rtp_extension_desc(std::string uri);
    void set_direction(std::string direction);
    void set_codec_payload(SrsCodecPayload *payload);
    // auxiliary paylod include red, rtx, ulpfec.
    void create_auxiliary_payload(const std::vector<SrsMediaPayloadType> payload_types);
    void set_rtx_ssrc(uint32_t ssrc);
    void set_fec_ssrc(uint32_t ssrc);
    void set_mid(std::string mid);
    int get_rtp_extension_id(std::string uri);

public:
    SrsRtcTrackDescription *copy();
};

class SrsRtcSourceDescription
{
public:
    // the id for this stream;
    std::string id_;

    SrsRtcTrackDescription *audio_track_desc_;
    std::vector<SrsRtcTrackDescription *> video_track_descs_;

public:
    SrsRtcSourceDescription();
    virtual ~SrsRtcSourceDescription();

public:
    SrsRtcSourceDescription *copy();
    SrsRtcTrackDescription *find_track_description_by_ssrc(uint32_t ssrc);
};

// The RTC packet receiver interface.
class ISrsRtcPacketReceiver
{
public:
    ISrsRtcPacketReceiver();
    virtual ~ISrsRtcPacketReceiver();

public:
    virtual srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp) = 0;
    virtual srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc) = 0;
    virtual void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks) = 0;
    virtual srs_error_t send_rtcp(char *data, int nb_data) = 0;
    virtual srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber) = 0;
};

// The RTC receive track.
class SrsRtcRecvTrack
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsRtcTrackDescription *track_desc_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsRtcPacketReceiver *receiver_;
    SrsRtpRingBuffer *rtp_queue_;
    SrsRtpNackForReceiver *nack_receiver_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // By config, whether no copy.
    bool nack_no_copy_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // Latest sender report ntp and rtp time.
    SrsNtp last_sender_report_ntp_;
    int64_t last_sender_report_rtp_time_;

    // Prev sender report ntp and rtp time.
    SrsNtp last_sender_report_ntp1_;
    int64_t last_sender_report_rtp_time1_;

    double rate_;
    uint64_t last_sender_report_sys_time_;

public:
    SrsRtcRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *stream_descs, bool is_audio, bool init_rate_from_sdp);
    virtual ~SrsRtcRecvTrack();

public:
    // SrsRtcSendTrack::set_nack_no_copy
    void set_nack_no_copy(bool v) { nack_no_copy_ = v; }
    bool has_ssrc(uint32_t ssrc);
    uint32_t get_ssrc();
    void update_rtt(int rtt);
    void update_send_report_time(const SrsNtp &ntp, uint32_t rtp_time);
    int64_t cal_avsync_time(uint32_t rtp_time);
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();

public:
    // Note that we can set the pkt to NULL to avoid copy, for example, if the NACK cache the pkt and
    // set to NULL, nack nerver copy it but set the pkt to NULL.
    srs_error_t on_nack(SrsRtpPacket **ppkt);

public:
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt) = 0;
    virtual srs_error_t check_send_nacks() = 0;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t do_check_send_nacks(uint32_t &timeout_nacks);
};

class SrsRtcAudioRecvTrack : public SrsRtcRecvTrack, public ISrsRtpPacketDecodeHandler
{
public:
    SrsRtcAudioRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *track_desc, bool init_rate_from_sdp);
    virtual ~SrsRtcAudioRecvTrack();

public:
    virtual void on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt);

public:
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt);
    virtual srs_error_t check_send_nacks();
};

class SrsRtcVideoRecvTrack : public SrsRtcRecvTrack, public ISrsRtpPacketDecodeHandler
{
public:
    SrsRtcVideoRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *stream_descs, bool init_rate_from_sdp);
    virtual ~SrsRtcVideoRecvTrack();

public:
    virtual void on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt);

public:
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt);
    virtual srs_error_t check_send_nacks();
};

// RTC jitter for TS or sequence number, only reset the base, and keep in original order.
template <typename T, typename ST>
class SrsRtcJitter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ST threshold_;
    typedef ST (*PFN)(const T &, const T &);
    PFN distance_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The value about packet.
    T pkt_base_;
    T pkt_last_;
    // The value after corrected.
    T correct_base_;
    T correct_last_;
    // The base timestamp by config, start from it.
    T base_;
    // Whether initialized. Note that we should not use correct_base_(0) as init state, because it might flip back.
    bool init_;

public:
    SrsRtcJitter(T base, ST threshold, PFN distance)
    {
        threshold_ = threshold;
        distance_ = distance;
        base_ = base;

        pkt_base_ = pkt_last_ = 0;
        correct_last_ = correct_base_ = 0;
        init_ = false;
    }
    virtual ~SrsRtcJitter()
    {
    }

public:
    T correct(T value)
    {
        if (!init_) {
            init_ = true;
            correct_base_ = base_;
            pkt_base_ = value;
            srs_trace("RTC: Jitter init base=%u, value=%u", base_, value);
        }

        if (true) {
            ST distance = distance_(value, pkt_last_);
            if (distance > threshold_ || distance < -1 * threshold_) {
                srs_trace("RTC: Jitter rebase value=%u, last=%u, distance=%d, pkt-base=%u/%u, correct-base=%u/%u",
                          value, pkt_last_, distance, pkt_base_, value, correct_base_, correct_last_);
                pkt_base_ = value;
                correct_base_ = correct_last_;
            }
        }

        pkt_last_ = value;
        correct_last_ = correct_base_ + value - pkt_base_;

        return correct_last_;
    }
};

// For RTC timestamp jitter.
class SrsRtcTsJitter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtcJitter<uint32_t, int32_t> *jitter_;

public:
    SrsRtcTsJitter(uint32_t base);
    virtual ~SrsRtcTsJitter();

public:
    uint32_t correct(uint32_t value);
};

// For RTC sequence jitter.
class SrsRtcSeqJitter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtcJitter<uint16_t, int16_t> *jitter_;

public:
    SrsRtcSeqJitter(uint16_t base);
    virtual ~SrsRtcSeqJitter();

public:
    uint16_t correct(uint16_t value);
};

// The RTC packet sender interface.
class ISrsRtcPacketSender
{
public:
    ISrsRtcPacketSender();
    virtual ~ISrsRtcPacketSender();

public:
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt) = 0;
};

class SrsRtcSendTrack
{
public:
    // send track description
    SrsRtcTrackDescription *track_desc_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The owner connection for this track.
    ISrsRtcPacketSender *sender_;
    // NACK ARQ ring buffer.
    SrsRtpRingBuffer *rtp_queue_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The jitter to correct ts and sequence number.
    SrsRtcTsJitter *jitter_ts_;
    SrsRtcSeqJitter *jitter_seq_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // By config, whether no copy.
    bool nack_no_copy_;
    // The pithy print for special stage.
    SrsErrorPithyPrint *nack_epp;

public:
    SrsRtcSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio);
    virtual ~SrsRtcSendTrack();

public:
    // SrsRtcSendTrack::set_nack_no_copy
    void set_nack_no_copy(bool v) { nack_no_copy_ = v; }
    bool has_ssrc(uint32_t ssrc);
    SrsRtpPacket *fetch_rtp_packet(uint16_t seq);
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    void rebuild_packet(SrsRtpPacket *pkt);

public:
    // Note that we can set the pkt to NULL to avoid copy, for example, if the NACK cache the pkt and
    // set to NULL, nack nerver copy it but set the pkt to NULL.
    srs_error_t on_nack(SrsRtpPacket **ppkt);

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt) = 0;
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt) = 0;
    virtual srs_error_t on_recv_nack(const std::vector<uint16_t> &lost_seqs);
};

class SrsRtcAudioSendTrack : public SrsRtcSendTrack
{
public:
    SrsRtcAudioSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc);
    virtual ~SrsRtcAudioSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt);
};

class SrsRtcVideoSendTrack : public SrsRtcSendTrack
{
public:
    SrsRtcVideoSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc);
    virtual ~SrsRtcVideoSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt);
};

class SrsRtcSSRCGenerator
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static SrsRtcSSRCGenerator *instance_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t ssrc_num_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtcSSRCGenerator();
    virtual ~SrsRtcSSRCGenerator();

public:
    static SrsRtcSSRCGenerator *instance();
    uint32_t generate_ssrc();
};

#endif
