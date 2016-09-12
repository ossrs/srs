/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#include <srs_protocol_stream.hpp>

#include <stdlib.h>

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

SrsFastStream::SrsFastStream()
{
#ifdef SRS_PERF_MERGED_READ
    merged_read = false;
    _handler = NULL;
#endif
    
    nb_buffer = SRS_DEFAULT_RECV_BUFFER_SIZE;
    buffer = (char*)malloc(nb_buffer);
    p = end = buffer;
}

SrsFastStream::~SrsFastStream()
{
    free(buffer);
    buffer = NULL;
}

int SrsFastStream::size()
{
    return (int)(end - p);
}

char* SrsFastStream::bytes()
{
    return p;
}

void SrsFastStream::set_buffer(int buffer_size)
{
    // never exceed the max size.
    if (buffer_size > SRS_MAX_SOCKET_BUFFER) {
        srs_warn("limit the user-space buffer from %d to %d", 
            buffer_size, SRS_MAX_SOCKET_BUFFER);
    }
    
    // the user-space buffer size limit to a max value.
    int nb_resize_buf = srs_min(buffer_size, SRS_MAX_SOCKET_BUFFER);

    // only realloc when buffer changed bigger
    if (nb_resize_buf <= nb_buffer) {
        return;
    }
    
    // realloc for buffer change bigger.
    int start = (int)(p - buffer);
    int nb_bytes = (int)(end - p);
    
    buffer = (char*)realloc(buffer, nb_resize_buf);
    nb_buffer = nb_resize_buf;
    p = buffer + start;
    end = p + nb_bytes;
}

char SrsFastStream::read_1byte()
{
    srs_assert(end - p >= 1);
    return *p++;
}

char* SrsFastStream::read_slice(int size)
{
    srs_assert(size >= 0);
    srs_assert(end - p >= size);
    srs_assert(p + size >= buffer);
    
    char* ptr = p;
    p += size;

    return ptr;
}

void SrsFastStream::skip(int size)
{
    srs_assert(end - p >= size);
    srs_assert(p + size >= buffer);
    p += size;
}

int SrsFastStream::grow(ISrsBufferReader* reader, int required_size)
{
    int ret = ERROR_SUCCESS;

    // already got required size of bytes.
    if (end - p >= required_size) {
        return ret;
    }

    // must be positive.
    srs_assert(required_size > 0);

    // the free space of buffer, 
    //      buffer = consumed_bytes + exists_bytes + free_space.
    int nb_free_space = (int)(buffer + nb_buffer - end);
    
    // the bytes already in buffer
    int nb_exists_bytes = (int)(end - p);
    srs_assert(nb_exists_bytes >= 0);
    
    // resize the space when no left space.
    if (nb_free_space < required_size - nb_exists_bytes) {
        srs_verbose("move fast buffer %d bytes", nb_exists_bytes);

        // reset or move to get more space.
        if (!nb_exists_bytes) {
            // reset when buffer is empty.
            p = end = buffer;
            srs_verbose("all consumed, reset fast buffer");
        } else {
            // move the left bytes to start of buffer.
            srs_assert(nb_exists_bytes < nb_buffer);
            buffer = (char*)memmove(buffer, p, nb_exists_bytes);
            p = buffer;
            end = p + nb_exists_bytes;
        }
        
        // check whether enough free space in buffer.
        nb_free_space = (int)(buffer + nb_buffer - end);
        if (nb_free_space < required_size - nb_exists_bytes) {
            ret = ERROR_READER_BUFFER_OVERFLOW;
            srs_error("buffer overflow, required=%d, max=%d, left=%d, ret=%d", 
                required_size, nb_buffer, nb_free_space, ret);
            return ret;
        }
    }

    // buffer is ok, read required size of bytes.
    while (end - p < required_size) {
        ssize_t nread;
        if ((ret = reader->read(end, nb_free_space, &nread)) != ERROR_SUCCESS) {
            return ret;
        }
        
#ifdef SRS_PERF_MERGED_READ
        /**
        * to improve read performance, merge some packets then read,
        * when it on and read small bytes, we sleep to wait more data.,
        * that is, we merge some data to read together.
        * @see https://github.com/ossrs/srs/issues/241
        */
        if (merged_read && _handler) {
            _handler->on_read(nread);
        }
#endif
        
        // we just move the ptr to next.
        srs_assert((int)nread > 0);
        end += nread;
        nb_free_space -= nread;
    }
    
    return ret;
}

#ifdef SRS_PERF_MERGED_READ
void SrsFastStream::set_merge_read(bool v, IMergeReadHandler* handler)
{
    merged_read = v;
    _handler = handler;
}
#endif

