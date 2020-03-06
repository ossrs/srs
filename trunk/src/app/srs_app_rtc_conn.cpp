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
		"a=candidate:10 1 udp 2115783679 192.168.170.129 9527 typ host generation 0\\r\\n"
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
		"a=candidate:10 1 udp 2115783679 192.168.170.129 9527 typ host generation 0\\r\\n"
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

SrsDtlsSession::SrsDtlsSession(srs_netfd_t lfd, const sockaddr* f, int fl)
{
    dtls = NULL;
    bio_in = NULL;
    bio_out = NULL;

    client_key = "";
    server_key = "";

    srtp_send = NULL;
    srtp_recv = NULL;

    fd = lfd;
    from = f;
    fromlen = fl;

    handshake_done = false;
}

SrsDtlsSession::~SrsDtlsSession()
{
}

srs_error_t SrsDtlsSession::handshake()
{
    srs_error_t err = srs_success;

	int ret = SSL_do_handshake(dtls);

    unsigned char *out_bio_data;
    int out_bio_len = BIO_get_mem_data(bio_out, &out_bio_data);

    int ssl_err = SSL_get_error(dtls, ret); 
    switch(ssl_err) {   
        case SSL_ERROR_NONE: {   
            srs_trace("dtls handshake done");
            handshake_done = true;
            srtp_init();
            srtp_sender_side_init();
            srtp_receiver_side_init();
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
        srs_sendto(fd, out_bio_data, out_bio_len, from, fromlen, 0);
    }

    return err;
}

srs_error_t SrsDtlsSession::on_dtls(const char* data, const int len)
{
    srs_error_t err = srs_success;
    if (! handshake_done) {
		BIO_reset(bio_in);
        BIO_reset(bio_out);
        BIO_write(bio_in, data, len);

        handshake();
    } else {
		BIO_reset(bio_in);
        BIO_reset(bio_out);
        BIO_write(bio_in, data, len);

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

srs_error_t SrsDtlsSession::on_dtls_application_data(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    return err;
}


void SrsDtlsSession::send_client_hello()
{
    if (dtls == NULL) {    
        srs_trace("send client hello");

        dtls = SSL_new(SrsDtls::instance()->get_dtls_ctx());
        SSL_set_connect_state(dtls);

        bio_in  = BIO_new(BIO_s_mem());
        bio_out = BIO_new(BIO_s_mem());

        SSL_set_bio(dtls, bio_in, bio_out);

        handshake();
    } 
}

srs_error_t SrsDtlsSession::srtp_init() 
{
    srs_error_t err = srs_success;

	unsigned char material[SRTP_MASTER_KEY_LEN * 2] = {0};  // client(SRTP_MASTER_KEY_KEY_LEN + SRTP_MASTER_KEY_SALT_LEN) + server
	char dtls_srtp_lable[] = "EXTRACTOR-dtls_srtp";
	if (! SSL_export_keying_material(dtls, material, sizeof(material), dtls_srtp_lable, strlen(dtls_srtp_lable), NULL, 0, 0)) {   
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

    srtp_sender_side_init();
    srtp_receiver_side_init();
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
    policy.window_size = 8192; // seq 相差8192认为无效
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    uint8_t *key = new uint8_t[client_key.size()];
    memcpy(key, client_key.data(), client_key.size());
    policy.key = key;

    if (srtp_create(&srtp_send, &policy) != 0) {
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
    policy.window_size = 8192; // seq 相差8192认为无效
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    uint8_t *key = new uint8_t[server_key.size()];
    memcpy(key, server_key.data(), server_key.size());
    policy.key = key;

    if (srtp_create(&srtp_recv, &policy) != 0) {
        return srs_error_wrap(err, "srtp_create failed");
    }

    delete [] key;

    return err;
}

SrsRtcSession::SrsRtcSession()
{
    session_state = INIT;
    dtls_session = NULL;
}

SrsRtcSession::~SrsRtcSession()
{
}

srs_error_t SrsRtcSession::on_binding_request(const SrsStunPacket& stun_packet, const string& peer_ip, const uint16_t peer_port, 
        SrsStunPacket& stun_binding_response)
{
    srs_error_t err = srs_success;

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(stun_packet.get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(stun_packet.get_local_ufrag());
    stun_binding_response.set_transcation_id(stun_packet.get_transcation_id());
    stun_binding_response.set_mapped_address(be32toh(inet_addr(peer_ip.c_str())));
    stun_binding_response.set_mapped_port(peer_port);

    return err;
}

srs_error_t SrsRtcSession::send_client_hello(srs_netfd_t fd, const sockaddr* from, int fromlen)
{
    if (dtls_session == NULL) {
        dtls_session = new SrsDtlsSession(fd, from, fromlen);
    }

    dtls_session->send_client_hello();
}

srs_error_t SrsRtcSession::on_dtls(const char* buf, const int nb_buf)
{
    dtls_session->on_dtls(buf, nb_buf);
}

srs_error_t SrsRtcSession::send_packet()
{
}

SrsRtcServer::SrsRtcServer(SrsServer* svr)
{
    server = svr;
}

SrsRtcServer::~SrsRtcServer()
{
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t SrsRtcServer::on_udp_packet(srs_netfd_t fd, const string& peer_ip, const int peer_port, 
        const sockaddr* from, const int fromlen, const char* data, const int size)
{
    srs_error_t err = srs_success;

    if (is_stun(data, size)) {
        return on_stun(fd, peer_ip, peer_port, from, fromlen, data, size);
    } else if (is_dtls(data, size)) { 
        srs_trace("dtls");
        return on_dtls(fd, peer_ip, peer_port, from, fromlen, data, size);
    } else if (is_rtp_or_rtcp(data, size)) {
        return on_rtp_or_rtcp(fd, peer_ip, peer_port, from, fromlen, data, size);
    } 

    return srs_error_wrap(err, "unknown packet type");
}

SrsRtcSession* SrsRtcServer::create_rtc_session(const SrsSdp& remote_sdp, SrsSdp& local_sdp)
{
    SrsRtcSession* session = new SrsRtcSession();

    std::string local_ufrag = gen_random_str(8);
    std::string local_pwd = gen_random_str(32);

    while (true) {
        bool ret = map_ufrag_sessions.insert(make_pair(remote_sdp.get_ice_ufrag(), session)).second;
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

SrsRtcSession* SrsRtcServer::find_rtc_session_by_ip_port(const string& peer_ip, const uint16_t peer_port)
{
    ostringstream os;
    os << peer_ip << ":" << peer_port;
    string key = os.str();
    map<string, SrsRtcSession*>::iterator iter = map_ip_port_sessions.find(key);
    if (iter == map_ip_port_sessions.end()) {
        return NULL; 
    }

    return iter->second;
}

srs_error_t SrsRtcServer::on_stun(srs_netfd_t fd, const string& peer_ip, const int peer_port, 
        const sockaddr* from, const int fromlen, const char* data, const int size)
{
    srs_error_t err = srs_success;

    srs_trace("peer %s:%d stun", peer_ip.c_str(), peer_port);

    SrsStunPacket stun_req;
    if (stun_req.decode(data, size) != srs_success) {
        return srs_error_wrap(err, "decode stun failed");
    }

    std::string remote_ufrag = stun_req.get_remote_ufrag();
    SrsRtcSession* rtc_session = find_rtc_session_by_ufrag(remote_ufrag);
    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc_session, ufrag=%s", remote_ufrag.c_str());
    }

    SrsStunPacket stun_rsp;
    char buf[1460];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    if (stun_req.is_binding_request()) {
        if (rtc_session->on_binding_request(stun_req, peer_ip, peer_port, stun_rsp) != srs_success) {
            return srs_error_wrap(err, "stun binding request failed");
        }
    }

    if (stun_rsp.encode(rtc_session->get_local_sdp()->get_ice_pwd(), stream) != srs_success) {
        return srs_error_wrap(err, "stun rsp encode failed");
    }

    srs_sendto(fd, stream->data(), stream->pos(), from, fromlen, 0);

    if (rtc_session->get_session_state() == WAITING_STUN) {
        rtc_session->set_session_state(DOING_DTLS_HANDSHAKE);
        rtc_session->send_client_hello(fd, from, fromlen);

        insert_into_ip_port_sessions(peer_ip, peer_port, rtc_session);
    }

    return err;
}

srs_error_t SrsRtcServer::on_dtls(srs_netfd_t fd, const string& peer_ip, const int peer_port, 
        const sockaddr* from, const int fromlen, const char* data, const int size)
{
    srs_error_t err = srs_success;
    srs_trace("on dtls");

    // FIXME
    SrsRtcSession* rtc_session = find_rtc_session_by_ip_port(peer_ip, peer_port);

    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc session by ip=%s, port=%u", peer_ip.c_str(), peer_port);
    }

    rtc_session->on_dtls(data, size);

    return err;
}

srs_error_t SrsRtcServer::on_rtp_or_rtcp(srs_netfd_t fd, const string& peer_ip, const int peer_port, 
        const sockaddr* from, const int fromlen, const char* data, const int size)
{
    srs_error_t err = srs_success;
    srs_trace("on rtp/rtcp");
    return err;
}

SrsRtcSession* SrsRtcServer::find_rtc_session_by_ufrag(const std::string& ufrag)
{
    map<string, SrsRtcSession*>::iterator iter = map_ufrag_sessions.find(ufrag);
    if (iter == map_ufrag_sessions.end()) {
        return NULL; 
    }

    return iter->second;
}

bool SrsRtcServer::insert_into_ip_port_sessions(const string& peer_ip, const uint16_t peer_port, SrsRtcSession* rtc_session)
{
    ostringstream os;
    os << peer_ip << ":" << peer_port;
    string key = os.str();

    return map_ip_port_sessions.insert(make_pair(key, rtc_session)).second;
}
