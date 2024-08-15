//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_source.hpp>

#include <sstream>
#include <algorithm>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_config.hpp>
#include <srs_app_encoder.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_dvr.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_app_edge.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_app_hds.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_dash.hpp>
#include <srs_protocol_format.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_http_hooks.hpp>

#define CONST_MAX_JITTER_MS         250
#define CONST_MAX_JITTER_MS_NEG         -250
#define DEFAULT_FRAME_TIME_MS         10

// for 26ms per audio packet,
// 115 packets is 3s.
#define SRS_PURE_AUDIO_GUESS_COUNT 115

// when got these videos or audios, pure audio or video, mix ok.
#define SRS_MIX_CORRECT_PURE_AV 10

// the time to cleanup source.
#define SRS_SOURCE_CLEANUP (3 * SRS_UTIME_SECONDS)

int srs_time_jitter_string2int(std::string time_jitter)
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
    last_pkt_correct_time = -1;
    last_pkt_time = 0;
}

SrsRtmpJitter::~SrsRtmpJitter()
{
}

srs_error_t SrsRtmpJitter::correct(SrsSharedPtrMessage* msg, SrsRtmpJitterAlgorithm ag)
{
    srs_error_t err = srs_success;
    
    // for performance issue
    if (ag != SrsRtmpJitterAlgorithmFULL) {
        // all jitter correct features is disabled, ignore.
        if (ag == SrsRtmpJitterAlgorithmOFF) {
            return err;
        }
        
        // start at zero, but donot ensure monotonically increasing.
        if (ag == SrsRtmpJitterAlgorithmZERO) {
            // for the first time, last_pkt_correct_time is -1.
            if (last_pkt_correct_time == -1) {
                last_pkt_correct_time = msg->timestamp;
            }
            msg->timestamp -= last_pkt_correct_time;
            return err;
        }
        
        // other algorithm, ignore.
        return err;
    }
    
    // full jitter algorithm, do jitter correct.
    // set to 0 for metadata.
    if (!msg->is_av()) {
        msg->timestamp = 0;
        return err;
    }
    
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
    if (delta < CONST_MAX_JITTER_MS_NEG || delta > CONST_MAX_JITTER_MS) {
        // use default 10ms to notice the problem of stream.
        // @see https://github.com/ossrs/srs/issues/425
        delta = DEFAULT_FRAME_TIME_MS;
    }
    
    last_pkt_correct_time = srs_max(0, last_pkt_correct_time + delta);
    
    msg->timestamp = last_pkt_correct_time;
    last_pkt_time = time;
    
    return err;
}

int64_t SrsRtmpJitter::get_time()
{
    return last_pkt_correct_time;
}

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
SrsFastVector::SrsFastVector()
{
    count = 0;
    nb_msgs = 8;
    msgs = new SrsSharedPtrMessage*[nb_msgs];
}

SrsFastVector::~SrsFastVector()
{
    free();
    srs_freepa(msgs);
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
        int size = srs_max(SRS_PERF_MW_MSGS * 8, nb_msgs * 2);
        SrsSharedPtrMessage** buf = msgs;
        msgs = new SrsSharedPtrMessage*[size];
        for (int i = 0; i < nb_msgs; i++) {
            msgs[i] = buf[i];
        }
        srs_info("fast vector incrase %d=>%d", nb_msgs, size);
        
        // use new array.
        srs_freepa(buf);
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
    max_queue_size = 0;
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

srs_utime_t SrsMessageQueue::duration()
{
    return (av_end_time - av_start_time);
}

void SrsMessageQueue::set_queue_size(srs_utime_t queue_size)
{
	max_queue_size = queue_size;
}

srs_error_t SrsMessageQueue::enqueue(SrsSharedPtrMessage* msg, bool* is_overflow)
{
    srs_error_t err = srs_success;

    msgs.push_back(msg);

    // If jitter is off, the timestamp of first sequence header is zero, which wll cause SRS to shrink and drop the
    // keyframes even if there is not overflow packets in queue, so we must ignore the zero timestamps, please
    // @see https://github.com/ossrs/srs/pull/2186#issuecomment-953383063
    if (msg->is_av() && msg->timestamp != 0) {
        if (av_start_time == -1) {
            av_start_time = srs_utime_t(msg->timestamp * SRS_UTIME_MILLISECONDS);
        }
        
        av_end_time = srs_utime_t(msg->timestamp * SRS_UTIME_MILLISECONDS);
    }

    if (max_queue_size <= 0) {
        return err;
    }

    while (av_end_time - av_start_time > max_queue_size) {
        // notice the caller queue already overflow and shrinked.
        if (is_overflow) {
            *is_overflow = true;
        }
        
        shrink();
    }
    
    return err;
}

srs_error_t SrsMessageQueue::dump_packets(int max_count, SrsSharedPtrMessage** pmsgs, int& count)
{
    srs_error_t err = srs_success;
    
    int nb_msgs = (int)msgs.size();
    if (nb_msgs <= 0) {
        return err;
    }
    
    srs_assert(max_count > 0);
    count = srs_min(max_count, nb_msgs);
    
    SrsSharedPtrMessage** omsgs = msgs.data();
    memcpy(pmsgs, omsgs, count * sizeof(SrsSharedPtrMessage*));

    SrsSharedPtrMessage* last = omsgs[count - 1];
    av_start_time = srs_utime_t(last->timestamp * SRS_UTIME_MILLISECONDS);

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
    
    return err;
}

srs_error_t SrsMessageQueue::dump_packets(SrsLiveConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm ag)
{
    srs_error_t err = srs_success;
    
    int nb_msgs = (int)msgs.size();
    if (nb_msgs <= 0) {
        return err;
    }
    
    SrsSharedPtrMessage** omsgs = msgs.data();
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = omsgs[i];
        if ((err = consumer->enqueue(msg, atc, ag)) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }
    
    return err;
}

void SrsMessageQueue::shrink()
{
    SrsSharedPtrMessage* video_sh = NULL;
    SrsSharedPtrMessage* audio_sh = NULL;
    int msgs_size = (int)msgs.size();
    
    // Remove all msgs, mark the sequence headers.
    for (int i = 0; i < (int)msgs.size(); i++) {
        SrsSharedPtrMessage* msg = msgs.at(i);
        
        if (msg->is_video() && SrsFlvVideo::sh(msg->payload, msg->size)) {
            srs_freep(video_sh);
            video_sh = msg;
            continue;
        }
        else if (msg->is_audio() && SrsFlvAudio::sh(msg->payload, msg->size)) {
            srs_freep(audio_sh);
            audio_sh = msg;
            continue;
        }
        
        srs_freep(msg);
    }
    msgs.clear();
    
    // Update av_start_time, the start time of queue.
    av_start_time = av_end_time;

    // Push back sequence headers and update their timestamps.
    if (video_sh) {
        video_sh->timestamp = srsu2ms(av_end_time);
        msgs.push_back(video_sh);
    }
    if (audio_sh) {
        audio_sh->timestamp = srsu2ms(av_end_time);
        msgs.push_back(audio_sh);
    }
    
    if (!_ignore_shrink) {
        srs_trace("shrinking, size=%d, removed=%d, max=%dms", (int)msgs.size(), msgs_size - (int)msgs.size(), srsu2msi(max_queue_size));
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

ISrsWakable::ISrsWakable()
{
}

ISrsWakable::~ISrsWakable()
{
}

SrsLiveConsumer::SrsLiveConsumer(SrsLiveSource* s)
{
    source_ = s;
    paused = false;
    jitter = new SrsRtmpJitter();
    queue = new SrsMessageQueue();
    should_update_source_id = false;
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    mw_wait = srs_cond_new();
    mw_min_msgs = 0;
    mw_duration = 0;
    mw_waiting = false;
#endif
}

SrsLiveConsumer::~SrsLiveConsumer()
{
    source_->on_consumer_destroy(this);
    srs_freep(jitter);
    srs_freep(queue);
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    srs_cond_destroy(mw_wait);
#endif
}

void SrsLiveConsumer::set_queue_size(srs_utime_t queue_size)
{
    queue->set_queue_size(queue_size);
}

void SrsLiveConsumer::update_source_id()
{
    should_update_source_id = true;
}

int64_t SrsLiveConsumer::get_time()
{
    return jitter->get_time();
}

srs_error_t SrsLiveConsumer::enqueue(SrsSharedPtrMessage* shared_msg, bool atc, SrsRtmpJitterAlgorithm ag)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* msg = shared_msg->copy();

    if (!atc) {
        if ((err = jitter->correct(msg, ag)) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    if ((err = queue->enqueue(msg, NULL)) != srs_success) {
        return srs_error_wrap(err, "enqueue message");
    }
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    // fire the mw when msgs is enough.
    if (mw_waiting) {
        // For RTMP, we wait for messages and duration.
        srs_utime_t duration = queue->duration();
        bool match_min_msgs = queue->size() > mw_min_msgs;
        
        // For ATC, maybe the SH timestamp bigger than A/V packet,
        // when encoder republish or overflow.
        // @see https://github.com/ossrs/srs/pull/749
        if (atc && duration < 0) {
            srs_cond_signal(mw_wait);
            mw_waiting = false;
            return err;
        }
        
        // when duration ok, signal to flush.
        if (match_min_msgs && duration > mw_duration) {
            srs_cond_signal(mw_wait);
            mw_waiting = false;
            return err;
        }
    }
#endif
    
    return err;
}

srs_error_t SrsLiveConsumer::dump_packets(SrsMessageArray* msgs, int& count)
{
    srs_error_t err = srs_success;
    
    srs_assert(count >= 0);
    srs_assert(msgs->max > 0);
    
    // the count used as input to reset the max if positive.
    int max = count? srs_min(count, msgs->max) : msgs->max;
    
    // the count specifies the max acceptable count,
    // here maybe 1+, and we must set to 0 when got nothing.
    count = 0;
    
    if (should_update_source_id) {
        srs_trace("update source_id=%s/%s", source_->source_id().c_str(), source_->pre_source_id().c_str());
        should_update_source_id = false;
    }
    
    // paused, return nothing.
    if (paused) {
        return err;
    }
    
    // pump msgs from queue.
    if ((err = queue->dump_packets(max, msgs->msgs, count)) != srs_success) {
        return srs_error_wrap(err, "dump packets");
    }
    
    return err;
}

#ifdef SRS_PERF_QUEUE_COND_WAIT
void SrsLiveConsumer::wait(int nb_msgs, srs_utime_t msgs_duration)
{
    if (paused) {
        srs_usleep(SRS_CONSTS_RTMP_PULSE);
        return;
    }
    
    mw_min_msgs = nb_msgs;
    mw_duration = msgs_duration;
    
    srs_utime_t duration = queue->duration();
    bool match_min_msgs = queue->size() > mw_min_msgs;
    
    // when duration ok, signal to flush.
    if (match_min_msgs && duration > mw_duration) {
        return;
    }
    
    // the enqueue will notify this cond.
    mw_waiting = true;
    
    // use cond block wait for high performance mode.
    srs_cond_wait(mw_wait);
}
#endif

srs_error_t SrsLiveConsumer::on_play_client_pause(bool is_pause)
{
    srs_error_t err = srs_success;
    
    srs_trace("stream consumer change pause state %d=>%d", paused, is_pause);
    paused = is_pause;
    
    return err;
}

void SrsLiveConsumer::wakeup()
{
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (mw_waiting) {
        srs_cond_signal(mw_wait);
        mw_waiting = false;
    }
#endif
}

SrsGopCache::SrsGopCache()
{
    cached_video_count = 0;
    enable_gop_cache = true;
    audio_after_last_video_count = 0;
    gop_cache_max_frames_ = 0;
}

SrsGopCache::~SrsGopCache()
{
    clear();
}

void SrsGopCache::dispose()
{
    clear();
}

void SrsGopCache::set(bool v)
{
    enable_gop_cache = v;
    
    if (!v) {
        clear();
        return;
    }
}

void SrsGopCache::set_gop_cache_max_frames(int v)
{
    gop_cache_max_frames_ = v;
}

bool SrsGopCache::enabled()
{
    return enable_gop_cache;
}

srs_error_t SrsGopCache::cache(SrsSharedPtrMessage* shared_msg)
{
    srs_error_t err = srs_success;
    
    if (!enable_gop_cache) {
        return err;
    }
    
    // the gop cache know when to gop it.
    SrsSharedPtrMessage* msg = shared_msg;
    
    // got video, update the video count if acceptable
    if (msg->is_video()) {
        // Drop video when not h.264 or h.265.
        bool codec_ok = SrsFlvVideo::h264(msg->payload, msg->size);
#ifdef SRS_H265
        codec_ok = codec_ok ? true : SrsFlvVideo::hevc(msg->payload, msg->size);
#endif
        if (!codec_ok) return err;

        cached_video_count++;
        audio_after_last_video_count = 0;
    }
    
    // no acceptable video or pure audio, disable the cache.
    if (pure_audio()) {
        return err;
    }
    
    // ok, gop cache enabled, and got an audio.
    if (msg->is_audio()) {
        audio_after_last_video_count++;
    }
    
    // clear gop cache when pure audio count overflow
    if (audio_after_last_video_count > SRS_PURE_AUDIO_GUESS_COUNT) {
        srs_warn("clear gop cache for guess pure audio overflow");
        clear();
        return err;
    }
    
    // clear gop cache when got key frame
    if (msg->is_video() && SrsFlvVideo::keyframe(msg->payload, msg->size)) {
        clear();
        
        // curent msg is video frame, so we set to 1.
        cached_video_count = 1;
    }

    // cache the frame.
    gop_cache.push_back(msg->copy());

    // Clear gop cache if exceed the max frames.
    if (gop_cache_max_frames_ > 0 && gop_cache.size() > (size_t)gop_cache_max_frames_) {
        srs_warn("Gop cache exceed max frames=%d, total=%d, videos=%d, aalvc=%d",
            gop_cache_max_frames_, (int)gop_cache.size(), cached_video_count, audio_after_last_video_count);
        clear();
    }

    return err;
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

srs_error_t SrsGopCache::dump(SrsLiveConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm jitter_algorithm)
{
    srs_error_t err = srs_success;
    
    std::vector<SrsSharedPtrMessage*>::iterator it;
    for (it = gop_cache.begin(); it != gop_cache.end(); ++it) {
        SrsSharedPtrMessage* msg = *it;
        if ((err = consumer->enqueue(msg, atc, jitter_algorithm)) != srs_success) {
            return srs_error_wrap(err, "enqueue message");
        }
    }
    srs_trace("dispatch cached gop success. count=%d, duration=%d", (int)gop_cache.size(), consumer->get_time());
    
    return err;
}

bool SrsGopCache::empty()
{
    return gop_cache.empty();
}

srs_utime_t SrsGopCache::start_time()
{
    if (empty()) {
        return 0;
    }
    
    SrsSharedPtrMessage* msg = gop_cache[0];
    srs_assert(msg);
    
    return srs_utime_t(msg->timestamp * SRS_UTIME_MILLISECONDS);
}

bool SrsGopCache::pure_audio()
{
    return cached_video_count == 0;
}

ISrsLiveSourceHandler::ISrsLiveSourceHandler()
{
}

ISrsLiveSourceHandler::~ISrsLiveSourceHandler()
{
}

// TODO: FIXME: Remove it?
bool srs_hls_can_continue(int ret, SrsSharedPtrMessage* sh, SrsSharedPtrMessage* msg)
{
    // only continue for decode error.
    if (ret != ERROR_HLS_DECODE_ERROR) {
        return false;
    }
    
    // when video size equals to sequence header,
    // the video actually maybe a sequence header,
    // continue to make ffmpeg happy.
    if (sh && sh->size == msg->size) {
        srs_warn("the msg is actually a sequence header, ignore this packet.");
        return true;
    }
    
    return false;
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
    bool mix_ok = false;
    
    // pure video
    if (nb_videos >= SRS_MIX_CORRECT_PURE_AV && nb_audios == 0) {
        mix_ok = true;
    }
    
    // pure audio
    if (nb_audios >= SRS_MIX_CORRECT_PURE_AV && nb_videos == 0) {
        mix_ok = true;
    }
    
    // got 1 video and 1 audio, mix ok.
    if (nb_videos >= 1 && nb_audios >= 1) {
        mix_ok = true;
    }
    
    if (!mix_ok) {
        return NULL;
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

SrsOriginHub::SrsOriginHub()
{
    source_ = NULL;
    req_ = NULL;
    is_active = false;
    
    hls = new SrsHls();
    dash = new SrsDash();
    dvr = new SrsDvr();
    encoder = new SrsEncoder();
#ifdef SRS_HDS
    hds = new SrsHds();
#endif
    ng_exec = new SrsNgExec();
    
    _srs_config->subscribe(this);
}

SrsOriginHub::~SrsOriginHub()
{
    _srs_config->unsubscribe(this);
    
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            srs_freep(forwarder);
        }
        forwarders.clear();
    }
    srs_freep(ng_exec);

    srs_freep(hls);
    srs_freep(dash);
    srs_freep(dvr);
    srs_freep(encoder);
#ifdef SRS_HDS
    srs_freep(hds);
#endif
}

srs_error_t SrsOriginHub::initialize(SrsSharedPtr<SrsLiveSource> s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    req_ = r;
    // Because source references to this object, so we should directly use the source ptr.
    source_ = s.get();
    
    if ((err = hls->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "hls initialize");
    }
    
    if ((err = dash->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "dash initialize");
    }
    
    if ((err = dvr->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "dvr initialize");
    }
    
    return err;
}

void SrsOriginHub::dispose()
{
    hls->dispose();
    dash->dispose();
}

srs_error_t SrsOriginHub::cycle()
{
    srs_error_t err = srs_success;
    
    if ((err = hls->cycle()) != srs_success) {
        return srs_error_wrap(err, "hls cycle");
    }
    
    if ((err = dash->cycle()) != srs_success) {
        return srs_error_wrap(err, "dash cycle");
    }
    
    return err;
}

bool SrsOriginHub::active()
{
    return is_active;
}

srs_utime_t SrsOriginHub::cleanup_delay()
{
    srs_utime_t hls_delay = hls->cleanup_delay();
    srs_utime_t dash_delay = dash->cleanup_delay();
    return srs_max(hls_delay, dash_delay);
}

srs_error_t SrsOriginHub::on_meta_data(SrsSharedPtrMessage* shared_metadata, SrsOnMetaDataPacket* packet)
{
    srs_error_t err = srs_success;
    
    // copy to all forwarders
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((err = forwarder->on_meta_data(shared_metadata)) != srs_success) {
                return srs_error_wrap(err, "Forwarder consume metadata");
            }
        }
    }
    
    if ((err = dvr->on_meta_data(shared_metadata)) != srs_success) {
        return srs_error_wrap(err, "DVR consume metadata");
    }
    
    return err;
}

srs_error_t SrsOriginHub::on_audio(SrsSharedPtrMessage* shared_audio)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* msg = shared_audio;
    SrsRtmpFormat* format = source_->format_;
    
    // Handle the metadata when got sequence header.
    if (format->is_aac_sequence_header() || format->is_mp3_sequence_header()) {
        srs_assert(format->acodec);
        SrsAudioCodecConfig* c = format->acodec;
        
        static int flv_sample_sizes[] = {8, 16, 0};
        static int flv_sound_types[] = {1, 2, 0};
        
        // when got audio stream info.
        SrsStatistic* stat = SrsStatistic::instance();
        if ((err = stat->on_audio_info(req_, format->acodec->id, c->sound_rate, c->sound_type, c->aac_object)) != srs_success) {
            return srs_error_wrap(err, "stat audio");
        }

        if (format->acodec->id == SrsAudioCodecIdMP3) {
            srs_trace("%dB audio sh, codec(%d, %dbits, %dchannels, %dHZ)",
                msg->size, c->id, flv_sample_sizes[c->sound_size], flv_sound_types[c->sound_type],
                srs_flv_srates[c->sound_rate]);
        } else {
            srs_trace("%dB audio sh, codec(%d, profile=%s, %dchannels, %dkbps, %dHZ), flv(%dbits, %dchannels, %dHZ)",
                msg->size, c->id, srs_aac_object2str(c->aac_object).c_str(), c->aac_channels,
                c->audio_data_rate / 1000, srs_aac_srates[c->aac_sample_rate],
                flv_sample_sizes[c->sound_size], flv_sound_types[c->sound_type],
                srs_flv_srates[c->sound_rate]);
        }
    }
    
    if ((err = hls->on_audio(msg, format)) != srs_success) {
        // apply the error strategy for hls.
        std::string hls_error_strategy = _srs_config->get_hls_on_error(req_->vhost);
        if (srs_config_hls_is_on_error_ignore(hls_error_strategy)) {
            srs_warn("hls: ignore audio error %s", srs_error_desc(err).c_str());
            hls->on_unpublish();
            srs_error_reset(err);
        } else if (srs_config_hls_is_on_error_continue(hls_error_strategy)) {
            if (srs_hls_can_continue(srs_error_code(err), source_->meta->ash(), msg)) {
                srs_error_reset(err);
            } else {
                return srs_error_wrap(err, "hls: audio");
            }
        } else {
            return srs_error_wrap(err, "hls: audio");
        }
    }
    
    if ((err = dash->on_audio(msg, format)) != srs_success) {
        srs_warn("dash: ignore audio error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        dash->on_unpublish();
    }
    
    if ((err = dvr->on_audio(msg, format)) != srs_success) {
        srs_warn("dvr: ignore audio error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        dvr->on_unpublish();
    }
    
#ifdef SRS_HDS
    if ((err = hds->on_audio(msg)) != srs_success) {
        srs_warn("hds: ignore audio error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        hds->on_unpublish();
    }
#endif
    
    // copy to all forwarders.
    if (true) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((err = forwarder->on_audio(msg)) != srs_success) {
                return srs_error_wrap(err, "forward: audio");
            }
        }
    }
    
    return err;
}

srs_error_t SrsOriginHub::on_video(SrsSharedPtrMessage* shared_video, bool is_sequence_header)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* msg = shared_video;
    SrsRtmpFormat* format = source_->format_;
 
    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (format->is_avc_sequence_header()) {
        SrsVideoCodecConfig* c = format->vcodec;
        srs_assert(c);
        
        // when got video stream info.
        SrsStatistic* stat = SrsStatistic::instance();

        if (c->id == SrsVideoCodecIdAVC) {
            err = stat->on_video_info(req_, c->id, c->avc_profile, c->avc_level, c->width, c->height);
            srs_trace("%dB video sh, codec(%d, profile=%s, level=%s, %dx%d, %dkbps, %.1ffps, %.1fs)",
                msg->size, c->id, srs_avc_profile2str(c->avc_profile).c_str(), srs_avc_level2str(c->avc_level).c_str(),
                c->width, c->height, c->video_data_rate / 1000, c->frame_rate, c->duration);
#ifdef SRS_H265
        } else if (c->id == SrsVideoCodecIdHEVC) {
            err = stat->on_video_info(req_, c->id, c->hevc_profile, c->hevc_level, c->width, c->height);
            srs_trace("%dB video sh, codec(%d, profile=%s, level=%s, %dx%d, %dkbps, %.1ffps, %.1fs)",
                msg->size, c->id, srs_hevc_profile2str(c->hevc_profile).c_str(), srs_hevc_level2str(c->hevc_level).c_str(),
                c->width, c->height, c->video_data_rate / 1000, c->frame_rate, c->duration);
#endif
        }
        if (err != srs_success) {
            return srs_error_wrap(err, "stat video");
        }
    }

    // Ignore video data when no sps/pps
    // @bug https://github.com/ossrs/srs/issues/703#issuecomment-578393155
    if (format->vcodec && !format->vcodec->is_avc_codec_ok()) {
        return err;
    }
    
    if ((err = hls->on_video(msg, format)) != srs_success) {
        // TODO: We should support more strategies.
        // apply the error strategy for hls.
        std::string hls_error_strategy = _srs_config->get_hls_on_error(req_->vhost);
        if (srs_config_hls_is_on_error_ignore(hls_error_strategy)) {
            srs_warn("hls: ignore video error %s", srs_error_desc(err).c_str());
            hls->on_unpublish();
            srs_error_reset(err);
        } else if (srs_config_hls_is_on_error_continue(hls_error_strategy)) {
            if (srs_hls_can_continue(srs_error_code(err), source_->meta->vsh(), msg)) {
                srs_error_reset(err);
            } else {
                return srs_error_wrap(err, "hls: video");
            }
        } else {
            return srs_error_wrap(err, "hls: video");
        }
    }
    
    if ((err = dash->on_video(msg, format)) != srs_success) {
        srs_warn("dash: ignore video error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        dash->on_unpublish();
    }
    
    if ((err = dvr->on_video(msg, format)) != srs_success) {
        srs_warn("dvr: ignore video error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        dvr->on_unpublish();
    }
    
#ifdef SRS_HDS
    if ((err = hds->on_video(msg)) != srs_success) {
        srs_warn("hds: ignore video error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        hds->on_unpublish();
    }
#endif
    
    // copy to all forwarders.
    if (!forwarders.empty()) {
        std::vector<SrsForwarder*>::iterator it;
        for (it = forwarders.begin(); it != forwarders.end(); ++it) {
            SrsForwarder* forwarder = *it;
            if ((err = forwarder->on_video(msg)) != srs_success) {
                return srs_error_wrap(err, "forward video");
            }
        }
    }
    
    return err;
}

srs_error_t SrsOriginHub::on_publish()
{
    srs_error_t err = srs_success;
    
    // create forwarders
    if ((err = create_forwarders()) != srs_success) {
        return srs_error_wrap(err, "create forwarders");
    }
    
    // TODO: FIXME: use initialize to set req.
    if ((err = encoder->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "encoder publish");
    }

    if ((err = hls->on_publish()) != srs_success) {
        return srs_error_wrap(err, "hls publish");
    }
    
    if ((err = dash->on_publish()) != srs_success) {
        return srs_error_wrap(err, "dash publish");
    }
    
    // @see https://github.com/ossrs/srs/issues/1613#issuecomment-961657927
    if ((err = dvr->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "dvr publish");
    }
    
    // TODO: FIXME: use initialize to set req.
#ifdef SRS_HDS
    if ((err = hds->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "hds publish");
    }
#endif
    
    // TODO: FIXME: use initialize to set req.
    if ((err = ng_exec->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "exec publish");
    }
    
    is_active = true;
    
    return err;
}

void SrsOriginHub::on_unpublish()
{
    is_active = false;
    
    // destroy all forwarders
    destroy_forwarders();
    
    encoder->on_unpublish();
    hls->on_unpublish();
    dash->on_unpublish();
    dvr->on_unpublish();
    
#ifdef SRS_HDS
    hds->on_unpublish();
#endif
    
    ng_exec->on_unpublish();
}

srs_error_t SrsOriginHub::on_forwarder_start(SrsForwarder* forwarder)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* cache_metadata = source_->meta->data();
    SrsSharedPtrMessage* cache_sh_video = source_->meta->vsh();
    SrsSharedPtrMessage* cache_sh_audio = source_->meta->ash();
    
    // feed the forwarder the metadata/sequence header,
    // when reload to enable the forwarder.
    if (cache_metadata && (err = forwarder->on_meta_data(cache_metadata)) != srs_success) {
        return srs_error_wrap(err, "forward metadata");
    }
    if (cache_sh_video && (err = forwarder->on_video(cache_sh_video)) != srs_success) {
        return srs_error_wrap(err, "forward video sh");
    }
    if (cache_sh_audio && (err = forwarder->on_audio(cache_sh_audio)) != srs_success) {
        return srs_error_wrap(err, "forward audio sh");
    }
    
    return err;
}

srs_error_t SrsOriginHub::on_dvr_request_sh()
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* cache_metadata = source_->meta->data();
    SrsSharedPtrMessage* cache_sh_video = source_->meta->vsh();
    SrsSharedPtrMessage* cache_sh_audio = source_->meta->ash();
    
    // feed the dvr the metadata/sequence header,
    // when reload to start dvr, dvr will never get the sequence header in stream,
    // use the SrsLiveSource.on_dvr_request_sh to push the sequence header to DVR.
    if (cache_metadata && (err = dvr->on_meta_data(cache_metadata)) != srs_success) {
        return srs_error_wrap(err, "dvr metadata");
    }
    
    if (cache_sh_video) {
        if ((err = dvr->on_video(cache_sh_video, source_->meta->vsh_format())) != srs_success) {
            return srs_error_wrap(err, "dvr video");
        }
    }
    
    if (cache_sh_audio) {
        if ((err = dvr->on_audio(cache_sh_audio, source_->meta->ash_format())) != srs_success) {
            return srs_error_wrap(err, "dvr audio");
        }
    }
    
    return err;
}

srs_error_t SrsOriginHub::on_hls_request_sh()
{
    srs_error_t err = srs_success;

    SrsSharedPtrMessage* cache_sh_video = source_->meta->vsh();
    if (cache_sh_video) {
        if ((err = hls->on_video(cache_sh_video, source_->meta->vsh_format())) != srs_success) {
            return srs_error_wrap(err, "hls video");
        }
    }

    SrsSharedPtrMessage* cache_sh_audio = source_->meta->ash();
    if (cache_sh_audio) {
        if ((err = hls->on_audio(cache_sh_audio, source_->meta->ash_format())) != srs_success) {
            return srs_error_wrap(err, "hls audio");
        }
    }

    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_forward(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }
    
    // TODO: FIXME: Must do async reload, see SrsHls::async_reload.
    
    // forwarders
    destroy_forwarders();
    
    // Don't start forwarders when source is not active.
    if (!is_active) {
        return err;
    }
    
    if ((err = create_forwarders()) != srs_success) {
        return srs_error_wrap(err, "create forwarders");
    }
    
    srs_trace("vhost %s forwarders reload success", vhost.c_str());
    
    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_dash(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }

    // TODO: FIXME: Must do async reload, see SrsHls::async_reload.

    dash->on_unpublish();
    
    // Don't start DASH when source is not active.
    if (!is_active) {
        return err;
    }
    
    if ((err = dash->on_publish()) != srs_success) {
        return srs_error_wrap(err, "dash start publish");
    }

    SrsRtmpFormat* format = source_->format_;
    
    SrsSharedPtrMessage* cache_sh_video = source_->meta->vsh();
    if (cache_sh_video) {
        if ((err = format->on_video(cache_sh_video)) != srs_success) {
            return srs_error_wrap(err, "format on_video");
        }
        if ((err = dash->on_video(cache_sh_video, format)) != srs_success) {
            return srs_error_wrap(err, "dash on_video");
        }
    }
    
    SrsSharedPtrMessage* cache_sh_audio = source_->meta->ash();
    if (cache_sh_audio) {
        if ((err = format->on_audio(cache_sh_audio)) != srs_success) {
            return srs_error_wrap(err, "format on_audio");
        }
        if ((err = dash->on_audio(cache_sh_audio, format)) != srs_success) {
            return srs_error_wrap(err, "dash on_audio");
        }
    }
    
    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_hls(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }

    hls->async_reload();
    
    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_hds(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }

    // TODO: FIXME: Must do async reload, see SrsHls::async_reload.
    
#ifdef SRS_HDS
    hds->on_unpublish();
    
    // Don't start HDS when source is not active.
    if (!is_active) {
        return err;
    }
    
    if ((err = hds->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "hds publish failed");
    }
    srs_trace("vhost %s hds reload success", vhost.c_str());
#endif
    
    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_dvr(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }

    // TODO: FIXME: Must do async reload, see SrsHls::async_reload.
    
    // cleanup dvr
    dvr->on_unpublish();
    
    // Don't start DVR when source is not active.
    if (!is_active) {
        return err;
    }
    
    // reinitialize the dvr, update plan.
    if ((err = dvr->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "reload dvr");
    }
    
    // start to publish by new plan.
    if ((err = dvr->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "dvr publish failed");
    }
    
    if ((err = on_dvr_request_sh()) != srs_success) {
        return srs_error_wrap(err, "request sh");
    }
    
    srs_trace("vhost %s dvr reload success", vhost.c_str());
    
    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_transcode(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }

    // TODO: FIXME: Must do async reload, see SrsHls::async_reload.
    
    encoder->on_unpublish();
    
    // Don't start transcode when source is not active.
    if (!is_active) {
        return err;
    }
    
    if ((err = encoder->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "start encoder failed");
    }
    srs_trace("vhost %s transcode reload success", vhost.c_str());
    
    return err;
}

srs_error_t SrsOriginHub::on_reload_vhost_exec(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req_->vhost != vhost) {
        return err;
    }

    // TODO: FIXME: Must do async reload, see SrsHls::async_reload.
    
    ng_exec->on_unpublish();
    
    // Don't start exec when source is not active.
    if (!is_active) {
        return err;
    }
    
    if ((err = ng_exec->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "start exec failed");
    }
    srs_trace("vhost %s exec reload success", vhost.c_str());
    
    return err;
}

srs_error_t SrsOriginHub::create_forwarders()
{
    srs_error_t err = srs_success;
    
    if (!_srs_config->get_forward_enabled(req_->vhost)) {
        return err;
    }

    // For backend config
    // If backend is enabled and applied, ignore destination.
    bool applied_backend_server = false;
    if ((err = create_backend_forwarders(applied_backend_server)) != srs_success) {
        return srs_error_wrap(err, "create backend applied=%d", applied_backend_server);
    }

    // Already applied backend server, ignore destination.
    if (applied_backend_server) {
        return err;
    }

    // For destanition config
    SrsConfDirective* conf = _srs_config->get_forwards(req_->vhost);
    for (int i = 0; conf && i < (int)conf->args.size(); i++) {
        std::string forward_server = conf->args.at(i);
        
        SrsForwarder* forwarder = new SrsForwarder(this);
        forwarders.push_back(forwarder);
        
        // initialize the forwarder with request.
        if ((err = forwarder->initialize(req_, forward_server)) != srs_success) {
            return srs_error_wrap(err, "init forwarder");
        }

        srs_utime_t queue_size = _srs_config->get_queue_length(req_->vhost);
        forwarder->set_queue_size(queue_size);
        
        if ((err = forwarder->on_publish()) != srs_success) {
            return srs_error_wrap(err, "start forwarder failed, vhost=%s, app=%s, stream=%s, forward-to=%s",
                req_->vhost.c_str(), req_->app.c_str(), req_->stream.c_str(), forward_server.c_str());
        }
    }
    
    return err;
}

srs_error_t SrsOriginHub::create_backend_forwarders(bool& applied)
{
    srs_error_t err = srs_success;

    // default not configure backend service
    applied = false;

    SrsConfDirective* conf = _srs_config->get_forward_backend(req_->vhost);
    if (!conf || conf->arg0().empty()) {
        return err;
    }

    // configure backend service
    applied = true;

    // only get first backend url
    std::string backend_url = conf->arg0();

    // get urls on forward backend
    std::vector<std::string> urls;
    if ((err = SrsHttpHooks::on_forward_backend(backend_url, req_, urls)) != srs_success) {
        return srs_error_wrap(err, "get forward backend failed, backend=%s", backend_url.c_str());
    }

    // create forwarders by urls
    std::vector<std::string>::iterator it;
    for (it = urls.begin(); it != urls.end(); ++it) {
        std::string url = *it;

        // create temp Request by url
        SrsUniquePtr<SrsRequest> req(new SrsRequest());
        srs_parse_rtmp_url(url, req->tcUrl, req->stream);
        srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->stream, req->port, req->param);

        // create forwarder
        SrsForwarder* forwarder = new SrsForwarder(this);
        forwarders.push_back(forwarder);

        std::stringstream forward_server;
        forward_server << req->host << ":" << req->port;

        // initialize the forwarder with request.
        if ((err = forwarder->initialize(req.get(), forward_server.str())) != srs_success) {
            return srs_error_wrap(err, "init backend forwarder failed, forward-to=%s", forward_server.str().c_str());
        }

        srs_utime_t queue_size = _srs_config->get_queue_length(req_->vhost);
        forwarder->set_queue_size(queue_size);

        if ((err = forwarder->on_publish()) != srs_success) {
            return srs_error_wrap(err, "start backend forwarder failed, vhost=%s, app=%s, stream=%s, forward-to=%s",
                req_->vhost.c_str(), req_->app.c_str(), req_->stream.c_str(), forward_server.str().c_str());
        }
    }

    return err;
}

void SrsOriginHub::destroy_forwarders()
{
    std::vector<SrsForwarder*>::iterator it;
    for (it = forwarders.begin(); it != forwarders.end(); ++it) {
        SrsForwarder* forwarder = *it;
        forwarder->on_unpublish();
        srs_freep(forwarder);
    }
    forwarders.clear();
}

SrsMetaCache::SrsMetaCache()
{
    meta = video = audio = NULL;
    previous_video = previous_audio = NULL;
    vformat = new SrsRtmpFormat();
    aformat = new SrsRtmpFormat();
}

SrsMetaCache::~SrsMetaCache()
{
    dispose();
    srs_freep(vformat);
    srs_freep(aformat);
}

void SrsMetaCache::dispose()
{
    clear();
    srs_freep(previous_video);
    srs_freep(previous_audio);
}

void SrsMetaCache::clear()
{
    srs_freep(meta);
    srs_freep(video);
    srs_freep(audio);
}

SrsSharedPtrMessage* SrsMetaCache::data()
{
    return meta;
}

SrsSharedPtrMessage* SrsMetaCache::vsh()
{
    return video;
}

SrsFormat* SrsMetaCache::vsh_format()
{
    return vformat;
}

SrsSharedPtrMessage* SrsMetaCache::ash()
{
    return audio;
}

SrsFormat* SrsMetaCache::ash_format()
{
    return aformat;
}

srs_error_t SrsMetaCache::dumps(SrsLiveConsumer* consumer, bool atc, SrsRtmpJitterAlgorithm ag, bool dm, bool ds)
{
    srs_error_t err = srs_success;
    
    // copy metadata.
    if (dm && meta && (err = consumer->enqueue(meta, atc, ag)) != srs_success) {
        return srs_error_wrap(err, "enqueue metadata");
    }
    
    // copy sequence header
    // copy audio sequence first, for hls to fast parse the "right" audio codec.
    if (aformat && aformat->acodec && aformat->acodec->id != SrsAudioCodecIdMP3) {
        if (ds && audio && (err = consumer->enqueue(audio, atc, ag)) != srs_success) {
            return srs_error_wrap(err, "enqueue audio sh");
        }
    }
    
    if (ds && video && (err = consumer->enqueue(video, atc, ag)) != srs_success) {
        return srs_error_wrap(err, "enqueue video sh");
    }
    
    return err;
}

SrsSharedPtrMessage* SrsMetaCache::previous_vsh()
{
    return previous_video;
}

SrsSharedPtrMessage* SrsMetaCache::previous_ash()
{
    return previous_audio;
}

void SrsMetaCache::update_previous_vsh()
{
    srs_freep(previous_video);
    previous_video = video? video->copy() : NULL;
}

void SrsMetaCache::update_previous_ash()
{
    srs_freep(previous_audio);
    previous_audio = audio? audio->copy() : NULL;
}

srs_error_t SrsMetaCache::update_data(SrsMessageHeader* header, SrsOnMetaDataPacket* metadata, bool& updated)
{
    updated = false;
    
    srs_error_t err = srs_success;
    
    SrsAmf0Any* prop = NULL;
    
    // when exists the duration, remove it to make ExoPlayer happy.
    if (metadata->metadata->get_property("duration") != NULL) {
        metadata->metadata->remove("duration");
    }
    
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
    srs_trace("got metadata%s", ss.str().c_str());
    
    // add server info to metadata
    metadata->metadata->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));

    // version, for example, 1.0.0
    // add version to metadata, please donot remove it, for debug.
    metadata->metadata->set("server_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    
    // encode the metadata to payload
    int size = 0;
    char* payload = NULL;
    if ((err = metadata->encode(size, payload)) != srs_success) {
        return srs_error_wrap(err, "encode metadata");
    }
    
    if (size <= 0) {
        srs_warn("ignore the invalid metadata. size=%d", size);
        return err;
    }
    
    // create a shared ptr message.
    srs_freep(meta);
    meta = new SrsSharedPtrMessage();
    updated = true;
    
    // dump message to shared ptr message.
    // the payload/size managed by cache_metadata, user should not free it.
    if ((err = meta->create(header, payload, size)) != srs_success) {
        return srs_error_wrap(err, "create metadata");
    }
    
    return err;
}

srs_error_t SrsMetaCache::update_ash(SrsSharedPtrMessage* msg)
{
    srs_freep(audio);
    audio = msg->copy();
    update_previous_ash();
    return aformat->on_audio(msg);
}

srs_error_t SrsMetaCache::update_vsh(SrsSharedPtrMessage* msg)
{
    srs_freep(video);
    video = msg->copy();
    update_previous_vsh();
    return vformat->on_video(msg);
}

SrsLiveSourceManager* _srs_sources = NULL;

SrsLiveSourceManager::SrsLiveSourceManager()
{
    lock = srs_mutex_new();
    timer_ = new SrsHourGlass("sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsLiveSourceManager::~SrsLiveSourceManager()
{
    srs_mutex_destroy(lock);
    srs_freep(timer_);
}

srs_error_t SrsLiveSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsLiveSourceManager::fetch_or_create(SrsRequest* r, ISrsLiveSourceHandler* h, SrsSharedPtr<SrsLiveSource>& pps)
{
    srs_error_t err = srs_success;

    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    // TODO: FIXME: Use smaller scope lock.
    SrsLocker(lock);

    string stream_url = r->get_stream_url();
    std::map< std::string, SrsSharedPtr<SrsLiveSource> >::iterator it = pool.find(stream_url);

    if (it != pool.end()) {
        SrsSharedPtr<SrsLiveSource>& source = it->second;

        // we always update the request of resource,
        // for origin auth is on, the token in request maybe invalid,
        // and we only need to update the token of request, it's simple.
        source->update_auth(r);
        pps = source;
        return err;
    }

    SrsSharedPtr<SrsLiveSource> source = new SrsLiveSource();
    srs_trace("new live source, stream_url=%s", stream_url.c_str());

    if ((err = source->initialize(source, r, h)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }
    
    pool[stream_url] = source;
    pps = source;
    return err;
}

SrsSharedPtr<SrsLiveSource> SrsLiveSourceManager::fetch(SrsRequest* r)
{
    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    // TODO: FIXME: Use smaller scope lock.
    SrsLocker(lock);
    
    string stream_url = r->get_stream_url();
    std::map< std::string, SrsSharedPtr<SrsLiveSource> >::iterator it = pool.find(stream_url);

    if (it == pool.end()) {
        return SrsSharedPtr<SrsLiveSource>(NULL);
    }

    SrsSharedPtr<SrsLiveSource>& source = it->second;
    return source;
}

void SrsLiveSourceManager::dispose()
{
    std::map< std::string, SrsSharedPtr<SrsLiveSource> >::iterator it;
    for (it = pool.begin(); it != pool.end(); ++it) {
        SrsSharedPtr<SrsLiveSource>& source = it->second;
        source->dispose();
    }
    return;
}

srs_error_t SrsLiveSourceManager::setup_ticks()
{
    srs_error_t err = srs_success;

    if ((err = timer_->tick(1, 3 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsLiveSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    std::map< std::string, SrsSharedPtr<SrsLiveSource> >::iterator it;
    for (it = pool.begin(); it != pool.end();) {
        SrsSharedPtr<SrsLiveSource>& source = it->second;

        // Do cycle source to cleanup components, such as hls dispose.
        if ((err = source->cycle()) != srs_success) {
            SrsContextId cid = source->source_id();
            if (cid.empty()) cid = source->pre_source_id();
            return srs_error_wrap(err, "source cycle, id=[%s]", cid.c_str());
        }

        // When source expired, remove it.
        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead()) {
            SrsContextId cid = source->source_id();
            if (cid.empty()) cid = source->pre_source_id();
            srs_trace("Live: cleanup die source, id=[%s], total=%d", cid.c_str(), (int)pool.size());
            pool.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

void SrsLiveSourceManager::destroy()
{
    pool.clear();
}

SrsLiveSource::SrsLiveSource()
{
    req = NULL;
    jitter_algorithm = SrsRtmpJitterAlgorithmOFF;
    mix_correct = false;
    mix_queue = new SrsMixQueue();
    
    _can_publish = true;
    stream_die_at_ = 0;
    publisher_idle_at_ = 0;

    handler = NULL;
    bridge_ = NULL;

    play_edge = new SrsPlayEdge();
    publish_edge = new SrsPublishEdge();
    gop_cache = new SrsGopCache();
    hub = new SrsOriginHub();
    meta = new SrsMetaCache();
    format_ = new SrsRtmpFormat();
    
    is_monotonically_increase = false;
    last_packet_time = 0;
    
    _srs_config->subscribe(this);
    atc = false;
}

SrsLiveSource::~SrsLiveSource()
{
    _srs_config->unsubscribe(this);
    
    // never free the consumers,
    // for all consumers are auto free.
    consumers.clear();

    srs_freep(format_);
    srs_freep(hub);
    srs_freep(meta);
    srs_freep(mix_queue);
    
    srs_freep(play_edge);
    srs_freep(publish_edge);
    srs_freep(gop_cache);
    
    srs_freep(req);
    srs_freep(bridge_);

    SrsContextId cid = _source_id;
    if (cid.empty()) cid = _pre_source_id;
    srs_trace("free live source id=[%s]", cid.c_str());
}

void SrsLiveSource::dispose()
{
    hub->dispose();
    meta->dispose();
    gop_cache->dispose();
}

srs_error_t SrsLiveSource::cycle()
{
    srs_error_t err = hub->cycle();
    if (err != srs_success) {
        return srs_error_wrap(err, "hub cycle");
    }
    
    return srs_success;
}

bool SrsLiveSource::stream_is_dead()
{
    // still publishing?
    if (!_can_publish || !publish_edge->can_publish()) {
        return false;
    }
    
    // has any consumers?
    if (!consumers.empty()) {
        return false;
    }

    // Delay cleanup source.
    srs_utime_t now = srs_get_system_time();
    if (now < stream_die_at_ + SRS_SOURCE_CLEANUP) {
        return false;
    }

    // Origin hub delay cleanup.
    if (now < stream_die_at_ + hub->cleanup_delay()) {
        return false;
    }
    
    return true;
}

bool SrsLiveSource::publisher_is_idle_for(srs_utime_t timeout)
{
    if (!publisher_idle_at_ || !timeout) {
        return false;
    }

    srs_utime_t now = srs_get_system_time();
    if (now > publisher_idle_at_ + timeout) {
        return true;
    }
    return false;
}

srs_error_t SrsLiveSource::initialize(SrsSharedPtr<SrsLiveSource> wrapper, SrsRequest* r, ISrsLiveSourceHandler* h)
{
    srs_error_t err = srs_success;
    
    srs_assert(h);
    srs_assert(!req);
    
    handler = h;
    req = r->copy();
    atc = _srs_config->get_atc(req->vhost);

    if ((err = format_->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    // Setup the SPS/PPS parsing strategy.
    format_->try_annexb_first = _srs_config->try_annexb_first(r->vhost);
    
    if ((err = hub->initialize(wrapper, req)) != srs_success) {
        return srs_error_wrap(err, "hub");
    }
    
    if ((err = play_edge->initialize(wrapper, req)) != srs_success) {
        return srs_error_wrap(err, "edge(play)");
    }
    if ((err = publish_edge->initialize(wrapper, req)) != srs_success) {
        return srs_error_wrap(err, "edge(publish)");
    }
    
    srs_utime_t queue_size = _srs_config->get_queue_length(req->vhost);
    publish_edge->set_queue_size(queue_size);
    
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_time_jitter(req->vhost);
    mix_correct = _srs_config->get_mix_correct(req->vhost);
    
    return err;
}

void SrsLiveSource::set_bridge(ISrsStreamBridge* v)
{
    srs_freep(bridge_);
    bridge_ = v;
}

srs_error_t SrsLiveSource::on_reload_vhost_play(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    // time_jitter
    jitter_algorithm = (SrsRtmpJitterAlgorithm)_srs_config->get_time_jitter(req->vhost);
    
    // mix_correct
    if (true) {
        bool v = _srs_config->get_mix_correct(req->vhost);
        
        // when changed, clear the mix queue.
        if (v != mix_correct) {
            mix_queue->clear();
        }
        mix_correct = v;
    }
    
    // atc changed.
    if (true) {
        bool v = _srs_config->get_atc(vhost);
        
        if (v != atc) {
            srs_warn("vhost %s atc changed to %d, connected client may corrupt.", vhost.c_str(), v);
            gop_cache->clear();
        }
        atc = v;
    }
    
    // gop cache changed.
    if (true) {
        bool v = _srs_config->get_gop_cache(vhost);
        
        if (v != gop_cache->enabled()) {
            string url = req->get_stream_url();
            srs_trace("vhost %s gop_cache changed to %d, source url=%s", vhost.c_str(), v, url.c_str());
            gop_cache->set(v);
            gop_cache->set_gop_cache_max_frames(_srs_config->get_gop_cache_max_frames(vhost));
        }
    }
    
    // queue length
    if (true) {
        srs_utime_t v = _srs_config->get_queue_length(req->vhost);
        
        if (true) {
            std::vector<SrsLiveConsumer*>::iterator it;
            
            for (it = consumers.begin(); it != consumers.end(); ++it) {
                SrsLiveConsumer* consumer = *it;
                consumer->set_queue_size(v);
            }
            
            srs_trace("consumers reload queue size success.");
        }
        
        // TODO: FIXME: https://github.com/ossrs/srs/issues/742#issuecomment-273656897
        // TODO: FIXME: support queue size.
#if 0
        if (true) {
            std::vector<SrsForwarder*>::iterator it;
            
            for (it = forwarders.begin(); it != forwarders.end(); ++it) {
                SrsForwarder* forwarder = *it;
                forwarder->set_queue_size(v);
            }
            
            srs_trace("forwarders reload queue size success.");
        }
        
        if (true) {
            publish_edge->set_queue_size(v);
            srs_trace("publish_edge reload queue size success.");
        }
#endif
    }
    
    return err;
}

srs_error_t SrsLiveSource::on_source_id_changed(SrsContextId id)
{
    srs_error_t err = srs_success;
    
    if (!_source_id.compare(id)) {
        return err;
    }

    if (_pre_source_id.empty()) {
        _pre_source_id = id;
    }
    _source_id = id;
    
    // notice all consumer
    std::vector<SrsLiveConsumer*>::iterator it;
    for (it = consumers.begin(); it != consumers.end(); ++it) {
        SrsLiveConsumer* consumer = *it;
        consumer->update_source_id();
    }
    
    return err;
}

SrsContextId SrsLiveSource::source_id()
{
    return _source_id;
}

SrsContextId SrsLiveSource::pre_source_id()
{
    return _pre_source_id;
}

bool SrsLiveSource::inactive()
{
    return _can_publish;
}

void SrsLiveSource::update_auth(SrsRequest* r)
{
    req->update_auth(r);
}

bool SrsLiveSource::can_publish(bool is_edge)
{
    // TODO: FIXME: Should check the status of bridge.

    if (is_edge) {
        return publish_edge->can_publish();
    }
    
    return _can_publish;
}

srs_error_t SrsLiveSource::on_meta_data(SrsCommonMessage* msg, SrsOnMetaDataPacket* metadata)
{
    srs_error_t err = srs_success;

    if ((err = format_->on_metadata(metadata)) != srs_success) {
        return srs_error_wrap(err, "Format parse metadata");
    }
    
    // if allow atc_auto and bravo-atc detected, open atc for vhost.
    SrsAmf0Any* prop = NULL;
    atc = _srs_config->get_atc(req->vhost);
    if (_srs_config->get_atc_auto(req->vhost)) {
        if ((prop = metadata->metadata->get_property("bravo_atc")) != NULL) {
            if (prop->is_string() && prop->to_str() == "true") {
                atc = true;
            }
        }
    }
    
    // Update the meta cache.
    bool updated = false;
    if ((err = meta->update_data(&msg->header, metadata, updated)) != srs_success) {
        return srs_error_wrap(err, "update metadata");
    }
    if (!updated) {
        return err;
    }
    
    // when already got metadata, drop when reduce sequence header.
    bool drop_for_reduce = false;
    if (meta->data() && _srs_config->get_reduce_sequence_header(req->vhost)) {
        drop_for_reduce = true;
        srs_warn("drop for reduce sh metadata, size=%d", msg->size);
    }
    
    // copy to all consumer
    if (!drop_for_reduce) {
        std::vector<SrsLiveConsumer*>::iterator it;
        for (it = consumers.begin(); it != consumers.end(); ++it) {
            SrsLiveConsumer* consumer = *it;
            if ((err = consumer->enqueue(meta->data(), atc, jitter_algorithm)) != srs_success) {
                return srs_error_wrap(err, "consume metadata");
            }
        }
    }
    
    // Copy to hub to all utilities.
    return hub->on_meta_data(meta->data(), metadata);
}

srs_error_t SrsLiveSource::on_audio(SrsCommonMessage* shared_audio)
{
    srs_error_t err = srs_success;

    // Detect where stream is monotonically increasing.
    if (!mix_correct && is_monotonically_increase) {
        if (last_packet_time > 0 && shared_audio->header.timestamp < last_packet_time) {
            is_monotonically_increase = false;
            srs_warn("AUDIO: Timestamp %" PRId64 "=>%" PRId64 ", may need mix_correct.",
                     last_packet_time, shared_audio->header.timestamp);
        }
    }
    last_packet_time = shared_audio->header.timestamp;

    // convert shared_audio to msg, user should not use shared_audio again.
    // the payload is transfer to msg, and set to NULL in shared_audio.
    SrsSharedPtrMessage msg;
    if ((err = msg.create(shared_audio)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }

    return on_frame(&msg);
}

srs_error_t SrsLiveSource::on_frame(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;
    
    // directly process the audio message.
    if (!mix_correct) {
        if (msg->is_audio()) {
            return on_audio_imp(msg);
        } else {
            return on_video_imp(msg);
        }
    }
    
    // insert msg to the queue.
    mix_queue->push(msg->copy());
    
    // fetch someone from mix queue.
    SrsSharedPtrMessage* m = mix_queue->pop();
    if (!m) {
        return err;
    }
    
    // consume the monotonically increase message.
    if (m->is_audio()) {
        err = on_audio_imp(m);
    } else {
        err = on_video_imp(m);
    }
    srs_freep(m);
    
    return err;
}

srs_error_t SrsLiveSource::on_audio_imp(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format_->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as G.711 codec
    if (!format_->acodec) {
        return err;
    }

    // Whether current packet is sequence header. Note that MP3 does not have one, but we use the first packet as it.
    bool is_sequence_header = format_->is_aac_sequence_header() || format_->is_mp3_sequence_header();

    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && meta->previous_ash() && _srs_config->get_reduce_sequence_header(req->vhost)) {
        if (meta->previous_ash()->size == msg->size) {
            drop_for_reduce = srs_bytes_equals(meta->previous_ash()->payload, msg->payload, msg->size);
            srs_warn("drop for reduce sh audio, size=%d", msg->size);
        }
    }
    
    // Copy to hub to all utilities.
    if ((err = hub->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "consume audio");
    }

    // For bridge to consume the message.
    if (bridge_ && (err = bridge_->on_frame(msg)) != srs_success) {
        return srs_error_wrap(err, "bridge consume audio");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsLiveConsumer* consumer = consumers.at(i);
            if ((err = consumer->enqueue(msg, atc, jitter_algorithm)) != srs_success) {
                return srs_error_wrap(err, "consume message");
            }
        }
    }
    
    // Refresh the sequence header in metadata.
    if (is_sequence_header || !meta->ash()) {
        if ((err = meta->update_ash(msg)) != srs_success) {
            return srs_error_wrap(err, "meta consume audio");
        }
    }
    
    // when sequence header, donot push to gop cache and adjust the timestamp.
    if (is_sequence_header) {
        return err;
    }
    
    // cache the last gop packets
    if ((err = gop_cache->cache(msg)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume audio");
    }

    // if atc, update the sequence header to abs time.
    if (atc) {
        if (meta->ash()) {
            meta->ash()->timestamp = msg->timestamp;
        }
        if (meta->data()) {
            meta->data()->timestamp = msg->timestamp;
        }
    }
    
    return err;
}

srs_error_t SrsLiveSource::on_video(SrsCommonMessage* shared_video)
{
    srs_error_t err = srs_success;

    // Detect where stream is monotonically increasing.
    if (!mix_correct && is_monotonically_increase) {
        if (last_packet_time > 0 && shared_video->header.timestamp < last_packet_time) {
            is_monotonically_increase = false;
            srs_warn("VIDEO: Timestamp %" PRId64 "=>%" PRId64 ", may need mix_correct.",
                     last_packet_time, shared_video->header.timestamp);
        }
    }
    last_packet_time = shared_video->header.timestamp;

    // drop any unknown header video.
    // @see https://github.com/ossrs/srs/issues/421
    if (!SrsFlvVideo::acceptable(shared_video->payload, shared_video->size)) {
        char b0 = 0x00;
        if (shared_video->size > 0) {
            b0 = shared_video->payload[0];
        }

        srs_warn("drop unknown header video, size=%d, bytes[0]=%#x", shared_video->size, b0);
        return err;
    }

    // convert shared_video to msg, user should not use shared_video again.
    // the payload is transfer to msg, and set to NULL in shared_video.
    SrsSharedPtrMessage msg;
    if ((err = msg.create(shared_video)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }

    return on_frame(&msg);
}

srs_error_t SrsLiveSource::on_video_imp(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    bool is_sequence_header = SrsFlvVideo::sh(msg->payload, msg->size);

    // user can disable the sps parse to workaround when parse sps failed.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sequence_header) {
        format_->avc_parse_sps = _srs_config->get_parse_sps(req->vhost);
    }

    if ((err = format_->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as H.263 codec
    if (!format_->vcodec) {
        return err;
    }
    
    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && meta->previous_vsh() && _srs_config->get_reduce_sequence_header(req->vhost)) {
        if (meta->previous_vsh()->size == msg->size) {
            drop_for_reduce = srs_bytes_equals(meta->previous_vsh()->payload, msg->payload, msg->size);
            srs_warn("drop for reduce sh video, size=%d", msg->size);
        }
    }
    
    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (is_sequence_header && (err = meta->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }
    
    // Copy to hub to all utilities.
    if ((err = hub->on_video(msg, is_sequence_header)) != srs_success) {
        return srs_error_wrap(err, "hub consume video");
    }

    // For bridge to consume the message.
    if (bridge_ && (err = bridge_->on_frame(msg)) != srs_success) {
        return srs_error_wrap(err, "bridge consume video");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsLiveConsumer* consumer = consumers.at(i);
            if ((err = consumer->enqueue(msg, atc, jitter_algorithm)) != srs_success) {
                return srs_error_wrap(err, "consume video");
            }
        }
    }
    
    // when sequence header, donot push to gop cache and adjust the timestamp.
    if (is_sequence_header) {
        return err;
    }
    
    // cache the last gop packets
    if ((err = gop_cache->cache(msg)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume vdieo");
    }
    
    // if atc, update the sequence header to abs time.
    if (atc) {
        if (meta->vsh()) {
            meta->vsh()->timestamp = msg->timestamp;
        }
        if (meta->data()) {
            meta->data()->timestamp = msg->timestamp;
        }
    }
    
    return err;
}

srs_error_t SrsLiveSource::on_aggregate(SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(msg->payload, msg->size));

    // the aggregate message always use abs time.
    int delta = -1;
    
    while (!stream->empty()) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate");
        }
        int8_t type = stream->read_1bytes();
        
        if (!stream->require(3)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate");
        }
        int32_t data_size = stream->read_3bytes();
        
        if (data_size < 0) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate size");
        }
        
        if (!stream->require(3)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate time");
        }
        int32_t timestamp = stream->read_3bytes();
        
        if (!stream->require(1)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate time(high bits)");
        }
        int32_t time_h = stream->read_1bytes();
        
        timestamp |= time_h<<24;
        timestamp &= 0x7FFFFFFF;
        
        // adjust abs timestamp in aggregate msg.
        // only -1 means uninitialized delta.
        if (delta == -1) {
            delta = (int)msg->header.timestamp - (int)timestamp;
        }
        timestamp += delta;
        
        if (!stream->require(3)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate stream id");
        }
        int32_t stream_id = stream->read_3bytes();
        
        if (data_size > 0 && !stream->require(data_size)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate data");
        }
        
        // to common message.
        SrsCommonMessage o;
        
        o.header.message_type = type;
        o.header.payload_length = data_size;
        o.header.timestamp_delta = timestamp;
        o.header.timestamp = timestamp;
        o.header.stream_id = stream_id;
        o.header.prefer_cid = msg->header.prefer_cid;
        
        if (data_size > 0) {
            o.size = data_size;
            o.payload = new char[o.size];
            stream->read_bytes(o.payload, o.size);
        }
        
        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate previous tag size");
        }
        stream->read_4bytes();
        
        // process parsed message
        if (o.header.is_audio()) {
            if ((err = on_audio(&o)) != srs_success) {
                return srs_error_wrap(err, "consume audio");
            }
        } else if (o.header.is_video()) {
            if ((err = on_video(&o)) != srs_success) {
                return srs_error_wrap(err, "consume video");
            }
        }
    }
    
    return err;
}

srs_error_t SrsLiveSource::on_publish()
{
    srs_error_t err = srs_success;
    
    // update the request object.
    srs_assert(req);
    
    _can_publish = false;
    
    // whatever, the publish thread is the source or edge source,
    // save its id to srouce id.
    if ((err = on_source_id_changed(_srs_context->get_id())) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }
    
    // reset the mix queue.
    mix_queue->clear();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->clear();
    
    // detect the monotonically again.
    is_monotonically_increase = true;
    last_packet_time = 0;
    
    // Notify the hub about the publish event.
    if ((err = hub->on_publish()) != srs_success) {
        return srs_error_wrap(err, "hub publish");
    }
    
    // notify the handler.
    srs_assert(handler);
    if ((err = handler->on_publish(req)) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    if (bridge_ && (err = bridge_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "bridge publish");
    }

    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_publish(req, _source_id.c_str());

    // When no players, the publisher is idle now.
    if (consumers.empty()) {
        publisher_idle_at_ = srs_get_system_time();
    }
    
    return err;
}

void SrsLiveSource::on_unpublish()
{
    // ignore when already unpublished.
    if (_can_publish) {
        return;
    }
    
    // Notify the hub about the unpublish event.
    hub->on_unpublish();
    
    // only clear the gop cache,
    // donot clear the sequence header, for it maybe not changed,
    // when drop dup sequence header, drop the metadata also.
    gop_cache->clear();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->update_previous_vsh();
    meta->update_previous_ash();

    srs_trace("cleanup when unpublish");

    if (!_source_id.empty()) {
        _pre_source_id = _source_id;
    }
    _source_id = SrsContextId();

    // notify the handler.
    srs_assert(handler);
    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_stream_close(req);

    handler->on_unpublish(req);

    if (bridge_) {
        bridge_->on_unpublish();
        srs_freep(bridge_);
    }

    // no consumer, stream is die.
    if (consumers.empty()) {
        stream_die_at_ = srs_get_system_time();
    }

    // Note that we should never set to unpublish before any other handler is done, especially the handler
    // which is actually an http stream that unmounts the HTTP path for streaming, because there maybe some
    // coroutine switch in these handlers.
    _can_publish = true;
}

srs_error_t SrsLiveSource::create_consumer(SrsLiveConsumer*& consumer)
{
    srs_error_t err = srs_success;

    // for edge, when play edge stream, check the state
    if (_srs_config->get_vhost_is_edge(req->vhost)) {
        // notice edge to start for the first client.
        if ((err = play_edge->on_client_play()) != srs_success) {
            return srs_error_wrap(err, "play edge");
        }
    }

    consumer = new SrsLiveConsumer(this);
    consumers.push_back(consumer);

    // There are more than one consumer, so reset the timeout.
    stream_die_at_ = 0;
    publisher_idle_at_ = 0;
    
    return err;
}

srs_error_t SrsLiveSource::consumer_dumps(SrsLiveConsumer* consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    srs_utime_t queue_size = _srs_config->get_queue_length(req->vhost);
    consumer->set_queue_size(queue_size);

    // if atc, update the sequence header to gop cache time.
    if (atc && !gop_cache->empty()) {
        if (meta->data()) {
            meta->data()->timestamp = srsu2ms(gop_cache->start_time());
        }
        if (meta->vsh()) {
            meta->vsh()->timestamp = srsu2ms(gop_cache->start_time());
        }
        if (meta->ash()) {
            meta->ash()->timestamp = srsu2ms(gop_cache->start_time());
        }
    }

    // If stream is publishing, dumps the sequence header and gop cache.
    if (hub->active()) {
        // Copy metadata and sequence header to consumer.
        if ((err = meta->dumps(consumer, atc, jitter_algorithm, dm, ds)) != srs_success) {
            return srs_error_wrap(err, "meta dumps");
        }

        // copy gop cache to client.
        if (dg && (err = gop_cache->dump(consumer, atc, jitter_algorithm)) != srs_success) {
            return srs_error_wrap(err, "gop cache dumps");
        }
    }

    // print status.
    if (dg) {
        srs_trace("create consumer, active=%d, queue_size=%dms, jitter=%d", hub->active(), srsu2msi(queue_size), jitter_algorithm);
    } else {
        srs_trace("create consumer, active=%d, ignore gop cache, jitter=%d", hub->active(), jitter_algorithm);
    }

    return err;
}

void SrsLiveSource::on_consumer_destroy(SrsLiveConsumer* consumer)
{
    std::vector<SrsLiveConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        it = consumers.erase(it);
    }

    if (consumers.empty()) {
        play_edge->on_all_client_stop();

        // If no publishers, the stream is die.
        if (_can_publish) {
            stream_die_at_ = srs_get_system_time();
        }

        // For edge server, the stream die when the last player quit, because the edge stream is created by player
        // activities, so it should die when all players quit.
        if (_srs_config->get_vhost_is_edge(req->vhost)) {
            stream_die_at_ = srs_get_system_time();
        }

        // When no players, the publisher is idle now.
        publisher_idle_at_ = srs_get_system_time();
    }
}

void SrsLiveSource::set_cache(bool enabled)
{
    gop_cache->set(enabled);
}

void SrsLiveSource::set_gop_cache_max_frames(int v)
{
    gop_cache->set_gop_cache_max_frames(v);
}

SrsRtmpJitterAlgorithm SrsLiveSource::jitter()
{
    return jitter_algorithm;
}

srs_error_t SrsLiveSource::on_edge_start_publish()
{
    return publish_edge->on_client_publish();
}

// TODO: FIXME: Use edge strategy pattern.
srs_error_t SrsLiveSource::on_edge_proxy_publish(SrsCommonMessage* msg)
{
    return publish_edge->on_proxy_publish(msg);
}

void SrsLiveSource::on_edge_proxy_unpublish()
{
    publish_edge->on_proxy_unpublish();
}

string SrsLiveSource::get_curr_origin()
{
    return play_edge->get_curr_origin();
}

