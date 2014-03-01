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

#ifndef SRS_CORE_SOCKET_HPP
#define SRS_CORE_SOCKET_HPP

/*
#include <srs_core_socket.hpp>
*/

#include <srs_core.hpp>

#include <srs_core_st.hpp>

/**
* the socket provides TCP socket over st,
* that is, the sync socket mechanism.
*/
class SrsSocket
{
private:
	int64_t recv_timeout;
	int64_t send_timeout;
	int64_t recv_bytes;
	int64_t send_bytes;
	int64_t start_time_ms;
    st_netfd_t stfd;
public:
    SrsSocket(st_netfd_t client_stfd);
    virtual ~SrsSocket();
public:
	virtual void set_recv_timeout(int64_t timeout_us);
	virtual int64_t get_recv_timeout();
	virtual void set_send_timeout(int64_t timeout_us);
	virtual int64_t get_send_timeout();
	virtual int64_t get_recv_bytes();
	virtual int64_t get_send_bytes();
	virtual int get_recv_kbps();
	virtual int get_send_kbps();
public:
    virtual int read(const void* buf, size_t size, ssize_t* nread);
    virtual int read_fully(const void* buf, size_t size, ssize_t* nread);
    virtual int write(const void* buf, size_t size, ssize_t* nwrite);
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite);
};

#endif