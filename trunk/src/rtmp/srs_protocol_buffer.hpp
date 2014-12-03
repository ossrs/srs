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

/**
* the buffer provices bytes cache for protocol. generally, 
* protocol recv data from socket, put into buffer, decode to RTMP message.
*/
class SrsBuffer
{
private:
    std::vector<char> data;
    char* buffer;
public:
    SrsBuffer();
    virtual ~SrsBuffer();
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
public:
    /**
    * grow buffer to the required size, loop to read from skt to fill.
    * @param reader, read more bytes from reader to fill the buffer to required size.
    * @param required_size, loop to fill to ensure buffer size to required. 
    * @return an int error code, error if required_size negative.
    * @remark, we actually maybe read more than required_size, maybe 4k for example.
    */
    virtual int grow(ISrsBufferReader* reader, int required_size);
};

#endif
