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
#include <srs_utest_amf0.hpp>

#include <string>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>

// user scenario: coding and decoding with amf0
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
        
        // use SrsStream to write props/info to binary buf.
        SrsStream s;
        EXPECT_EQ(ERROR_SUCCESS, s.initialize(bytes, nb_bytes));
        EXPECT_EQ(ERROR_SUCCESS, props->write(&s));
        EXPECT_EQ(ERROR_SUCCESS, info->write(&s));
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
        
        // use SrsStream to assist amf0 object to read from bytes.
        SrsStream s;
        EXPECT_EQ(ERROR_SUCCESS, s.initialize(bytes, nb_bytes));
        
        // decoding
        // if user know the schema, for instance, it's an amf0 object,
        // user can use specified object to decoding.
        SrsAmf0Object* props = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, props);
        EXPECT_EQ(ERROR_SUCCESS, props->read(&s));
        
        // user can use specified object to decoding.
        SrsAmf0Object* info = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, info);
        EXPECT_EQ(ERROR_SUCCESS, info->read(&s));
        
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
        
        // use SrsStream to assist amf0 object to read from bytes.
        SrsStream s;
        EXPECT_EQ(ERROR_SUCCESS, s.initialize(bytes, nb_bytes));
        
        // decoding a amf0 any, for user donot know
        SrsAmf0Any* any = NULL;
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &any));
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

VOID TEST(ProtocolAMF0Test, ApiAnyIO) 
{
    SrsStream s;
    SrsAmf0Any* o = NULL;
    
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ERROR_SUCCESS, s.initialize(buf, sizeof(buf)));
    
    // object eof
    if (true) {
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[2] = 0x09;
        
        o = SrsAmf0Any::object_eof();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_EQ(3, s.pos());
        
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[0] = 0x01;
        EXPECT_NE(ERROR_SUCCESS, o->read(&s));
    }
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::object_eof();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
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
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(2, s.read_1bytes());
        EXPECT_EQ(6, s.read_2bytes());
        EXPECT_EQ('w', (s.data() + s.pos())[0]);
        EXPECT_EQ('n', (s.data() + s.pos())[5]);
        
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[3] = 'x';
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_STREQ("xinlin", o->to_str().c_str());
    }
    
    // number
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::number(10);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(0, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_DOUBLE_EQ(10, o->to_number());
    }
    
    // boolean
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::boolean(true);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(1, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_TRUE(o->to_boolean());
    }
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::boolean(false);
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(1, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_FALSE(o->to_boolean());
    }
    
    // null
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::null();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(5, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_TRUE(o->is_null());
    }
    
    // undefined
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        EXPECT_EQ(6, s.read_1bytes());
        
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
        EXPECT_EQ(o->total_size(), s.pos());
        EXPECT_TRUE(o->is_undefined());
    }

    // any: string
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::str("winlin");
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
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
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
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
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
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
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_null());
    }

    // any: undefined
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::undefined();
        SrsAutoFree(SrsAmf0Any, o);
        
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(o->total_size(), s.pos());

        s.skip(-1 * s.pos());
        
        SrsAmf0Any* po = NULL;
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        SrsAutoFree(SrsAmf0Any, po);
        ASSERT_TRUE(po->is_undefined());
    }
    
    // mixed any
    if (true) {
        s.skip(-1 * s.pos());
        
        o = SrsAmf0Any::str("winlin");
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::number(10);
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::boolean(true);
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::null();
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        srs_freep(o);
        
        o = SrsAmf0Any::undefined();
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        srs_freep(o);
        
        s.skip(-1 * s.pos());
        SrsAmf0Any* po = NULL;
        
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_string());
        EXPECT_STREQ("winlin", po->to_str().c_str());
        srs_freep(po);
        
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_number());
        EXPECT_DOUBLE_EQ(10, po->to_number());
        srs_freep(po);
        
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_boolean());
        EXPECT_TRUE(po->to_boolean());
        srs_freep(po);
        
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_null());
        srs_freep(po);
        
        EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
        ASSERT_TRUE(NULL != po);
        ASSERT_TRUE(po->is_undefined());
        srs_freep(po);
    }
}

VOID TEST(ProtocolAMF0Test, ApiAnyAssert) 
{
    SrsStream s;
    SrsAmf0Any* o = NULL;
    
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ERROR_SUCCESS, s.initialize(buf, sizeof(buf)));
    
    // read any
    if (true) {
        s.skip(-1 * s.pos());
        (s.data() + s.pos())[0] = 0x12;
        EXPECT_NE(ERROR_SUCCESS, srs_amf0_read_any(&s, &o));
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
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(1+3, s.pos());
    }
    
    // empty ecma array
    if (true) {
        o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0Any, o);
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(1+4+3, s.pos());
    }
    
    // strict array
    if (true) {
        o = SrsAmf0Any::strict_array();
        SrsAutoFree(SrsAmf0Any, o);
        s.skip(-1 * s.pos());
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(1+4, s.pos());
    }
}

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

VOID TEST(ProtocolAMF0Test, ApiStrictArray)
{
    SrsStream s;
    
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(ERROR_SUCCESS, s.initialize(buf, sizeof(buf)));
    
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
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
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
        EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
        EXPECT_EQ(5 + SrsAmf0Size::number(), s.pos());
    }
}

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
