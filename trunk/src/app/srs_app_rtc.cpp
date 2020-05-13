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
#include <srs_kernel_rtc_rtp.hpp>
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
#include <srs_app_rtc_codec.hpp>

// TODO: Add this function into SrsRtpMux class.
extern srs_error_t aac_raw_append_adts_header(SrsSharedPtrMessage* shared_audio, SrsFormat* format, char** pbuf, int* pnn_buf);

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

SrsRtc::SrsRtc()
{
    req = NULL;

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

srs_error_t SrsRtc::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    req = r;

    rtp_h264_muxer = new SrsRtpH264Muxer();
    rtp_h264_muxer->discard_bframe = _srs_config->get_rtc_bframe_discard(req->vhost);
    // TODO: FIXME: Support reload and log it.
    discard_aac = _srs_config->get_rtc_aac_discard(req->vhost);
    
    return err;
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
