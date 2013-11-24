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
#include <stdlib.h>

#include <srs_core_error.hpp>
#include <srs_core_log.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_protocol.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_source.hpp>
#include <srs_core_server.hpp>
#include <srs_core_pithy_print.hpp>
#include <srs_core_config.hpp>
#include <srs_core_refer.hpp>
#include <srs_core_hls.hpp>

#define SRS_PULSE_TIMEOUT_MS 100
#define SRS_SEND_TIMEOUT_MS 5000000L
#define SRS_RECV_TIMEOUT_MS SRS_SEND_TIMEOUT_MS

SrsClient::SrsClient(SrsServer* srs_server, st_netfd_t client_stfd)
	: SrsConnection(srs_server, client_stfd)
{
	ip = NULL;
	req = new SrsRequest();
	res = new SrsResponse();
	rtmp = new SrsRtmp(client_stfd);
	refer = new SrsRefer();
}

SrsClient::~SrsClient()
{
	srs_freepa(ip);
	srs_freep(req);
	srs_freep(res);
	srs_freep(rtmp);
	srs_freep(refer);
}

int SrsClient::do_cycle()
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
		srs_error("get peer ip failed. ret=%d", ret);
		return ret;
	}
	srs_trace("get peer ip success. ip=%s, send_to=%"PRId64", recv_to=%"PRId64"", 
		ip, SRS_SEND_TIMEOUT_MS, SRS_RECV_TIMEOUT_MS);

	rtmp->set_recv_timeout(SRS_RECV_TIMEOUT_MS * 1000);
	rtmp->set_send_timeout(SRS_SEND_TIMEOUT_MS * 1000);
	
	if ((ret = rtmp->handshake()) != ERROR_SUCCESS) {
		srs_error("rtmp handshake failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("rtmp handshake success");
	
	if ((ret = rtmp->connect_app(req)) != ERROR_SUCCESS) {
		srs_error("rtmp connect vhost/app failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("rtmp connect app success");
	
	if ((ret = check_vhost()) != ERROR_SUCCESS) {
		srs_error("check vhost failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("check vhost success.");
	
	srs_trace("rtmp connect app success. "
		"tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%s, app=%s", 
		req->tcUrl.c_str(), req->pageUrl.c_str(), req->swfUrl.c_str(), 
		req->schema.c_str(), req->vhost.c_str(), req->port.c_str(),
		req->app.c_str());
	
	if ((ret = refer->check(req->pageUrl, config->get_refer(req->vhost))) != ERROR_SUCCESS) {
		srs_error("check refer failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("check refer success.");
		
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
	req->strip();
	srs_trace("identify client success. type=%d, stream_name=%s", type, req->stream.c_str());
	
	int chunk_size = 4096;
	SrsConfDirective* conf = config->get_chunk_size();
	if (conf && !conf->arg0().empty()) {
		chunk_size = ::atoi(conf->arg0().c_str());
	}
	if ((ret = rtmp->set_chunk_size(chunk_size)) != ERROR_SUCCESS) {
		srs_error("set chunk_size=%d failed. ret=%d", chunk_size, ret);
		return ret;
	}
	srs_trace("set chunk_size=%d success", chunk_size);
	
	// find a source to publish.
	SrsSource* source = SrsSource::find(req->get_stream_url());
	srs_assert(source != NULL);
	SrsHLS* hls = source->get_hls();
	
	bool enabled_cache = true;
	conf = config->get_gop_cache(req->vhost);
	if (conf && conf->arg0() == "off") {
		enabled_cache = false;
	}
	source->set_cache(enabled_cache);

	srs_info("source found, url=%s, enabled_cache=%d", req->get_stream_url().c_str(), enabled_cache);
	
	switch (type) {
		case SrsClientPlay: {
			srs_verbose("start to play stream %s.", req->stream.c_str());
			
			if ((ret = rtmp->start_play(res->stream_id)) != ERROR_SUCCESS) {
				srs_error("start to play stream failed. ret=%d", ret);
				return ret;
			}
			srs_info("start to play stream %s success", req->stream.c_str());
			return playing(source);
		}
		case SrsClientFMLEPublish: {
			srs_verbose("FMLE start to publish stream %s.", req->stream.c_str());
			
			if ((ret = rtmp->start_fmle_publish(res->stream_id)) != ERROR_SUCCESS) {
				srs_error("start to publish stream failed. ret=%d", ret);
				return ret;
			}
			srs_info("start to publish stream %s success", req->stream.c_str());
			ret = publish(source, true);
			hls->on_unpublish();
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
			ret = publish(source, false);
			hls->on_unpublish();
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

int SrsClient::check_vhost()
{
	int ret = ERROR_SUCCESS;
	
	srs_assert(req != NULL);
	
	SrsConfDirective* vhost = config->get_vhost(req->vhost);
	if (vhost == NULL) {
		ret = ERROR_RTMP_VHOST_NOT_FOUND;
		srs_error("vhost %s not found. ret=%d", req->vhost.c_str(), ret);
		return ret;
	}
	
	SrsConfDirective* conf = NULL;
	if ((conf = config->get_vhost_enabled(req->vhost)) != NULL && conf->arg0() != "on") {
		ret = ERROR_RTMP_VHOST_NOT_FOUND;
		srs_error("vhost %s disabled. ret=%d", req->vhost.c_str(), ret);
		return ret;
	}
	
	if (req->vhost != vhost->arg0()) {
		srs_trace("vhost change from %s to %s", req->vhost.c_str(), vhost->arg0().c_str());
		req->vhost = vhost->arg0();
	}
	
	return ret;
}

int SrsClient::playing(SrsSource* source)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = refer->check(req->pageUrl, config->get_refer_play(req->vhost))) != ERROR_SUCCESS) {
		srs_error("check play_refer failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("check play_refer success.");
	
	SrsConsumer* consumer = NULL;
	if ((ret = source->create_consumer(consumer)) != ERROR_SUCCESS) {
		srs_error("create consumer failed. ret=%d", ret);
		return ret;
	}
	
	srs_assert(consumer != NULL);
	SrsAutoFree(SrsConsumer, consumer, false);
	srs_verbose("consumer created success.");
	
	rtmp->set_recv_timeout(SRS_PULSE_TIMEOUT_MS * 1000);
	
	SrsPithyPrint pithy_print(SRS_STAGE_PLAY_USER);

	while (true) {
		pithy_print.elapse(SRS_PULSE_TIMEOUT_MS);
		
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
			if ((ret = process_play_control_msg(consumer, msg)) != ERROR_SUCCESS) {
				srs_error("process play control message failed. ret=%d", ret);
				return ret;
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
		if (pithy_print.can_print()) {
			srs_trace("-> clock=%u, time=%"PRId64", cmr=%d, msgs=%d, obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
				(int)(srs_get_system_time_ms()/1000), pithy_print.get_age(), ctl_msg_ret, count, rtmp->get_send_bytes(), rtmp->get_recv_bytes(), rtmp->get_send_kbps(), rtmp->get_recv_kbps());
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

int SrsClient::publish(SrsSource* source, bool is_fmle)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = refer->check(req->pageUrl, config->get_refer_publish(req->vhost))) != ERROR_SUCCESS) {
		srs_error("check publish_refer failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("check publish_refer success.");
	
	SrsPithyPrint pithy_print(SRS_STAGE_PUBLISH_USER);
	SrsHLS* hls = source->get_hls();
	
	// notify the hls to prepare when publish start.
	if ((ret = hls->on_publish(req->vhost)) != ERROR_SUCCESS) {
		srs_error("hls on_publish failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("hls on_publish success.");
	
	while (true) {
		// switch to other st-threads.
		st_usleep(0);
		
		SrsCommonMessage* msg = NULL;
		if ((ret = rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
			srs_error("recv identify client message failed. ret=%d", ret);
			return ret;
		}

		SrsAutoFree(SrsCommonMessage, msg, false);
		
		pithy_print.set_age(msg->header.timestamp);

		// reportable
		if (pithy_print.can_print()) {
			srs_trace("<- clock=%u, time=%"PRId64", obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
				(int)(srs_get_system_time_ms()/1000), pithy_print.get_age(), rtmp->get_send_bytes(), rtmp->get_recv_bytes(), rtmp->get_send_kbps(), rtmp->get_recv_kbps());
		}

		if ((ret = process_publish_message(source, hls, msg, is_fmle)) != ERROR_SUCCESS) {
			srs_error("process publish message failed. ret=%d", ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsClient::process_publish_message(SrsSource* source, SrsHLS* hls, SrsCommonMessage* msg, bool is_fmle)
{
	int ret = ERROR_SUCCESS;
	
	// process audio packet
	if (msg->header.is_audio()) {
		if ((ret = hls->on_audio(msg)) != ERROR_SUCCESS) {
			srs_error("hls process audio message failed. ret=%d", ret);
			return ret;
		}
		if ((ret = source->on_audio(msg)) != ERROR_SUCCESS) {
			srs_error("source process audio message failed. ret=%d", ret);
			return ret;
		}
	}
	// process video packet
	if (msg->header.is_video()) {
		if ((ret = hls->on_video(msg)) != ERROR_SUCCESS) {
			srs_error("hls process video message failed. ret=%d", ret);
			return ret;
		}
		if ((ret = source->on_video(msg)) != ERROR_SUCCESS) {
			srs_error("source process video message failed. ret=%d", ret);
			return ret;
		}
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
			if ((ret = hls->on_meta_data(metadata)) != ERROR_SUCCESS) {
				srs_error("hls process onMetaData message failed. ret=%d", ret);
				return ret;
			}
			if ((ret = source->on_meta_data(msg, metadata)) != ERROR_SUCCESS) {
				srs_error("source process onMetaData message failed. ret=%d", ret);
				return ret;
			}
			srs_trace("process onMetaData message success.");
			return ret;
		}
		
		srs_trace("ignore AMF0/AMF3 data message.");
		return ret;
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
		return ret;
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
    
    srs_verbose("get peer ip success. ip=%s, fd=%d", ip, fd);
    
    return ret;
}

int SrsClient::process_play_control_msg(SrsConsumer* consumer, SrsCommonMessage* msg)
{
	int ret = ERROR_SUCCESS;
	
	if (!msg) {
		srs_verbose("ignore all empty message.");
		return ret;
	}
	SrsAutoFree(SrsCommonMessage, msg, false);
	
	if (!msg->header.is_amf0_command() && !msg->header.is_amf3_command()) {
		srs_info("ignore all message except amf0/amf3 command.");
		return ret;
	}
	
	if ((ret = msg->decode_packet()) != ERROR_SUCCESS) {
		srs_error("decode the amf0/amf3 command packet failed. ret=%d", ret);
		return ret;
	}
	srs_info("decode the amf0/amf3 command packet success.");
	
	SrsPausePacket* pause = dynamic_cast<SrsPausePacket*>(msg->get_packet());
	if (!pause) {
		srs_info("ignore all amf0/amf3 command except pause.");
		return ret;
	}
	
	if ((ret = rtmp->on_play_client_pause(res->stream_id, pause->is_pause)) != ERROR_SUCCESS) {
		srs_error("rtmp process play client pause failed. ret=%d", ret);
		return ret;
	}
	
	if ((ret = consumer->on_play_client_pause(pause->is_pause)) != ERROR_SUCCESS) {
		srs_error("consumer process play client pause failed. ret=%d", ret);
		return ret;
	}
	srs_info("process pause success, is_pause=%d, time=%d.", pause->is_pause, pause->time_ms);
	
    return ret;
}

