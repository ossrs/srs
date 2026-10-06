/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <stdint.h>

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for live values across a switch. A compiler keeps loop state in callee-saved registers across a call, so a
// context switch that misses one of them, such as rdi, rsi or xmm6-xmm15 on Windows x64, corrupts the coroutine that
// resumes. The utest is built without optimization, which keeps locals in memory, so the function that holds the values
// is optimized by itself; clang has no such attribute and checks the values in memory only.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER)
#define COROUTINE_TEST_OPTIMIZE
#elif defined(__GNUC__) && !defined(__clang__)
#define COROUTINE_TEST_OPTIMIZE __attribute__((optimize("O2")))
#else
#define COROUTINE_TEST_OPTIMIZE
#endif

#define COROUTINE_TEST_LIVE_ROUNDS 100
#define COROUTINE_TEST_LIVE_COROUTINES 4

// Seven integers and six doubles, more than the volatile registers can hold across a call.
struct CoroutineTestLive {
    uint64_t seed_;
    uint64_t ints_;
    double doubles_;
    CoroutineTestLive() : seed_(0), ints_(0), doubles_(0) {
    }
};

static void coroutine_test_no_yield()
{
}

static void coroutine_test_yield()
{
    st_usleep(0);
}

#if defined(_MSC_VER)
#pragma optimize("gt", on)
#endif
static COROUTINE_TEST_OPTIMIZE void coroutine_test_live_values(CoroutineTestLive* live, void (*yield)())
{
    uint64_t a = live->seed_, b = a + 1, c = a + 2, d = a + 3, e = a + 5, f = a + 7, g = a + 11;
    double x = (double)a, y = x + 0.5, z = x + 1.5, u = x + 2.5, v = x + 3.5, w = x + 4.5;
    for (int i = 0; i < COROUTINE_TEST_LIVE_ROUNDS; i++) {
        yield();
        a = a * 3 + b; b = b * 5 + c; c = c * 7 + d; d = d * 11 + e; e = e * 13 + f; f = f * 17 + g; g = g * 19 + a;
        x = x * 0.5 + y; y = y * 0.25 + z; z = z * 0.5 + u; u = u * 0.25 + v; v = v * 0.5 + w; w = w * 0.25 + x + i;
    }
    live->ints_ = a ^ b ^ c ^ d ^ e ^ f ^ g;
    live->doubles_ = x + y + z + u + v + w;
}
#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

static void* coroutine_test_live_coroutine(void* arg)
{
    coroutine_test_live_values((CoroutineTestLive*)arg, coroutine_test_yield);
    return NULL;
}

VOID TEST(CoroutineTest, LiveValuesSurviveSwitches)
{
    // Every coroutine holds different values, so a register another coroutine left behind is caught.
    CoroutineTestLive lives[COROUTINE_TEST_LIVE_COROUTINES];
    st_thread_t trds[COROUTINE_TEST_LIVE_COROUTINES];
    for (int i = 0; i < COROUTINE_TEST_LIVE_COROUTINES; i++) {
        lives[i].seed_ = 1000 * (i + 1);
        trds[i] = st_thread_create(coroutine_test_live_coroutine, &lives[i], 1, 0);
        ASSERT_TRUE(trds[i] != NULL);
    }
    for (int i = 0; i < COROUTINE_TEST_LIVE_COROUTINES; i++) {
        st_thread_join(trds[i], NULL);
    }

    // The same computation without switches gives the expected values.
    for (int i = 0; i < COROUTINE_TEST_LIVE_COROUTINES; i++) {
        CoroutineTestLive expected;
        expected.seed_ = lives[i].seed_;
        coroutine_test_live_values(&expected, coroutine_test_no_yield);
        EXPECT_EQ(expected.ints_, lives[i].ints_) << "coroutine " << i;
        EXPECT_EQ(expected.doubles_, lives[i].doubles_) << "coroutine " << i;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for deep recursion on a coroutine stack. Every frame fills a buffer, the deepest frame switches to the other
// coroutines, and every frame checks its buffer on the way back. A frame larger than a page makes Windows probe the
// stack page by page against the stack bounds of the coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define COROUTINE_TEST_RECURSE_COROUTINES 3

struct CoroutineTestRecurse {
    int id_;
    int depth_;
    int frames_;
    int corrupted_;
    CoroutineTestRecurse() : id_(0), depth_(0), frames_(0), corrupted_(0) {
    }
};

template <int SIZE>
static void coroutine_test_recurse(CoroutineTestRecurse* r, int depth)
{
    volatile unsigned char buf[SIZE];
    unsigned char v = (unsigned char)(r->id_ * 31 + depth);
    for (int i = 0; i < SIZE; i++) {
        buf[i] = (unsigned char)(v + i);
    }

    if (depth < r->depth_) {
        coroutine_test_recurse<SIZE>(r, depth + 1);
    } else {
        st_usleep(0);
        st_usleep(0);
    }

    for (int i = 0; i < SIZE; i++) {
        if (buf[i] != (unsigned char)(v + i)) {
            r->corrupted_++;
            break;
        }
    }
    r->frames_++;
}

template <int SIZE>
static void* coroutine_test_recurse_coroutine(void* arg)
{
    CoroutineTestRecurse* r = (CoroutineTestRecurse*)arg;
    coroutine_test_recurse<SIZE>(r, 1);
    return NULL;
}

template <int SIZE>
static void coroutine_test_recurse_coroutines(int depth, int stk_size)
{
    CoroutineTestRecurse rs[COROUTINE_TEST_RECURSE_COROUTINES];
    st_thread_t trds[COROUTINE_TEST_RECURSE_COROUTINES];
    for (int i = 0; i < COROUTINE_TEST_RECURSE_COROUTINES; i++) {
        rs[i].id_ = i + 1;
        rs[i].depth_ = depth;
        trds[i] = st_thread_create(coroutine_test_recurse_coroutine<SIZE>, &rs[i], 1, stk_size);
        ASSERT_TRUE(trds[i] != NULL);
    }
    for (int i = 0; i < COROUTINE_TEST_RECURSE_COROUTINES; i++) {
        st_thread_join(trds[i], NULL);
    }

    for (int i = 0; i < COROUTINE_TEST_RECURSE_COROUTINES; i++) {
        EXPECT_EQ(depth, rs[i].frames_) << "coroutine " << i;
        EXPECT_EQ(0, rs[i].corrupted_) << "coroutine " << i;
    }
}

VOID TEST(CoroutineTest, DeepRecursionOnDefaultStack)
{
    // About 70 KB of the default 128 KB stack.
    coroutine_test_recurse_coroutines<256>(200, 0);
}

VOID TEST(CoroutineTest, DeepRecursionWithLargeFramesOnLargeStack)
{
    // About 640 KB of a 1 MB stack, in frames larger than a page.
    coroutine_test_recurse_coroutines<5000>(128, 1024 * 1024);
}
