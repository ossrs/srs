//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtmp_source.hpp>

#include <algorithm>
#include <sstream>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_encoder.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_hds.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

#define CONST_MAX_JITTER_MS 250
#define CONST_MAX_JITTER_MS_NEG -250
#define DEFAULT_FRAME_TIME_MS 10

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
    last_pkt_correct_time_ = -1;
    last_pkt_time_ = 0;
}

SrsRtmpJitter::~SrsRtmpJitter()
{
}

srs_error_t SrsRtmpJitter::correct(SrsMediaPacket *msg, SrsRtmpJitterAlgorithm ag)
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
            // for the first time, last_pkt_correct_time_ is -1.
            if (last_pkt_correct_time_ == -1) {
                last_pkt_correct_time_ = msg->timestamp_;
            }
            msg->timestamp_ -= last_pkt_correct_time_;
            return err;
        }

        // other algorithm, ignore.
        return err;
    }

    // full jitter algorithm, do jitter correct.
    // set to 0 for metadata.
    if (!msg->is_av()) {
        msg->timestamp_ = 0;
        return err;
    }

    /**
     * we use a very simple time jitter detect/correct algorithm:
     * 1. delta: ensure the delta is positive and valid,
     *     we set the delta to DEFAULT_FRAME_TIME_MS,
     *     if the delta of time is nagative or greater than CONST_MAX_JITTER_MS.
     * 2. last_pkt_time_: specifies the original packet time,
     *     is used to detect next jitter.
     * 3. last_pkt_correct_time_: simply add the positive delta,
     *     and enforce the time monotonically.
     */
    int64_t time = msg->timestamp_;
    int64_t delta = time - last_pkt_time_;

    // if jitter detected, reset the delta.
    if (delta < CONST_MAX_JITTER_MS_NEG || delta > CONST_MAX_JITTER_MS) {
        // use default 10ms to notice the problem of stream.
        // @see https://github.com/ossrs/srs/issues/425
        delta = DEFAULT_FRAME_TIME_MS;
    }

    last_pkt_correct_time_ = srs_max(0, last_pkt_correct_time_ + delta);

    msg->timestamp_ = last_pkt_correct_time_;
    last_pkt_time_ = time;

    return err;
}

int64_t SrsRtmpJitter::get_time()
{
    return last_pkt_correct_time_;
}

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
SrsFastVector::SrsFastVector()
{
    count_ = 0;
    nb_msgs_ = 8;
    msgs_ = new SrsMediaPacket *[nb_msgs_];
}

SrsFastVector::~SrsFastVector()
{
    free();
    srs_freepa(msgs_);
}

int SrsFastVector::size()
{
    return count_;
}

int SrsFastVector::begin()
{
    return 0;
}

int SrsFastVector::end()
{
    return count_;
}

SrsMediaPacket **SrsFastVector::data()
{
    return msgs_;
}

SrsMediaPacket *SrsFastVector::at(int index)
{
    srs_assert(index < count_);
    return msgs_[index];
}

void SrsFastVector::clear()
{
    count_ = 0;
}

void SrsFastVector::erase(int _begin, int _end)
{
    srs_assert(_begin < _end);

    // move all erased to previous.
    for (int i = 0; i < count_ - _end; i++) {
        msgs_[_begin + i] = msgs_[_end + i];
    }

    // update the count.
    count_ -= _end - _begin;
}

void SrsFastVector::push_back(SrsMediaPacket *msg)
{
    // increase vector.
    if (count_ >= nb_msgs_) {
        int size = srs_max(SRS_PERF_MW_MSGS * 8, nb_msgs_ * 2);
        SrsMediaPacket **buf = msgs_;
        msgs_ = new SrsMediaPacket *[size];
        for (int i = 0; i < nb_msgs_; i++) {
            msgs_[i] = buf[i];
        }
        srs_info("fast vector incrase %d=>%d", nb_msgs_, size);

        // use new array.
        srs_freepa(buf);
        nb_msgs_ = size;
    }

    msgs_[count_++] = msg;
}

void SrsFastVector::free()
{
    for (int i = 0; i < count_; i++) {
        SrsMediaPacket *msg = msgs_[i];
        srs_freep(msg);
    }
    count_ = 0;
}
#endif

ISrsMessageQueue::ISrsMessageQueue()
{
}

ISrsMessageQueue::~ISrsMessageQueue()
{
}

SrsMessageQueue::SrsMessageQueue(bool ignore_shrink)
{
    _ignore_shrink = ignore_shrink;
    max_queue_size_ = 0;
    av_start_time_ = av_end_time_ = -1;
}

SrsMessageQueue::~SrsMessageQueue()
{
    clear();
}

int SrsMessageQueue::size()
{
    return (int)msgs_.size();
}

srs_utime_t SrsMessageQueue::duration()
{
    return (av_end_time_ - av_start_time_);
}

void SrsMessageQueue::set_queue_size(srs_utime_t queue_size)
{
    max_queue_size_ = queue_size;
}

srs_error_t SrsMessageQueue::enqueue(SrsMediaPacket *msg, bool *is_overflow)
{
    srs_error_t err = srs_success;

    msgs_.push_back(msg);

    // If jitter is off, the timestamp of first sequence header is zero, which wll cause SRS to shrink and drop the
    // keyframes even if there is not overflow packets in queue, so we must ignore the zero timestamps, please
    // @see https://github.com/ossrs/srs/pull/2186#issuecomment-953383063
    if (msg->is_av() && (msg->timestamp_ != 0 || av_end_time_ == -1)) {
        if (av_start_time_ == -1) {
            av_start_time_ = srs_utime_t(msg->timestamp_ * SRS_UTIME_MILLISECONDS);
        }

        av_end_time_ = srs_utime_t(msg->timestamp_ * SRS_UTIME_MILLISECONDS);
    }

    if (max_queue_size_ <= 0) {
        return err;
    }

    while (av_end_time_ - av_start_time_ > max_queue_size_) {
        // notice the caller queue already overflow and shrinked.
        if (is_overflow) {
            *is_overflow = true;
        }

        shrink();
    }

    return err;
}

srs_error_t SrsMessageQueue::dump_packets(int max_count, SrsMediaPacket **pmsgs, int &count)
{
    srs_error_t err = srs_success;

    int nb_msgs = (int)msgs_.size();
    if (nb_msgs <= 0) {
        return err;
    }

    srs_assert(max_count > 0);
    count = srs_min(max_count, nb_msgs);

    SrsMediaPacket **omsgs = msgs_.data();
    memcpy(pmsgs, omsgs, count * sizeof(SrsMediaPacket *));

    SrsMediaPacket *last = omsgs[count - 1];
    av_start_time_ = srs_utime_t(last->timestamp_ * SRS_UTIME_MILLISECONDS);

    if (count >= nb_msgs) {
        // the pmsgs is big enough and clear msgs at most time.
        msgs_.clear();
    } else {
        // erase some vector elements may cause memory copy,
        // maybe can use more efficient vector.swap to avoid copy.
        // @remark for the pmsgs is big enough, for instance, SRS_PERF_MW_MSGS 128,
        //      the rtmp play client will get 128msgs once, so this branch rarely execute.
        msgs_.erase(msgs_.begin(), msgs_.begin() + count);
    }

    return err;
}

srs_error_t SrsMessageQueue::dump_packets(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm ag)
{
    srs_error_t err = srs_success;

    int nb_msgs = (int)msgs_.size();
    if (nb_msgs <= 0) {
        return err;
    }

    SrsMediaPacket **omsgs = msgs_.data();
    for (int i = 0; i < nb_msgs; i++) {
        SrsMediaPacket *msg = omsgs[i];
        if ((err = consumer->enqueue(msg, atc, ag)) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    return err;
}

void SrsMessageQueue::shrink()
{
    SrsMediaPacket *video_sh = NULL;
    SrsMediaPacket *audio_sh = NULL;
    int msgs_size = (int)msgs_.size();

    // Remove all msgs, mark the sequence headers.
    for (int i = 0; i < (int)msgs_.size(); i++) {
        SrsMediaPacket *msg = msgs_.at(i);

        if (msg->is_video() && SrsFlvVideo::sh(msg->payload(), msg->size())) {
            srs_freep(video_sh);
            video_sh = msg;
            continue;
        } else if (msg->is_audio() && SrsFlvAudio::sh(msg->payload(), msg->size())) {
            srs_freep(audio_sh);
            audio_sh = msg;
            continue;
        }

        srs_freep(msg);
    }
    msgs_.clear();

    // Update av_start_time_, the start time of queue.
    av_start_time_ = av_end_time_;

    // Push back sequence headers and update their timestamps.
    if (video_sh) {
        video_sh->timestamp_ = srsu2ms(av_end_time_);
        msgs_.push_back(video_sh);
    }
    if (audio_sh) {
        audio_sh->timestamp_ = srsu2ms(av_end_time_);
        msgs_.push_back(audio_sh);
    }

    if (!_ignore_shrink) {
        srs_trace("shrinking, size=%d, removed=%d, max=%dms", (int)msgs_.size(), msgs_size - (int)msgs_.size(), srsu2msi(max_queue_size_));
    }
}

void SrsMessageQueue::clear()
{
#ifndef SRS_PERF_QUEUE_FAST_VECTOR
    std::vector<SrsMediaPacket *>::iterator it;

    for (it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsMediaPacket *msg = *it;
        srs_freep(msg);
    }
#else
    msgs_.free();
#endif

    msgs_.clear();

    av_start_time_ = av_end_time_ = -1;
}

ISrsWakable::ISrsWakable()
{
}

ISrsWakable::~ISrsWakable()
{
}

ISrsLiveConsumer::ISrsLiveConsumer()
{
}

ISrsLiveConsumer::~ISrsLiveConsumer()
{
}

SrsLiveConsumer::SrsLiveConsumer(ISrsLiveSource *s)
{
    source_ = s;
    paused_ = false;
    jitter_ = new SrsRtmpJitter();
    queue_ = new SrsMessageQueue();
    should_update_source_id_ = false;

#ifdef SRS_PERF_QUEUE_COND_WAIT
    mw_wait_ = srs_cond_new();
    mw_min_msgs_ = 0;
    mw_duration_ = 0;
    mw_waiting_ = false;
#endif
}

SrsLiveConsumer::~SrsLiveConsumer()
{
    source_->on_consumer_destroy(this);
    srs_freep(jitter_);
    srs_freep(queue_);

#ifdef SRS_PERF_QUEUE_COND_WAIT
    srs_cond_destroy(mw_wait_);
#endif
}

void SrsLiveConsumer::set_queue_size(srs_utime_t queue_size)
{
    queue_->set_queue_size(queue_size);
}

void SrsLiveConsumer::update_source_id()
{
    should_update_source_id_ = true;
}

int64_t SrsLiveConsumer::get_time()
{
    return jitter_->get_time();
}

srs_error_t SrsLiveConsumer::enqueue(SrsMediaPacket *shared_msg, bool atc, SrsRtmpJitterAlgorithm ag)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *msg = shared_msg->copy();

    if (!atc) {
        if ((err = jitter_->correct(msg, ag)) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    if ((err = queue_->enqueue(msg, NULL)) != srs_success) {
        return srs_error_wrap(err, "enqueue message");
    }

#ifdef SRS_PERF_QUEUE_COND_WAIT
    // fire the mw when msgs is enough.
    if (mw_waiting_) {
        // For RTMP, we wait for messages and duration.
        srs_utime_t duration = queue_->duration();
        bool match_min_msgs = queue_->size() > mw_min_msgs_;

        // For ATC, maybe the SH timestamp bigger than A/V packet,
        // when encoder republish or overflow.
        // @see https://github.com/ossrs/srs/pull/749
        if (atc && duration < 0) {
            srs_cond_signal(mw_wait_);
            mw_waiting_ = false;
            return err;
        }

        // when duration ok, signal to flush.
        if (match_min_msgs && duration >= mw_duration_) {
            srs_cond_signal(mw_wait_);
            mw_waiting_ = false;
            return err;
        }
    }
#endif

    return err;
}

srs_error_t SrsLiveConsumer::dump_packets(SrsMessageArray *msgs, int &count)
{
    srs_error_t err = srs_success;

    srs_assert(count >= 0);
    srs_assert(msgs->max_ > 0);

    // the count used as input to reset the max if positive.
    int max = count ? srs_min(count, msgs->max_) : msgs->max_;

    // the count specifies the max acceptable count,
    // here maybe 1+, and we must set to 0 when got nothing.
    count = 0;

    if (should_update_source_id_) {
        srs_trace("update source_id=%s/%s", source_->source_id().c_str(), source_->pre_source_id().c_str());
        should_update_source_id_ = false;
    }

    // paused, return nothing.
    if (paused_) {
        return err;
    }

    // pump msgs from queue.
    if ((err = queue_->dump_packets(max, msgs->msgs_, count)) != srs_success) {
        return srs_error_wrap(err, "dump packets");
    }

    return err;
}

#ifdef SRS_PERF_QUEUE_COND_WAIT
void SrsLiveConsumer::wait(int nb_msgs, srs_utime_t msgs_duration)
{
    if (paused_) {
        srs_usleep(SRS_CONSTS_RTMP_PULSE);
        return;
    }

    mw_min_msgs_ = nb_msgs;
    mw_duration_ = msgs_duration;

    srs_utime_t duration = queue_->duration();
    bool match_min_msgs = queue_->size() > mw_min_msgs_;

    // when duration ok, signal to flush.
    if (match_min_msgs && duration > mw_duration_) {
        return;
    }

    // the enqueue will notify this cond.
    mw_waiting_ = true;

    // use cond block wait for high performance mode.
    srs_cond_wait(mw_wait_);
}
#endif

srs_error_t SrsLiveConsumer::on_play_client_pause(bool is_pause)
{
    srs_error_t err = srs_success;

    srs_trace("stream consumer change pause state %d=>%d", paused_, is_pause);
    paused_ = is_pause;

    return err;
}

void SrsLiveConsumer::wakeup()
{
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (mw_waiting_) {
        srs_cond_signal(mw_wait_);
        mw_waiting_ = false;
    }
#endif
}

SrsGopCache::SrsGopCache()
{
    cached_video_count_ = 0;
    enable_gop_cache_ = true;
    audio_after_last_video_count_ = 0;
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
    enable_gop_cache_ = v;

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
    return enable_gop_cache_;
}

srs_error_t SrsGopCache::cache(SrsMediaPacket *shared_msg)
{
    srs_error_t err = srs_success;

    if (!enable_gop_cache_) {
        return err;
    }

    // the gop cache know when to gop it.
    SrsMediaPacket *msg = shared_msg;

    // got video, update the video count if acceptable
    if (msg->is_video()) {
        // Drop video when not h.264 or h.265.
        bool codec_ok = SrsFlvVideo::h264(msg->payload(), msg->size());
        codec_ok = codec_ok ? true : SrsFlvVideo::hevc(msg->payload(), msg->size());
        if (!codec_ok)
            return err;

        cached_video_count_++;
        audio_after_last_video_count_ = 0;
    }

    // no acceptable video or pure audio, disable the cache.
    if (pure_audio()) {
        return err;
    }

    // ok, gop cache enabled, and got an audio.
    if (msg->is_audio()) {
        audio_after_last_video_count_++;
    }

    // clear gop cache when pure audio count overflow
    if (audio_after_last_video_count_ > SRS_PURE_AUDIO_GUESS_COUNT) {
        srs_warn("clear gop cache for guess pure audio overflow");
        clear();
        return err;
    }

    // clear gop cache when got key frame
    if (msg->is_video() && SrsFlvVideo::keyframe(msg->payload(), msg->size())) {
        clear();

        // curent msg is video frame, so we set to 1.
        cached_video_count_ = 1;
    }

    // cache the frame.
    gop_cache_.push_back(msg->copy());

    // Clear gop cache if exceed the max frames.
    if (gop_cache_max_frames_ > 0 && gop_cache_.size() > (size_t)gop_cache_max_frames_) {
        srs_warn("Gop cache exceed max frames=%d, total=%d, videos=%d, aalvc=%d",
                 gop_cache_max_frames_, (int)gop_cache_.size(), cached_video_count_, audio_after_last_video_count_);
        clear();
    }

    return err;
}

void SrsGopCache::clear()
{
    std::vector<SrsMediaPacket *>::iterator it;
    for (it = gop_cache_.begin(); it != gop_cache_.end(); ++it) {
        SrsMediaPacket *msg = *it;
        srs_freep(msg);
    }
    gop_cache_.clear();

    cached_video_count_ = 0;
    audio_after_last_video_count_ = 0;
}

srs_error_t SrsGopCache::dump(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm jitter_algorithm)
{
    srs_error_t err = srs_success;

    std::vector<SrsMediaPacket *>::iterator it;
    for (it = gop_cache_.begin(); it != gop_cache_.end(); ++it) {
        SrsMediaPacket *msg = *it;
        if ((err = consumer->enqueue(msg, atc, jitter_algorithm)) != srs_success) {
            return srs_error_wrap(err, "enqueue message");
        }
    }
    srs_trace("dispatch cached gop success. count=%d, duration=%d", (int)gop_cache_.size(), consumer->get_time());

    return err;
}

bool SrsGopCache::empty()
{
    return gop_cache_.empty();
}

// LCOV_EXCL_START
srs_utime_t SrsGopCache::start_time()
{
    if (empty()) {
        return 0;
    }

    SrsMediaPacket *msg = gop_cache_[0];
    srs_assert(msg);

    return srs_utime_t(msg->timestamp_ * SRS_UTIME_MILLISECONDS);
}
// LCOV_EXCL_STOP

bool SrsGopCache::pure_audio()
{
    return cached_video_count_ == 0;
}

ISrsLiveSourceHandler::ISrsLiveSourceHandler()
{
}

ISrsLiveSourceHandler::~ISrsLiveSourceHandler()
{
}

// LCOV_EXCL_START
// TODO: FIXME: Remove it?
bool srs_hls_can_continue(int ret, SrsMediaPacket *sh, SrsMediaPacket *msg)
{
    // only continue for decode error.
    if (ret != ERROR_HLS_DECODE_ERROR) {
        return false;
    }

    // when video size equals to sequence header,
    // the video actually maybe a sequence header,
    // continue to make ffmpeg happy.
    if (sh && sh->size() == msg->size()) {
        srs_warn("the msg is actually a sequence header, ignore this packet.");
        return true;
    }

    return false;
}
// LCOV_EXCL_STOP

SrsMixQueue::SrsMixQueue()
{
    nb_videos_ = 0;
    nb_audios_ = 0;
}

SrsMixQueue::~SrsMixQueue()
{
    clear();
}

void SrsMixQueue::clear()
{
    std::multimap<int64_t, SrsMediaPacket *>::iterator it;
    for (it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsMediaPacket *msg = it->second;
        srs_freep(msg);
    }
    msgs_.clear();

    nb_videos_ = 0;
    nb_audios_ = 0;
}

void SrsMixQueue::push(SrsMediaPacket *msg)
{
    msgs_.insert(std::make_pair(msg->timestamp_, msg));

    if (msg->is_video()) {
        nb_videos_++;
    } else {
        nb_audios_++;
    }
}

SrsMediaPacket *SrsMixQueue::pop()
{
    bool mix_ok = false;

    // pure video
    if (nb_videos_ >= SRS_MIX_CORRECT_PURE_AV && nb_audios_ == 0) {
        mix_ok = true;
    }

    // pure audio
    if (nb_audios_ >= SRS_MIX_CORRECT_PURE_AV && nb_videos_ == 0) {
        mix_ok = true;
    }

    // got 1 video and 1 audio, mix ok.
    if (nb_videos_ >= 1 && nb_audios_ >= 1) {
        mix_ok = true;
    }

    if (!mix_ok) {
        return NULL;
    }

    // pop the first msg.
    std::multimap<int64_t, SrsMediaPacket *>::iterator it = msgs_.begin();
    SrsMediaPacket *msg = it->second;
    msgs_.erase(it);

    if (msg->is_video()) {
        nb_videos_--;
    } else {
        nb_audios_--;
    }

    return msg;
}

ISrsOriginHub::ISrsOriginHub()
{
}

ISrsOriginHub::~ISrsOriginHub()
{
}

SrsOriginHub::SrsOriginHub()
{
    source_ = NULL;
    req_ = NULL;
    is_active_ = false;

    hls_ = new SrsHls();
    dash_ = new SrsDash();

    dvr_ = new SrsDvr();
    dvr_->assemble();

    encoder_ = new SrsEncoder();
#ifdef SRS_HDS
    hds_ = new SrsHds();
#endif
    ng_exec_ = new SrsNgExec();

    config_ = _srs_config;
    stat_ = _srs_stat;
    hooks_ = _srs_hooks;
}

void SrsOriginHub::assemble()
{
    config_->subscribe(this);
}

SrsOriginHub::~SrsOriginHub()
{
    config_->unsubscribe(this);

    if (true) {
        std::vector<ISrsForwarder *>::iterator it;
        for (it = forwarders_.begin(); it != forwarders_.end(); ++it) {
            ISrsForwarder *forwarder = *it;
            srs_freep(forwarder);
        }
        forwarders_.clear();
    }
    srs_freep(ng_exec_);

    srs_freep(hls_);
    srs_freep(dash_);
    srs_freep(dvr_);
    srs_freep(encoder_);
#ifdef SRS_HDS
    srs_freep(hds_);
#endif

    config_ = NULL;
    stat_ = NULL;
    hooks_ = NULL;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsOriginHub::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r;
    // Because source references to this object, so we should directly use the source ptr.
    source_ = s.get();

    if ((err = hls_->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "hls initialize");
    }

    if ((err = dash_->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "dash initialize");
    }

    if ((err = dvr_->initialize(this, req_)) != srs_success) {
        return srs_error_wrap(err, "dvr initialize");
    }

    return err;
}

void SrsOriginHub::dispose()
{
    hls_->dispose();
    dash_->dispose();
}

srs_error_t SrsOriginHub::cycle()
{
    srs_error_t err = srs_success;

    if ((err = hls_->cycle()) != srs_success) {
        return srs_error_wrap(err, "hls cycle");
    }

    if ((err = dash_->cycle()) != srs_success) {
        return srs_error_wrap(err, "dash cycle");
    }

    return err;
}

bool SrsOriginHub::active()
{
    return is_active_;
}

srs_utime_t SrsOriginHub::cleanup_delay()
{
    srs_utime_t hls_delay = hls_->cleanup_delay();
    srs_utime_t dash_delay = dash_->cleanup_delay();
    return srs_max(hls_delay, dash_delay);
}

srs_error_t SrsOriginHub::on_meta_data(SrsMediaPacket *shared_metadata, SrsOnMetaDataPacket *packet)
{
    srs_error_t err = srs_success;

    // copy to all forwarders
    if (true) {
        std::vector<ISrsForwarder *>::iterator it;
        for (it = forwarders_.begin(); it != forwarders_.end(); ++it) {
            ISrsForwarder *forwarder = *it;
            if ((err = forwarder->on_meta_data(shared_metadata)) != srs_success) {
                return srs_error_wrap(err, "Forwarder consume metadata");
            }
        }
    }

    if ((err = dvr_->on_meta_data(shared_metadata)) != srs_success) {
        return srs_error_wrap(err, "DVR consume metadata");
    }

    return err;
}

srs_error_t SrsOriginHub::on_audio(SrsMediaPacket *shared_audio)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *msg = shared_audio;
    SrsRtmpFormat *format = source_->format();

    // Handle the metadata when got sequence header.
    if (format->is_aac_sequence_header() || format->is_mp3_sequence_header()) {
        srs_assert(format->acodec_);
        SrsAudioCodecConfig *c = format->acodec_;

        static int flv_sample_sizes[] = {8, 16, 0};
        static int flv_sound_types[] = {1, 2, 0};

        // when got audio stream info.
        // For AAC, convert aac_sample_rate_ index to SrsAudioSampleRate enum
        // For MP3 and other codecs, use sound_rate_ directly (FLV enum)
        SrsAudioSampleRate sample_rate = c->sound_rate_;
        if (format->acodec_->id_ == SrsAudioCodecIdAAC && c->aac_sample_rate_ < SrsAAcSampleRateNumbers) {
            sample_rate = srs_audio_sample_rate_from_number(srs_aac_srates[c->aac_sample_rate_]);
        }

        // The FLV specification force to be 1(stereo) for AAC codec, see E.4.2 Audio Tags,
        // video_file_format_spec_v10_1.pdf, page 77.
        //      If the SoundFormat indicates AAC, the SoundType should be 1 (stereo) and the
        //      SoundRate should be 3 (44 kHz).
        // FFmpeg also follows this specification, that is why it always send fixed channels
        // and sample rate to SRS.
        SrsAudioChannels channels = c->sound_type_;
        if (format->acodec_->id_ == SrsAudioCodecIdAAC) {
            channels = (c->aac_channels_ == 1) ? SrsAudioChannelsMono : SrsAudioChannelsStereo;
        }

        if ((err = stat_->on_audio_info(req_, format->acodec_->id_, sample_rate, channels, c->aac_object_)) != srs_success) {
            return srs_error_wrap(err, "stat audio");
        }

        if (format->acodec_->id_ == SrsAudioCodecIdMP3) {
            srs_trace("%dB audio sh, codec(%d, %dbits, %dchannels, %dHZ)",
                      msg->size(), c->id_, flv_sample_sizes[c->sound_size_], flv_sound_types[c->sound_type_],
                      srs_flv_srates[c->sound_rate_]);
        } else {
            srs_trace("%dB audio sh, codec(%d, profile=%s, %dchannels, %dkbps, %dHZ), flv(%dbits, %dchannels, %dHZ)",
                      msg->size(), c->id_, srs_aac_object2str(c->aac_object_).c_str(), c->aac_channels_,
                      c->audio_data_rate_ / 1000, srs_aac_srates[c->aac_sample_rate_],
                      flv_sample_sizes[c->sound_size_], flv_sound_types[c->sound_type_],
                      srs_flv_srates[c->sound_rate_]);
        }
    }

    if ((err = hls_->on_audio(msg, format)) != srs_success) {
        // apply the error strategy for hls.
        std::string hls_error_strategy = config_->get_hls_on_error(req_->vhost_);
        if (srs_config_hls_is_on_error_ignore(hls_error_strategy)) {
            srs_warn("hls: ignore audio error %s", srs_error_desc(err).c_str());
            hls_->on_unpublish();
            srs_freep(err);
        } else if (srs_config_hls_is_on_error_continue(hls_error_strategy)) {
            if (srs_hls_can_continue(srs_error_code(err), source_->meta()->ash(), msg)) {
                srs_freep(err);
            } else {
                return srs_error_wrap(err, "hls: audio");
            }
        } else {
            return srs_error_wrap(err, "hls: audio");
        }
    }

    if ((err = dash_->on_audio(msg, format)) != srs_success) {
        srs_warn("dash: ignore audio error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        dash_->on_unpublish();
    }

    if ((err = dvr_->on_audio(msg, format)) != srs_success) {
        srs_warn("dvr: ignore audio error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        dvr_->on_unpublish();
    }

#ifdef SRS_HDS
    if ((err = hds_->on_audio(msg)) != srs_success) {
        srs_warn("hds: ignore audio error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        hds_->on_unpublish();
    }
#endif

    // copy to all forwarders.
    if (true) {
        std::vector<ISrsForwarder *>::iterator it;
        for (it = forwarders_.begin(); it != forwarders_.end(); ++it) {
            ISrsForwarder *forwarder = *it;
            if ((err = forwarder->on_audio(msg)) != srs_success) {
                return srs_error_wrap(err, "forward: audio");
            }
        }
    }

    return err;
}

srs_error_t SrsOriginHub::on_video(SrsMediaPacket *shared_video, bool is_sequence_header)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *msg = shared_video;
    SrsRtmpFormat *format = source_->format();

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (format->is_avc_sequence_header()) {
        SrsVideoCodecConfig *c = format->vcodec_;
        srs_assert(c);

        // when got video stream info.
        if (c->id_ == SrsVideoCodecIdAVC) {
            err = stat_->on_video_info(req_, c->id_, c->avc_profile_, c->avc_level_, c->width_, c->height_);
            srs_trace("%dB video sh, codec(%d, profile=%s, level=%s, %dx%d, %dkbps, %.1ffps, %.1fs)",
                      msg->size(), c->id_, srs_avc_profile2str(c->avc_profile_).c_str(), srs_avc_level2str(c->avc_level_).c_str(),
                      c->width_, c->height_, c->video_data_rate_ / 1000, c->frame_rate_, c->duration_);
        } else if (c->id_ == SrsVideoCodecIdHEVC) {
            err = stat_->on_video_info(req_, c->id_, c->hevc_profile_, c->hevc_level_, c->width_, c->height_);
            srs_trace("%dB video sh, codec(%d, profile=%s, level=%s, %dx%d, %dkbps, %.1ffps, %.1fs)",
                      msg->size(), c->id_, srs_hevc_profile2str(c->hevc_profile_).c_str(), srs_hevc_level2str(c->hevc_level_).c_str(),
                      c->width_, c->height_, c->video_data_rate_ / 1000, c->frame_rate_, c->duration_);
        }
        if (err != srs_success) {
            return srs_error_wrap(err, "stat video");
        }
    }

    // Ignore video data when no sps/pps
    // @bug https://github.com/ossrs/srs/issues/703#issuecomment-578393155
    if (format->vcodec_ && !format->vcodec_->is_avc_codec_ok()) {
        return err;
    }

    if ((err = hls_->on_video(msg, format)) != srs_success) {
        // TODO: We should support more strategies.
        // apply the error strategy for hls.
        std::string hls_error_strategy = config_->get_hls_on_error(req_->vhost_);
        if (srs_config_hls_is_on_error_ignore(hls_error_strategy)) {
            srs_warn("hls: ignore video error %s", srs_error_desc(err).c_str());
            hls_->on_unpublish();
            srs_freep(err);
        } else if (srs_config_hls_is_on_error_continue(hls_error_strategy)) {
            if (srs_hls_can_continue(srs_error_code(err), source_->meta()->vsh(), msg)) {
                srs_freep(err);
            } else {
                return srs_error_wrap(err, "hls: video");
            }
        } else {
            return srs_error_wrap(err, "hls: video");
        }
    }

    if ((err = dash_->on_video(msg, format)) != srs_success) {
        srs_warn("dash: ignore video error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        dash_->on_unpublish();
    }

    if ((err = dvr_->on_video(msg, format)) != srs_success) {
        srs_warn("dvr: ignore video error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        dvr_->on_unpublish();
    }

#ifdef SRS_HDS
    if ((err = hds_->on_video(msg)) != srs_success) {
        srs_warn("hds: ignore video error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        hds_->on_unpublish();
    }
#endif

    // copy to all forwarders.
    if (!forwarders_.empty()) {
        std::vector<ISrsForwarder *>::iterator it;
        for (it = forwarders_.begin(); it != forwarders_.end(); ++it) {
            ISrsForwarder *forwarder = *it;
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
    if ((err = encoder_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "encoder publish");
    }

    if ((err = hls_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "hls publish");
    }

    if ((err = dash_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "dash publish");
    }

    // @see https://github.com/ossrs/srs/issues/1613#issuecomment-961657927
    if ((err = dvr_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "dvr publish");
    }

    // TODO: FIXME: use initialize to set req.
#ifdef SRS_HDS
    if ((err = hds_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "hds publish");
    }
#endif

    // TODO: FIXME: use initialize to set req.
    if ((err = ng_exec_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "exec publish");
    }

    is_active_ = true;

    return err;
}

void SrsOriginHub::on_unpublish()
{
    is_active_ = false;

    // destroy all forwarders
    destroy_forwarders();

    encoder_->on_unpublish();
    hls_->on_unpublish();
    dash_->on_unpublish();
    dvr_->on_unpublish();

#ifdef SRS_HDS
    hds_->on_unpublish();
#endif

    ng_exec_->on_unpublish();
}

srs_error_t SrsOriginHub::on_forwarder_start(SrsForwarder *forwarder)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *cache_metadata = source_->meta()->data();
    SrsMediaPacket *cache_sh_video = source_->meta()->vsh();
    SrsMediaPacket *cache_sh_audio = source_->meta()->ash();

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

    SrsMediaPacket *cache_metadata = source_->meta()->data();
    SrsMediaPacket *cache_sh_video = source_->meta()->vsh();
    SrsMediaPacket *cache_sh_audio = source_->meta()->ash();

    // feed the dvr the metadata/sequence header,
    // when reload to start dvr, dvr will never get the sequence header in stream,
    // use the SrsLiveSource.on_dvr_request_sh to push the sequence header to DVR.
    if (cache_metadata && (err = dvr_->on_meta_data(cache_metadata)) != srs_success) {
        return srs_error_wrap(err, "dvr metadata");
    }

    if (cache_sh_video) {
        if ((err = dvr_->on_video(cache_sh_video, source_->meta()->vsh_format())) != srs_success) {
            return srs_error_wrap(err, "dvr video");
        }
    }

    if (cache_sh_audio) {
        if ((err = dvr_->on_audio(cache_sh_audio, source_->meta()->ash_format())) != srs_success) {
            return srs_error_wrap(err, "dvr audio");
        }
    }

    return err;
}

srs_error_t SrsOriginHub::on_hls_request_sh()
{
    srs_error_t err = srs_success;

    SrsMediaPacket *cache_sh_video = source_->meta()->vsh();
    if (cache_sh_video) {
        if ((err = hls_->on_video(cache_sh_video, source_->meta()->vsh_format())) != srs_success) {
            return srs_error_wrap(err, "hls video");
        }
    }

    SrsMediaPacket *cache_sh_audio = source_->meta()->ash();
    if (cache_sh_audio) {
        if ((err = hls_->on_audio(cache_sh_audio, source_->meta()->ash_format())) != srs_success) {
            return srs_error_wrap(err, "hls audio");
        }
    }

    return err;
}

srs_error_t SrsOriginHub::create_forwarders()
{
    srs_error_t err = srs_success;

    if (!config_->get_forward_enabled(req_->vhost_)) {
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
    SrsConfDirective *conf = config_->get_forwards(req_->vhost_);
    for (int i = 0; conf && i < (int)conf->args_.size(); i++) {
        std::string forward_server = conf->args_.at(i);

        ISrsForwarder *forwarder = new SrsForwarder(this);
        forwarders_.push_back(forwarder);

        // initialize the forwarder with request.
        if ((err = forwarder->initialize(req_, forward_server)) != srs_success) {
            return srs_error_wrap(err, "init forwarder");
        }

        srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
        forwarder->set_queue_size(queue_size);

        if ((err = forwarder->on_publish()) != srs_success) {
            return srs_error_wrap(err, "start forwarder failed, vhost=%s, app=%s, stream=%s, forward-to=%s",
                                  req_->vhost_.c_str(), req_->app_.c_str(), req_->stream_.c_str(), forward_server.c_str());
        }
    }

    return err;
}

srs_error_t SrsOriginHub::create_backend_forwarders(bool &applied)
{
    srs_error_t err = srs_success;

    // default not configure backend service
    applied = false;

    SrsConfDirective *conf = config_->get_forward_backend(req_->vhost_);
    if (!conf || conf->arg0().empty()) {
        return err;
    }

    // configure backend service
    applied = true;

    // only get first backend url
    std::string backend_url = conf->arg0();

    // get urls on forward backend
    std::vector<std::string> urls;
    if ((err = hooks_->on_forward_backend(backend_url, req_, urls)) != srs_success) {
        return srs_error_wrap(err, "get forward backend failed, backend=%s", backend_url.c_str());
    }

    // create forwarders by urls
    std::vector<std::string>::iterator it;
    for (it = urls.begin(); it != urls.end(); ++it) {
        std::string url = *it;

        // create temp Request by url
        SrsUniquePtr<ISrsRequest> req(new SrsRequest());
        srs_net_url_parse_rtmp_url(url, req->tcUrl_, req->stream_);
        srs_net_url_parse_tcurl(req->tcUrl_, req->schema_, req->host_, req->vhost_, req->app_, req->stream_, req->port_, req->param_);

        // create forwarder
        ISrsForwarder *forwarder = new SrsForwarder(this);
        forwarders_.push_back(forwarder);

        std::stringstream forward_server;
        forward_server << req->host_ << ":" << req->port_;

        // initialize the forwarder with request.
        if ((err = forwarder->initialize(req.get(), forward_server.str())) != srs_success) {
            return srs_error_wrap(err, "init backend forwarder failed, forward-to=%s", forward_server.str().c_str());
        }

        srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
        forwarder->set_queue_size(queue_size);

        if ((err = forwarder->on_publish()) != srs_success) {
            return srs_error_wrap(err, "start backend forwarder failed, vhost=%s, app=%s, stream=%s, forward-to=%s",
                                  req_->vhost_.c_str(), req_->app_.c_str(), req_->stream_.c_str(), forward_server.str().c_str());
        }
    }

    return err;
}

void SrsOriginHub::destroy_forwarders()
{
    std::vector<ISrsForwarder *>::iterator it;
    for (it = forwarders_.begin(); it != forwarders_.end(); ++it) {
        ISrsForwarder *forwarder = *it;
        forwarder->on_unpublish();
        srs_freep(forwarder);
    }
    forwarders_.clear();
}

SrsMetaCache::SrsMetaCache()
{
    meta_ = video_ = audio_ = NULL;
    previous_video_ = previous_audio_ = NULL;
    vformat_ = new SrsRtmpFormat();
    aformat_ = new SrsRtmpFormat();
}

SrsMetaCache::~SrsMetaCache()
{
    dispose();
    srs_freep(vformat_);
    srs_freep(aformat_);
}

void SrsMetaCache::dispose()
{
    clear();
    srs_freep(previous_video_);
    srs_freep(previous_audio_);
}

void SrsMetaCache::clear()
{
    srs_freep(meta_);
    srs_freep(video_);
    srs_freep(audio_);
}

SrsMediaPacket *SrsMetaCache::data()
{
    return meta_;
}

SrsMediaPacket *SrsMetaCache::vsh()
{
    return video_;
}

SrsFormat *SrsMetaCache::vsh_format()
{
    return vformat_;
}

SrsMediaPacket *SrsMetaCache::ash()
{
    return audio_;
}

SrsFormat *SrsMetaCache::ash_format()
{
    return aformat_;
}

srs_error_t SrsMetaCache::dumps(ISrsLiveConsumer *consumer, bool atc, SrsRtmpJitterAlgorithm ag, bool dm, bool ds)
{
    srs_error_t err = srs_success;

    // copy metadata.
    if (dm && meta_ && (err = consumer->enqueue(meta_, atc, ag)) != srs_success) {
        return srs_error_wrap(err, "enqueue metadata");
    }

    // copy sequence header
    // copy audio sequence first, for hls to fast parse the "right" audio codec.
    if (aformat_ && aformat_->acodec_ && aformat_->acodec_->id_ != SrsAudioCodecIdMP3) {
        if (ds && audio_ && (err = consumer->enqueue(audio_, atc, ag)) != srs_success) {
            return srs_error_wrap(err, "enqueue audio sh");
        }
    }

    if (ds && video_ && (err = consumer->enqueue(video_, atc, ag)) != srs_success) {
        return srs_error_wrap(err, "enqueue video sh");
    }

    return err;
}

SrsMediaPacket *SrsMetaCache::previous_vsh()
{
    return previous_video_;
}

SrsMediaPacket *SrsMetaCache::previous_ash()
{
    return previous_audio_;
}

void SrsMetaCache::update_previous_vsh()
{
    srs_freep(previous_video_);
    previous_video_ = video_ ? video_->copy() : NULL;
}

void SrsMetaCache::update_previous_ash()
{
    srs_freep(previous_audio_);
    previous_audio_ = audio_ ? audio_->copy() : NULL;
}

srs_error_t SrsMetaCache::update_data(SrsMessageHeader *header, SrsOnMetaDataPacket *metadata, bool &updated)
{
    updated = false;

    srs_error_t err = srs_success;

    SrsAmf0Any *prop = NULL;

    // when exists the duration, remove it to make ExoPlayer happy.
    if (metadata->metadata_->get_property("duration") != NULL) {
        metadata->metadata_->remove("duration");
    }

    // generate metadata info to print
    std::stringstream ss;
    if ((prop = metadata->metadata_->ensure_property_number("width")) != NULL) {
        ss << ", width=" << (int)prop->to_number();
    }
    if ((prop = metadata->metadata_->ensure_property_number("height")) != NULL) {
        ss << ", height=" << (int)prop->to_number();
    }
    if ((prop = metadata->metadata_->ensure_property_number("videocodecid")) != NULL) {
        ss << ", vcodec=" << (int)prop->to_number();
    }
    if ((prop = metadata->metadata_->ensure_property_number("audiocodecid")) != NULL) {
        ss << ", acodec=" << (int)prop->to_number();
    }
    srs_trace("got metadata%s", ss.str().c_str());

    // add server info to metadata
    metadata->metadata_->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));

    // version, for example, 1.0.0
    // add version to metadata, please donot remove it, for debug.
    metadata->metadata_->set("server_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));

    // encode the metadata to payload
    int size = 0;
    char *payload = NULL;
    if ((err = metadata->encode(size, payload)) != srs_success) {
        return srs_error_wrap(err, "encode metadata");
    }

    if (size <= 0) {
        srs_warn("ignore the invalid metadata. size=%d", size);
        return err;
    }

    // create a shared ptr message.
    srs_freep(meta_);
    meta_ = new SrsMediaPacket();
    updated = true;

    // dump message to shared ptr message.
    // the payload/size managed by cache_metadata, user should not free it.
    SrsRtmpCommonMessage common_msg;
    if ((err = common_msg.create(header, payload, size)) != srs_success) {
        return srs_error_wrap(err, "create metadata");
    }
    common_msg.to_msg(meta_);

    return err;
}

srs_error_t SrsMetaCache::update_ash(SrsMediaPacket *msg)
{
    srs_freep(audio_);
    audio_ = msg->copy();
    update_previous_ash();
    return aformat_->on_audio(msg);
}

srs_error_t SrsMetaCache::update_vsh(SrsMediaPacket *msg)
{
    srs_freep(video_);
    video_ = msg->copy();
    update_previous_vsh();
    return vformat_->on_video(msg);
}

ISrsLiveSourceManager::ISrsLiveSourceManager()
{
}

ISrsLiveSourceManager::~ISrsLiveSourceManager()
{
}

SrsLiveSourceManager *_srs_sources = NULL;

SrsLiveSourceManager::SrsLiveSourceManager()
{
    lock_ = srs_mutex_new();
    timer_ = new SrsHourGlass("sources", this, 1 * SRS_UTIME_SECONDS);

    app_factory_ = _srs_app_factory;
}

SrsLiveSourceManager::~SrsLiveSourceManager()
{
    srs_mutex_destroy(lock_);
    srs_freep(timer_);

    app_factory_ = NULL;
}

srs_error_t SrsLiveSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsLiveSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps)
{
    srs_error_t err = srs_success;

    bool created = false;
    // Should never invoke any function during the locking.
    if (true) {
        // Use lock to protect coroutine switch.
        // @bug https://github.com/ossrs/srs/issues/1230
        // TODO: FIXME: Use smaller scope lock.
        SrsLocker(&lock_);

        string stream_url = r->get_stream_url();
        std::map<std::string, SrsSharedPtr<SrsLiveSource> >::iterator it = pool_.find(stream_url);

        if (it != pool_.end()) {
            SrsSharedPtr<SrsLiveSource> &source = it->second;
            pps = source;
        } else {
            SrsSharedPtr<SrsLiveSource> source = app_factory_->create_live_source();
            srs_trace("new live source, stream_url=%s", stream_url.c_str());
            pps = source;

            // Callback to notify request of source creation
            r->on_source_created();

            pool_[stream_url] = source;
            created = true;
        }
    }

    // Initialize source with the wrapper of itself.
    if (created && (err = pps->initialize(pps, r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    // we always update the request of resource,
    // for origin auth is on, the token in request maybe invalid,
    // and we only need to update the token of request, it's simple.
    if (!created) {
        pps->update_auth(r);
    }

    return err;
}

SrsSharedPtr<SrsLiveSource> SrsLiveSourceManager::fetch(ISrsRequest *r)
{
    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    // TODO: FIXME: Use smaller scope lock.
    SrsLocker(&lock_);

    string stream_url = r->get_stream_url();
    std::map<std::string, SrsSharedPtr<SrsLiveSource> >::iterator it = pool_.find(stream_url);

    if (it == pool_.end()) {
        return SrsSharedPtr<SrsLiveSource>(NULL);
    }

    SrsSharedPtr<SrsLiveSource> &source = it->second;
    return source;
}

void SrsLiveSourceManager::dispose()
{
    std::map<std::string, SrsSharedPtr<SrsLiveSource> >::iterator it;
    for (it = pool_.begin(); it != pool_.end(); ++it) {
        SrsSharedPtr<SrsLiveSource> &source = it->second;
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

    std::map<std::string, SrsSharedPtr<SrsLiveSource> >::iterator it;
    for (it = pool_.begin(); it != pool_.end();) {
        SrsSharedPtr<SrsLiveSource> &source = it->second;

        // Do cycle source to cleanup components, such as hls dispose.
        if ((err = source->cycle()) != srs_success) {
            SrsContextId cid = source->source_id();
            if (cid.empty())
                cid = source->pre_source_id();
            return srs_error_wrap(err, "source cycle, id=[%s]", cid.c_str());
        }

        // When source expired, remove it.
        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead()) {
            SrsContextId cid = source->source_id();
            if (cid.empty())
                cid = source->pre_source_id();
            srs_trace("Live: cleanup die source, id=[%s], total=%d", cid.c_str(), (int)pool_.size());
            pool_.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

void SrsLiveSourceManager::destroy()
{
    pool_.clear();
}

ISrsLiveSource::ISrsLiveSource()
{
}

ISrsLiveSource::~ISrsLiveSource()
{
}

SrsLiveSource::SrsLiveSource()
{
    req_ = NULL;
    jitter_algorithm_ = SrsRtmpJitterAlgorithmOFF;
    mix_correct_ = false;
    mix_queue_ = new SrsMixQueue();

    can_publish_ = true;
    stream_die_at_ = 0;
    publisher_idle_at_ = 0;

    rtmp_bridge_ = NULL;

    play_edge_ = new SrsPlayEdge();
    publish_edge_ = new SrsPublishEdge();
    gop_cache_ = new SrsGopCache();
    hub_ = NULL;
    meta_ = new SrsMetaCache();
    format_ = new SrsRtmpFormat();

    is_monotonically_increase_ = false;
    last_packet_time_ = 0;

    atc_ = false;

    config_ = _srs_config;
    stat_ = _srs_stat;
    handler_ = _srs_server;
    app_factory_ = _srs_app_factory;
}

void SrsLiveSource::assemble()
{
    config_->subscribe(this);
}

SrsLiveSource::~SrsLiveSource()
{
    config_->unsubscribe(this);

    // never free the consumers,
    // for all consumers are auto free.
    consumers_.clear();

    srs_freep(format_);
    srs_freep(hub_);
    srs_freep(meta_);
    srs_freep(mix_queue_);

    srs_freep(play_edge_);
    srs_freep(publish_edge_);
    srs_freep(gop_cache_);

    srs_freep(req_);
    srs_freep(rtmp_bridge_);

    SrsContextId cid = _source_id;
    if (cid.empty())
        cid = _pre_source_id;
    srs_trace("free live source id=[%s]", cid.c_str());

    config_ = NULL;
    stat_ = NULL;
    handler_ = NULL;
    app_factory_ = NULL;
}

void SrsLiveSource::dispose()
{
    if (hub_) {
        hub_->dispose();
    }
    meta_->dispose();
    gop_cache_->dispose();
}

srs_error_t SrsLiveSource::cycle()
{
    srs_error_t err = srs_success;

    if (hub_ && (err = hub_->cycle()) != srs_success) {
        return srs_error_wrap(err, "hub cycle");
    }

    return srs_success;
}

bool SrsLiveSource::stream_is_dead()
{
    // still publishing?
    if (!can_publish_ || !publish_edge_->can_publish()) {
        return false;
    }

    // has any consumers?
    if (!consumers_.empty()) {
        return false;
    }

    // Delay cleanup source.
    srs_utime_t now = srs_time_now_cached();
    if (now < stream_die_at_ + SRS_SOURCE_CLEANUP) {
        return false;
    }

    // Origin hub delay cleanup.
    if (hub_ && now < stream_die_at_ + hub_->cleanup_delay()) {
        return false;
    }

    return true;
}

bool SrsLiveSource::publisher_is_idle_for(srs_utime_t timeout)
{
    if (!publisher_idle_at_ || !timeout) {
        return false;
    }

    srs_utime_t now = srs_time_now_cached();
    if (now > publisher_idle_at_ + timeout) {
        return true;
    }
    return false;
}

SrsMetaCache *SrsLiveSource::meta()
{
    return meta_;
}

SrsRtmpFormat *SrsLiveSource::format()
{
    return format_;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches
// before completing the basic field setup.
//
// If context switches occur before all fields are properly initialized, other coroutines
// accessing this source from the pool may encounter uninitialized state, leading to crashes
// or undefined behavior.
//
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsLiveSource::initialize(SrsSharedPtr<SrsLiveSource> wrapper, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    srs_assert(!req_);

    req_ = r->copy();
    atc_ = config_->get_atc(req_->vhost_);

    jitter_algorithm_ = (SrsRtmpJitterAlgorithm)config_->get_time_jitter(req_->vhost_);
    mix_correct_ = config_->get_mix_correct(req_->vhost_);

    if ((err = format_->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    // Setup the SPS/PPS parsing strategy.
    format_->try_annexb_first_ = config_->try_annexb_first(r->vhost_);

    if ((err = play_edge_->initialize(wrapper, req_)) != srs_success) {
        return srs_error_wrap(err, "edge(play)");
    }
    if ((err = publish_edge_->initialize(wrapper, req_)) != srs_success) {
        return srs_error_wrap(err, "edge(publish)");
    }

    srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
    publish_edge_->set_queue_size(queue_size);

    // Create and initialize origin hub only for origin servers, not edge servers
    bool edge = config_->get_vhost_is_edge(req_->vhost_);
    if (!edge) {
        srs_freep(hub_);
        hub_ = app_factory_->create_origin_hub();
    } else {
        srs_warn("disable OriginHub creation for edge vhost=%s", req_->vhost_.c_str());
    }

    if (hub_ && (err = hub_->initialize(wrapper, req_)) != srs_success) {
        return srs_error_wrap(err, "hub");
    }

    return err;
}

void SrsLiveSource::set_bridge(ISrsRtmpBridge *v)
{
    srs_freep(rtmp_bridge_);
    rtmp_bridge_ = v;
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
    std::vector<SrsLiveConsumer *>::iterator it;
    for (it = consumers_.begin(); it != consumers_.end(); ++it) {
        SrsLiveConsumer *consumer = *it;
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
    return can_publish_;
}

void SrsLiveSource::update_auth(ISrsRequest *r)
{
    req_->update_auth(r);
}

bool SrsLiveSource::can_publish(bool is_edge)
{
    // TODO: FIXME: Should check the status of bridge.

    if (is_edge) {
        return publish_edge_->can_publish();
    }

    return can_publish_;
}

srs_error_t SrsLiveSource::on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata)
{
    srs_error_t err = srs_success;

    if ((err = format_->on_metadata(metadata)) != srs_success) {
        return srs_error_wrap(err, "Format parse metadata");
    }

    // if allow atc_auto and bravo-atc detected, open atc for vhost.
    SrsAmf0Any *prop = NULL;
    atc_ = config_->get_atc(req_->vhost_);
    if (config_->get_atc_auto(req_->vhost_)) {
        if ((prop = metadata->metadata_->get_property("bravo_atc")) != NULL) {
            if (prop->is_string() && prop->to_str() == "true") {
                atc_ = true;
            }
        }
    }

    // Update the meta cache.
    bool updated = false;
    if ((err = meta_->update_data(&msg->header_, metadata, updated)) != srs_success) {
        return srs_error_wrap(err, "update metadata");
    }
    if (!updated) {
        return err;
    }

    // when already got metadata, drop when reduce sequence header.
    bool drop_for_reduce = false;
    if (meta_->data() && config_->get_reduce_sequence_header(req_->vhost_)) {
        drop_for_reduce = true;
        srs_warn("drop for reduce sh metadata, size=%d", msg->size());
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        std::vector<SrsLiveConsumer *>::iterator it;
        for (it = consumers_.begin(); it != consumers_.end(); ++it) {
            SrsLiveConsumer *consumer = *it;
            if ((err = consumer->enqueue(meta_->data(), atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume metadata");
            }
        }
    }

    // Copy to hub to all utilities.
    if (hub_ && (err = hub_->on_meta_data(meta_->data(), metadata)) != srs_success) {
        return srs_error_wrap(err, "hub consume metadata");
    }

    return err;
}

srs_error_t SrsLiveSource::on_audio(SrsRtmpCommonMessage *shared_audio)
{
    // Detect where stream is monotonically increasing.
    if (!mix_correct_ && is_monotonically_increase_) {
        if (last_packet_time_ > 0 && shared_audio->header_.timestamp_ < last_packet_time_) {
            is_monotonically_increase_ = false;
            srs_warn("AUDIO: Timestamp %" PRId64 "=>%" PRId64 ", may need mix_correct.",
                     last_packet_time_, shared_audio->header_.timestamp_);
        }
    }
    last_packet_time_ = shared_audio->header_.timestamp_;

    // convert shared_audio to msg, user should not use shared_audio again.
    // the payload is transfer to msg, and set to NULL in shared_audio.
    SrsMediaPacket msg;
    shared_audio->to_msg(&msg);

    return on_frame(&msg);
}

srs_error_t SrsLiveSource::on_frame(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // directly process the audio message.
    if (!mix_correct_) {
        if (msg->is_audio()) {
            return on_audio_imp(msg);
        } else {
            return on_video_imp(msg);
        }
    }

    // insert msg to the queue.
    mix_queue_->push(msg->copy());

    // fetch someone from mix queue.
    SrsMediaPacket *m = mix_queue_->pop();
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

srs_error_t SrsLiveSource::on_audio_imp(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format_->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as G.711 codec
    if (!format_->acodec_) {
        return err;
    }

    // Whether current packet is sequence header. Note that MP3 does not have one, but we use the first packet as it.
    bool is_sequence_header = format_->is_aac_sequence_header() || format_->is_mp3_sequence_header();

    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && meta_->previous_ash() && config_->get_reduce_sequence_header(req_->vhost_)) {
        if (meta_->previous_ash()->size() == msg->size()) {
            drop_for_reduce = srs_bytes_equal(meta_->previous_ash()->payload(), msg->payload(), msg->size());
            srs_warn("drop for reduce sh audio, size=%d", msg->size());
        }
    }

    // Copy to hub to all utilities.
    if (hub_ && (err = hub_->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "consume audio");
    }

    // For bridge to consume the message.
    if (rtmp_bridge_ && (err = rtmp_bridge_->on_frame(msg)) != srs_success) {
        return srs_error_wrap(err, "bridge consume audio");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers_.size(); i++) {
            SrsLiveConsumer *consumer = consumers_.at(i);
            if ((err = consumer->enqueue(msg, atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume message");
            }
        }
    }

    // Refresh the sequence header in metadata.
    if (is_sequence_header || !meta_->ash()) {
        if ((err = meta_->update_ash(msg)) != srs_success) {
            return srs_error_wrap(err, "meta consume audio");
        }
    }

    // when sequence header, donot push to gop cache and adjust the timestamp.
    if (is_sequence_header) {
        return err;
    }

    // cache the last gop packets
    if ((err = gop_cache_->cache(msg)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume audio");
    }

    // if atc, update the sequence header to abs time.
    if (atc_) {
        if (meta_->ash()) {
            meta_->ash()->timestamp_ = msg->timestamp_;
        }
        if (meta_->data()) {
            meta_->data()->timestamp_ = msg->timestamp_;
        }
    }

    return err;
}

srs_error_t SrsLiveSource::on_video(SrsRtmpCommonMessage *shared_video)
{
    srs_error_t err = srs_success;

    // Detect where stream is monotonically increasing.
    if (!mix_correct_ && is_monotonically_increase_) {
        if (last_packet_time_ > 0 && shared_video->header_.timestamp_ < last_packet_time_) {
            is_monotonically_increase_ = false;
            srs_warn("VIDEO: Timestamp %" PRId64 "=>%" PRId64 ", may need mix_correct.",
                     last_packet_time_, shared_video->header_.timestamp_);
        }
    }
    last_packet_time_ = shared_video->header_.timestamp_;

    // drop any unknown header video.
    // @see https://github.com/ossrs/srs/issues/421
    if (!SrsFlvVideo::acceptable(shared_video->payload(), shared_video->size())) {
        char b0 = 0x00;
        if (shared_video->size() > 0) {
            b0 = shared_video->payload()[0];
        }

        srs_warn("drop unknown header video, size=%d, bytes[0]=%#x", shared_video->size(), b0);
        return err;
    }

    // convert shared_video to msg, user should not use shared_video again.
    // the payload is transfer to msg, and set to NULL in shared_video.
    SrsMediaPacket msg;
    shared_video->to_msg(&msg);

    return on_frame(&msg);
}

srs_error_t SrsLiveSource::on_video_imp(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    bool is_sequence_header = SrsFlvVideo::sh(msg->payload(), msg->size());

    // user can disable the sps parse to workaround when parse sps failed.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sequence_header) {
        format_->avc_parse_sps_ = config_->get_parse_sps(req_->vhost_);
    }

    if ((err = format_->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as H.263 codec
    if (!format_->vcodec_) {
        return err;
    }

    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && meta_->previous_vsh() && config_->get_reduce_sequence_header(req_->vhost_)) {
        if (meta_->previous_vsh()->size() == msg->size()) {
            drop_for_reduce = srs_bytes_equal(meta_->previous_vsh()->payload(), msg->payload(), msg->size());
            srs_warn("drop for reduce sh video, size=%d", msg->size());
        }
    }

    // cache the sequence header if h264
    // donot cache the sequence header to gop_cache, return here.
    if (is_sequence_header && (err = meta_->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    // Copy to hub to all utilities.
    if (hub_ && (err = hub_->on_video(msg, is_sequence_header)) != srs_success) {
        return srs_error_wrap(err, "hub consume video");
    }

    // For bridge to consume the message.
    if (rtmp_bridge_ && (err = rtmp_bridge_->on_frame(msg)) != srs_success) {
        return srs_error_wrap(err, "bridge consume video");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers_.size(); i++) {
            SrsLiveConsumer *consumer = consumers_.at(i);
            if ((err = consumer->enqueue(msg, atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume video");
            }
        }
    }

    // when sequence header, donot push to gop cache and adjust the timestamp.
    if (is_sequence_header) {
        return err;
    }

    // cache the last gop packets
    if ((err = gop_cache_->cache(msg)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume vdieo");
    }

    // if atc, update the sequence header to abs time.
    if (atc_) {
        if (meta_->vsh()) {
            meta_->vsh()->timestamp_ = msg->timestamp_;
        }
        if (meta_->data()) {
            meta_->data()->timestamp_ = msg->timestamp_;
        }
    }

    return err;
}

srs_error_t SrsLiveSource::on_aggregate(SrsRtmpCommonMessage *msg)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(msg->payload(), msg->size()));

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

        timestamp |= time_h << 24;
        timestamp &= 0x7FFFFFFF;

        // adjust abs timestamp in aggregate msg.
        // only -1 means uninitialized delta.
        if (delta == -1) {
            delta = (int)msg->header_.timestamp_ - (int)timestamp;
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
        SrsRtmpCommonMessage o;

        o.header_.message_type_ = type;
        o.header_.payload_length_ = data_size;
        o.header_.timestamp_delta_ = timestamp;
        o.header_.timestamp_ = timestamp;
        o.header_.stream_id_ = stream_id;

        if (data_size > 0) {
            o.create_payload(data_size);
            stream->read_bytes(o.payload(), data_size);
        }

        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTMP_AGGREGATE, "aggregate previous tag size");
        }
        stream->read_4bytes();

        // process parsed message
        if (o.header_.is_audio()) {
            if ((err = on_audio(&o)) != srs_success) {
                return srs_error_wrap(err, "consume audio");
            }
        } else if (o.header_.is_video()) {
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
    srs_assert(req_);

    can_publish_ = false;

    // whatever, the publish thread is the source or edge source,
    // save its id to srouce id.
    if ((err = on_source_id_changed(_srs_context->get_id())) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    // reset the mix queue.
    mix_queue_->clear();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->clear();

    // detect the monotonically again.
    is_monotonically_increase_ = true;
    last_packet_time_ = 0;

    // Notify the hub about the publish event.
    if (hub_ && (err = hub_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "hub publish");
    }

    // notify the handler.
    srs_assert(handler_);
    if ((err = handler_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    if (rtmp_bridge_ && (err = rtmp_bridge_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "bridge publish");
    }

    stat_->on_stream_publish(req_, _source_id.c_str());

    // When no players, the publisher is idle now.
    if (consumers_.empty()) {
        publisher_idle_at_ = srs_time_now_cached();
    }

    return err;
}

void SrsLiveSource::on_unpublish()
{
    // ignore when already unpublished.
    if (can_publish_) {
        return;
    }

    // Notify the hub about the unpublish event.
    if (hub_) {
        hub_->on_unpublish();
    }

    // only clear the gop cache,
    // donot clear the sequence header, for it maybe not changed,
    // when drop dup sequence header, drop the metadata also.
    gop_cache_->clear();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->update_previous_vsh();
    meta_->update_previous_ash();

    srs_trace("cleanup when unpublish");

    if (!_source_id.empty()) {
        _pre_source_id = _source_id;
    }
    _source_id = SrsContextId();

    // notify the handler.
    stat_->on_stream_close(req_);

    srs_assert(handler_);
    handler_->on_unpublish(req_);

    if (rtmp_bridge_) {
        rtmp_bridge_->on_unpublish();
        srs_freep(rtmp_bridge_);
    }

    // no consumer, stream is die.
    if (consumers_.empty()) {
        stream_die_at_ = srs_time_now_cached();
    }

    // Note that we should never set to unpublish before any other handler is done, especially the handler
    // which is actually an http stream that unmounts the HTTP path for streaming, because there maybe some
    // coroutine switch in these handlers.
    can_publish_ = true;
}

srs_error_t SrsLiveSource::create_consumer(SrsLiveConsumer *&consumer)
{
    srs_error_t err = srs_success;

    // for edge, when play edge stream, check the state
    if (config_->get_vhost_is_edge(req_->vhost_)) {
        // notice edge to start for the first client.
        if ((err = play_edge_->on_client_play()) != srs_success) {
            return srs_error_wrap(err, "play edge");
        }
    }

    consumer = new SrsLiveConsumer(this);
    consumers_.push_back(consumer);

    // There are more than one consumer, so reset the timeout.
    stream_die_at_ = 0;
    publisher_idle_at_ = 0;

    return err;
}

srs_error_t SrsLiveSource::consumer_dumps(ISrsLiveConsumer *consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
    consumer->set_queue_size(queue_size);

    // if atc, update the sequence header to gop cache time.
    if (atc_ && !gop_cache_->empty()) {
        if (meta_->data()) {
            meta_->data()->timestamp_ = srsu2ms(gop_cache_->start_time());
        }
        if (meta_->vsh()) {
            meta_->vsh()->timestamp_ = srsu2ms(gop_cache_->start_time());
        }
        if (meta_->ash()) {
            meta_->ash()->timestamp_ = srsu2ms(gop_cache_->start_time());
        }
    }

    // If stream is publishing, dumps the sequence header and gop cache.
    bool hub_active = hub_ ? hub_->active() : false;
    if (hub_active) {
        // Copy metadata and sequence header to consumer.
        if ((err = meta_->dumps(consumer, atc_, jitter_algorithm_, dm, ds)) != srs_success) {
            return srs_error_wrap(err, "meta dumps");
        }

        // copy gop cache to client.
        if (dg && (err = gop_cache_->dump(consumer, atc_, jitter_algorithm_)) != srs_success) {
            return srs_error_wrap(err, "gop cache dumps");
        }
    }

    // print status.
    if (dg) {
        srs_trace("create consumer, active=%d, queue_size=%dms, jitter=%d", hub_active, srsu2msi(queue_size), jitter_algorithm_);
    } else {
        srs_trace("create consumer, active=%d, ignore gop cache, jitter=%d", hub_active, jitter_algorithm_);
    }

    return err;
}

void SrsLiveSource::on_consumer_destroy(SrsLiveConsumer *consumer)
{
    std::vector<SrsLiveConsumer *>::iterator it;
    it = std::find(consumers_.begin(), consumers_.end(), consumer);
    if (it != consumers_.end()) {
        it = consumers_.erase(it);
    }

    if (consumers_.empty()) {
        play_edge_->on_all_client_stop();

        // If no publishers, the stream is die.
        if (can_publish_) {
            stream_die_at_ = srs_time_now_cached();
        }

        // For edge server, the stream die when the last player quit, because the edge stream is created by player
        // activities, so it should die when all players quit.
        if (config_->get_vhost_is_edge(req_->vhost_)) {
            stream_die_at_ = srs_time_now_cached();
        }

        // When no players, the publisher is idle now.
        publisher_idle_at_ = srs_time_now_cached();
    }
}

void SrsLiveSource::set_cache(bool enabled)
{
    gop_cache_->set(enabled);
}

void SrsLiveSource::set_gop_cache_max_frames(int v)
{
    gop_cache_->set_gop_cache_max_frames(v);
}

// LCOV_EXCL_START
SrsRtmpJitterAlgorithm SrsLiveSource::jitter()
{
    return jitter_algorithm_;
}

srs_error_t SrsLiveSource::on_edge_start_publish()
{
    return publish_edge_->on_client_publish();
}

// TODO: FIXME: Use edge strategy pattern.
srs_error_t SrsLiveSource::on_edge_proxy_publish(SrsRtmpCommonMessage *msg)
{
    return publish_edge_->on_proxy_publish(msg);
}

void SrsLiveSource::on_edge_proxy_unpublish()
{
    publish_edge_->on_proxy_unpublish();
}
// LCOV_EXCL_STOP

