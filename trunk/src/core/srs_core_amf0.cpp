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

int srs_amf0_get_object_eof_size();
int srs_amf0_get_any_size(SrsAmf0Any* value);
int srs_amf0_read_object_eof(SrsStream* stream, SrsAmf0ObjectEOF*&);
int srs_amf0_write_object_eof(SrsStream* stream, SrsAmf0ObjectEOF*);
int srs_amf0_read_any(SrsStream* stream, SrsAmf0Any*& value);
int srs_amf0_write_any(SrsStream* stream, SrsAmf0Any* value);

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

bool SrsAmf0Any::is_null()
{
	return marker == RTMP_AMF0_Null;
}

bool SrsAmf0Any::is_undefined()
{
	return marker == RTMP_AMF0_Undefined;
}

bool SrsAmf0Any::is_object()
{
	return marker == RTMP_AMF0_Object;
}

bool SrsAmf0Any::is_ecma_array()
{
	return marker == RTMP_AMF0_EcmaArray;
}

bool SrsAmf0Any::is_object_eof()
{
	return marker == RTMP_AMF0_ObjectEnd;
}

SrsAmf0String::SrsAmf0String(const char* _value)
{
	marker = RTMP_AMF0_String;
	if (_value) {
		value = _value;
	}
}

SrsAmf0String::~SrsAmf0String()
{
}

SrsAmf0Boolean::SrsAmf0Boolean(bool _value)
{
	marker = RTMP_AMF0_Boolean;
	value = _value;
}

SrsAmf0Boolean::~SrsAmf0Boolean()
{
}

SrsAmf0Number::SrsAmf0Number(double _value)
{
	marker = RTMP_AMF0_Number;
	value = _value;
}

SrsAmf0Number::~SrsAmf0Number()
{
}

SrsAmf0Null::SrsAmf0Null()
{
	marker = RTMP_AMF0_Null;
}

SrsAmf0Null::~SrsAmf0Null()
{
}

SrsAmf0Undefined::SrsAmf0Undefined()
{
	marker = RTMP_AMF0_Undefined;
}

SrsAmf0Undefined::~SrsAmf0Undefined()
{
}

SrsAmf0ObjectEOF::SrsAmf0ObjectEOF()
{
	marker = RTMP_AMF0_ObjectEnd;
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

SrsASrsAmf0EcmaArray::SrsASrsAmf0EcmaArray()
{
	marker = RTMP_AMF0_EcmaArray;
}

SrsASrsAmf0EcmaArray::~SrsASrsAmf0EcmaArray()
{
	std::map<std::string, SrsAmf0Any*>::iterator it;
	for (it = properties.begin(); it != properties.end(); ++it) {
		SrsAmf0Any* any = it->second;
		delete any;
	}
	properties.clear();
}

SrsAmf0Any* SrsASrsAmf0EcmaArray::get_property(std::string name)
{
	std::map<std::string, SrsAmf0Any*>::iterator it;
	
	if ((it = properties.find(name)) == properties.end()) {
		return NULL;
	}
	
	return it->second;
}

SrsAmf0Any* SrsASrsAmf0EcmaArray::ensure_property_string(std::string name)
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
int srs_amf0_write_utf8(SrsStream* stream, std::string value)
{
	int ret = ERROR_SUCCESS;
	
	// len
	if (!stream->require(2)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write string length failed. ret=%d", ret);
		return ret;
	}
	stream->write_2bytes(value.length());
	srs_verbose("amf0 write string length success. len=%d", (int)value.length());
	
	// empty string
	if (value.length() <= 0) {
		srs_verbose("amf0 write empty string. ret=%d", ret);
		return ret;
	}
	
	// data
	if (!stream->require(value.length())) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write string data failed. ret=%d", ret);
		return ret;
	}
	stream->write_string(value);
	srs_verbose("amf0 write string data success. str=%s", value.c_str());
	
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
	
	char marker = stream->read_1bytes();
	if (marker != RTMP_AMF0_String) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check string marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_String, ret);
		return ret;
	}
	srs_verbose("amf0 read string marker success");
	
	return srs_amf0_read_utf8(stream, value);
}

int srs_amf0_write_string(SrsStream* stream, std::string value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write string marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_String);
	srs_verbose("amf0 write string marker success");
	
	return srs_amf0_write_utf8(stream, value);
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
	
	char marker = stream->read_1bytes();
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

	if (stream->read_1bytes() == 0) {
		value = false;
	} else {
		value = true;
	}
	
	srs_verbose("amf0 read bool value success. value=%d", value);
	
	return ret;
}
int srs_amf0_write_boolean(SrsStream* stream, bool value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write bool marker failed. ret=%d", ret);
		return ret;
	}
	stream->write_1bytes(RTMP_AMF0_Boolean);
	srs_verbose("amf0 write bool marker success");

	// value
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write bool value failed. ret=%d", ret);
		return ret;
	}

	if (value) {
		stream->write_1bytes(0x01);
	} else {
		stream->write_1bytes(0x00);
	}
	
	srs_verbose("amf0 write bool value success. value=%d", value);
	
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
	
	char marker = stream->read_1bytes();
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
int srs_amf0_write_number(SrsStream* stream, double value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write number marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_Number);
	srs_verbose("amf0 write number marker success");

	// value
	if (!stream->require(8)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write number value failed. ret=%d", ret);
		return ret;
	}

	int64_t temp = 0x00;
	memcpy(&temp, &value, 8);
	stream->write_8bytes(temp);
	
	srs_verbose("amf0 write number value success. value=%.2f", value);
	
	return ret;
}

int srs_amf0_read_null(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read null marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_1bytes();
	if (marker != RTMP_AMF0_Null) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check null marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Null, ret);
		return ret;
	}
	srs_verbose("amf0 read null success");
	
	return ret;
}
int srs_amf0_write_null(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write null marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_Null);
	srs_verbose("amf0 write null marker success");
	
	return ret;
}

int srs_amf0_read_undefined(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read undefined marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_1bytes();
	if (marker != RTMP_AMF0_Undefined) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check undefined marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Undefined, ret);
		return ret;
	}
	srs_verbose("amf0 read undefined success");
	
	return ret;
}
int srs_amf0_write_undefined(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write undefined marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_Undefined);
	srs_verbose("amf0 write undefined marker success");
	
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
	
	char marker = stream->read_1bytes();
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
		case RTMP_AMF0_Null: {
			value = new SrsAmf0Null();
			return ret;
		}
		case RTMP_AMF0_Undefined: {
			value = new SrsAmf0Undefined();
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
		case RTMP_AMF0_EcmaArray: {
			SrsASrsAmf0EcmaArray* p = NULL;
			if ((ret = srs_amf0_read_ecma_array(stream, p)) != ERROR_SUCCESS) {
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
int srs_amf0_write_any(SrsStream* stream, SrsAmf0Any* value)
{
	int ret = ERROR_SUCCESS;
	
	srs_assert(value != NULL);
	
	switch (value->marker) {
		case RTMP_AMF0_String: {
			std::string data = srs_amf0_convert<SrsAmf0String>(value)->value;
			return srs_amf0_write_string(stream, data);
		}
		case RTMP_AMF0_Boolean: {
			bool data = srs_amf0_convert<SrsAmf0Boolean>(value)->value;
			return srs_amf0_write_boolean(stream, data);
		}
		case RTMP_AMF0_Number: {
			double data = srs_amf0_convert<SrsAmf0Number>(value)->value;
			return srs_amf0_write_number(stream, data);
		}
		case RTMP_AMF0_Null: {
			return srs_amf0_write_null(stream);
		}
		case RTMP_AMF0_Undefined: {
			return srs_amf0_write_undefined(stream);
		}
		case RTMP_AMF0_ObjectEnd: {
			SrsAmf0ObjectEOF* p = srs_amf0_convert<SrsAmf0ObjectEOF>(value);
			return srs_amf0_write_object_eof(stream, p);
		}
		case RTMP_AMF0_Object: {
			SrsAmf0Object* p = srs_amf0_convert<SrsAmf0Object>(value);
			return srs_amf0_write_object(stream, p);
		}
		case RTMP_AMF0_EcmaArray: {
			SrsASrsAmf0EcmaArray* p = srs_amf0_convert<SrsASrsAmf0EcmaArray>(value);
			return srs_amf0_write_ecma_array(stream, p);
		}
		case RTMP_AMF0_Invalid:
		default: {
			ret = ERROR_RTMP_AMF0_INVALID;
			srs_error("invalid amf0 message type. marker=%#x, ret=%d", value->marker, ret);
			return ret;
		}
	}
	
	return ret;
}
int srs_amf0_get_any_size(SrsAmf0Any* value)
{
	if (!value) {
		return 0;
	}
	
	int size = 0;
	
	switch (value->marker) {
		case RTMP_AMF0_String: {
			SrsAmf0String* p = srs_amf0_convert<SrsAmf0String>(value);
			size += srs_amf0_get_string_size(p->value);
			break;
		}
		case RTMP_AMF0_Boolean: {
			size += srs_amf0_get_boolean_size();
			break;
		}
		case RTMP_AMF0_Number: {
			size += srs_amf0_get_number_size();
			break;
		}
		case RTMP_AMF0_Null: {
			size += srs_amf0_get_null_size();
			break;
		}
		case RTMP_AMF0_Undefined: {
			size += srs_amf0_get_undefined_size();
			break;
		}
		case RTMP_AMF0_ObjectEnd: {
			size += srs_amf0_get_object_eof_size();
			break;
		}
		case RTMP_AMF0_Object: {
			SrsAmf0Object* p = srs_amf0_convert<SrsAmf0Object>(value);
			size += srs_amf0_get_object_size(p);
			break;
		}
		case RTMP_AMF0_EcmaArray: {
			SrsASrsAmf0EcmaArray* p = srs_amf0_convert<SrsASrsAmf0EcmaArray>(value);
			size += srs_amf0_get_ecma_array_size(p);
			break;
		}
		default: {
			// TOOD: other AMF0 types.
			srs_warn("ignore unkown AMF0 type size.");
			break;
		}
	}
	
	return size;
}

int srs_amf0_read_object_eof(SrsStream* stream, SrsAmf0ObjectEOF*& value)
{
	int ret = ERROR_SUCCESS;
	
	// auto skip -2 to read the object eof.
	srs_assert(stream->pos() >= 2);
	stream->skip(-2);
	
	// value
	if (!stream->require(2)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read object eof value failed. ret=%d", ret);
		return ret;
	}
	int16_t temp = stream->read_2bytes();
	if (temp != 0x00) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read object eof value check failed. "
			"must be 0x00, actual is %#x, ret=%d", temp, ret);
		return ret;
	}
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read object eof marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_1bytes();
	if (marker != RTMP_AMF0_ObjectEnd) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check object eof marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_ObjectEnd, ret);
		return ret;
	}
	srs_verbose("amf0 read object eof marker success");
	
	value = new SrsAmf0ObjectEOF();
	srs_verbose("amf0 read object eof success");
	
	return ret;
}
int srs_amf0_write_object_eof(SrsStream* stream, SrsAmf0ObjectEOF* value)
{
	int ret = ERROR_SUCCESS;
	
	srs_assert(value != NULL);
	
	// value
	if (!stream->require(2)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write object eof value failed. ret=%d", ret);
		return ret;
	}
	stream->write_2bytes(0x00);
	srs_verbose("amf0 write object eof value success");
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write object eof marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_ObjectEnd);
	
	srs_verbose("amf0 read object eof success");
	
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
	
	char marker = stream->read_1bytes();
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
int srs_amf0_write_object(SrsStream* stream, SrsAmf0Object* value)
{
	int ret = ERROR_SUCCESS;

	srs_assert(value != NULL);
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write object marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_Object);
	srs_verbose("amf0 write object marker success");
	
	// value
	std::map<std::string, SrsAmf0Any*>::iterator it;
	for (it = value->properties.begin(); it != value->properties.end(); ++it) {
		std::string name = it->first;
		SrsAmf0Any* any = it->second;
		
		if ((ret = srs_amf0_write_utf8(stream, name)) != ERROR_SUCCESS) {
			srs_error("write object property name failed. ret=%d", ret);
			return ret;
		}
		
		if ((ret = srs_amf0_write_any(stream, any)) != ERROR_SUCCESS) {
			srs_error("write object property value failed. ret=%d", ret);
			return ret;
		}
		
		srs_verbose("write amf0 property success. name=%s", name.c_str());
	}
	
	if ((ret = srs_amf0_write_object_eof(stream, &value->eof)) != ERROR_SUCCESS) {
		srs_error("write object eof failed. ret=%d", ret);
		return ret;
	}
	
	srs_verbose("write amf0 object success.");
	
	return ret;
}

int srs_amf0_read_ecma_array(SrsStream* stream, SrsASrsAmf0EcmaArray*& value)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read ecma_array marker failed. ret=%d", ret);
		return ret;
	}
	
	char marker = stream->read_1bytes();
	if (marker != RTMP_AMF0_EcmaArray) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 check ecma_array marker failed. "
			"marker=%#x, required=%#x, ret=%d", marker, RTMP_AMF0_Object, ret);
		return ret;
	}
	srs_verbose("amf0 read ecma_array marker success");

	// count
	if (!stream->require(4)) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 read ecma_array count failed. ret=%d", ret);
		return ret;
	}
	
	int32_t count = stream->read_4bytes();
	srs_verbose("amf0 read ecma_array count success. count=%d", count);
	
	// value
	value = new SrsASrsAmf0EcmaArray();
	value->count = count;

	while (!stream->empty()) {
		// property-name: utf8 string
		std::string property_name;
		if ((ret =srs_amf0_read_utf8(stream, property_name)) != ERROR_SUCCESS) {
			srs_error("amf0 ecma_array read property name failed. ret=%d", ret);
			return ret;
		}
		// property-value: any
		SrsAmf0Any* property_value = NULL;
		if ((ret = srs_amf0_read_any(stream, property_value)) != ERROR_SUCCESS) {
			srs_error("amf0 ecma_array read property_value failed. "
				"name=%s, ret=%d", property_name.c_str(), ret);
			return ret;
		}
		
		// AMF0 Object EOF.
		if (property_name.empty() || !property_value || property_value->is_object_eof()) {
			if (property_value) {
				delete property_value;
			}
			srs_info("amf0 read ecma_array EOF.");
			break;
		}
		
		// add property
		value->properties[property_name] = property_value;
	}
	
	return ret;
}
int srs_amf0_write_ecma_array(SrsStream* stream, SrsASrsAmf0EcmaArray* value)
{
	int ret = ERROR_SUCCESS;

	srs_assert(value != NULL);
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write ecma_array marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_EcmaArray);
	srs_verbose("amf0 write ecma_array marker success");

	// count
	if (!stream->require(4)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write ecma_array count failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_4bytes(value->count);
	srs_verbose("amf0 write ecma_array count success. count=%d", value->count);
	
	// value
	std::map<std::string, SrsAmf0Any*>::iterator it;
	for (it = value->properties.begin(); it != value->properties.end(); ++it) {
		std::string name = it->first;
		SrsAmf0Any* any = it->second;
		
		if ((ret = srs_amf0_write_utf8(stream, name)) != ERROR_SUCCESS) {
			srs_error("write ecma_array property name failed. ret=%d", ret);
			return ret;
		}
		
		if ((ret = srs_amf0_write_any(stream, any)) != ERROR_SUCCESS) {
			srs_error("write ecma_array property value failed. ret=%d", ret);
			return ret;
		}
		
		srs_verbose("write amf0 property success. name=%s", name.c_str());
	}
	
	if ((ret = srs_amf0_write_object_eof(stream, &value->eof)) != ERROR_SUCCESS) {
		srs_error("write ecma_array eof failed. ret=%d", ret);
		return ret;
	}
	
	srs_verbose("write ecma_array object success.");
	
	return ret;
}

int srs_amf0_get_utf8_size(std::string value)
{
	return 2 + value.length();
}

int srs_amf0_get_string_size(std::string value)
{
	return 1 + srs_amf0_get_utf8_size(value);
}

int srs_amf0_get_number_size()
{
	return 1 + 8;
}

int srs_amf0_get_null_size()
{
	return 1;
}

int srs_amf0_get_undefined_size()
{
	return 1;
}

int srs_amf0_get_boolean_size()
{
	return 1 + 1;
}

int srs_amf0_get_object_size(SrsAmf0Object* obj)
{
	if (!obj) {
		return 0;
	}
	
	int size = 1;
	
	std::map<std::string, SrsAmf0Any*>::iterator it;
	for (it = obj->properties.begin(); it != obj->properties.end(); ++it) {
		std::string name = it->first;
		SrsAmf0Any* value = it->second;
		
		size += srs_amf0_get_utf8_size(name);
		size += srs_amf0_get_any_size(value);
	}
	
	size += srs_amf0_get_object_eof_size();
	
	return size;
}

int srs_amf0_get_ecma_array_size(SrsASrsAmf0EcmaArray* arr)
{
	if (!arr) {
		return 0;
	}
	
	int size = 1 + 4;
	
	std::map<std::string, SrsAmf0Any*>::iterator it;
	for (it = arr->properties.begin(); it != arr->properties.end(); ++it) {
		std::string name = it->first;
		SrsAmf0Any* value = it->second;
		
		size += srs_amf0_get_utf8_size(name);
		size += srs_amf0_get_any_size(value);
	}
	
	size += srs_amf0_get_object_eof_size();
	
	return size;
}

int srs_amf0_get_object_eof_size()
{
	return 2 + 1;
}
