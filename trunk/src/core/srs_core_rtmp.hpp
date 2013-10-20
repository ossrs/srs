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

#ifndef SRS_CORE_RTMP_HPP
#define SRS_CORE_RTMP_HPP

/*
#include <srs_core_rtmp.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <st.h>

class SrsProtocol;

/**
* the original request from client.
*/
struct SrsRequest
{
	std::string tcUrl;
	std::string pageUrl;
	std::string swfUrl;
	
	std::string schema;
	std::string vhost;
	std::string port;
	std::string app;
	std::string stream;
	
	/**
	* disconvery vhost/app from tcUrl.
	*/
	virtual int discovery_app();
};

/**
* the rtmp provices rtmp-command-protocol services,
* a high level protocol, media stream oriented services,
* such as connect to vhost/app, play stream, get audio/video data.
*/
class SrsRtmp
{
private:
	SrsProtocol* protocol;
	st_netfd_t stfd;
public:
	SrsRtmp(st_netfd_t client_stfd);
	virtual ~SrsRtmp();
public:
	virtual int handshake();
	virtual int connect_app(SrsRequest* req);
	virtual int set_window_ack_size(int ack_size);
};

#endif