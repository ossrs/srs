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
        SrsAmf0Object o;
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    // object: elem
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("age")+SrsAmf0Size::number();
        o.set("age", SrsAmf0Any::number(9));
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::null();
        o.set("email", SrsAmf0Any::null());
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::undefined();
        o.set("email", SrsAmf0Any::undefined());
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("sex")+SrsAmf0Size::boolean();
        o.set("sex", SrsAmf0Any::boolean(true));
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    
    // array: empty
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    // array: elem
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("age")+SrsAmf0Size::number();
        o.set("age", SrsAmf0Any::number(9));
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::null();
        o.set("email", SrsAmf0Any::null());
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("email")+SrsAmf0Size::undefined();
        o.set("email", SrsAmf0Any::undefined());
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("sex")+SrsAmf0Size::boolean();
        o.set("sex", SrsAmf0Any::boolean(true));
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    
    // object: array
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0EcmaArray* args = new SrsAmf0EcmaArray();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::array(args);
        o.set("args", args);
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0EcmaArray* args = new SrsAmf0EcmaArray();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::array(args);
        o.set("args", args);
        
        SrsAmf0EcmaArray* params = new SrsAmf0EcmaArray();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::array(params);
        o.set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    
    // array: object
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0Object* args = new SrsAmf0Object();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::object(args);
        o.set("args", args);
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0Object* args = new SrsAmf0Object();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::object(args);
        o.set("args", args);
        
        SrsAmf0Object* params = new SrsAmf0Object();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::object(params);
        o.set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o));
    }
    
    // object: object
    if (true) {
        int size = 1+3;
        SrsAmf0Object o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0Object* args = new SrsAmf0Object();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::object(args);
        o.set("args", args);
        
        SrsAmf0Object* params = new SrsAmf0Object();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::object(params);
        o.set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::object(&o));
    }
    
    // array: array
    if (true) {
        int size = 1+4+3;
        SrsAmf0EcmaArray o;
        
        size += SrsAmf0Size::utf8("name")+SrsAmf0Size::str("winlin");
        o.set("name", SrsAmf0Any::str("winlin"));
        
        SrsAmf0EcmaArray* args = new SrsAmf0EcmaArray();
        args->set("p0", SrsAmf0Any::str("function"));
        size += SrsAmf0Size::utf8("args")+SrsAmf0Size::array(args);
        o.set("args", args);
        
        SrsAmf0EcmaArray* params = new SrsAmf0EcmaArray();
        params->set("p1", SrsAmf0Any::number(10));
        size += SrsAmf0Size::utf8("params")+SrsAmf0Size::array(params);
        o.set("params", params);
        
        EXPECT_EQ(size, SrsAmf0Size::array(&o)); 
    }
}

VOID TEST(AMF0Test, AnyElem) 
{
	SrsAmf0Any* o = NULL;
	
	// string
	if (true) {
		o = SrsAmf0Any::str();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_string());
		EXPECT_STREQ("", o->to_str().c_str());
	}
	if (true) {
		o = SrsAmf0Any::str("winlin");
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_string());
		EXPECT_STREQ("winlin", o->to_str().c_str());
	}
	
	// bool
	if (true) {
		o = SrsAmf0Any::boolean();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_boolean());
		EXPECT_FALSE(o->to_boolean());
	}
	if (true) {
		o = SrsAmf0Any::boolean(false);
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_boolean());
		EXPECT_FALSE(o->to_boolean());
	}
	if (true) {
		o = SrsAmf0Any::boolean(true);
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_boolean());
		EXPECT_TRUE(o->to_boolean());
	}
	
	// number
	if (true) {
		o = SrsAmf0Any::number();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_number());
		EXPECT_DOUBLE_EQ(0, o->to_number());
	}
	if (true) {
		o = SrsAmf0Any::number(100);
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_number());
		EXPECT_DOUBLE_EQ(100, o->to_number());
	}
	if (true) {
		o = SrsAmf0Any::number(-100);
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_number());
		EXPECT_DOUBLE_EQ(-100, o->to_number());
	}

	// null
	if (true) {
		o = SrsAmf0Any::null();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_null());
	}

	// undefined
	if (true) {
		o = SrsAmf0Any::undefined();
		SrsAutoFree(SrsAmf0Any, o, false);
		EXPECT_TRUE(NULL != o);
		EXPECT_TRUE(o->is_undefined());
	}
}
