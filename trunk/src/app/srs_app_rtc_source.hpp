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

#include <srs_service_st.hpp>
#include <srs_app_source.hpp>

class SrsRequest;
class SrsConnection;
class SrsMetaCache;
class SrsRtcPublisher;
class SrsSharedPtrMessage;
class SrsCommonMessage;
class SrsMessageArray;
class SrsRtcSource;
class SrsRtcFromRtmpBridger;
class SrsAudioRecode;
class SrsRtpPacket2;
class SrsSample;

class SrsRtcConsumer
{
private:
    SrsRtcSource* source;
    std::vector<SrsRtpPacket2*> queue;
    // when source id changed, notice all consumers
    bool should_update_source_id;
    // The cond wait for mw.
    // @see https://github.com/ossrs/srs/issues/251
    srs_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;
public:
    SrsRtcConsumer(SrsRtcSource* s);
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
private:
    // Get the exists source, NULL when not exists.
    // update the request and return the exists source.
    virtual SrsRtcSource* fetch(SrsRequest* r);
};

// Global singleton instance.
extern SrsRtcSourceManager* _srs_rtc_sources;

class SrsRtcSource
{
private:
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_id_changed() to let all clients know.
    int _source_id;
    // previous source id.
    int _pre_source_id;
    SrsRequest* req;
    SrsRtcPublisher* rtc_publisher_;
    // Transmux RTMP to RTC.
    SrsRtcFromRtmpBridger* bridger_;
private:
    // To delivery stream to clients.
    std::vector<SrsRtcConsumer*> consumers;
    // Whether source is avaiable for publishing.
    bool _can_publish;
public:
    SrsRtcSource();
    virtual ~SrsRtcSource();
public:
    virtual srs_error_t initialize(SrsRequest* r);
    // Update the authentication information in request.
    virtual void update_auth(SrsRequest* r);
    // The source id changed.
    virtual srs_error_t on_source_id_changed(int id);
    // Get current source id.
    virtual int source_id();
    virtual int pre_source_id();
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
    SrsRtcPublisher* rtc_publisher();
    void set_rtc_publisher(SrsRtcPublisher* v);
    // Consume the shared RTP packet, user must free it.
    srs_error_t on_rtp(SrsRtpPacket2* pkt);
};

class SrsRtcFromRtmpBridger : public ISrsSourceBridger
{
private:
    SrsRequest* req;
    SrsRtcSource* source_;
    // The format, codec information.
    SrsRtmpFormat* format;
    // The metadata cache.
    SrsMetaCache* meta;
private:
    bool discard_aac;
    SrsAudioRecode* codec;
    bool discard_bframe;
    bool merge_nalus;
public:
    SrsRtcFromRtmpBridger(SrsRtcSource* source);
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
    srs_error_t package_stap_a(SrsRtcSource* source, SrsSharedPtrMessage* msg, SrsRtpPacket2** ppkt);
    srs_error_t package_nalus(SrsSharedPtrMessage* msg, const std::vector<SrsSample*>& samples, std::vector<SrsRtpPacket2*>& pkts);
    srs_error_t package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, std::vector<SrsRtpPacket2*>& pkts);
    srs_error_t package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, std::vector<SrsRtpPacket2*>& pkts);
    srs_error_t consume_packets(std::vector<SrsRtpPacket2*>& pkts);
};

#endif

