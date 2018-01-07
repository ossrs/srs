/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_PROTOCOL_JSON_HPP
#define SRS_PROTOCOL_JSON_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// json decode
// 1. SrsJsonAny: read any from str:char*
//        SrsJsonAny* any = NULL;
//        if ((any = SrsJsonAny::loads(str)) == NULL) {
//            return -1;
//         }
//        srs_assert(pany); // if success, always valid object.
// 2. SrsJsonAny: convert to specifid type, for instance, string
//        SrsJsonAny* any = ...
//        if (any->is_string()) {
//            string v = any->to_str();
//        }
//
// for detail usage, see interfaces of each object.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// @see: https://github.com/udp/json-parser

class SrsAmf0Any;
class SrsJsonArray;
class SrsJsonObject;

class SrsJsonAny
{
public:
    char marker;
    // donot directly create this object,
    // instead, for examle, use SrsJsonAny::str() to create a concreated one.
protected:
    SrsJsonAny();
public:
    virtual ~SrsJsonAny();
public:
    virtual bool is_string();
    virtual bool is_boolean();
    virtual bool is_integer();
    virtual bool is_number();
    virtual bool is_object();
    virtual bool is_array();
    virtual bool is_null();
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
     * get the integer of any when is_integer() indicates true.
     * user must ensure the type is a integer, or assert failed.
     */
    virtual int64_t to_integer();
    /**
     * get the number of any when is_number() indicates true.
     * user must ensure the type is a number, or assert failed.
     */
    virtual double to_number();
    /**
     * get the object of any when is_object() indicates true.
     * user must ensure the type is a object, or assert failed.
     */
    virtual SrsJsonObject* to_object();
    /**
     * get the ecma array of any when is_ecma_array() indicates true.
     * user must ensure the type is a ecma array, or assert failed.
     */
    virtual SrsJsonArray* to_array();
public:
    virtual std::string dumps();
    virtual SrsAmf0Any* to_amf0();
public:
    static SrsJsonAny* str(const char* value = NULL);
    static SrsJsonAny* str(const char* value, int length);
    static SrsJsonAny* boolean(bool value = false);
    static SrsJsonAny* integer(int64_t value = 0);
    static SrsJsonAny* number(double value = 0.0);
    static SrsJsonAny* null();
    static SrsJsonObject* object();
    static SrsJsonArray* array();
public:
    /**
     * read json tree from string.
     * @return json object. NULL if error.
     */
    static SrsJsonAny* loads(const std::string& str);
};

class SrsJsonObject : public SrsJsonAny
{
private:
    typedef std::pair<std::string, SrsJsonAny*> SrsJsonObjectPropertyType;
    std::vector<SrsJsonObjectPropertyType> properties;
private:
    // use SrsJsonAny::object() to create it.
    friend class SrsJsonAny;
    SrsJsonObject();
public:
    virtual ~SrsJsonObject();
public:
    virtual int count();
    // @remark: max index is count().
    virtual std::string key_at(int index);
    // @remark: max index is count().
    virtual SrsJsonAny* value_at(int index);
public:
    virtual std::string dumps();
    virtual SrsAmf0Any* to_amf0();
public:
    virtual void set(std::string key, SrsJsonAny* value);
    virtual SrsJsonAny* get_property(std::string name);
    virtual SrsJsonAny* ensure_property_string(std::string name);
    virtual SrsJsonAny* ensure_property_integer(std::string name);
    virtual SrsJsonAny* ensure_property_number(std::string name);
    virtual SrsJsonAny* ensure_property_boolean(std::string name);
    virtual SrsJsonAny* ensure_property_object(std::string name);
    virtual SrsJsonAny* ensure_property_array(std::string name);
};

class SrsJsonArray : public SrsJsonAny
{
private:
    std::vector<SrsJsonAny*> properties;
    
private:
    // use SrsJsonAny::array() to create it.
    friend class SrsJsonAny;
    SrsJsonArray();
public:
    virtual ~SrsJsonArray();
public:
    virtual int count();
    // @remark: max index is count().
    virtual SrsJsonAny* at(int index);
    virtual void add(SrsJsonAny* value);
    // alias to add.
    virtual void append(SrsJsonAny* value);
public:
    virtual std::string dumps();
    virtual SrsAmf0Any* to_amf0();
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// json encode, please use JSON.dumps() to encode json object.

#endif
