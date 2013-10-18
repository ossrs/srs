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

#include <srs_core_client.hpp>

#include <arpa/inet.h>

#include <srs_core_error.hpp>
#include <srs_core_log.hpp>
#include <srs_core_rtmp.hpp>

SrsClient::SrsClient(SrsServer* srs_server, st_netfd_t client_stfd)
	: SrsConnection(srs_server, client_stfd)
{
	ip = NULL;
	rtmp = new SrsRtmp(client_stfd);
}

SrsClient::~SrsClient()
{
	if (ip) {
		delete[] ip;
		ip = NULL;
	}
	
	if (rtmp) {
		delete rtmp;
		rtmp = NULL;
	}
}

int SrsClient::do_cycle()
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
		srs_warn("get peer ip failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("get peer ip success. ip=%s", ip);
	
	if ((ret = rtmp->handshake()) != ERROR_SUCCESS) {
		srs_warn("rtmp handshake failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("rtmp handshake success");
	
	return ret;
}

int SrsClient::get_peer_ip()
{
	int ret = ERROR_SUCCESS;
	
    int fd = st_netfd_fileno(stfd);
    
    // discovery client information
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(fd, (sockaddr*)&addr, &addrlen) == -1) {
        ret = ERROR_SOCKET_GET_PEER_NAME;
        srs_error("discovery client information failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("get peer name success.");

    // ip v4 or v6
    char buf[INET6_ADDRSTRLEN];
    memset(buf, 0, sizeof(buf));
    
    if ((inet_ntop(addr.sin_family, &addr.sin_addr, buf, sizeof(buf))) == NULL) {
        ret = ERROR_SOCKET_GET_PEER_IP;
        srs_error("convert client information failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("get peer ip of client ip=%s, fd=%d", buf, fd);
    
    ip = new char[strlen(buf) + 1];
    strcpy(ip, buf);
    
    srs_info("get peer ip success. ip=%s, fd=%d", ip, fd);
    
    return ret;
}

