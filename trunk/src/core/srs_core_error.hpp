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

#ifndef SRS_CORE_ERROR_HPP
#define SRS_CORE_ERROR_HPP

/*
#include <srs_core_error.hpp>
*/

#include <srs_core.hpp>

#define ERROR_SUCCESS 					0

#define ERROR_ST_SET_EPOLL 				100
#define ERROR_ST_INITIALIZE 			101
#define ERROR_ST_OPEN_SOCKET			102
#define ERROR_ST_CREATE_LISTEN_THREAD	103
#define ERROR_ST_CREATE_CYCLE_THREAD	104

#define ERROR_SOCKET_CREATE 			200
#define ERROR_SOCKET_SETREUSE 			201
#define ERROR_SOCKET_BIND 				202
#define ERROR_SOCKET_LISTEN 			203
#define ERROR_SOCKET_CLOSED 			204
#define ERROR_SOCKET_GET_PEER_NAME		205
#define ERROR_SOCKET_GET_PEER_IP		206
#define ERROR_SOCKET_READ				207
#define ERROR_SOCKET_READ_FULLY			208
#define ERROR_SOCKET_WRITE				209
#define ERROR_SOCKET_WAIT				210
#define ERROR_SOCKET_TIMEOUT			211

#define ERROR_RTMP_PLAIN_REQUIRED		300
#define ERROR_RTMP_CHUNK_START			301
#define ERROR_RTMP_MSG_INVLIAD_SIZE		302
#define ERROR_RTMP_AMF0_DECODE			303
#define ERROR_RTMP_AMF0_INVALID			304
#define ERROR_RTMP_REQ_CONNECT			305
#define ERROR_RTMP_REQ_TCURL			306
#define ERROR_RTMP_MESSAGE_DECODE		307
#define ERROR_RTMP_MESSAGE_ENCODE		308
#define ERROR_RTMP_AMF0_ENCODE			309
#define ERROR_RTMP_CHUNK_SIZE			310
#define ERROR_RTMP_TRY_SIMPLE_HS		311

#define ERROR_SYSTEM_STREAM_INIT		400
#define ERROR_SYSTEM_PACKET_INVALID		401
#define ERROR_SYSTEM_CLIENT_INVALID		402
#define ERROR_SYSTEM_ASSERT_FAILED		403

#endif