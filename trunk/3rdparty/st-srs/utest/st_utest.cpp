/*
The MIT License (MIT)

Copyright (c) 2021 Winlin

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

#include <st_utest.hpp>

#include <st.h>
#include <assert.h>

// We could do something in the main of utest.
// Copy from gtest-1.6.0/src/gtest_main.cc
GTEST_API_ int main(int argc, char **argv) {
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
    assert(st_set_eventsys(ST_EVENTSYS_ALT) != -1);
    assert(st_init() == 0);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// basic test and samples.
VOID TEST(SampleTest, FastSampleInt64Test)
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

