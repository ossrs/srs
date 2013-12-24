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
private:
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
	/**
	* to skip some size.
	* @size can be any value. positive to forward; nagetive to backward.
	*/
	virtual void skip(int size);
	/**
	* tell the current pos.
	*/
	virtual int pos();
	/**
	* left size of bytes.
	*/
	virtual int left();
	virtual char* current();
public:
	/**
	* get 1bytes char from stream.
	*/
	virtual int8_t read_1bytes();
	/**
	* get 2bytes int from stream.
	*/
	virtual int16_t read_2bytes();
	/**
	* get 3bytes int from stream.
	*/
	virtual int32_t read_3bytes();
	/**
	* get 4bytes int from stream.
	*/
	virtual int32_t read_4bytes();
	/**
	* get 8bytes int from stream.
	*/
	virtual int64_t read_8bytes();
	/**
	* get string from stream, length specifies by param len.
	*/
	virtual std::string read_string(int len);
public:
	/**
	* write 1bytes char to stream.
	*/
	virtual void write_1bytes(int8_t value);
	/**
	* write 2bytes int to stream.
	*/
	virtual void write_2bytes(int16_t value);
	/**
	* write 4bytes int to stream.
	*/
	virtual void write_4bytes(int32_t value);
	/**
	* write 8bytes int to stream.
	*/
	virtual void write_8bytes(int64_t value);
	/**
	* write string to stream
	*/
	virtual void write_string(std::string value);
};

#endif