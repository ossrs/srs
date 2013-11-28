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

#include <srs_core_forward.hpp>

#include <stdlib.h>

#include <srs_core_error.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_log.hpp>

SrsForwarder::SrsForwarder()
{
	client = new SrsRtmpClient();
	tid = NULL;
	loop = false;
}

SrsForwarder::~SrsForwarder()
{
	if (tid) {
		loop = false;
		st_thread_interrupt(tid);
		st_thread_join(tid, NULL);
		tid = NULL;
	}
	
	srs_freep(client);
}

int SrsForwarder::on_publish(std::string vhost, std::string app, std::string stream, std::string forward_server)
{
	int ret = ERROR_SUCCESS;
	
	std::string tc_url = "rtmp://";
	tc_url += vhost;
	tc_url += "/";
	tc_url += app;
	
	std::string stream_name = stream;
	std::string server = forward_server;
	int port = 1935;
	
	size_t pos = forward_server.find(":");
	if (pos != std::string::npos) {
		port = ::atoi(forward_server.substr(pos + 1).c_str());
		server = forward_server.substr(0, pos);
	}
	
	srs_trace("forward stream=%s, tcUrl=%s to server=%s, port=%d",
		stream_name.c_str(), tc_url.c_str(), server.c_str(), port);
	
	return ret;
}

void SrsForwarder::on_unpublish()
{
}

int SrsForwarder::on_meta_data(SrsOnMetaDataPacket* metadata)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SrsForwarder::on_audio(SrsSharedPtrMessage* msg)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

int SrsForwarder::on_video(SrsSharedPtrMessage* msg)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

