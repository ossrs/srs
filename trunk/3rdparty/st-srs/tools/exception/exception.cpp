/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

/*
 * C++ exceptions on coroutine stacks, from a C++ program that links
 * obj/libst.a as SRS does. ST switches stacks with its own assembly, so the
 * unwinder walks frames on a stack the OS did not create. Linux and macOS
 * unwind with DWARF and need nothing from ST. Windows builds C++ exceptions on
 * SEH, whose dispatcher rejects every frame outside the stack bounds in the
 * TIB, so ST must switch those bounds with the stack, or a throw on a
 * coroutine stack ends the process with 0xE06D7363.
 *
 * The checks: a throw caught in a coroutine; unwinding that runs the
 * destructor of every frame in order; coroutines that switch before they
 * throw, on the default and a small stack; frames larger than a page across
 * most of a large stack; a destructor that switches while it is unwound; an
 * exception_ptr rethrown by another coroutine; and a throw in main while
 * coroutines wait deep in their own frames. On Windows, SEH itself: a
 * RaiseException and an access violation caught by __except, and a
 * __finally run by the unwind, in coroutines that switch.
 *
 * Never switch inside a catch block: the C++ runtime keeps one list of the
 * exceptions being handled per OS thread, so every check copies what it needs
 * out of the exception and leaves the catch block before it switches.
 */

#include "tool.h"

#include <exception>
#include <stdexcept>
#include <string>

/* Throw a runtime_error whose what() is v. */
static void throw_value(int v)
{
    throw std::runtime_error(std::to_string(v));
}

/* A coroutine's id, and the value it caught, or -1. */
struct Call {
    int id;
    int caught;
};

/* Throw the id after switches switches, and catch it. */
static void *switch_then_catch(void *arg)
{
    Call *call = (Call *)arg;
    int switches = call->id % 3;
    try {
        for (int i = 0; i < switches; i++) {
            st_usleep(0);
        }
        throw_value(call->id);
    } catch (const std::runtime_error &e) {
        call->caught = atoi(e.what());
    }
    return NULL;
}

/* A throw caught in the coroutine that threw it. */
static int catch_in_coroutine(void)
{
    Call call = {7, -1};
    st_thread_t t;
    CHECK((t = st_thread_create(switch_then_catch, &call, 1, 0)) != NULL);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(call.caught == 7);
    return 0;
}

/*
 * Unwinding runs the destructor of every frame, deepest first, after the
 * deepest frame switched to main and back.
 */
#define FRAMES_DEPTH 32

struct Frames {
    int destroyed;
    int ordered;
    int caught;
};

class FrameGuard {
public:
    FrameGuard(Frames *f, int depth) : f_(f), depth_(depth) {
    }
    ~FrameGuard() {
        if (depth_ != FRAMES_DEPTH - f_->destroyed) {
            f_->ordered = 0;
        }
        f_->destroyed++;
    }
private:
    Frames *f_;
    int depth_;
};

static void frames_recurse(Frames *f, int depth)
{
    FrameGuard guard(f, depth);
    if (depth < FRAMES_DEPTH) {
        frames_recurse(f, depth + 1);
        return;
    }
    st_usleep(0);
    throw_value(depth);
}

static void *frames_coroutine(void *arg)
{
    Frames *f = (Frames *)arg;
    try {
        frames_recurse(f, 1);
    } catch (const std::runtime_error &e) {
        f->caught = atoi(e.what());
    }
    return NULL;
}

static int unwind_frames(void)
{
    Frames f = {0, 1, -1};
    st_thread_t t;
    CHECK((t = st_thread_create(frames_coroutine, &f, 1, 0)) != NULL);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(f.caught == FRAMES_DEPTH);
    CHECK(f.destroyed == FRAMES_DEPTH);
    CHECK(f.ordered);
    return 0;
}

/*
 * Coroutines switch a different number of times before they throw, so the
 * throws interleave, and each catches its own value, on a stack of stk_size.
 */
#define INTERLEAVED 8

static int interleaved(int stk_size)
{
    Call calls[INTERLEAVED];
    st_thread_t t[INTERLEAVED];
    for (int i = 0; i < INTERLEAVED; i++) {
        calls[i].id = 100 + i;
        calls[i].caught = -1;
        CHECK((t[i] = st_thread_create(switch_then_catch, &calls[i], 1, stk_size)) != NULL);
    }
    for (int i = 0; i < INTERLEAVED; i++) {
        CHECK(st_thread_join(t[i], NULL) == 0);
        CHECK(calls[i].caught == 100 + i);
    }
    return 0;
}

/*
 * Frames larger than a page, which MSVC probes with __chkstk, down most of a
 * 1 MB stack in three coroutines, each switching at the bottom before it
 * throws. Each destructor checks that its buffer still holds what it wrote.
 */
#define LARGE_DEPTH 128
#define LARGE_FRAME 5000
#define LARGE_STACK (1024 * 1024)
#define LARGE_COROUTINES 3

struct Large {
    int id;
    int destroyed;
    int ordered;
    int intact;
    int caught;
};

class LargeGuard {
public:
    LargeGuard(Large *l, int depth) : l_(l), depth_(depth) {
        buf_[0] = buf_[LARGE_FRAME - 1] = (char)(l->id + depth);
    }
    ~LargeGuard() {
        if (depth_ != LARGE_DEPTH - l_->destroyed) {
            l_->ordered = 0;
        }
        if (buf_[0] != (char)(l_->id + depth_) || buf_[LARGE_FRAME - 1] != buf_[0]) {
            l_->intact = 0;
        }
        l_->destroyed++;
    }
private:
    Large *l_;
    int depth_;
    volatile char buf_[LARGE_FRAME];
};

static void large_recurse(Large *l, int depth)
{
    LargeGuard guard(l, depth);
    if (depth < LARGE_DEPTH) {
        large_recurse(l, depth + 1);
        return;
    }
    st_usleep(0);
    throw_value(l->id);
}

static void *large_coroutine(void *arg)
{
    Large *l = (Large *)arg;
    try {
        large_recurse(l, 1);
    } catch (const std::runtime_error &e) {
        l->caught = atoi(e.what());
    }
    return NULL;
}

static int large_frames(void)
{
    Large l[LARGE_COROUTINES];
    st_thread_t t[LARGE_COROUTINES];
    for (int i = 0; i < LARGE_COROUTINES; i++) {
        l[i].id = 10 + i;
        l[i].destroyed = 0;
        l[i].ordered = l[i].intact = 1;
        l[i].caught = -1;
        CHECK((t[i] = st_thread_create(large_coroutine, &l[i], 1, LARGE_STACK)) != NULL);
    }
    for (int i = 0; i < LARGE_COROUTINES; i++) {
        CHECK(st_thread_join(t[i], NULL) == 0);
        CHECK(l[i].caught == 10 + i);
        CHECK(l[i].destroyed == LARGE_DEPTH);
        CHECK(l[i].ordered && l[i].intact);
    }
    return 0;
}

/*
 * A destructor switches while its frame is unwound, and another coroutine
 * throws and catches before the unwind resumes.
 */
class YieldingGuard {
public:
    YieldingGuard(int *destroyed) : destroyed_(destroyed) {
    }
    ~YieldingGuard() {
        st_usleep(0);
        st_usleep(0);
        (*destroyed_)++;
    }
private:
    int *destroyed_;
};

struct Yielding {
    int destroyed;
    int caught;
};

static void throw_with_yielding_guard(Yielding *y)
{
    YieldingGuard guard(&y->destroyed);
    throw_value(300);
}

static void *yielding_coroutine(void *arg)
{
    Yielding *y = (Yielding *)arg;
    try {
        throw_with_yielding_guard(y);
    } catch (const std::runtime_error &e) {
        y->caught = atoi(e.what());
    }
    return NULL;
}

static int yield_while_unwinding(void)
{
    Yielding y = {0, -1};
    Call other = {301, -1};
    st_thread_t t, t2;
    CHECK((t = st_thread_create(yielding_coroutine, &y, 1, 0)) != NULL);
    CHECK((t2 = st_thread_create(switch_then_catch, &other, 1, 0)) != NULL);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(st_thread_join(t2, NULL) == 0);
    CHECK(y.destroyed == 1 && y.caught == 300);
    CHECK(other.caught == 301);
    return 0;
}

/*
 * One coroutine keeps the exception it caught as an exception_ptr, leaves the
 * catch block and switches; another rethrows it and catches the same value.
 */
static std::exception_ptr kept;

static void *keep_coroutine(void *arg)
{
    (void)arg;
    try {
        throw_value(400);
    } catch (...) {
        kept = std::current_exception();
    }
    st_usleep(0);
    return NULL;
}

static void *rethrow_coroutine(void *arg)
{
    Call *call = (Call *)arg;
    try {
        std::rethrow_exception(kept);
    } catch (const std::runtime_error &e) {
        call->caught = atoi(e.what());
    }
    return NULL;
}

static int exception_ptr_across(void)
{
    Call call = {0, -1};
    st_thread_t t, t2;
    CHECK((t = st_thread_create(keep_coroutine, NULL, 1, 0)) != NULL);
    st_usleep(0);
    CHECK(kept != NULL);
    CHECK((t2 = st_thread_create(rethrow_coroutine, &call, 1, 0)) != NULL);
    CHECK(st_thread_join(t2, NULL) == 0);
    CHECK(st_thread_join(t, NULL) == 0);
    CHECK(call.caught == 400);
    kept = NULL;
    return 0;
}

/*
 * main throws and catches on its own stack while coroutines wait deep in
 * their frames, then wakes them and they unwind theirs.
 */
#define PARKED 4

static st_cond_t parked_cond;

static void parked_recurse(Frames *f, int depth)
{
    FrameGuard guard(f, depth);
    if (depth < FRAMES_DEPTH) {
        parked_recurse(f, depth + 1);
        return;
    }
    st_cond_wait(parked_cond);
    throw_value(depth);
}

static void *parked_coroutine(void *arg)
{
    Frames *f = (Frames *)arg;
    try {
        parked_recurse(f, 1);
    } catch (const std::runtime_error &e) {
        f->caught = atoi(e.what());
    }
    return NULL;
}

static int main_while_parked(void)
{
    Frames f[PARKED];
    st_thread_t t[PARKED];
    CHECK((parked_cond = st_cond_new()) != NULL);
    for (int i = 0; i < PARKED; i++) {
        f[i].destroyed = 0;
        f[i].ordered = 1;
        f[i].caught = -1;
        CHECK((t[i] = st_thread_create(parked_coroutine, &f[i], 1, 0)) != NULL);
    }
    /* Every coroutine runs down to its st_cond_wait. */
    st_usleep(0);
    CHECK(f[0].destroyed == 0);

    int caught = -1;
    try {
        throw_value(500);
    } catch (const std::runtime_error &e) {
        caught = atoi(e.what());
    }
    CHECK(caught == 500);

    CHECK(st_cond_broadcast(parked_cond) == 0);
    for (int i = 0; i < PARKED; i++) {
        CHECK(st_thread_join(t[i], NULL) == 0);
        CHECK(f[i].caught == FRAMES_DEPTH);
        CHECK(f[i].destroyed == FRAMES_DEPTH && f[i].ordered);
    }
    CHECK(st_cond_destroy(parked_cond) == 0);
    return 0;
}

#if defined(_WIN32)
/*
 * SEH itself, which the C++ exceptions above are built on. These functions
 * hold no C++ objects, as __try requires.
 */
#define SEH_CODE 0xE0535431u

static int seh_filter(DWORD code, DWORD want)
{
    return code == want ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

/* RaiseException caught by __except, after the __finally between them ran. */
static DWORD seh_raise(int *finally_ran)
{
    DWORD caught = 0;
    __try {
        __try {
            RaiseException(SEH_CODE, 0, 0, NULL);
        } __finally {
            (*finally_ran)++;
        }
    } __except (seh_filter(GetExceptionCode(), SEH_CODE)) {
        caught = GetExceptionCode();
    }
    return caught;
}

/* An access violation, a hardware fault, caught by __except. */
static DWORD seh_fault(void)
{
    int *volatile p = NULL;
    DWORD caught = 0;
    __try {
        *p = 1;
    } __except (seh_filter(GetExceptionCode(), EXCEPTION_ACCESS_VIOLATION)) {
        caught = GetExceptionCode();
    }
    return caught;
}

struct Seh {
    int id;
    int finally_ran;
    DWORD raised;
    DWORD faulted;
};

static void *seh_coroutine(void *arg)
{
    Seh *s = (Seh *)arg;
    for (int i = 0; i < s->id % 3; i++) {
        st_usleep(0);
    }
    s->raised = seh_raise(&s->finally_ran);
    st_usleep(0);
    s->faulted = seh_fault();
    return NULL;
}

/* Coroutines that switch between their SEH exceptions, on the default and a small stack. */
static int seh(void)
{
    Seh s[INTERLEAVED];
    st_thread_t t[INTERLEAVED];
    for (int i = 0; i < INTERLEAVED; i++) {
        memset(&s[i], 0, sizeof(s[i]));
        s[i].id = i;
        CHECK((t[i] = st_thread_create(seh_coroutine, &s[i], 1, i % 2 ? 32 * 1024 : 0)) != NULL);
    }
    for (int i = 0; i < INTERLEAVED; i++) {
        CHECK(st_thread_join(t[i], NULL) == 0);
        CHECK(s[i].raised == SEH_CODE && s[i].finally_ran == 1);
        CHECK(s[i].faulted == EXCEPTION_ACCESS_VIOLATION);
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    CHECK(tool_init() == 0);

    CHECK(catch_in_coroutine() == 0);
    CHECK(unwind_frames() == 0);
    CHECK(interleaved(0) == 0);
    CHECK(interleaved(32 * 1024) == 0);
    CHECK(large_frames() == 0);
    CHECK(yield_while_unwinding() == 0);
    CHECK(exception_ptr_across() == 0);
    CHECK(main_while_parked() == 0);
#if defined(_WIN32)
    CHECK(seh() == 0);
#endif

    printf("exception OK\n");
    return 0;
}
