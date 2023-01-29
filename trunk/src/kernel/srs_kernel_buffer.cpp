//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
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
    if (value.empty()) {
        return;
    }

    srs_assert(require((int)value.length()));
    
    memcpy(p, value.data(), value.length());
    p += value.length();
}

void SrsBuffer::write_bytes(char* data, int size)
{
    if (size <= 0) {
        return;
    }

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

bool SrsBitBuffer::require_bits(int n)
{
    if (n < 0) {
        return false;
    }

    return n <= left_bits();
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

int SrsBitBuffer::left_bits()
{
    return cb_left + stream->left() * 8;
}

void SrsBitBuffer::skip_bits(int n)
{
    srs_assert(n <= left_bits());

    for (int i = 0; i < n; i++) {
        read_bit();
    }
}

int32_t SrsBitBuffer::read_bits(int n)
{
    srs_assert(n <= left_bits());

    int32_t v = 0;
    for (int i = 0; i < n; i++) {
        v |= (read_bit() << (n - i - 1));
    }
    return v;
}

int8_t SrsBitBuffer::read_8bits()
{
    // FAST_8
    if (!cb_left) {
        srs_assert(!stream->empty());
        return stream->read_1bytes();
    }

    return read_bits(8);
}

int16_t SrsBitBuffer::read_16bits()
{
    // FAST_16
    if (!cb_left) {
        srs_assert(!stream->empty());
        return stream->read_2bytes();
    }

    return read_bits(16);
}

int32_t SrsBitBuffer::read_32bits()
{
    // FAST_32
    if (!cb_left) {
        srs_assert(!stream->empty());
        return stream->read_4bytes();
    }

    return read_bits(32);
}

srs_error_t SrsBitBuffer::read_bits_ue(uint32_t& v)
{
    srs_error_t err = srs_success;

    if (empty()) {
        return srs_error_new(ERROR_HEVC_NALU_UEV, "empty stream");
    }

    // ue(v) in 9.2 Parsing process for Exp-Golomb codes
    // ITU-T-H.265-2021.pdf, page 221.
    // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
    //      leadingZeroBits = -1;
    //      for( b = 0; !b; leadingZeroBits++ )
    //          b = read_bits( 1 )
    // The variable codeNum is then assigned as follows:
    //      codeNum = (2<<leadingZeroBits) - 1 + read_bits( leadingZeroBits )
    int leadingZeroBits = -1;
    for (int8_t b = 0; !b && !empty(); leadingZeroBits++) {
        b = read_bit();
    }

    if (leadingZeroBits >= 31) {
        return srs_error_new(ERROR_HEVC_NALU_UEV, "%dbits overflow 31bits", leadingZeroBits);
    }

    v = (1 << leadingZeroBits) - 1;
    for (int i = 0; i < (int)leadingZeroBits; i++) {
        if (empty()) {
            return srs_error_new(ERROR_HEVC_NALU_UEV, "no bytes for leadingZeroBits=%d", leadingZeroBits);
        }

        uint32_t b = read_bit();
        v += b << (leadingZeroBits - 1 - i);
    }

    return err;
}

srs_error_t SrsBitBuffer::read_bits_se(int32_t& v)
{
    srs_error_t err = srs_success;

    if (empty()) {
        return srs_error_new(ERROR_HEVC_NALU_SEV, "empty stream");
    }

    // ue(v) in 9.2.1 General Parsing process for Exp-Golomb codes
    // ITU-T-H.265-2021.pdf, page 221.
    uint32_t val = 0;
    if ((err = read_bits_ue(val)) != srs_success) {
        return srs_error_wrap(err, "read uev");
    }

    // se(v) in 9.2.2 Mapping process for signed Exp-Golomb codes
    // ITU-T-H.265-2021.pdf, page 222.
    if (val & 0x01) {
        v = (val + 1) / 2;
    } else {
        v = -(val / 2);
    }

    return err;
}
