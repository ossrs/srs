//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
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
private:
    static int bits_len64(uint64_t x);
public:
    static int sizeof_varint(uint64_t v);
    static srs_error_t encode(SrsBuffer* b, uint64_t v);
};

// See https://developers.google.com/protocol-buffers/docs/encoding#structure
class SrsProtobufFixed64
{
public:
    static int sizeof_int(uint64_t v);
    static srs_error_t encode(SrsBuffer* b, uint64_t v);
};

// See https://developers.google.com/protocol-buffers/docs/encoding#strings
class SrsProtobufString
{
public:
    static int sizeof_string(const std::string& v);
    static srs_error_t encode(SrsBuffer* b, const std::string& v);
};

// For embeded messages and packed repeated fields, see usage of SrsProtobufKey.
class SrsProtobufObject
{
public:
    static int sizeof_object(ISrsEncoder* obj);
    static srs_error_t encode(SrsBuffer* b, ISrsEncoder* obj);
};

// See https://developers.google.com/protocol-buffers/docs/encoding#structure
enum SrsProtobufField
{
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
//
////////////////////////////////////////////////////////////////////////////////
// If key is string, for example:
//      message Content {
//          required string value = 2;
//      }
// We can get the size of value:
//      SrsProtobufKey::sizeof_key() + SrsProtobufString::sizeof_string(value_)
// And marshal as:
//      SrsProtobufKey::encode(b, 2, SrsProtobufFieldString)
//      SrsProtobufString::encode(b, value_)
//
////////////////////////////////////////////////////////////////////////////////
// If key is varints, for example:
//      message Log {
//          required int64   time     = 1;
//      }
// We can get the size of logs:
//      SrsProtobufKey::sizeof_key() + SrsProtobufVarints::sizeof_varint(time_)
// And marshal as:
//      SrsProtobufKey::encode(b, 1, SrsProtobufFieldVarint)
//      SrsProtobufVarints::encode(b, time_)
//
////////////////////////////////////////////////////////////////////////////////
// If key is object(or embeded message, or packed repeated fields), for example:
//      message LogGroupList {
//          repeated LogGroup logGroupList = 1;
//      }
// We can get the size of logs:
//      for group in vector<LogGroup*>:
//          size += SrsProtobufKey::sizeof_key() + SrsProtobufObject::sizeof_object(group)
// And marshal as:
//      for group in vector<LogGroup*>:
//          SrsProtobufKey::encode(b, 1, SrsProtobufFieldLengthDelimited)
//          SrsProtobufObject::encode(b, group)
// Note that there is always a key and length for each repeat object.
class SrsProtobufKey
{
public:
    static int sizeof_key();
    // Each key in the streamed message is a varint with the value (field_number << 3) | wire_type – in other words,
    // the last three bits of the number store the wire type.
    static srs_error_t encode(SrsBuffer* b, uint8_t fieldId, SrsProtobufField fieldType);
};

#endif

