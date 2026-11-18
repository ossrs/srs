//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_PROTOBUF_HPP
#define SRS_PROTOCOL_PROTOBUF_HPP

#include <srs_core.hpp>

#include <string>

class SrsBuffer;
class ISrsEncoder;

// See https://developers.google.com/protocol-buffers/docs/encoding#varints
class SrsProtobufVarints
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static int bits_len64(uint64_t x);

public:
    static int sizeof_varint(uint64_t v);
    static srs_error_t encode(SrsBuffer *b, uint64_t v);
};

// See https://developers.google.com/protocol-buffers/docs/encoding#structure
class SrsProtobufFixed64
{
public:
    static int sizeof_int(uint64_t v);
    static srs_error_t encode(SrsBuffer *b, uint64_t v);
};

// See https://developers.google.com/protocol-buffers/docs/encoding#strings
class SrsProtobufString
{
public:
    static int sizeof_string(const std::string &v);
    static srs_error_t encode(SrsBuffer *b, const std::string &v);
};

// For embedded messages and packed repeated fields.
class SrsProtobufObject
{
public:
    static int sizeof_object(ISrsEncoder *obj);
    static srs_error_t encode(SrsBuffer *b, ISrsEncoder *obj);
};

// See https://developers.google.com/protocol-buffers/docs/encoding#structure
enum SrsProtobufField {
    // For int32, int64, uint32, uint64, sint32, sint64, bool, enum
    SrsProtobufFieldEnum = 0,
    SrsProtobufFieldVarint = 0,
    // For fixed64, sfixed64, double
    SrsProtobufField64bit = 1,
    // For string, bytes, embedded messages, packed repeated fields
    SrsProtobufFieldString = 2,
    SrsProtobufFieldBytes = 2,
    SrsProtobufFieldObject = 2,
    SrsProtobufFieldLengthDelimited = 2,
    // For fixed32, sfixed32, float
    SrsProtobufField32bit = 5,
};

// See https://developers.google.com/protocol-buffers/docs/encoding#structure
// See https://cloud.tencent.com/document/api/614/16873
// A field is its key followed by its value, and a repeated message has a key and
// a length for each element.
class SrsProtobufKey
{
public:
    static int sizeof_key();
    // Each key is a varint of (field_number << 3) | wire_type.
    static srs_error_t encode(SrsBuffer *b, uint8_t fieldId, SrsProtobufField fieldType);
};

#endif
