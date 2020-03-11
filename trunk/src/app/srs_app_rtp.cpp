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

    if (format->is_avc_sequence_header()) {
        sps.assign(format->vcodec->sequenceParameterSetNALUnit.data(), format->vcodec->sequenceParameterSetNALUnit.size());
        pps.assign(format->vcodec->pictureParameterSetNALUnit.data(), format->vcodec->pictureParameterSetNALUnit.size());
    }

    vector<SrsSample> rtp_packet_vec;

    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample sample = format->video->samples[i];

        uint8_t header = sample.bytes[0];
        uint8_t nal_type = header & kNalTypeMask;

        if (nal_type == 0x06) {
            srs_trace("ignore SEI");
            continue;
        }

        if (sample.size <= max_payload_size) {
            packet_single_nalu(shared_frame, format, &sample, rtp_packet_vec);
        } else {
            packet_fu_a(shared_frame, format, &sample, rtp_packet_vec);
        }

        srs_trace("nal size=%d, nal=%s", sample.size, dump_string_hex(sample.bytes, sample.size, sample.size).c_str());
        for (int i = 0; i < shared_frame->nb_rtp_fragments; ++i) {
            srs_trace("rtp=%s", dump_string_hex(shared_frame->rtp_fragments[i].bytes, shared_frame->rtp_fragments[i].size, kRtpPacketSize).c_str());
        }
    }

    SrsSample* rtp_samples = new SrsSample[rtp_packet_vec.size()];
    for (int i = 0; i < rtp_packet_vec.size(); ++i) {
        rtp_samples[i] = rtp_packet_vec[i];
    }

    shared_frame->set_rtp_fragments(rtp_samples, rtp_packet_vec.size());

    return err;
}

srs_error_t SrsRtpMuxer::packet_fu_a(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample, vector<SrsSample>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    char* p = sample->bytes + 1;
    int nb_left = sample->size - 1;
    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    if (nal_type == kIdr) {
        packet_stap_a(sps, pps, shared_frame, rtp_packet_vec);
    }

    int num_of_packet = (sample->size - 1 + max_payload_size) / max_payload_size;
    int avg_packet_size = sample->size / num_of_packet;
    for (int i = 0; i < num_of_packet; ++i) {
        char* buf = new char[kRtpPacketSize];
        SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
        SrsAutoFree(SrsBuffer, stream);

        int packet_size = min(nb_left, max_payload_size);

        // v=2,p=0,x=0,cc=0
        stream->write_1bytes(0x80);
        // marker payloadtype
        if (i == num_of_packet - 1) {
            stream->write_1bytes(kMarker | kH264PayloadType);
        } else {
            stream->write_1bytes(kH264PayloadType);
        }
        // sequence
        srs_trace("sequence=%u", sequence);
        stream->write_2bytes(sequence++);
        // timestamp
        stream->write_4bytes(int32_t(shared_frame->timestamp * 90));
        // ssrc
        stream->write_4bytes(int32_t(kVideoSSRC));

        // fu-indicate
        uint8_t fu_indicate = kFuA;
        fu_indicate |= (header & (~kNalTypeMask));
        stream->write_1bytes(fu_indicate);

        uint8_t fu_header = nal_type;
        if (i == 0)
            fu_header |= kStart;
        if (i == num_of_packet - 1)
            fu_header |= kEnd;
        stream->write_1bytes(fu_header);

        stream->write_bytes(p, packet_size);
        p += packet_size;
        nb_left -= packet_size;


        SrsSample rtp_packet;
        rtp_packet.bytes = stream->data();
        rtp_packet.size = stream->pos();

        rtp_packet_vec.push_back(rtp_packet);
    }
}

srs_error_t SrsRtpMuxer::packet_single_nalu(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample, vector<SrsSample>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
    SrsAutoFree(SrsBuffer, stream);

    if (nal_type == kIdr) {
        packet_stap_a(sps, pps, shared_frame, rtp_packet_vec);
    }

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kMarker | kH264PayloadType);
    // sequenct
    srs_trace("sequence=%u", sequence);
    stream->write_2bytes(sequence++);
    // timestamp
    stream->write_4bytes(int32_t(shared_frame->timestamp * 90));
    // ssrc
    stream->write_4bytes(int32_t(kVideoSSRC));

    stream->write_bytes(sample->bytes, sample->size);

    SrsSample rtp_packet;
    rtp_packet.bytes = stream->data();
    rtp_packet.size = stream->pos();

    rtp_packet_vec.push_back(rtp_packet);

    return err;
}

srs_error_t SrsRtpMuxer::packet_stap_a(const string &sps, const string& pps, SrsSharedPtrMessage* shared_frame, vector<SrsSample>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    uint8_t header = sps[0];
    uint8_t nal_type = header & kNalTypeMask;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
    SrsAutoFree(SrsBuffer, stream);

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kMarker | kH264PayloadType);
    // sequenct
    srs_trace("sequence=%u", sequence);
    stream->write_2bytes(sequence++);
    // timestamp
    stream->write_4bytes(int32_t(shared_frame->timestamp * 90));
    // ssrc
    stream->write_4bytes(int32_t(kVideoSSRC));

    // stap-a header
    uint8_t stap_a_header = kStapA;
    stap_a_header |= (nal_type & (~kNalTypeMask));
    stream->write_1bytes(stap_a_header);

    stream->write_2bytes(sps.size());
    stream->write_bytes((char*)sps.data(), sps.size());

    stream->write_2bytes(pps.size());
    stream->write_bytes((char*)pps.data(), pps.size());

    SrsSample rtp_packet;
    rtp_packet.bytes = stream->data();
    rtp_packet.size = stream->pos();

    rtp_packet_vec.push_back(rtp_packet);

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
