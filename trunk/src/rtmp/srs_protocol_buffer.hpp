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

#ifndef SRS_PROTOCOL_BUFFER_HPP
#define SRS_PROTOCOL_BUFFER_HPP

/*
#include <srs_protocol_buffer.hpp>
*/

#include <srs_core.hpp>

#include <vector>

#include <srs_protocol_io.hpp>
#include <srs_core_performance.hpp>

// 4KB=4096
// 8KB=8192
// 16KB=16384
// 32KB=32768
// 64KB=65536
// @see https://github.com/winlinvip/simple-rtmp-server/issues/241
#define SOCKET_READ_SIZE 65536
// the max buffer for user space socket buffer.
#define SOCKET_MAX_BUF SOCKET_READ_SIZE

/**
* the simple buffer use vector to append bytes,
* it's for hls and http, and need to be refined in future.
*/
class SrsSimpleBuffer
{
private:
    std::vector<char> data;
public:
    SrsSimpleBuffer();
    virtual ~SrsSimpleBuffer();
public:
    /**
    * get the length of buffer. empty if zero.
    * @remark assert length() is not negative.
    */
    virtual int length();
    /**
    * get the buffer bytes.
    * @return the bytes, NULL if empty.
    */
    virtual char* bytes();
    /**
    * erase size of bytes from begin.
    * @param size to erase size of bytes. 
    *       clear if size greater than or equals to length()
    * @remark ignore size is not positive.
    */
    virtual void erase(int size);
    /**
    * append specified bytes to buffer.
    * @param size the size of bytes
    * @remark assert size is positive.
    */
    virtual void append(const char* bytes, int size);
};

#ifdef SRS_PERF_MERGED_READ
/**
* to improve read performance, merge some packets then read,
* when it on and read small bytes, we sleep to wait more data.,
* that is, we merge some data to read together.
* @see https://github.com/winlinvip/simple-rtmp-server/issues/241
*/
class IMergeReadHandler
{
public:
    IMergeReadHandler();
    virtual ~IMergeReadHandler();
public:
    /**
    * when read from channel, notice the merge handler to sleep for
    * some small bytes.
    * @remark, it only for server-side, client srs-librtmp just ignore.
    */
    virtual void on_read(ssize_t nread) = 0;
    /**
    * when buffer size changed.
    * @param nb_buffer the new buffer size.
    */
    virtual void on_buffer_change(int nb_buffer) = 0;
};
#endif

/**
* the buffer provices bytes cache for protocol. generally, 
* protocol recv data from socket, put into buffer, decode to RTMP message.
*/
// TODO: FIXME: add utest for it.
class SrsFastBuffer
{
private:
#ifdef SRS_PERF_MERGED_READ
    // the merged handler
    bool merged_read;
    IMergeReadHandler* _handler;
#endif
    // the user-space buffer to fill by reader,
    // which use fast index and reset when chunk body read ok.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/248
    // ptr to the current read position.
    char* p;
    // ptr to the content end.
    char* end;
    // ptr to the buffer.
    //      buffer <= p <= end <= buffer+nb_buffer
    char* buffer;
    // the max size of buffer.
    int nb_buffer;
public:
    SrsFastBuffer();
    virtual ~SrsFastBuffer();
public:
    /**
    * read 1byte from buffer, move to next bytes.
    * @remark assert buffer already grow(1).
    */
    virtual char read_1byte();
    /**
    * read a slice in size bytes, move to next bytes.
    * user can use this char* ptr directly, and should never free it.
    * @remark assert buffer already grow(size).
    * @remark the ptr returned maybe invalid after grow(x).
    */
    virtual char* read_slice(int size);
    /**
    * skip some bytes in buffer.
    * @param size the bytes to skip. positive to next; negative to previous.
    * @remark assert buffer already grow(size).
    */
    virtual void skip(int size);
public:
    /**
    * grow buffer to the required size, loop to read from skt to fill.
    * @param reader, read more bytes from reader to fill the buffer to required size.
    * @param required_size, loop to fill to ensure buffer size to required. 
    * @return an int error code, error if required_size negative.
    * @remark, we actually maybe read more than required_size, maybe 4k for example.
    */
    virtual int grow(ISrsBufferReader* reader, int required_size);
public:
#ifdef SRS_PERF_MERGED_READ
    /**
    * to improve read performance, merge some packets then read,
    * when it on and read small bytes, we sleep to wait more data.,
    * that is, we merge some data to read together.
    * @param v true to ename merged read.
    * @param max_buffer the max buffer size, the socket buffer.
    * @param handler the handler when merge read is enabled.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/241
    */
    virtual void set_merge_read(bool v, int max_buffer, IMergeReadHandler* handler);
#endif
public:
    /**
    * when chunk size changed, the buffer should change the buffer also.
    * to keep the socket buffer size always greater than chunk size.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/241
    */
    virtual void on_chunk_size(int32_t chunk_size);
    /**
    * get the size of socket buffer to read.
    */
    virtual int buffer_size();
private:
    virtual void reset_buffer(int size);
};

#endif
