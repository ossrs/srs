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

#include <srs_kernel_buffer.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

ISrsCodec::ISrsCodec()
{
}

ISrsCodec::~ISrsCodec()
{
}

SrsBuffer::SrsBuffer()
{
    set_value(NULL, 0);
}

SrsBuffer::SrsBuffer(char* b, int nb_b)
{
    set_value(b, nb_b);
}

SrsBuffer::~SrsBuffer()
{
}

void SrsBuffer::set_value(char* b, int nb_b)
{
    p = bytes = b;
    nb_bytes = nb_b;
    
    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());
}

int SrsBuffer::initialize(char* b, int nb)
{
    int ret = ERROR_SUCCESS;
    
    if (!b) {
        ret = ERROR_KERNEL_STREAM_INIT;
        srs_error("stream param bytes must not be NULL. ret=%d", ret);
        return ret;
    }
    
    if (nb <= 0) {
        ret = ERROR_KERNEL_STREAM_INIT;
        srs_error("stream param size must be positive. ret=%d", ret);
        return ret;
    }

    nb_bytes = nb;
    p = bytes = b;
    srs_info("init stream ok, size=%d", size());

    return ret;
}

char* SrsBuffer::data()
{
    return bytes;
}

int SrsBuffer::size()
{
    return nb_bytes;
}

int SrsBuffer::pos()
{
    return (int)(p - bytes);
}

bool SrsBuffer::empty()
{
    return !bytes || (p >= bytes + nb_bytes);
}

bool SrsBuffer::require(int required_size)
{
    srs_assert(required_size >= 0);
    
    return required_size <= nb_bytes - (p - bytes);
}

void SrsBuffer::skip(int size)
{
    srs_assert(p);
    
    p += size;
}

int8_t SrsBuffer::read_1bytes()
{
    srs_assert(require(1));
    
    return (int8_t)*p++;
}

int16_t SrsBuffer::read_2bytes()
{
    srs_assert(require(2));
    
    int16_t value;
    char* pp = (char*)&value;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsBuffer::read_3bytes()
{
    srs_assert(require(3));
    
    int32_t value = 0x00;
    char* pp = (char*)&value;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsBuffer::read_4bytes()
{
    srs_assert(require(4));
    
    int32_t value;
    char* pp = (char*)&value;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int64_t SrsBuffer::read_8bytes()
{
    srs_assert(require(8));
    
    int64_t value;
    char* pp = (char*)&value;
    pp[7] = *p++;
    pp[6] = *p++;
    pp[5] = *p++;
    pp[4] = *p++;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

string SrsBuffer::read_string(int len)
{
    srs_assert(require(len));
    
    std::string value;
    value.append(p, len);
    
    p += len;
    
    return value;
}

void SrsBuffer::read_bytes(char* data, int size)
{
    srs_assert(require(size));
    
    memcpy(data, p, size);
    
    p += size;
}

void SrsBuffer::write_1bytes(int8_t value)
{
    srs_assert(require(1));
    
    *p++ = value;
}

void SrsBuffer::write_2bytes(int16_t value)
{
    srs_assert(require(2));
    
    char* pp = (char*)&value;
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_4bytes(int32_t value)
{
    srs_assert(require(4));
    
    char* pp = (char*)&value;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_3bytes(int32_t value)
{
    srs_assert(require(3));
    
    char* pp = (char*)&value;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_8bytes(int64_t value)
{
    srs_assert(require(8));
    
    char* pp = (char*)&value;
    *p++ = pp[7];
    *p++ = pp[6];
    *p++ = pp[5];
    *p++ = pp[4];
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_string(string value)
{
    srs_assert(require((int)value.length()));
    
    memcpy(p, value.data(), value.length());
    p += value.length();
}

void SrsBuffer::write_bytes(char* data, int size)
{
    srs_assert(require(size));
    
    memcpy(p, data, size);
    p += size;
}

SrsBitBuffer::SrsBitBuffer()
{
    cb = 0;
    cb_left = 0;
    stream = NULL;
}

SrsBitBuffer::~SrsBitBuffer()
{
}

int SrsBitBuffer::initialize(SrsBuffer* s) {
    stream = s;
    return ERROR_SUCCESS;
}

bool SrsBitBuffer::empty() {
    if (cb_left) {
        return false;
    }
    return stream->empty();
}

int8_t SrsBitBuffer::read_bit() {
    if (!cb_left) {
        srs_assert(!stream->empty());
        cb = stream->read_1bytes();
        cb_left = 8;
    }
    
    int8_t v = (cb >> (cb_left - 1)) & 0x01;
    cb_left--;
    return v;
}

