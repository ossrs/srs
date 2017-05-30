/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_service_st.hpp>

#include <st.h>
#include <fcntl.h>
#include <sys/socket.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_service_utility.hpp>
#include <srs_kernel_utility.hpp>

#ifdef __linux__
#include <sys/epoll.h>

bool srs_st_epoll_is_supported(void)
{
    struct epoll_event ev;
    
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;
    /* Guaranteed to fail */
    epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev);
    
    return (errno != ENOSYS);
}
#endif

int srs_st_init()
{
    int ret = ERROR_SUCCESS;
    
#ifdef __linux__
    // check epoll, some old linux donot support epoll.
    // @see https://github.com/ossrs/srs/issues/162
    if (!srs_st_epoll_is_supported()) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("epoll required on Linux. ret=%d", ret);
        return ret;
    }
#endif
    
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use %s failed. ret=%d", st_get_eventsys_name(), ret);
        return ret;
    }
    srs_info("st_set_eventsys to %s", st_get_eventsys_name());
    
    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }
    srs_trace("st_init success, use %s", st_get_eventsys_name());
    
    return ret;
}

void srs_close_stfd(srs_netfd_t& stfd)
{
    if (stfd) {
        // we must ensure the close is ok.
        int err = st_netfd_close((st_netfd_t)stfd);
        srs_assert(err != -1);
        stfd = NULL;
    }
}

void srs_fd_close_exec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    flags |= FD_CLOEXEC;
    fcntl(fd, F_SETFD, flags);
}

void srs_socket_reuse_addr(int fd)
{
    int v = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int));
}

srs_thread_t srs_thread_self()
{
    return (srs_thread_t)st_thread_self();
}

int srs_socket_connect(string server, int port, int64_t tm, srs_netfd_t* pstfd)
{
    int ret = ERROR_SUCCESS;
    
    st_utime_t timeout = ST_UTIME_NO_TIMEOUT;
    if (tm != SRS_CONSTS_NO_TMMS) {
        timeout = (st_utime_t)(tm * 1000);
    }
    
    *pstfd = NULL;
    srs_netfd_t stfd = NULL;
    sockaddr_in addr;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    srs_fd_close_exec(sock);
    
    srs_assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
    
    // connect to server.
    std::string ip = srs_dns_resolve(server);
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        goto failed;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect((st_netfd_t)stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), timeout) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        goto failed;
    }
    srs_info("connect ok. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
    *pstfd = stfd;
    return ret;
    
failed:
    if (stfd) {
        srs_close_stfd(stfd);
    }
    return ret;
}

srs_cond_t srs_cond_new()
{
    return (srs_cond_t)st_cond_new();
}

int srs_cond_destroy(srs_cond_t cond)
{
    return st_cond_destroy((st_cond_t)cond);
}

int srs_cond_wait(srs_cond_t cond)
{
    return st_cond_wait((st_cond_t)cond);
}

int srs_cond_timedwait(srs_cond_t cond, srs_utime_t timeout)
{
    return st_cond_timedwait((st_cond_t)cond, (st_utime_t)timeout);
}

int srs_cond_signal(srs_cond_t cond)
{
    return st_cond_signal((st_cond_t)cond);
}

srs_mutex_t srs_mutex_new()
{
    return (srs_mutex_t)st_mutex_new();
}

int srs_mutex_destroy(srs_mutex_t mutex)
{
    return st_mutex_destroy((st_mutex_t)mutex);
}

int srs_mutex_lock(srs_mutex_t mutex)
{
    return st_mutex_lock((st_mutex_t)mutex);
}

int srs_mutex_unlock(srs_mutex_t mutex)
{
    return st_mutex_unlock((st_mutex_t)mutex);
}

int srs_netfd_fileno(srs_netfd_t stfd)
{
    return st_netfd_fileno((st_netfd_t)stfd);
}

int srs_usleep(srs_utime_t usecs)
{
    return st_usleep((st_utime_t)usecs);
}

srs_netfd_t srs_netfd_open_socket(int osfd)
{
    return (srs_netfd_t)st_netfd_open_socket(osfd);
}

srs_netfd_t srs_netfd_open(int osfd)
{
    return (srs_netfd_t)st_netfd_open(osfd);
}

int srs_recvfrom(srs_netfd_t stfd, void *buf, int len, struct sockaddr *from, int *fromlen, srs_utime_t timeout)
{
    return st_recvfrom((st_netfd_t)stfd, buf, len, from, fromlen, (st_utime_t)timeout);
}

srs_netfd_t srs_accept(srs_netfd_t stfd, struct sockaddr *addr, int *addrlen, srs_utime_t timeout)
{
    return (srs_netfd_t)st_accept((st_netfd_t)stfd, addr, addrlen, (st_utime_t)timeout);
}

ssize_t srs_read(srs_netfd_t stfd, void *buf, size_t nbyte, srs_utime_t timeout)
{
    return st_read((st_netfd_t)stfd, buf, nbyte, (st_utime_t)timeout);
}

SrsStSocket::SrsStSocket()
{
    stfd = NULL;
    stm = rtm = SRS_CONSTS_NO_TMMS;
    rbytes = sbytes = 0;
}

SrsStSocket::~SrsStSocket()
{
}

int SrsStSocket::initialize(srs_netfd_t fd)
{
    stfd = fd;
    return ERROR_SUCCESS;
}

bool SrsStSocket::is_never_timeout(int64_t tm)
{
    return tm == SRS_CONSTS_NO_TMMS;
}

void SrsStSocket::set_recv_timeout(int64_t tm)
{
    rtm = tm;
}

int64_t SrsStSocket::get_recv_timeout()
{
    return rtm;
}

void SrsStSocket::set_send_timeout(int64_t tm)
{
    stm = tm;
}

int64_t SrsStSocket::get_send_timeout()
{
    return stm;
}

int64_t SrsStSocket::get_recv_bytes()
{
    return rbytes;
}

int64_t SrsStSocket::get_send_bytes()
{
    return sbytes;
}

int SrsStSocket::read(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read;
    if (rtm == SRS_CONSTS_NO_TMMS) {
        nb_read = st_read((st_netfd_t)stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read((st_netfd_t)stfd, buf, size, rtm * 1000);
    }
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value of 0 means the network connection is closed or end of file is reached).
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read == 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ;
    }
    
    rbytes += nb_read;
    
    return ret;
}

int SrsStSocket::read_fully(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read;
    if (rtm == SRS_CONSTS_NO_TMMS) {
        nb_read = st_read_fully((st_netfd_t)stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read_fully((st_netfd_t)stfd, buf, size, rtm * 1000);
    }
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read != (ssize_t)size) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read >= 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ_FULLY;
    }
    
    rbytes += nb_read;
    
    return ret;
}

int SrsStSocket::write(void* buf, size_t size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write;
    if (stm == SRS_CONSTS_NO_TMMS) {
        nb_write = st_write((st_netfd_t)stfd, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_write((st_netfd_t)stfd, buf, size, stm * 1000);
    }
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    sbytes += nb_write;
    
    return ret;
}

int SrsStSocket::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write;
    if (stm == SRS_CONSTS_NO_TMMS) {
        nb_write = st_writev((st_netfd_t)stfd, iov, iov_size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_writev((st_netfd_t)stfd, iov, iov_size, stm * 1000);
    }
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        // @see https://github.com/ossrs/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    sbytes += nb_write;
    
    return ret;
}

SrsTcpClient::SrsTcpClient(string h, int p, int64_t tm)
{
    stfd = NULL;
    io = new SrsStSocket();
    
    host = h;
    port = p;
    timeout = tm;
}

SrsTcpClient::~SrsTcpClient()
{
    close();
    
    srs_freep(io);
}

int SrsTcpClient::connect()
{
    int ret = ERROR_SUCCESS;
    
    close();
    
    srs_assert(stfd == NULL);
    if ((ret = srs_socket_connect(host, port, timeout, &stfd)) != ERROR_SUCCESS) {
        srs_error("connect tcp://%s:%d failed, to=%" PRId64 "ms. ret=%d", host.c_str(), port, timeout, ret);
        return ret;
    }
    
    if ((ret = io->initialize(stfd)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsTcpClient::close()
{
    // Ignore when already closed.
    if (!io) {
        return;
    }
    
    srs_close_stfd(stfd);
}

bool SrsTcpClient::is_never_timeout(int64_t tm)
{
    return io->is_never_timeout(tm);
}

void SrsTcpClient::set_recv_timeout(int64_t tm)
{
    io->set_recv_timeout(tm);
}

int64_t SrsTcpClient::get_recv_timeout()
{
    return io->get_recv_timeout();
}

void SrsTcpClient::set_send_timeout(int64_t tm)
{
    io->set_send_timeout(tm);
}

int64_t SrsTcpClient::get_send_timeout()
{
    return io->get_send_timeout();
}

int64_t SrsTcpClient::get_recv_bytes()
{
    return io->get_recv_bytes();
}

int64_t SrsTcpClient::get_send_bytes()
{
    return io->get_send_bytes();
}

int SrsTcpClient::read(void* buf, size_t size, ssize_t* nread)
{
    return io->read(buf, size, nread);
}

int SrsTcpClient::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return io->read_fully(buf, size, nread);
}

int SrsTcpClient::write(void* buf, size_t size, ssize_t* nwrite)
{
    return io->write(buf, size, nwrite);
}

int SrsTcpClient::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return io->writev(iov, iov_size, nwrite);
}

