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
    0x0f, 0xe1, 0x01, 0xf0, 0x00, /* aac, pid=0x101=257 */
    /*0x03, 0xe1, 0x01, 0xf0, 0x00,*/ /* mp3 */
    /* CRC */
    0x2f, 0x44, 0xb9, 0x9b, /* crc for aac */
    /*0x4e, 0x59, 0x3d, 0x1e,*/ /* crc for mp3 */
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
    static int write_header(SrsFileWriter* writer)
    {
        int ret = ERROR_SUCCESS;
        
        if ((ret = writer->write(mpegts_header, sizeof(mpegts_header), NULL)) != ERROR_SUCCESS) {
            ret = ERROR_HLS_WRITE_FAILED;
            srs_error("write ts file header failed. ret=%d", ret);
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
                    p = write_pcr(p, frame->dts - SRS_AUTO_HLS_DELAY);
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
        // the pcr=dts-delay
        // and the pcr maybe negative
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/268
        int64_t v = srs_max(0, pcr);
        
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

SrsTSMuxer::SrsTSMuxer(SrsFileWriter* w)
{
    writer = w;
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

    // write mpegts header
    if ((ret = SrsMpegtsWriter::write_header(writer)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTSMuxer::write_audio(SrsMpegtsFrame* af, SrsSimpleBuffer* ab)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsMpegtsWriter::write_frame(writer, af, ab)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTSMuxer::write_video(SrsMpegtsFrame* vf, SrsSimpleBuffer* vb)
{
    int ret = ERROR_SUCCESS;
    
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
    
    // start buffer, set the af
    if (ab->length() == 0) {
        pts = aac_jitter->on_buffer_start(pts, sample->sound_rate, codec->aac_sample_rate);
        
        af->dts = af->pts = pts;
        af->pid = TS_AUDIO_PID;
        af->sid = TS_AUDIO_AAC;
    } else {
        aac_jitter->on_buffer_continue();
    }
    
    // write audio to cache.
    if ((ret = do_cache_audio(codec, sample)) != ERROR_SUCCESS) {
        return ret;
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
        srs_error("http: ts codec demux audio failed. ret=%d", ret);
        return ret;
    }
    
    if (codec->audio_codec_id != SrsCodecAudioAAC) {
        return ret;
    }
    
    // ignore sequence header
    if (sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
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
        if ((ret = muxer->write_audio(cache->af, cache->ab)) != ERROR_SUCCESS) {
            return ret;
        }
    
        // write success, clear and free the buffer
        cache->ab->erase(cache->ab->length());
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
    
    if ((ret = muxer->write_video(cache->vf, cache->vb)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the buffer
    cache->vb->erase(cache->vb->length());

    return ret;
}


