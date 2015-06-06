/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_config.hpp>
#include <srs_app_encoder.hpp>
#include <srs_rtmp_sdk.hpp>
#include <srs_app_dvr.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_edge.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_hds.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_utility.hpp>

#define CONST_MAX_JITTER_MS         500
#define DEFAULT_FRAME_TIME_MS         40

// for 26ms per audio packet,
// 115 packets is 3s.
#define SRS_PURE_AUDIO_GUESS_COUNT 115

// when got these videos or audios, mix ok.
#define SRS_MIX_CORRECT_MIX_AV 10

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
            if (last_pkt_correct_time <= 0 || last_pkt_correct_time > msg->timestamp) {
                last_pkt_correct_time = msg->timestamp;
            }
            msg->timestamp -= last_pkt_correct_time;
            return ret;
        }
        
        // other algorithm, ignore.
        return ret;
    }
    
    // full jitter algorithm, do jitter correct.
    
    // set to 0 for metadata.
    if (!msg->is_av()) {
        msg->timestamp = 0;
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
    int64_t time = msg->timestamp;
    int64_t delta = time - last_pkt_time;

    // if jitter detected, reset the delta.
    if (delta < 0 || delta > CONST_MAX_JITTER_MS) {
        // calc the right diff by audio sample rate
        if (msg->is_audio() && sample_rate > 0) {
            delta = (int64_t)(delta * 1000.0 / sample_rate);
        } else if (msg->is_video() && frame_rate > 0) {
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
    
    msg->timestamp = last_pkt_correct_time;
    last_pkt_time = time;
    
    return ret;
}

int SrsRtmpJitter::get_time()
{
    return (int)last_pkt_correct_time;
}

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
SrsFastVector::SrsFastVector()
{
    count = 0;
    nb_msgs = SRS_PERF_MW_MSGS * 8;
    msgs = new SrsSharedPtrMessage*[nb_msgs];
}

SrsFastVector::~SrsFastVector()
{
    free();
    srs_freep(msgs);
}

int SrsFastVector::size()
{
    return count;
}

int SrsFastVector::begin()
{
    return 0;
}

int SrsFastVector::end()
{
    return count;
}

SrsSharedPtrMessage** SrsFastVector::data()
{
    return msgs;
}

SrsSharedPtrMessage* SrsFastVector::at(int index)
{
    srs_assert(index < count);
    return msgs[index];
}

void SrsFastVector::clear()
{
    count = 0;
}

void SrsFastVector::erase(int _begin, int _end)
{
    srs_assert(_begin < _end);
    
    // move all erased to previous.
    for (int i = 0; i < count - _end; i++) {
        msgs[_begin + i] = msgs[_end + i];
    }
    
    // update the count.
    count -= _end - _begin;
}

void SrsFastVector::push_back(SrsSharedPtrMessage* msg)
{
    // increase vector.
    if (count >= nb_msgs) {
        int size = nb_msgs * 2;
        SrsSharedPtrMessage** buf = new SrsSharedPtrMessage*[size];
        for (int i = 0; i < nb_msgs; i++) {
            buf[i] = msgs[i];
        }
        srs_warn("fast vector incrase %d=>%d", nb_msgs, size);
        
        // use new array.
        srs_freep(msgs);
        msgs = buf;
        nb_msgs = size;
    }
    
    msgs[count++] = msg;
}

void SrsFastVector::free()
{
    for (int i = 0; i < count; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
    }
    count = 0;
}
#endif

SrsMessageQueue::SrsMessageQueue(bool ignore_shrink)
{
    _ignore_shrink = ignore_shrink;
    queue_size_ms = 0;
    av_start_time = av_end_time = -1;
}

SrsMessageQueue::~SrsMessageQueue()
{
    clear();
}

int SrsMessageQueue::size()
{
    return (int)msgs.size();
}

int SrsMessageQueue::duration()
{
    return (int)(av_end_time - av_start_time);
}

void SrsMessageQueue::set_queue_size(double queue_size)
{
    queue_size_ms = (int)(queue_size * 1000);
}

int SrsMessageQueue::enqueue(SrsSharedPtrMessage* msg, bool* is_overflow)
{
    int ret = ERROR_SUCCESS;
    
    if (msg->is_av()) {
        if (av_start_time == -1) {
            av_start_time = msg->timestamp;
        }
        
        av_end_time = msg->timestamp;
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

int SrsMessageQueue::dump_packets(int max_count, SrsSharedPtrMessage** pmsgs, int& count)
{
    int ret = ERROR_SUCCESS;
    
    int nb_msgs = (int)msgs.size();
    if (nb_msgs <= 0) {
        return ret;
    }
    
    srs_assert(max_count > 0);
    count = srs_min(max_count, nb_msgs);

    SrsSharedPtrMessage** omsgs = msgs.data();
    for (int i = 0; i < count; i++) {
        pmsgs[i] = omsgs[i];
    }
    
    SrsSharedPtrMessage* last = omsgs[count - 1];
    av_start_time = last->timestamp;
    
    if (count >= nb_msgs) {
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

int SrsMessageQueue::dump_packets(SrsConsumer* consumer, bool atc, int tba, int tbv, SrsRtmpJitterAlgorithm ag)
{
    int ret = ERROR_SUCCESS;
    
    int nb_msgs = (int)msgs.size();
    if (nb_msgs <= 0) {
        return ret;
    }

    SrsSharedPtrMessage** omsgs = msgs.data();
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = omsgs[i];
        if ((ret = consumer->enqueue(msg, atc, tba, tbv, ag)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

void SrsMessageQueue::shrink()
{
    SrsSharedPtrMessage* video_sh = NULL;
    SrsSharedPtrMessage* audio_sh = NULL;
    int msgs_size = (int)msgs.size();
    
    // remove all msg
    // igone the sequence header
    for (int i = 0; i < (int)msgs.size(); i++) {
        SrsSharedPtrMessage* msg = msgs.at(i);

        if (msg->is_video() && SrsFlvCodec::video_is_sequence_header(msg->payload, msg->size)) {
            srs_freep(video_sh);
            video_sh = msg;
            continue;
        }
        else if (msg->is_audio() && SrsFlvCodec::audio_is_sequence_header(msg->payload, msg->size)) {
            srs_freep(audio_sh);
            audio_sh = msg;
            continue;
        }

        srs_freep(msg);
    }
    msgs.clear();  

    // update av_start_time
    av_start_time = av_end_time;
    //push_back secquence header and update timestamp
    if (video_sh) {
        video_sh->timestamp = av_end_time;
        msgs.push_back(video_sh);
    }
    if (audio_sh) {
        audio_sh->timestamp = av_end_time;
        msgs.push_back(audio_sh);
    }
    
    if (_ignore_shrink) {
        srs_info("shrink the cache queue, size=%d, removed=%d, max=%.2f", 
            (int)msgs.size(), msgs_size - (int)msgs.size(), queue_size_ms / 1000.0);
    } else {
        srs_trace("shrink the cache queue, size=%d, removed=%d, max=%.2f", 
            (int)msgs.size(), msgs_size - (int)msgs.size(), queue_size_ms / 1000.0);
    }
}

void SrsMessageQueue::clear()
{
#ifndef SRS_PERF_QUEUE_FAST_VECTOR
    std::vector<SrsSharedPtrMessage*>::iterator it;

    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        srs_freep(msg);
    }
#else
    msgs.free();
#endif

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
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    mw_wait = st_cond_new();
    mw_min_msgs = 0;
    mw_duration = 0;
    mw_waiting = false;
#endif
}

SrsConsumer::~SrsConsumer()
{
    source->on_consumer_destroy(this);
    srs_freep(jitter);
    srs_freep(queue);
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    st_cond_destroy(mw_wait);
#endif
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

int SrsConsumer::enqueue(SrsSharedPtrMessage* shared_msg, bool atc, int tba, int tbv, SrsRtmpJitterAlgorithm ag)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = shared_msg->copy();

    if (!atc) {
        if ((ret = jitter->correct(msg, tba, tbv, ag)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    }
    
    if ((ret = queue->enqueue(msg, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    srs_verbose("enqueue msg, time=%"PRId64", size=%d, duration=%d, waiting=%d, min_msg=%d", 
        msg->timestamp, msg->size, queue->duration(), mw_waiting, mw_min_msgs);
        
    // fire the mw when msgs is enough.
    if (mw_waiting) {
        int duration_ms = queue->duration();
        bool match_min_msgs = queue->size() > mw_min_msgs;
        
        // when duration ok, signal to flush.
        if (match_min_msgs && duration_ms > mw_duration) {
            st_cond_signal(mw_wait);
            mw_waiting = false;
        }
    }
#endif
    
    return ret;
}

int SrsConsumer::dump_packets(SrsMessageArray* msgs, int& count)
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

    // pump msgs from queue.
    if ((ret = queue->dump_packets(msgs->max, msgs->msgs, count)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

#ifdef SRS_PERF_QUEUE_COND_WAIT
void SrsConsumer::wait(int nb_msgs, int duration)
{
    if (paused) {
        st_usleep(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
        return;
    }

    mw_min_msgs = nb_msgs;
    mw_duration = duration;

    int duration_ms = queue->duration();
    bool match_min_msgs = queue->size() > mw_min_msgs;
    
    // when duration ok, signal to flush.
    if (match_min_msgs && duration_ms > mw_duration) {
        return;
    }
    
    // the enqueue will notify this cond.
    mw_waiting = true;
    
    // use cond block wait for high performance mode.
    st_cond_wait(mw_wait);
}

void SrsConsumer::wakeup()
{
    if (mw_waiting) {
        st_cond_signal(mw_wait);
        mw_waiting = false;
    }
}
#endif

int SrsConsumer::on_play_client_pause(bool is_pause)
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("stream consumer change pause state %d=>%d", paused, is_pause);
    paused = is_pause;
    
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

int SrsGopCache::cache(SrsSharedPtrMessage* shared_msg)
{
    int ret = ERROR_SUCCESS;
    
    if (!enable_gop_cache) {
        srs_verbose("gop cache is disabled.");
        return ret;
    }

    // the gop cache know when to gop it.
    SrsSharedPtrMessage* msg = shared_msg;
    
    // got video, update the video count if acceptable
    if (msg->is_video()) {
        // drop video when not h.264
        if (!SrsFlvCodec::video_is_h264(msg->payload, msg->size)) {
            srs_info("gop cache drop video for none h.264");
            return ret;
        }
        
        cached_video_count++;
        audio_after_last_video_count = 0;
    }
    
    // no acceptable video or pure audio, disable the cache.
    if (pure_audio()) {
        srs_verbose("ignore any frame util got a h264 video frame.");
        return ret;
    }
    
    // ok, gop cache enabled, and got an audio.
    if (msg->is_audio()) {
        audio_after_last_video_count++;
    }
    
    // clear gop cache when pure audio count overflow
    if (audio_after_last_video_count > SRS_PURE_AUDIO_GUESS_COUNT) {
        srs_warn("clear gop cache for guess pure audio overflow");
        clear();
        return ret;
    }
    
    // clear gop cache when got key frame
    if (msg->is_video() && SrsFlvCodec::video_is_keyframe(msg->payload, msg->size)) {
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
        if ((ret = consumer->enqueue(msg, atc, tba, tbv, jitter_algorithm)) != ERROR_SUCCESS) {
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
    
    return msg->timestamp;
}

bool SrsGopCache::pure_audio()
{
    return cached_video_count == 0;
}

ISrsSourceHandler::ISrsSourceHandler()
{
}

ISrsSourceHandler::~ISrsSourceHandler()
{
}

std::map<std::string, SrsSource*> SrsSource::pool;

int SrsSource::create(SrsRequest* r, ISrsSourceHandler* h, ISrsHlsHandler* hh, SrsSource** pps)
{
    int ret = ERROR_SUCCESS;
    
    string stream_url = r->get_stream_url();
    string vhost = r->vhost;
    
    // should always not exists for create a source.
    srs_assert (pool.find(stream_url) == pool.end());

    SrsSource* source = new SrsSource();
    if ((ret = source->initialize(r, h, hh)) != ERROR_SUCCESS) {
        srs_freep(source);
        return ret;
    }
        
    pool[stream_url] = source;
    srs_info("create new source for url=%s, vhost=%s", stream_url.c_str(), vhost.c_str());
    
    *pps = source;
    
    return ret;
}

SrsSource* SrsSource::fetch(SrsRequest* r)
{
    SrsSource* source = NULL;
    
    string stream_url = r->get_stream_url();
    if (pool.find(stream_url) == pool.end()) {
        return NULL;
    }

    source = pool[stream_url];

    // we always update the request of resource, 
    // for origin auth is on, the token in request maybe invalid,
    // and we only need to update the token of request, it's simple.
    source->_req->update_auth(r);

    return source;
}

SrsSource* SrsSource::fetch(std::string vhost, std::string app, std::string stream)
{
    SrsSource* source = NULL;
	string stream_url = srs_generate_stream_url(vhost, app, stream);
    
    if (pool.find(stream_url) == pool.end()) {
        return NULL;
    }

    source = pool[stream_url];

    return source;
}

void SrsSource::dispose_all()
{
    std::map<std::string, SrsSource*>::iterator it;
    for (it = pool.begin(); it != pool.end(); ++it) {
        SrsSource* source = it->second;
        source->dispose();
    }
    return;
}

int SrsSource::cycle_all()
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: support remove dead source for a long time.
    std::map<std::string, SrsSource*>::iterator it;
    for (it = pool.begin(); it != pool.end(); ++it) {
        SrsSource* source = it->second;
        if ((ret = source->cycle()) != ERROR_SUCCESS) {
            return ret;
        }
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

SrsMixQueue::SrsMixQueue()
{
    nb_videos = 0;
    nb_audios = 0;
}

SrsMixQueue::~SrsMixQueue()
{
    clear();
}

void SrsMixQueue::clear()
{
    std::multimap<int64_t, SrsSharedPtrMessage*>::iterator it;
    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsSharedPtrMessage* msg = it->second;
        srs_freep(msg);
    }
    msgs.clear();
    
    nb_videos = 0;
    nb_audios = 0;
}

void SrsMixQueue::push(SrsSharedPtrMessage* msg)
{
    msgs.insert(std::make_pair(msg->timestamp, msg));
    
    if (msg->is_video()) {
        nb_videos++;
    } else {
        nb_audios++;
    }
}

SrsSharedPtrMessage* SrsMixQueue::pop()
{
    // when got 10+ videos or audios, mix ok.
    // when got 1 video and 1 audio, mix ok.
    if (nb_videos < SRS_MIX_CORRECT_MIX_AV && nb_audios < SRS_MIX_CORRECT_MIX_AV) {
        if (nb_videos < 1 || nb_audios < 1) {
            return NULL;
        }
    }
    
    // pop the first msg.
    std::multimap<int64_t, SrsSharedPtrMessage*>::iterator it = msgs.begin();
    SrsSharedPtrMessage* msg = it->second;
    msgs.erase(it);
    
    if (msg->is_video()) {
        nb_videos--;
    } else {
        nb_audios--;
    }
    
    return msg;
}

SrsSource::SrsSource()
{
    _req = NULL;
    jitter_algorithm = SrsRtmpJitterAlgorithmOFF;
    mix_correct = false;
    mix_queue = new SrsMixQueue();
    
#ifdef SRS_AUTO_HLS
    hls = new SrsHls();
#endif
#ifdef SRS_AUTO_DVR
    dvr = new SrsDvr();
#endif
#ifdef SRS_AUTO_TRANSCODE
    encoder = new SrsEncoder();
#endif
#ifdef SRS_AUTO_HDS
    hds = new SrsHds(this);
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
    atc = false;
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
    
    srs_freep(mix_queue);
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
#ifdef SRS_AUTO_HDS
    srs_freep(hds);
#endif

    srs_freep(_req);
}

void SrsSource::dispose()
{
#ifdef SRS_AUTO_HLS
    hls->dispose();
#endif
}

int SrsSource::cycle()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->cycle()) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
    return ret;
}

int SrsSource::initialize(SrsRequest* r, ISrsSourceHandler* h, ISrsHlsHandler* hh)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(h);
    srs_assert(hh);
    srs_assert(!_req);

    handler = h;
    _req = r->copy();
    atc = _srs_config->get_atc(_req->vhost);

#ifdef SRS_AUTO_HLS
    if ((ret = hls->initialize(this, hh)) != ERROR_SUCCESS) {
        return ret;
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->initialize(this, _req)) != ERROR_SUCCESS) {
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
    mix_correct = _srs_config->get_mix_correct(_req->vhost);
    
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

int SrsSource::on_reload_vhost_mix_correct(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (_req->vhost != vhost) {
        return ret;
    }
    
    bool v = _srs_config->get_mix_correct(_req->vhost);
    
    // when changed, clear the mix queue.
    if (v != mix_correct) {
        mix_queue->clear();
    }
    mix_correct = v;
    
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

int SrsSource::on_reload_vhost_hds(string vhost)
{
    int ret = ERROR_SUCCESS;

    if (_req->vhost != vhost) {
        return ret;
    }

#ifdef SRS_AUTO_HDS
    hds->on_unpublish();
    if ((ret = hds->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("hds publish failed. ret=%d", ret);
        return ret;
    }
    srs_trace("vhost %s hds reload success", vhost.c_str());
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
    if ((ret = dvr->initialize(this, _req)) != ERROR_SUCCESS) {
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
    if (cache_metadata && (ret = forwarder->on_meta_data(cache_metadata)) != ERROR_SUCCESS) {
        srs_error("forwarder process onMetaData message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_video && (ret = forwarder->on_video(cache_sh_video)) != ERROR_SUCCESS) {
        srs_error("forwarder process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = forwarder->on_audio(cache_sh_audio)) != ERROR_SUCCESS) {
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
    if (cache_sh_video && (ret = hls->on_video(cache_sh_video)) != ERROR_SUCCESS) {
        srs_error("hls process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = hls->on_audio(cache_sh_audio)) != ERROR_SUCCESS) {
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
    
    if (cache_sh_video && (ret = dvr->on_video(cache_sh_video)) != ERROR_SUCCESS) {
        srs_error("dvr process video sequence header message failed. ret=%d", ret);
        return ret;
    }
    if (cache_sh_audio && (ret = dvr->on_audio(cache_sh_audio)) != ERROR_SUCCESS) {
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

int SrsSource::on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata)
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
    metadata->metadata->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
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
            if ((ret = consumer->enqueue(cache_metadata, atc, sample_rate, frame_rate, jitter_algorithm)) != ERROR_SUCCESS) {
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
            if ((ret = forwarder->on_meta_data(cache_metadata)) != ERROR_SUCCESS) {
                srs_error("forwarder process onMetaData message failed. ret=%d", ret);
                return ret;
            }
        }
    }
    
    return ret;
}

int SrsSource::on_audio(SrsCommonMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    // convert shared_audio to msg, user should not use shared_audio again.
    // the payload is transfer to msg, and set to NULL in shared_audio.
    SrsSharedPtrMessage msg;
    if ((ret = msg.create(shared_audio)) != ERROR_SUCCESS) {
        srs_error("initialize the audio failed. ret=%d", ret);
        return ret;
    }
    srs_info("Audio dts=%"PRId64", size=%d", msg.timestamp, msg.size);
    
    // directly process the audio message.
    if (!mix_correct) {
        return on_audio_imp(&msg);
    }
    
    // insert msg to the queue.
    mix_queue->push(msg.copy());
    
    // fetch someone from mix queue.
    SrsSharedPtrMessage* m = mix_queue->pop();
    if (!m) {
        return ret;
    }
    
    // consume the monotonically increase message.
    if (m->is_audio()) {
        ret = on_audio_imp(m);
    } else {
        ret = on_video_imp(m);
    }
    srs_freep(m);
    
    return ret;
}

int SrsSource::on_audio_imp(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_info("Audio dts=%"PRId64", size=%d", msg->timestamp, msg->size);
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_audio(msg)) != ERROR_SUCCESS) {
        // apply the error strategy for hls.
        // @see https://github.com/simple-rtmp-server/srs/issues/264
        std::string hls_error_strategy = _srs_config->get_hls_on_error(_req->vhost);
        if (hls_error_strategy == SRS_CONF_DEFAULT_HLS_ON_ERROR_IGNORE) {
            srs_warn("hls process audio message failed, ignore and disable hls. ret=%d", ret);
            
            // unpublish, ignore ret.
            hls->on_unpublish();
            
            // ignore.
            ret = ERROR_SUCCESS;
        } else if (hls_error_strategy == SRS_CONF_DEFAULT_HLS_ON_ERROR_CONTINUE) {
            // compare the sequence header with audio, continue when it's actually an sequence header.
            if (ret == ERROR_HLS_DECODE_ERROR && cache_sh_audio && cache_sh_audio->size == msg->size) {
                srs_warn("the audio is actually a sequence header, ignore this packet.");
                ret = ERROR_SUCCESS;
            } else {
                srs_warn("hls continue audio failed. ret=%d", ret);
                return ret;
            }
        } else {
            srs_warn("hls disconnect publisher for audio error. ret=%d", ret);
            return ret;
        }
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_audio(msg)) != ERROR_SUCCESS) {
        srs_warn("dvr process audio message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        dvr->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif

#ifdef SRS_AUTO_HDS
    if ((ret = hds->on_audio(msg)) != ERROR_SUCCESS) {
        srs_warn("hds process audio message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        hds->on_unpublish();
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
    // copy to all consumer
    int nb_consumers = (int)consumers.size();
    if (nb_consumers > 0) {
        SrsConsumer** pconsumer = consumers.data();
        for (int i = 0; i < nb_consumers; i++) {
            SrsConsumer* consumer = pconsumer[i];
            if ((ret = consumer->enqueue(msg, atc, sample_rate, frame_rate, jitter_algorithm)) != ERROR_SUCCESS) {
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
            if ((ret = forwarder->on_audio(msg)) != ERROR_SUCCESS) {
                srs_error("forwarder process audio message failed. ret=%d", ret);
                return ret;
            }
        }
    }

    // cache the sequence header of aac, or first packet of mp3.
    // for example, the mp3 is used for hls to write the "right" audio codec.
    // TODO: FIXME: to refine the stream info system.
    bool is_aac_sequence_header = SrsFlvCodec::audio_is_sequence_header(msg->payload, msg->size);
    if (is_aac_sequence_header || !cache_sh_audio) {
        srs_freep(cache_sh_audio);
        cache_sh_audio = msg->copy();
    }

    // cache the sequence header if aac
    // donot cache the sequence header to gop_cache, return here.
    if (is_aac_sequence_header) {
        // parse detail audio codec
        SrsAvcAacCodec codec;
        SrsCodecSample sample;
        if ((ret = codec.audio_aac_demux(msg->payload, msg->size, &sample)) != ERROR_SUCCESS) {
            srs_error("source codec demux audio failed. ret=%d", ret);
            return ret;
        }
        
        static int flv_sample_sizes[] = {8, 16, 0};
        static int flv_sound_types[] = {1, 2, 0};
        
        // when got audio stream info.
        SrsStatistic* stat = SrsStatistic::instance();
        if ((ret = stat->on_audio_info(_req, SrsCodecAudioAAC, sample.sound_rate, sample.sound_type, codec.aac_object)) != ERROR_SUCCESS) {
            return ret;
        }
        
        srs_trace("%dB audio sh, "
            "codec(%d, profile=%s, %dchannels, %dkbps, %dHZ), "
            "flv(%dbits, %dchannels, %dHZ)", 
            msg->size, codec.audio_codec_id,
            srs_codec_aac_object2str(codec.aac_object).c_str(), codec.aac_channels, 
            codec.audio_data_rate / 1000, aac_sample_rates[codec.aac_sample_rate], 
            flv_sample_sizes[sample.sound_size], flv_sound_types[sample.sound_type], 
            flv_sample_rates[sample.sound_rate]);
        return ret;
    }
    
    // cache the last gop packets
    if ((ret = gop_cache->cache(msg)) != ERROR_SUCCESS) {
        srs_error("shrink gop cache failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("cache gop success.");
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (cache_sh_audio) {
            cache_sh_audio->timestamp = msg->timestamp;
        }
        if (cache_metadata) {
            cache_metadata->timestamp = msg->timestamp;
        }
    }
    
    return ret;
}

int SrsSource::on_video(SrsCommonMessage* shared_video)
{
    int ret = ERROR_SUCCESS;
    
    // drop any unknown header video.
    // @see https://github.com/simple-rtmp-server/srs/issues/421
    if (!SrsFlvCodec::video_is_acceptable(shared_video->payload, shared_video->size)) {
        char b0 = 0x00;
        if (shared_video->size > 0) {
            b0 = shared_video->payload[0];
        }
        
        srs_warn("drop unknown header video, size=%d, bytes[0]=%#x", shared_video->size, b0);
        return ret;
    }
    
    // convert shared_video to msg, user should not use shared_video again.
    // the payload is transfer to msg, and set to NULL in shared_video.
    SrsSharedPtrMessage msg;
    if ((ret = msg.create(shared_video)) != ERROR_SUCCESS) {
        srs_error("initialize the video failed. ret=%d", ret);
        return ret;
    }
    srs_info("Video dts=%"PRId64", size=%d", msg.timestamp, msg.size);
    
    // directly process the audio message.
    if (!mix_correct) {
        return on_video_imp(&msg);
    }
    
    // insert msg to the queue.
    mix_queue->push(msg.copy());
    
    // fetch someone from mix queue.
    SrsSharedPtrMessage* m = mix_queue->pop();
    if (!m) {
        return ret;
    }
    SrsAutoFree(SrsSharedPtrMessage, m);
    
    // consume the monotonically increase message.
    if (m->is_audio()) {
        ret = on_audio_imp(m);
    } else {
        ret = on_video_imp(m);
    }
    srs_freep(m);
    
    return ret;
}

int SrsSource::on_video_imp(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_info("Video dts=%"PRId64", size=%d", msg->timestamp, msg->size);
    
#ifdef SRS_AUTO_HLS
    if ((ret = hls->on_video(msg)) != ERROR_SUCCESS) {
        // apply the error strategy for hls.
        // @see https://github.com/simple-rtmp-server/srs/issues/264
        std::string hls_error_strategy = _srs_config->get_hls_on_error(_req->vhost);
        if (hls_error_strategy == SRS_CONF_DEFAULT_HLS_ON_ERROR_IGNORE) {
            srs_warn("hls process video message failed, ignore and disable hls. ret=%d", ret);
            
            // unpublish, ignore ret.
            hls->on_unpublish();
            
            // ignore.
            ret = ERROR_SUCCESS;
        } else if (hls_error_strategy == SRS_CONF_DEFAULT_HLS_ON_ERROR_CONTINUE) {
            // compare the sequence header with video, continue when it's actually an sequence header.
            if (ret == ERROR_HLS_DECODE_ERROR && cache_sh_video && cache_sh_video->size == msg->size) {
                srs_warn("the video is actually a sequence header, ignore this packet.");
                ret = ERROR_SUCCESS;
            } else {
                srs_warn("hls continue video failed. ret=%d", ret);
                return ret;
            }
        } else {
            srs_warn("hls disconnect publisher for video error. ret=%d", ret);
            return ret;
        }
    }
#endif
    
#ifdef SRS_AUTO_DVR
    if ((ret = dvr->on_video(msg)) != ERROR_SUCCESS) {
        srs_warn("dvr process video message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        dvr->on_unpublish();
        
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif

#ifdef SRS_AUTO_HDS
    if ((ret = hds->on_video(msg)) != ERROR_SUCCESS) {
        srs_warn("hds process video message failed, ignore and disable dvr. ret=%d", ret);
        
        // unpublish, ignore ret.
        hds->on_unpublish();
        // ignore.
        ret = ERROR_SUCCESS;
    }
#endif
    
    // copy to all consumer
    if (true) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsConsumer* consumer = consumers.at(i);
            if ((ret = consumer->enqueue(msg, atc, sample_rate, frame_rate, jitter_algorithm)) != ERROR_SUCCESS) {
                srs_error("dispatch the video failed. ret=%d", ret);
                return ret;
            }
        }
        srs_info("dispatch video success.");
    }

    // copy to all forwarders.
    if (!forwarders.empty()) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((ret = forwarder->on_video(msg)) != ERROR_SUCCESS) {
                srs_error("forwarder process video message failed. ret=%d", ret);
                return ret;
            }
        }
    }

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (SrsFlvCodec::video_is_sequence_header(msg->payload, msg->size)) {
        srs_freep(cache_sh_video);
        cache_sh_video = msg->copy();
        
        // parse detail audio codec
        SrsAvcAacCodec codec;
        SrsCodecSample sample;
        if ((ret = codec.video_avc_demux(msg->payload, msg->size, &sample)) != ERROR_SUCCESS) {
            srs_error("source codec demux video failed. ret=%d", ret);
            return ret;
        }
        
        // when got video stream info.
        SrsStatistic* stat = SrsStatistic::instance();
        if ((ret = stat->on_video_info(_req, SrsCodecVideoAVC, codec.avc_profile, codec.avc_level)) != ERROR_SUCCESS) {
            return ret;
        }
        
        srs_trace("%dB video sh, "
            "codec(%d, profile=%s, level=%s, %dx%d, %dkbps, %dfps, %ds)",
            msg->size, codec.video_codec_id,
            srs_codec_avc_profile2str(codec.avc_profile).c_str(), 
            srs_codec_avc_level2str(codec.avc_level).c_str(), codec.width, codec.height,
            codec.video_data_rate / 1000, codec.frame_rate, codec.duration);
        return ret;
    }

    // cache the last gop packets
    if ((ret = gop_cache->cache(msg)) != ERROR_SUCCESS) {
        srs_error("gop cache msg failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("cache gop success.");
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (cache_sh_video) {
            cache_sh_video->timestamp = msg->timestamp;
        }
        if (cache_metadata) {
            cache_metadata->timestamp = msg->timestamp;
        }
    }
    
    return ret;
}

int SrsSource::on_aggregate(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    SrsStream* stream = aggregate_stream;
    if ((ret = stream->initialize(msg->payload, msg->size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the aggregate message always use abs time.
    int delta = -1;
    
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
        
        // adjust abs timestamp in aggregate msg.
        if (delta < 0) {
            delta = (int)msg->header.timestamp - (int)timestamp;
        }
        timestamp += delta;
        
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
        SrsCommonMessage o;
        
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
    
    // reset the mix queue.
    mix_queue->clear();
    
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

#ifdef SRS_AUTO_HDS
    if ((ret = hds->on_publish(_req)) != ERROR_SUCCESS) {
        srs_error("start hds failed. ret=%d", ret);
        return ret;
    }
#endif

    // notify the handler.
    srs_assert(handler);
    if ((ret = handler->on_publish(this, _req)) != ERROR_SUCCESS) {
        srs_error("handle on publish failed. ret=%d", ret);
        return ret;
    }
    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_publish(_req);
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

#ifdef SRS_AUTO_HDS
    hds->on_unpublish();
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

    // notify the handler.
    srs_assert(handler);
    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_close(_req);
    handler->on_unpublish(this, _req);
}

int SrsSource::create_consumer(SrsConsumer*& consumer, bool ds, bool dm, bool dg)
{
    int ret = ERROR_SUCCESS;
    
    consumer = new SrsConsumer(this);
    consumers.push_back(consumer);
    
    double queue_size = _srs_config->get_queue_length(_req->vhost);
    consumer->set_queue_size(queue_size);
    
    // if atc, update the sequence header to gop cache time.
    if (atc && !gop_cache->empty()) {
        if (cache_metadata) {
            cache_metadata->timestamp = gop_cache->start_time();
        }
        if (cache_sh_video) {
            cache_sh_video->timestamp = gop_cache->start_time();
        }
        if (cache_sh_audio) {
            cache_sh_audio->timestamp = gop_cache->start_time();
        }
    }

    int tba = sample_rate;
    int tbv = frame_rate;
    SrsRtmpJitterAlgorithm ag = jitter_algorithm;
    
    // copy metadata.
    if (dm && cache_metadata && (ret = consumer->enqueue(cache_metadata, atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        srs_error("dispatch metadata failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch metadata success");
    
    // copy sequence header
    // copy audio sequence first, for hls to fast parse the "right" audio codec.
    // @see https://github.com/simple-rtmp-server/srs/issues/301
    if (ds && cache_sh_audio && (ret = consumer->enqueue(cache_sh_audio, atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        srs_error("dispatch audio sequence header failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch audio sequence header success");

    if (ds && cache_sh_video && (ret = consumer->enqueue(cache_sh_video, atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        srs_error("dispatch video sequence header failed. ret=%d", ret);
        return ret;
    }
    srs_info("dispatch video sequence header success");
    
    // copy gop cache to client.
    if (dg && (ret = gop_cache->dump(consumer, atc, tba, tbv, ag)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // print status.
    if (dg) {
        srs_trace("create consumer, queue_size=%.2f, tba=%d, tbv=%d", queue_size, sample_rate, frame_rate);
    } else {
        srs_trace("create consumer, ignore gop cache, tba=%d, tbv=%d", sample_rate, frame_rate);
    }
    
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

int SrsSource::on_edge_proxy_publish(SrsCommonMessage* msg)
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

