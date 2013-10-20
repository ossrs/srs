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

#include <srs_core_stream.hpp>

#include <srs_core_log.hpp>
#include <srs_core_error.hpp>

SrsStream::SrsStream()
{
	p = bytes = NULL;
	size = 0;
}

SrsStream::~SrsStream()
{
}

int SrsStream::initialize(char* _bytes, int _size)
{
	int ret = ERROR_SUCCESS;
	
	if (!_bytes) {
		ret = ERROR_SYSTEM_STREAM_INIT;
		srs_error("stream param bytes must not be NULL. ret=%d", ret);
		return ret;
	}
	
	if (_size <= 0) {
		ret = ERROR_SYSTEM_STREAM_INIT;
		srs_error("stream param size must be positive. ret=%d", ret);
		return ret;
	}

	size = _size;
	p = bytes = _bytes;

	return ret;
}

void SrsStream::reset()
{
	p = bytes;
}

bool SrsStream::empty()
{
	return !p || !bytes || (p >= bytes + size);
}

bool SrsStream::require(int required_size)
{
	return !empty() && (required_size <= bytes + size - p);
}

void SrsStream::skip(int size)
{
	p += size;
}

int8_t SrsStream::read_1bytes()
{
	srs_assert(require(1));
	
	return (int8_t)*p++;
}

int16_t SrsStream::read_2bytes()
{
	srs_assert(require(2));
	
	int16_t value;
	pp = (char*)&value;
	pp[1] = *p++;
	pp[0] = *p++;
	
	return value;
}

int32_t SrsStream::read_4bytes()
{
	srs_assert(require(4));
	
	int32_t value;
	pp = (char*)&value;
	pp[3] = *p++;
	pp[2] = *p++;
	pp[1] = *p++;
	pp[0] = *p++;
	
	return value;
}

int64_t SrsStream::read_8bytes()
{
	srs_assert(require(8));
	
	int64_t value;
	pp = (char*)&value;
    pp[7] = *p++;
    pp[6] = *p++;
    pp[5] = *p++;
    pp[4] = *p++;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
	
	return value;
}

std::string SrsStream::read_string(int len)
{
	srs_assert(require(len));
	
	std::string value;
	value.append(p, len);
	
	p += len;
	
	return value;
}

void SrsStream::write_4bytes(int32_t value)
{
	srs_assert(require(4));
	
	pp = (char*)&value;
	*p++ = pp[3];
	*p++ = pp[2];
	*p++ = pp[1];
	*p++ = pp[0];
}

void SrsStream::write_1bytes(int8_t value)
{
	srs_assert(require(1));
	
	*p++ = value;
}

