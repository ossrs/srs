/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

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

#include <srs_utest.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_buffer.hpp>
using namespace _srs_internal;

#include <string>
using namespace std;

// Temporary disk config.
std::string _srs_tmp_file_prefix = "/tmp/srs-utest-";
// Temporary network config.
std::string _srs_tmp_host = "127.0.0.1";
int _srs_tmp_port = 11935;
srs_utime_t _srs_tmp_timeout = (100 * SRS_UTIME_MILLISECONDS);

// kernel module.
ISrsLog* _srs_log = new MockEmptyLog(SrsLogLevelDisabled);
ISrsThreadContext* _srs_context = new ISrsThreadContext();
// app module.
SrsConfig* _srs_config = NULL;
SrsServer* _srs_server = NULL;

// Disable coroutine test for OSX.
#if !defined(SRS_OSX)
#include <srs_app_st.hpp>
#endif

// Initialize global settings.
srs_error_t prepare_main() {
    srs_error_t err = srs_success;

    #if !defined(SRS_OSX)
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "init st");
    }

    srs_freep(_srs_context);
    _srs_context = new SrsThreadContext();
    #endif

    return err;
}

// We could do something in the main of utest.
// Copy from gtest-1.6.0/src/gtest_main.cc
GTEST_API_ int main(int argc, char **argv) {
    srs_error_t err = srs_success;

    if ((err = prepare_main()) != srs_success) {
        fprintf(stderr, "Failed, %s\n", srs_error_desc(err).c_str());

        int ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

MockEmptyLog::MockEmptyLog(SrsLogLevel l)
{
    level = l;
}

MockEmptyLog::~MockEmptyLog()
{
}

void srs_bytes_print(char* pa, int size)
{
    for(int i = 0; i < size; i++) {
        char v = pa[i];
        printf("%#x ", v);
    }
    printf("\n");
}

// basic test and samples.
VOID TEST(SampleTest, FastSampleInt64Test) 
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

VOID TEST(SampleTest, FastSampleMacrosTest) 
{
    EXPECT_TRUE(1);
    EXPECT_FALSE(0);
    
    EXPECT_EQ(1, 1); // ==
    EXPECT_NE(1, 2); // !=
    EXPECT_LE(1, 2); // <=
    EXPECT_LT(1, 2); // <
    EXPECT_GE(2, 1); // >=
    EXPECT_GT(2, 1); // >

    EXPECT_STREQ("winlin", "winlin");
    EXPECT_STRNE("winlin", "srs");
    EXPECT_STRCASEEQ("winlin", "Winlin");
    EXPECT_STRCASENE("winlin", "srs");
    
    EXPECT_FLOAT_EQ(1.0, 1.000000000000001);
    EXPECT_DOUBLE_EQ(1.0, 1.0000000000000001);
    EXPECT_NEAR(10, 15, 5);
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

