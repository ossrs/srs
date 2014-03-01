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

#include <srs_librtmp.hpp>

#include <stdlib.h>

#include <string>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
    
/**
* the stream over epoll: never wait for data coming, that is async mode.
*/
class SimpleSocketStream
{
private:
    int sock;
public:
    SimpleSocketStream(int fd){
        sock = fd;
    }
    virtual ~SimpleSocketStream() {
        ::close(sock);
    }
public:
};

/**
* export runtime context.
*/
struct Context
{
	std::string url;
    SrsRtmpClient* rtmp;
    SimpleSocketStream* stream;
    int stream_id;
    
    Context() {
        rtmp = NULL;
        stream = NULL;
        stream_id = 0;
    }
    virtual ~Context() {
        srs_freep(rtmp);
        srs_freep(stream);
    }
};

#ifdef __cplusplus
extern "C"{
#endif

srs_rtmp_t srs_rtmp_create(const char* url){
    Context* context = new Context();
	context->url = url;
    return context;
}

void srs_rtmp_destroy(srs_rtmp_t rtmp){
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    srs_freep(context);
}

int srs_simple_handshake(srs_rtmp_t rtmp)
{
	return ERROR_SUCCESS;
}

int srs_complex_handshake(srs_rtmp_t rtmp)
{
	return ERROR_SUCCESS;
}

int srs_connect_app(srs_rtmp_t rtmp)
{
	return ERROR_SUCCESS;
}

int srs_play_stream(srs_rtmp_t rtmp)
{
	return ERROR_SUCCESS;
}

int srs_publish_stream(srs_rtmp_t rtmp)
{
	return ERROR_SUCCESS;
}

int srs_version_major()
{
	return ::atoi(VERSION_MAJOR);
}

int srs_version_minor()
{
	return ::atoi(VERSION_MINOR);
}

int srs_version_revision()
{
	return ::atoi(VERSION_REVISION);
}

#ifdef __cplusplus
}
#endif
