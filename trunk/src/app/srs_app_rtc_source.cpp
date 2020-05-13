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
#include <srs_kernel_buffer.hpp>
#include <srs_app_rtc_codec.hpp>

const int kChannel              = 2;
const int kSamplerate           = 48000;

// An AAC packet may be transcoded to many OPUS packets.
const int kMaxOpusPackets = 8;
// The max size for each OPUS packet.
const int kMaxOpusPacketSize = 4096;

using namespace std;

// TODO: Add this function into SrsRtpMux class.
srs_error_t aac_raw_append_adts_header(SrsSharedPtrMessage* shared_audio, SrsFormat* format, char** pbuf, int* pnn_buf)
{
    srs_error_t err = srs_success;

    if (format->is_aac_sequence_header()) {
        return err;
    }

    if (format->audio->nb_samples != 1) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "adts");
    }

    int nb_buf = format->audio->samples[0].size + 7;
    char* buf = new char[nb_buf];
    SrsBuffer stream(buf, nb_buf);

    // TODO: Add comment.
    stream.write_1bytes(0xFF);
    stream.write_1bytes(0xF9);
    stream.write_1bytes(((format->acodec->aac_object - 1) << 6) | ((format->acodec->aac_sample_rate & 0x0F) << 2) | ((format->acodec->aac_channels & 0x04) >> 2));
    stream.write_1bytes(((format->acodec->aac_channels & 0x03) << 6) | ((nb_buf >> 11) & 0x03));
    stream.write_1bytes((nb_buf >> 3) & 0xFF);
    stream.write_1bytes(((nb_buf & 0x07) << 5) | 0x1F);
    stream.write_1bytes(0xFC);

    stream.write_bytes(format->audio->samples[0].bytes, format->audio->samples[0].size);

    *pbuf = buf;
    *pnn_buf = nb_buf;

    return err;
}

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
    _source_id = _pre_source_id = -1;
    _can_publish = true;
    rtc_publisher_ = NULL;

    req = NULL;
    bridger_ = new SrsRtcFromRtmpBridger(this);
}

SrsRtcSource::~SrsRtcSource()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers.clear();

    srs_freep(req);
    srs_freep(bridger_);
}

srs_error_t SrsRtcSource::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r->copy();

    if ((err = bridger_->initialize(req)) != srs_success) {
        return srs_error_wrap(err, "bridge initialize");
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

    // whatever, the publish thread is the source or edge source,
    // save its id to srouce id.
    if ((err = on_source_id_changed(_srs_context->get_id())) != srs_success) {
        return srs_error_wrap(err, "source id change");
    }

    // TODO: FIXME: Handle by statistic.

    return err;
}

void SrsRtcSource::on_unpublish()
{
    // ignore when already unpublished.
    if (_can_publish) {
        return;
    }

    srs_trace("cleanup when unpublish");

    _can_publish = true;
    _source_id = -1;

    // TODO: FIXME: Handle by statistic.
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

    // copy to all consumer
    for (int i = 0; i < (int)consumers.size(); i++) {
        SrsRtcConsumer* consumer = consumers.at(i);
        if ((err = consumer->enqueue(msg, true, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    return err;
}

srs_error_t SrsRtcSource::on_video_imp(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    // copy to all consumer
    for (int i = 0; i < (int)consumers.size(); i++) {
        SrsRtcConsumer* consumer = consumers.at(i);
        if ((err = consumer->enqueue(msg, true, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
            return srs_error_wrap(err, "consume video");
        }
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
    req = NULL;
    source_ = source;
    meta = new SrsMetaCache();
    format = new SrsRtmpFormat();
    codec = new SrsAudioRecode(kChannel, kSamplerate);
    discard_aac = false;
    discard_bframe = false;
}

SrsRtcFromRtmpBridger::~SrsRtcFromRtmpBridger()
{
    srs_freep(meta);
    srs_freep(format);
    srs_freep(codec);
}

srs_error_t SrsRtcFromRtmpBridger::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r;

    if ((err = format->initialize()) != srs_success) {
        return srs_error_wrap(err, "format initialize");
    }

    if ((err = codec->initialize()) != srs_success) {
        return srs_error_wrap(err, "init codec");
    }

    // TODO: FIXME: Support reload and log it.
    discard_aac = _srs_config->get_rtc_aac_discard(req->vhost);
    discard_bframe = _srs_config->get_rtc_bframe_discard(req->vhost);
    srs_trace("RTC bridge from RTMP, discard_aac=%d, discard_bframe=%d", discard_aac, discard_bframe);

    return err;
}

SrsMetaCache* SrsRtcFromRtmpBridger::cached_meta()
{
    return meta;
}

srs_error_t SrsRtcFromRtmpBridger::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridger?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->clear();

    return err;
}

void SrsRtcFromRtmpBridger::on_unpublish()
{
    // TODO: FIXME: Should sync with bridger?
    source_->on_unpublish();

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->update_previous_vsh();
    meta->update_previous_ash();
}

srs_error_t SrsRtcFromRtmpBridger::on_audio(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format->acodec) {
        return err;
    }

    // ts support audio codec: aac/mp3
    SrsAudioCodecId acodec = format->acodec->id;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return err;
    }

    // When drop aac audio packet, never transcode.
    if (discard_aac && acodec == SrsAudioCodecIdAAC) {
        return err;
    }

    // ignore sequence header
    srs_assert(format->audio);

    char* adts_audio = NULL;
    int nn_adts_audio = 0;
    // TODO: FIXME: Reserve 7 bytes header when create shared message.
    if ((err = aac_raw_append_adts_header(msg, format, &adts_audio, &nn_adts_audio)) != srs_success) {
        return srs_error_wrap(err, "aac append header");
    }

    if (adts_audio) {
        err = transcode(msg, adts_audio, nn_adts_audio);
        srs_freep(adts_audio);
    }

    return source_->on_audio_imp(msg);
}

srs_error_t SrsRtcFromRtmpBridger::transcode(SrsSharedPtrMessage* shared_audio, char* adts_audio, int nn_adts_audio)
{
    srs_error_t err = srs_success;

    // Opus packet cache.
    static char* opus_payloads[kMaxOpusPackets];

    static bool initialized = false;
    if (!initialized) {
        initialized = true;

        static char opus_packets_cache[kMaxOpusPackets][kMaxOpusPacketSize];
        opus_payloads[0] = &opus_packets_cache[0][0];
        for (int i = 1; i < kMaxOpusPackets; i++) {
           opus_payloads[i] = opus_packets_cache[i];
        }
    }

    // Transcode an aac packet to many opus packets.
    SrsSample aac;
    aac.bytes = adts_audio;
    aac.size = nn_adts_audio;

    int nn_opus_packets = 0;
    int opus_sizes[kMaxOpusPackets];
    if ((err = codec->recode(&aac, opus_payloads, opus_sizes, nn_opus_packets)) != srs_success) {
        return srs_error_wrap(err, "recode error");
    }

    // Save OPUS packets in shared message.
    if (nn_opus_packets <= 0) {
        return err;
    }

    int nn_max_extra_payload = 0;
    SrsSample samples[nn_opus_packets];
    for (int i = 0; i < nn_opus_packets; i++) {
        SrsSample* p = samples + i;
        p->size = opus_sizes[i];
        p->bytes = new char[p->size];
        memcpy(p->bytes, opus_payloads[i], p->size);

        nn_max_extra_payload = srs_max(nn_max_extra_payload, p->size);
    }

    shared_audio->set_extra_payloads(samples, nn_opus_packets);
    shared_audio->set_max_extra_payload(nn_max_extra_payload);

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::on_video(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    bool is_sequence_header = SrsFlvVideo::sh(msg->payload, msg->size);

    // user can disable the sps parse to workaround when parse sps failed.
    // @see https://github.com/ossrs/srs/issues/474
    if (is_sequence_header) {
        format->avc_parse_sps = _srs_config->get_parse_sps(req->vhost);
    }

    // cache the sequence header if h264
    if (is_sequence_header && (err = meta->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    if ((err = format->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    return source_->on_video_imp(msg);
}

srs_error_t SrsRtcFromRtmpBridger::filter(SrsSharedPtrMessage* shared_frame, SrsFormat* format)
{
    srs_error_t err = srs_success;

    // If IDR, we will insert SPS/PPS before IDR frame.
    if (format->video && format->video->has_idr) {
        shared_frame->set_has_idr(true);
    }

    // Update samples to shared frame.
    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample* sample = &format->video->samples[i];

        // Because RTC does not support B-frame, so we will drop them.
        // TODO: Drop B-frame in better way, which not cause picture corruption.
        if (discard_bframe) {
            if ((err = sample->parse_bframe()) != srs_success) {
                return srs_error_wrap(err, "parse bframe");
            }
            if (sample->bframe) {
                continue;
            }
        }
    }

    if (format->video->nb_samples <= 0) {
        return err;
    }

    shared_frame->set_samples(format->video->samples, format->video->nb_samples);

    return err;
}

