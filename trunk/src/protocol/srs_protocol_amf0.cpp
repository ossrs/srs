/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_protocol_amf0.hpp>

#include <utility>
#include <vector>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_json.hpp>

using namespace _srs_internal;

// AMF0 marker
#define RTMP_AMF0_Number                     0x00
#define RTMP_AMF0_Boolean                     0x01
#define RTMP_AMF0_String                     0x02
#define RTMP_AMF0_Object                     0x03
#define RTMP_AMF0_MovieClip                 0x04 // reserved, not supported
#define RTMP_AMF0_Null                         0x05
#define RTMP_AMF0_Undefined                 0x06
#define RTMP_AMF0_Reference                 0x07
#define RTMP_AMF0_EcmaArray                 0x08
#define RTMP_AMF0_ObjectEnd                 0x09
#define RTMP_AMF0_StrictArray                 0x0A
#define RTMP_AMF0_Date                         0x0B
#define RTMP_AMF0_LongString                 0x0C
#define RTMP_AMF0_UnSupported                 0x0D
#define RTMP_AMF0_RecordSet                 0x0E // reserved, not supported
#define RTMP_AMF0_XmlDocument                 0x0F
#define RTMP_AMF0_TypedObject                 0x10
// AVM+ object is the AMF3 object.
#define RTMP_AMF0_AVMplusObject             0x11
// origin array whos data takes the same form as LengthValueBytes
#define RTMP_AMF0_OriginStrictArray         0x20

// User defined
#define RTMP_AMF0_Invalid                     0x3F

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

bool SrsAmf0Any::is_strict_array()
{
    return marker == RTMP_AMF0_StrictArray;
}

bool SrsAmf0Any::is_date()
{
    return marker == RTMP_AMF0_Date;
}

bool SrsAmf0Any::is_complex_object()
{
    return is_object() || is_object_eof() || is_ecma_array() || is_strict_array();
}

string SrsAmf0Any::to_str()
{
    SrsAmf0String* p = dynamic_cast<SrsAmf0String*>(this);
    srs_assert(p != NULL);
    return p->value;
}

const char* SrsAmf0Any::to_str_raw()
{
    SrsAmf0String* p = dynamic_cast<SrsAmf0String*>(this);
    srs_assert(p != NULL);
    return p->value.data();
}

bool SrsAmf0Any::to_boolean()
{
    SrsAmf0Boolean* p = dynamic_cast<SrsAmf0Boolean*>(this);
    srs_assert(p != NULL);
    return p->value;
}

double SrsAmf0Any::to_number()
{
    SrsAmf0Number* p = dynamic_cast<SrsAmf0Number*>(this);
    srs_assert(p != NULL);
    return p->value;
}

int64_t SrsAmf0Any::to_date()
{
    SrsAmf0Date* p = dynamic_cast<SrsAmf0Date*>(this);
    srs_assert(p != NULL);
    return p->date();
}

int16_t SrsAmf0Any::to_date_time_zone()
{
    SrsAmf0Date* p = dynamic_cast<SrsAmf0Date*>(this);
    srs_assert(p != NULL);
    return p->time_zone();
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

SrsAmf0StrictArray* SrsAmf0Any::to_strict_array()
{
    SrsAmf0StrictArray* p = dynamic_cast<SrsAmf0StrictArray*>(this);
    srs_assert(p != NULL);
    return p;
}

void SrsAmf0Any::set_number(double value)
{
    SrsAmf0Number* p = dynamic_cast<SrsAmf0Number*>(this);
    srs_assert(p != NULL);
    p->value = value;
}

bool SrsAmf0Any::is_object_eof()
{
    return marker == RTMP_AMF0_ObjectEnd;
}

void srs_fill_level_spaces(stringstream& ss, int level)
{
    for (int i = 0; i < level; i++) {
        ss << "    ";
    }
}
void srs_amf0_do_print(SrsAmf0Any* any, stringstream& ss, int level)
{
    std::ios_base::fmtflags oflags = ss.flags();
    
    if (any->is_boolean()) {
        ss << "Boolean " << (any->to_boolean()? "true":"false") << endl;
    } else if (any->is_number()) {
        ss << "Number " << std::fixed << any->to_number() << endl;
    } else if (any->is_string()) {
        ss << "String " << any->to_str() << endl;
    } else if (any->is_date()) {
        ss << "Date " << std::hex << any->to_date()
        << "/" << std::hex << any->to_date_time_zone() << endl;
    } else if (any->is_null()) {
        ss << "Null" << endl;
    } else if (any->is_undefined()) {
        ss << "Undefined" << endl;
    } else if (any->is_ecma_array()) {
        SrsAmf0EcmaArray* obj = any->to_ecma_array();
        ss << "EcmaArray " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            srs_fill_level_spaces(ss, level + 1);
            ss << "Elem '" << obj->key_at(i) << "' ";
            if (obj->value_at(i)->is_complex_object()) {
                srs_amf0_do_print(obj->value_at(i), ss, level + 1);
            } else {
                srs_amf0_do_print(obj->value_at(i), ss, 0);
            }
        }
    } else if (any->is_strict_array()) {
        SrsAmf0StrictArray* obj = any->to_strict_array();
        ss << "StrictArray " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            srs_fill_level_spaces(ss, level + 1);
            ss << "Elem ";
            if (obj->at(i)->is_complex_object()) {
                srs_amf0_do_print(obj->at(i), ss, level + 1);
            } else {
                srs_amf0_do_print(obj->at(i), ss, 0);
            }
        }
    } else if (any->is_object()) {
        SrsAmf0Object* obj = any->to_object();
        ss << "Object " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            srs_fill_level_spaces(ss, level + 1);
            ss << "Property '" << obj->key_at(i) << "' ";
            if (obj->value_at(i)->is_complex_object()) {
                srs_amf0_do_print(obj->value_at(i), ss, level + 1);
            } else {
                srs_amf0_do_print(obj->value_at(i), ss, 0);
            }
        }
    } else {
        ss << "Unknown" << endl;
    }
    
    ss.flags(oflags);
}

char* SrsAmf0Any::human_print(char** pdata, int* psize)
{
    stringstream ss;
    
    ss.precision(1);
    
    srs_amf0_do_print(this, ss, 0);
    
    string str = ss.str();
    if (str.empty()) {
        return NULL;
    }
    
    char* data = new char[str.length() + 1];
    memcpy(data, str.data(), str.length());
    data[str.length()] = 0;
    
    if (pdata) {
        *pdata = data;
    }
    if (psize) {
        *psize = (int)str.length();
    }
    
    return data;
}

SrsJsonAny* SrsAmf0Any::to_json()
{
    switch (marker) {
        case RTMP_AMF0_String: {
            return SrsJsonAny::str(to_str().c_str());
        }
        case RTMP_AMF0_Boolean: {
            return SrsJsonAny::boolean(to_boolean());
        }
        case RTMP_AMF0_Number: {
            double dv = to_number();
            int64_t iv = (int64_t)dv;
            if (iv == dv) {
                return SrsJsonAny::integer(iv);
            } else {
                return SrsJsonAny::number(dv);
            }
        }
        case RTMP_AMF0_Null: {
            return SrsJsonAny::null();
        }
        case RTMP_AMF0_Undefined: {
            return SrsJsonAny::null();
        }
        case RTMP_AMF0_Object: {
            // amf0 object implements it.
            srs_assert(false);
        }
        case RTMP_AMF0_EcmaArray: {
            // amf0 ecma array implements it.
            srs_assert(false);
        }
        case RTMP_AMF0_StrictArray: {
            // amf0 strict array implements it.
            srs_assert(false);
        }
        case RTMP_AMF0_Date: {
            // TODO: FIXME: implements it.
            return SrsJsonAny::null();
        }
        default: {
            return SrsJsonAny::null();
        }
    }
    
}

SrsAmf0Any* SrsAmf0Any::str(const char* value)
{
    return new SrsAmf0String(value);
}

SrsAmf0Any* SrsAmf0Any::boolean(bool value)
{
    return new SrsAmf0Boolean(value);
}

SrsAmf0Any* SrsAmf0Any::number(double value)
{
    return new SrsAmf0Number(value);
}

SrsAmf0Any* SrsAmf0Any::null()
{
    return new SrsAmf0Null();
}

SrsAmf0Any* SrsAmf0Any::undefined()
{
    return new SrsAmf0Undefined();
}

SrsAmf0Object* SrsAmf0Any::object()
{
    return new SrsAmf0Object();
}

SrsAmf0Any* SrsAmf0Any::object_eof()
{
    return new SrsAmf0ObjectEOF();
}

SrsAmf0EcmaArray* SrsAmf0Any::ecma_array()
{
    return new SrsAmf0EcmaArray();
}

SrsAmf0StrictArray* SrsAmf0Any::strict_array()
{
    return new SrsAmf0StrictArray();
}

SrsAmf0Any* SrsAmf0Any::date(int64_t value)
{
    return new SrsAmf0Date(value);
}

srs_error_t SrsAmf0Any::discovery(SrsBuffer* stream, SrsAmf0Any** ppvalue)
{
    srs_error_t err = srs_success;
    
    // detect the object-eof specially
    if (srs_amf0_is_object_eof(stream)) {
        *ppvalue = new SrsAmf0ObjectEOF();
        return err;
    }
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "marker requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    
    // backward the 1byte marker.
    stream->skip(-1);
    
    switch (marker) {
        case RTMP_AMF0_String: {
            *ppvalue = SrsAmf0Any::str();
            return err;
        }
        case RTMP_AMF0_Boolean: {
            *ppvalue = SrsAmf0Any::boolean();
            return err;
        }
        case RTMP_AMF0_Number: {
            *ppvalue = SrsAmf0Any::number();
            return err;
        }
        case RTMP_AMF0_Null: {
            *ppvalue = SrsAmf0Any::null();
            return err;
        }
        case RTMP_AMF0_Undefined: {
            *ppvalue = SrsAmf0Any::undefined();
            return err;
        }
        case RTMP_AMF0_Object: {
            *ppvalue = SrsAmf0Any::object();
            return err;
        }
        case RTMP_AMF0_EcmaArray: {
            *ppvalue = SrsAmf0Any::ecma_array();
            return err;
        }
        case RTMP_AMF0_StrictArray: {
            *ppvalue = SrsAmf0Any::strict_array();
            return err;
        }
        case RTMP_AMF0_Date: {
            *ppvalue = SrsAmf0Any::date();
            return err;
        }
        case RTMP_AMF0_Invalid:
        default: {
            return srs_error_new(ERROR_RTMP_AMF0_INVALID, "invalid amf0 message, marker=%#x", marker);
        }
    }
}

SrsUnSortedHashtable::SrsUnSortedHashtable()
{
}

SrsUnSortedHashtable::~SrsUnSortedHashtable()
{
    clear();
}

int SrsUnSortedHashtable::count()
{
    return (int)properties.size();
}

void SrsUnSortedHashtable::clear()
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        SrsAmf0Any* any = elem.second;
        srs_freep(any);
    }
    properties.clear();
}

string SrsUnSortedHashtable::key_at(int index)
{
    srs_assert(index < count());
    SrsAmf0ObjectPropertyType& elem = properties[index];
    return elem.first;
}

const char* SrsUnSortedHashtable::key_raw_at(int index)
{
    srs_assert(index < count());
    SrsAmf0ObjectPropertyType& elem = properties[index];
    return elem.first.data();
}

SrsAmf0Any* SrsUnSortedHashtable::value_at(int index)
{
    srs_assert(index < count());
    SrsAmf0ObjectPropertyType& elem = properties[index];
    return elem.second;
}

void SrsUnSortedHashtable::set(string key, SrsAmf0Any* value)
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        std::string name = elem.first;
        SrsAmf0Any* any = elem.second;
        
        if (key == name) {
            srs_freep(any);
            properties.erase(it);
            break;
        }
    }
    
    if (value) {
        properties.push_back(std::make_pair(key, value));
    }
}

SrsAmf0Any* SrsUnSortedHashtable::get_property(string name)
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        std::string key = elem.first;
        SrsAmf0Any* any = elem.second;
        if (key == name) {
            return any;
        }
    }
    
    return NULL;
}

SrsAmf0Any* SrsUnSortedHashtable::ensure_property_string(string name)
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

SrsAmf0Any* SrsUnSortedHashtable::ensure_property_number(string name)
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

void SrsUnSortedHashtable::remove(string name)
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end();) {
        std::string key = it->first;
        SrsAmf0Any* any = it->second;
        
        if (key == name) {
            srs_freep(any);
            
            it = properties.erase(it);
        } else {
            ++it;
        }
    }
}

void SrsUnSortedHashtable::copy(SrsUnSortedHashtable* src)
{
    std::vector<SrsAmf0ObjectPropertyType>::iterator it;
    for (it = src->properties.begin(); it != src->properties.end(); ++it) {
        SrsAmf0ObjectPropertyType& elem = *it;
        std::string key = elem.first;
        SrsAmf0Any* any = elem.second;
        set(key, any->copy());
    }
}

SrsAmf0ObjectEOF::SrsAmf0ObjectEOF()
{
    marker = RTMP_AMF0_ObjectEnd;
}

SrsAmf0ObjectEOF::~SrsAmf0ObjectEOF()
{
}

int SrsAmf0ObjectEOF::total_size()
{
    return SrsAmf0Size::object_eof();
}

srs_error_t SrsAmf0ObjectEOF::read(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // value
    if (!stream->require(2)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "EOF requires 2 only %d bytes", stream->left());
    }
    int16_t temp = stream->read_2bytes();
    if (temp != 0x00) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "EOF invalid marker=%#x", temp);
    }
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "EOF requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_ObjectEnd) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "EOF invalid marker=%#x", marker);
    }
    
    return err;
}

srs_error_t SrsAmf0ObjectEOF::write(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // value
    if (!stream->require(2)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "EOF requires 2 only %d bytes", stream->left());
    }
    stream->write_2bytes(0x00);
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "EOF requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_ObjectEnd);
    
    return err;
}

SrsAmf0Any* SrsAmf0ObjectEOF::copy()
{
    return new SrsAmf0ObjectEOF();
}

SrsAmf0Object::SrsAmf0Object()
{
    properties = new SrsUnSortedHashtable();
    eof = new SrsAmf0ObjectEOF();
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

srs_error_t SrsAmf0Object::read(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "object requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Object) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "object invalid marker=%#x", marker);
    }
    
    // value
    while (!stream->empty()) {
        // detect whether is eof.
        if (srs_amf0_is_object_eof(stream)) {
            SrsAmf0ObjectEOF pbj_eof;
            if ((err = pbj_eof.read(stream)) != srs_success) {
                return srs_error_wrap(err, "read EOF");
            }
            break;
        }
        
        // property-name: utf8 string
        std::string property_name;
        if ((err = srs_amf0_read_utf8(stream, property_name)) != srs_success) {
            return srs_error_wrap(err, "read property name");
        }
        // property-value: any
        SrsAmf0Any* property_value = NULL;
        if ((err = srs_amf0_read_any(stream, &property_value)) != srs_success) {
            srs_freep(property_value);
            return srs_error_wrap(err, "read property value, name=%s", property_name.c_str());
        }
        
        // add property
        this->set(property_name, property_value);
    }
    
    return err;
}

srs_error_t SrsAmf0Object::write(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "object requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_Object);
    
    // value
    for (int i = 0; i < properties->count(); i++) {
        std::string name = this->key_at(i);
        SrsAmf0Any* any = this->value_at(i);
        
        if ((err = srs_amf0_write_utf8(stream, name)) != srs_success) {
            return srs_error_wrap(err, "write property name=%s", name.c_str());
        }
        
        if ((err = srs_amf0_write_any(stream, any)) != srs_success) {
            return srs_error_wrap(err, "write property value, name=%s", name.c_str());
        }
    }
    
    if ((err = eof->write(stream)) != srs_success) {
        return srs_error_wrap(err, "write EOF");
    }
    
    return err;
}

SrsAmf0Any* SrsAmf0Object::copy()
{
    SrsAmf0Object* copy = new SrsAmf0Object();
    copy->properties->copy(properties);
    return copy;
}

SrsJsonAny* SrsAmf0Object::to_json()
{
    SrsJsonObject* obj = SrsJsonAny::object();
    
    for (int i = 0; i < properties->count(); i++) {
        std::string name = this->key_at(i);
        SrsAmf0Any* any = this->value_at(i);
        
        obj->set(name, any->to_json());
    }
    
    return obj;
}

void SrsAmf0Object::clear()
{
    properties->clear();
}

int SrsAmf0Object::count()
{
    return properties->count();
}

string SrsAmf0Object::key_at(int index)
{
    return properties->key_at(index);
}

const char* SrsAmf0Object::key_raw_at(int index)
{
    return properties->key_raw_at(index);
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

void SrsAmf0Object::remove(string name)
{
    properties->remove(name);
}

SrsAmf0EcmaArray::SrsAmf0EcmaArray()
{
    _count = 0;
    properties = new SrsUnSortedHashtable();
    eof = new SrsAmf0ObjectEOF();
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

srs_error_t SrsAmf0EcmaArray::read(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_EcmaArray) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "EcmaArray invalid marker=%#x", marker);
    }
    
    // count
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 4 only %d bytes", stream->left());
    }
    
    int32_t count = stream->read_4bytes();
    
    // value
    this->_count = count;
    
    while (!stream->empty()) {
        // detect whether is eof.
        if (srs_amf0_is_object_eof(stream)) {
            SrsAmf0ObjectEOF pbj_eof;
            if ((err = pbj_eof.read(stream)) != srs_success) {
                return srs_error_wrap(err, "read EOF");
            }
            break;
        }
        
        // property-name: utf8 string
        std::string property_name;
        if ((err =srs_amf0_read_utf8(stream, property_name)) != srs_success) {
            return srs_error_wrap(err, "read property name");
        }
        // property-value: any
        SrsAmf0Any* property_value = NULL;
        if ((err = srs_amf0_read_any(stream, &property_value)) != srs_success) {
            return srs_error_wrap(err, "read property value, name=%s", property_name.c_str());
        }
        
        // add property
        this->set(property_name, property_value);
    }
    
    return err;
}

srs_error_t SrsAmf0EcmaArray::write(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_EcmaArray);
    
    // count
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 4 only %d bytes", stream->left());
    }
    
    stream->write_4bytes(this->_count);
    
    // value
    for (int i = 0; i < properties->count(); i++) {
        std::string name = this->key_at(i);
        SrsAmf0Any* any = this->value_at(i);
        
        if ((err = srs_amf0_write_utf8(stream, name)) != srs_success) {
            return srs_error_wrap(err, "write property name=%s", name.c_str());
        }
        
        if ((err = srs_amf0_write_any(stream, any)) != srs_success) {
            return srs_error_wrap(err, "write property value, name=%s", name.c_str());
        }
    }
    
    if ((err = eof->write(stream)) != srs_success) {
        return srs_error_wrap(err, "write EOF");
    }
    
    return err;
}

SrsAmf0Any* SrsAmf0EcmaArray::copy()
{
    SrsAmf0EcmaArray* copy = new SrsAmf0EcmaArray();
    copy->properties->copy(properties);
    copy->_count = _count;
    return copy;
}

SrsJsonAny* SrsAmf0EcmaArray::to_json()
{
    SrsJsonObject* obj = SrsJsonAny::object();
    
    for (int i = 0; i < properties->count(); i++) {
        std::string name = this->key_at(i);
        SrsAmf0Any* any = this->value_at(i);
        
        obj->set(name, any->to_json());
    }
    
    return obj;
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

const char* SrsAmf0EcmaArray::key_raw_at(int index)
{
    return properties->key_raw_at(index);
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

SrsAmf0StrictArray::SrsAmf0StrictArray()
{
    marker = RTMP_AMF0_StrictArray;
    _count = 0;
}

SrsAmf0StrictArray::~SrsAmf0StrictArray()
{
    std::vector<SrsAmf0Any*>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0Any* any = *it;
        srs_freep(any);
    }
    properties.clear();
}

int SrsAmf0StrictArray::total_size()
{
    int size = 1 + 4;
    
    for (int i = 0; i < (int)properties.size(); i++){
        SrsAmf0Any* any = properties[i];
        size += any->total_size();
    }
    
    return size;
}

srs_error_t SrsAmf0StrictArray::read(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_StrictArray) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "StrictArray invalid marker=%#x", marker);
    }
    
    // count
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 4 only %d bytes", stream->left());
    }
    
    int32_t count = stream->read_4bytes();
    
    // value
    this->_count = count;
    
    for (int i = 0; i < count && !stream->empty(); i++) {
        // property-value: any
        SrsAmf0Any* elem = NULL;
        if ((err = srs_amf0_read_any(stream, &elem)) != srs_success) {
            return srs_error_wrap(err, "read property");
        }
        
        // add property
        properties.push_back(elem);
    }
    
    return err;
}

srs_error_t SrsAmf0StrictArray::write(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_StrictArray);
    
    // count
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 4 only %d bytes", stream->left());
    }
    
    stream->write_4bytes(this->_count);
    
    // value
    for (int i = 0; i < (int)properties.size(); i++) {
        SrsAmf0Any* any = properties[i];
        
        if ((err = srs_amf0_write_any(stream, any)) != srs_success) {
            return srs_error_wrap(err, "write property");
        }
    }
    
    return err;
}

SrsAmf0Any* SrsAmf0StrictArray::copy()
{
    SrsAmf0StrictArray* copy = new SrsAmf0StrictArray();
    
    std::vector<SrsAmf0Any*>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsAmf0Any* any = *it;
        copy->append(any->copy());
    }
    
    copy->_count = _count;
    return copy;
}

SrsJsonAny* SrsAmf0StrictArray::to_json()
{
    SrsJsonArray* arr = SrsJsonAny::array();
    
    for (int i = 0; i < (int)properties.size(); i++) {
        SrsAmf0Any* any = properties[i];
        
        arr->append(any->to_json());
    }
    
    return arr;
}

void SrsAmf0StrictArray::clear()
{
    properties.clear();
}

int SrsAmf0StrictArray::count()
{
    return (int)properties.size();
}

SrsAmf0Any* SrsAmf0StrictArray::at(int index)
{
    srs_assert(index < (int)properties.size());
    return properties.at(index);
}

void SrsAmf0StrictArray::append(SrsAmf0Any* any)
{
    properties.push_back(any);
    _count = (int32_t)properties.size();
}

int SrsAmf0Size::utf8(string value)
{
    return 2 + (int)value.length();
}

int SrsAmf0Size::str(string value)
{
    return 1 + SrsAmf0Size::utf8(value);
}

int SrsAmf0Size::number()
{
    return 1 + 8;
}

int SrsAmf0Size::date()
{
    return 1 + 8 + 2;
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

int SrsAmf0Size::strict_array(SrsAmf0StrictArray* arr)
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

int SrsAmf0String::total_size()
{
    return SrsAmf0Size::str(value);
}

srs_error_t SrsAmf0String::read(SrsBuffer* stream)
{
    return srs_amf0_read_string(stream, value);
}

srs_error_t SrsAmf0String::write(SrsBuffer* stream)
{
    return srs_amf0_write_string(stream, value);
}

SrsAmf0Any* SrsAmf0String::copy()
{
    SrsAmf0String* copy = new SrsAmf0String(value.c_str());
    return copy;
}

SrsAmf0Boolean::SrsAmf0Boolean(bool _value)
{
    marker = RTMP_AMF0_Boolean;
    value = _value;
}

SrsAmf0Boolean::~SrsAmf0Boolean()
{
}

int SrsAmf0Boolean::total_size()
{
    return SrsAmf0Size::boolean();
}

srs_error_t SrsAmf0Boolean::read(SrsBuffer* stream)
{
    return srs_amf0_read_boolean(stream, value);
}

srs_error_t SrsAmf0Boolean::write(SrsBuffer* stream)
{
    return srs_amf0_write_boolean(stream, value);
}

SrsAmf0Any* SrsAmf0Boolean::copy()
{
    SrsAmf0Boolean* copy = new SrsAmf0Boolean(value);
    return copy;
}

SrsAmf0Number::SrsAmf0Number(double _value)
{
    marker = RTMP_AMF0_Number;
    value = _value;
}

SrsAmf0Number::~SrsAmf0Number()
{
}

int SrsAmf0Number::total_size()
{
    return SrsAmf0Size::number();
}

srs_error_t SrsAmf0Number::read(SrsBuffer* stream)
{
    return srs_amf0_read_number(stream, value);
}

srs_error_t SrsAmf0Number::write(SrsBuffer* stream)
{
    return srs_amf0_write_number(stream, value);
}

SrsAmf0Any* SrsAmf0Number::copy()
{
    SrsAmf0Number* copy = new SrsAmf0Number(value);
    return copy;
}

SrsAmf0Date::SrsAmf0Date(int64_t value)
{
    marker = RTMP_AMF0_Date;
    _date_value = value;
    _time_zone = 0;
}

SrsAmf0Date::~SrsAmf0Date()
{
}

int SrsAmf0Date::total_size()
{
    return SrsAmf0Size::date();
}

srs_error_t SrsAmf0Date::read(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Date) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "Date invalid marker=%#x", marker);
    }
    
    // date value
    // An ActionScript Date is serialized as the number of milliseconds
    // elapsed since the epoch of midnight on 1st Jan 1970 in the UTC
    // time zone.
    if (!stream->require(8)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 8 only %d bytes", stream->left());
    }
    
    _date_value = stream->read_8bytes();
    
    // time zone
    // While the design of this type reserves room for time zone offset
    // information, it should not be filled in, nor used, as it is unconventional
    // to change time zones when serializing dates on a network. It is suggested
    // that the time zone be queried independently as needed.
    if (!stream->require(2)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 2 only %d bytes", stream->left());
    }
    
    _time_zone = stream->read_2bytes();
    
    return err;
}

srs_error_t SrsAmf0Date::write(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_Date);
    
    // date value
    if (!stream->require(8)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 8 only %d bytes", stream->left());
    }
    
    stream->write_8bytes(_date_value);
    
    // time zone
    if (!stream->require(2)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 2 only %d bytes", stream->left());
    }
    
    stream->write_2bytes(_time_zone);
    
    return err;
}

SrsAmf0Any* SrsAmf0Date::copy()
{
    SrsAmf0Date* copy = new SrsAmf0Date(0);
    
    copy->_date_value = _date_value;
    copy->_time_zone = _time_zone;
    
    return copy;
}

int64_t SrsAmf0Date::date()
{
    return _date_value;
}

int16_t SrsAmf0Date::time_zone()
{
    return _time_zone;
}

SrsAmf0Null::SrsAmf0Null()
{
    marker = RTMP_AMF0_Null;
}

SrsAmf0Null::~SrsAmf0Null()
{
}

int SrsAmf0Null::total_size()
{
    return SrsAmf0Size::null();
}

srs_error_t SrsAmf0Null::read(SrsBuffer* stream)
{
    return srs_amf0_read_null(stream);
}

srs_error_t SrsAmf0Null::write(SrsBuffer* stream)
{
    return srs_amf0_write_null(stream);
}

SrsAmf0Any* SrsAmf0Null::copy()
{
    SrsAmf0Null* copy = new SrsAmf0Null();
    return copy;
}

SrsAmf0Undefined::SrsAmf0Undefined()
{
    marker = RTMP_AMF0_Undefined;
}

SrsAmf0Undefined::~SrsAmf0Undefined()
{
}

int SrsAmf0Undefined::total_size()
{
    return SrsAmf0Size::undefined();
}

srs_error_t SrsAmf0Undefined::read(SrsBuffer* stream)
{
    return srs_amf0_read_undefined(stream);
}

srs_error_t SrsAmf0Undefined::write(SrsBuffer* stream)
{
    return srs_amf0_write_undefined(stream);
}

SrsAmf0Any* SrsAmf0Undefined::copy()
{
    SrsAmf0Undefined* copy = new SrsAmf0Undefined();
    return copy;
}

srs_error_t srs_amf0_read_any(SrsBuffer* stream, SrsAmf0Any** ppvalue)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsAmf0Any::discovery(stream, ppvalue)) != srs_success) {
        return srs_error_wrap(err, "discovery");
    }
    
    srs_assert(*ppvalue);
    
    if ((err = (*ppvalue)->read(stream)) != srs_success) {
        srs_freep(*ppvalue);
        return srs_error_wrap(err, "parse elem");
    }
    
    return err;
}

srs_error_t srs_amf0_read_string(SrsBuffer* stream, string& value)
{
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_String) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "String invalid marker=%#x", marker);
    }
    
    return srs_amf0_read_utf8(stream, value);
}

srs_error_t srs_amf0_write_string(SrsBuffer* stream, string value)
{   
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_String);
    
    return srs_amf0_write_utf8(stream, value);
}

srs_error_t srs_amf0_read_boolean(SrsBuffer* stream, bool& value)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Boolean) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "Boolean invalid marker=%#x", marker);
    }
    
    // value
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    value = (stream->read_1bytes() != 0);
    
    return err;
}

srs_error_t srs_amf0_write_boolean(SrsBuffer* stream, bool value)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    stream->write_1bytes(RTMP_AMF0_Boolean);
    
    // value
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    if (value) {
        stream->write_1bytes(0x01);
    } else {
        stream->write_1bytes(0x00);
    }
    
    return err;
}

srs_error_t srs_amf0_read_number(SrsBuffer* stream, double& value)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Number) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "Number invalid marker=%#x", marker);
    }
    
    // value
    if (!stream->require(8)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 8 only %d bytes", stream->left());
    }
    
    int64_t temp = stream->read_8bytes();
    memcpy(&value, &temp, 8);
    
    return err;
}

srs_error_t srs_amf0_write_number(SrsBuffer* stream, double value)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_Number);
    
    // value
    if (!stream->require(8)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 8 only %d bytes", stream->left());
    }
    
    int64_t temp = 0x00;
    memcpy(&temp, &value, 8);
    stream->write_8bytes(temp);
    
    return err;
}

srs_error_t srs_amf0_read_null(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Null) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "Null invalid marker=%#x", marker);
    }
    
    return err;
}

srs_error_t srs_amf0_write_null(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_Null);
    
    return err;
}

srs_error_t srs_amf0_read_undefined(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 1 only %d bytes", stream->left());
    }
    
    char marker = stream->read_1bytes();
    if (marker != RTMP_AMF0_Undefined) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "Undefined invalid marker=%#x", marker);
    }
    
    return err;
}

srs_error_t srs_amf0_write_undefined(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    // marker
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 1 only %d bytes", stream->left());
    }
    
    stream->write_1bytes(RTMP_AMF0_Undefined);
    
    return err;
}

namespace _srs_internal
{
    srs_error_t srs_amf0_read_utf8(SrsBuffer* stream, string& value)
    {
        srs_error_t err = srs_success;
        
        // len
        if (!stream->require(2)) {
            return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires 2 only %d bytes", stream->left());
        }
        int16_t len = stream->read_2bytes();
        
        // empty string
        if (len <= 0) {
            return err;
        }
        
        // data
        if (!stream->require(len)) {
            return srs_error_new(ERROR_RTMP_AMF0_DECODE, "requires %d only %d bytes", len, stream->left());
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
        
        return err;
    }
    
    srs_error_t srs_amf0_write_utf8(SrsBuffer* stream, string value)
    {
        srs_error_t err = srs_success;
        
        // len
        if (!stream->require(2)) {
            return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires 2 only %d bytes", stream->left());
        }
        stream->write_2bytes(value.length());
        
        // empty string
        if (value.length() <= 0) {
            return err;
        }
        
        // data
        if (!stream->require((int)value.length())) {
            return srs_error_new(ERROR_RTMP_AMF0_ENCODE, "requires %d only %d bytes", value.length(), stream->left());
        }
        stream->write_string(value);
        
        return err;
    }
    
    bool srs_amf0_is_object_eof(SrsBuffer* stream)
    {
        // detect the object-eof specially
        if (stream->require(3)) {
            int32_t flag = stream->read_3bytes();
            stream->skip(-3);
            
            return 0x09 == flag;
        }
        
        return false;
    }
    
    srs_error_t srs_amf0_write_object_eof(SrsBuffer* stream, SrsAmf0ObjectEOF* value)
    {
        srs_assert(value != NULL);
        return value->write(stream);
    }
    
    srs_error_t srs_amf0_write_any(SrsBuffer* stream, SrsAmf0Any* value)
    {
        srs_assert(value != NULL);
        return value->write(stream);
    }
}

