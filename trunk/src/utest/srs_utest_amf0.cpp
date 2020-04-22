/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

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
#include <srs_utest_amf0.hpp>

#include <string>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_buffer.hpp>
using namespace _srs_internal;

/**
* main scenario to use amf0.
* user scenario: coding and decoding with amf0
*/
VOID TEST(ProtocolAMF0Test, ScenarioMain)
{
    // coded amf0 object
    int nb_bytes = 0;
    char* bytes = NULL;
    
    // coding data to binaries by amf0
    // for example, send connect app response to client.
    if (true) {
        // props: object
        //        fmsVer: string
        //        capabilities: number
        //        mode: number
        // info: object
        //         level: string
        //        code: string
        //        descrption: string
        //        objectEncoding: number
        //        data: array
        //                version: string
        //                srs_sig: string
        SrsAmf0Object* props = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, props);
        props->set("fmsVer", SrsAmf0Any::str("FMS/3,5,3,888"));
        props->set("capabilities", SrsAmf0Any::number(253));
        props->set("mode", SrsAmf0Any::number(123));
        
        SrsAmf0Object* info = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, info);
        info->set("level", SrsAmf0Any::str("info"));
        info->set("code", SrsAmf0Any::str("NetStream.Connnect.Success"));
        info->set("descrption", SrsAmf0Any::str("connected"));
        info->set("objectEncoding", SrsAmf0Any::number(3));
        
        SrsAmf0EcmaArray* data = SrsAmf0Any::ecma_array();
        info->set("data", data);
        data->set("version", SrsAmf0Any::str("FMS/3,5,3,888"));
        data->set("srs_sig", SrsAmf0Any::str("srs"));
        
        // buf store the serialized props/info
        nb_bytes = props->total_size() + info->total_size();
        ASSERT_GT(nb_bytes, 0);
        bytes = new char[nb_bytes];
        
        // use SrsBuffer to write props/info to binary buf.
        SrsBuffer s(bytes, nb_bytes);
        EXPECT_EQ(srs_success, props->write(&s));
        EXPECT_EQ(srs_success, info->write(&s));
        EXPECT_TRUE(s.empty());
        
        // now, user can use the buf
        EXPECT_EQ(0x03, bytes[0]);
        EXPECT_EQ(0x09, bytes[nb_bytes - 1]);
    }
    SrsAutoFree(char, bytes);
    
    // decoding amf0 object from bytes
    // when user know the schema
    if (true) {
        ASSERT_TRUE(NULL != bytes);
        
        // use SrsBuffer to assist amf0 object to read from bytes.
        SrsBuffer s(bytes, nb_bytes);
        
        // decoding
        // if user know the schema, for instance, it's an amf0 object,
        // user can use specified object to decoding.
        SrsAmf0Object* props = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, props);
        EXPECT_EQ(srs_success, props->read(&s));
        
        // user can use specified object to decoding.
        SrsAmf0Object* info = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, info);
        EXPECT_EQ(srs_success, info->read(&s));
        
        // use the decoded data.
        SrsAmf0Any* prop = NULL;
        
        // if user requires specified property, use ensure of amf0 object
        EXPECT_TRUE(NULL != (prop = props->ensure_property_string("fmsVer")));
        // the property can assert to string.
        ASSERT_TRUE(prop->is_string());
        // get the prop string value.
        EXPECT_STREQ("FMS/3,5,3,888", prop->to_str().c_str());
        
        // get other type property value
        EXPECT_TRUE(NULL != (prop = info->get_property("data")));
        // we cannot assert the property is ecma array
        if (prop->is_ecma_array()) {
            SrsAmf0EcmaArray* data = prop->to_ecma_array();
            // it must be a ecma array.
            ASSERT_TRUE(NULL != data);
            
            // get property of array
            EXPECT_TRUE(NULL != (prop = data->ensure_property_string("srs_sig")));
            ASSERT_TRUE(prop->is_string());
            EXPECT_STREQ("srs", prop->to_str().c_str());
        }
        
        // confidence about the schema
        EXPECT_TRUE(NULL != (prop = info->ensure_property_string("level")));
        ASSERT_TRUE(prop->is_string());
        EXPECT_STREQ("info", prop->to_str().c_str());
    }
    
    // use any to decoding it,
    // if user donot know the schema
    if (true) {
        ASSERT_TRUE(NULL != bytes);
        
        // use SrsBuffer to assist amf0 object to read from bytes.
        SrsBuffer s(bytes, nb_bytes);
        
        // decoding a amf0 any, for user donot know
        SrsAmf0Any* any = NULL;
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &any));
        SrsAutoFree(SrsAmf0Any, any);
        
        // for amf0 object
        if (any->is_object()) {
            SrsAmf0Object* obj = any->to_object();
            ASSERT_TRUE(NULL != obj);
            
            // use foreach to process properties
            for (int i = 0; i < obj->count(); ++i) {
                string name = obj->key_at(i);
                SrsAmf0Any* value = obj->value_at(i);
                
                // use the property name
                EXPECT_TRUE("" != name);
                // use the property value
                EXPECT_TRUE(NULL != value);
            }
        }
    }
}

/**
* to calc the size of amf0 instances.
*/
VOID TEST(ProtocolAMF0Test, ApiSize) 
{
    // size of elem
    EXPECT_EQ(2+6, SrsAmf0Size::utf8("winlin"));
    EXPECT_EQ(2+0, SrsAmf0Size::utf8(""));
    
    EXPECT_EQ(1+2+6, SrsAmf0Size::str("winlin"));
    EXPECT_EQ(1+2+0, SrsAmf0Size::str(""));
    
    EXPECT_EQ(1+8, SrsAmf0Size::number());
    
    EXPECT_EQ(1, SrsAmf0Size::null());
    
    EXPECT_EQ(1, SrsAmf0Size::undefined());
    
    EXPECT_EQ(1+1, SrsAmf0Size::boolean());
    
    // object: empty
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    // object: elem
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("age")+SrsAmf0Size::number();
        o->set("age", SrsAmf0Any::number(9));
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::null();
        o->set("email", SrsAmf0Any::null());
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::undefined();
        o->set("email", SrsAmf0Any::undefined());
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("sex")+SrsAmf0Size::boolean();
        o->set("sex", SrsAmf0Any::boolean(true));
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    
    // array: empty
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    // array: elem
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("age")+SrsAmf0Size::number();
        o->set("age", SrsAmf0Any::number(9));
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::null();
        o->set("email", SrsAmf0Any::null());
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::undefined();
        o->set("email", SrsAmf0Any::undefined());
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("sex")+SrsAmf0Size::boolean();
        o->set("sex", SrsAmf0Any::boolean(true));
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    
    // object: array
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0EcmaArray* args = SrsAmf0Any::ecma_array();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::ecma_array(args);
        o->set("args", args);
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0EcmaArray* args = SrsAmf0Any::ecma_array();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::ecma_array(args);
        o->set("args", args);
        
        SrsAmf0EcmaArray* params = SrsAmf0Any::ecma_array();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::ecma_array(params);
        o->set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    
    // array: object
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0Object* args = SrsAmf0Any::object();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::object(args);
        o->set("args", args);
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0Object* args = SrsAmf0Any::object();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::object(args);
        o->set("args", args);
        
        SrsAmf0Object* params = SrsAmf0Any::object();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::object(params);
        o->set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    
    // object: object
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0Object* args = SrsAmf0Any::object();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::object(args);
        o->set("args", args);
        
        SrsAmf0Object* params = SrsAmf0Any::object();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::object(params);
        o->set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    
    // array: array
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0EcmaArray* args = SrsAmf0Any::ecma_array();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::ecma_array(args);
        o->set("args", args);
        
        SrsAmf0EcmaArray* params = SrsAmf0Any::ecma_array();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::ecma_array(params);
        o->set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o)); 
    }
}

/**
* about the AMF0 any.
*/
VOID TEST(ProtocolAMF0Test, ApiAnyElem) 
{
    SrsAmf0Any* o = NULL;
    
    // string
    if (true) {
        o = SrsAmf0Any::str();
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_string());
        EXPECT_STREQ("", o->to_str().c_str());
    }
    if (true) {
        o = SrsAmf0Any::str("winlin");
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_string());
        EXPECT_STREQ("winlin", o->to_str().c_str());
    }
    
    // bool
    if (true) {
        o = SrsAmf0Any::boolean();
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_boolean());
        EXPECT_FALSE(o->to_boolean());
    }
    if (true) {
        o = SrsAmf0Any::boolean(false);
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_boolean());
        EXPECT_FALSE(o->to_boolean());
    }
    if (true) {
        o = SrsAmf0Any::boolean(true);
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_boolean());
        EXPECT_TRUE(o->to_boolean());
    }
    
    // number
    if (true) {
        o = SrsAmf0Any::number();
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_number());
        EXPECT_DOUBLE_EQ(0, o->to_number());
    }
    if (true) {
        o = SrsAmf0Any::number(100);
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_number());
        EXPECT_DOUBLE_EQ(100, o->to_number());
    }
    if (true) {
        o = SrsAmf0Any::number(-100);
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_number());
        EXPECT_DOUBLE_EQ(-100, o->to_number());
    }

    // null
    if (true) {
        o = SrsAmf0Any::null();
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_null());
    }

    // undefined
    if (true) {
        o = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, o);
        ASSERT_TRUE(NULL != o);
        EXPECT_TRUE(o->is_undefined());
    }
}

/**
* about the stream to serialize/deserialize AMF0 instance.
*/
VOID TEST(ProtocolAMF0Test, ApiAnyIO) 
{
    srs_error_t err;

    SrsAmf0Any* o = NULL;
    
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    SrsBuffer s(buf, sizeof(buf));
    
    // object eof
    if (true) {
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[2] = 0x09;
        
        o = SrsAmf0Any::object_eof();
        SrsAutoFree(SrsAmf0Any, o);

        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_EQ(3, s.pos());
        
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[0] = 0x01;

        HELPER_EXPECT_FAILED(o->read(&s));
    }
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::object_eof();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_EQ(3, s.pos());
        
        s.skip(-3);
        EXPECT_EQ(0x09, s.read_3bytes());
    }
    
    // string
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::str("winlin");
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(2, s.read_1bytes());
        EXPECT_EQ(6, s.read_2bytes());
        EXPECT_EQ('w', (s.data() + s.pos())[0]);
        EXPECT_EQ('n', (s.data() + s.pos())[5]);
        
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[3] = 'x';
        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_STREQ("xinlin", o->to_str().c_str());
    }
    
    // number
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::number(10);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(0, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_DOUBLE_EQ(10, o->to_number());
    }
    
    // boolean
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::boolean(true);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(1, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_TRUE(o->to_boolean());
    }
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::boolean(false);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(1, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_FALSE(o->to_boolean());
    }
    
    // null
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::null();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(5, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_TRUE(o->is_null());
    }
    
    // undefined
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(6, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        HELPER_EXPECT_SUCCESS(o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_TRUE(o->is_undefined());
    }

    // any: string
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::str("winlin");
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_string());
        EXPECT_STREQ("winlin", po->to_str().c_str());
    }

    // any: number
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::number(10);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_number());
        EXPECT_DOUBLE_EQ(10, po->to_number());
    }

    // any: boolean
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::boolean(true);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_boolean());
        EXPECT_TRUE(po->to_boolean());
    }

    // any: null
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::null();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_null());
    }

    // any: undefined
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_undefined());
    }
    
    // mixed any
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::str("winlin");
        EXPECT_EQ(srs_success, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::number(10);
        EXPECT_EQ(srs_success, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::boolean(true);
        EXPECT_EQ(srs_success, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::null();
        EXPECT_EQ(srs_success, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::undefined();
        EXPECT_EQ(srs_success, o->write(&s));
        srs_freep(o);
        
        s.skip(-1 * s.pos());
        SrsAmf0Any* po = NULL;
        
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_string());
        EXPECT_STREQ("winlin", po->to_str().c_str());
        srs_freep(po);
        
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_number());
        EXPECT_DOUBLE_EQ(10, po->to_number());
        srs_freep(po);
        
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_boolean());
        EXPECT_TRUE(po->to_boolean());
        srs_freep(po);
        
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_null());
        srs_freep(po);
        
        EXPECT_EQ(srs_success, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_undefined());
        srs_freep(po);
    }
}

/**
* to get the type identity
*/
VOID TEST(ProtocolAMF0Test, ApiAnyTypeAssert)
{
    srs_error_t err;
    SrsAmf0Any* o = NULL;
    
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    SrsBuffer s(buf, sizeof(buf));
    
    // read any
    if (true) {
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[0] = 0x12;
        HELPER_EXPECT_FAILED(srs_amf0_read_any(&s, &o));
        EXPECT_TRUE(NULL == o);
        srs_freep(o);
    }
    
    // any convert
    if (true) {
        o = SrsAmf0Any::str();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_string());
    }
    if (true) {
        o = SrsAmf0Any::number();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_number());
    }
    if (true) {
        o = SrsAmf0Any::boolean();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_boolean());
    }
    if (true) {
        o = SrsAmf0Any::null();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_null());
    }
    if (true) {
        o = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_undefined());
    }
    if (true) {
        o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_object());
    }
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_ecma_array());
    }
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0Any, o);
        EXPECT_TRUE(o->is_strict_array());
    }
    
    // empty object
    if (true) {
        o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Any, o);
        s.skip(-1 * s.pos());
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(1+3, s.pos());
    }
    
    // empty ecma array
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0Any, o);
        s.skip(-1 * s.pos());
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(1+4+3, s.pos());
    }
    
    // strict array
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0Any, o);
        s.skip(-1 * s.pos());
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(1+4, s.pos());
    }
}

/**
* object property get/set
*/
VOID TEST(ProtocolAMF0Test, ApiObjectProps) 
{
    SrsAmf0Object* o = NULL;
    
    // get/set property
    if (true) {
        o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        EXPECT_TRUE(NULL == o->get_property("name"));
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_TRUE(NULL != o->get_property("name"));
        
        EXPECT_TRUE(NULL == o->get_property("age"));
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_TRUE(NULL != o->get_property("age"));
    }
    
    // index property
    if (true) {
        o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_STREQ("name", o->key_at(0).c_str());
        ASSERT_TRUE(o->value_at(0)->is_string());
        EXPECT_STREQ("winlin", o->value_at(0)->to_str().c_str());
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_STREQ("name", o->key_at(0).c_str());
        ASSERT_TRUE(o->value_at(0)->is_string());
        EXPECT_STREQ("winlin", o->value_at(0)->to_str().c_str());
        
        EXPECT_STREQ("age", o->key_at(1).c_str());
        ASSERT_TRUE(o->value_at(1)->is_number());
        EXPECT_DOUBLE_EQ(100, o->value_at(1)->to_number());
    }
    
    // ensure property
    if (true) {
        o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        EXPECT_TRUE(NULL == o->ensure_property_string("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("age"));
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_TRUE(NULL != o->ensure_property_string("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("age"));
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_TRUE(NULL != o->ensure_property_string("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("name"));
        EXPECT_TRUE(NULL != o->ensure_property_number("age"));
        EXPECT_TRUE(NULL == o->ensure_property_string("age"));
    }

    // count
    if (true) {
        o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);
        
        EXPECT_EQ(0, o->count());
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_EQ(1, o->count());
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_EQ(1, o->count());
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_EQ(2, o->count());
    }
}

/**
* ecma array properties.
*/
VOID TEST(ProtocolAMF0Test, ApiEcmaArrayProps) 
{
    SrsAmf0EcmaArray* o = NULL;
    
    // get/set property
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        EXPECT_TRUE(NULL == o->get_property("name"));
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_TRUE(NULL != o->get_property("name"));
        
        EXPECT_TRUE(NULL == o->get_property("age"));
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_TRUE(NULL != o->get_property("age"));
    }
    
    // index property
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_STREQ("name", o->key_at(0).c_str());
        ASSERT_TRUE(o->value_at(0)->is_string());
        EXPECT_STREQ("winlin", o->value_at(0)->to_str().c_str());
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_STREQ("name", o->key_at(0).c_str());
        ASSERT_TRUE(o->value_at(0)->is_string());
        EXPECT_STREQ("winlin", o->value_at(0)->to_str().c_str());
        
        EXPECT_STREQ("age", o->key_at(1).c_str());
        ASSERT_TRUE(o->value_at(1)->is_number());
        EXPECT_DOUBLE_EQ(100, o->value_at(1)->to_number());
    }
    
    // ensure property
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        EXPECT_TRUE(NULL == o->ensure_property_string("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("age"));
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_TRUE(NULL != o->ensure_property_string("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("age"));
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_TRUE(NULL != o->ensure_property_string("name"));
        EXPECT_TRUE(NULL == o->ensure_property_number("name"));
        EXPECT_TRUE(NULL != o->ensure_property_number("age"));
        EXPECT_TRUE(NULL == o->ensure_property_string("age"));
    }

    // count
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o);
        
        EXPECT_EQ(0, o->count());
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_EQ(1, o->count());
        
        o->set("name", SrsAmf0Any::str("winlin"));
        EXPECT_EQ(1, o->count());
        
        o->set("age", SrsAmf0Any::number(100));
        EXPECT_EQ(2, o->count());
    }
}

/**
* strict array.
*/
VOID TEST(ProtocolAMF0Test, ApiStrictArray)
{
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    SrsBuffer s(buf, sizeof(buf));
    
    SrsAmf0StrictArray* o = NULL;
    
    // append property
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0StrictArray, o);
        
        o->append(SrsAmf0Any::number(100));
        EXPECT_DOUBLE_EQ(100, o->at(0)->to_number());
        
        o->append(SrsAmf0Any::number(101));
        EXPECT_DOUBLE_EQ(101, o->at(1)->to_number());
        
        o->append(SrsAmf0Any::str("winlin"));
        EXPECT_STREQ("winlin", o->at(2)->to_str().c_str());
    }
    
    // count
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0StrictArray, o);
        
        EXPECT_EQ(0, o->count());
        
        o->append(SrsAmf0Any::boolean());
        EXPECT_EQ(1, o->count());
        
        o->append(SrsAmf0Any::boolean());
        EXPECT_EQ(2, o->count());

        o->clear();
        EXPECT_EQ(0, o->count());
    }
    
    // io
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0StrictArray, o);
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(5, s.pos());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(0x0a, s.read_1bytes());
        EXPECT_EQ(0x00, s.read_4bytes());
    }
    
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0StrictArray, o);
        
        o->append(SrsAmf0Any::number(0));
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(srs_success, o->write(&s));
        EXPECT_EQ(5 + SrsAmf0Size::number(), s.pos());
    }
}

/**
* object has object property, 
*/
VOID TEST(ProtocolAMF0Test, ObjectObjectObject)
{
    SrsAmf0Any* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Any, obj);
    EXPECT_EQ(0, obj->to_object()->count());
    
    SrsAmf0Any* child1 = SrsAmf0Any::object();
    obj->to_object()->set("child1", child1);
    EXPECT_EQ(1, obj->to_object()->count());
    EXPECT_EQ(0, child1->to_object()->count());
    
    SrsAmf0Any* child2 = SrsAmf0Any::object();
    child1->to_object()->set("child2", child2);
    EXPECT_EQ(1, obj->to_object()->count());
    EXPECT_EQ(1, child1->to_object()->count());
    EXPECT_EQ(0, child2->to_object()->count());
    
    SrsAmf0Any* child3 = SrsAmf0Any::object();
    child2->to_object()->set("child3", child3);
    EXPECT_EQ(1, obj->to_object()->count());
    EXPECT_EQ(1, child1->to_object()->count());
    EXPECT_EQ(1, child2->to_object()->count());
    EXPECT_EQ(0, child3->to_object()->count());
}

/**
* ecma array has ecma array property, 
*/
VOID TEST(ProtocolAMF0Test, EcmaEcmaEcma)
{
    SrsAmf0Any* arr = SrsAmf0Any::ecma_array();
    SrsAutoFree(SrsAmf0Any, arr);
    EXPECT_EQ(0, arr->to_ecma_array()->count());
    
    SrsAmf0Any* arr1 = SrsAmf0Any::ecma_array();
    arr->to_ecma_array()->set("child1", arr1);
    EXPECT_EQ(1, arr->to_ecma_array()->count());
    EXPECT_EQ(0, arr1->to_ecma_array()->count());
    
    SrsAmf0Any* arr2 = SrsAmf0Any::ecma_array();
    arr1->to_ecma_array()->set("child2", arr2);
    EXPECT_EQ(1, arr->to_ecma_array()->count());
    EXPECT_EQ(1, arr1->to_ecma_array()->count());
    EXPECT_EQ(0, arr2->to_ecma_array()->count());
    
    SrsAmf0Any* arr3 = SrsAmf0Any::ecma_array();
    arr2->to_ecma_array()->set("child3", arr3);
    EXPECT_EQ(1, arr->to_ecma_array()->count());
    EXPECT_EQ(1, arr1->to_ecma_array()->count());
    EXPECT_EQ(1, arr2->to_ecma_array()->count());
    EXPECT_EQ(0, arr3->to_ecma_array()->count());
}

/**
* strict array contains strict array
*/
VOID TEST(ProtocolAMF0Test, StrictStrictStrict)
{
    SrsAmf0Any* arr = SrsAmf0Any::strict_array();
    SrsAutoFree(SrsAmf0Any, arr);
    EXPECT_EQ(0, arr->to_strict_array()->count());
    
    SrsAmf0Any* arr1 = SrsAmf0Any::strict_array();
    arr->to_strict_array()->append(arr1);
    EXPECT_EQ(1, arr->to_strict_array()->count());
    EXPECT_EQ(0, arr1->to_strict_array()->count());
    
    SrsAmf0Any* arr2 = SrsAmf0Any::strict_array();
    arr1->to_strict_array()->append(arr2);
    EXPECT_EQ(1, arr->to_strict_array()->count());
    EXPECT_EQ(1, arr1->to_strict_array()->count());
    EXPECT_EQ(0, arr2->to_strict_array()->count());
    
    SrsAmf0Any* arr3 = SrsAmf0Any::strict_array();
    arr2->to_strict_array()->append(arr3);
    EXPECT_EQ(1, arr->to_strict_array()->count());
    EXPECT_EQ(1, arr1->to_strict_array()->count());
    EXPECT_EQ(1, arr2->to_strict_array()->count());
    EXPECT_EQ(0, arr3->to_strict_array()->count());
}

/**
* object has ecma array property,
* where ecma array contains strict array.
*/
VOID TEST(ProtocolAMF0Test, ObjectEcmaStrict)
{
    SrsAmf0Any* obj = SrsAmf0Any::object();
    SrsAutoFree(SrsAmf0Any, obj);
    EXPECT_EQ(0, obj->to_object()->count());
    
    SrsAmf0Any* arr1 = SrsAmf0Any::ecma_array();
    obj->to_object()->set("child1", arr1);
    EXPECT_EQ(1, obj->to_object()->count());
    EXPECT_EQ(0, arr1->to_ecma_array()->count());
    
    SrsAmf0Any* arr2 = SrsAmf0Any::strict_array();
    arr1->to_ecma_array()->set("child2", arr2);
    EXPECT_EQ(1, obj->to_object()->count());
    EXPECT_EQ(1, arr1->to_ecma_array()->count());
    EXPECT_EQ(0, arr2->to_strict_array()->count());
    
    SrsAmf0Any* arr3 = SrsAmf0Any::ecma_array();
    arr2->to_strict_array()->append(arr3);
    EXPECT_EQ(1, obj->to_object()->count());
    EXPECT_EQ(1, arr1->to_ecma_array()->count());
    EXPECT_EQ(1, arr2->to_strict_array()->count());
    EXPECT_EQ(0, arr3->to_ecma_array()->count());
}

VOID TEST(ProtocolAMF0Test, InterfacesString)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::str("hello");
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_TRUE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(p->is_complex_object());
        EXPECT_TRUE(string("hello") == p->to_str());
        EXPECT_STREQ("hello", p->to_str_raw());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("String hello\n", d);
        delete[] d;

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_string());
        EXPECT_TRUE(string("hello") == j->to_str());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
        EXPECT_TRUE(string("hello") == pp->to_str());

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(string("hello") == cp->to_str());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_SUCCESS(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x02};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x02, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x02, 0x00, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_SUCCESS(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x02, 0x00, 0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::str();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesBoolean)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::boolean();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_TRUE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(p->is_complex_object());
        EXPECT_FALSE(p->to_boolean());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("Boolean false\n", d);
        delete[] d;

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_boolean());
        EXPECT_FALSE(j->to_boolean());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
        EXPECT_FALSE(p->to_boolean());

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_FALSE(cp->to_boolean());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::boolean();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::boolean();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::boolean();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::boolean();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::boolean();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesNumber)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::number();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(!p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(p->is_complex_object());
        EXPECT_TRUE(0.0 == p->to_number());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("Number 0.0\n", d);
        delete[] d;

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_integer());
        EXPECT_TRUE(0.0 == j->to_integer());
        srs_freep(j);

        p->set_number(100.1);
        EXPECT_TRUE(100.1 == p->to_number());

        j = p->to_json();
        EXPECT_TRUE(j->is_number());
        EXPECT_TRUE(100.1 == j->to_number());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
        EXPECT_TRUE(100.1 == p->to_number());

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(100.1 == cp->to_number());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::number();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::number();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::number();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::number();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::number();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesDate)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::date();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(!p->is_date());
        EXPECT_FALSE(p->is_complex_object());
        EXPECT_TRUE(0 == p->to_date());
        EXPECT_TRUE(0 == p->to_date_time_zone());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("Date 0/0\n", d);
        delete[] d;

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesNull)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::null();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(!p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(p->is_complex_object());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("Null\n", d);
        delete[] d;

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_null());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::null();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::null();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::null();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesUndefined)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(!p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(p->is_complex_object());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("Undefined\n", d);
        delete[] d;

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_null());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::undefined();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* o = SrsAmf0Any::undefined();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Any* o = SrsAmf0Any::undefined();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesObject)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(!p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(!p->is_complex_object());
        EXPECT_TRUE(NULL != p->to_object());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("Object (0 items)\n", d);
        delete[] d;

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_object());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
        EXPECT_TRUE(NULL != pp->to_object());

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(NULL != cp->to_object());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Object* o = SrsAmf0Any::object();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Object* o = SrsAmf0Any::object();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Object* o = SrsAmf0Any::object();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Object* o = SrsAmf0Any::object();
        o->set("id", SrsAmf0Any::number(3.0));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x03, 0x00, 0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Object* o = SrsAmf0Any::object();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x02, 'i', 'd', 0x02, 0x00, 0x03};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Object* o = SrsAmf0Any::object();
        o->set("id", SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x03, 0x00, 0x01, 'a'};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Object* o = SrsAmf0Any::object();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x02, 'i', 'd', 0x02, 0x00, 0x03, 's', 'r', 's'};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0Object* o = SrsAmf0Any::object();
        o->set("id", SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesObject2)
{
    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Any, p);

        SrsAmf0Object* o = p->to_object();
        EXPECT_TRUE(NULL != o);

        o->set("version", SrsAmf0Any::number(3.0));
        o->set("name", SrsAmf0Any::str("srs"));
        EXPECT_STREQ("version", o->key_raw_at(0));

        SrsAmf0Any* prop = o->get_property("version");
        EXPECT_TRUE(prop->is_number());
        EXPECT_TRUE(3.0 == prop->to_number());

        prop = o->ensure_property_number("version");
        EXPECT_TRUE(NULL != prop);

        prop = o->ensure_property_string("name");
        EXPECT_TRUE(NULL != prop);

        o->remove("version");
        EXPECT_TRUE(NULL == o->get_property("version"));

        char* s = p->human_print(NULL, NULL);
        EXPECT_TRUE(s != NULL);
        srs_freepa(s);

        SrsJsonAny* j = o->to_json();
        EXPECT_TRUE(j != NULL);
        srs_freep(j);
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0Any, p);

        SrsAmf0StrictArray* o = p->to_strict_array();
        EXPECT_TRUE(NULL != o);

        o->append(SrsAmf0Any::number(3.0));
        o->append(SrsAmf0Any::str("srs"));

        SrsAmf0Any* prop = o->at(0);
        EXPECT_TRUE(prop->is_number());
        EXPECT_TRUE(3.0 == prop->to_number());

        prop = o->at(0);
        EXPECT_TRUE(NULL != prop);

        prop = o->at(1);
        EXPECT_TRUE(NULL != prop);

        char* s = p->human_print(NULL, NULL);
        EXPECT_TRUE(s != NULL);
        srs_freepa(s);
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0Any, p);

        SrsAmf0EcmaArray* o = p->to_ecma_array();
        EXPECT_TRUE(NULL != o);

        o->set("version", SrsAmf0Any::number(3.0));
        o->set("name", SrsAmf0Any::str("srs"));

        EXPECT_TRUE(string("version") == o->key_at(0));
        EXPECT_STREQ("version", o->key_raw_at(0));
        EXPECT_TRUE(string("name") == o->key_at(1));

        SrsAmf0Any* prop = o->get_property("version");
        EXPECT_TRUE(prop->is_number());
        EXPECT_TRUE(3.0 == prop->to_number());

        prop = o->ensure_property_number("version");
        EXPECT_TRUE(NULL != prop);

        prop = o->ensure_property_string("name");
        EXPECT_TRUE(NULL != prop);

        prop = o->value_at(1);
        EXPECT_TRUE(NULL != prop);
        EXPECT_TRUE(prop->is_string());

        char* s = p->human_print(NULL, NULL);
        EXPECT_TRUE(s != NULL);
        srs_freepa(s);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesObjectEOF)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::object_eof();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(!p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(!p->is_complex_object());

        SrsJsonAny* j = p->to_json();
        EXPECT_TRUE(j->is_null());
        srs_freep(j);

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(cp->is_object_eof());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0ObjectEOF* o = new SrsAmf0ObjectEOF();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0ObjectEOF* o = new SrsAmf0ObjectEOF();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0ObjectEOF* o = new SrsAmf0ObjectEOF();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0ObjectEOF* o = new SrsAmf0ObjectEOF();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0ObjectEOF* o = new SrsAmf0ObjectEOF();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0ObjectEOF* o = new SrsAmf0ObjectEOF();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesEcmaArray)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(!p->is_ecma_array());
        EXPECT_FALSE(p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(!p->is_complex_object());
        EXPECT_TRUE(NULL != p->to_ecma_array());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("EcmaArray (0 items)\n", d);
        delete[] d;

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
        EXPECT_TRUE(NULL != pp->to_ecma_array());

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(NULL != cp->to_ecma_array());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        o->set("id", SrsAmf0Any::number(3.0));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x08, 0x00, 0x00, 0x00, 0x01, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 'i', 'd', 0x02, 0x00, 0x03};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        o->set("id", SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x08, 0x00, 0x00, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 'a'};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 'i', 'd', 0x02, 0x00, 0x03, 's', 'r', 's'};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        o->set("id", SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesStrictArray)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0Any, p);

        EXPECT_FALSE(p->is_string());
        EXPECT_FALSE(p->is_boolean());
        EXPECT_FALSE(p->is_number());
        EXPECT_FALSE(p->is_null());
        EXPECT_FALSE(p->is_undefined());
        EXPECT_FALSE(p->is_object());
        EXPECT_FALSE(p->is_object_eof());
        EXPECT_FALSE(p->is_ecma_array());
        EXPECT_FALSE(!p->is_strict_array());
        EXPECT_FALSE(p->is_date());
        EXPECT_FALSE(!p->is_complex_object());
        EXPECT_TRUE(NULL != p->to_strict_array());

        char* d = p->human_print(NULL, NULL);
        EXPECT_STREQ("StrictArray (0 items)\n", d);
        delete[] d;

        // For marshal and unmarshal.
        char* bb = new char[p->total_size()];
        SrsAutoFreeA(char, bb);
        SrsBuffer b(bb, p->total_size());
        HELPER_EXPECT_SUCCESS(p->write(&b));

        b.skip(-1 * b.pos());
        SrsAmf0Any* pp = NULL;
        SrsAutoFree(SrsAmf0Any, pp);
        HELPER_EXPECT_SUCCESS(SrsAmf0Any::discovery(&b, &pp));

        b.skip(-1 * b.pos());
        HELPER_EXPECT_SUCCESS(pp->read(&b));
        EXPECT_TRUE(NULL != pp->to_strict_array());

        // For copy.
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(NULL != cp->to_strict_array());
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x01};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        o->append(SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x0a, 0x00, 0x00, 0x00, 0x01, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x0a, 0x00, 0x00, 0x00};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x0a, 0x00, 0x00, 0x00, 0x01, 0x02};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x03};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        o->append(SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x0a, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 'a'};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        HELPER_EXPECT_FAILED(o->read(&b));
        srs_freep(o);
    }

    if (true) {
        uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x03, 's', 'r'};
        SrsBuffer b((char*)data, sizeof(data));
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        o->append(SrsAmf0Any::str("srs"));
        HELPER_EXPECT_FAILED(o->write(&b));
        srs_freep(o);
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesOthers)
{
    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Any, p);
        EXPECT_FALSE(!p->is_complex_object());
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::object_eof();
        SrsAutoFree(SrsAmf0Any, p);
        EXPECT_FALSE(!p->is_complex_object());
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0Any, p);
        EXPECT_FALSE(!p->is_complex_object());
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0Any, p);
        EXPECT_FALSE(!p->is_complex_object());
    }
}

VOID TEST(ProtocolAMF0Test, InterfacesError)
{
    srs_error_t err;

    if (true) {
        SrsBuffer b;
        HELPER_EXPECT_FAILED(SrsAmf0Any::discovery(&b, NULL));
    }

    if (true) {
        char data = 0x3f;
        SrsBuffer b(&data, 1);
        HELPER_EXPECT_FAILED(SrsAmf0Any::discovery(&b, NULL));
    }

    if (true) {
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);

        o->set("name", SrsAmf0Any::number(3.0));
        o->set("name", SrsAmf0Any::str("srs"));

        SrsAmf0Any* prop;
        prop = o->ensure_property_number("name");
        EXPECT_TRUE(prop == NULL);

        prop = o->ensure_property_string("id");
        EXPECT_TRUE(prop == NULL);

        prop = o->ensure_property_string("name");
        ASSERT_TRUE(prop != NULL);
        EXPECT_STREQ("srs", prop->to_str_raw());
    }

    if (true) {
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o);

        o->set("name", SrsAmf0Any::str("srs"));
        o->set("name", SrsAmf0Any::number(3.0));

        SrsAmf0Any* prop;
        prop = o->ensure_property_number("id");
        EXPECT_TRUE(prop == NULL);

        prop = o->ensure_property_string("name");
        EXPECT_TRUE(prop == NULL);

        prop = o->ensure_property_number("name");
        ASSERT_TRUE(prop != NULL);
        EXPECT_TRUE(3.0 == prop->to_number());
    }

    if (true) {
        SrsAmf0Object* src = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, src);

        src->set("name", SrsAmf0Any::str("srs"));
        src->set("name", SrsAmf0Any::number(3.0));

        SrsAmf0Any* cp = src->copy();
        SrsAutoFree(SrsAmf0Any, cp);

        SrsAmf0Object* o = cp->to_object();

        SrsAmf0Any* prop;
        prop = o->ensure_property_number("id");
        EXPECT_TRUE(prop == NULL);

        prop = o->ensure_property_string("name");
        EXPECT_TRUE(prop == NULL);

        prop = o->ensure_property_number("name");
        ASSERT_TRUE(prop != NULL);
        EXPECT_TRUE(3.0 == prop->to_number());
    }
}

VOID TEST(ProtocolAMF0Test, Amf0Object2)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Object* o = SrsAmf0Any::object();
        o->set("id", SrsAmf0Any::number(3.0));
        EXPECT_EQ(1, o->count());

        o->clear();
        EXPECT_EQ(0, o->count());

        srs_freep(o);
    }

    if (true) {
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        o->set("id", SrsAmf0Any::number(3.0));
        EXPECT_EQ(1, o->count());

        o->clear();
        EXPECT_EQ(0, o->count());

        srs_freep(o);
    }

    if (true) {
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        o->set("id", SrsAmf0Any::number(3.0));

        SrsJsonAny* j = o->to_json();
        EXPECT_TRUE(j->is_object());

        SrsJsonObject* jo = j->to_object();
        EXPECT_EQ(1, jo->count());

        srs_freep(j);
        srs_freep(o);
    }

    if (true) {
        SrsAmf0StrictArray* o = SrsAmf0Any::strict_array();
        o->append(SrsAmf0Any::number(3.0));

        SrsJsonAny* j = o->to_json();
        EXPECT_TRUE(j->is_array());

        SrsJsonArray* ja = j->to_array();
        EXPECT_EQ(1, ja->count());

        srs_freep(j);
        srs_freep(o);
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::null();
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(cp->is_null());
        srs_freep(cp);
        srs_freep(p);
    }

    if (true) {
        SrsAmf0Any* p = SrsAmf0Any::undefined();
        SrsAmf0Any* cp = p->copy();
        EXPECT_TRUE(cp->is_undefined());
        srs_freep(cp);
        srs_freep(p);
    }

    if (true) {
        SrsBuffer b;
        SrsAmf0Any* eof = SrsAmf0Any::object_eof();
        HELPER_EXPECT_FAILED(srs_amf0_write_object_eof(&b, (SrsAmf0ObjectEOF*)eof));
        srs_freep(eof);
    }
}

VOID TEST(ProtocolJSONTest, Interfaces)
{
    if (true) {
        SrsJsonAny* p = SrsJsonAny::str();
        EXPECT_TRUE(p->is_string());
        EXPECT_TRUE(p->to_str().empty());

        SrsAmf0Any* a = p->to_amf0();
        EXPECT_TRUE(a->is_string());

        srs_freep(a);
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::str("hello");
        EXPECT_TRUE(p->is_string());
        EXPECT_TRUE(string("hello") == p->to_str());

        SrsAmf0Any* a = p->to_amf0();
        EXPECT_TRUE(a->is_string());

        srs_freep(a);
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::str("hello", 2);
        EXPECT_TRUE(p->is_string());
        EXPECT_TRUE(string("he") == p->to_str());

        SrsAmf0Any* a = p->to_amf0();
        EXPECT_TRUE(a->is_string());

        srs_freep(a);
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::boolean(true);
        EXPECT_TRUE(p->is_boolean());

        SrsAmf0Any* a = p->to_amf0();
        EXPECT_TRUE(a->is_boolean());

        srs_freep(a);
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::integer();
        EXPECT_TRUE(p->is_integer());

        SrsAmf0Any* a = p->to_amf0();
        EXPECT_TRUE(a->is_number());

        srs_freep(a);
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::number();
        EXPECT_TRUE(p->is_number());

        SrsAmf0Any* a = p->to_amf0();
        EXPECT_TRUE(a->is_number());

        srs_freep(a);
        srs_freep(p);
    }
}

VOID TEST(ProtocolJSONTest, Dumps)
{
    if (true) {
        SrsJsonAny* p = SrsJsonAny::str("hello");
        EXPECT_TRUE(p->is_string());
        EXPECT_STREQ("\"hello\"", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::boolean(true);
        EXPECT_STREQ("true", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::integer(3);
        EXPECT_STREQ("3", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::number(3.1);
        EXPECT_STREQ("3.10", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::null();
        EXPECT_STREQ("null", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        EXPECT_STREQ("{}", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonArray* p = SrsJsonAny::array();
        EXPECT_STREQ("[]", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::object();
        EXPECT_STREQ("{}", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::array();
        EXPECT_STREQ("[]", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonObject* p = SrsJsonAny::object();
        p->set("id", SrsJsonAny::integer(3));
        p->set("year", SrsJsonAny::integer(2019));
        EXPECT_STREQ("{\"id\":3,\"year\":2019}", p->dumps().c_str());
        srs_freep(p);
    }

    if (true) {
        SrsJsonArray* p = SrsJsonAny::array();
        p->add(SrsJsonAny::integer(3));
        p->add(SrsJsonAny::integer(2));
        EXPECT_STREQ("[3,2]", p->dumps().c_str());
        srs_freep(p);
    }
}

VOID TEST(ProtocolJSONTest, Parse)
{
    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("\"hello\"");
        EXPECT_TRUE(p->is_string());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("true");
        EXPECT_TRUE(p->is_boolean());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("3");
        EXPECT_TRUE(p->is_integer());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("3.0");
        EXPECT_TRUE(p->is_number());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("null");
        EXPECT_TRUE(p->is_null());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("{}");
        EXPECT_TRUE(p->is_object());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("[]");
        EXPECT_TRUE(p->is_array());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("{\"id\":3}");
        EXPECT_TRUE(p->is_object());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("[\"id\",3]");
        EXPECT_TRUE(p->is_array());
        srs_freep(p);
    }
}

VOID TEST(ProtocolJSONTest, ObjectAPI)
{
    SrsJsonObject* p = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, p);

    p->set("id", SrsJsonAny::integer(3));
    p->set("name", SrsJsonAny::str("srs"));

    EXPECT_STREQ("name", p->key_at(1).c_str());
    EXPECT_STREQ("srs", p->value_at(1)->to_str().c_str());

    p->set("name", NULL);
    p->set("name", SrsJsonAny::str("ossrs"));
    p->set("version", SrsJsonAny::number(3.1));
    p->set("stable", SrsJsonAny::boolean(true));

    SrsJsonObject* pp = SrsJsonAny::object();
    p->set("args", pp);
    pp->set("url", SrsJsonAny::str("ossrs.net"));
    pp->set("year", SrsJsonAny::integer(2019));

    SrsJsonArray* pa = SrsJsonAny::array();
    p->set("authors", pa);
    pa->add(SrsJsonAny::str("winlin"));
    pa->add(SrsJsonAny::str("wenjie"));

    SrsJsonAny* prop = p->get_property("name");
    EXPECT_STREQ("ossrs", prop->to_str().c_str());
    EXPECT_STREQ("ossrs", p->ensure_property_string("name")->to_str().c_str());

    EXPECT_TRUE(NULL == p->get_property("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_string("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_string("id"));

    EXPECT_TRUE(NULL == p->ensure_property_integer("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_integer("name"));
    EXPECT_EQ(3, p->ensure_property_integer("id")->to_integer());

    EXPECT_TRUE(NULL == p->ensure_property_number("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_number("name"));
    EXPECT_EQ(3.1, p->ensure_property_number("version")->to_number());

    EXPECT_TRUE(NULL == p->ensure_property_boolean("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_boolean("name"));
    EXPECT_TRUE(p->ensure_property_boolean("stable")->to_boolean());

    EXPECT_TRUE(NULL == p->ensure_property_object("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_object("name"));
    EXPECT_TRUE(NULL != p->ensure_property_object("args")->to_object());

    EXPECT_TRUE(NULL == p->ensure_property_array("invalid"));
    EXPECT_TRUE(NULL == p->ensure_property_array("name"));
    EXPECT_TRUE(NULL != p->ensure_property_array("authors")->to_array());

    SrsAmf0Object* a = (SrsAmf0Object*)p->to_amf0();
    EXPECT_EQ(6, a->count());
    srs_freep(a);
}

VOID TEST(ProtocolJSONTest, ArrayAPI)
{
    SrsJsonArray* p = SrsJsonAny::array();
    SrsAutoFree(SrsJsonArray, p);

    p->add(SrsJsonAny::integer(2019));
    p->add(SrsJsonAny::str("srs"));
    p->add(SrsJsonAny::boolean(true));

    EXPECT_STREQ("srs", p->at(1)->to_str().c_str());

    SrsAmf0StrictArray* a = (SrsAmf0StrictArray*)p->to_amf0();
    EXPECT_EQ(3, a->count());
    srs_freep(a);
}

VOID TEST(ProtocolJSONTest, ParseSpecial)
{
    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("[\"hello\"\r\n, 2019]");
        EXPECT_TRUE(p->is_array());
        EXPECT_EQ(2, p->to_array()->count());
        srs_freep(p);
    }

    if (true) {
        SrsJsonAny* p = SrsJsonAny::loads("[\"hello\"\r\n, 2019, \"\\xe6\\xb5\\x81\"]");
        EXPECT_TRUE(p->is_array());
        EXPECT_EQ(3, p->to_array()->count());
        srs_freep(p);
    }
}

