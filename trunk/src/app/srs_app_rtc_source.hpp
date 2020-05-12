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

class SrsRtcConsumer : public ISrsConsumerQueue
{
private:
    SrsRtcSource* source;
    // The owner connection for debug, maybe NULL.
    SrsConnection* conn;
    SrsMessageQueue* queue;
    // when source id changed, notice all consumers
    bool should_update_source_id;
#ifdef SRS_PERF_QUEUE_COND_WAIT
    // The cond wait for mw.
    // @see https://github.com/ossrs/srs/issues/251
    srs_cond_t mw_wait;
    bool mw_waiting;
    int mw_min_msgs;
    srs_utime_t mw_duration;
#endif
public:
    SrsRtcConsumer(SrsRtcSource* s, SrsConnection* c);
    virtual ~SrsRtcConsumer();
public:
    // when source id changed, notice client to print.
    virtual void update_source_id();
    // Enqueue an shared ptr message.
    // @param shared_msg, directly ptr, copy it if need to save it.
    // @param whether atc, donot use jitter correct if true.
    // @param ag the algorithm of time jitter.
    virtual srs_error_t enqueue(SrsSharedPtrMessage* shared_msg, bool atc, SrsRtmpJitterAlgorithm ag);
    // Get packets in consumer queue.
    // @param msgs the msgs array to dump packets to send.
    // @param count the count in array, intput and output param.
    // @remark user can specifies the count to get specified msgs; 0 to get all if possible.
    virtual srs_error_t dump_packets(SrsMessageArray* msgs, int& count);
#ifdef SRS_PERF_QUEUE_COND_WAIT
    // wait for messages incomming, atleast nb_msgs and in duration.
    // @param nb_msgs the messages count to wait.
    // @param msgs_duration the messages duration to wait.
    virtual void wait(int nb_msgs, srs_utime_t msgs_duration);
#endif
};

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
    // The metadata cache.
    SrsMetaCache* meta;
    // Whether source is avaiable for publishing.
    bool _can_publish;
private:
    // The format, codec information.
    SrsRtmpFormat* format;
    // rtc handler
    SrsRtc* rtc;
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
    virtual srs_error_t create_consumer(SrsConnection* conn, SrsRtcConsumer*& consumer);
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
    // For RTC, we need to package SPS/PPS(in cached meta) before each IDR.
    SrsMetaCache* cached_meta();
    // Get and set the publisher, passed to consumer to process requests such as PLI.
    SrsRtcPublisher* rtc_publisher();
    void set_rtc_publisher(SrsRtcPublisher* v);
    // When got RTC audio message, which is encoded in opus.
    // TODO: FIXME: Merge with on_audio.
    srs_error_t on_rtc_audio(SrsSharedPtrMessage* audio);
    virtual srs_error_t on_video(SrsCommonMessage* video);
    virtual srs_error_t on_audio_imp(SrsSharedPtrMessage* audio);
    virtual srs_error_t on_video_imp(SrsSharedPtrMessage* video);
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

class SrsRtcFromRtmpBridger : public ISrsSourceBridger
{
private:
    SrsRtcSource* source_;
public:
    SrsRtcFromRtmpBridger(SrsRtcSource* source);
    virtual ~SrsRtcFromRtmpBridger();
public:
    virtual srs_error_t on_publish();
    virtual srs_error_t on_audio(SrsSharedPtrMessage* audio);
    virtual srs_error_t on_video(SrsSharedPtrMessage* video);
    virtual void on_unpublish();
};

#endif

