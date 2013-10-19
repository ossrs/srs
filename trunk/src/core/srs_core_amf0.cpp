/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#include <srs_core_amf0.hpp>

#include <srs_core_log.hpp>
#include <srs_core_stream.hpp>

// AMF0 marker
#define RTMP_AMF0_Number 					0x00
#define RTMP_AMF0_Boolean 					0x01
#define RTMP_AMF0_String 					0x02
#define RTMP_AMF0_Object 					0x03
#define RTMP_AMF0_MovieClip 				0x04 // reserved, not supported
#define RTMP_AMF0_Null 						0x05
#define RTMP_AMF0_Undefined 				0x06
#define RTMP_AMF0_Reference 				0x07
#define RTMP_AMF0_EcmaArray 				0x08
#define RTMP_AMF0_ObjectEnd 				0x09
#define RTMP_AMF0_StrictArray 				0x0A
#define RTMP_AMF0_Date 						0x0B
#define RTMP_AMF0_LongString 				0x0C
#define RTMP_AMF0_UnSupported 				0x0D
#define RTMP_AMF0_RecordSet 				0x0E // reserved, not supported
#define RTMP_AMF0_XmlDocument 				0x0F
#define RTMP_AMF0_TypedObject 				0x10
// AVM+ object is the AMF3 object.
#define RTMP_AMF0_AVMplusObject 			0x11
// origin array whos data takes the same form as LengthValueBytes
#define RTMP_AMF0_OriginStrictArray 		0x20

std::string srs_amf0_read_utf8(SrsStream* stream)
{
	std::string str;
	
	// len
	if (!stream->require(2)) {
		srs_warn("amf0 read string length failed");
		return str;
	}
	int16_t len = stream->read_2bytes();
	srs_verbose("amf0 read string length success. len=%d", len);
	
	// empty string
	if (len <= 0) {
		srs_verbose("amf0 read empty string.");
		return str;
	}
	
	// data
	if (!stream->require(len)) {
		srs_warn("amf0 read string data failed");
		return str;
	}
	str = stream->read_string(len);
	
	// support utf8-1 only
	// 1.3.1 Strings and UTF-8
	// UTF8-1 = %x00-7F
	for (int i = 0; i < len; i++) {
		char ch = *(str.data() + i);
		if ((ch & 0x80) != 0) {
			srs_warn("only support utf8-1, 0x00-0x7F, actual is %#x", (int)ch);
			return "";
		}
	}
	srs_verbose("amf0 read string data success. str=%s", str.c_str());
	
	return str;
}

std::string srs_amf0_read_string(SrsStream* stream)
{
	// marker
	if (!stream->require(1)) {
		srs_warn("amf0 read string marker failed");
		return "";
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_String) {
		srs_warn("amf0 check string marker failed. marker=%#x, required=%#x", marker, RTMP_AMF0_String);
		return "";
	}
	srs_verbose("amf0 read string marker success");
	
	return srs_amf0_read_utf8(stream);
}

double srs_amf0_read_number(SrsStream* stream)
{
	double value = 0;
	
	// marker
	if (!stream->require(1)) {
		srs_warn("amf0 read number marker failed");
		return value;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_Number) {
		srs_warn("amf0 check number marker failed. marker=%#x, required=%#x", marker, RTMP_AMF0_Number);
		return value;
	}
	srs_verbose("amf0 read number marker success");

	// value
	if (!stream->require(8)) {
		srs_warn("amf0 read number value failed");
		return value;
	}

	int64_t temp = stream->read_8bytes();
	memcpy(&value, &temp, 8);
	
	srs_verbose("amf0 read number value success. value=%.2f", value);
	
	return value;
}

SrsAmf0Object* srs_amf0_read_object(SrsStream* stream)
{
	SrsAmf0Object* value = NULL;
	return value;
}

SrsAmf0Any::SrsAmf0Any()
{
	marker = RTMP_AMF0_Null;
}

SrsAmf0Any::~SrsAmf0Any()
{
}

bool SrsAmf0Any::is_string()
{
	return marker == RTMP_AMF0_String;
}

bool SrsAmf0Any::is_number()
{
	return marker == RTMP_AMF0_Number;
}

bool SrsAmf0Any::is_object()
{
	return marker == RTMP_AMF0_Object;
}

SrsAmf0String::SrsAmf0String()
{
	marker = RTMP_AMF0_String;
}

SrsAmf0String::~SrsAmf0String()
{
}

SrsAmf0Number::SrsAmf0Number()
{
	marker = RTMP_AMF0_Number;
	value = 0;
}

SrsAmf0Number::~SrsAmf0Number()
{
}

SrsAmf0ObjectEOF::SrsAmf0ObjectEOF()
{
	utf8_empty = 0x00;
	object_end_marker = RTMP_AMF0_ObjectEnd;
}

SrsAmf0ObjectEOF::~SrsAmf0ObjectEOF()
{
}

SrsAmf0Object::SrsAmf0Object()
{
	marker = RTMP_AMF0_Object;
}

SrsAmf0Object::~SrsAmf0Object()
{
	std::map<std::string, SrsAmf0Any*>::iterator it;
	for (it = properties.begin(); it != properties.end(); ++it) {
		SrsAmf0Any* any = it->second;
		delete any;
	}
	properties.clear();
}
