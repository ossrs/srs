/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#ifndef SRS_PROTOCOL_JSON_HPP
#define SRS_PROTOCOL_JSON_HPP

/*
#include <srs_protocol_json.hpp>
*/
#include <srs_core.hpp>

#include <string>
#include <vector>

// whether use nxjson
// @see: https://bitbucket.org/yarosla/nxjson
#undef SRS_JSON_USE_NXJSON
#define SRS_JSON_USE_NXJSON

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// json decode
// 1. SrsJsonAny: read any from str:char*
//        SrsJsonAny* pany = NULL;
//        if ((ret = srs_json_read_any(str, &pany)) != ERROR_SUCCESS) {
//            return ret;
//         }
//        srs_assert(pany); // if success, always valid object.
// 2. SrsJsonAny: convert to specifid type, for instance, string
//        SrsJsonAny* pany = ...
//        if (pany->is_string()) {
//            string v = pany->to_str();
//        }
//
// for detail usage, see interfaces of each object.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// @see: https://bitbucket.org/yarosla/nxjson
// @see: https://github.com/udp/json-parser

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
    static SrsJsonAny* str(const char* value = NULL); 
    static SrsJsonAny* boolean(bool value = false);
    static SrsJsonAny* ingeter(int64_t value = 0);
    static SrsJsonAny* number(double value = 0.0);
    static SrsJsonAny* null();
    static SrsJsonObject* object();
    static SrsJsonArray* array();
public:
    /**
    * read json tree from str:char*
    * @return json object. NULL if error.
    */
    static SrsJsonAny* loads(char* str);
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
    virtual void set(std::string key, SrsJsonAny* value);
    virtual SrsJsonAny* get_property(std::string name);
    virtual SrsJsonAny* ensure_property_string(std::string name);
    virtual SrsJsonAny* ensure_property_integer(std::string name);
    virtual SrsJsonAny* ensure_property_boolean(std::string name);
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
};

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
/* json encode
    cout<< SRS_JOBJECT_START
            << SRS_JFIELD_STR("name", "srs") << SRS_JFIELD_CONT
            << SRS_JFIELD_ORG("version", 100) << SRS_JFIELD_CONT
            << SRS_JFIELD_NAME("features") << SRS_JOBJECT_START
                << SRS_JFIELD_STR("rtmp", "released") << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("hls", "released") << SRS_JFIELD_CONT
                << SRS_JFIELD_STR("dash", "plan")
            << SRS_JOBJECT_END << SRS_JFIELD_CONT
            << SRS_JFIELD_STR("author", "srs team")
        << SRS_JOBJECT_END
it's:
    cont<< "{"
            << "name:" << "srs" << ","
            << "version:" << 100 << ","
            << "features:" << "{"
                << "rtmp:" << "released" << ","
                << "hls:" << "released" << ","
                << "dash:" << "plan"
            << "}" << ","
            << "author:" << "srs team"
        << "}"
that is:
    """
        {
            "name": "srs",
            "version": 100,
            "features": {
                "rtmp": "released",
                "hls": "released",
                "dash": "plan"
            },
            "author": "srs team"
        }
    """
*/
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
#define SRS_JOBJECT_START "{"
#define SRS_JFIELD_NAME(k) "\"" << k << "\":"
#define SRS_JFIELD_OBJ(k) SRS_JFIELD_NAME(k) << SRS_JOBJECT_START
#define SRS_JFIELD_STR(k, v) SRS_JFIELD_NAME(k) << "\"" << v << "\""
#define SRS_JFIELD_ORG(k, v) SRS_JFIELD_NAME(k) << std::dec << v
#define SRS_JFIELD_BOOL(k, v) SRS_JFIELD_ORG(k, (v? "true":"false"))
#define SRS_JFIELD_NULL(k) SRS_JFIELD_NAME(k) << "null"
#define SRS_JFIELD_ERROR(ret) "\"" << "code" << "\":" << ret
#define SRS_JFIELD_CONT ","
#define SRS_JOBJECT_END "}"
#define SRS_JARRAY_START "["
#define SRS_JARRAY_END "]"

#endif
