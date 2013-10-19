/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#ifndef SRS_CORE_STREAM_HPP
#define SRS_CORE_STREAM_HPP

/*
#include <srs_core_stream.hpp>
*/

#include <srs_core.hpp>

#include <sys/types.h>
#include <string>

class SrsStream
{
protected:
	char* p;
	char* pp;
	char* bytes;
	int size;
public:
	SrsStream();
	virtual ~SrsStream();
public:
	/**
	* initialize the stream from bytes.
	* @_bytes, must not be NULL, or return error.
	* @_size, must be positive, or return error.
	* @remark, stream never free the _bytes, user must free it.
	*/
	virtual int initialize(char* _bytes, int _size);
	/**
	* reset the position to beginning.
	*/
	virtual void reset();
	/**
	* whether stream is empty.
	* if empty, never read or write.
	*/
	virtual bool empty();
	/**
	* whether required size is ok.
	* @return true if stream can read/write specified required_size bytes.
	*/
	virtual bool require(int required_size);
public:
	virtual char read_char();
	virtual int16_t read_2bytes();
	virtual std::string read_string(int len);
};

#endif