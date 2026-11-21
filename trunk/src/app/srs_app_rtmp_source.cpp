//
// Copyright (c) 2013-2026 The SRS Authors
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
#include <srs_app_hls.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_kbps.hpp>
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

// At 26ms per audio packet, 115 packets is 3s.
#define SRS_PURE_AUDIO_GUESS_COUNT 115

#define SRS_MIX_CORRECT_PURE_AV 10

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

    if (ag != SrsRtmpJitterAlgorithmFULL) {
        if (ag == SrsRtmpJitterAlgorithmOFF) {
            return err;
        }

        if (ag == SrsRtmpJitterAlgorithmZERO) {
            if (last_pkt_correct_time_ == -1) {
                last_pkt_correct_time_ = msg->timestamp_;
            }
            msg->timestamp_ -= last_pkt_correct_time_;
            return err;
        }

        return err;
    }

    if (!msg->is_av()) {
        msg->timestamp_ = 0;
        return err;
    }

    int64_t time = msg->timestamp_;
    int64_t delta = time - last_pkt_time_;

    if (delta < CONST_MAX_JITTER_MS_NEG || delta > CONST_MAX_JITTER_MS) {
        // Use 10ms, so the problem of the stream can be noticed.
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

void SrsFastVector::erase(int begin, int end)
{
    srs_assert(begin < end);

    for (int i = 0; i < count_ - end; i++) {
        msgs_[begin + i] = msgs_[end + i];
    }

    count_ -= end - begin;
}

void SrsFastVector::push_back(SrsMediaPacket *msg)
{
    if (count_ >= nb_msgs_) {
        int size = srs_max(SRS_PERF_MW_MSGS * 8, nb_msgs_ * 2);
        SrsMediaPacket **buf = msgs_;
        msgs_ = new SrsMediaPacket *[size];
        for (int i = 0; i < nb_msgs_; i++) {
            msgs_[i] = buf[i];
        }
        srs_info("fast vector incrase %d=>%d", nb_msgs_, size);

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
    ignore_shrink_ = ignore_shrink;
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

    // With jitter off, the first sequence header has timestamp zero, which made SRS shrink and drop the
    // keyframes with no overflow, so zero timestamps are ignored.
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
        msgs_.clear();
    } else {
        // Erasing from the front copies the rest. With SRS_PERF_MW_MSGS 128, an RTMP player
        // takes 128 messages at once, so this branch is rare.
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

    av_start_time_ = av_end_time_;

    if (video_sh) {
        video_sh->timestamp_ = srsu2ms(av_end_time_);
        msgs_.push_back(video_sh);
    }
    if (audio_sh) {
        audio_sh->timestamp_ = srsu2ms(av_end_time_);
        msgs_.push_back(audio_sh);
    }

    if (!ignore_shrink_) {
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
    if (mw_waiting_) {
        srs_utime_t duration = queue_->duration();
        bool match_min_msgs = queue_->size() > mw_min_msgs_;

        // A negative duration never grows to the wait's duration, so flush now. For ATC, maybe the SH
        // timestamp bigger than A/V packet, when encoder republish or overflow, @see
        // https://github.com/ossrs/srs/pull/749. With time_jitter zero or off, the publisher's timestamps
        // may jump back before the last message sent to the player.
        if (duration < 0) {
            srs_cond_signal(mw_wait_);
            mw_waiting_ = false;
            return err;
        }

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

    int max = count ? srs_min(count, msgs->max_) : msgs->max_;

    count = 0;

    if (should_update_source_id_) {
        srs_trace("update source_id=%s/%s", source_->source_id().c_str(), source_->pre_source_id().c_str());
        should_update_source_id_ = false;
    }

    if (paused_) {
        return err;
    }

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

    if (match_min_msgs && duration > mw_duration_) {
        return;
    }

    // The enqueue signals this cond.
    mw_waiting_ = true;

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
    wait_keyframe_ = false;
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

    SrsMediaPacket *msg = shared_msg;

    if (msg->is_video()) {
        bool codec_ok = SrsFlvVideo::h264(msg->payload(), msg->size());
        codec_ok = codec_ok ? true : SrsFlvVideo::hevc(msg->payload(), msg->size());
        if (!codec_ok)
            return err;

        // Skip the frames before the keyframe after the max frames cleared the cache.
        if (wait_keyframe_ && !SrsFlvVideo::keyframe(msg->payload(), msg->size())) {
            return err;
        }

        cached_video_count_++;
        audio_after_last_video_count_ = 0;
    }

    if (pure_audio()) {
        return err;
    }

    if (msg->is_audio()) {
        audio_after_last_video_count_++;
    }

    if (audio_after_last_video_count_ > SRS_PURE_AUDIO_GUESS_COUNT) {
        srs_warn("clear gop cache for guess pure audio overflow");
        clear();
        return err;
    }

    if (msg->is_video() && SrsFlvVideo::keyframe(msg->payload(), msg->size())) {
        clear();

        cached_video_count_ = 1;
    }

    gop_cache_.push_back(msg->copy());

    if (gop_cache_max_frames_ > 0 && gop_cache_.size() > (size_t)gop_cache_max_frames_) {
        srs_warn("Gop cache exceed max frames=%d, total=%d, videos=%d, aalvc=%d",
                 gop_cache_max_frames_, (int)gop_cache_.size(), cached_video_count_, audio_after_last_video_count_);
        clear();
        wait_keyframe_ = true;
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
    wait_keyframe_ = false;
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

srs_utime_t SrsGopCache::start_time()
{
    if (empty()) {
        return 0;
    }

    SrsMediaPacket *msg = gop_cache_[0];
    srs_assert(msg);

    return srs_utime_t(msg->timestamp_ * SRS_UTIME_MILLISECONDS);
}

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

// TODO: FIXME: Remove it?
bool srs_hls_can_continue(int ret, SrsMediaPacket *sh, SrsMediaPacket *msg)
{
    if (ret != ERROR_HLS_DECODE_ERROR) {
        return false;
    }

    // A packet of the size of the sequence header may be a sequence header; continue, for
    // FFmpeg.
    if (sh && sh->size() == msg->size()) {
        srs_warn("the msg is actually a sequence header, ignore this packet.");
        return true;
    }

    return false;
}

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

    if (nb_videos_ >= SRS_MIX_CORRECT_PURE_AV && nb_audios_ == 0) {
        mix_ok = true;
    }

    if (nb_audios_ >= SRS_MIX_CORRECT_PURE_AV && nb_videos_ == 0) {
        mix_ok = true;
    }

    if (nb_videos_ >= 1 && nb_audios_ >= 1) {
        mix_ok = true;
    }

    if (!mix_ok) {
        return NULL;
    }

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

    encoder_ = new SrsEncoder();
    ng_exec_ = new SrsNgExec();

    config_ = _srs_config;
    stat_ = _srs_stat;
    hooks_ = _srs_hooks;
}

void SrsOriginHub::assemble()
{
    hls_->assemble();
    dvr_->assemble();
    encoder_->assemble();
    ng_exec_->assemble();
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

    config_ = NULL;
    stat_ = NULL;
    hooks_ = NULL;
}

// Called after fetch_or_create puts the source in the pool (see PR 4449), so it must not switch
// coroutines, or other coroutines could create a duplicate source for the stream.
srs_error_t SrsOriginHub::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r;
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

    if (true) {
        std::vector<ISrsForwarder *>::iterator it;
        for (it = forwarders_.begin(); it != forwarders_.end(); ++it) {
            ISrsForwarder *forwarder = *it;
            if ((err = forwarder->on_meta_data(shared_metadata)) != srs_success) {
                return srs_error_wrap(err, "Forwarder consume metadata");
            }
        }
    }

    // TODO: FIXME: Refine it, a DVR metadata error, such as a full disk, disconnects the publisher.
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

    if (format->is_aac_sequence_header() || format->is_mp3_sequence_header()) {
        srs_assert(format->acodec_);
        SrsAudioCodecConfig *c = format->acodec_;

        static int flv_sample_sizes[] = {8, 16, 0};
        static int flv_sound_types[] = {1, 2, 0};

        // sound_rate_ is the FLV enum, while the AAC aac_sample_rate_ is an index.
        SrsAudioSampleRate sample_rate = c->sound_rate_;
        if (format->acodec_->id_ == SrsAudioCodecIdAAC && c->aac_sample_rate_ < SrsAAcSampleRateNumbers) {
            sample_rate = srs_audio_sample_rate_from_number(srs_aac_srates[c->aac_sample_rate_]);
        }

        // The FLV specification fixes SoundType to 1 (stereo) for AAC, see E.4.2 Audio Tags,
        // video_file_format_spec_v10_1.pdf, page 77.
        //      If the SoundFormat indicates AAC, the SoundType should be 1 (stereo) and the
        //      SoundRate should be 3 (44 kHz).
        // FFmpeg follows it, so it always sends these fixed channels and sample rate.
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

    if (format->is_avc_sequence_header()) {
        SrsVideoCodecConfig *c = format->vcodec_;
        srs_assert(c);

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
    if ((err = ng_exec_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "exec publish");
    }

    is_active_ = true;

    return err;
}

void SrsOriginHub::on_unpublish()
{
    is_active_ = false;

    destroy_forwarders();

    encoder_->on_unpublish();
    hls_->on_unpublish();
    dash_->on_unpublish();
    dvr_->on_unpublish();

    ng_exec_->on_unpublish();
}

srs_error_t SrsOriginHub::on_forwarder_start(SrsForwarder *forwarder)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *cache_metadata = source_->meta()->data();
    SrsMediaPacket *cache_sh_video = source_->meta()->vsh();
    SrsMediaPacket *cache_sh_audio = source_->meta()->ash();

    // A forwarder enabled by reload never gets the metadata and sequence headers in the stream.
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

    // A DVR started by reload never gets the metadata and sequence headers in the stream.
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

srs_error_t SrsOriginHub::on_dash_request_sh()
{
    srs_error_t err = srs_success;

    SrsMediaPacket *cache_sh_video = source_->meta()->vsh();
    if (cache_sh_video) {
        if ((err = dash_->on_video(cache_sh_video, source_->meta()->vsh_format())) != srs_success) {
            return srs_error_wrap(err, "dash video");
        }
    }

    SrsMediaPacket *cache_sh_audio = source_->meta()->ash();
    if (cache_sh_audio) {
        if ((err = dash_->on_audio(cache_sh_audio, source_->meta()->ash_format())) != srs_success) {
            return srs_error_wrap(err, "dash audio");
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

    bool applied_backend_server = false;
    if ((err = create_backend_forwarders(applied_backend_server)) != srs_success) {
        return srs_error_wrap(err, "create backend applied=%d", applied_backend_server);
    }

    if (applied_backend_server) {
        return err;
    }

    SrsConfDirective *conf = config_->get_forwards(req_->vhost_);
    for (int i = 0; conf && i < (int)conf->args_.size(); i++) {
        std::string forward_server = conf->args_.at(i);

        ISrsForwarder *forwarder = new SrsForwarder(this, req_);
        forwarders_.push_back(forwarder);

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

    applied = false;

    SrsConfDirective *conf = config_->get_forward_backend(req_->vhost_);
    if (!conf || conf->arg0().empty()) {
        return err;
    }

    applied = true;

    std::string backend_url = conf->arg0();

    std::vector<std::string> urls;
    if ((err = hooks_->on_forward_backend(backend_url, req_, urls)) != srs_success) {
        return srs_error_wrap(err, "get forward backend failed, backend=%s", backend_url.c_str());
    }

    std::vector<std::string>::iterator it;
    for (it = urls.begin(); it != urls.end(); ++it) {
        std::string url = *it;

        SrsUniquePtr<ISrsRequest> req(new SrsRequest());
        srs_net_url_parse_rtmp_url(url, req->tcUrl_, req->stream_);
        srs_net_url_parse_tcurl(req->tcUrl_, req->schema_, req->host_, req->vhost_, req->app_, req->stream_, req->port_, req->param_);

        // The forwarder belongs to the publishing stream, not to the app and stream of the url.
        ISrsForwarder *forwarder = new SrsForwarder(this, req_);
        forwarders_.push_back(forwarder);

        std::stringstream forward_server;
        forward_server << req->host_ << ":" << req->port_;

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
    previous_meta_ = previous_video_ = previous_audio_ = NULL;
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
    srs_freep(previous_meta_);
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

    if (dm && meta_ && (err = consumer->enqueue(meta_, atc, ag)) != srs_success) {
        return srs_error_wrap(err, "enqueue metadata");
    }

    // The audio sequence header goes first, for HLS to find the right audio codec fast.
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

SrsMediaPacket *SrsMetaCache::previous_data()
{
    return previous_meta_;
}

void SrsMetaCache::update_previous_data()
{
    srs_freep(previous_meta_);
    previous_meta_ = meta_ ? meta_->copy() : NULL;
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

    // Remove the duration, for ExoPlayer.
    if (metadata->metadata_->get_property("duration") != NULL) {
        metadata->metadata_->remove("duration");
    }

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

    metadata->metadata_->set("server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));

    // Do not remove the version, for debugging.
    metadata->metadata_->set("server_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));

    int size = 0;
    char *payload = NULL;
    if ((err = metadata->encode(size, payload)) != srs_success) {
        return srs_error_wrap(err, "encode metadata");
    }

    if (size <= 0) {
        srs_warn("ignore the invalid metadata. size=%d", size);
        return err;
    }

    srs_freep(meta_);
    meta_ = new SrsMediaPacket();
    updated = true;

    // The payload goes to meta_, so do not free it.
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
    timer_ = NULL;

    app_factory_ = _srs_app_factory;
    stream_publish_tokens_ = _srs_stream_publish_tokens;
}

void SrsLiveSourceManager::assemble()
{
    timer_ = app_factory_->create_hourglass("sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsLiveSourceManager::~SrsLiveSourceManager()
{
    srs_mutex_destroy(lock_);
    srs_freep(timer_);

    app_factory_ = NULL;
    stream_publish_tokens_ = NULL;
}

srs_error_t SrsLiveSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsLiveSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps)
{
    srs_error_t err = srs_success;

    bool created = false;
    if (true) {
        // The lock guards against coroutine switches.
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
            srs_trace("new live source, stream_url=%s, dead=%d", stream_url.c_str(), source->stream_is_dead());
            pps = source;

            r->on_source_created();

            pool_[stream_url] = source;
            created = true;
        }
    }

    if (created && (err = pps->initialize(pps, r)) != srs_success) {
        // Drop the half built source, or the next client gets it without initialize.
        std::map<std::string, SrsSharedPtr<SrsLiveSource> >::iterator it = pool_.find(r->get_stream_url());
        if (it != pool_.end() && it->second.get() == pps.get()) {
            pool_.erase(it);
        }
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    // With origin auth on, the token in the source's request may be stale.
    if (!created) {
        pps->update_auth(r);
    }

    return err;
}

SrsSharedPtr<SrsLiveSource> SrsLiveSourceManager::fetch(ISrsRequest *r)
{
    // The lock guards against coroutine switches.
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

        if ((err = source->cycle()) != srs_success) {
            SrsContextId cid = source->source_id();
            if (cid.empty())
                cid = source->pre_source_id();
            return srs_error_wrap(err, "source cycle, id=[%s]", cid.c_str());
        }

        // A publisher may yield after fetching the source but before activating it.
        // Keep the source in the pool while its publish token is still acquired.
        const string &stream_url = it->first;
        bool is_stream_acquired = stream_publish_tokens_->is_acquired(stream_url);

        // @see https://github.com/ossrs/srs/issues/713
        if (source->stream_is_dead() && !is_stream_acquired) {
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
    clk_ = _srs_clock;
}

void SrsLiveSource::assemble()
{
    config_->subscribe(this);

    // So a new source is not dead at once.
    // @see https://github.com/ossrs/srs/issues/4449
    stream_die_at_ = clk_->now();
}

SrsLiveSource::~SrsLiveSource()
{
    config_->unsubscribe(this);

    // Never free the consumers; they free themselves.
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

    SrsContextId cid = source_id_;
    if (cid.empty())
        cid = pre_source_id_;
    srs_trace("free live source id=[%s]", cid.c_str());

    config_ = NULL;
    stat_ = NULL;
    handler_ = NULL;
    app_factory_ = NULL;
    clk_ = NULL;
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
    if (!can_publish_ || !publish_edge_->can_publish()) {
        return false;
    }

    if (!consumers_.empty()) {
        return false;
    }

    srs_utime_t now = clk_->now();
    if (now < stream_die_at_ + SRS_SOURCE_CLEANUP) {
        return false;
    }

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

    srs_utime_t now = clk_->now();
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

// Called after fetch_or_create puts the source in the pool (see PR 4449), so it must not switch
// coroutines before the fields are set: other coroutines may use the source from the pool, or
// create a duplicate source for the stream.
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

    format_->try_annexb_first_ = config_->try_annexb_first(r->vhost_);

    if ((err = play_edge_->initialize(wrapper, req_)) != srs_success) {
        return srs_error_wrap(err, "edge(play)");
    }
    if ((err = publish_edge_->initialize(wrapper, req_)) != srs_success) {
        return srs_error_wrap(err, "edge(publish)");
    }

    srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
    publish_edge_->set_queue_size(queue_size);

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

    if (!source_id_.compare(id)) {
        return err;
    }

    if (pre_source_id_.empty()) {
        pre_source_id_ = id;
    }
    source_id_ = id;

    std::vector<SrsLiveConsumer *>::iterator it;
    for (it = consumers_.begin(); it != consumers_.end(); ++it) {
        SrsLiveConsumer *consumer = *it;
        consumer->update_source_id();
    }

    return err;
}

SrsContextId SrsLiveSource::source_id()
{
    return source_id_;
}

SrsContextId SrsLiveSource::pre_source_id()
{
    return pre_source_id_;
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

    atc_ = config_->get_atc(req_->vhost_);

    bool updated = false;
    if ((err = meta_->update_data(&msg->header_, metadata, updated)) != srs_success) {
        return srs_error_wrap(err, "update metadata");
    }
    if (!updated) {
        return err;
    }

    bool drop_for_reduce = false;
    SrsMediaPacket *previous = meta_->previous_data();
    SrsMediaPacket *data = meta_->data();
    if (previous && config_->get_reduce_sequence_header(req_->vhost_)) {
        if (previous->size() == data->size()) {
            drop_for_reduce = srs_bytes_equal(previous->payload(), data->payload(), data->size());
        }
        if (drop_for_reduce) {
            srs_warn("drop for reduce sh metadata, size=%d", msg->size());
        }
    }
    meta_->update_previous_data();

    if (!drop_for_reduce) {
        std::vector<SrsLiveConsumer *>::iterator it;
        for (it = consumers_.begin(); it != consumers_.end(); ++it) {
            SrsLiveConsumer *consumer = *it;
            if ((err = consumer->enqueue(meta_->data(), atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume metadata");
            }
        }
    }

    if (hub_ && (err = hub_->on_meta_data(meta_->data(), metadata)) != srs_success) {
        return srs_error_wrap(err, "hub consume metadata");
    }

    return err;
}

srs_error_t SrsLiveSource::on_audio(SrsRtmpCommonMessage *shared_audio)
{
    if (!mix_correct_ && is_monotonically_increase_) {
        if (last_packet_time_ > 0 && shared_audio->header_.timestamp_ < last_packet_time_) {
            is_monotonically_increase_ = false;
            srs_warn("AUDIO: Timestamp %" PRId64 "=>%" PRId64 ", may need mix_correct.",
                     last_packet_time_, shared_audio->header_.timestamp_);
        }
    }
    last_packet_time_ = shared_audio->header_.timestamp_;

    // Drop the empty audio, as an edge pulling HTTP-FLV may get from its origin, like the protocol and on_video do.
    if (shared_audio->size() <= 0) {
        srs_warn("drop empty audio");
        return srs_success;
    }

    SrsMediaPacket msg;
    shared_audio->to_msg(&msg);

    return on_frame(&msg);
}

srs_error_t SrsLiveSource::on_frame(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    if (!mix_correct_) {
        if (msg->is_audio()) {
            return on_audio_imp(msg);
        } else {
            return on_video_imp(msg);
        }
    }

    mix_queue_->push(msg->copy());

    SrsMediaPacket *m = mix_queue_->pop();
    if (!m) {
        return err;
    }

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

    // No acodec_ for an unsupported codec, such as G.711.
    if (!format_->acodec_) {
        return err;
    }

    // MP3 has no sequence header, so its first packet is used as one.
    bool is_sequence_header = format_->is_aac_sequence_header() || format_->is_mp3_sequence_header();

    bool drop_for_reduce = false;
    if (is_sequence_header && meta_->previous_ash() && config_->get_reduce_sequence_header(req_->vhost_)) {
        if (meta_->previous_ash()->size() == msg->size()) {
            drop_for_reduce = srs_bytes_equal(meta_->previous_ash()->payload(), msg->payload(), msg->size());
            srs_warn("drop for reduce sh audio, size=%d", msg->size());
        }
    }

    if (hub_ && (err = hub_->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "consume audio");
    }

    if (rtmp_bridge_ && (err = rtmp_bridge_->on_frame(msg)) != srs_success) {
        return srs_error_wrap(err, "bridge consume audio");
    }

    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers_.size(); i++) {
            SrsLiveConsumer *consumer = consumers_.at(i);
            if ((err = consumer->enqueue(msg, atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume message");
            }
        }
    }

    if (is_sequence_header || !meta_->ash()) {
        if ((err = meta_->update_ash(msg)) != srs_success) {
            return srs_error_wrap(err, "meta consume audio");
        }
    }

    if (is_sequence_header) {
        return err;
    }

    if ((err = gop_cache_->cache(msg)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume audio");
    }

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

    if (!mix_correct_ && is_monotonically_increase_) {
        if (last_packet_time_ > 0 && shared_video->header_.timestamp_ < last_packet_time_) {
            is_monotonically_increase_ = false;
            srs_warn("VIDEO: Timestamp %" PRId64 "=>%" PRId64 ", may need mix_correct.",
                     last_packet_time_, shared_video->header_.timestamp_);
        }
    }
    last_packet_time_ = shared_video->header_.timestamp_;

    // @see https://github.com/ossrs/srs/issues/421
    if (!SrsFlvVideo::acceptable(shared_video->payload(), shared_video->size())) {
        char b0 = 0x00;
        if (shared_video->size() > 0) {
            b0 = shared_video->payload()[0];
        }

        srs_warn("drop unknown header video, size=%d, bytes[0]=%#x", shared_video->size(), b0);
        return err;
    }

    SrsMediaPacket msg;
    shared_video->to_msg(&msg);

    return on_frame(&msg);
}

srs_error_t SrsLiveSource::on_video_imp(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    bool is_sequence_header = SrsFlvVideo::sh(msg->payload(), msg->size());

    // The parse_sps config works around SPS that fail to parse.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sequence_header) {
        format_->avc_parse_sps_ = config_->get_parse_sps(req_->vhost_);
        meta_->vsh_format()->avc_parse_sps_ = format_->avc_parse_sps_;
    }

    if ((err = format_->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // No vcodec_ for an unsupported codec, such as H.263.
    if (!format_->vcodec_) {
        return err;
    }

    bool drop_for_reduce = false;
    if (is_sequence_header && meta_->previous_vsh() && config_->get_reduce_sequence_header(req_->vhost_)) {
        if (meta_->previous_vsh()->size() == msg->size()) {
            drop_for_reduce = srs_bytes_equal(meta_->previous_vsh()->payload(), msg->payload(), msg->size());
            srs_warn("drop for reduce sh video, size=%d", msg->size());
        }
    }

    if (is_sequence_header && (err = meta_->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    if (hub_ && (err = hub_->on_video(msg, is_sequence_header)) != srs_success) {
        return srs_error_wrap(err, "hub consume video");
    }

    if (rtmp_bridge_ && (err = rtmp_bridge_->on_frame(msg)) != srs_success) {
        return srs_error_wrap(err, "bridge consume video");
    }

    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers_.size(); i++) {
            SrsLiveConsumer *consumer = consumers_.at(i);
            if ((err = consumer->enqueue(msg, atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume video");
            }
        }
    }

    if (is_sequence_header) {
        return err;
    }

    if ((err = gop_cache_->cache(msg)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume vdieo");
    }

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

    // Aggregate messages use absolute time.
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

        // Ignore an empty tag, as SrsProtocol ignores an empty message; the players and muxers expect a payload.
        if (data_size == 0) {
            continue;
        }

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

    srs_assert(req_);

    can_publish_ = false;

    if ((err = on_source_id_changed(_srs_context->get_id())) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    mix_queue_->clear();

    // Reset the metadata cache, for VLC when the stream is disabled and enabled.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->clear();

    is_monotonically_increase_ = true;
    last_packet_time_ = 0;

    if (hub_ && (err = hub_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "hub publish");
    }

    srs_assert(handler_);
    if ((err = handler_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    if (rtmp_bridge_ && (err = rtmp_bridge_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "bridge publish");
    }

    stat_->on_stream_publish(req_, source_id_.c_str());

    if (consumers_.empty()) {
        publisher_idle_at_ = clk_->now();
    }

    return err;
}

void SrsLiveSource::on_unpublish()
{
    if (can_publish_) {
        return;
    }

    if (hub_) {
        hub_->on_unpublish();
    }

    // Only the GOP cache is cleared: the sequence headers may not change, and when a duplicate
    // sequence header is dropped, so is the metadata.
    gop_cache_->clear();

    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->update_previous_data();
    meta_->update_previous_vsh();
    meta_->update_previous_ash();

    srs_trace("cleanup when unpublish");

    if (!source_id_.empty()) {
        pre_source_id_ = source_id_;
    }
    source_id_ = SrsContextId();

    stat_->on_stream_close(req_);

    srs_assert(handler_);
    handler_->on_unpublish(req_);

    if (rtmp_bridge_) {
        rtmp_bridge_->on_unpublish();
        srs_freep(rtmp_bridge_);
    }

    if (consumers_.empty()) {
        stream_die_at_ = clk_->now();
    }

    // Set it only after every handler is done, especially the HTTP stream that unmounts its path,
    // since the handlers may switch coroutines.
    can_publish_ = true;
}

srs_error_t SrsLiveSource::create_consumer(SrsLiveConsumer *&consumer)
{
    srs_error_t err = srs_success;

    if (config_->get_vhost_is_edge(req_->vhost_)) {
        if ((err = play_edge_->on_client_play()) != srs_success) {
            return srs_error_wrap(err, "play edge");
        }
    }

    consumer = new SrsLiveConsumer(this);
    consumers_.push_back(consumer);

    stream_die_at_ = 0;
    publisher_idle_at_ = 0;

    return err;
}

srs_error_t SrsLiveSource::consumer_dumps(ISrsLiveConsumer *consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
    consumer->set_queue_size(queue_size);

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

    // An edge has no hub; its source is publishing once the edge pull fills the meta cache, and
    // late players still need the metadata, sequence headers and GOP.
    bool hub_active = hub_ ? hub_->active() : (meta_->data() || meta_->vsh() || meta_->ash());
    if (hub_active) {
        if ((err = meta_->dumps(consumer, atc_, jitter_algorithm_, dm, ds)) != srs_success) {
            return srs_error_wrap(err, "meta dumps");
        }

        if (dg && (err = gop_cache_->dump(consumer, atc_, jitter_algorithm_)) != srs_success) {
            return srs_error_wrap(err, "gop cache dumps");
        }
    }

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

        if (can_publish_) {
            stream_die_at_ = clk_->now();
        }

        // Players create an edge stream, so it dies when the last player quits.
        if (config_->get_vhost_is_edge(req_->vhost_)) {
            stream_die_at_ = clk_->now();
        }

        publisher_idle_at_ = clk_->now();
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
