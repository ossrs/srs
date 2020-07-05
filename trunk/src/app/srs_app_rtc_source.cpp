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
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_core_autofree.hpp>

#ifdef SRS_FFMPEG_FIT
#include <srs_app_rtc_codec.hpp>
#endif

const int kChannel              = 2;
const int kSamplerate           = 48000;

// An AAC packet may be transcoded to many OPUS packets.
const int kMaxOpusPackets = 8;
// The max size for each OPUS packet.
const int kMaxOpusPacketSize = 4096;

// The RTP payload max size, reserved some paddings for SRTP as such:
//      kRtpPacketSize = kRtpMaxPayloadSize + paddings
// For example, if kRtpPacketSize is 1500, recommend to set kRtpMaxPayloadSize to 1400,
// which reserves 100 bytes for SRTP or paddings.
const int kRtpMaxPayloadSize = kRtpPacketSize - 200;

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

SrsRtcConsumer::SrsRtcConsumer(SrsRtcSource* s)
{
    source = s;
    should_update_source_id = false;

    mw_wait = srs_cond_new();
    mw_min_msgs = 0;
    mw_waiting = false;
}

SrsRtcConsumer::~SrsRtcConsumer()
{
    source->on_consumer_destroy(this);

    vector<SrsRtpPacket2*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsRtpPacket2* pkt = *it;
        srs_freep(pkt);
    }

    srs_cond_destroy(mw_wait);
}

void SrsRtcConsumer::update_source_id()
{
    should_update_source_id = true;
}

srs_error_t SrsRtcConsumer::enqueue(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    queue.push_back(pkt);

    if (mw_waiting) {
        if ((int)queue.size() > mw_min_msgs) {
            srs_cond_signal(mw_wait);
            mw_waiting = false;
            return err;
        }
    }

    return err;
}

srs_error_t SrsRtcConsumer::dump_packets(std::vector<SrsRtpPacket2*>& pkts)
{
    srs_error_t err = srs_success;

    if (should_update_source_id) {
        srs_trace("update source_id=%d[%d]", source->source_id().c_str(), source->source_id().c_str());
        should_update_source_id = false;
    }

    queue.swap(pkts);

    return err;
}

void SrsRtcConsumer::wait(int nb_msgs)
{
    mw_min_msgs = nb_msgs;

    // when duration ok, signal to flush.
    if ((int)queue.size() > mw_min_msgs) {
        return;
    }

    // the enqueue will notify this cond.
    mw_waiting = true;

    // use cond block wait for high performance mode.
    srs_cond_wait(mw_wait);
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

ISrsRtcPublisher::ISrsRtcPublisher()
{
}

ISrsRtcPublisher::~ISrsRtcPublisher()
{
}

SrsRtcSource::SrsRtcSource()
{
    _can_publish = true;
    rtc_publisher_ = NULL;

    req = NULL;
#ifdef SRS_FFMPEG_FIT
    bridger_ = new SrsRtcFromRtmpBridger(this);
#else
    bridger_ = new SrsRtcDummyBridger();
#endif
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

#ifdef SRS_FFMPEG_FIT
    SrsRtcFromRtmpBridger* bridger = dynamic_cast<SrsRtcFromRtmpBridger*>(bridger_);
    if ((err = bridger->initialize(req)) != srs_success) {
        return srs_error_wrap(err, "bridge initialize");
    }
#endif

    return err;
}

void SrsRtcSource::update_auth(SrsRequest* r)
{
    req->update_auth(r);
}

srs_error_t SrsRtcSource::on_source_id_changed(SrsContextId id)
{
    srs_error_t err = srs_success;

    if (!_source_id.compare(id)) {
        return err;
    }

    if (_pre_source_id.empty()) {
        _pre_source_id = id;
    } else if (_pre_source_id.compare(_source_id)) {
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

SrsContextId SrsRtcSource::source_id()
{
    return _source_id;
}

SrsContextId SrsRtcSource::pre_source_id()
{
    return _pre_source_id;
}

ISrsSourceBridger* SrsRtcSource::bridger()
{
    return bridger_;
}

srs_error_t SrsRtcSource::create_consumer(SrsRtcConsumer*& consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtcConsumer(this);
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
    _source_id = SrsContextId();

    // TODO: FIXME: Handle by statistic.
}

ISrsRtcPublisher* SrsRtcSource::rtc_publisher()
{
    return rtc_publisher_;
}

void SrsRtcSource::set_rtc_publisher(ISrsRtcPublisher* v)
{
    rtc_publisher_ = v;
}

srs_error_t SrsRtcSource::on_rtp(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < (int)consumers.size(); i++) {
        SrsRtcConsumer* consumer = consumers.at(i);
        if ((err = consumer->enqueue(pkt->copy())) != srs_success) {
            return srs_error_wrap(err, "consume message");
        }
    }

    return err;
}

#ifdef SRS_FFMPEG_FIT
SrsRtcFromRtmpBridger::SrsRtcFromRtmpBridger(SrsRtcSource* source)
{
    req = NULL;
    source_ = source;
    format = new SrsRtmpFormat();
    codec = new SrsAudioRecode(kChannel, kSamplerate);
    discard_aac = false;
    discard_bframe = false;
    merge_nalus = false;
    meta = new SrsMetaCache();
    audio_timestamp = 0;
    audio_sequence = 0;
    video_sequence = 0;
}

SrsRtcFromRtmpBridger::~SrsRtcFromRtmpBridger()
{
    srs_freep(format);
    srs_freep(codec);
    srs_freep(meta);
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

    // TODO: FIXME: Support reload.
    discard_aac = _srs_config->get_rtc_aac_discard(req->vhost);
    discard_bframe = _srs_config->get_rtc_bframe_discard(req->vhost);
    merge_nalus = _srs_config->get_rtc_server_merge_nalus();
    srs_trace("RTC bridge from RTMP, discard_aac=%d, discard_bframe=%d, merge_nalus=%d",
        discard_aac, discard_bframe, merge_nalus);

    return err;
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
        err = transcode(adts_audio, nn_adts_audio);
        srs_freep(adts_audio);
    }

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::transcode(char* adts_audio, int nn_adts_audio)
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
    if ((err = codec->transcode(&aac, opus_payloads, opus_sizes, nn_opus_packets)) != srs_success) {
        return srs_error_wrap(err, "recode error");
    }

    // Save OPUS packets in shared message.
    if (nn_opus_packets <= 0) {
        return err;
    }

    int nn_max_extra_payload = 0;
    for (int i = 0; i < nn_opus_packets; i++) {
        char* data = (char*)opus_payloads[i];
        int size = (int)opus_sizes[i];

        // TODO: FIXME: Use it to padding audios.
        nn_max_extra_payload = srs_max(nn_max_extra_payload, size);

        SrsRtpPacket2* pkt = NULL;
        SrsAutoFree(SrsRtpPacket2, pkt);

        if ((err = package_opus(data, size, &pkt)) != srs_success) {
            return srs_error_wrap(err, "package opus");
        }

        if ((err = source_->on_rtp(pkt)) != srs_success) {
            return srs_error_wrap(err, "consume opus");
        }
    }

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::package_opus(char* data, int size, SrsRtpPacket2** ppkt)
{
    srs_error_t err = srs_success;

    SrsRtpPacket2* pkt = new SrsRtpPacket2();
    pkt->frame_type = SrsFrameTypeAudio;
    pkt->header.set_marker(true);
    pkt->header.set_sequence(audio_sequence++);
    pkt->header.set_timestamp(audio_timestamp);

    // TODO: FIXME: Why 960? Need Refactoring?
    audio_timestamp += 960;

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->payload = raw;

    raw->payload = new char[size];
    raw->nn_payload = size;
    memcpy(raw->payload, data, size);

    pkt->shared_msg = new SrsSharedPtrMessage();
    pkt->shared_msg->wrap(raw->payload, size);

    *ppkt = pkt;

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::on_video(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    // cache the sequence header if h264
    bool is_sequence_header = SrsFlvVideo::sh(msg->payload, msg->size);
    if (is_sequence_header && (err = meta->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    if ((err = format->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    bool has_idr = false;
    vector<SrsSample*> samples;
    if ((err = filter(msg, format, has_idr, samples)) != srs_success) {
        return srs_error_wrap(err, "filter video");
    }
    int nn_samples = (int)samples.size();

    // Well, for each IDR, we append a SPS/PPS before it, which is packaged in STAP-A.
    if (has_idr) {
        SrsRtpPacket2* pkt = NULL;
        SrsAutoFree(SrsRtpPacket2, pkt);

        if ((err = package_stap_a(source_, msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "package stap-a");
        }

        if ((err = source_->on_rtp(pkt)) != srs_success) {
            return srs_error_wrap(err, "consume sps/pps");
        }
    }

    // If merge Nalus, we pcakges all NALUs(samples) as one NALU, in a RTP or FUA packet.
    vector<SrsRtpPacket2*> pkts;
    if (merge_nalus && nn_samples > 1) {
        if ((err = package_nalus(msg, samples, pkts)) != srs_success) {
            return srs_error_wrap(err, "package nalus as one");
        }
    } else {
        // By default, we package each NALU(sample) to a RTP or FUA packet.
        for (int i = 0; i < nn_samples; i++) {
            SrsSample* sample = samples[i];

            // We always ignore bframe here, if config to discard bframe,
            // the bframe flag will not be set.
            if (sample->bframe) {
                continue;
            }

            if (sample->size <= kRtpMaxPayloadSize) {
                if ((err = package_single_nalu(msg, sample, pkts)) != srs_success) {
                    return srs_error_wrap(err, "package single nalu");
                }
            } else {
                if ((err = package_fu_a(msg, sample, kRtpMaxPayloadSize, pkts)) != srs_success) {
                    return srs_error_wrap(err, "package fu-a");
                }
            }
        }
    }

    if (pkts.size() > 0) {
        pkts.back()->header.set_marker(true);
    }

    return consume_packets(pkts);
}

srs_error_t SrsRtcFromRtmpBridger::filter(SrsSharedPtrMessage* msg, SrsFormat* format, bool& has_idr, vector<SrsSample*>& samples)
{
    srs_error_t err = srs_success;

    // If IDR, we will insert SPS/PPS before IDR frame.
    if (format->video && format->video->has_idr) {
        has_idr = true;
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

        samples.push_back(sample);
    }

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::package_stap_a(SrsRtcSource* source, SrsSharedPtrMessage* msg, SrsRtpPacket2** ppkt)
{
    srs_error_t err = srs_success;

    SrsFormat* format = meta->vsh_format();
    if (!format || !format->vcodec) {
        return err;
    }

    // Note that the sps/pps may change, so we should copy it.
    const vector<char>& sps = format->vcodec->sequenceParameterSetNALUnit;
    const vector<char>& pps = format->vcodec->pictureParameterSetNALUnit;
    if (sps.empty() || pps.empty()) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "sps/pps empty");
    }

    SrsRtpPacket2* pkt = new SrsRtpPacket2();
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->header.set_marker(false);
    pkt->header.set_sequence(video_sequence++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpSTAPPayload* stap = new SrsRtpSTAPPayload();
    pkt->payload = stap;

    uint8_t header = sps[0];
    stap->nri = (SrsAvcNaluType)header;

    // Copy the SPS/PPS bytes, because it may change.
    int size = (int)(sps.size() + pps.size());
    char* payload = new char[size];
    pkt->shared_msg = new SrsSharedPtrMessage();
    pkt->shared_msg->wrap(payload, size);

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = (int)sps.size();
        stap->nalus.push_back(sample);

        memcpy(payload, (char*)&sps[0], sps.size());
        payload += (int)sps.size();
    }

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = (int)pps.size();
        stap->nalus.push_back(sample);

        memcpy(payload, (char*)&pps[0], pps.size());
        payload += (int)pps.size();
    }

    *ppkt = pkt;
    srs_info("RTC STAP-A seq=%u, sps %d, pps %d bytes", pkt->header.get_sequence(), sps.size(), pps.size());

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::package_nalus(SrsSharedPtrMessage* msg, const vector<SrsSample*>& samples, vector<SrsRtpPacket2*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpRawNALUs* raw = new SrsRtpRawNALUs();

    for (int i = 0; i < (int)samples.size(); i++) {
        SrsSample* sample = samples[i];

        // We always ignore bframe here, if config to discard bframe,
        // the bframe flag will not be set.
        if (sample->bframe) {
            continue;
        }

        raw->push_back(sample->copy());
    }

    // Ignore empty.
    int nn_bytes = raw->nb_bytes();
    if (nn_bytes <= 0) {
        srs_freep(raw);
        return err;
    }

    if (nn_bytes < kRtpMaxPayloadSize) {
        // Package NALUs in a single RTP packet.
        SrsRtpPacket2* pkt = new SrsRtpPacket2();
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->header.set_sequence(video_sequence++);
        pkt->header.set_timestamp(msg->timestamp * 90);
        pkt->payload = raw;
        pkt->shared_msg = msg->copy();
        pkts.push_back(pkt);
    } else {
        // We must free it, should never use RTP packets to free it,
        // because more than one RTP packet will refer to it.
        SrsAutoFree(SrsRtpRawNALUs, raw);

        // Package NALUs in FU-A RTP packets.
        int fu_payload_size = kRtpMaxPayloadSize;

        // The first byte is store in FU-A header.
        uint8_t header = raw->skip_first_byte();
        uint8_t nal_type = header & kNalTypeMask;
        int nb_left = nn_bytes - 1;

        int num_of_packet = 1 + (nn_bytes - 1) / fu_payload_size;
        for (int i = 0; i < num_of_packet; ++i) {
            int packet_size = srs_min(nb_left, fu_payload_size);

            SrsRtpFUAPayload* fua = new SrsRtpFUAPayload();
            if ((err = raw->read_samples(fua->nalus, packet_size)) != srs_success) {
                srs_freep(fua);
                return srs_error_wrap(err, "read samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
            }

            SrsRtpPacket2* pkt = new SrsRtpPacket2();
            pkt->frame_type = SrsFrameTypeVideo;
            pkt->header.set_sequence(video_sequence++);
            pkt->header.set_timestamp(msg->timestamp * 90);

            fua->nri = (SrsAvcNaluType)header;
            fua->nalu_type = (SrsAvcNaluType)nal_type;
            fua->start = bool(i == 0);
            fua->end = bool(i == num_of_packet - 1);

            pkt->payload = fua;
            pkt->shared_msg = msg->copy();
            pkts.push_back(pkt);

            nb_left -= packet_size;
        }
    }

    return err;
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtcFromRtmpBridger::package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, vector<SrsRtpPacket2*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpPacket2* pkt = new SrsRtpPacket2();
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->header.set_sequence(video_sequence++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->payload = raw;

    raw->payload = sample->bytes;
    raw->nn_payload = sample->size;

    pkt->shared_msg = msg->copy();
    pkts.push_back(pkt);

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, vector<SrsRtpPacket2*>& pkts)
{
    srs_error_t err = srs_success;

    char* p = sample->bytes + 1;
    int nb_left = sample->size - 1;
    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    int num_of_packet = 1 + (sample->size - 1) / fu_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        int packet_size = srs_min(nb_left, fu_payload_size);

        SrsRtpPacket2* pkt = new SrsRtpPacket2();
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->header.set_sequence(video_sequence++);
        pkt->header.set_timestamp(msg->timestamp * 90);

        SrsRtpFUAPayload2* fua = new SrsRtpFUAPayload2();
        pkt->payload = fua;

        fua->nri = (SrsAvcNaluType)header;
        fua->nalu_type = (SrsAvcNaluType)nal_type;
        fua->start = bool(i == 0);
        fua->end = bool(i == num_of_packet - 1);

        fua->payload = p;
        fua->size = packet_size;

        pkt->shared_msg = msg->copy();
        pkts.push_back(pkt);

        p += packet_size;
        nb_left -= packet_size;
    }

    return err;
}

srs_error_t SrsRtcFromRtmpBridger::consume_packets(vector<SrsRtpPacket2*>& pkts)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Consume a range of packets.
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket2* pkt = pkts[i];
        if ((err = source_->on_rtp(pkt)) != srs_success) {
            err = srs_error_wrap(err, "consume sps/pps");
            break;
        }
    }

    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket2* pkt = pkts[i];
        srs_freep(pkt);
    }

    return err;
}
#endif

SrsRtcDummyBridger::SrsRtcDummyBridger()
{
}

SrsRtcDummyBridger::~SrsRtcDummyBridger()
{
}

srs_error_t SrsRtcDummyBridger::on_publish()
{
    return srs_error_new(ERROR_RTC_DUMMY_BRIDGER, "no FFmpeg fit");
}

srs_error_t SrsRtcDummyBridger::on_audio(SrsSharedPtrMessage* /*audio*/)
{
    return srs_error_new(ERROR_RTC_DUMMY_BRIDGER, "no FFmpeg fit");
}

srs_error_t SrsRtcDummyBridger::on_video(SrsSharedPtrMessage* /*video*/)
{
    return srs_error_new(ERROR_RTC_DUMMY_BRIDGER, "no FFmpeg fit");
}

void SrsRtcDummyBridger::on_unpublish()
{
}

