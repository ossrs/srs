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

#ifndef SRS_UTEST_PUBLIC_SHARED_HPP
#define SRS_UTEST_PUBLIC_SHARED_HPP

// Public all private and protected members.
#define private public
#define protected public

/*
#include <srs_utest.hpp>
*/
#include <srs_core.hpp>

#include "gtest/gtest.h"
#include <string>
using namespace std;

#include <srs_app_log.hpp>
#include <srs_kernel_stream.hpp>

// we add an empty macro for upp to show the smart tips.
#define VOID

// Temporary disk config.
extern std::string _srs_tmp_file_prefix;
// Temporary network config.
extern std::string _srs_tmp_host;
extern int _srs_tmp_port;
extern srs_utime_t _srs_tmp_timeout;

// For errors.
#define HELPER_EXPECT_SUCCESS(x) \
    if ((err = x) != srs_success) fprintf(stderr, "err %s", srs_error_desc(err).c_str()); \
    EXPECT_TRUE(srs_success == err); \
    srs_freep(err)
#define HELPER_EXPECT_FAILED(x) EXPECT_TRUE(srs_success != (err = x)); srs_freep(err)

// For errors, assert.
// @remark The err is leak when error, but it's ok in utest.
#define HELPER_ASSERT_SUCCESS(x) \
    if ((err = x) != srs_success) fprintf(stderr, "err %s", srs_error_desc(err).c_str()); \
    ASSERT_TRUE(srs_success == err); \
    srs_freep(err)
#define HELPER_ASSERT_FAILED(x) ASSERT_TRUE(srs_success != (err = x)); srs_freep(err)

// For init array data.
#define HELPER_ARRAY_INIT(buf, sz, val) \
    memset(buf, val, sz)

// Dump simple stream to string.
#define HELPER_BUFFER2STR(io) \
    string((const char*)(io)->bytes(), (size_t)(io)->length())

// Covert uint8_t array to string.
#define HELPER_ARR2STR(arr, size) \
    string((char*)(arr), (int)size)

// the asserts of gtest:
//    * {ASSERT|EXPECT}_EQ(expected, actual): Tests that expected == actual
//    * {ASSERT|EXPECT}_NE(v1, v2):           Tests that v1 != v2
//    * {ASSERT|EXPECT}_LT(v1, v2):           Tests that v1 < v2
//    * {ASSERT|EXPECT}_LE(v1, v2):           Tests that v1 <= v2
//    * {ASSERT|EXPECT}_GT(v1, v2):           Tests that v1 > v2
//    * {ASSERT|EXPECT}_GE(v1, v2):           Tests that v1 >= v2
//    * {ASSERT|EXPECT}_STREQ(s1, s2):     Tests that s1 == s2
//    * {ASSERT|EXPECT}_STRNE(s1, s2):     Tests that s1 != s2
//    * {ASSERT|EXPECT}_STRCASEEQ(s1, s2): Tests that s1 == s2, ignoring case
//    * {ASSERT|EXPECT}_STRCASENE(s1, s2): Tests that s1 != s2, ignoring case
//    * {ASSERT|EXPECT}_FLOAT_EQ(expected, actual): Tests that two float values are almost equal.
//    * {ASSERT|EXPECT}_DOUBLE_EQ(expected, actual): Tests that two double values are almost equal.
//    * {ASSERT|EXPECT}_NEAR(v1, v2, abs_error): Tests that v1 and v2 are within the given distance to each other.

// print the bytes.
void srs_bytes_print(char* pa, int size);

class MockEmptyLog : public SrsFileLog
{
public:
    MockEmptyLog(SrsLogLevel l);
    virtual ~MockEmptyLog();
};

#endif

