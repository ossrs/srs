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
#define RTMP_SIG_AMF0_VER 0
#define RTMP_SIG_CLIENT_ID "ASAICiss"

/**
* onStatus consts.
*/
#define StatusLevel "level"
#define StatusCode "code"
#define StatusDescription "description"
#define StatusDetails "details"
#define StatusClientId "clientid"
// status value
#define StatusLevelStatus "status"
// code value
#define StatusCodeConnectSuccess "NetConnection.Connect.Success"
#define StatusCodeStreamReset "NetStream.Play.Reset"
#define StatusCodeStreamStart "NetStream.Play.Start"
#define StatusCodePublishStart "NetStream.Publish.Start"
#define StatusCodeDataStart "NetStream.Data.Start"
#define StatusCodeUnpublishSuccess "NetStream.Unpublish.Success"

// FMLE
#define RTMP_AMF0_COMMAND_ON_FC_PUBLISH		"onFCPublish"
#define RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH	"onFCUnpublish"

// default stream id for response the createStream request.
#define SRS_DEFAULT_SID 1

SrsRequest::SrsRequest()
{
	objectEncoding = RTMP_SIG_AMF0_VER;
}

SrsRequest::~SrsRequest()
{
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

std::string SrsRequest::get_stream_url()
{
	std::string url = "";
	
	//url += vhost;
	
	url += "/";
	url += app;
	url += "/";
	url += stream;

	return url;
}

SrsResponse::SrsResponse()
{
	stream_id = SRS_DEFAULT_SID;
}

SrsResponse::~SrsResponse()
{
}

SrsRtmp::SrsRtmp(st_netfd_t client_stfd)
{
	protocol = new SrsProtocol(client_stfd);
	stfd = client_stfd;
}

SrsRtmp::~SrsRtmp()
{
	srs_freep(protocol);
}

void SrsRtmp::set_recv_timeout(int timeout_ms)
{
	return protocol->set_recv_timeout(timeout_ms);
}

void SrsRtmp::set_send_timeout(int timeout_ms)
{
	return protocol->set_send_timeout(timeout_ms);
}

int SrsRtmp::recv_message(SrsCommonMessage** pmsg)
{
	return protocol->recv_message(pmsg);
}

int SrsRtmp::send_message(ISrsMessage* msg)
{
	return protocol->send_message(msg);
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
	req->tcUrl = srs_amf0_convert<SrsAmf0String>(prop)->value;
	
	if ((prop = pkt->command_object->ensure_property_string("pageUrl")) != NULL) {
		req->pageUrl = srs_amf0_convert<SrsAmf0String>(prop)->value;
	}
	
	if ((prop = pkt->command_object->ensure_property_string("swfUrl")) != NULL) {
		req->swfUrl = srs_amf0_convert<SrsAmf0String>(prop)->value;
	}
	
	if ((prop = pkt->command_object->ensure_property_number("objectEncoding")) != NULL) {
		req->objectEncoding = srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	
	srs_info("get connect app message params success.");
	
	return req->discovery_app();
}

int SrsRtmp::set_window_ack_size(int ack_size)
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

int SrsRtmp::set_peer_bandwidth(int bandwidth, int type)
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

int SrsRtmp::response_connect_app(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsCommonMessage* msg = new SrsCommonMessage();
	SrsConnectAppResPacket* pkt = new SrsConnectAppResPacket();
	
	pkt->props->set("fmsVer", new SrsAmf0String("FMS/"RTMP_SIG_FMS_VER));
	pkt->props->set("capabilities", new SrsAmf0Number(127));
	pkt->props->set("mode", new SrsAmf0Number(1));
	
	pkt->info->set(StatusLevel, new SrsAmf0String(StatusLevelStatus));
	pkt->info->set(StatusCode, new SrsAmf0String(StatusCodeConnectSuccess));
	pkt->info->set(StatusDescription, new SrsAmf0String("Connection succeeded"));
	pkt->info->set("objectEncoding", new SrsAmf0Number(req->objectEncoding));
	SrsASrsAmf0EcmaArray* data = new SrsASrsAmf0EcmaArray();
	pkt->info->set("data", data);
	
	data->set("version", new SrsAmf0String(RTMP_SIG_FMS_VER));
	data->set("server", new SrsAmf0String(RTMP_SIG_SRS_NAME));
	data->set("srs_url", new SrsAmf0String(RTMP_SIG_SRS_URL));
	data->set("srs_version", new SrsAmf0String(RTMP_SIG_SRS_VERSION));
	
	msg->set_packet(pkt, 0);
	
	if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
		srs_error("send connect app response message failed. ret=%d", ret);
		return ret;
	}
	srs_info("send connect app response message success.");
	
	return ret;
}

int SrsRtmp::on_bw_done()
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

int SrsRtmp::identify_client(int stream_id, SrsClientType& type, std::string& stream_name)
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
		
		if ((ret = msg->decode_packet()) != ERROR_SUCCESS) {
			srs_error("identify decode message failed. ret=%d", ret);
			return ret;
		}
		
		SrsPacket* pkt = msg->get_packet();
		if (dynamic_cast<SrsCreateStreamPacket*>(pkt)) {
			srs_info("identify client by create stream, play or flash publish.");
			return identify_create_stream_client(
				dynamic_cast<SrsCreateStreamPacket*>(pkt), stream_id, type, stream_name);
		}
		if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
			srs_info("identify client by releaseStream, fmle publish.");
			return identify_fmle_publish_client(
				dynamic_cast<SrsFMLEStartPacket*>(pkt), type, stream_name);
		}
		
		srs_trace("ignore AMF0/AMF3 command message.");
	}
	
	return ret;
}

int SrsRtmp::set_chunk_size(int chunk_size)
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

int SrsRtmp::start_play(int stream_id)
{
	int ret = ERROR_SUCCESS;
	
	// StreamBegin
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsPCUC4BytesPacket* pkt = new SrsPCUC4BytesPacket();
		
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
		
		pkt->data->set(StatusLevel, new SrsAmf0String(StatusLevelStatus));
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodeStreamReset));
		pkt->data->set(StatusDescription, new SrsAmf0String("Playing and resetting stream."));
		pkt->data->set(StatusDetails, new SrsAmf0String("stream"));
		pkt->data->set(StatusClientId, new SrsAmf0String(RTMP_SIG_CLIENT_ID));
		
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
		
		pkt->data->set(StatusLevel, new SrsAmf0String(StatusLevelStatus));
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodeStreamStart));
		pkt->data->set(StatusDescription, new SrsAmf0String("Started playing stream."));
		pkt->data->set(StatusDetails, new SrsAmf0String("stream"));
		pkt->data->set(StatusClientId, new SrsAmf0String(RTMP_SIG_CLIENT_ID));
		
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
		
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodeDataStart));
		
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

int SrsRtmp::start_publish(int stream_id)
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
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodePublishStart));
		pkt->data->set(StatusDescription, new SrsAmf0String("Started publishing stream."));
		
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
		
		pkt->data->set(StatusLevel, new SrsAmf0String(StatusLevelStatus));
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodePublishStart));
		pkt->data->set(StatusDescription, new SrsAmf0String("Started publishing stream."));
		pkt->data->set(StatusClientId, new SrsAmf0String(RTMP_SIG_CLIENT_ID));
		
		msg->set_packet(pkt, stream_id);
		
		if ((ret = protocol->send_message(msg)) != ERROR_SUCCESS) {
			srs_error("send onStatus(NetStream.Publish.Start) message failed. ret=%d", ret);
			return ret;
		}
		srs_info("send onStatus(NetStream.Publish.Start) message success.");
	}
	
	return ret;
}

int SrsRtmp::fmle_unpublish(int stream_id, double unpublish_tid)
{
	int ret = ERROR_SUCCESS;
	
	// publish response onFCUnpublish(NetStream.unpublish.Success)
	if (true) {
		SrsCommonMessage* msg = new SrsCommonMessage();
		SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
		
		pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodeUnpublishSuccess));
		pkt->data->set(StatusDescription, new SrsAmf0String("Stop publishing stream."));
		
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
		
		pkt->data->set(StatusLevel, new SrsAmf0String(StatusLevelStatus));
		pkt->data->set(StatusCode, new SrsAmf0String(StatusCodeUnpublishSuccess));
		pkt->data->set(StatusDescription, new SrsAmf0String("Stream is now unpublished"));
		pkt->data->set(StatusClientId, new SrsAmf0String(RTMP_SIG_CLIENT_ID));
		
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

int SrsRtmp::identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsClientType& type, std::string& stream_name)
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
		
		if ((ret = msg->decode_packet()) != ERROR_SUCCESS) {
			srs_error("identify decode message failed. ret=%d", ret);
			return ret;
		}
		
		SrsPacket* pkt = msg->get_packet();
		if (dynamic_cast<SrsPlayPacket*>(pkt)) {
			SrsPlayPacket* play = dynamic_cast<SrsPlayPacket*>(pkt);
			type = SrsClientPlay;
			stream_name = play->stream_name;
			srs_trace("identity client type=play, stream_name=%s", stream_name.c_str());
			return ret;
		}
		
		srs_trace("ignore AMF0/AMF3 command message.");
	}
	
	return ret;
}

int SrsRtmp::identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsClientType& type, std::string& stream_name)
{
	int ret = ERROR_SUCCESS;
	
	type = SrsClientPublish;
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

