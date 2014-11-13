/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_APP_SOURCE_HPP
#define SRS_APP_SOURCE_HPP

/*
#include <srs_app_source.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>

class SrsPlayEdge;
class SrsPublishEdge;
class SrsSource;
class SrsMessage;
class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;
class SrsForwarder;
class SrsRequest;
class SrsStSocket;
class SrsRtmpServer;
class SrsEdgeProxyContext;
#ifdef SRS_AUTO_HLS
class SrsHls;
#endif
#ifdef SRS_AUTO_DVR
class SrsDvr;
#endif
#ifdef SRS_AUTO_TRANSCODE
class SrsEncoder;
#endif
class SrsStream;

/**
* the time jitter algorithm:
* 1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
* 2. zero, only ensure sttream start at zero, ignore timestamp jitter.
* 3. off, disable the time jitter algorithm, like atc.
*/
enum SrsRtmpJitterAlgorithm
{
    SrsRtmpJitterAlgorithmFULL = 0x01,
    SrsRtmpJitterAlgorithmZERO,
    SrsRtmpJitterAlgorithmOFF
};
int _srs_time_jitter_string2int(std::string time_jitter);

/**
* time jitter detect and correct,
* to ensure the rtmp stream is monotonically.
*/
class SrsRtmpJitter
{
private:
    int64_t last_pkt_time;
    int64_t last_pkt_correct_time;
public:
    SrsRtmpJitter();
    virtual ~SrsRtmpJitter();
public:
    /**
    * detect the time jitter and correct it.
    * @param tba, the audio timebase, used to calc the "right" delta if jitter detected.
    * @param tbv, the video timebase, used to calc the "right" delta if jitter detected.
    * @param start_at_zero whether ensure stream start at zero.
    * @param mono_increasing whether ensure stream is monotonically inscreasing.
    */
    virtual int correct(SrsSharedPtrMessage* msg, int tba, int tbv, SrsRtmpJitterAlgorithm ag);
    /**
    * get current client time, the last packet time.
    */
    virtual int get_time();
};

/**
* the message queue for the consumer(client), forwarder.
* we limit the size in seconds, drop old messages(the whole gop) if full.
*/
class SrsMessageQueue
{
private:
    int64_t av_start_time;
    int64_t av_end_time;
    int queue_size_ms;
    std::vector<SrsSharedPtrMessage*> msgs;
public:
    SrsMessageQueue();
    virtual ~SrsMessageQueue();
public:
    /**
    * set the queue size
    * @param queue_size the queue size in seconds.
    */
    virtual void set_queue_size(double queue_size);
public:
    /**
    * enqueue the message, the timestamp always monotonically.
    * @param msg, the msg to enqueue, user never free it whatever the return code.
    */
    virtual int enqueue(SrsSharedPtrMessage* msg);
    /**
    * get packets in consumer queue.
    * @pmsgs SrsMessages*[], used to store the msgs, user must alloc it.
    * @count the count in array, output param.
    * @max_count the max count to dequeue, must be positive.
    */
    virtual int dump_packets(int max_count, SrsMessage** pmsgs, int& count);
private:
    /**
    * remove a gop from the front.
    * if no iframe found, clear it.
    */
    virtual void shrink();
    virtual void clear();
};

/**
* the consumer for SrsSource, that is a play client.
*/
class SrsConsumer
{
private:
    SrsRtmpJitter* jitter;
    SrsSource* source;
    SrsMessageQueue* queue;
    bool paused;
    // when source id changed, notice all consumers
    bool should_update_source_id;
public:
    SrsConsumer(SrsSource* _source);
    virtual ~SrsConsumer();
public:
    /**
    * set the size of queue.
    */
    virtual void set_queue_size(double queue_size);
    /**
    * when source id changed, notice client to print.
    */
    virtual void update_source_id();
public:
    /**
    * get current client time, the last packet time.
    */
    virtual int get_time();
    /**
    * enqueue an shared ptr message.
    * @param whether atc, donot use jitter correct if true.
    * @param tba timebase of audio.
    *         used to calc the audio time delta if time-jitter detected.
    * @param tbv timebase of video.
    *        used to calc the video time delta if time-jitter detected.
    * @param ag the algorithm of time jitter.
    */
    virtual int enqueue(SrsSharedPtrMessage* msg, bool atc, int tba, int tbv, SrsRtmpJitterAlgorithm ag);
    /**
    * get packets in consumer queue.
    * @pmsgs SrsMessages*[], used to store the msgs, user must alloc it.
    * @count the count in array, output param.
    * @max_count the max count to dequeue, must be positive.
    */
    virtual int dump_packets(int max_count, SrsMessage** pmsgs, int& count);
    /**
    * when client send the pause message.
    */
    virtual int on_play_client_pause(bool is_pause);
};

/**
* cache a gop of video/audio data,
* delivery at the connect of flash player,
* to enable it to fast startup.
*/
class SrsGopCache
{
private:
    /**
    * if disabled the gop cache,
    * the client will wait for the next keyframe for h264,
    * and will be black-screen.
    */
    bool enable_gop_cache;
    /**
    * the video frame count, avoid cache for pure audio stream.
    */
    int cached_video_count;
    /**
    * when user disabled video when publishing, and gop cache enalbed,
    * we will cache the audio/video for we already got video, but we never
    * know when to clear the gop cache, for there is no video in future,
    * so we must guess whether user disabled the video.
    * when we got some audios after laster video, for instance, 600 audio packets,
    * about 3s(26ms per packet) 115 audio packets, clear gop cache.
    * 
    * @remark, it is ok for performance, for when we clear the gop cache,
    *       gop cache is disabled for pure audio stream.
    * @see: https://github.com/winlinvip/simple-rtmp-server/issues/124
    */
    int audio_after_last_video_count;
    /**
    * cached gop.
    */
    std::vector<SrsSharedPtrMessage*> gop_cache;
public:
    SrsGopCache();
    virtual ~SrsGopCache();
public:
    /**
    * to enable or disable the gop cache.
    */
    virtual void set(bool enabled);
    /**
    * only for h264 codec
    * 1. cache the gop when got h264 video packet.
    * 2. clear gop when got keyframe.
    */
    virtual int cache(SrsSharedPtrMessage* msg);
    /**
    * clear the gop cache.
    */
    virtual void clear();
    /**
    * dump the cached gop to consumer.
    */
    virtual int dump(SrsConsumer* consumer, 
        bool atc, int tba, int tbv, SrsRtmpJitterAlgorithm jitter_algorithm
    );
    /**
    * used for atc to get the time of gop cache,
    * the atc will adjust the sequence header timestamp to gop cache.
    */
    virtual bool empty();
    /**
    * get the start time of gop cache, in ms.
    * @return 0 if no packets.
    */
    virtual int64_t start_time();
    /**
    * whether current stream is pure audio,
    * when no video in gop cache, the stream is pure audio right now.
    */
    virtual bool pure_audio();
};

/**
* live streaming source.
*/
class SrsSource : public ISrsReloadHandler
{
private:
    static std::map<std::string, SrsSource*> pool;
public:
    /**
    * find stream by vhost/app/stream.
    * @param req the client request.
    * @param ppsource the matched source, if success never be NULL.
    * @remark stream_url should without port and schema.
    */
    static int find(SrsRequest* req, SrsSource** ppsource);
    /**
    * when system exit, destroy the sources,
    * for gmc to analysis mem leaks.
    */
    static void destroy();
private:
    // source id,
    // for publish, it's the publish client id.
    // for edge, it's the edge ingest id.
    // when source id changed, for example, the edge reconnect,
    // invoke the on_source_id_changed() to let all clients know.
    int _source_id;
    // deep copy of client request.
    SrsRequest* _req;
    // to delivery stream to clients.
    std::vector<SrsConsumer*> consumers;
    // the time jitter algorithm for vhost.
    SrsRtmpJitterAlgorithm jitter_algorithm;
    // hls handler.
#ifdef SRS_AUTO_HLS
    SrsHls* hls;
#endif
    // dvr handler.
#ifdef SRS_AUTO_DVR
    SrsDvr* dvr;
#endif
    // transcoding handler.
#ifdef SRS_AUTO_TRANSCODE
    SrsEncoder* encoder;
#endif
    // edge control service
    SrsPlayEdge* play_edge;
    SrsPublishEdge* publish_edge;
    // gop cache for client fast startup.
    SrsGopCache* gop_cache;
    // to forward stream to other servers
    std::vector<SrsForwarder*> forwarders;
    // for aggregate message
    SrsStream* aggregate_stream;
private:
    /**
    * the sample rate of audio in metadata.
    */
    int sample_rate;
    /**
    * the video frame rate in metadata.
    */
    int frame_rate;
    /**
    * can publish, true when is not streaming
    */
    bool _can_publish;
    /**
    * atc whether atc(use absolute time and donot adjust time),
    * directly use msg time and donot adjust if atc is true,
    * otherwise, adjust msg time to start from 0 to make flash happy.
    */
    // TODO: FIXME: to support reload atc.
    bool atc;
private:
    SrsSharedPtrMessage* cache_metadata;
    // the cached video sequence header.
    SrsSharedPtrMessage* cache_sh_video;
    // the cached audio sequence header.
    SrsSharedPtrMessage* cache_sh_audio;
public:
    /**
    * @param _req the client request object, 
    *     this object will deep copy it for reload.
    */
    SrsSource(SrsRequest* req);
    virtual ~SrsSource();
public:
    virtual int initialize();
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_atc(std::string vhost);
    virtual int on_reload_vhost_gop_cache(std::string vhost);
    virtual int on_reload_vhost_queue_length(std::string vhost);
    virtual int on_reload_vhost_time_jitter(std::string vhost);
    virtual int on_reload_vhost_forward(std::string vhost);
    virtual int on_reload_vhost_hls(std::string vhost);
    virtual int on_reload_vhost_dvr(std::string vhost);
    virtual int on_reload_vhost_transcode(std::string vhost);
// for the tools callback
public:
    // for the SrsForwarder to callback to request the sequence headers.
    virtual int on_forwarder_start(SrsForwarder* forwarder);
    // for the SrsHls to callback to request the sequence headers.
    virtual int on_hls_start();
    // for the SrsDvr to callback to request the sequence headers.
    virtual int on_dvr_request_sh();
    // source id changed.
    virtual int on_source_id_changed(int id);
    // get current source id.
    virtual int source_id();
// logic data methods
public:
    virtual bool can_publish();
    virtual int on_meta_data(SrsMessage* msg, SrsOnMetaDataPacket* metadata);
    virtual int on_audio(SrsMessage* audio);
    virtual int on_video(SrsMessage* video);
    virtual int on_aggregate(SrsMessage* msg);
    /**
    * the pre-publish is we are very sure we are
    * trying to publish stream, please lock the resource,
    * and we use release_publish() to release the resource.
    */
    virtual int acquire_publish();
    virtual void release_publish();
    /**
    * publish stream event notify.
    * @param _req the request from client, the source will deep copy it,
    *         for when reload the request of client maybe invalid.
    */
    virtual int on_publish();
    virtual void on_unpublish();
// consumer methods
public:
    virtual int create_consumer(SrsConsumer*& consumer);
    virtual void on_consumer_destroy(SrsConsumer* consumer);
    virtual void set_cache(bool enabled);
// internal
public:
    // for edge, when play edge stream, check the state
    virtual int on_edge_start_play();
    // for edge, when publish edge stream, check the state
    virtual int on_edge_start_publish();
    // for edge, proxy the publish
    virtual int on_edge_proxy_publish(SrsMessage* msg);
    // for edge, proxy stop publish
    virtual void on_edge_proxy_unpublish();
private:
    virtual int create_forwarders();
    virtual void destroy_forwarders();
};

#endif
