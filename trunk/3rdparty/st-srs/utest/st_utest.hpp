/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2025 The SRS Authors */

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
#define ST_COROUTINE_JOIN(trd, r0) ErrorObject* r0 = NULL; if (trd) st_thread_join(trd, (void**)&r0); SrsUniquePtr<ErrorObject> r0_uptr(r0)
#define ST_EXPECT_SUCCESS(r0) EXPECT_TRUE(!r0) << r0
#define ST_EXPECT_FAILED(r0) EXPECT_TRUE(r0) << r0

#include <stdlib.h>

#endif

