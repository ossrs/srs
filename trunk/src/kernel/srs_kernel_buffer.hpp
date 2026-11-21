//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_BUFFER_HPP
#define SRS_KERNEL_BUFFER_HPP

#include <srs_core.hpp>

#include <string>
#include <sys/types.h>

class SrsBuffer;

class ISrsEncoder
{
public:
    ISrsEncoder();
    virtual ~ISrsEncoder();

public:
    virtual uint64_t nb_bytes() = 0;
    virtual srs_error_t encode(SrsBuffer *buf) = 0;
};

class ISrsDecoder
{
public:
    ISrsDecoder();
    virtual ~ISrsDecoder();

public:
    virtual srs_error_t decode(SrsBuffer *buf) = 0;
};

// TODO: FIXME: protocol, amf0, json should implement this interface.
class ISrsCodec : public ISrsEncoder, public ISrsDecoder
{
public:
    ISrsCodec();
    virtual ~ISrsCodec();
};

class SrsBuffer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *p_;
    char *bytes_;
    int nb_bytes_;

public:
    SrsBuffer(char *b, int nn);
    virtual ~SrsBuffer();

public:
    SrsBuffer *copy();
    char *data();
    char *head();
    int size();
    void set_size(int v);
    int pos();
    int left();
    bool empty();
    // Callers pass sizes computed from untrusted data.
    bool require(int required_size);

public:
    void skip(int size);

public:
    int8_t read_1bytes();
    int16_t read_2bytes();
    int16_t read_le2bytes();
    int32_t read_3bytes();
    int32_t read_le3bytes();
    int32_t read_4bytes();
    int32_t read_le4bytes();
    int64_t read_8bytes();
    int64_t read_le8bytes();
    std::string read_string(int len);
    void read_bytes(char *data, int size);

public:
    void write_1bytes(int8_t value);
    void write_2bytes(int16_t value);
    void write_le2bytes(int16_t value);
    void write_4bytes(int32_t value);
    void write_le4bytes(int32_t value);
    void write_3bytes(int32_t value);
    void write_le3bytes(int32_t value);
    void write_8bytes(int64_t value);
    void write_le8bytes(int64_t value);
    void write_string(std::string value);
    void write_bytes(char *data, int size);
};

class SrsBitBuffer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int8_t cb_;
    uint8_t cb_left_;
    SrsBuffer *stream_;

public:
    SrsBitBuffer(SrsBuffer *b);
    virtual ~SrsBitBuffer();

public:
    bool empty();
    int8_t read_bit();
    bool require_bits(int n);
    int left_bits();
    void skip_bits(int n);
    int32_t read_bits(int n);
    int8_t read_8bits();
    int16_t read_16bits();
    int32_t read_32bits();
    srs_error_t read_bits_ue(uint32_t &v);
    srs_error_t read_bits_se(int32_t &v);
};

class SrsMemoryBlock
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int size_;
    char *payload_;

public:
    SrsMemoryBlock();
    virtual ~SrsMemoryBlock();

public:
    char *payload() { return payload_; }
    int size() { return size_; }

public:
    virtual void create(int size);
    virtual void create(char *data, int size);
    virtual void attach(char *data, int size);
};

#endif
