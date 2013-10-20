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

#ifndef SRS_CORE_AMF0_HPP
#define SRS_CORE_AMF0_HPP

/*
#include <srs_core_amf0.hpp>
*/

#include <srs_core.hpp>

#include <string>
#include <map>

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
	virtual bool is_object();
	virtual bool is_object_eof();
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

	SrsAmf0String();
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

	SrsAmf0Boolean();
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

	SrsAmf0Number();
	virtual ~SrsAmf0Number();
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
* 2.5 Object Type
* anonymous-object-type = object-marker *(object-property)
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
struct SrsAmf0Object : public SrsAmf0Any
{
	std::map<std::string, SrsAmf0Any*> properties;
	SrsAmf0ObjectEOF eof;

	SrsAmf0Object();
	virtual ~SrsAmf0Object();

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
* read amf0 object from stream.
* 2.5 Object Type
* anonymous-object-type = object-marker *(object-property)
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
extern int srs_amf0_read_object(SrsStream* stream, SrsAmf0Object*& value);
extern int srs_amf0_write_object(SrsStream* stream, SrsAmf0Object* value);

/**
* get amf0 objects size.
*/
extern int srs_amf0_get_utf8_size(std::string value);
extern int srs_amf0_get_string_size(std::string value);
extern int srs_amf0_get_number_size();
extern int srs_amf0_get_boolean_size();
extern int srs_amf0_get_object_size(SrsAmf0Object* obj);
	
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