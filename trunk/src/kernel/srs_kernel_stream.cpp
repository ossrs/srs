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

#include <srs_kernel_stream.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

SrsStream::SrsStream()
{
    p = bytes = NULL;
    nb_bytes = 0;
    
    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());
}

SrsStream::~SrsStream()
{
}

int SrsStream::initialize(char* b, int nb)
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

char* SrsStream::data()
{
    return bytes;
}

int SrsStream::size()
{
    return nb_bytes;
}

int SrsStream::pos()
{
    return (int)(p - bytes);
}

bool SrsStream::empty()
{
    return !bytes || (p >= bytes + nb_bytes);
}

bool SrsStream::require(int required_size)
{
    srs_assert(required_size >= 0);
    
    return required_size <= nb_bytes - (p - bytes);
}

void SrsStream::skip(int size)
{
    srs_assert(p);
    
    p += size;
}

int8_t SrsStream::read_1bytes()
{
    srs_assert(require(1));
    
    return (int8_t)*p++;
}

int16_t SrsStream::read_2bytes()
{
    srs_assert(require(2));
    
    int16_t value;
    char* pp = (char*)&value;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsStream::read_3bytes()
{
    srs_assert(require(3));
    
    int32_t value = 0x00;
    char* pp = (char*)&value;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsStream::read_4bytes()
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

int64_t SrsStream::read_8bytes()
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

string SrsStream::read_string(int len)
{
    srs_assert(require(len));
    
    std::string value;
    value.append(p, len);
    
    p += len;
    
    return value;
}

void SrsStream::read_bytes(char* data, int size)
{
    srs_assert(require(size));
    
    memcpy(data, p, size);
    
    p += size;
}

void SrsStream::write_1bytes(int8_t value)
{
    srs_assert(require(1));
    
    *p++ = value;
}

void SrsStream::write_2bytes(int16_t value)
{
    srs_assert(require(2));
    
    char* pp = (char*)&value;
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_4bytes(int32_t value)
{
    srs_assert(require(4));
    
    char* pp = (char*)&value;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_3bytes(int32_t value)
{
    srs_assert(require(3));
    
    char* pp = (char*)&value;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_8bytes(int64_t value)
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

void SrsStream::write_string(string value)
{
    srs_assert(require((int)value.length()));
    
    memcpy(p, value.data(), value.length());
    p += value.length();
}

void SrsStream::write_bytes(char* data, int size)
{
    srs_assert(require(size));
    
    memcpy(p, data, size);
    p += size;
}

SrsBitStream::SrsBitStream()
{
    cb = 0;
    cb_left = 0;
    stream = NULL;
}

SrsBitStream::~SrsBitStream()
{
}

int SrsBitStream::initialize(SrsStream* s) {
    stream = s;
    return ERROR_SUCCESS;
}

bool SrsBitStream::empty() {
    if (cb_left) {
        return false;
    }
    return stream->empty();
}

int8_t SrsBitStream::read_bit() {
    if (!cb_left) {
        srs_assert(!stream->empty());
        cb = stream->read_1bytes();
        cb_left = 8;
    }
    
    int8_t v = (cb >> (cb_left - 1)) & 0x01;
    cb_left--;
    return v;
}

