/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_rtmp_buffer.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>

// the default recv buffer size, 128KB.
#define SRS_DEFAULT_RECV_BUFFER_SIZE 131072

// limit user-space buffer to 256KB, for 3Mbps stream delivery.
//      800*2000/8=200000B(about 195KB).
// @remark it's ok for higher stream, the buffer is ok for one chunk is 256KB.
#define SRS_MAX_SOCKET_BUFFER 262144

// the max header size,
// @see SrsProtocol::read_message_header().
#define SRS_RTMP_MAX_MESSAGE_HEADER 11

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
    // the user-space buffer size limit to a max value.
    int nb_max_buf = srs_min(buffer_size, SRS_MAX_SOCKET_BUFFER);
    if (nb_max_buf < buffer_size) {
        srs_warn("limit the user-space buffer from %d to %d", buffer_size, nb_max_buf);
    }

    // only realloc when buffer changed bigger
    if (nb_max_buf <= nb_buffer) {
        return;
    }
    
    int start = p - buffer;
    int cap = end - p;
    
    char* buf = new char[nb_max_buf];
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

    // generally the required size is ok.
    if (end - p >= required_size) {
        return ret;
    }

    // must be positive.
    srs_assert(required_size > 0);

    // when read payload or there is no space to read,
    // reset the buffer with exists bytes.
    int max_to_read = buffer + nb_buffer - end;
    if (required_size > SRS_RTMP_MAX_MESSAGE_HEADER || max_to_read < required_size) {
        int nb_cap = end - p;
        srs_verbose("move fast buffer %d bytes", nb_cap);
        if (nb_cap < nb_buffer) {
            buffer = (char*)memmove(buffer, p, nb_cap);
            p = buffer;
            end = p + nb_cap;
        }
    }

    // directly check the available bytes to read in buffer.
    max_to_read = buffer + nb_buffer - end;
    if (max_to_read < required_size) {
        ret = ERROR_READER_BUFFER_OVERFLOW;
        srs_error("buffer overflow, required=%d, max=%d, ret=%d", required_size, nb_buffer, ret);
        return ret;
    }

    // buffer is ok, read required size of bytes.
    while (end - p < required_size) {
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

