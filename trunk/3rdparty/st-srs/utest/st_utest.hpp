/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2026 The SRS Authors */

#ifndef ST_UTEST_PUBLIC_HPP
#define ST_UTEST_PUBLIC_HPP

// Before define the private/protected, we must include some system header files.
// Or it may fail with:
//      redeclared with different access struct __xfer_bufptrs
// @see https://stackoverflow.com/questions/47839718/sstream-redeclared-with-public-access-compiler-error
#include <gtest/gtest.h>

#include <st.h>
#include <string>
#include <memory>

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#define VOID

// Portable descriptors. A test that only needs a connection ST can wait on uses these, not a pipe or read and write,
// so it runs unchanged where only sockets can be polled, such as Windows. A test about a POSIX feature itself, such as
// a pipe, a signal or a chosen descriptor number, keeps the POSIX calls.

// Two connected stream sockets. Returns 0, or -1 with errno set.
static inline int st_utest_stream_pair(int fds[2])
{
    return ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

static inline ssize_t st_utest_send(int fd, const void* buf, size_t size)
{
    return ::send(fd, buf, size, 0);
}

static inline ssize_t st_utest_recv(int fd, void* buf, size_t size)
{
    return ::recv(fd, buf, size, 0);
}

static inline int st_utest_close(int fd)
{
    return ::close(fd);
}

// Whether the last send or recv failed because the socket wasn't ready.
static inline bool st_utest_would_block()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

// A connection whose one end ST waits on, wrapped with st_netfd_open_socket, and whose other end, the peer, the test
// reads and writes directly.
struct StUtestPair {
    st_netfd_t stfd_;
    int peer_;
    StUtestPair() : stfd_(NULL), peer_(-1) {
    }
    ~StUtestPair() {
        if (stfd_) st_netfd_close(stfd_);
        if (peer_ >= 0) st_utest_close(peer_);
    }
};

// Returns false, with errno set, if the sockets can't be created or wrapped.
static inline bool st_utest_pair_open(StUtestPair& p)
{
    int fds[2];
    if (st_utest_stream_pair(fds) < 0) return false;

    p.peer_ = fds[1];
    if ((p.stfd_ = st_netfd_open_socket(fds[0])) == NULL) {
        st_utest_close(fds[0]);
        return false;
    }
    return true;
}

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
            st_utest_close(*fd_);
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
#define ST_COROUTINE_JOIN(trd, r0) ErrorObject* r0 = NULL; if (trd) st_thread_join(trd, (void**)&r0); std::unique_ptr<ErrorObject> r0##_uptr(r0)
#define ST_EXPECT_SUCCESS(r0) EXPECT_TRUE(!r0) << r0
#define ST_EXPECT_FAILED(r0) EXPECT_TRUE(r0) << r0

#include <stdlib.h>

#endif

