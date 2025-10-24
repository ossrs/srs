//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_core.hpp>

using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_core_deprecated.hpp>
#include <srs_protocol_conn.hpp>

VOID TEST(CoreAutoFreeTest, Free)
{
    char *data = new char[32];
    srs_freepa(data);
    EXPECT_TRUE(data == NULL);
}

VOID TEST(CoreAutoFreeTest, FreepPointer)
{
    int *ptr = new int(42);
    EXPECT_TRUE(ptr != NULL);
    EXPECT_EQ(42, *ptr);

    srs_freep(ptr);
    EXPECT_TRUE(ptr == NULL);
}

VOID TEST(CoreAutoFreeTest, FreepObject)
{
    MyNormalObject *obj = new MyNormalObject(100);
    EXPECT_TRUE(obj != NULL);
    EXPECT_EQ(100, obj->id());

    srs_freep(obj);
    EXPECT_TRUE(obj == NULL);
}

VOID TEST(CoreAutoFreeTest, FreepNullPointer)
{
    int *ptr = NULL;
    srs_freep(ptr); // Should not crash
    EXPECT_TRUE(ptr == NULL);
}

VOID TEST(CoreAutoFreeTest, FreepaArray)
{
    int *arr = new int[10];
    for (int i = 0; i < 10; i++) {
        arr[i] = i * 2;
    }
    EXPECT_TRUE(arr != NULL);
    EXPECT_EQ(0, arr[0]);
    EXPECT_EQ(18, arr[9]);

    srs_freepa(arr);
    EXPECT_TRUE(arr == NULL);
}

VOID TEST(CoreAutoFreeTest, FreepaNullArray)
{
    int *arr = NULL;
    srs_freepa(arr); // Should not crash
    EXPECT_TRUE(arr == NULL);
}

VOID TEST(CoreAutoFreeTest, FreepaCharArray)
{
    char *chars = new char[256];
    for (int i = 0; i < 10; i++) {
        chars[i] = 'a' + i;
    }
    chars[10] = '\0';
    EXPECT_TRUE(chars != NULL);
    EXPECT_STREQ("abcdefghij", chars);

    srs_freepa(chars);
    EXPECT_TRUE(chars == NULL);
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

        // Test intentional truncation - suppress warning as this is expected behavior
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-truncation"
        EXPECT_EQ(5, snprintf(buf, sizeof(buf), "Hello"));
        EXPECT_STREQ("Hell", buf);

        EXPECT_EQ(10, snprintf(buf, sizeof(buf), "HelloWorld"));
        EXPECT_STREQ("Hell", buf);
#pragma clang diagnostic pop
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

SrsSharedPtr<int> mock_create_from_ptr(SrsSharedPtr<int> p)
{
    return p;
}

VOID TEST(CoreSmartPtr, SharedPtrContructor)
{
    int *p = new int(100);
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
    int *ptr;

public:
    MockWrapper(int *p)
    {
        ptr = p;
        if (ptr)
            *ptr = *ptr + 1;
    }
    ~MockWrapper()
    {
        if (ptr)
            *ptr = *ptr - 1;
    }
};

VOID TEST(CoreSmartPtr, SharedPtrWrapper)
{
    int *ptr = new int(100);
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

        int *q0 = q.get();
        q = p;
        EXPECT_EQ(p.get(), q.get());
        EXPECT_NE(q0, q.get());
    }

    int *ptr0 = new int(100);
    SrsUniquePtr<int> ptr0_uptr(ptr0);
    EXPECT_EQ(100, *ptr0);

    int *ptr1 = new int(200);
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

template <typename T>
SrsSharedPtr<T> mock_shared_ptr_move_assign(SrsSharedPtr<T> p)
{
    SrsSharedPtr<T> q = p;
    return q;
}

template <typename T>
SrsSharedPtr<T> mock_shared_ptr_move_ctr(SrsSharedPtr<T> p)
{
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

    int *ptr = new int(100);
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

VOID TEST(CoreSmartPtr, SharedPtrResetMethod)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        EXPECT_TRUE(p);
        EXPECT_EQ(100, *p);

        p.reset();
        EXPECT_FALSE(p);
        EXPECT_TRUE(p.get() == NULL);
    }

    if (true) {
        SrsSharedPtr<MyNormalObject> p(new MyNormalObject(200));
        EXPECT_TRUE(p);
        EXPECT_EQ(200, p->id());

        p.reset();
        EXPECT_FALSE(p);
        EXPECT_TRUE(p.get() == NULL);
    }
}

VOID TEST(CoreSmartPtr, SharedPtrSelfAssignment)
{
    if (true) {
        SrsSharedPtr<int> p(new int(100));
        int *original_ptr = p.get();

        // Test self assignment - suppress warning as this is intentional
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
        p = p; // Self assignment
#pragma clang diagnostic pop
        EXPECT_EQ(original_ptr, p.get());
        EXPECT_EQ(100, *p);
    }
}

VOID TEST(CoreSmartPtr, SharedPtrMultipleReferences)
{
    int *ptr = new int(100);
    SrsUniquePtr<int> ptr_uptr(ptr);
    EXPECT_EQ(100, *ptr);

    if (true) {
        SrsSharedPtr<MockWrapper> p1(new MockWrapper(ptr));
        EXPECT_EQ(101, *ptr);

        SrsSharedPtr<MockWrapper> p2 = p1;
        EXPECT_EQ(101, *ptr);

        SrsSharedPtr<MockWrapper> p3(p1);
        EXPECT_EQ(101, *ptr);

        SrsSharedPtr<MockWrapper> p4(NULL);
        p4 = p1;
        EXPECT_EQ(101, *ptr);

        // All should point to the same object
        EXPECT_EQ(p1.get(), p2.get());
        EXPECT_EQ(p1.get(), p3.get());
        EXPECT_EQ(p1.get(), p4.get());
    }
    EXPECT_EQ(100, *ptr); // All references gone, object destroyed
}

VOID TEST(CoreSmartPtr, SharedPtrBoolOperator)
{
    if (true) {
        SrsSharedPtr<int> p(new int(42));
        EXPECT_TRUE(p);
        EXPECT_TRUE(static_cast<bool>(p));
    }

    if (true) {
        SrsSharedPtr<int> p(NULL);
        EXPECT_FALSE(p);
        EXPECT_FALSE(static_cast<bool>(p));
    }

    if (true) {
        SrsSharedPtr<int> p(new int(42));
        EXPECT_TRUE(p);
        p.reset();
        EXPECT_FALSE(p);
    }
}

class MockIntResource : public ISrsResource
{
public:
    SrsContextId id_;
    int value_;

public:
    MockIntResource(int value) : value_(value)
    {
    }
    virtual ~MockIntResource()
    {
    }

public:
    virtual const SrsContextId &get_id()
    {
        return id_;
    }
    virtual std::string desc()
    {
        return id_.c_str();
    }
};

VOID TEST(CoreSmartPtr, SharedResourceTypical)
{
    if (true) {
        SrsSharedResource<MockIntResource> *p = new SrsSharedResource<MockIntResource>(new MockIntResource(100));
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

template <typename T>
SrsSharedResource<T> mock_shared_resource_move_assign(SrsSharedResource<T> p)
{
    SrsSharedResource<T> q = p;
    return q;
}

template <typename T>
SrsSharedResource<T> mock_shared_resource_move_ctr(SrsSharedResource<T> p)
{
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

VOID TEST(CoreSmartPtr, SharedResourceNullPointer)
{
    if (true) {
        SrsSharedResource<MockIntResource> p(NULL);
        EXPECT_FALSE(p);
        EXPECT_TRUE(p.get() == NULL);
    }
}

VOID TEST(CoreSmartPtr, SharedResourceSelfAssignment)
{
    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(100));
        MockIntResource *original_ptr = p.get();

        // Test self assignment - suppress warning as this is intentional
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
        p = p; // Self assignment
#pragma clang diagnostic pop
        EXPECT_EQ(original_ptr, p.get());
        EXPECT_EQ(100, p->value_);
    }
}

VOID TEST(CoreSmartPtr, SharedResourceBoolOperator)
{
    if (true) {
        SrsSharedResource<MockIntResource> p(new MockIntResource(42));
        EXPECT_TRUE(p);
        EXPECT_TRUE(static_cast<bool>(p));
    }

    if (true) {
        SrsSharedResource<MockIntResource> p(NULL);
        EXPECT_FALSE(p);
        EXPECT_FALSE(static_cast<bool>(p));
    }
}

VOID TEST(CoreSmartPtr, SharedResourceISrsResourceInterface)
{
    if (true) {
        SrsSharedResource<MockIntResource> *p = new SrsSharedResource<MockIntResource>(new MockIntResource(100));

        // Test ISrsResource interface
        const SrsContextId &id = p->get_id();
        EXPECT_TRUE(id.empty());

        std::string desc = p->desc();
        EXPECT_TRUE(desc.empty());

        // Test access to wrapped object
        EXPECT_EQ(100, (*p)->value_);
        EXPECT_EQ(100, p->get()->value_);

        srs_freep(p);
    }
}

VOID TEST(CoreSmartPtr, SharedResourceMultipleReferences)
{
    if (true) {
        SrsSharedResource<MockIntResource> p1(new MockIntResource(200));
        SrsSharedResource<MockIntResource> p2 = p1;
        SrsSharedResource<MockIntResource> p3(p1);

        // All should point to the same object
        EXPECT_EQ(p1.get(), p2.get());
        EXPECT_EQ(p1.get(), p3.get());
        EXPECT_EQ(200, p1->value_);
        EXPECT_EQ(200, p2->value_);
        EXPECT_EQ(200, p3->value_);

        // Modify through one reference, should be visible through all
        p1->value_ = 300;
        EXPECT_EQ(300, p2->value_);
        EXPECT_EQ(300, p3->value_);
    }
}

VOID TEST(CoreSmartPtr, UniquePtrNormal)
{
    if (true) {
        SrsUniquePtr<int> p(new int(100));
        EXPECT_EQ(100, *p.get());
    }

    int *ptr = new int(100);
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
        int *ptr = new int[100];
        ptr[0] = 100;

        SrsUniquePtr<int[]> p(ptr);
        EXPECT_EQ(100, *p.get());
    }

    int *ptr = new int(100);
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

#ifndef _WIN32
#include <netdb.h>
#endif

void mock_free_chars(char *p)
{
    free(p);
}

VOID TEST(CoreSmartPtr, UniquePtrDeleterExample)
{
    if (true) {
        char *p = (char *)malloc(1024);
        SrsUniquePtr<char> ptr(p, mock_free_chars);
    }

    if (true) {
        addrinfo *r = NULL;
        getaddrinfo("127.0.0.1", NULL, NULL, &r);
        SrsUniquePtr<addrinfo> ptr(r, freeaddrinfo);
    }
}

class MockSlice
{
public:
    const char *bytes_;

public:
    MockSlice(const char *bytes)
    {
        bytes_ = bytes;
    }
    virtual ~MockSlice()
    {
    }

public:
    static void deleter(MockSlice *p)
    {
        p->bytes_ = NULL;
    }
};

VOID TEST(CoreSmartPtr, UniquePtrDeleterSlice)
{
    MockSlice p("Hello");
    EXPECT_TRUE(p.bytes_ != NULL);

    if (true) {
        SrsUniquePtr<MockSlice> ptr(&p, MockSlice::deleter);
    }
    EXPECT_TRUE(p.bytes_ == NULL);
}

class MockSpecialPacket
{
public:
    char *bytes_;
    int size_;

public:
    MockSpecialPacket(char *bytes, int size)
    {
        bytes_ = bytes;
        size_ = size;
    }
    virtual ~MockSpecialPacket()
    {
        srs_freep(bytes_);
    }

public:
    static void deleter(vector<MockSpecialPacket *> *pkts)
    {
        vector<MockSpecialPacket *>::iterator it;
        for (it = pkts->begin(); it != pkts->end(); ++it) {
            MockSpecialPacket *pkt = *it;
            srs_freep(pkt);
        }
        pkts->clear();
    }
};

VOID TEST(CoreSmartPtr, UniquePtrDeleterVector)
{
    vector<MockSpecialPacket *> pkts;
    for (int i = 0; i < 10; i++) {
        char *bytes = new char[1024];
        MockSpecialPacket *pkt = new MockSpecialPacket(bytes, 1024);
        pkts.push_back(pkt);
    }
    EXPECT_EQ(10, (int)pkts.size());

    if (true) {
        SrsUniquePtr<vector<MockSpecialPacket *> > ptr(&pkts, MockSpecialPacket::deleter);
    }
    EXPECT_EQ(0, (int)pkts.size());
}

class MockMalloc
{
public:
    const char *bytes_;

public:
    MockMalloc(int size)
    {
        bytes_ = (char *)malloc(size);
    }
    virtual ~MockMalloc()
    {
    }

public:
    static void deleter(MockMalloc *p)
    {
        free((void *)p->bytes_);
        p->bytes_ = NULL;
    }
};

VOID TEST(CoreSmartPtr, UniquePtrDeleterMalloc)
{
    MockMalloc p(1024);
    EXPECT_TRUE(p.bytes_ != NULL);

    if (true) {
        SrsUniquePtr<MockMalloc> ptr(&p, MockMalloc::deleter);
    }
    EXPECT_TRUE(p.bytes_ == NULL);
}

VOID TEST(CoreSmartPtr, UniquePtrNullPointer)
{
    if (true) {
        SrsUniquePtr<int> ptr(NULL);
        EXPECT_TRUE(ptr.get() == NULL);
    }

    if (true) {
        SrsUniquePtr<MyNormalObject> ptr(NULL);
        EXPECT_TRUE(ptr.get() == NULL);
    }
}

VOID TEST(CoreSmartPtr, UniquePtrGetMethod)
{
    if (true) {
        int *raw_ptr = new int(42);
        SrsUniquePtr<int> ptr(raw_ptr);
        EXPECT_EQ(raw_ptr, ptr.get());
        EXPECT_EQ(42, *ptr.get());
    }

    if (true) {
        MyNormalObject *raw_obj = new MyNormalObject(100);
        SrsUniquePtr<MyNormalObject> ptr(raw_obj);
        EXPECT_EQ(raw_obj, ptr.get());
        EXPECT_EQ(100, ptr.get()->id());
    }
}

VOID TEST(CoreSmartPtr, UniquePtrArrowOperator)
{
    if (true) {
        MyNormalObject *raw_obj = new MyNormalObject(200);
        SrsUniquePtr<MyNormalObject> ptr(raw_obj);
        EXPECT_EQ(200, ptr->id());
    }
}

VOID TEST(CoreSmartPtr, UniquePtrArrayIndexOperator)
{
    if (true) {
        int *arr = new int[5];
        for (int i = 0; i < 5; i++) {
            arr[i] = i * 10;
        }

        SrsUniquePtr<int[]> ptr(arr);
        EXPECT_EQ(0, ptr[0]);
        EXPECT_EQ(10, ptr[1]);
        EXPECT_EQ(40, ptr[4]);
    }
}

VOID TEST(CoreSmartPtr, UniquePtrArrayConstIndexOperator)
{
    if (true) {
        int *arr = new int[3];
        arr[0] = 100;
        arr[1] = 200;
        arr[2] = 300;

        const SrsUniquePtr<int[]> ptr(arr);
        EXPECT_EQ(100, ptr[0]);
        EXPECT_EQ(200, ptr[1]);
        EXPECT_EQ(300, ptr[2]);
    }
}

VOID TEST(CoreSmartPtr, SmartPointerEdgeCases)
{
    // Test SrsUniquePtr with custom deleter and NULL pointer
    if (true) {
        SrsUniquePtr<char> ptr(NULL, mock_free_chars);
        EXPECT_TRUE(ptr.get() == NULL);
        // Should not crash when destroyed with NULL pointer
    }

    // Test SrsSharedPtr with NULL pointer in copy constructor
    if (true) {
        SrsSharedPtr<int> p1(NULL);
        SrsSharedPtr<int> p2(p1);
        EXPECT_FALSE(p1);
        EXPECT_FALSE(p2);
        EXPECT_EQ(p1.get(), p2.get());
    }

    // Test SrsSharedPtr with NULL pointer in assignment
    if (true) {
        SrsSharedPtr<int> p1(NULL);
        SrsSharedPtr<int> p2(new int(42));
        EXPECT_TRUE(p2);

        p2 = p1;
        EXPECT_FALSE(p1);
        EXPECT_FALSE(p2);
    }

    // Test SrsSharedResource with NULL pointer in copy constructor
    if (true) {
        SrsSharedResource<MockIntResource> p1(NULL);
        SrsSharedResource<MockIntResource> p2(p1);
        EXPECT_FALSE(p1);
        EXPECT_FALSE(p2);
        EXPECT_EQ(p1.get(), p2.get());
    }
}

VOID TEST(CoreSmartPtr, SmartPointerMemoryManagement)
{
    // Test that SrsUniquePtr properly manages memory
    int *counter = new int(0);
    SrsUniquePtr<int> counter_uptr(counter);

    if (true) {
        SrsUniquePtr<MockWrapper> ptr(new MockWrapper(counter));
        EXPECT_EQ(1, *counter);
    }
    EXPECT_EQ(0, *counter); // MockWrapper destructor should have decremented

    // Test that SrsSharedPtr properly manages reference counting
    if (true) {
        SrsSharedPtr<MockWrapper> p1(new MockWrapper(counter));
        EXPECT_EQ(1, *counter);

        if (true) {
            SrsSharedPtr<MockWrapper> p2 = p1;
            EXPECT_EQ(1, *counter); // Same object, counter unchanged

            SrsSharedPtr<MockWrapper> p3(new MockWrapper(counter));
            EXPECT_EQ(2, *counter); // New object created
        }
        EXPECT_EQ(1, *counter); // p3 destroyed, one object remains
    }
    EXPECT_EQ(0, *counter); // All objects destroyed
}

VOID TEST(CoreSmartPtr, SmartPointerOperatorOverloads)
{
    // Test SrsSharedPtr dereference operator
    if (true) {
        SrsSharedPtr<int> ptr(new int(42));
        EXPECT_EQ(42, *ptr);

        *ptr = 100;
        EXPECT_EQ(100, *ptr);
    }

    // Test SrsSharedResource dereference operator
    if (true) {
        SrsSharedResource<MockIntResource> ptr(new MockIntResource(200));
        EXPECT_EQ(200, (*ptr).value_);

        (*ptr).value_ = 300;
        EXPECT_EQ(300, (*ptr).value_);
    }
}
