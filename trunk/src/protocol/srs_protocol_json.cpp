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

#include <srs_protocol_json.hpp>

#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>

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

#ifdef SRS_JSON_USE_NXJSON

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Copyright (c) 2013 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXJSON.
 *
 * NXJSON is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXJSON is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXJSON. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NXJSON_H
#define NXJSON_H

#ifdef  __cplusplus
extern "C" {
#endif


typedef enum nx_json_type {
  NX_JSON_NULL,    // this is null value
  NX_JSON_OBJECT,  // this is an object; properties can be found in child nodes
  NX_JSON_ARRAY,   // this is an array; items can be found in child nodes
  NX_JSON_STRING,  // this is a string; value can be found in text_value field
  NX_JSON_INTEGER, // this is an integer; value can be found in int_value field
  NX_JSON_DOUBLE,  // this is a double; value can be found in dbl_value field
  NX_JSON_BOOL     // this is a boolean; value can be found in int_value field
} nx_json_type;

typedef struct nx_json {
  nx_json_type type;       // type of json node, see above
  const char* key;         // key of the property; for object's children only
  const char* text_value;  // text value of STRING node
  long int_value;          // the value of INTEGER or BOOL node
  double dbl_value;        // the value of DOUBLE node
  int length;              // number of children of OBJECT or ARRAY
  struct nx_json* child;   // points to first child
  struct nx_json* next;    // points to next child
  struct nx_json* last_child;
} nx_json;

typedef int (*nx_json_unicode_encoder)(unsigned int codepoint, char* p, char** endp);

extern nx_json_unicode_encoder nx_json_unicode_to_utf8;

const nx_json* nx_json_parse(char* text, nx_json_unicode_encoder encoder);
const nx_json* nx_json_parse_utf8(char* text);
void nx_json_free(const nx_json* js);
const nx_json* nx_json_get(const nx_json* json, const char* key); // get object's property by key
const nx_json* nx_json_item(const nx_json* json, int idx); // get array element by index


#ifdef  __cplusplus
}
#endif

#endif  /* NXJSON_H */

#endif

// Json marker
#define SRS_JSON_Boolean                   0x01
#define SRS_JSON_String                    0x02
#define SRS_JSON_Object                    0x03
#define SRS_JSON_Integer                   0x04
#define SRS_JSON_Number                    0x05
#define SRS_JSON_Null                      0x06
#define SRS_JSON_Array                     0x07

class SrsJsonString : public SrsJsonAny
{
public:
    std::string value;

    SrsJsonString(const char* _value) 
    {
        marker = SRS_JSON_String;
        if (_value) {
            value = _value;
        }
    }
    virtual ~SrsJsonString() 
    {
    }
};

class SrsJsonBoolean : public SrsJsonAny
{
public:
    bool value;

    SrsJsonBoolean(bool _value) 
    {
        marker = SRS_JSON_Boolean;
        value = _value;
    }
    virtual ~SrsJsonBoolean() 
    {
    }
};

class SrsJsonInteger : public SrsJsonAny
{
public:
    int64_t value;

    SrsJsonInteger(int64_t _value) 
    {
        marker = SRS_JSON_Integer;
        value = _value;
    }
    virtual ~SrsJsonInteger() 
    {
    }
};

class SrsJsonNumber : public SrsJsonAny
{
public:
    double value;

    SrsJsonNumber(double _value) 
    {
        marker = SRS_JSON_Number;
        value = _value;
    }
    virtual ~SrsJsonNumber() 
    {
    }
};

class SrsJsonNull : public SrsJsonAny
{
public:
    SrsJsonNull() {
        marker = SRS_JSON_Null;
    }
    virtual ~SrsJsonNull() {
    }
};

SrsJsonAny::SrsJsonAny()
{
    marker = 0;
}

SrsJsonAny::~SrsJsonAny()
{
}

bool SrsJsonAny::is_string()
{
    return marker == SRS_JSON_String;
}

bool SrsJsonAny::is_boolean()
{
    return marker == SRS_JSON_Boolean;
}

bool SrsJsonAny::is_number()
{
    return marker == SRS_JSON_Number;
}

bool SrsJsonAny::is_integer()
{
    return marker == SRS_JSON_Integer;
}

bool SrsJsonAny::is_object()
{
    return marker == SRS_JSON_Object;
}

bool SrsJsonAny::is_array()
{
    return marker == SRS_JSON_Array;
}

bool SrsJsonAny::is_null()
{
    return marker == SRS_JSON_Null;
}

string SrsJsonAny::to_str()
{
    SrsJsonString* p = dynamic_cast<SrsJsonString*>(this);
    srs_assert(p != NULL);
    return p->value;
}

bool SrsJsonAny::to_boolean()
{
    SrsJsonBoolean* p = dynamic_cast<SrsJsonBoolean*>(this);
    srs_assert(p != NULL);
    return p->value;
}

int64_t SrsJsonAny::to_integer()
{
    SrsJsonInteger* p = dynamic_cast<SrsJsonInteger*>(this);
    srs_assert(p != NULL);
    return p->value;
}

double SrsJsonAny::to_number()
{
    SrsJsonNumber* p = dynamic_cast<SrsJsonNumber*>(this);
    srs_assert(p != NULL);
    return p->value;
}

SrsJsonObject* SrsJsonAny::to_object()
{
    SrsJsonObject* p = dynamic_cast<SrsJsonObject*>(this);
    srs_assert(p != NULL);
    return p;
}

SrsJsonArray* SrsJsonAny::to_array()
{
    SrsJsonArray* p = dynamic_cast<SrsJsonArray*>(this);
    srs_assert(p != NULL);
    return p;
}

string SrsJsonAny::to_json()
{
    switch (marker) {
        case SRS_JSON_String: {
            return "\"" + to_str() + "\"";
        }
        case SRS_JSON_Boolean: {
            return to_boolean()? "true":"false";
        }
        case SRS_JSON_Integer: {
            // len(max int64_t) is 20, plus one "+-."
            char tmp[22];
            snprintf(tmp, 22, "%"PRId64, to_integer());
            return tmp;
        }
        case SRS_JSON_Number: {
            // len(max int64_t) is 20, plus one "+-."
            char tmp[22];
            snprintf(tmp, 22, "%.6f", to_number());
            return tmp;
        }
        case SRS_JSON_Null: {
            return "null";
        }
        case SRS_JSON_Object: {
            SrsJsonObject* obj = to_object();
            return obj->to_json();
        }
        case SRS_JSON_Array: {
            SrsJsonArray* arr = to_array();
            return arr->to_json();
        }
        default: {
            break;
        }
    }
    
    return "null";
}

SrsJsonAny* SrsJsonAny::str(const char* value)
{
    return new SrsJsonString(value);
}

SrsJsonAny* SrsJsonAny::boolean(bool value)
{
    return new SrsJsonBoolean(value);
}

SrsJsonAny* SrsJsonAny::ingeter(int64_t value)
{
    return new SrsJsonInteger(value);
}

SrsJsonAny* SrsJsonAny::number(double value)
{
    return new SrsJsonNumber(value);
}

SrsJsonAny* SrsJsonAny::null()
{
    return new SrsJsonNull();
}

SrsJsonObject* SrsJsonAny::object()
{
    return new SrsJsonObject();
}

SrsJsonArray* SrsJsonAny::array()
{
    return new SrsJsonArray();
}

#ifdef SRS_JSON_USE_NXJSON
SrsJsonAny* srs_json_parse_tree_nx_json(const nx_json* node)
{
    if (!node) {
        return NULL;
    }
    
    switch (node->type) {
        case NX_JSON_NULL:
            return SrsJsonAny::null();
        case NX_JSON_STRING:
            return SrsJsonAny::str(node->text_value);
        case NX_JSON_INTEGER:
            return SrsJsonAny::ingeter(node->int_value);
        case NX_JSON_DOUBLE:
            return SrsJsonAny::number(node->dbl_value);
        case NX_JSON_BOOL:
            return SrsJsonAny::boolean(node->int_value != 0);
        case NX_JSON_OBJECT: {
            SrsJsonObject* obj = SrsJsonAny::object();
            for (nx_json* p = node->child; p != NULL; p = p->next) {
                SrsJsonAny* value = srs_json_parse_tree_nx_json(p);
                if (value) {
                    obj->set(p->key, value);
                }
            }
            return obj;
        }
        case NX_JSON_ARRAY: {
            SrsJsonArray* arr = SrsJsonAny::array();
            for (nx_json* p = node->child; p != NULL; p = p->next) {
                SrsJsonAny* value = srs_json_parse_tree_nx_json(p);
                if (value) {
                    arr->add(value);
                }
            }
            return arr;
        }
    }
    
    return NULL;
}

SrsJsonAny* SrsJsonAny::loads(char* str)
{
    if (!str) {
        return NULL;
    }
    
    if (strlen(str) == 0) {
        return NULL;
    }
    
    const nx_json* o = nx_json_parse(str, 0);
    
    SrsJsonAny* json = srs_json_parse_tree_nx_json(o);
    
    if (o) {
        nx_json_free(o);
    }
    
    return json;
}
#endif

SrsJsonObject::SrsJsonObject()
{
    marker = SRS_JSON_Object;
}

SrsJsonObject::~SrsJsonObject()
{
    std::vector<SrsJsonObjectPropertyType>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonObjectPropertyType item = *it;
        SrsJsonAny* obj = item.second;
        srs_freep(obj);
    }
    properties.clear();
}

int SrsJsonObject::count()
{
    return (int)properties.size();
}

string SrsJsonObject::key_at(int index)
{
    srs_assert(index < count());
    SrsJsonObjectPropertyType& elem = properties[index];
    return elem.first;
}

SrsJsonAny* SrsJsonObject::value_at(int index)
{
    srs_assert(index < count());
    SrsJsonObjectPropertyType& elem = properties[index];
    return elem.second;
}

string SrsJsonObject::to_json()
{
    stringstream ss;
    
    ss << SRS_JOBJECT_START;
    
    for (int i = 0; i < (int)properties.size(); i++) {
        std::string name = this->key_at(i);
        SrsJsonAny* any = this->value_at(i);
        
        ss << SRS_JFIELD_NAME(name) << any->to_json();
        if (i < (int)properties.size() - 1) {
            ss << SRS_JFIELD_CONT;
        }
    }
    
    ss << SRS_JOBJECT_END;
    
    return ss.str();
}

void SrsJsonObject::set(string key, SrsJsonAny* value)
{
    if (!value) {
        srs_warn("add a NULL propertity %s", key.c_str());
        return;
    }
    
    std::vector<SrsJsonObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonObjectPropertyType& elem = *it;
        std::string name = elem.first;
        SrsJsonAny* any = elem.second;
        
        if (key == name) {
            srs_freep(any);
            properties.erase(it);
            break;
        }
    }
    
    properties.push_back(std::make_pair(key, value));
}

SrsJsonAny* SrsJsonObject::get_property(string name)
{
    std::vector<SrsJsonObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonObjectPropertyType& elem = *it;
        std::string key = elem.first;
        SrsJsonAny* any = elem.second;
        if (key == name) {
            return any;
        }
    }
    
    return NULL;
}

SrsJsonAny* SrsJsonObject::ensure_property_string(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_string()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_integer(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_integer()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_boolean(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_boolean()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_object(string name)
{
    SrsJsonAny* prop = get_property(name);

    if (!prop) {
        return NULL;
    }

    if (!prop->is_object()) {
        return NULL;
    }

    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_array(string name)
{
    SrsJsonAny* prop = get_property(name);

    if (!prop) {
        return NULL;
    }

    if (!prop->is_array()) {
        return NULL;
    }

    return prop;
}

SrsJsonArray::SrsJsonArray()
{
    marker = SRS_JSON_Array;
}

SrsJsonArray::~SrsJsonArray()
{
    std::vector<SrsJsonAny*>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonAny* item = *it;
        srs_freep(item);
    }
    properties.clear();
}

int SrsJsonArray::count()
{
    return (int)properties.size();
}

SrsJsonAny* SrsJsonArray::at(int index)
{
    srs_assert(index < count());
    SrsJsonAny* elem = properties[index];
    return elem;
}

void SrsJsonArray::add(SrsJsonAny* value)
{
    properties.push_back(value);
}

void SrsJsonArray::append(SrsJsonAny* value)
{
    add(value);
}

string SrsJsonArray::to_json()
{
    stringstream ss;
    
    ss << SRS_JARRAY_START;
    
    for (int i = 0; i < (int)properties.size(); i++) {
        SrsJsonAny* any = properties[i];
        
        ss << any->to_json();
        
        if (i < (int)properties.size() - 1) {
            ss << SRS_JFIELD_CONT;
        }
    }
    
    ss << SRS_JARRAY_END;
    
    return ss.str();
}

#ifdef SRS_JSON_USE_NXJSON

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
/*
 * Copyright (c) 2013 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXJSON.
 *
 * NXJSON is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXJSON is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXJSON. If not, see <http://www.gnu.org/licenses/>.
 */

// this file can be #included in your code
#ifndef NXJSON_C
#define NXJSON_C

#ifdef  __cplusplus
extern "C" {
#endif


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

//#include "nxjson.h"

// redefine NX_JSON_CALLOC & NX_JSON_FREE to use custom allocator
#ifndef NX_JSON_CALLOC
#define NX_JSON_CALLOC() calloc(1, sizeof(nx_json))
#define NX_JSON_FREE(json) free((void*)(json))
#endif

// redefine NX_JSON_REPORT_ERROR to use custom error reporting
#ifndef NX_JSON_REPORT_ERROR
#define NX_JSON_REPORT_ERROR(msg, p) srs_warn("NXJSON PARSE ERROR (%d): " msg " at %s", __LINE__, p)
#endif

#define IS_WHITESPACE(c) ((unsigned char)(c)<=(unsigned char)' ')

static const nx_json dummy={ NX_JSON_NULL };

static nx_json* create_json(nx_json_type type, const char* key, nx_json* parent) {
  nx_json* js=(nx_json*)NX_JSON_CALLOC();
  memset(js, 0, sizeof(nx_json));
  assert(js);
  js->type=type;
  js->key=key;
  if (!parent->last_child) {
    parent->child=parent->last_child=js;
  }
  else {
    parent->last_child->next=js;
    parent->last_child=js;
  }
  parent->length++;
  return js;
}

void nx_json_free(const nx_json* js) {
  nx_json* p=js->child;
  nx_json* p1;
  while (p) {
    p1=p->next;
    nx_json_free(p);
    p=p1;
  }
  NX_JSON_FREE(js);
}

static int unicode_to_utf8(unsigned int codepoint, char* p, char** endp) {
  // code from http://stackoverflow.com/a/4609989/697313
  if (codepoint<0x80) *p++=codepoint;
  else if (codepoint<0x800) *p++=192+codepoint/64, *p++=128+codepoint%64;
  else if (codepoint-0xd800u<0x800) return 0; // surrogate must have been treated earlier
  else if (codepoint<0x10000) *p++=224+codepoint/4096, *p++=128+codepoint/64%64, *p++=128+codepoint%64;
  else if (codepoint<0x110000) *p++=240+codepoint/262144, *p++=128+codepoint/4096%64, *p++=128+codepoint/64%64, *p++=128+codepoint%64;
  else return 0; // error
  *endp=p;
  return 1;
}

nx_json_unicode_encoder nx_json_unicode_to_utf8=unicode_to_utf8;

static inline int hex_val(char c) {
  if (c>='0' && c<='9') return c-'0';
  if (c>='a' && c<='f') return c-'a'+10;
  if (c>='A' && c<='F') return c-'A'+10;
  return -1;
}

static char* unescape_string(char* s, char** end, nx_json_unicode_encoder encoder) {
  char* p=s;
  char* d=s;
  char c;
  while ((c=*p++)) {
    if (c=='"') {
      *d='\0';
      *end=p;
      return s;
    }
    else if (c=='\\') {
      switch (*p) {
        case '\\':
        case '/':
        case '"':
          *d++=*p++;
          break;
        case 'b':
          *d++='\b'; p++;
          break;
        case 'f':
          *d++='\f'; p++;
          break;
        case 'n':
          *d++='\n'; p++;
          break;
        case 'r':
          *d++='\r'; p++;
          break;
        case 't':
          *d++='\t'; p++;
          break;
        case 'u': { // unicode
          if (!encoder) {
            // leave untouched
            *d++=c;
            break;
          }
          char* ps=p-1;
          int h1, h2, h3, h4;
          if ((h1=hex_val(p[1]))<0 || (h2=hex_val(p[2]))<0 || (h3=hex_val(p[3]))<0 || (h4=hex_val(p[4]))<0) {
            NX_JSON_REPORT_ERROR("invalid unicode escape", p-1);
            return 0;
          }
          unsigned int codepoint=h1<<12|h2<<8|h3<<4|h4;
          if ((codepoint & 0xfc00)==0xd800) { // high surrogate; need one more unicode to succeed
            p+=6;
            if (p[-1]!='\\' || *p!='u' || (h1=hex_val(p[1]))<0 || (h2=hex_val(p[2]))<0 || (h3=hex_val(p[3]))<0 || (h4=hex_val(p[4]))<0) {
              NX_JSON_REPORT_ERROR("invalid unicode surrogate", ps);
              return 0;
            }
            unsigned int codepoint2=h1<<12|h2<<8|h3<<4|h4;
            if ((codepoint2 & 0xfc00)!=0xdc00) {
              NX_JSON_REPORT_ERROR("invalid unicode surrogate", ps);
              return 0;
            }
            codepoint=0x10000+((codepoint-0xd800)<<10)+(codepoint2-0xdc00);
          }
          if (!encoder(codepoint, d, &d)) {
            NX_JSON_REPORT_ERROR("invalid codepoint", ps);
            return 0;
          }
          p+=5;
          break;
      }
        default: {
          // leave untouched
          *d++=c;
          break;
        }
      }
    }
    else {
      *d++=c;
    }
  }
  NX_JSON_REPORT_ERROR("no closing quote for string", s);
  return 0;
}

static char* skip_block_comment(char* p) {
  // assume p[-2]=='/' && p[-1]=='*'
  char* ps=p-2;
  if (!*p) {
    NX_JSON_REPORT_ERROR("endless comment", ps);
    return 0;
  }
  REPEAT:
  p=strchr(p+1, '/');
  if (!p) {
    NX_JSON_REPORT_ERROR("endless comment", ps);
    return 0;
  }
  if (p[-1]!='*') goto REPEAT;
  return p+1;
}

static char* parse_key(const char** key, char* p, nx_json_unicode_encoder encoder) {
  // on '}' return with *p=='}'
  char c;
  while ((c=*p++)) {
    if (c=='"') {
      *key=unescape_string(p, &p, encoder);
      if (!*key) return 0; // propagate error
      while (*p && IS_WHITESPACE(*p)) p++;
      if (*p==':') return p+1;
      NX_JSON_REPORT_ERROR("unexpected chars", p);
      return 0;
    }
    else if (IS_WHITESPACE(c) || c==',') {
      // continue
    }
    else if (c=='}') {
      return p-1;
    }
    else if (c=='/') {
      if (*p=='/') { // line comment
        char* ps=p-1;
        p=strchr(p+1, '\n');
        if (!p) {
          NX_JSON_REPORT_ERROR("endless comment", ps);
          return 0; // error
        }
        p++;
      }
      else if (*p=='*') { // block comment
        p=skip_block_comment(p+1);
        if (!p) return 0;
      }
      else {
        NX_JSON_REPORT_ERROR("unexpected chars", p-1);
        return 0; // error
      }
    }
    else {
      NX_JSON_REPORT_ERROR("unexpected chars", p-1);
      return 0; // error
    }
  }
  NX_JSON_REPORT_ERROR("unexpected chars", p-1);
  return 0; // error
}

static char* parse_value(nx_json* parent, const char* key, char* p, nx_json_unicode_encoder encoder) {
  nx_json* js;
  while (1) {
    switch (*p) {
      case '\0':
        NX_JSON_REPORT_ERROR("unexpected end of text", p);
        return 0; // error
      case ' ': case '\t': case '\n': case '\r':
      case ',':
        // skip
        p++;
        break;
      case '{':
        js=create_json(NX_JSON_OBJECT, key, parent);
        p++;
        while (1) {
          const char* new_key;
          p=parse_key(&new_key, p, encoder);
          if (!p) return 0; // error
          if (*p=='}') return p+1; // end of object
          p=parse_value(js, new_key, p, encoder);
          if (!p) return 0; // error
        }
      case '[':
        js=create_json(NX_JSON_ARRAY, key, parent);
        p++;
        while (1) {
          p=parse_value(js, 0, p, encoder);
          if (!p) return 0; // error
          if (*p==']') return p+1; // end of array
        }
      case ']':
        return p;
      case '"':
        p++;
        js=create_json(NX_JSON_STRING, key, parent);
        js->text_value=unescape_string(p, &p, encoder);
        if (!js->text_value) return 0; // propagate error
        return p;
      case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        {
          js=create_json(NX_JSON_INTEGER, key, parent);
          char* pe;
          js->int_value=strtol(p, &pe, 0);
          if (pe==p) {
            NX_JSON_REPORT_ERROR("invalid number", p);
            return 0; // error
          }
          if (*pe=='.' || *pe=='e' || *pe=='E') { // double value
            js->type=NX_JSON_DOUBLE;
            js->dbl_value=strtod(p, &pe);
            if (pe==p) {
              NX_JSON_REPORT_ERROR("invalid number", p);
              return 0; // error
            }
          }
          else {
            js->dbl_value=js->int_value;
          }
          return pe;
        }
      case 't':
        if (!strncmp(p, "true", 4)) {
          js=create_json(NX_JSON_BOOL, key, parent);
          js->int_value=1;
          return p+4;
        }
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
      case 'f':
        if (!strncmp(p, "false", 5)) {
          js=create_json(NX_JSON_BOOL, key, parent);
          js->int_value=0;
          return p+5;
        }
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
      case 'n':
        if (!strncmp(p, "null", 4)) {
          create_json(NX_JSON_NULL, key, parent);
          return p+4;
        }
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
      case '/': // comment
        if (p[1]=='/') { // line comment
          char* ps=p;
          p=strchr(p+2, '\n');
          if (!p) {
            NX_JSON_REPORT_ERROR("endless comment", ps);
            return 0; // error
          }
          p++;
        }
        else if (p[1]=='*') { // block comment
          p=skip_block_comment(p+2);
          if (!p) return 0;
        }
        else {
          NX_JSON_REPORT_ERROR("unexpected chars", p);
          return 0; // error
        }
        break;
      default:
        NX_JSON_REPORT_ERROR("unexpected chars", p);
        return 0; // error
    }
  }
}

const nx_json* nx_json_parse_utf8(char* text) {
  return nx_json_parse(text, unicode_to_utf8);
}

const nx_json* nx_json_parse(char* text, nx_json_unicode_encoder encoder) {
  nx_json js;
  memset(&js, 0, sizeof(nx_json));
  if (!parse_value(&js, 0, text, encoder)) {
    if (js.child) nx_json_free(js.child);
    return 0;
  }
  return js.child;
}

const nx_json* nx_json_get(const nx_json* json, const char* key) {
  if (!json || !key) return &dummy; // never return null
  nx_json* js;
  for (js=json->child; js; js=js->next) {
    if (js->key && !strcmp(js->key, key)) return js;
  }
  return &dummy; // never return null
}

const nx_json* nx_json_item(const nx_json* json, int idx) {
  if (!json) return &dummy; // never return null
  nx_json* js;
  for (js=json->child; js; js=js->next) {
    if (!idx--) return js;
  }
  return &dummy; // never return null
}


#ifdef  __cplusplus
}
#endif

#endif  /* NXJSON_C */

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////

#endif


