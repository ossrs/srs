//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_buffer.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

ISrsEncoder::ISrsEncoder()
{
}

ISrsEncoder::~ISrsEncoder()
{
}

ISrsDecoder::ISrsDecoder()
{
}

ISrsDecoder::~ISrsDecoder()
{
}

ISrsCodec::ISrsCodec()
{
}

ISrsCodec::~ISrsCodec()
{
}

SrsBuffer::SrsBuffer(char *b, int nn)
{
    p_ = bytes_ = b;
    nb_bytes_ = nn;
}

SrsBuffer::~SrsBuffer()
{
}

SrsBuffer *SrsBuffer::copy()
{
    SrsBuffer *cp = new SrsBuffer(bytes_, nb_bytes_);
    cp->p_ = p_;
    return cp;
}

char *SrsBuffer::data()
{
    return bytes_;
}

char *SrsBuffer::head()
{
    return p_;
}

int SrsBuffer::size()
{
    return nb_bytes_;
}

void SrsBuffer::set_size(int v)
{
    nb_bytes_ = v;
}

int SrsBuffer::pos()
{
    return (int)(p_ - bytes_);
}

int SrsBuffer::left()
{
    return nb_bytes_ - (int)(p_ - bytes_);
}

bool SrsBuffer::empty()
{
    return !bytes_ || (p_ >= bytes_ + nb_bytes_);
}

bool SrsBuffer::require(int required_size)
{
    if (required_size < 0) {
        return false;
    }

    return required_size <= nb_bytes_ - (p_ - bytes_);
}

void SrsBuffer::skip(int size)
{
    srs_assert(p_);
    srs_assert(p_ + size >= bytes_);
    srs_assert(p_ + size <= bytes_ + nb_bytes_);

    p_ += size;
}

int8_t SrsBuffer::read_1bytes()
{
    srs_assert(require(1));

    return (int8_t)*p_++;
}

int16_t SrsBuffer::read_2bytes()
{
    srs_assert(require(2));

    int16_t value;
    char *pp = (char *)&value;
    pp[1] = *p_++;
    pp[0] = *p_++;

    return value;
}

int16_t SrsBuffer::read_le2bytes()
{
    srs_assert(require(2));

    int16_t value;
    char *pp = (char *)&value;
    pp[0] = *p_++;
    pp[1] = *p_++;

    return value;
}

int32_t SrsBuffer::read_3bytes()
{
    srs_assert(require(3));

    int32_t value = 0x00;
    char *pp = (char *)&value;
    pp[2] = *p_++;
    pp[1] = *p_++;
    pp[0] = *p_++;

    return value;
}

int32_t SrsBuffer::read_le3bytes()
{
    srs_assert(require(3));

    int32_t value = 0x00;
    char *pp = (char *)&value;
    pp[0] = *p_++;
    pp[1] = *p_++;
    pp[2] = *p_++;

    return value;
}

int32_t SrsBuffer::read_4bytes()
{
    srs_assert(require(4));

    int32_t value;
    char *pp = (char *)&value;
    pp[3] = *p_++;
    pp[2] = *p_++;
    pp[1] = *p_++;
    pp[0] = *p_++;

    return value;
}

int32_t SrsBuffer::read_le4bytes()
{
    srs_assert(require(4));

    int32_t value;
    char *pp = (char *)&value;
    pp[0] = *p_++;
    pp[1] = *p_++;
    pp[2] = *p_++;
    pp[3] = *p_++;

    return value;
}

int64_t SrsBuffer::read_8bytes()
{
    srs_assert(require(8));

    int64_t value;
    char *pp = (char *)&value;
    pp[7] = *p_++;
    pp[6] = *p_++;
    pp[5] = *p_++;
    pp[4] = *p_++;
    pp[3] = *p_++;
    pp[2] = *p_++;
    pp[1] = *p_++;
    pp[0] = *p_++;

    return value;
}

int64_t SrsBuffer::read_le8bytes()
{
    srs_assert(require(8));

    int64_t value;
    char *pp = (char *)&value;
    pp[0] = *p_++;
    pp[1] = *p_++;
    pp[2] = *p_++;
    pp[3] = *p_++;
    pp[4] = *p_++;
    pp[5] = *p_++;
    pp[6] = *p_++;
    pp[7] = *p_++;

    return value;
}

string SrsBuffer::read_string(int len)
{
    srs_assert(require(len));

    std::string value;
    value.append(p_, len);

    p_ += len;

    return value;
}

void SrsBuffer::read_bytes(char *data, int size)
{
    srs_assert(require(size));

    memcpy(data, p_, size);

    p_ += size;
}

void SrsBuffer::write_1bytes(int8_t value)
{
    srs_assert(require(1));

    *p_++ = value;
}

void SrsBuffer::write_2bytes(int16_t value)
{
    srs_assert(require(2));

    char *pp = (char *)&value;
    *p_++ = pp[1];
    *p_++ = pp[0];
}

void SrsBuffer::write_le2bytes(int16_t value)
{
    srs_assert(require(2));

    char *pp = (char *)&value;
    *p_++ = pp[0];
    *p_++ = pp[1];
}

void SrsBuffer::write_4bytes(int32_t value)
{
    srs_assert(require(4));

    char *pp = (char *)&value;
    *p_++ = pp[3];
    *p_++ = pp[2];
    *p_++ = pp[1];
    *p_++ = pp[0];
}

void SrsBuffer::write_le4bytes(int32_t value)
{
    srs_assert(require(4));

    char *pp = (char *)&value;
    *p_++ = pp[0];
    *p_++ = pp[1];
    *p_++ = pp[2];
    *p_++ = pp[3];
}

void SrsBuffer::write_3bytes(int32_t value)
{
    srs_assert(require(3));

    char *pp = (char *)&value;
    *p_++ = pp[2];
    *p_++ = pp[1];
    *p_++ = pp[0];
}

void SrsBuffer::write_le3bytes(int32_t value)
{
    srs_assert(require(3));

    char *pp = (char *)&value;
    *p_++ = pp[0];
    *p_++ = pp[1];
    *p_++ = pp[2];
}

void SrsBuffer::write_8bytes(int64_t value)
{
    srs_assert(require(8));

    char *pp = (char *)&value;
    *p_++ = pp[7];
    *p_++ = pp[6];
    *p_++ = pp[5];
    *p_++ = pp[4];
    *p_++ = pp[3];
    *p_++ = pp[2];
    *p_++ = pp[1];
    *p_++ = pp[0];
}

void SrsBuffer::write_le8bytes(int64_t value)
{
    srs_assert(require(8));

    char *pp = (char *)&value;
    *p_++ = pp[0];
    *p_++ = pp[1];
    *p_++ = pp[2];
    *p_++ = pp[3];
    *p_++ = pp[4];
    *p_++ = pp[5];
    *p_++ = pp[6];
    *p_++ = pp[7];
}

void SrsBuffer::write_string(string value)
{
    if (value.empty()) {
        return;
    }

    srs_assert(require((int)value.length()));

    memcpy(p_, value.data(), value.length());
    p_ += value.length();
}

void SrsBuffer::write_bytes(char *data, int size)
{
    if (size <= 0) {
        return;
    }

    srs_assert(require(size));

    memcpy(p_, data, size);
    p_ += size;
}

SrsBitBuffer::SrsBitBuffer(SrsBuffer *b)
{
    cb_ = 0;
    cb_left_ = 0;
    stream_ = b;
}

SrsBitBuffer::~SrsBitBuffer()
{
}

bool SrsBitBuffer::empty()
{
    if (cb_left_) {
        return false;
    }
    return stream_->empty();
}

bool SrsBitBuffer::require_bits(int n)
{
    if (n < 0) {
        return false;
    }

    return n <= left_bits();
}

int8_t SrsBitBuffer::read_bit()
{
    if (!cb_left_) {
        srs_assert(!stream_->empty());
        cb_ = stream_->read_1bytes();
        cb_left_ = 8;
    }

    int8_t v = (cb_ >> (cb_left_ - 1)) & 0x01;
    cb_left_--;
    return v;
}

int SrsBitBuffer::left_bits()
{
    return cb_left_ + stream_->left() * 8;
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
    if (!cb_left_) {
        srs_assert(!stream_->empty());
        return stream_->read_1bytes();
    }

    return read_bits(8);
}

int16_t SrsBitBuffer::read_16bits()
{
    // FAST_16
    if (!cb_left_) {
        srs_assert(!stream_->empty());
        return stream_->read_2bytes();
    }

    return read_bits(16);
}

int32_t SrsBitBuffer::read_32bits()
{
    // FAST_32
    if (!cb_left_) {
        srs_assert(!stream_->empty());
        return stream_->read_4bytes();
    }

    return read_bits(32);
}

srs_error_t SrsBitBuffer::read_bits_ue(uint32_t &v)
{
    srs_error_t err = srs_success;

    if (empty()) {
        return srs_error_new(ERROR_HEVC_NALU_UEV, "empty stream");
    }

    // Unsigned Exp-Golomb decoding algorithm from ITU-T H.265 specification
    // ue(v) in 9.2 Parsing process for Exp-Golomb codes
    // ITU-T-H.265-2021.pdf, page 221.
    //
    // Algorithm:
    // 1. Count leading zero bits until first '1' bit
    // 2. Read the '1' bit (prefix)
    // 3. Read leadingZeroBits more bits (suffix)
    // 4. Calculate: codeNum = (2^leadingZeroBits) - 1 + suffix_value
    //
    // Examples:
    //   "1"       -> leadingZeroBits=0, suffix=none  -> value=0
    //   "010"     -> leadingZeroBits=1, suffix=0     -> value=1
    //   "011"     -> leadingZeroBits=1, suffix=1     -> value=2
    //   "00100"   -> leadingZeroBits=2, suffix=00    -> value=3

    // Step 1: Count leading zero bits
    int leadingZeroBits = -1;
    for (int8_t b = 0; !b && !empty(); leadingZeroBits++) {
        b = read_bit();
    }

    if (leadingZeroBits >= 31) {
        return srs_error_new(ERROR_HEVC_NALU_UEV, "%dbits overflow 31bits", leadingZeroBits);
    }

    // Step 2: Calculate base value: (2^leadingZeroBits) - 1
    v = (1 << leadingZeroBits) - 1;

    // Step 3: Read suffix bits and add to base value
    for (int i = 0; i < (int)leadingZeroBits; i++) {
        if (empty()) {
            return srs_error_new(ERROR_HEVC_NALU_UEV, "no bytes for leadingZeroBits=%d", leadingZeroBits);
        }

        uint32_t b = read_bit();
        v += b << (leadingZeroBits - 1 - i);
    }

    return err;
}

srs_error_t SrsBitBuffer::read_bits_se(int32_t &v)
{
    srs_error_t err = srs_success;

    if (empty()) {
        return srs_error_new(ERROR_HEVC_NALU_SEV, "empty stream");
    }

    // Signed Exp-Golomb decoding algorithm from ITU-T H.265 specification
    // se(v) in 9.2.2 Mapping process for signed Exp-Golomb codes
    // ITU-T-H.265-2021.pdf, page 222.
    //
    // Algorithm:
    // 1. First decode as unsigned Exp-Golomb to get codeNum
    // 2. Map to signed value using alternating positive/negative pattern:
    //    - If codeNum is odd:  se_value = (codeNum + 1) / 2
    //    - If codeNum is even: se_value = -(codeNum / 2)
    //
    // Mapping table:
    //   codeNum: 0  1  2  3  4  5  6  7  8  ...
    //   se(v):   0  1 -1  2 -2  3 -3  4 -4  ...
    //
    // This encoding efficiently represents signed integers with smaller
    // absolute values using fewer bits.

    // Step 1: Decode unsigned Exp-Golomb value
    uint32_t val = 0;
    if ((err = read_bits_ue(val)) != srs_success) {
        return srs_error_wrap(err, "read uev");
    }

    // Step 2: Map unsigned code to signed value
    if (val & 0x01) {
        v = (val + 1) / 2;
    } else {
        v = -(val / 2);
    }

    return err;
}

SrsMemoryBlock::SrsMemoryBlock()
{
    payload_ = NULL;
    size_ = 0;
}

SrsMemoryBlock::~SrsMemoryBlock()
{
    srs_freepa(payload_);
}

void SrsMemoryBlock::create(int size)
{
    srs_assert(size >= 0);

    // Free existing payload
    srs_freepa(payload_);

    // Allocate new buffer
    if (size > 0) {
        payload_ = new char[size];
        memset(payload_, 0, size);
        size_ = size;
    } else {
        payload_ = NULL;
        size_ = 0;
    }
}

void SrsMemoryBlock::create(char *data, int size)
{
    srs_assert(size >= 0);

    create(size);

    // Copy data if provided
    if (data && size > 0) {
        memcpy(payload_, data, size);
        size_ = size;
    }
}

void SrsMemoryBlock::attach(char *data, int size)
{
    srs_assert(size >= 0);

    // Free existing payload
    srs_freepa(payload_);

    // Attach new buffer
    payload_ = data;
    size_ = size;
}
