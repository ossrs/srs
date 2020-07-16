/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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
#include <srs_service_st.hpp>
#include <srs_app_source.hpp>

class SrsRequest;
class SrsConnection;
class SrsMetaCache;
class SrsSharedPtrMessage;
class SrsCommonMessage;
class SrsMessageArray;
class SrsRtcStream;
class SrsRtcFromRtmpBridger;
class SrsAudioRecode;
class SrsRtpPacket2;
class SrsSample;
class SrsRtcStreamDescription;
class SrsRtcTrackDescription;
class SrsRtcConnection;
class SrsRtpRingBuffer;
class SrsRtpNackForReceiver;

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

class SrsRtcConsumer
{
private:
    SrsRtcStream* source;
    std::vector<SrsRtpPacket2*> queue;
    // when source id changed, notice all consumers
    bool should_update_source_id;
    // The cond wait for mw.
    // @see https://github.com/ossrs/srs/issues/251
    srs_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;
public:
    SrsRtcConsumer(SrsRtcStream* s);
    virtual ~SrsRtcConsumer();
public:
    // When source id changed, notice client to print.
    virtual void update_source_id();
    // Put RTP packet into queue.
    // @note We do not drop packet here, but drop it in sender.
    srs_error_t enqueue(SrsRtpPacket2* pkt);
    // Get all RTP packets from queue.
    virtual srs_error_t dump_packets(std::vector<SrsRtpPacket2*>& pkts);
    // Wait for at-least some messages incoming in queue.
    virtual void wait(int nb_msgs);
};

class SrsRtcStreamManager
{
private:
    srs_mutex_t lock;
    std::map<std::string, SrsRtcStream*> pool;
public:
    SrsRtcStreamManager();
    virtual ~SrsRtcStreamManager();
public:
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param pps the matched source, if success never be NULL.
    virtual srs_error_t fetch_or_create(SrsRequest* r, SrsRtcStream** pps);
private:
    // Get the exists source, NULL when not exists.
    // update the request and return the exists source.
    virtual SrsRtcStream* fetch(SrsRequest* r);
};

// Global singleton instance.
extern SrsRtcStreamManager* _srs_rtc_sources;

// A publish stream interface, for source to callback with.
class ISrsRtcPublishStream
{
public:
    ISrsRtcPublishStream();
    virtual ~ISrsRtcPublishStream();
public:
    virtual void request_keyframe(uint32_t ssrc) = 0;
};

// A Source is a stream, to publish and to play with, binding to SrsRtcPublishStream and SrsRtcPlayStream.
class SrsRtcStream
{
private:
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_id_changed() to let all clients know.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    SrsRequest* req;
    ISrsRtcPublishStream* publish_stream_;
    // Transmux RTMP to RTC.
    ISrsSourceBridger* bridger_;
    // Steam description for this steam.
    SrsRtcStreamDescription* stream_desc_;
private:
    // To delivery stream to clients.
    std::vector<SrsRtcConsumer*> consumers;
    // Whether source is avaiable for publishing.
    bool _can_publish;
public:
    SrsRtcStream();
    virtual ~SrsRtcStream();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    // Update the authentication information in request.
    virtual void update_auth(SrsRequest* r);
    // The source id changed.
    virtual srs_error_t on_source_id_changed(SrsContextId id);
    // Get current source id.
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
    // Get the bridger.
    ISrsSourceBridger* bridger();
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
    // TODO: FIXME: Remove the param is_edge.
    virtual bool can_publish(bool is_edge);
    // When start publish stream.
    virtual srs_error_t on_publish();
    // When stop publish stream.
    virtual void on_unpublish();
public:
    // Get and set the publisher, passed to consumer to process requests such as PLI.
    ISrsRtcPublishStream* publish_stream();
    void set_publish_stream(ISrsRtcPublishStream* v);
    // Consume the shared RTP packet, user must free it.
    srs_error_t on_rtp(SrsRtpPacket2* pkt);
    // Set and get stream description for souce
    void set_stream_desc(SrsRtcStreamDescription* stream_desc);
    std::vector<SrsRtcTrackDescription*> get_track_desc(std::string type, std::string media_type);
};

#ifdef SRS_FFMPEG_FIT
class SrsRtcFromRtmpBridger : public ISrsSourceBridger
{
private:
    SrsRequest* req;
    SrsRtcStream* source_;
    // The format, codec information.
    SrsRtmpFormat* format;
    // The metadata cache.
    SrsMetaCache* meta;
private:
    bool discard_aac;
    SrsAudioRecode* codec;
    bool discard_bframe;
    bool merge_nalus;
    uint32_t audio_timestamp;
    uint16_t audio_sequence;
    uint16_t video_sequence;
public:
    SrsRtcFromRtmpBridger(SrsRtcStream* source);
    virtual ~SrsRtcFromRtmpBridger();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsSharedPtrMessage* msg);
private:
    srs_error_t transcode(char* adts_audio, int nn_adts_audio);
    srs_error_t package_opus(char* data, int size, SrsRtpPacket2** ppkt);
public:
    virtual srs_error_t on_video(SrsSharedPtrMessage* msg);
private:
    srs_error_t filter(SrsSharedPtrMessage* msg, SrsFormat* format, bool& has_idr, std::vector<SrsSample*>& samples);
    srs_error_t package_stap_a(SrsRtcStream* source, SrsSharedPtrMessage* msg, SrsRtpPacket2** ppkt);
    srs_error_t package_nalus(SrsSharedPtrMessage* msg, const std::vector<SrsSample*>& samples, std::vector<SrsRtpPacket2*>& pkts);
    srs_error_t package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, std::vector<SrsRtpPacket2*>& pkts);
    srs_error_t package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, std::vector<SrsRtpPacket2*>& pkts);
    srs_error_t consume_packets(std::vector<SrsRtpPacket2*>& pkts);
};
#endif

class SrsRtcDummyBridger : public ISrsSourceBridger
{
public:
    SrsRtcDummyBridger();
    virtual ~SrsRtcDummyBridger();
public:
    virtual srs_error_t on_publish();
    virtual srs_error_t on_audio(SrsSharedPtrMessage* audio);
    virtual srs_error_t on_video(SrsSharedPtrMessage* video);
    virtual void on_unpublish();
};

// TODO: FIXME: Rename it.
class SrsCodecPayload
{
public:
    std::string type_;
    uint8_t pt_;
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
    struct H264SpecificParameter
    {
        std::string profile_level_id;
        std::string packetization_mode;
        std::string level_asymmerty_allow;
    };
    H264SpecificParameter h264_param_;

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
    // TODO: FIXME: whether mid is needed?
    std::string mid_;

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
public:
    // find media with payload type.
    SrsMediaPayloadType generate_media_payload_type(int payload_type);
};

class SrsRtcStreamDescription
{
public:
    // the id for this stream;
    std::string id_;

    SrsRtcTrackDescription* audio_track_desc_;
    std::vector<SrsRtcTrackDescription*> video_track_descs_;
public:
    SrsRtcStreamDescription();
    virtual ~SrsRtcStreamDescription();

public:
    SrsRtcStreamDescription* copy();
    SrsRtcTrackDescription* find_track_description_by_ssrc(uint32_t ssrc);
};

class SrsRtcRecvTrack
{
protected:
    SrsRtcTrackDescription* track_desc_;

    SrsRtcConnection* session_;
    SrsRtpRingBuffer* rtp_queue_;
    SrsRtpNackForReceiver* nack_receiver_;

    // send report ntp and received time.
    SrsNtp last_sender_report_ntp;
    uint64_t last_sender_report_sys_time;
public:
    SrsRtcRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* stream_descs, bool is_audio);
    virtual ~SrsRtcRecvTrack();
public:
    bool has_ssrc(uint32_t ssrc);
    void update_rtt(int rtt);
    void update_send_report_time(const SrsNtp& ntp);
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();
protected:
    srs_error_t on_nack(SrsRtpPacket2* pkt);
public:
    virtual srs_error_t on_rtp(SrsRtcStream* source, SrsRtpPacket2* pkt);
};

class SrsRtcAudioRecvTrack : public SrsRtcRecvTrack
{
public:
    SrsRtcAudioRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc);
    virtual ~SrsRtcAudioRecvTrack();
public:
    virtual srs_error_t on_rtp(SrsRtcStream* source, SrsRtpPacket2* pkt);
};

class SrsRtcVideoRecvTrack : public SrsRtcRecvTrack
{
private:
    bool request_key_frame_;
public:
    SrsRtcVideoRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* stream_descs);
    virtual ~SrsRtcVideoRecvTrack();
public:
    virtual srs_error_t on_rtp(SrsRtcStream* source, SrsRtpPacket2* pkt);
public:
    void request_keyframe();
};

class SrsRtcSendTrack
{
protected:
    // send track description
    SrsRtcTrackDescription* track_desc_;

    SrsRtcConnection* session_;
    // NACK ARQ ring buffer.
    SrsRtpRingBuffer* rtp_queue_;
public:
    SrsRtcSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc, bool is_audio);
    virtual ~SrsRtcSendTrack();
public:
    bool has_ssrc(uint32_t ssrc);
    SrsRtpPacket2* fetch_rtp_packet(uint16_t seq);
public:
    virtual srs_error_t on_rtp(std::vector<SrsRtpPacket2*>& send_packets, SrsRtpPacket2* pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket2* pkt);
};

class SrsRtcAudioSendTrack : public SrsRtcSendTrack
{
public:
    SrsRtcAudioSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc);
    virtual ~SrsRtcAudioSendTrack();
public:
    virtual srs_error_t on_rtp(std::vector<SrsRtpPacket2*>& send_packets, SrsRtpPacket2* pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket2* pkt);
};

class SrsRtcVideoSendTrack : public SrsRtcSendTrack
{
public:
    SrsRtcVideoSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc);
    virtual ~SrsRtcVideoSendTrack();
public:
    virtual srs_error_t on_rtp(std::vector<SrsRtpPacket2*>& send_packets, SrsRtpPacket2* pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket2* pkt);
};

#endif

