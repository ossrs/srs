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
    string ret;
    ret.reserve(max_len * 4);

    char tmp_buf[1024*16];
    tmp_buf[0] = '\n';
    int len = 1;
    
    for (int i = 0; i < nb_buf && i < max_len; ++i) {
        //int nb = snprintf(tmp_buf + len, sizeof(tmp_buf) - len - 2, "(%03d)%02X ", i, (uint8_t)buf[i]);
        int nb = snprintf(tmp_buf + len, sizeof(tmp_buf) - len - 2, "%02X ", (uint8_t)buf[i]);
        if (nb <= 0)
            break;

        len += nb; 

        if (i % 48 == 47) {
            tmp_buf[len++] = '\n';
            ret.append(tmp_buf, len);
            len = 0;
        }   
    }   
    tmp_buf[len] = '\0';
    ret.append(tmp_buf, len);

    return ret;
}


SrsRtpMuxer::SrsRtpMuxer()
{
    sequence = 0;
}

SrsRtpMuxer::~SrsRtpMuxer()
{
}

#if 0
srs_error_t SrsRtpMuxer::frame_to_packet(SrsSharedPtrMessage* shared_frame, SrsFormat* format)
{
    srs_error_t err = srs_success;

    int nb_samples = format->video->nb_samples;
    SrsSample* samples = format->video->samples;

    SrsSample* rtp_fragment_samples = new SrsSample[2000];
    int rtp_fragment_samples_index = 0;

    static int debug_fd = -1;
    static uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};
    if (debug_fd < 0) {
        debug_fd = open("./raw.264", O_CREAT|O_TRUNC|O_RDWR, 0664);
    }

    SrsSample sps_pps_samples[2];
    if (format->is_avc_sequence_header()) {
        sps_pps_samples[0].bytes = format->vcodec->sequenceParameterSetNALUnit.data();
        sps_pps_samples[0].size = format->vcodec->sequenceParameterSetNALUnit.size();
        sps_pps_samples[1].bytes = format->vcodec->pictureParameterSetNALUnit.data();
        sps_pps_samples[1].size = format->vcodec->pictureParameterSetNALUnit.size();

        nb_samples = 2;
        samples = sps_pps_samples;

        {
            char* buf = new char[1460];
            SrsBuffer* stream = new SrsBuffer(buf, 1460);
            SrsAutoFree(SrsBuffer, stream);
            // write rtp header first
            stream->write_1bytes(0x80);
            stream->write_1bytes(102);

            stream->write_2bytes(sequence++);
            stream->write_4bytes((int32_t)shared_frame->timestamp * 90);
            stream->write_4bytes((int32_t)3233846889);

            stream->write_1bytes(24/*STAP-A*/);
            // AUD
            stream->write_2bytes(2);
            stream->write_1bytes(0x09);
            stream->write_1bytes(0x10);

            stream->write_2bytes(sps_pps_samples[0].size);
            stream->write_bytes(sps_pps_samples[0].bytes, sps_pps_samples[0].size);
            stream->write_2bytes(sps_pps_samples[1].size);
            stream->write_bytes(sps_pps_samples[1].bytes, sps_pps_samples[1].size);

            if (debug_fd >= 0) {
                write(debug_fd, start_code, sizeof(start_code));
                write(debug_fd, sps_pps_samples[0].bytes, sps_pps_samples[0].size);
                write(debug_fd, start_code, sizeof(start_code));
                write(debug_fd, sps_pps_samples[1].bytes, sps_pps_samples[1].size);
            }

            rtp_fragment_samples[rtp_fragment_samples_index].bytes = stream->data();
            rtp_fragment_samples[rtp_fragment_samples_index].size = stream->pos();

            ++rtp_fragment_samples_index;
        }
        shared_frame->set_rtp_fragments(rtp_fragment_samples, rtp_fragment_samples_index);

        return err;
    }

    for (int i = 0; i < nb_samples; ++i) {
        SrsSample sample = samples[i];

        srs_trace("nal size=%d, dump=%s", sample.size, dump_string_hex(sample.bytes, sample.size, sample.size).c_str());

        if ((sample.bytes[0] & 0x1F) == 0x06) {
            srs_trace("ignore SEI");
            continue;
        }

        if (debug_fd >= 0) {
            write(debug_fd, start_code, sizeof(start_code));
            write(debug_fd, sample.bytes, sample.size);
        }

        static int max_packet_size = 900;
        if (sample.size <= max_packet_size) {
            char* buf = new char[1460];
            SrsBuffer* stream = new SrsBuffer(buf, 1460);
            SrsAutoFree(SrsBuffer, stream);
            // write rtp header first
            stream->write_1bytes(0x80);
            if ((sample.bytes[0] & 0x1F) <= 5) {
                stream->write_1bytes((1 << 7) | 102);
            } else {
                stream->write_1bytes(102);
            }

            stream->write_2bytes(sequence++);
            stream->write_4bytes((int32_t)shared_frame->timestamp * 90);
            stream->write_4bytes((int32_t)3233846889);

#if 0 // single nalu
            stream->write_bytes(sample.bytes, sample.size);
#else
            stream->write_1bytes((sample.bytes[0] & 0xE0) | 24/*STAP-A*/);
            stream->write_2bytes(sample.size);
            stream->write_bytes(sample.bytes, sample.size);
#endif

            rtp_fragment_samples[rtp_fragment_samples_index].bytes = stream->data();
            rtp_fragment_samples[rtp_fragment_samples_index].size = stream->pos();

            ++rtp_fragment_samples_index;
        } else {
            int num_of_packet = (sample.size + max_packet_size) / max_packet_size;
            char* p = sample.bytes + 1;
            int left_bytes = sample.size - 1;
            for (int n = 0; n < num_of_packet; ++n) {
                char* buf = new char[1460];
                SrsBuffer* stream = new SrsBuffer(buf, 1460);
                SrsAutoFree(SrsBuffer, stream);
                // write rtp header first
                stream->write_1bytes(0x80);
                if ((sample.bytes[0] & 0x1F) <= 5) {
                    stream->write_1bytes((1 << 7) | 102);
                } else {
                    stream->write_1bytes(102);
                }

                stream->write_2bytes(sequence++);
                stream->write_4bytes((int32_t)shared_frame->timestamp * 90);
                stream->write_4bytes((int32_t)3233846889);

                stream->write_1bytes((sample.bytes[0] & 0xE0) | 28);
                if (n == 0) {
                    stream->write_1bytes(0x80 | (sample.bytes[0] & 0x1F));
                } else if (n == num_of_packet - 1) {
                    stream->write_1bytes(0x40 | (sample.bytes[0] & 0x1F));
                } else {
                    stream->write_1bytes(0x00 | (sample.bytes[0] & 0x1F));
                }

                int len = left_bytes > max_packet_size ? max_packet_size : left_bytes;
                stream->write_bytes(p, len);
                left_bytes -= len;
                p += len;

                rtp_fragment_samples[rtp_fragment_samples_index].bytes = stream->data();
                rtp_fragment_samples[rtp_fragment_samples_index].size = stream->pos();

                ++rtp_fragment_samples_index;

            }
        }
    }
    shared_frame->set_rtp_fragments(rtp_fragment_samples, rtp_fragment_samples_index);

    return err;
}
#endif

const int max_payload_size = 1200;
const int kRtpPacketSize = 1500;

const uint8_t kMarker = 0x80;
const uint8_t kH264PayloadType = 102;

const uint8_t kNalTypeMask = 0x1F;

const uint8_t kIdr = 5;
const uint8_t kStapA = 24;
const uint8_t kFua = 28;

const uint8_t kStart = 0x80;
const uint8_t kEnd = 0x40;

const uint32_t kVideoSSRC = 3233846889;

srs_error_t SrsRtpMuxer::frame_to_packet(SrsSharedPtrMessage* shared_frame, SrsFormat* format)
{
    srs_error_t err = srs_success;

    if (format->is_avc_sequence_header()) {
        sps.assign(format->vcodec->sequenceParameterSetNALUnit.data(), format->vcodec->sequenceParameterSetNALUnit.size());
        pps.assign(format->vcodec->pictureParameterSetNALUnit.data(), format->vcodec->pictureParameterSetNALUnit.size());
    }

    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample sample = format->video->samples[i];

        uint8_t header = sample.bytes[0];
        uint8_t nal_type = header & 0x1F;

        if (nal_type == 0x06) {
            srs_trace("ignore SEI");
            continue;
        }

        if (sample.size <= max_payload_size) {
            packet_single_nalu(shared_frame, format, &sample);
        } else {
            packet_fu_a(shared_frame, format, &sample);
        }

        srs_trace("nal size=%d, nal=%s", sample.size, dump_string_hex(sample.bytes, sample.size, sample.size).c_str());
        for (int i = 0; i < shared_frame->nb_rtp_fragments; ++i) {
            srs_trace("rtp=%s", dump_string_hex(shared_frame->rtp_fragments[i].bytes, shared_frame->rtp_fragments[i].size, kRtpPacketSize).c_str());
        }
    }

    return err;
}

srs_error_t SrsRtpMuxer::packet_fu_a(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample)
{
    srs_error_t err = srs_success;

    vector<SrsSample> rtp_packet_vec;

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
        SrsBuffer* stream = new SrsBuffer(buf, 1460);
        SrsAutoFree(SrsBuffer, stream);

        int packet_size = min(nb_left, max_payload_size);

        // v=2,p=0,x=0,cc=0
        stream->write_1bytes(0x80);
        // marker payloadtype
        stream->write_1bytes(kMarker | kH264PayloadType);
        // sequenct
        stream->write_2bytes(sequence++);
        // timestamp
        stream->write_4bytes(int32_t(shared_frame->timestamp * 90));
        // ssrc
        stream->write_4bytes(int32_t(kVideoSSRC));

        // fu-indicate
        uint8_t fu_indicate = kFua;
        fu_indicate |= (nal_type & (~kNalTypeMask));
        stream->write_1bytes(fu_indicate);

        uint8_t fu_header = nal_type & kNalTypeMask;
        if (i == 0)
            fu_header |= kStart;
        if (i == num_of_packet - 1)
            fu_header |= kEnd;
        stream->write_1bytes(fu_header);

        stream->write_bytes(p, packet_size);
        p += packet_size;
        nb_left -= max_payload_size;


        SrsSample rtp_packet;
        rtp_packet.bytes = stream->data();
        rtp_packet.size = stream->pos();

        rtp_packet_vec.push_back(rtp_packet);
    }

    SrsSample* rtp_samples = new SrsSample[rtp_packet_vec.size()];
    for (int i = 0; i < rtp_packet_vec.size(); ++i) {
        rtp_samples[i] = rtp_packet_vec[i];
    }

    shared_frame->set_rtp_fragments(rtp_samples, rtp_packet_vec.size());
}

srs_error_t SrsRtpMuxer::packet_single_nalu(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample)
{
    srs_error_t err = srs_success;

    vector<SrsSample> rtp_packet_vec;

    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, 1460);
    SrsAutoFree(SrsBuffer, stream);

    if (nal_type == kIdr) {
        packet_stap_a(sps, pps, shared_frame, rtp_packet_vec);
    }

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kMarker | kH264PayloadType);
    // sequenct
    stream->write_2bytes(sequence++);
    // timestamp
    stream->write_4bytes(int32_t(shared_frame->timestamp * 90));
    // ssrc
    stream->write_4bytes(int32_t(kVideoSSRC));

    stream->write_bytes(sample->bytes, sample->size);

    SrsSample rtp_packet;
    rtp_packet.bytes = stream->data();
    rtp_packet.size = stream->pos();

    SrsSample* rtp_samples = new SrsSample[rtp_packet_vec.size()];
    for (int i = 0; i < rtp_packet_vec.size(); ++i) {
        rtp_samples[i] = rtp_packet_vec[i];
    }

    shared_frame->set_rtp_fragments(rtp_samples, rtp_packet_vec.size());

    return err;
}

srs_error_t SrsRtpMuxer::packet_stap_a(const string &sps, const string& pps, SrsSharedPtrMessage* shared_frame, vector<SrsSample>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    uint8_t header = sps[0];
    uint8_t nal_type = header & kNalTypeMask;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, 1460);
    SrsAutoFree(SrsBuffer, stream);

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kMarker | kH264PayloadType);
    // sequenct
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
