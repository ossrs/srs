/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#include <st_utest.hpp>

#include <st.h>
#include <errno.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <algorithm>
#include <string>
#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The utest for per-coroutine private data, the way SRS keeps a log context ID on every coroutine.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A log context ID, like SrsContextId in SRS. Each coroutine owns one on the heap, and the key's destructor frees it
// when the ID is replaced or the coroutine exits.
struct KeyTestContextId {
    std::string id_;
    explicit KeyTestContextId(const std::string& id) : id_(id) {
    }
};

// The IDs the destructor freed, in order. Each test clears it first.
static std::vector<std::string> _key_freed_ids;

static void key_context_destructor(void* arg)
{
    KeyTestContextId* cid = (KeyTestContextId*)arg;
    _key_freed_ids.push_back(cid->id_);
    delete cid;
}

// Keys are never released, and an OS thread has only st_key_getlimit() of them, so the tests in this process share
// one context key, created on first use like SRS does.
static int key_context()
{
    static int key = -1;
    if (key < 0) {
        int r0 = st_key_create(&key, key_context_destructor);
        EXPECT_EQ(0, r0);
    }
    return key;
}

// The context ID of the current coroutine, or empty if it has none.
static std::string key_current_id()
{
    KeyTestContextId* cid = (KeyTestContextId*)st_thread_getspecific(key_context());
    return cid ? cid->id_ : "";
}

struct KeyTestCoroutine {
    // The ID the coroutine sets on itself, or empty to set none.
    std::string set_id_;
    // The ID the coroutine sees when it starts, and after other coroutines have run.
    std::string seen_at_start_;
    std::string seen_later_;
};

static void* key_coroutine(void* arg)
{
    KeyTestCoroutine* c = (KeyTestCoroutine*)arg;

    c->seen_at_start_ = key_current_id();
    if (!c->set_id_.empty()) {
        st_thread_setspecific(key_context(), new KeyTestContextId(c->set_id_));
    }

    // Let the other coroutines run and set their own IDs.
    st_usleep(0);

    c->seen_later_ = key_current_id();
    return NULL;
}

// Each connection coroutine sets its own context ID and keeps seeing it while other coroutines set theirs. When a
// coroutine exits, ST frees its ID with the key's destructor, so SRS never leaks the ID of a finished connection. A
// coroutine with no ID exits without calling the destructor. Locks in current behavior.
VOID TEST(KeyTest, EachCoroutineOwnsItsContextId)
{
    _key_freed_ids.clear();

    KeyTestCoroutine a, b, c;
    a.set_id_ = "conn-a";
    b.set_id_ = "conn-b";

    st_thread_t trd_a = st_thread_create(key_coroutine, &a, 1, 0);
    st_thread_t trd_b = st_thread_create(key_coroutine, &b, 1, 0);
    st_thread_t trd_c = st_thread_create(key_coroutine, &c, 1, 0);
    ASSERT_TRUE(trd_a && trd_b && trd_c);
    st_thread_join(trd_a, NULL);
    st_thread_join(trd_b, NULL);
    st_thread_join(trd_c, NULL);

    // A new coroutine starts with no ID, then sees only its own.
    EXPECT_EQ("", a.seen_at_start_);
    EXPECT_EQ("conn-a", a.seen_later_);
    EXPECT_EQ("", b.seen_at_start_);
    EXPECT_EQ("conn-b", b.seen_later_);
    EXPECT_EQ("", c.seen_later_);

    // The coroutine running the test is not affected.
    EXPECT_EQ("", key_current_id());

    // Exit freed the two IDs, and nothing for the coroutine without one.
    std::vector<std::string> freed = _key_freed_ids;
    std::sort(freed.begin(), freed.end());
    ASSERT_EQ(2, (int)freed.size());
    EXPECT_EQ("conn-a", freed[0]);
    EXPECT_EQ("conn-b", freed[1]);
}

// SRS stamps a new coroutine's context ID from the coroutine that creates it, before the new one first runs
// (SrsFastCoroutine::set_cid). The new coroutine starts with that ID, and ST frees it when the coroutine exits.
// Locks in current behavior.
VOID TEST(KeyTest, SetContextIdOfAnotherCoroutineBeforeItRuns)
{
    _key_freed_ids.clear();

    KeyTestCoroutine c;
    st_thread_t trd = st_thread_create(key_coroutine, &c, 1, 0);
    ASSERT_TRUE(trd != NULL);

    EXPECT_EQ(0, st_thread_setspecific2(trd, key_context(), new KeyTestContextId("conn-1")));
    EXPECT_TRUE(_key_freed_ids.empty());

    st_thread_join(trd, NULL);

    EXPECT_EQ("conn-1", c.seen_at_start_);
    EXPECT_EQ("conn-1", c.seen_later_);
    EXPECT_EQ("", key_current_id());

    ASSERT_EQ(1, (int)_key_freed_ids.size());
    EXPECT_EQ("conn-1", _key_freed_ids[0]);
}

// SRS changes a coroutine's context ID, for example when a connection retries, and allocates a new ID each time.
// Setting a new value frees the old one, so the change doesn't leak. Setting the same pointer again frees nothing,
// because that value is still in use. Setting NULL frees the value and clears the slot. Locks in current behavior.
VOID TEST(KeyTest, ReplaceContextIdFreesTheOldOne)
{
    _key_freed_ids.clear();

    st_thread_t self = st_thread_self();
    KeyTestContextId* first = new KeyTestContextId("first");
    KeyTestContextId* retry = new KeyTestContextId("retry");

    EXPECT_EQ(0, st_thread_setspecific2(self, key_context(), first));
    EXPECT_EQ("first", key_current_id());
    EXPECT_TRUE(_key_freed_ids.empty());

    EXPECT_EQ(0, st_thread_setspecific2(self, key_context(), retry));
    EXPECT_EQ("retry", key_current_id());
    ASSERT_EQ(1, (int)_key_freed_ids.size());
    EXPECT_EQ("first", _key_freed_ids[0]);

    EXPECT_EQ(0, st_thread_setspecific2(self, key_context(), retry));
    EXPECT_EQ("retry", key_current_id());
    EXPECT_EQ(1, (int)_key_freed_ids.size());

    EXPECT_EQ(0, st_thread_setspecific2(self, key_context(), NULL));
    EXPECT_EQ("", key_current_id());
    ASSERT_EQ(2, (int)_key_freed_ids.size());
    EXPECT_EQ("retry", _key_freed_ids[1]);
}

static int _key_borrowed_first = 1;
static int _key_borrowed_second = 2;

static void* key_borrow_coroutine(void* arg)
{
    int key = *(int*)arg;

    // Replace the borrowed value, then exit while still holding the second one.
    st_thread_setspecific(key, &_key_borrowed_second);
    return st_thread_getspecific(key);
}

// A key created without a destructor holds values the caller owns, such as a pointer to a static object. ST never
// frees them: not when a value is replaced, and not when the coroutine exits. Locks in current behavior.
VOID TEST(KeyTest, KeyWithoutDestructorLeavesValuesToTheCaller)
{
    static int key = -1;
    if (key < 0) {
        ASSERT_EQ(0, st_key_create(&key, NULL));
    }
    st_thread_t trd = st_thread_create(key_borrow_coroutine, &key, 1, 0);
    ASSERT_TRUE(trd != NULL);
    EXPECT_EQ(0, st_thread_setspecific2(trd, key, &_key_borrowed_first));

    // The static objects were never passed to free, or this would crash.
    void* last = NULL;
    st_thread_join(trd, &last);
    EXPECT_TRUE(last == &_key_borrowed_second);
    EXPECT_EQ(1, _key_borrowed_first);
    EXPECT_EQ(2, _key_borrowed_second);
}

// A negative key, or one beyond the table, is rejected with EINVAL, and reading it returns NULL instead of crashing.
// SRS reads its context key as -1 before the first ID is set. Locks in current behavior.
VOID TEST(KeyTest, InvalidKeyIsRejected)
{
    int value = 0;

    errno = 0;
    EXPECT_EQ(-1, st_thread_setspecific(-1, &value));
    EXPECT_EQ(EINVAL, errno);

    errno = 0;
    EXPECT_EQ(-1, st_thread_setspecific2(st_thread_self(), st_key_getlimit(), &value));
    EXPECT_EQ(EINVAL, errno);

    EXPECT_TRUE(st_thread_getspecific(-1) == NULL);
    EXPECT_TRUE(st_thread_getspecific(st_key_getlimit()) == NULL);
}

#ifndef _WIN32 // POSIX only: an OS thread with pthread
struct KeyTestThread {
    // The keys created, and the one past the limit.
    std::vector<int> keys_;
    int create_over_limit_;
    int errno_over_limit_;
};

static void* key_create_all_thread(void* arg)
{
    KeyTestThread* r = (KeyTestThread*)arg;

    for (int i = 0; i < st_key_getlimit(); i++) {
        int key = -1;
        if (st_key_create(&key, NULL) == 0) {
            r->keys_.push_back(key);
        }
    }

    int key = -1;
    errno = 0;
    r->create_over_limit_ = st_key_create(&key, NULL);
    r->errno_over_limit_ = errno;
    return NULL;
}

// The key table belongs to the OS thread, so another OS thread starts with no keys, whatever this thread created.
// It can create exactly st_key_getlimit() keys, numbered from 0, and the next one fails with EAGAIN. Keys are never
// released, so the limit holds for the thread's lifetime; that's why the limit is tested on a throwaway thread.
// Locks in current behavior.
VOID TEST(KeyTest, KeyLimitPerThread)
{
    // Make sure this thread has created a key already.
    EXPECT_GE(key_context(), 0);

    KeyTestThread r;
    pthread_t thread;
    ASSERT_EQ(0, pthread_create(&thread, NULL, key_create_all_thread, &r));
    ASSERT_EQ(0, pthread_join(thread, NULL));

    ASSERT_EQ(st_key_getlimit(), (int)r.keys_.size());
    for (int i = 0; i < (int)r.keys_.size(); i++) {
        EXPECT_EQ(i, r.keys_[i]);
    }

    EXPECT_EQ(-1, r.create_over_limit_);
    EXPECT_EQ(EAGAIN, r.errno_over_limit_);
}
#endif
