/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#ifndef SRS_APP_SOURCE_HPP
#define SRS_APP_SOURCE_HPP

#include <srs_core.hpp>

#include <map>
#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_core_performance.hpp>

class SrsFormat;
class SrsRtmpFormat;
class SrsConsumer;
class SrsPlayEdge;
class SrsPublishEdge;
class SrsSource;
class SrsCommonMessage;
class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;
class SrsForwarder;
class SrsRequest;
class SrsStSocket;
class SrsRtmpServer;
class SrsEdgeProxyContext;
class SrsMessageArray;
class SrsNgExec;
class SrsConnection;
class SrsMessageHeader;
class SrsHls;
class SrsDvr;
class SrsDash;
class SrsEncoder;
class SrsBuffer;
#ifdef SRS_AUTO_HDS
class SrsHds;
#endif

// The time jitter algorithm:
// 1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
// 2. zero, only ensure sttream start at zero, ignore timestamp jitter.
// 3. off, disable the time jitter algorithm, like atc.
enum SrsRtmpJitterAlgorithm
{
    SrsRtmpJitterAlgorithmFULL = 0x01,
    SrsRtmpJitterAlgorithmZERO,
    SrsRtmpJitterAlgorithmOFF
};
int _srs_time_jitter_string2int(std::string time_jitter);

// Time jitter detect and correct, to ensure the rtmp stream is monotonically.
class SrsRtmpJitter
{
private:
    int64_t last_pkt_time;
    int64_t last_pkt_correct_time;
public:
    SrsRtmpJitter();
    virtual ~SrsRtmpJitter();
public:
    // detect the time jitter and correct it.
    // @param ag the algorithm to use for time jitter.
    virtual srs_error_t correct(SrsSharedPtrMessage* msg, SrsRtmpJitterAlgorithm ag);
    // Get current client time, the last packet time.
    virtual int64_t get_time();
};

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
// To alloc and increase fixed space, fast remove and insert for msgs sender.
// @see https://github.com/ossrs/srs/issues/251
class SrsFastVector
{
private:
    SrsSharedPtrMessage** msgs;
    int nb_msgs;
    int count;
public:
    SrsFastVector();
    virtual ~SrsFastVector();
public:
    virtual int size();
    virtual int begin();
    virtual int end();
    virtual SrsSharedPtrMessage** data();
    virtual SrsSharedPtrMessage* at(int index);
    virtual void clear();
    virtual void erase(int _begin, int _end);
    virtual void push_back(SrsSharedPtrMessage* msg);
    virtual void free();
};
#endif

// The message queue for the consumer(client), forwarder.
// We limit the size in seconds, drop old messages(the whole gop) if full.
class SrsMessageQueue
{
private:
    // The start and end time.
    srs_utime_t av_start_time;
    srs_utime_t av_end_time;
private:
	// Whether do logging when shrinking.
    bool _ignore_shrink;
    // The max queue size, shrink if exceed it.
    srs_utime_t max_queue_size;
#ifdef SRS_PERF_QUEUE_FAST_VECTOR
    SrsFastVector msgs;
#else
    std::vector<SrsSharedPtrMessage*> msgs;
#endif
public:
    SrsMessageQueue(bool ignore_shrink = false);
    virtual ~SrsMessageQueue();
public:
    // Get the size of queue.
    virtual int size();
    // Get the duration of queue.
    virtual srs_utime_t duration();
    // Set the queue size
    // @param queue_size the queue size in srs_utime_t.
    virtual void set_queue_size(srs_utime_t queue_size);
public:
    // Enqueue the message, the timestamp always monotonically.
    // @param msg, the msg to enqueue, user never free it whatever the return code.
    // @param is_overflow, whether overflow and shrinked. NULL to ignore.
    virtual srs_error_t enqueue(SrsSharedPtrMessage* msg, bool* is_overflow = NULL);
    // Get packets in consumer queue.
    // @pmsgs SrsSharedPtrMessage*[], used to store the msgs, user must alloc it.
    // @count the count in array, output param.
    // @max_count the max count to dequeue, must be positive.
    virtual srs_error_t dump_packets(int max_count, SrsSharedPtrMessage** pmsgs, int& count);
    // Dumps packets to consumer, use specified args.
    // @remark the atc/tba/tbv/ag are same to SrsConsumer.enqueue().
    virtual srs_error_t dump_packets(SrsConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm ag);
private:
    // Remove a gop from the front.
    // if no iframe found, clear it.
    virtual void shrink();
public:
    // clear all messages in queue.
    virtual void clear();
};

// The wakable used for some object
// which is waiting on cond.
class ISrsWakable
{
public:
    ISrsWakable();
    virtual ~ISrsWakable();
public:
    // when the consumer(for player) got msg from recv thread,
    // it must be processed for maybe it's a close msg, so the cond
    // wait must be wakeup.
    virtual void wakeup() = 0;
};

// The consumer for SrsSource, that is a play client.
class SrsConsumer : public ISrsWakable
{
private:
    SrsRtmpJitter* jitter;
    SrsSource* source;
    SrsMessageQueue* queue;
    // The owner connection for debug, maybe NULL.
    SrsConnection* conn;
    bool paused;
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
    SrsConsumer(SrsSource* s, SrsConnection* c);
    virtual ~SrsConsumer();
public:
    // Set the size of queue.
    virtual void set_queue_size(srs_utime_t queue_size);
    // when source id changed, notice client to print.
    virtual void update_source_id();
public:
    // Get current client time, the last packet time.
    virtual int64_t get_time();
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
    // when client send the pause message.
    virtual srs_error_t on_play_client_pause(bool is_pause);
    // ISrsWakable
public:
    // when the consumer(for player) got msg from recv thread,
    // it must be processed for maybe it's a close msg, so the cond
    // wait must be wakeup.
    virtual void wakeup();
};

// cache a gop of video/audio data,
// delivery at the connect of flash player,
// To enable it to fast startup.
class SrsGopCache
{
private:
    // if disabled the gop cache,
    // The client will wait for the next keyframe for h264,
    // and will be black-screen.
    bool enable_gop_cache;
    // The video frame count, avoid cache for pure audio stream.
    int cached_video_count;
    // when user disabled video when publishing, and gop cache enalbed,
    // We will cache the audio/video for we already got video, but we never
    // know when to clear the gop cache, for there is no video in future,
    // so we must guess whether user disabled the video.
    // when we got some audios after laster video, for instance, 600 audio packets,
    // about 3s(26ms per packet) 115 audio packets, clear gop cache.
    //
    // @remark, it is ok for performance, for when we clear the gop cache,
    //       gop cache is disabled for pure audio stream.
    // @see: https://github.com/ossrs/srs/issues/124
    int audio_after_last_video_count;
    // cached gop.
    std::vector<SrsSharedPtrMessage*> gop_cache;
public:
    SrsGopCache();
    virtual ~SrsGopCache();
public:
    // cleanup when system quit.
    virtual void dispose();
    // To enable or disable the gop cache.
    virtual void set(bool v);
    virtual bool enabled();
    // only for h264 codec
    // 1. cache the gop when got h264 video packet.
    // 2. clear gop when got keyframe.
    // @param shared_msg, directly ptr, copy it if need to save it.
    virtual srs_error_t cache(SrsSharedPtrMessage* shared_msg);
    // clear the gop cache.
    virtual void clear();
    // dump the cached gop to consumer.
    virtual srs_error_t dump(SrsConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm jitter_algorithm);
    // used for atc to get the time of gop cache,
    // The atc will adjust the sequence header timestamp to gop cache.
    virtual bool empty();
    // Get the start time of gop cache, in srs_utime_t.
    // @return 0 if no packets.
    virtual srs_utime_t start_time();
    // whether current stream is pure audio,
    // when no video in gop cache, the stream is pure audio right now.
    virtual bool pure_audio();
};

// The handler to handle the event of srs source.
// For example, the http flv streaming module handle the event and
// mount http when rtmp start publishing.
class ISrsSourceHandler
{
public:
    ISrsSourceHandler();
    virtual ~ISrsSourceHandler();
public:
    // when stream start publish, mount stream.
    virtual srs_error_t on_publish(SrsSource* s, SrsRequest* r) = 0;
    // when stream stop publish, unmount stream.
    virtual void on_unpublish(SrsSource* s, SrsRequest* r) = 0;
};

// The mix queue to correct the timestamp for mix_correct algorithm.
class SrsMixQueue
{
private:
    uint32_t nb_videos;
    uint32_t nb_audios;
    std::multimap<int64_t, SrsSharedPtrMessage*> msgs;
public:
    SrsMixQueue();
    virtual ~SrsMixQueue();
public:
    virtual void clear();
    virtual void push(SrsSharedPtrMessage* msg);
    virtual SrsSharedPtrMessage* pop();
};

// The hub for origin is a collection of utilities for origin only,
// For example, DVR, HLS, Forward and Transcode are only available for origin,
// they are meanless for edge server.
class SrsOriginHub : public ISrsReloadHandler
{
private:
    SrsSource* source;
    SrsRequest* req;
    // Whether the stream hub is active, or stream is publishing.
    bool is_active;
private:
    // The format, codec information.
    SrsRtmpFormat* format;
    // hls handler.
    SrsHls* hls;
    // The DASH encoder.
    SrsDash* dash;
    // dvr handler.
    SrsDvr* dvr;
    // transcoding handler.
    SrsEncoder* encoder;
#ifdef SRS_AUTO_HDS
    // adobe hds(http dynamic streaming).
    SrsHds *hds;
#endif
    // nginx-rtmp exec feature.
    SrsNgExec* ng_exec;
    // To forward stream to other servers
    std::vector<SrsForwarder*> forwarders;
public:
    SrsOriginHub();
    virtual ~SrsOriginHub();
public:
    // Initialize the hub with source and request.
    // @param r The request object, managed by source.
    virtual srs_error_t initialize(SrsSource* s, SrsRequest* r);
    // Dispose the hub, release utilities resource,
    // For example, delete all HLS pieces.
    virtual void dispose();
    // Cycle the hub, process some regular events,
    // For example, dispose hls in cycle.
    virtual srs_error_t cycle();
public:
    // When got a parsed metadata.
    virtual srs_error_t on_meta_data(SrsSharedPtrMessage* shared_metadata, SrsOnMetaDataPacket* packet);
    // When got a parsed audio packet.
    virtual srs_error_t on_audio(SrsSharedPtrMessage* shared_audio);
    // When got a parsed video packet.
    virtual srs_error_t on_video(SrsSharedPtrMessage* shared_video, bool is_sequence_header);
public:
    // When start publish stream.
    virtual srs_error_t on_publish();
    // When stop publish stream.
    virtual void on_unpublish();
    // Internal callback.
public:
    // For the SrsForwarder to callback to request the sequence headers.
    virtual srs_error_t on_forwarder_start(SrsForwarder* forwarder);
    // For the SrsDvr to callback to request the sequence headers.
    virtual srs_error_t on_dvr_request_sh();
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_forward(std::string vhost);
    virtual srs_error_t on_reload_vhost_dash(std::string vhost);
    virtual srs_error_t on_reload_vhost_hls(std::string vhost);
    virtual srs_error_t on_reload_vhost_hds(std::string vhost);
    virtual srs_error_t on_reload_vhost_dvr(std::string vhost);
    virtual srs_error_t on_reload_vhost_transcode(std::string vhost);
    virtual srs_error_t on_reload_vhost_exec(std::string vhost);
private:
    virtual srs_error_t create_forwarders();
    virtual void destroy_forwarders();
};

// Each stream have optional meta(sps/pps in sequence header and metadata).
// This class cache and update the meta.
class SrsMetaCache
{
private:
    // The cached metadata, FLV script data tag.
    SrsSharedPtrMessage* meta;
    // The cached video sequence header, for example, sps/pps for h.264.
    SrsSharedPtrMessage* video;
    // The cached audio sequence header, for example, asc for aac.
    SrsSharedPtrMessage* audio;
    // The format for sequence header.
    SrsRtmpFormat* vformat;
    SrsRtmpFormat* aformat;
public:
    SrsMetaCache();
    virtual ~SrsMetaCache();
public:
    // Dispose the metadata cache.
    virtual void dispose();
public:
    // Get the cached metadata.
    virtual SrsSharedPtrMessage* data();
    // Get the cached vsh(video sequence header).
    virtual SrsSharedPtrMessage* vsh();
    virtual SrsFormat* vsh_format();
    // Get the cached ash(audio sequence header).
    virtual SrsSharedPtrMessage* ash();
    virtual SrsFormat* ash_format();
    // Dumps cached metadata to consumer.
    // @param dm Whether dumps the metadata.
    // @param ds Whether dumps the sequence header.
    virtual srs_error_t dumps(SrsConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm ag, bool dm, bool ds);
public:
    // Update the cached metadata by packet.
    virtual srs_error_t update_data(SrsMessageHeader* header, SrsOnMetaDataPacket* metadata, bool& updated);
    // Update the cached audio sequence header.
    virtual srs_error_t update_ash(SrsSharedPtrMessage* msg);
    // Update the cached video sequence header.
    virtual srs_error_t update_vsh(SrsSharedPtrMessage* msg);
};

// live streaming source.
class SrsSource : public ISrsReloadHandler
{
    friend class SrsOriginHub;
private:
    static std::map<std::string, SrsSource*> pool;
public:
    //  create source when fetch from cache failed.
    // @param r the client request.
    // @param h the event handler for source.
    // @param pps the matched source, if success never be NULL.
    static srs_error_t fetch_or_create(SrsRequest* r, ISrsSourceHandler* h, SrsSource** pps);
private:
    // Get the exists source, NULL when not exists.
    // update the request and return the exists source.
    static SrsSource* fetch(SrsRequest* r);
public:
    // dispose and cycle all sources.
    static void dispose_all();
    static srs_error_t cycle_all();
private:
    static srs_error_t do_cycle_all();
public:
    // when system exit, destroy the sources,
    // For gmc to analysis mem leaks.
    static void destroy();
private:
    // For publish, it's the publish client id.
    // For edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_id_changed() to let all clients know.
    int _source_id;
    // previous source id.
    int _pre_source_id;
    // deep copy of client request.
    SrsRequest* req;
    // To delivery stream to clients.
    std::vector<SrsConsumer*> consumers;
    // The time jitter algorithm for vhost.
    SrsRtmpJitterAlgorithm jitter_algorithm;
    // For play, whether use interlaced/mixed algorithm to correct timestamp.
    bool mix_correct;
    // The mix queue to implements the mix correct algorithm.
    SrsMixQueue* mix_queue;
    // For play, whether enabled atc.
    // The atc(use absolute time and donot adjust time),
    // directly use msg time and donot adjust if atc is true,
    // otherwise, adjust msg time to start from 0 to make flash happy.
    bool atc;
    // whether stream is monotonically increase.
    bool is_monotonically_increase;
    // The time of the packet we just got.
    int64_t last_packet_time;
    // The event handler.
    ISrsSourceHandler* handler;
    // The edge control service
    SrsPlayEdge* play_edge;
    SrsPublishEdge* publish_edge;
    // The gop cache for client fast startup.
    SrsGopCache* gop_cache;
    // The hub for origin server.
    SrsOriginHub* hub;
    // The metadata cache.
    SrsMetaCache* meta;
private:
    // Whether source is avaiable for publishing.
    bool _can_publish;
    // The last die time, when all consumers quit and no publisher,
    // We will remove the source when source die.
    srs_utime_t die_at;
public:
    SrsSource();
    virtual ~SrsSource();
public:
    virtual void dispose();
    virtual srs_error_t cycle();
    // Remove source when expired.
    virtual bool expired();
public:
    // Initialize the hls with handlers.
    virtual srs_error_t initialize(SrsRequest* r, ISrsSourceHandler* h);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
public:
    // The source id changed.
    virtual srs_error_t on_source_id_changed(int id);
    // Get current source id.
    virtual int source_id();
    virtual int pre_source_id();
    // Whether source is inactive, which means there is no publishing stream source.
    // @remark For edge, it's inactive util stream has been pulled from origin.
    virtual bool inactive();
public:
    virtual bool can_publish(bool is_edge);
    virtual srs_error_t on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata);
public:
    virtual srs_error_t on_audio(SrsCommonMessage* audio);
private:
    virtual srs_error_t on_audio_imp(SrsSharedPtrMessage* audio);
public:
    virtual srs_error_t on_video(SrsCommonMessage* video);
private:
    virtual srs_error_t on_video_imp(SrsSharedPtrMessage* video);
public:
    virtual srs_error_t on_aggregate(SrsCommonMessage* msg);
    // Publish stream event notify.
    // @param _req the request from client, the source will deep copy it,
    //         for when reload the request of client maybe invalid.
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
public:
    // Create consumer and dumps packets in cache.
    // @param consumer, output the create consumer.
    // @param ds, whether dumps the sequence header.
    // @param dm, whether dumps the metadata.
    // @param dg, whether dumps the gop cache.
    virtual srs_error_t create_consumer(SrsConnection* conn, SrsConsumer*& consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(SrsConsumer* consumer);
    virtual void set_cache(bool enabled);
    virtual SrsRtmpJitterAlgorithm jitter();
public:
    // For edge, when publish edge stream, check the state
    virtual srs_error_t on_edge_start_publish();
    // For edge, proxy the publish
    virtual srs_error_t on_edge_proxy_publish(SrsCommonMessage* msg);
    // For edge, proxy stop publish
    virtual void on_edge_proxy_unpublish();
public:
    virtual std::string get_curr_origin();
};

#endif
