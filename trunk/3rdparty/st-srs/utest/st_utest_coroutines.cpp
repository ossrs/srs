/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2022 Winlin */

#include <st_utest.hpp>

#include <st.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for empty coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* coroutine(void* /*arg*/)
{
    st_usleep(0);
    return NULL;
}

VOID TEST(CoroutineTest, StartCoroutine)
{
    st_thread_t trd = st_thread_create(coroutine, NULL, 1, 0);
    EXPECT_TRUE(trd != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd, NULL);
}

VOID TEST(CoroutineTest, StartCoroutineX3)
{
    st_thread_t trd0 = st_thread_create(coroutine, NULL, 1, 0);
    st_thread_t trd1 = st_thread_create(coroutine, NULL, 1, 0);
    st_thread_t trd2 = st_thread_create(coroutine, NULL, 1, 0);
    EXPECT_TRUE(trd0 != NULL && trd1 != NULL && trd2 != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd1, NULL);
    st_thread_join(trd2, NULL);
    st_thread_join(trd0, NULL);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for adding coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void* coroutine_add(void* arg)
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

VOID TEST(CoroutineTest, StartCoroutineAdd)
{
    int v = 0;
    st_thread_t trd = st_thread_create(coroutine_add, &v, 1, 0);
    EXPECT_TRUE(trd != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd, NULL);

    EXPECT_EQ(100, v);
}

VOID TEST(CoroutineTest, StartCoroutineAddX3)
{
    int v = 0;
    st_thread_t trd0 = st_thread_create(coroutine_add, &v, 1, 0);
    st_thread_t trd1 = st_thread_create(coroutine_add, &v, 1, 0);
    st_thread_t trd2 = st_thread_create(coroutine_add, &v, 1, 0);
    EXPECT_TRUE(trd0 != NULL && trd1 != NULL && trd2 != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd0, NULL);
    st_thread_join(trd1, NULL);
    st_thread_join(trd2, NULL);

    EXPECT_EQ(300, v);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for output params coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int coroutine_params_x4(int a, int b, int c, int d)
{
    int e = 0;

    st_usleep(0);

    e += a + b + c + d;
    e += 100;
    return e;
}

void* coroutine_params(void* arg)
{
    int r0 = coroutine_params_x4(1, 2, 3, 4);
    *(int*)arg = r0;
    return NULL;
}

VOID TEST(CoroutineTest, StartCoroutineParams)
{
    int r0 = 0;
    st_thread_t trd = st_thread_create(coroutine_params, &r0, 1, 0);
    EXPECT_TRUE(trd != NULL);

    // Wait for joinable coroutine to quit.
    st_thread_join(trd, NULL);

    EXPECT_EQ(110, r0);
}

