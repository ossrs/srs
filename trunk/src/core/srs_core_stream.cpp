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
	bytes = NULL;
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
	bytes = _bytes;

	return ret;
}

