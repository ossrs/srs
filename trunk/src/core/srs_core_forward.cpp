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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <srs_core_error.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_log.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_pithy_print.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_config.hpp>

#define SRS_PULSE_TIMEOUT_MS 100
#define SRS_FORWARDER_SLEEP_MS 2000
#define SRS_SEND_TIMEOUT_US 3000000L
#define SRS_RECV_TIMEOUT_US SRS_SEND_TIMEOUT_US

SrsForwarder::SrsForwarder()
{
	client = NULL;
	stfd = NULL;
	stream_id = 0;
	
	tid = NULL;
	loop = false;
}

SrsForwarder::~SrsForwarder()
{
	on_unpublish();
	
	std::vector<SrsSharedPtrMessage*>::iterator it;
	for (it = msgs.begin(); it != msgs.end(); ++it) {
		SrsSharedPtrMessage* msg = *it;
		srs_freep(msg);
	}
	msgs.clear();
}

int SrsForwarder::on_publish(SrsRequest* req, std::string forward_server)
{
	int ret = ERROR_SUCCESS;
	
	// forward app
	app = req->app;
	
	stream_name = req->stream;
	server = forward_server;
	std::string s_port = RTMP_DEFAULT_PORTS;
	port = RTMP_DEFAULT_PORT;
	
	size_t pos = forward_server.find(":");
	if (pos != std::string::npos) {
		s_port = forward_server.substr(pos + 1);
		server = forward_server.substr(0, pos);
	}
	// discovery vhost
	std::string vhost = req->vhost;
	srs_vhost_resolve(vhost, s_port);
	port = ::atoi(s_port.c_str());
	
	// generate tcUrl
	tc_url = "rtmp://";
	tc_url += vhost;
	tc_url += "/";
	tc_url += req->app;
	
	// dead loop check
	std::string source_ep = req->vhost;
	source_ep += ":";
	source_ep += req->port;
	
	std::string dest_ep = vhost;
	dest_ep += ":";
	dest_ep += s_port;
	
	if (source_ep == dest_ep) {
		ret = ERROR_SYSTEM_FORWARD_LOOP;
		srs_warn("farder loop detected. src=%s, dest=%s, ret=%d", 
			source_ep.c_str(), dest_ep.c_str(), ret);
		return ret;
	}
	srs_trace("start forward %s to %s, stream: %s/%s", 
		source_ep.c_str(), dest_ep.c_str(), tc_url.c_str(), 
		stream_name.c_str());
	
	// TODO: seems bug when republish and reforward.
	
	// start forward
	if ((ret = open_socket()) != ERROR_SUCCESS) {
		return ret;
	}
    
    srs_assert(!tid);
    if((tid = st_thread_create(forward_thread, this, 1, 0)) == NULL){
		ret = ERROR_ST_CREATE_FORWARD_THREAD;
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
	
	return ret;
}

void SrsForwarder::on_unpublish()
{
	if (tid) {
		loop = false;
		st_thread_interrupt(tid);
		st_thread_join(tid, NULL);
		tid = NULL;
	}
	
	if (stfd) {
		int fd = st_netfd_fileno(stfd);
		st_netfd_close(stfd);
		stfd = NULL;
		
		// st does not close it sometimes, 
		// close it manually.
		close(fd);
	}
	
	srs_freep(client);
}

int SrsForwarder::on_meta_data(SrsSharedPtrMessage* metadata)
{
	int ret = ERROR_SUCCESS;
	
	msgs.push_back(metadata);
	
	return ret;
}

int SrsForwarder::on_audio(SrsSharedPtrMessage* msg)
{
	int ret = ERROR_SUCCESS;
	
	msgs.push_back(ms
	// TODO: FIXME: must drop the msgs when server failed.g);
	
	return ret;
}

int SrsForwarder::on_video(SrsSharedPtrMessage* msg)
{
	int ret = ERROR_SUCCESS;
	
	// TODO: FIXME: must drop the msgs when server failed.
	msgs.push_back(msg);
	
	return ret;
}

int SrsForwarder::open_socket()
{
	int ret = ERROR_SUCCESS;
	
	srs_trace("forward stream=%s, tcUrl=%s to server=%s, port=%d",
		stream_name.c_str(), tc_url.c_str(), server.c_str(), port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }

	srs_freep(client);
	client = new SrsRtmpClient(stfd);
	
	return ret;
}

int SrsForwarder::connect_server()
{
	int ret = ERROR_SUCCESS;
	
	std::string ip = srs_dns_resolve(server);
	if (ip.empty()) {
		ret = ERROR_SYSTEM_IP_INVALID;
		srs_error("dns resolve server error, ip empty. ret=%d", ret);
		return ret;
	}
	
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_trace("connect to server success. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
	return ret;
}

int SrsForwarder::cycle()
{
	int ret = ERROR_SUCCESS;

	client->set_recv_timeout(SRS_RECV_TIMEOUT_US);
	client->set_send_timeout(SRS_SEND_TIMEOUT_US);
	
	if ((ret = connect_server()) != ERROR_SUCCESS) {
		return ret;
	}
	srs_assert(client);
	
	if ((ret = client->handshake()) != ERROR_SUCCESS) {
		srs_error("handshake with server failed. ret=%d", ret);
		return ret;
	}
	if ((ret = client->connect_app(app, tc_url)) != ERROR_SUCCESS) {
		srs_error("connect with server failed, tcUrl=%s. ret=%d", tc_url.c_str(), ret);
		return ret;
	}
	if ((ret = client->create_stream(stream_id)) != ERROR_SUCCESS) {
		srs_error("connect with server failed, stream_id=%d. ret=%d", stream_id, ret);
		return ret;
	}
	if ((ret = client->publish(stream_name, stream_id)) != ERROR_SUCCESS) {
		srs_error("connect with server failed, stream_name=%s, stream_id=%d. ret=%d", 
			stream_name.c_str(), stream_id, ret);
		return ret;
	}
	
	if ((ret = forward()) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

int SrsForwarder::forward()
{
	int ret = ERROR_SUCCESS;
	
	client->set_recv_timeout(SRS_PULSE_TIMEOUT_MS * 1000);
	
	SrsPithyPrint pithy_print(SRS_STAGE_FORWARDER);

	while (loop) {
		pithy_print.elapse(SRS_PULSE_TIMEOUT_MS);
		
		// switch to other st-threads.
		st_usleep(0);

		// read from client.
		if (true) {
			SrsCommonMessage* msg = NULL;
			ret = client->recv_message(&msg);
			
			srs_verbose("play loop recv message. ret=%d", ret);
			if (ret != ERROR_SUCCESS && ret != ERROR_SOCKET_TIMEOUT) {
				srs_error("recv server control message failed. ret=%d", ret);
				return ret;
			}
		}
		
		// ignore when no messages.
		int count = (int)msgs.size();
		if (msgs.empty()) {
			continue;
		}

		// reportable
		if (pithy_print.can_print()) {
			srs_trace("-> time=%"PRId64", msgs=%d, obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
				pithy_print.get_age(), count, client->get_send_bytes(), client->get_recv_bytes(), client->get_send_kbps(), client->get_recv_kbps());
		}
	
		// all msgs to forward.
		int i = 0;
		for (i = 0; i < count; i++) {
			SrsSharedPtrMessage* msg = msgs[i];
			msgs[i] = NULL;

			// we erased the sendout messages, the msg must not be NULL.
			srs_assert(msg);
			
			ret = client->send_message(msg);
			if (ret != ERROR_SUCCESS) {
				srs_error("forwarder send message to server failed. ret=%d", ret);
		
				// convert the index to count when error.
				i++;
				
				break;
			}
		}

		// clear sendout mesages.
		if (i < count) {
			srs_warn("clear forwarded msg, total=%d, forwarded=%d, ret=%d", count, i, ret);
		} else {
			srs_info("clear forwarded msg, total=%d, forwarded=%d, ret=%d", count, i, ret);
		}
		msgs.erase(msgs.begin(), msgs.begin() + i);
		
		if (ret != ERROR_SUCCESS) {
			break;
		}
	}
	
	return ret;
}

void SrsForwarder::forward_cycle()
{
	int ret = ERROR_SUCCESS;
	
	log_context->generate_id();
	srs_trace("forward cycle start");
	
	while (loop) {
		if ((ret = cycle()) != ERROR_SUCCESS) {
			srs_warn("forward cycle failed, ignored and retry, ret=%d", ret);
		} else {
			srs_info("forward cycle success, retry");
		}
		
		if (!loop) {
			break;
		}
		
		st_usleep(SRS_FORWARDER_SLEEP_MS * 1000);

		if ((ret = open_socket()) != ERROR_SUCCESS) {
			srs_warn("forward cycle reopen failed, ignored and retry, ret=%d", ret);
		} else {
			srs_info("forward cycle reopen success");
		}
	}
	srs_trace("forward cycle finished");
}

void* SrsForwarder::forward_thread(void* arg)
{
	SrsForwarder* obj = (SrsForwarder*)arg;
	srs_assert(obj != NULL);
	
	obj->loop = true;
	obj->forward_cycle();
	
	return NULL;
}

