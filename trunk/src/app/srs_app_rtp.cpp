/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_app_rtp.hpp>

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
#include <openssl/rand.h>

static string dump_string_hex(const char* buf, const int nb_buf, const int& max_len);
static string dump_string_hex(const std::string& str, const int& max_len = 128)
{
	return dump_string_hex(str.c_str(), str.size(), max_len);
}

static string dump_string_hex(const char* buf, const int nb_buf, const int& max_len = 128)
{
    char tmp_buf[1024*16];
    int len = 0;
    
    for (int i = 0; i < nb_buf && i < max_len; ++i) {
        int nb = snprintf(tmp_buf + len, sizeof(tmp_buf) - len - 1, "%02X ", (uint8_t)buf[i]);
        if (nb <= 0)
            break;

        len += nb; 
    }   
    tmp_buf[len] = '\0';

    return string(tmp_buf, len);
}

SrsRtpMuxer::SrsRtpMuxer()
{
    sequence = 0;
}

SrsRtpMuxer::~SrsRtpMuxer()
{
}

srs_error_t SrsRtpMuxer::frame_to_packet(SrsSharedPtrMessage* shared_frame, SrsFormat* format)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample sample = format->video->samples[i];
    }

    return err;
}

SrsRtp::SrsRtp()
{
    req = NULL;
    hub = NULL;
    
    enabled = false;
    disposable = false;
    last_update_time = 0;
}

SrsRtp::~SrsRtp()
{
}

void SrsRtp::dispose()
{
    if (enabled) {
        on_unpublish();
    }
}

srs_error_t SrsRtp::cycle()
{
    srs_error_t err = srs_success;
    
    return err;
}

srs_error_t SrsRtp::initialize(SrsOriginHub* h, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    hub = h;
    req = r;

    rtp_h264_muxer = new SrsRtpMuxer();
    
    return err;
}

srs_error_t SrsRtp::on_publish()
{
    srs_error_t err = srs_success;

    // update the hls time, for hls_dispose.
    last_update_time = srs_get_system_time();
    
    // support multiple publish.
    if (enabled) {
        return err;
    }
    
    // if enabled, open the muxer.
    enabled = true;
    
    // ok, the hls can be dispose, or need to be dispose.
    disposable = true;
    
    return err;
}

void SrsRtp::on_unpublish()
{
    srs_error_t err = srs_success;
    
    // support multiple unpublish.
    if (!enabled) {
        return;
    }
    
    enabled = false;
}

srs_error_t SrsRtp::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
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
    
    SrsSharedPtrMessage* audio = shared_audio->copy();
    SrsAutoFree(SrsSharedPtrMessage, audio);
    
    // ts support audio codec: aac/mp3
    SrsAudioCodecId acodec = format->acodec->id;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return err;
    }
    
    // ignore sequence header
    srs_assert(format->audio);

    // TODO: rtc no support aac
    return err;
}

srs_error_t SrsRtp::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    srs_error_t err = srs_success;
    
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
    
    SrsSharedPtrMessage* video = shared_video->copy();
    SrsAutoFree(SrsSharedPtrMessage, video);
    
    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    srs_assert(format->video);
    return rtp_h264_muxer->frame_to_packet(video, format);
}
