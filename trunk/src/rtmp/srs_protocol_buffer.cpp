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
#include <srs_core_performance.hpp>

// the default recv buffer size
#define SRS_DEFAULT_RECV_BUFFER_SIZE 32768

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

#ifdef SRS_PERF_MERGED_READ
IMergeReadHandler::IMergeReadHandler()
{
}

IMergeReadHandler::~IMergeReadHandler()
{
}
#endif

SrsFastBuffer::SrsFastBuffer()
{
#ifdef SRS_PERF_MERGED_READ
    merged_read = false;
    _handler = NULL;
#endif
    
    nb_buffer = SRS_DEFAULT_RECV_BUFFER_SIZE;
    buffer = new char[nb_buffer];
    p = end = buffer;
}

void SrsFastBuffer::set_buffer(int buffer_size)
{
    // only realloc when buffer changed bigger
    if (buffer_size <= nb_buffer) {
        return;
    }
    
    int start = p - buffer;
    int cap = end - p;
    
    char* buf = new char[buffer_size];
    if (cap > 0) {
        memcpy(buf, buffer, nb_buffer);
    }
    srs_freep(buffer);
    
    buffer = buf;
    p = buffer + start;
    end = p + cap;
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
    // or there is no space to read.
    int max_to_read = buffer + nb_buffer - end;
    if (end - p < required_size 
        && (required_size > SRS_RTMP_MAX_MESSAGE_HEADER || max_to_read < required_size)
    ) {
        int nb_cap = end - p;
        srs_verbose("move fast buffer %d bytes", nb_cap);
        if (nb_cap < nb_buffer) {
            buffer = (char*)memmove(buffer, p, nb_cap);
            p = buffer;
            end = p + nb_cap;
        }
    }

    while (end - p < required_size) {
        // the max to read is the left bytes.
        max_to_read = buffer + nb_buffer - end;
        
        if (max_to_read <= 0) {
            ret = ERROR_RTMP_BUFFER_OVERFLOW;
            srs_error("buffer overflow, required=%d, max=%d, ret=%d", required_size, nb_buffer, ret);
            return ret;
        }
        
        ssize_t nread;
        if ((ret = reader->read(end, max_to_read, &nread)) != ERROR_SUCCESS) {
            return ret;
        }
        
#ifdef SRS_PERF_MERGED_READ
        /**
        * to improve read performance, merge some packets then read,
        * when it on and read small bytes, we sleep to wait more data.,
        * that is, we merge some data to read together.
        * @see https://github.com/winlinvip/simple-rtmp-server/issues/241
        */
        if (merged_read && _handler) {
            _handler->on_read(nread);
        }
#endif
        
        // we just move the ptr to next.
        srs_assert((int)nread > 0);
        end += nread;
    }
    
    return ret;
}

#ifdef SRS_PERF_MERGED_READ
void SrsFastBuffer::set_merge_read(bool v, IMergeReadHandler* handler)
{
    merged_read = v;
    _handler = handler;
}
#endif

