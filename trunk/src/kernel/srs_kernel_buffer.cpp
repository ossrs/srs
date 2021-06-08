//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_buffer.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

ISrsEncoder::ISrsEncoder()
{
}

ISrsEncoder::~ISrsEncoder()
{
}

ISrsCodec::ISrsCodec()
{
}

ISrsCodec::~ISrsCodec()
{
}

SrsBuffer::SrsBuffer(char* b, int nn)
{
    p = bytes = b;
    nb_bytes = nn;
}

SrsBuffer::~SrsBuffer()
{
}

SrsBuffer* SrsBuffer::copy()
{
    SrsBuffer* cp = new SrsBuffer(bytes, nb_bytes);
    cp->p = p;
    return cp;
}

char* SrsBuffer::data()
{
    return bytes;
}

char* SrsBuffer::head()
{
    return p;
}

int SrsBuffer::size()
{
    return nb_bytes;
}

void SrsBuffer::set_size(int v)
{
    nb_bytes = v;
}

int SrsBuffer::pos()
{
    return (int)(p - bytes);
}

int SrsBuffer::left()
{
    return nb_bytes - (int)(p - bytes);
}

bool SrsBuffer::empty()
{
    return !bytes || (p >= bytes + nb_bytes);
}

bool SrsBuffer::require(int required_size)
{
    if (required_size < 0) {
        return false;
    }
    
    return required_size <= nb_bytes - (p - bytes);
}

void SrsBuffer::skip(int size)
{
    srs_assert(p);
    srs_assert(p + size >= bytes);
    srs_assert(p + size <= bytes + nb_bytes);
    
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

int16_t SrsBuffer::read_le2bytes()
{
    srs_assert(require(2));

    int16_t value;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;

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

int32_t SrsBuffer::read_le3bytes()
{
    srs_assert(require(3));

    int32_t value = 0x00;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;
    pp[2] = *p++;

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

int32_t SrsBuffer::read_le4bytes()
{
    srs_assert(require(4));

    int32_t value;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;
    pp[2] = *p++;
    pp[3] = *p++;

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

int64_t SrsBuffer::read_le8bytes()
{
    srs_assert(require(8));

    int64_t value;
    char* pp = (char*)&value;
    pp[0] = *p++;
    pp[1] = *p++;
    pp[2] = *p++;
    pp[3] = *p++;
    pp[4] = *p++;
    pp[5] = *p++;
    pp[6] = *p++;
    pp[7] = *p++;

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

void SrsBuffer::write_le2bytes(int16_t value)
{
    srs_assert(require(2));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
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

void SrsBuffer::write_le4bytes(int32_t value)
{
    srs_assert(require(4));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
}

void SrsBuffer::write_3bytes(int32_t value)
{
    srs_assert(require(3));
    
    char* pp = (char*)&value;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
}

void SrsBuffer::write_le3bytes(int32_t value)
{
    srs_assert(require(3));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
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

void SrsBuffer::write_le8bytes(int64_t value)
{
    srs_assert(require(8));

    char* pp = (char*)&value;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
    *p++ = pp[4];
    *p++ = pp[5];
    *p++ = pp[6];
    *p++ = pp[7];
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

SrsBitBuffer::SrsBitBuffer(SrsBuffer* b)
{
    cb = 0;
    cb_left = 0;
    stream = b;
}

SrsBitBuffer::~SrsBitBuffer()
{
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

