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

#ifndef SRS_PROTOCOL_AMF0_HPP
#define SRS_PROTOCOL_AMF0_HPP

/*
#include <srs_protocol_amf0.hpp>
*/

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsStream;
class SrsAmf0Object;

/**
* any amf0 value.
* 2.1 Types Overview
* value-type = number-type | boolean-type | string-type | object-type 
* 		| null-marker | undefined-marker | reference-type | ecma-array-type 
* 		| strict-array-type | date-type | long-string-type | xml-document-type 
* 		| typed-object-type
*/
struct SrsAmf0Any
{
	char marker;

	SrsAmf0Any();
	virtual ~SrsAmf0Any();
	
	virtual bool is_string();
	virtual bool is_boolean();
	virtual bool is_number();
	virtual bool is_null();
	virtual bool is_undefined();
	virtual bool is_object();
	virtual bool is_object_eof();
	virtual bool is_ecma_array();
};

/**
* read amf0 string from stream.
* 2.4 String Type
* string-type = string-marker UTF-8
* @return default value is empty string.
*/
struct SrsAmf0String : public SrsAmf0Any
{
	std::string value;

	SrsAmf0String(const char* _value = NULL);
	virtual ~SrsAmf0String();
};

/**
* read amf0 boolean from stream.
* 2.4 String Type
* boolean-type = boolean-marker U8
* 		0 is false, <> 0 is true
* @return default value is false.
*/
struct SrsAmf0Boolean : public SrsAmf0Any
{
	bool value;

	SrsAmf0Boolean(bool _value = false);
	virtual ~SrsAmf0Boolean();
};

/**
* read amf0 number from stream.
* 2.2 Number Type
* number-type = number-marker DOUBLE
* @return default value is 0.
*/
struct SrsAmf0Number : public SrsAmf0Any
{
	double value;

	SrsAmf0Number(double _value = 0.0);
	virtual ~SrsAmf0Number();
};

/**
* read amf0 null from stream.
* 2.7 null Type
* null-type = null-marker
*/
struct SrsAmf0Null : public SrsAmf0Any
{
	SrsAmf0Null();
	virtual ~SrsAmf0Null();
};

/**
* read amf0 undefined from stream.
* 2.8 undefined Type
* undefined-type = undefined-marker
*/
struct SrsAmf0Undefined : public SrsAmf0Any
{
	SrsAmf0Undefined();
	virtual ~SrsAmf0Undefined();
};

/**
* 2.11 Object End Type
* object-end-type = UTF-8-empty object-end-marker
* 0x00 0x00 0x09
*/
struct SrsAmf0ObjectEOF : public SrsAmf0Any
{
	int16_t utf8_empty;

	SrsAmf0ObjectEOF();
	virtual ~SrsAmf0ObjectEOF();
};

/**
* to ensure in inserted order.
* for the FMLE will crash when AMF0Object is not ordered by inserted,
* if ordered in map, the string compare order, the FMLE will creash when
* get the response of connect app.
*/
struct SrsUnSortedHashtable
{
private:
	typedef std::pair<std::string, SrsAmf0Any*> SrsObjectPropertyType;
	std::vector<SrsObjectPropertyType> properties;
public:
	SrsUnSortedHashtable();
	virtual ~SrsUnSortedHashtable();
	
	virtual int size();
	virtual void clear();
	virtual std::string key_at(int index);
	virtual SrsAmf0Any* value_at(int index);
	virtual void set(std::string key, SrsAmf0Any* value);
	
	virtual SrsAmf0Any* get_property(std::string name);
	virtual SrsAmf0Any* ensure_property_string(std::string name);
	virtual SrsAmf0Any* ensure_property_number(std::string name);
};

/**
* 2.5 Object Type
* anonymous-object-type = object-marker *(object-property)
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
struct SrsAmf0Object : public SrsAmf0Any
{
private:
	SrsUnSortedHashtable properties;
public:
	SrsAmf0ObjectEOF eof;

	SrsAmf0Object();
	virtual ~SrsAmf0Object();
	
	virtual int size();
	virtual std::string key_at(int index);
	virtual SrsAmf0Any* value_at(int index);
	virtual void set(std::string key, SrsAmf0Any* value);

	virtual SrsAmf0Any* get_property(std::string name);
	virtual SrsAmf0Any* ensure_property_string(std::string name);
	virtual SrsAmf0Any* ensure_property_number(std::string name);
};

/**
* 2.10 ECMA Array Type
* ecma-array-type = associative-count *(object-property)
* associative-count = U32
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
struct SrsASrsAmf0EcmaArray : public SrsAmf0Any
{
private:
	SrsUnSortedHashtable properties;
public:
	int32_t count;
	SrsAmf0ObjectEOF eof;

	SrsASrsAmf0EcmaArray();
	virtual ~SrsASrsAmf0EcmaArray();
	
	virtual int size();
	virtual void clear();
	virtual std::string key_at(int index);
	virtual SrsAmf0Any* value_at(int index);
	virtual void set(std::string key, SrsAmf0Any* value);

	virtual SrsAmf0Any* get_property(std::string name);
	virtual SrsAmf0Any* ensure_property_string(std::string name);
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

/**
* read amf0 string from stream.
* 2.4 String Type
* string-type = string-marker UTF-8
*/
extern int srs_amf0_read_string(SrsStream* stream, std::string& value);
extern int srs_amf0_write_string(SrsStream* stream, std::string value);

/**
* read amf0 boolean from stream.
* 2.4 String Type
* boolean-type = boolean-marker U8
* 		0 is false, <> 0 is true
*/
extern int srs_amf0_read_boolean(SrsStream* stream, bool& value);
extern int srs_amf0_write_boolean(SrsStream* stream, bool value);

/**
* read amf0 number from stream.
* 2.2 Number Type
* number-type = number-marker DOUBLE
*/
extern int srs_amf0_read_number(SrsStream* stream, double& value);
extern int srs_amf0_write_number(SrsStream* stream, double value);

/**
* read amf0 null from stream.
* 2.7 null Type
* null-type = null-marker
*/
extern int srs_amf0_read_null(SrsStream* stream);
extern int srs_amf0_write_null(SrsStream* stream);

/**
* read amf0 undefined from stream.
* 2.8 undefined Type
* undefined-type = undefined-marker
*/
extern int srs_amf0_read_undefined(SrsStream* stream);
extern int srs_amf0_write_undefined(SrsStream* stream);

extern int srs_amf0_read_any(SrsStream* stream, SrsAmf0Any*& value);

/**
* read amf0 object from stream.
* 2.5 Object Type
* anonymous-object-type = object-marker *(object-property)
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
extern int srs_amf0_read_object(SrsStream* stream, SrsAmf0Object*& value);
extern int srs_amf0_write_object(SrsStream* stream, SrsAmf0Object* value);

/**
* read amf0 object from stream.
* 2.10 ECMA Array Type
* ecma-array-type = associative-count *(object-property)
* associative-count = U32
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
extern int srs_amf0_read_ecma_array(SrsStream* stream, SrsASrsAmf0EcmaArray*& value);
extern int srs_amf0_write_ecma_array(SrsStream* stream, SrsASrsAmf0EcmaArray* value);

/**
* get amf0 objects size.
*/
extern int srs_amf0_get_utf8_size(std::string value);
extern int srs_amf0_get_string_size(std::string value);
extern int srs_amf0_get_number_size();
extern int srs_amf0_get_null_size();
extern int srs_amf0_get_undefined_size();
extern int srs_amf0_get_boolean_size();
extern int srs_amf0_get_object_size(SrsAmf0Object* obj);
extern int srs_amf0_get_ecma_array_size(SrsASrsAmf0EcmaArray* arr);

/**
* convert the any to specified object.
* @return T*, the converted object. never NULL.
* @remark, user must ensure the current object type, 
* 		or the covert will cause assert failed.
*/
template<class T>
T* srs_amf0_convert(SrsAmf0Any* any)
{
	T* p = dynamic_cast<T*>(any);
	srs_assert(p != NULL);
	return p;
}

#endif