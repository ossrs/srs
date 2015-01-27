/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_kernel_ts.hpp>

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_avc.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>

// in ms, for HLS aac sync time.
#define SRS_CONF_DEFAULT_AAC_SYNC 100

// @see: ngx_rtmp_hls_audio
/* We assume here AAC frame size is 1024
 * Need to handle AAC frames with frame size of 960 */
#define _SRS_AAC_SAMPLE_SIZE 1024

// the mpegts header specifed the video/audio pid.
#define TS_VIDEO_PID 256
#define TS_AUDIO_PID 257

// ts aac stream id.
#define TS_AUDIO_AAC 0xc0
#define TS_AUDIO_MP3 0x04
// ts avc stream id.
#define TS_VIDEO_AVC 0xe0

/**
* the public data, event HLS disable, others can use it.
*/
// 0 = 5.5 kHz = 5512 Hz
// 1 = 11 kHz = 11025 Hz
// 2 = 22 kHz = 22050 Hz
// 3 = 44 kHz = 44100 Hz
int flv_sample_rates[] = {5512, 11025, 22050, 44100};

// the sample rates in the codec,
// in the sequence header.
int aac_sample_rates[] = 
{
    96000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025,  8000,
    7350,     0,     0,    0
};

// @see: NGX_RTMP_HLS_DELAY, 
// 63000: 700ms, ts_tbn=90000
#define SRS_AUTO_HLS_DELAY 63000

// @see: ngx_rtmp_mpegts_header
u_int8_t mpegts_header[] = {
    /* TS */
    0x47, 0x40, 0x00, 0x10, 0x00,
    /* PSI */
    0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00,
    /* PAT */
    0x00, 0x01, 0xf0, 0x01,
    /* CRC */
    0x2e, 0x70, 0x19, 0x05,
    /* stuffing 167 bytes */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    
    /* TS */
    0x47, 0x50, 0x01, 0x10, 0x00,
    /* PSI */
    0x02, 0xb0, 0x17, 0x00, 0x01, 0xc1, 0x00, 0x00,
    /* PMT */
    0xe1, 0x00,
    0xf0, 0x00,
    // must generate header with/without video, @see:
    // https://github.com/winlinvip/simple-rtmp-server/issues/40
    0x1b, 0xe1, 0x00, 0xf0, 0x00, /* h264, pid=0x100=256 */
};
u_int8_t mpegts_header_aac[] = {
    0x0f, 0xe1, 0x01, 0xf0, 0x00, /* aac, pid=0x101=257 */
    /* CRC */
    0x2f, 0x44, 0xb9, 0x9b, /* crc for aac */
};
u_int8_t mpegts_header_mp3[] = {
    0x03, 0xe1, 0x01, 0xf0, 0x00, /* mp3 */
    /* CRC */
    0x4e, 0x59, 0x3d, 0x1e, /* crc for mp3 */
};
u_int8_t mpegts_header_padding[] = {
    /* stuffing 157 bytes */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

// @see: ngx_rtmp_mpegts.c
// TODO: support full mpegts feature in future.
class SrsMpegtsWriter
{
public:
    static int write_header(SrsFileWriter* writer, SrsCodecAudio acodec)
    {
        int ret = ERROR_SUCCESS;
        
        if ((ret = writer->write(mpegts_header, sizeof(mpegts_header), NULL)) != ERROR_SUCCESS) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("write ts file header failed. ret=%d", ret);
            return ret;
        }

        if (acodec == SrsCodecAudioAAC) {
            if ((ret = writer->write(mpegts_header_aac, sizeof(mpegts_header_aac), NULL)) != ERROR_SUCCESS) {
                ret = ERROR_HLS_WRITE_FAILED;
                srs_error("write ts file aac header failed. ret=%d", ret);
                return ret;
            }
        } else {
            if ((ret = writer->write(mpegts_header_mp3, sizeof(mpegts_header_mp3), NULL)) != ERROR_SUCCESS) {
                ret = ERROR_HLS_WRITE_FAILED;
                srs_error("write ts file mp3 header failed. ret=%d", ret);
                return ret;
            }
        }
        
        if ((ret = writer->write(mpegts_header_padding, sizeof(mpegts_header_padding), NULL)) != ERROR_SUCCESS) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("write ts file padding header failed. ret=%d", ret);
            return ret;
        }

        return ret;
    }
    static int write_frame(SrsFileWriter* writer, SrsMpegtsFrame* frame, SrsSimpleBuffer* buffer)
    {
        int ret = ERROR_SUCCESS;
        
        if (!buffer->bytes() || buffer->length() <= 0) {
            return ret;
        }
        
        char* last = buffer->bytes() + buffer->length();
        char* pos = buffer->bytes();
        
        bool first = true;
        while (pos < last) {
            static char packet[188];
            char* p = packet;
            
            frame->cc++;
            
            // sync_byte; //8bits
            *p++ = 0x47;
            // pid; //13bits
            *p++ = (frame->pid >> 8) & 0x1f;
            // payload_unit_start_indicator; //1bit
            if (first) {
                p[-1] |= 0x40;
            }
            *p++ = frame->pid;
            
            // transport_scrambling_control; //2bits
            // adaption_field_control; //2bits, 0x01: PayloadOnly
            // continuity_counter; //4bits
            *p++ = 0x10 | (frame->cc & 0x0f);
            
            if (first) {
                first = false;
                if (frame->key) {
                    p[-1] |= 0x20; // Both Adaption and Payload
                    *p++ = 7;    // size
                    *p++ = 0x50; // random access + PCR
                    p = write_pcr(p, frame->dts);
                }
                
                // PES header
                // packet_start_code_prefix; //24bits, '00 00 01'
                *p++ = 0x00;
                *p++ = 0x00;
                *p++ = 0x01;
                //8bits
                *p++ = frame->sid;
                
                // pts(33bits) need 5bytes.
                u_int8_t header_size = 5;
                u_int8_t flags = 0x80; // pts
                
                // dts(33bits) need 5bytes also
                if (frame->dts != frame->pts) {
                    header_size += 5;
                    flags |= 0x40; // dts
                }
                
                // 3bytes: flag fields from PES_packet_length to PES_header_data_length
                int pes_size = (last - pos) + header_size + 3;
                if (pes_size > 0xffff) {
                    /**
                    * when actual packet length > 0xffff(65535),
                    * which exceed the max u_int16_t packet length,
                    * use 0 packet length, the next unit start indicates the end of packet.
                    */
                    pes_size = 0;
                }
                
                // PES_packet_length; //16bits
                *p++ = (pes_size >> 8);
                *p++ = pes_size;
                
                // PES_scrambling_control; //2bits, '10'
                // PES_priority; //1bit
                // data_alignment_indicator; //1bit
                // copyright; //1bit
                // original_or_copy; //1bit    
                *p++ = 0x80; /* H222 */
                
                // PTS_DTS_flags; //2bits
                // ESCR_flag; //1bit
                // ES_rate_flag; //1bit
                // DSM_trick_mode_flag; //1bit
                // additional_copy_info_flag; //1bit
                // PES_CRC_flag; //1bit
                // PES_extension_flag; //1bit
                *p++ = flags;
                
                // PES_header_data_length; //8bits
                *p++ = header_size;

                // pts; // 33bits
                p = write_dts_pts(p, flags >> 6, frame->pts + SRS_AUTO_HLS_DELAY);
                
                // dts; // 33bits
                if (frame->dts != frame->pts) {
                    p = write_dts_pts(p, 1, frame->dts + SRS_AUTO_HLS_DELAY);
                }
            }
            
            int body_size = sizeof(packet) - (p - packet);
            int in_size = last - pos;
            
            if (body_size <= in_size) {
                memcpy(p, pos, body_size);
                pos += body_size;
            } else {
                p = fill_stuff(p, packet, body_size, in_size);
                memcpy(p, pos, in_size);
                pos = last;
            }
            
            // write ts packet
            if ((ret = writer->write(packet, sizeof(packet), NULL)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("write ts file failed. ret=%d", ret);
                }
                return ret;
            }
        }
        
        return ret;
    }
private:
    static char* fill_stuff(char* pes_body_end, char* packet, int body_size, int in_size)
    {
        char* p = pes_body_end;
        
        // insert the stuff bytes before PES body
        int stuff_size = (body_size - in_size);
        
        // adaption_field_control; //2bits
        if (packet[3] & 0x20) {
            //  has adaptation
            // packet[4]: adaption_field_length
            // packet[5]: adaption field data
            // base: start of PES body
            char* base = &packet[5] + packet[4];
            int len = p - base;
            p = (char*)memmove(base + stuff_size, base, len) + len;
            // increase the adaption field size.
            packet[4] += stuff_size;
            
            return p;
        }

        // create adaption field.
        // adaption_field_control; //2bits
        packet[3] |= 0x20;
        // base: start of PES body
        char* base = &packet[4];
        int len = p - base;
        p = (char*)memmove(base + stuff_size, base, len) + len;
        // adaption_field_length; //8bits
        packet[4] = (stuff_size - 1);
        if (stuff_size >= 2) {
            // adaption field flags.
            packet[5] = 0;
            // adaption data.
            if (stuff_size > 2) {
                memset(&packet[6], 0xff, stuff_size - 2);
            }
        }
        
        return p;
    }
    static char* write_pcr(char* p, int64_t pcr)
    {
        // the pcr=dts-delay, where dts = frame->dts + delay
        // and the pcr should never be negative
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/268
        srs_assert(pcr >= 0);
        
        int64_t v = pcr;
        
        *p++ = (char) (v >> 25);
        *p++ = (char) (v >> 17);
        *p++ = (char) (v >> 9);
        *p++ = (char) (v >> 1);
        *p++ = (char) (v << 7 | 0x7e);
        *p++ = 0;
    
        return p;
    }
    static char* write_dts_pts(char* p, u_int8_t fb, int64_t pts)
    {
        int32_t val;
    
        val = fb << 4 | (((pts >> 30) & 0x07) << 1) | 1;
        *p++ = val;
    
        val = (((pts >> 15) & 0x7fff) << 1) | 1;
        *p++ = (val >> 8);
        *p++ = val;
    
        val = (((pts) & 0x7fff) << 1) | 1;
        *p++ = (val >> 8);
        *p++ = val;
    
        return p;
    }
};

SrsMpegtsFrame::SrsMpegtsFrame()
{
    pts = dts = 0;
    pid = sid = cc = 0;
    key = false;
}

SrsTsContext::SrsTsContext()
{
}

SrsTsContext::~SrsTsContext()
{
}

int SrsTsContext::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // parse util EOF of stream.
    // for example, parse multiple times for the PES_packet_length(0) packet.
    while (!stream->empty()) {
        SrsTsPacket* packet = new SrsTsPacket(this);
        SrsAutoFree(SrsTsPacket, packet);

        if ((ret = packet->decode(stream)) != ERROR_SUCCESS) {
            srs_error("mpegts: decode ts packet failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

SrsTsPidApply SrsTsContext::get(int pid)
{
    if (pids.find(pid) == pids.end()) {
        return SrsTsPidApplyReserved;
    }
    return pids[pid];
}

void SrsTsContext::set(int pid, SrsTsPidApply apply_pid)
{
    pids[pid] = apply_pid;
}

SrsTsPacket::SrsTsPacket(SrsTsContext* c)
{
    context = c;

    sync_byte = 0;
    transport_error_indicator = 0;
    payload_unit_start_indicator = 0;
    transport_priority = 0;
    pid = SrsTsPidPAT;
    transport_scrambling_control = SrsTsScrambledDisabled;
    adaption_field_control = SrsTsAdaptationFieldTypeReserved;
    continuity_counter = 0;
    adaptation_field = NULL;
    payload = NULL;
}

SrsTsPacket::~SrsTsPacket()
{
    srs_freep(adaptation_field);
    srs_freep(payload);
}

int SrsTsPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    int pos = stream->pos();

    // 4B ts packet header.
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_HEADER;
        srs_error("ts: demux header failed. ret=%d", ret);
        return ret;
    }

    sync_byte = stream->read_1bytes();
    if (sync_byte != 0x47) {
        ret = ERROR_STREAM_CASTER_TS_SYNC_BYTE;
        srs_error("ts: sync_bytes must be 0x47, actual=%#x. ret=%d", sync_byte, ret);
        return ret;
    }
    
    int16_t pidv = stream->read_2bytes();
    transport_error_indicator = (pidv >> 15) & 0x01;
    payload_unit_start_indicator = (pidv >> 14) & 0x01;
    transport_priority = (pidv >> 13) & 0x01;
    pid = (SrsTsPid)(pidv & 0x1FFF);

    int8_t ccv = stream->read_1bytes();
    transport_scrambling_control = (SrsTsScrambled)((ccv >> 6) & 0x03);
    adaption_field_control = (SrsTsAdaptationFieldType)((ccv >> 4) & 0x03);
    continuity_counter = (SrsTsPid)(ccv & 0x0F);

    // TODO: FIXME: create pids map when got new pid.
    
    srs_info("ts: header sync=%#x error=%d unit_start=%d priotiry=%d pid=%d scrambling=%d adaption=%d counter=%d",
        sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
        transport_scrambling_control, adaption_field_control, continuity_counter);

    // optional: adaptation field
    if (adaption_field_control == SrsTsAdaptationFieldTypeAdaptionOnly || adaption_field_control == SrsTsAdaptationFieldTypeBoth) {
        srs_freep(adaptation_field);
        adaptation_field = new SrsTsAdaptationField(this);

        if ((ret = adaptation_field->decode(stream)) != ERROR_SUCCESS) {
            srs_error("ts: demux af faield. ret=%d", ret);
            return ret;
        }
        srs_verbose("ts: demux af ok.");
    }

    // calc the user defined data size for payload.
    int nb_payload = SRS_TS_PACKET_SIZE - (stream->pos() - pos);

    // optional: payload.
    if (adaption_field_control == SrsTsAdaptationFieldTypePayloadOnly || adaption_field_control == SrsTsAdaptationFieldTypeBoth) {
        if (pid == SrsTsPidPAT) {
            // 2.4.4.3 Program association Table
            srs_freep(payload);
            payload = new SrsTsPayloadPAT(this);
        } else {
            SrsTsPidApply apply_pid = context->get(pid);
            if (apply_pid == SrsTsPidApplyPMT) {
                // 2.4.4.8 Program Map Table
                srs_freep(payload);
                payload = new SrsTsPayloadPMT(this);
            } else {
                // left bytes as reserved.
                stream->skip(nb_payload);
            }
        }

        if (payload && (ret = payload->decode(stream)) != ERROR_SUCCESS) {
            srs_error("ts: demux payload failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

SrsTsAdaptationField::SrsTsAdaptationField(SrsTsPacket* pkt)
{
    packet = pkt;

    adaption_field_length = 0;
    discontinuity_indicator = 0;
    random_access_indicator = 0;
    elementary_stream_priority_indicator = 0;
    PCR_flag = 0;
    OPCR_flag = 0;
    splicing_point_flag = 0;
    transport_private_data_flag = 0;
    adaptation_field_extension_flag = 0;
    program_clock_reference_base = 0;
    program_clock_reference_extension = 0;
    original_program_clock_reference_base = 0;
    original_program_clock_reference_extension = 0;
    splice_countdown = 0;
    transport_private_data_length = 0;
    transport_private_data = NULL;
    adaptation_field_extension_length = 0;
    ltw_flag = 0;
    piecewise_rate_flag = 0;
    seamless_splice_flag = 0;
    ltw_valid_flag = 0;
    ltw_offset = 0;
    piecewise_rate = 0;
    splice_type = 0;
    DTS_next_AU0 = 0;
    marker_bit0 = 0;
    DTS_next_AU1 = 0;
    marker_bit1 = 0;
    DTS_next_AU2 = 0;
    marker_bit2 = 0;
    nb_af_ext_reserved = 0;
    nb_af_reserved = 0;
}

SrsTsAdaptationField::~SrsTsAdaptationField()
{
    srs_freep(transport_private_data);
}

int SrsTsAdaptationField::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if (!stream->require(2)) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: demux af failed. ret=%d", ret);
        return ret;
    }
    adaption_field_length = stream->read_1bytes();

    // When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
    // be in the range 0 to 182. 
    if (packet->adaption_field_control == SrsTsAdaptationFieldTypeBoth && adaption_field_length > 182) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: demux af length failed, must in [0, 182], actual=%d. ret=%d", adaption_field_length, ret);
        return ret;
    }
    // When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
    // be 183.
    if (packet->adaption_field_control == SrsTsAdaptationFieldTypeAdaptionOnly && adaption_field_length != 183) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: demux af length failed, must be 183, actual=%d. ret=%d", adaption_field_length, ret);
        return ret;
    }
    
    // no adaptation field.
    if (adaption_field_length == 0) {
        srs_info("ts: demux af empty.");
        return ret;
    }

    // the adaptation field start at here.
    int pos_af = stream->pos();
    int8_t tmpv = stream->read_1bytes();
    
    discontinuity_indicator              =   (tmpv >> 7) & 0x01;
    random_access_indicator              =   (tmpv >> 6) & 0x01;
    elementary_stream_priority_indicator =   (tmpv >> 5) & 0x01;
    PCR_flag                             =   (tmpv >> 4) & 0x01;
    OPCR_flag                            =   (tmpv >> 3) & 0x01;
    splicing_point_flag                  =   (tmpv >> 2) & 0x01;
    transport_private_data_flag          =   (tmpv >> 1) & 0x01;
    adaptation_field_extension_flag      =   (tmpv >> 0) & 0x01;
    
    if (PCR_flag) {
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af PCR_flag failed. ret=%d", ret);
            return ret;
        }

        char* pp = NULL;
        char* p = stream->data() + stream->pos();
        stream->skip(6);

        pp = (char*)&program_clock_reference_base;
        pp[5] = *p++;
        pp[4] = *p++;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        
        // @remark, use pcr base and ignore the extension
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/250#issuecomment-71349370
        program_clock_reference_extension = program_clock_reference_base & 0x1ff;
        program_clock_reference_base = (program_clock_reference_base >> 15) & 0x1ffffffffLL;
    }

    if (OPCR_flag) {
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af OPCR_flag failed. ret=%d", ret);
            return ret;
        }

        char* pp = NULL;
        char* p = stream->data() + stream->pos();
        stream->skip(6);

        pp = (char*)&original_program_clock_reference_base;
        pp[5] = *p++;
        pp[4] = *p++;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        
        // @remark, use pcr base and ignore the extension
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/250#issuecomment-71349370
        original_program_clock_reference_extension = program_clock_reference_base & 0x1ff;
        original_program_clock_reference_base = (program_clock_reference_base >> 15) & 0x1ffffffffLL;
    }

    if (splicing_point_flag) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af splicing_point_flag failed. ret=%d", ret);
            return ret;
        }
        splice_countdown = stream->read_1bytes();
    }
    
    if (transport_private_data_flag) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af transport_private_data_flag failed. ret=%d", ret);
            return ret;
        }
        transport_private_data_length = (u_int8_t)stream->read_1bytes();

        if (transport_private_data_length> 0) {
            if (!stream->require(transport_private_data_length)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af transport_private_data_flag failed. ret=%d", ret);
                return ret;
            }
            srs_freep(transport_private_data);
            transport_private_data = new char[transport_private_data_length];
            stream->read_bytes(transport_private_data, transport_private_data_length);
        }
    }
    
    if (adaptation_field_extension_flag) {
        int pos_af_ext = stream->pos();

        if (!stream->require(2)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af adaptation_field_extension_flag failed. ret=%d", ret);
            return ret;
        }
        adaptation_field_extension_length = (u_int8_t)stream->read_1bytes();
        ltw_flag = stream->read_1bytes();
        
        piecewise_rate_flag = (ltw_flag >> 6) & 0x01;
        seamless_splice_flag = (ltw_flag >> 5) & 0x01;
        ltw_flag = (ltw_flag >> 7) & 0x01;

        if (ltw_flag) {
            if (!stream->require(2)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af ltw_flag failed. ret=%d", ret);
                return ret;
            }
            ltw_offset = stream->read_2bytes();
            
            ltw_valid_flag = (ltw_offset >> 15) &0x01;
            ltw_offset &= 0x7FFF;
        }

        if (piecewise_rate_flag) {
            if (!stream->require(3)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af piecewise_rate_flag failed. ret=%d", ret);
                return ret;
            }
            piecewise_rate = stream->read_3bytes();

            piecewise_rate &= 0x3FFFFF;
        }

        if (seamless_splice_flag) {
            if (!stream->require(5)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af seamless_splice_flag failed. ret=%d", ret);
                return ret;
            }
            marker_bit0 = stream->read_1bytes();
            DTS_next_AU1 = stream->read_2bytes();
            DTS_next_AU2 = stream->read_2bytes();
            
            splice_type = (marker_bit0 >> 4) & 0x0F;
            DTS_next_AU0 = (marker_bit0 >> 1) & 0x07;
            marker_bit0 &= 0x01;
            
            marker_bit1 = DTS_next_AU1 & 0x01;
            DTS_next_AU1 = (DTS_next_AU1 >> 1) & 0x7FFF;
            
            marker_bit2 = DTS_next_AU2 & 0x01;
            DTS_next_AU2 = (DTS_next_AU2 >> 1) & 0x7FFF;
        }

        nb_af_ext_reserved = adaptation_field_extension_length - (stream->pos() - pos_af_ext);
        stream->skip(nb_af_ext_reserved);
    }

    nb_af_reserved = adaption_field_length - (stream->pos() - pos_af);
    stream->skip(nb_af_reserved);
    
    srs_info("ts: af parsed, discontinuity=%d random=%d priority=%d PCR=%d OPCR=%d slicing=%d private=%d extension=%d/%d pcr=%"PRId64"/%d opcr=%"PRId64"/%d",
        discontinuity_indicator, random_access_indicator, elementary_stream_priority_indicator, PCR_flag, OPCR_flag, splicing_point_flag,
        transport_private_data_flag, adaptation_field_extension_flag, adaptation_field_extension_length, program_clock_reference_base, 
        program_clock_reference_extension, original_program_clock_reference_base, original_program_clock_reference_extension);

    return ret;
}

SrsTsPayload::SrsTsPayload(SrsTsPacket* p)
{
    packet = p;
}

SrsTsPayload::~SrsTsPayload()
{
}

SrsTsPayloadPSI::SrsTsPayloadPSI(SrsTsPacket* p) : SrsTsPayload(p)
{
    pointer_field = 0;
    CRC_32 = 0;
}

SrsTsPayloadPSI::~SrsTsPayloadPSI()
{
}

int SrsTsPayloadPSI::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    /**
    * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
    * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
    * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
    * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
    * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
    * stream_type 5 (refer to Table 2-29).
    */
    if (packet->payload_unit_start_indicator) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_PSI;
            srs_error("ts: demux PSI failed. ret=%d", ret);
            return ret;
        }
        pointer_field = stream->read_1bytes();
    }

    // to calc the crc32
    char* ppat = stream->data() + stream->pos();
    int pat_pos = stream->pos();

    // atleast 3B for all psi.
    if (!stream->require(3)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: demux PSI failed. ret=%d", ret);
        return ret;
    }
    // 1B
    table_id = (SrsTsPsiId)stream->read_1bytes();
    
    // 2B
    section_length = stream->read_2bytes();
    
    section_syntax_indicator = (section_length >> 15) & 0x01;
    const0_value = (section_length >> 14) & 0x01;
    section_length &= 0x0FFF;

    // no section, ignore.
    if (section_length == 0) {
        srs_warn("ts: demux PAT ignore empty section");
        return ret;
    }

    if (!stream->require(section_length)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: demux PAT section failed. ret=%d", ret);
        return ret;
    }

    // call the virtual method of actual PSI.
    if ((ret = psi_decode(stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // 4B
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: demux PSI crc32 failed. ret=%d", ret);
        return ret;
    }
    CRC_32 = stream->read_4bytes();

    // verify crc32.
    int32_t crc32 = srs_crc32(ppat, stream->pos() - pat_pos - 4);
    if (crc32 != CRC_32) {
        ret = ERROR_STREAM_CASTER_TS_CRC32;
        srs_error("ts: verify PSI crc32 failed. ret=%d", ret);
        return ret;
    }

    // consume left stuffings
    if (!stream->empty()) {
        stream->skip(stream->size() - stream->pos());
    }

    return ret;
}

SrsTsPayloadPATProgram::SrsTsPayloadPATProgram()
{
    number = 0;
    pid = 0;
}

SrsTsPayloadPATProgram::~SrsTsPayloadPATProgram()
{
}

SrsTsPayloadPAT::SrsTsPayloadPAT(SrsTsPacket* p) : SrsTsPayloadPSI(p)
{
}

SrsTsPayloadPAT::~SrsTsPayloadPAT()
{
    std::vector<SrsTsPayloadPATProgram*>::iterator it;
    for (it = programs.begin(); it != programs.end(); ++it) {
        SrsTsPayloadPATProgram* program = *it;
        srs_freep(program);
    }
    programs.clear();
}

int SrsTsPayloadPAT::psi_decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 5B for PAT specified
    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PAT;
        srs_error("ts: demux PAT failed. ret=%d", ret);
        return ret;
    }

    int pos = stream->pos();

    // 2B
    transport_stream_id = stream->read_2bytes();
    
    // 1B
    current_next_indicator = stream->read_1bytes();
    
    version_number = (current_next_indicator >> 1) & 0x1F;
    current_next_indicator &= 0x01;

    // TODO: FIXME: check the indicator.
    
    // 1B
    section_number = stream->read_1bytes();
    // 1B
    last_section_number = stream->read_1bytes();

    // multiple 4B program data.
    int program_bytes = section_length - 4 - (stream->pos() - pos);
    for (int i = 0; i < program_bytes; i += 4) {
        SrsTsPayloadPATProgram* program = new SrsTsPayloadPATProgram();

        int tmpv = stream->read_4bytes();
        program->number = (int16_t)((tmpv >> 16) & 0xFFFF);
        program->pid = (int16_t)(tmpv & 0x1FFF);

        // update the apply pid table.
        packet->context->set(program->pid, SrsTsPidApplyPMT);

        programs.push_back(program);
    }

    // update the apply pid table.
    packet->context->set(packet->pid, SrsTsPidApplyPAT);

    return ret;
}

SrsTsPayloadPMTESInfo::SrsTsPayloadPMTESInfo()
{
    ES_info_length = 0;
    ES_info = NULL;
}

SrsTsPayloadPMTESInfo::~SrsTsPayloadPMTESInfo()
{
    srs_freep(ES_info);
}

SrsTsPayloadPMT::SrsTsPayloadPMT(SrsTsPacket* p) : SrsTsPayloadPSI(p)
{
    program_info_length = 0;
    program_info_desc = NULL;
}

SrsTsPayloadPMT::~SrsTsPayloadPMT()
{
    srs_freep(program_info_desc);

    std::vector<SrsTsPayloadPMTESInfo*>::iterator it;
    for (it = infos.begin(); it != infos.end(); ++it) {
        SrsTsPayloadPMTESInfo* info = *it;
        srs_freep(info);
    }
    infos.clear();
}

int SrsTsPayloadPMT::psi_decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 9B for PMT specified
    if (!stream->require(9)) {
        ret = ERROR_STREAM_CASTER_TS_PMT;
        srs_error("ts: demux PMT failed. ret=%d", ret);
        return ret;
    }

    // 2B
    program_number = stream->read_2bytes();
    
    // 1B
    current_next_indicator = stream->read_1bytes();
    
    version_number = (current_next_indicator >> 1) & 0x1F;
    current_next_indicator &= 0x01;
    
    // 1B
    section_number = stream->read_1bytes();
    
    // 1B
    last_section_number = stream->read_1bytes();

    // 2B
    PCR_PID = stream->read_2bytes();
    
    PCR_PID &= 0x1FFF;
    
    // 2B
    program_info_length = stream->read_2bytes();

    program_info_length &= 0xFFF;
    
    if (program_info_length > 0) {
        if (!stream->require(program_info_length)) {
            ret = ERROR_STREAM_CASTER_TS_PMT;
            srs_error("ts: demux PMT program info failed. ret=%d", ret);
            return ret;
        }

        srs_freep(program_info_desc);
        program_info_desc = new char[program_info_length];
        stream->read_bytes(program_info_desc, program_info_length);
    }

    // [section_length] - 4(CRC) - 9B - [program_info_length]
    int ES_EOF_pos = stream->pos() + section_length - 4 - 9 - program_info_length;
    while (stream->pos() < ES_EOF_pos) {
        SrsTsPayloadPMTESInfo* info = new SrsTsPayloadPMTESInfo();
        infos.push_back(info);

        // 5B
        if (!stream->require(5)) {
            ret = ERROR_STREAM_CASTER_TS_PMT;
            srs_error("ts: demux PMT es info failed. ret=%d", ret);
            return ret;
        }

        info->stream_type = stream->read_1bytes();
        info->elementary_PID = stream->read_2bytes();
        info->ES_info_length = stream->read_2bytes();

        info->elementary_PID &= 0x1FFF;
        info->ES_info_length &= 0x0FFF;

        if (info->ES_info_length > 0) {
            if (!stream->require(info->ES_info_length)) {
                ret = ERROR_STREAM_CASTER_TS_PMT;
                srs_error("ts: demux PMT es info data failed. ret=%d", ret);
                return ret;
            }
            srs_freep(info->ES_info);
            info->ES_info = new char[info->ES_info_length];
            stream->read_bytes(info->ES_info, info->ES_info_length);
        }
    }

    // update the apply pid table.
    packet->context->set(packet->pid, SrsTsPidApplyPMT);

    return ret;
}

SrsTSMuxer::SrsTSMuxer(SrsFileWriter* w)
{
    writer = w;

    // reserved is not written.
    previous = SrsCodecAudioReserved1;
    // current default to aac.
    current = SrsCodecAudioAAC;
}

SrsTSMuxer::~SrsTSMuxer()
{
    close();
}

int SrsTSMuxer::open(string _path)
{
    int ret = ERROR_SUCCESS;
    
    path = _path;
    
    close();
    
    if ((ret = writer->open(path)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTSMuxer::update_acodec(SrsCodecAudio ac)
{
    int ret = ERROR_SUCCESS;

    if (current == ac) {
        return ret;
    }
    current = ac;

    return ret;
}

int SrsTSMuxer::write_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab)
{
    int ret = ERROR_SUCCESS;
    
    // when acodec changed, write header.
    if (current != previous) {
        previous = current;
        if ((ret = SrsMpegtsWriter::write_header(writer, previous)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    if ((ret = SrsMpegtsWriter::write_frame(writer, af, ab)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTSMuxer::write_video(SrsMpegtsFrame* vf, SrsSimpleBuffer* vb)
{
    int ret = ERROR_SUCCESS;
    
    // when acodec changed, write header.
    if (current != previous) {
        previous = current;
        if ((ret = SrsMpegtsWriter::write_header(writer, previous)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    if ((ret = SrsMpegtsWriter::write_frame(writer, vf, vb)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsTSMuxer::close()
{
    writer->close();
}

SrsTsAacJitter::SrsTsAacJitter()
{
    base_pts = 0;
    nb_samples = 0;

    // TODO: config it, 0 means no adjust
    sync_ms = SRS_CONF_DEFAULT_AAC_SYNC;
}

SrsTsAacJitter::~SrsTsAacJitter()
{
}

int64_t SrsTsAacJitter::on_buffer_start(int64_t flv_pts, int sample_rate, int aac_sample_rate)
{
    // use sample rate in flv/RTMP.
    int flv_sample_rate = flv_sample_rates[sample_rate & 0x03];

    // override the sample rate by sequence header
    if (aac_sample_rate != __SRS_AAC_SAMPLE_RATE_UNSET) {
        flv_sample_rate = aac_sample_rates[aac_sample_rate];
    }

    // sync time set to 0, donot adjust the aac timestamp.
    if (!sync_ms) {
        return flv_pts;
    }
    
    // @see: ngx_rtmp_hls_audio
    // drop the rtmp audio packet timestamp, re-calc it by sample rate.
    // 
    // resample for the tbn of ts is 90000, flv is 1000,
    // we will lost timestamp if use audio packet timestamp,
    // so we must resample. or audio will corupt in IOS.
    int64_t est_pts = base_pts + nb_samples * 90000LL * _SRS_AAC_SAMPLE_SIZE / flv_sample_rate;
    int64_t dpts = (int64_t) (est_pts - flv_pts);

    if (dpts <= (int64_t) sync_ms * 90 && dpts >= (int64_t) sync_ms * -90) {
        srs_info("HLS correct aac pts "
            "from %"PRId64" to %"PRId64", base=%"PRId64", nb_samples=%d, sample_rate=%d",
            flv_pts, est_pts, nb_samples, flv_sample_rate, base_pts);

        nb_samples++;
        
        return est_pts;
    }
    
    // resync
    srs_trace("HLS aac resync, dpts=%"PRId64", pts=%"PRId64
        ", base=%"PRId64", nb_samples=%"PRId64", sample_rate=%d",
        dpts, flv_pts, base_pts, nb_samples, flv_sample_rate);
    
    base_pts = flv_pts;
    nb_samples = 1;
    
    return flv_pts;
}

void SrsTsAacJitter::on_buffer_continue()
{
    nb_samples++;
}

SrsTsCache::SrsTsCache()
{
    aac_jitter = new SrsTsAacJitter();
    
    ab = new SrsSimpleBuffer();
    vb = new SrsSimpleBuffer();
    
    af = new SrsMpegtsFrame();
    vf = new SrsMpegtsFrame();

    audio_buffer_start_pts = 0;
}

SrsTsCache::~SrsTsCache()
{
    srs_freep(aac_jitter);
    
    ab->erase(ab->length());
    vb->erase(vb->length());
    
    srs_freep(ab);
    srs_freep(vb);
    
    srs_freep(af);
    srs_freep(vf);
}
    
int SrsTsCache::cache_audio(SrsAvcAacCodec* codec, int64_t pts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;

    // @remark, always use the orignal pts.
    if (ab->length() == 0) {
         audio_buffer_start_pts = pts;
    }
    
    // must be aac or mp3
    SrsCodecAudio acodec = (SrsCodecAudio)codec->audio_codec_id;
    srs_assert(acodec == SrsCodecAudioAAC || acodec == SrsCodecAudioMP3);
    
    // cache the aac audio.
    if (codec->audio_codec_id == SrsCodecAudioAAC) {
        // for aac audio, recalc the timestamp by aac jitter.
        if (ab->length() == 0) {
            pts = aac_jitter->on_buffer_start(pts, sample->sound_rate, codec->aac_sample_rate);
        
            af->dts = af->pts = pts;
            af->pid = TS_AUDIO_PID;
            af->sid = TS_AUDIO_AAC;
        } else {
            aac_jitter->on_buffer_continue();
        }
    
        // write aac audio to cache.
        if ((ret = do_cache_audio(codec, sample)) != ERROR_SUCCESS) {
            return ret;
        }

        return ret;
    }
    
    // cache the mp3 audio.
    if (codec->audio_codec_id == SrsCodecAudioMP3) {
        // for mp3 audio, recalc the timestamp by mp3 jitter.
        // TODO: FIXME: implements it.
        af->dts = af->pts = pts;
        af->pid = TS_AUDIO_PID;
        af->sid = SrsCodecAudioMP3;
        
        // for mp3, directly write to cache.
        // TODO: FIXME: implements it.
        for (int i = 0; i < sample->nb_sample_units; i++) {
            SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
            ab->append(sample_unit->bytes, sample_unit->size);
        }
    }
    
    return ret;
}
    
int SrsTsCache::cache_video(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // write video to cache.
    if ((ret = do_cache_video(codec, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    vf->dts = dts;
    vf->pts = vf->dts + sample->cts * 90;
    vf->pid = TS_VIDEO_PID;
    vf->sid = TS_VIDEO_AVC;
    vf->key = sample->frame_type == SrsCodecVideoAVCFrameKeyFrame;
    
    return ret;
}

int SrsTsCache::do_cache_audio(SrsAvcAacCodec* codec, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < sample->nb_sample_units; i++) {
        SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
        int32_t size = sample_unit->size;
        
        if (!sample_unit->bytes || size <= 0 || size > 0x1fff) {
            ret = ERROR_HLS_AAC_FRAME_LENGTH;
            srs_error("invalid aac frame length=%d, ret=%d", size, ret);
            return ret;
        }
        
        // the frame length is the AAC raw data plus the adts header size.
        int32_t frame_length = size + 7;
        
        // AAC-ADTS
        // 6.2 Audio Data Transport Stream, ADTS
        // in aac-iso-13818-7.pdf, page 26.
        // fixed 7bytes header
        static u_int8_t adts_header[7] = {0xff, 0xf1, 0x00, 0x00, 0x00, 0x0f, 0xfc};
        /*
        // adts_fixed_header
        // 2B, 16bits
        int16_t syncword; //12bits, '1111 1111 1111'
        int8_t ID; //1bit, '0'
        int8_t layer; //2bits, '00'
        int8_t protection_absent; //1bit, can be '1'
        // 12bits
        int8_t profile; //2bit, 7.1 Profiles, page 40
        TSAacSampleFrequency sampling_frequency_index; //4bits, Table 35, page 46
        int8_t private_bit; //1bit, can be '0'
        int8_t channel_configuration; //3bits, Table 8
        int8_t original_or_copy; //1bit, can be '0'
        int8_t home; //1bit, can be '0'
        
        // adts_variable_header
        // 28bits
        int8_t copyright_identification_bit; //1bit, can be '0'
        int8_t copyright_identification_start; //1bit, can be '0'
        int16_t frame_length; //13bits
        int16_t adts_buffer_fullness; //11bits, 7FF signals that the bitstream is a variable rate bitstream.
        int8_t number_of_raw_data_blocks_in_frame; //2bits, 0 indicating 1 raw_data_block()
        */
        // profile, 2bits
        adts_header[2] = (codec->aac_profile << 6) & 0xc0;
        // sampling_frequency_index 4bits
        adts_header[2] |= (codec->aac_sample_rate << 2) & 0x3c;
        // channel_configuration 3bits
        adts_header[2] |= (codec->aac_channels >> 2) & 0x01;
        adts_header[3] = (codec->aac_channels << 6) & 0xc0;
        // frame_length 13bits
        adts_header[3] |= (frame_length >> 11) & 0x03;
        adts_header[4] = (frame_length >> 3) & 0xff;
        adts_header[5] = ((frame_length << 5) & 0xe0);
        // adts_buffer_fullness; //11bits
        adts_header[5] |= 0x1f;

        // copy to audio buffer
        ab->append((const char*)adts_header, sizeof(adts_header));
        ab->append(sample_unit->bytes, sample_unit->size);
    }
    
    return ret;
}

int SrsTsCache::do_cache_video(SrsAvcAacCodec* codec, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // for type1/5/6, insert aud packet.
    static u_int8_t aud_nal[] = { 0x00, 0x00, 0x00, 0x01, 0x09, 0xf0 };
    
    bool sps_pps_sent = false;
    bool aud_sent = false;
    /**
    * a ts sample is format as:
    * 00 00 00 01 // header
    *       xxxxxxx // data bytes
    * 00 00 01 // continue header
    *       xxxxxxx // data bytes.
    * so, for each sample, we append header in aud_nal, then appends the bytes in sample.
    */
    for (int i = 0; i < sample->nb_sample_units; i++) {
        SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
        int32_t size = sample_unit->size;
        
        if (!sample_unit->bytes || size <= 0) {
            ret = ERROR_HLS_AVC_SAMPLE_SIZE;
            srs_error("invalid avc sample length=%d, ret=%d", size, ret);
            return ret;
        }
        
        /**
        * step 1:
        * first, before each "real" sample, 
        * we add some packets according to the nal_unit_type,
        * for example, when got nal_unit_type=5, insert SPS/PPS before sample.
        */
        
        // 5bits, 7.3.1 NAL unit syntax, 
        // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        u_int8_t nal_unit_type;
        nal_unit_type = *sample_unit->bytes;
        nal_unit_type &= 0x1f;
        
        // @see: ngx_rtmp_hls_video
        // Table 7-1 ¨C NAL unit type codes, page 61
        // 1: Coded slice
        if (nal_unit_type == 1) {
            sps_pps_sent = false;
        }
        
        // 6: Supplemental enhancement information (SEI) sei_rbsp( ), page 61
        // @see: ngx_rtmp_hls_append_aud
        if (!aud_sent) {
            // @remark, when got type 9, we donot send aud_nal, but it will make 
            //      ios unhappy, so we remove it.
            // @see https://github.com/winlinvip/simple-rtmp-server/issues/281
            /*if (nal_unit_type == 9) {
                aud_sent = true;
            }*/
            
            if (nal_unit_type == 1 || nal_unit_type == 5 || nal_unit_type == 6) {
                // for type 6, append a aud with type 9.
                vb->append((const char*)aud_nal, sizeof(aud_nal));
                aud_sent = true;
            }
        }
        
        // 5: Coded slice of an IDR picture.
        // insert sps/pps before IDR or key frame is ok.
        if (nal_unit_type == 5 && !sps_pps_sent) {
            sps_pps_sent = true;
            
            // @see: ngx_rtmp_hls_append_sps_pps
            if (codec->sequenceParameterSetLength > 0) {
                // AnnexB prefix, for sps always 4 bytes header
                vb->append((const char*)aud_nal, 4);
                // sps
                vb->append(codec->sequenceParameterSetNALUnit, codec->sequenceParameterSetLength);
            }
            if (codec->pictureParameterSetLength > 0) {
                // AnnexB prefix, for pps always 4 bytes header
                vb->append((const char*)aud_nal, 4);
                // pps
                vb->append(codec->pictureParameterSetNALUnit, codec->pictureParameterSetLength);
            }
        }
        
        // 7-9, ignore, @see: ngx_rtmp_hls_video
        if (nal_unit_type >= 7 && nal_unit_type <= 9) {
            continue;
        }
        
        /**
        * step 2:
        * output the "real" sample, in buf.
        * when we output some special assist packets according to nal_unit_type
        */
        
        // sample start prefix, '00 00 00 01' or '00 00 01'
        u_int8_t* p = aud_nal + 1;
        u_int8_t* end = p + 3;
        
        // first AnnexB prefix is long (4 bytes)
        if (vb->length() == 0) {
            p = aud_nal;
        }
        vb->append((const char*)p, end - p);
        
        // sample data
        vb->append(sample_unit->bytes, sample_unit->size);
    }
    
    return ret;
}

SrsTsEncoder::SrsTsEncoder()
{
    _fs = NULL;
    codec = new SrsAvcAacCodec();
    sample = new SrsCodecSample();
    cache = new SrsTsCache();
    muxer = NULL;
}

SrsTsEncoder::~SrsTsEncoder()
{
    srs_freep(codec);
    srs_freep(sample);
    srs_freep(cache);
    srs_freep(muxer);
}

int SrsTsEncoder::initialize(SrsFileWriter* fs)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fs);
    
    if (!fs->is_open()) {
        ret = ERROR_KERNEL_FLV_STREAM_CLOSED;
        srs_warn("stream is not open for encoder. ret=%d", ret);
        return ret;
    }
    
    _fs = fs;

    srs_freep(muxer);
    muxer = new SrsTSMuxer(fs);

    if ((ret = muxer->open("")) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTsEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    sample->clear();
    if ((ret = codec->audio_aac_demux(data, size, sample)) != ERROR_SUCCESS) {
        if (ret != ERROR_HLS_TRY_MP3) {
            srs_error("http: ts aac demux audio failed. ret=%d", ret);
            return ret;
        }
        if ((ret = codec->audio_mp3_demux(data, size, sample)) != ERROR_SUCCESS) {
            srs_error("http: ts mp3 demux audio failed. ret=%d", ret);
            return ret;
        }
    }
    SrsCodecAudio acodec = (SrsCodecAudio)codec->audio_codec_id;
    
    // ts support audio codec: aac/mp3
    if (acodec != SrsCodecAudioAAC && acodec != SrsCodecAudioMP3) {
        return ret;
    }

    // when codec changed, write new header.
    if ((ret = muxer->update_acodec(acodec)) != ERROR_SUCCESS) {
        srs_error("http: ts audio write header failed. ret=%d", ret);
        return ret;
    }
    
    // for aac: ignore sequence header
    if (acodec == SrsCodecAudioAAC && sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        return ret;
    }

    // the dts calc from rtmp/flv header.
    // @remark for http ts stream, the timestamp is always monotonically increase,
    //      for the packet is filtered by consumer.
    int64_t dts = timestamp * 90;
    
    // write audio to cache.
    if ((ret = cache->cache_audio(codec, dts, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // flush if buffer exceed max size.
    if (cache->ab->length() > SRS_AUTO_HLS_AUDIO_CACHE_SIZE) {
        return flush_video();
    }

    // TODO: config it.
    // in ms, audio delay to flush the audios.
    int64_t audio_delay = SRS_CONF_DEFAULT_AAC_DELAY;
    // flush if audio delay exceed
    if (dts - cache->audio_buffer_start_pts > audio_delay * 90) {
        return flush_audio();
    }

    return ret;
}

int SrsTsEncoder::write_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    sample->clear();
    if ((ret = codec->video_avc_demux(data, size, sample)) != ERROR_SUCCESS) {
        srs_error("http: ts codec demux video failed. ret=%d", ret);
        return ret;
    }
    
    // ignore info frame,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/288#issuecomment-69863909
    if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
        return ret;
    }
    
    if (codec->video_codec_id != SrsCodecVideoAVC) {
        return ret;
    }
    
    // ignore sequence header
    if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
         && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        return ret;
    }
    
    int64_t dts = timestamp * 90;
    
    // write video to cache.
    if ((ret = cache->cache_video(codec, dts, sample)) != ERROR_SUCCESS) {
        return ret;
    }

    return flush_video();
}

int SrsTsEncoder::flush_audio()
{
    int ret = ERROR_SUCCESS;

    if ((ret = muxer->write_audio(cache->af, cache->ab)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the buffer
    cache->ab->erase(cache->ab->length());

    return ret;
}

int SrsTsEncoder::flush_video()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = muxer->write_video(cache->vf, cache->vb)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the buffer
    cache->vb->erase(cache->vb->length());

    return ret;
}


