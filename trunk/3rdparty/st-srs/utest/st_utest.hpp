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
#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#endif

#ifdef _WIN32
// winnt.h defines VOID as void, which the tests use as an empty prefix.
#undef VOID
// For the tests that declare ST's thread-local variables, as md.h maps it for WIN64.
#define __thread __declspec(thread)
// MSVC has no frame address builtin; the slot of the return address is in the caller's frame, next to it.
#include <intrin.h>
#define __builtin_frame_address(level) _AddressOfReturnAddress()
static inline int getpagesize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwPageSize;
}
// Sleeps the OS thread, rounded up to milliseconds.
static inline int usleep(unsigned int us)
{
    Sleep((us + 999) / 1000);
    return 0;
}
#endif
#define VOID

// Portable descriptors. A test that only needs a connection ST can wait on uses these, not a pipe or read and write,
// so it runs unchanged where only sockets can be polled, such as Windows. A test about a POSIX feature itself, such as
// a pipe, a signal or a chosen descriptor number, keeps the POSIX calls.

#ifdef _WIN32
// Maps the last Winsock error to errno, as ST does.
extern "C" int _st_win64_errno(int wsaerr, int connecting);

// Native Windows (winsock comes from st.h) has no socketpair, so the pair is a loopback TCP connection of two blocking
// sockets, with Nagle off so small writes are not delayed, as on a Unix-domain socketpair. Descriptors are winsock
// sockets that fit in an int. Returns 0, or -1 with errno set.
static inline int st_utest_stream_pair(int fds[2])
{
    fds[0] = fds[1] = -1;
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKET client = INVALID_SOCKET, server = INVALID_SOCKET;
    int err = 0;

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int addrlen = sizeof(addr);
    if (listener == INVALID_SOCKET || ::bind(listener, (sockaddr*)&addr, addrlen) != 0 ||
        ::listen(listener, SOMAXCONN) != 0 || ::getsockname(listener, (sockaddr*)&addr, &addrlen) != 0 ||
        (client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET ||
        ::connect(client, (sockaddr*)&addr, addrlen) != 0) {
        err = ::WSAGetLastError();
    }

    // Accept the client's own connection: another process may connect to the port first, so check the peer address.
    sockaddr_in self;
    memset(&self, 0, sizeof(self));
    int selflen = sizeof(self);
    if (!err && ::getsockname(client, (sockaddr*)&self, &selflen) != 0) err = ::WSAGetLastError();
    for (int i = 0; !err && server == INVALID_SOCKET && i < 16; i++) {
        sockaddr_in peer;
        int peerlen = sizeof(peer);
        SOCKET s = ::accept(listener, (sockaddr*)&peer, &peerlen);
        if (s == INVALID_SOCKET) {
            err = ::WSAGetLastError();
        } else if (peer.sin_port == self.sin_port && peer.sin_addr.s_addr == self.sin_addr.s_addr) {
            server = s;
        } else {
            ::closesocket(s);
        }
    }
    if (!err && server == INVALID_SOCKET) err = WSAECONNREFUSED;

    BOOL nodelay = TRUE;
    if (!err && (::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay)) != 0 ||
        ::setsockopt(server, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay)) != 0)) {
        err = ::WSAGetLastError();
    }

    if (listener != INVALID_SOCKET) ::closesocket(listener);
    if (err) {
        if (client != INVALID_SOCKET) ::closesocket(client);
        if (server != INVALID_SOCKET) ::closesocket(server);
        errno = _st_win64_errno(err, 0);
        return -1;
    }
    fds[0] = (int)client;
    fds[1] = (int)server;
    return 0;
}

// Winsock takes the option value as char*, so these overloads let tests pass any pointer as on POSIX.
static inline int setsockopt(int fd, int level, int name, const void* value, socklen_t size)
{
    return ::setsockopt((SOCKET)fd, level, name, (const char*)value, size);
}

static inline int getsockopt(int fd, int level, int name, void* value, socklen_t* size)
{
    return ::getsockopt((SOCKET)fd, level, name, (char*)value, size);
}

static inline ssize_t st_utest_send(int fd, const void* buf, size_t size)
{
    return ::send((SOCKET)fd, (const char*)buf, (int)size, 0);
}

static inline ssize_t st_utest_recv(int fd, void* buf, size_t size)
{
    return ::recv((SOCKET)fd, (char*)buf, (int)size, 0);
}

static inline int st_utest_close(int fd)
{
    return ::closesocket((SOCKET)fd);
}

// Whether the last send or recv failed because the socket wasn't ready.
static inline bool st_utest_would_block()
{
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
}
#else
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
#endif

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

