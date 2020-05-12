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

#include <srs_app_rtc_source.hpp>

#include <srs_app_conn.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_format.hpp>
#include <srs_app_rtc.hpp>

using namespace std;

SrsRtcConsumer::SrsRtcConsumer(SrsRtcSource* s, SrsConnection* c)
{
    source = s;
    conn = c;
    should_update_source_id = false;
    queue = new SrsMessageQueue();

#ifdef SRS_PERF_QUEUE_COND_WAIT
    mw_wait = srs_cond_new();
    mw_min_msgs = 0;
    mw_duration = 0;
    mw_waiting = false;
#endif
}

SrsRtcConsumer::~SrsRtcConsumer()
{
    source->on_consumer_destroy(this);

    srs_freep(queue);

#ifdef SRS_PERF_QUEUE_COND_WAIT
    srs_cond_destroy(mw_wait);
#endif
}

void SrsRtcConsumer::update_source_id()
{
    should_update_source_id = true;
}

srs_error_t SrsRtcConsumer::enqueue(SrsSharedPtrMessage* shared_msg, bool atc, SrsRtmpJitterAlgorithm ag)
{
    srs_error_t err = srs_success;

    SrsSharedPtrMessage* msg = shared_msg->copy();

    if ((err = queue->enqueue(msg, NULL)) != srs_success) {
        return srs_error_wrap(err, "enqueue message");
    }

#ifdef SRS_PERF_QUEUE_COND_WAIT
    // fire the mw when msgs is enough.
    if (mw_waiting) {
        if (queue->size() > mw_min_msgs) {
            srs_cond_signal(mw_wait);
            mw_waiting = false;
            return err;
        }
        return err;
    }
#endif

    return err;
}

srs_error_t SrsRtcConsumer::dump_packets(SrsMessageArray* msgs, int& count)
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
        srs_trace("update source_id=%d[%d]", source->source_id(), source->source_id());
        should_update_source_id = false;
    }

    // pump msgs from queue.
    if ((err = queue->dump_packets(max, msgs->msgs, count)) != srs_success) {
        return srs_error_wrap(err, "dump packets");
    }

    return err;
}

#ifdef SRS_PERF_QUEUE_COND_WAIT
void SrsRtcConsumer::wait(int nb_msgs, srs_utime_t msgs_duration)
{
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

SrsRtcSource::SrsRtcSource()
{
    req = NULL;
    _source_id = _pre_source_id = -1;

    meta = new SrsMetaCache();
    format = new SrsRtmpFormat();
    rtc = new SrsRtc();

    _can_publish = true;
    rtc_publisher_ = NULL;
    bridger_ = new SrsRtcFromRtmpBridger(this);
}

SrsRtcSource::~SrsRtcSource()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers.clear();

    srs_freep(meta);
    srs_freep(format);
    srs_freep(rtc);

    srs_freep(req);
    srs_freep(bridger_);
}

srs_error_t SrsRtcSource::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r->copy();

    if ((err = format->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    if ((err = rtc->initialize(req)) != srs_success) {
        return srs_error_wrap(err, "rtc initialize");
    }

    return err;
}

void SrsRtcSource::update_auth(SrsRequest* r)
{
    req->update_auth(r);
}

srs_error_t SrsRtcSource::on_source_id_changed(int id)
{
    srs_error_t err = srs_success;

    if (_source_id == id) {
        return err;
    }

    if (_pre_source_id == -1) {
        _pre_source_id = id;
    } else if (_pre_source_id != _source_id) {
        _pre_source_id = _source_id;
    }

    _source_id = id;

    // notice all consumer
    std::vector<SrsRtcConsumer*>::iterator it;
    for (it = consumers.begin(); it != consumers.end(); ++it) {
        SrsRtcConsumer* consumer = *it;
        consumer->update_source_id();
    }

    return err;
}

int SrsRtcSource::source_id()
{
    return _source_id;
}

int SrsRtcSource::pre_source_id()
{
    return _pre_source_id;
}

ISrsSourceBridger* SrsRtcSource::bridger()
{
    return bridger_;
}

srs_error_t SrsRtcSource::create_consumer(SrsConnection* conn, SrsRtcConsumer*& consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtcConsumer(this, conn);
    consumers.push_back(consumer);

    // TODO: FIXME: Implements edge cluster.

    return err;
}

srs_error_t SrsRtcSource::consumer_dumps(SrsRtcConsumer* consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    // Copy metadata and sequence header to consumer.
    // TODO: FIXME: Maybe should not do this for RTC?
    if ((err = meta->dumps(consumer, true, SrsRtmpJitterAlgorithmOFF, dm, dg)) != srs_success) {
        return srs_error_wrap(err, "meta dumps");
    }

    // print status.
    srs_trace("create consumer, no gop cache");

    return err;
}

void SrsRtcSource::on_consumer_destroy(SrsRtcConsumer* consumer)
{
    std::vector<SrsRtcConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        consumers.erase(it);
    }
}

bool SrsRtcSource::can_publish(bool is_edge)
{
    return _can_publish;
}

srs_error_t SrsRtcSource::on_publish()
{
    srs_error_t err = srs_success;

    // update the request object.
    srs_assert(req);

    _can_publish = false;

    if ((err = rtc->on_publish()) != srs_success) {
        return srs_error_wrap(err, "rtc publish");
    }

    // whatever, the publish thread is the source or edge source,
    // save its id to srouce id.
    if ((err = on_source_id_changed(_srs_context->get_id())) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->clear();

    // TODO: FIXME: Handle by statistic.

    return err;
}

void SrsRtcSource::on_unpublish()
{
    // ignore when already unpublished.
    if (_can_publish) {
        return;
    }

    rtc->on_unpublish();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->update_previous_vsh();
    meta->update_previous_ash();

    srs_trace("cleanup when unpublish");

    _can_publish = true;
    _source_id = -1;

    // TODO: FIXME: Handle by statistic.
}

SrsMetaCache* SrsRtcSource::cached_meta()
{
    return meta;
}

SrsRtcPublisher* SrsRtcSource::rtc_publisher()
{
    return rtc_publisher_;
}

void SrsRtcSource::set_rtc_publisher(SrsRtcPublisher* v)
{
    rtc_publisher_ = v;
}

srs_error_t SrsRtcSource::on_rtc_audio(SrsSharedPtrMessage* audio)
{
    // TODO: FIXME: Merge with on_audio.
    // TODO: FIXME: Print key information.
    return on_audio_imp(audio);
}

srs_error_t SrsRtcSource::on_video(SrsCommonMessage* shared_video)
{
    srs_error_t err = srs_success;

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

    // directly process the video message.
    return on_video_imp(&msg);
}

srs_error_t SrsRtcSource::on_audio_imp(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    bool is_aac_sequence_header = SrsFlvAudio::sh(msg->payload, msg->size);
    bool is_sequence_header = is_aac_sequence_header;

    // whether consumer should drop for the duplicated sequence header.
    bool drop_for_reduce = false;
    if (is_sequence_header && meta->previous_ash() && _srs_config->get_reduce_sequence_header(req->vhost)) {
        if (meta->previous_ash()->size == msg->size) {
            drop_for_reduce = srs_bytes_equals(meta->previous_ash()->payload, msg->payload, msg->size);
            srs_warn("drop for reduce sh audio, size=%d", msg->size);
        }
    }

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Parse RTMP message to RTP packets, in FU-A if too large.
    if ((err = rtc->on_audio(msg, format)) != srs_success) {
        // TODO: We should support more strategies.
        srs_warn("rtc: ignore audio error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        rtc->on_unpublish();
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsRtcConsumer* consumer = consumers.at(i);
            if ((err = consumer->enqueue(msg, true, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
                return srs_error_wrap(err, "consume message");
            }
        }
    }

    // cache the sequence header of aac, or first packet of mp3.
    // for example, the mp3 is used for hls to write the "right" audio codec.
    // TODO: FIXME: to refine the stream info system.
    if (is_aac_sequence_header || !meta->ash()) {
        if ((err = meta->update_ash(msg)) != srs_success) {
            return srs_error_wrap(err, "meta consume audio");
        }
    }

    // if atc, update the sequence header to abs time.
    if (meta->ash()) {
        meta->ash()->timestamp = msg->timestamp;
    }
    if (meta->data()) {
        meta->data()->timestamp = msg->timestamp;
    }

    return err;
}

srs_error_t SrsRtcSource::on_video_imp(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    bool is_sequence_header = SrsFlvVideo::sh(msg->payload, msg->size);

    // user can disable the sps parse to workaround when parse sps failed.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sequence_header) {
        format->avc_parse_sps = _srs_config->get_parse_sps(req->vhost);
    }

    if ((err = format->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // Parse RTMP message to RTP packets, in FU-A if too large.
    if ((err = rtc->on_video(msg, format)) != srs_success) {
        // TODO: We should support more strategies.
        srs_warn("rtc: ignore video error %s", srs_error_desc(err).c_str());
        srs_error_reset(err);
        rtc->on_unpublish();
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
    if (is_sequence_header && (err = meta->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    // copy to all consumer
    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers.size(); i++) {
            SrsRtcConsumer* consumer = consumers.at(i);
            if ((err = consumer->enqueue(msg, true, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
                return srs_error_wrap(err, "consume video");
            }
        }
    }

    // if atc, update the sequence header to abs time.
    if (meta->vsh()) {
        meta->vsh()->timestamp = msg->timestamp;
    }
    if (meta->data()) {
        meta->data()->timestamp = msg->timestamp;
    }

    return err;
}

SrsRtcSourceManager::SrsRtcSourceManager()
{
    lock = NULL;
}

SrsRtcSourceManager::~SrsRtcSourceManager()
{
    srs_mutex_destroy(lock);
}

srs_error_t SrsRtcSourceManager::fetch_or_create(SrsRequest* r, SrsRtcSource** pps)
{
    srs_error_t err = srs_success;

    // Lazy create lock, because ST is not ready in SrsRtcSourceManager constructor.
    if (!lock) {
        lock = srs_mutex_new();
    }

    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(lock);

    SrsRtcSource* source = NULL;
    if ((source = fetch(r)) != NULL) {
        *pps = source;
        return err;
    }

    string stream_url = r->get_stream_url();
    string vhost = r->vhost;

    // should always not exists for create a source.
    srs_assert (pool.find(stream_url) == pool.end());

    srs_trace("new source, stream_url=%s", stream_url.c_str());

    source = new SrsRtcSource();
    if ((err = source->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    pool[stream_url] = source;

    *pps = source;

    return err;
}

SrsRtcSource* SrsRtcSourceManager::fetch(SrsRequest* r)
{
    SrsRtcSource* source = NULL;

    string stream_url = r->get_stream_url();
    if (pool.find(stream_url) == pool.end()) {
        return NULL;
    }

    source = pool[stream_url];

    // we always update the request of resource,
    // for origin auth is on, the token in request maybe invalid,
    // and we only need to update the token of request, it's simple.
    source->update_auth(r);

    return source;
}

SrsRtcSourceManager* _srs_rtc_sources = new SrsRtcSourceManager();

SrsRtcFromRtmpBridger::SrsRtcFromRtmpBridger(SrsRtcSource* source)
{
    source_ = source;
}

SrsRtcFromRtmpBridger::~SrsRtcFromRtmpBridger()
{
}

srs_error_t SrsRtcFromRtmpBridger::on_publish()
{
    // TODO: FIXME: Should sync with bridger?
    return source_->on_publish();
}

srs_error_t SrsRtcFromRtmpBridger::on_audio(SrsSharedPtrMessage* audio)
{
    return source_->on_audio_imp(audio);
}

srs_error_t SrsRtcFromRtmpBridger::on_video(SrsSharedPtrMessage* video)
{
    return source_->on_video_imp(video);
}

void SrsRtcFromRtmpBridger::on_unpublish()
{
    // TODO: FIXME: Should sync with bridger?
    source_->on_unpublish();
}

