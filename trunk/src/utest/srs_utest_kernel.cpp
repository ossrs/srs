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

MockSrsFileWriter::MockSrsFileWriter()
{
}

MockSrsFileWriter::~MockSrsFileWriter()
{
}

int MockSrsFileWriter::open(string file)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

void MockSrsFileWriter::close()
{
    int ret = ERROR_SUCCESS;
    return;
}

bool MockSrsFileWriter::is_open()
{
    return true;
}

int64_t MockSrsFileWriter::tellg()
{
    return 0;
}

int MockSrsFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

MockSrsFileReader::MockSrsFileReader()
{
}

MockSrsFileReader::~MockSrsFileReader()
{
}

int MockSrsFileReader::open(string file)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

void MockSrsFileReader::close()
{
    int ret = ERROR_SUCCESS;
    return;
}

bool MockSrsFileReader::is_open()
{
    return true;
}

int64_t MockSrsFileReader::tellg()
{
    return 0;
}

void MockSrsFileReader::skip(int64_t size)
{
}

int64_t MockSrsFileReader::lseek(int64_t offset)
{
    return offset;
}

int64_t MockSrsFileReader::filesize()
{
    return 0;
}

int MockSrsFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

VOID TEST(KernelCodecTest, IsKeyFrame)
{
    int8_t data;
    
    data = 0x10;
    EXPECT_TRUE(SrsFlvCodec::video_is_keyframe(&data, 1));
    EXPECT_FALSE(SrsFlvCodec::video_is_keyframe(&data, 0));
    
    data = 0x20;
    EXPECT_FALSE(SrsFlvCodec::video_is_keyframe(&data, 1));
}

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

VOID TEST(KernelFlvTest, IsAudioSequenceHeader)
{
    MockSrsFileWriter fs;
    SrsFlvEncoder enc;
    ASSERT_TRUE(ERROR_SUCCESS == enc.initialize(&fs));
}
