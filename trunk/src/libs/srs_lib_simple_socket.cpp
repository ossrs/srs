/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/uio.h>

#include <srs_kernel_utility.hpp>

#ifndef ST_UTIME_NO_TIMEOUT
    #define ST_UTIME_NO_TIMEOUT -1
#endif

SimpleSocketStream::SimpleSocketStream()
{
    fd = -1;
    send_timeout = recv_timeout = ST_UTIME_NO_TIMEOUT;
    recv_bytes = send_bytes = 0;
}

SimpleSocketStream::~SimpleSocketStream()
{
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
}

int SimpleSocketStream::create_socket()
{
    if((fd = ::socket(AF_INET, SOCK_STREAM, 0)) < 0){
        return ERROR_SOCKET_CREATE;
    }

    return ERROR_SUCCESS;
}

int SimpleSocketStream::connect(const char* server_ip, int port)
{
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(server_ip);
    
    if(::connect(fd, (const struct sockaddr*)&addr, sizeof(sockaddr_in)) < 0){
        return ERROR_SOCKET_CONNECT;
    }

    return ERROR_SUCCESS;
}

// ISrsBufferReader
int SimpleSocketStream::read(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read = ::recv(fd, buf, size, 0);
    
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned 
    // (a value of 0 means the network connection is closed or end of file is reached).
    if (nb_read <= 0) {
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read == 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ;
    }
    
    recv_bytes += nb_read;
    
    return ret;
}

// ISrsProtocolReader
void SimpleSocketStream::set_recv_timeout(int64_t timeout_us)
{
    recv_timeout = timeout_us;
}

int64_t SimpleSocketStream::get_recv_timeout()
{
    return recv_timeout;
}

int64_t SimpleSocketStream::get_recv_bytes()
{
    return recv_bytes;
}

// ISrsProtocolWriter
void SimpleSocketStream::set_send_timeout(int64_t timeout_us)
{
    send_timeout = timeout_us;
}

int64_t SimpleSocketStream::get_send_timeout()
{
    return send_timeout;
}

int64_t SimpleSocketStream::get_send_bytes()
{
    return send_bytes;
}

int SimpleSocketStream::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write = ::writev(fd, iov, iov_size);
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On  success,  the  readv()  function  returns the number of bytes read; 
    // the writev() function returns the number of bytes written.  On error, -1 is
    // returned, and errno is set appropriately.
    if (nb_write <= 0) {
        // @see https://github.com/simple-rtmp-server/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    send_bytes += nb_write;
    
    return ret;
}

// ISrsProtocolReaderWriter
bool SimpleSocketStream::is_never_timeout(int64_t timeout_us)
{
    return timeout_us == (int64_t)ST_UTIME_NO_TIMEOUT;
}

int SimpleSocketStream::read_fully(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    size_t left = size;
    ssize_t nb_read = 0;
    
    while (left > 0) {
        char* this_buf = (char*)buf + nb_read;
        ssize_t this_nread;
        
        if ((ret = this->read(this_buf, left, &this_nread)) != ERROR_SUCCESS) {
            return ret;
        }
        
        nb_read += this_nread;
        left -= this_nread;
    }
    
    if (nread) {
        *nread = nb_read;
    }
    recv_bytes += nb_read;
    
    return ret;
}

int SimpleSocketStream::write(void* buf, size_t size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write = ::send(fd, (void*)buf, size, 0);
    
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    if (nb_write <= 0) {
        // @see https://github.com/simple-rtmp-server/srs/issues/200
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    send_bytes += nb_write;
    
    return ret;
}


