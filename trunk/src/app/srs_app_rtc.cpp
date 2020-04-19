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

#include <srs_app_rtc.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
using namespace std;

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_rtp.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_protocol_format.hpp>
#include <srs_rtmp_stack.hpp>
#include <openssl/rand.h>
#include <srs_app_audio_recode.hpp>

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

SrsRtpH264Muxer::SrsRtpH264Muxer()
{
    discard_bframe = false;
}

SrsRtpH264Muxer::~SrsRtpH264Muxer()
{
}

srs_error_t SrsRtpH264Muxer::filter(SrsSharedPtrMessage* shared_frame, SrsFormat* format)
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

SrsRtpOpusMuxer::SrsRtpOpusMuxer()
{
    codec = NULL;
}

SrsRtpOpusMuxer::~SrsRtpOpusMuxer()
{
    srs_freep(codec);
}

srs_error_t SrsRtpOpusMuxer::initialize()
{
    srs_error_t err = srs_success;

    codec = new SrsAudioRecode(kChannel, kSamplerate);
    if (!codec) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "SrsAacOpus init failed");
    }

    if ((err = codec->initialize()) != srs_success) {
        return srs_error_wrap(err, "init codec");
    }

    return err;
}

// An AAC packet may be transcoded to many OPUS packets.
const int kMaxOpusPackets = 8;
// The max size for each OPUS packet.
const int kMaxOpusPacketSize = 4096;

srs_error_t SrsRtpOpusMuxer::transcode(SrsSharedPtrMessage* shared_audio, char* adts_audio, int nn_adts_audio)
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

SrsRtc::SrsRtc()
{
    req = NULL;
    hub = NULL;
    
    enabled = false;
    disposable = false;
    last_update_time = 0;

    discard_aac = false;
}

SrsRtc::~SrsRtc()
{
    srs_freep(rtp_h264_muxer);
}

void SrsRtc::dispose()
{
    if (enabled) {
        on_unpublish();
    }
}

// TODO: FIXME: Dead code?
srs_error_t SrsRtc::cycle()
{
    srs_error_t err = srs_success;
    
    return err;
}

srs_error_t SrsRtc::initialize(SrsOriginHub* h, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    hub = h;
    req = r;

    rtp_h264_muxer = new SrsRtpH264Muxer();
    rtp_h264_muxer->discard_bframe = _srs_config->get_rtc_bframe_discard(req->vhost);
    // TODO: FIXME: Support reload and log it.
    discard_aac = _srs_config->get_rtc_aac_discard(req->vhost);

    rtp_opus_muxer = new SrsRtpOpusMuxer();
    if (!rtp_opus_muxer) {
        return srs_error_wrap(err, "rtp_opus_muxer nullptr");
    }
    
    return rtp_opus_muxer->initialize();
}

srs_error_t SrsRtc::on_publish()
{
    srs_error_t err = srs_success;

    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time();
    
    // support multiple publish.
    if (enabled) {
        return err;
    }

	if (!_srs_config->get_rtc_enabled(req->vhost)) {
        return err; 
    }
    
    // if enabled, open the muxer.
    enabled = true;
    
    // ok, the hls can be dispose, or need to be dispose.
    disposable = true;
    
    return err;
}

void SrsRtc::on_unpublish()
{
    // support multiple unpublish.
    if (!enabled) {
        return;
    }
    
    enabled = false;
}

srs_error_t SrsRtc::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
    if (!enabled) {
        return err;
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format->acodec) {
        return err;
    }
    
    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time();
    
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
    if ((err = aac_raw_append_adts_header(shared_audio, format, &adts_audio, &nn_adts_audio)) != srs_success) {
        return srs_error_wrap(err, "aac append header");
    }

    if (adts_audio) {
        err = rtp_opus_muxer->transcode(shared_audio, adts_audio, nn_adts_audio);
        srs_freep(adts_audio);
    }

    return err;
}

srs_error_t SrsRtc::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Maybe it should config on vhost level.
    if (!enabled) {
        return err;
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format->vcodec) {
        return err;
    }

    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time();
    
    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    srs_assert(format->video);
    return rtp_h264_muxer->filter(shared_video, format);
}
