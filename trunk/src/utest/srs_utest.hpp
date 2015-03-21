/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

/*
#include <srs_utest.hpp>
*/
#include <srs_core.hpp>

#include "gtest/gtest.h"

#include <srs_app_log.hpp>

#define SRS_UTEST_DEV
#undef SRS_UTEST_DEV

// enable all utest.
#ifndef SRS_UTEST_DEV
    #define ENABLE_UTEST_AMF0
    #define ENABLE_UTEST_CONFIG
    #define ENABLE_UTEST_CORE
    #define ENABLE_UTEST_KERNEL
    #define ENABLE_UTEST_PROTOCOL
    #define ENABLE_UTEST_RELOAD
#endif

// disable some for fast dev, compile and startup.
#ifdef SRS_UTEST_DEV
    #undef ENABLE_UTEST_AMF0
    #undef ENABLE_UTEST_CONFIG
    #undef ENABLE_UTEST_CORE
    #undef ENABLE_UTEST_KERNEL
    #undef ENABLE_UTEST_PROTOCOL
    #undef ENABLE_UTEST_RELOAD
#endif

#ifdef SRS_UTEST_DEV
    #define ENABLE_UTEST_RELOAD
#endif

// we add an empty macro for upp to show the smart tips.
#define VOID

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

class MockEmptyLog : public SrsFastLog
{
public:
    MockEmptyLog(int level);
    virtual ~MockEmptyLog();
};

#endif

