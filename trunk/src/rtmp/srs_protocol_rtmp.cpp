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

#include <srs_protocol_rtmp.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_handshake.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

using namespace std;

/**
* the signature for packets to client.
*/
#define RTMP_SIG_FMS_VER 					"3,5,3,888"
#define RTMP_SIG_AMF0_VER 					0
#define RTMP_SIG_CLIENT_ID 					"ASAICiss"

/**
* onStatus consts.
*/
#define StatusLevel 						"level"
#define StatusCode 							"code"
#define StatusDescription 					"description"
#define StatusDetails 						"details"
#define StatusClientId 						"clientid"
// status value
#define StatusLevelStatus 					"status"
// status error
#define StatusLevelError                    "error"
// code value
#define StatusCodeConnectSuccess 			"NetConnection.Connect.Success"
#define StatusCodeConnectRejected 			"NetConnection.Connect.Rejected"
#define StatusCodeStreamReset 				"NetStream.Play.Reset"
#define StatusCodeStreamStart 				"NetStream.Play.Start"
#define StatusCodeStreamPause 				"NetStream.Pause.Notify"
#define StatusCodeStreamUnpause 			"NetStream.Unpause.Notify"
#define StatusCodePublishStart 				"NetStream.Publish.Start"
#define StatusCodeDataStart 				"NetStream.Data.Start"
#define StatusCodeUnpublishSuccess 			"NetStream.Unpublish.Success"

// FMLE
#define RTMP_AMF0_COMMAND_ON_FC_PUBLISH		"onFCPublish"
#define RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH	"onFCUnpublish"

// default stream id for response the createStream request.
#define SRS_DEFAULT_SID 					1

SrsRequest::SrsRequest()
{
	objectEncoding = RTMP_SIG_AMF0_VER;
}

SrsRequest::~SrsRequest()
{
}

SrsRequest* SrsRequest::copy()
{
	SrsRequest* cp = new SrsRequest();
	
	cp->app = app;
	cp->objectEncoding = objectEncoding;
	cp->pageUrl = pageUrl;
	cp->port = port;
	cp->schema = schema;
	cp->stream = stream;
	cp->swfUrl = swfUrl;
	cp->tcUrl = tcUrl;
	cp->vhost = vhost;
	
	return cp;
}

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

	port = RTMP_DEFAULT_PORT;
	if ((pos = vhost.find(":")) != std::string::npos) {
		port = vhost.substr(pos + 1);
		vhost = vhost.substr(0, pos);
		srs_verbose("discovery vhost=%s, port=%s", vhost.c_str(), port.c_str());
	}
	
	app = url;
	srs_vhost_resolve(vhost, app);
	strip();
	
	return ret;
}

string SrsRequest::get_stream_url()
{
	std::string url = "";
	
	url += vhost;
	url += "/";
	url += app;
	url += "/";
	url += stream;

	return url;
}

void SrsRequest::strip()
{
	trim(vhost, "/ \n\r\t");
	trim(app, "/ \n\r\t");
	trim(stream, "/ \n\r\t");
}

string& SrsRequest::trim(string& str, string chs)
{
	for (int i = 0; i < (int)chs.length(); i++) {
		char ch = chs.at(i);
		
		for (std::string::iterator it = str.begin(); it != str.end();) {
			if (ch == *it) {
				it = str.erase(it);
			} else {
				++it;
			}
		}
	}
	
	return str;
}

SrsResponse::SrsResponse()
{
	stream_id = SRS_DEFAULT_SID;
}

SrsResponse::~SrsResponse()
{
}

string srs_client_type_string(SrsClientType type)
{
	switch (type) {
		case SrsClientPlay: return "Play";
		case SrsClientFlashPublish: return "FlashPublish";
		case SrsClientFMLEPublish: return "FMLEPublish";
		default: return "Unknown";
	}
	return "Unknown";
}

SrsRtmpClient::SrsRtmpClient(ISrsProtocolReaderWriter* skt)
{
	io = skt;
	protocol = new SrsProtocol(skt);
}

SrsRtmpClient::~SrsRtmpClient()
{
	srs_freep(protocol);
}

void SrsRtmpClient::set_recv_timeout(int64_t timeout_us)
{
	protocol->set_recv_timeout(timeout_us);
}

void SrsRtmpClient::set_send_timeout(int64_t timeout_us)
{
	protocol->set_send_timeout(timeout_us);
}

int64_t SrsRtmpClient::get_recv_bytes()
{
	return protocol->get_recv_bytes();
}

int64_t SrsRtmpClient::get_send_bytes()
{
	return protocol->get_send_bytes();
}

int SrsRtmpClient::get_recv_kbps()
{
	return protocol->get_recv_kbps();
}

int SrsRtmpClient::get_send_kbps()
{
	return protocol->get_send_kbps();
}

int SrsRtmpClient::recv_message(SrsCommonMessage** pmsg)
{
	return protocol->recv_message(pmsg);
}

int SrsRtmpClient::send_message(ISrsMessage* msg)
{
	return protocol->send_message(msg);
}

int SrsRtmpClient::handshake()
{
	int ret = ERROR_SUCCESS;
    
    SrsComplexHandshake complex_hs;
    SrsSimpleHandshake simple_hs;
    if ((ret = simple_hs.handshake_with_server(io, &complex_hs)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsRtmpClient::simple_handshake()
{
	int ret = ERROR_SUCCESS;
    
    SrsSimpleHandshake simple_hs;
    if ((ret = simple_hs.handshake_with_server(io, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsRtmpClient::complex_handshake()
{
	// TODO: FIXME: only use complex handshake.
    return handshake();
}

int SrsRtmpClient::connect_app(string app, string tc_url)
{
	int ret = ERROR_SUCCESS;
	
	// Connect(vhost, app)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsConnectAppPacket* pkt = new SrsConnectAppPacket();
		msg->set_packet(pkt, 0);
		
		pkt->command_object = new SrsAmf0Object();
		pkt->command_object->set("app", SrsAmf0Any::str(app.c_str()));
		pkt->command_object->set("swfUrl", SrsAmf0Any::str());
		pkt->command_object->set("tcUrl", SrsAmf0Any::str(tc_url.c_str()));
		pkt->command_object->set("fpad", new SrsAmf0Boolean(false));
		pkt->command_object->set("capabilities", new SrsAmf0Number(239));
		pkt->command_object->set("audioCodecs", new SrsAmf0Number(3575));
		pkt->command_object->set("videoCodecs", new SrsAmf0Number(252));
		pkt->command_object->set("videoFunction", new SrsAmf0Number(1));
		pkt->command_object->set("pageUrl", SrsAmf0Any::str());
		pkt->command_object->set("objectEncoding", new SrsAmf0Number(0));
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			return ret;
		}
	}
	
	// Set Window Acknowledgement size(2500000)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
	
		pkt->ackowledgement_window_size = 2500000;
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			return ret;
		}
	}
	
	// expect connect _result
	SrsCommonMessage* msg = NULL;
	SrsConnectAppResPacket* pkt = NULL;
	if ((ret = srs_rtmp_expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
		srs_error("expect connect app response message failed. ret=%d", ret);
		return ret;
	}
	SrsAutoFree(SrsCommonMessage, msg, false);
	srs_info("get connect app response message");
	
    return ret;
}

int SrsRtmpClient::create_stream(int& stream_id)
{
	int ret = ERROR_SUCCESS;
	
	// CreateStream
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsCreateStreamPacket* pkt = new SrsCreateStreamPacket();
	
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			return ret;
		}
	}

	// CreateStream _result.
	if (true) {
		SrsCommonMessage* msg = NULL;
		SrsCreateStreamResPacket* pkt = NULL;
		if ((ret = srs_rtmp_expect_message<SrsCreateStreamResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
			srs_error("expect create stream response message failed. ret=%d", ret);
			return ret;
		}
		SrsAutoFree(SrsCommonMessage, msg, false);
		srs_info("get create stream response message");

		stream_id = (int)pkt->stream_id;
	}
	
	return ret;
}

int SrsRtmpClient::play(string stream, int stream_id)
{
	int ret = ERROR_SUCCESS;

	// Play(stream)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsPlayPacket* pkt = new SrsPlayPacket();
	
		pkt->stream_name = stream;
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send play stream failed. "
				"stream=%s, stream_id=%d, ret=%d", 
				stream.c_str(), stream_id, ret);
			return ret;
		}
	}
	
	// SetBufferLength(1000ms)
	int buffer_length_ms = 1000;
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsUserControlPacket* pkt = new SrsUserControlPacket();
	
		pkt->event_type = SrcPCUCSetBufferLength;
		pkt->event_data = stream_id;
		pkt->extra_data = buffer_length_ms;
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send set buffer length failed. "
				"stream=%s, stream_id=%d, bufferLength=%d, ret=%d", 
				stream.c_str(), stream_id, buffer_length_ms, ret);
			return ret;
		}
	}
	
	// SetChunkSize
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
	
		pkt->chunk_size = SRS_CONF_DEFAULT_CHUNK_SIZE;
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send set chunk size failed. "
				"stream=%s, chunk_size=%d, ret=%d", 
				stream.c_str(), SRS_CONF_DEFAULT_CHUNK_SIZE, ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsRtmpClient::publish(string stream, int stream_id)
{
	int ret = ERROR_SUCCESS;
	
	// SetChunkSize
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
	
		pkt->chunk_size = SRS_CONF_DEFAULT_CHUNK_SIZE;
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send set chunk size failed. "
				"stream=%s, chunk_size=%d, ret=%d", 
				stream.c_str(), SRS_CONF_DEFAULT_CHUNK_SIZE, ret);
			return ret;
		}
	}

	// publish(stream)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsPublishPacket* pkt = new SrsPublishPacket();
	
		pkt->stream_name = stream;
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send publish message failed. "
				"stream=%s, stream_id=%d, ret=%d", 
				stream.c_str(), stream_id, ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsRtmpClient::fmle_publish(string stream, int& stream_id)
{
	stream_id = 0;
	
	int ret = ERROR_SUCCESS;
	
	// SrsFMLEStartPacket
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsFMLEStartPacket* pkt = SrsFMLEStartPacket::create_release_stream(stream);
	
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send FMLE publish "
				"release stream failed. stream=%s, ret=%d", stream.c_str(), ret);
			return ret;
		}
	}
	
	// FCPublish
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsFMLEStartPacket* pkt = SrsFMLEStartPacket::create_FC_publish(stream);
	
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send FMLE publish "
				"FCPublish failed. stream=%s, ret=%d", stream.c_str(), ret);
			return ret;
		}
	}
	
	// CreateStream
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsCreateStreamPacket* pkt = new SrsCreateStreamPacket();
	
		pkt->transaction_id = 4;
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send FMLE publish "
				"createStream failed. stream=%s, ret=%d", stream.c_str(), ret);
			return ret;
		}
	}
	
	// expect result of CreateStream
	if (true) {
		SrsCommonMessage* msg = NULL;
		SrsCreateStreamResPacket* pkt = NULL;
		if ((ret = srs_rtmp_expect_message<SrsCreateStreamResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
			srs_error("expect create stream response message failed. ret=%d", ret);
			return ret;
		}
		SrsAutoFree(SrsCommonMessage, msg, false);
		srs_info("get create stream response message");

		stream_id = (int)pkt->stream_id;
	}

	// publish(stream)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsPublishPacket* pkt = new SrsPublishPacket();
	
		pkt->stream_name = stream;
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send FMLE publish publish failed. "
				"stream=%s, stream_id=%d, ret=%d", stream.c_str(), stream_id, ret);
			return ret;
		}
	}
	
	return ret;
}

SrsRtmpServer::SrsRtmpServer(ISrsProtocolReaderWriter* skt)
{
	io = skt;
	protocol = new SrsProtocol(skt);
}

SrsRtmpServer::~SrsRtmpServer()
{
	srs_freep(protocol);
}

SrsProtocol* SrsRtmpServer::get_protocol()
{
	return protocol;
}

void SrsRtmpServer::set_recv_timeout(int64_t timeout_us)
{
	protocol->set_recv_timeout(timeout_us);
}

int64_t SrsRtmpServer::get_recv_timeout()
{
	return protocol->get_recv_timeout();
}

void SrsRtmpServer::set_send_timeout(int64_t timeout_us)
{
	protocol->set_send_timeout(timeout_us);
}

int64_t SrsRtmpServer::get_send_timeout()
{
	return protocol->get_send_timeout();
}

int64_t SrsRtmpServer::get_recv_bytes()
{
	return protocol->get_recv_bytes();
}

int64_t SrsRtmpServer::get_send_bytes()
{
	return protocol->get_send_bytes();
}

int SrsRtmpServer::get_recv_kbps()
{
	return protocol->get_recv_kbps();
}

int SrsRtmpServer::get_send_kbps()
{
	return protocol->get_send_kbps();
}

int SrsRtmpServer::recv_message(SrsCommonMessage** pmsg)
{
	return protocol->recv_message(pmsg);
}

int SrsRtmpServer::send_message(ISrsMessage* msg)
{
	return protocol->send_message(msg);
}

int SrsRtmpServer::handshake()
{
	int ret = ERROR_SUCCESS;
    
    SrsComplexHandshake complex_hs;
    SrsSimpleHandshake simple_hs;
    if ((ret = simple_hs.handshake_with_client(io, &complex_hs)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsRtmpServer::connect_app(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = NULL;
	SrsConnectAppPacket* pkt = NULL;
	if ((ret = srs_rtmp_expect_message<SrsConnectAppPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
		srs_error("expect connect app message failed. ret=%d", ret);
		return ret;
	}
	SrsAutoFree(SrsCommonMessage, msg, false);
	srs_info("get connect app message");
	
	SrsAmf0Any* prop = NULL;
	
	if ((prop = pkt->command_object->ensure_property_string("tcUrl")) == NULL) {
		ret = ERROR_RTMP_REQ_CONNECT;
		srs_error("invalid request, must specifies the tcUrl. ret=%d", ret);
		return ret;
	}
	req->tcUrl = prop->to_str();
	
	if ((prop = pkt->command_object->ensure_property_string("pageUrl")) != NULL) {
		req->pageUrl = prop->to_str();
	}
	
	if ((prop = pkt->command_object->ensure_property_string("swfUrl")) != NULL) {
		req->swfUrl = prop->to_str();
	}
	
	if ((prop = pkt->command_object->ensure_property_number("objectEncoding")) != NULL) {
		req->objectEncoding = srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	
	srs_info("get connect app message params success.");
	
	return req->discovery_app();
}

int SrsRtmpServer::set_window_ack_size(int ack_size)
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = new SrsCommonMessage();
	SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
	
	pkt->ackowledgement_window_size = ack_size;
	msg->set_packet(pkt, 0);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send ack size message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send ack size message success. ack_size=%d", ack_size);
	
	return ret;
}

int SrsRtmpServer::set_peer_bandwidth(int bandwidth, int type)
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = new SrsCommonMessage();
	SrsSetPeerBandwidthPacket* pkt = new SrsSetPeerBandwidthPacket();
	
	pkt->bandwidth = bandwidth;
	pkt->type = type;
	msg->set_packet(pkt, 0);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send set bandwidth message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send set bandwidth message "
		"success. bandwidth=%d, type=%d", bandwidth, type);
	
	return ret;
}

int SrsRtmpServer::response_connect_app(SrsRequest *req, const char* server_ip)
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = new SrsCommonMessage();
	SrsConnectAppResPacket* pkt = new SrsConnectAppResPacket();
	
	pkt->props->set("fmsVer", SrsAmf0Any::str("FMS/"RTMP_SIG_FMS_VER));
	pkt->props->set("capabilities", new SrsAmf0Number(127));
	pkt->props->set("mode", new SrsAmf0Number(1));
	
	pkt->info->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
	pkt->info->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectSuccess));
	pkt->info->set(StatusDescription, SrsAmf0Any::str("Connection succeeded"));
	pkt->info->set("objectEncoding", new SrsAmf0Number(req->objectEncoding));
	SrsAmf0EcmaArray* data = new SrsAmf0EcmaArray();
	pkt->info->set("data", data);
	
	data->set("version", SrsAmf0Any::str(RTMP_SIG_FMS_VER));
	data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
	data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
	data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
	data->set("srs_role", SrsAmf0Any::str(RTMP_SIG_SRS_ROLE));
	data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
	data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
	data->set("srs_site", SrsAmf0Any::str(RTMP_SIG_SRS_WEB));
	data->set("srs_email", SrsAmf0Any::str(RTMP_SIG_SRS_EMAIL));
	data->set("srs_copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
	data->set("srs_primary_authors", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY_AUTHROS));
	
    if (server_ip) {
        data->set("srs_server_ip", SrsAmf0Any::str(server_ip));
    }
	
	msg->set_packet(pkt, 0);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send connect app response message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send connect app response message success.");
	
    return ret;
}

void SrsRtmpServer::response_connect_reject(SrsRequest *req, const char* desc)
{
    int ret = ERROR_SUCCESS;

    SrsConnectAppResPacket* pkt = new SrsConnectAppResPacket();
    pkt->command_name = "_error";
    pkt->props->set(StatusLevel, SrsAmf0Any::str(StatusLevelError));
    pkt->props->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectRejected));
    pkt->props->set(StatusDescription, SrsAmf0Any::str(desc));
    //pkt->props->set("objectEncoding", new SrsAmf0Number(req->objectEncoding));

    SrsCommonMessage* msg = (new SrsCommonMessage())->set_packet(pkt, 0);
    if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
        srs_error("send connect app response rejected message failed. ret=%d", ret);
        return;
    }
    srs_info("send connect app response rejected message success.");

    return;
}

int SrsRtmpServer::on_bw_done()
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = new SrsCommonMessage();
	SrsOnBWDonePacket* pkt = new SrsOnBWDonePacket();
	
	msg->set_packet(pkt, 0);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send onBWDone message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send onBWDone message success.");
	
	return ret;
}

int SrsRtmpServer::identify_client(int stream_id, SrsClientType& type, string& stream_name)
{
	type = SrsClientUnknown;
	int ret = ERROR_SUCCESS;
	
	while (true) {
		SrsCommonMessage* msg = NULL;
		if ((ret = protocol->recv_message(&msg)) != ERROR_SUCCESS) {
			srs_error("recv identify client message failed. ret=%d", ret);
			return ret;
		}

		SrsAutoFree(SrsCommonMessage, msg, false);

		if (!msg->header.is_amf0_command() && !msg->header.is_amf3_command()) {
			srs_trace("identify ignore messages except "
				"AMF0/AMF3 command message. type=%#x", msg->header.message_type);
			continue;
		}
		
		if ((ret = msg->decode_packet(protocol)) != ERROR_SUCCESS) {
			srs_error("identify decode message failed. ret=%d", ret);
			return ret;
		}
		
		SrsPacket* pkt = msg->get_packet();
		if (dynamic_cast<SrsCreateStreamPacket*>(pkt)) {
			srs_info("identify client by create stream, play or flash publish.");
			return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket*>(pkt), stream_id, type, stream_name);
		}
		if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
			srs_info("identify client by releaseStream, fmle publish.");
			return identify_fmle_publish_client(dynamic_cast<SrsFMLEStartPacket*>(pkt), type, stream_name);
		}
		if (dynamic_cast<SrsPlayPacket*>(pkt)) {
			srs_info("level0 identify client by play.");
			return identify_play_client(dynamic_cast<SrsPlayPacket*>(pkt), type, stream_name);
		}
		
		srs_trace("ignore AMF0/AMF3 command message.");
	}
	
	return ret;
}

int SrsRtmpServer::set_chunk_size(int chunk_size)
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = new SrsCommonMessage();
	SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
	
	pkt->chunk_size = chunk_size;
	msg->set_packet(pkt, 0);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send set chunk size message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send set chunk size message success. chunk_size=%d", chunk_size);
	
	return ret;
}

int SrsRtmpServer::start_play(int stream_id)
{
	int ret = ERROR_SUCCESS;
	
	// StreamBegin
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsUserControlPacket* pkt = new SrsUserControlPacket();
		
		pkt->event_type = SrcPCUCStreamBegin;
		pkt->event_data = stream_id;
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send PCUC(StreamBegin) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send PCUC(StreamBegin) message success.");
	}
	
	// onStatus(NetStream.Play.Reset)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamReset));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Playing and resetting stream."));
		pkt->data->set(StatusDetails, SrsAmf0Any::str("stream"));
		pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Play.Reset) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Play.Reset) message success.");
	}
	
	// onStatus(NetStream.Play.Start)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamStart));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Started playing stream."));
		pkt->data->set(StatusDetails, SrsAmf0Any::str("stream"));
		pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Play.Reset) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Play.Reset) message success.");
	}
	
	// |RtmpSampleAccess(false, false)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsSampleAccessPacket* pkt = new SrsSampleAccessPacket();
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send |RtmpSampleAccess(false, false) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send |RtmpSampleAccess(false, false) message success.");
	}
	
	// onStatus(NetStream.Data.Start)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusDataPacket* pkt = new SrsOnStatusDataPacket();
		
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeDataStart));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Data.Start) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Data.Start) message success.");
	}
	
	srs_info("start play success.");
	
	return ret;
}

int SrsRtmpServer::on_play_client_pause(int stream_id, bool is_pause)
{
	int ret = ERROR_SUCCESS;
	
	if (is_pause) {
		// onStatus(NetStream.Pause.Notify)
		if (true) {
			SrsCommonMessage* msg = new SrsCommonMessage();
			SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
			
			pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
			pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamPause));
			pkt->data->set(StatusDescription, SrsAmf0Any::str("Paused stream."));
			
			msg->set_packet(pkt, stream_id);
			
			if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
				srs_error("send onStatus(NetStream.Pause.Notify) message failed. ret=%d", ret);
				return ret;
			}
			srs_info("send onStatus(NetStream.Pause.Notify) message success.");
		}
		// StreamEOF
		if (true) {
			SrsCommonMessage* msg = new SrsCommonMessage();
			SrsUserControlPacket* pkt = new SrsUserControlPacket();
			
			pkt->event_type = SrcPCUCStreamEOF;
			pkt->event_data = stream_id;
			msg->set_packet(pkt, 0);
			
			if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
				srs_error("send PCUC(StreamEOF) message failed. ret=%d", ret);
				return ret;
			}
			srs_info("send PCUC(StreamEOF) message success.");
		}
	} else {
		// onStatus(NetStream.Unpause.Notify)
		if (true) {
			SrsCommonMessage* msg = new SrsCommonMessage();
			SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
			
			pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
			pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamUnpause));
			pkt->data->set(StatusDescription, SrsAmf0Any::str("Unpaused stream."));
			
			msg->set_packet(pkt, stream_id);
			
			if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
				srs_error("send onStatus(NetStream.Unpause.Notify) message failed. ret=%d", ret);
				return ret;
			}
			srs_info("send onStatus(NetStream.Unpause.Notify) message success.");
		}
		// StreanBegin
		if (true) {
			SrsCommonMessage* msg = new SrsCommonMessage();
			SrsUserControlPacket* pkt = new SrsUserControlPacket();
			
			pkt->event_type = SrcPCUCStreamBegin;
			pkt->event_data = stream_id;
			msg->set_packet(pkt, 0);
			
			if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
				srs_error("send PCUC(StreanBegin) message failed. ret=%d", ret);
				return ret;
			}
			srs_info("send PCUC(StreanBegin) message success.");
		}
	}
	
	return ret;
}

int SrsRtmpServer::start_fmle_publish(int stream_id)
{
	int ret = ERROR_SUCCESS;
	
	// FCPublish
	double fc_publish_tid = 0;
	if (true) {
		SrsCommonMessage* msg = NULL;
		SrsFMLEStartPacket* pkt = NULL;
		if ((ret = srs_rtmp_expect_message<SrsFMLEStartPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
			srs_error("recv FCPublish message failed. ret=%d", ret);
			return ret;
		}
		srs_info("recv FCPublish request message success.");
		
		SrsAutoFree(SrsCommonMessage, msg, false);
		fc_publish_tid = pkt->transaction_id;
	}
	// FCPublish response
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(fc_publish_tid);
		
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send FCPublish response message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send FCPublish response message success.");
	}
	
	// createStream
	double create_stream_tid = 0;
	if (true) {
		SrsCommonMessage* msg = NULL;
		SrsCreateStreamPacket* pkt = NULL;
		if ((ret = srs_rtmp_expect_message<SrsCreateStreamPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
			srs_error("recv createStream message failed. ret=%d", ret);
			return ret;
		}
		srs_info("recv createStream request message success.");
		
		SrsAutoFree(SrsCommonMessage, msg, false);
		create_stream_tid = pkt->transaction_id;
	}
	// createStream response
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsCreateStreamResPacket* pkt = new SrsCreateStreamResPacket(create_stream_tid, stream_id);
		
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send createStream response message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send createStream response message success.");
	}
	
	// publish
	if (true) {
		SrsCommonMessage* msg = NULL;
		SrsPublishPacket* pkt = NULL;
		if ((ret = srs_rtmp_expect_message<SrsPublishPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
			srs_error("recv publish message failed. ret=%d", ret);
			return ret;
		}
		srs_info("recv publish request message success.");
		
		SrsAutoFree(SrsCommonMessage, msg, false);
	}
	// publish response onFCPublish(NetStream.Publish.Start)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onFCPublish(NetStream.Publish.Start) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onFCPublish(NetStream.Publish.Start) message success.");
	}
	// publish response onStatus(NetStream.Publish.Start)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
		pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Publish.Start) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Publish.Start) message success.");
	}
	
	srs_info("FMLE publish success.");
	
	return ret;
}

int SrsRtmpServer::fmle_unpublish(int stream_id, double unpublish_tid)
{
	int ret = ERROR_SUCCESS;
	
	// publish response onFCUnpublish(NetStream.unpublish.Success)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Stop publishing stream."));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onFCUnpublish(NetStream.unpublish.Success) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onFCUnpublish(NetStream.unpublish.Success) message success.");
	}
	// FCUnpublish response
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(unpublish_tid);
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send FCUnpublish response message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send FCUnpublish response message success.");
	}
	// publish response onStatus(NetStream.Unpublish.Success)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Stream is now unpublished"));
		pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Unpublish.Success) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Unpublish.Success) message success.");
	}
	
	srs_info("FMLE unpublish success.");
	
	return ret;
}

int SrsRtmpServer::start_flash_publish(int stream_id)
{
	int ret = ERROR_SUCCESS;
	
	// publish response onStatus(NetStream.Publish.Start)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
		pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
		pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
		pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Publish.Start) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Publish.Start) message success.");
	}
	
	srs_info("flash publish success.");
	
	return ret;
}

int SrsRtmpServer::identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsClientType& type, string& stream_name)
{
	int ret = ERROR_SUCCESS;
	
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsCreateStreamResPacket* pkt = new SrsCreateStreamResPacket(req->transaction_id, stream_id);
		
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send createStream response message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send createStream response message success.");
	}
	
	while (true) {
		SrsCommonMessage* msg = NULL;
		if ((ret = protocol->recv_message(&msg)) != ERROR_SUCCESS) {
			srs_error("recv identify client message failed. ret=%d", ret);
			return ret;
		}

		SrsAutoFree(SrsCommonMessage, msg, false);

		if (!msg->header.is_amf0_command() && !msg->header.is_amf3_command()) {
			srs_trace("identify ignore messages except "
				"AMF0/AMF3 command message. type=%#x", msg->header.message_type);
			continue;
		}
		
		if ((ret = msg->decode_packet(protocol)) != ERROR_SUCCESS) {
			srs_error("identify decode message failed. ret=%d", ret);
			return ret;
		}
		
		SrsPacket* pkt = msg->get_packet();
		if (dynamic_cast<SrsPlayPacket*>(pkt)) {
			srs_info("level1 identify client by play.");
			return identify_play_client(dynamic_cast<SrsPlayPacket*>(pkt), type, stream_name);
		}
		if (dynamic_cast<SrsPublishPacket*>(pkt)) {
			srs_info("identify client by publish, falsh publish.");
			return identify_flash_publish_client(dynamic_cast<SrsPublishPacket*>(pkt), type, stream_name);
		}
		
		srs_trace("ignore AMF0/AMF3 command message.");
	}
	
	return ret;
}

int SrsRtmpServer::identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsClientType& type, string& stream_name)
{
	int ret = ERROR_SUCCESS;
	
	type = SrsClientFMLEPublish;
	stream_name = req->stream_name;
	
	// releaseStream response
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(req->transaction_id);
		
		msg->set_packet(pkt, 0);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send releaseStream response message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send releaseStream response message success.");
	}
	
	return ret;
}

int SrsRtmpServer::identify_flash_publish_client(SrsPublishPacket* req, SrsClientType& type, string& stream_name)
{
	int ret = ERROR_SUCCESS;
	
	type = SrsClientFlashPublish;
	stream_name = req->stream_name;
	
	return ret;
}

int SrsRtmpServer::identify_play_client(SrsPlayPacket* req, SrsClientType& type, string& stream_name)
{
	int ret = ERROR_SUCCESS;
	
	type = SrsClientPlay;
	stream_name = req->stream_name;
	
	srs_trace("identity client type=play, stream_name=%s", stream_name.c_str());

	return ret;
}

