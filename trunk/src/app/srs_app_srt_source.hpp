//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_SRT_SOURCE_HPP
#define SRS_APP_SRT_SOURCE_HPP

#include <srs_core.hpp>

#include <map>
#include <vector>

#include <srs_kernel_ts.hpp>
#include <srs_protocol_st.hpp>
#include <srs_app_stream_bridge.hpp>

class SrsSharedPtrMessage;
class SrsRequest;
class SrsLiveSource;
class SrsSrtSource;
class SrsAlonePithyPrint;
class SrsSrtFrameBuilder;

// The SRT packet with shared message.
class SrsSrtPacket
{
public:
    SrsSrtPacket();
    virtual ~SrsSrtPacket();
public:
    // Wrap buffer to shared_message, which is managed by us.
    char* wrap(int size);
    char* wrap(char* data, int size);
    // Wrap the shared message, we copy it.
    char* wrap(SrsSharedPtrMessage* msg);
    // Copy the SRT packet.
    virtual SrsSrtPacket* copy();
public:
    char* data();
    int size();
private:
    SrsSharedPtrMessage* shared_buffer_;
    // The size of SRT packet or SRT payload.
    int actual_buffer_size_;
};

class SrsSrtSourceManager
{
private:
    srs_mutex_t lock;
    std::map<std::string, SrsSrtSource*> pool;
public:
    SrsSrtSourceManager();
    virtual ~SrsSrtSourceManager();
public:
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param pps the matched source, if success never be NULL.
    virtual srs_error_t fetch_or_create(SrsRequest* r, SrsSrtSource** pps);
public:
    // Get the exists source, NULL when not exists.
    virtual SrsSrtSource* fetch(SrsRequest* r);
};

// Global singleton instance.
extern SrsSrtSourceManager* _srs_srt_sources;

class SrsSrtConsumer
{
public:
    SrsSrtConsumer(SrsSrtSource* source);
    virtual ~SrsSrtConsumer();
private:
    SrsSrtSource* source;
    std::vector<SrsSrtPacket*> queue;
    // when source id changed, notice all consumers
    bool should_update_source_id;
    // The cond wait for mw.
    srs_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;
public:
    // When source id changed, notice client to print.
    void update_source_id();
    // Put SRT packet into queue.
    srs_error_t enqueue(SrsSrtPacket* packet);
    // For SRT, we only got one packet, because there is not many packets in queue.
    virtual srs_error_t dump_packet(SrsSrtPacket** ppkt);
    // Wait for at-least some messages incoming in queue.
    virtual void wait(int nb_msgs, srs_utime_t timeout);
};

// Collect and build SRT TS packet to AV frames.
class SrsSrtFrameBuilder : public ISrsTsHandler
{
public:
    SrsSrtFrameBuilder(ISrsStreamBridge* bridge);
    virtual ~SrsSrtFrameBuilder();
public:
    srs_error_t initialize(SrsRequest* r);
public:
    virtual srs_error_t on_publish();
    virtual srs_error_t on_packet(SrsSrtPacket* pkt);
    virtual void on_unpublish();
// Interface ISrsTsHandler
public:
    virtual srs_error_t on_ts_message(SrsTsMessage* msg);
private:
    srs_error_t on_ts_video_avc(SrsTsMessage* msg, SrsBuffer* avs);
    srs_error_t on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs);
    srs_error_t check_sps_pps_change(SrsTsMessage* msg);
    srs_error_t on_h264_frame(SrsTsMessage* msg, std::vector<std::pair<char*, int> >& ipb_frames);
    srs_error_t check_audio_sh_change(SrsTsMessage* msg, uint32_t pts);
    srs_error_t on_aac_frame(SrsTsMessage* msg, uint32_t pts, char* frame, int frame_size);
#ifdef SRS_H265
    srs_error_t on_ts_video_hevc(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t check_vps_sps_pps_change(SrsTsMessage *msg);
    srs_error_t on_hevc_frame(SrsTsMessage *msg, std::vector<std::pair<char *, int>> &ipb_frames);
#endif
private:
    ISrsStreamBridge* bridge_;
private:
    SrsTsContext* ts_ctx_;
    // Record sps/pps had changed, if change, need to generate new video sh frame.
    bool sps_pps_change_;
    std::string sps_;
    std::string pps_;
#ifdef SRS_H265
    bool vps_sps_pps_change_;
    std::string hevc_vps_;
    std::string hevc_sps_;
    std::string hevc_pps_;
#endif
    // Record audio sepcific config had changed, if change, need to generate new audio sh frame.
    bool audio_sh_change_;
    std::string audio_sh_;
private:
    SrsRequest* req_;
private:
    // SRT to rtmp, video stream id.
    int video_streamid_;
    // SRT to rtmp, audio stream id.
    int audio_streamid_;
    // Cycle print when audio duration too large because mpegts may merge multi audio frame in one pes packet.
    SrsAlonePithyPrint* pp_audio_duration_;
};

class SrsSrtSource
{
public:
    SrsSrtSource();
    virtual ~SrsSrtSource();
public:
    virtual srs_error_t initialize(SrsRequest* r);
public:
    // The source id changed.
    virtual srs_error_t on_source_id_changed(SrsContextId id);
    // Get current source id.
    virtual SrsContextId source_id();
    virtual SrsContextId pre_source_id();
    // Update the authentication information in request.
    virtual void update_auth(SrsRequest* r);
public:
    void set_bridge(ISrsStreamBridge* bridge);
public:
    // Create consumer
    // @param consumer, output the create consumer.
    virtual srs_error_t create_consumer(SrsSrtConsumer*& consumer);
    // Dumps packets in cache to consumer.
    virtual srs_error_t consumer_dumps(SrsSrtConsumer* consumer);
    virtual void on_consumer_destroy(SrsSrtConsumer* consumer);
    // Whether we can publish stream to the source, return false if it exists.
    virtual bool can_publish();
    // When start publish stream.
    virtual srs_error_t on_publish();
    // When stop publish stream.
    virtual void on_unpublish();
public:
    srs_error_t on_packet(SrsSrtPacket* packet);
private:
    // Source id.
    SrsContextId _source_id;
    // previous source id.
    SrsContextId _pre_source_id;
    SrsRequest* req;
    // To delivery packets to clients.
    std::vector<SrsSrtConsumer*> consumers;
    bool can_publish_;
private:
    SrsSrtFrameBuilder* frame_builder_;
    ISrsStreamBridge* bridge_;
};

#endif

