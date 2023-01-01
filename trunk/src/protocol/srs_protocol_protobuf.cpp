//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_protobuf.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>

using namespace std;

// See Go bits.Len64 of package math/bits.
int SrsProtobufVarints::bits_len64(uint64_t x)
{
    // See Go bits.len8tab of package math/bits.
    static uint8_t bits_len8tab[256] = {
            0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
            0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
            0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
            0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
            0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
            0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
            0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
            0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    };

    int n = 0;
    if (x >= (uint64_t)1<<32) {
        x >>= 32;
        n = 32;
    }
    if (x >= (uint64_t)1<<16) {
        x >>= 16;
        n += 16;
    }
    if (x >= (uint64_t)1<<8) {
        x >>= 8;
        n += 8;
    }
    return n + int(bits_len8tab[x]);
}

// See Go protowire.SizeVarint of package google.golang.org/protobuf/encoding/protowire
int SrsProtobufVarints::sizeof_varint(uint64_t v)
{
    int n = bits_len64(v);
    return int(9 * uint32_t(n) + 64) / 64;
}

// See Go protowire.AppendVarint of package google.golang.org/protobuf/encoding/protowire
srs_error_t SrsProtobufVarints::encode(SrsBuffer* b, uint64_t v)
{
    srs_error_t err = srs_success;

    if (!b->require(SrsProtobufVarints::sizeof_varint(v))) {
        return srs_error_new(ERROR_PB_NO_SPACE, "require %d only %d bytes", v, b->left());
    }

    if (v < (uint64_t)1<<7) {
        b->write_1bytes((uint8_t)v);
    } else if (v < (uint64_t)1<<14) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>7));
    } else if (v < (uint64_t)1<<21) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>14));
    } else if (v < (uint64_t)1<<28) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>21));
    } else if (v < (uint64_t)1<<35) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>21)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>28));
    } else if (v < (uint64_t)1<<42) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>21)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>28)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>35));
    } else if (v < (uint64_t)1<<49) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>21)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>28)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>35)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>42));
    } else if(v < (uint64_t)1<<56) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>21)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>28)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>35)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>42)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>49));
    } else if (v < (uint64_t)1<<63) {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>21)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>28)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>35)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>42)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>49)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(v>>56));
    } else {
        b->write_1bytes((uint8_t)(((v>>0)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>7)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>14)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>21)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>28)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>35)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>42)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>49)&0x7f)|0x80));
        b->write_1bytes((uint8_t)(((v>>56)&0x7f)|0x80));
        b->write_1bytes((uint8_t)1);
    }

    return err;
}

int SrsProtobufFixed64::sizeof_int(uint64_t v)
{
    return 8;
}

srs_error_t SrsProtobufFixed64::encode(SrsBuffer* b, uint64_t v)
{
    srs_error_t  err = srs_success;

    if (!b->require(8)) {
        return srs_error_new(ERROR_PB_NO_SPACE, "require 8 only %d byte", b->left());
    }

    // Encode values in little-endian byte order,
    // see https://developers.google.com/protocol-buffers/docs/encoding#non-varint_numbers
    b->write_le8bytes((int64_t)v);

    return err;
}

// See Go protowire.SizeBytes of package google.golang.org/protobuf/encoding/protowire
int SrsProtobufString::sizeof_string(const std::string& v)
{
    uint64_t n = v.length();
    return SrsProtobufVarints::sizeof_varint(uint64_t(n)) + n;
}

// See Go protowire.AppendString of package google.golang.org/protobuf/encoding/protowire
srs_error_t SrsProtobufString::encode(SrsBuffer* b, const std::string& v)
{
    srs_error_t  err = srs_success;

    uint64_t n = v.length();
    if ((err = SrsProtobufVarints::encode(b, n)) != srs_success) {
        return srs_error_wrap(err, "string size %d", n);
    }

    // Ignore content if empty.
    if (v.empty()) {
        return err;
    }

    if (!b->require(n)) {
        return srs_error_new(ERROR_PB_NO_SPACE, "require %d only %d byte", n, b->left());
    }
    b->write_string(v);

    return err;
}

int SrsProtobufObject::sizeof_object(ISrsEncoder* obj)
{
    uint64_t size = obj->nb_bytes();
    return SrsProtobufVarints::sizeof_varint(size) + size;
}
srs_error_t SrsProtobufObject::encode(SrsBuffer* b, ISrsEncoder* obj)
{
    srs_error_t err = srs_success;

    // Encode the varint size of children.
    uint64_t size = obj->nb_bytes();
    if ((err = SrsProtobufVarints::encode(b, size)) != srs_success) {
        return srs_error_wrap(err, "encode size=%d", (int)size);
    }

    // Encode the log group itself.
    if ((err = obj->encode(b)) != srs_success) {
        return srs_error_wrap(err, "encode group");
    }

    return err;
}

int SrsProtobufKey::sizeof_key()
{
    return 1;
}

srs_error_t SrsProtobufKey::encode(SrsBuffer* b, uint8_t fieldId, SrsProtobufField fieldType)
{
    srs_error_t err = srs_success;

    if (!b->require(1)) {
        return srs_error_new(ERROR_PB_NO_SPACE, "require 1 byte");
    }

    uint8_t v = (fieldId << 3) | uint8_t(fieldType);
    b->write_1bytes(v);

    return err;
}

