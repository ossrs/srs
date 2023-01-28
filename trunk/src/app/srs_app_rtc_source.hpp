//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_RTC_SOURCE_HPP
#define SRS_APP_RTC_SOURCE_HPP

#include <srs_core.hpp>

#include <vector>
#include <map>
#include <inttypes.h>
#include <vector>
#include <string>
#include <map>

#include <srs_app_rtc_sdp.hpp>
#include <srs_protocol_st.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_protocol_format.hpp>
#include <srs_app_stream_bridge.hpp>

class SrsRequest;
class SrsMetaCache;
class SrsSharedPtrMessage;
class SrsCommonMessage;
class SrsMessageArray;
class SrsRtcSource;
class SrsFrameToRtcBridge;
class SrsAudioTranscoder;
class SrsRtpPacket;
class SrsSample;
class SrsRtcSourceDescription;
class SrsRtcTrackDescription;
class SrsRtcConnection;
class SrsRtpRingBuffer;
class SrsRtpNackForReceiver;
class SrsJsonObject;
class SrsErrorPithyPrint;
class SrsRtcFrameBuilder;
class SrsLiveSource;

// Firefox defaults as 109, Chrome is 111.
const int kAudioPayloadType     = 111;
// Firefox defaults as 126, Chrome is 102.
const int kVideoPayloadType = 102;

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
    virtual void on_stream_change(SrsRtcSourceDescription* desc) = 0;
};

// The RTC stream consumer, consume packets from RTC stream source.
class SrsRtcConsumer
{
private:
    SrsRtcSource* source;
    std::vector<SrsRtpPacket*> queue;
    // when source id changed, notice all consumers
    bool should_update_source_id;
    // The cond wait for mw.
    srs_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;
private:
    // The callback for stream change event.
    ISrsRtcSourceChangeCallback* handler_;
public:
    SrsRtcConsumer(SrsRtcSource* s);
    virtual ~SrsRtcConsumer();
public:
    // When source id changed, notice client to print.
    virtual void update_source_id();
    // Put RTP packet into queue.
    // @note We do not drop packet here, but drop it in sender.
    srs_error_t enqueue(SrsRtpPacket* pkt);
    // For RTC, we only got one packet, because there is not many packets in queue.
    virtual srs_error_t dump_packet(SrsRtpPacket** ppkt);
    // Wait for at-least some messages incoming in queue.
    virtual void wait(int nb_msgs);
public:
    void set_handler(ISrsRtcSourceChangeCallback* h) { handler_ = h; } // SrsRtcConsumer::set_handler()
    void on_stream_change(SrsRtcSourceDescription* desc);
};

class SrsRtcSourceManager
{
private:
    srs_mutex_t lock;
    std::map<std::string, SrsRtcSource*> pool;
public:
    SrsRtcSourceManager();
    virtual ~SrsRtcSourceManager();
public:
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param pps the matched source, if success never be NULL.
    virtual srs_error_t fetch_or_create(SrsRequest* r, SrsRtcSource** pps);
public:
    // Get the exists source, NULL when not exists.
    virtual SrsRtcSource* fetch(SrsRequest* r);
};

// Global singleton instance.
extern SrsRtcSourceManager* _srs_rtc_sources;

// A publish stream interface, for source to callback with.
class ISrsRtcPublishStream
{
public:
    ISrsRtcPublishStream();
    virtual ~ISrsRtcPublishStream();
public:
    // Request keyframe(PLI) from publisher, for fresh consumer.
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid) = 0;
    // Get context id.
    virtual const SrsContextId& context_id() = 0;
};

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
class SrsRtcSource : public ISrsFastTimer
{
private:
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_changed() to let all clients know.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    SrsRequest* req;
    ISrsRtcPublishStream* publish_stream_;
    // Steam description for this steam.
    SrsRtcSourceDescription* stream_desc_;
private:
#ifdef SRS_FFMPEG_FIT
    // Collect and build WebRTC RTP packets to AV frames.
    SrsRtcFrameBuilder* frame_builder_;
#endif
    // The Source bridge, bridge stream to other source.
    ISrsStreamBridge* bridge_;
private:
    // To delivery stream to clients.
    std::vector<SrsRtcConsumer*> consumers;
    // Whether stream is created, that is, SDP is done.
    bool is_created_;
    // Whether stream is delivering data, that is, DTLS is done.
    bool is_delivering_packets_;
    // Notify stream event to event handler
    std::vector<ISrsRtcSourceEventHandler*> event_handlers_;
private:
    // The PLI for RTC2RTMP.
    srs_utime_t pli_for_rtmp_;
    srs_utime_t pli_elapsed_;
public:
    SrsRtcSource();
    virtual ~SrsRtcSource();
public:
    virtual srs_error_t initialize(SrsRequest* r);
private:
    void init_for_play_before_publishing();
public:
    // Update the authentication information in request.
    virtual void update_auth(SrsRequest* r);
private:
    // The stream source changed.
    virtual srs_error_t on_source_changed();
public:
    // Get current source id.
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
public:
    void set_bridge(ISrsStreamBridge* bridge);
public:
    // Create consumer
    // @param consumer, output the create consumer.
    virtual srs_error_t create_consumer(SrsRtcConsumer*& consumer);
    // Dumps packets in cache to consumer.
    // @param ds, whether dumps the sequence header.
    // @param dm, whether dumps the metadata.
    // @param dg, whether dumps the gop cache.
    virtual srs_error_t consumer_dumps(SrsRtcConsumer* consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(SrsRtcConsumer* consumer);
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
    void subscribe(ISrsRtcSourceEventHandler* h);
    void unsubscribe(ISrsRtcSourceEventHandler* h);
public:
    // Get and set the publisher, passed to consumer to process requests such as PLI.
    ISrsRtcPublishStream* publish_stream();
    void set_publish_stream(ISrsRtcPublishStream* v);
    // Consume the shared RTP packet, user must free it.
    srs_error_t on_rtp(SrsRtpPacket* pkt);
    // Set and get stream description for souce
    bool has_stream_desc();
    void set_stream_desc(SrsRtcSourceDescription* stream_desc);
    std::vector<SrsRtcTrackDescription*> get_track_desc(std::string type, std::string media_type);
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

#ifdef SRS_FFMPEG_FIT

// Convert AV frame to RTC RTP packets.
class SrsRtcRtpBuilder
{
private:
    SrsRequest* req;
    SrsFrameToRtcBridge* bridge_;
    // The format, codec information.
    SrsRtmpFormat* format;
    // The metadata cache.
    SrsMetaCache* meta;
private:
    SrsAudioCodecId latest_codec_;
    SrsAudioTranscoder* codec_;
    bool keep_bframe;
    bool merge_nalus;
    uint16_t audio_sequence;
    uint16_t video_sequence;
private:
    uint32_t audio_ssrc_;
    uint32_t video_ssrc_;
    uint8_t audio_payload_type_;
    uint8_t video_payload_type_;
public:
    SrsRtcRtpBuilder(SrsFrameToRtcBridge* bridge, uint32_t assrc, uint8_t apt, uint32_t vssrc, uint8_t vpt);
    virtual ~SrsRtcRtpBuilder();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsSharedPtrMessage* frame);
private:
    virtual srs_error_t on_audio(SrsSharedPtrMessage* msg);
private:
    srs_error_t init_codec(SrsAudioCodecId codec);
    srs_error_t transcode(SrsAudioFrame* audio);
    srs_error_t package_opus(SrsAudioFrame* audio, SrsRtpPacket* pkt);
private:
    virtual srs_error_t on_video(SrsSharedPtrMessage* msg);
private:
    srs_error_t filter(SrsSharedPtrMessage* msg, SrsFormat* format, bool& has_idr, std::vector<SrsSample*>& samples);
    srs_error_t package_stap_a(SrsSharedPtrMessage* msg, SrsRtpPacket* pkt);
    srs_error_t package_nalus(SrsSharedPtrMessage* msg, const std::vector<SrsSample*>& samples, std::vector<SrsRtpPacket*>& pkts);
    srs_error_t package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, std::vector<SrsRtpPacket*>& pkts);
    srs_error_t package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, std::vector<SrsRtpPacket*>& pkts);
    srs_error_t consume_packets(std::vector<SrsRtpPacket*>& pkts);
};

// Collect and build WebRTC RTP packets to AV frames.
class SrsRtcFrameBuilder
{
private:
    ISrsStreamBridge* bridge_;
private:
    bool is_first_audio_;
    SrsAudioTranscoder *codec_;
private:
    const static uint16_t s_cache_size = 512;
    //TODO:use SrsRtpRingBuffer
    //TODO:jitter buffer class
    struct RtcPacketCache {
        bool in_use;
        uint16_t sn;
        uint32_t ts;
        uint32_t rtp_ts;
        SrsRtpPacket* pkt;
    };
    RtcPacketCache cache_video_pkts_[s_cache_size];
    uint16_t header_sn_;
    uint16_t lost_sn_;
    int64_t rtp_key_frame_ts_;
public:
    SrsRtcFrameBuilder(ISrsStreamBridge* bridge);
    virtual ~SrsRtcFrameBuilder();
public:
    srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
private:
    srs_error_t transcode_audio(SrsRtpPacket *pkt);
    void packet_aac(SrsCommonMessage* audio, char* data, int len, uint32_t pts, bool is_header);
private:
    srs_error_t packet_video(SrsRtpPacket* pkt);
    srs_error_t packet_video_key_frame(SrsRtpPacket* pkt);
    inline uint16_t cache_index(uint16_t current_sn) {
        return current_sn % s_cache_size;
    }
    int32_t find_next_lost_sn(uint16_t current_sn, uint16_t& end_sn);
    bool check_frame_complete(const uint16_t start, const uint16_t end);
    srs_error_t packet_video_rtmp(const uint16_t start, const uint16_t end);
    void clear_cached_video();
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
public:
    SrsCodecPayload();
    SrsCodecPayload(uint8_t pt, std::string encode_name, int sample);
    virtual ~SrsCodecPayload();
public:
    virtual SrsCodecPayload* copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
};

// TODO: FIXME: Rename it.
class SrsVideoPayload : public SrsCodecPayload
{
public:
    H264SpecificParam h264_param_;

public:
    SrsVideoPayload();
    SrsVideoPayload(uint8_t pt, std::string encode_name, int sample);
    virtual ~SrsVideoPayload();
public:
    virtual SrsVideoPayload* copy();
    virtual SrsMediaPayloadType generate_media_payload_type();
public:
    srs_error_t set_h264_param_desc(std::string fmtp);
};

// TODO: FIXME: Rename it.
class SrsAudioPayload : public SrsCodecPayload
{
    struct SrsOpusParameter
    {
        int minptime;
        bool use_inband_fec;
        bool usedtx;

        SrsOpusParameter() {
            minptime = 0;
            use_inband_fec = false;
            usedtx = false;
        }
    };

public:
    int channel_;
    SrsOpusParameter opus_param_;
public:
    SrsAudioPayload();
    SrsAudioPayload(uint8_t pt, std::string encode_name, int sample, int channel);
    virtual ~SrsAudioPayload();
public:
    virtual SrsAudioPayload* copy();
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
    virtual SrsRedPayload* copy();
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
    virtual SrsRtxPayloadDes* copy();
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
    SrsCodecPayload* media_;
    SrsCodecPayload* red_;
    SrsCodecPayload* rtx_;
    SrsCodecPayload* ulpfec_;
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
    void set_codec_payload(SrsCodecPayload* payload);
    // auxiliary paylod include red, rtx, ulpfec.
    void create_auxiliary_payload(const std::vector<SrsMediaPayloadType> payload_types);
    void set_rtx_ssrc(uint32_t ssrc);
    void set_fec_ssrc(uint32_t ssrc);
    void set_mid(std::string mid);
    int get_rtp_extension_id(std::string uri);
public:
    SrsRtcTrackDescription* copy();
};

class SrsRtcSourceDescription
{
public:
    // the id for this stream;
    std::string id_;

    SrsRtcTrackDescription* audio_track_desc_;
    std::vector<SrsRtcTrackDescription*> video_track_descs_;
public:
    SrsRtcSourceDescription();
    virtual ~SrsRtcSourceDescription();

public:
    SrsRtcSourceDescription* copy();
    SrsRtcTrackDescription* find_track_description_by_ssrc(uint32_t ssrc);
};

class SrsRtcRecvTrack
{
protected:
    SrsRtcTrackDescription* track_desc_;
protected:
    SrsRtcConnection* session_;
    SrsRtpRingBuffer* rtp_queue_;
    SrsRtpNackForReceiver* nack_receiver_;
private:
    // By config, whether no copy.
    bool nack_no_copy_;
protected:
    // Latest sender report ntp and rtp time.
    SrsNtp last_sender_report_ntp_;
    int64_t last_sender_report_rtp_time_;

    // Prev sender report ntp and rtp time.
    SrsNtp last_sender_report_ntp1_;
    int64_t last_sender_report_rtp_time1_;

    double rate_;
    uint64_t last_sender_report_sys_time_;
public:
    SrsRtcRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* stream_descs, bool is_audio);
    virtual ~SrsRtcRecvTrack();
public:
    // SrsRtcSendTrack::set_nack_no_copy
    void set_nack_no_copy(bool v) { nack_no_copy_ = v; }
    bool has_ssrc(uint32_t ssrc);
    uint32_t get_ssrc();
    void update_rtt(int rtt);
    void update_send_report_time(const SrsNtp& ntp, uint32_t rtp_time);
    int64_t cal_avsync_time(uint32_t rtp_time);
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();
public:
    // Note that we can set the pkt to NULL to avoid copy, for example, if the NACK cache the pkt and
    // set to NULL, nack nerver copy it but set the pkt to NULL.
    srs_error_t on_nack(SrsRtpPacket** ppkt);
public:
    virtual srs_error_t on_rtp(SrsRtcSource* source, SrsRtpPacket* pkt) = 0;
    virtual srs_error_t check_send_nacks() = 0;
protected:
    virtual srs_error_t do_check_send_nacks(uint32_t& timeout_nacks);
};

class SrsRtcAudioRecvTrack : public SrsRtcRecvTrack, public ISrsRtspPacketDecodeHandler
{
public:
    SrsRtcAudioRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc);
    virtual ~SrsRtcAudioRecvTrack();
public:
    virtual void on_before_decode_payload(SrsRtpPacket* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtspPacketPayloadType* ppt);
public:
    virtual srs_error_t on_rtp(SrsRtcSource* source, SrsRtpPacket* pkt);
    virtual srs_error_t check_send_nacks();
};

class SrsRtcVideoRecvTrack : public SrsRtcRecvTrack, public ISrsRtspPacketDecodeHandler
{
public:
    SrsRtcVideoRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* stream_descs);
    virtual ~SrsRtcVideoRecvTrack();
public:
    virtual void on_before_decode_payload(SrsRtpPacket* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtspPacketPayloadType* ppt);
public:
    virtual srs_error_t on_rtp(SrsRtcSource* source, SrsRtpPacket* pkt);
    virtual srs_error_t check_send_nacks();
};

// RTC jitter for TS or sequence number, only reset the base, and keep in original order.
template<typename T, typename ST>
class SrsRtcJitter
{
private:
    ST threshold_;
    typedef ST (*PFN) (const T&, const T&);
    PFN distance_;
private:
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
    SrsRtcJitter(T base, ST threshold, PFN distance) {
        threshold_ = threshold;
        distance_ = distance;
        base_ = base;

        pkt_base_ = pkt_last_ = 0;
        correct_last_ = correct_base_ = 0;
        init_ = false;
    }
    virtual ~SrsRtcJitter() {
    }
public:
    T correct(T value) {
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
private:
    SrsRtcJitter<uint32_t, int32_t>* jitter_;
public:
    SrsRtcTsJitter(uint32_t base);
    virtual ~SrsRtcTsJitter();
public:
    uint32_t correct(uint32_t value);
};

// For RTC sequence jitter.
class SrsRtcSeqJitter
{
private:
    SrsRtcJitter<uint16_t, int16_t>* jitter_;
public:
    SrsRtcSeqJitter(uint16_t base);
    virtual ~SrsRtcSeqJitter();
public:
    uint16_t correct(uint16_t value);
};

class SrsRtcSendTrack
{
public:
    // send track description
    SrsRtcTrackDescription* track_desc_;
protected:
    // The owner connection for this track.
    SrsRtcConnection* session_;
    // NACK ARQ ring buffer.
    SrsRtpRingBuffer* rtp_queue_;
protected:
    // The jitter to correct ts and sequence number.
    SrsRtcTsJitter* jitter_ts_;
    SrsRtcSeqJitter* jitter_seq_;
private:
    // By config, whether no copy.
    bool nack_no_copy_;
    // The pithy print for special stage.
    SrsErrorPithyPrint* nack_epp;
public:
    SrsRtcSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc, bool is_audio);
    virtual ~SrsRtcSendTrack();
public:
    // SrsRtcSendTrack::set_nack_no_copy
    void set_nack_no_copy(bool v) { nack_no_copy_ = v; }
    bool has_ssrc(uint32_t ssrc);
    SrsRtpPacket* fetch_rtp_packet(uint16_t seq);
    bool set_track_status(bool active);
    bool get_track_status();
    std::string get_track_id();
protected:
    void rebuild_packet(SrsRtpPacket* pkt);
public:
    // Note that we can set the pkt to NULL to avoid copy, for example, if the NACK cache the pkt and
    // set to NULL, nack nerver copy it but set the pkt to NULL.
    srs_error_t on_nack(SrsRtpPacket** ppkt);
public:
    virtual srs_error_t on_rtp(SrsRtpPacket* pkt) = 0;
    virtual srs_error_t on_rtcp(SrsRtpPacket* pkt) = 0;
    virtual srs_error_t on_recv_nack(const std::vector<uint16_t>& lost_seqs);
};

class SrsRtcAudioSendTrack : public SrsRtcSendTrack
{
public:
    SrsRtcAudioSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc);
    virtual ~SrsRtcAudioSendTrack();
public:
    virtual srs_error_t on_rtp(SrsRtpPacket* pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket* pkt);
};

class SrsRtcVideoSendTrack : public SrsRtcSendTrack
{
public:
    SrsRtcVideoSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc);
    virtual ~SrsRtcVideoSendTrack();
public:
    virtual srs_error_t on_rtp(SrsRtpPacket* pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket* pkt);
};

class SrsRtcSSRCGenerator
{
private:
    static SrsRtcSSRCGenerator* _instance;
private:
    uint32_t ssrc_num;
private:
    SrsRtcSSRCGenerator();
    virtual ~SrsRtcSSRCGenerator();
public:
    static SrsRtcSSRCGenerator* instance();
    uint32_t generate_ssrc();
};

#endif

