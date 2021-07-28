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
    // epoll(). On BSD it will be kqueue. On Cygwin it will be select.
#if __CYGWIN__
    assert(st_set_eventsys(ST_EVENTSYS_SELECT) != -1);
#else
    assert(st_set_eventsys(ST_EVENTSYS_ALT) != -1);
#endif

    // Initialize state-threads, create idle coroutine.
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

void* pfn_coroutine(void* /*arg*/)
{
    st_usleep(0);
    return NULL;
}

VOID TEST(SampleTest, StartCoroutine)
{
    st_thread_t trd = st_thread_create(pfn_coroutine, NULL, 1, 0);
    EXPECT_TRUE(trd != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd, NULL);
}

VOID TEST(SampleTest, StartCoroutineX3)
{
    st_thread_t trd0 = st_thread_create(pfn_coroutine, NULL, 1, 0);
    st_thread_t trd1 = st_thread_create(pfn_coroutine, NULL, 1, 0);
    st_thread_t trd2 = st_thread_create(pfn_coroutine, NULL, 1, 0);
    EXPECT_TRUE(trd0 != NULL && trd1 != NULL && trd2 != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd1, NULL);
    st_thread_join(trd2, NULL);
    st_thread_join(trd0, NULL);
}

void* pfn_coroutine_add(void* arg)
{
    int v = 0;
    int* pi = (int*)arg;

    // Load the change of arg.
    while (v != *pi) {
        v = *pi;
        st_usleep(0);
    }

    // Add with const.
    v += 100;
    *pi = v;

    return NULL;
}

VOID TEST(SampleTest, StartCoroutineAdd)
{
    int v = 0;
    st_thread_t trd = st_thread_create(pfn_coroutine_add, &v, 1, 0);
    EXPECT_TRUE(trd != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd, NULL);

    EXPECT_EQ(100, v);
}

VOID TEST(SampleTest, StartCoroutineAddX3)
{
    int v = 0;
    st_thread_t trd0 = st_thread_create(pfn_coroutine_add, &v, 1, 0);
    st_thread_t trd1 = st_thread_create(pfn_coroutine_add, &v, 1, 0);
    st_thread_t trd2 = st_thread_create(pfn_coroutine_add, &v, 1, 0);
    EXPECT_TRUE(trd0 != NULL && trd1 != NULL && trd2 != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd0, NULL);
    st_thread_join(trd1, NULL);
    st_thread_join(trd2, NULL);

    EXPECT_EQ(300, v);
}

