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

#include <unistd.h>

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
#include <srs_app_rtc_queue.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_json.hpp>

#ifdef SRS_FFMPEG_FIT
#include <srs_app_rtc_codec.hpp>
#endif

const int kAudioPayloadType     = 111;
const int kAudioChannel         = 2;
const int kAudioSamplerate      = 48000;

const int kVideoPayloadType = 102;
const int kVideoSamplerate  = 90000;

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

uint64_t SrsNtp::kMagicNtpFractionalUnit = 1ULL << 32;

SrsNtp::SrsNtp()
{
    system_ms_ = 0;
    ntp_ = 0;
    ntp_second_ = 0;
    ntp_fractions_ = 0;
}

SrsNtp::~SrsNtp()
{
}

SrsNtp SrsNtp::from_time_ms(uint64_t ms)
{
    SrsNtp srs_ntp;
    srs_ntp.system_ms_ = ms;
    srs_ntp.ntp_second_ = ms / 1000;
    srs_ntp.ntp_fractions_ = (static_cast<double>(ms % 1000 / 1000.0)) * kMagicNtpFractionalUnit;
    srs_ntp.ntp_ = (static_cast<uint64_t>(srs_ntp.ntp_second_) << 32) | srs_ntp.ntp_fractions_;
    return srs_ntp;
}

SrsNtp SrsNtp::to_time_ms(uint64_t ntp)
{
    SrsNtp srs_ntp;
    srs_ntp.ntp_ = ntp;
    srs_ntp.ntp_second_ = (ntp & 0xFFFFFFFF00000000ULL) >> 32;
    srs_ntp.ntp_fractions_ = (ntp & 0x00000000FFFFFFFFULL);
    srs_ntp.system_ms_ = (static_cast<uint64_t>(srs_ntp.ntp_second_) * 1000) +
        (static_cast<double>(static_cast<uint64_t>(srs_ntp.ntp_fractions_) * 1000.0) / kMagicNtpFractionalUnit);
    return srs_ntp;
}

SrsRtcConsumer::SrsRtcConsumer(SrsRtcStream* s)
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
        srs_trace("update source_id=%s[%s]", source->source_id().c_str(), source->source_id().c_str());
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

SrsRtcStreamManager::SrsRtcStreamManager()
{
    lock = NULL;
}

SrsRtcStreamManager::~SrsRtcStreamManager()
{
    srs_mutex_destroy(lock);
}

srs_error_t SrsRtcStreamManager::fetch_or_create(SrsRequest* r, SrsRtcStream** pps)
{
    srs_error_t err = srs_success;

    // Lazy create lock, because ST is not ready in SrsRtcStreamManager constructor.
    if (!lock) {
        lock = srs_mutex_new();
    }

    // Use lock to protect coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(lock);

    SrsRtcStream* source = NULL;
    if ((source = fetch(r)) != NULL) {
        *pps = source;
        return err;
    }

    string stream_url = r->get_stream_url();
    string vhost = r->vhost;

    // should always not exists for create a source.
    srs_assert (pool.find(stream_url) == pool.end());

    srs_trace("new source, stream_url=%s", stream_url.c_str());

    source = new SrsRtcStream();
    if ((err = source->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    pool[stream_url] = source;

    *pps = source;

    return err;
}

SrsRtcStream* SrsRtcStreamManager::fetch(SrsRequest* r)
{
    SrsRtcStream* source = NULL;

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

SrsRtcStreamManager* _srs_rtc_sources = new SrsRtcStreamManager();

ISrsRtcPublishStream::ISrsRtcPublishStream()
{
}

ISrsRtcPublishStream::~ISrsRtcPublishStream()
{
}

SrsRtcStream::SrsRtcStream()
{
    _can_publish = true;
    publish_stream_ = NULL;
    stream_desc_ = NULL;

    req = NULL;
#ifdef SRS_FFMPEG_FIT
    bridger_ = new SrsRtcFromRtmpBridger(this);
#else
    bridger_ = new SrsRtcDummyBridger();
#endif
}

SrsRtcStream::~SrsRtcStream()
{
    // never free the consumers,
    // for all consumers are auto free.
    consumers.clear();

    srs_freep(req);
    srs_freep(bridger_);
    srs_freep(stream_desc_);
}

srs_error_t SrsRtcStream::initialize(SrsRequest* r)
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

void SrsRtcStream::update_auth(SrsRequest* r)
{
    req->update_auth(r);
}

srs_error_t SrsRtcStream::on_source_id_changed(SrsContextId id)
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

SrsContextId SrsRtcStream::source_id()
{
    return _source_id;
}

SrsContextId SrsRtcStream::pre_source_id()
{
    return _pre_source_id;
}

ISrsSourceBridger* SrsRtcStream::bridger()
{
    return bridger_;
}

srs_error_t SrsRtcStream::create_consumer(SrsRtcConsumer*& consumer)
{
    srs_error_t err = srs_success;

    consumer = new SrsRtcConsumer(this);
    consumers.push_back(consumer);

    // TODO: FIXME: Implements edge cluster.

    return err;
}

srs_error_t SrsRtcStream::consumer_dumps(SrsRtcConsumer* consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    // print status.
    srs_trace("create consumer, no gop cache");

    return err;
}

void SrsRtcStream::on_consumer_destroy(SrsRtcConsumer* consumer)
{
    std::vector<SrsRtcConsumer*>::iterator it;
    it = std::find(consumers.begin(), consumers.end(), consumer);
    if (it != consumers.end()) {
        consumers.erase(it);
    }
}

bool SrsRtcStream::can_publish(bool is_edge)
{
    return _can_publish;
}

srs_error_t SrsRtcStream::on_publish()
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

void SrsRtcStream::on_unpublish()
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

ISrsRtcPublishStream* SrsRtcStream::publish_stream()
{
    return publish_stream_;
}

void SrsRtcStream::set_publish_stream(ISrsRtcPublishStream* v)
{
    publish_stream_ = v;
}

srs_error_t SrsRtcStream::on_rtp(SrsRtpPacket2* pkt)
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

void SrsRtcStream::set_stream_desc(SrsRtcStreamDescription* stream_desc)
{
    srs_freep(stream_desc_);
    stream_desc_ = stream_desc->copy();
}

std::vector<SrsRtcTrackDescription*> SrsRtcStream::get_track_desc(std::string type, std::string media_name)
{
    std::vector<SrsRtcTrackDescription*> track_descs;
    if (!stream_desc_) {
        return track_descs;
    }

    if (type == "audio") {
        if (stream_desc_->audio_track_desc_->media_->name_ == media_name) {
            track_descs.push_back(stream_desc_->audio_track_desc_);
        }
    }

    if (type == "video") {
        std::vector<SrsRtcTrackDescription*>::iterator it = stream_desc_->video_track_descs_.begin();
        while (it != stream_desc_->video_track_descs_.end() ){
            track_descs.push_back(*it);
            ++it;
        }
    }

    return track_descs;
}

#ifdef SRS_FFMPEG_FIT
SrsRtcFromRtmpBridger::SrsRtcFromRtmpBridger(SrsRtcStream* source)
{
    req = NULL;
    source_ = source;
    format = new SrsRtmpFormat();
    codec = new SrsAudioRecode(kAudioChannel, kAudioSamplerate);
    discard_aac = false;
    discard_bframe = false;
    merge_nalus = false;
    meta = new SrsMetaCache();
    audio_timestamp = 0;
    audio_sequence = 0;
    video_sequence = 0;

    SrsRtcStreamDescription* stream_desc = new SrsRtcStreamDescription();
    SrsAutoFree(SrsRtcStreamDescription, stream_desc);

    // audio track description
    if (true) {
        SrsRtcTrackDescription* audio_track_desc = new SrsRtcTrackDescription();
        audio_track_desc->type_ = "audio";
        audio_track_desc->id_ = "audio-"  + srs_random_str(8);

        audio_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        audio_track_desc->ssrc_ = audio_ssrc;
        audio_track_desc->direction_ = "recvonly";

        audio_track_desc->media_ = new SrsAudioPayload(kAudioPayloadType, "opus", kAudioSamplerate, kAudioChannel);
        stream_desc->audio_track_desc_ = audio_track_desc->copy();
    }

    // video track description
    if (true) {
        SrsRtcTrackDescription* video_track_desc = new SrsRtcTrackDescription();
        video_track_desc->type_ = "video";
        video_track_desc->id_ = "video-"  + srs_random_str(8);

        video_ssrc = SrsRtcSSRCGenerator::instance()->generate_ssrc();
        video_track_desc->ssrc_ = video_ssrc;
        video_track_desc->direction_ = "recvonly";

        video_track_desc->media_ = new SrsVideoPayload(kVideoPayloadType, "H264", kVideoSamplerate);
        stream_desc->video_track_descs_.push_back(video_track_desc->copy());
    }

    source_->set_stream_desc(stream_desc);
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
    pkt->header.set_payload_type(kAudioPayloadType);
    pkt->header.set_ssrc(audio_ssrc);
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

srs_error_t SrsRtcFromRtmpBridger::package_stap_a(SrsRtcStream* source, SrsSharedPtrMessage* msg, SrsRtpPacket2** ppkt)
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
    pkt->header.set_payload_type(kVideoPayloadType);
    pkt->header.set_ssrc(video_ssrc);
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
        pkt->header.set_payload_type(kVideoPayloadType);
        pkt->header.set_ssrc(video_ssrc);
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
            pkt->header.set_payload_type(kVideoPayloadType);
            pkt->header.set_ssrc(video_ssrc);
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
    pkt->header.set_payload_type(kVideoPayloadType);
    pkt->header.set_ssrc(video_ssrc);
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
        pkt->header.set_payload_type(kVideoPayloadType);
        pkt->header.set_ssrc(video_ssrc);
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

SrsCodecPayload::SrsCodecPayload()
{
}

SrsCodecPayload::SrsCodecPayload(uint8_t pt, std::string encode_name, int sample)
{
    pt_ = pt;
    name_ = encode_name;
    sample_ = sample;
}

SrsCodecPayload::~SrsCodecPayload()
{
}

SrsCodecPayload* SrsCodecPayload::copy()
{
    SrsCodecPayload* cp = new SrsCodecPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;

    return cp;
}

SrsMediaPayloadType SrsCodecPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    return media_payload_type;
}

SrsVideoPayload::SrsVideoPayload()
{
}

SrsVideoPayload::SrsVideoPayload(uint8_t pt, std::string encode_name, int sample)
    :SrsCodecPayload(pt, encode_name, sample)
{
    h264_param_.profile_level_id = "";
    h264_param_.packetization_mode = "";
    h264_param_.level_asymmerty_allow = "";
}

SrsVideoPayload::~SrsVideoPayload()
{
}

SrsVideoPayload* SrsVideoPayload::copy()
{
    SrsVideoPayload* cp = new SrsVideoPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->h264_param_ = h264_param_;

    return cp;
}

SrsMediaPayloadType SrsVideoPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    if (!h264_param_.level_asymmerty_allow.empty()) {
        format_specific_param << "level-asymmetry-allowed=" << h264_param_.level_asymmerty_allow;
    }
    if (!h264_param_.packetization_mode.empty()) {
        format_specific_param << ";packetization-mode=" << h264_param_.packetization_mode;
    }
    if (!h264_param_.profile_level_id.empty()) {
        format_specific_param << ";profile-level-id=" << h264_param_.profile_level_id;
    }

    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

srs_error_t SrsVideoPayload::set_h264_param_desc(std::string fmtp)
{
    srs_error_t err = srs_success;
    std::vector<std::string> vec = split_str(fmtp, ";");
    for (size_t i = 0; i < vec.size(); ++i) {
        std::vector<std::string> kv = split_str(vec[i], "=");
        if (kv.size() == 2) {
            if (kv[0] == "profile-level-id") {
                h264_param_.profile_level_id = kv[1];
            } else if (kv[0] == "packetization-mode") {
                // 6.3.  Non-Interleaved Mode
                // This mode is in use when the value of the OPTIONAL packetization-mode
                // media type parameter is equal to 1.  This mode SHOULD be supported.
                // It is primarily intended for low-delay applications.  Only single NAL
                // unit packets, STAP-As, and FU-As MAY be used in this mode.  STAP-Bs,
                // MTAPs, and FU-Bs MUST NOT be used.  The transmission order of NAL
                // units MUST comply with the NAL unit decoding order.
                // @see https://tools.ietf.org/html/rfc6184#section-6.3
                h264_param_.packetization_mode = kv[1];
            } else if (kv[0] == "level-asymmetry-allowed") {
                h264_param_.level_asymmerty_allow = kv[1];
            } else {
                return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h264 param=%s", kv[0].c_str());
            }
        } else {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid h264 param=%s", vec[i].c_str());
        }
    }

    return err;
}

SrsAudioPayload::SrsAudioPayload()
{
}

SrsAudioPayload::SrsAudioPayload(uint8_t pt, std::string encode_name, int sample, int channel)
    :SrsCodecPayload(pt, encode_name, sample)
{
    channel_ = channel;
    opus_param_.minptime = 0;
    opus_param_.use_inband_fec = false;
    opus_param_.usedtx = false;
}

SrsAudioPayload::~SrsAudioPayload()
{
}

SrsAudioPayload* SrsAudioPayload::copy()
{
    SrsAudioPayload* cp = new SrsAudioPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->channel_ = channel_;
    cp->opus_param_ = opus_param_;

    return cp;
}

SrsMediaPayloadType SrsAudioPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    if (channel_ != 0) {
        media_payload_type.encoding_param_ = srs_int2str(channel_);
    }
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    std::ostringstream format_specific_param;
    if (opus_param_.minptime) {
        format_specific_param << "minptime=" << opus_param_.minptime;
    }
    if (opus_param_.use_inband_fec) {
        format_specific_param << ";useinbandfec=1";
    }
    if (opus_param_.usedtx) {
        format_specific_param << ";usedtx=1";
    }
    media_payload_type.format_specific_param_ = format_specific_param.str();

    return media_payload_type;
}

srs_error_t SrsAudioPayload::set_opus_param_desc(std::string fmtp)
{
    srs_error_t err = srs_success;
    std::vector<std::string> vec = split_str(fmtp, ";");
    for (size_t i = 0; i < vec.size(); ++i) {
        std::vector<std::string> kv = split_str(vec[i], "=");
        if (kv.size() == 2) {
            if (kv[0] == "minptime") {
                opus_param_.minptime = (int)::atol(kv[1].c_str());
            } else if (kv[0] == "useinbandfec") {
                opus_param_.use_inband_fec = (kv[1] == "1") ? true : false;
            } else if (kv[0] == "usedtx") {
                opus_param_.usedtx = (kv[1] == "1") ? true : false;
            }
        } else {
            return srs_error_new(ERROR_RTC_SDP_DECODE, "invalid opus param=%s", vec[i].c_str());
        }
    }

    return err;
}

SrsRedPayload::SrsRedPayload()
{
}

SrsRedPayload::SrsRedPayload(uint8_t pt, std::string encode_name, int sample, int channel)
    :SrsCodecPayload(pt, encode_name, sample)
{
    channel_ = channel;
}

SrsRedPayload::~SrsRedPayload()
{
}

SrsRedPayload* SrsRedPayload::copy()
{
    SrsRedPayload* cp = new SrsRedPayload();

    cp->type_ = type_;
    cp->pt_ = pt_;
    cp->name_ = name_;
    cp->sample_ = sample_;
    cp->rtcp_fbs_ = rtcp_fbs_;
    cp->channel_ = channel_;

    return cp;
}

SrsMediaPayloadType SrsRedPayload::generate_media_payload_type()
{
    SrsMediaPayloadType media_payload_type(pt_);

    media_payload_type.encoding_name_ = name_;
    media_payload_type.clock_rate_ = sample_;
    if (channel_ != 0) {
        media_payload_type.encoding_param_ = srs_int2str(channel_);
    }
    media_payload_type.rtcp_fb_ = rtcp_fbs_;

    return media_payload_type;
}

SrsRtcTrackDescription::SrsRtcTrackDescription()
{
    ssrc_ = 0;
    rtx_ssrc_ = 0;
    fec_ssrc_ = 0;
    is_active_ = true;

    media_ = NULL;
    red_ = NULL;
    rtx_ = NULL;
    ulpfec_ = NULL;
}

SrsRtcTrackDescription::~SrsRtcTrackDescription()
{
    srs_freep(media_);
    srs_freep(red_);
    srs_freep(rtx_);
    srs_freep(ulpfec_);
}

bool SrsRtcTrackDescription::has_ssrc(uint32_t ssrc)
{
    if (!is_active_) {
        return false;
    }

    if (ssrc == ssrc_ || ssrc == rtx_ssrc_ || ssrc == fec_ssrc_) {
        return true;
    }
    return false;
}

void SrsRtcTrackDescription::add_rtp_extension_desc(int id, std::string uri)
{
    extmaps_[id] = uri;
}

void SrsRtcTrackDescription::set_direction(std::string direction)
{
    direction_ = direction;
}

void SrsRtcTrackDescription::set_codec_payload(SrsCodecPayload* payload)
{
    media_ = payload;
}

void SrsRtcTrackDescription::create_auxiliary_payload(const std::vector<SrsMediaPayloadType> payloads)
{
    if (!payloads.size()) {
        return;
    }

    SrsMediaPayloadType payload = payloads.at(0);
    if (payload.encoding_name_ == "red"){
        srs_freep(red_);
        red_ = new SrsRedPayload(payload.payload_type_, "red", payload.clock_rate_, ::atol(payload.encoding_param_.c_str()));
    } else if (payload.encoding_name_ == "rtx") {
        srs_freep(rtx_);
        rtx_ = new SrsCodecPayload(payload.payload_type_, "rtx", payload.clock_rate_);
    } else if (payload.encoding_name_ == "ulpfec") {
        srs_freep(ulpfec_);
        ulpfec_ = new SrsCodecPayload(payload.payload_type_, "ulpfec", payload.clock_rate_);
    }
}

void SrsRtcTrackDescription::set_rtx_ssrc(uint32_t ssrc)
{
    rtx_ssrc_ = ssrc;
}

void SrsRtcTrackDescription::set_fec_ssrc(uint32_t ssrc)
{
    fec_ssrc_ = ssrc;
}

void SrsRtcTrackDescription::set_mid(std::string mid)
{
    mid_ = mid;
}

int SrsRtcTrackDescription::get_rtp_extension_id(std::string uri)
{
     for(std::map<int, std::string>::iterator it = extmaps_.begin(); it != extmaps_.end(); ++it) {
        if(uri == it->second) {
            return it->first;
        }
    }

    return 0;
}

SrsRtcTrackDescription* SrsRtcTrackDescription::copy()
{
    SrsRtcTrackDescription* cp = new SrsRtcTrackDescription();

    cp->type_ = type_;
    cp->id_ = id_;
    cp->ssrc_ = ssrc_;
    cp->fec_ssrc_ = fec_ssrc_;
    cp->rtx_ssrc_ = rtx_ssrc_;
    cp->extmaps_ = extmaps_;
    cp->direction_ = direction_;
    cp->mid_ = mid_;
    cp->msid_ = msid_;
    cp->is_active_ = is_active_;
    cp->media_ = media_ ? media_->copy():NULL;
    cp->red_ = red_ ? red_->copy():NULL;
    cp->rtx_ = rtx_ ? rtx_->copy():NULL;
    cp->ulpfec_ = ulpfec_ ? ulpfec_->copy():NULL;

    return cp;
}

SrsRtcStreamDescription::SrsRtcStreamDescription()
{
    audio_track_desc_ = NULL;
}

SrsRtcStreamDescription::~SrsRtcStreamDescription()
{
    srs_freep(audio_track_desc_);

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        srs_freep(video_track_descs_.at(i));
    }
    video_track_descs_.clear();
}

SrsRtcStreamDescription* SrsRtcStreamDescription::copy()
{
    SrsRtcStreamDescription* stream_desc = new SrsRtcStreamDescription();

    if (audio_track_desc_) {
        stream_desc->audio_track_desc_ = audio_track_desc_->copy();
    }

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        stream_desc->video_track_descs_.push_back(video_track_descs_.at(i)->copy());
    }

    return stream_desc;
}

SrsRtcTrackDescription* SrsRtcStreamDescription::find_track_description_by_ssrc(uint32_t ssrc)
{
    if (audio_track_desc_->has_ssrc(ssrc)) {
        return audio_track_desc_;
    }

    for (int i = 0; i < (int)video_track_descs_.size(); ++i) {
        if (video_track_descs_.at(i)->has_ssrc(ssrc)) {
            return video_track_descs_.at(i);
        }
    }

    return NULL;
}

SrsRtcTrackStatistic::SrsRtcTrackStatistic()
{
    packets = 0;
    last_packets = 0;
    bytes = 0;
    last_bytes = 0;
    nacks = 0;
    last_nacks = 0;
    padding_packets = 0;
    last_padding_packets = 0;
    padding_bytes = 0;
    last_padding_bytes = 0;
    replay_packets = 0;
    last_replay_packets = 0;
    replay_bytes = 0;
    last_replay_bytes = 0;
}

SrsRtcRecvTrack::SrsRtcRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc, bool is_audio)
{
    session_ = session;
    track_desc_ = track_desc->copy();
    statistic_ = new SrsRtcTrackStatistic();

    if (is_audio) {
        rtp_queue_ = new SrsRtpRingBuffer(100);
        nack_receiver_ = new SrsRtpNackForReceiver(rtp_queue_, 100 * 2 / 3);
    } else {
        rtp_queue_ = new SrsRtpRingBuffer(1000);
        nack_receiver_ = new SrsRtpNackForReceiver(rtp_queue_, 1000 * 2 / 3);
    }
}

SrsRtcRecvTrack::~SrsRtcRecvTrack()
{
    srs_freep(rtp_queue_);
    srs_freep(nack_receiver_);
    srs_freep(track_desc_);
    srs_freep(statistic_);
}

bool SrsRtcRecvTrack::has_ssrc(uint32_t ssrc)
{
    if (track_desc_) {
        return track_desc_->has_ssrc(ssrc);
    }

    return false;
}

void SrsRtcRecvTrack::update_rtt(int rtt)
{
    if (nack_receiver_) {
        nack_receiver_->update_rtt(rtt);
    }
}

void SrsRtcRecvTrack::update_send_report_time(const SrsNtp& ntp)
{
    last_sender_report_ntp = ntp;
    last_sender_report_sys_time = srs_update_system_time();;
}

srs_error_t SrsRtcRecvTrack::send_rtcp_rr()
{
    srs_error_t err = srs_success;

    if (session_) {
        return session_->send_rtcp_rr(track_desc_->ssrc_, rtp_queue_, last_sender_report_sys_time, last_sender_report_ntp);
    }

    return err;
}

srs_error_t SrsRtcRecvTrack::send_rtcp_xr_rrtr()
{
    srs_error_t err = srs_success;

    if (track_desc_) {
        return session_->send_rtcp_xr_rrtr(track_desc_->ssrc_);
    }

    return err;
}

srs_error_t SrsRtcRecvTrack::on_nack(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    uint32_t ssrc = pkt->header.get_ssrc();
    uint16_t seq = pkt->header.get_sequence();

    // TODO: check whether is necessary?
    nack_receiver_->remove_timeout_packets();
    SrsRtpNackInfo* nack_info = nack_receiver_->find(seq);
    if (nack_info) {
        // seq had been received.
        nack_receiver_->remove(seq);
        return err;
    }

    // insert check nack list
    uint16_t nack_first = 0, nack_last = 0;
    if (!rtp_queue_->update(seq, nack_first, nack_last)) {
        srs_warn("too old seq %u, range [%u, %u]", seq, rtp_queue_->begin, rtp_queue_->end);
    }
    if (srs_rtp_seq_distance(nack_first, nack_last) > 0) {
        srs_trace("update seq=%u, nack range [%u, %u]", seq, nack_first, nack_last);
        nack_receiver_->insert(nack_first, nack_last);
        nack_receiver_->check_queue_size();
    }

    // insert into video_queue and audio_queue
    rtp_queue_->set(seq, pkt->copy());

    // send_nack
    uint32_t sent_nacks = 0;
    session_->check_send_nacks(nack_receiver_, ssrc, sent_nacks);
    statistic_->nacks += sent_nacks;

    return err;
}

srs_error_t SrsRtcRecvTrack::on_rtp(SrsRtcStream* source, SrsRtpPacket2* pkt)
{
    return srs_success;
}

SrsRtcAudioRecvTrack::SrsRtcAudioRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcRecvTrack(session, track_desc, true)
{
}

SrsRtcAudioRecvTrack::~SrsRtcAudioRecvTrack()
{
}

srs_error_t SrsRtcAudioRecvTrack::on_rtp(SrsRtcStream* source, SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    // connection level statistic
    session_->stat_->nn_in_audios++;

    // track level statistic
    statistic_->packets++;
    statistic_->bytes += pkt->nb_bytes();

    if (source) {
        if ((err = source->on_rtp(pkt)) != srs_success) {
            return srs_error_wrap(err, "source on rtp");
        }
    }

    // For NACK to handle packet.
    if ((err = on_nack(pkt)) != srs_success) {
        return srs_error_wrap(err, "on nack");
    }

    return err;
}

SrsRtcVideoRecvTrack::SrsRtcVideoRecvTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcRecvTrack(session, track_desc, false)
{
    request_key_frame_ = false;
}

SrsRtcVideoRecvTrack::~SrsRtcVideoRecvTrack()
{
}

srs_error_t SrsRtcVideoRecvTrack::on_rtp(SrsRtcStream* source, SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;

    // connection level statistic
    session_->stat_->nn_in_videos++;

    // track level statistic
    statistic_->packets++;
    statistic_->bytes += pkt->nb_bytes();

    pkt->frame_type = SrsFrameTypeVideo;

    if (source) {
        if ((err = source->on_rtp(pkt)) != srs_success) {
            return srs_error_wrap(err, "source on rtp");
        }
    }

    // TODO: FIXME: add rtp process
    if (request_key_frame_) {
        // TODO: FIXME: add coroutine to request key frame.
        request_key_frame_ = false;
        // TODO: FIXME: Check error.
        session_->send_rtcp_fb_pli(track_desc_->ssrc_);
    }

    // For NACK to handle packet.
    if ((err = on_nack(pkt)) != srs_success) {
        return srs_error_wrap(err, "on nack");
    }

    return err;
}

void SrsRtcVideoRecvTrack::request_keyframe()
{
    request_key_frame_ = true;
}


SrsRtcSendTrack::SrsRtcSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc, bool is_audio)
{
    session_ = session;
    track_desc_ = track_desc->copy();
    statistic_ = new SrsRtcTrackStatistic();

    if (is_audio) {
        rtp_queue_ = new SrsRtpRingBuffer(100);
    } else {
        rtp_queue_ = new SrsRtpRingBuffer(1000);
    }
}

SrsRtcSendTrack::~SrsRtcSendTrack()
{
    srs_freep(rtp_queue_);
    srs_freep(track_desc_);
    srs_freep(statistic_);
}

bool SrsRtcSendTrack::has_ssrc(uint32_t ssrc)
{
    if (track_desc_) {
        return track_desc_->has_ssrc(ssrc);
    }

    return false;
}

SrsRtpPacket2* SrsRtcSendTrack::fetch_rtp_packet(uint16_t seq)
{
    if (rtp_queue_) {
        return rtp_queue_->at(seq);
    }

    return NULL;
}

// TODO: FIXME: Should refine logs, set tracks in a time.
void SrsRtcSendTrack::set_track_status(bool active)
{
    track_desc_->is_active_ = active;
}

bool SrsRtcSendTrack::get_track_status()
{
    return track_desc_->is_active_;
}

std::string SrsRtcSendTrack::get_track_id()
{
    return track_desc_->id_;
}

srs_error_t SrsRtcSendTrack::on_rtp(SrsRtpPacket2* pkt, SrsRtcPlayStreamStatistic& info)
{
    return srs_success;
}

srs_error_t SrsRtcSendTrack::on_rtcp(SrsRtpPacket2* pkt)
{
    return srs_success;
}

void SrsRtcSendTrack::on_recv_nack()
{
    SrsRtcTrackStatistic* statistic = statistic_;

    statistic->nacks++;
}

SrsRtcAudioSendTrack::SrsRtcAudioSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcSendTrack(session, track_desc, true)
{
}

SrsRtcAudioSendTrack::~SrsRtcAudioSendTrack()
{
}

srs_error_t SrsRtcAudioSendTrack::on_rtp(SrsRtpPacket2* pkt, SrsRtcPlayStreamStatistic& info)
{
    srs_error_t err = srs_success;

    if (!track_desc_->is_active_) {
        return err;
    }

    pkt->header.set_ssrc(track_desc_->ssrc_);

    // Put rtp packet to NACK/ARQ queue
    if (true) {
        SrsRtpPacket2* nack = pkt->copy();
        rtp_queue_->set(nack->header.get_sequence(), nack);
    }

    // Update stats.
    info.nn_bytes += pkt->nb_bytes();
    info.nn_audios++;
    session_->stat_->nn_out_audios++;

    // track level statistic
    statistic_->packets++;
    statistic_->bytes += pkt->nb_bytes();

    if (true) {
        std::vector<SrsRtpPacket2*> pkts;
        pkts.push_back(pkt);

        if ((err = session_->do_send_packets(pkts, info)) != srs_success) {
            return srs_error_wrap(err, "raw send");
        }
    }

    return err;
}

srs_error_t SrsRtcAudioSendTrack::on_rtcp(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;
    // process rtcp
    return err;
}

SrsRtcVideoSendTrack::SrsRtcVideoSendTrack(SrsRtcConnection* session, SrsRtcTrackDescription* track_desc)
    : SrsRtcSendTrack(session, track_desc, false)
{
}

SrsRtcVideoSendTrack::~SrsRtcVideoSendTrack()
{
}

srs_error_t SrsRtcVideoSendTrack::on_rtp(SrsRtpPacket2* pkt, SrsRtcPlayStreamStatistic& info)
{
    srs_error_t err = srs_success;

    if (!track_desc_->is_active_) {
        return err;
    }

    SrsRtcTrackStatistic* statistic = statistic_;
    
    pkt->header.set_ssrc(track_desc_->ssrc_);

    // Put rtp packet to NACK/ARQ queue
    if (true) {
        SrsRtpPacket2* nack = pkt->copy();
        rtp_queue_->set(nack->header.get_sequence(), nack);
    }

    // Update stats.
    info.nn_bytes += pkt->nb_bytes();
    info.nn_videos++;
    session_->stat_->nn_out_videos++;

    // track level statistic
    statistic->packets++;
    statistic->bytes += pkt->nb_bytes();

    if (true) {
        std::vector<SrsRtpPacket2*> pkts;
        pkts.push_back(pkt);

        if ((err = session_->do_send_packets(pkts, info)) != srs_success) {
            return srs_error_wrap(err, "raw send");
        }
    }
    
    return err;
}

srs_error_t SrsRtcVideoSendTrack::on_rtcp(SrsRtpPacket2* pkt)
{
    srs_error_t err = srs_success;
    // process rtcp
    return err;
}

SrsRtcSSRCGenerator* SrsRtcSSRCGenerator::_instance = NULL;

SrsRtcSSRCGenerator::SrsRtcSSRCGenerator()
{
    ssrc_num = 0;
}

SrsRtcSSRCGenerator::~SrsRtcSSRCGenerator()
{
}

SrsRtcSSRCGenerator* SrsRtcSSRCGenerator::instance()
{
    if (!_instance) {
        _instance = new SrsRtcSSRCGenerator();
    }
    return _instance;
}

uint32_t SrsRtcSSRCGenerator::generate_ssrc()
{
    if (!ssrc_num) {
        ssrc_num = ::getpid() * 10000 + ::getpid() * 100 + ::getpid();
    }

    return ++ssrc_num;
}

