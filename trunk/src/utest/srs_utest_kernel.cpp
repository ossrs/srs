/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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
#include <srs_rtmp_utility.hpp>
#include <srs_kernel_stream.hpp>

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
    
    int size = srs_min(MAX_MOCK_DATA_SIZE - offset, (int)count);
    
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

#ifdef ENABLE_UTEST_KERNEL

VOID TEST(KernelBufferTest, DefaultObject)
{
    SrsSimpleBuffer b;
    
    EXPECT_EQ(0, b.length());
    EXPECT_EQ(NULL, b.bytes());
}

VOID TEST(KernelBufferTest, AppendBytes)
{
    SrsSimpleBuffer b;
    
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

VOID TEST(KernelBufferTest, EraseBytes)
{
    SrsSimpleBuffer b;
    
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

VOID TEST(KernelFastBufferTest, Grow)
{
    SrsFastBuffer b;
    MockBufferReader r("winlin");
    
    b.grow(&r, 1);
    EXPECT_EQ('w', b.read_1byte());

    b.grow(&r, 3);
    b.skip(1);
    EXPECT_EQ('n', b.read_1byte());
    
    b.grow(&r, 100);
    b.skip(99);
    EXPECT_EQ('w', b.read_1byte());
}

/**
* test the codec,
* whether H.264 keyframe
*/
VOID TEST(KernelCodecTest, IsKeyFrame)
{
    char data;
    
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
    char data;
    
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
    
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 0));
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 1));
    
    pp[0] = 0x17;
    pp[1] = 0x00;
    EXPECT_TRUE(SrsFlvCodec::video_is_sequence_header((char*)pp, 2));
    pp[0] = 0x18;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 2));
    pp[0] = 0x27;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 2));
    pp[0] = 0x17;
    pp[1] = 0x01;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 2));
}

/**
* test the codec,
* check whether AAC codec
*/
VOID TEST(KernelCodecTest, IsAAC)
{
    char data;
    
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
    
    EXPECT_FALSE(SrsFlvCodec::audio_is_sequence_header((char*)pp, 0));
    EXPECT_FALSE(SrsFlvCodec::audio_is_sequence_header((char*)pp, 1));
    
    pp[0] = 0xa0;
    pp[1] = 0x00;
    EXPECT_TRUE(SrsFlvCodec::audio_is_sequence_header((char*)pp, 2));
    pp[0] = 0x00;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 2));
    pp[0] = 0xa0;
    pp[1] = 0x01;
    EXPECT_FALSE(SrsFlvCodec::video_is_sequence_header((char*)pp, 2));
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
    
    ASSERT_TRUE(ERROR_SUCCESS == enc.write_metadata(18, md, 8));
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

/**
* test the stream utility, bytes from/to basic types.
*/
VOID TEST(KernelStreamTest, StreamInitialize)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_TRUE(ERROR_SUCCESS != s.initialize(NULL, 1024));
    EXPECT_TRUE(ERROR_SUCCESS != s.initialize(data, 0));
    EXPECT_TRUE(ERROR_SUCCESS != s.initialize(data, -1));
}

/**
* test the stream utility, access data
*/
VOID TEST(KernelStreamTest, StreamData)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(s.data() == NULL);
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_TRUE(s.data() == data);
}

/**
* test the stream utility, access size
*/
VOID TEST(KernelStreamTest, StreamSize)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(s.size() == 0);
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_TRUE(s.size() == 1024);
}

/**
* test the stream utility, access pos
*/
VOID TEST(KernelStreamTest, StreamPos)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(s.pos() == 0);
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_TRUE(s.pos() == 0);
    
    s.read_bytes(data, 1024);
    EXPECT_TRUE(s.pos() == 1024);
}

/**
* test the stream utility, access empty
*/
VOID TEST(KernelStreamTest, StreamEmpty)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_FALSE(s.empty());
    
    s.read_bytes(data, 1024);
    EXPECT_TRUE(s.empty());
}

/**
* test the stream utility, access require
*/
VOID TEST(KernelStreamTest, StreamRequire)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_FALSE(s.require(1));
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_TRUE(s.require(1));
    EXPECT_TRUE(s.require(1024));
    
    s.read_bytes(data, 1000);
    EXPECT_TRUE(s.require(1));
    
    s.read_bytes(data, 24);
    EXPECT_FALSE(s.require(1));
}

/**
* test the stream utility, skip bytes
*/
VOID TEST(KernelStreamTest, StreamSkip)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    EXPECT_EQ(0, s.pos());
    
    s.skip(1);
    EXPECT_EQ(1, s.pos());

    s.skip(-1);
    EXPECT_EQ(0 , s.pos());
}

/**
* test the stream utility, read 1bytes
*/
VOID TEST(KernelStreamTest, StreamRead1Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    data[0] = 0x12;
    data[99] = 0x13;
    data[100] = 0x14;
    data[101] = 0x15;
    EXPECT_EQ(0x12, s.read_1bytes());
    
    s.skip(-1 * s.pos());
    s.skip(100);
    EXPECT_EQ(0x14, s.read_1bytes());
}

/**
* test the stream utility, read 2bytes
*/
VOID TEST(KernelStreamTest, StreamRead2Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    data[0] = 0x01;
    data[1] = 0x02;
    data[2] = 0x03;
    data[3] = 0x04;
    data[4] = 0x05;
    data[5] = 0x06;
    data[6] = 0x07;
    data[7] = 0x08;
    data[8] = 0x09;
    data[9] = 0x0a;
    
    EXPECT_EQ(0x0102, s.read_2bytes());
    EXPECT_EQ(0x0304, s.read_2bytes());

    s.skip(-1 * s.pos());
    s.skip(3);
    EXPECT_EQ(0x0405, s.read_2bytes());
}

/**
* test the stream utility, read 3bytes
*/
VOID TEST(KernelStreamTest, StreamRead3Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    data[0] = 0x01;
    data[1] = 0x02;
    data[2] = 0x03;
    data[3] = 0x04;
    data[4] = 0x05;
    data[5] = 0x06;
    data[6] = 0x07;
    data[7] = 0x08;
    data[8] = 0x09;
    data[9] = 0x0a;
    
    EXPECT_EQ(0x010203, s.read_3bytes());
    EXPECT_EQ(0x040506, s.read_3bytes());

    s.skip(-1 * s.pos());
    s.skip(5);
    EXPECT_EQ(0x060708, s.read_3bytes());
}

/**
* test the stream utility, read 4bytes
*/
VOID TEST(KernelStreamTest, StreamRead4Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    data[0] = 0x01;
    data[1] = 0x02;
    data[2] = 0x03;
    data[3] = 0x04;
    data[4] = 0x05;
    data[5] = 0x06;
    data[6] = 0x07;
    data[7] = 0x08;
    data[8] = 0x09;
    data[9] = 0x0a;
    
    EXPECT_EQ(0x01020304, s.read_4bytes());
    EXPECT_EQ(0x05060708, s.read_4bytes());

    s.skip(-1 * s.pos());
    s.skip(5);
    EXPECT_EQ(0x06070809, s.read_4bytes());
}

/**
* test the stream utility, read 8bytes
*/
VOID TEST(KernelStreamTest, StreamRead8Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    data[0] = 0x01;
    data[1] = 0x02;
    data[2] = 0x03;
    data[3] = 0x04;
    data[4] = 0x05;
    data[5] = 0x06;
    data[6] = 0x07;
    data[7] = 0x08;
    data[8] = 0x09;
    data[9] = 0x0a;
    data[10] = 0x0b;
    data[11] = 0x0c;
    data[12] = 0x0d;
    data[13] = 0x0e;
    data[14] = 0x0f;
    data[15] = 0x10;
    data[16] = 0x11;
    data[17] = 0x12;
    data[18] = 0x13;
    data[19] = 0x14;
    
    EXPECT_EQ(0x0102030405060708LL, s.read_8bytes());
    EXPECT_EQ(0x090a0b0c0d0e0f10LL, s.read_8bytes());

    s.skip(-1 * s.pos());
    s.skip(5);
    EXPECT_EQ(0x060708090a0b0c0dLL, s.read_8bytes());
}

/**
* test the stream utility, read string
*/
VOID TEST(KernelStreamTest, StreamReadString)
{
    SrsStream s;
    char data[] = "Hello, world!";
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, sizeof(data) - 1));
    
    string str = s.read_string(2);
    EXPECT_STREQ("He", str.c_str());
    
    str = s.read_string(2);
    EXPECT_STREQ("ll", str.c_str());
    
    s.skip(3);
    str = s.read_string(6);
    EXPECT_STREQ("world!", str.c_str());
    
    EXPECT_TRUE(s.empty());
}

/**
* test the stream utility, read bytes
*/
VOID TEST(KernelStreamTest, StreamReadBytes)
{
    SrsStream s;
    char data[] = "Hello, world!";
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, sizeof(data) - 1));
    
    char bytes[64];
    s.read_bytes(bytes, 2);
    bytes[2] = 0;
    EXPECT_STREQ("He", bytes);
    
    s.read_bytes(bytes, 2);
    bytes[2] = 0;
    EXPECT_STREQ("ll", bytes);
    
    s.skip(3);
    s.read_bytes(bytes, 6);
    bytes[6] = 0;
    EXPECT_STREQ("world!", bytes);
    
    EXPECT_TRUE(s.empty());
}

/**
* test the stream utility, write 1bytes
*/
VOID TEST(KernelStreamTest, StreamWrite1Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    s.write_1bytes(0x10);
    s.write_1bytes(0x11);
    s.write_1bytes(0x12);
    s.write_1bytes(0x13);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
}

/**
* test the stream utility, write 2bytes
*/
VOID TEST(KernelStreamTest, StreamWrite2Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    s.write_2bytes(0x1011);
    s.write_2bytes(0x1213);
    s.write_2bytes(0x1415);
    s.write_2bytes(0x1617);
    s.write_2bytes(0x1819);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
    s.skip(5);
    EXPECT_EQ(0x19, s.read_1bytes());
}

/**
* test the stream utility, write 3bytes
*/
VOID TEST(KernelStreamTest, StreamWrite3Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    s.write_3bytes(0x101112);
    s.write_3bytes(0x131415);
    s.write_3bytes(0x161718);
    s.write_3bytes(0x192021);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
    s.skip(5);
    EXPECT_EQ(0x19, s.read_1bytes());
}

/**
* test the stream utility, write 34bytes
*/
VOID TEST(KernelStreamTest, StreamWrite4Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    s.write_4bytes(0x10111213);
    s.write_4bytes(0x14151617);
    s.write_4bytes(0x18192021);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
    s.skip(5);
    EXPECT_EQ(0x19, s.read_1bytes());
}

/**
* test the stream utility, write 8bytes
*/
VOID TEST(KernelStreamTest, StreamWrite8Bytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    s.write_8bytes(0x1011121314151617LL);
    s.write_8bytes(0x1819202122232425LL);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
    s.skip(5);
    EXPECT_EQ(0x19, s.read_1bytes());
}

/**
* test the stream utility, write string
*/
VOID TEST(KernelStreamTest, StreamWriteString)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    char str[] = {
        (char)0x10, (char)0x11, (char)0x12, (char)0x13,
        (char)0x14, (char)0x15, (char)0x16, (char)0x17, 
        (char)0x18, (char)0x19, (char)0x20, (char)0x21
    };
    string str1;
    str1.append(str, 12);
    
    s.write_string(str1);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
    s.skip(5);
    EXPECT_EQ(0x19, s.read_1bytes());
}

/**
* test the stream utility, write bytes
*/
VOID TEST(KernelStreamTest, StreamWriteBytes)
{
    SrsStream s;
    char data[1024];
    
    EXPECT_TRUE(ERROR_SUCCESS == s.initialize(data, 1024));
    
    char str[] = {
        (char)0x10, (char)0x11, (char)0x12, (char)0x13,
        (char)0x14, (char)0x15, (char)0x16, (char)0x17, 
        (char)0x18, (char)0x19, (char)0x20, (char)0x21
    };
    
    s.write_bytes(str, 12);

    s.skip(-1 * s.pos());
    EXPECT_EQ(0x10, s.read_1bytes());
    s.skip(2);
    EXPECT_EQ(0x13, s.read_1bytes());
    s.skip(5);
    EXPECT_EQ(0x19, s.read_1bytes());
}

/**
* test the kernel utility, time
*/
VOID TEST(KernelUtilityTest, UtilityTime)
{
    int64_t time = srs_get_system_time_ms();
    EXPECT_TRUE(time > 0);
    
    int64_t time1 = srs_get_system_time_ms();
    EXPECT_EQ(time, time1);
    
    usleep(1000);
    srs_update_system_time_ms();
    time1 = srs_get_system_time_ms();
    EXPECT_TRUE(time1 > time);
}

/**
* test the kernel utility, startup time
*/
VOID TEST(KernelUtilityTest, UtilityStartupTime)
{
    int64_t time = srs_get_system_startup_time_ms();
    EXPECT_TRUE(time > 0);
    
    int64_t time1 = srs_get_system_startup_time_ms();
    EXPECT_EQ(time, time1);
    
    usleep(1000);
    srs_update_system_time_ms();
    time1 = srs_get_system_startup_time_ms();
    EXPECT_EQ(time, time1);
}

/**
* test the kernel utility, little endian
*/
VOID TEST(KernelUtilityTest, UtilityLittleEndian)
{
    EXPECT_TRUE(srs_is_little_endian());
}

/**
* test the kernel utility, string
*/
VOID TEST(KernelUtilityTest, UtilityString)
{
    string str = "Hello, World! Hello, SRS!";
    string str1;
    
    str1 = srs_string_replace(str, "xxx", "");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_replace(str, "He", "XX");
    EXPECT_STREQ("XXllo, World! XXllo, SRS!", str1.c_str());
    
    str1 = srs_string_replace(str, "o", "XX");
    EXPECT_STREQ("HellXX, WXXrld! HellXX, SRS!", str1.c_str());
    
    str1 = srs_string_trim_end(str, "x");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_trim_end(str, "He");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_trim_end(str, "HeS!R");
    EXPECT_STREQ("Hello, World! Hello, ", str1.c_str());
    
    str1 = srs_string_remove(str, "x");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_remove(str, "o");
    EXPECT_STREQ("Hell, Wrld! Hell, SRS!", str1.c_str());
    
    str1 = srs_string_remove(str, "ol");
    EXPECT_STREQ("He, Wrd! He, SRS!", str1.c_str());
    
    EXPECT_FALSE(srs_string_ends_with("Hello", "x"));
    EXPECT_TRUE(srs_string_ends_with("Hello", "o"));
    EXPECT_TRUE(srs_string_ends_with("Hello", "lo"));
}

#endif

