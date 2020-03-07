/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_rtc_conn.hpp>

using namespace std;

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>

#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_stun_stack.hpp>
#include <srs_app_dtls.hpp>
#include <srs_app_config.hpp>
#include <srs_service_utility.hpp>

static bool is_stun(const char* data, const int size) 
{
    return data != NULL && size > 0 && (data[0] == 0 || data[0] == 1); 
}

static bool is_rtp_or_rtcp(const char* data, const int size) 
{
    return data != NULL && size > 0 && (data[0] >= 128 && data[0] <= 191);
}

static bool is_dtls(const char* data, const int size) 
{
    return data != NULL && size > 0 && (data[0] >= 20 && data[0] <= 64);
}

static string gen_random_str(int len)
{
    static string random_table = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

    string ret;
    ret.reserve(len);
    for (int i = 0; i < len; ++i) {
        ret.append(1, random_table[random() % random_table.size()]);
    }

    return ret;
}

const int SRTP_MASTER_KEY_KEY_LEN = 16;
const int SRTP_MASTER_KEY_SALT_LEN = 14;

SrsCandidate::SrsCandidate()
{
}

SrsCandidate::~SrsCandidate()
{
}

std::vector<std::string> SrsCandidate::get_candidate_ips()
{
    std::vector<std::string> candidate_ips;

    string candidate = _srs_config->get_rtc_candidates();
    if (candidate == "*" || candidate == "0.0.0.0") {
        std::vector<std::string> tmp = srs_get_local_ips();
        for (int i = 0; i < tmp.size(); ++i) {
            if (tmp[i] != "127.0.0.1") {
                candidate_ips.push_back(tmp[i]);
            }
        }
    } else {
        candidate_ips.push_back(candidate);
    }

    return candidate_ips;
}

SrsSdpMediaInfo::SrsSdpMediaInfo()
{
}

SrsSdpMediaInfo::~SrsSdpMediaInfo()
{
}

SrsSdp::SrsSdp()
{
}

SrsSdp::~SrsSdp()
{
}

srs_error_t SrsSdp::decode(const string& sdp_str)
{
    srs_error_t err = srs_success;

    if (sdp_str.size() < 2 || sdp_str[0] != 'v' || sdp_str[1] != '=') {
        return srs_error_wrap(err, "invalid sdp_str");
    }

    string line;
    istringstream is(sdp_str);
    while (getline(is, line)) {
        srs_trace("line=%s", line.c_str());

        if (line.size() < 2 || line[1] != '=') {
            return srs_error_wrap(err, "invalid sdp line=%s", line.c_str());
        }

        switch (line[0]) {
            case 'v' :{
                break;
            }
            case 'o' :{
                break;
            }
            case 's' :{
                break;
            }
            case 't' :{
                break;
            }
            case 'c' :{
                break;
            }
            case 'a' :{
                if (parse_attr(line) != srs_success) {
                    return srs_error_wrap(err, "decode sdp line=%s failed", line.c_str());
                }
                break;
            }
            case 'm' :{
                break;
            }
        }
    }

    return err;
}

srs_error_t SrsSdp::encode(string& sdp_str)
{
    srs_error_t err = srs_success;

    string candidate_lines = "";

    std::vector<string> candidate_ips = SrsCandidate::get_candidate_ips();
    for (int i = 0; i < candidate_ips.size(); ++i) {
        ostringstream os;
        os << "a=candidate:10 1 udp 2115783679 " << candidate_ips[i] << " " << _srs_config->get_rtc_listen() <<" typ host generation 0\\r\\n";
        candidate_lines += os.str();
    }

    // FIXME:
    sdp_str = 
		"v=0\\r\\n"
		"o=- 0 0 IN IP4 127.0.0.1\\r\\n"
		"s=-\\r\\n"
		"t=0 0\\r\\n"
		"a=ice-lite\\r\\n"
		"a=group:BUNDLE 0 1\\r\\n"
		"a=msid-semantic: WMS 6VrfBKXrwK\\r\\n"
		"m=audio 9 UDP/TLS/RTP/SAVPF 111\\r\\n"
		"c=IN IP4 0.0.0.0\\r\\n"
        + candidate_lines +
		"a=rtcp:9 IN IP4 0.0.0.0\\r\\n"
		"a=ice-ufrag:" + ice_ufrag + "\\r\\n"
		"a=ice-pwd:" + ice_pwd + "\\r\\n"
		"a=ice-options:trickle\\r\\n"
		"a=fingerprint:sha-256 " + SrsDtls::instance()->get_fingerprint() + "\\r\\n"
		"a=sendrecv\\r\\n"
		"a=mid:0\\r\\n"
		"a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\\r\\n"
		"a=extmap:3 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\\r\\n"
		"a=rtcp-mux\\r\\n"
		"a=rtpmap:111 opus/48000/2\\r\\n"
		"a=fmtp:111 minptime=10;useinbandfec=1\\r\\n"
		"a=maxptime:60\\r\\n"
		"a=ssrc:3233846890 cname:o/i14u9pJrxRKAsu\\r\\n"
		"a=ssrc:3233846890 msid:6VrfBKXrwK a0\\r\\n"
		"a=ssrc:3233846890 mslabel:6VrfBKXrwK\\r\\n"
		"a=ssrc:3233846890 label:6VrfBKXrwKa0\\r\\n"
		"m=video 9 UDP/TLS/RTP/SAVPF 96 98 102\\r\\n"
		"c=IN IP4 0.0.0.0\\r\\n"
        + candidate_lines +
		"a=rtcp:9 IN IP4 0.0.0.0\\r\\n"
		"b=as:2000000\\r\\n"
		"a=ice-ufrag:" + ice_ufrag + "\\r\\n"
		"a=ice-pwd:" + ice_pwd + "\\r\\n"
		"a=ice-options:trickle\\r\\n"
		"a=extmap:2 urn:ietf:params:rtp-hdrext:toffset\\r\\n"
		"a=extmap:3 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\\r\\n"
		"a=extmap:4 urn:3gpp:video-orientation\\r\\n"
		"a=fingerprint:sha-256 " + SrsDtls::instance()->get_fingerprint() + "\\r\\n"
		"a=sendrecv\\r\\n"
		"a=mid:1\\r\\n"
		"a=rtcp-mux\\r\\n"
		"a=rtpmap:96 VP8/90000\\r\\n"
		"a=rtcp-fb:96 ccm fir\\r\\n"
		"a=rtcp-fb:96 nack\\r\\n"
		"a=rtcp-fb:96 nack pli\\r\\n"
		"a=rtcp-fb:96 goog-remb\\r\\n"
		"a=rtcp-fb:96 transport-cc\\r\\n"
		"a=rtpmap:98 VP9/90000\\r\\n"
		"a=rtcp-fb:98 ccm fir\\r\\n"
		"a=rtcp-fb:98 nack\\r\\n"
		"a=rtcp-fb:98 nack pli\\r\\n"
		"a=rtcp-fb:98 goog-remb\\r\\n"
		"a=rtcp-fb:98 transport-cc\\r\\n"
		"a=rtpmap:102 H264/90000\\r\\n"
		"a=rtcp-fb:102 goog-remb\\r\\n"
		"a=rtcp-fb:102 transport-cc\\r\\n"
		"a=rtcp-fb:102 ccm fir \\r\\n"
		"a=rtcp-fb:102 nack\\r\\n"
		"a=rtcp-fb:102 nack pli \\r\\n"
		"a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f\\r\\n"
		"a=ssrc:3233846889 cname:o/i14u9pJrxRKAsu\\r\\n"
		"a=ssrc:3233846889 msid:6VrfBKXrwK v0\\r\\n"
		"a=ssrc:3233846889 mslabel:6VrfBKXrwK\\r\\n"
		"a=ssrc:3233846889 label:6VrfBKXrwKv0\\r\\n";

    return err;
}

srs_error_t SrsSdp::parse_attr(const string& line)
{
    srs_error_t err = srs_success;

    string key = "";
    string val = "";
    string* p = &key;
    for (int i = 2; i < line.size(); ++i) {
        if (line[i] == ':' && p == &key) {
            p = &val;
        } else {
            if (line[i] != '\r' && line[i] != '\n') {
                p->append(1, line[i]);
            }
        }
    }

    srs_trace("sdp attribute key=%s, val=%s", key.c_str(), val.c_str());

    if (key == "ice-ufrag") {
        ice_ufrag = val;
    } else if (key == "ice-pwd") {
        ice_pwd = val;
    } else if (key == "fingerprint") {
        
    } else {
    }

    return err;
}

SrsDtlsSession::SrsDtlsSession()
{
    dtls = NULL;
    bio_in = NULL;
    bio_out = NULL;

    client_key = "";
    server_key = "";

    srtp_send = NULL;
    srtp_recv = NULL;

    handshake_done = false;
}

SrsDtlsSession::~SrsDtlsSession()
{
}

srs_error_t SrsDtlsSession::handshake(SrsUdpRemuxSocket* udp_remux_socket)
{
    srs_error_t err = srs_success;

	int ret = SSL_do_handshake(dtls);

    unsigned char *out_bio_data;
    int out_bio_len = BIO_get_mem_data(bio_out, &out_bio_data);

    int ssl_err = SSL_get_error(dtls, ret); 
    switch(ssl_err) {   
        case SSL_ERROR_NONE: {   
            err = on_dtls_handshake_done();
        }  
        break;

        case SSL_ERROR_WANT_READ: {   
            break;
        }   

        case SSL_ERROR_WANT_WRITE: {   
            break;
        }

        default: {   
            break;
        }   
    }   

    if (out_bio_len) {   
        srs_trace("send dtls handshake data");
        udp_remux_socket->sendto(out_bio_data, out_bio_len, 0);
    }

    return err;
}

srs_error_t SrsDtlsSession::on_dtls(SrsUdpRemuxSocket* udp_remux_socket)
{
    srs_error_t err = srs_success;
    if (! handshake_done) {
		BIO_reset(bio_in);
        BIO_reset(bio_out);
        BIO_write(bio_in, udp_remux_socket->data(), udp_remux_socket->size());

        handshake(udp_remux_socket);
    } else {
		BIO_reset(bio_in);
        BIO_reset(bio_out);
        BIO_write(bio_in, udp_remux_socket->data(), udp_remux_socket->size());

        while (BIO_ctrl_pending(bio_in) > 0) {
            char dtls_read_buf[8092];
            int nb = SSL_read(dtls, dtls_read_buf, sizeof(dtls_read_buf));

            if (nb > 0) {
                on_dtls_application_data(dtls_read_buf, nb);
            }
        }
    }

	return err;
}

srs_error_t SrsDtlsSession::on_dtls_handshake_done()
{
    srs_trace("dtls handshake done");

    handshake_done = true;
    return srtp_init();
}

srs_error_t SrsDtlsSession::on_dtls_application_data(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    return err;
}

void SrsDtlsSession::send_client_hello(SrsUdpRemuxSocket* udp_remux_socket)
{
    if (dtls == NULL) {    
        srs_trace("send client hello");

        dtls = SSL_new(SrsDtls::instance()->get_dtls_ctx());
        SSL_set_connect_state(dtls);

        bio_in  = BIO_new(BIO_s_mem());
        bio_out = BIO_new(BIO_s_mem());

        SSL_set_bio(dtls, bio_in, bio_out);

        handshake(udp_remux_socket);
    } 
}

srs_error_t SrsDtlsSession::srtp_init() 
{
    srs_error_t err = srs_success;

	unsigned char material[SRTP_MASTER_KEY_LEN * 2] = {0};  // client(SRTP_MASTER_KEY_KEY_LEN + SRTP_MASTER_KEY_SALT_LEN) + server
	static string dtls_srtp_lable = "EXTRACTOR-dtls_srtp";
	if (! SSL_export_keying_material(dtls, material, sizeof(material), dtls_srtp_lable.c_str(), dtls_srtp_lable.size(), NULL, 0, 0)) {   
        return srs_error_wrap(err, "SSL_export_keying_material failed");
	}   

	size_t offset = 0;

	std::string sClientMasterKey(reinterpret_cast<char*>(material), SRTP_MASTER_KEY_KEY_LEN);
	offset += SRTP_MASTER_KEY_KEY_LEN;
	std::string sServerMasterKey(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_KEY_LEN);
	offset += SRTP_MASTER_KEY_KEY_LEN;
	std::string sClientMasterSalt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);
	offset += SRTP_MASTER_KEY_SALT_LEN;
	std::string sServerMasterSalt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);

	client_key = sClientMasterKey + sClientMasterSalt;
	server_key = sServerMasterKey + sServerMasterSalt;

    if (srtp_sender_side_init() != srs_success) {
        return srs_error_wrap(err, "srtp sender size init failed");
    }

    if (srtp_receiver_side_init() != srs_success) { 
        return srs_error_wrap(err, "srtp receiver size init failed");
    }

    return err;
}

srs_error_t SrsDtlsSession::srtp_sender_side_init()
{
    srs_error_t err = srs_success;

    srtp_policy_t policy;
    bzero(&policy, sizeof(policy));

    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

    policy.ssrc.type = ssrc_any_outbound;
    
    policy.ssrc.value = 0;
    // TODO: adjust window_size
    policy.window_size = 8192;
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    uint8_t *key = new uint8_t[client_key.size()];
    memcpy(key, client_key.data(), client_key.size());
    policy.key = key;

    if (srtp_create(&srtp_send, &policy) != 0) {
        delete [] key;
        return srs_error_wrap(err, "srtp_create failed");
    }

    delete [] key;

    return err;
}

srs_error_t SrsDtlsSession::srtp_receiver_side_init()
{
    srs_error_t err = srs_success;

    srtp_policy_t policy;
    bzero(&policy, sizeof(policy));

    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

    policy.ssrc.type = ssrc_any_inbound;

    policy.ssrc.value = 0;
    // TODO: adjust window_size
    policy.window_size = 8192;
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    uint8_t *key = new uint8_t[server_key.size()];
    memcpy(key, server_key.data(), server_key.size());
    policy.key = key;

    if (srtp_create(&srtp_recv, &policy) != 0) {
        delete [] key;
        return srs_error_wrap(err, "srtp_create failed");
    }

    delete [] key;

    return err;
}

SrsRtcSession::SrsRtcSession(SrsRtcServer* svr)
{
    rtc_server = svr;
    session_state = INIT;
    dtls_session = NULL;
}

SrsRtcSession::~SrsRtcSession()
{
}

srs_error_t SrsRtcSession::on_stun(SrsUdpRemuxSocket* udp_remux_socket, SrsStunPacket* stun_req)
{
    srs_error_t err = srs_success;

    if (stun_req->is_binding_request()) {
        if (on_binding_request(udp_remux_socket, stun_req) != srs_success) {
            return srs_error_wrap(err, "stun binding request failed");
        }
    }

    return err;
}

srs_error_t SrsRtcSession::on_binding_request(SrsUdpRemuxSocket* udp_remux_socket, SrsStunPacket* stun_req)
{
    srs_error_t err = srs_success;

    SrsStunPacket stun_binding_response;
    char buf[1460];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(stun_req->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(stun_req->get_local_ufrag());
    stun_binding_response.set_transcation_id(stun_req->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(udp_remux_socket->get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(udp_remux_socket->get_peer_port());

    if (stun_binding_response.encode(get_local_sdp()->get_ice_pwd(), stream) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if (udp_remux_socket->sendto(stream->data(), stream->pos(), 0) <= 0) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (get_session_state() == WAITING_STUN) {
        set_session_state(DOING_DTLS_HANDSHAKE);
        send_client_hello(udp_remux_socket);

        string peer_id = udp_remux_socket->get_peer_id();
        rtc_server->insert_into_id_sessions(peer_id, this);
    }

    // TODO: dtls send client retry

    return err;
}

srs_error_t SrsRtcSession::send_client_hello(SrsUdpRemuxSocket* udp_remux_socket)
{
    if (dtls_session == NULL) {
        dtls_session = new SrsDtlsSession();
    }

    dtls_session->send_client_hello(udp_remux_socket);
}

srs_error_t SrsRtcSession::on_dtls(SrsUdpRemuxSocket* udp_remux_socket)
{
    return dtls_session->on_dtls(udp_remux_socket);
}

SrsRtcServer::SrsRtcServer()
{
}

SrsRtcServer::~SrsRtcServer()
{
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t SrsRtcServer::on_udp_packet(SrsUdpRemuxSocket* udp_remux_socket)
{
    srs_error_t err = srs_success;

    if (is_stun(udp_remux_socket->data(), udp_remux_socket->size())) {
        return on_stun(udp_remux_socket);
    } else if (is_dtls(udp_remux_socket->data(), udp_remux_socket->size())) {
        return on_dtls(udp_remux_socket);
    } else if (is_rtp_or_rtcp(udp_remux_socket->data(), udp_remux_socket->size())) {
        return on_rtp_or_rtcp(udp_remux_socket);
    } 

    return srs_error_wrap(err, "unknown udp packet type");
}

SrsRtcSession* SrsRtcServer::create_rtc_session(const SrsSdp& remote_sdp, SrsSdp& local_sdp)
{
    SrsRtcSession* session = new SrsRtcSession(this);

    std::string local_pwd = gen_random_str(32);
    std::string local_ufrag = "";
    while (true) {
        local_ufrag = gen_random_str(8);
        std::string username = local_ufrag + ":" + remote_sdp.get_ice_ufrag();

        bool ret = map_username_session.insert(make_pair(username, session)).second;
        if (ret) {
            break;
        }
    }

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);

    session->set_remote_sdp(remote_sdp);
    session->set_local_sdp(local_sdp);

    session->set_session_state(WAITING_STUN);

    return session;
}

SrsRtcSession* SrsRtcServer::find_rtc_session_by_peer_id(const string& peer_id)
{
    map<string, SrsRtcSession*>::iterator iter = map_id_session.find(peer_id);
    if (iter == map_id_session.end()) {
        return NULL; 
    }

    return iter->second;
}

srs_error_t SrsRtcServer::on_stun(SrsUdpRemuxSocket* udp_remux_socket)
{
    srs_error_t err = srs_success;

    srs_trace("recv stun packet from %s", udp_remux_socket->get_peer_id().c_str());

    SrsStunPacket stun_req;
    if (stun_req.decode(udp_remux_socket->data(), udp_remux_socket->size()) != srs_success) {
        return srs_error_wrap(err, "decode stun packet failed");
    }

    std::string username = stun_req.get_username();
    SrsRtcSession* rtc_session = find_rtc_session_by_username(username);
    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc_session, stun username=%s", username.c_str());
    }

    return rtc_session->on_stun(udp_remux_socket, &stun_req);
}

srs_error_t SrsRtcServer::on_dtls(SrsUdpRemuxSocket* udp_remux_socket)
{
    srs_error_t err = srs_success;
    srs_trace("on dtls");

    // FIXME
    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(udp_remux_socket->get_peer_id());

    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc session by peer_id=%s", udp_remux_socket->get_peer_id().c_str());
    }

    rtc_session->on_dtls(udp_remux_socket);

    return err;
}

srs_error_t SrsRtcServer::on_rtp_or_rtcp(SrsUdpRemuxSocket* udp_remux_socket)
{
    srs_error_t err = srs_success;
    srs_trace("on rtp/rtcp");
    return err;
}

SrsRtcSession* SrsRtcServer::find_rtc_session_by_username(const std::string& username)
{
    map<string, SrsRtcSession*>::iterator iter = map_username_session.find(username);
    if (iter == map_username_session.end()) {
        return NULL; 
    }

    return iter->second;
}

bool SrsRtcServer::insert_into_id_sessions(const string& peer_id, SrsRtcSession* rtc_session)
{
    return map_id_session.insert(make_pair(peer_id, rtc_session)).second;
}
