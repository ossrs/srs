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

#ifndef SRS_KERNEL_BUFFER_HPP
#define SRS_KERNEL_BUFFER_HPP

/*
#include <srs_kernel_buffer.hpp>
*/

#include <srs_core.hpp>

#include <vector>

/**
* the reader for the buffer to read from whatever channel.
*/
class ISrsBufferReader
{
public:
    ISrsBufferReader();
    virtual ~ISrsBufferReader();
// for protocol/amf0/msg-codec
public:
    virtual int read(const void* buf, size_t size, ssize_t* nread) = 0;
};

/**
* the buffer provices bytes cache for protocol. generally, 
* protocol recv data from socket, put into buffer, decode to RTMP message.
* protocol encode RTMP message to bytes, put into buffer, send to socket.
*/
class SrsBuffer
{
private:
    std::vector<char> data;
public:
    SrsBuffer();
    virtual ~SrsBuffer();
public:
    virtual int size();
    virtual char* bytes();
    virtual void erase(int size);
private:
    virtual void append(char* bytes, int size);
public:
    virtual int ensure_buffer_bytes(ISrsBufferReader* skt, int required_size);
};

#endif