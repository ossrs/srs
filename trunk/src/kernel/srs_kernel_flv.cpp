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

#include <srs_kernel_flv.hpp>

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
#include <srs_kernel_codec.hpp>

#define SRS_FLV_TAG_HEADER_SIZE 11
#define SRS_FLV_PREVIOUS_TAG_SIZE 4

SrsFlvEncoder::SrsFlvEncoder()
{
    _fs = NULL;
    tag_stream = new SrsStream();
}

SrsFlvEncoder::~SrsFlvEncoder()
{
    srs_freep(tag_stream);
}

int SrsFlvEncoder::initialize(SrsFileWriter* fs)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fs);
    
    if (!fs->is_open()) {
        ret = ERROR_KERNEL_FLV_STREAM_CLOSED;
        srs_warn("stream is not open for encoder. ret=%d", ret);
        return ret;
    }
    
    _fs = fs;
    
    return ret;
}

int SrsFlvEncoder::write_header()
{
    int ret = ERROR_SUCCESS;
    
    // 9bytes header and 4bytes first previous-tag-size
    char flv_header[] = {
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

int SrsFlvEncoder::write_header(char flv_header[9])
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

int SrsFlvEncoder::write_metadata(char type, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    // 11 bytes tag header
    char tag_header[] = {
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

int SrsFlvEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    char tag_header[] = {
        (char)SrsCodecFlvTagAudio, // TagType UB [5], 8 = audio
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

int SrsFlvEncoder::write_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    char tag_header[] = {
        (char)SrsCodecFlvTagVideo, // TagType UB [5], 9 = video
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

int SrsFlvEncoder::size_tag(int data_size)
{
    srs_assert(data_size >= 0);
    return SRS_FLV_TAG_HEADER_SIZE + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
}

int SrsFlvEncoder::write_tag(char* header, int header_size, char* tag, int tag_size)
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
    char pre_size[SRS_FLV_PREVIOUS_TAG_SIZE];
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

SrsFlvDecoder::SrsFlvDecoder()
{
    _fs = NULL;
    tag_stream = new SrsStream();
}

SrsFlvDecoder::~SrsFlvDecoder()
{
    srs_freep(tag_stream);
}

int SrsFlvDecoder::initialize(SrsFileReader* fs)
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

int SrsFlvDecoder::read_header(char header[9])
{
    int ret = ERROR_SUCCESS;

    srs_assert(header);
    
    if ((ret = _fs->read(header, 9, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char* h = header;
    if (h[0] != 'F' || h[1] != 'L' || h[2] != 'V') {
        ret = ERROR_KERNEL_FLV_HEADER;
        srs_warn("flv header must start with FLV. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvDecoder::read_tag_header(char* ptype, int32_t* pdata_size, u_int32_t* ptime)
{
    int ret = ERROR_SUCCESS;

    srs_assert(ptype);
    srs_assert(pdata_size);
    srs_assert(ptime);

    char th[11]; // tag header
    
    // read tag header
    if ((ret = _fs->read(th, 11, NULL)) != ERROR_SUCCESS) {
        if (ret != ERROR_SYSTEM_FILE_EOF) {
            srs_error("read flv tag header failed. ret=%d", ret);
        }
        return ret;
    }
    
    // Reserved UB [2]
    // Filter UB [1]
    // TagType UB [5]
    *ptype = (th[0] & 0x1F);
    
    // DataSize UI24
    char* pp = (char*)pdata_size;
    pp[3] = 0;
    pp[2] = th[1];
    pp[1] = th[2];
    pp[0] = th[3];
    
    // Timestamp UI24
    pp = (char*)ptime;
    pp[2] = th[4];
    pp[1] = th[5];
    pp[0] = th[6];
    
    // TimestampExtended UI8
    pp[3] = th[7];

    return ret;
}

int SrsFlvDecoder::read_tag_data(char* data, int32_t size)
{
    int ret = ERROR_SUCCESS;

    srs_assert(data);
    
    if ((ret = _fs->read(data, size, NULL)) != ERROR_SUCCESS) {
        if (ret != ERROR_SYSTEM_FILE_EOF) {
            srs_error("read flv tag header failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;

}

int SrsFlvDecoder::read_previous_tag_size(char previous_tag_size[4])
{
    int ret = ERROR_SUCCESS;

    srs_assert(previous_tag_size);
    
    // ignore 4bytes tag size.
    if ((ret = _fs->read(previous_tag_size, 4, NULL)) != ERROR_SUCCESS) {
        if (ret != ERROR_SYSTEM_FILE_EOF) {
            srs_error("read flv previous tag size failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

SrsFlvVodStreamDecoder::SrsFlvVodStreamDecoder()
{
    _fs = NULL;
    tag_stream = new SrsStream();
}

SrsFlvVodStreamDecoder::~SrsFlvVodStreamDecoder()
{
    srs_freep(tag_stream);
}

int SrsFlvVodStreamDecoder::initialize(SrsFileReader* fs)
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

int SrsFlvVodStreamDecoder::read_header_ext(char header[13])
{
    int ret = ERROR_SUCCESS;

    srs_assert(header);
    
    // @remark, always false, for sizeof(char[13]) equals to sizeof(char*)
    //srs_assert(13 == sizeof(header));
    
    // 9bytes header and 4bytes first previous-tag-size
    int size = 13;
    
    if ((ret = _fs->read(header, size, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvVodStreamDecoder::read_sequence_header_summary(int64_t* pstart, int* psize)
{
    int ret = ERROR_SUCCESS;

    srs_assert(pstart);
    srs_assert(psize);
    
    // simply, the first video/audio must be the sequence header.
    // and must be a sequence video and audio.
    
    // 11bytes tag header
    char tag_header[] = {
        (char)0x00, // TagType UB [5], 9 = video, 8 = audio, 18 = script data
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // discovery the sequence header video and audio.
    // @remark, maybe no video or no audio.
    bool got_video = false;
    bool got_audio = false;
    // audio/video sequence and data offset.
    int64_t av_sequence_offset_start = -1;
    int64_t av_sequence_offset_end = -1;
    for (;;) {
        if ((ret = _fs->read(tag_header, SRS_FLV_TAG_HEADER_SIZE, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = tag_stream->initialize(tag_header, SRS_FLV_TAG_HEADER_SIZE)) != ERROR_SUCCESS) {
            return ret;
        }
        
        int8_t tag_type = tag_stream->read_1bytes();
        int32_t data_size = tag_stream->read_3bytes();
        
        bool is_video = tag_type == 0x09;
        bool is_audio = tag_type == 0x08;
        bool is_not_av = !is_video && !is_audio;
        if (is_not_av) {
            // skip body and tag size.
            _fs->skip(data_size + SRS_FLV_PREVIOUS_TAG_SIZE);
            continue;
        }
        
        // if video duplicated, no audio
        if (is_video && got_video) {
            break;
        }
        // if audio duplicated, no video
        if (is_audio && got_audio) {
            break;
        }
        
        // video
        if (is_video) {
            srs_assert(!got_video);
            got_video = true;
            
            if (av_sequence_offset_start < 0) {
                av_sequence_offset_start = _fs->tellg() - SRS_FLV_TAG_HEADER_SIZE;
            }
            av_sequence_offset_end = _fs->tellg() + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
            _fs->skip(data_size + SRS_FLV_PREVIOUS_TAG_SIZE);
        }
        
        // audio
        if (is_audio) {
            srs_assert(!got_audio);
            got_audio = true;
            
            if (av_sequence_offset_start < 0) {
                av_sequence_offset_start = _fs->tellg() - SRS_FLV_TAG_HEADER_SIZE;
            }
            av_sequence_offset_end = _fs->tellg() + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
            _fs->skip(data_size + SRS_FLV_PREVIOUS_TAG_SIZE);
        }
    }
    
    // seek to the sequence header start offset.
    if (av_sequence_offset_start > 0) {
        _fs->lseek(av_sequence_offset_start);
        *pstart = av_sequence_offset_start;
        *psize = (int)(av_sequence_offset_end - av_sequence_offset_start);
    }
    
    return ret;
}

int SrsFlvVodStreamDecoder::lseek(int64_t offset)
{
    int ret = ERROR_SUCCESS;
    
    if (offset >= _fs->filesize()) {
        ret = ERROR_SYSTEM_FILE_EOF;
        srs_warn("flv fast decoder seek overflow file, "
            "size=%"PRId64", offset=%"PRId64", ret=%d", 
            _fs->filesize(), offset, ret);
        return ret;
    }
    
    if (_fs->lseek(offset) < 0) {
        ret = ERROR_SYSTEM_FILE_SEEK;
        srs_warn("flv fast decoder seek error, "
            "size=%"PRId64", offset=%"PRId64", ret=%d", 
            _fs->filesize(), offset, ret);
        return ret;
    }
    
    return ret;
}


