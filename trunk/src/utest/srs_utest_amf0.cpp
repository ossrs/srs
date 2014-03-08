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

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_stream.hpp>

VOID TEST(AMF0Test, Size) 
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
        SrsAutoFree(SrsAmf0Object, o, false);
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    // object: elem
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o, false);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o, false);
        
        size += SrsAmf0Size::utf8("age")+SrsAmf0Size::number();
        o->set("age", SrsAmf0Any::number(9));
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o, false);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::null();
        o->set("email", SrsAmf0Any::null());
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o, false);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::undefined();
        o->set("email", SrsAmf0Any::undefined());
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o, false);
        
        size += SrsAmf0Size::utf8("sex")+SrsAmf0Size::boolean();
        o->set("sex", SrsAmf0Any::boolean(true));
        
        EXPECT_EQ(size, SrsAmf0Size::object(o));
    }
    
    // array: empty
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    // array: elem
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o->set("name", SrsAmf0Any::str("winlin"));
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
        size += SrsAmf0Size::utf8("age")+SrsAmf0Size::number();
        o->set("age", SrsAmf0Any::number(9));
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::null();
        o->set("email", SrsAmf0Any::null());
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::undefined();
        o->set("email", SrsAmf0Any::undefined());
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray* o = SrsAmf0Any::ecma_array();
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
        size += SrsAmf0Size::utf8("sex")+SrsAmf0Size::boolean();
        o->set("sex", SrsAmf0Any::boolean(true));
        
        EXPECT_EQ(size, SrsAmf0Size::ecma_array(o));
    }
    
    // object: array
    if (true) {
        int size = 1+3;
        SrsAmf0Object* o = SrsAmf0Any::object();
        SrsAutoFree(SrsAmf0Object, o, false);
        
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
        SrsAutoFree(SrsAmf0Object, o, false);
        
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
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
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
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
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
        SrsAutoFree(SrsAmf0Object, o, false);
        
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
        SrsAutoFree(SrsAmf0EcmaArray, o, false);
        
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

VOID TEST(AMF0Test, AnyElem) 
{
	SrsAmf0Any* o = NULL;
	
	// string
	if (true) {
		o = SrsAmf0Any::str();
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_string());
		EXPECT_STREQ("", o->to_str().c_str());
	}
	if (true) {
		o = SrsAmf0Any::str("winlin");
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_string());
		EXPECT_STREQ("winlin", o->to_str().c_str());
	}
	
	// bool
	if (true) {
		o = SrsAmf0Any::boolean();
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_boolean());
		EXPECT_FALSE(o->to_boolean());
	}
	if (true) {
		o = SrsAmf0Any::boolean(false);
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_boolean());
		EXPECT_FALSE(o->to_boolean());
	}
	if (true) {
		o = SrsAmf0Any::boolean(true);
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_boolean());
		EXPECT_TRUE(o->to_boolean());
	}
	
	// number
	if (true) {
		o = SrsAmf0Any::number();
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_number());
		EXPECT_DOUBLE_EQ(0, o->to_number());
	}
	if (true) {
		o = SrsAmf0Any::number(100);
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_number());
		EXPECT_DOUBLE_EQ(100, o->to_number());
	}
	if (true) {
		o = SrsAmf0Any::number(-100);
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_number());
		EXPECT_DOUBLE_EQ(-100, o->to_number());
	}

	// null
	if (true) {
		o = SrsAmf0Any::null();
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_null());
	}

	// undefined
	if (true) {
		o = SrsAmf0Any::undefined();
		SrsAutoFree(SrsAmf0Any, o, false);
		ASSERT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_undefined());
	}
}

VOID TEST(AMF0Test, AnyIO) 
{
	SrsStream s;
	SrsAmf0Any* o = NULL;
	
	char buf[1024];
	memset(buf, 0, sizeof(buf));
	EXPECT_EQ(ERROR_SUCCESS, s.initialize(buf, sizeof(buf)));
	
	// object eof
	if (true) {
		s.reset();
		s.current()[2] = 0x09;
		
		o = SrsAmf0Any::object_eof();
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_EQ(3, s.pos());
		
		s.reset();
		s.current()[0] = 0x01;
		EXPECT_NE(ERROR_SUCCESS, o->read(&s));
	}
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::object_eof();
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_EQ(3, s.pos());
		
		s.skip(-3);
		EXPECT_EQ(0x09, s.read_3bytes());
	}
	
	// string
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::str("winlin");
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());
		
		s.reset();
		EXPECT_EQ(2, s.read_1bytes());
		EXPECT_EQ(6, s.read_2bytes());
		EXPECT_EQ('w', s.current()[0]);
		EXPECT_EQ('n', s.current()[5]);
		
		s.reset();
		s.current()[3] = 'x';
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_STREQ("xinlin", o->to_str().c_str());
	}
	
	// number
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::number(10);
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		EXPECT_EQ(0, s.read_1bytes());
		
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_DOUBLE_EQ(10, o->to_number());
	}
	
	// boolean
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::boolean(true);
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		EXPECT_EQ(1, s.read_1bytes());
		
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_TRUE(o->to_boolean());
	}
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::boolean(false);
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		EXPECT_EQ(1, s.read_1bytes());
		
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_FALSE(o->to_boolean());
	}
	
	// null
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::null();
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		EXPECT_EQ(5, s.read_1bytes());
		
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_TRUE(o->is_null());
	}
	
	// undefined
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::undefined();
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		EXPECT_EQ(6, s.read_1bytes());
		
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->read(&s));
		EXPECT_EQ(o->size(), s.pos());
		EXPECT_TRUE(o->is_undefined());
	}

	// any: string
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::str("winlin");
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		
		SrsAmf0Any* po = NULL;
		EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
		ASSERT_TRUE(NULL != po);
		SrsAutoFree(SrsAmf0Any, po, false);
		ASSERT_TRUE(po->is_string());
		EXPECT_STREQ("winlin", po->to_str().c_str());
	}

	// any: number
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::number(10);
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		
		SrsAmf0Any* po = NULL;
		EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
		ASSERT_TRUE(NULL != po);
		SrsAutoFree(SrsAmf0Any, po, false);
		ASSERT_TRUE(po->is_number());
		EXPECT_DOUBLE_EQ(10, po->to_number());
	}

	// any: boolean
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::boolean(true);
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		
		SrsAmf0Any* po = NULL;
		EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
		ASSERT_TRUE(NULL != po);
		SrsAutoFree(SrsAmf0Any, po, false);
		ASSERT_TRUE(po->is_boolean());
		EXPECT_TRUE(po->to_boolean());
	}

	// any: null
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::null();
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		
		SrsAmf0Any* po = NULL;
		EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
		ASSERT_TRUE(NULL != po);
		SrsAutoFree(SrsAmf0Any, po, false);
		ASSERT_TRUE(po->is_null());
	}

	// any: undefined
	if (true) {
		s.reset();
		
		o = SrsAmf0Any::undefined();
		SrsAutoFree(SrsAmf0Any, o, false);
		
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(o->size(), s.pos());

		s.reset();
		
		SrsAmf0Any* po = NULL;
		EXPECT_EQ(ERROR_SUCCESS, srs_amf0_read_any(&s, &po));
		ASSERT_TRUE(NULL != po);
		SrsAutoFree(SrsAmf0Any, po, false);
		ASSERT_TRUE(po->is_undefined());
	}
	
	// mixed any
	if (true) {
		s.reset();
		
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
		
		s.reset();
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

VOID TEST(AMF0Test, AnyAssert) 
{
	SrsStream s;
	SrsAmf0Any* o = NULL;
	
	char buf[1024];
	memset(buf, 0, sizeof(buf));
	EXPECT_EQ(ERROR_SUCCESS, s.initialize(buf, sizeof(buf)));
	
	// read any
	if (true) {
		s.reset();
		s.current()[0] = 0x12;
		EXPECT_NE(ERROR_SUCCESS, srs_amf0_read_any(&s, &o));
		EXPECT_TRUE(NULL == o);
		srs_freep(o);
	}
	
	// any convert
	if (true) {
		o = SrsAmf0Any::str();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_string());
	}
	if (true) {
		o = SrsAmf0Any::number();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_number());
	}
	if (true) {
		o = SrsAmf0Any::boolean();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_boolean());
	}
	if (true) {
		o = SrsAmf0Any::null();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_null());
	}
	if (true) {
		o = SrsAmf0Any::undefined();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_undefined());
	}
	if (true) {
		o = SrsAmf0Any::object();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_object());
	}
	if (true) {
		o = SrsAmf0Any::ecma_array();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(o->is_ecma_array());
	}
	
	// empty object
	if (true) {
		o = SrsAmf0Any::object();
		SrsAutoFree(SrsAmf0Any, o, false);
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(1+3, s.pos());
	}
	
	// empty ecma array
	if (true) {
		o = SrsAmf0Any::ecma_array();
		SrsAutoFree(SrsAmf0Any, o, false);
		s.reset();
		EXPECT_EQ(ERROR_SUCCESS, o->write(&s));
		EXPECT_EQ(1+4+3, s.pos());
	}
}

VOID TEST(AMF0Test, ObjectProps) 
{
	SrsAmf0Object* o = NULL;
	
	// get/set property
	if (true) {
		o = SrsAmf0Any::object();
		SrsAutoFree(SrsAmf0Object, o, false);
		
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
		SrsAutoFree(SrsAmf0Object, o, false);
		
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
		SrsAutoFree(SrsAmf0Object, o, false);
		
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
}
