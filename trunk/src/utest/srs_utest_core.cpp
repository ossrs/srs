//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_core.hpp>

using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_app_conn.hpp>
#include <srs_core_deprecated.hpp>

VOID TEST(CoreAutoFreeTest, Free)
{
    char* data = new char[32];
    srs_freepa(data);
    EXPECT_TRUE(data == NULL);

    if (true) {
        data = new char[32];
        SrsAutoFreeA(char, data);
    }
    EXPECT_TRUE(data == NULL);
}

VOID TEST(CoreMacroseTest, Check)
{
#ifndef SRS_BUILD_TS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_BUILD_DATE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_UNAME
    EXPECT_TRUE(false);
#endif
#ifndef SRS_USER_CONFIGURE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONFIGURE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_PREFIX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONSTRIBUTORS
    EXPECT_TRUE(false);
#endif
}

VOID TEST(CoreLogger, CheckVsnprintf)
{
    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0xf);

        // Return the number of characters printed.
        EXPECT_EQ(6, snprintf(buf, sizeof(buf), "%s", "Hello!"));
        EXPECT_EQ('H', buf[0]);
        EXPECT_EQ('!', buf[5]);
        EXPECT_EQ(0x0, buf[6]);
        EXPECT_EQ(0xf, buf[7]);
    }

    if (true) {
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0xf);

        // Return the number of characters that would have been printed if the size were unlimited.
        EXPECT_EQ(6, snprintf(buf, 3, "%s", "Hello!"));
        EXPECT_EQ('H', buf[0]);
        EXPECT_EQ('e', buf[1]);
        EXPECT_EQ(0, buf[2]);
        EXPECT_EQ(0xf, buf[3]);
    }

    if (true) {
        char buf[5];
        EXPECT_EQ(4, snprintf(buf, sizeof(buf), "Hell"));
        EXPECT_STREQ("Hell", buf);

        EXPECT_EQ(5, snprintf(buf, sizeof(buf), "Hello"));
        EXPECT_STREQ("Hell", buf);

        EXPECT_EQ(10, snprintf(buf, sizeof(buf), "HelloWorld"));
        EXPECT_STREQ("Hell", buf);
    }
}

VOID TEST(CoreSmartPtr, SharedPtrTypical)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, *p);
    }

    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q = p;
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q(p);
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q = p;
        EXPECT_TRUE(p);
        EXPECT_TRUE(q);
        EXPECT_EQ(100, *p);
        EXPECT_EQ(100, *q);
    }
}

VOID TEST(CoreSmartPtr, SharedPtrReset)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q = p;
        p.reset();
        EXPECT_FALSE(p);
        EXPECT_TRUE(q);
        EXPECT_EQ(100, *q);
    }

    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q = p;
        q.reset();
        EXPECT_TRUE(p);
        EXPECT_FALSE(q);
        EXPECT_EQ(100, *p);
    }
}

SrsSharedPtr<int> mock_create_from_ptr(SrsSharedPtr<int> p) {
    return p;
}

VOID TEST(CoreSmartPtr, SharedPtrContructor)
{
    int* p = new int(100);
    SrsSharedPtr<int> q = mock_create_from_ptr(p);
    EXPECT_EQ(100, *q);
}

VOID TEST(CoreSmartPtr, SharedPtrObject)
{
    SrsSharedPtr<MyNormalObject> p(new MyNormalObject(100));
    EXPECT_TRUE(p);
    EXPECT_EQ(100, p->id());
}

VOID TEST(CoreSmartPtr, SharedPtrNullptr)
{
    SrsSharedPtr<int> p(NULL);
    EXPECT_FALSE(p);

    p.reset();
    EXPECT_FALSE(p);

    SrsSharedPtr<int> q = p;
    EXPECT_FALSE(q);
}

class MockWrapper
{
public:
    int* ptr;
public:
    MockWrapper(int* p) {
        ptr = p;
        if (ptr) *ptr = *ptr + 1;
    }
    ~MockWrapper() {
        if (ptr) *ptr = *ptr - 1;
    }
};

VOID TEST(CoreSmartPtr, SharedPtrWrapper)
{
    int* ptr = new int(100);
    SrsUniquePtr<int> ptr_uptr(ptr);
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsSharedPtr<MockWrapper> p(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);

        SrsSharedPtr<MockWrapper> q = p;
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);
        EXPECT_EQ(101, *q->ptr);

        SrsSharedPtr<MockWrapper> r(new MockWrapper(ptr));
        EXPECT_EQ(102, *ptr);
        EXPECT_EQ(102, *p->ptr);
        EXPECT_EQ(102, *q->ptr);
        EXPECT_EQ(102, *r->ptr);

        SrsSharedPtr<MockWrapper> s(new MockWrapper(ptr));
        EXPECT_EQ(103, *ptr);
        EXPECT_EQ(103, *p->ptr);
        EXPECT_EQ(103, *q->ptr);
        EXPECT_EQ(103, *r->ptr);
        EXPECT_EQ(103, *s->ptr);
    }
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsSharedPtr<MockWrapper> p(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);
    }
    EXPECT_EQ(100, *ptr);
}

VOID TEST(CoreSmartPtr, SharedPtrAssign)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q(NULL);
        q = p;
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q(new int(101));

        int* q0 = q.get();
        q = p;
        EXPECT_EQ(p.get(), q.get());
        EXPECT_NE(q0, q.get());
    }

    int* ptr0 = new int(100);
    SrsUniquePtr<int> ptr0_uptr(ptr0);
    EXPECT_EQ(100, *ptr0);

    int* ptr1 = new int(200);
    SrsUniquePtr<int> ptr1_uptr(ptr1);
    EXPECT_EQ(200, *ptr1);

    if (true) {
        SrsSharedPtr<MockWrapper> p(new MockWrapper(ptr0));
        EXPECT_EQ(101, *ptr0);
        EXPECT_EQ(101, *p->ptr);

        SrsSharedPtr<MockWrapper> q(new MockWrapper(ptr1));
        EXPECT_EQ(201, *ptr1);
        EXPECT_EQ(201, *q->ptr);

        q = p;
        EXPECT_EQ(200, *ptr1);
        EXPECT_EQ(101, *ptr0);
        EXPECT_EQ(101, *p->ptr);
        EXPECT_EQ(101, *q->ptr);
    }

    EXPECT_EQ(100, *ptr0);
    EXPECT_EQ(200, *ptr1);
}

template<typename T>
SrsSharedPtr<T> mock_shared_ptr_move_assign(SrsSharedPtr<T> p) {
    SrsSharedPtr<T> q = p;
    return q;
}

template<typename T>
SrsSharedPtr<T> mock_shared_ptr_move_ctr(SrsSharedPtr<T> p) {
    return p;
}

VOID TEST(CoreSmartPtr, SharedPtrMove)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q(new int(101));
        q = mock_shared_ptr_move_ctr(p);
        EXPECT_EQ(q.get(), p.get());
    }

    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q(new int(101));
        q = mock_shared_ptr_move_assign(p);
        EXPECT_EQ(q.get(), p.get());
    }

    int* ptr = new int(100);
    SrsUniquePtr<int> ptr_uptr(ptr);
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsSharedPtr<MockWrapper> p(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);

        SrsSharedPtr<MockWrapper> q(new MockWrapper(ptr));
        q = mock_shared_ptr_move_ctr(p);
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *q->ptr);
    }
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsSharedPtr<MockWrapper> p(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);

        SrsSharedPtr<MockWrapper> q(new MockWrapper(ptr));
        q = mock_shared_ptr_move_assign(p);
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *q->ptr);
    }
    EXPECT_EQ(100, *ptr);

    // Note that this will not trigger the move constructor or move assignment operator.
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        SrsSharedPtr<int> q = mock_shared_ptr_move_assign(p);
        EXPECT_EQ(q.get(), p.get());
    }

    // Note that this will not trigger the move constructor or move assignment operator.
    if (true) {
        SrsSharedPtr<int> p = SrsSharedPtr<int>(new int(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, *p);
    }
}

class MockIntResource : public ISrsResource
{
public:
    SrsContextId id_;
    int value_;
public:
    MockIntResource(int value) : value_(value) {
    }
    virtual ~MockIntResource() {
    }
public:
    virtual const SrsContextId& get_id() {
        return id_;
    }
    virtual std::string desc() {
        return id_.c_str();
    }
};

VOID TEST(CoreSmartPtr, SharedResourceTypical)
{
    if (true) {
        SrsSharedResource<MockIntResource>* p = new SrsSharedResource<MockIntResource>(new MockIntResource(100));
        EXPECT_TRUE(*p);
        EXPECT_EQ(100, (*p)->value_);
        srs_freep(p);
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, p->value_);
    }

    if (true) {
        SrsSharedResource<MockIntResource> p = SrsSharedResource<MockIntResource>(new MockIntResource(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, p->value_);
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        SrsSharedResource<MockIntResource> q = p;
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        SrsSharedResource<MockIntResource> q(NULL);
        q = p;
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        SrsSharedResource<MockIntResource> q(new MockIntResource(200));
        q = p;
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        SrsSharedResource<MockIntResource> q = p;
        EXPECT_TRUE(p);
        EXPECT_TRUE(q);
        EXPECT_EQ(100, p->value_);
        EXPECT_EQ(100, q->value_);
    }
}

template<typename T>
SrsSharedResource<T> mock_shared_resource_move_assign(SrsSharedResource<T> p) {
    SrsSharedResource<T> q = p;
    return q;
}

template<typename T>
SrsSharedResource<T> mock_shared_resource_move_ctr(SrsSharedResource<T> p) {
    return p;
}

VOID TEST(CoreSmartPtr, SharedResourceMove)
{
    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        SrsSharedResource<MockIntResource> q(new MockIntResource(101));
        q = mock_shared_resource_move_ctr(p);
        EXPECT_EQ(100, q->value_);
        EXPECT_EQ(q.get(), p.get());
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        SrsSharedResource<MockIntResource> q(new MockIntResource(101));
        q = mock_shared_resource_move_assign(p);
        EXPECT_EQ(100, q->value_);
        EXPECT_EQ(q.get(), p.get());
    }
}

VOID TEST(CoreSmartPtr, UniquePtrNormal)
{
    if (true) {
        SrsUniquePtr<int> p(new int(100));
        EXPECT_EQ(100, *p.get());
    }

    int* ptr = new int(100);
    SrsUniquePtr<int> ptr_uptr(ptr);
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsUniquePtr<MockWrapper> p(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);

        SrsUniquePtr<MockWrapper> p0(new MockWrapper(ptr));
        EXPECT_EQ(102, *ptr);
        EXPECT_EQ(102, *p0->ptr);
    }
    EXPECT_EQ(100, *ptr);
}

VOID TEST(CoreSmartPtr, UniquePtrArray)
{
    if (true) {
        int* ptr = new int[100];
        ptr[0] = 100;

        SrsUniquePtr<int[]> p(ptr);
        EXPECT_EQ(100, *p.get());
    }

    int* ptr = new int(100);
    SrsUniquePtr<int> ptr_uptr(ptr);
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsUniquePtr<MockWrapper[]> p(new MockWrapper[1]{MockWrapper(ptr)});
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *(p[0].ptr));

        SrsUniquePtr<MockWrapper[]> p0(new MockWrapper[1]{MockWrapper(ptr)});
        EXPECT_EQ(102, *ptr);
        EXPECT_EQ(102, *(p0[0].ptr));
    }
    EXPECT_EQ(100, *ptr);
}

