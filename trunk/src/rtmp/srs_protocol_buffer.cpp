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

#include <srs_protocol_buffer.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

// 4KB=4096
// 8KB=8192
// 16KB=16384
// 32KB=32768
// 64KB=65536
// @see https://github.com/winlinvip/simple-rtmp-server/issues/241
#define SOCKET_READ_SIZE 4096

SrsBuffer::SrsBuffer()
{
    buffer = new char[SOCKET_READ_SIZE];
}

SrsBuffer::~SrsBuffer()
{
    srs_freep(buffer);
}

int SrsBuffer::length()
{
    int len = (int)data.size();
    srs_assert(len >= 0);
    return len;
}

char* SrsBuffer::bytes()
{
    return (length() == 0)? NULL : &data.at(0);
}

void SrsBuffer::erase(int size)
{
    if (size <= 0) {
        return;
    }
    
    if (size >= length()) {
        data.clear();
        return;
    }
    
    data.erase(data.begin(), data.begin() + size);
}

void SrsBuffer::append(const char* bytes, int size)
{
    srs_assert(size > 0);

    data.insert(data.end(), bytes, bytes + size);
}

int SrsBuffer::grow(ISrsBufferReader* reader, int required_size)
{
    int ret = ERROR_SUCCESS;

    if (required_size < 0) {
        ret = ERROR_SYSTEM_SIZE_NEGATIVE;
        srs_error("size is negative. size=%d, ret=%d", required_size, ret);
        return ret;
    }

    while (length() < required_size) {
        ssize_t nread;
        if ((ret = reader->read(buffer, SOCKET_READ_SIZE, &nread)) != ERROR_SUCCESS) {
            return ret;
        }
        
        srs_assert((int)nread > 0);
        append(buffer, (int)nread);
    }
    
    return ret;
}


