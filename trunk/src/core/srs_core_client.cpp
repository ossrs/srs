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
#include <srs_core_protocol.hpp>
#include <srs_core_auto_free.hpp>
#include <srs_core_source.hpp>
#include <srs_core_server.hpp>

#define SRS_PULSE_TIMEOUT_MS 100
#define SRS_SEND_TIMEOUT_MS 5000

SrsClient::SrsClient(SrsServer* srs_server, st_netfd_t client_stfd)
	: SrsConnection(srs_server, client_stfd)
{
	ip = NULL;
	req = new SrsRequest();
	res = new SrsResponse();
	rtmp = new SrsRtmp(client_stfd);
}

SrsClient::~SrsClient()
{
	srs_freepa(ip);
	srs_freep(req);
	srs_freep(res);
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

	rtmp->set_recv_timeout(SRS_SEND_TIMEOUT_MS);
	rtmp->set_send_timeout(SRS_SEND_TIMEOUT_MS);
	
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
	
	SrsClientType type;
	if ((ret = rtmp->identify_client(res->stream_id, type, req->stream)) != ERROR_SUCCESS) {
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
	
	// find a source to publish.
	SrsSource* source = SrsSource::find(req->get_stream_url());
	srs_assert(source != NULL);
	srs_info("source found, url=%s", req->get_stream_url().c_str());
	
	switch (type) {
		case SrsClientPlay: {
			srs_verbose("start to play stream %s.", req->stream.c_str());
			
			if ((ret = rtmp->start_play(res->stream_id)) != ERROR_SUCCESS) {
				srs_error("start to play stream failed. ret=%d", ret);
				return ret;
			}
			srs_info("start to play stream %s success", req->stream.c_str());
			return streaming_play(source);
		}
		case SrsClientFMLEPublish: {
			srs_verbose("FMLE start to publish stream %s.", req->stream.c_str());
			
			if ((ret = rtmp->start_fmle_publish(res->stream_id)) != ERROR_SUCCESS) {
				srs_error("start to publish stream failed. ret=%d", ret);
				return ret;
			}
			srs_info("start to publish stream %s success", req->stream.c_str());
			ret = streaming_publish(source, true);
			source->on_unpublish();
			return ret;
		}
		case SrsClientFlashPublish: {
			srs_verbose("flash start to publish stream %s.", req->stream.c_str());
			
			if ((ret = rtmp->start_flash_publish(res->stream_id)) != ERROR_SUCCESS) {
				srs_error("flash start to publish stream failed. ret=%d", ret);
				return ret;
			}
			srs_info("flash start to publish stream %s success", req->stream.c_str());
			ret = streaming_publish(source, false);
			source->on_unpublish();
			return ret;
		}
		default: {
			ret = ERROR_SYSTEM_CLIENT_INVALID;
			srs_info("invalid client type=%d. ret=%d", type, ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsClient::streaming_play(SrsSource* source)
{
	int ret = ERROR_SUCCESS;
	
	SrsConsumer* consumer = NULL;
	if ((ret = source->create_consumer(consumer)) != ERROR_SUCCESS) {
		srs_error("create consumer failed. ret=%d", ret);
		return ret;
	}
	
	srs_assert(consumer != NULL);
	SrsAutoFree(SrsConsumer, consumer, false);
	srs_verbose("consumer created success.");
	
	rtmp->set_recv_timeout(SRS_PULSE_TIMEOUT_MS);
	
	int64_t report_time = 0;
	int64_t reported_time = 0;

	while (true) {
		report_time += SRS_PULSE_TIMEOUT_MS;
		
		// switch to other st-threads.
		st_usleep(0);

		// read from client.
		int ctl_msg_ret = ERROR_SUCCESS;
		if (true) {
			SrsCommonMessage* msg = NULL;
			ctl_msg_ret = ret = rtmp->recv_message(&msg);
			
			srs_verbose("play loop recv message. ret=%d", ret);
			if (ret != ERROR_SUCCESS && ret != ERROR_SOCKET_TIMEOUT) {
				srs_error("recv client control message failed. ret=%d", ret);
				return ret;
			}
			if (ret == ERROR_SUCCESS && !msg) {
				srs_info("play loop got a message.");
				SrsAutoFree(SrsCommonMessage, msg, false);
				// TODO: process it.
			}
		}
		
		// get messages from consumer.
		SrsSharedPtrMessage** msgs = NULL;
		int count = 0;
		if ((ret = consumer->get_packets(0, msgs, count)) != ERROR_SUCCESS) {
			srs_error("get messages from consumer failed. ret=%d", ret);
			return ret;
		}

		// reportable
		if (server->can_report(reported_time, report_time)) {
			srs_trace("play report, time=%"PRId64", ctl_msg_ret=%d, msgs=%d", report_time, ctl_msg_ret, count);
		}
		
		if (count <= 0) {
			srs_verbose("no packets in queue.");
			continue;
		}
		SrsAutoFree(SrsSharedPtrMessage*, msgs, true);
		
		// sendout messages
		for (int i = 0; i < count; i++) {
			SrsSharedPtrMessage* msg = msgs[i];
			
			// the send_message will free the msg, 
			// so set the msgs[i] to NULL.
			msgs[i] = NULL;
			
			if ((ret = rtmp->send_message(msg)) != ERROR_SUCCESS) {
				srs_error("send message to client failed. ret=%d", ret);
				return ret;
			}
		}
	}
	
	return ret;
}

int SrsClient::streaming_publish(SrsSource* source, bool is_fmle)
{
	int ret = ERROR_SUCCESS;
	
	while (true) {
		// switch to other st-threads.
		st_usleep(0);
		
		SrsCommonMessage* msg = NULL;
		if ((ret = rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
			srs_error("recv identify client message failed. ret=%d", ret);
			return ret;
		}

		SrsAutoFree(SrsCommonMessage, msg, false);
		
		// process audio packet
		if (msg->header.is_audio() && ((ret = source->on_audio(msg)) != ERROR_SUCCESS)) {
			srs_error("process audio message failed. ret=%d", ret);
			return ret;
		}
		// process video packet
		if (msg->header.is_video() && ((ret = source->on_video(msg)) != ERROR_SUCCESS)) {
			srs_error("process video message failed. ret=%d", ret);
			return ret;
		}
		
		// process onMetaData
		if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
			if ((ret = msg->decode_packet()) != ERROR_SUCCESS) {
				srs_error("decode onMetaData message failed. ret=%d", ret);
				return ret;
			}
		
			SrsPacket* pkt = msg->get_packet();
			if (dynamic_cast<SrsOnMetaDataPacket*>(pkt)) {
				SrsOnMetaDataPacket* metadata = dynamic_cast<SrsOnMetaDataPacket*>(pkt);
				if ((ret = source->on_meta_data(msg, metadata)) != ERROR_SUCCESS) {
					srs_error("process onMetaData message failed. ret=%d", ret);
					return ret;
				}
				srs_trace("process onMetaData message success.");
				continue;
			}
			
			srs_trace("ignore AMF0/AMF3 data message.");
			continue;
		}
		
		// process UnPublish event.
		if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
			if ((ret = msg->decode_packet()) != ERROR_SUCCESS) {
				srs_error("decode unpublish message failed. ret=%d", ret);
				return ret;
			}
			
			// flash unpublish.
			if (!is_fmle) {
				srs_trace("flash publish finished.");
				return ret;
			}
		
			SrsPacket* pkt = msg->get_packet();
			if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
				SrsFMLEStartPacket* unpublish = dynamic_cast<SrsFMLEStartPacket*>(pkt);
				return rtmp->fmle_unpublish(res->stream_id, unpublish->transaction_id);
			}
			
			srs_trace("ignore AMF0/AMF3 command message.");
			continue;
		}
	}
	
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

