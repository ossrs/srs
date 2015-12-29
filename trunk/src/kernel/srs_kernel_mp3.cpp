/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

SrsMp3Encoder::SrsMp3Encoder()
{
    writer = NULL;
    tag_stream = new SrsBuffer();
}

SrsMp3Encoder::~SrsMp3Encoder()
{
    srs_freep(tag_stream);
}

int SrsMp3Encoder::initialize(SrsFileWriter* fw)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fw);
    
    if (!fw->is_open()) {
        ret = ERROR_KERNEL_MP3_STREAM_CLOSED;
        srs_warn("stream is not open for encoder. ret=%d", ret);
        return ret;
    }
    
    writer = fw;
    
    return ret;
}

int SrsMp3Encoder::write_header()
{
    char id3[] = {
        (char)0x49, (char)0x44, (char)0x33, // ID3
        (char)0x03, (char)0x00, // version
        (char)0x00, // flags
        (char)0x00, (char)0x00, (char)0x00, (char)0x0a, // size
        
        (char)0x00, (char)0x00, (char)0x00, (char)0x00, // FrameID
        (char)0x00, (char)0x00, (char)0x00, (char)0x00, // FrameSize
        (char)0x00, (char)0x00 // Flags
    };
    return writer->write(id3, sizeof(id3), NULL);
}

int SrsMp3Encoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    SrsBuffer* stream = tag_stream;
    if ((ret = stream->initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }

    // audio decode
    if (!stream->require(1)) {
        ret = ERROR_MP3_DECODE_ERROR;
        srs_error("mp3 decode audio sound_format failed. ret=%d", ret);
        return ret;
    }
    
    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();
    
    // @see: SrsAvcAacCodec::audio_aac_demux
    //int8_t sound_type = sound_format & 0x01;
    //int8_t sound_size = (sound_format >> 1) & 0x01;
    //int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;
    
    if ((SrsCodecAudio)sound_format != SrsCodecAudioMP3) {
        ret = ERROR_MP3_DECODE_ERROR;
        srs_error("mp3 required, format=%d. ret=%d", sound_format, ret);
        return ret;
    }

    if (!stream->require(1)) {
        ret = ERROR_MP3_DECODE_ERROR;
        srs_error("mp3 decode aac_packet_type failed. ret=%d", ret);
        return ret;
    }
    
    return writer->write(data + stream->pos(), size - stream->pos(), NULL);
}

#endif

