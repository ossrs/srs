//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_ST_HPP
#define SRS_PROTOCOL_ST_HPP

#include <srs_core.hpp>

#include <errno.h>
#include <string>

#include <srs_kernel_error.hpp>
#include <srs_kernel_st.hpp>
#include <srs_protocol_io.hpp>

typedef int srs_srt_t;

typedef void *srs_netfd_t;
typedef void *srs_thread_t;
typedef void *srs_cond_t;
typedef void *srs_mutex_t;

#ifdef SRS_SANITIZER
void srs_set_primordial_stack(void *stack_top);
#endif

extern srs_error_t srs_st_init();
// Frees ST's resources, for ASan.
extern void srs_st_destroy(void);

// The caller must make sure no IO is in progress on the fd.
extern void srs_close_stfd(srs_netfd_t &stfd);
extern void srs_close_stfd_ptr(srs_netfd_t *stfd);

extern srs_error_t srs_fd_closeexec(int fd);

extern srs_error_t srs_fd_reuseaddr(int fd);

extern srs_error_t srs_fd_reuseport(int fd);

extern srs_error_t srs_fd_keepalive(int fd);

extern srs_thread_t srs_thread_self();
extern void srs_thread_exit(void *retval);
extern int srs_thread_join(srs_thread_t thread, void **retvalp);
extern void srs_thread_interrupt(srs_thread_t thread);
extern void srs_thread_yield();

// For utest to mock the thread create.
typedef void *(*_ST_THREAD_CREATE_PFN)(void *(*start)(void *arg), void *arg, int joinable, int stack_size);
extern _ST_THREAD_CREATE_PFN _pfn_st_thread_create;

extern srs_error_t srs_tcp_connect(std::string server, int port, srs_utime_t tm, srs_netfd_t *pstfd);

extern srs_error_t srs_tcp_listen(std::string ip, int port, srs_netfd_t *pfd);

extern srs_error_t srs_udp_listen(std::string ip, int port, srs_netfd_t *pfd);

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

// A class, so SrsUniquePtr and SrsSharedPtr can manage it.
class SrsCond : public ISrsCond
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_cond_t cond_;

public:
    SrsCond();
    virtual ~SrsCond();

public:
    int wait();
    int timedwait(srs_utime_t timeout);
    int signal();
    int broadcast();
};

// A class, so SrsUniquePtr and SrsSharedPtr can manage it.
class SrsMutex
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t mutex_;

public:
    SrsMutex();
    virtual ~SrsMutex();

public:
    int lock();
    int unlock();
    srs_mutex_t *get();
};

extern int srs_key_create(int *keyp, void (*destructor)(void *));
extern int srs_thread_setspecific(int key, void *value);
extern int srs_thread_setspecific2(srs_thread_t thread, int key, void *value);
extern void *srs_thread_getspecific(int key);

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

#define SrsLocker(instance) \
    impl__SrsLocker _SRS_free_instance(instance)

class impl__SrsLocker
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t *lock_;

public:
    impl__SrsLocker(srs_mutex_t *l)
    {
        lock_ = l;

        // An interrupt pending or arriving while waiting fails the lock with EINTR. The coroutine keeps the stop and
        // its cycle checks it by pull(), so take the lock anyway, as stop() does for the join.
        int r0 = 0;
        while ((r0 = srs_mutex_lock(*lock_)) != 0 && errno == EINTR) {
        }
        srs_assert(!r0);
    }
    virtual ~impl__SrsLocker()
    {
        int r0 = srs_mutex_unlock(*lock_);
        srs_assert(!r0);
    }
};

class SrsStSocket : public ISrsProtocolReadWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t rtm_;
    srs_utime_t stm_;
    int64_t rbytes_;
    int64_t sbytes_;
    srs_netfd_t stfd_;

public:
    SrsStSocket();
    SrsStSocket(srs_netfd_t fd);
    virtual ~SrsStSocket();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void init(srs_netfd_t fd);

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

class SrsTcpClient : public ISrsProtocolReadWriter
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_netfd_t stfd_;
    SrsStSocket *io_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string host_;
    int port_;
    srs_utime_t timeout_;

public:
    SrsTcpClient(std::string h, int p, srs_utime_t tm);
    virtual ~SrsTcpClient();

public:
    virtual srs_error_t connect();

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

#endif
