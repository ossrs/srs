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

#ifndef SRS_RTMP_PROTOCOL_AMF0_HPP
#define SRS_RTMP_PROTOCOL_AMF0_HPP

/*
#include <srs_protocol_amf0.hpp>
*/

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsStream;
class SrsAmf0Object;
class SrsAmf0EcmaArray;
class __SrsUnSortedHashtable;
class __SrsAmf0ObjectEOF;

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// amf0 codec
// 1. SrsAmf0Any: read any from stream
//		SrsAmf0Any* pany = NULL;
//		if ((ret = srs_amf0_read_any(stream, &pany)) != ERROR_SUCCESS) {
//			return ret;
// 		}
//		srs_assert(pany); // if success, always valid object.
// 2. SrsAmf0Any: convert to specifid type, for instance, string
//		SrsAmf0Any* pany = ...
//		if (pany->is_string()) {
//			string v = pany->to_str();
//		}
// 3. SrsAmf0Any: parse specified type to any, for instance, string
//		SrsAmf0Any* pany = SrsAmf0Any::str("winlin");
// 4. SrsAmf0Size: get amf0 instance size
//		int size = SrsAmf0Size::str("winlin");
// 5. SrsAmf0Object: the amf0 object.
//		SrsAmf0Object* obj = SrsAmf0Any::object();
// 5. SrsAmf0EcmaArray: the amf0 ecma array.
//		SrsAmf0EcmaArray* arr = SrsAmf0Any::array();
// for detail usage, see interfaces of each object.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

/**
* any amf0 value.
* 2.1 Types Overview
* value-type = number-type | boolean-type | string-type | object-type 
* 		| null-marker | undefined-marker | reference-type | ecma-array-type 
* 		| strict-array-type | date-type | long-string-type | xml-document-type 
* 		| typed-object-type
*/
class SrsAmf0Any
{
public:
	char marker;
public:
	SrsAmf0Any();
	virtual ~SrsAmf0Any();
public:
	virtual bool is_string();
	virtual bool is_boolean();
	virtual bool is_number();
	virtual bool is_null();
	virtual bool is_undefined();
	virtual bool is_object();
	virtual bool is_object_eof();
	virtual bool is_ecma_array();
public:
	/**
	* get the string of any when is_string() indicates true.
	* user must ensure the type is a string, or assert failed.
	*/
	virtual std::string to_str();
	/**
	* get the boolean of any when is_boolean() indicates true.
	* user must ensure the type is a boolean, or assert failed.
	*/
	virtual bool to_boolean();
	/**
	* get the number of any when is_number() indicates true.
	* user must ensure the type is a number, or assert failed.
	*/
	virtual double to_number();
	/**
	* get the object of any when is_object() indicates true.
	* user must ensure the type is a object, or assert failed.
	*/
	virtual SrsAmf0Object* to_object();
	/**
	* get the ecma array of any when is_ecma_array() indicates true.
	* user must ensure the type is a ecma array, or assert failed.
	*/
	virtual SrsAmf0EcmaArray* to_array();
public:
	/**
	* get the size of amf0 any, including the marker size.
	*/
	virtual int size() = 0;
	/**
	* read elem from stream
	*/
	virtual int read(SrsStream* stream) = 0;
	virtual int write(SrsStream* stream) = 0;
public:
	static SrsAmf0Any* str(const char* value = NULL); 
	static SrsAmf0Any* boolean(bool value = false);
	static SrsAmf0Any* number(double value = 0.0);
	static SrsAmf0Any* null();
	static SrsAmf0Any* undefined();
	static SrsAmf0Object* object();
	static SrsAmf0Any* object_eof();
	static SrsAmf0EcmaArray* array();
public:
	static int discovery(SrsStream* stream, SrsAmf0Any** ppvalue);
};

/**
* 2.5 Object Type
* anonymous-object-type = object-marker *(object-property)
* object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
*/
class SrsAmf0Object : public SrsAmf0Any
{
private:
	__SrsUnSortedHashtable* properties;
	__SrsAmf0ObjectEOF* eof;

private:
	// use SrsAmf0Any::object() to create it.
	friend class SrsAmf0Any;
	SrsAmf0Object();
public:
	virtual ~SrsAmf0Object();

	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
	
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
class SrsAmf0EcmaArray : public SrsAmf0Any
{
private:
	__SrsUnSortedHashtable* properties;
	__SrsAmf0ObjectEOF* eof;
	int32_t count;

private:
	// use SrsAmf0Any::array() to create it.
	friend class SrsAmf0Any;
	SrsAmf0EcmaArray();
public:
	virtual ~SrsAmf0EcmaArray();

	virtual int read(SrsStream* stream);
	virtual int write(SrsStream* stream);
	
	virtual int size();
	virtual void clear();
	virtual std::string key_at(int index);
	virtual SrsAmf0Any* value_at(int index);
	virtual void set(std::string key, SrsAmf0Any* value);

	virtual SrsAmf0Any* get_property(std::string name);
	virtual SrsAmf0Any* ensure_property_string(std::string name);
};

/**
* the class to get amf0 object size
*/
class SrsAmf0Size
{
public:
	static int utf8(std::string value);
	static int str(std::string value);
	static int number();
	static int null();
	static int undefined();
	static int boolean();
	static int object(SrsAmf0Object* obj);
	static int object_eof();
	static int array(SrsAmf0EcmaArray* arr);
	static int any(SrsAmf0Any* o);
};

/**
* read anything from stream.
* @param ppvalue, the output amf0 any elem.
* 		NULL if error; otherwise, never NULL and user must free it.
*/
extern int srs_amf0_read_any(SrsStream* stream, SrsAmf0Any** ppvalue);

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
extern int srs_amf0_read_ecma_array(SrsStream* stream, SrsAmf0EcmaArray*& value);
extern int srs_amf0_write_ecma_array(SrsStream* stream, SrsAmf0EcmaArray* value);

#endif