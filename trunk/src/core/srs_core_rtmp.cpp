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

#include <srs_core_rtmp.hpp>

#include <srs_core_log.hpp>
#include <srs_core_error.hpp>
#include <srs_core_socket.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_auto_free.hpp>

SrsRtmp::SrsRtmp(st_netfd_t client_stfd)
{
	protocol = new SrsProtocol(client_stfd);
	stfd = client_stfd;
}

SrsRtmp::~SrsRtmp()
{
	if (protocol) {
		delete protocol;
		protocol = NULL;
	}
}

int SrsRtmp::handshake()
{
	int ret = ERROR_SUCCESS;
	
    ssize_t nsize;
    Socket skt(stfd);
    
    char* c0c1 = new char[1537];
    SrsAutoFree(char, c0c1, true);
    if ((ret = skt.read_fully(c0c1, 1537, &nsize)) != ERROR_SUCCESS) {
        srs_warn("read c0c1 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("read c0c1 success.");

	// plain text required.
	if (c0c1[0] != 0x03) {
		ret = ERROR_RTMP_PLAIN_REQUIRED;
		srs_warn("only support rtmp plain text. ret=%d", ret);
		return ret;
	}
    srs_verbose("check c0 success, required plain text.");
	
	char* s0s1s2 = new char[3073];
    SrsAutoFree(char, s0s1s2, true);
	// plain text required.
    s0s1s2[0] = 0x03;
    if ((ret = skt.write(s0s1s2, 3073, &nsize)) != ERROR_SUCCESS) {
        srs_warn("send s0s1s2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("send s0s1s2 success.");
    
    char* c2 = new char[1536];
    SrsAutoFree(char, c2, true);
    if ((ret = skt.read_fully(c2, 1536, &nsize)) != ERROR_SUCCESS) {
        srs_warn("read c2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("read c2 success.");
    
    srs_trace("handshake success.");
    
	return ret;
}

int SrsRtmp::connect_app(SrsApp** papp)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

