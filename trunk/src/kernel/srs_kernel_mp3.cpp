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

#include <srs_kernel_mp3.hpp>

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
#include <srs_kernel_file.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_core_autofree.hpp>

SrsMp3Transmuxer::SrsMp3Transmuxer()
{
    writer = NULL;
}

SrsMp3Transmuxer::~SrsMp3Transmuxer()
{
}

srs_error_t SrsMp3Transmuxer::initialize(SrsFileWriter* fw)
{
    srs_error_t err = srs_success;
    
    srs_assert(fw);
    
    if (!fw->is_open()) {
        return srs_error_new(ERROR_KERNEL_MP3_STREAM_CLOSED, "stream is not open");
    }
    
    writer = fw;
    
    return err;
}

srs_error_t SrsMp3Transmuxer::write_header()
{
    srs_error_t err = srs_success;
    
    char id3[] = {
        (char)0x49, (char)0x44, (char)0x33, // ID3
        (char)0x03, (char)0x00, // version
        (char)0x00, // flags
        (char)0x00, (char)0x00, (char)0x00, (char)0x0a, // size
        
        (char)0x00, (char)0x00, (char)0x00, (char)0x00, // FrameID
        (char)0x00, (char)0x00, (char)0x00, (char)0x00, // FrameSize
        (char)0x00, (char)0x00 // Flags
    };
    
    if ((err = writer->write(id3, sizeof(id3), NULL)) != srs_success) {
        return srs_error_wrap(err, "write id3");
    }
    
    return err;
}

srs_error_t SrsMp3Transmuxer::write_audio(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    SrsBuffer* stream = new SrsBuffer(data, size);
    SrsAutoFree(SrsBuffer, stream);
    
    // audio decode
    if (!stream->require(1)) {
        return srs_error_new(ERROR_MP3_DECODE_ERROR, "decode sound_format");
    }
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();
    
    //int8_t sound_type = sound_format & 0x01;
    //int8_t sound_size = (sound_format >> 1) & 0x01;
    //int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    if ((SrsAudioCodecId)sound_format != SrsAudioCodecIdMP3) {
        return srs_error_new(ERROR_MP3_DECODE_ERROR, "mp3 required, format=%d", sound_format);
    }
    
    if (!stream->require(1)) {
        return srs_error_new(ERROR_MP3_DECODE_ERROR, "mp3 decode aac_packet_type failed");
    }
    
    if ((err = writer->write(data + stream->pos(), size - stream->pos(), NULL)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }
    
    return err;
}

#endif

