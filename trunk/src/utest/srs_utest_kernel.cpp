//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_kernel.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_core_autofree.hpp>

#define MAX_MOCK_DATA_SIZE 1024 * 1024

MockSrsFile::MockSrsFile()
{
    _buf = new SrsBuffer(_data.bytes(), _data.length());
}

MockSrsFile::~MockSrsFile()
{
    close();
}

srs_error_t MockSrsFile::open(std::string /*file*/)
{
    srs_error_t err = srs_success;
    return err;
}

void MockSrsFile::close()
{
    srs_freep(_buf);
}

srs_error_t MockSrsFile::write(void* data, size_t count, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;

    for (;;) {
        if (_buf->left() >= (int)count) {
            _buf->write_bytes((char*)data, count);
            break;
        }

        // Append enough data and rebuild the buffer.
        off_t pos = _buf->pos();
        _data.append((const char*)data, count - _buf->left());
        srs_freep(_buf);
        _buf = new SrsBuffer(_data.bytes(), _data.length());
        lseek(pos, SEEK_SET, NULL);
    }

    if (pnwrite) {
        *pnwrite = count;
    }

    return err;
}

srs_error_t MockSrsFile::read(void* data, size_t count, ssize_t* pnread)
{
    srs_error_t err = srs_success;

    if (_buf->left() == 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_EOF, "EOF");
    }

    int limit = srs_min(_buf->left(), (int)count);

    if (limit <= 0) {
        return srs_error_new(-1, "EOF");
    }

    _buf->read_bytes((char*)data, limit);

    if (pnread) {
        *pnread = limit;
    }

    return err;
}

srs_error_t MockSrsFile::lseek(off_t offset, int whence, off_t* seeked)
{
    srs_error_t err = srs_success;

    if (whence == SEEK_SET) {
        if (offset > _data.length()) {
            return srs_error_new(-1, "Overflow");
        }
        if (_buf->data()) {
            _buf->skip(offset - _buf->pos());
        }
    } else if (whence == SEEK_CUR) {
    } else if (whence == SEEK_END) {
        if (_buf->data()) {
            _buf->skip(_buf->left());
        }
    }

    if (seeked) {
        *seeked = (off_t)_buf->pos();
    }
    return err;
}

MockSrsFileWriter::MockSrsFileWriter()
{
    err = srs_success;
    error_offset = -1;
    opened = false;
    uf = new MockSrsFile();
}

MockSrsFileWriter::~MockSrsFileWriter()
{
    srs_freep(err);
    srs_freep(uf);
}

srs_error_t MockSrsFileWriter::open(string file)
{
    if (err != srs_success) {
        return srs_error_copy(err);
    }
    opened = true;
    return uf->open(file);
}

void MockSrsFileWriter::close()
{
    uf->close();
}

bool MockSrsFileWriter::is_open()
{
    return opened;
}

void MockSrsFileWriter::seek2(int64_t offset)
{
    lseek(offset, SEEK_SET, NULL);
}

int64_t MockSrsFileWriter::tellg()
{
    off_t offset = 0;
    lseek(0, SEEK_CUR, &offset);
    return (int64_t)offset;
}

int64_t MockSrsFileWriter::filesize()
{
    int64_t cur = tellg();

    off_t offset = 0;
    lseek(0, SEEK_END, &offset);

    seek2(cur);
    return offset;
}

char* MockSrsFileWriter::data()
{
    return uf->_data.bytes();
}

string MockSrsFileWriter::str()
{
    int size = filesize();
    if (size == 0) {
        return "";
    }

    return string(data(), size);
}

srs_error_t MockSrsFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    if (err != srs_success) {
        return srs_error_copy(err);
    }

    if (error_offset >= 0 && tellg() >= error_offset) {
        return srs_error_new(-1, "overflow");
    }

    return uf->write(buf, count, pnwrite);
}

srs_error_t MockSrsFileWriter::lseek(off_t offset, int whence, off_t* seeked)
{
    if (error_offset >= 0 && offset > error_offset) {
        return srs_error_new(-1, "overflow");
    }
    return uf->lseek(offset, whence, seeked);
}

void MockSrsFileWriter::mock_reset_offset()
{
    seek2(0);
}

MockSrsFileReader::MockSrsFileReader()
{
    opened = false;
    seekable = true;
    uf = new MockSrsFile();
}

MockSrsFileReader::MockSrsFileReader(const char* src, int nb_src)
{
    opened = false;
    seekable = true;
    uf = new MockSrsFile();

    srs_error_t err = uf->write((void*)src, nb_src, NULL);
    srs_freep(err);

    err = uf->lseek(0, SEEK_SET, NULL);
    srs_freep(err);
}

MockSrsFileReader::~MockSrsFileReader()
{
    srs_freep(uf);
}

srs_error_t MockSrsFileReader::open(string file)
{
    opened = true;
    return uf->open(file);
}

void MockSrsFileReader::close()
{
    uf->close();
}

bool MockSrsFileReader::is_open()
{
    return opened;
}

int64_t MockSrsFileReader::tellg()
{
    off_t offset = 0;
    srs_error_t err = lseek(0, SEEK_CUR, &offset);
    srs_freep(err);

    return offset;
}

void MockSrsFileReader::skip(int64_t _size)
{
    int64_t offset = tellg() + _size;
    srs_error_t err = lseek(offset, SEEK_SET, NULL);
    srs_freep(err);
}

int64_t MockSrsFileReader::seek2(int64_t _offset)
{
    off_t offset = 0;
    srs_error_t err = lseek(_offset, SEEK_SET, &offset);
    srs_freep(err);

    return offset;
}

int64_t MockSrsFileReader::filesize()
{
    int64_t cur = tellg();

    off_t offset = 0;
    srs_error_t err = lseek(0, SEEK_END, &offset);
    srs_freep(err);

    seek2(cur);
    return offset;
}

srs_error_t MockSrsFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    return uf->read(buf, count, pnread);
}

srs_error_t MockSrsFileReader::lseek(off_t offset, int whence, off_t* seeked)
{
    if (!seekable) {
        return srs_error_new(-1, "unseekable");
    }
    return uf->lseek(offset, whence, seeked);
}

void MockSrsFileReader::mock_append_data(const char* data, int size)
{
    srs_error_t err = uf->write((void*)data, size, NULL);
    srs_freep(err);
}

void MockSrsFileReader::mock_reset_offset()
{
    seek2(0);
}

MockBufferReader::MockBufferReader(const char* data)
{
    str = data;
}

MockBufferReader::~MockBufferReader()
{
}

srs_error_t MockBufferReader::read(void* buf, size_t size, ssize_t* nread)
{
    if (str.empty()) {
        return srs_error_new(ERROR_SYSTEM_FILE_EOF, "EOF");
    }

    int len = srs_min(str.length(), size);
    if (len == 0) {
        return srs_error_new(-1, "no data");
    }

    memcpy(buf, str.data(), len);
    str = str.substr(len);
    
    if (nread) {
        *nread = len;
    }

    return srs_success;
}

MockSrsCodec::MockSrsCodec()
{
}

MockSrsCodec::~MockSrsCodec()
{
}

uint64_t MockSrsCodec::nb_bytes()
{
    return 0;
}

srs_error_t MockSrsCodec::encode(SrsBuffer* /*buf*/)
{
    return srs_success;
}

srs_error_t MockSrsCodec::decode(SrsBuffer* /*buf*/)
{
    return srs_success;
}

MockTsHandler::MockTsHandler()
{
    msg = NULL;
}

MockTsHandler::~MockTsHandler()
{
    srs_freep(msg);
}

srs_error_t MockTsHandler::on_ts_message(SrsTsMessage* m)
{
    srs_freep(msg);
    msg = m->detach();

    return srs_success;
}

MockPsHandler::MockPsHandler()
{
}

MockPsHandler::~MockPsHandler()
{
    clear();
}

srs_error_t MockPsHandler::on_ts_message(SrsTsMessage* m)
{
    msgs_.push_back(m->detach());
    return srs_success;
}

void MockPsHandler::on_recover_mode(int nn_recover)
{
}

void MockPsHandler::on_recover_done(srs_utime_t duration)
{
}

MockPsHandler* MockPsHandler::clear()
{
    for (vector<SrsTsMessage*>::iterator it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsTsMessage* msg = *it;
        srs_freep(msg);
    }

    msgs_.clear();
    return this;
}

void mock_print_err(srs_error_t err)
{
    fprintf(stderr, "err %s\n", srs_error_desc(err).c_str());
    srs_freep(err);
}

void mock_print_mp4(string data)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fr(data.data(), data.length());

    SrsMp4BoxReader br;
    if ((err = br.initialize(&fr)) != srs_success) {
        mock_print_err(srs_error_wrap(err, "init br"));
        return;
    }

    SrsUniquePtr<SrsSimpleStream> stream(new SrsSimpleStream());

    while (true) {
        SrsMp4Box* box = NULL;
        if ((err = br.read(stream.get(), &box)) != srs_success) {
            if (srs_error_code(err) != ERROR_SYSTEM_FILE_EOF) {
                mock_print_err(srs_error_wrap(err, "read"));
            }
            return;
        }
        SrsUniquePtr<SrsMp4Box> box_uptr(box);

        SrsBuffer* buffer = new SrsBuffer(stream->bytes(), stream->length());
        SrsUniquePtr<SrsBuffer> buffer_uptr(buffer);

        if ((err = box->decode(buffer)) != srs_success) {
            mock_print_err(srs_error_wrap(err, "decode"));
            return;
        }

        if ((err = br.skip(box, stream.get())) != srs_success) {
            mock_print_err(srs_error_wrap(err, "skip"));
            return;
        }

        SrsMp4DumpContext ctx;
        ctx.level = 1;
        ctx.summary = true;

        stringstream ss;
        fprintf(stderr, "%s", box->dumps(ss, ctx).str().c_str());
    }
}

VOID TEST(KernelBufferTest, DefaultObject)
{
    SrsSimpleStream b;
    
    EXPECT_EQ(0, b.length());
    EXPECT_EQ(NULL, b.bytes());
}

VOID TEST(KernelBufferTest, AppendBytes)
{
    SrsSimpleStream b;
    
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
    SrsSimpleStream b;
    
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
    srs_error_t err;

    if(true) {
        SrsFastStream b(5);
        MockBufferReader r("Hello, world!");

        HELPER_ASSERT_SUCCESS(b.grow(&r, 5));
        b.skip(2);

        HELPER_ASSERT_FAILED(b.grow(&r, 6));
    }

    if(true) {
        SrsFastStream b(5);
        MockBufferReader r("Hello, world!");

        HELPER_ASSERT_SUCCESS(b.grow(&r, 5));
        b.skip(5);

        HELPER_ASSERT_FAILED(b.grow(&r, 6));
    }

    if(true) {
        SrsFastStream b(6);
        MockBufferReader r("Hello, world!");

        HELPER_ASSERT_SUCCESS(b.grow(&r, 5));
        EXPECT_EQ('H', b.read_1byte()); EXPECT_EQ('e', b.read_1byte()); EXPECT_EQ('l', b.read_1byte());
        b.skip(2);

        HELPER_ASSERT_SUCCESS(b.grow(&r, 2));
        b.skip(2);

        HELPER_ASSERT_SUCCESS(b.grow(&r, 5));
        EXPECT_EQ('w', b.read_1byte()); EXPECT_EQ('o', b.read_1byte()); EXPECT_EQ('r', b.read_1byte());
        b.skip(2);
    }

    if(true) {
        SrsFastStream b(5);
        MockBufferReader r("Hello, world!");

        HELPER_ASSERT_SUCCESS(b.grow(&r, 5));
        EXPECT_EQ('H', b.read_1byte()); EXPECT_EQ('e', b.read_1byte()); EXPECT_EQ('l', b.read_1byte());
        b.skip(2);

        HELPER_ASSERT_SUCCESS(b.grow(&r, 2));
        b.skip(2);

        HELPER_ASSERT_SUCCESS(b.grow(&r, 5));
        EXPECT_EQ('w', b.read_1byte()); EXPECT_EQ('o', b.read_1byte()); EXPECT_EQ('r', b.read_1byte());
        b.skip(2);
    }

    if (true) {
        SrsFastStream b;
        MockBufferReader r("winlin");

        HELPER_ASSERT_SUCCESS(b.grow(&r, 1));
        EXPECT_EQ('w', b.read_1byte());

        HELPER_ASSERT_SUCCESS(b.grow(&r, 3));
        b.skip(1);
        EXPECT_EQ('n', b.read_1byte());

        HELPER_ASSERT_FAILED(b.grow(&r, 100));
    }
}

/**
* test the codec,
* whether H.264 keyframe
*/
VOID TEST(KernelCodecTest, IsKeyFrame)
{
    char data;
    
    data = 0x10;
    EXPECT_TRUE(SrsFlvVideo::keyframe(&data, 1));
    EXPECT_FALSE(SrsFlvVideo::keyframe(&data, 0));
    
    data = 0x20;
    EXPECT_FALSE(SrsFlvVideo::keyframe(&data, 1));
}

/**
* test the codec,
* check whether H.264 video
*/
VOID TEST(KernelCodecTest, IsH264)
{
    char data;
    
    EXPECT_FALSE(SrsFlvVideo::h264(&data, 0));
    
    data = 0x17;
    EXPECT_TRUE(SrsFlvVideo::h264(&data, 1));
    
    data = 0x07;
    EXPECT_TRUE(SrsFlvVideo::h264(&data, 1));
    
    data = 0x08;
    EXPECT_FALSE(SrsFlvVideo::h264(&data, 1));
}

/**
* test the codec,
* whether H.264 video sequence header
*/
VOID TEST(KernelCodecTest, IsSequenceHeader)
{
    int16_t data;
    char* pp = (char*)&data;
    
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 0));
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 1));
    
    pp[0] = 0x17;
    pp[1] = 0x00;
    EXPECT_TRUE(SrsFlvVideo::sh((char*)pp, 2));
    pp[0] = 0x18;
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 2));
    pp[0] = 0x27;
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 2));
    pp[0] = 0x17;
    pp[1] = 0x01;
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 2));
}

/**
* test the codec,
* check whether AAC codec
*/
VOID TEST(KernelCodecTest, IsAAC)
{
    char data;
    
    EXPECT_FALSE(SrsFlvAudio::aac(&data, 0));
    
    data = 0xa0;
    EXPECT_TRUE(SrsFlvAudio::aac(&data, 1));
    
    data = 0xa7;
    EXPECT_TRUE(SrsFlvAudio::aac(&data, 1));
    
    data = 0x00;
    EXPECT_FALSE(SrsFlvAudio::aac(&data, 1));
}

/**
* test the codec,
* AAC audio sequence header
*/
VOID TEST(KernelCodecTest, IsAudioSequenceHeader)
{
    int16_t data;
    char* pp = (char*)&data;
    
    EXPECT_FALSE(SrsFlvAudio::sh((char*)pp, 0));
    EXPECT_FALSE(SrsFlvAudio::sh((char*)pp, 1));
    
    pp[0] = 0xa0;
    pp[1] = 0x00;
    EXPECT_TRUE(SrsFlvAudio::sh((char*)pp, 2));
    pp[0] = 0x00;
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 2));
    pp[0] = 0xa0;
    pp[1] = 0x01;
    EXPECT_FALSE(SrsFlvVideo::sh((char*)pp, 2));
}

/**
* test the flv encoder,
* exception: file stream not open
*/
VOID TEST(KernelFlvTest, FlvEncoderStreamClosed)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter fs;
    SrsFlvTransmuxer enc;
    // The decoder never check the reader status.
    HELPER_ASSERT_SUCCESS(enc.initialize(&fs));
}

/**
* test the flv encoder,
* write flv header
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteHeader)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter fs;
    SrsFlvTransmuxer enc;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(enc.initialize(&fs));
    
    // write header, 9bytes
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x05, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    
    HELPER_EXPECT_SUCCESS(enc.write_header());
    ASSERT_TRUE(9 + 4 == fs.tellg());
    
    EXPECT_TRUE(srs_bytes_equals(flv_header, fs.data(), 9));
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data() + 9, 4));

    // customer header
    flv_header[3] = 0xF0;
    flv_header[4] = 0xF1;
    flv_header[5] = 0x01;
    
    fs.mock_reset_offset();
    
    HELPER_EXPECT_SUCCESS(enc.write_header(flv_header));
    ASSERT_TRUE(9 + 4 == fs.tellg());
    
    EXPECT_TRUE(srs_bytes_equals(flv_header, fs.data(), 9));
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data() + 9, 4));
}

/**
* test the flv encoder,
* write metadata tag
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteMetadata)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter fs;
    HELPER_EXPECT_SUCCESS(fs.open(""));
    SrsFlvTransmuxer enc;
    HELPER_ASSERT_SUCCESS(enc.initialize(&fs));
    
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
    
    HELPER_ASSERT_SUCCESS(enc.write_metadata(18, md, 8));
    ASSERT_TRUE(11 + 8 + 4 == fs.tellg());
    
    EXPECT_TRUE(srs_bytes_equals(tag_header, fs.data(), 11));
    EXPECT_TRUE(srs_bytes_equals(md, fs.data() + 11, 8));
    EXPECT_TRUE(true); // donot know why, if not add it, the print is disabled.
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data() + 19, 4));
}

/**
* test the flv encoder,
* write audio tag
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteAudio)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter fs;
    SrsFlvTransmuxer enc;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(enc.initialize(&fs));
    
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
    
    HELPER_ASSERT_SUCCESS(enc.write_audio(0x30, audio, 8));
    ASSERT_TRUE(11 + 8 + 4 == fs.tellg());
    
    EXPECT_TRUE(srs_bytes_equals(tag_header, fs.data(), 11));
    EXPECT_TRUE(srs_bytes_equals(audio, fs.data() + 11, 8));
    EXPECT_TRUE(true); // donot know why, if not add it, the print is disabled.
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data() + 11 + 8, 4));
}

/**
* test the flv encoder,
* write video tag
*/
VOID TEST(KernelFlvTest, FlvEncoderWriteVideo)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter fs;
    SrsFlvTransmuxer enc;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(enc.initialize(&fs));
    
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
    
    HELPER_ASSERT_SUCCESS(enc.write_video(0x30, video, 8));
    ASSERT_TRUE(11 + 8 + 4 == fs.tellg());
    
    EXPECT_TRUE(srs_bytes_equals(tag_header, fs.data(), 11));
    EXPECT_TRUE(srs_bytes_equals(video, fs.data() + 11, 8));
    EXPECT_TRUE(true); // donot know why, if not add it, the print is disabled.
    EXPECT_TRUE(srs_bytes_equals(pts, fs.data() + 11 + 8, 4));
}

/**
* test the flv encoder,
* calc the tag size.
*/
VOID TEST(KernelFlvTest, FlvEncoderSizeTag)
{
    EXPECT_EQ(11+4+10, SrsFlvTransmuxer::size_tag(10));
    EXPECT_EQ(11+4+0, SrsFlvTransmuxer::size_tag(0));
}

VOID TEST(KernelFLVTest, CoverWriterErrorCase)
{
	srs_error_t err;

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));
		HELPER_EXPECT_SUCCESS(m.write_header());
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));
		HELPER_EXPECT_FAILED(m.write_header());
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 9;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));
		HELPER_EXPECT_FAILED(m.write_header());
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));
		HELPER_EXPECT_FAILED(m.write_metadata(0, NULL, 0));
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));
		HELPER_EXPECT_FAILED(m.write_audio(0, NULL, 0));
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));
		HELPER_EXPECT_FAILED(m.write_video(0, NULL, 0));
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));

        SrsMessageHeader h;
        h.initialize_video(10, 30, 20);
        SrsSharedPtrMessage msg;
        HELPER_EXPECT_SUCCESS(msg.create(&h, new char[1], 1));

        SrsSharedPtrMessage* msgs = &msg;
		HELPER_EXPECT_FAILED(m.write_tags(&msgs, 1));
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));

        SrsMessageHeader h;
        h.initialize_audio(10, 30, 20);
        SrsSharedPtrMessage msg;
        HELPER_EXPECT_SUCCESS(msg.create(&h, new char[1], 1));

        SrsSharedPtrMessage* msgs = &msg;
		HELPER_EXPECT_FAILED(m.write_tags(&msgs, 1));
	}

	if (true) {
		MockSrsFileWriter w;
		HELPER_EXPECT_SUCCESS(w.open(""));
		w.error_offset = 0;

		SrsFlvTransmuxer m;
		HELPER_EXPECT_SUCCESS(m.initialize(&w));

        SrsMessageHeader h;
        h.initialize_amf0_script(10, 20);
        SrsSharedPtrMessage msg;
        HELPER_EXPECT_SUCCESS(msg.create(&h, new char[1], 1));

        SrsSharedPtrMessage* msgs = &msg;
		HELPER_EXPECT_FAILED(m.write_tags(&msgs, 1));
	}
}

VOID TEST(KernelFLVTest, CoverReaderErrorCase)
{
	srs_error_t err;

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		char header[9];
		HELPER_EXPECT_FAILED(d.read_header(header));
	}

	if (true) {
		char header[9] = {'T', 'E', 'S', 'T', 0, 0, 0, 0, 0};
		MockSrsFileReader r(header, sizeof(header));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		HELPER_EXPECT_FAILED(d.read_header(header));
	}

	if (true) {
		char header[9] = {'F', 'E', 'S', 'T', 0, 0, 0, 0, 0};
		MockSrsFileReader r(header, sizeof(header));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		HELPER_EXPECT_FAILED(d.read_header(header));
	}

	if (true) {
		char header[9] = {'F', 'L', 'S', 'T', 0, 0, 0, 0, 0};
		MockSrsFileReader r(header, sizeof(header));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		HELPER_EXPECT_FAILED(d.read_header(header));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		char ptype;
		int32_t pdata_size;
		uint32_t ptime;
		HELPER_EXPECT_FAILED(d.read_tag_header(&ptype, &pdata_size, &ptime));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		char data[9];
		HELPER_EXPECT_FAILED(d.read_tag_data(data, 9));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		char data[4];
		HELPER_EXPECT_FAILED(d.read_previous_tag_size(data));
	}
}

VOID TEST(KernelFLVTest, CoverVodStreamErrorCase)
{
	srs_error_t err;

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		char header[13];
		HELPER_EXPECT_FAILED(d.read_header_ext(header));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		int64_t start;
		int size;
		HELPER_EXPECT_FAILED(d.read_sequence_header_summary(&start, &size));
	}

	if (true) {
		MockSrsFileReader r;
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		HELPER_EXPECT_FAILED(d.seek2(1));
	}
}

VOID TEST(KernelFLVTest, CoverVodStreamErrorCase2)
{
    srs_error_t err;

	if (true) {
		MockSrsFileReader r("HELLO", 5);
		HELPER_EXPECT_SUCCESS(r.open(""));
		r.seekable = false;

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		HELPER_EXPECT_FAILED(d.seek2(1));
	}
}

VOID TEST(KernelFLVTest, CoverFLVVodSHCase)
{
	srs_error_t err;

	if (true) {
		uint8_t buf[] = {
			0x12, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x09, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x08, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00
		};

		MockSrsFileReader r((const char*)buf, sizeof(buf));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		int64_t start = -1; int size = 0;
		HELPER_EXPECT_SUCCESS(d.read_sequence_header_summary(&start, &size));
		EXPECT_EQ(15, start);
		EXPECT_EQ(30, size);
	}

	if (true) {
		uint8_t buf[] = {
			0x12, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x09, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x09, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00
		};

		MockSrsFileReader r((const char*)buf, sizeof(buf));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		int64_t start = -1; int size = 0;
		HELPER_EXPECT_SUCCESS(d.read_sequence_header_summary(&start, &size));
		EXPECT_EQ(15, start);
		EXPECT_EQ(15, size);
	}

	if (true) {
		uint8_t buf[] = {
			0x12, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x08, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x08, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00
		};

		MockSrsFileReader r((const char*)buf, sizeof(buf));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		int64_t start = -1; int size = 0;
		HELPER_EXPECT_SUCCESS(d.read_sequence_header_summary(&start, &size));
		EXPECT_EQ(15, start);
		EXPECT_EQ(15, size);
	}

	if (true) {
		uint8_t buf[] = {
			0x12, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x12, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
			0x12, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,0x00
		};

		MockSrsFileReader r((const char*)buf, sizeof(buf));
		HELPER_EXPECT_SUCCESS(r.open(""));

		SrsFlvVodStreamDecoder d;
		HELPER_EXPECT_SUCCESS(d.initialize(&r));

		int64_t start = -1; int size = 0;
		HELPER_EXPECT_FAILED(d.read_sequence_header_summary(&start, &size));
		EXPECT_EQ(-1, start);
		EXPECT_EQ(0, size);
	}
}

/**
* test the flv decoder,
* exception: file stream not open.
*/
VOID TEST(KernelFlvTest, FlvDecoderStreamClosed)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    // The decoder never check the reader status.
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
}

/**
* test the flv decoder,
* decode flv header
*/
VOID TEST(KernelFlvTest, FlvDecoderHeader)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    
    HELPER_EXPECT_SUCCESS(dec.read_header(data));
    EXPECT_TRUE(srs_bytes_equals(flv_header, data, 9));
    
    HELPER_EXPECT_SUCCESS(dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv decoder,
* decode metadata tag
*/
VOID TEST(KernelFlvTest, FlvDecoderMetadata)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    uint32_t time = 0;
    char data[1024];
    fs.mock_reset_offset();
    
    HELPER_EXPECT_SUCCESS(dec.read_tag_header(&type, &size, &time));
    EXPECT_TRUE(18 == type);
    EXPECT_TRUE(8 == size);
    EXPECT_TRUE(0 == time);
    
    HELPER_EXPECT_SUCCESS(dec.read_tag_data(data, size));
    EXPECT_TRUE(srs_bytes_equals(md, data, 8));
    
    HELPER_EXPECT_SUCCESS(dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv decoder,
* decode audio tag
*/
VOID TEST(KernelFlvTest, FlvDecoderAudio)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    uint32_t time = 0;
    char data[1024];
    fs.mock_reset_offset();
    
    HELPER_EXPECT_SUCCESS(dec.read_tag_header(&type, &size, &time));
    EXPECT_TRUE(8 == type);
    EXPECT_TRUE(8 == size);
    EXPECT_TRUE(0x30 == time);
    
    HELPER_EXPECT_SUCCESS(dec.read_tag_data(data, size));
    EXPECT_TRUE(srs_bytes_equals(audio, data, 8));
    
    HELPER_EXPECT_SUCCESS(dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv decoder,
* decode video tag
*/
VOID TEST(KernelFlvTest, FlvDecoderVideo)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    uint32_t time = 0;
    char data[1024];
    fs.mock_reset_offset();
    
    HELPER_EXPECT_SUCCESS(dec.read_tag_header(&type, &size, &time));
    EXPECT_TRUE(9 == type);
    EXPECT_TRUE(8 == size);
    EXPECT_TRUE(0x30 == time);
    
    HELPER_EXPECT_SUCCESS(dec.read_tag_data(data, size));
    EXPECT_TRUE(srs_bytes_equals(video, data, 8));
    
    HELPER_EXPECT_SUCCESS(dec.read_previous_tag_size(data));
    EXPECT_TRUE(srs_bytes_equals(pts, data, 4));
}

/**
* test the flv vod stream decoder,
* exception: file stream not open.
*/
VOID TEST(KernelFlvTest, FlvVSDecoderStreamClosed)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    fs.close();

    SrsFlvVodStreamDecoder dec;
    HELPER_ASSERT_FAILED(dec.initialize(&fs));
}

/**
* test the flv vod stream decoder,
* decode the flv header
*/
VOID TEST(KernelFlvTest, FlvVSDecoderHeader)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    
    HELPER_EXPECT_SUCCESS(dec.read_header_ext(data));
    EXPECT_TRUE(srs_bytes_equals(flv_header, data, 9));
}

/**
* test the flv vod stream decoder,
* get the start offset and size of sequence header
* mock data: metadata-audio-video
*/
VOID TEST(KernelFlvTest, FlvVSDecoderSequenceHeader)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    HELPER_EXPECT_SUCCESS(dec.read_sequence_header_summary(&start, &size));
    EXPECT_EQ(23, (int)start);
    EXPECT_EQ(46, size);
}

/**
* test the flv vod stream decoder,
* get the start offset and size of sequence header
* mock data: metadata-video-audio
*/
VOID TEST(KernelFlvTest, FlvVSDecoderSequenceHeader2)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
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
    HELPER_EXPECT_SUCCESS(dec.read_sequence_header_summary(&start, &size));
    EXPECT_EQ(23, (int)start);
    EXPECT_EQ(46, size);
}

/**
* test the flv vod stream decoder,
* seek stream after got the offset and start of flv sequence header,
* to directly response flv data by seek to the offset of file.
*/
VOID TEST(KernelFlvTest, FlvVSDecoderSeek)
{
    srs_error_t err = srs_success;

    MockSrsFileReader fs;
    SrsFlvVodStreamDecoder dec;
    HELPER_ASSERT_SUCCESS(fs.open(""));
    HELPER_ASSERT_SUCCESS(dec.initialize(&fs));
    
    // 11bytes tag header
    char tag_header[] = {
        (char)8, // TagType UB [5], 8 = audio
        (char)0x00, (char)0x00, (char)0x08, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x30, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    fs.mock_append_data(tag_header, 11);
    EXPECT_TRUE(11 == fs.tellg());

    HELPER_EXPECT_SUCCESS(dec.seek2(0));
    EXPECT_TRUE(0 == fs.tellg());

    HELPER_EXPECT_SUCCESS(dec.seek2(5));
    EXPECT_TRUE(5 == fs.tellg());
}

VOID TEST(KernelFLVTest, CoverFLVVodError)
{
	srs_error_t err;

	if (true) {
	    SrsFlvVodStreamDecoder dec;
	    HELPER_EXPECT_FAILED(dec.initialize((ISrsReader*)&dec));
    }

	if (true) {
	    MockSrsFileReader fs;
	    SrsFlvVodStreamDecoder dec;
	    HELPER_EXPECT_FAILED(dec.initialize(&fs));
    }
}

/**
* test the stream utility, access pos
*/
VOID TEST(KernelStreamTest, StreamPos)
{
    char data[1024];
    SrsBuffer s(data, 1024);
    EXPECT_TRUE(s.pos() == 0);
    
    s.read_bytes(data, 1024);
    EXPECT_TRUE(s.pos() == 1024);
}

/**
* test the stream utility, access empty
*/
VOID TEST(KernelStreamTest, StreamEmpty)
{
    char data[1024];
    SrsBuffer s(data, 1024);
    EXPECT_FALSE(s.empty());
    
    s.read_bytes(data, 1024);
    EXPECT_TRUE(s.empty());
}

/**
* test the stream utility, access require
*/
VOID TEST(KernelStreamTest, StreamRequire)
{
    char data[1024];
    SrsBuffer s(data, 1024);
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
    char data[1024];
    SrsBuffer s(data, 1024);
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[] = "Hello, world!";
    SrsBuffer s(data, sizeof(data) - 1);
    
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
    char data[] = "Hello, world!";
    SrsBuffer s(data, sizeof(data) - 1);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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
    char data[1024];
    SrsBuffer s(data, 1024);
    
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

VOID TEST(KernelBufferTest, CoverAll)
{
	srs_error_t err;

    if (true) {
        MockSrsCodec codec;
        EXPECT_TRUE(0 == codec.nb_bytes());
        HELPER_EXPECT_SUCCESS(codec.encode(NULL));
        HELPER_EXPECT_SUCCESS(codec.decode(NULL));
    }
    
    if (true) {
        SrsBuffer buf((char*)"hello", 5);
        EXPECT_EQ(5, buf.size());
        EXPECT_EQ(5, buf.left());
        
        buf.read_1bytes();
        EXPECT_EQ(5, buf.size());
        EXPECT_EQ(4, buf.left());
    }
}

/**
* test the kernel utility, time
*/
VOID TEST(KernelUtilityTest, UtilityTime)
{
    srs_utime_t time = srs_get_system_time();
    EXPECT_TRUE(time > 0);
    
    srs_utime_t time1 = srs_get_system_time();
    EXPECT_EQ(time, time1);
    
    usleep(1 * SRS_UTIME_MILLISECONDS);
    time1 = srs_update_system_time();
    EXPECT_TRUE(time1 > time);
}

/**
* test the kernel utility, startup time
*/
VOID TEST(KernelUtilityTest, UtilityStartupTime)
{
    srs_utime_t time = srs_get_system_startup_time();
    EXPECT_TRUE(time > 0);
    
    srs_utime_t time1 = srs_get_system_startup_time();
    EXPECT_EQ(time, time1);
    
    usleep(1 * SRS_UTIME_MILLISECONDS);
    srs_update_system_time();
    time1 = srs_get_system_startup_time();
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

    // origin_str == old_str
    std::string origin_str = "xxd";
    str1 = srs_string_replace(origin_str, "xxd", "x1d");
    EXPECT_STREQ("x1d", str1.c_str());

    // new_str include old_str.
    str1 = srs_string_replace(str, "Hello", "HelloN");
    EXPECT_STREQ("HelloN, World! HelloN, SRS!", str1.c_str());

    str1 = srs_string_trim_start(str, "x");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());

    str1 = srs_string_trim_start(str, "S!R");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());

    str1 = srs_string_trim_start(str, "lHe");
    EXPECT_STREQ("o, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_trim_end(str, "x");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_trim_end(str, "He");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_trim_end(str, "S!R");
    EXPECT_STREQ("Hello, World! Hello, ", str1.c_str());
    
    str1 = srs_string_remove(str, "x");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    str1 = srs_string_remove(str, "o");
    EXPECT_STREQ("Hell, Wrld! Hell, SRS!", str1.c_str());
    
    str1 = srs_string_remove(str, "ol");
    EXPECT_STREQ("He, Wrd! He, SRS!", str1.c_str());

    str1 = srs_erase_first_substr(str, "Hello");
    EXPECT_STREQ(", World! Hello, SRS!", str1.c_str());

    str1 = srs_erase_first_substr(str, "XX");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());

    str1 = srs_erase_last_substr(str, "Hello");
    EXPECT_STREQ("Hello, World! , SRS!", str1.c_str());

    str1 = srs_erase_last_substr(str, "XX");
    EXPECT_STREQ("Hello, World! Hello, SRS!", str1.c_str());
    
    EXPECT_FALSE(srs_string_ends_with("Hello", "x"));
    EXPECT_TRUE(srs_string_ends_with("Hello", "o"));
    EXPECT_TRUE(srs_string_ends_with("Hello", "lo"));
}

VOID TEST(KernelUtility, AvcUev)
{
    int32_t v;
    char data[32];
    
    if (true) {
        data[0] = 0xff;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 1;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(0, v);
    }
    
    if (true) {
        data[0] = 0x40;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(1, v);
    }
    
    if (true) {
        data[0] = 0x60;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(2, v);
    }
    
    if (true) {
        data[0] = 0x20;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(3, v);
    }
    
    if (true) {
        data[0] = 0x28;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(4, v);
    }
    
    if (true) {
        data[0] = 0x30;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(5, v);
    }
    
    if (true) {
        data[0] = 0x38;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(6, v);
    }
    
    if (true) {
        data[0] = 0x10;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(7, v);
    }
    
    if (true) {
        data[0] = 0x12;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(8, v);
    }
    
    if (true) {
        data[0] = 0x14;
        SrsBuffer buf((char*)data, 1); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(9, v);
    }
    
    if (true) {
        data[0] = 0x01; data[1] = 0x12;
        SrsBuffer buf((char*)data, 2); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(128-1+9, v);
    }
    
    if (true) {
        data[0] = 0x00; data[1] = 0x91; data[2] = 0x00;
        SrsBuffer buf((char*)data, 3); SrsBitBuffer bb(&buf); v = 0;
        srs_avc_nalu_read_uev(&bb, v);
        EXPECT_EQ(256-1+0x22, v);
    }
}

extern void __crc32_make_table(uint32_t t[256], uint32_t poly, bool reflect_in);

VOID TEST(KernelUtility, CRC32MakeTable)
{
    uint32_t t[256];
    
    // IEEE, @see https://github.com/ossrs/srs/blob/608c88b8f2b352cdbce3b89b9042026ea907e2d3/trunk/src/kernel/srs_kernel_utility.cpp#L770
    __crc32_make_table(t, 0x4c11db7, true);
    
    EXPECT_EQ((uint32_t)0x00000000, t[0]);
    EXPECT_EQ((uint32_t)0x77073096, t[1]);
    EXPECT_EQ((uint32_t)0xEE0E612C, t[2]);
    EXPECT_EQ((uint32_t)0x990951BA, t[3]);
    EXPECT_EQ((uint32_t)0x076DC419, t[4]);
    EXPECT_EQ((uint32_t)0x706AF48F, t[5]);
    EXPECT_EQ((uint32_t)0xE963A535, t[6]);
    EXPECT_EQ((uint32_t)0x9E6495A3, t[7]);
    
    EXPECT_EQ((uint32_t)0xB3667A2E, t[248]);
    EXPECT_EQ((uint32_t)0xC4614AB8, t[249]);
    EXPECT_EQ((uint32_t)0x5D681B02, t[250]);
    EXPECT_EQ((uint32_t)0x2A6F2B94, t[251]);
    EXPECT_EQ((uint32_t)0xB40BBE37, t[252]);
    EXPECT_EQ((uint32_t)0xC30C8EA1, t[253]);
    EXPECT_EQ((uint32_t)0x5A05DF1B, t[254]);
    EXPECT_EQ((uint32_t)0x2D02EF8D, t[255]);
    
    // IEEE, @see https://github.com/ossrs/srs/blob/608c88b8f2b352cdbce3b89b9042026ea907e2d3/trunk/src/kernel/srs_kernel_utility.cpp#L770
    __crc32_make_table(t, 0x4c11db7, true);
    
    EXPECT_EQ((uint32_t)0x00000000, t[0]);
    EXPECT_EQ((uint32_t)0x77073096, t[1]);
    EXPECT_EQ((uint32_t)0xEE0E612C, t[2]);
    EXPECT_EQ((uint32_t)0x990951BA, t[3]);
    EXPECT_EQ((uint32_t)0x076DC419, t[4]);
    EXPECT_EQ((uint32_t)0x706AF48F, t[5]);
    EXPECT_EQ((uint32_t)0xE963A535, t[6]);
    EXPECT_EQ((uint32_t)0x9E6495A3, t[7]);
    
    EXPECT_EQ((uint32_t)0xB3667A2E, t[248]);
    EXPECT_EQ((uint32_t)0xC4614AB8, t[249]);
    EXPECT_EQ((uint32_t)0x5D681B02, t[250]);
    EXPECT_EQ((uint32_t)0x2A6F2B94, t[251]);
    EXPECT_EQ((uint32_t)0xB40BBE37, t[252]);
    EXPECT_EQ((uint32_t)0xC30C8EA1, t[253]);
    EXPECT_EQ((uint32_t)0x5A05DF1B, t[254]);
    EXPECT_EQ((uint32_t)0x2D02EF8D, t[255]);
    
    // MPEG, @see https://github.com/ossrs/srs/blob/608c88b8f2b352cdbce3b89b9042026ea907e2d3/trunk/src/kernel/srs_kernel_utility.cpp#L691
    __crc32_make_table(t, 0x4c11db7, false);
    
    EXPECT_EQ((uint32_t)0x00000000, t[0]);
    EXPECT_EQ((uint32_t)0x04c11db7, t[1]);
    EXPECT_EQ((uint32_t)0x09823b6e, t[2]);
    EXPECT_EQ((uint32_t)0x0d4326d9, t[3]);
    EXPECT_EQ((uint32_t)0x130476dc, t[4]);
    EXPECT_EQ((uint32_t)0x17c56b6b, t[5]);
    EXPECT_EQ((uint32_t)0x1a864db2, t[6]);
    EXPECT_EQ((uint32_t)0x1e475005, t[7]);
    
    EXPECT_EQ((uint32_t)0xafb010b1, t[248]);
    EXPECT_EQ((uint32_t)0xab710d06, t[249]);
    EXPECT_EQ((uint32_t)0xa6322bdf, t[250]);
    EXPECT_EQ((uint32_t)0xa2f33668, t[251]);
    EXPECT_EQ((uint32_t)0xbcb4666d, t[252]);
    EXPECT_EQ((uint32_t)0xb8757bda, t[253]);
    EXPECT_EQ((uint32_t)0xb5365d03, t[254]);
    EXPECT_EQ((uint32_t)0xb1f740b4, t[255]);
    
    // MPEG, @see https://github.com/ossrs/srs/blob/608c88b8f2b352cdbce3b89b9042026ea907e2d3/trunk/src/kernel/srs_kernel_utility.cpp#L691
    __crc32_make_table(t, 0x4c11db7, false);
    
    EXPECT_EQ((uint32_t)0x00000000, t[0]);
    EXPECT_EQ((uint32_t)0x04c11db7, t[1]);
    EXPECT_EQ((uint32_t)0x09823b6e, t[2]);
    EXPECT_EQ((uint32_t)0x0d4326d9, t[3]);
    EXPECT_EQ((uint32_t)0x130476dc, t[4]);
    EXPECT_EQ((uint32_t)0x17c56b6b, t[5]);
    EXPECT_EQ((uint32_t)0x1a864db2, t[6]);
    EXPECT_EQ((uint32_t)0x1e475005, t[7]);
    
    EXPECT_EQ((uint32_t)0xafb010b1, t[248]);
    EXPECT_EQ((uint32_t)0xab710d06, t[249]);
    EXPECT_EQ((uint32_t)0xa6322bdf, t[250]);
    EXPECT_EQ((uint32_t)0xa2f33668, t[251]);
    EXPECT_EQ((uint32_t)0xbcb4666d, t[252]);
    EXPECT_EQ((uint32_t)0xb8757bda, t[253]);
    EXPECT_EQ((uint32_t)0xb5365d03, t[254]);
    EXPECT_EQ((uint32_t)0xb1f740b4, t[255]);
}

VOID TEST(KernelUtility, CRC32IEEE)
{
    if (true) {
        string datas[] = {
            "123456789", "srs", "ossrs.net",
            "SRS's a simplest, conceptual integrated, industrial-strength live streaming origin cluster."
        };
        
        uint32_t checksums[] = {
            0xcbf43926, 0x7df334e9, 0x2f52242b,
            0x7e8677bd,
        };
        
        for (int i = 0; i < (int)(sizeof(datas)/sizeof(string)); i++) {
            string data = datas[i];
            uint32_t checksum = checksums[i];
            EXPECT_EQ(checksum, srs_crc32_ieee(data.data(), data.length(), 0));
        }
        
        uint32_t previous = 0;
        for (int i = 0; i < (int)(sizeof(datas)/sizeof(string)); i++) {
            string data = datas[i];
            previous = srs_crc32_ieee(data.data(), data.length(), previous);
        }
        EXPECT_EQ((uint32_t)0x431b8785, previous);
    }
    
    if (true) {
        string data = "123456789srs";
        EXPECT_EQ((uint32_t)0xf567b5cf, srs_crc32_ieee(data.data(), data.length(), 0));
    }
    
    if (true) {
        string data = "123456789";
        EXPECT_EQ((uint32_t)0xcbf43926, srs_crc32_ieee(data.data(), data.length(), 0));
        
        data = "srs";
        EXPECT_EQ((uint32_t)0xf567b5cf, srs_crc32_ieee(data.data(), data.length(), 0xcbf43926));
    }
}

VOID TEST(KernelUtility, CRC32MPEGTS)
{
    string datas[] = {
        "123456789", "srs", "ossrs.net",
        "SRS's a simplest, conceptual integrated, industrial-strength live streaming origin cluster."
    };
    
    uint32_t checksums[] = {
        0x0376e6e7, 0xd9089591, 0xbd17933f,
        0x9f389f7d
    };
    
    for (int i = 0; i < (int)(sizeof(datas)/sizeof(string)); i++) {
        string data = datas[i];
        uint32_t checksum = checksums[i];
        EXPECT_EQ(checksum, (uint32_t)srs_crc32_mpegts(data.data(), data.length()));
    }
}

VOID TEST(KernelUtility, Base64Decode)
{
	srs_error_t err;

    string cipher = "dXNlcjpwYXNzd29yZA==";
    string expect = "user:password";
    
    string plaintext;
    HELPER_EXPECT_SUCCESS(srs_av_base64_decode(cipher, plaintext));
    EXPECT_TRUE(expect == plaintext);
}

VOID TEST(KernelUtility, Base64Encode)
{
	srs_error_t err;

    string expect = "dXNlcjpwYXNzd29yZA==";
    string plaintext = "user:password";
    
    string cipher;
    HELPER_EXPECT_SUCCESS(srs_av_base64_encode(plaintext, cipher));
    EXPECT_TRUE(expect == cipher);
}

VOID TEST(KernelUtility, Base64)
{
    srs_error_t err = srs_success;
    struct testpair {
	    string decoded;
        string encoded;
    };

    struct testpair data[] = {
        // RFC 3548 examples
        {"\x14\xfb\x9c\x03\xd9\x7e", "FPucA9l+"},
        {"\x14\xfb\x9c\x03\xd9", "FPucA9k="},
        {"\x14\xfb\x9c\x03", "FPucAw=="},

        // RFC 4648 examples
        {"", ""},
        {"f", "Zg=="},
        {"fo", "Zm8="},
        {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},

        // Wikipedia examples
        {"sure.", "c3VyZS4="},
        {"sure", "c3VyZQ=="},
        {"sur", "c3Vy"},
        {"su", "c3U="},
        {"leasure.", "bGVhc3VyZS4="},
        {"easure.", "ZWFzdXJlLg=="},
        {"asure.", "YXN1cmUu"},
        {"sure.", "c3VyZS4="},
        {"Twas brillig, and the slithy toves", "VHdhcyBicmlsbGlnLCBhbmQgdGhlIHNsaXRoeSB0b3Zlcw=="}
    };

    for(int i = 0; i < (int)(sizeof(data) / sizeof(struct testpair)); ++i) {
        struct testpair& d = data[i];
        string cipher;
        HELPER_EXPECT_SUCCESS(srs_av_base64_encode(d.decoded, cipher));
        EXPECT_STREQ(d.encoded.c_str(), cipher.c_str());

        string plaintext;
        HELPER_EXPECT_SUCCESS(srs_av_base64_decode(d.encoded, plaintext));
        EXPECT_STREQ(d.decoded.c_str(), plaintext.c_str());
    }

    string expected = "sure";
	string examples[11] = {
		"c3VyZQ==",
		"c3VyZQ==\r",
		"c3VyZQ==\n",
		"c3VyZQ==\r\n",
		"c3VyZ\r\nQ==",
		"c3V\ryZ\nQ==",
		"c3V\nyZ\rQ==",
		"c3VyZ\nQ==",
		"c3VyZQ\n==",
		"c3VyZQ=\n=",
		"c3VyZQ=\r\n\r\n=",
	};

    for(int i = 0; i < 11; ++i) {
        string& encoded_str = examples[i];
        string plaintext;
        HELPER_EXPECT_SUCCESS(srs_av_base64_decode(encoded_str, plaintext));
        EXPECT_STREQ(expected.c_str(), plaintext.c_str());
    }


}

VOID TEST(KernelUtility, StringToHex)
{
    if (true) {
        uint8_t h[16];
        EXPECT_EQ(-1, srs_hex_to_data(h, NULL, 0));
        EXPECT_EQ(-1, srs_hex_to_data(h, "0", 1));
        EXPECT_EQ(-1, srs_hex_to_data(h, "0g", 2));
    }
    
    if (true) {
        string s = "139056E5A0";
        uint8_t h[16];
        
        int n = srs_hex_to_data(h, s.data(), s.length());
        EXPECT_EQ(n, 5);
        EXPECT_EQ(0x13, h[0]);
        EXPECT_EQ(0x90, h[1]);
        EXPECT_EQ(0x56, h[2]);
        EXPECT_EQ(0xe5, h[3]);
        EXPECT_EQ(0xa0, h[4]);
    }
}

VOID TEST(KernelUtility, StringUtils)
{
    if (true) {
        EXPECT_TRUE("srs" == srs_string_replace("srsx", "x", ""));
        EXPECT_TRUE("srs" == srs_string_replace("srs", "", ""));
        EXPECT_TRUE("srs" == srs_string_replace("sxs", "x", "r"));
        EXPECT_TRUE("srs" == srs_string_replace("xrx", "x", "s"));
        EXPECT_TRUE("srs" == srs_string_replace("xyrxy", "xy", "s"));
        EXPECT_TRUE("srs" == srs_string_replace("sxys", "xy", "r"));
    }
    
    if (true) {
        EXPECT_TRUE("srs" == srs_string_trim_end("srs", ""));
        EXPECT_TRUE("srs" == srs_string_trim_end("srsx", "x"));
        EXPECT_TRUE("srs" == srs_string_trim_end("srsxx", "x"));
        EXPECT_TRUE("srs" == srs_string_trim_end("srsxy", "xy"));
        EXPECT_TRUE("srs" == srs_string_trim_end("srsx ", "x "));
        EXPECT_TRUE("srs" == srs_string_trim_end("srsx yx", "x y"));
        EXPECT_TRUE("srs" == srs_string_trim_end("srsx yxy", "x y"));
    }
    
    if (true) {
        EXPECT_TRUE("srs" == srs_string_trim_start("srs", ""));
        EXPECT_TRUE("srs" == srs_string_trim_start("xsrs", "x"));
        EXPECT_TRUE("srs" == srs_string_trim_start("xxsrs", "x"));
        EXPECT_TRUE("srs" == srs_string_trim_start("xysrs", "xy"));
        EXPECT_TRUE("srs" == srs_string_trim_start("x srs", "x "));
        EXPECT_TRUE("srs" == srs_string_trim_start("x yxsrs", "x y"));
        EXPECT_TRUE("srs" == srs_string_trim_start("x yxysrs", "x y"));
    }
    
    if (true) {
        EXPECT_TRUE("srs" == srs_string_remove("srs", ""));
        EXPECT_TRUE("srs" == srs_string_remove("xsrs", "x"));
        EXPECT_TRUE("srs" == srs_string_remove("xsrsx", "x"));
        EXPECT_TRUE("srs" == srs_string_remove("xsxrsx", "x"));
        EXPECT_TRUE("srs" == srs_string_remove("yxsxrsx", "xy"));
        EXPECT_TRUE("srs" == srs_string_remove("yxsxrysx", "xy"));
    }
    
    if (true) {
        EXPECT_TRUE("srs" == srs_erase_first_substr("srs", ""));
        EXPECT_TRUE("srs" == srs_erase_first_substr("xsrs", "x"));
        EXPECT_TRUE("srs" == srs_erase_first_substr("srssrs", "srs"));
    }
    
    if (true) {
        EXPECT_TRUE("srs" == srs_erase_last_substr("srs", ""));
        EXPECT_TRUE("srs" == srs_erase_last_substr("srsx", "x"));
        EXPECT_TRUE("srs" == srs_erase_last_substr("srssrs", "srs"));
    }
    
    if (true) {
        EXPECT_TRUE(srs_string_ends_with("srs", "s"));
        EXPECT_TRUE(srs_string_ends_with("srs", "rs"));
        EXPECT_TRUE(srs_string_ends_with("srs", "srs"));
        EXPECT_TRUE(!srs_string_ends_with("srs", "x"));
        EXPECT_TRUE(!srs_string_ends_with("srs", "srx"));
        
        EXPECT_TRUE(srs_string_ends_with("srs", "r", "s"));
        EXPECT_TRUE(srs_string_ends_with("srs", "sr", "s"));
        EXPECT_TRUE(srs_string_ends_with("srs", "x", "r", "s"));
        EXPECT_TRUE(srs_string_ends_with("srs", "y", "x", "r", "s"));
        EXPECT_TRUE(!srs_string_ends_with("srs", "x", "y", "z", "srx"));
    }
    
    if (true) {
        EXPECT_TRUE(srs_string_starts_with("srs", "s"));
        EXPECT_TRUE(srs_string_starts_with("srs", "sr"));
        EXPECT_TRUE(srs_string_starts_with("srs", "srs"));
        EXPECT_TRUE(!srs_string_starts_with("srs", "x"));
        
        EXPECT_TRUE(srs_string_starts_with("srs", "r", "s"));
        EXPECT_TRUE(srs_string_starts_with("srs", "sr", "s"));
        EXPECT_TRUE(srs_string_starts_with("srs", "x", "r", "s"));
        EXPECT_TRUE(srs_string_starts_with("srs", "y", "x", "r", "s"));
        EXPECT_TRUE(!srs_string_starts_with("srs", "x", "y", "z", "srx"));
    }
    
    if (true) {
        EXPECT_TRUE(srs_string_contains("srs", "s"));
        EXPECT_TRUE(srs_string_contains("srs", "s", "sr"));
        EXPECT_TRUE(srs_string_contains("srs", "s", "sr", "srs"));
    }

    if (true) {
        EXPECT_EQ(0, srs_string_count("srs", "y"));
        EXPECT_EQ(0, srs_string_count("srs", ""));
        EXPECT_EQ(1, srs_string_count("srs", "r"));
        EXPECT_EQ(2, srs_string_count("srs", "s"));
        EXPECT_EQ(3, srs_string_count("srs", "sr"));
    }
    
    if (true) {
        vector<string> flags;
        EXPECT_TRUE("srs" == srs_string_min_match("srs", flags));
    }
    
    if (true) {
        vector<string> flags;
        flags.push_back("s");
        EXPECT_TRUE("s" == srs_string_min_match("srs", flags));
    }
    
    if (true) {
        vector<string> flags;
        flags.push_back("sr");
        EXPECT_TRUE("sr" == srs_string_min_match("srs", flags));
    }
    
    if (true) {
        vector<string> flags;
        flags.push_back("rs");
        EXPECT_TRUE("rs" == srs_string_min_match("srs", flags));
    }
    
    if (true) {
        vector<string> flags;
        flags.push_back("x"); flags.push_back("rs");
        EXPECT_TRUE("rs" == srs_string_min_match("srs", flags));
    }
    
    if (true) {
        vector<string> flags;
        flags.push_back("x");
        EXPECT_TRUE("" == srs_string_min_match("srs", flags));
    }
}

VOID TEST(KernelUtility, StringSplitUtils)
{
    if (true) {
        vector<string> ss = srs_string_split("ossrs", "r");
        EXPECT_EQ(2, (int)ss.size());
        EXPECT_STREQ("oss", ss.at(0).c_str());
        EXPECT_STREQ("s", ss.at(1).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split("ossrs", "");
        EXPECT_EQ(1, (int)ss.size());
        EXPECT_STREQ("ossrs", ss.at(0).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split("ossrs", "live");
        EXPECT_EQ(1, (int)ss.size());
        EXPECT_STREQ("ossrs", ss.at(0).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split("srs,live,rtc", ",");
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("srs", ss.at(0).c_str());
        EXPECT_STREQ("live", ss.at(1).c_str());
        EXPECT_STREQ("rtc", ss.at(2).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split("srs,,rtc", ",");
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("srs", ss.at(0).c_str());
        EXPECT_STREQ("", ss.at(1).c_str());
        EXPECT_STREQ("rtc", ss.at(2).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split("srs,,,rtc", ",");
        EXPECT_EQ(4, (int)ss.size());
        EXPECT_STREQ("srs", ss.at(0).c_str());
        EXPECT_STREQ("", ss.at(1).c_str());
        EXPECT_STREQ("", ss.at(2).c_str());
        EXPECT_STREQ("rtc", ss.at(3).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split("srs,live,", ",");
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("srs", ss.at(0).c_str());
        EXPECT_STREQ("live", ss.at(1).c_str());
        EXPECT_STREQ("", ss.at(2).c_str());
    }

    if (true) {
        vector<string> ss = srs_string_split(",live,rtc", ",");
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("", ss.at(0).c_str());
        EXPECT_STREQ("live", ss.at(1).c_str());
        EXPECT_STREQ("rtc", ss.at(2).c_str());
    }

    if (true) {
        EXPECT_TRUE("srs" == srs_string_split("srs", "").at(0));
        EXPECT_TRUE("s" == srs_string_split("srs", "r").at(0));
        EXPECT_TRUE("s" == srs_string_split("srs", "rs").at(0));
    }

    if (true) {
        vector<string> ss = srs_string_split("/xxx/yyy", "/");
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("", ss.at(0).c_str());
        EXPECT_STREQ("xxx", ss.at(1).c_str());
        EXPECT_STREQ("yyy", ss.at(2).c_str());
    }
}

VOID TEST(KernelUtility, StringSplitUtils2)
{
    if (true) {
        vector<string> flags;
        flags.push_back("e");
        flags.push_back("wo");
        vector<string> ss = srs_string_split("hello, world", flags);
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("h", ss.at(0).c_str());
        EXPECT_STREQ("llo, ", ss.at(1).c_str());
        EXPECT_STREQ("rld", ss.at(2).c_str());
    }

    if (true) {
        vector<string> flags;
        flags.push_back("");
        flags.push_back("");
        vector<string> ss = srs_string_split("hello, world", flags);
        EXPECT_EQ(1, (int)ss.size());
        EXPECT_STREQ("hello, world", ss.at(0).c_str());
    }

    if (true) {
        vector<string> flags;
        flags.push_back(",");
        flags.push_back(" ");
        vector<string> ss = srs_string_split("hello, world", flags);
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("hello", ss.at(0).c_str());
        EXPECT_STREQ("", ss.at(1).c_str());
        EXPECT_STREQ("world", ss.at(2).c_str());
    }

    if (true) {
        vector<string> flags;
        flags.push_back(",");
        vector<string> ss = srs_string_split("hello,,world", flags);
        EXPECT_EQ(3, (int)ss.size());
        EXPECT_STREQ("hello", ss.at(0).c_str());
        EXPECT_STREQ("", ss.at(1).c_str());
        EXPECT_STREQ("world", ss.at(2).c_str());
    }
}

VOID TEST(KernelUtility, BytesUtils)
{
    if (true) {
        EXPECT_TRUE(srs_bytes_equals(NULL, NULL, 0));
        EXPECT_TRUE(srs_bytes_equals((void*)"s", (void*)"s", 0));
        EXPECT_TRUE(srs_bytes_equals((void*)"s", (void*)"s", 1));
        EXPECT_TRUE(srs_bytes_equals((void*)"s", (void*)"srs", 1));
        EXPECT_TRUE(!srs_bytes_equals((void*)"xrs", (void*)"srs", 3));
    }
}

VOID TEST(KernelUtility, PathUtils)
{
    if (true) {
        EXPECT_TRUE("./" == srs_path_dirname(""));
        EXPECT_TRUE("/" == srs_path_dirname("/"));
        EXPECT_TRUE("/" == srs_path_dirname("//"));
        EXPECT_TRUE("/" == srs_path_dirname("/stream"));
        EXPECT_TRUE("live" == srs_path_dirname("live/stream"));
    }
    
    if (true) {
        EXPECT_TRUE("" == srs_path_basename(""));
        EXPECT_TRUE("/" == srs_path_basename("/"));
        EXPECT_TRUE("stream" == srs_path_basename("/stream"));
        EXPECT_TRUE("stream" == srs_path_basename("live/stream"));
        EXPECT_TRUE("stream.flv" == srs_path_basename("live/stream.flv"));
    }
    
    if (true) {
        EXPECT_TRUE("" == srs_path_filename(""));
        EXPECT_TRUE("stream" == srs_path_filename("stream.flv"));
    }
    
    if (true) {
        EXPECT_TRUE("" == srs_path_filext(""));
        EXPECT_TRUE(".flv" == srs_path_filext("stream.flv"));
    }
}

VOID TEST(KernelUtility, AnnexbUtils)
{
    if (true) {
        EXPECT_TRUE(!srs_avc_startswith_annexb(NULL, NULL));
        
        SrsBuffer buf(NULL, 0);
        EXPECT_TRUE(!srs_avc_startswith_annexb(&buf, NULL));
    }
    
    if (true) {
        char data[] = {0x00};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(!srs_avc_startswith_annexb(&buf, NULL));
    }
    
    if (true) {
        char data[] = {0x00, 0x00};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(!srs_avc_startswith_annexb(&buf, NULL));
    }
    
    if (true) {
        char data[] = {0x00, 0x00, 0x02};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(!srs_avc_startswith_annexb(&buf, NULL));
    }
    
    if (true) {
        char data[] = {0x00, 0x00, 0x01};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(srs_avc_startswith_annexb(&buf, NULL));
    }
    
    if (true) {
        char data[] = {0x00, 0x01, 0x00, 00};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(!srs_avc_startswith_annexb(&buf, NULL));
    }
    
    if (true) {
        char data[] = {0x00, 0x00, 0x01};
        SrsBuffer buf((char*)data, sizeof(data));
        
        int start;
        EXPECT_TRUE(srs_avc_startswith_annexb(&buf, &start));
        EXPECT_EQ(3, start);
    }
    
    if (true) {
        char data[] = {0x00, 0x00, 0x00, 0x01};
        SrsBuffer buf((char*)data, sizeof(data));
        
        int start;
        EXPECT_TRUE(srs_avc_startswith_annexb(&buf, &start));
        EXPECT_EQ(4, start);
    }
}

VOID TEST(KernelUtility, AdtsUtils)
{
    if (true) {
        EXPECT_TRUE(!srs_aac_startswith_adts(NULL));
        
        SrsBuffer buf(NULL, 0);
        EXPECT_TRUE(!srs_aac_startswith_adts(&buf));
    }
    
    if (true) {
        char data[] = {0x00};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(!srs_aac_startswith_adts(&buf));
    }
    
    if (true) {
        uint8_t data[] = {0xFF, 0x00};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(!srs_aac_startswith_adts(&buf));
    }
    
    if (true) {
        uint8_t data[] = {0xFF, 0xF0};
        SrsBuffer buf((char*)data, sizeof(data));
        EXPECT_TRUE(srs_aac_startswith_adts(&buf));
    }
}

VOID TEST(KernelUtility, RTMPUtils)
{
    if (true) {
        char buf[16];
        EXPECT_EQ(0, srs_chunk_header_c0(0, 0, 0, 0, 0, NULL, 0));
        EXPECT_EQ(12, srs_chunk_header_c0(0, 0, 0, 0, 0, buf, 16));
    }
    
    if (true) {
        char buf[16];
        EXPECT_EQ(12, srs_chunk_header_c0(1, 0, 0, 0, 0, buf, 16));
        
        EXPECT_EQ(1, buf[0]);
    }
    
    if (true) {
        char buf[16];
        EXPECT_EQ(12, srs_chunk_header_c0(1, 0x345678, 0, 0, 0, buf, 16));
        
        EXPECT_EQ(1, buf[0]);
        
        EXPECT_EQ(0x34, buf[1]);
        EXPECT_EQ(0x56, buf[2]);
        EXPECT_EQ(0x78, buf[3]);
    }
    
    if (true) {
        char buf[16];
        EXPECT_EQ(12, srs_chunk_header_c0(1, 0x345678, 0x123456, 0, 0, buf, 16));
        
        EXPECT_EQ(1, buf[0]);
        
        EXPECT_EQ(0x34, buf[1]);
        EXPECT_EQ(0x56, buf[2]);
        EXPECT_EQ(0x78, buf[3]);
        
        EXPECT_EQ(0x12, buf[4]);
        EXPECT_EQ(0x34, buf[5]);
        EXPECT_EQ(0x56, buf[6]);
    }
    
    if (true) {
        char buf[16];
        EXPECT_EQ(12, srs_chunk_header_c0(1, 0x345678, 0x123456, 2, 0, buf, 16));
        
        EXPECT_EQ(1, buf[0]);
        
        EXPECT_EQ(0x34, buf[1]);
        EXPECT_EQ(0x56, buf[2]);
        EXPECT_EQ(0x78, buf[3]);
        
        EXPECT_EQ(0x12, buf[4]);
        EXPECT_EQ(0x34, buf[5]);
        EXPECT_EQ(0x56, buf[6]);
        
        EXPECT_EQ(2, buf[7]);
    }
    
    if (true) {
        char buf[16];
        EXPECT_EQ(12, srs_chunk_header_c0(1, 0x345678, 0x123456, 2, 0x12345678, buf, 16));
        
        EXPECT_EQ(1, buf[0]);
        
        EXPECT_EQ(0x34, buf[1]);
        EXPECT_EQ(0x56, buf[2]);
        EXPECT_EQ(0x78, buf[3]);
        
        EXPECT_EQ(0x12, buf[4]);
        EXPECT_EQ(0x34, buf[5]);
        EXPECT_EQ(0x56, buf[6]);
        
        EXPECT_EQ(2, buf[7]);
        
        EXPECT_EQ(0x78, buf[8]);
        EXPECT_EQ(0x56, buf[9]);
        EXPECT_EQ(0x34, buf[10]);
        EXPECT_EQ(0x12, buf[11]);
    }
    
    if (true) {
        char buf[16];
        EXPECT_EQ(16, srs_chunk_header_c0(1, 0x12345678, 0x123456, 2, 0x12345678, buf, 16));
        
        EXPECT_EQ(1, buf[0]);
        
        EXPECT_EQ((char)0xff, buf[1]);
        EXPECT_EQ((char)0xff, buf[2]);
        EXPECT_EQ((char)0xff, buf[3]);
        
        EXPECT_EQ(0x12, buf[4]);
        EXPECT_EQ(0x34, buf[5]);
        EXPECT_EQ(0x56, buf[6]);
        
        EXPECT_EQ(2, buf[7]);
        
        EXPECT_EQ(0x78, buf[8]);
        EXPECT_EQ(0x56, buf[9]);
        EXPECT_EQ(0x34, buf[10]);
        EXPECT_EQ(0x12, buf[11]);
        
        EXPECT_EQ(0x12, buf[12]);
        EXPECT_EQ(0x34, buf[13]);
        EXPECT_EQ(0x56, buf[14]);
        EXPECT_EQ(0x78, buf[15]);
    }
}

VOID TEST(KernelUtility, RTMPUtils2)
{
    if (true) {
        char buf[5];
        EXPECT_EQ(0, srs_chunk_header_c3(0, 0, NULL, 0));
        EXPECT_EQ(1, srs_chunk_header_c3(0, 0, buf, 5));
    }
    
    if (true) {
        char buf[5];
        EXPECT_EQ(1, srs_chunk_header_c3(1, 0, buf, 5));
        
        EXPECT_EQ((char)0xC1, buf[0]);
    }
    
    if (true) {
        char buf[5];
        EXPECT_EQ(1, srs_chunk_header_c3(1, 0x234567, buf, 5));
        
        EXPECT_EQ((char)0xC1, buf[0]);
    }
    
    if (true) {
        char buf[5];
        EXPECT_EQ(5, srs_chunk_header_c3(1, 0x12345678, buf, 5));
        
        EXPECT_EQ((char)0xC1, buf[0]);
        
        EXPECT_EQ((char)0x12, buf[1]);
        EXPECT_EQ((char)0x34, buf[2]);
        EXPECT_EQ((char)0x56, buf[3]);
        EXPECT_EQ((char)0x78, buf[4]);
    }
}

VOID TEST(KernelErrorTest, CoverAll)
{
    srs_error_t err;
    if (true) {
        EXPECT_TRUE(srs_is_system_control_error(err = srs_error_new(ERROR_CONTROL_RTMP_CLOSE, "err"))); srs_freep(err);
        EXPECT_TRUE(srs_is_system_control_error(err = srs_error_new(ERROR_CONTROL_REPUBLISH, "err"))); srs_freep(err);
        EXPECT_TRUE(srs_is_system_control_error(err = srs_error_new(ERROR_CONTROL_REDIRECT, "err"))); srs_freep(err);
    }
    
    if (true) {
        EXPECT_TRUE(srs_is_system_control_error(err = srs_error_new(ERROR_CONTROL_RTMP_CLOSE, "control error"))); srs_freep(err);
    }
    
    if (true) {
        EXPECT_TRUE(srs_is_client_gracefully_close(err = srs_error_new(ERROR_SOCKET_READ, "err"))); srs_freep(err);
        EXPECT_TRUE(srs_is_client_gracefully_close(err = srs_error_new(ERROR_SOCKET_READ_FULLY, "err"))); srs_freep(err);
        EXPECT_TRUE(srs_is_client_gracefully_close(err = srs_error_new(ERROR_SOCKET_WRITE, "err"))); srs_freep(err);
    }
    
    if (true) {
        srs_error_t err = srs_error_new(ERROR_SOCKET_READ, "graceful close error");
        EXPECT_TRUE(srs_is_client_gracefully_close(err));
        srs_freep(err);
    }

    if (true) {
        srs_error_t err = srs_error_new(ERROR_HTTP_STREAM_EOF, "graceful close error");
        EXPECT_TRUE(srs_is_server_gracefully_close(err));
        srs_freep(err);
    }
    
    if (true) {
        srs_error_t err = srs_error_wrap(srs_error_new(ERROR_CONTROL_RTMP_CLOSE, "control error"), "wrapped");
        EXPECT_TRUE(srs_error_desc(err) != "");
        srs_freep(err);
    }
    
    if (true) {
        EXPECT_TRUE(srs_error_desc(srs_success) == "Success");
    }
    
    if (true) {
        srs_error_t err = srs_success;
        HELPER_EXPECT_SUCCESS(srs_error_copy(err));
    }
    
    if (true) {
        srs_error_t err = srs_error_new(ERROR_SOCKET_READ, "graceful close error");
        srs_error_t r0 = srs_error_copy(err);
        EXPECT_TRUE(err != r0);
        srs_freep(err);
        srs_freep(r0);
    }
}

VOID TEST(KernelAACTest, TransmaxRTMP2AAC)
{
	srs_error_t err;

    if (true) {
        SrsAacTransmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        srs_error_t err = m.write_audio(0, (char*)"", 0);
        EXPECT_EQ(ERROR_AAC_DECODE_ERROR, srs_error_code(err));
        srs_freep(err);
        
        err = m.write_audio(0, (char*)"\x0f", 1);
        EXPECT_TRUE(ERROR_AAC_DECODE_ERROR == srs_error_code(err));
        srs_freep(err);
        
        err = m.write_audio(0, (char*)"\xaf", 1);
        EXPECT_TRUE(ERROR_AAC_DECODE_ERROR == srs_error_code(err));
        srs_freep(err);
    }
    
    if (true) {
        SrsAacTransmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        srs_error_t err = m.write_audio(0, (char*)"\xaf\x01\x00", 3);
        EXPECT_TRUE(ERROR_AAC_DECODE_ERROR == srs_error_code(err));
        srs_freep(err);
        
        EXPECT_TRUE(!m.got_sequence_header);
    }
    
    if (true) {
        SrsAacTransmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        srs_error_t err = m.write_audio(0, (char*)"\xaf\x00", 2);
        EXPECT_TRUE(ERROR_AAC_DECODE_ERROR == srs_error_code(err));
        srs_freep(err);
        
        err = m.write_audio(0, (char*)"\xaf\x00\x12\x10", 4);
        HELPER_EXPECT_SUCCESS(err);
        
        EXPECT_TRUE(m.got_sequence_header);
        EXPECT_EQ(44100, srs_aac_srates[m.aac_sample_rate]);
        EXPECT_EQ(2, m.aac_channels);
        
        err = m.write_audio(0, (char*)"\xaf\x01\xcb", 3);
        HELPER_EXPECT_SUCCESS(err);
        
        EXPECT_EQ(8, f.tellg());
        EXPECT_EQ((char)0xff, f.data()[0]);
        EXPECT_EQ((char)0xf1, f.data()[1]);
        EXPECT_EQ((char)0x50, f.data()[2]);
        EXPECT_EQ((char)0x80, f.data()[3]);
        EXPECT_EQ((char)0x01, f.data()[4]);
        EXPECT_EQ((char)0x00, f.data()[5]);
        EXPECT_EQ((char)0xfc, f.data()[6]);
        EXPECT_EQ((char)0xcb, f.data()[7]);
    }
    
    if (true) {
        SrsAacTransmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        srs_error_t err = m.write_audio(0, (char*)"\xaf\x00", 2);
        EXPECT_TRUE(ERROR_AAC_DECODE_ERROR == srs_error_code(err));
        srs_freep(err);
        
        err = m.write_audio(0, (char*)"\xaf\x00\x12\x10", 4);
        HELPER_EXPECT_SUCCESS(err);
        
        EXPECT_TRUE(m.got_sequence_header);
        EXPECT_EQ(44100, srs_aac_srates[m.aac_sample_rate]);
        EXPECT_EQ(2, m.aac_channels);
        
        f.error_offset = 6;
        
        err = m.write_audio(0, (char*)"\xaf\x01\x00", 3);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsAacTransmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        srs_error_t err = m.write_audio(0, (char*)"\xaf\x00", 2);
        EXPECT_TRUE(ERROR_AAC_DECODE_ERROR == srs_error_code(err));
        srs_freep(err);
        
        err = m.write_audio(0, (char*)"\xaf\x00\x12\x10", 4);
        HELPER_EXPECT_SUCCESS(err);
        
        EXPECT_TRUE(m.got_sequence_header);
        EXPECT_EQ(44100, srs_aac_srates[m.aac_sample_rate]);
        EXPECT_EQ(2, m.aac_channels);
        
        f.error_offset = 7;
        
        err = m.write_audio(0, (char*)"\xaf\x01\x00", 3);
        HELPER_EXPECT_FAILED(err);
    }
}

VOID TEST(KernelLBRRTest, CoverAll)
{
    if (true) {
        SrsLbRoundRobin lb;
        EXPECT_EQ(0, (int)lb.count);
        EXPECT_EQ(-1, lb.index);
        EXPECT_EQ(-1, (int)lb.current());
        EXPECT_TRUE("" == lb.selected());
    }
    
    if (true) {
        vector<string> servers;
        servers.push_back("s0");
        servers.push_back("s1");
        servers.push_back("s2");
        
        SrsLbRoundRobin lb;
        lb.select(servers);
        EXPECT_EQ(0, (int)lb.current());
        EXPECT_TRUE("s0" == lb.selected());
        
        lb.select(servers);
        EXPECT_EQ(1, (int)lb.current());
        EXPECT_TRUE("s1" == lb.selected());
        
        lb.select(servers);
        EXPECT_EQ(2, (int)lb.current());
        EXPECT_TRUE("s2" == lb.selected());
        
        lb.select(servers);
        EXPECT_EQ(0, (int)lb.current());
        EXPECT_TRUE("s0" == lb.selected());
    }
}

VOID TEST(KernelCodecTest, CoverAll)
{
    if (true) {
        EXPECT_TRUE("H264" == srs_video_codec_id2str(SrsVideoCodecIdAVC));
        EXPECT_TRUE("VP6" == srs_video_codec_id2str(SrsVideoCodecIdOn2VP6));
        EXPECT_TRUE("HEVC" == srs_video_codec_id2str(SrsVideoCodecIdHEVC));
        EXPECT_TRUE("AV1" == srs_video_codec_id2str(SrsVideoCodecIdAV1));
        EXPECT_TRUE("Other" == srs_video_codec_id2str(SrsVideoCodecIdScreenVideo));
    }
    
    if (true) {
        EXPECT_TRUE("AAC" == srs_audio_codec_id2str(SrsAudioCodecIdAAC));
        EXPECT_TRUE("MP3" == srs_audio_codec_id2str(SrsAudioCodecIdMP3));
        EXPECT_TRUE("Opus" == srs_audio_codec_id2str(SrsAudioCodecIdOpus));
        EXPECT_TRUE("Other" == srs_audio_codec_id2str(SrsAudioCodecIdSpeex));
    }
    
    if (true) {
        EXPECT_TRUE("5512" == srs_audio_sample_rate2str(SrsAudioSampleRate5512));
        EXPECT_TRUE("11025" == srs_audio_sample_rate2str(SrsAudioSampleRate11025));
        EXPECT_TRUE("22050" == srs_audio_sample_rate2str(SrsAudioSampleRate22050));
        EXPECT_TRUE("44100" == srs_audio_sample_rate2str(SrsAudioSampleRate44100));
        EXPECT_TRUE("NB8kHz" == srs_audio_sample_rate2str(SrsAudioSampleRateNB8kHz));
        EXPECT_TRUE("MB12kHz" == srs_audio_sample_rate2str(SrsAudioSampleRateMB12kHz));
        EXPECT_TRUE("WB16kHz" == srs_audio_sample_rate2str(SrsAudioSampleRateWB16kHz));
        EXPECT_TRUE("SWB24kHz" == srs_audio_sample_rate2str(SrsAudioSampleRateSWB24kHz));
        EXPECT_TRUE("FB48kHz" == srs_audio_sample_rate2str(SrsAudioSampleRateFB48kHz));
        EXPECT_TRUE("Other" == srs_audio_sample_rate2str(SrsAudioSampleRateForbidden));
    }
    
    if (true) {
        SrsFlvVideo v;
        EXPECT_TRUE(!v.sh((char*)"\x07", 1));
        
        EXPECT_TRUE(!v.acceptable(NULL, 0));
        EXPECT_TRUE(!v.acceptable((char*)"\x00", 1));
        EXPECT_TRUE(!v.acceptable((char*)"\xf0", 1));
        EXPECT_TRUE(!v.acceptable((char*)"\x10", 1));
        EXPECT_TRUE(!v.acceptable((char*)"\x1f", 1));
        EXPECT_TRUE(v.acceptable((char*)"\x17", 1)); // AVC = 7
        EXPECT_TRUE(v.acceptable((char*)"\x1c", 1)); // HEVC = 12
        EXPECT_TRUE(v.acceptable((char*)"\x1d", 1)); // AV1 = 13
    }
    
    if (true) {
        SrsFlvAudio a;
        EXPECT_TRUE(!a.sh((char*)"\xa0", 1));
    }
    
    if (true) {
        EXPECT_TRUE("16bits" == srs_audio_sample_bits2str(SrsAudioSampleBits16bit));
        EXPECT_TRUE("8bits" == srs_audio_sample_bits2str(SrsAudioSampleBits8bit));
        EXPECT_TRUE("Other" == srs_audio_sample_bits2str(SrsAudioSampleBitsForbidden));
    }
    
    if (true) {
        EXPECT_TRUE("Stereo" == srs_audio_channels2str(SrsAudioChannelsStereo));
        EXPECT_TRUE("Mono" == srs_audio_channels2str(SrsAudioChannelsMono));
        EXPECT_TRUE("Other" == srs_audio_channels2str(SrsAudioChannelsForbidden));
    }
    
    if (true) {
        EXPECT_TRUE("NonIDR" == srs_avc_nalu2str(SrsAvcNaluTypeNonIDR));
	    EXPECT_TRUE("DataPartitionA" == srs_avc_nalu2str(SrsAvcNaluTypeDataPartitionA));
	    EXPECT_TRUE("DataPartitionB" == srs_avc_nalu2str(SrsAvcNaluTypeDataPartitionB));
	    EXPECT_TRUE("DataPartitionC" == srs_avc_nalu2str(SrsAvcNaluTypeDataPartitionC));
	    EXPECT_TRUE("IDR" == srs_avc_nalu2str(SrsAvcNaluTypeIDR));
	    EXPECT_TRUE("SEI" == srs_avc_nalu2str(SrsAvcNaluTypeSEI));
	    EXPECT_TRUE("SPS" == srs_avc_nalu2str(SrsAvcNaluTypeSPS));
	    EXPECT_TRUE("PPS" == srs_avc_nalu2str(SrsAvcNaluTypePPS));
	    EXPECT_TRUE("AccessUnitDelimiter" == srs_avc_nalu2str(SrsAvcNaluTypeAccessUnitDelimiter));
	    EXPECT_TRUE("EOSequence" == srs_avc_nalu2str(SrsAvcNaluTypeEOSequence));
	    EXPECT_TRUE("EOStream" == srs_avc_nalu2str(SrsAvcNaluTypeEOStream));
	    EXPECT_TRUE("FilterData" == srs_avc_nalu2str(SrsAvcNaluTypeFilterData));
	    EXPECT_TRUE("SPSExt" == srs_avc_nalu2str(SrsAvcNaluTypeSPSExt));
	    EXPECT_TRUE("PrefixNALU" == srs_avc_nalu2str(SrsAvcNaluTypePrefixNALU));
	    EXPECT_TRUE("SubsetSPS" == srs_avc_nalu2str(SrsAvcNaluTypeSubsetSPS));
	    EXPECT_TRUE("LayerWithoutPartition" == srs_avc_nalu2str(SrsAvcNaluTypeLayerWithoutPartition));
        EXPECT_TRUE("CodedSliceExt" == srs_avc_nalu2str(SrsAvcNaluTypeCodedSliceExt));
        EXPECT_TRUE("Other" == srs_avc_nalu2str(SrsAvcNaluTypeForbidden));
    }

    if (true) {
	    EXPECT_TRUE("Main" == srs_aac_profile2str(SrsAacProfileMain));
	    EXPECT_TRUE("LC" == srs_aac_profile2str(SrsAacProfileLC));
        EXPECT_TRUE("SSR" == srs_aac_profile2str(SrsAacProfileSSR));
        EXPECT_TRUE("Other" == srs_aac_profile2str(SrsAacProfileReserved));
    }

    if (true) {
	    EXPECT_TRUE("Main" == srs_aac_object2str(SrsAacObjectTypeAacMain));
	    EXPECT_TRUE("LC" == srs_aac_object2str(SrsAacObjectTypeAacLC));
	    EXPECT_TRUE("SSR" == srs_aac_object2str(SrsAacObjectTypeAacSSR));
	    EXPECT_TRUE("HE" == srs_aac_object2str(SrsAacObjectTypeAacHE));
        EXPECT_TRUE("HEv2" == srs_aac_object2str(SrsAacObjectTypeAacHEV2));
        EXPECT_TRUE("Other" == srs_aac_object2str(SrsAacObjectTypeForbidden));
    }

    if (true) {
        EXPECT_TRUE(SrsAacObjectTypeAacMain == srs_aac_ts2rtmp(SrsAacProfileMain));
        EXPECT_TRUE(SrsAacObjectTypeAacLC == srs_aac_ts2rtmp(SrsAacProfileLC));
        EXPECT_TRUE(SrsAacObjectTypeAacSSR == srs_aac_ts2rtmp(SrsAacProfileSSR));
        EXPECT_TRUE(SrsAacObjectTypeReserved == srs_aac_ts2rtmp(SrsAacProfileReserved));
    }

    if (true) {
        EXPECT_TRUE(SrsAacProfileMain == srs_aac_rtmp2ts(SrsAacObjectTypeAacMain));
        EXPECT_TRUE(SrsAacProfileLC == srs_aac_rtmp2ts(SrsAacObjectTypeAacHE));
        EXPECT_TRUE(SrsAacProfileLC == srs_aac_rtmp2ts(SrsAacObjectTypeAacHEV2));
        EXPECT_TRUE(SrsAacProfileLC == srs_aac_rtmp2ts(SrsAacObjectTypeAacLC));
        EXPECT_TRUE(SrsAacProfileSSR == srs_aac_rtmp2ts(SrsAacObjectTypeAacSSR));
        EXPECT_TRUE(SrsAacProfileReserved == srs_aac_rtmp2ts(SrsAacObjectTypeReserved));
    }

    if (true) {
	    EXPECT_TRUE("Baseline" == srs_avc_profile2str(SrsAvcProfileBaseline));
	    EXPECT_TRUE("Baseline(Constrained)" == srs_avc_profile2str(SrsAvcProfileConstrainedBaseline ));
	    EXPECT_TRUE("Main" == srs_avc_profile2str(SrsAvcProfileMain));
	    EXPECT_TRUE("Extended" == srs_avc_profile2str(SrsAvcProfileExtended));
	    EXPECT_TRUE("High" == srs_avc_profile2str(SrsAvcProfileHigh ));
	    EXPECT_TRUE("High(10)" == srs_avc_profile2str(SrsAvcProfileHigh10 ));
	    EXPECT_TRUE("High(10+Intra)" == srs_avc_profile2str(SrsAvcProfileHigh10Intra));
	    EXPECT_TRUE("High(422)" == srs_avc_profile2str(SrsAvcProfileHigh422 ));
	    EXPECT_TRUE("High(422+Intra)" == srs_avc_profile2str(SrsAvcProfileHigh422Intra));
	    EXPECT_TRUE("High(444)" == srs_avc_profile2str(SrsAvcProfileHigh444 ));
	    EXPECT_TRUE("High(444+Predictive)" == srs_avc_profile2str(SrsAvcProfileHigh444Predictive));
        EXPECT_TRUE("High(444+Intra)" == srs_avc_profile2str(SrsAvcProfileHigh444Intra));
        EXPECT_TRUE("Other" == srs_avc_profile2str(SrsAvcProfileReserved));
    }

    if (true) {
	    EXPECT_TRUE("1" == srs_avc_level2str(SrsAvcLevel_1));
	    EXPECT_TRUE("1.1" == srs_avc_level2str(SrsAvcLevel_11));
	    EXPECT_TRUE("1.2" == srs_avc_level2str(SrsAvcLevel_12));
	    EXPECT_TRUE("1.3" == srs_avc_level2str(SrsAvcLevel_13));
	    EXPECT_TRUE("2" == srs_avc_level2str(SrsAvcLevel_2));
	    EXPECT_TRUE("2.1" == srs_avc_level2str(SrsAvcLevel_21));
	    EXPECT_TRUE("2.2" == srs_avc_level2str(SrsAvcLevel_22));
	    EXPECT_TRUE("3" == srs_avc_level2str(SrsAvcLevel_3));
	    EXPECT_TRUE("3.1" == srs_avc_level2str(SrsAvcLevel_31));
	    EXPECT_TRUE("3.2" == srs_avc_level2str(SrsAvcLevel_32));
	    EXPECT_TRUE("4" == srs_avc_level2str(SrsAvcLevel_4));
	    EXPECT_TRUE("4.1" == srs_avc_level2str(SrsAvcLevel_41));
	    EXPECT_TRUE("5" == srs_avc_level2str(SrsAvcLevel_5));
        EXPECT_TRUE("5.1" == srs_avc_level2str(SrsAvcLevel_51));
        EXPECT_TRUE("Other" == srs_avc_level2str(SrsAvcLevelReserved));
    }

    if (true) {
        SrsAudioCodecConfig acc;
        EXPECT_TRUE(!acc.is_aac_codec_ok());
        
        acc.aac_extra_data.push_back('\xff');
        EXPECT_TRUE(acc.is_aac_codec_ok());

        SrsVideoCodecConfig vcc;
        EXPECT_TRUE(!vcc.is_avc_codec_ok());

        vcc.avc_extra_data.push_back('\xff');
        EXPECT_TRUE(vcc.is_avc_codec_ok());
    }
}

VOID TEST(KernelCodecTest, AVFrame)
{
	srs_error_t err;

	if (true) {
		SrsAudioFrame f;
        SrsAudioCodecConfig* cc = new SrsAudioCodecConfig();
        SrsUniquePtr<SrsAudioCodecConfig> cc_uptr(cc);
        HELPER_EXPECT_SUCCESS(f.initialize(cc));
        EXPECT_TRUE(f.acodec() != NULL);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)1, 10));
        EXPECT_TRUE((char*)1 == f.samples[0].bytes);
        EXPECT_TRUE(10 == f.samples[0].size);
        EXPECT_TRUE(1 == f.nb_samples);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)2, 20));
        EXPECT_TRUE((char*)2 == f.samples[1].bytes);
        EXPECT_TRUE(20 == f.samples[1].size);
        EXPECT_TRUE(2 == f.nb_samples);
	}

    if (true) {
        SrsAudioFrame f;
        EXPECT_TRUE(0 == f.nb_samples);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)1, 0));
        EXPECT_TRUE(0 == f.nb_samples);

        HELPER_EXPECT_SUCCESS(f.add_sample(NULL, 1));
        EXPECT_TRUE(0 == f.nb_samples);
    }

    if (true) {
        SrsAudioFrame f;
        for (int i = 0; i < SrsMaxNbSamples; i++) {
            HELPER_EXPECT_SUCCESS(f.add_sample((char*)(int64_t)(i + 1), i*10 + 1));
        }

        srs_error_t err = f.add_sample((char*)1, 1);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsVideoFrame f;
        SrsVideoCodecConfig* cc = new SrsVideoCodecConfig();
        SrsUniquePtr<SrsVideoCodecConfig> cc_uptr(cc);
        HELPER_EXPECT_SUCCESS(f.initialize(cc));
        EXPECT_TRUE(f.vcodec() != NULL);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)"\x05", 1));
        EXPECT_TRUE(f.has_idr == true);
        EXPECT_TRUE(f.first_nalu_type == SrsAvcNaluTypeIDR);
    }
    
    if (true) {
        SrsVideoFrame f;
        SrsVideoCodecConfig* cc = new SrsVideoCodecConfig();
        SrsUniquePtr<SrsVideoCodecConfig> cc_uptr(cc);
        HELPER_EXPECT_SUCCESS(f.initialize(cc));
        EXPECT_TRUE(f.vcodec() != NULL);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)"\x07", 1));
        EXPECT_TRUE(f.has_sps_pps == true);
    }
    
    if (true) {
        SrsVideoFrame f;
        SrsVideoCodecConfig* cc = new SrsVideoCodecConfig();
        SrsUniquePtr<SrsVideoCodecConfig> cc_uptr(cc);
        HELPER_EXPECT_SUCCESS(f.initialize(cc));
        EXPECT_TRUE(f.vcodec() != NULL);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)"\x08", 1));
        EXPECT_TRUE(f.has_sps_pps == true);
    }

    if (true) {
        SrsVideoFrame f;
        SrsVideoCodecConfig* cc = new SrsVideoCodecConfig();
        SrsUniquePtr<SrsVideoCodecConfig> cc_uptr(cc);
        HELPER_EXPECT_SUCCESS(f.initialize(cc));
        EXPECT_TRUE(f.vcodec() != NULL);

        HELPER_EXPECT_SUCCESS(f.add_sample((char*)"\x09", 1));
        EXPECT_TRUE(f.has_aud == true);
    }

    if (true) {
        SrsVideoFrame f;
        SrsVideoCodecConfig* cc = new SrsVideoCodecConfig();
        SrsUniquePtr<SrsVideoCodecConfig> cc_uptr(cc);
        HELPER_EXPECT_SUCCESS(f.initialize(cc));
        EXPECT_TRUE(f.vcodec() != NULL);

        for (int i = 0; i < SrsMaxNbSamples; i++) {
            HELPER_EXPECT_SUCCESS(f.add_sample((char*)"\x05", 1));
        }

        srs_error_t err = f.add_sample((char*)"\x05", 1);
        HELPER_EXPECT_FAILED(err);
    }
}

VOID TEST(KernelCodecTest, AVFrameNoConfig)
{
    srs_error_t err;

    if (true) {
        SrsAudioFrame f;
        HELPER_EXPECT_SUCCESS(f.add_sample((char*)1, 10));
    }

    if (true) {
        SrsVideoFrame f;
        HELPER_EXPECT_SUCCESS(f.add_sample((char*)"\x05", 1));
    }
}

VOID TEST(KernelCodecTest, IsSequenceHeaderSpecial)
{
	if (true) {
		SrsFormat f;
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.vcodec = new SrsVideoCodecConfig();
		f.video = new SrsVideoFrame();
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.vcodec->id = SrsVideoCodecIdAVC;
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.video->avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
		EXPECT_TRUE(f.is_avc_sequence_header());
	}

	if (true) {
		SrsFormat f;
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.vcodec = new SrsVideoCodecConfig();
		f.video = new SrsVideoFrame();
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.vcodec->id = SrsVideoCodecIdHEVC;
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.vcodec->id = SrsVideoCodecIdAV1;
		EXPECT_FALSE(f.is_avc_sequence_header());

		f.video->avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
		EXPECT_TRUE(f.is_avc_sequence_header());
	}
}

VOID TEST(KernelCodecTest, AudioFormat)
{
	srs_error_t err;

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        
        HELPER_EXPECT_SUCCESS(f.on_audio(0, NULL, 0));
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\x00", 0));
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\x00", 1));
    }

    // For MP3
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\x20", 1));
        EXPECT_TRUE(0 == f.nb_raw);
        EXPECT_TRUE(0 == f.audio->nb_samples);

        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\x20\x00", 2));
        EXPECT_TRUE(1 == f.nb_raw);
        EXPECT_TRUE(1 == f.audio->nb_samples);
        
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\x20\x00\x00", 3));
        EXPECT_TRUE(2 == f.nb_raw);
        EXPECT_TRUE(1 == f.audio->nb_samples);
    }

    // For AAC
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_audio(0, (char*)"\xa0", 1));

        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x12\x10", 4));
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xa0\x01", 2));
        EXPECT_TRUE(0 == f.nb_raw);
        EXPECT_TRUE(0 == f.audio->nb_samples);

        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xa0\x01\x00", 3));
        EXPECT_TRUE(1 == f.nb_raw);
        EXPECT_TRUE(1 == f.audio->nb_samples);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x12\x10", 4));
        EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, f.audio->aac_packet_type);
        EXPECT_EQ(2, f.acodec->aac_channels);
        EXPECT_EQ(4, f.acodec->aac_sample_rate);
        EXPECT_EQ(2, f.acodec->aac_object);
        
        EXPECT_EQ(SrsAudioChannelsStereo, f.acodec->sound_type);
        EXPECT_EQ(SrsAudioSampleRate44100, f.acodec->sound_rate);
        EXPECT_EQ(SrsAudioSampleBits16bit, f.acodec->sound_size);
        
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x01\x00", 3));
        EXPECT_EQ(SrsAudioAacFrameTraitRawData, f.audio->aac_packet_type);
        EXPECT_EQ(1, f.audio->nb_samples);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x13\x90", 4));
        EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, f.audio->aac_packet_type);
        EXPECT_EQ(7, f.acodec->aac_sample_rate);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x15\x10", 4));
        EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, f.audio->aac_packet_type);
        EXPECT_EQ(10, f.acodec->aac_sample_rate);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x01\x00", 3));
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x12\x10", 4));
        
        SrsBuffer b((char*)"\x20", 1);
        srs_error_t err = f.audio_aac_demux(&b, 0);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x12\x10", 4));
        
        SrsBuffer b((char*)"\x30", 1);
        srs_error_t err = f.audio_aac_demux(&b, 0);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsFormat f;
        srs_error_t err = f.on_audio(0, (char*)"\xaf\x00\x12", 3);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsFormat f;
        srs_error_t err = f.on_audio(0, (char*)"\xaf\x00\x02\x00", 4);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.on_aac_sequence_header((char*)"\x12\x10", 2));
        EXPECT_EQ(2, f.acodec->aac_channels);
        EXPECT_EQ(4, f.acodec->aac_sample_rate);
        EXPECT_EQ(2, f.acodec->aac_object);
    }
        
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_audio(0, (char*)"\xaf\x00\x12\x10", 4));
        EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, f.audio->aac_packet_type);
        EXPECT_TRUE(f.is_aac_sequence_header());
        EXPECT_TRUE(!f.is_avc_sequence_header());
    }
}

VOID TEST(KernelCodecTest, VideoFormatSepcial)
{
	srs_error_t err;

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x17", 1));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x27", 1));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x02, // lengthSizeMinusOne
            0x00,
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x00, // SPS
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x01, // SPS
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x01, 0x00, 0x00, // SPS, empty
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x01, 0x00, 0x00, // SPS, empty
            0x00, // PPS
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x01, 0x00, 0x00, // SPS
            0x01, // PPS
        };
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x01, 0x00, 0x00, // SPS, empty
            0x01, 0x00, 0x00, // PPS, empty
        };
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)buf, sizeof(buf)));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        uint8_t buf[] = {
            0x17, // 1, Keyframe; 7, AVC.
            0x00, // 0, Sequence header.
            0x00, 0x00, 0x00, // Timestamp.
            // AVC extra data, SPS/PPS.
            0x00, 0x00, 0x00, 0x00,
            0x00, // lengthSizeMinusOne
            0x02, 0x00, 0x00, 0x00, 0x00, // 2 SPS, 
            0x02, 0x00, 0x00, 0x00, 0x00  // 2 PPS, 
        };
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)buf, sizeof(buf)));
    }
}

VOID TEST(KernelCoecTest, VideoFormatRbspData)
{
    if (true) {
        vector<uint8_t> nalu = {
                0x25, 0x00, 0x1f, 0xe2, 0x22, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80, 0xab, 0xff
        };

        SrsBuffer b((char*)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)nalu.size());
        EXPECT_TRUE(srs_bytes_equals(rbsp.data(), nalu.data(), nb_rbsp));
    }

    if (true) {
        SrsFormat f;
        vector<uint8_t> nalu = {
                0x25, 0x00, 0x1f, 0xe2, 0x22, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x80, 0xab, 0xff
        };
        vector<uint8_t> expect = {
                0x25, 0x00, 0x1f, 0xe2, 0x22, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80, 0xab, 0xff
        };

        // |----------------|----------------------------|
        // |      rbsp      |  nalu with emulation bytes |
        // |----------------|----------------------------|
        // | 0x00 0x00 0x00 |     0x00 0x00 0x03 0x00    |
        // | 0x00 0x00 0x01 |     0x00 0x00 0x03 0x01    |
        // | 0x00 0x00 0x02 |     0x00 0x00 0x03 0x02    |
        // | 0x00 0x00 0x03 |     0x00 0x00 0x03 0x03    |
        // |----------------|----------------------------|
        for (int i = 0; i <= 3; ++i) {
            nalu[8] = uint8_t(i);
            expect[7] = uint8_t(i);

            SrsBuffer b((char*)nalu.data(), nalu.size());
            vector<uint8_t> rbsp(nalu.size());
            int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

            ASSERT_EQ(nb_rbsp, (int)expect.size());
            EXPECT_TRUE(srs_bytes_equals(rbsp.data(), expect.data(), nb_rbsp));
        }

        // 0x00 0x00 0x04 ~ 0x00 0x00 0xFF, no need to add emulation bytes.
        for (int i = 4; i <= 0xff; ++i) {
            nalu[8] = uint8_t(i);

            SrsBuffer b((char*)nalu.data(), nalu.size());
            vector<uint8_t> rbsp(nalu.size());
            int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

            ASSERT_EQ(nb_rbsp, (int)nalu.size());
            EXPECT_TRUE(srs_bytes_equals(rbsp.data(), nalu.data(), nb_rbsp));
        }
    }

    if (true) {
        vector<uint8_t> nalu = {
                0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x02, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x04
        };
        vector<uint8_t> expect = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x04
        };

        SrsBuffer b((char*)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equals(rbsp.data(), expect.data(), nb_rbsp));
    }
}

VOID TEST(KernelCodecTest, VideoFormat)
{
	srs_error_t err;

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, NULL, 0));
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x00", 0));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x00", 1));
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x57", 1));
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x57", 1));
        
        SrsBuffer b((char*)"\x00", 1);
        srs_error_t err = f.video_avc_demux(&b, 0);
        HELPER_EXPECT_FAILED(err);
    }
    
    uint8_t spspps[] = {
        0x17,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
        0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
        0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
    };
    uint8_t rawIBMF[] = {
        0x27,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x35, 0x01, 0x9e, 0x82, 0x74, 0x43, 0xdf, 0x00, 0x16,
        0x02, 0x5b, 0x65, 0xa4, 0xbd, 0x42, 0x77, 0xfc, 0x23, 0x61, 0x5e, 0xc2, 0xc9, 0xe9, 0xf8, 0x50,
        0xd9, 0xaf, 0xc7, 0x49, 0xdc, 0xb6, 0x3a, 0xd4, 0xb5, 0x80, 0x02, 0x04, 0xac, 0xe7, 0x97, 0xc1,
        0xbf, 0xea, 0xf0, 0x13, 0x36, 0xd2, 0xa4, 0x0b, 0x6a, 0xc4, 0x32, 0x22, 0xe1
    };
    uint8_t rawAnnexb[] = {
        0x27,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x9e, 0x82, 0x74, 0x43, 0xdf, 0x00, 0x16,
        0x02, 0x5b, 0x65, 0xa4, 0xbd, 0x42, 0x77, 0xfc, 0x23, 0x61, 0x5e, 0xc2, 0xc9, 0xe9, 0xf8, 0x50,
        0xd9, 0xaf, 0xc7, 0x49, 0xdc, 0xb6, 0x3a, 0xd4, 0xb5, 0x80, 0x02, 0x04, 0xac, 0xe7, 0x97, 0xc1,
        0xbf, 0xea, 0xf0, 0x13, 0x36, 0xd2, 0xa4, 0x0b, 0x6a, 0xc4, 0x32, 0x22, 0xe1
    };
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)spspps, sizeof(spspps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);
        
        EXPECT_EQ(768, f.vcodec->width);
        EXPECT_EQ(320, f.vcodec->height);
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawIBMF, sizeof(rawIBMF)));
        EXPECT_EQ(1, f.video->nb_samples);
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawIBMF, sizeof(rawIBMF)));
        EXPECT_EQ(1, f.video->nb_samples);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)spspps, sizeof(spspps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawAnnexb, sizeof(rawAnnexb)));
        EXPECT_EQ(1, f.video->nb_samples);
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawAnnexb, sizeof(rawAnnexb)));
        EXPECT_EQ(1, f.video->nb_samples);
        
        f.vcodec->payload_format = SrsAvcPayloadFormatAnnexb;
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawAnnexb, sizeof(rawAnnexb)));
        EXPECT_EQ(1, f.video->nb_samples);
    }
    
    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());
        
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)spspps, sizeof(spspps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);

        // If force to ANNEXB, other format is failed to parse.
        f.vcodec->payload_format = SrsAvcPayloadFormatAnnexb;

        HELPER_EXPECT_FAILED(f.on_video(0, (char*)rawIBMF, sizeof(rawIBMF)));
        EXPECT_EQ(0, f.video->nb_samples);

        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawAnnexb, sizeof(rawAnnexb)));
        EXPECT_EQ(1, f.video->nb_samples);

        HELPER_EXPECT_FAILED(f.on_video(0, (char*)rawIBMF, sizeof(rawIBMF)));
        EXPECT_EQ(0, f.video->nb_samples);
    }
}

#ifdef SRS_H265
VOID TEST(KernelCodecTest, HevcVideoFormat)
{
    srs_error_t err;

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());

        HELPER_EXPECT_SUCCESS(f.on_video(0, NULL, 0));
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x00", 0));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x00", 1));

        // enhanced rtmp/flv
        HELPER_EXPECT_SUCCESS(f.on_video(0, NULL, 0));
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x80", 0));
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x90", 0));
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\xd0\x68\x76\x63\x31", 5));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x80", 1));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x90", 1));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x90\x68\x76\x63\x31", 5));
    }

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());

        //HEVC: 0x5c
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)"\x5c", 1));
        HELPER_EXPECT_FAILED(f.on_video(0, (char*)"\x1c", 1));

        // CodecId: 0x00
        SrsBuffer b((char*)"\x00", 1);
        srs_error_t err = f.video_avc_demux(&b, 0);
        HELPER_EXPECT_FAILED(err);

        // enhanced rtmp/flv
        SrsBuffer b1((char*)"\x80", 1);
        HELPER_EXPECT_FAILED(f.video_avc_demux(&b1, 0));
        SrsBuffer b2((char*)"\x90", 1);
        HELPER_EXPECT_FAILED(f.video_avc_demux(&b2, 0));
        SrsBuffer b3((char*)"\x90\x68\x76\x63\x31", 5);
        HELPER_EXPECT_FAILED(f.video_avc_demux(&b3, 0));
        SrsBuffer b4((char*)"\xd0\x68\x76\x63\x31", 5);
        HELPER_EXPECT_SUCCESS(f.video_avc_demux(&b4, 0));
    }

    uint8_t vps_sps_pps[] = {
        // frame_type
        0x1c,
        // avc_packet_type
        0x00,
        // composition_time
        0x00, 0x00, 0x00,
        // SrsHevcDecoderConfigurationRecord
        0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5d, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x03,
        // Nalus
        // data_byte(1B)+num_nalus(2B)+nal_unit_length(2B)
        0x20, 0x00, 0x01, 0x00, 0x18,
        // VPS
        0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0x95, 0x98, 0x09,
        // data_byte(1B)+num_nalus(2B)+nal_unit_length(2B)
        0x21, 0x00, 0x01, 0x00, 0x28,
        // SPS
        0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16,
        0x59, 0x59, 0xa4, 0x93, 0x2b, 0xc0, 0x40, 0x40, 0x00, 0x00, 0xfa, 0x40, 0x00, 0x17, 0x70, 0x02,
        // data_byte(1B)+num_nalus(2B)+nal_unit_length(2B)
        0x22, 0x00, 0x01, 0x00, 0x07,
        // PPS
        0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40
    };
    uint8_t rawIBMF[] = {
        0x2c,
        0x01,
        0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0b,
        0x28, 0x1, 0xaf, 0x1d, 0x18, 0x38, 0xd4, 0x38, 0x32, 0xda, 0x23
    };

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());

        // firstly demux sequence header
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)vps_sps_pps, sizeof(vps_sps_pps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);
        EXPECT_EQ(3, f.vcodec->hevc_dec_conf_record_.nalu_vec.size());
        EXPECT_EQ(1280, f.vcodec->width);
        EXPECT_EQ(720, f.vcodec->height);

        // secondly demux sequence header
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)vps_sps_pps, sizeof(vps_sps_pps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);
        EXPECT_EQ(3, f.vcodec->hevc_dec_conf_record_.nalu_vec.size());
        EXPECT_EQ(1280, f.vcodec->width);
        EXPECT_EQ(720, f.vcodec->height);

        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawIBMF, sizeof(rawIBMF)));
        EXPECT_EQ(1, f.video->nb_samples);

        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)rawIBMF, sizeof(rawIBMF)));
        EXPECT_EQ(1, f.video->nb_samples);
    }

    // enhanced rtmp
    uint8_t ext_vps_sps_pps[] = {
        // IsExHeader | FrameType: UB[4]
        // PacketType: UB[4]
        0x90,
        // Video FourCC
        0x68, 0x76, 0x63, 0x31,
        // SrsHevcDecoderConfigurationRecord
        0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5d, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x03,
        // Nalus
        // data_byte(1B)+num_nalus(2B)+nal_unit_length(2B)
        0x20, 0x00, 0x01, 0x00, 0x18,
        // VPS
        0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0x95, 0x98, 0x09,
        // data_byte(1B)+num_nalus(2B)+nal_unit_length(2B)
        0x21, 0x00, 0x01, 0x00, 0x28,
        // SPS
        0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16,
        0x59, 0x59, 0xa4, 0x93, 0x2b, 0xc0, 0x40, 0x40, 0x00, 0x00, 0xfa, 0x40, 0x00, 0x17, 0x70, 0x02,
        // data_byte(1B)+num_nalus(2B)+nal_unit_length(2B)
        0x22, 0x00, 0x01, 0x00, 0x07,
        // PPS
        0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40
    };

    uint8_t ext_rawIBMF[] = {
        // IsExHeader | FrameType: UB[4]
        // PacketType: UB[4]
        0x93,
        // Video FourCC
        0x68, 0x76, 0x63, 0x31,
        // HEVC NALU
        0x00, 0x00, 0x00, 0x0b,
        0x28, 0x1, 0xaf, 0x1d, 0x18, 0x38, 0xd4, 0x38, 0x32, 0xda, 0x23
    };

    uint8_t ext_rawIBMF1[] = {
        // IsExHeader | FrameType: UB[4]
        // PacketType: UB[4]
        0x91,
        // Video FourCC
        0x68, 0x76, 0x63, 0x31,
        // CompositionTime Offset
        0x00, 0x00, 0x7d,
        // HEVC NALU
        0x00, 0x00, 0x00, 0x0b,
        0x28, 0x1, 0xaf, 0x1d, 0x18, 0x38, 0xd4, 0x38, 0x32, 0xda, 0x23
    };

    if (true) {
        SrsFormat f;
        HELPER_EXPECT_SUCCESS(f.initialize());

        // firstly demux sequence header
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)ext_vps_sps_pps, sizeof(ext_vps_sps_pps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);
        EXPECT_EQ(3, f.vcodec->hevc_dec_conf_record_.nalu_vec.size());
        EXPECT_EQ(1280, f.vcodec->width);
        EXPECT_EQ(720, f.vcodec->height);

        // secondly demux sequence header
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)ext_vps_sps_pps, sizeof(ext_vps_sps_pps)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(0, f.video->avc_packet_type);
        EXPECT_EQ(3, f.vcodec->hevc_dec_conf_record_.nalu_vec.size());
        EXPECT_EQ(1280, f.vcodec->width);
        EXPECT_EQ(720, f.vcodec->height);

        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)ext_rawIBMF, sizeof(ext_rawIBMF)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(3, f.video->avc_packet_type);
        EXPECT_EQ(1, f.video->nb_samples);

        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)ext_rawIBMF, sizeof(ext_rawIBMF)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(3, f.video->avc_packet_type);
        EXPECT_EQ(1, f.video->nb_samples);

        // check cts
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)ext_rawIBMF1, sizeof(ext_rawIBMF1)));
        EXPECT_EQ(1, f.video->frame_type);
        EXPECT_EQ(125, f.video->cts);
        EXPECT_EQ(1, f.video->avc_packet_type);
        EXPECT_EQ(1, f.video->nb_samples);
    }
}
#endif

VOID TEST(KernelFileTest, FileWriteReader)
{
	srs_error_t err;

    if (true) {
        SrsFileWriter f;
        EXPECT_TRUE(!f.is_open());
    }
    
    if (true) {
        SrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
        EXPECT_TRUE(f.is_open());
        
        EXPECT_EQ(0, f.tellg());
        
        ssize_t nwriten = 0;
        HELPER_EXPECT_SUCCESS(f.write((void*)"Hello", 5, &nwriten));
        EXPECT_EQ(5, nwriten);
        
        HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_CUR, NULL));
        
        f.seek2(0);
        EXPECT_EQ(0, f.tellg());
    }
    
    if (true) {
        SrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open_append("/dev/null"));
        EXPECT_TRUE(f.is_open());
    }
    
    if (true) {
        SrsFileReader f;
        EXPECT_TRUE(!f.is_open());
    }
    
    if (true) {
        SrsFileReader f;
        HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
        EXPECT_TRUE(f.is_open());
        EXPECT_EQ(0, f.tellg());
        EXPECT_EQ(0, f.filesize());
        
        f.skip(1);
        
        f.seek2(0);
        EXPECT_EQ(0, f.tellg());
    }
    
    if (true) {
        SrsFileReader f;
        HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
        
        char buf[16];
        ssize_t nread = 0;
        srs_error_t err = f.read((void*)buf, sizeof(buf), &nread);
        EXPECT_EQ(ERROR_SYSTEM_FILE_EOF, srs_error_code(err));
        srs_freep(err);
        
        f.lseek(1, SEEK_CUR, NULL);
    }
}

// Mock the system call hooks.
extern srs_open_t _srs_open_fn;
extern srs_write_t _srs_write_fn;
extern srs_read_t _srs_read_fn;
extern srs_lseek_t _srs_lseek_fn;
extern srs_close_t _srs_close_fn;

int mock_open(const char* /*path*/, int /*oflag*/, ...) {
	return -1;
}

ssize_t mock_write(int /*fildes*/, const void* /*buf*/, size_t /*nbyte*/) {
	return -1;
}

ssize_t mock_read(int /*fildes*/, void* /*buf*/, size_t /*nbyte*/) {
	return -1;
}

off_t mock_lseek(int /*fildes*/, off_t /*offset*/, int /*whence*/) {
	return -1;
}

int mock_close(int /*fildes*/) {
	return -1;
}

class MockSystemIO
{
private:
	srs_open_t oo;
	srs_write_t ow;
	srs_read_t _or;
	srs_lseek_t os;
	srs_close_t oc;
public:
	MockSystemIO(srs_open_t o = NULL, srs_write_t w = NULL, srs_read_t r = NULL, srs_lseek_t s = NULL, srs_close_t c = NULL) {
		oo = _srs_open_fn;
		ow = _srs_write_fn;
		os = _srs_lseek_fn;
		_or = _srs_read_fn;
		oc = _srs_close_fn;
		if (o) {
			_srs_open_fn = o;
		}
		if (w) {
			_srs_write_fn = w;
		}
		if (r) {
			_srs_read_fn = r;
		}
		if (s) {
			_srs_lseek_fn = s;
		}
		if (c) {
			_srs_close_fn = c;
		}
	}
	virtual ~MockSystemIO() {
		if (oo) {
			_srs_open_fn = oo;
		}
		if (ow) {
			_srs_write_fn = ow;
		}
		if (_or) {
			_srs_read_fn = _or;
		}
		if (os) {
			_srs_lseek_fn = os;
		}
		if (oc) {
			_srs_close_fn = oc;
		}
	}
};


extern srs_fopen_t _srs_fopen_fn;
extern srs_fwrite_t _srs_fwrite_fn;
extern srs_fread_t _srs_fread_fn;
extern srs_fseek_t _srs_fseek_fn;
extern srs_fclose_t _srs_fclose_fn;
extern srs_ftell_t _srs_ftell_fn;
extern srs_setvbuf_t _srs_setvbuf_fn;


FILE* mock_fopen(const char* path, const char*  mode) {
	return NULL;
}

size_t mock_fwrite(const void* ptr, size_t size, size_t nitems, FILE* stream) {
	return -1;
}

size_t mock_fread(void* ptr, size_t size, size_t nitems, FILE* stream) {
	return -1;
}

int mock_fseek(FILE* stream, long offset, int whence) {
    return -1;
}

int mock_fclose(FILE *stream) {
	return -1;
}

long mock_ftell(FILE *stream) {
	return -1;
}

int mock_setvbuf(FILE* stream, char* buf, int type, size_t size) {
	return -1;
}

class MockLibcIO
{
private:
	srs_fopen_t oo_;
	srs_fwrite_t ow_;
	srs_fread_t or_;
	srs_fseek_t os_;
	srs_fclose_t oc_;
	srs_ftell_t ot_;
	srs_setvbuf_t osb_;

public:
	MockLibcIO(srs_fopen_t o = NULL, srs_fwrite_t w = NULL, srs_fread_t r = NULL, 
					srs_fseek_t s = NULL, srs_fclose_t c = NULL, srs_ftell_t t = NULL,
					srs_setvbuf_t sb = NULL) {
		oo_ = _srs_fopen_fn;
		ow_ = _srs_fwrite_fn;
		os_ = _srs_fseek_fn;
		or_ = _srs_fread_fn;
		oc_ = _srs_fclose_fn;
		ot_ = _srs_ftell_fn;
		osb_= _srs_setvbuf_fn;

		if (o) {
			_srs_fopen_fn = o;
		}
		if (w) {
			_srs_fwrite_fn = w;
		}
		if (r) {
			_srs_fread_fn = r;
		}
		if (s) {
			_srs_fseek_fn = s;
		}
		if (c) {
			_srs_fclose_fn = c;
		}
		if (t) {
			_srs_ftell_fn = t;
		}
		if (sb){
			_srs_setvbuf_fn = sb;
		}
	}
	virtual ~MockLibcIO() {
		if (oo_) {
			_srs_fopen_fn = oo_;
		}
		if (ow_) {
			_srs_fwrite_fn = ow_;
		}
		if (or_) {
			_srs_fread_fn = or_;
		}
		if (os_) {
			_srs_fseek_fn = os_;
		}
		if (oc_) {
			_srs_fclose_fn = oc_;
		}
		if (ot_) {
			_srs_ftell_fn = ot_;
		}
		if (osb_) {
			_srs_setvbuf_fn = osb_;
		}
	}
};

VOID TEST(KernelFileWriterTest, WriteSpecialCase)
{
	srs_error_t err;

	// Should fail when open multiple times.
	if (true) {
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
		HELPER_EXPECT_FAILED(f.open("/dev/null"));
	}

	// Should fail when open multiple times.
	if (true) {
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open_append("/dev/null"));
		HELPER_EXPECT_FAILED(f.open_append("/dev/null"));
	}

	if (true) {
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open_append("/dev/null"));   
		HELPER_EXPECT_SUCCESS(f.set_iobuf_size(65536));
	}

	// Always fail.
	if (true) {
		MockLibcIO _mockio(mock_fopen);
		SrsFileWriter f;
		HELPER_EXPECT_FAILED(f.open("/dev/null"));
		HELPER_EXPECT_FAILED(f.open("/dev/null"));
	}
	
	if (true) {
		MockLibcIO _mockio(mock_fopen);
		SrsFileWriter f;
		HELPER_EXPECT_FAILED(f.open_append("/dev/null"));
		HELPER_EXPECT_FAILED(f.open_append("/dev/null"));
	}

	// Should ok for write, writev or lseek.
	if (true) {
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		ssize_t nn = 0;
		HELPER_EXPECT_SUCCESS(f.write((void*)"Hello", 5, &nn));
		EXPECT_EQ(5, nn);

		iovec iovs[3];
		iovs[0].iov_base = (void*)"H";
		iovs[0].iov_len = 1;
		iovs[1].iov_base = (void*)"e";
		iovs[1].iov_len = 1;
		iovs[2].iov_base = (void*)"llo";
		iovs[2].iov_len = 3;
		nn = 0;
		HELPER_EXPECT_SUCCESS(f.writev(iovs, 3, &nn));
		EXPECT_EQ(5, nn);

		off_t seeked = 0;
		HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_CUR, &seeked));
#ifdef SRS_OSX
		EXPECT_EQ(10, seeked);
#else
		EXPECT_EQ(0, seeked);
#endif
	}

	// Always fail.
	if (true) {
		MockLibcIO _mockio(NULL, mock_fwrite);
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		ssize_t nn = 0;
		HELPER_EXPECT_FAILED(f.write((void*)"Hello", 5, &nn));

		iovec iovs[3];
		iovs[0].iov_base = (void*)"H";
		iovs[0].iov_len = 1;
		iovs[1].iov_base = (void*)"e";
		iovs[1].iov_len = 1;
		iovs[2].iov_base = (void*)"llo";
		iovs[2].iov_len = 3;
		HELPER_EXPECT_FAILED(f.writev(iovs, 3, NULL));
	}
	if (true) {
		MockLibcIO _mockio(NULL, NULL, NULL, mock_fseek);
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		HELPER_EXPECT_FAILED(f.lseek(0, 0, NULL));
	}
	if (true) {
		MockLibcIO _mockio(NULL, NULL, NULL, NULL, mock_fclose);
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
		f.close();
	}

	if (true) {
		MockLibcIO _mockio(NULL, NULL, NULL, NULL, NULL, NULL, mock_setvbuf);
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		HELPER_EXPECT_FAILED(f.set_iobuf_size(100));
	}

    if (true) {
		MockLibcIO _mockio(NULL, NULL, NULL, NULL, NULL, mock_ftell);
		SrsFileWriter f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		EXPECT_EQ(f.tellg(), -1);
	}
}

VOID TEST(KernelFileReaderTest, WriteSpecialCase)
{
	srs_error_t err;

	// Should fail when open multiple times.
	if (true) {
		SrsFileReader f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
		HELPER_EXPECT_FAILED(f.open("/dev/null"));
	}

	// Always fail.
	if (true) {
		MockSystemIO _mockio(mock_open);
		SrsFileReader f;
		HELPER_EXPECT_FAILED(f.open("/dev/null"));
		HELPER_EXPECT_FAILED(f.open("/dev/null"));
	}

	// Should ok for lseek.
	if (true) {
		SrsFileReader f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		off_t seeked = 0;
		HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_CUR, &seeked));
		EXPECT_EQ(0, seeked);
	}

	// Always fail.
	if (true) {
		MockSystemIO _mockio(NULL, NULL, mock_read);
		SrsFileReader f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		ssize_t nn = 0;
		char buf[16];
		HELPER_EXPECT_FAILED(f.read(buf, sizeof(buf), &nn));
	}
	if (true) {
		MockSystemIO _mockio(NULL, NULL, NULL, mock_lseek);
		SrsFileReader f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));

		HELPER_EXPECT_FAILED(f.lseek(0, 0, NULL));
	}
	if (true) {
		MockSystemIO _mockio(NULL, NULL, NULL, NULL, mock_close);
		SrsFileReader f;
		HELPER_EXPECT_SUCCESS(f.open("/dev/null"));
		f.close();
	}
}

MockFileRemover::MockFileRemover(string p)
{
    path_ = p;
}

MockFileRemover::~MockFileRemover()
{
    // Only remove {_srs_tmp_file_prefix}*.log file.
    if (path_.find(_srs_tmp_file_prefix) != 0) return;
    if (path_.find(".log") == string::npos) return;
    ::unlink(path_.c_str());
}

VOID TEST(KernelFileTest, ReadWriteCase)
{
	srs_error_t err;

	string filepath = _srs_tmp_file_prefix + "kernel-file-read-write-case.log";
	MockFileRemover _mfr(filepath);

	SrsFileWriter w;
	HELPER_EXPECT_SUCCESS(w.open(filepath.c_str()));

	SrsFileReader r;
	HELPER_EXPECT_SUCCESS(r.open(filepath.c_str()));

	ssize_t nn = 0;
	HELPER_EXPECT_SUCCESS(w.write((void*)"Hello", 5, &nn));
	EXPECT_EQ(5, nn);

	w.close();

	char buf[16] = {0};
	HELPER_EXPECT_SUCCESS(r.read(buf, sizeof(buf), &nn));
	EXPECT_EQ(5, nn);

	EXPECT_STREQ("Hello", buf);
}


VOID TEST(KernelFileTest, SeekCase)
{
	srs_error_t err;

	string filepath = _srs_tmp_file_prefix + "kernel-file-read-write-case.log";
	MockFileRemover _mfr(filepath);

	SrsFileWriter w;
	HELPER_EXPECT_SUCCESS(w.open(filepath.c_str()));

	HELPER_EXPECT_SUCCESS(w.set_iobuf_size(65536));

	SrsFileReader r;
	HELPER_EXPECT_SUCCESS(r.open(filepath.c_str()));

	ssize_t nn = 0;
	HELPER_EXPECT_SUCCESS(w.write((void*)"Hello", 5, &nn));
	EXPECT_EQ(5, nn);

	// over 4g file test
	long seek_pos = 0x100000002l;
	off_t pos;  
	HELPER_EXPECT_SUCCESS(w.lseek(seek_pos, SEEK_SET, &pos));
	EXPECT_EQ(seek_pos, pos);

	HELPER_EXPECT_SUCCESS(w.write((void*)"World", 5, &nn));
	EXPECT_EQ(5, nn);

	w.close();

	char buf[16] = {0};
	HELPER_EXPECT_SUCCESS(r.read(buf, 5, &nn));
	EXPECT_EQ(5, nn);

	EXPECT_STREQ("Hello", buf);

	HELPER_EXPECT_SUCCESS(r.lseek(seek_pos, SEEK_SET, NULL));
	HELPER_EXPECT_SUCCESS(r.read(buf, 5, &nn));
	EXPECT_EQ(5, nn);

	EXPECT_STREQ("World", buf);
}

VOID TEST(KernelFLVTest, CoverAll)
{
	srs_error_t err;

    if (true) {
        SrsMessageHeader h;
        h.message_type = RTMP_MSG_SetChunkSize;
        EXPECT_TRUE(h.is_set_chunk_size());
        
        h.message_type = RTMP_MSG_SetPeerBandwidth;
        EXPECT_TRUE(h.is_set_peer_bandwidth());
        
        h.message_type = RTMP_MSG_AggregateMessage;
        EXPECT_TRUE(h.is_aggregate());
        
        h.initialize_amf0_script(10, 20);
        EXPECT_EQ(RTMP_MSG_AMF0DataMessage, h.message_type);
        EXPECT_EQ(10, h.payload_length);
        EXPECT_EQ(20, h.stream_id);
        
        h.initialize_audio(10, 30, 20);
        EXPECT_EQ(RTMP_MSG_AudioMessage, h.message_type);
        EXPECT_EQ(10, h.payload_length);
        EXPECT_EQ(20, h.stream_id);
        EXPECT_EQ(30, h.timestamp_delta);
        EXPECT_EQ(30, h.timestamp);
        
        h.initialize_video(10, 30, 20);
        EXPECT_EQ(RTMP_MSG_VideoMessage, h.message_type);
        EXPECT_EQ(10, h.payload_length);
        EXPECT_EQ(20, h.stream_id);
        EXPECT_EQ(30, h.timestamp_delta);
        EXPECT_EQ(30, h.timestamp);
    }
    
    if (true) {
        SrsMessageHeader h;
        h.initialize_video(10, 30, 20);
        
        SrsCommonMessage m;
        HELPER_EXPECT_SUCCESS(m.create(&h, NULL, 0));
        EXPECT_EQ(RTMP_MSG_VideoMessage, m.header.message_type);
        EXPECT_EQ(10, m.header.payload_length);
        EXPECT_EQ(20, m.header.stream_id);
        EXPECT_EQ(30, m.header.timestamp_delta);
        EXPECT_EQ(30, m.header.timestamp);
        
        SrsSharedPtrMessage s;
        HELPER_EXPECT_SUCCESS(s.create(&m));
        EXPECT_TRUE(s.is_av());
        EXPECT_TRUE(!s.is_audio());
        EXPECT_TRUE(s.is_video());
    }
    
    if (true) {
        MockSrsFileWriter f;
        SrsFlvTransmuxer mux;
        HELPER_EXPECT_SUCCESS(mux.initialize(&f));
        
        SrsMessageHeader h;
        h.initialize_video(10, 30, 20);
        
        SrsSharedPtrMessage m;
        HELPER_EXPECT_SUCCESS(m.create(&h, new char[1], 1));
        
        SrsSharedPtrMessage* msgs = &m;
        HELPER_EXPECT_SUCCESS(mux.write_tags(&msgs, 1));
        
        EXPECT_EQ(16, f.tellg());
    }
}

VOID TEST(KernelFLVTest, CoverSharedPtrMessage)
{
	srs_error_t err;

	if (true) {
		SrsMessageHeader h;
		SrsSharedPtrMessage m;
		HELPER_EXPECT_SUCCESS(m.create(&h, new char[1], 1));
	}

	if (true) {
		SrsMessageHeader h;
		SrsSharedPtrMessage m;
		HELPER_EXPECT_SUCCESS(m.create(&h, NULL, 0));
	}

	if (true) {
		SrsMessageHeader h;
		SrsSharedPtrMessage m;
		HELPER_EXPECT_FAILED(m.create(&h, NULL, -1));
	}

	if (true) {
		SrsCommonMessage cm;
		cm.size = -1;

		SrsSharedPtrMessage m;
		HELPER_EXPECT_FAILED(m.create(&cm));
	}

	if (true) {
		SrsMessageHeader h;
		h.prefer_cid = 1;

		SrsSharedPtrMessage m;
		HELPER_EXPECT_SUCCESS(m.create(&h, NULL, 0));

		EXPECT_FALSE(m.check(1));
		EXPECT_TRUE(m.check(1));
	}
}

VOID TEST(KernelMp3Test, CoverAll)
{
	srs_error_t err;

    if (true) {
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open(""));

        SrsMp3Transmuxer m;
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        HELPER_EXPECT_SUCCESS(m.write_header());
        EXPECT_EQ((char)0x49, f.data()[0]);
    }
    
    if (true) {
        SrsMp3Transmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open(""));
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        HELPER_EXPECT_SUCCESS(m.write_audio(0, (char*)"\x20\x01", 2));
        EXPECT_EQ((char)0x01, f.data()[0]);
    }
    
    if (true) {
        SrsMp3Transmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open(""));
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        srs_error_t err = m.write_audio(0, (char*)"\x30\x01", 2);
        HELPER_EXPECT_FAILED(err);
        
        err = m.write_audio(0, (char*)"\x20", 1);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsMp3Transmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open(""));
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        f.err = srs_error_new(-1, "mock file error");
        srs_error_t err = m.write_audio(0, (char*)"\x20\x01", 2);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsMp3Transmuxer m;
        MockSrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open(""));
        HELPER_EXPECT_SUCCESS(m.initialize(&f));
        
        f.err = srs_error_new(-1, "mock file error");
        srs_error_t err = m.write_header();
        HELPER_EXPECT_FAILED(err);
    }
}

/**
* test the bitbuffer utility, access require
*/
VOID TEST(KernelUtilityTest, BitBufferRequire)
{
    char data[128];
    SrsBuffer b(data, 128);
    SrsBitBuffer bb(&b);

    EXPECT_TRUE(bb.require_bits(1));
    EXPECT_TRUE(bb.require_bits(1024));

    bb.read_bits(1000);
    EXPECT_TRUE(bb.require_bits(1));

    bb.read_bits(24);
    EXPECT_FALSE(bb.require_bits(1));
}

VOID TEST(KernelUtilityTest, CoverBitsBufferAll)
{
    if (true) {
        SrsBuffer b(NULL, 0);
        SrsBitBuffer bb(&b);
        
        int32_t v = 0;
        srs_error_t err = srs_avc_nalu_read_uev(&bb, v);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsBuffer b((char*)"\x00", 1);
        SrsBitBuffer bb(&b);
        
        int32_t v = 0;
        srs_error_t err = srs_avc_nalu_read_uev(&bb, v);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsBuffer b((char*)"\x00\x00\x00\x00", 4);
        SrsBitBuffer bb(&b);
        
        int32_t v = 0;
        srs_error_t err = srs_avc_nalu_read_uev(&bb, v);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsBuffer b(NULL, 0);
        SrsBitBuffer bb(&b);
        
        int8_t v = 0;
        srs_error_t err = srs_avc_nalu_read_bit(&bb, v);
        HELPER_EXPECT_FAILED(err);
    }

    if (true) {
        SrsBuffer b((char*)"\x20\x01", 2);
        SrsBitBuffer bb(&b);

        int8_t v = bb.read_8bits();
        EXPECT_EQ(0x20, v);
    }

    if (true) {
        SrsBuffer b((char*)"\x04\x00\x01\x01", 4);
        SrsBitBuffer bb(&b);

        int16_t v = bb.read_16bits();
        EXPECT_EQ(0x0400, v);
    }

    if (true) {
        SrsBuffer b((char*)"\x00\x00\x04\x00\x01\x01", 6);
        SrsBitBuffer bb(&b);

        int32_t v = bb.read_32bits();
        EXPECT_EQ(0x0400, v);
    }

    if (true) {
        SrsBuffer b((char*)"\x00\x28\x08\x02\xd1\x65\x95\x9a", 8);
        SrsBitBuffer bb(&b);

        uint32_t v = 0;
        srs_error_t err = bb.read_bits_ue(v);
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_EQ(1280, (int)v);

        err = bb.read_bits_ue(v);
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_EQ(720, (int)v);
    }

    if (true) {
        SrsBuffer b((char*)"\x10\x00\x04\x00\x01\x01\x04\x00", 8);
        SrsBitBuffer bb(&b);

        int32_t v = bb.read_bits(1);
        EXPECT_EQ(0, v);

        v = bb.read_bits(7);
        EXPECT_EQ(0x10, v);

        v = bb.read_bits(13);
        EXPECT_EQ(0, v);

        v = bb.read_bits(20);
        //100 0000 0000 0000 0001 0
        EXPECT_EQ(0x80002, v);

        v = bb.read_bits(23);
        //000 0001 0000 0100 0000 0000
        EXPECT_EQ(0x10400, v);
    }

    if (true) {
        SrsBuffer b((char*)"\xb4\x62\x40\x00\x00\x00\x88\x00", 8);
        SrsBitBuffer bb(&b);

        int32_t v = 0;
        srs_error_t err = bb.read_bits_se(v);
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_EQ(0, v);

        v = 0;
        err = bb.read_bits_se(v);
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_EQ(-1, v);

        v = 0;
        err = bb.read_bits_se(v);
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_EQ(1, v);
    }
}

#ifndef SRS_OSX
extern srs_gettimeofday_t _srs_gettimeofday;
int mock_gettimeofday(struct timeval* /*tp*/, struct timezone* /*tzp*/) {
	return -1;
}

class MockTime
{
private:
	srs_gettimeofday_t ot;
public:
	MockTime(srs_gettimeofday_t t = NULL) {
		ot = _srs_gettimeofday;
		if (t) {
			_srs_gettimeofday = t;
		}
	}
	virtual ~MockTime() {
		if (ot) {
			_srs_gettimeofday = ot;
		}
	}
};

VOID TEST(KernelUtilityTest, CoverTimeSpecial)
{
	if (true) {
		MockTime _mt(mock_gettimeofday);
		EXPECT_TRUE(-1 == srs_update_system_time());
	}
}
#endif

extern int64_t _srs_system_time_startup_time;
extern int64_t _srs_system_time_us_cache;
extern int av_toupper(int c);

VOID TEST(KernelUtilityTest, CoverTimeUtilityAll)
{
	srs_error_t err;

    _srs_system_time_us_cache = 0;
    _srs_system_time_startup_time = 0;
    EXPECT_TRUE(srs_get_system_startup_time() > 0);
    
    _srs_system_time_us_cache -= 300*1000 * 1000 + 1;
    EXPECT_TRUE(srs_update_system_time() > 0);

    if (true) {
        string host = "127.0.0.1:1935";
        int port = 0;
        srs_parse_hostport(host, host, port);
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("127.0.0.1", host.c_str());
    }

    if (true) {
        string host;
        int port = 8080;
        srs_parse_hostport("::1", host, port);
        EXPECT_EQ(8080, port);
        EXPECT_STREQ("::1", host.c_str());
    }

    if (true) {
        string host;
        int port = 8080;
        srs_parse_hostport("::", host, port);
        EXPECT_EQ(8080, port);
        EXPECT_STREQ("::", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("3ffe:dead:beef::1", host, port);
        EXPECT_EQ(0, port);
        EXPECT_STREQ("3ffe:dead:beef::1", host.c_str());
    }

    if (true) {
        string host;
        int port = 10;
        srs_parse_hostport("2001:da8:6000:291:21f:d0ff:fed4:928c", host, port);
        EXPECT_EQ(10, port);
        EXPECT_STREQ("2001:da8:6000:291:21f:d0ff:fed4:928c", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("[2001:da8:6000:291:21f:d0ff:fed4:928c]:167", host, port);
        EXPECT_EQ(167, port);
        EXPECT_STREQ("2001:da8:6000:291:21f:d0ff:fed4:928c", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("[::A.B.C.D]:167", host, port);
        EXPECT_EQ(167, port);
        EXPECT_STREQ("::A.B.C.D", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("::A.B.C.D", host, port);
        EXPECT_EQ(0, port);
        EXPECT_STREQ("::A.B.C.D", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("[::FFFF:A.B.C.D]:167", host, port);
        EXPECT_EQ(167, port);
        EXPECT_STREQ("::FFFF:A.B.C.D", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("[ff00::]:167", host, port);
        EXPECT_EQ(167, port);
        EXPECT_STREQ("ff00::", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("[fe80::a00:27ff:fe84:be2%eth0]:167", host, port);
        EXPECT_EQ(167, port);
        EXPECT_STREQ("fe80::a00:27ff:fe84:be2%eth0", host.c_str());
    }

    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("::FFFF:A.B.C.D", host, port);
        EXPECT_EQ(0, port);
        EXPECT_STREQ("::FFFF:A.B.C.D", host.c_str());
    }

    if (true) {
        string host;
        int port = 8080;
        srs_parse_hostport("", host, port);
        EXPECT_EQ(8080, port);
        EXPECT_STREQ("", host.c_str());
    }

    if (true) {
        string host;
        int port = 8080;
        srs_parse_hostport("3ffe:dead:beef::1", host, port);
        EXPECT_EQ(8080, port);
        EXPECT_STREQ("3ffe:dead:beef::1", host.c_str());
    }
    
    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("[3ffe:dead:beef::1]:1935", host, port);
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("3ffe:dead:beef::1", host.c_str());
    }
    
    if (true) {
        string host;
        int port = 0;
        srs_parse_hostport("domain.com", host, port);
        EXPECT_STREQ("domain.com", host.c_str());
    }

    if (true) {
        string host;
        int port = 1935;
        srs_parse_hostport("domain.com:0", host, port);
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("domain.com", host.c_str());
    }
    
    if (true) {
        string ep = srs_any_address_for_listener();
        EXPECT_TRUE(ep == "0.0.0.0" || ep == "::");
    }
    
    if (true) {
        string host;
        int port = 0;
        srs_parse_endpoint("[3ffe:dead:beef::1]:1935", host, port);
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("3ffe:dead:beef::1", host.c_str());
    }
    
    if (true) {
        string host;
        int port = 0;
        srs_parse_endpoint("domain.com:1935", host, port);
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("domain.com", host.c_str());
    }
    
    if (true) {
        string host;
        int port = 0;
        srs_parse_endpoint("1935", host, port);
        EXPECT_EQ(1935, port);
        EXPECT_TRUE(host == "0.0.0.0" || host == "::");
    }
    
    if (true) {
        EXPECT_STREQ("1.00", srs_float2str(1).c_str());
        EXPECT_STREQ("on", srs_bool2switch(true).c_str());
        EXPECT_STREQ("off", srs_bool2switch(false).c_str());
    }
    
    if (true) {
        EXPECT_EQ('H', av_toupper('h'));
    }
    
    if (true) {
        int family = 0;
        string ip = srs_dns_resolve("localhost", family);
        EXPECT_TRUE(ip == "127.0.0.1" || ip == "::1");
    }
    
    if (true) {
        EXPECT_TRUE(srs_path_exists("."));
        HELPER_EXPECT_SUCCESS(srs_create_dir_recursively("."));
    }
    
    if (true) {
        char buf[16] = {0};
        EXPECT_STREQ("FE", srs_data_to_hex(buf, (const uint8_t*)"\xfe", 1));
    }
}

VOID TEST(KernelTSTest, CoverContextUtility)
{
	srs_error_t err;

    if (true) {
        EXPECT_STREQ("Reserved", srs_ts_stream2string(SrsTsStreamReserved).c_str());
        EXPECT_STREQ("MP3", srs_ts_stream2string(SrsTsStreamAudioMp3).c_str());
        EXPECT_STREQ("AAC", srs_ts_stream2string(SrsTsStreamAudioAAC).c_str());
        EXPECT_STREQ("AC3", srs_ts_stream2string(SrsTsStreamAudioAC3).c_str());
        EXPECT_STREQ("AudioDTS", srs_ts_stream2string(SrsTsStreamAudioDTS).c_str());
        EXPECT_STREQ("H.264", srs_ts_stream2string(SrsTsStreamVideoH264).c_str());
        EXPECT_STREQ("MP4", srs_ts_stream2string(SrsTsStreamVideoMpeg4).c_str());
        EXPECT_STREQ("MP4A", srs_ts_stream2string(SrsTsStreamAudioMpeg4).c_str());
        EXPECT_STREQ("Other", srs_ts_stream2string(SrsTsStreamForbidden).c_str());
    }
    
    if (true) {
        SrsTsContext ctx;
        SrsTsPacket p(&ctx);
        
        SrsTsChannel c;
        SrsTsMessage m(&c, &p);
        
        EXPECT_TRUE(m.fresh());
        EXPECT_TRUE(!m.is_audio());
        EXPECT_TRUE(!m.is_video());
        EXPECT_EQ(-1, m.stream_number());
        
        m.sid = SrsTsPESStreamId(0x06<<5 | 0x01);
        EXPECT_TRUE(m.is_audio());
        EXPECT_EQ(1, m.stream_number());
        
        m.sid = SrsTsPESStreamId(0x0e<<4 | 0x02);
        EXPECT_TRUE(m.is_video());
        EXPECT_EQ(2, m.stream_number());
        
        SrsTsMessage* cp = m.detach();
        EXPECT_TRUE(cp != NULL);
        srs_freep(cp);
        
        ctx.reset();
        EXPECT_FALSE(ctx.ready);
    }
    
    if (true) {
        SrsTsContext ctx;
        SrsTsPacket p(&ctx);
        
        SrsTsChannel c;
        SrsTsMessage m(&c, &p);
        
        m.PES_packet_length = 8;
        SrsBuffer b(NULL, 0);
        
        int nb_bytes = 0;
        HELPER_EXPECT_SUCCESS(m.dump(&b, &nb_bytes));
        EXPECT_EQ(0, nb_bytes);
    }
    
    if (true) {
        SrsTsContext ctx;
        SrsTsPacket p(&ctx);
        
        SrsTsChannel c;
        SrsTsMessage m(&c, &p);
        
        m.PES_packet_length = 8;
        SrsBuffer b((char*)"\x00\x01\x02\x03", 4);
        
        int nb_bytes = 0;
        HELPER_EXPECT_SUCCESS(m.dump(&b, &nb_bytes));
        EXPECT_EQ(4, nb_bytes);
    }
    
    if (true) {
        SrsTsContext ctx;
        SrsTsPacket p(&ctx);
        
        SrsTsChannel c;
        SrsTsMessage m(&c, &p);
        
        EXPECT_TRUE(m.completed(1));
        EXPECT_TRUE(!m.completed(0));
        
        m.PES_packet_length = 8;
        SrsBuffer b((char*)"\x00\x01\x02\x03", 4);
        
        int nb_bytes = 0;
        HELPER_EXPECT_SUCCESS(m.dump(&b, &nb_bytes));
        EXPECT_EQ(4, nb_bytes);
        
        b.skip(-4);
        HELPER_EXPECT_SUCCESS(m.dump(&b, &nb_bytes));
        EXPECT_EQ(4, nb_bytes);
        
        EXPECT_TRUE(m.completed(0));
    }
    
    if (true) {
        SrsTsMessage m;
        
        EXPECT_TRUE(m.completed(1));
        EXPECT_TRUE(!m.completed(0));
        
        m.PES_packet_length = 8;
        SrsBuffer b((char*)"\x00\x01\x02\x03", 4);
        
        int nb_bytes = 0;
        HELPER_EXPECT_SUCCESS(m.dump(&b, &nb_bytes));
        EXPECT_EQ(4, nb_bytes);
        
        b.skip(-4);
        HELPER_EXPECT_SUCCESS(m.dump(&b, &nb_bytes));
        EXPECT_EQ(4, nb_bytes);
        
        EXPECT_TRUE(m.completed(0));
    }
    
    if (true) {
        MockTsHandler* h = new MockTsHandler();
        srs_freep(h);
        
        SrsTsContext ctx;
        EXPECT_TRUE(!ctx.is_pure_audio());
        
        ctx.set(100, SrsTsPidApplyPAT);
        ctx.set(101, SrsTsPidApplyPMT);
        ctx.set(102, SrsTsPidApplyVideo);
        ctx.set(102, SrsTsPidApplyVideo);
        
        ctx.on_pmt_parsed();
        EXPECT_TRUE(!ctx.is_pure_audio());
        
        EXPECT_EQ(100, ctx.get(100)->pid);
    }
    
    if (true) {
        MockTsHandler* h = new MockTsHandler();
        srs_freep(h);
        
        SrsTsContext ctx;
        EXPECT_TRUE(!ctx.is_pure_audio());
        
        ctx.set(100, SrsTsPidApplyPAT);
        ctx.set(101, SrsTsPidApplyPMT);
        ctx.set(102, SrsTsPidApplyAudio);
        
        ctx.on_pmt_parsed();
        EXPECT_TRUE(ctx.is_pure_audio());
        
        EXPECT_EQ(100, ctx.get(100)->pid);
        EXPECT_TRUE(NULL == ctx.get(200));
    }
}

VOID TEST(KernelTSTest, CoverContextEncode)
{
	srs_error_t err;

    SrsTsContext ctx;
    MockTsHandler h;
    
    if (true) {
        SrsBuffer b(NULL, 0);
        HELPER_EXPECT_SUCCESS(ctx.decode(&b, &h));
        EXPECT_TRUE(NULL == h.msg);
    }
    
    if (true) {
        SrsBuffer b((char*)"\x00", 1);
        
        srs_error_t err = ctx.decode(&b, &h);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        SrsBuffer b((char*)"\x00\x00\x00\x00", 4);
        
        srs_error_t err = ctx.decode(&b, &h);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        MockSrsFileWriter f;
        SrsTsMessage m;
        
        srs_error_t err = ctx.encode(&f, &m, SrsVideoCodecIdDisabled, SrsAudioCodecIdDisabled);
        HELPER_EXPECT_FAILED(err);

        err = ctx.encode(&f, &m, SrsVideoCodecIdAV1, SrsAudioCodecIdOpus);
        HELPER_EXPECT_FAILED(err);

        err = ctx.encode_pat_pmt(&f, 0, SrsTsStreamReserved, 0, SrsTsStreamReserved);
        HELPER_EXPECT_FAILED(err);
    }
    
    if (true) {
        MockSrsFileWriter f;
        SrsTsMessage m;
        
        srs_error_t err = ctx.encode_pes(&f, &m, 0x200, SrsTsStreamVideoH264, false);
        HELPER_EXPECT_FAILED(err);
        
        HELPER_EXPECT_SUCCESS(ctx.encode_pat_pmt(&f, 200, SrsTsStreamVideoH264, 201, SrsTsStreamAudioAAC));
    }
    
    if (true) {
        MockSrsFileWriter f;
        SrsTsMessage m;
        
        HELPER_EXPECT_SUCCESS(ctx.encode(&f, &m, SrsVideoCodecIdAVC, SrsAudioCodecIdAAC));
        
        m.payload->append("Hello, world!", 13);
        HELPER_EXPECT_SUCCESS(ctx.encode(&f, &m, SrsVideoCodecIdAVC, SrsAudioCodecIdAAC));
    }
}

VOID TEST(KernelTSTest, CoverContextEncodeHEVC)
{
    srs_error_t err;

    SrsTsContext ctx;
    MockTsHandler h;

#ifndef SRS_H265
    if (true) {
        MockSrsFileWriter f;
        SrsTsMessage m;

        err = ctx.encode(&f, &m, SrsVideoCodecIdHEVC, SrsAudioCodecIdOpus);
        HELPER_EXPECT_FAILED(err);
    }
#endif

#ifdef SRS_H265
    if (true) {
        MockSrsFileWriter f;
        SrsTsMessage m;

        err = ctx.encode(&f, &m, SrsVideoCodecIdHEVC, SrsAudioCodecIdOpus);
        HELPER_EXPECT_SUCCESS(err);
    }
#endif
}

VOID TEST(KernelTSTest, CoverContextDecode)
{
	srs_error_t err;

    SrsTsContext ctx;
    MockTsHandler h;

    if (true) {
        uint8_t raw[] = {
			0x47, 0x40, 0x11, 0x10, 0x00, 0x42, 0xf0, 0x25, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xff, 0x01, 0xff,
			0x00, 0x01, 0xfc, 0x80, 0x14, 0x48, 0x12, 0x01, 0x06, 0x46, 0x46, 0x6d, 0x70, 0x65, 0x67, 0x09,
			0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x30, 0x31, 0x77, 0x7c, 0x43, 0xca, 0xff, 0xff, 0xff
        };
        SrsBuffer b((char*)raw, sizeof(raw));
        HELPER_EXPECT_SUCCESS(ctx.decode(&b, &h));
    }

    if (true) {
        uint8_t raw[] = {
			0x47, 0x40, 0x00, 0x10, 0x00, 0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xf0,
            0x00, 0x2a, 0xb1, 0x04, 0xb2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        };
        SrsBuffer b((char*)raw, sizeof(raw));
        HELPER_EXPECT_SUCCESS(ctx.decode(&b, &h));
    }

    if (true) {
        uint8_t raw[] = {
			0x47, 0x50, 0x00, 0x10, 0x00, 0x02, 0xb0, 0x17, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xe1, 0x00, 0xf0,
            0x00, 0x1b, 0xe1, 0x00, 0xf0, 0x00, 0x0f, 0xe1, 0x01, 0xf0, 0x00, 0x2f, 0x44, 0xb9, 0x9b, 0xff
        };
        SrsBuffer b((char*)raw, sizeof(raw));
        HELPER_EXPECT_SUCCESS(ctx.decode(&b, &h));
    }

    if (true) {
        uint8_t raw[] = {
			0x47, 0x41, 0x00, 0x30, 0x07, 0x50, 0x00, 0x00, 0x7b, 0x0c, 0x7e, 0x00, 0x00, 0x00, 0x01, 0xe0,
            0x00, 0x00, 0x80, 0xc0, 0x0a, 0x31, 0x00, 0x09, 0x10, 0xa1, 0x11, 0x00, 0x07, 0xd8, 0x61, 0x00,
            0x00, 0x00, 0x01, 0x09, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0xff, 0xff, 0xac, 0xdc, 0x45,
            0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20, 0xd9, 0x23, 0xee, 0xef, 0x78, 0x32,
            0x36, 0x34, 0x20, 0x2d, 0x20, 0x63, 0x6f, 0x72, 0x65, 0x20, 0x31, 0x33, 0x38, 0x20, 0x2d, 0x20,
            0x48, 0x2e, 0x32, 0x36, 0x34, 0x2f, 0x4d, 0x50, 0x45, 0x47, 0x2d, 0x34, 0x20, 0x41, 0x56, 0x43,
            0x20, 0x63, 0x6f, 0x64, 0x65, 0x63, 0x20, 0x2d, 0x20, 0x43, 0x6f, 0x70, 0x79, 0x6c, 0x65, 0x66,
            0x74, 0x20, 0x32, 0x30, 0x30, 0x33, 0x2d, 0x32, 0x30, 0x31, 0x33, 0x20, 0x2d, 0x20, 0x68, 0x74,
            0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x76, 0x69, 0x64, 0x65, 0x6f, 0x6c, 0x61,
            0x6e, 0x2e, 0x6f, 0x72, 0x67, 0x2f, 0x78, 0x32, 0x36, 0x34, 0x2e, 0x68, 0x74, 0x6d, 0x6c, 0x20,
            0x2d, 0x20, 0x6f, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x3a, 0x20, 0x63, 0x61, 0x62, 0x61, 0x63,
            0x3d, 0x31, 0x20, 0x72, 0x65, 0x66, 0x3d, 0x33, 0x20, 0x64, 0x65, 0x62
        };
        SrsBuffer b((char*)raw, sizeof(raw));
        HELPER_EXPECT_SUCCESS(ctx.decode(&b, &h));
    }
}

VOID TEST(KernelTSTest, CoverTransmuxer)
{
	srs_error_t err;

    SrsTsTransmuxer m;
    MockSrsFileWriter f;
    HELPER_EXPECT_SUCCESS(m.initialize(&f));
    
    if (true) {
        uint8_t raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
        };
        HELPER_EXPECT_SUCCESS(m.write_video(0, (char*)raw, sizeof(raw)));
    }
    
    if (true) {
        uint8_t raw[] = {
            0xaf, 0x00, 0x12, 0x10
        };
        HELPER_EXPECT_SUCCESS(m.write_audio(0, (char*)raw, sizeof(raw)));
    }
    
    if (true) {
        uint8_t raw[] = {
            0xaf,
            0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
        };
        HELPER_EXPECT_SUCCESS(m.write_audio(34, (char*)raw, sizeof(raw)));
    }

    if (true) {
        uint8_t raw[] = {
            0x27,
            0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00,
            0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59,
            0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13,
            0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74,
            0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02,
            0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8,
            0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b,
            0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82,
            0xb2, 0x72, 0x5a
        };
        HELPER_EXPECT_SUCCESS(m.write_video(40, (char*)raw, sizeof(raw)));
    }
}

VOID TEST(KernelMP4Test, CoverMP4All)
{
	if (true) {
		SrsMp4ElstEntry e;
		EXPECT_EQ(0, e.media_time);
		EXPECT_EQ(0, (int)e.segment_duration);
	}

	if (true) {
		SrsMp4CttsEntry e;
		EXPECT_EQ(0, (int)e.sample_count);
		EXPECT_EQ(0, (int)e.sample_offset);
	}
}

VOID TEST(KernelMP4Test, CoverMP4CodecSingleFrame)
{
	srs_error_t err;

    MockSrsFileWriter f;

    // MP4 encoder.
    if (true) {
        SrsMp4Encoder enc; SrsFormat fmt;
        HELPER_EXPECT_SUCCESS(enc.initialize(&f));
        HELPER_EXPECT_SUCCESS(fmt.initialize());

        if (true) {
            uint8_t raw[] = {
                0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
            EXPECT_EQ(768, (int)enc.width); EXPECT_EQ(320, (int)enc.height);
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x00, 0x12, 0x10
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0x17, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        enc.acodec = SrsAudioCodecIdAAC;
        enc.vcodec = SrsVideoCodecIdAVC;

        HELPER_EXPECT_SUCCESS(enc.flush());
        //mock_print_mp4(string(f.data(), f.filesize()));
    }

    // MP4 decoder.
    if (true) {
        MockSrsFileReader fr((const char*)f.data(), f.filesize());
        SrsMp4Decoder dec; HELPER_EXPECT_SUCCESS(dec.initialize(&fr));
        
        SrsMp4HandlerType ht; uint16_t ft, ct; uint32_t dts, pts, nb_sample; uint8_t* sample = NULL;

        // Sequence header.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(41, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(2, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        // Frame group #0
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(87, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);
    }

    // MP4 box reader.
    if (true) {
        SrsMp4BoxReader br; MockSrsFileReader fr((const char*)f.data(), f.filesize());
        HELPER_EXPECT_SUCCESS(br.initialize(&fr));
        
        SrsSimpleStream stream;
        
        for (;;) {
            SrsMp4Box* box = NULL;
            srs_error_t err = br.read(&stream, &box);
            if (err != srs_success) {
                srs_freep(err);
                break;
            }
            
            stringstream ss;
            SrsMp4DumpContext dc;
            dc.level = 0;
            dc.summary = false;
            box->dumps(ss, dc);
            
            HELPER_EXPECT_SUCCESS(br.skip(box, &stream));
            
            srs_freep(box);
        }
    }
}

VOID TEST(KernelMP4Test, CoverMP4MultipleVideos)
{
	srs_error_t err;

    MockSrsFileWriter f;

    // Encode frames.
    // V-A V-V
    if (true) {
        SrsMp4Encoder enc; SrsFormat fmt;
        HELPER_EXPECT_SUCCESS(enc.initialize(&f));
        HELPER_EXPECT_SUCCESS(fmt.initialize());

        // Sequence header, V-A
        if (true) {
            uint8_t raw[] = {
                0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
            EXPECT_EQ(768, (int)enc.width); EXPECT_EQ(320, (int)enc.height);
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x00, 0x12, 0x10
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        // Frame group #0, V-V
        if (true) {
            uint8_t raw[] = {
                0x17, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0x27, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 40, 40, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        enc.acodec = SrsAudioCodecIdAAC;
        enc.vcodec = SrsVideoCodecIdAVC;

        // Flush encoder.
        HELPER_EXPECT_SUCCESS(enc.flush());
        //mock_print_mp4(string(f.data(), f.filesize()));
    }

    // Decode frames.
    if (true) {
        MockSrsFileReader fr((const char*)f.data(), f.filesize());
        SrsMp4Decoder dec; HELPER_EXPECT_SUCCESS(dec.initialize(&fr));

        SrsMp4HandlerType ht; uint16_t ft, ct; uint32_t dts, pts, nb_sample; uint8_t* sample = NULL;

        // Sequence header.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(41, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(2, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        // Frames order by dts asc.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(40, (int)dts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);
    }
}

VOID TEST(KernelMP4Test, CoverMP4MultipleCTTs)
{
	srs_error_t err;

    MockSrsFileWriter f;

    // Encode frames.
    // V-A V-V
    if (true) {
        SrsMp4Encoder enc; SrsFormat fmt;
        HELPER_EXPECT_SUCCESS(enc.initialize(&f));
        HELPER_EXPECT_SUCCESS(fmt.initialize());

        // Sequence header, V-A
        if (true) {
            uint8_t raw[] = {
                0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
            EXPECT_EQ(768, (int)enc.width); EXPECT_EQ(320, (int)enc.height);
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x00, 0x12, 0x10
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        // Frame group #0, V-V-V
        if (true) {
            uint8_t raw[] = {
                0x17, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0x27, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 40, 80, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0x27, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 80, 40, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        enc.acodec = SrsAudioCodecIdAAC;
        enc.vcodec = SrsVideoCodecIdAVC;

        // Flush encoder.
        HELPER_EXPECT_SUCCESS(enc.flush());
        //mock_print_mp4(string(f.data(), f.filesize()));
    }

    // Decode frames.
    if (true) {
        MockSrsFileReader fr((const char*)f.data(), f.filesize());
        SrsMp4Decoder dec; HELPER_EXPECT_SUCCESS(dec.initialize(&fr));

        SrsMp4HandlerType ht; uint16_t ft, ct; uint32_t dts, pts, nb_sample; uint8_t* sample = NULL;

        // Sequence header.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(41, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(2, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        // Frames order by dts asc.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(40, (int)dts); EXPECT_EQ(80, (int)pts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(80, (int)dts); EXPECT_EQ(40, (int)pts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);
    }
}

VOID TEST(KernelMP4Test, CoverMP4MultipleAVs)
{
	srs_error_t err;

    MockSrsFileWriter f;

    // Encode frames.
    // V-A V-V
    if (true) {
        SrsMp4Encoder enc; SrsFormat fmt;
        HELPER_EXPECT_SUCCESS(enc.initialize(&f));
        HELPER_EXPECT_SUCCESS(fmt.initialize());

        // Sequence header, V-A
        if (true) {
            uint8_t raw[] = {
                0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
            EXPECT_EQ(768, (int)enc.width); EXPECT_EQ(320, (int)enc.height);
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x00, 0x12, 0x10
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        // Frame group #0, V-A-A-V
        if (true) {
            uint8_t raw[] = {
                0x17, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 20, 20, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0x27, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 40, 40, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        enc.acodec = SrsAudioCodecIdAAC;
        enc.vcodec = SrsVideoCodecIdAVC;

        // Flush encoder.
        HELPER_EXPECT_SUCCESS(enc.flush());
        //mock_print_mp4(string(f.data(), f.filesize()));
    }

    // Decode frames.
    if (true) {
        MockSrsFileReader fr((const char*)f.data(), f.filesize());
        SrsMp4Decoder dec; HELPER_EXPECT_SUCCESS(dec.initialize(&fr));

        SrsMp4HandlerType ht; uint16_t ft, ct; uint32_t dts, pts, nb_sample; uint8_t* sample = NULL;

        // Sequence header.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(41, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(2, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        // Frames order by dts asc.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(87, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(20, (int)dts); EXPECT_EQ(87, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(40, (int)dts); EXPECT_EQ(40, (int)pts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioAacFrameTraitSequenceHeader, ct);
        srs_freepa(sample);
    }
}

VOID TEST(KernelMP4Test, CoverMP4MultipleAVsWithMp3)
{
    srs_error_t err;

    MockSrsFileWriter f;

    // Encode frames.
    // V-A V-V
    if (true) {
        SrsMp4Encoder enc; SrsFormat fmt;
        HELPER_EXPECT_SUCCESS(enc.initialize(&f));
        HELPER_EXPECT_SUCCESS(fmt.initialize());

        // Sequence header, V-A
        if (true) {
            uint8_t raw[] = {
                0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
            EXPECT_EQ(768, (int)enc.width); EXPECT_EQ(320, (int)enc.height);
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x00, 0x12, 0x10
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        // Frame group #0, V-A-A-V
        if (true) {
            uint8_t raw[] = {
                0x17, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 20, 20, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        if (true) {
            uint8_t raw[] = {
                0x27, 0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00, 0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59, 0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13, 0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74, 0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02, 0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8, 0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b, 0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82, 0xb2, 0x72, 0x5a
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 40, 40, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        enc.acodec = SrsAudioCodecIdMP3;
        enc.vcodec = SrsVideoCodecIdAVC;

        // Flush encoder.
        HELPER_EXPECT_SUCCESS(enc.flush());
        //mock_print_mp4(string(f.data(), f.filesize()));
    }

    // Decode frames.
    if (true) {
        MockSrsFileReader fr((const char*)f.data(), f.filesize());
        SrsMp4Decoder dec; HELPER_EXPECT_SUCCESS(dec.initialize(&fr));

        SrsMp4HandlerType ht; uint16_t ft, ct; uint32_t dts, pts, nb_sample; uint8_t* sample = NULL;

        // Sequence header.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(41, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        // Frames order by dts asc.
        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioMp3FrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(0, (int)dts); EXPECT_EQ(87, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_NE(SrsVideoAvcFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(20, (int)dts); EXPECT_EQ(87, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeSOUN, ht); EXPECT_NE(SrsVideoAvcFrameTraitSequenceHeader, ct);
        srs_freepa(sample);

        HELPER_EXPECT_SUCCESS(dec.read_sample(&ht, &ft, &ct, &dts, &pts, &sample, &nb_sample));
        EXPECT_EQ(40, (int)dts); EXPECT_EQ(40, (int)pts); EXPECT_EQ(127, (int)nb_sample); EXPECT_EQ(SrsMp4HandlerTypeVIDE, ht); EXPECT_NE(SrsAudioMp3FrameTraitSequenceHeader, ct);
        srs_freepa(sample);
    }
}

VOID TEST(KernelMP4Test, CoverMP4CodecErrorNoFrames)
{
	srs_error_t err;

    MockSrsFileWriter f;

    // MP4 encoder.
    if (true) {
        SrsMp4Encoder enc; SrsFormat fmt;
        HELPER_EXPECT_SUCCESS(enc.initialize(&f));
        HELPER_EXPECT_SUCCESS(fmt.initialize());

        if (true) {
            uint8_t raw[] = {
                0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
            };
            HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeVIDE, fmt.video->frame_type, fmt.video->avc_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
            EXPECT_EQ(768, (int)enc.width); EXPECT_EQ(320, (int)enc.height);
        }

        if (true) {
            uint8_t raw[] = {
                0xaf, 0x00, 0x12, 0x10
            };
            HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));
            HELPER_EXPECT_SUCCESS(enc.write_sample(
                &fmt, SrsMp4HandlerTypeSOUN, 0x00, fmt.audio->aac_packet_type, 0, 0, (uint8_t*)fmt.raw, fmt.nb_raw
            ));
        }

        HELPER_ASSERT_FAILED(enc.flush());
        //mock_print_mp4(string(f.data(), f.filesize()));
    }
}

uint8_t* mock_copy_bytes(char* data, int size)
{
    uint8_t* cp = new uint8_t[size];
    memcpy(cp, data, size);
    return cp;
}

VOID TEST(KernelMP4Test, CoverMP4M2tsSegmentEncoder)
{
	srs_error_t err;

    SrsMp4M2tsSegmentEncoder enc;
    MockSrsFileWriter f;
    HELPER_EXPECT_SUCCESS(enc.initialize(&f, 0, 0, 100));
    
    SrsFormat fmt;
    HELPER_EXPECT_SUCCESS(fmt.initialize());
    
    if (true) {
        uint8_t raw[] = {
            0x17,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
            0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
            0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c
        };
        HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));

        uint8_t* cp = mock_copy_bytes(fmt.raw, fmt.nb_raw);
        SrsUniquePtr<uint8_t[]> cp_uptr(cp);
        HELPER_EXPECT_SUCCESS(enc.write_sample(
            SrsMp4HandlerTypeVIDE, fmt.video->frame_type, 0, 0, cp, fmt.nb_raw
        ));
    }
    
    if (true) {
        uint8_t raw[] = {
            0xaf, 0x00, 0x12, 0x10
        };
        HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));

        uint8_t* cp = mock_copy_bytes(fmt.raw, fmt.nb_raw);
        SrsUniquePtr<uint8_t[]> cp_uptr(cp);
        HELPER_EXPECT_SUCCESS(enc.write_sample(
            SrsMp4HandlerTypeSOUN, 0x00, 0, 0, cp, fmt.nb_raw
        ));
    }
    
    if (true) {
        uint8_t raw[] = {
            0xaf,
            0x01, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46, 0xf3, 0xf1, 0x0a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5e
        };
        HELPER_EXPECT_SUCCESS(fmt.on_audio(0, (char*)raw, sizeof(raw)));

        uint8_t* cp = mock_copy_bytes(fmt.raw, fmt.nb_raw);
        SrsUniquePtr<uint8_t[]> cp_uptr(cp);
        HELPER_EXPECT_SUCCESS(enc.write_sample(
            SrsMp4HandlerTypeSOUN, 0x00, 34, 34, cp, fmt.nb_raw
        ));
    }
    
    if (true) {
        uint8_t raw[] = {
            0x27,
            0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x7b, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x1f, 0x00, 0x00,
            0xf1, 0x68, 0x1a, 0x35, 0x84, 0xb3, 0xee, 0xe0, 0x61, 0xba, 0x4e, 0xa8, 0x52, 0x48, 0x50, 0x59,
            0x75, 0x42, 0xd9, 0x96, 0x4a, 0x51, 0x38, 0x2c, 0x63, 0x5e, 0x41, 0xc9, 0x70, 0x60, 0x9d, 0x13,
            0x53, 0xc2, 0xa8, 0xf5, 0x45, 0x86, 0xc5, 0x3e, 0x28, 0x1a, 0x69, 0x5f, 0x71, 0x1e, 0x51, 0x74,
            0x0e, 0x31, 0x47, 0x3c, 0xd3, 0xd2, 0x10, 0x25, 0x45, 0xc5, 0xb7, 0x31, 0xec, 0x7f, 0xd8, 0x02,
            0xae, 0xa4, 0x77, 0x6d, 0xcb, 0xc6, 0x1e, 0x2f, 0xa2, 0xd1, 0x12, 0x08, 0x34, 0x52, 0xea, 0xe8,
            0x0b, 0x4f, 0x81, 0x21, 0x4f, 0x71, 0x3f, 0xf2, 0xad, 0x02, 0x58, 0xdf, 0x9e, 0x31, 0x86, 0x9b,
            0x1b, 0x41, 0xbf, 0x2a, 0x09, 0x00, 0x43, 0x5c, 0xa1, 0x7e, 0x76, 0x59, 0xef, 0xa6, 0xfc, 0x82,
            0xb2, 0x72, 0x5a
        };
        HELPER_EXPECT_SUCCESS(fmt.on_video(0, (char*)raw, sizeof(raw)));

        uint8_t* cp = mock_copy_bytes(fmt.raw, fmt.nb_raw);
        SrsUniquePtr<uint8_t[]> cp_uptr(cp);
        HELPER_EXPECT_SUCCESS(enc.write_sample(
            SrsMp4HandlerTypeVIDE, fmt.video->frame_type, 40, 40, cp, fmt.nb_raw
        ));
    }
    
    uint64_t dts = 0;
    HELPER_EXPECT_SUCCESS(enc.flush(dts));
}

VOID TEST(KernelUtilityTest, CoverStringAssign)
{
    string sps = "SRS";
    ASSERT_STREQ("SRS", sps.c_str());

    sps.assign("Hello", 5);
    ASSERT_STREQ("Hello", sps.c_str());

    sps.assign("World", 0);
    ASSERT_EQ(0, (int)sps.length());
    ASSERT_STREQ("", sps.c_str());
}

VOID TEST(KernelUtilityTest, CoverCheckIPAddrValid)
{
    ASSERT_TRUE(srs_check_ip_addr_valid("172.16.254.1"));
    ASSERT_TRUE(srs_check_ip_addr_valid("2001:0db8:85a3:0:0:8A2E:0370:7334"));
    ASSERT_FALSE(srs_check_ip_addr_valid(""));

    //IPv4 any addr
    ASSERT_TRUE(srs_check_ip_addr_valid("0.0.0.0"));
    //IPV6 any addr
    ASSERT_TRUE(srs_check_ip_addr_valid("::"));

    ASSERT_FALSE(srs_check_ip_addr_valid("256.256.256.256"));
#ifdef SRS_CYGWIN64
    // TODO: Might be a bug for cygwin64.
    ASSERT_TRUE(srs_check_ip_addr_valid("2001:0db8:85a3:0:0:8A2E:0370:7334:"));
#else
     ASSERT_FALSE(srs_check_ip_addr_valid("2001:0db8:85a3:0:0:8A2E:0370:7334:"));
#endif
    ASSERT_FALSE(srs_check_ip_addr_valid("1e1.4.5.6"));
}

VOID TEST(KernelUtilityTest, Base64Decode)
{
    srs_error_t err = srs_success;

    if (true) {
        string plaintext;
        HELPER_EXPECT_SUCCESS(srs_av_base64_decode("YWRtaW46YWRtaW4=", plaintext));
        EXPECT_STREQ("admin:admin", plaintext.c_str());
    }

    if (true) {
        string plaintext;
        HELPER_EXPECT_SUCCESS(srs_av_base64_decode("YWRtaW46MTIzNDU2", plaintext));
        EXPECT_STREQ("admin:123456", plaintext.c_str());
    }

    if (true) {
        string plaintext;
        HELPER_EXPECT_SUCCESS(srs_av_base64_decode("YWRtaW46MTIzNDU2", plaintext));
        EXPECT_STRNE("admin:admin", plaintext.c_str());
    }

    if (true) {
        string plaintext;
        HELPER_EXPECT_FAILED(srs_av_base64_decode("YWRtaW46YWRtaW", plaintext));
        EXPECT_STRNE("admin:admin", plaintext.c_str());
    }
}
