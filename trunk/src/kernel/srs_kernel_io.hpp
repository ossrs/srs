/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#ifndef SRS_KERNEL_IO_HPP
#define SRS_KERNEL_IO_HPP

/*
#include <srs_kernel_io.hpp>
*/

#include <srs_core.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

/**
 * The reader to read data from channel.
 */
class ISrsReader
{
public:
    ISrsReader();
    virtual ~ISrsReader();
public:
    /**
     * Read bytes from reader.
     * @param nread How many bytes read from channel. NULL to ignore.
     */
    virtual int read(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
 * The writer to write stream data to channel.
 */
class ISrsStreamWriter
{
public:
    ISrsStreamWriter();
    virtual ~ISrsStreamWriter();
public:
    /**
     * write bytes over writer.
     * @nwrite the actual written bytes. NULL to ignore.
     */
    virtual int write(void* buf, size_t size, ssize_t* nwrite) = 0;
};

/**
 * The vector writer to write vector(iovc) to channel.
 */
class ISrsVectorWriter
{
public:
    ISrsVectorWriter();
    virtual ~ISrsVectorWriter();
public:
    /**
     * write iov over writer.
     * @nwrite the actual written bytes. NULL to ignore.
     */
    virtual int writev(const iovec *iov, int iov_size, ssize_t* nwrite) = 0;
};

/**
 * The generally writer, stream and vector writer.
 */
class ISrsWriter : virtual public ISrsStreamWriter, virtual public ISrsVectorWriter
{
public:
    ISrsWriter();
    virtual ~ISrsWriter();
};

#endif

