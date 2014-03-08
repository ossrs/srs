/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_protocol_amf0.hpp>

#include <utility>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>

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

int srs_amf0_read_object_eof(SrsStream* stream, __SrsAmf0ObjectEOF*&);
int srs_amf0_write_object_eof(SrsStream* stream, __SrsAmf0ObjectEOF*);
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

string SrsAmf0Any::to_str()
{
	__SrsAmf0String* o = srs_amf0_convert<__SrsAmf0String>(this);
	return o->value;
}

bool SrsAmf0Any::to_boolean()
{
	__SrsAmf0Boolean* o = srs_amf0_convert<__SrsAmf0Boolean>(this);
	return o->value;
}

double SrsAmf0Any::to_number()
{
	__SrsAmf0Number* o = srs_amf0_convert<__SrsAmf0Number>(this);
	return o->value;
}

bool SrsAmf0Any::is_object_eof()
{
	return marker == RTMP_AMF0_ObjectEnd;
}

SrsAmf0Any* SrsAmf0Any::str(const char* value)
{
	return new __SrsAmf0String(value);
}

SrsAmf0Any* SrsAmf0Any::boolean(bool value)
{
	return new __SrsAmf0Boolean(value);
}

SrsAmf0Any* SrsAmf0Any::number(double value)
{
	return new __SrsAmf0Number(value);
}

SrsAmf0Any* SrsAmf0Any::null()
{
	return new __SrsAmf0Null();
}

SrsAmf0Any* SrsAmf0Any::undefined()
{
	return new __SrsAmf0Undefined();
}

__SrsUnSortedHashtable::__SrsUnSortedHashtable()
{
}

__SrsUnSortedHashtable::~__SrsUnSortedHashtable()
{
	std::vector<SrsObjectPropertyType>::iterator it;
	for (it = properties.begin(); it != properties.end(); ++it) {
		SrsObjectPropertyType& elem = *it;
		SrsAmf0Any* any = elem.second;
		srs_freep(any);
	}
	properties.clear();
}

int __SrsUnSortedHashtable::size()
{
	return (int)properties.size();
}

void __SrsUnSortedHashtable::clear()
{
	properties.clear();
}

std::string __SrsUnSortedHashtable::key_at(int index)
{
	srs_assert(index < size());
	SrsObjectPropertyType& elem = properties[index];
	return elem.first;
}

SrsAmf0Any* __SrsUnSortedHashtable::value_at(int index)
{
	srs_assert(index < size());
	SrsObjectPropertyType& elem = properties[index];
	return elem.second;
}

void __SrsUnSortedHashtable::set(std::string key, SrsAmf0Any* value)
{
	std::vector<SrsObjectPropertyType>::iterator it;
	
	for (it = properties.begin(); it != properties.end(); ++it) {
		SrsObjectPropertyType& elem = *it;
		std::string name = elem.first;
		SrsAmf0Any* any = elem.second;
		
		if (key == name) {
			srs_freep(any);
			properties.erase(it);
			break;
		}
	}
	
	properties.push_back(std::make_pair(key, value));
}

SrsAmf0Any* __SrsUnSortedHashtable::get_property(std::string name)
{
	std::vector<SrsObjectPropertyType>::iterator it;
	
	for (it = properties.begin(); it != properties.end(); ++it) {
		SrsObjectPropertyType& elem = *it;
		std::string key = elem.first;
		SrsAmf0Any* any = elem.second;
		if (key == name) {
			return any;
		}
	}
	
	return NULL;
}

SrsAmf0Any* __SrsUnSortedHashtable::ensure_property_string(std::string name)
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

SrsAmf0Any* __SrsUnSortedHashtable::ensure_property_number(std::string name)
{
	SrsAmf0Any* prop = get_property(name);
	
	if (!prop) {
		return NULL;
	}
	
	if (!prop->is_number()) {
		return NULL;
	}
	
	return prop;
}

__SrsAmf0ObjectEOF::__SrsAmf0ObjectEOF()
{
	marker = RTMP_AMF0_ObjectEnd;
	utf8_empty = 0x00;
}

__SrsAmf0ObjectEOF::~__SrsAmf0ObjectEOF()
{
}

int __SrsAmf0ObjectEOF::size()
{
	return SrsAmf0Size::object_eof();
}

SrsAmf0Object::SrsAmf0Object()
{
	marker = RTMP_AMF0_Object;
}

SrsAmf0Object::~SrsAmf0Object()
{
}

int SrsAmf0Object::read(SrsStream* stream)
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
				srs_freep(property_value);
			}
			srs_info("amf0 read object EOF.");
			break;
		}
		
		// add property
		this->set(property_name, property_value);
	}
	
	return ret;
}

int SrsAmf0Object::write(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
	// marker
	if (!stream->require(1)) {
		ret = ERROR_RTMP_AMF0_ENCODE;
		srs_error("amf0 write object marker failed. ret=%d", ret);
		return ret;
	}
	
	stream->write_1bytes(RTMP_AMF0_Object);
	srs_verbose("amf0 write object marker success");
	
	// value
	for (int i = 0; i < properties.size(); i++) {
		std::string name = this->key_at(i);
		SrsAmf0Any* any = this->value_at(i);
		
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
	
	if ((ret = srs_amf0_write_object_eof(stream, &this->eof)) != ERROR_SUCCESS) {
		srs_error("write object eof failed. ret=%d", ret);
		return ret;
	}
	
	srs_verbose("write amf0 object success.");
	
	return ret;
}

int SrsAmf0Object::size()
{
	int size = 1;
	
	for (int i = 0; i < properties.size(); i++){
		std::string name = key_at(i);
		SrsAmf0Any* value = value_at(i);
		
		size += SrsAmf0Size::utf8(name);
		size += SrsAmf0Size::any(value);
	}
	
	size += SrsAmf0Size::object_eof();
	
	return size;
}

std::string SrsAmf0Object::key_at(int index)
{
	return properties.key_at(index);
}

SrsAmf0Any* SrsAmf0Object::value_at(int index)
{
	return properties.value_at(index);
}

void SrsAmf0Object::set(std::string key, SrsAmf0Any* value)
{
	properties.set(key, value);
}

SrsAmf0Any* SrsAmf0Object::get_property(std::string name)
{
	return properties.get_property(name);
}

SrsAmf0Any* SrsAmf0Object::ensure_property_string(std::string name)
{
	return properties.ensure_property_string(name);
}

SrsAmf0Any* SrsAmf0Object::ensure_property_number(std::string name)
{
	return properties.ensure_property_number(name);
}

SrsAmf0EcmaArray::SrsAmf0EcmaArray()
{
	marker = RTMP_AMF0_EcmaArray;
}

SrsAmf0EcmaArray::~SrsAmf0EcmaArray()
{
}

int SrsAmf0EcmaArray::read(SrsStream* stream)
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
	this->count = count;

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
				srs_freep(property_value);
			}
			srs_info("amf0 read ecma_array EOF.");
			break;
		}
		
		// add property
		this->set(property_name, property_value);
	}
	
	return ret;
}
int SrsAmf0EcmaArray::write(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
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
	
	stream->write_4bytes(this->count);
	srs_verbose("amf0 write ecma_array count success. count=%d", value->count);
	
	// value
	for (int i = 0; i < properties.size(); i++) {
		std::string name = this->key_at(i);
		SrsAmf0Any* any = this->value_at(i);
		
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
	
	if ((ret = srs_amf0_write_object_eof(stream, &this->eof)) != ERROR_SUCCESS) {
		srs_error("write ecma_array eof failed. ret=%d", ret);
		return ret;
	}
	
	srs_verbose("write ecma_array object success.");
	
	return ret;
}

int SrsAmf0EcmaArray::size()
{
	int size = 1 + 4;
	
	for (int i = 0; i < properties.size(); i++){
		std::string name = key_at(i);
		SrsAmf0Any* value = value_at(i);
		
		size += SrsAmf0Size::utf8(name);
		size += SrsAmf0Size::any(value);
	}
	
	size += SrsAmf0Size::object_eof();
	
	return size;
}

void SrsAmf0EcmaArray::clear()
{
	properties.clear();
}

std::string SrsAmf0EcmaArray::key_at(int index)
{
	return properties.key_at(index);
}

SrsAmf0Any* SrsAmf0EcmaArray::value_at(int index)
{
	return properties.value_at(index);
}

void SrsAmf0EcmaArray::set(std::string key, SrsAmf0Any* value)
{
	properties.set(key, value);
}

SrsAmf0Any* SrsAmf0EcmaArray::get_property(std::string name)
{
	return properties.get_property(name);
}

SrsAmf0Any* SrsAmf0EcmaArray::ensure_property_string(std::string name)
{
	return properties.ensure_property_string(name);
}

int SrsAmf0Size::utf8(string value)
{
	return 2 + value.length();
}

int SrsAmf0Size::str(string value)
{
	return 1 + SrsAmf0Size::utf8(value);
}

int SrsAmf0Size::number()
{
	return 1 + 8;
}

int SrsAmf0Size::null()
{
	return 1;
}

int SrsAmf0Size::undefined()
{
	return 1;
}

int SrsAmf0Size::boolean()
{
	return 1 + 1;
}

int SrsAmf0Size::object(SrsAmf0Object* obj)
{
	if (!obj) {
		return 0;
	}
	
	return obj->size();
}

int SrsAmf0Size::object_eof()
{
	return 2 + 1;
}

int SrsAmf0Size::array(SrsAmf0EcmaArray* arr)
{
	if (!arr) {
		return 0;
	}
	
	return arr->size();
}

int SrsAmf0Size::any(SrsAmf0Any* o)
{
	if (!o) {
		return 0;
	}
	
	return o->size();
}

__SrsAmf0String::__SrsAmf0String(const char* _value)
{
	marker = RTMP_AMF0_String;
	if (_value) {
		value = _value;
	}
}

__SrsAmf0String::~__SrsAmf0String()
{
}

int __SrsAmf0String::size()
{
	return SrsAmf0Size::str(value);
}

__SrsAmf0Boolean::__SrsAmf0Boolean(bool _value)
{
	marker = RTMP_AMF0_Boolean;
	value = _value;
}

__SrsAmf0Boolean::~__SrsAmf0Boolean()
{
}

int __SrsAmf0Boolean::size()
{
	return SrsAmf0Size::boolean();
}

__SrsAmf0Number::__SrsAmf0Number(double _value)
{
	marker = RTMP_AMF0_Number;
	value = _value;
}

__SrsAmf0Number::~__SrsAmf0Number()
{
}

int __SrsAmf0Number::size()
{
	return SrsAmf0Size::number();
}

__SrsAmf0Null::__SrsAmf0Null()
{
	marker = RTMP_AMF0_Null;
}

__SrsAmf0Null::~__SrsAmf0Null()
{
}

int __SrsAmf0Null::size()
{
	return SrsAmf0Size::null();
}

__SrsAmf0Undefined::__SrsAmf0Undefined()
{
	marker = RTMP_AMF0_Undefined;
}

__SrsAmf0Undefined::~__SrsAmf0Undefined()
{
}

int __SrsAmf0Undefined::size()
{
	return SrsAmf0Size::undefined();
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
			srs_error("ignored. only support utf8-1, 0x00-0x7F, actual is %#x. ret=%d", (int)ch, ret);
			ret = ERROR_SUCCESS;
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
			value = SrsAmf0Any::str(data.c_str());
			return ret;
		}
		case RTMP_AMF0_Boolean: {
			bool data;
			if ((ret = srs_amf0_read_boolean(stream, data)) != ERROR_SUCCESS) {
				return ret;
			}
			value = SrsAmf0Any::boolean(data);
			return ret;
		}
		case RTMP_AMF0_Number: {
			double data;
			if ((ret = srs_amf0_read_number(stream, data)) != ERROR_SUCCESS) {
				return ret;
			}
			value = SrsAmf0Any::number(data);
			return ret;
		}
		case RTMP_AMF0_Null: {
			stream->skip(1);
			value = new __SrsAmf0Null();
			return ret;
		}
		case RTMP_AMF0_Undefined: {
			stream->skip(1);
			value = new __SrsAmf0Undefined();
			return ret;
		}
		case RTMP_AMF0_ObjectEnd: {
			__SrsAmf0ObjectEOF* p = NULL;
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
			SrsAmf0EcmaArray* p = NULL;
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
			std::string data = srs_amf0_convert<__SrsAmf0String>(value)->value;
			return srs_amf0_write_string(stream, data);
		}
		case RTMP_AMF0_Boolean: {
			bool data = srs_amf0_convert<__SrsAmf0Boolean>(value)->value;
			return srs_amf0_write_boolean(stream, data);
		}
		case RTMP_AMF0_Number: {
			double data = srs_amf0_convert<__SrsAmf0Number>(value)->value;
			return srs_amf0_write_number(stream, data);
		}
		case RTMP_AMF0_Null: {
			return srs_amf0_write_null(stream);
		}
		case RTMP_AMF0_Undefined: {
			return srs_amf0_write_undefined(stream);
		}
		case RTMP_AMF0_ObjectEnd: {
			__SrsAmf0ObjectEOF* p = srs_amf0_convert<__SrsAmf0ObjectEOF>(value);
			return srs_amf0_write_object_eof(stream, p);
		}
		case RTMP_AMF0_Object: {
			SrsAmf0Object* p = srs_amf0_convert<SrsAmf0Object>(value);
			return srs_amf0_write_object(stream, p);
		}
		case RTMP_AMF0_EcmaArray: {
			SrsAmf0EcmaArray* p = srs_amf0_convert<SrsAmf0EcmaArray>(value);
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

int srs_amf0_read_object_eof(SrsStream* stream, __SrsAmf0ObjectEOF*& value)
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
	
	value = new __SrsAmf0ObjectEOF();
	srs_verbose("amf0 read object eof success");
	
	return ret;
}
int srs_amf0_write_object_eof(SrsStream* stream, __SrsAmf0ObjectEOF* value)
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
	
	value = new SrsAmf0Object();
	
	if ((ret = value->read(stream)) != ERROR_SUCCESS) {
		srs_freep(value);
		return ret;
	}
	
	return ret;
}
int srs_amf0_write_object(SrsStream* stream, SrsAmf0Object* value)
{
	return value->write(stream);
}

int srs_amf0_read_ecma_array(SrsStream* stream, SrsAmf0EcmaArray*& value)
{
	int ret = ERROR_SUCCESS;
	
	value = new SrsAmf0EcmaArray();
	
	if ((ret = value->read(stream)) != ERROR_SUCCESS) {
		srs_freep(value);
		return ret;
	}
	
	return ret;
}
int srs_amf0_write_ecma_array(SrsStream* stream, SrsAmf0EcmaArray* value)
{
	return value->write(stream);
}
