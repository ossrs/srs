//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_ST_HPP
#define SRS_PROTOCOL_ST_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_protocol_io.hpp>
#include <srs_kernel_error.hpp>

// Wrap for coroutine.
typedef void* srs_netfd_t;
typedef void* srs_thread_t;
typedef void* srs_cond_t;
typedef void* srs_mutex_t;


#ifdef SRS_SANITIZER
// Setup the primordial stack for asan detecting.
void srs_set_primordial_stack(void* stack_top);
#endif

// Initialize ST, requires epoll for linux.
extern srs_error_t srs_st_init();
// Destroy ST, free resources for asan detecting.
extern void srs_st_destroy(void);

// Close the netfd, and close the underlayer fd.
// @remark when close, user must ensure io completed.
extern void srs_close_stfd(srs_netfd_t& stfd);

// Set the FD_CLOEXEC of FD.
extern srs_error_t srs_fd_closeexec(int fd);

// Set the SO_REUSEADDR of fd.
extern srs_error_t srs_fd_reuseaddr(int fd);

// Set the SO_REUSEPORT of fd.
extern srs_error_t srs_fd_reuseport(int fd);

// Set the SO_KEEPALIVE of fd.
extern srs_error_t srs_fd_keepalive(int fd);

// Get current coroutine/thread.
extern srs_thread_t srs_thread_self();
extern void srs_thread_exit(void* retval);
extern int srs_thread_join(srs_thread_t thread, void **retvalp);
extern void srs_thread_interrupt(srs_thread_t thread);
extern void srs_thread_yield();

// For utest to mock the thread create.
typedef void* (*_ST_THREAD_CREATE_PFN)(void *(*start)(void *arg), void *arg, int joinable, int stack_size);
extern _ST_THREAD_CREATE_PFN _pfn_st_thread_create;

// For client, to open socket and connect to server.
// @param tm The timeout in srs_utime_t.
extern srs_error_t srs_tcp_connect(std::string server, int port, srs_utime_t tm, srs_netfd_t* pstfd);

// For server, listen at TCP endpoint.
extern srs_error_t srs_tcp_listen(std::string ip, int port, srs_netfd_t* pfd);

// For server, listen at UDP endpoint.
extern srs_error_t srs_udp_listen(std::string ip, int port, srs_netfd_t* pfd);

// Wrap for coroutine.
extern srs_cond_t srs_cond_new();
extern int srs_cond_destroy(srs_cond_t cond);
extern int srs_cond_wait(srs_cond_t cond);
extern int srs_cond_timedwait(srs_cond_t cond, srs_utime_t timeout);
extern int srs_cond_signal(srs_cond_t cond);
extern int srs_cond_broadcast(srs_cond_t cond);

extern srs_mutex_t srs_mutex_new();
extern int srs_mutex_destroy(srs_mutex_t mutex);
extern int srs_mutex_lock(srs_mutex_t mutex);
extern int srs_mutex_unlock(srs_mutex_t mutex);

extern int srs_key_create(int* keyp, void (*destructor)(void*));
extern int srs_thread_setspecific(int key, void* value);
extern int srs_thread_setspecific2(srs_thread_t thread, int key, void* value);
extern void* srs_thread_getspecific(int key);

extern int srs_netfd_fileno(srs_netfd_t stfd);

extern int srs_usleep(srs_utime_t usecs);

extern srs_netfd_t srs_netfd_open_socket(int osfd);
extern srs_netfd_t srs_netfd_open(int osfd);

extern int srs_recvfrom(srs_netfd_t stfd, void *buf, int len, struct sockaddr *from, int *fromlen, srs_utime_t timeout);
extern int srs_sendto(srs_netfd_t stfd, void *buf, int len, const struct sockaddr *to, int tolen, srs_utime_t timeout);
extern int srs_recvmsg(srs_netfd_t stfd, struct msghdr *msg, int flags, srs_utime_t timeout);
extern int srs_sendmsg(srs_netfd_t stfd, const struct msghdr *msg, int flags, srs_utime_t timeout);

extern srs_netfd_t srs_accept(srs_netfd_t stfd, struct sockaddr *addr, int *addrlen, srs_utime_t timeout);

extern ssize_t srs_read(srs_netfd_t stfd, void *buf, size_t nbyte, srs_utime_t timeout);

extern bool srs_is_never_timeout(srs_utime_t tm);

// The mutex locker.
#define SrsLocker(instance) \
    impl__SrsLocker _SRS_free_##instance(&instance)

class impl__SrsLocker
{
private:
    srs_mutex_t* lock;
public:
    impl__SrsLocker(srs_mutex_t* l) {
        lock = l;
        int r0 = srs_mutex_lock(*lock);
        srs_assert(!r0);
    }
    virtual ~impl__SrsLocker() {
        int r0 = srs_mutex_unlock(*lock);
        srs_assert(!r0);
    }
};

// the socket provides TCP socket over st,
// that is, the sync socket mechanism.
class SrsStSocket : public ISrsProtocolReadWriter
{
private:
    // The recv/send timeout in srs_utime_t.
    // @remark Use SRS_UTIME_NO_TIMEOUT for never timeout.
    srs_utime_t rtm;
    srs_utime_t stm;
    // The recv/send data in bytes
    int64_t rbytes;
    int64_t sbytes;
    // The underlayer st fd.
    srs_netfd_t stfd_;
public:
    SrsStSocket();
    SrsStSocket(srs_netfd_t fd);
    virtual ~SrsStSocket();
private:
    void init(srs_netfd_t fd);
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
public:
    // @param nread, the actual read bytes, ignore if NULL.
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    // @param nwrite, the actual write bytes, ignore if NULL.
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

// The client to connect to server over TCP.
// User must never reuse the client when close it.
// Usage:
//      SrsTcpClient client("127.0.0.1", 1935, 9 * SRS_UTIME_SECONDS);
//      client.connect();
//      client.write("Hello world!", 12, NULL);
//      client.read(buf, 4096, NULL);
// @remark User can directly free the object, which will close the fd.
class SrsTcpClient : public ISrsProtocolReadWriter
{
private:
    srs_netfd_t stfd_;
    SrsStSocket* io;
private:
    std::string host;
    int port;
    // The timeout in srs_utime_t.
    srs_utime_t timeout;
public:
    // Constructor.
    // @param h the ip or hostname of server.
    // @param p the port to connect to.
    // @param tm the timeout in srs_utime_t.
    SrsTcpClient(std::string h, int p, srs_utime_t tm);
    virtual ~SrsTcpClient();
public:
    // Connect to server over TCP.
    // @remark We will close the exists connection before do connect.
    virtual srs_error_t connect();
// Interface ISrsProtocolReadWriter
public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

#endif

