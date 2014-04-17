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

#include <srs_kernel_stream.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>

SrsStream::SrsStream()
{
    p = bytes = NULL;
    size = 0;
    
    // TODO: support both little and big endian.
    srs_assert(srs_is_little_endian());
}

SrsStream::~SrsStream()
{
}

int SrsStream::initialize(char* _bytes, int _size)
{
    int ret = ERROR_SUCCESS;
    
    if (!_bytes) {
        ret = ERROR_SYSTEM_STREAM_INIT;
        srs_error("stream param bytes must not be NULL. ret=%d", ret);
        return ret;
    }
    
    if (_size <= 0) {
        ret = ERROR_SYSTEM_STREAM_INIT;
        srs_error("stream param size must be positive. ret=%d", ret);
        return ret;
    }

    size = _size;
    p = bytes = _bytes;

    return ret;
}

void SrsStream::reset()
{
    p = bytes;
}

bool SrsStream::empty()
{
    return !p || !bytes || (p >= bytes + size);
}

bool SrsStream::require(int required_size)
{
    return !empty() && (required_size <= bytes + size - p);
}

void SrsStream::skip(int size)
{
    p += size;
}

int SrsStream::pos()
{
    return p - bytes;
}

int SrsStream::left()
{
    return size - pos();
}

char* SrsStream::current()
{
    return p;
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
    pp = (char*)&value;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsStream::read_3bytes()
{
    srs_assert(require(3));
    
    int32_t value = 0x00;
    pp = (char*)&value;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    return value;
}

int32_t SrsStream::read_4bytes()
{
    srs_assert(require(4));
    
    int32_t value;
    pp = (char*)&value;
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
    pp = (char*)&value;
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

std::string SrsStream::read_string(int len)
{
    srs_assert(require(len));
    
    std::string value;
    value.append(p, len);
    
    p += len;
    
    return value;
}

void SrsStream::write_1bytes(int8_t value)
{
    srs_assert(require(1));
    
    *p++ = value;
}

void SrsStream::write_2bytes(int16_t value)
{
    srs_assert(require(2));
    
    pp = (char*)&value;
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_4bytes(int32_t value)
{
    srs_assert(require(4));
    
    pp = (char*)&value;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_3bytes(int32_t value)
{
    srs_assert(require(3));
    
    pp = (char*)&value;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_8bytes(int64_t value)
{
    srs_assert(require(8));
    
    pp = (char*)&value;
    *p++ = pp[7];
    *p++ = pp[6];
    *p++ = pp[5];
    *p++ = pp[4];
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsStream::write_string(std::string value)
{
    srs_assert(require(value.length()));
    
    memcpy(p, value.data(), value.length());
    p += value.length();
}

