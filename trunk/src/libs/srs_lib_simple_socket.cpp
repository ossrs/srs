/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_lib_simple_socket.hpp>

#include <srs_kernel_error.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
    #define SOCKET_ETIME EWOULDBLOCK
    #define SOCKET_ECONNRESET ECONNRESET

    #define SOCKET_ERRNO() errno
    #define SOCKET_RESET(fd) fd = -1; (void)0
    #define SOCKET_CLOSE(fd) \
        if (fd > 0) {\
            ::close(fd); \
            fd = -1; \
        } \
        (void)0
    #define SOCKET_VALID(x) (x > 0)
    #define SOCKET_SETUP() (void)0
    #define SOCKET_CLEANUP() (void)0
#else
    #define SOCKET_ETIME WSAETIMEDOUT
    #define SOCKET_ECONNRESET WSAECONNRESET
    #define SOCKET_ERRNO() WSAGetLastError()
    #define SOCKET_RESET(x) x=INVALID_SOCKET
    #define SOCKET_CLOSE(x) if(x!=INVALID_SOCKET){::closesocket(x);x=INVALID_SOCKET;}
    #define SOCKET_VALID(x) (x!=INVALID_SOCKET)
    #define SOCKET_BUFF(x) ((char*)x)
    #define SOCKET_SETUP() socket_setup()
    #define SOCKET_CLEANUP() socket_cleanup()
#endif

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <sys/uio.h>
#endif

#include <sys/types.h>
#include <errno.h>

#include <srs_kernel_utility.hpp>
#include <srs_kernel_consts.hpp>

// when io not hijacked, use simple socket, the block sync stream.
#ifndef SRS_HIJACK_IO
    struct SrsBlockSyncSocket
    {
        SOCKET fd;
        int64_t rbytes;
        int64_t sbytes;
        // The send/recv timeout in ms.
        int64_t rtm;
        int64_t stm;
        
        SrsBlockSyncSocket() {
            stm = rtm = SRS_CONSTS_NO_TMMS;
            rbytes = sbytes = 0;
            
            SOCKET_RESET(fd);
            SOCKET_SETUP();
        }
        
        virtual ~SrsBlockSyncSocket() {
            SOCKET_CLOSE(fd);
            SOCKET_CLEANUP();
        }
    };
    srs_hijack_io_t srs_hijack_io_create()
    {
        SrsBlockSyncSocket* skt = new SrsBlockSyncSocket();
        return skt;
    }
    void srs_hijack_io_destroy(srs_hijack_io_t ctx)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        srs_freep(skt);
    }
    int srs_hijack_io_create_socket(srs_hijack_io_t ctx, srs_rtmp_t owner)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        skt->fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (!SOCKET_VALID(skt->fd)) {
            return ERROR_SOCKET_CREATE;
        }
    
        return ERROR_SUCCESS;
    }
    int srs_hijack_io_connect(srs_hijack_io_t ctx, const char* server_ip, int port)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(server_ip);
        
        if(::connect(skt->fd, (const struct sockaddr*)&addr, sizeof(sockaddr_in)) < 0){
            return ERROR_SOCKET_CONNECT;
        }
        
        return ERROR_SUCCESS;
    }
    int srs_hijack_io_read(srs_hijack_io_t ctx, void* buf, size_t size, ssize_t* nread)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        int ret = ERROR_SUCCESS;
        
        ssize_t nb_read = ::recv(skt->fd, (char*)buf, size, 0);
        
        if (nread) {
            *nread = nb_read;
        }
        
        // On success a non-negative integer indicating the number of bytes actually read is returned 
        // (a value of 0 means the network connection is closed or end of file is reached).
        if (nb_read <= 0) {
            if (nb_read < 0 && SOCKET_ERRNO() == SOCKET_ETIME) {
                return ERROR_SOCKET_TIMEOUT;
            }
            
            if (nb_read == 0) {
                errno = SOCKET_ECONNRESET;
            }
            
            return ERROR_SOCKET_READ;
        }
        
        skt->rbytes += nb_read;
        
        return ret;
    }
    int srs_hijack_io_set_recv_timeout(srs_hijack_io_t ctx, int64_t tm)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        // The default for this option is zero,
        // which indicates that a receive operation shall not time out.
        int32_t sec = 0;
        int32_t usec = 0;
        
        if (tm != SRS_CONSTS_NO_TMMS) {
            sec = (int32_t)(tm / 1000);
            usec = (int32_t)((tm % 1000)*1000);
        }
        
        struct timeval tv = { sec , usec };
        if (setsockopt(skt->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
            return SOCKET_ERRNO();
        }
        
        skt->rtm = tm;
        
        return ERROR_SUCCESS;
    }
    int64_t srs_hijack_io_get_recv_timeout(srs_hijack_io_t ctx)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        return skt->rtm;
    }
    int64_t srs_hijack_io_get_recv_bytes(srs_hijack_io_t ctx)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        return skt->rbytes;
    }
    int srs_hijack_io_set_send_timeout(srs_hijack_io_t ctx, int64_t tm)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        // The default for this option is zero,
        // which indicates that a receive operation shall not time out.
        int32_t sec = 0;
        int32_t usec = 0;
        
        if (tm != SRS_CONSTS_NO_TMMS) {
            sec = (int32_t)(tm / 1000);
            usec = (int32_t)((tm % 1000)*1000);
        }
        
        struct timeval tv = { sec , usec };
        if (setsockopt(skt->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
            return SOCKET_ERRNO();
        }

        skt->stm = tm;
        
        return ERROR_SUCCESS;
    }
    int64_t srs_hijack_io_get_send_timeout(srs_hijack_io_t ctx)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        return skt->stm;
    }
    int64_t srs_hijack_io_get_send_bytes(srs_hijack_io_t ctx)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        return skt->sbytes;
    }
    int srs_hijack_io_writev(srs_hijack_io_t ctx, const iovec *iov, int iov_size, ssize_t* nwrite)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        int ret = ERROR_SUCCESS;
        
        ssize_t nb_write = ::writev(skt->fd, iov, iov_size);
        
        if (nwrite) {
            *nwrite = nb_write;
        }
        
        // On  success,  the  readv()  function  returns the number of bytes read; 
        // the writev() function returns the number of bytes written.  On error, -1 is
        // returned, and errno is set appropriately.
        if (nb_write <= 0) {
            // @see https://github.com/ossrs/srs/issues/200
            if (nb_write < 0 && SOCKET_ERRNO() == SOCKET_ETIME) {
                return ERROR_SOCKET_TIMEOUT;
            }
            
            return ERROR_SOCKET_WRITE;
        }
        
        skt->sbytes += nb_write;
        
        return ret;
    }
    int srs_hijack_io_is_never_timeout(srs_hijack_io_t ctx, int64_t tm)
    {
        return tm == SRS_CONSTS_NO_TMMS;
    }
    int srs_hijack_io_read_fully(srs_hijack_io_t ctx, void* buf, size_t size, ssize_t* nread)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        int ret = ERROR_SUCCESS;
        
        size_t left = size;
        ssize_t nb_read = 0;
        
        while (left > 0) {
            char* this_buf = (char*)buf + nb_read;
            ssize_t this_nread;
            
            if ((ret = srs_hijack_io_read(ctx, this_buf, left, &this_nread)) != ERROR_SUCCESS) {
                return ret;
            }
            
            nb_read += this_nread;
            left -= (size_t)this_nread;
        }
        
        if (nread) {
            *nread = nb_read;
        }
        skt->rbytes += nb_read;
        
        return ret;
    }
    int srs_hijack_io_write(srs_hijack_io_t ctx, void* buf, size_t size, ssize_t* nwrite)
    {
        SrsBlockSyncSocket* skt = (SrsBlockSyncSocket*)ctx;
        
        int ret = ERROR_SUCCESS;
        
        ssize_t nb_write = ::send(skt->fd, (char*)buf, size, 0);
        
        if (nwrite) {
            *nwrite = nb_write;
        }
        
        if (nb_write <= 0) {
            // @see https://github.com/ossrs/srs/issues/200
            if (nb_write < 0 && SOCKET_ERRNO() == SOCKET_ETIME) {
                return ERROR_SOCKET_TIMEOUT;
            }
            
            return ERROR_SOCKET_WRITE;
        }
        
        skt->sbytes += nb_write;
        
        return ret;
    }
#endif

SimpleSocketStream::SimpleSocketStream()
{
    io = srs_hijack_io_create();
}

SimpleSocketStream::~SimpleSocketStream()
{
    if (io) {
        srs_hijack_io_destroy(io);
        io = NULL;
    }
}

srs_hijack_io_t SimpleSocketStream::hijack_io()
{
    return io;
}

int SimpleSocketStream::create_socket(srs_rtmp_t owner)
{
    srs_assert(io);
    return srs_hijack_io_create_socket(io, owner);
}

int SimpleSocketStream::connect(const char* server_ip, int port)
{
    srs_assert(io);
    return srs_hijack_io_connect(io, server_ip, port);
}

// ISrsReader
int SimpleSocketStream::read(void* buf, size_t size, ssize_t* nread)
{
    srs_assert(io);
    return srs_hijack_io_read(io, buf, size, nread);
}

// ISrsProtocolReader
void SimpleSocketStream::set_recv_timeout(int64_t tm)
{
    srs_assert(io);
    srs_hijack_io_set_recv_timeout(io, tm);
}

int64_t SimpleSocketStream::get_recv_timeout()
{
    srs_assert(io);
    return srs_hijack_io_get_recv_timeout(io);
}

int64_t SimpleSocketStream::get_recv_bytes()
{
    srs_assert(io);
    return srs_hijack_io_get_recv_bytes(io);
}

// ISrsProtocolWriter
void SimpleSocketStream::set_send_timeout(int64_t tm)
{
    srs_assert(io);
    srs_hijack_io_set_send_timeout(io, tm);
}

int64_t SimpleSocketStream::get_send_timeout()
{
    srs_assert(io);
    return srs_hijack_io_get_send_timeout(io);
}

int64_t SimpleSocketStream::get_send_bytes()
{
    srs_assert(io);
    return srs_hijack_io_get_send_bytes(io);
}

int SimpleSocketStream::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    srs_assert(io);
    return srs_hijack_io_writev(io, iov, iov_size, nwrite);
}

// ISrsProtocolReaderWriter
bool SimpleSocketStream::is_never_timeout(int64_t tm)
{
    srs_assert(io);
    return srs_hijack_io_is_never_timeout(io, tm);
}

int SimpleSocketStream::read_fully(void* buf, size_t size, ssize_t* nread)
{
    srs_assert(io);
    return srs_hijack_io_read_fully(io, buf, size, nread);
}

int SimpleSocketStream::write(void* buf, size_t size, ssize_t* nwrite)
{
    srs_assert(io);
    return srs_hijack_io_write(io, buf, size, nwrite);
}


