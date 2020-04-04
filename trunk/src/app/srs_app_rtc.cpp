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
srs_error_t aac_raw_append_adts_header(SrsSharedPtrMessage* shared_audio, SrsFormat* format, SrsBuffer** stream_ptr)
{
    srs_error_t err = srs_success;

    if (format->is_aac_sequence_header()) {
        return err;
    }

    if (stream_ptr == NULL) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "adts");
    }

    srs_verbose("audio samples=%d", format->audio->nb_samples);

    if (format->audio->nb_samples != 1) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "adts");
    }

    int nb_buf = format->audio->samples[0].size + 7;
    char* buf = new char[nb_buf];
    SrsBuffer* stream = new SrsBuffer(buf, nb_buf);

    // TODO: Add comment.
    stream->write_1bytes(0xFF);
    stream->write_1bytes(0xF9);
    stream->write_1bytes(((format->acodec->aac_object - 1) << 6) | ((format->acodec->aac_sample_rate & 0x0F) << 2) | ((format->acodec->aac_channels & 0x04) >> 2));
    stream->write_1bytes(((format->acodec->aac_channels & 0x03) << 6) | ((nb_buf >> 11) & 0x03));
    stream->write_1bytes((nb_buf >> 3) & 0xFF);
    stream->write_1bytes(((nb_buf & 0x07) << 5) | 0x1F);
    stream->write_1bytes(0xFC);

    stream->write_bytes(format->audio->samples[0].bytes, format->audio->samples[0].size);

    *stream_ptr = stream;

    return err;
}

SrsRtpMuxer::SrsRtpMuxer()
{
    sequence = 0;
    discard_bframe = false;
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
        // only collect SPS/PPS.
        return err;
    }

    vector<SrsRtpSharedPacket*> rtp_packet_vec;

    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample sample = format->video->samples[i];

        uint8_t header = sample.bytes[0];
        uint8_t nal_type = header & kNalTypeMask;

        // TODO: Use config to determine should check avc stream.
        if (nal_type == SrsAvcNaluTypeNonIDR || nal_type == SrsAvcNaluTypeDataPartitionA || nal_type == SrsAvcNaluTypeIDR) {
            SrsBuffer* stream = new SrsBuffer(sample.bytes, sample.size);
            SrsAutoFree(SrsBuffer, stream);

            // Skip nalu header.
            stream->skip(1);

            SrsBitBuffer bitstream(stream);
            int32_t first_mb_in_slice = 0;
            if ((err = srs_avc_nalu_read_uev(&bitstream, first_mb_in_slice)) != srs_success) {
                return srs_error_wrap(err, "nalu read uev");
            }

            int32_t slice_type = 0;
            if ((err = srs_avc_nalu_read_uev(&bitstream, slice_type)) != srs_success) {
                return srs_error_wrap(err, "nalu read uev");
            }

            srs_verbose("nal_type=%d, slice type=%d", nal_type, slice_type);
            if (slice_type == SrsAvcSliceTypeB || slice_type == SrsAvcSliceTypeB1) {
                if (discard_bframe) {
                    continue;
                }
            }
        }

        if (sample.size <= max_payload_size) {
            if ((err = packet_single_nalu(shared_frame, format, &sample, rtp_packet_vec)) != srs_success) {
                return srs_error_wrap(err, "packet single nalu");
            }
        } else {
            if ((err = packet_fu_a(shared_frame, format, &sample, rtp_packet_vec)) != srs_success) {
                return srs_error_wrap(err, "packet fu-a");
            }
        }
    }

    if (! rtp_packet_vec.empty()) {
        // At the end of the frame, set marker bit.
        // One frame may have multi nals. Set the marker bit in the last nal end, no the end of the nal.
        if ((err = rtp_packet_vec.back()->set_marker(true)) != srs_success) {
            return srs_error_wrap(err, "set marker");
        }
    }

    shared_frame->set_rtp_packets(rtp_packet_vec);

    return err;
}

srs_error_t SrsRtpMuxer::packet_fu_a(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample, vector<SrsRtpSharedPacket*>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    char* p = sample->bytes + 1;
    int nb_left = sample->size - 1;
    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    if (nal_type == SrsAvcNaluTypeIDR) {
        if ((err = packet_stap_a(sps, pps, shared_frame, rtp_packet_vec)) != srs_success) {
            return srs_error_wrap(err, "packet stap-a");
        }
    }

    int num_of_packet = (sample->size - 1 + max_payload_size) / max_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        char* buf = new char[kRtpPacketSize];
        SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
        SrsAutoFree(SrsBuffer, stream);

        int packet_size = min(nb_left, max_payload_size);

        // v=2,p=0,x=0,cc=0
        stream->write_1bytes(0x80);
        // marker payloadtype
        stream->write_1bytes(kH264PayloadType);
        // sequence
        stream->write_2bytes(sequence);
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

        srs_verbose("rtp fu-a nalu, size=%u, seq=%u, timestamp=%lu, ssrc=%u, payloadtype=%u", 
            sample->size, sequence, (shared_frame->timestamp * 90), kVideoSSRC, kH264PayloadType);

        SrsRtpSharedPacket* rtp_shared_pkt = new SrsRtpSharedPacket();
        rtp_shared_pkt->create((shared_frame->timestamp * 90), sequence++, kVideoSSRC, kH264PayloadType, stream->data(), stream->pos());

        rtp_packet_vec.push_back(rtp_shared_pkt);
    }

    return err;
}

srs_error_t SrsRtpMuxer::packet_single_nalu(SrsSharedPtrMessage* shared_frame, SrsFormat* format, SrsSample* sample, vector<SrsRtpSharedPacket*>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
    SrsAutoFree(SrsBuffer, stream);

    if (nal_type == SrsAvcNaluTypeIDR) {
        if ((err = packet_stap_a(sps, pps, shared_frame, rtp_packet_vec)) != srs_success) {
            return srs_error_wrap(err, "packet stap-a");
        }
    }

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kH264PayloadType);
    // sequenct
    stream->write_2bytes(sequence);
    // timestamp
    stream->write_4bytes(int32_t(shared_frame->timestamp * 90));
    // ssrc
    stream->write_4bytes(int32_t(kVideoSSRC));

    stream->write_bytes(sample->bytes, sample->size);

    srs_verbose("rtp single nalu, size=%u, seq=%u, timestamp=%lu, ssrc=%u, payloadtype=%u", 
        sample->size, sequence, (shared_frame->timestamp * 90), kVideoSSRC, kH264PayloadType);

    SrsRtpSharedPacket* rtp_shared_pkt = new SrsRtpSharedPacket();
    rtp_shared_pkt->create((shared_frame->timestamp * 90), sequence++, kVideoSSRC, kH264PayloadType, stream->data(), stream->pos());

    rtp_packet_vec.push_back(rtp_shared_pkt);

    return err;
}

srs_error_t SrsRtpMuxer::packet_stap_a(const string &sps, const string& pps, SrsSharedPtrMessage* shared_frame, vector<SrsRtpSharedPacket*>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    if (sps.empty() || pps.empty()) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "sps/pps empty");
    }

    uint8_t header = sps[0];
    uint8_t nal_type = header & kNalTypeMask;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
    SrsAutoFree(SrsBuffer, stream);

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kH264PayloadType);
    // sequenct
    stream->write_2bytes(sequence);
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

    srs_verbose("rtp stap-a nalu, size=%u, seq=%u, timestamp=%lu, ssrc=%u, payloadtype=%u", 
        (sps.size() + pps.size()), sequence, (shared_frame->timestamp * 90), kVideoSSRC, kH264PayloadType);

    SrsRtpSharedPacket* rtp_shared_pkt = new SrsRtpSharedPacket();
    rtp_shared_pkt->create((shared_frame->timestamp * 90), sequence++, kVideoSSRC, kH264PayloadType, stream->data(), stream->pos());

    rtp_packet_vec.push_back(rtp_shared_pkt);

    return err;
}

SrsRtpOpusMuxer::SrsRtpOpusMuxer()
{
    sequence = 0;
    timestamp = 0;
    transcode = NULL;
}

SrsRtpOpusMuxer::~SrsRtpOpusMuxer()
{
    if (transcode) {
        delete transcode;
        transcode = NULL;
    }
}

srs_error_t SrsRtpOpusMuxer::initialize()
{
    srs_error_t err = srs_success;

    transcode = new SrsAudioRecode(kChannel, kSamplerate);
    if (!transcode) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "SrsAacOpus init failed");
    }
    transcode->initialize();

    return err;
}

srs_error_t SrsRtpOpusMuxer::frame_to_packet(SrsSharedPtrMessage* shared_audio, SrsFormat* format, SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    vector<SrsRtpSharedPacket*> rtp_packet_vec;

    char* data_ptr[kArrayLength];
    static char data_array[kArrayLength][kArrayBuffer];
    int elen[kArrayLength], number = 0;

    data_ptr[0] = &data_array[0][0];
    for (int i = 1; i < kArrayLength; i++) {
       data_ptr[i] = data_array[i];
    }

    SrsSample pkt;
    pkt.bytes = stream->data();
    pkt.size = stream->pos();

    if ((err = transcode->recode(&pkt, data_ptr, elen, number)) != srs_success) {
        return srs_error_wrap(err, "recode error");
    }

    for (int i = 0; i < number; i++) {
        SrsSample sample;
        sample.size = elen[i];
        sample.bytes = data_ptr[i];
        packet_opus(shared_audio, &sample, rtp_packet_vec);
    }

    shared_audio->set_rtp_packets(rtp_packet_vec);

    return err;
}

srs_error_t SrsRtpOpusMuxer::packet_opus(SrsSharedPtrMessage* shared_frame, SrsSample* sample, std::vector<SrsRtpSharedPacket*>& rtp_packet_vec)
{
    srs_error_t err = srs_success;

    char* buf = new char[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, kRtpPacketSize);
    SrsAutoFree(SrsBuffer, stream);

    // v=2,p=0,x=0,cc=0
    stream->write_1bytes(0x80);
    // marker payloadtype
    stream->write_1bytes(kOpusPayloadType);
    // sequenct
    stream->write_2bytes(sequence);
    // timestamp
    stream->write_4bytes(int32_t(timestamp));
    // TODO: FIXME: Why 960? Need Refactoring?
    timestamp += 960;
    // ssrc
    stream->write_4bytes(int32_t(kAudioSSRC));

    stream->write_bytes(sample->bytes, sample->size);

    srs_verbose("sample=%s", srs_string_dumps_hex(sample->bytes, sample->size).c_str());
    srs_verbose("opus, size=%u, seq=%u, timestamp=%lu, ssrc=%u, payloadtype=%u",
        sample->size, sequence, timestamp, kAudioSSRC, kOpusPayloadType);

    SrsRtpSharedPacket* rtp_shared_pkt = new SrsRtpSharedPacket();
    rtp_shared_pkt->create(timestamp, sequence++, kAudioSSRC, kOpusPayloadType, stream->data(), stream->pos());
    rtp_shared_pkt->set_marker(true);

    rtp_packet_vec.push_back(rtp_shared_pkt);

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

    rtp_h264_muxer = new SrsRtpMuxer();
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

    SrsBuffer* stream = NULL;
    SrsAutoFree(SrsBuffer, stream);
    if ((err = aac_raw_append_adts_header(shared_audio, format, &stream)) != srs_success) {
        return srs_error_wrap(err, "aac append header");
    }

    if (stream) {
        return rtp_opus_muxer->frame_to_packet(shared_audio, format, stream);
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
    return rtp_h264_muxer->frame_to_packet(shared_video, format);
}
