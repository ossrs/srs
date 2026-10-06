/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <errno.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <stdint.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/syscall.h>
#endif
#include <vector>

#define ST_UTIME_MILLISECONDS 1000

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for randomized stacks, the exploit hardening of a server that parses untrusted input: with
// st_randomize_stacks(1), each new coroutine's stack starts at a random 16-byte-aligned offset within one extra page,
// so an attacker who overflows a buffer can't predict the addresses on the stack. Each stack has a guard page at each
// end in a DEBUG build, so a stack overflow faults at once instead of overwriting the memory next to it. SRS doesn't
// call st_randomize_stacks.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A coroutine stack is one mapping, from low to high addresses: a guard page, the stack, the random extra page when
// randomized, and a guard page. A coroutine starts near the top, so for a stack that isn't randomized, the top is the
// page boundary just above the frame of the coroutine's start function. The tests read the frame address, not the
// address of a local variable, because ASAN may move a local variable to a fake stack on the heap.
#define ST_STACK_TEST_SIZE (64 * 1024)

struct StackTestCoroutine {
    uintptr_t frame_;
    StackTestCoroutine() : frame_(0) {
    }
};

static void* stack_test_record_coroutine(void* arg)
{
    StackTestCoroutine* c = (StackTestCoroutine*)arg;
    c->frame_ = (uintptr_t)__builtin_frame_address(0);
    return NULL;
}

#ifndef _WIN32 // POSIX only: pipes and syscall
// Whether the byte at p can be read. write reports EFAULT for an unreadable page instead of faulting. It calls the
// system call directly, because ASAN checks the buffer of write, and p may be heap memory just past a stack that
// MALLOC_STACK allocated. macOS deprecates syscall, but it still works.
static bool stack_test_readable(uintptr_t p)
{
    int fds[2];
    if (pipe(fds) < 0) return true;
    bool readable = syscall(SYS_write, fds[1], (void*)p, 1) == 1;
    ::close(fds[0]);
    ::close(fds[1]);
    return readable;
}

// The top of a stack that isn't randomized, from the frame of its coroutine.
static uintptr_t stack_test_top(const StackTestCoroutine& c)
{
    uintptr_t pagesize = (uintptr_t)getpagesize();
    return (c.frame_ & ~(pagesize - 1)) + pagesize;
}
#endif

// Randomization is off by default, and each call returns the previous setting. Locks in current behavior.
VOID TEST(RandomizeStacksTest, SwitchReturnsPrevious)
{
    EXPECT_EQ(0, st_randomize_stacks(1));
    EXPECT_EQ(1, st_randomize_stacks(1));
    EXPECT_EQ(1, st_randomize_stacks(0));
    EXPECT_EQ(0, st_randomize_stacks(0));
}

// Without randomization every coroutine's stack starts at the same place within its page, so the frame of the same
// function sits at the same page offset in each one. With randomization on, new coroutines put it at different
// offsets, each shifted by a multiple of 16 bytes so the stack stays aligned, and each coroutine runs normally.
// Locks in current behavior.
VOID TEST(RandomizeStacksTest, StacksStartAtDifferentOffsets)
{
    const int n = 16;
    uintptr_t pagesize = (uintptr_t)getpagesize();

    // Off: the same page offset in every coroutine.
    uintptr_t plain = 0;
    for (int i = 0; i < n; i++) {
        StackTestCoroutine c;
        st_thread_t trd = st_thread_create(stack_test_record_coroutine, &c, 1, ST_STACK_TEST_SIZE);
        ASSERT_TRUE(trd != NULL);
        EXPECT_EQ(0, st_thread_join(trd, NULL));
        if (i == 0) plain = c.frame_ & (pagesize - 1);
        EXPECT_EQ(plain, c.frame_ & (pagesize - 1));
    }

    // On: different page offsets, each a multiple of 16 bytes away from the plain one. All n coroutines landing on one
    // offset by chance has a probability of at most (16 / 4096) ^ 15.
    st_randomize_stacks(1);
    int moved = 0;
    for (int i = 0; i < n; i++) {
        StackTestCoroutine c;
        st_thread_t trd = st_thread_create(stack_test_record_coroutine, &c, 1, ST_STACK_TEST_SIZE);
        ASSERT_TRUE(trd != NULL);
        EXPECT_EQ(0, st_thread_join(trd, NULL));
        uintptr_t offset = c.frame_ & (pagesize - 1);
        EXPECT_EQ(0u, (offset - plain) & 0xf);
        if (offset != plain) moved++;
    }
    st_randomize_stacks(0);
    EXPECT_GT(moved, 0);
}

#ifndef _WIN32 // POSIX only: mmap, mprotect interposition with dlsym, pipes
// A server turns randomization on while it runs, after some coroutines already exist. One of them exits, and the next
// coroutine created frees its stack. The coroutines still running must keep their guard pages, so an overflow of their
// stacks still faults. The stacks are mapped next to each other: each new one below the last on Linux, above it on
// macOS, so the page just past the end of the freed stack is the lower guard page of the coroutine created before it
// on Linux, or after it on macOS. The stacks are 2 MB, bigger than any hole earlier tests leave, so they are mapped
// side by side in fresh memory. If that page is free all the same, the test maps it with no access itself.
#define ST_STACK_TEST_BIG (2 * 1024 * 1024)

static void* stack_test_park_coroutine(void* arg)
{
    StackTestCoroutine* c = (StackTestCoroutine*)arg;
    c->frame_ = (uintptr_t)__builtin_frame_address(0);
    st_usleep(ST_UTIME_NO_TIMEOUT);
    return NULL;
}

VOID TEST(RandomizeStacksTest, FreeingAStackKeepsNeighbourGuard)
{
    uintptr_t pagesize = (uintptr_t)getpagesize();

    // Coroutines joined by earlier tests finish first, so creating the next coroutine frees their stacks, and the
    // create after the worker exits frees only the worker's.
    st_thread_yield();

    StackTestCoroutine before, c, after;
    st_thread_t parked0 = st_thread_create(stack_test_park_coroutine, &before, 1, ST_STACK_TEST_BIG);
    ASSERT_TRUE(parked0 != NULL);
    st_thread_t worker = st_thread_create(stack_test_record_coroutine, &c, 1, ST_STACK_TEST_BIG);
    ASSERT_TRUE(worker != NULL);
    st_thread_t parked1 = st_thread_create(stack_test_park_coroutine, &after, 1, ST_STACK_TEST_BIG);
    ASSERT_TRUE(parked1 != NULL);

    // The worker exits and is joined; the yield lets it run once more to put its stack on the free list.
    EXPECT_EQ(0, st_thread_join(worker, NULL));
    st_thread_yield();

    // The page past the worker's stack: its upper guard page ends at the top plus one page.
    uintptr_t neighbour = stack_test_top(c) + pagesize;
    void* held = mmap((void*)neighbour, pagesize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (held != MAP_FAILED && held != (void*)neighbour) {
        munmap(held, pagesize);
        held = MAP_FAILED;
    }
    if (held == MAP_FAILED && stack_test_readable(neighbour)) {
        st_thread_interrupt(parked0);
        st_thread_interrupt(parked1);
        EXPECT_EQ(0, st_thread_join(parked0, NULL));
        EXPECT_EQ(0, st_thread_join(parked1, NULL));
        GTEST_SKIP() << "the page past the stack is readable memory, such as the heap with MALLOC_STACK";
    }
    EXPECT_FALSE(stack_test_readable(neighbour));

    // Randomization goes on, and the next coroutine created frees the worker's stack.
    st_randomize_stacks(1);
    st_thread_t next = st_thread_create(stack_test_record_coroutine, &c, 1, 0);
    st_randomize_stacks(0);
    ASSERT_TRUE(next != NULL);
    EXPECT_EQ(0, st_thread_join(next, NULL));

    // The neighbour's guard page is still unreadable.
    EXPECT_FALSE(stack_test_readable(neighbour));

    if (held != MAP_FAILED) munmap(held, pagesize);
    st_thread_interrupt(parked0);
    st_thread_interrupt(parked1);
    EXPECT_EQ(0, st_thread_join(parked0, NULL));
    EXPECT_EQ(0, st_thread_join(parked1, NULL));
}

// Every mprotect call of this test binary goes through this one, which calls the real mprotect, and records the call
// while a test sets stack_test_recording, to see which pages ST protects and restores, and whether each call succeeds.
struct StackTestProtect {
    uintptr_t addr_;
    int prot_;
    int r0_;
};

static bool stack_test_recording = false;
static std::vector<StackTestProtect> stack_test_protects;

#ifndef __THROW
#define __THROW
#endif
extern "C" int mprotect(void* addr, size_t len, int prot) __THROW
{
    typedef int (*mprotect_fn)(void*, size_t, int);
    static mprotect_fn real = NULL;
    if (!real) real = (mprotect_fn)dlsym(RTLD_NEXT, "mprotect");

    int r0 = real(addr, len, prot);
    if (stack_test_recording) {
        StackTestProtect p;
        p.addr_ = (uintptr_t)addr;
        p.prot_ = prot;
        p.r0_ = r0;
        stack_test_protects.push_back(p);
    }
    return r0;
}

// The calls recorded with protection prot.
static std::vector<StackTestProtect> stack_test_protects_with(int prot)
{
    std::vector<StackTestProtect> v;
    for (size_t i = 0; i < stack_test_protects.size(); i++) {
        if (stack_test_protects[i].prot_ == prot) v.push_back(stack_test_protects[i]);
    }
    return v;
}

// Freeing a stack makes its two guard pages accessible again before it releases the memory, which matters when the
// memory goes back to malloc (MALLOC_STACK, as SRS builds ST) and will be reused for other data. The two pages it
// restores must be exactly the two pages its creation protected, and both calls must succeed, whether the stack was
// randomized or not, and whatever the setting is when it is freed: a server may turn randomization on or off at any
// time. A stack is freed when the next coroutine is created after it exited.
VOID TEST(RandomizeStacksTest, FreeingRestoresTheGuardPagesCreationProtected)
{
    // Coroutines joined by earlier tests finish first, so the stacks freed below are only the worker's.
    st_thread_yield();

    for (int created = 0; created < 2; created++) {
        for (int freed = 0; freed < 2; freed++) {
            // The worker's stack is created, which protects its two guard pages.
            st_randomize_stacks(created);
            stack_test_protects.clear();
            stack_test_recording = true;
            StackTestCoroutine c;
            st_thread_t worker = st_thread_create(stack_test_record_coroutine, &c, 1, ST_STACK_TEST_SIZE);
            stack_test_recording = false;
            ASSERT_TRUE(worker != NULL);
            std::vector<StackTestProtect> guards = stack_test_protects_with(PROT_NONE);
            ASSERT_EQ(2u, guards.size());
            if (guards[0].r0_ != 0 || guards[1].r0_ != 0) {
                st_randomize_stacks(0);
                EXPECT_EQ(0, st_thread_join(worker, NULL));
                GTEST_SKIP() << "the stack memory isn't page-aligned, such as malloc on Linux with MALLOC_STACK";
            }

            // The worker exits and is joined; the yield lets it run once more to put its stack on the free list.
            EXPECT_EQ(0, st_thread_join(worker, NULL));
            st_thread_yield();

            // The next coroutine is created with the setting at that time, which frees the worker's stack.
            st_randomize_stacks(freed);
            stack_test_protects.clear();
            stack_test_recording = true;
            st_thread_t next = st_thread_create(stack_test_record_coroutine, &c, 1, ST_STACK_TEST_SIZE);
            stack_test_recording = false;
            st_randomize_stacks(0);
            ASSERT_TRUE(next != NULL);
            EXPECT_EQ(0, st_thread_join(next, NULL));
            st_thread_yield();

            // The free restored exactly the two guard pages, and both calls succeeded.
            std::vector<StackTestProtect> restores = stack_test_protects_with(PROT_READ | PROT_WRITE);
            ASSERT_EQ(2u, restores.size()) << "created " << created << ", freed " << freed;
            for (int i = 0; i < 2; i++) {
                EXPECT_EQ(guards[i].addr_, restores[i].addr_) << "created " << created << ", freed " << freed;
                EXPECT_EQ(0, restores[i].r0_) << "created " << created << ", freed " << freed;
            }
        }
    }
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for describing the main thread's stack to AddressSanitizer. ST runs the main thread as its primordial
// coroutine on the stack the OS gave it, not on a stack ST allocated, so in an MD_ASAN build ST doesn't know where that
// stack is when it tells ASAN about a switch back to the main thread. A program built with ASAN calls
// st_set_primordial_stack(top, bottom) once in main(), as SRS does in its sanitizer build, and ST passes that range to
// ASAN on every switch back to the main thread. Without MD_ASAN, ST only stores the range.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The main thread's stack as ST stores it, private to ST: its lowest address and its size.
extern "C" {
extern void* _st_primordial_stack_bottom;
extern size_t _st_primordial_stack_size;
}

// The stack of the calling OS thread, from its lowest to its highest address.
static void stack_test_thread_stack(char** bottom, char** top)
{
#if defined(_WIN32)
    // The whole reserved stack, from the TIB's DeallocationStack up to its StackBase.
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    *bottom = (char*)low;
    *top = (char*)high;
#elif defined(__APPLE__)
    pthread_t self = pthread_self();
    *top = (char*)pthread_get_stackaddr_np(self);
    *bottom = *top - pthread_get_stacksize_np(self);
#else
    void* addr = NULL;
    size_t size = 0;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &addr, &size);
    pthread_attr_destroy(&attr);
    *bottom = (char*)addr;
    *top = *bottom + size;
#endif
}

// A coroutine that counts its turns, each time the main thread yields to it.
static void* stack_test_count_turns(void* arg)
{
    int* turns = (int*)arg;
    for (int i = 0; i < 3; i++) {
        (*turns)++;
        st_thread_yield();
    }
    return NULL;
}

// A sanitizer build describes the main thread's whole stack, from its top down to its lowest address. ST stores the
// lowest address and the size, then the main thread switches to a coroutine and back three times, and finds its own
// data on its stack unchanged each time. Locks in current behavior.
VOID TEST(PrimordialStackTest, DescribesTheMainThreadStack)
{
    void* saved_bottom = _st_primordial_stack_bottom;
    size_t saved_size = _st_primordial_stack_size;

    char* bottom = NULL;
    char* top = NULL;
    stack_test_thread_stack(&bottom, &top);
    char* frame = (char*)__builtin_frame_address(0);
    ASSERT_TRUE(bottom < frame && frame < top);

    st_set_primordial_stack(top, bottom);
    EXPECT_EQ((void*)bottom, _st_primordial_stack_bottom);
    EXPECT_EQ((size_t)(top - bottom), _st_primordial_stack_size);

    char data[4096];
    memset(data, 0x5a, sizeof(data));

    int turns = 0;
    st_thread_t worker = st_thread_create(stack_test_count_turns, &turns, 1, ST_STACK_TEST_SIZE);
    ASSERT_TRUE(worker != NULL);
    for (int i = 1; i <= 3; i++) {
        st_thread_yield();
        EXPECT_EQ(i, turns);
        for (int j = 0; j < (int)sizeof(data); j++) {
            ASSERT_EQ(0x5a, (unsigned char)data[j]) << "turn " << i << ", byte " << j;
        }
    }
    EXPECT_EQ(0, st_thread_join(worker, NULL));

    st_set_primordial_stack((char*)saved_bottom + saved_size, saved_bottom);
}
