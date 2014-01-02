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

#include <srs_core_buffer.hpp>

#include <srs_core_error.hpp>
#include <srs_core_socket.hpp>
#include <srs_core_log.hpp>

#define SOCKET_READ_SIZE 4096

SrsBuffer::SrsBuffer()
{
}

SrsBuffer::~SrsBuffer()
{
}

int SrsBuffer::size()
{
	return (int)data.size();
}

char* SrsBuffer::bytes()
{
	return &data.at(0);
}

void SrsBuffer::erase(int size)
{
	data.erase(data.begin(), data.begin() + size);
}

void SrsBuffer::append(char* bytes, int size)
{
	data.insert(data.end(), bytes, bytes + size);
}

int SrsBuffer::ensure_buffer_bytes(SrsSocket* skt, int required_size)
{
	int ret = ERROR_SUCCESS;

	if (required_size < 0) {
		ret = ERROR_SYSTEM_SIZE_NEGATIVE;
		srs_error("size is negative. size=%d, ret=%d", required_size, ret);
		return ret;
	}

	while (size() < required_size) {
		char buffer[SOCKET_READ_SIZE];
		
		ssize_t nread;
		if ((ret = skt->read(buffer, SOCKET_READ_SIZE, &nread)) != ERROR_SUCCESS) {
			return ret;
		}
		
		srs_assert((int)nread > 0);
		append(buffer, (int)nread);
	}
	
	return ret;
}

