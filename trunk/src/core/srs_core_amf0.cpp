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
#include <srs_core_error.hpp>
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

// User defined
#define RTMP_AMF0_Invalid 					0x3F

SrsAmf0Any::SrsAmf0Any()
{
	marker = RTMP_AMF0_Invalid;
}

SrsAmf0Any::~SrsAmf0Any()
{
}

bool SrsAmf0Any::is_string()
{
	return marker == RTMP_AMF0_String;
}

bool SrsAmf0Any::is_boolean()
{
	return marker == RTMP_AMF0_Boolean;
}

bool SrsAmf0Any::is_number()
{
	return marker == RTMP_AMF0_Number;
}

bool SrsAmf0Any::is_object()
{
	return marker == RTMP_AMF0_Object;
}

bool SrsAmf0Any::is_object_eof()
{
	return marker == RTMP_AMF0_ObjectEnd;
}

SrsAmf0String::SrsAmf0String()
{
	marker = RTMP_AMF0_String;
}

SrsAmf0String::~SrsAmf0String()
{
}

SrsAmf0Boolean::SrsAmf0Boolean()
{
	marker = RTMP_AMF0_Boolean;
	value = false;
}

SrsAmf0Boolean::~SrsAmf0Boolean()
{
}

SrsAmf0Number::SrsAmf0Number()
{
	marker = RTMP_AMF0_Number;
	value = 0;
}

SrsAmf0Number::~SrsAmf0Number()
{
	marker = RTMP_AMF0_ObjectEnd;
}

SrsAmf0ObjectEOF::SrsAmf0ObjectEOF()
{
	utf8_empty = 0x00;
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

SrsAmf0Any* SrsAmf0Object::get_property(std::string name)
{
	std::map<std::string, SrsAmf0Any*>::iterator it;
	
	if ((it = properties.find(name)) == properties.end()) {
		return NULL;
	}
	
	return it->second;
}

SrsAmf0Any* SrsAmf0Object::ensure_property_string(std::string name)
{
	SrsAmf0Any* prop = get_property(name);
	
	if (!prop) {
		return NULL;
	}
	
	if (!prop->is_string()) {
		return NULL;
	}
	
	return prop;
}

int srs_amf0_read_object_eof(SrsStream* stream, SrsAmf0ObjectEOF*&);

int srs_amf0_read_utf8(SrsStream* stream, std::string& value)
{
	int ret = ERROR_SUCCESS;
	
	// len
	if (!stream->require(2)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read string length failed. ret=%d", ret);
		return ret;
	}
	int16_t len = stream->read_2bytes();
	srs_verbose("amf0 read string length success. len=%d", len);
	
	// empty string
	if (len <= 0) {
		srs_verbose("amf0 read empty string. ret=%d", ret);
		return ret;
	}
	
	// data
	if (!stream->require(len)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read string data failed. ret=%d", ret);
		return ret;
	}
	std::string str = stream->read_string(len);
	
	// support utf8-1 only
	// 1.3.1 Strings and UTF-8
	// UTF8-1 = %x00-7F
	for (int i = 0; i < len; i++) {
		char ch = *(str.data() + i);
		if ((ch & 0x80) != 0) {
			ret = ERROR_RTMP_AMF0_DECODE;
			srs_error("only support utf8-1, 0x00-0x7F, actual is %#x. ret=%d", (int)ch, ret);
			return ret;
		}
	}
	
	value = str;
	srs_verbose("amf0 read string data success. str=%s", str.c_str());
	
	return ret;
}

int srs_amf0_read_string(SrsStream* stream, std::string& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read string marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_String) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check string marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_String, ret);
		return ret;
	}
	srs_verbose("amf0 read string marker success");
	
	return srs_amf0_read_utf8(stream, value);
}

int srs_amf0_read_boolean(SrsStream* stream, bool& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read bool marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_Boolean) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check bool marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Boolean, ret);
		return ret;
	}
	srs_verbose("amf0 read bool marker success");

	// value
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read bool value failed. ret=%d", ret);
		return ret;
	}

	if (stream->read_char() == 0) {
		value = false;
	} else {
		value = true;
	}
	
	srs_verbose("amf0 read bool value success. value=%d", value);
	
	return ret;
}

int srs_amf0_read_number(SrsStream* stream, double& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read number marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_Number) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check number marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Number, ret);
		return ret;
	}
	srs_verbose("amf0 read number marker success");

	// value
	if (!stream->require(8)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read number value failed. ret=%d", ret);
		return ret;
	}

	int64_t temp = stream->read_8bytes();
	memcpy(&value, &temp, 8);
	
	srs_verbose("amf0 read number value success. value=%.2f", value);
	
	return ret;
}

int srs_amf0_read_any(SrsStream* stream, SrsAmf0Any*& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read any marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_char();
	srs_verbose("amf0 any marker success");
	
	// backward the 1byte marker.
	stream->skip(-1);
	
	switch (marker) {
		case RTMP_AMF0_String: {
			std::string data;
			if ((ret = srs_amf0_read_string(stream, data)) != ERROR_SUCCESS) {
				return ret;
			}
			value = new SrsAmf0String();
			srs_amf0_convert<SrsAmf0String>(value)->value = data;
			return ret;
		}
		case RTMP_AMF0_Boolean: {
			bool data;
			if ((ret = srs_amf0_read_boolean(stream, data)) != ERROR_SUCCESS) {
				return ret;
			}
			value = new SrsAmf0Boolean();
			srs_amf0_convert<SrsAmf0Boolean>(value)->value = data;
			return ret;
		}
		case RTMP_AMF0_Number: {
			double data;
			if ((ret = srs_amf0_read_number(stream, data)) != ERROR_SUCCESS) {
				return ret;
			}
			value = new SrsAmf0Number();
			srs_amf0_convert<SrsAmf0Number>(value)->value = data;
			return ret;
		}
		case RTMP_AMF0_ObjectEnd: {
			SrsAmf0ObjectEOF* p = NULL;
			if ((ret = srs_amf0_read_object_eof(stream, p)) != ERROR_SUCCESS) {
				return ret;
			}
			value = p;
			return ret;
		}
		case RTMP_AMF0_Object: {
			SrsAmf0Object* p = NULL;
			if ((ret = srs_amf0_read_object(stream, p)) != ERROR_SUCCESS) {
				return ret;
			}
			value = p;
			return ret;
		}
		case RTMP_AMF0_Invalid:
		default: {
			ret = ERROR_RTMP_AMF0_INVALID;
			srs_error("invalid amf0 message type. marker=%#x, ret=%d", marker, ret);
			return ret;
		}
	}
	
	return ret;
}

int srs_amf0_read_object_eof(SrsStream* stream, SrsAmf0ObjectEOF*& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read object eof marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_ObjectEnd) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check object eof marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_ObjectEnd, ret);
		return ret;
	}
	srs_verbose("amf0 read object eof marker success");
	
	// value
	value = new SrsAmf0ObjectEOF();
	srs_verbose("amf0 read object eof marker success");
	
	return ret;
}

int srs_amf0_read_object(SrsStream* stream, SrsAmf0Object*& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read object marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_Object) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check object marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Object, ret);
		return ret;
	}
	srs_verbose("amf0 read object marker success");
	
	// value
	value = new SrsAmf0Object();

	while (!stream->empty()) {
		// property-name: utf8 string
		std::string property_name;
		if ((ret =srs_amf0_read_utf8(stream, property_name)) != ERROR_SUCCESS) {
			srs_error("amf0 object read property name failed. ret=%d", ret);
			return ret;
		}
		// property-value: any
		SrsAmf0Any* property_value = NULL;
		if ((ret = srs_amf0_read_any(stream, property_value)) != ERROR_SUCCESS) {
			srs_error("amf0 object read property_value failed. "
				"name=%s, ret=%d", property_name.c_str(), ret);
			return ret;
		}
		
		// AMF0 Object EOF.
		if (property_name.empty() || !property_value || property_value->is_object_eof()) {
			if (property_value) {
				delete property_value;
			}
			srs_info("amf0 read object EOF.");
			break;
		}
		
		// add property
		value->properties[property_name] = property_value;
	}
	
	return ret;
}
