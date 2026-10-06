/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>

#include <stdexcept>
#include <string>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for C++ exceptions on coroutine stacks. ST switches stacks with its own assembly, so the unwinder walks a
// coroutine's frames on a stack the OS didn't create. This works with the DWARF unwinder on Linux and macOS. Windows
// implements C++ exceptions with SEH, which checks every frame against the stack bounds the OS records for the thread,
// so a port must update those bounds on each switch or a throw on a coroutine stack crashes.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The value a coroutine threw and caught, or the error when it didn't catch the expected value.
struct ExceptionTestCall {
    int id_;
    int caught_;
    std::string what_;
    ExceptionTestCall() : id_(0), caught_(-1) {
    }
};

[[noreturn]] static void exception_test_throw(int v)
{
    throw std::runtime_error(std::to_string(v));
}

static void* exception_test_catch_coroutine(void* arg)
{
    ExceptionTestCall* call = (ExceptionTestCall*)arg;
    try {
        exception_test_throw(call->id_);
    } catch (const std::runtime_error& e) {
        call->what_ = e.what();
        call->caught_ = call->id_;
    }
    return NULL;
}

VOID TEST(ExceptionTest, CatchInCoroutine)
{
    ExceptionTestCall call;
    call.id_ = 7;
    st_thread_t trd = st_thread_create(exception_test_catch_coroutine, &call, 1, 0);
    ASSERT_TRUE(trd != NULL);
    st_thread_join(trd, NULL);

    EXPECT_EQ(7, call.caught_);
    EXPECT_STREQ("7", call.what_.c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Unwinding through many frames runs the destructor of every object in them, in reverse order of construction.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXCEPTION_TEST_DEPTH 32

struct ExceptionTestFrames {
    int destroyed_;
    bool ordered_;
    bool caught_;
    ExceptionTestFrames() : destroyed_(0), ordered_(true), caught_(false) {
    }
};

class ExceptionTestGuard {
    ExceptionTestFrames* frames_;
    int depth_;
public:
    ExceptionTestGuard(ExceptionTestFrames* frames, int depth) : frames_(frames), depth_(depth) {
    }
    ~ExceptionTestGuard() {
        // The deepest frame unwinds first, so depth counts down from EXCEPTION_TEST_DEPTH to 1.
        if (depth_ != EXCEPTION_TEST_DEPTH - frames_->destroyed_) frames_->ordered_ = false;
        frames_->destroyed_++;
    }
};

static void exception_test_recurse(ExceptionTestFrames* frames, int depth)
{
    ExceptionTestGuard guard(frames, depth);
    if (depth == EXCEPTION_TEST_DEPTH) exception_test_throw(depth);
    exception_test_recurse(frames, depth + 1);
}

static void* exception_test_frames_coroutine(void* arg)
{
    ExceptionTestFrames* frames = (ExceptionTestFrames*)arg;
    try {
        exception_test_recurse(frames, 1);
    } catch (const std::runtime_error&) {
        frames->caught_ = true;
    }
    return NULL;
}

VOID TEST(ExceptionTest, UnwindRunsDestructorsOfEveryFrame)
{
    ExceptionTestFrames frames;
    st_thread_t trd = st_thread_create(exception_test_frames_coroutine, &frames, 1, 0);
    ASSERT_TRUE(trd != NULL);
    st_thread_join(trd, NULL);

    EXPECT_TRUE(frames.caught_);
    EXPECT_EQ(EXCEPTION_TEST_DEPTH, frames.destroyed_);
    EXPECT_TRUE(frames.ordered_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Coroutines that switch before they throw each catch their own exception, on a small stack as on the default one.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXCEPTION_TEST_COROUTINES 8

static void exception_test_switch_then_throw(int v, int switches)
{
    for (int i = 0; i < switches; i++) {
        st_usleep(0);
    }
    exception_test_throw(v);
}

static void* exception_test_switch_coroutine(void* arg)
{
    ExceptionTestCall* call = (ExceptionTestCall*)arg;
    try {
        // Each coroutine switches a different number of times, so the throws interleave with the other coroutines.
        exception_test_switch_then_throw(call->id_, call->id_ % 3 + 1);
    } catch (const std::runtime_error& e) {
        call->what_ = e.what();
        call->caught_ = std::stoi(call->what_);
    }
    return NULL;
}

static void exception_test_switch_coroutines(int stk_size)
{
    ExceptionTestCall calls[EXCEPTION_TEST_COROUTINES];
    st_thread_t trds[EXCEPTION_TEST_COROUTINES];
    for (int i = 0; i < EXCEPTION_TEST_COROUTINES; i++) {
        calls[i].id_ = 100 + i;
        trds[i] = st_thread_create(exception_test_switch_coroutine, &calls[i], 1, stk_size);
        ASSERT_TRUE(trds[i] != NULL);
    }
    for (int i = 0; i < EXCEPTION_TEST_COROUTINES; i++) {
        st_thread_join(trds[i], NULL);
    }

    for (int i = 0; i < EXCEPTION_TEST_COROUTINES; i++) {
        EXPECT_EQ(100 + i, calls[i].caught_);
    }
}

VOID TEST(ExceptionTest, CoroutinesSwitchThenCatchTheirOwn)
{
    exception_test_switch_coroutines(0);
}

VOID TEST(ExceptionTest, CoroutinesOnSmallStacksCatchTheirOwn)
{
    exception_test_switch_coroutines(32 * 1024);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A limitation: never switch inside a catch block. The C++ runtime keeps one list of the exceptions being handled for
// each OS thread, and ST runs every coroutine on one OS thread, so the list assumes catch blocks end in the reverse
// order they began. When coroutines switch inside their catch blocks and resume in another order, leaving a catch block
// pops and frees the exception another coroutine is still handling: that coroutine's reference dangles, and `throw;` or
// std::current_exception() return the wrong exception. Both libc++abi and libstdc++ do this, so it can't be tested as a
// behavior, only avoided: copy what the handler needs out of the exception, leave the catch block, then switch.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* exception_test_switch_after_catch_coroutine(void* arg)
{
    ExceptionTestCall* call = (ExceptionTestCall*)arg;
    std::string what;
    try {
        exception_test_throw(call->id_);
    } catch (const std::runtime_error& e) {
        what = e.what();
    }

    st_usleep(0);
    st_usleep(0);
    call->what_ = what;
    call->caught_ = std::stoi(what);
    return NULL;
}

VOID TEST(ExceptionTest, CopyThenSwitchAfterCatch)
{
    ExceptionTestCall calls[EXCEPTION_TEST_COROUTINES];
    st_thread_t trds[EXCEPTION_TEST_COROUTINES];
    for (int i = 0; i < EXCEPTION_TEST_COROUTINES; i++) {
        calls[i].id_ = 200 + i;
        trds[i] = st_thread_create(exception_test_switch_after_catch_coroutine, &calls[i], 1, 0);
        ASSERT_TRUE(trds[i] != NULL);
    }
    for (int i = 0; i < EXCEPTION_TEST_COROUTINES; i++) {
        st_thread_join(trds[i], NULL);
    }

    for (int i = 0; i < EXCEPTION_TEST_COROUTINES; i++) {
        EXPECT_EQ(200 + i, calls[i].caught_);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A destructor may switch while its frame is being unwound, and other coroutines may throw and catch meanwhile.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class ExceptionTestYieldingGuard {
    int* destroyed_;
public:
    ExceptionTestYieldingGuard(int* destroyed) : destroyed_(destroyed) {
    }
    ~ExceptionTestYieldingGuard() {
        st_usleep(0);
        st_usleep(0);
        (*destroyed_)++;
    }
};

struct ExceptionTestUnwinding {
    int destroyed_;
    ExceptionTestCall call_;
    ExceptionTestUnwinding() : destroyed_(0) {
    }
};

static void exception_test_throw_with_yielding_guard(ExceptionTestUnwinding* u)
{
    ExceptionTestYieldingGuard guard(&u->destroyed_);
    exception_test_throw(u->call_.id_);
}

static void* exception_test_unwinding_coroutine(void* arg)
{
    ExceptionTestUnwinding* u = (ExceptionTestUnwinding*)arg;
    try {
        exception_test_throw_with_yielding_guard(u);
    } catch (const std::runtime_error& e) {
        u->call_.what_ = e.what();
        u->call_.caught_ = std::stoi(u->call_.what_);
    }
    return NULL;
}

VOID TEST(ExceptionTest, YieldInDestructorWhileUnwinding)
{
    ExceptionTestUnwinding u;
    u.call_.id_ = 300;
    ExceptionTestCall other;
    other.id_ = 301;

    // The unwinding coroutine switches out of its destructor, then the other one throws and catches before it resumes.
    st_thread_t trd = st_thread_create(exception_test_unwinding_coroutine, &u, 1, 0);
    st_thread_t trd2 = st_thread_create(exception_test_catch_coroutine, &other, 1, 0);
    ASSERT_TRUE(trd != NULL && trd2 != NULL);
    st_thread_join(trd, NULL);
    st_thread_join(trd2, NULL);

    EXPECT_EQ(1, u.destroyed_);
    EXPECT_EQ(300, u.call_.caught_);
    EXPECT_EQ(301, other.caught_);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A throw from the bottom of a deep recursion unwinds frames larger than a page across most of a large stack, while
// other coroutines do the same on their own stacks, so each one runs every destructor of its own frames in order.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define EXCEPTION_TEST_DEEP_DEPTH 128
#define EXCEPTION_TEST_DEEP_FRAME 5000

struct ExceptionTestDeep {
    int id_;
    int destroyed_;
    bool ordered_;
    int caught_;
    ExceptionTestDeep() : id_(0), destroyed_(0), ordered_(true), caught_(-1) {
    }
};

class ExceptionTestDeepGuard {
    ExceptionTestDeep* deep_;
    int depth_;
    volatile char buf_[EXCEPTION_TEST_DEEP_FRAME];
public:
    ExceptionTestDeepGuard(ExceptionTestDeep* deep, int depth) : deep_(deep), depth_(depth) {
        buf_[0] = buf_[EXCEPTION_TEST_DEEP_FRAME - 1] = (char)(deep->id_ + depth);
    }
    ~ExceptionTestDeepGuard() {
        // The deepest frame unwinds first, and its buffer is still the one it wrote.
        if (depth_ != EXCEPTION_TEST_DEEP_DEPTH - deep_->destroyed_) deep_->ordered_ = false;
        if (buf_[0] != (char)(deep_->id_ + depth_) || buf_[EXCEPTION_TEST_DEEP_FRAME - 1] != buf_[0]) {
            deep_->ordered_ = false;
        }
        deep_->destroyed_++;
    }
};

static void exception_test_deep_recurse(ExceptionTestDeep* deep, int depth)
{
    ExceptionTestDeepGuard guard(deep, depth);
    if (depth == EXCEPTION_TEST_DEEP_DEPTH) {
        st_usleep(0);
        exception_test_throw(deep->id_);
    }
    exception_test_deep_recurse(deep, depth + 1);
}

static void* exception_test_deep_coroutine(void* arg)
{
    ExceptionTestDeep* deep = (ExceptionTestDeep*)arg;
    try {
        exception_test_deep_recurse(deep, 1);
    } catch (const std::runtime_error& e) {
        deep->caught_ = std::stoi(e.what());
    }
    return NULL;
}

VOID TEST(ExceptionTest, ThrowFromDeepRecursionWithLargeFrames)
{
    ExceptionTestDeep deeps[3];
    st_thread_t trds[3];
    for (int i = 0; i < 3; i++) {
        deeps[i].id_ = 400 + i;
        // About 640 KB of a 1 MB stack.
        trds[i] = st_thread_create(exception_test_deep_coroutine, &deeps[i], 1, 1024 * 1024);
        ASSERT_TRUE(trds[i] != NULL);
    }
    for (int i = 0; i < 3; i++) {
        st_thread_join(trds[i], NULL);
    }

    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(400 + i, deeps[i].caught_) << "coroutine " << i;
        EXPECT_EQ(EXCEPTION_TEST_DEEP_DEPTH, deeps[i].destroyed_) << "coroutine " << i;
        EXPECT_TRUE(deeps[i].ordered_) << "coroutine " << i;
    }
}
