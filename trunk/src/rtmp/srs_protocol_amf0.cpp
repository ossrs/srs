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
#include <vector>
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

/**
* read amf0 string from stream.
* 2.4 String Type
* string-type = string-marker UTF-8
* @return default value is empty string.
* @remark: use SrsAmf0Any::str() to create it.
*/
class __SrsAmf0String : public SrsAmf0Any
{
public:
	std::string value;

	__SrsAmf0String(const char* _value);
	virtual ~__SrsAmf0String();
	
	virtual int total_size();
	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
};

/**
* read amf0 boolean from stream.
* 2.4 String Type
* boolean-type = boolean-marker U8
* 		0 is false, <> 0 is true
* @return default value is false.
*/
class __SrsAmf0Boolean : public SrsAmf0Any
{
public:
	bool value;

	__SrsAmf0Boolean(bool _value);
	virtual ~__SrsAmf0Boolean();
	
	virtual int total_size();
	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
};

/**
* read amf0 number from stream.
* 2.2 Number Type
* number-type = number-marker DOUBLE
* @return default value is 0.
*/
class __SrsAmf0Number : public SrsAmf0Any
{
public:
	double value;

	__SrsAmf0Number(double _value);
	virtual ~__SrsAmf0Number();
	
	virtual int total_size();
	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
};

/**
* read amf0 null from stream.
* 2.7 null Type
* null-type = null-marker
*/
class __SrsAmf0Null : public SrsAmf0Any
{
public:
	__SrsAmf0Null();
	virtual ~__SrsAmf0Null();
	
	virtual int total_size();
	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
};

/**
* read amf0 undefined from stream.
* 2.8 undefined Type
* undefined-type = undefined-marker
*/
class __SrsAmf0Undefined : public SrsAmf0Any
{
public:
	__SrsAmf0Undefined();
	virtual ~__SrsAmf0Undefined();
	
	virtual int total_size();
	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
};

/**
* to ensure in inserted order.
* for the FMLE will crash when AMF0Object is not ordered by inserted,
* if ordered in map, the string compare order, the FMLE will creash when
* get the response of connect app.
*/
class __SrsUnSortedHashtable
{
private:
	typedef std::pair<std::string, SrsAmf0Any*> SrsObjectPropertyType;
	std::vector<SrsObjectPropertyType> properties;
public:
	__SrsUnSortedHashtable();
	virtual ~__SrsUnSortedHashtable();
	
	virtual int count();
	virtual void clear();
	virtual std::string key_at(int index);
	virtual SrsAmf0Any* value_at(int index);
	virtual void set(std::string key, SrsAmf0Any* value);
	
	virtual SrsAmf0Any* get_property(std::string name);
	virtual SrsAmf0Any* ensure_property_string(std::string name);
	virtual SrsAmf0Any* ensure_property_number(std::string name);
};

/**
* 2.11 Object End Type
* object-end-type = UTF-8-empty object-end-marker
* 0x00 0x00 0x09
*/
class __SrsAmf0ObjectEOF : public SrsAmf0Any
{
public:
	int16_t utf8_empty;

	__SrsAmf0ObjectEOF();
	virtual ~__SrsAmf0ObjectEOF();
	
	virtual int total_size();
	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
};

/**
* read amf0 utf8 string from stream.
* 1.3.1 Strings and UTF-8
* UTF-8 = U16 *(UTF8-char)
* UTF8-char = UTF8-1 | UTF8-2 | UTF8-3 | UTF8-4
* UTF8-1 = %x00-7F
* @remark only support UTF8-1 char.
*/
extern int srs_amf0_read_utf8(SrsStream* stream, std::string& value);
extern int srs_amf0_write_utf8(SrsStream* stream, std::string value);

bool srs_amf0_is_object_eof(SrsStream* stream);
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
	__SrsAmf0String* p = dynamic_cast<__SrsAmf0String*>(this);
	srs_assert(p != NULL);
	return p->value;
}

bool SrsAmf0Any::to_boolean()
{
	__SrsAmf0Boolean* p = dynamic_cast<__SrsAmf0Boolean*>(this);
	srs_assert(p != NULL);
	return p->value;
}

double SrsAmf0Any::to_number()
{
	__SrsAmf0Number* p = dynamic_cast<__SrsAmf0Number*>(this);
	srs_assert(p != NULL);
	return p->value;
}

SrsAmf0Object* SrsAmf0Any::to_object()
{
	SrsAmf0Object* p = dynamic_cast<SrsAmf0Object*>(this);
	srs_assert(p != NULL);
	return p;
}

SrsAmf0EcmaArray* SrsAmf0Any::to_ecma_array()
{
	SrsAmf0EcmaArray* p = dynamic_cast<SrsAmf0EcmaArray*>(this);
	srs_assert(p != NULL);
	return p;
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

SrsAmf0Object* SrsAmf0Any::object()
{
	return new SrsAmf0Object();
}

SrsAmf0Any* SrsAmf0Any::object_eof()
{
	return new __SrsAmf0ObjectEOF();
}

SrsAmf0EcmaArray* SrsAmf0Any::ecma_array()
{
	return new SrsAmf0EcmaArray();
}

int SrsAmf0Any::discovery(SrsStream* stream, SrsAmf0Any** ppvalue)
{
	int ret = ERROR_SUCCESS;
	
	// detect the object-eof specially
	if (srs_amf0_is_object_eof(stream)) {
		*ppvalue = new __SrsAmf0ObjectEOF();
		return ret;
	}
	
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
			*ppvalue = SrsAmf0Any::str();
			return ret;
		}
		case RTMP_AMF0_Boolean: {
			*ppvalue = SrsAmf0Any::boolean();
			return ret;
		}
		case RTMP_AMF0_Number: {
			*ppvalue = SrsAmf0Any::number();
			return ret;
		}
		case RTMP_AMF0_Null: {
			*ppvalue = SrsAmf0Any::null();
			return ret;
		}
		case RTMP_AMF0_Undefined: {
			*ppvalue = SrsAmf0Any::undefined();
			return ret;
		}
		case RTMP_AMF0_Object: {
			*ppvalue = SrsAmf0Any::object();
			return ret;
		}
		case RTMP_AMF0_EcmaArray: {
			*ppvalue = SrsAmf0Any::ecma_array();
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

int __SrsUnSortedHashtable::count()
{
	return (int)properties.size();
}

void __SrsUnSortedHashtable::clear()
{
	properties.clear();
}

string __SrsUnSortedHashtable::key_at(int index)
{
	srs_assert(index < count());
	SrsObjectPropertyType& elem = properties[index];
	return elem.first;
}

SrsAmf0Any* __SrsUnSortedHashtable::value_at(int index)
{
	srs_assert(index < count());
	SrsObjectPropertyType& elem = properties[index];
	return elem.second;
}

void __SrsUnSortedHashtable::set(string key, SrsAmf0Any* value)
{
	if (!value) {
		srs_warn("add a NULL propertity %s", key.c_str());
		return;
	}
	
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

SrsAmf0Any* __SrsUnSortedHashtable::get_property(string name)
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

SrsAmf0Any* __SrsUnSortedHashtable::ensure_property_string(string name)
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

SrsAmf0Any* __SrsUnSortedHashtable::ensure_property_number(string name)
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

int __SrsAmf0ObjectEOF::total_size()
{
	return SrsAmf0Size::object_eof();
}

int __SrsAmf0ObjectEOF::read(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
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
	
	srs_verbose("amf0 read object eof success");
	
	return ret;
}
int __SrsAmf0ObjectEOF::write(SrsStream* stream)
{
	int ret = ERROR_SUCCESS;
	
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

SrsAmf0Object::SrsAmf0Object()
{
	properties = new __SrsUnSortedHashtable();
	eof = new __SrsAmf0ObjectEOF();
	marker = RTMP_AMF0_Object;
}

SrsAmf0Object::~SrsAmf0Object()
{
	srs_freep(properties);
	srs_freep(eof);
}

int SrsAmf0Object::total_size()
{
	int size = 1;
	
	for (int i = 0; i < properties->count(); i++){
		std::string name = key_at(i);
		SrsAmf0Any* value = value_at(i);
		
		size += SrsAmf0Size::utf8(name);
		size += SrsAmf0Size::any(value);
	}
	
	size += SrsAmf0Size::object_eof();
	
	return size;
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
		// detect whether is eof.
		if (srs_amf0_is_object_eof(stream)) {
			__SrsAmf0ObjectEOF pbj_eof;
			if ((ret = pbj_eof.read(stream)) != ERROR_SUCCESS) {
				srs_error("amf0 object read eof failed. ret=%d", ret);
				return ret;
			}
			srs_info("amf0 read object EOF.");
			break;
		}
		
		// property-name: utf8 string
		std::string property_name;
		if ((ret =srs_amf0_read_utf8(stream, property_name)) != ERROR_SUCCESS) {
			srs_error("amf0 object read property name failed. ret=%d", ret);
			return ret;
		}
		// property-value: any
		SrsAmf0Any* property_value = NULL;
		if ((ret = srs_amf0_read_any(stream, &property_value)) != ERROR_SUCCESS) {
			srs_error("amf0 object read property_value failed. "
				"name=%s, ret=%d", property_name.c_str(), ret);
			srs_freep(property_value);
			return ret;
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
	for (int i = 0; i < properties->count(); i++) {
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
	
	if ((ret = eof->write(stream)) != ERROR_SUCCESS) {
		srs_error("write object eof failed. ret=%d", ret);
		return ret;
	}
	
	srs_verbose("write amf0 object success.");
	
	return ret;
}

int SrsAmf0Object::count()
{
	return properties->count();
}

string SrsAmf0Object::key_at(int index)
{
	return properties->key_at(index);
}

SrsAmf0Any* SrsAmf0Object::value_at(int index)
{
	return properties->value_at(index);
}

void SrsAmf0Object::set(string key, SrsAmf0Any* value)
{
	properties->set(key, value);
}

SrsAmf0Any* SrsAmf0Object::get_property(string name)
{
	return properties->get_property(name);
}

SrsAmf0Any* SrsAmf0Object::ensure_property_string(string name)
{
	return properties->ensure_property_string(name);
}

SrsAmf0Any* SrsAmf0Object::ensure_property_number(string name)
{
	return properties->ensure_property_number(name);
}

SrsAmf0EcmaArray::SrsAmf0EcmaArray()
{
	properties = new __SrsUnSortedHashtable();
	eof = new __SrsAmf0ObjectEOF();
	marker = RTMP_AMF0_EcmaArray;
}

SrsAmf0EcmaArray::~SrsAmf0EcmaArray()
{
	srs_freep(properties);
	srs_freep(eof);
}

int SrsAmf0EcmaArray::total_size()
{
	int size = 1 + 4;
	
	for (int i = 0; i < properties->count(); i++){
		std::string name = key_at(i);
		SrsAmf0Any* value = value_at(i);
		
		size += SrsAmf0Size::utf8(name);
		size += SrsAmf0Size::any(value);
	}
	
	size += SrsAmf0Size::object_eof();
	
	return size;
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
	this->_count = count;

	while (!stream->empty()) {
		// detect whether is eof.
		if (srs_amf0_is_object_eof(stream)) {
			__SrsAmf0ObjectEOF pbj_eof;
			if ((ret = pbj_eof.read(stream)) != ERROR_SUCCESS) {
				srs_error("amf0 ecma_array read eof failed. ret=%d", ret);
				return ret;
			}
			srs_info("amf0 read ecma_array EOF.");
			break;
		}
		
		// property-name: utf8 string
		std::string property_name;
		if ((ret =srs_amf0_read_utf8(stream, property_name)) != ERROR_SUCCESS) {
			srs_error("amf0 ecma_array read property name failed. ret=%d", ret);
			return ret;
		}
		// property-value: any
		SrsAmf0Any* property_value = NULL;
		if ((ret = srs_amf0_read_any(stream, &property_value)) != ERROR_SUCCESS) {
			srs_error("amf0 ecma_array read property_value failed. "
				"name=%s, ret=%d", property_name.c_str(), ret);
			return ret;
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
	
	stream->write_4bytes(this->_count);
	srs_verbose("amf0 write ecma_array count success. count=%d", _count);
	
	// value
	for (int i = 0; i < properties->count(); i++) {
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
	
	if ((ret = eof->write(stream)) != ERROR_SUCCESS) {
		srs_error("write ecma_array eof failed. ret=%d", ret);
		return ret;
	}
	
	srs_verbose("write ecma_array object success.");
	
	return ret;
}

void SrsAmf0EcmaArray::clear()
{
	properties->clear();
}

int SrsAmf0EcmaArray::count()
{
	return properties->count();
}

string SrsAmf0EcmaArray::key_at(int index)
{
	return properties->key_at(index);
}

SrsAmf0Any* SrsAmf0EcmaArray::value_at(int index)
{
	return properties->value_at(index);
}

void SrsAmf0EcmaArray::set(string key, SrsAmf0Any* value)
{
	properties->set(key, value);
}

SrsAmf0Any* SrsAmf0EcmaArray::get_property(string name)
{
	return properties->get_property(name);
}

SrsAmf0Any* SrsAmf0EcmaArray::ensure_property_string(string name)
{
	return properties->ensure_property_string(name);
}

SrsAmf0Any* SrsAmf0EcmaArray::ensure_property_number(string name)
{
	return properties->ensure_property_number(name);
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
	
	return obj->total_size();
}

int SrsAmf0Size::object_eof()
{
	return 2 + 1;
}

int SrsAmf0Size::ecma_array(SrsAmf0EcmaArray* arr)
{
	if (!arr) {
		return 0;
	}
	
	return arr->total_size();
}

int SrsAmf0Size::any(SrsAmf0Any* o)
{
	if (!o) {
		return 0;
	}
	
	return o->total_size();
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

int __SrsAmf0String::total_size()
{
	return SrsAmf0Size::str(value);
}

int __SrsAmf0String::read(SrsStream* stream)
{
	return srs_amf0_read_string(stream, value);
}

int __SrsAmf0String::write(SrsStream* stream)
{
	return srs_amf0_write_string(stream, value);
}

__SrsAmf0Boolean::__SrsAmf0Boolean(bool _value)
{
	marker = RTMP_AMF0_Boolean;
	value = _value;
}

__SrsAmf0Boolean::~__SrsAmf0Boolean()
{
}

int __SrsAmf0Boolean::total_size()
{
	return SrsAmf0Size::boolean();
}

int __SrsAmf0Boolean::read(SrsStream* stream)
{
	return srs_amf0_read_boolean(stream, value);
}

int __SrsAmf0Boolean::write(SrsStream* stream)
{
	return srs_amf0_write_boolean(stream, value);
}

__SrsAmf0Number::__SrsAmf0Number(double _value)
{
	marker = RTMP_AMF0_Number;
	value = _value;
}

__SrsAmf0Number::~__SrsAmf0Number()
{
}

int __SrsAmf0Number::total_size()
{
	return SrsAmf0Size::number();
}

int __SrsAmf0Number::read(SrsStream* stream)
{
	return srs_amf0_read_number(stream, value);
}

int __SrsAmf0Number::write(SrsStream* stream)
{
	return srs_amf0_write_number(stream, value);
}

__SrsAmf0Null::__SrsAmf0Null()
{
	marker = RTMP_AMF0_Null;
}

__SrsAmf0Null::~__SrsAmf0Null()
{
}

int __SrsAmf0Null::total_size()
{
	return SrsAmf0Size::null();
}

int __SrsAmf0Null::read(SrsStream* stream)
{
	return srs_amf0_read_null(stream);
}

int __SrsAmf0Null::write(SrsStream* stream)
{
	return srs_amf0_write_null(stream);
}

__SrsAmf0Undefined::__SrsAmf0Undefined()
{
	marker = RTMP_AMF0_Undefined;
}

__SrsAmf0Undefined::~__SrsAmf0Undefined()
{
}

int __SrsAmf0Undefined::total_size()
{
	return SrsAmf0Size::undefined();
}

int __SrsAmf0Undefined::read(SrsStream* stream)
{
	return srs_amf0_read_undefined(stream);
}

int __SrsAmf0Undefined::write(SrsStream* stream)
{
	return srs_amf0_write_undefined(stream);
}

int srs_amf0_read_any(SrsStream* stream, SrsAmf0Any** ppvalue)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = SrsAmf0Any::discovery(stream, ppvalue)) != ERROR_SUCCESS) {
		srs_error("amf0 discovery any elem failed. ret=%d", ret);
		return ret;
	}
	
	srs_assert(*ppvalue);
	
	if ((ret = (*ppvalue)->read(stream)) != ERROR_SUCCESS) {
		srs_error("amf0 parse elem failed. ret=%d", ret);
		srs_freep(*ppvalue);
		return ret;
	}
	
	return ret;
}

int srs_amf0_write_any(SrsStream* stream, SrsAmf0Any* value)
{
	srs_assert(value != NULL);
	return value->write(stream);
}

int srs_amf0_read_utf8(SrsStream* stream, string& value)
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
	// TODO: support other utf-8 strings
	/*for (int i = 0; i < len; i++) {
		char ch = *(str.data() + i);
		if ((ch & 0x80) != 0) {
			ret = ERROR_RTMP_AMF0_DECODE;
			srs_error("ignored. only support utf8-1, 0x00-0x7F, actual is %#x. ret=%d", (int)ch, ret);
			ret = ERROR_SUCCESS;
		}
	}*/
	
	value = str;
	srs_verbose("amf0 read string data success. str=%s", str.c_str());
	
	return ret;
}
int srs_amf0_write_utf8(SrsStream* stream, string value)
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

int srs_amf0_read_string(SrsStream* stream, string& value)
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

int srs_amf0_write_string(SrsStream* stream, string value)
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

bool srs_amf0_is_object_eof(SrsStream* stream) 
{
	// detect the object-eof specially
	if (stream->require(3)) {
		int32_t flag = stream->read_3bytes();
		stream->skip(-3);
		
		return 0x09 == flag;
	}
	
	return false;
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
