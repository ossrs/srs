/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_SERVICE_ST_HPP
#define SRS_SERVICE_ST_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_protocol_io.hpp>

// Wrap for coroutine.
typedef void* srs_netfd_t;
typedef void* srs_thread_t;
typedef void* srs_cond_t;
typedef void* srs_mutex_t;
typedef uint64_t srs_utime_t;

#define SRS_UTIME_NO_TIMEOUT ((srs_utime_t) -1LL)

// initialize st, requires epoll.
extern srs_error_t srs_st_init();

// close the netfd, and close the underlayer fd.
// @remark when close, user must ensure io completed.
extern void srs_close_stfd(srs_netfd_t& stfd);

// Set the FD_CLOEXEC of FD.
extern void srs_fd_close_exec(int fd);

// Set the SO_REUSEADDR of socket.
extern void srs_socket_reuse_addr(int fd);

// Get current coroutine/thread.
extern srs_thread_t srs_thread_self();

// client open socket and connect to server.
// @param tm The timeout in ms.
extern srs_error_t srs_socket_connect(std::string server, int port, int64_t tm, srs_netfd_t* pstfd);

// Wrap for coroutine.
extern srs_cond_t srs_cond_new();
extern int srs_cond_destroy(srs_cond_t cond);
extern int srs_cond_wait(srs_cond_t cond);
extern int srs_cond_timedwait(srs_cond_t cond, srs_utime_t timeout);
extern int srs_cond_signal(srs_cond_t cond);

extern srs_mutex_t srs_mutex_new();
extern int srs_mutex_destroy(srs_mutex_t mutex);
extern int srs_mutex_lock(srs_mutex_t mutex);
extern int srs_mutex_unlock(srs_mutex_t mutex);

extern int srs_netfd_fileno(srs_netfd_t stfd);

extern int srs_usleep(srs_utime_t usecs);

extern srs_netfd_t srs_netfd_open_socket(int osfd);
extern srs_netfd_t srs_netfd_open(int osfd);

extern int srs_recvfrom(srs_netfd_t stfd, void *buf, int len, struct sockaddr *from, int *fromlen, srs_utime_t timeout);

extern srs_netfd_t srs_accept(srs_netfd_t stfd, struct sockaddr *addr, int *addrlen, srs_utime_t timeout);

extern ssize_t srs_read(srs_netfd_t stfd, void *buf, size_t nbyte, srs_utime_t timeout);

/**
 * The mutex locker.
 */
#define SrsLocker(instance) \
    impl__SrsLocker _srs_auto_free_##instance(&instance)

class impl__SrsLocker
{
private:
    srs_mutex_t* lock;
public:
    impl__SrsLocker(srs_mutex_t* l) : lock(l) {
        int r0 = srs_mutex_lock(lock);
        srs_assert(!r0);
    }
    virtual ~impl__SrsLocker() {
        int r0 = srs_mutex_unlock(lock);
        srs_assert(!r0);
    }
};

/**
 * the socket provides TCP socket over st,
 * that is, the sync socket mechanism.
 */
class SrsStSocket : public ISrsProtocolReaderWriter
{
private:
    // The recv/send timeout in ms.
    // @remark Use SRS_CONSTS_NO_TMMS for never timeout in ms.
    int64_t rtm;
    int64_t stm;
    // The recv/send data in bytes
    int64_t rbytes;
    int64_t sbytes;
    // The underlayer st fd.
    srs_netfd_t stfd;
public:
    SrsStSocket();
    virtual ~SrsStSocket();
public:
    // Initialize the socket with stfd, user must manage it.
    virtual srs_error_t initialize(srs_netfd_t fd);
public:
    virtual bool is_never_timeout(int64_t tm);
    virtual void set_recv_timeout(int64_t tm);
    virtual int64_t get_recv_timeout();
    virtual void set_send_timeout(int64_t tm);
    virtual int64_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
public:
    /**
     * @param nread, the actual read bytes, ignore if NULL.
     */
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    /**
     * @param nwrite, the actual write bytes, ignore if NULL.
     */
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

/**
 * The client to connect to server over TCP.
 * User must never reuse the client when close it.
 * Usage:
 *      SrsTcpClient client("127.0.0.1", 1935,9000);
 *      client.connect();
 *      client.write("Hello world!", 12, NULL);
 *      client.read(buf, 4096, NULL);
 * @remark User can directly free the object, which will close the fd.
 */
class SrsTcpClient : public ISrsProtocolReaderWriter
{
private:
    srs_netfd_t stfd;
    SrsStSocket* io;
private:
    std::string host;
    int port;
    // The timeout in ms.
    int64_t timeout;
public:
    /**
     * Constructor.
     * @param h the ip or hostname of server.
     * @param p the port to connect to.
     * @param tm the timeout in ms.
     */
    SrsTcpClient(std::string h, int p, int64_t tm);
    virtual ~SrsTcpClient();
public:
    /**
     * Connect to server over TCP.
     * @remark We will close the exists connection before do connect.
     */
    virtual srs_error_t connect();
private:
    /**
     * Close the connection to server.
     * @remark User should never use the client when close it.
     */
    virtual void close();
// interface ISrsProtocolReaderWriter
public:
    virtual bool is_never_timeout(int64_t tm);
    virtual void set_recv_timeout(int64_t tm);
    virtual int64_t get_recv_timeout();
    virtual void set_send_timeout(int64_t tm);
    virtual int64_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t read_fully(void* buf, size_t size, ssize_t* nread);
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

#endif

