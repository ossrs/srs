/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#include <srs_kernel_aac.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_core_autofree.hpp>

SrsAacTransmuxer::SrsAacTransmuxer()
{
    writer = NULL;
    got_sequence_header = false;
    aac_object = SrsAacObjectTypeReserved;
}

SrsAacTransmuxer::~SrsAacTransmuxer()
{
}

srs_error_t SrsAacTransmuxer::initialize(ISrsStreamWriter* fs)
{
    srs_error_t err = srs_success;
    
    srs_assert(fs);
    
    writer = fs;
    
    return err;
}

srs_error_t SrsAacTransmuxer::write_audio(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    SrsBuffer* stream = new SrsBuffer(data, size);
    SrsAutoFree(SrsBuffer, stream);
    
    // audio decode
    if (!stream->require(1)) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac decode audio sound_format failed");
    }
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();
    
    //int8_t sound_type = sound_format & 0x01;
    //int8_t sound_size = (sound_format >> 1) & 0x01;
    //int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    if ((SrsAudioCodecId)sound_format != SrsAudioCodecIdAAC) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac required, format=%d", sound_format);
    }
    
    if (!stream->require(1)) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac decode aac_packet_type failed");
    }
    
    SrsAudioAacFrameTrait aac_packet_type = (SrsAudioAacFrameTrait)stream->read_1bytes();
    if (aac_packet_type == SrsAudioAacFrameTraitSequenceHeader) {
        // AudioSpecificConfig
        // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
        //
        // only need to decode the first 2bytes:
        // audioObjectType, 5bits.
        // samplingFrequencyIndex, aac_sample_rate, 4bits.
        // channelConfiguration, aac_channels, 4bits
        if (!stream->require(2)) {
            return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac decode sequence header failed");
        }
        
        int8_t audioObjectType = stream->read_1bytes();
        aac_sample_rate = stream->read_1bytes();
        
        aac_channels = (aac_sample_rate >> 3) & 0x0f;
        aac_sample_rate = ((audioObjectType << 1) & 0x0e) | ((aac_sample_rate >> 7) & 0x01);
        
        audioObjectType = (audioObjectType >> 3) & 0x1f;
        aac_object = (SrsAacObjectType)audioObjectType;
        
        got_sequence_header = true;
        
        return err;
    }
    
    if (!got_sequence_header) {
        return srs_error_new(ERROR_AAC_DECODE_ERROR, "aac no sequence header");
    }
    
    // the left is the aac raw frame data.
    int16_t aac_raw_length = stream->size() - stream->pos();
    
    // write the ADTS header.
    // @see ISO_IEC_14496-3-AAC-2001.pdf, page 75,
    //      1.A.2.2 Audio_Data_Transport_Stream frame, ADTS
    // @see https://github.com/ossrs/srs/issues/212#issuecomment-64145885
    // byte_alignment()
    
    // adts_fixed_header:
    //      12bits syncword,
    //      16bits left.
    // adts_variable_header:
    //      28bits
    //      12+16+28=56bits
    // adts_error_check:
    //      16bits if protection_absent
    //      56+16=72bits
    // if protection_absent:
    //      require(7bytes)=56bits
    // else
    //      require(9bytes)=72bits
    char aac_fixed_header[7];
    if(true) {
        char* pp = aac_fixed_header;
        int16_t aac_frame_length = aac_raw_length + 7;
        
        // Syncword 12 bslbf
        *pp++ = 0xff;
        // 4bits left.
        // adts_fixed_header(), 1.A.2.2.1 Fixed Header of ADTS
        // ID 1 bslbf
        // Layer 2 uimsbf
        // protection_absent 1 bslbf
        *pp++ = 0xf1;
        
        // profile 2 uimsbf
        // sampling_frequency_index 4 uimsbf
        // private_bit 1 bslbf
        // channel_configuration 3 uimsbf
        // original/copy 1 bslbf
        // home 1 bslbf
        SrsAacProfile aac_profile = srs_aac_rtmp2ts(aac_object);
        *pp++ = ((aac_profile << 6) & 0xc0) | ((aac_sample_rate << 2) & 0x3c) | ((aac_channels >> 2) & 0x01);
        // 4bits left.
        // adts_variable_header(), 1.A.2.2.2 Variable Header of ADTS
        // copyright_identification_bit 1 bslbf
        // copyright_identification_start 1 bslbf
        *pp++ = ((aac_channels << 6) & 0xc0) | ((aac_frame_length >> 11) & 0x03);
        
        // aac_frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
        // use the left 2bits as the 13 and 12 bit,
        // the aac_frame_length is 13bits, so we move 13-2=11.
        *pp++ = aac_frame_length >> 3;
        // adts_buffer_fullness 11 bslbf
        *pp++ = (aac_frame_length << 5) & 0xe0;
        
        // no_raw_data_blocks_in_frame 2 uimsbf
        *pp++ = 0xfc;
    }
    
    // write 7bytes fixed header.
    if ((err = writer->write(aac_fixed_header, 7, NULL)) != srs_success) {
        return srs_error_wrap(err, "write aac header");
    }
    
    // write aac frame body.
    if ((err = writer->write(data + stream->pos(), aac_raw_length, NULL)) != srs_success) {
        return srs_error_wrap(err, "write aac frame");
    }
    
    return err;
}

#endif

