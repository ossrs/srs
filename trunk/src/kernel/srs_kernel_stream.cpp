/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_kernel_stream.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>

SrsSimpleStream::SrsSimpleStream()
{
}

SrsSimpleStream::~SrsSimpleStream()
{
}

int SrsSimpleStream::length()
{
    int len = (int)data.size();
    srs_assert(len >= 0);
    return len;
}

char* SrsSimpleStream::bytes()
{
    return (length() == 0)? NULL : &data.at(0);
}

void SrsSimpleStream::erase(int size)
{
    if (size <= 0) {
        return;
    }
    
    if (size >= length()) {
        data.clear();
        return;
    }
    
    data.erase(data.begin(), data.begin() + size);
}

void SrsSimpleStream::append(const char* bytes, int size)
{
    if (size > 0) {
        data.insert(data.end(), bytes, bytes + size);
    }
}

void SrsSimpleStream::append(SrsSimpleStream* src)
{
    append(src->bytes(), src->length());
}



/*
* beikesong: SrsSimpleBufferX
*
*/
SrsSimpleBufferX::SrsSimpleBufferX()
{
	oft = 0;
}

SrsSimpleBufferX::~SrsSimpleBufferX()
{
}

bool SrsSimpleBufferX::require(int require)
{
	int len = length();

	return require <= len - oft;
}

char * SrsSimpleBufferX::curat()
{
	return (length() == 0) ? NULL : &data.at(oft);
}

int SrsSimpleBufferX::cursize()
{
	int len = length();
	return (len < oft) ? 0 : (len - oft);
}

int SrsSimpleBufferX::getoft()
{
	return oft;
}

bool SrsSimpleBufferX::skip_x(int size)
{
	if (require(size)) {
		oft += size;
		return true;
	}
	else {
		return false;
	}
}

bool SrsSimpleBufferX::chk_bytes(char * cb, int size)
{
	if (require(size)){
		memcpy(cb, &data.at(oft), size);
		return true;
	}
	else {
		return false;
	}
}

bool SrsSimpleBufferX::read_bytes_x(char * cb, int size)
{
	if (require(size)) {
		memcpy(cb, &data.at(oft), size);
		oft += size;
		return true;
	}
	else {
		return false;
	}
}

void SrsSimpleBufferX::resetoft()
{
	oft = 0;
}

int SrsSimpleBufferX::length()
{
	int len = (int)data.size();
	srs_assert(len >= 0);
	return len;
}

char* SrsSimpleBufferX::bytes()
{
	return (length() == 0) ? NULL : &data.at(0);
}

void SrsSimpleBufferX::erase(int size)
{
	if (size <= 0) {
		return;
	}

	if (size >= length()) {
		data.clear();
		return;
	}

	data.erase(data.begin(), data.begin() + size);
}

void SrsSimpleBufferX::append(const char* bytes, int size)
{
	srs_assert(size > 0);

	data.insert(data.end(), bytes, bytes + size);
}

