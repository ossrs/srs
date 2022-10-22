/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2022 Winlin */

#ifndef ST_UTEST_PUBLIC_HPP
#define ST_UTEST_PUBLIC_HPP

// Before define the private/protected, we must include some system header files.
// Or it may fail with:
//      redeclared with different access struct __xfer_bufptrs
// @see https://stackoverflow.com/questions/47839718/sstream-redeclared-with-public-access-compiler-error
#include <gtest/gtest.h>

#include <st.h>
#include <string>

#define VOID

// Close the fd automatically.
#define StFdCleanup(fd, stfd) impl__StFdCleanup _ST_free_##fd(&fd, &stfd)
#define StStfdCleanup(stfd) impl__StFdCleanup _ST_free_##stfd(NULL, &stfd)
class impl__StFdCleanup {
    int* fd_;
    st_netfd_t* stfd_;
public:
    impl__StFdCleanup(int* fd, st_netfd_t* stfd) : fd_(fd), stfd_(stfd) {
    }
    virtual ~impl__StFdCleanup() {
        if (stfd_ && *stfd_) {
            st_netfd_close(*stfd_);
        } else if (fd_ && *fd_ > 0) {
            ::close(*fd_);
        }
    }
};

// For coroutine function to return with error object.
struct ErrorObject {
    int r0_;
    int errno_;
    std::string message_;

    ErrorObject(int r0, std::string message) : r0_(r0), errno_(errno), message_(message) {
    }
};
extern std::ostream& operator<<(std::ostream& out, const ErrorObject* err);
#define ST_ASSERT_ERROR(error, r0, message) if (error) return new ErrorObject(r0, message)
#define ST_COROUTINE_JOIN(trd, r0) ErrorObject* r0 = NULL; SrsAutoFree(ErrorObject, r0); if (trd) st_thread_join(trd, (void**)&r0)
#define ST_EXPECT_SUCCESS(r0) EXPECT_TRUE(!r0) << r0
#define ST_EXPECT_FAILED(r0) EXPECT_TRUE(r0) << r0

#include <stdlib.h>

// To free the instance in the current scope, for instance, MyClass* ptr,
// which is a ptr and this class will:
//       1. free the ptr.
//       2. set ptr to NULL.
//
// Usage:
//       MyClass* po = new MyClass();
//       // ...... use po
//       SrsAutoFree(MyClass, po);
//
// Usage for array:
//      MyClass** pa = new MyClass*[size];
//      // ....... use pa
//      SrsAutoFreeA(MyClass*, pa);
//
// @remark the MyClass can be basic type, for instance, SrsAutoFreeA(char, pstr),
//      where the char* pstr = new char[size].
// To delete object.
#define SrsAutoFree(className, instance) \
    impl_SrsAutoFree<className> _auto_free_##instance(&instance, false, false, NULL)
// To delete array.
#define SrsAutoFreeA(className, instance) \
    impl_SrsAutoFree<className> _auto_free_array_##instance(&instance, true, false, NULL)
// Use free instead of delete.
#define SrsAutoFreeF(className, instance) \
    impl_SrsAutoFree<className> _auto_free_##instance(&instance, false, true, NULL)
// Use hook instead of delete.
#define SrsAutoFreeH(className, instance, hook) \
    impl_SrsAutoFree<className> _auto_free_##instance(&instance, false, false, hook)
// The template implementation.
template<class T>
class impl_SrsAutoFree
{
private:
    T** ptr;
    bool is_array;
    bool _use_free;
    void (*_hook)(T*);
public:
    // If use_free, use free(void*) to release the p.
    // If specified hook, use hook(p) to release it.
    // Use delete to release p, or delete[] if p is an array.
    impl_SrsAutoFree(T** p, bool array, bool use_free, void (*hook)(T*)) {
        ptr = p;
        is_array = array;
        _use_free = use_free;
        _hook = hook;
    }

    virtual ~impl_SrsAutoFree() {
        if (ptr == NULL || *ptr == NULL) {
            return;
        }

        if (_use_free) {
            free(*ptr);
        } else if (_hook) {
            _hook(*ptr);
        } else {
            if (is_array) {
                delete[] *ptr;
            } else {
                delete *ptr;
            }
        }

        *ptr = NULL;
    }
};

// The time unit in ms, for example 100 * SRS_UTIME_MILLISECONDS means 100ms.
#define SRS_UTIME_MILLISECONDS 1000

#endif

