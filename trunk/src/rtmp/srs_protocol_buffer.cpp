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
#include <srs_kernel_utility.hpp>

// the max header size,
// @see SrsProtocol::read_message_header().
#define SRS_RTMP_MAX_MESSAGE_HEADER 11

SrsSimpleBuffer::SrsSimpleBuffer()
{
}

SrsSimpleBuffer::~SrsSimpleBuffer()
{
}

int SrsSimpleBuffer::length()
{
    int len = (int)data.size();
    srs_assert(len >= 0);
    return len;
}

char* SrsSimpleBuffer::bytes()
{
    return (length() == 0)? NULL : &data.at(0);
}

void SrsSimpleBuffer::erase(int size)
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

void SrsSimpleBuffer::append(const char* bytes, int size)
{
    srs_assert(size > 0);

    data.insert(data.end(), bytes, bytes + size);
}

IMergeReadHandler::IMergeReadHandler()
{
}

IMergeReadHandler::~IMergeReadHandler()
{
}

SrsFastBuffer::SrsFastBuffer()
{
    merged_read = false;
    _handler = NULL;
    
    p = end = buffer = NULL;
    nb_buffer = 0;
    
    reset_buffer(SOCKET_READ_SIZE);
}

SrsFastBuffer::~SrsFastBuffer()
{
    srs_freep(buffer);
}

char SrsFastBuffer::read_1byte()
{
    srs_assert(end - p >= 1);
    return *p++;
}

char* SrsFastBuffer::read_slice(int size)
{
    srs_assert(end - p >= size);
    srs_assert(p + size > buffer);
    
    char* ptr = p;
    p += size;
    
    // reset when consumed all.
    if (p == end) {
        p = end = buffer;
        srs_verbose("all consumed, reset fast buffer");
    }

    return ptr;
}

void SrsFastBuffer::skip(int size)
{
    srs_assert(end - p >= size);
    srs_assert(p + size > buffer);
    p += size;
}

int SrsFastBuffer::grow(ISrsBufferReader* reader, int required_size)
{
    int ret = ERROR_SUCCESS;

    if (required_size < 0) {
        ret = ERROR_SYSTEM_SIZE_NEGATIVE;
        srs_error("size is negative. size=%d, ret=%d", required_size, ret);
        return ret;
    }

    // when read payload and need to grow, reset buffer.
    if (end - p < required_size && required_size > SRS_RTMP_MAX_MESSAGE_HEADER) {
        int nb_cap = end - p;
        srs_verbose("move fast buffer %d bytes", nb_cap);
        buffer = (char*)memmove(buffer, p, nb_cap);
        p = buffer;
        end = p + nb_cap;
    }

    while (end - p < required_size) {
        // the max to read is the left bytes.
        size_t max_to_read = buffer + nb_buffer - end;
        
        if (max_to_read <= 0) {
            ret = ERROR_RTMP_BUFFER_OVERFLOW;
            srs_error("buffer overflow, required=%d, max=%d, ret=%d", required_size, nb_buffer, ret);
            return ret;
        }
        
        ssize_t nread;
        if ((ret = reader->read(end, max_to_read, &nread)) != ERROR_SUCCESS) {
            return ret;
        }
        
        /**
        * to improve read performance, merge some packets then read,
        * when it on and read small bytes, we sleep to wait more data.,
        * that is, we merge some data to read together.
        * @see https://github.com/winlinvip/simple-rtmp-server/issues/241
        */
        if (merged_read && _handler) {
            _handler->on_read(nread);
        }
        
        // we just move the ptr to next.
        srs_assert((int)nread > 0);
        end += nread;
    }
    
    return ret;
}

void SrsFastBuffer::set_merge_read(bool v, int max_buffer, IMergeReadHandler* handler)
{
    merged_read = v;
    _handler = handler;

    // limit the max buffer.
    int buffer_size = srs_min(max_buffer, SOCKET_MAX_BUF);

    if (v && buffer_size != nb_buffer) {
        reset_buffer(buffer_size);
    }

    if (_handler) {
        _handler->on_buffer_change(nb_buffer);
    }
}

void SrsFastBuffer::on_chunk_size(int32_t chunk_size)
{
    if (nb_buffer >= chunk_size) {
        return;
    }

    // limit the max buffer.
    int buffer_size = srs_min(chunk_size, SOCKET_MAX_BUF);

    if (buffer_size != nb_buffer) {
        reset_buffer(buffer_size);
    }

    if (_handler) {
        _handler->on_buffer_change(nb_buffer);
    }
}

int SrsFastBuffer::buffer_size()
{
    return nb_buffer;
}

void SrsFastBuffer::reset_buffer(int size)
{
    // remember the cap.
    int nb_cap = end - p;
    
    // atleast to put the old data.
    nb_buffer = srs_max(nb_cap, size);
    
    // copy old data to buf.
    char* buf = new char[nb_buffer];
    if (nb_cap > 0) {
        memcpy(buf, p, nb_cap);
    }
    
    srs_freep(buffer);
    p = buffer = buf;
    end = p + nb_cap;
}
