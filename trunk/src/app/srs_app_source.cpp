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

#include <srs_app_source.hpp>

#include <sstream>
#include <algorithm>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_protocol_stack.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_config.hpp>
#include <srs_app_encoder.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_app_dvr.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_edge.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_avc_aac.hpp>
#include <srs_protocol_msg_array.hpp>

#define CONST_MAX_JITTER_MS         500
#define DEFAULT_FRAME_TIME_MS         40

// for 26ms per audio packet,
// 115 packets is 3s.
#define __SRS_PURE_AUDIO_GUESS_COUNT 115

int _srs_time_jitter_string2int(std::string time_jitter)
{
    if (time_jitter == "full") {
        return SrsRtmpJitterAlgorithmFULL;
    } else if (time_jitter == "zero") {
        return SrsRtmpJitterAlgorithmZERO;
    } else {
        return SrsRtmpJitterAlgorithmOFF;
    }
}

SrsRtmpJitter::SrsRtmpJitter()
{
    last_pkt_correct_time = last_pkt_time = 0;
}

SrsRtmpJitter::~SrsRtmpJitter()
{
}

int SrsRtmpJitter::correct(SrsSharedPtrMessage* msg, int tba, int tbv, SrsRtmpJitterAlgorithm ag)
{
    int ret = ERROR_SUCCESS;
    
    // for performance issue
    if (ag != SrsRtmpJitterAlgorithmFULL) {
        // all jitter correct features is disabled, ignore.
        if (ag == SrsRtmpJitterAlgorithmOFF) {
            return ret;
        }
    
        // start at zero, but donot ensure monotonically increasing.
        if (ag == SrsRtmpJitterAlgorithmZERO) {
            // for the first time, last_pkt_correct_time is zero.
            // while when timestamp overflow, the timestamp become smaller, reset the last_pkt_correct_time.
            if (last_pkt_correct_time <= 0 || last_pkt_correct_time > msg->header.timestamp) {
                last_pkt_correct_time = msg->header.timestamp;
            }
            msg->header.timestamp -= last_pkt_correct_time;
            return ret;
        }
        
        // other algorithm, ignore.
        return ret;
    }
    
    // full jitter algorithm, do jitter correct.
    
    // set to 0 for metadata.
    if (!msg->header.is_audio() && !msg->header.is_video()) {
        msg->header.timestamp = 0;
        return ret;
    }
    
    int sample_rate = tba;
    int frame_rate = tbv;
    
    /**
    * we use a very simple time jitter detect/correct algorithm:
    * 1. delta: ensure the delta is positive and valid,
    *     we set the delta to DEFAULT_FRAME_TIME_MS,
    *     if the delta of time is nagative or greater than CONST_MAX_JITTER_MS.
    * 2. last_pkt_time: specifies the original packet time,
    *     is used to detect next jitter.
    * 3. last_pkt_correct_time: simply add the positive delta, 
    *     and enforce the time monotonically.
    */
    int64_t time = msg->header.timestamp;
    int64_t delta = time - last_pkt_time;

    // if jitter detected, reset the delta.
    if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
        // calc the right diff by audio sample rate
        if (msg->header.is_audio() && sample_rate > 0) {
            delta = (int64_t)(delta * 1000.0 / sample_rate);
        } else if (msg->header.is_video() && frame_rate > 0) {
            delta = (int64_t)(delta * 1.0 / frame_rate);
        } else {
            delta = DEFAULT_FRAME_TIME_MS;
        }

        // sometimes, the time is absolute time, so correct it again.
        if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
            delta = DEFAULT_FRAME_TIME_MS;
        }
        
        srs_info("jitter detected, last_pts=%"PRId64", pts=%"PRId64", diff=%"PRId64", last_time=%"PRId64", time=%"PRId64", diff=%"PRId64"",
            last_pkt_time, time, time - last_pkt_time, last_pkt_correct_time, last_pkt_correct_time + delta, delta);
    } else {
        srs_verbose("timestamp no jitter. time=%"PRId64", last_pkt=%"PRId64", correct_to=%"PRId64"", 
            time, last_pkt_time, last_pkt_correct_time + delta);
    }
    
    last_pkt_correct_time = srs_max(0, last_pkt_correct_time + delta);
    
    msg->header.timestamp = last_pkt_correct_time;
    last_pkt_time = time;
    
    return ret;
}

int SrsRtmpJitter::get_time()
{
    return (int)last_pkt_correct_time;
}

SrsMessageQueue::SrsMessageQueue()
{
    queue_size_ms = 0;
    av_start_time = av_end_time = -1;
}

SrsMessageQueue::~SrsMessageQueue()
{
    clear();
}

void SrsMessageQueue::set_queue_size(double queue_size)
{
    queue_size_ms = (int)(queue_size * 1000);
}

int SrsMessageQueue::enqueue(SrsSharedPtrMessage* msg, bool* is_overflow)
{
    int ret = ERROR_SUCCESS;
    
    if (msg->header.is_audio() || msg->header.is_video()) {
        if (av_start_time == -1) {
            av_start_time = msg->header.timestamp;
        }
        
        av_end_time = msg->header.timestamp;
    }
    
    msgs.push_back(msg);

    while (av_end_time - av_start_time > queue_size_ms) {
        // notice the caller queue already overflow and shrinked.
        if (is_overflow) {
            *is_overflow = true;
        }
        
        shrink();
    }
    
    return ret;
}

int SrsMessageQueue::dump_packets(int max_count, SrsMessage** pmsgs, int& count)
{
    int ret = ERROR_SUCCESS;
    
    if (msgs.empty()) {
        return ret;
    }
    
    srs_assert(max_count > 0);
    count = srs_min(max_count, (int)msgs.size());
    
    for (int i = 0; i < count; i++) {
        pmsgs[i] = msgs[i];
    }
    
    SrsMessage* last = msgs[count - 1];
    av_start_time = last->header.timestamp;
    
    if (count == (int)msgs.size()) {
        // the pmsgs is big enough and clear msgs at most time.
        msgs.clear();
    } else {
        // erase some vector elements may cause memory copy,
        // maybe can use more efficient vector.swap to avoid copy.
        // @remark for the pmsgs is big enough, for instance, SRS_PERF_MW_MSGS 128,
        //      the rtmp play client will get 128msgs once, so this branch rarely execute.
        msgs.erase(msgs.begin(), msgs.begin() + count);
    }
    
    return ret;
}

void SrsMessageQueue::shrink()
{
    int iframe_index = -1;
    
    // issue the first iframe.
    // skip the first frame, whatever the type of it,
    // for when we shrinked, the first is the iframe,
    // we will directly remove the gop next time.
    for (int i = 1; i < (int)msgs.size(); i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        if (msg->header.is_video()) {
            if (SrsFlvCodec::video_is_keyframe(msg->payload, msg->size)) {
                // the max frame index to remove.
                iframe_index = i;
                
                // set the start time, we will remove until this frame.
                av_start_time = msg->header.timestamp;
                
                break;
            }
        }
    }
    
    // no iframe, for audio, clear the queue.
    // it is ok to clear for audio, for the shrink tell us the queue is full.
    // for video, we clear util the I-Frame, for the decoding must start from I-frame,
    // for audio, it's ok to clear any data, also we can clear the whole queue.
    // @see: https://github.com/winlinvip/simple-rtmp-server/issues/134
    if (iframe_index < 0) {
        clear();
        return;
    }
    
    srs_trace("shrink the cache queue, size=%d, removed=%d, max=%.2f", 
        (int)msgs.size(), iframe_index, queue_size_ms / 1000.0);
    
    // remove the first gop from the front
    for (int i = 0; i < iframe_index; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
    }
    msgs.erase(msgs.begin(), msgs.begin() + iframe_index);
}

void SrsMessageQueue::clear()
{
    std::vector<SrsSharedPtrMessage*>::iterator it;

    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        srs_freep(msg);
    }

    msgs.clear();
    
    av_start_time = av_end_time = -1;
}

SrsConsumer::SrsConsumer(SrsSource* _source)
{
    source = _source;
    paused = false;
    jitter = new SrsRtmpJitter();
    queue = new SrsMessageQueue();
    should_update_source_id = false;
    
    mw_wait = st_cond_new();
    mw_min_msgs = 0;
    mw_duration = 0;
    mw_waiting = false;
    
    mw_cache = new SrsMessageArray(SRS_PERF_MW_MSGS);
    mw_count = 0;
    mw_first_pkt = mw_last_pkt = 0;
}

SrsConsumer::~SrsConsumer()
{
    if (mw_cache) {
        mw_cache->free(mw_count);
        mw_count = 0;
    }
    srs_freep(mw_cache);
    
    source->on_consumer_destroy(this);
    srs_freep(jitter);
    srs_freep(queue);
    st_cond_destroy(mw_wait);
}

void SrsConsumer::set_queue_size(double queue_size)
{
    queue->set_queue_size(queue_size);
}

void SrsConsumer::update_source_id()
{
    should_update_source_id = true;
}

int SrsConsumer::get_time()
{
    return jitter->get_time();
}

int SrsConsumer::enqueue(SrsSharedPtrMessage* msg, bool atc, int tba, int tbv, SrsRtmpJitterAlgorithm ag)
{
    int ret = ERROR_SUCCESS;
    
    if (!atc) {
        if ((ret = jitter->correct(msg, tba, tbv, ag)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    }
    
    // use fast cache if available
    if (mw_count < mw_cache->max) {
        // update fast cache timestamps
        if (mw_count == 0) {
            mw_first_pkt = msg->header.timestamp;
        }
        mw_last_pkt = msg->header.timestamp;
        
        mw_cache->msgs[mw_count++] = msg;
    } else{
        // fast cache is full, use queue.
        bool is_overflow = false;
        if ((ret = queue->enqueue(msg, &is_overflow)) != ERROR_SUCCESS) {
            return ret;
        }
        // when overflow, clear cache and refresh the fast cache.
        if (is_overflow) {
            mw_cache->free(mw_count);
            if ((ret = dumps_queue_to_fast_cache()) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    // fire the mw when msgs is enough.
    if (mw_waiting) {
        // when fast cache not overflow, always flush.
        // so we donot care about the queue.
        bool fast_cache_overflow = mw_count >= mw_cache->max;
        int duration_ms = (int)(mw_last_pkt - mw_first_pkt);
        bool match_min_msgs = mw_count > mw_min_msgs;
        
        // when fast cache overflow, or duration ok, signal to flush.
        if (fast_cache_overflow || (match_min_msgs && duration_ms > mw_duration)) {
            st_cond_signal(mw_wait);
            mw_waiting = false;
        }
    }
    
    return ret;
}

int SrsConsumer::dump_packets(SrsMessageArray* msgs, int* count)
{
    int ret =ERROR_SUCCESS;
    
    srs_assert(msgs->max > 0);
    
    if (should_update_source_id) {
        srs_trace("update source_id=%d[%d]", source->source_id(), source->source_id());
        should_update_source_id = false;
    }
    
    // paused, return nothing.
    if (paused) {
        return ret;
    }
    
    // only dumps an whole array to msgs.
    for (int i = 0; i < mw_count; i++) {
        msgs->msgs[i] = mw_cache->msgs[i];
    }
    *count = mw_count;
    
    // when fast cache is not filled, 
    // we donot check the queue, direclty zero fast cache.
    if (mw_count < mw_cache->max) {
        mw_count = 0;
        mw_first_pkt = mw_last_pkt = 0;
        return ret;
    }
    
    return dumps_queue_to_fast_cache();
}

void SrsConsumer::wait(int nb_msgs, int duration)
{
    mw_min_msgs = nb_msgs;
    mw_duration = duration;
    
    // when fast cache not overflow, always flush.
    // so we donot care about the queue.
    bool fast_cache_overflow = mw_count >= mw_cache->max;
    int duration_ms = (int)(mw_last_pkt - mw_first_pkt);
    bool match_min_msgs = mw_count > mw_min_msgs;
    
    // when fast cache overflow, or duration ok, signal to flush.
    if (fast_cache_overflow || (match_min_msgs && duration_ms > mw_duration)) {
        return;
    }
    
    // the enqueue will notify this cond.
    mw_waiting = true;
    // wait for msgs to incoming.
    st_cond_wait(mw_wait);
}

int SrsConsumer::on_play_client_pause(bool is_pause)
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("stream consumer change pause state %d=>%d", paused, is_pause);
    paused = is_pause;
    
    return ret;
}

int SrsConsumer::dumps_queue_to_fast_cache()
{
    int ret =ERROR_SUCCESS;
    
    // fill fast cache with queue.
    if ((ret = queue->dump_packets(mw_cache->max, mw_cache->msgs, mw_count)) != ERROR_SUCCESS) {
        return ret;
    }
    // set the timestamp when got message.
    if (mw_count > 0) {
        SrsMessage* first_msg = mw_cache->msgs[0];
        mw_first_pkt = first_msg->header.timestamp;
        
        SrsMessage* last_msg = mw_cache->msgs[mw_count - 1];
        mw_last_pkt = last_msg->header.timestamp;
    }
    
    return ret;
}

SrsGopCache::SrsGopCache()
{
    cached_video_count = 0;
    enable_gop_cache = true;
    audio_after_last_video_count = 0;
}

SrsGopCache::~SrsGopCache()
{
    clear();
}

void SrsGopCache::set(bool enabled)
{
    enable_gop_cache = enabled;
    
    if (!enabled) {
        srs_info("disable gop cache, clear %d packets.", (int)gop_cache.size());
        clear();
        return;
    }
    
    srs_info("enable gop cache");
}

int SrsGopCache::cache(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if (!enable_gop_cache) {
        srs_verbose("gop cache is disabled.");
        return ret;
    }

    // disable gop cache when not h.264
    if (!SrsFlvCodec::video_is_h264(msg->payload, msg->size)) {
        srs_info("gop donot cache video for none h.264");
        return ret;
    }
    
    // got video, update the video count if acceptable
    if (msg->header.is_video()) {
        cached_video_count++;
        audio_after_last_video_count = 0;
    }
    
    // no acceptable video or pure audio, disable the cache.
    if (pure_audio()) {
        srs_verbose("ignore any frame util got a h264 video frame.");
        return ret;
    }
    
    // ok, gop cache enabled, and got an audio.
    if (msg->header.is_audio()) {
        audio_after_last_video_count++;
    }
    
    // clear gop cache when pure audio count overflow
    if (audio_after_last_video_count > __SRS_PURE_AUDIO_GUESS_COUNT) {
        srs_warn("clear gop cache for guess pure audio overflow");
        clear();
        return ret;
    }
    
    // clear gop cache when got key frame
    if (msg->header.is_video() && SrsFlvCodec::video_is_keyframe(msg->payload, msg->size)) {
        srs_info("clear gop cache when got keyframe. vcount=%d, count=%d",
            cached_video_count, (int)gop_cache.size());
            
        clear();
        
        // curent msg is video frame, so we set to 1.
        cached_video_count = 1;
    }
    
    // cache the frame.
    gop_cache.push_back(msg->copy());
    
    return ret;
}

void SrsGopCache::clear()
{
    std::vector<SrsSharedPtrMessage*>::iterator it;
    for (it = gop_cache.begin(); it != gop_cache.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        srs_freep(msg);
    }
    gop_cache.clear();

    cached_video_count = 0;
    audio_after_last_video_count = 0;
}
    
int SrsGopCache::dump(SrsConsumer* consumer, bool atc, int tba, int tbv, SrsRtmpJitterAlgorithm jitter_algorithm)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsSharedPtrMessage*>::iterator it;
    for (it = gop_cache.begin(); it != gop_cache.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        SrsSharedPtrMessage* copy = msg->copy();
        if ((ret = consumer->enqueue(copy, atc, tba, tbv, jitter_algorithm)) != ERROR_SUCCESS) {
            srs_error("dispatch cached gop failed. ret=%d", ret);
            return ret;
        }
    }
    srs_trace("dispatch cached gop success. count=%d, duration=%d", (int)gop_cache.size(), consumer->get_time());
    
    return ret;
}

bool SrsGopCache::empty()
{
    return gop_cache.empty();
}

int64_t SrsGopCache::start_time()
{
    if (empty()) {
        return 0;
    }
    
    SrsSharedPtrMessage* msg = gop_cache[0];
    srs_assert(msg);
    
    return msg->header.timestamp;
}

bool SrsGopCache::pure_audio()
{
    return cached_video_count == 0;
}

std::map<std::string, SrsSource*> SrsSource::pool;

int SrsSource::find(SrsRequest* req, SrsSource** ppsource)
{
    int ret = ERROR_SUCCESS;
    
    string stream_url = req->get_stream_url();
    string vhost = req->vhost;
    
    if (pool.find(stream_url) == pool.end()) {
        SrsSource* source = new SrsSource(req);
        if ((ret = source->initialize()) != ERROR_SUCCESS) {
            srs_freep(source);
            return ret;
        }
        
        pool[stream_url] = source;
        srs_info("create new source for url=%s, vhost=%s", stream_url.c_str(), vhost.c_str());
    }
    
    // we always update the request of resource, 
    // for origin auth is on, the token in request maybe invalid,
    // and we only need to update the token of request, it's simple.
    if (true) {
        SrsSource* source = pool[stream_url];
        source->_req->update_auth(req);
        *ppsource = source;
    }
    
    return ret;
}

void SrsSource::destroy()
{
    std::map<std::string, SrsSource*>::iterator it;
    for (it = pool.begin(); it != pool.end(); ++it) {
        SrsSource* source = it->second;
        srs_freep(source);
    }
    pool.clear();
}

SrsSource::SrsSource(SrsRequest* req)
{
    _req = req->copy();
    jitter_algorithm = SrsRtmpJitterAlgorithmOFF;
    
#ifdef SRS_AUTO_HLS
    hls = new SrsHls(this);
#endif
#ifdef SRS_AUTO_DVR
    dvr = new SrsDvr(this);
#endif
#ifdef SRS_AUTO_TRANSCODE
    encoder = new SrsEncoder();
#endif
    
    cache_metadata = cache_sh_video = cache_sh_audio = NULL;
    
    frame_rate = sample_rate = 0;
    _can_publish = true;
    _source_id = -1;
    
    play_edge = new SrsPlayEdge();
    publish_edge = new SrsPublishEdge();
    gop_cache = new SrsGopCache();
    aggregate_stream = new SrsStream();
    
    _srs_config->subscribe(this);
    atc = _srs_config->get_atc(_req->vhost);
}

SrsSource::~SrsSource()
{
    _srs_config->unsubscribe(this);
    
    // never free the consumers, 
    // for all consumers are auto free.
    consumers.clear();

    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            srs_freep(forwarder);
        }
        forwarders.clear();
    }
    
    srs_freep(cache_metadata);
    srs_freep(cache_sh_video);
    srs_freep(cache_sh_audio);
    
    srs_freep(play_edge);
    srs_freep(publish_edge);
    srs_freep(gop_cache);
    srs_freep(aggregate_stream);
    
#ifdef SRS_AUTO_HLS
    srs_freep(hls);
#endif
#ifdef SRS_AUTO_DVR
    srs_freep(dvr);
#endif
#ifdef SRS_AUTO_TRANSCODE
    srs_freep(encoder);
#endif

    srs_freep(_req);
}

int SrsSource::initialize()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->initialize(_req)) != ERROR_SUCCESS) {
        return ret;
    }
#endif

    if ((ret = play_edge->initialize(this, _req)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = publish_edge->initialize(this, _req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    double queue_size = _srs_config->get_queue_length(_req->vhost);
    publish_edge->set_queue_size(queue_size);
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_time_jitter(_req->vhost);
    
    return ret;
}

int SrsSource::on_reload_vhost_atc(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
    // atc changed.
    bool enabled_atc = _srs_config->get_atc(vhost);
    
    srs_warn("vhost %s atc changed to %d, connected client may corrupt.", 
        vhost.c_str(), enabled_atc);
    
    gop_cache->clear();
    
    return ret;
}

int SrsSource::on_reload_vhost_gop_cache(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
    // gop cache changed.
    bool enabled_cache = _srs_config->get_gop_cache(vhost);
    
    srs_trace("vhost %s gop_cache changed to %d, source url=%s", 
        vhost.c_str(), enabled_cache, _req->get_stream_url().c_str());
    
    set_cache(enabled_cache);
    
    return ret;
}

int SrsSource::on_reload_vhost_queue_length(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }

    double queue_size = _srs_config->get_queue_length(_req->vhost);
    
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            consumer->set_queue_size(queue_size);
        }

        srs_trace("consumers reload queue size success.");
    }
    
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            forwarder->set_queue_size(queue_size);
        }

        srs_trace("forwarders reload queue size success.");
    }
    
    if (true) {
        publish_edge->set_queue_size(queue_size);
        srs_trace("publish_edge reload queue size success.");
    }
    
    return ret;
}

int SrsSource::on_reload_vhost_time_jitter(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_time_jitter(_req->vhost);
    
    return ret;
}

int SrsSource::on_reload_vhost_forward(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }

    // forwarders
    destroy_forwarders();
    if ((ret = create_forwarders()) != ERROR_SUCCESS) {
        srs_error("create forwarders failed. ret=%d", ret);
        return ret;
    }

    srs_trace("vhost %s forwarders reload success", vhost.c_str());
    
    return ret;
}

int SrsSource::on_reload_vhost_hls(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
#ifdef SRS_AUTO_HLS
    hls->on_unpublish();
    if ((ret = hls->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("hls publish failed. ret=%d", ret);
        return ret;
    }
    srs_trace("vhost %s hls reload success", vhost.c_str());
#endif
    
    return ret;
}

int SrsSource::on_reload_vhost_dvr(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
#ifdef SRS_AUTO_DVR
    // cleanup dvr
    dvr->on_unpublish();

    // reinitialize the dvr, update plan.
    if ((ret = dvr->initialize(_req)) != ERROR_SUCCESS) {
        return ret;
    }

    // start to publish by new plan.
    if ((ret = dvr->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("dvr publish failed. ret=%d", ret);
        return ret;
    }
    
    srs_trace("vhost %s dvr reload success", vhost.c_str());
#endif
    
    return ret;
}

int SrsSource::on_reload_vhost_transcode(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
#ifdef SRS_AUTO_TRANSCODE
    encoder->on_unpublish();
    if ((ret = encoder->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("start encoder failed. ret=%d", ret);
        return ret;
    }
    srs_trace("vhost %s transcode reload success", vhost.c_str());
#endif
    
    return ret;
}

int SrsSource::on_forwarder_start(SrsForwarder* forwarder)
{
    int ret = ERROR_SUCCESS;
        
    // feed the forwarder the metadata/sequence header,
    // when reload to enable the forwarder.
    if (cache_metadata && (ret = forwarder->on_meta_data(cache_metadata->copy())) != ERROR_SUCCESS) {
        srs_error("forwarder process onMetaData message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_video && (ret = forwarder->on_video(cache_sh_video->copy())) != ERROR_SUCCESS) {
        srs_error("forwarder process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = forwarder->on_audio(cache_sh_audio->copy())) != ERROR_SUCCESS) {
        srs_error("forwarder process audio sequence header message failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsSource::on_hls_start()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HLS
    // feed the hls the metadata/sequence header,
    // when reload to start hls, hls will never get the sequence header in stream,
    // use the SrsSource.on_hls_start to push the sequence header to HLS.
    // TODO: maybe need to decode the metadata?
    if (cache_sh_video && (ret = hls->on_video(cache_sh_video->copy())) != ERROR_SUCCESS) {
        srs_error("hls process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = hls->on_audio(cache_sh_audio->copy())) != ERROR_SUCCESS) {
        srs_error("hls process audio sequence header message failed. ret=%d", ret);
        return ret;
    }
#endif
    
    return ret;
}

int SrsSource::on_dvr_request_sh()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_DVR
    // feed the dvr the metadata/sequence header,
    // when reload to start dvr, dvr will never get the sequence header in stream,
    // use the SrsSource.on_dvr_request_sh to push the sequence header to DVR.
    if (cache_metadata) {
        char* payload = cache_metadata->payload;
        int size = cache_metadata->size;
        
        SrsStream stream;
        if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
            srs_error("dvr decode metadata stream failed. ret=%d", ret);
            return ret;
        }
        
        SrsOnMetaDataPacket pkt;
        if ((ret = pkt.decode(&stream)) != ERROR_SUCCESS) {
            srs_error("dvr decode metadata packet failed.");
            return ret;
        }
        
        if ((ret = dvr->on_meta_data(&pkt)) != ERROR_SUCCESS) {
            srs_error("dvr process onMetaData message failed. ret=%d", ret);
            return ret;
        }
    }
    
    if (cache_sh_video && (ret = dvr->on_video(cache_sh_video->copy())) != ERROR_SUCCESS) {
        srs_error("dvr process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = dvr->on_audio(cache_sh_audio->copy())) != ERROR_SUCCESS) {
        srs_error("dvr process audio sequence header message failed. ret=%d", ret);
        return ret;
    }
#endif
    
    return ret;
}

int SrsSource::on_source_id_changed(int id)
{
    int ret = ERROR_SUCCESS;
    
    if (_source_id == id) {
        return ret;
    }
    
    _source_id = id;
    
    // notice all consumer
    std::vector<SrsConsumer*>::iterator it;
    for (it = consumers.begin(); it != consumers.end(); ++it) {
        SrsConsumer* consumer = *it;
        consumer->update_source_id();
    }
    
    return ret;
}

int SrsSource::source_id()
{
    return _source_id;
}

bool SrsSource::can_publish()
{
    return _can_publish;
}

int SrsSource::on_meta_data(SrsMessage* msg, SrsOnMetaDataPacket* metadata)
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HLS
    if (metadata && (ret = hls->on_meta_data(metadata->metadata)) != ERROR_SUCCESS) {
        srs_error("hls process onMetaData message failed. ret=%d", ret);
        return ret;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if (metadata && (ret = dvr->on_meta_data(metadata)) != ERROR_SUCCESS) {
        srs_error("dvr process onMetaData message failed. ret=%d", ret);
        return ret;
    }
#endif

    SrsAmf0Any* prop = NULL;
    
    // generate metadata info to print
    std::stringstream ss;
    if ((prop = metadata->metadata->ensure_property_number("width")) != NULL) {
        ss << ", width=" << (int)prop->to_number();
    }
    if ((prop = metadata->metadata->ensure_property_number("height")) != NULL) {
        ss << ", height=" << (int)prop->to_number();
    }
    if ((prop = metadata->metadata->ensure_property_number("videocodecid")) != NULL) {
        ss << ", vcodec=" << (int)prop->to_number();
    }
    if ((prop = metadata->metadata->ensure_property_number("audiocodecid")) != NULL) {
        ss << ", acodec=" << (int)prop->to_number();
    }
    
    // add server info to metadata
    metadata->metadata->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
    metadata->metadata->set("srs_primary", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY));
    metadata->metadata->set("srs_authors", SrsAmf0Any::str(RTMP_SIG_SRS_AUTHROS));
    
    // version, for example, 1.0.0
    // add version to metadata, please donot remove it, for debug.
    metadata->metadata->set("server_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    
    if ((prop = metadata->metadata->get_property("audiosamplerate")) != NULL) {
        if (prop->is_number()) {
            sample_rate = (int)prop->to_number();
        }
    }
    if ((prop = metadata->metadata->get_property("framerate")) != NULL) {
        if (prop->is_number()) {
            frame_rate = (int)prop->to_number();
        }
    }
    
    // if allow atc_auto and bravo-atc detected, open atc for vhost.
    atc = _srs_config->get_atc(_req->vhost);
    if (_srs_config->get_atc_auto(_req->vhost)) {
        if ((prop = metadata->metadata->get_property("bravo_atc")) != NULL) {
            if (prop->is_string() && prop->to_str() == "true") {
                atc = true;
            }
        }
    }
    
    // encode the metadata to payload
    int size = 0;
    char* payload = NULL;
    if ((ret = metadata->encode(size, payload)) != ERROR_SUCCESS) {
        srs_error("encode metadata error. ret=%d", ret);
        srs_freep(payload);
        return ret;
    }
    srs_verbose("encode metadata success.");
    
    if (size <= 0) {
        srs_warn("ignore the invalid metadata. size=%d", size);
        return ret;
    }
    
    // create a shared ptr message.
    srs_freep(cache_metadata);
    cache_metadata = new SrsSharedPtrMessage();
    
    // dump message to shared ptr message.
    // the payload/size managed by cache_metadata, user should not free it.
    if ((ret = cache_metadata->create(&msg->header, payload, size)) != ERROR_SUCCESS) {
        srs_error("initialize the cache metadata failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr metadata success.");
    
    // copy to all consumer
    if (true) {
        std::vector<SrsConsumer*>::iterator it;
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsConsumer* consumer = *it;
            SrsSharedPtrMessage* copy = cache_metadata->copy();
            if ((ret = consumer->enqueue(copy, atc, sample_rate, frame_rate, jitter_algorithm)) != ERROR_SUCCESS) {
                srs_error("dispatch the metadata failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("got metadata%s", ss.str().c_str());
    }
    
    // copy to all forwarders
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_meta_data(cache_metadata->copy())) != ERROR_SUCCESS) {
                srs_error("forwarder process onMetaData message failed. ret=%d", ret);
                return ret;
            }
        }
    }
    
    return ret;
}

int SrsSource::on_audio(SrsMessage* __audio)
{
    int ret = ERROR_SUCCESS;
    
    // convert __audio to msg, user should not use __audio again.
    // the payload is transfer to msg, and set to NULL in __audio.
    SrsSharedPtrMessage msg;
    if ((ret = msg.create(__audio)) != ERROR_SUCCESS) {
        srs_error("initialize the audio failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr audio success.");
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_audio(msg.copy())) != ERROR_SUCCESS) {
        srs_warn("hls process audio message failed, ignore and disable hls. ret=%d", ret);
        
        // unpublish, ignore ret.
        hls->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_audio(msg.copy())) != ERROR_SUCCESS) {
        srs_warn("dvr process audio message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        dvr->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
    // copy to all consumer
    if (true) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsConsumer* consumer = consumers.at(i);
            SrsSharedPtrMessage* copy = msg.copy();
            if ((ret = consumer->enqueue(copy, atc, sample_rate, frame_rate, jitter_algorithm)) != ERROR_SUCCESS) {
                srs_error("dispatch the audio failed. ret=%d", ret);
                return ret;
            }
        }
        srs_info("dispatch audio success.");
    }
    
    // copy to all forwarders.
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_audio(msg.copy())) != ERROR_SUCCESS) {
                srs_error("forwarder process audio message failed. ret=%d", ret);
                return ret;
            }
        }
    }

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (SrsFlvCodec::audio_is_sequence_header(msg.payload, msg.size)) {
        srs_freep(cache_sh_audio);
        cache_sh_audio = msg.copy();
        
        // parse detail audio codec
        SrsAvcAacCodec codec;
        SrsCodecSample sample;
        if ((ret = codec.audio_aac_demux(msg.payload, msg.size, &sample)) != ERROR_SUCCESS) {
            srs_error("codec demux audio failed. ret=%d", ret);
            return ret;
        }
        
        static int flv_sample_sizes[] = {8, 16, 0};
        static int flv_sound_types[] = {1, 2, 0};
        srs_trace("%dB audio sh, "
            "codec(%d, profile=%d, %dchannels, %dkbps, %dHZ), "
            "flv(%dbits, %dchannels, %dHZ)", 
            msg.header.payload_length, codec.audio_codec_id,
            codec.aac_profile, codec.aac_channels, 
            codec.audio_data_rate / 1000, aac_sample_rates[codec.aac_sample_rate], 
            flv_sample_sizes[sample.sound_size], flv_sound_types[sample.sound_type], 
            flv_sample_rates[sample.sound_rate]);
        return ret;
    }
    
    // cache the last gop packets
    if ((ret = gop_cache->cache(&msg)) != ERROR_SUCCESS) {
        srs_error("shrink gop cache failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("cache gop success.");
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (cache_sh_audio) {
            cache_sh_audio->header.timestamp = msg.header.timestamp;
        }
        if (cache_metadata) {
            cache_metadata->header.timestamp = msg.header.timestamp;
        }
    }
    
    return ret;
}

int SrsSource::on_video(SrsMessage* __video)
{
    int ret = ERROR_SUCCESS;
    
    // convert __video to msg, user should not use __video again.
    // the payload is transfer to msg, and set to NULL in __video.
    SrsSharedPtrMessage msg;
    if ((ret = msg.create(__video)) != ERROR_SUCCESS) {
        srs_error("initialize the video failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr video success.");
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_video(msg.copy())) != ERROR_SUCCESS) {
        srs_warn("hls process video message failed, ignore and disable hls. ret=%d", ret);
        
        // unpublish, ignore ret.
        hls->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_video(msg.copy())) != ERROR_SUCCESS) {
        srs_warn("dvr process video message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        dvr->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
    // copy to all consumer
    if (true) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsConsumer* consumer = consumers.at(i);
            SrsSharedPtrMessage* copy = msg.copy();
            if ((ret = consumer->enqueue(copy, atc, sample_rate, frame_rate, jitter_algorithm)) != ERROR_SUCCESS) {
                srs_error("dispatch the video failed. ret=%d", ret);
                return ret;
            }
        }
        srs_info("dispatch video success.");
    }

    // copy to all forwarders.
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_video(msg.copy())) != ERROR_SUCCESS) {
                srs_error("forwarder process video message failed. ret=%d", ret);
                return ret;
            }
        }
    }

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (SrsFlvCodec::video_is_sequence_header(msg.payload, msg.size)) {
        srs_freep(cache_sh_video);
        cache_sh_video = msg.copy();
        
        // parse detail audio codec
        SrsAvcAacCodec codec;
        SrsCodecSample sample;
        if ((ret = codec.video_avc_demux(msg.payload, msg.size, &sample)) != ERROR_SUCCESS) {
            srs_error("codec demux video failed. ret=%d", ret);
            return ret;
        }
        
        srs_trace("%dB video sh, "
            "codec(%d, profile=%d, level=%d, %dx%d, %dkbps, %dfps, %ds)",
            msg.header.payload_length, codec.video_codec_id,
            codec.avc_profile, codec.avc_level, codec.width, codec.height,
            codec.video_data_rate / 1000, codec.frame_rate, codec.duration);
        return ret;
    }

    // cache the last gop packets
    if ((ret = gop_cache->cache(&msg)) != ERROR_SUCCESS) {
        srs_error("gop cache msg failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("cache gop success.");
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (cache_sh_video) {
            cache_sh_video->header.timestamp = msg.header.timestamp;
        }
        if (cache_metadata) {
            cache_metadata->header.timestamp = msg.header.timestamp;
        }
    }
    
    return ret;
}

int SrsSource::on_aggregate(SrsMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    SrsStream* stream = aggregate_stream;
    if ((ret = stream->initialize(msg->payload, msg->size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    while (!stream->empty()) {
        if (!stream->require(1)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message type. ret=%d", ret);
            return ret;
        }
        int8_t type = stream->read_1bytes();
        
        if (!stream->require(3)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message size. ret=%d", ret);
            return ret;
        }
        int32_t data_size = stream->read_3bytes();
        
        if (data_size < 0) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message size(negative). ret=%d", ret);
            return ret;
        }
        
        if (!stream->require(3)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message time. ret=%d", ret);
            return ret;
        }
        int32_t timestamp = stream->read_3bytes();
        
        if (!stream->require(1)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message time(high). ret=%d", ret);
            return ret;
        }
        int32_t time_h = stream->read_1bytes();
        
        timestamp |= time_h<<24;
        timestamp &= 0x7FFFFFFF;
        
        if (!stream->require(3)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message stream_id. ret=%d", ret);
            return ret;
        }
        int32_t stream_id = stream->read_3bytes();
        
        if (data_size > 0 && !stream->require(data_size)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message data. ret=%d", ret);
            return ret;
        }
        
        // to common message.
        SrsCommonMessage __o;
        SrsMessage& o = __o;
        
        o.header.message_type = type;
        o.header.payload_length = data_size;
        o.header.timestamp_delta = timestamp;
        o.header.timestamp = timestamp;
        o.header.stream_id = stream_id;
        o.header.perfer_cid = msg->header.perfer_cid;

        if (data_size > 0) {
            o.size = data_size;
            o.payload = new char[o.size];
            stream->read_bytes(o.payload, o.size);
        }
        
        if (!stream->require(4)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message previous tag size. ret=%d", ret);
            return ret;
        }
        stream->read_4bytes();

        // process parsed message
        if (o.header.is_audio()) {
            if ((ret = on_audio(&o)) != ERROR_SUCCESS) {
                return ret;
            }
        } else if (o.header.is_video()) {
            if ((ret = on_video(&o)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    return ret;
}

int SrsSource::acquire_publish()
{
    int ret = ERROR_SUCCESS;
    
    if (!_can_publish) {
        ret = ERROR_SYSTEM_STREAM_BUSY;
        srs_warn("publish lock stream failed, ret=%d", ret);
        return ret;
    }
    
    _can_publish = false;
    
    return ret;
}

void SrsSource::release_publish()
{
    _can_publish = true;
}

int SrsSource::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // update the request object.
    srs_assert(_req);
    
    _can_publish = false;
    
    // whatever, the publish thread is the source or edge source,
    // save its id to srouce id.
    on_source_id_changed(_srs_context->get_id());
    
    // create forwarders
    if ((ret = create_forwarders()) != ERROR_SUCCESS) {
        srs_error("create forwarders failed. ret=%d", ret);
        return ret;
    }
    
    // TODO: FIXME: use initialize to set req.
#ifdef SRS_AUTO_TRANSCODE
    if ((ret = encoder->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("start encoder failed. ret=%d", ret);
        return ret;
    }
#endif
    
    // TODO: FIXME: use initialize to set req.
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("start hls failed. ret=%d", ret);
        return ret;
    }
#endif
    
    // TODO: FIXME: use initialize to set req.
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("start dvr failed. ret=%d", ret);
        return ret;
    }
#endif

    return ret;
}

void SrsSource::on_unpublish()
{
    // destroy all forwarders
    destroy_forwarders();

#ifdef SRS_AUTO_TRANSCODE
    encoder->on_unpublish();
#endif

#ifdef SRS_AUTO_HLS
    hls->on_unpublish();
#endif
    
#ifdef SRS_AUTO_DVR
    dvr->on_unpublish();
#endif

    gop_cache->clear();

    srs_freep(cache_metadata);
    frame_rate = sample_rate = 0;
    
    srs_freep(cache_sh_video);
    srs_freep(cache_sh_audio);
    
    srs_info("clear cache/metadata/sequence-headers when unpublish.");
    srs_trace("cleanup when unpublish");
    
    _can_publish = true;
    _source_id = -1;
}

 int SrsSource::create_consumer(SrsConsumer*& consumer)
{
    int ret = ERROR_SUCCESS;
    
    consumer = new SrsConsumer(this);
    consumers.push_back(consumer);
    
    double queue_size = _srs_config->get_queue_length(_req->vhost);
    consumer->set_queue_size(queue_size);
    
    // if atc, update the sequence header to gop cache time.
    if (atc && !gop_cache->empty()) {
        if (cache_metadata) {
            cache_metadata->header.timestamp = gop_cache->start_time();
        }
        if (cache_sh_video) {
            cache_sh_video->header.timestamp = gop_cache->start_time();
        }
        if (cache_sh_audio) {
            cache_sh_audio->header.timestamp = gop_cache->start_time();
        }
    }

    int tba = sample_rate;
    int tbv = frame_rate;
    SrsRtmpJitterAlgorithm ag = jitter_algorithm;
    
    // copy metadata.
    if (cache_metadata && (ret = consumer->enqueue(cache_metadata->copy(), atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        srs_error("dispatch metadata failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch metadata success");
    
    // copy sequence header
    if (cache_sh_video && (ret = consumer->enqueue(cache_sh_video->copy(), atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        srs_error("dispatch video sequence header failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch video sequence header success");
    
    if (cache_sh_audio && (ret = consumer->enqueue(cache_sh_audio->copy(), atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        srs_error("dispatch audio sequence header failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch audio sequence header success");
    
    // copy gop cache to client.
    if ((ret = gop_cache->dump(consumer, atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("create consumer, queue_size=%.2f, tba=%d, tbv=%d", queue_size, sample_rate, frame_rate);
    
    return ret;
}

void SrsSource::on_consumer_destroy(SrsConsumer* consumer)
{
    std::vector<SrsConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        consumers.erase(it);
    }
    srs_info("handle consumer destroy success.");
    
    if (consumers.empty()) {
        play_edge->on_all_client_stop();
    }
}

void SrsSource::set_cache(bool enabled)
{
    gop_cache->set(enabled);
}

int SrsSource::on_edge_start_play()
{
    return play_edge->on_client_play();
}

int SrsSource::on_edge_start_publish()
{
    return publish_edge->on_client_publish();
}

int SrsSource::on_edge_proxy_publish(SrsMessage* msg)
{
    return publish_edge->on_proxy_publish(msg);
}

void SrsSource::on_edge_proxy_unpublish()
{
    publish_edge->on_proxy_unpublish();
}

int SrsSource::create_forwarders()
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* conf = _srs_config->get_forward(_req->vhost);
    for (int i = 0; conf && i < (int)conf->args.size(); i++) {
        std::string forward_server = conf->args.at(i);
        
        SrsForwarder* forwarder = new SrsForwarder(this);
        forwarders.push_back(forwarder);
        
        // initialize the forwarder with request.
        if ((ret = forwarder->initialize(_req, forward_server)) != ERROR_SUCCESS) {
            return ret;
        }
    
        double queue_size = _srs_config->get_queue_length(_req->vhost);
        forwarder->set_queue_size(queue_size);
        
        if ((ret = forwarder->on_publish()) != ERROR_SUCCESS) {
            srs_error("start forwarder failed. "
                "vhost=%s, app=%s, stream=%s, forward-to=%s",
                _req->vhost.c_str(), _req->app.c_str(), _req->stream.c_str(),
                forward_server.c_str());
            return ret;
        }
    }

    return ret;
}

void SrsSource::destroy_forwarders()
{
    std::vector<SrsForwarder*>::iterator it;
    for (it = forwarders.begin(); it != forwarders.end(); ++it) {
        SrsForwarder* forwarder = *it;
        forwarder->on_unpublish();
        srs_freep(forwarder);
    }
    forwarders.clear();
}


