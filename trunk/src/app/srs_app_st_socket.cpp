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

#include <srs_app_st_socket.hpp>

#include <srs_kernel_error.hpp>

SrsStSocket::SrsStSocket(st_netfd_t client_stfd)
{
    stfd = client_stfd;
    send_timeout = recv_timeout = ST_UTIME_NO_TIMEOUT;
    recv_bytes = send_bytes = 0;
}

SrsStSocket::~SrsStSocket()
{
}

bool SrsStSocket::is_never_timeout(int64_t timeout_us)
{
    return timeout_us == (int64_t)ST_UTIME_NO_TIMEOUT;
}

void SrsStSocket::set_recv_timeout(int64_t timeout_us)
{
    recv_timeout = timeout_us;
}

int64_t SrsStSocket::get_recv_timeout()
{
    return recv_timeout;
}

void SrsStSocket::set_send_timeout(int64_t timeout_us)
{
    send_timeout = timeout_us;
}

int64_t SrsStSocket::get_send_timeout()
{
    return send_timeout;
}

int64_t SrsStSocket::get_recv_bytes()
{
    return recv_bytes;
}

int64_t SrsStSocket::get_send_bytes()
{
    return send_bytes;
}

int SrsStSocket::read(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read = st_read(stfd, buf, size, recv_timeout);
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned 
    // (a value of 0 means the network connection is closed or end of file is reached).
    // Otherwise, a value of -1 is returned and errno is set to indicate the error. 
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

int SrsStSocket::read_fully(void* buf, size_t size, ssize_t* nread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_read = st_read_fully(stfd, buf, size, recv_timeout);
    if (nread) {
        *nread = nb_read;
    }
    
    // On success a non-negative integer indicating the number of bytes actually read is returned 
    // (a value less than nbyte means the network connection is closed or end of file is reached)
    // Otherwise, a value of -1 is returned and errno is set to indicate the error. 
    if (nb_read != (ssize_t)size) {
        if (nb_read < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        if (nb_read >= 0) {
            errno = ECONNRESET;
        }
        
        return ERROR_SOCKET_READ_FULLY;
    }
    
    recv_bytes += nb_read;
    
    return ret;
}

int SrsStSocket::write(void* buf, size_t size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write = st_write(stfd, buf, size, send_timeout);
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned. 
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    send_bytes += nb_write;
        
    return ret;
}

int SrsStSocket::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nb_write = st_writev(stfd, iov, iov_size, send_timeout);
    if (nwrite) {
        *nwrite = nb_write;
    }
    
    // On success a non-negative integer equal to nbyte is returned. 
    // Otherwise, a value of -1 is returned and errno is set to indicate the error.
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return ERROR_SOCKET_TIMEOUT;
        }
        
        return ERROR_SOCKET_WRITE;
    }
    
    send_bytes += nb_write;
    
    return ret;
}


