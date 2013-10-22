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

// default stream id for response the createStream request.
#define SRS_DEFAULT_SID 1

SrsClient::SrsClient(SrsServer* srs_server, st_netfd_t client_stfd)
	: SrsConnection(srs_server, client_stfd)
{
	ip = NULL;
	req = new SrsRequest();
	rtmp = new SrsRtmp(client_stfd);
}

SrsClient::~SrsClient()
{
	srs_freepa(ip);
	srs_freep(req);
	srs_freep(rtmp);
}

int SrsClient::do_cycle()
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
		srs_error("get peer ip failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("get peer ip success. ip=%s", ip);
	
	if ((ret = rtmp->handshake()) != ERROR_SUCCESS) {
		srs_error("rtmp handshake failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("rtmp handshake success");
	
	if ((ret = rtmp->connect_app(req)) != ERROR_SUCCESS) {
		srs_error("rtmp connect vhost/app failed. ret=%d", ret);
		return ret;
	}
	srs_trace("rtmp connect app success. "
		"tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%s, app=%s", 
		req->tcUrl.c_str(), req->pageUrl.c_str(), req->swfUrl.c_str(), 
		req->schema.c_str(), req->vhost.c_str(), req->port.c_str(),
		req->app.c_str());
		
	if ((ret = rtmp->set_window_ack_size(2.5 * 1000 * 1000)) != ERROR_SUCCESS) {
		srs_error("set window acknowledgement size failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("set window acknowledgement size success");
		
	if ((ret = rtmp->set_peer_bandwidth(2.5 * 1000 * 1000, 2)) != ERROR_SUCCESS) {
		srs_error("set peer bandwidth failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("set peer bandwidth success");
		
	if ((ret = rtmp->response_connect_app(req)) != ERROR_SUCCESS) {
		srs_error("response connect app failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("response connect app success");
		
	if ((ret = rtmp->on_bw_done()) != ERROR_SUCCESS) {
		srs_error("on_bw_done failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("on_bw_done success");
	
	int stream_id = SRS_DEFAULT_SID;
	SrsClientType type;
	if ((ret = rtmp->identify_client(stream_id, type, req->stream)) != ERROR_SUCCESS) {
		srs_error("identify client failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("identify client success. type=%d, stream_name=%s", type, req->stream.c_str());
		
	// TODO: read from config.
	int chunk_size = 4096;
	if ((ret = rtmp->set_chunk_size(chunk_size)) != ERROR_SUCCESS) {
		srs_error("set chunk size failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("set chunk size success");
	
	switch (type) {
		case SrsClientPlay: {
			srs_verbose("start to play stream %s.", req->stream.c_str());
			
			if ((ret = rtmp->start_play(stream_id)) != ERROR_SUCCESS) {
				srs_error("start to play stream failed. ret=%d", ret);
				return ret;
			}
			srs_info("start to play stream %s success", req->stream.c_str());
			return streaming_play();
		}
		default: {
			ret = ERROR_SYSTEM_CLIENT_INVALID;
			srs_info("invalid client type=%d. ret=%d", type, ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsClient::streaming_play()
{
	int ret = ERROR_SUCCESS;
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
    
    srs_trace("get peer ip success. ip=%s, fd=%d", ip, fd);
    
    return ret;
}

