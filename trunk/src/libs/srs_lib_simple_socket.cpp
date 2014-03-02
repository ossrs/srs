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

SimpleSocketStream::SimpleSocketStream()
{
	fd = -1;
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
        return -1;
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
        return -1;
    }

	return ERROR_SUCCESS;
}

// ISrsBufferReader
int SimpleSocketStream::read(const void* buf, size_t size, ssize_t* nread)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

// ISrsProtocolReader
void SimpleSocketStream::set_recv_timeout(int64_t timeout_us)
{
}

int64_t SimpleSocketStream::get_recv_timeout()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int64_t SimpleSocketStream::get_recv_bytes()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SimpleSocketStream::get_recv_kbps()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

// ISrsProtocolWriter
void SimpleSocketStream::set_send_timeout(int64_t timeout_us)
{
}

int64_t SimpleSocketStream::get_send_timeout()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int64_t SimpleSocketStream::get_send_bytes()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SimpleSocketStream::get_send_kbps()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SimpleSocketStream::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

// ISrsProtocolReaderWriter
bool SimpleSocketStream::is_never_timeout(int64_t timeout_us)
{
	return true;
}

int SimpleSocketStream::read_fully(const void* buf, size_t size, ssize_t* nread)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SimpleSocketStream::write(const void* buf, size_t size, ssize_t* nwrite)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

