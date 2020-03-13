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
#include <fcntl.h>
#include <unistd.h>

#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_stun_stack.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_dtls.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtp.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>

static bool is_stun(const char* data, const int size) 
{
    return data != NULL && size > 0 && (data[0] == 0 || data[0] == 1); 
}

static bool is_dtls(const char* data, size_t len) 
{
  	return (len >= 13 && (data[0] > 19 && data[0] < 64));
}

static bool is_rtp_or_rtcp(const char* data, size_t len) 
{
  	return (len >= 12 && (data[0] & 0xC0) == 0x80);
}

static bool is_rtcp(const char* data, size_t len)
{
    return (len >=12) && (data[0] & 0x80) && (data[1] >= 200 && data[1] <= 209);
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
		"m=video 9 UDP/TLS/RTP/SAVPF 102\\r\\n"
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

SrsDtlsSession::SrsDtlsSession(SrsRtcSession* s)
{
    rtc_session = s;

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
    if (dtls) {
        // this function will free bio_in and bio_out
        SSL_free(dtls);
        dtls = NULL;
    }

    if (srtp_send) {
        srtp_dealloc(srtp_send);
    }

    if (srtp_recv) {
        srtp_dealloc(srtp_recv);
    }
}

srs_error_t SrsDtlsSession::handshake(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;

	int ret = SSL_do_handshake(dtls);

    unsigned char *out_bio_data;
    int out_bio_len = BIO_get_mem_data(bio_out, &out_bio_data);

    int ssl_err = SSL_get_error(dtls, ret); 
    switch(ssl_err) {   
        case SSL_ERROR_NONE: {   
            err = on_dtls_handshake_done(udp_mux_skt);
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
        udp_mux_skt->sendto(out_bio_data, out_bio_len, 0);
    }

    return err;
}

srs_error_t SrsDtlsSession::on_dtls(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;
    if (! handshake_done) {
		BIO_reset(bio_in);
        BIO_reset(bio_out);
        BIO_write(bio_in, udp_mux_skt->data(), udp_mux_skt->size());

        handshake(udp_mux_skt);
    } else {
		BIO_reset(bio_in);
        BIO_reset(bio_out);
        BIO_write(bio_in, udp_mux_skt->data(), udp_mux_skt->size());

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

srs_error_t SrsDtlsSession::on_dtls_handshake_done(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;
    srs_trace("dtls handshake done");

    handshake_done = true;
    if ((err = srtp_initialize()) != srs_success) {
        srs_error("srtp init failed, err=%s", srs_error_desc(err).c_str());
        return srs_error_wrap(err, "srtp init failed");
    }

    rtc_session->on_connection_established(udp_mux_skt);

    return err;
}

srs_error_t SrsDtlsSession::on_dtls_application_data(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    return err;
}

void SrsDtlsSession::send_client_hello(SrsUdpMuxSocket* udp_mux_skt)
{
    if (dtls == NULL) {    
        srs_trace("send client hello");

        dtls = SSL_new(SrsDtls::instance()->get_dtls_ctx());
        SSL_set_connect_state(dtls);

        bio_in  = BIO_new(BIO_s_mem());
        bio_out = BIO_new(BIO_s_mem());

        SSL_set_bio(dtls, bio_in, bio_out);

        handshake(udp_mux_skt);
    } 
}

srs_error_t SrsDtlsSession::srtp_initialize() 
{
    srs_error_t err = srs_success;

	unsigned char material[SRTP_MASTER_KEY_LEN * 2] = {0};  // client(SRTP_MASTER_KEY_KEY_LEN + SRTP_MASTER_KEY_SALT_LEN) + server
	static const string dtls_srtp_lable = "EXTRACTOR-dtls_srtp";
	if (! SSL_export_keying_material(dtls, material, sizeof(material), dtls_srtp_lable.c_str(), dtls_srtp_lable.size(), NULL, 0, 0)) {   
        return srs_error_wrap(err, "SSL_export_keying_material failed");
	}   

	size_t offset = 0;

	std::string client_master_key(reinterpret_cast<char*>(material), SRTP_MASTER_KEY_KEY_LEN);
	offset += SRTP_MASTER_KEY_KEY_LEN;
	std::string server_master_key(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_KEY_LEN);
	offset += SRTP_MASTER_KEY_KEY_LEN;
	std::string client_master_salt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);
	offset += SRTP_MASTER_KEY_SALT_LEN;
	std::string server_master_salt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);

	client_key = client_master_key + client_master_salt;
	server_key = server_master_key + server_master_salt;

    if (srtp_send_init() != srs_success) {
        return srs_error_wrap(err, "srtp send init failed");
    }

    if (srtp_recv_init() != srs_success) { 
        return srs_error_wrap(err, "srtp recv init failed");
    }

    return err;
}

srs_error_t SrsDtlsSession::srtp_send_init()
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
        srs_freepa(key);
        return srs_error_wrap(err, "srtp_create failed");
    }

    srs_freepa(key);

    return err;
}

srs_error_t SrsDtlsSession::srtp_recv_init()
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
        srs_freepa(key);
        return srs_error_wrap(err, "srtp_create failed");
    }

    srs_freepa(key);

    return err;
}

srs_error_t SrsDtlsSession::protect_rtp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_send) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_protect(srtp_send, out_buf, &nb_out_buf) != 0) {
            return srs_error_wrap(err, "rtp protect failed");
        }

        return err;
    }

    return srs_error_wrap(err, "rtp protect failed");
}

srs_error_t SrsDtlsSession::unprotect_rtp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_recv) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_unprotect(srtp_recv, out_buf, &nb_out_buf) != 0) {
            return srs_error_wrap(err, "rtp unprotect failed");
        }

        return err;
    }

    return srs_error_wrap(err, "rtp unprotect failed");
}

srs_error_t SrsDtlsSession::protect_rtcp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_send) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_protect_rtcp(srtp_send, out_buf, &nb_out_buf) != 0) {
            return srs_error_wrap(err, "rtcp protect failed");
        }

        return err;
    }

    return srs_error_wrap(err, "rtcp protect failed");
}

srs_error_t SrsDtlsSession::unprotect_rtcp(char* out_buf, const char* in_buf, int& nb_out_buf)
{
    srs_error_t err = srs_success;

    if (srtp_recv) {
        memcpy(out_buf, in_buf, nb_out_buf);
        if (srtp_unprotect_rtcp(srtp_recv, out_buf, &nb_out_buf) != 0) {
            return srs_error_wrap(err, "rtcp unprotect failed");
        }

        return err;
    }

    return srs_error_wrap(err, "rtcp unprotect failed");
}

SrsRtcSenderThread::SrsRtcSenderThread(SrsRtcSession* s, SrsUdpMuxSocket* u, int parent_cid)
    : ukt(NULL)
{
    _parent_cid = parent_cid;
    trd = new SrsDummyCoroutine();

    rtc_session = s;
    ukt = *u;
}

SrsRtcSenderThread::~SrsRtcSenderThread()
{
    srs_freep(trd);
}

int SrsRtcSenderThread::cid()
{
    return trd->cid();
}

srs_error_t SrsRtcSenderThread::start()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("rtc_sender", this, _parent_cid);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_sender");
    }
    
    return err;
}

void SrsRtcSenderThread::stop()
{
    trd->stop();
}

void SrsRtcSenderThread::stop_loop()
{
    trd->interrupt();
}


srs_error_t SrsRtcSenderThread::cycle()
{
    srs_error_t err = srs_success;

	SrsSource* source = NULL;
    SrsRequest req;
    req.app = rtc_session->app;
    req.stream = rtc_session->stream;

    if (_srs_sources->fetch_or_create(&req, rtc_session->server, &source) != srs_success) {
        srs_error("rtc fetch source failed");
        return srs_error_wrap(err, "rtc fetch source failed");
    }

    srs_trace("rtc fetch source success, app=%s, stream=%s", rtc_session->app.c_str(), rtc_session->stream.c_str());

	SrsConsumer* consumer = NULL;
    if (source->create_consumer(NULL, consumer) != srs_success) {
        srs_trace("rtc create consumer, app=%s, stream=%s", rtc_session->app.c_str(), rtc_session->stream.c_str());
        return srs_error_wrap(err, "rtc create consumer, app=%s, stream=%s", rtc_session->app.c_str(), rtc_session->stream.c_str());
    }    

    SrsAutoFree(SrsConsumer, consumer);

    while (true) {
		if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc sender thread");
        }

        SrsMessageArray msgs(SRS_PERF_MW_MSGS);

#ifdef SRS_PERF_QUEUE_COND_WAIT
        consumer->wait(0, SRS_PERF_MW_SLEEP);
#endif

        int msg_count = 0;
        if (consumer->dump_packets(&msgs, msg_count) != srs_success) {
            continue;
        }

        if (msg_count <= 0) { 
#ifndef SRS_PERF_QUEUE_COND_WAIT
            srs_usleep(mw_sleep);
#endif
            // ignore when nothing got.
            continue;
        }

        send_and_free_messages(msgs.msgs, msg_count, &ukt);
    }
}

void SrsRtcSenderThread::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, SrsUdpMuxSocket* udp_mux_skt)
{
	for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];

        for (int i = 0; i < msg->rtp_packets.size(); ++i) {
            if (rtc_session->dtls_session) {
                char protected_buf[kRtpPacketSize];
                int nb_protected_buf = msg->rtp_packets[i]->size;

                rtc_session->dtls_session->protect_rtp(protected_buf, msg->rtp_packets[i]->payload, nb_protected_buf);
                // TODO: use sendmmsg to send multi packet one system call
                udp_mux_skt->sendto(protected_buf, nb_protected_buf, 0);
            }
        }

        srs_freep(msg);
    }
}

SrsRtcSession::SrsRtcSession(SrsServer* svr, SrsRtcServer* rtc_svr, const string& un)
{
    server = svr;
    rtc_server = rtc_svr;
    session_state = INIT;
    dtls_session = NULL;
    strd = NULL;

    username = un;
    
    last_stun_time = srs_get_system_time();
}

SrsRtcSession::~SrsRtcSession()
{
    srs_freep(dtls_session);

    if (strd) {
        strd->stop();
    }
    srs_freep(strd);
}

srs_error_t SrsRtcSession::on_stun(SrsUdpMuxSocket* udp_mux_skt, SrsStunPacket* stun_req)
{
    srs_error_t err = srs_success;

    if (stun_req->is_binding_request()) {
        if (on_binding_request(udp_mux_skt, stun_req) != srs_success) {
            return srs_error_wrap(err, "stun binding request failed");
        }
    }

    last_stun_time = srs_get_system_time();

    return err;
}

srs_error_t SrsRtcSession::on_binding_request(SrsUdpMuxSocket* udp_mux_skt, SrsStunPacket* stun_req)
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
    stun_binding_response.set_mapped_address(be32toh(inet_addr(udp_mux_skt->get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(udp_mux_skt->get_peer_port());

    if (stun_binding_response.encode(get_local_sdp()->get_ice_pwd(), stream) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if (udp_mux_skt->sendto(stream->data(), stream->pos(), 0) <= 0) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (get_session_state() == WAITING_STUN) {
        set_session_state(DOING_DTLS_HANDSHAKE);
        send_client_hello(udp_mux_skt);

        peer_id = udp_mux_skt->get_peer_id();
        rtc_server->insert_into_id_sessions(peer_id, this);
    }

    return err;
}

srs_error_t SrsRtcSession::send_client_hello(SrsUdpMuxSocket* udp_mux_skt)
{
    if (dtls_session == NULL) {
        dtls_session = new SrsDtlsSession(this);
    }

    dtls_session->send_client_hello(udp_mux_skt);
}

void SrsRtcSession::on_connection_established(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_trace("rtc session=%s, connection established", id().c_str());
    start_play(udp_mux_skt);
}

srs_error_t SrsRtcSession::start_play(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;

    srs_freep(strd);
    strd = new SrsRtcSenderThread(this, udp_mux_skt, _srs_context->get_id());
    strd->start();

    return err;
}

srs_error_t SrsRtcSession::on_dtls(SrsUdpMuxSocket* udp_mux_skt)
{
    return dtls_session->on_dtls(udp_mux_skt);
}

srs_error_t SrsRtcSession::on_rtp(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;
    if (dtls_session == NULL) {
        return srs_error_wrap(err, "recv unexpect rtp packet before dtls done");
    }

    char unprotected_buf[1460];
    int nb_unprotected_buf = udp_mux_skt->size();
    if (dtls_session->unprotect_rtp(unprotected_buf, udp_mux_skt->data(), nb_unprotected_buf) != srs_success) {
        return srs_error_wrap(err, "rtp unprotect failed");
    }

    // FIXME: use SrsRtpPacket 
    SrsBuffer* stream = new SrsBuffer(unprotected_buf, nb_unprotected_buf);
    SrsAutoFree(SrsBuffer, stream);
    uint8_t first = stream->read_1bytes();
    uint8_t second = stream->read_1bytes();

    bool padding = (first & 0x20);
    bool ext = (first & 0x10);
    uint8_t cc = (first & 0x0F);

    bool marker = (second & 0x80);

    uint16_t sequence = stream->read_2bytes();
    uint32_t timestamp = stream->read_4bytes();
    uint32_t ssrc = stream->read_4bytes();

    srs_verbose("sequence=%u, timestamp=%u, ssrc=%u, padding=%d, ext=%d, cc=%u, marker=%d, payload_type=%u", 
        sequence, timestamp, ssrc, padding, ext, cc, marker, payload_type);

    for (uint8_t i = 0; i < cc; ++i) {
        uint32_t csrc = 0;
        csrc = stream->read_4bytes();
    }

    if (ext) {
        uint16_t extern_profile = stream->read_2bytes();
        uint16_t extern_length = stream->read_2bytes();

        srs_verbose("extern_profile=%u, extern_length=%u", extern_profile, extern_length);

        stream->read_string(extern_length * 4);
    }

    return err;
}

srs_error_t SrsRtcSession::on_rtcp(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;
    if (dtls_session == NULL) {
        return srs_error_wrap(err, "recv unexpect rtcp packet before dtls done");
    }

    char unprotected_buf[1460];
    int nb_unprotected_buf = udp_mux_skt->size();
    if (dtls_session->unprotect_rtcp(unprotected_buf, udp_mux_skt->data(), nb_unprotected_buf) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect failed");
    }

    // FIXME: use SrsRtpPacket 
    SrsBuffer* stream = new SrsBuffer(unprotected_buf, nb_unprotected_buf);
    SrsAutoFree(SrsBuffer, stream);
    uint8_t first = stream->read_1bytes();
    uint8_t payload_type = stream->read_1bytes();

    if (payload_type == kSR) {
    } else if (payload_type == kRR) {
    } else if (kSDES) {
    } else if (kBye) {
    } else if (kApp) {
    } else {
        return srs_error_wrap(err, "unknown rtcp type=%u", payload_type);
    }

    return err;
}

SrsRtcServer::SrsRtcServer(SrsServer* svr)
{
    server = svr;
}

SrsRtcServer::~SrsRtcServer()
{
    rttrd->stop();
    srs_freep(rttrd);
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    rttrd = new SrsRtcTimerThread(this, _srs_context->get_id());
    if (rttrd->start() != srs_success) {
        return srs_error_wrap(err, "rtc timer thread init failed");
    }

    return err;
}

srs_error_t SrsRtcServer::on_udp_packet(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;

    if (is_stun(udp_mux_skt->data(), udp_mux_skt->size())) {
        return on_stun(udp_mux_skt);
    } else if (is_dtls(udp_mux_skt->data(), udp_mux_skt->size())) {
        return on_dtls(udp_mux_skt);
    } else if (is_rtp_or_rtcp(udp_mux_skt->data(), udp_mux_skt->size())) {
        return on_rtp_or_rtcp(udp_mux_skt);
    } 

    return srs_error_wrap(err, "unknown udp packet type");
}

SrsRtcSession* SrsRtcServer::create_rtc_session(const SrsSdp& remote_sdp, SrsSdp& local_sdp)
{
    std::string local_pwd = gen_random_str(32);
    std::string local_ufrag = "";
    std::string username = "";
    while (true) {
        local_ufrag = gen_random_str(8);

        username = local_ufrag + ":" + remote_sdp.get_ice_ufrag();
        if (! map_username_session.count(username))
            break;
    }

    SrsRtcSession* session = new SrsRtcSession(server, this, username);
    map_username_session.insert(make_pair(username, session));

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

srs_error_t SrsRtcServer::on_stun(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;

    srs_trace("recv stun packet from %s", udp_mux_skt->get_peer_id().c_str());

    SrsStunPacket stun_req;
    if (stun_req.decode(udp_mux_skt->data(), udp_mux_skt->size()) != srs_success) {
        return srs_error_wrap(err, "decode stun packet failed");
    }

    std::string username = stun_req.get_username();
    SrsRtcSession* rtc_session = find_rtc_session_by_username(username);
    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc_session, stun username=%s", username.c_str());
    }

    return rtc_session->on_stun(udp_mux_skt, &stun_req);
}

srs_error_t SrsRtcServer::on_dtls(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;
    srs_trace("on dtls");

    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(udp_mux_skt->get_peer_id());

    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc session by peer_id=%s", udp_mux_skt->get_peer_id().c_str());
    }

    rtc_session->on_dtls(udp_mux_skt);

    return err;
}

srs_error_t SrsRtcServer::on_rtp_or_rtcp(SrsUdpMuxSocket* udp_mux_skt)
{
    srs_error_t err = srs_success;

    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(udp_mux_skt->get_peer_id());

    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc session by peer_id=%s", udp_mux_skt->get_peer_id().c_str());
    }

    if (is_rtcp(udp_mux_skt->data(), udp_mux_skt->size())) {
        rtc_session->on_rtcp(udp_mux_skt);
    } else {
        rtc_session->on_rtp(udp_mux_skt);
    }

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

void SrsRtcServer::check_and_clean_timeout_session()
{
    map<string, SrsRtcSession*>::iterator iter = map_username_session.begin();
    while (iter != map_username_session.end()) {
        SrsRtcSession* session = iter->second;
        if (session == NULL) {
            map_username_session.erase(iter++);
            continue;
        }

        if (session->is_stun_timeout()) {
            srs_trace("rtc session=%s, stun timeout", session->id().c_str());
            map_username_session.erase(iter++);
            map_id_session.erase(session->get_peer_id());
            delete session;
            continue;
        }

        ++iter;
    }
}

SrsRtcTimerThread::SrsRtcTimerThread(SrsRtcServer* rtc_svr, int parent_cid)
{
    _parent_cid = parent_cid;
    trd = new SrsDummyCoroutine();

    rtc_server = rtc_svr;
}

SrsRtcTimerThread::~SrsRtcTimerThread()
{
    srs_freep(trd);
}

int SrsRtcTimerThread::cid()
{
    return trd->cid();
}

srs_error_t SrsRtcTimerThread::start()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("rtc_timer", this, _parent_cid);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "rtc_timer");
    }
    
    return err;
}

void SrsRtcTimerThread::stop()
{
    trd->stop();
}

void SrsRtcTimerThread::stop_loop()
{
    trd->interrupt();
}

srs_error_t SrsRtcTimerThread::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
		if ((err = trd->pull()) != srs_success) {
            srs_trace("rtc_timer cycle failed");
            return srs_error_wrap(err, "rtc timer thread");
        }

        srs_usleep(1*1000*1000LL);
        rtc_server->check_and_clean_timeout_session();
    }
}
