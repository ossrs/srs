/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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
#include <srs_utest_kernel.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

#define MAX_MOCK_DATA_SIZE 1024 * 1024

MockSrsFileWriter::MockSrsFileWriter()
{
    data = new char[MAX_MOCK_DATA_SIZE];
    offset = -1;
}

MockSrsFileWriter::~MockSrsFileWriter()
{
    srs_freep(data);
}

int MockSrsFileWriter::open(string /*file*/)
{
    int ret = ERROR_SUCCESS;
    
    offset = 0;
    
    return ret;
}

void MockSrsFileWriter::close()
{
    offset = 0;
}

bool MockSrsFileWriter::is_open()
{
    return offset >= 0;
}

int64_t MockSrsFileWriter::tellg()
{
    return offset;
}

int MockSrsFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    int ret = ERROR_SUCCESS;
    
    int size = srs_min(MAX_MOCK_DATA_SIZE - offset, count);
    
    memcpy(data + offset, buf, size);

    if (pnwrite) {
        *pnwrite = size;
    }
    
    offset += size;
    
    return ret;
}

void MockSrsFileWriter::mock_reset_offset()
{
    offset = 0;
}

MockSrsFileReader::MockSrsFileReader()
{
    data = new char[MAX_MOCK_DATA_SIZE];
    size = 0;
    offset = -1;
}

MockSrsFileReader::~MockSrsFileReader()
{
    srs_freep(data);
}

int MockSrsFileReader::open(string /*file*/)
{
    int ret = ERROR_SUCCESS;
    
    offset = 0;
    
    return ret;
}

void MockSrsFileReader::close()
{
    offset = 0;
}

bool MockSrsFileReader::is_open()
{
    return offset >= 0;
}

int64_t MockSrsFileReader::tellg()
{
    return offset;
}

void MockSrsFileReader::skip(int64_t _size)
{
    offset += _size;
}

int64_t MockSrsFileReader::lseek(int64_t _offset)
{
    offset = (int)_offset;
    return offset;
}

int64_t MockSrsFileReader::filesize()
{
    return size;
}

int MockSrsFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    int ret = ERROR_SUCCESS;
    
    int s = srs_min(size - offset, (int)count);
    
    if (s <= 0) {
        return ret;
    }
    
    memcpy(buf, data + offset, s);

    if (pnread) {
        *pnread = s;
    }
    
    offset += s;
    
    return ret;
}

void MockSrsFileReader::mock_append_data(const char* _data, int _size)
{
    int s = srs_min(MAX_MOCK_DATA_SIZE - offset, _size);
    memcpy(data + offset, _data, s);
    
    offset += s;
    size += s;
}

void MockSrsFileReader::mock_reset_offset()
{
    offset = 0;
}

MockBufferReader::MockBufferReader(const char* data)
{
    str = data;
}

MockBufferReader::~MockBufferReader()
{
}

int MockBufferReader::read(void* buf, size_t size, ssize_t* nread)
{
    int len = srs_min(str.length(), size);

    memcpy(buf, str.data(), len);
    
    if (nread) {
        *nread = len;
    }

    return ERROR_SUCCESS;
}

VOID TEST(BufferTest, DefaultObject)
{
    SrsBuffer b;
    
    EXPECT_EQ(0, b.length());
    EXPECT_EQ(NULL, b.bytes());
}

VOID TEST(BufferTest, AppendBytes)
{
    SrsBuffer b;
    
    char winlin[] = "winlin";
    b.append(winlin, strlen(winlin));
    EXPECT_EQ((int)strlen(winlin), b.length());
    ASSERT_TRUE(NULL != b.bytes());
    EXPECT_EQ('w', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[5]);

    b.append(winlin, strlen(winlin));
    EXPECT_EQ(2 * (int)strlen(winlin), b.length());
    ASSERT_TRUE(NULL != b.bytes());
    EXPECT_EQ('w', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[5]);
    EXPECT_EQ('w', b.bytes()[6]);
    EXPECT_EQ('n', b.bytes()[11]);
}

VOID TEST(BufferTest, EraseBytes)
{
    SrsBuffer b;
    
    b.erase(0);
    b.erase(-1);
    EXPECT_EQ(0, b.length());
    
    char winlin[] = "winlin";
    b.append(winlin, strlen(winlin));
    b.erase(b.length());
    EXPECT_EQ(0, b.length());
    
    b.erase(0);
    b.erase(-1);
    EXPECT_EQ(0, b.length());
    
    b.append(winlin, strlen(winlin));
    b.erase(1);
    EXPECT_EQ(5, b.length());
    EXPECT_EQ('i', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[4]);
    
    b.erase(2);
    EXPECT_EQ(3, b.length());
    EXPECT_EQ('l', b.bytes()[0]);
    EXPECT_EQ('n', b.bytes()[2]);
    
    b.erase(0);
    b.erase(-1);
    EXPECT_EQ(3, b.length());
    
    b.erase(3);
    EXPECT_EQ(0, b.length());
}

VOID TEST(BufferTest, Grow)
{
    SrsBuffer b;
    MockBufferReader r("winlin");
    
    b.grow(&r, 1);
    EXPECT_EQ(6, b.length());
    EXPECT_EQ('w', b.bytes()[0]);

    b.grow(&r, 3);
    EXPECT_EQ(6, b.length());
    EXPECT_EQ('n', b.bytes()[2]);
    
    b.grow(&r, 100);
    EXPECT_EQ(102, b.length());
    EXPECT_EQ('l', b.bytes()[99]);
}

/**
* test the codec,
* whether H.264 keyframe
*/
VOID TEST(KernelCodecTest, IsKeyFrame)
{
    int8_t data;
    
    data = 0x10;
    EXPECT_TRUE(SrsFlvCodec::video_is_keyframe(&data, 1));
    EXPECT_FALSE(SrsFlvCodec::video_is_keyframe(&data, 0));
    
    data = 0x20;
    EXPECT_FALSE(SrsFlvCodec::video_is_keyframe(&data, 1));
}

/**
* test the codec,
* check whether H.264 video
*/
VOID TEST(KernelCodecTest, IsH264)
{
    int8_t data;
    
    EXPECT_FALSE(SrsFlvCodec::video_is_h264(&data, 0));
    
    data = 0x17;
    EXPECT_TRUE(SrsFlvCodec::video_is_h264(&data, 1));
    
    data = 0x07;
    EXPECT_TRUE(SrsFlvCodec::video_is_h264(&data, 1));
    
    data = 0x08;
    EXPECT_FALSE(SrsFlvCodec::video_is_h264(&data, 1));
}

/**
* test the codec,
* whether H.264 video sequence header
*/
VOID TEST(KernelCodecTest, IsSequenceHeader)
{
    int16_t data;
    char* pp = (char*)&data;
    
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 0));
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 1));
    
    pp[0] = 0x17;
    pp[1] = 0x00;
    EXPECT_TRUE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 2));
    pp[0] = 0x18;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 2));
    pp[0] = 0x27;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 2));
    pp[0] = 0x17;
    pp[1] = 0x01;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 2));
}

/**
* test the codec,
* check whether AAC codec
*/
VOID TEST(KernelCodecTest, IsAAC)
{
    int8_t data;
    
    EXPECT_FALSE(SrsFlvCodec::audio_is_aac(&data, 0));
    
    data = 0xa0;
    EXPECT_TRUE(SrsFlvCodec::audio_is_aac(&data, 1));
    
    data = 0xa7;
    EXPECT_TRUE(SrsFlvCodec::audio_is_aac(&data, 1));
    
    data = 0x00;
    EXPECT_FALSE(SrsFlvCodec::audio_is_aac(&data, 1));
}

/**
* test the codec,
* AAC audio sequence header
*/
VOID TEST(KernelCodecTest, IsAudioSequenceHeader)
{
    int16_t data;
    char* pp = (char*)&data;
    
    EXPECT_FALSE(SrsFlvCodec::audio_is_sequence_header((int8_t*)pp, 0));
    EXPECT_FALSE(SrsFlvCodec::audio_is_sequence_header((int8_t*)pp, 1));
    
    pp[0] = 0xa0;
    pp[1] = 0x00;
    EXPECT_TRUE(SrsFlvCodec::audio_is_sequence_header((int8_t*)pp, 2));
    pp[0] = 0x00;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 2));
    pp[0] = 0xa0;
    pp[1] = 0x01;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((int8_t*)pp, 2));
}

/**
* test the flv encoder,
* exception: file stream not open
*/
VOID TEST(KernelFlvTest, FlvEncoderStreamClosed)
{
    MockSrsFileWriter fs;
    SrsFlvEncoder enc;
    ASSERT_TRUE(ERROR_SUCCESS != enc.initialize(&fs));
}

/**
* test the flv encoder,
* write flv header
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteHeader)
{
    MockSrsFileWriter fs;
    SrsFlvEncoder enc;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == enc.initialize(&fs));
    
    // write header, 9bytes
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x00, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    
    EXPECT_TRUE(ERROR_SUCCESS == enc.write_header());
    ASSERT_TRUE(9 + 4 == fs.offset);
    
    EXPECT_TRUE(srs_bytes_equals(flv_header, fs.data, 9));
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data + 9, 4));

    // customer header
    flv_header[3] = 0xF0;
    flv_header[4] = 0xF1;
    flv_header[5] = 0x01;
    
    fs.mock_reset_offset();
    
    EXPECT_TRUE(ERROR_SUCCESS == enc.write_header(flv_header));
    ASSERT_TRUE(9 + 4 == fs.offset);
    
    EXPECT_TRUE(srs_bytes_equals(flv_header, fs.data, 9));
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data + 9, 4));
}

/**
* test the flv encoder,
* write metadata tag
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteMetadata)
{
    MockSrsFileWriter fs;
    EXPECT_TRUE(ERROR_SUCCESS == fs.open(""));
    SrsFlvEncoder enc;
    ASSERT_TRUE(ERROR_SUCCESS == enc.initialize(&fs));
    
    // 11 bytes tag header
    char tag_header[] = {
        (char)18, // TagType UB [5], 18 = script data
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    char md[] = {
        (char)0x01, (char)0x02, (char)0x03, (char)0x04,
        (char)0x04, (char)0x03, (char)0x02, (char)0x01
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
    
    ASSERT_TRUE(ERROR_SUCCESS == enc.write_metadata(md, 8));
    ASSERT_TRUE(11 + 8 + 4 == fs.offset);
    
    EXPECT_TRUE(srs_bytes_equals(tag_header, fs.data, 11));
    EXPECT_TRUE(srs_bytes_equals(md, fs.data + 11, 8));
    EXPECT_TRUE(true); // donot know why, if not add it, the print is disabled.
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data + 19, 4));
}

/**
* test the flv encoder,
* write audio tag
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteAudio)
{
    MockSrsFileWriter fs;
    SrsFlvEncoder enc;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == enc.initialize(&fs));
    
    // 11bytes tag header
    char tag_header[] = {
        (char)8, // TagType UB [5], 8 = audio
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    char audio[] = {
        (char)0x01, (char)0x02, (char)0x03, (char)0x04,
        (char)0x04, (char)0x03, (char)0x02, (char)0x01
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
    
    ASSERT_TRUE(ERROR_SUCCESS == enc.write_audio(0x30, audio, 8));
    ASSERT_TRUE(11 + 8 + 4 == fs.offset);
    
    EXPECT_TRUE(srs_bytes_equals(tag_header, fs.data, 11));
    EXPECT_TRUE(srs_bytes_equals(audio, fs.data + 11, 8));
    EXPECT_TRUE(true); // donot know why, if not add it, the print is disabled.
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data + 11 + 8, 4));
}

/**
* test the flv encoder,
* write video tag
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteVideo)
{
    MockSrsFileWriter fs;
    SrsFlvEncoder enc;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == enc.initialize(&fs));
    
    // 11bytes tag header
    char tag_header[] = {
        (char)9, // TagType UB [5], 9 = video
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    char video[] = {
        (char)0x01, (char)0x02, (char)0x03, (char)0x04,
        (char)0x04, (char)0x03, (char)0x02, (char)0x01
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
    
    ASSERT_TRUE(ERROR_SUCCESS == enc.write_video(0x30, video, 8));
    ASSERT_TRUE(11 + 8 + 4 == fs.offset);
    
    EXPECT_TRUE(srs_bytes_equals(tag_header, fs.data, 11));
    EXPECT_TRUE(srs_bytes_equals(video, fs.data + 11, 8));
    EXPECT_TRUE(true); // donot know why, if not add it, the print is disabled.
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data + 11 + 8, 4));
}

/**
* test the flv encoder,
* calc the tag size.
*/
VOID TEST(KernelFlvTest, FlvEncoderSizeTag)
{
    EXPECT_EQ(11+4+10, SrsFlvEncoder::size_tag(10));
    EXPECT_EQ(11+4+0, SrsFlvEncoder::size_tag(0));
}

/**
* test the flv decoder,
* exception: file stream not open.
*/
VOID TEST(KernelFlvTest, FlvDecoderStreamClosed)
{
    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS != dec.initialize(&fs));
}

/**
* test the flv decoder,
* decode flv header
*/
VOID TEST(KernelFlvTest, FlvDecoderHeader)
{
    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // 9bytes
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x00, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    fs.mock_append_data(flv_header, 9);
    fs.mock_append_data(pts, 4);
    
    char data[1024];
    fs.mock_reset_offset();
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_header(data));
    EXPECT_TRUE(srs_bytes_equals(flv_header, data, 9));
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv decoder,
* decode metadata tag
*/
VOID TEST(KernelFlvTest, FlvDecoderMetadata)
{
    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // 11 bytes tag header
    char tag_header[] = {
        (char)18, // TagType UB [5], 18 = script data
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    char md[] = {
        (char)0x01, (char)0x02, (char)0x03, (char)0x04,
        (char)0x04, (char)0x03, (char)0x02, (char)0x01
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
    fs.mock_append_data(tag_header, 11);
    fs.mock_append_data(md, 8);
    fs.mock_append_data(pts, 4);
    
    char type = 0;
    int32_t size = 0;
    u_int32_t time = 0;
    char data[1024];
    fs.mock_reset_offset();
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_tag_header(&type, &size, &time));
    EXPECT_TRUE(18 == type);
    EXPECT_TRUE(8 == size);
    EXPECT_TRUE(0 == time);
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_tag_data(data, size));
    EXPECT_TRUE(srs_bytes_equals(md, data, 8));
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv decoder,
* decode audio tag
*/
VOID TEST(KernelFlvTest, FlvDecoderAudio)
{
    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // 11bytes tag header
    char tag_header[] = {
        (char)8, // TagType UB [5], 8 = audio
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    char audio[] = {
        (char)0x01, (char)0x02, (char)0x03, (char)0x04,
        (char)0x04, (char)0x03, (char)0x02, (char)0x01
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
    fs.mock_append_data(tag_header, 11);
    fs.mock_append_data(audio, 8);
    fs.mock_append_data(pts, 4);
    
    char type = 0;
    int32_t size = 0;
    u_int32_t time = 0;
    char data[1024];
    fs.mock_reset_offset();
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_tag_header(&type, &size, &time));
    EXPECT_TRUE(8 == type);
    EXPECT_TRUE(8 == size);
    EXPECT_TRUE(0x30 == time);
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_tag_data(data, size));
    EXPECT_TRUE(srs_bytes_equals(audio, data, 8));
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv decoder,
* decode video tag
*/
VOID TEST(KernelFlvTest, FlvDecoderVideo)
{
    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // 11bytes tag header
    char tag_header[] = {
        (char)9, // TagType UB [5], 9 = video
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    char video[] = {
        (char)0x01, (char)0x02, (char)0x03, (char)0x04,
        (char)0x04, (char)0x03, (char)0x02, (char)0x01
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
    fs.mock_append_data(tag_header, 11);
    fs.mock_append_data(video, 8);
    fs.mock_append_data(pts, 4);
    
    char type = 0;
    int32_t size = 0;
    u_int32_t time = 0;
    char data[1024];
    fs.mock_reset_offset();
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_tag_header(&type, &size, &time));
    EXPECT_TRUE(9 == type);
    EXPECT_TRUE(8 == size);
    EXPECT_TRUE(0x30 == time);
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_tag_data(data, size));
    EXPECT_TRUE(srs_bytes_equals(video, data, 8));
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv vod stream decoder,
* exception: file stream not open.
*/
VOID TEST(KernelFlvTest, FlvVSDecoderStreamClosed)
{
    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS != dec.initialize(&fs));
}

/**
* test the flv vod stream decoder,
* decode the flv header
*/
VOID TEST(KernelFlvTest, FlvVSDecoderHeader)
{
    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // 9bytes
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x00, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    fs.mock_append_data(flv_header, 9);
    fs.mock_append_data(pts, 4);
    
    char data[1024];
    fs.mock_reset_offset();
    
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_header_ext(data));
    EXPECT_TRUE(srs_bytes_equals(flv_header, data, 9));
}

/**
* test the flv vod stream decoder,
* get the start offset and size of sequence header
* mock data: metadata-audio-video
*/
VOID TEST(KernelFlvTest, FlvVSDecoderSequenceHeader)
{
    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // push metadata tag
    if (true) {
        // 11 bytes tag header
        char tag_header[] = {
            (char)18, // TagType UB [5], 18 = script data
            (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
            (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
            (char)0x00, // TimestampExtended UI8
            (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
        };
        char md[] = {
            (char)0x01, (char)0x02, (char)0x03, (char)0x04,
            (char)0x04, (char)0x03, (char)0x02, (char)0x01
        };
        char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
        fs.mock_append_data(tag_header, 11);
        fs.mock_append_data(md, 8);
        fs.mock_append_data(pts, 4);
    }
    // push audio tag
    if (true) {
        // 11bytes tag header
        char tag_header[] = {
            (char)8, // TagType UB [5], 8 = audio
            (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
            (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
            (char)0x00, // TimestampExtended UI8
            (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
        };
        char audio[] = {
            (char)0x01, (char)0x02, (char)0x03, (char)0x04,
            (char)0x04, (char)0x03, (char)0x02, (char)0x01
        };
        char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
        fs.mock_append_data(tag_header, 11);
        fs.mock_append_data(audio, 8);
        fs.mock_append_data(pts, 4);
    }
    // push video tag
    if (true) {
        // 11bytes tag header
        char tag_header[] = {
            (char)9, // TagType UB [5], 9 = video
            (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
            (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
            (char)0x00, // TimestampExtended UI8
            (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
        };
        char video[] = {
            (char)0x01, (char)0x02, (char)0x03, (char)0x04,
            (char)0x04, (char)0x03, (char)0x02, (char)0x01
        };
        char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
        fs.mock_append_data(tag_header, 11);
        fs.mock_append_data(video, 8);
        fs.mock_append_data(pts, 4);
    }
    
    fs.mock_reset_offset();
    
    int64_t start = 0;
    int size = 0;
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_sequence_header_summary(&start, &size));
    EXPECT_TRUE(23 == start);
    EXPECT_TRUE(46 == size);
}

/**
* test the flv vod stream decoder,
* get the start offset and size of sequence header
* mock data: metadata-video-audio
*/
VOID TEST(KernelFlvTest, FlvVSDecoderSequenceHeader2)
{
    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // push metadata tag
    if (true) {
        // 11 bytes tag header
        char tag_header[] = {
            (char)18, // TagType UB [5], 18 = script data
            (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
            (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
            (char)0x00, // TimestampExtended UI8
            (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
        };
        char md[] = {
            (char)0x01, (char)0x02, (char)0x03, (char)0x04,
            (char)0x04, (char)0x03, (char)0x02, (char)0x01
        };
        char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
        fs.mock_append_data(tag_header, 11);
        fs.mock_append_data(md, 8);
        fs.mock_append_data(pts, 4);
    }
    // push video tag
    if (true) {
        // 11bytes tag header
        char tag_header[] = {
            (char)9, // TagType UB [5], 9 = video
            (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
            (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
            (char)0x00, // TimestampExtended UI8
            (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
        };
        char video[] = {
            (char)0x01, (char)0x02, (char)0x03, (char)0x04,
            (char)0x04, (char)0x03, (char)0x02, (char)0x01
        };
        char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
        fs.mock_append_data(tag_header, 11);
        fs.mock_append_data(video, 8);
        fs.mock_append_data(pts, 4);
    }
    // push audio tag
    if (true) {
        // 11bytes tag header
        char tag_header[] = {
            (char)8, // TagType UB [5], 8 = audio
            (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
            (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
            (char)0x00, // TimestampExtended UI8
            (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
        };
        char audio[] = {
            (char)0x01, (char)0x02, (char)0x03, (char)0x04,
            (char)0x04, (char)0x03, (char)0x02, (char)0x01
        };
        char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)19 };
        fs.mock_append_data(tag_header, 11);
        fs.mock_append_data(audio, 8);
        fs.mock_append_data(pts, 4);
    }
    
    fs.mock_reset_offset();
    
    int64_t start = 0;
    int size = 0;
    EXPECT_TRUE(ERROR_SUCCESS == dec.read_sequence_header_summary(&start, &size));
    EXPECT_TRUE(23 == start);
    EXPECT_TRUE(46 == size);
}

/**
* test the flv vod stream decoder,
* seek stream after got the offset and start of flv sequence header,
* to directly response flv data by seek to the offset of file.
*/
VOID TEST(KernelFlvTest, FlvVSDecoderSeek)
{
    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    ASSERT_TRUE(ERROR_SUCCESS == fs.open(""));
    ASSERT_TRUE(ERROR_SUCCESS == dec.initialize(&fs));
    
    // 11bytes tag header
    char tag_header[] = {
        (char)8, // TagType UB [5], 8 = audio
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    fs.mock_append_data(tag_header, 11);
    EXPECT_TRUE(11 == fs.offset);

    EXPECT_TRUE(ERROR_SUCCESS == dec.lseek(0));
    EXPECT_TRUE(0 == fs.offset);

    EXPECT_TRUE(ERROR_SUCCESS == dec.lseek(5));
    EXPECT_TRUE(5 == fs.offset);
}
