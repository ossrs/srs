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

#ifndef SRS_CORE_HANDSHKAE_HPP
#define SRS_CORE_HANDSHKAE_HPP

/*
#include <srs_core_complex_handshake.hpp>
*/

#include <srs_core.hpp>

class ISrsProtocolReaderWriter;
class SrsComplexHandshake;

/**
* try complex handshake, if failed, fallback to simple handshake.
*/
class SrsSimpleHandshake
{
public:
	SrsSimpleHandshake();
	virtual ~SrsSimpleHandshake();
public:
	/**
	* simple handshake.
	* @param complex_hs, try complex handshake first, 
	* 		if failed, rollback to simple handshake.
	*/
	virtual int handshake_with_client(ISrsProtocolReaderWriter* io, SrsComplexHandshake& complex_hs);
	virtual int handshake_with_server(ISrsProtocolReaderWriter* io, SrsComplexHandshake& complex_hs);
};

/**
* rtmp complex handshake,
* @see also crtmp(crtmpserver) or librtmp,
* @see also: http://blog.csdn.net/win_lin/article/details/13006803
*/
class SrsComplexHandshake
{
public:
	SrsComplexHandshake();
	virtual ~SrsComplexHandshake();
public:
	/**
	* complex hanshake.
	* @_c1, size of c1 must be 1536.
	* @remark, user must free the c1.
	* @return user must:
	* 	continue connect app if success,
	* 	try simple handshake if error is ERROR_RTMP_TRY_SIMPLE_HS,
	* 	otherwise, disconnect
	*/
	virtual int handshake_with_client(ISrsProtocolReaderWriter* io, char* _c1);
	virtual int handshake_with_server(ISrsProtocolReaderWriter* io);
};

#endif