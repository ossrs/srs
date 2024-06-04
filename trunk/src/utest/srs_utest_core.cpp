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

VOID TEST(CoreLogger, SharedPtrTypical)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, *p);
    }

    if (true) {
        SrsSharedPtr<int> p = SrsSharedPtr<int>(new int(100));
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
        SrsSharedPtr<int> q = p;
        EXPECT_TRUE(p);
        EXPECT_TRUE(q);
        EXPECT_EQ(100, *p);
        EXPECT_EQ(100, *q);
    }
}

VOID TEST(CoreLogger, SharedPtrReset)
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

VOID TEST(CoreLogger, SharedPtrObject)
{
    SrsSharedPtr<MyNormalObject> p(new MyNormalObject(100));
    EXPECT_TRUE(p);
    EXPECT_EQ(100, p->id());
}

VOID TEST(CoreLogger, SharedPtrNullptr)
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
        *ptr = *ptr + 1;
    }
    ~MockWrapper() {
        *ptr = *ptr - 1;
    }
};

VOID TEST(CoreLogger, SharedPtrWrapper)
{
    int* ptr = new int(100);
    SrsAutoFree(int, ptr);
    EXPECT_EQ(100, *ptr);

    {
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

    {
        SrsSharedPtr<MockWrapper> p(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);
        EXPECT_EQ(101, *p->ptr);
    }
    EXPECT_EQ(100, *ptr);
}

class MockResource : public ISrsResource
{
public:
    SrsContextId id_;
    int value_;
public:
    MockResource(int value) : value_(value) {
    }
    virtual ~MockResource() {
    }
public:
    virtual const SrsContextId& get_id() {
        return id_;
    }
    virtual std::string desc() {
        return id_.c_str();
    }
};

VOID TEST(CoreLogger, SharedResourceTypical)
{
    if (true) {
        SrsSharedResource<MockResource>* p = new SrsSharedResource<MockResource>(new MockResource(100));
        EXPECT_TRUE(*p);
        EXPECT_EQ(100, (*p)->value_);
        srs_freep(p);
    }

    if (true) {
        SrsSharedResource<MockResource> p(new MockResource(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, p->value_);
    }

    if (true) {
        SrsSharedResource<MockResource> p = SrsSharedResource<MockResource>(new MockResource(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, p->value_);
    }

    if (true) {
        SrsSharedResource<MockResource> p(new MockResource(100));
        SrsSharedResource<MockResource> q = p;
        EXPECT_EQ(p.get(), q.get());
    }

    if (true) {
        SrsSharedResource<MockResource> p(new MockResource(100));
        SrsSharedResource<MockResource> q = p;
        EXPECT_TRUE(p);
        EXPECT_TRUE(q);
        EXPECT_EQ(100, p->value_);
        EXPECT_EQ(100, q->value_);
    }
}

