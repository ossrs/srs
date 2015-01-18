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

#include <srs_kernel_aac.hpp>

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_file.hpp>

#define SRS_FLV_TAG_HEADER_SIZE 11
#define SRS_FLV_PREVIOUS_TAG_SIZE 4

SrsAacEncoder::SrsAacEncoder()
{
    _fs = NULL;
    tag_stream = new SrsStream();
}

SrsAacEncoder::~SrsAacEncoder()
{
    srs_freep(tag_stream);
}

int SrsAacEncoder::initialize(SrsFileWriter* fs)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fs);
    
    if (!fs->is_open()) {
        ret = ERROR_KERNEL_FLV_STREAM_CLOSED;
        srs_warn("stream is not open for decoder. ret=%d", ret);
        return ret;
    }
    
    _fs = fs;
    
    return ret;
}

int SrsAacEncoder::write_header()
{
    int ret = ERROR_SUCCESS;
    
    // 9bytes header and 4bytes first previous-tag-size
    static char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x00, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    
    // flv specification should set the audio and video flag,
    // actually in practise, application generally ignore this flag,
    // so we generally set the audio/video to 0.
    
    // write 9bytes header.
    if ((ret = write_header(flv_header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsAacEncoder::write_header(char flv_header[9])
{
    int ret = ERROR_SUCCESS;
    
    // write data.
    if ((ret = _fs->write(flv_header, 9, NULL)) != ERROR_SUCCESS) {
        srs_error("write flv header failed. ret=%d", ret);
        return ret;
    }
    
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    if ((ret = _fs->write(pts, 4, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsAacEncoder::write_metadata(char type, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    // 11 bytes tag header
    static char tag_header[] = {
        (char)type, // TagType UB [5], 18 = script data
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // write data size.
    if ((ret = tag_stream->initialize(tag_header + 1, 3)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_3bytes(size);
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv data tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsAacEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    static char tag_header[] = {
        (char)8, // TagType UB [5], 8 = audio
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // write data size.
    if ((ret = tag_stream->initialize(tag_header + 1, 7)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv audio tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsAacEncoder::write_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    static char tag_header[] = {
        (char)9, // TagType UB [5], 9 = video
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // write data size.
    if ((ret = tag_stream->initialize(tag_header + 1, 7)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv video tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsAacEncoder::size_tag(int data_size)
{
    srs_assert(data_size >= 0);
    return SRS_FLV_TAG_HEADER_SIZE + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
}

int SrsAacEncoder::write_tag(char* header, int header_size, char* tag, int tag_size)
{
    int ret = ERROR_SUCCESS;
    
    // write tag header.
    if ((ret = _fs->write(header, header_size, NULL)) != ERROR_SUCCESS) {
        srs_error("write flv tag header failed. ret=%d", ret);
        return ret;
    }
    
    // write tag data.
    if ((ret = _fs->write(tag, tag_size, NULL)) != ERROR_SUCCESS) {
        srs_error("write flv tag failed. ret=%d", ret);
        return ret;
    }
    
    // PreviousTagSizeN UI32 Size of last tag, including its header, in bytes.
    static char pre_size[SRS_FLV_PREVIOUS_TAG_SIZE];
    if ((ret = tag_stream->initialize(pre_size, SRS_FLV_PREVIOUS_TAG_SIZE)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_4bytes(tag_size + header_size);
    if ((ret = _fs->write(pre_size, sizeof(pre_size), NULL)) != ERROR_SUCCESS) {
        srs_error("write flv previous tag size failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}


