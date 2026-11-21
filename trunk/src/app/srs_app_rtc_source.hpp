//
// Copyright (c) 2013-2026 The SRS Authors
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
class SrsRtcpSR;
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
class ISrsStatistic;
class ISrsAppConfig;
class ISrsRtcSSRCGenerator;
class ISrsClock;
class ISrsRand;

// Firefox defaults to 109, Chrome to 111.
const int kAudioPayloadType = 111;
// Firefox defaults to 126, Chrome to 102.
const int kVideoPayloadType = 102;
// Chrome defaults to 49 for HEVC.
const int KVideoPayloadTypeHevc = 49;
// Chrome defaults to 45 for AV1.
const int KVideoPayloadTypeAv1 = 45;

// In packets.
const int AUDIO_JITTER_BUFFER_SIZE = 100;
const int SLIDING_WINDOW_SIZE = 10;
const int MAX_AUDIO_WAIT_MS = 100;

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

class ISrsRtcSourceChangeCallback
{
public:
    ISrsRtcSourceChangeCallback();
    virtual ~ISrsRtcSourceChangeCallback();

public:
    virtual void on_stream_change(SrsRtcSourceDescription *desc) = 0;
};

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

class SrsRtcConsumer : public ISrsRtcConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The source holds this consumer, so this is a raw pointer to the source.
    ISrsRtcSourceForConsumer *source_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsRtpPacket *> queue_;
    bool should_update_source_id_;
    srs_cond_t mw_wait_;
    bool mw_waiting_;
    int mw_min_msgs_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcSourceChangeCallback *handler_;

public:
    SrsRtcConsumer(ISrsRtcSourceForConsumer *s);
    virtual ~SrsRtcConsumer();

public:
    virtual void update_source_id();
    // The sender drops packets, not this queue.
    srs_error_t enqueue(SrsRtpPacket *pkt);
    // One packet at a time, since the RTC queue holds few packets.
    virtual srs_error_t dump_packet(SrsRtpPacket **ppkt);
    virtual void wait(int nb_msgs);

public:
    void set_handler(ISrsRtcSourceChangeCallback *h) { handler_ = h; }
    void on_stream_change(SrsRtcSourceDescription *desc);
};

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

class SrsRtcSourceManager : public ISrsRtcSourceManager, public ISrsHourGlassHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsRtcSource> > pool_;
    ISrsHourGlass *timer_;

public:
    SrsRtcSourceManager();
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsRtcSourceManager();

public:
    virtual srs_error_t initialize();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);

public:
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps);

public:
    virtual SrsSharedPtr<SrsRtcSource> fetch(ISrsRequest *r);
};

extern SrsRtcSourceManager *_srs_rtc_sources;

class ISrsRtcSourceEventHandler
{
public:
    ISrsRtcSourceEventHandler();
    virtual ~ISrsRtcSourceEventHandler();

public:
    // A sync API.
    virtual void on_unpublish() = 0;
    // No player plays this stream. A sync API.
    virtual void on_consumers_finished() = 0;
};

// One stream, shared by its SrsRtcPublishStream and SrsRtcPlayStream.
class SrsRtcSource : public ISrsRtpTarget, public ISrsFastTimerHandler, public ISrsRtcSourceForConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcBridge *rtc_bridge_;
    ISrsCircuitBreaker *circuit_breaker_;
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsSharedTimer *shared_timer_;
    ISrsRtcSSRCGenerator *ssrc_generator_;
    ISrsClock *clk_;
    ISrsRand *rand_;
    // The context id of the publisher.
    SrsContextId source_id_;
    SrsContextId pre_source_id_;
    ISrsRequest *req_;
    ISrsRtcPublishStream *publish_stream_;
    SrsRtcSourceDescription *stream_desc_;
    // The video codec the bridge detected in the stream it is publishing, or
    // SrsVideoCodecIdReserved if there is no bridge or it has not parsed a sequence header yet.
    SrsVideoCodecId bridge_video_codec_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsRtcConsumer *>
        consumers_;
    // Set when SDP is done.
    bool is_created_;
    // Set when DTLS is done.
    bool is_delivering_packets_;
    std::vector<ISrsRtcSourceEventHandler *> event_handlers_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t pli_for_rtmp_;
    srs_utime_t pli_elapsed_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // When the source last had neither publishers nor players.
    srs_utime_t stream_die_at_;

public:
    SrsRtcSource();
    virtual ~SrsRtcSource();
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    virtual srs_error_t initialize(ISrsRequest *r);

public:
    virtual bool stream_is_dead();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void init_for_play_before_publishing();

public:
    virtual void update_auth(ISrsRequest *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_source_changed();

public:
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();

public:
    virtual void set_bridge(ISrsRtcBridge *bridge);

public:
    virtual srs_error_t create_consumer(ISrsRtcConsumer *&consumer);
    virtual srs_error_t consumer_dumps(ISrsRtcConsumer *consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(ISrsRtcConsumer *consumer);
    virtual bool can_publish();
    // Called when SDP is done, before DTLS.
    virtual void set_stream_created();
    virtual srs_error_t on_publish();
    virtual void on_unpublish();

public:
    virtual void rtc_source_subscribe(ISrsRtcSourceEventHandler *h);
    virtual void rtc_source_unsubscribe(ISrsRtcSourceEventHandler *h);

public:
    // The players use the publisher for requests such as PLI.
    virtual ISrsRtcPublishStream *publish_stream();
    virtual void set_publish_stream(ISrsRtcPublishStream *v);
    // The caller still owns and frees pkt.
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual bool has_stream_desc();
    virtual SrsRtcSourceDescription *stream_desc();
    virtual void set_stream_desc(SrsRtcSourceDescription *stream_desc);
    virtual std::vector<SrsRtcTrackDescription *> get_track_desc(std::string type, std::string media_type);
    virtual void set_bridge_video_codec(SrsVideoCodecId codec);
    // SrsVideoCodecIdReserved while the codec is unknown, for example when nothing is
    // published yet, so the player may choose any codec.
    virtual SrsVideoCodecId publish_video_codec();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

#ifdef SRS_FFMPEG_FIT

class SrsRtcRtpBuilder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsRtpTarget *rtp_target_;
    ISrsAppConfig *config_;
    SrsRtmpFormat *format_;
    SrsMetaCache *meta_;
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
    bool audio_initialized_;
    bool video_initialized_;

public:
    SrsRtcRtpBuilder(ISrsAppFactory *factory, ISrsRtpTarget *target, SrsSharedPtr<SrsRtcSource> source);
    virtual ~SrsRtcRtpBuilder();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
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
    // Returns the lost sn, -1 for a complete frame (and sets end_sn), or -2 when the frame
    // has more packets than the cache.
    int32_t find_next_lost_sn(uint16_t current_sn, uint16_t header_sn, uint16_t &end_sn);
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

class ISrsRtcFrameBuilderAudioPacketCache
{
public:
    ISrsRtcFrameBuilderAudioPacketCache();
    virtual ~ISrsRtcFrameBuilderAudioPacketCache();

public:
    virtual srs_error_t process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets) = 0;
    virtual void clear_all() = 0;
};

// A jitter buffer for the audio RTP packets.
class SrsRtcFrameBuilderAudioPacketCache : public ISrsRtcFrameBuilderAudioPacketCache
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<uint16_t, SrsRtpPacket *>
        audio_buffer_;
    uint16_t last_audio_seq_num_;
    srs_utime_t last_audio_process_time_;
    bool initialized_;
    // How long to wait for out-of-order packets.
    srs_utime_t timeout_;

public:
    SrsRtcFrameBuilderAudioPacketCache();
    virtual ~SrsRtcFrameBuilderAudioPacketCache();

public:
    void set_timeout(srs_utime_t timeout);
    // The ready packets are in sequence order, for the transcoder.
    srs_error_t process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets);
    void clear_all();
};

class SrsRtcFrameBuilder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsAppConfig *config_;

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
    // The timestamp sync state: -1 at start, 0 not synced, 2 synced.
    int sync_state_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // OBS WHIP sends the VPS, SPS and PPS each in its own RTP packet.
    SrsRtpPacket *obs_whip_vps_;
    SrsRtpPacket *obs_whip_sps_;
    SrsRtpPacket *obs_whip_pps_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // WebRTC publishers may repeat unchanged parameter sets with every keyframe.
    std::string last_video_sequence_header_;

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
    srs_error_t packet_video_frame(const uint16_t start, const uint16_t end);
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
    // For publish, the same as pt_; for play, the PT of the publisher.
    uint8_t pt_of_publisher_;
    std::string name_;
    int sample_;

    std::vector<std::string> rtcp_fbs_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // An SrsVideoCodecId or SrsAudioCodecId of name_, or -1 until codec(bool) caches it.
    int8_t codec_;

public:
    SrsCodecPayload();
    SrsCodecPayload(uint8_t pt, std::string encode_name, int sample);
    virtual ~SrsCodecPayload();

public:
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
    // For the SDP fmtp line.
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

// Parse the associated payload type from the fmtp of an rtx payload, for example "apt=106", RFC 4588 section 8.1.
// Return 0 when the fmtp names no apt.
uint8_t srs_rtx_parse_apt(const std::string &fmtp);

class SrsRtxPayloadDes : public SrsCodecPayload
{
public:
    uint8_t apt_;

public:
    SrsRtxPayloadDes();
    SrsRtxPayloadDes(uint8_t pt, uint8_t apt, int sample);
    virtual ~SrsRtxPayloadDes();

public:
    virtual SrsRtxPayloadDes *copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
};

class SrsRtcTrackDescription
{
public:
    // Audio or video.
    std::string type_;
    std::string id_;
    // The primary SSRC; with an ssrc-group in the SDP, the first SSRC of the group.
    uint32_t ssrc_;
    // The second SSRC of the FEC ssrc-group, or 0.
    uint32_t fec_ssrc_;
    // The second SSRC of the FID ssrc-group, or 0.
    uint32_t rtx_ssrc_;
    // The RTP header extension id to its URI.
    std::map<int, std::string> extmaps_;
    bool is_active_;
    std::string direction_;
    // Used by BUNDLE.
    std::string mid_;
    // The stream id of the track.
    std::string msid_;

    SrsCodecPayload *media_;
    SrsCodecPayload *red_;
    SrsCodecPayload *rtx_;
    SrsCodecPayload *ulpfec_;

public:
    SrsRtcTrackDescription();
    virtual ~SrsRtcTrackDescription();

public:
    bool has_ssrc(uint32_t ssrc);

public:
    void add_rtp_extension_desc(int id, std::string uri);
    void del_rtp_extension_desc(std::string uri);
    void set_direction(std::string direction);
    void set_codec_payload(SrsCodecPayload *payload);
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
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // From the config.
    bool nack_no_copy_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsNtp last_sender_report_ntp_;
    int64_t last_sender_report_rtp_time_;

    // The Sender Report before the last one.
    SrsNtp last_sender_report_ntp1_;
    int64_t last_sender_report_rtp_time1_;

    double rate_;
    uint64_t last_sender_report_sys_time_;

public:
    SrsRtcRecvTrack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *stream_descs, bool is_audio, bool init_rate_from_sdp);
    virtual ~SrsRtcRecvTrack();

public:
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
    SrsRtcTrackDescription *get_track_desc();

public:
    // May take the packet to avoid a copy, and then sets *ppkt to NULL.
    srs_error_t on_nack(SrsRtpPacket **ppkt);

public:
    // Whether the packet counts as a media frame in the statistics, which RTX and FEC packets do not.
    virtual bool is_media_frame(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt) = 0;
    virtual srs_error_t check_send_nacks() = 0;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    bool is_primary_rtp(SrsRtpPacket *pkt);
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
    virtual bool is_media_frame(SrsRtpPacket *pkt);
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
    virtual bool is_media_frame(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtp(SrsSharedPtr<SrsRtcSource> &source, SrsRtpPacket *pkt);
    virtual srs_error_t check_send_nacks();
};

// For a TS or sequence number, it only resets the base on a jump, and keeps the original order.
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
    T pkt_base_;
    T pkt_last_;
    T correct_base_;
    T correct_last_;
    // The corrected values start from it.
    T base_;
    // Not correct_base_ == 0, since the value may wrap back to 0.
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

// The sender info of a send track for its Sender Report, see RFC 3550 section 6.4.1: the RTP time of the last packet
// sent, mapped to the stream's clock, and the packets and payload octets sent.
class SrsRtcSenderInfo
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The RTP time of the last mapped packet, as sent, and its time in ms on the stream's clock, which a WebRTC
    // publisher's Sender Reports map, or a converted stream's RTMP time, or -1 when not mapped.
    uint32_t rtp_time_;
    int64_t avsync_time_;
    uint32_t packets_;
    uint32_t octets_;
    // Whether a mapped packet was sent since the last Sender Report.
    bool pending_;

public:
    SrsRtcSenderInfo();
    virtual ~SrsRtcSenderInfo();

public:
    void on_packet(SrsRtpPacket *pkt);
    // Returns false when no mapped packet was sent since the last Sender Report, so none is due.
    bool fill(SrsRtcpSR *sr);
};

class ISrsRtcPacketSender
{
public:
    ISrsRtcPacketSender();
    virtual ~ISrsRtcPacketSender();

public:
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt) = 0;
    // Send an RTCP packet; the buffer must have room for the SRTP trailer, see kRtpPacketSize.
    virtual srs_error_t send_rtcp(char *data, int nb_data) = 0;
};

class SrsRtcSendTrack
{
public:
    SrsRtcTrackDescription *track_desc_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The owner connection.
    ISrsRtcPacketSender *sender_;
    // For NACK retransmission.
    SrsRtpRingBuffer *rtp_queue_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsRtcTsJitter *jitter_ts_;
    SrsRtcSeqJitter *jitter_seq_;
    SrsRtcSenderInfo *sender_info_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // From the config.
    bool nack_no_copy_;
    // From the config; it keeps the original sequence number and timestamp too.
    bool keep_original_ssrc_;
    SrsErrorPithyPrint *nack_epp;
    // The RTX sequence space of this track, RFC 4588 section 4, independent of the media sequence.
    uint16_t rtx_seq_;
    ISrsRand *rand_;

public:
    SrsRtcSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio);
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsRtcSendTrack();

public:
    void set_nack_no_copy(bool v) { nack_no_copy_ = v; }
    void set_keep_original_ssrc(bool v) { keep_original_ssrc_ = v; }
    bool has_ssrc(uint32_t ssrc);
    SrsRtpPacket *fetch_rtp_packet(uint16_t seq);
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    void rebuild_packet(SrsRtpPacket *pkt);

public:
    // The Sender Report maps the RTP times of the track to the stream's clock, the publisher's or the RTMP times
    // of a converted stream, so a player syncs the tracks by it.
    srs_error_t send_rtcp_sr();

public:
    // May take the packet to avoid a copy, and then sets *ppkt to NULL.
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

class ISrsRtcSSRCGenerator
{
public:
    ISrsRtcSSRCGenerator();
    virtual ~ISrsRtcSSRCGenerator();

public:
    virtual uint32_t generate_ssrc() = 0;
};

class SrsRtcSSRCGenerator : public ISrsRtcSSRCGenerator
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

// The global SSRC generator, the same object as SrsRtcSSRCGenerator::instance(), so every
// user draws from one sequence.
extern SrsRtcSSRCGenerator *_srs_rtc_ssrc_generator;

class ISrsRtcFormat
{
public:
    ISrsRtcFormat();
    virtual ~ISrsRtcFormat();

public:
    virtual srs_error_t initialize(ISrsRequest *req) = 0;
    virtual srs_error_t on_rtp_packet(SrsRtcRecvTrack *track, SrsRtpPacket *pkt, bool is_audio) = 0;
};

// Reports the codec information of an RTC stream to the statistics.
class SrsRtcFormat : public ISrsRtcFormat
{
public:
    SrsRtcFormat();
    virtual ~SrsRtcFormat();

public:
    virtual srs_error_t initialize(ISrsRequest *req);
    virtual srs_error_t on_rtp_packet(SrsRtcRecvTrack *track, SrsRtpPacket *pkt, bool is_audio);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_video_codec(SrsCodecPayload *media);
    virtual srs_error_t on_video_sps(SrsVideoCodecId codec_id, SrsRtpPacket *pkt);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsStatistic *stat_;
    bool video_codec_reported_;
    bool audio_codec_reported_;
    // The profile and level of the SDP, reported again with the size of each new SPS.
    int video_profile_;
    int video_level_;
    // The last SPS whose size was reported.
    std::string video_sps_;
};

#endif
