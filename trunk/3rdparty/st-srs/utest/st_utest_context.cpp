/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2025 The SRS Authors */

#include <st_utest.hpp>
#include <st.h>
#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests for context switching: verify that _st_md_cxt_save/_st_md_cxt_restore
// and st_thread_create's save-then-patch-SP trick actually work.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Test: a coroutine runs on a different stack than the primordial thread.
// This proves the SP-patching trick in st_thread_create works.
static void* coroutine_stack_addr(void* arg)
{
    int local_var = 42;
    // Write our stack variable address back to the caller.
    *(uintptr_t*)arg = (uintptr_t)&local_var;
    return NULL;
}

VOID TEST(ContextSwitchTest, CoroutineRunsOnSeparateStack)
{
    uintptr_t primordial_stack_addr = 0;
    uintptr_t coroutine_stack = 0;

    // Capture primordial thread's stack address.
    int local = 0;
    primordial_stack_addr = (uintptr_t)&local;

    st_thread_t trd = st_thread_create(coroutine_stack_addr, &coroutine_stack, 1, 0);
    ASSERT_TRUE(trd != NULL);
    st_thread_join(trd, NULL);

    ASSERT_NE((uintptr_t)0, coroutine_stack);

    // The coroutine's stack should be far from the primordial stack.
    // Default stack is 128KB, so the difference should be at least that much.
    uintptr_t diff = (primordial_stack_addr > coroutine_stack)
        ? (primordial_stack_addr - coroutine_stack)
        : (coroutine_stack - primordial_stack_addr);
    EXPECT_GT(diff, (uintptr_t)4096) << "Coroutine stack should be on a separate heap-allocated stack";
}

// Test: context switches preserve local state across yields.
// Each coroutine writes to its own local variable, yields, then checks the value.
// This proves save/restore of registers and stack is correct.
static void* coroutine_preserve_local(void* arg)
{
    int id = *(int*)arg;
    int local_state = id * 1000;

    // Yield to let other coroutines run.
    st_usleep(0);

    // After resume, local_state should be intact — it lives on our own stack.
    local_state += 1;

    // Write result back.
    *(int*)arg = local_state;
    return NULL;
}

VOID TEST(ContextSwitchTest, LocalStatePreservedAcrossYield)
{
    int a = 1, b = 2, c = 3;
    st_thread_t t1 = st_thread_create(coroutine_preserve_local, &a, 1, 0);
    st_thread_t t2 = st_thread_create(coroutine_preserve_local, &b, 1, 0);
    st_thread_t t3 = st_thread_create(coroutine_preserve_local, &c, 1, 0);
    ASSERT_TRUE(t1 && t2 && t3);

    st_thread_join(t1, NULL);
    st_thread_join(t2, NULL);
    st_thread_join(t3, NULL);

    // Each coroutine computed: id * 1000 + 1
    EXPECT_EQ(1001, a);
    EXPECT_EQ(2001, b);
    EXPECT_EQ(3001, c);
}

// Test: context switching via st_thread_yield works correctly.
// We create coroutines that increment a shared counter in a deterministic order
// using yields. This verifies the scheduler + context switch round-trips.
static int g_counter = 0;

static void* coroutine_yield_order(void* arg)
{
    int my_order = *(int*)arg;

    // Wait until it's our turn.
    while (g_counter < my_order) {
        st_usleep(0);
    }

    // It's our turn — increment.
    g_counter++;
    return NULL;
}

VOID TEST(ContextSwitchTest, YieldOrderPreserved)
{
    g_counter = 0;

    int order0 = 0, order1 = 1, order2 = 2;
    st_thread_t t0 = st_thread_create(coroutine_yield_order, &order0, 1, 0);
    st_thread_t t1 = st_thread_create(coroutine_yield_order, &order1, 1, 0);
    st_thread_t t2 = st_thread_create(coroutine_yield_order, &order2, 1, 0);
    ASSERT_TRUE(t0 && t1 && t2);

    st_thread_join(t0, NULL);
    st_thread_join(t1, NULL);
    st_thread_join(t2, NULL);

    EXPECT_EQ(3, g_counter);
}

// Test: return value from coroutine is correctly passed through st_thread_join.
// This proves the full lifecycle: create (save+patch SP) → schedule (restore) →
// run → exit (save retval) → join (read retval).
static void* coroutine_retval(void* arg)
{
    int input = *(int*)arg;
    st_usleep(0);
    // Return a computed value as void*.
    return (void*)(intptr_t)(input * input);
}

VOID TEST(ContextSwitchTest, ReturnValueThroughJoin)
{
    int input = 7;
    st_thread_t trd = st_thread_create(coroutine_retval, &input, 1, 0);
    ASSERT_TRUE(trd != NULL);

    void* retval = NULL;
    st_thread_join(trd, &retval);

    EXPECT_EQ(49, (int)(intptr_t)retval);
}

// Test: start routine must NOT execute inline in st_thread_create.
// It should run only after scheduler handoff.
static int g_create_started = 0;

static void* coroutine_mark_started(void* /*arg*/)
{
    g_create_started++;
    return NULL;
}

VOID TEST(ContextSwitchTest, StartRoutineNotExecutedInline)
{
    g_create_started = 0;

    st_thread_t trd = st_thread_create(coroutine_mark_started, NULL, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Creator path: _st_md_cxt_save returns 0, so _st_thread_main is not run inline.
    EXPECT_EQ(0, g_create_started) << "Coroutine must not run inline inside st_thread_create";

    // After yielding, scheduler can run the created coroutine.
    st_usleep(0);
    EXPECT_EQ(1, g_create_started) << "Coroutine should run after scheduler handoff";

    st_thread_join(trd, NULL);
}

static void* coroutine_run_once(void* arg)
{
    int* runs = (int*)arg;
    (*runs)++;
    return NULL;
}

VOID TEST(ContextSwitchTest, JoinDrivesFirstRunWhenNoManualYield)
{
    int runs = 0;

    st_thread_t trd = st_thread_create(coroutine_run_once, &runs, 1, 0);
    ASSERT_TRUE(trd != NULL);

    // Still not run yet, because creator hasn't yielded.
    EXPECT_EQ(0, runs);

    // Join blocks current coroutine and hands control to scheduler.
    st_thread_join(trd, NULL);
    EXPECT_EQ(1, runs);
}
