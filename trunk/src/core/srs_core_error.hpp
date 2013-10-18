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

#endif