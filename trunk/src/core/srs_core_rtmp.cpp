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
#include <srs_core_amf0.hpp>

/**
* the signature for packets to client.
*/
#define RTMP_SIG_FMS_VER "3,5,3,888"
#define RTMP_SIG_AMF0_VER 3
#define RTMP_SIG_SRS_NAME "srs(simple rtmp server)"
#define RTMP_SIG_SRS_URL "https://github.com/winlinvip/simple-rtmp-server"
#define RTMP_SIG_SRS_VERSION "0.1"

int SrsRequest::discovery_app()
{
	int ret = ERROR_SUCCESS;
	
	size_t pos = std::string::npos;
	std::string url = tcUrl;
	
	if ((pos = url.find("://")) != std::string::npos) {
		schema = url.substr(0, pos);
		url = url.substr(schema.length() + 3);
		srs_verbose("discovery schema=%s", schema.c_str());
	}
	
	if ((pos = url.find("/")) != std::string::npos) {
		vhost = url.substr(0, pos);
		url = url.substr(vhost.length() + 1);
		srs_verbose("discovery vhost=%s", vhost.c_str());
	}

	port = "1935";
	if ((pos = vhost.find(":")) != std::string::npos) {
		port = vhost.substr(pos + 1);
		vhost = vhost.substr(0, pos);
		srs_verbose("discovery vhost=%s, port=%s", vhost.c_str(), port.c_str());
	}
	
	app = url;
	srs_info("discovery app success. schema=%s, vhost=%s, port=%s, app=%s",
		schema.c_str(), vhost.c_str(), port.c_str(), app.c_str());
	
	if (schema.empty() || vhost.empty() || port.empty() || app.empty()) {
		ret = ERROR_RTMP_REQ_TCURL;
		srs_error("discovery tcUrl failed. "
			"tcUrl=%s, schema=%s, vhost=%s, port=%s, app=%s, ret=%d",
			tcUrl.c_str(), schema.c_str(), vhost.c_str(), port.c_str(), app.c_str(), ret);
		return ret;
	}
	
	return ret;
}

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
    SrsSocket skt(stfd);
    
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

int SrsRtmp::connect_app(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsMessage* msg = NULL;
	SrsConnectAppPacket* pkt = NULL;
	if ((ret = srs_rtmp_expect_message<SrsConnectAppPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
		srs_error("expect connect app message failed. ret=%d", ret);
		return ret;
	}
	SrsAutoFree(SrsMessage, msg, false);
	srs_info("get connect app message");
	
	SrsAmf0Any* prop = NULL;
	
	if ((prop = pkt->command_object->ensure_property_string("tcUrl")) == NULL) {
		ret = ERROR_RTMP_REQ_CONNECT;
		srs_error("invalid request, must specifies the tcUrl. ret=%d", ret);
		return ret;
	}
	req->tcUrl = srs_amf0_convert<SrsAmf0String>(prop)->value;
	
	if ((prop = pkt->command_object->ensure_property_string("pageUrl")) != NULL) {
		req->pageUrl = srs_amf0_convert<SrsAmf0String>(prop)->value;
	}
	
	if ((prop = pkt->command_object->ensure_property_string("swfUrl")) != NULL) {
		req->swfUrl = srs_amf0_convert<SrsAmf0String>(prop)->value;
	}
	srs_info("get connect app message params success.");
	
	return req->discovery_app();
}

int SrsRtmp::set_window_ack_size(int ack_size)
{
	int ret = ERROR_SUCCESS;
	
	SrsMessage* msg = new SrsMessage();
	SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
	
	pkt->ackowledgement_window_size = ack_size;
	msg->set_packet(pkt);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send ack size message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send ack size message success. ack_size=%d", ack_size);
	
	return ret;
}

int SrsRtmp::set_peer_bandwidth(int bandwidth, int type)
{
	int ret = ERROR_SUCCESS;
	
	SrsMessage* msg = new SrsMessage();
	SrsSetPeerBandwidthPacket* pkt = new SrsSetPeerBandwidthPacket();
	
	pkt->bandwidth = bandwidth;
	pkt->type = type;
	msg->set_packet(pkt);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send set bandwidth message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send set bandwidth message "
		"success. bandwidth=%d, type=%d", bandwidth, type);
	
	return ret;
}

int SrsRtmp::response_connect_app()
{
	int ret = ERROR_SUCCESS;
	
	SrsMessage* msg = new SrsMessage();
	SrsConnectAppResPacket* pkt = new SrsConnectAppResPacket();
	
	pkt->command_name = "_result";
	
	pkt->props->properties["fmsVer"] = new SrsAmf0String("FMS/"RTMP_SIG_FMS_VER);
	pkt->props->properties["capabilities"] = new SrsAmf0Number(123);
	pkt->props->properties["mode"] = new SrsAmf0Number(1);
	
	pkt->info->properties["level"] = new SrsAmf0String("status");
	pkt->info->properties["code"] = new SrsAmf0String("NetConnection.Connect.Success");
	pkt->info->properties["description"] = new SrsAmf0String("Connection succeeded");
	pkt->info->properties["objectEncoding"] = new SrsAmf0Number(RTMP_SIG_AMF0_VER);
	SrsASrsAmf0EcmaArray* data = new SrsASrsAmf0EcmaArray();
	pkt->info->properties["data"] = data;
	
	data->properties["version"] = new SrsAmf0String(RTMP_SIG_FMS_VER);
	data->properties["server"] = new SrsAmf0String(RTMP_SIG_SRS_NAME);
	data->properties["srs_url"] = new SrsAmf0String(RTMP_SIG_SRS_URL);
	data->properties["srs_version"] = new SrsAmf0String(RTMP_SIG_SRS_VERSION);
	
	msg->set_packet(pkt);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send connect app response message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send connect app response message success.");
	
	return ret;
}

