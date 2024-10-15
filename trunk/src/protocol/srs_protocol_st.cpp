//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_st.hpp>

#include <st.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <netdb.h>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_deprecated.hpp>

// nginx also set to 512
#define SERVER_LISTEN_BACKLOG 512

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

#ifdef SRS_SANITIZER
void srs_set_primordial_stack(void* stack_top)
{
    if (!stack_top) {
        return;
    }

    struct rlimit limit;
    if (getrlimit (RLIMIT_STACK, &limit) != 0) {
        return;
    }

    void* stack_bottom = (char*)stack_top - (uint64_t)limit.rlim_cur;
    st_set_primordial_stack(stack_top, stack_bottom);
}
#endif

srs_error_t srs_st_init()
{
#ifdef __linux__
    // check epoll, some old linux donot support epoll.
    if (!srs_st_epoll_is_supported()) {
        return srs_error_new(ERROR_ST_SET_EPOLL, "linux epoll disabled");
    }
#endif
    
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue.
#if defined(SRS_CYGWIN64)
    if (st_set_eventsys(ST_EVENTSYS_SELECT) == -1) {
        return srs_error_new(ERROR_ST_SET_SELECT, "st enable st failed, current is %s", st_get_eventsys_name());
    }
#else
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        return srs_error_new(ERROR_ST_SET_EPOLL, "st enable st failed, current is %s", st_get_eventsys_name());
    }
#endif

    // Before ST init, we might have already initialized the background cid.
    SrsContextId cid = _srs_context->get_id();
    if (cid.empty()) {
        cid = _srs_context->generate_id();
    }
    
    int r0 = 0;
    if((r0 = st_init()) != 0){
        return srs_error_new(ERROR_ST_INITIALIZE, "st initialize failed, r0=%d", r0);
    }

    // Switch to the background cid.
    _srs_context->set_id(cid);
    srs_info("st_init success, use %s", st_get_eventsys_name());
    
    return srs_success;
}

void srs_st_destroy(void)
{
    st_destroy();
}

void srs_close_stfd(srs_netfd_t& stfd)
{
    if (stfd) {
        // we must ensure the close is ok.
        int r0 = st_netfd_close((st_netfd_t)stfd);
        if (r0) {
            // By _st_epoll_fd_close or _st_kq_fd_close
            if (errno == EBUSY) srs_assert(!r0);
            // By close
            if (errno == EBADF) srs_assert(!r0);
            if (errno == EINTR) srs_assert(!r0);
            if (errno == EIO) srs_assert(!r0);
            // Others
            srs_assert(!r0);
        }
        stfd = NULL;
    }
}

srs_error_t srs_fd_closeexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    flags |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, flags) == -1) {
        return srs_error_new(ERROR_SOCKET_SETCLOSEEXEC, "FD_CLOEXEC fd=%d", fd);
    }

    return srs_success;
}

srs_error_t srs_fd_reuseaddr(int fd)
{
    int v = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int)) == -1) {
        return srs_error_new(ERROR_SOCKET_SETREUSEADDR, "SO_REUSEADDR fd=%d", fd);
    }

    return srs_success;
}

srs_error_t srs_fd_reuseport(int fd)
{
#if defined(SO_REUSEPORT)
    int v = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(int)) == -1) {
        srs_warn("SO_REUSEPORT failed for fd=%d", fd);
    }
#else
    #warning "SO_REUSEPORT is not supported by your OS"
    srs_warn("SO_REUSEPORT is not supported util Linux kernel 3.9");
#endif

    return srs_success;
}

srs_error_t srs_fd_keepalive(int fd)
{
#ifdef SO_KEEPALIVE
    int v = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(int)) == -1) {
        return srs_error_new(ERROR_SOCKET_SETKEEPALIVE, "SO_KEEPALIVE fd=%d", fd);
    }
#endif

    return srs_success;
}

srs_thread_t srs_thread_self()
{
    return (srs_thread_t)st_thread_self();
}

void srs_thread_exit(void* retval)
{
    st_thread_exit(retval);
}

int srs_thread_join(srs_thread_t thread, void **retvalp)
{
    return st_thread_join((st_thread_t)thread, retvalp);
}

void srs_thread_interrupt(srs_thread_t thread)
{
    st_thread_interrupt((st_thread_t)thread);
}

void srs_thread_yield()
{
    st_thread_yield();
}

_ST_THREAD_CREATE_PFN _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)st_thread_create;

srs_error_t srs_tcp_connect(string server, int port, srs_utime_t tm, srs_netfd_t* pstfd)
{
    st_utime_t timeout = ST_UTIME_NO_TIMEOUT;
    if (tm != SRS_UTIME_NO_TIMEOUT) {
        timeout = tm;
    }
    
    *pstfd = NULL;
    srs_netfd_t stfd = NULL;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    srs_assert(r0 > 0 && r0 < (int)sizeof(sport));
    
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    addrinfo* r  = NULL;
    SrsAutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(server.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "get address info");
    }
    
    int sock = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
    if(sock == -1){
        return srs_error_new(ERROR_SOCKET_CREATE, "create socket");
    }
    
    srs_assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ::close(sock);
        return srs_error_new(ERROR_ST_OPEN_SOCKET, "open socket");
    }
    
    if (st_connect((st_netfd_t)stfd, r->ai_addr, r->ai_addrlen, timeout) == -1){
        srs_close_stfd(stfd);
        return srs_error_new(ERROR_ST_CONNECT, "connect to %s:%d", server.c_str(), port);
    }
    
    *pstfd = stfd;
    return srs_success;
}

srs_error_t do_srs_tcp_listen(int fd, addrinfo* r, srs_netfd_t* pfd)
{
    srs_error_t err = srs_success;

    // Detect alive for TCP connection.
    // @see https://github.com/ossrs/srs/issues/1044
    if ((err = srs_fd_keepalive(fd)) != srs_success) {
        return srs_error_wrap(err, "set keepalive");
    }

    if ((err = srs_fd_closeexec(fd)) != srs_success) {
        return srs_error_wrap(err, "set closeexec");
    }

    if ((err = srs_fd_reuseaddr(fd)) != srs_success) {
        return srs_error_wrap(err, "set reuseaddr");
    }

    if ((err = srs_fd_reuseport(fd)) != srs_success) {
        return srs_error_wrap(err, "set reuseport");
    }

    if (::bind(fd, r->ai_addr, r->ai_addrlen) == -1) {
        return srs_error_new(ERROR_SOCKET_BIND, "bind");
    }

    if (::listen(fd, SERVER_LISTEN_BACKLOG) == -1) {
        return srs_error_new(ERROR_SOCKET_LISTEN, "listen");
    }

    if ((*pfd = srs_netfd_open_socket(fd)) == NULL){
        return srs_error_new(ERROR_ST_OPEN_SOCKET, "st open");
    }

    return err;
}

srs_error_t srs_tcp_listen(std::string ip, int port, srs_netfd_t* pfd)
{
    srs_error_t err = srs_success;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    srs_assert(r0 > 0 && r0 < (int)sizeof(sport));

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICHOST;

    addrinfo* r = NULL;
    SrsAutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "getaddrinfo hints=(%d,%d,%d)",
            hints.ai_family, hints.ai_socktype, hints.ai_flags);
    }

    int fd = 0;
    if ((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
        return srs_error_new(ERROR_SOCKET_CREATE, "socket domain=%d, type=%d, protocol=%d",
            r->ai_family, r->ai_socktype, r->ai_protocol);
    }

    if ((err = do_srs_tcp_listen(fd, r, pfd)) != srs_success) {
        ::close(fd);
        return srs_error_wrap(err, "fd=%d", fd);
    }

    return err;
}

srs_error_t do_srs_udp_listen(int fd, addrinfo* r, srs_netfd_t* pfd)
{
    srs_error_t err = srs_success;

    if ((err = srs_fd_closeexec(fd)) != srs_success) {
        return srs_error_wrap(err, "set closeexec");
    }

    if ((err = srs_fd_reuseaddr(fd)) != srs_success) {
        return srs_error_wrap(err, "set reuseaddr");
    }

    if ((err = srs_fd_reuseport(fd)) != srs_success) {
        return srs_error_wrap(err, "set reuseport");
    }

    if (::bind(fd, r->ai_addr, r->ai_addrlen) == -1) {
        return srs_error_new(ERROR_SOCKET_BIND, "bind");
    }

    if ((*pfd = srs_netfd_open_socket(fd)) == NULL){
        return srs_error_new(ERROR_ST_OPEN_SOCKET, "st open");
    }

    return err;
}

srs_error_t srs_udp_listen(std::string ip, int port, srs_netfd_t* pfd)
{
    srs_error_t err = srs_success;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    srs_assert(r0 > 0 && r0 < (int)sizeof(sport));

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags    = AI_NUMERICHOST;

    addrinfo* r  = NULL;
    SrsAutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "getaddrinfo hints=(%d,%d,%d)",
            hints.ai_family, hints.ai_socktype, hints.ai_flags);
    }

    int fd = 0;
    if ((fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
        return srs_error_new(ERROR_SOCKET_CREATE, "socket domain=%d, type=%d, protocol=%d",
            r->ai_family, r->ai_socktype, r->ai_protocol);
    }

    if ((err = do_srs_udp_listen(fd, r, pfd)) != srs_success) {
        ::close(fd);
        return srs_error_wrap(err, "fd=%d", fd);
    }

    return err;
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

int srs_cond_broadcast(srs_cond_t cond)
{
    return st_cond_broadcast((st_cond_t)cond);
}

srs_mutex_t srs_mutex_new()
{
    return (srs_mutex_t)st_mutex_new();
}

int srs_mutex_destroy(srs_mutex_t mutex)
{
    if (!mutex) {
        return 0;
    }
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

int srs_key_create(int *keyp, void (*destructor)(void *))
{
    return st_key_create(keyp, destructor);
}

int srs_thread_setspecific(int key, void *value)
{
    return st_thread_setspecific(key, value);
}

void *srs_thread_getspecific(int key)
{
    return st_thread_getspecific(key);
}

int srs_thread_setspecific2(srs_thread_t thread, int key, void* value)
{
    return st_thread_setspecific2((st_thread_t)thread, key, value);
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

int srs_sendto(srs_netfd_t stfd, void *buf, int len, const struct sockaddr * to, int tolen, srs_utime_t timeout)
{
    return st_sendto((st_netfd_t)stfd, buf, len, to, tolen, (st_utime_t)timeout);
}

int srs_recvmsg(srs_netfd_t stfd, struct msghdr *msg, int flags, srs_utime_t timeout)
{
    return st_recvmsg((st_netfd_t)stfd, msg, flags, (st_utime_t)timeout);
}

int srs_sendmsg(srs_netfd_t stfd, const struct msghdr *msg, int flags, srs_utime_t timeout)
{
    return st_sendmsg((st_netfd_t)stfd, msg, flags, (st_utime_t)timeout);
}

srs_netfd_t srs_accept(srs_netfd_t stfd, struct sockaddr *addr, int *addrlen, srs_utime_t timeout)
{
    return (srs_netfd_t)st_accept((st_netfd_t)stfd, addr, addrlen, (st_utime_t)timeout);
}

ssize_t srs_read(srs_netfd_t stfd, void *buf, size_t nbyte, srs_utime_t timeout)
{
    return st_read((st_netfd_t)stfd, buf, nbyte, (st_utime_t)timeout);
}

bool srs_is_never_timeout(srs_utime_t tm)
{
    return tm == SRS_UTIME_NO_TIMEOUT;
}

SrsStSocket::SrsStSocket()
{
    init(NULL);
}

SrsStSocket::SrsStSocket(srs_netfd_t fd)
{
    init(fd);
}

SrsStSocket::~SrsStSocket()
{
}

void SrsStSocket::init(srs_netfd_t fd)
{
    stfd_ = fd;
    stm = rtm = SRS_UTIME_NO_TIMEOUT;
    rbytes = sbytes = 0;
}

void SrsStSocket::set_recv_timeout(srs_utime_t tm)
{
    rtm = tm;
}

srs_utime_t SrsStSocket::get_recv_timeout()
{
    return rtm;
}

void SrsStSocket::set_send_timeout(srs_utime_t tm)
{
    stm = tm;
}

srs_utime_t SrsStSocket::get_send_timeout()
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

srs_error_t SrsStSocket::read(void* buf, size_t size, ssize_t* nread)
{
    srs_error_t err = srs_success;

    srs_assert(stfd_);

    ssize_t nb_read;
    if (rtm == SRS_UTIME_NO_TIMEOUT) {
        nb_read = st_read((st_netfd_t)stfd_, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read((st_netfd_t)stfd_, buf, size, rtm);
    }
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value of 0 means the network connection is closed or end of file is reached).
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read <= 0) {
        if (nb_read < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "timeout %d ms", srsu2msi(rtm));
        }
        
        if (nb_read == 0) {
            errno = ECONNRESET;
        }
        
        return srs_error_new(ERROR_SOCKET_READ, "read");
    }
    
    rbytes += nb_read;
    
    return err;
}

srs_error_t SrsStSocket::read_fully(void* buf, size_t size, ssize_t* nread)
{
    srs_error_t err = srs_success;

    srs_assert(stfd_);
    
    ssize_t nb_read;
    if (rtm == SRS_UTIME_NO_TIMEOUT) {
        nb_read = st_read_fully((st_netfd_t)stfd_, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_read = st_read_fully((st_netfd_t)stfd_, buf, size, rtm);
    }
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_read != (ssize_t)size) {
        if (nb_read < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "timeout %d ms", srsu2msi(rtm));
        }
        
        if (nb_read >= 0) {
            errno = ECONNRESET;
        }
        
        return srs_error_new(ERROR_SOCKET_READ_FULLY, "read fully, size=%d, nn=%d", size, nb_read);
    }
    
    rbytes += nb_read;
    
    return err;
}

srs_error_t SrsStSocket::write(void* buf, size_t size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    srs_assert(stfd_);
    
    ssize_t nb_write;
    if (stm == SRS_UTIME_NO_TIMEOUT) {
        nb_write = st_write((st_netfd_t)stfd_, buf, size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_write((st_netfd_t)stfd_, buf, size, stm);
    }
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "write timeout %d ms", srsu2msi(stm));
        }
        
        return srs_error_new(ERROR_SOCKET_WRITE, "write");
    }
    
    sbytes += nb_write;
    
    return err;
}

srs_error_t SrsStSocket::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    srs_assert(stfd_);
    
    ssize_t nb_write;
    if (stm == SRS_UTIME_NO_TIMEOUT) {
        nb_write = st_writev((st_netfd_t)stfd_, iov, iov_size, ST_UTIME_NO_TIMEOUT);
    } else {
        nb_write = st_writev((st_netfd_t)stfd_, iov, iov_size, stm);
    }
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned.
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "writev timeout %d ms", srsu2msi(stm));
        }
        
        return srs_error_new(ERROR_SOCKET_WRITE, "writev");
    }
    
    sbytes += nb_write;
    
    return err;
}

SrsTcpClient::SrsTcpClient(string h, int p, srs_utime_t tm)
{
    stfd_ = NULL;
    io = new SrsStSocket();
    
    host = h;
    port = p;
    timeout = tm;
}

SrsTcpClient::~SrsTcpClient()
{
    srs_freep(io);
    srs_close_stfd(stfd_);
}

srs_error_t SrsTcpClient::connect()
{
    srs_error_t err = srs_success;
    
    srs_netfd_t stfd = NULL;
    if ((err = srs_tcp_connect(host, port, timeout, &stfd)) != srs_success) {
        return srs_error_wrap(err, "tcp: connect %s:%d to=%dms", host.c_str(), port, srsu2msi(timeout));
    }

    // TODO: FIMXE: The timeout set on io need to be set to new object.
    srs_freep(io);
    io = new SrsStSocket(stfd);

    srs_close_stfd(stfd_);
    stfd_ = stfd;
    
    return err;
}

void SrsTcpClient::set_recv_timeout(srs_utime_t tm)
{
    io->set_recv_timeout(tm);
}

srs_utime_t SrsTcpClient::get_recv_timeout()
{
    return io->get_recv_timeout();
}

void SrsTcpClient::set_send_timeout(srs_utime_t tm)
{
    io->set_send_timeout(tm);
}

srs_utime_t SrsTcpClient::get_send_timeout()
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

srs_error_t SrsTcpClient::read(void* buf, size_t size, ssize_t* nread)
{
    return io->read(buf, size, nread);
}

srs_error_t SrsTcpClient::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return io->read_fully(buf, size, nread);
}

srs_error_t SrsTcpClient::write(void* buf, size_t size, ssize_t* nwrite)
{
    return io->write(buf, size, nwrite);
}

srs_error_t SrsTcpClient::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return io->writev(iov, iov_size, nwrite);
}

