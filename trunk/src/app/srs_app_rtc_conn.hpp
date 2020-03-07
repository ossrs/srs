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

#ifndef SRS_APP_RTC_CONN_HPP
#define SRS_APP_RTC_CONN_HPP

#include <srs_core.hpp>
#include <srs_app_listener.hpp>
#include <srs_service_st.hpp>

#include <string>
#include <map>
#include <vector>

#include <openssl/ssl.h>
#include <srtp2/srtp.h>

class SrsUdpRemuxSocket;
class SrsServer;
class SrsStunPacket;

class SrsCandidate
{
private:
public:
    SrsCandidate();
    virtual ~SrsCandidate();

    static std::vector<std::string> get_candidate_ips();
};

class SrsSdpMediaInfo
{
private:
public:
    SrsSdpMediaInfo();
    virtual ~SrsSdpMediaInfo();
};

class SrsSdp
{
private:
    std::string sdp;
    int version;
    std::string ice_ufrag;
    std::string ice_pwd;
    std::string fingerprint;
    std::string setup;
    std::vector<SrsSdpMediaInfo> media_infos;
public:
    SrsSdp();
    virtual ~SrsSdp();

    srs_error_t decode(const std::string& sdp_str);
    srs_error_t encode(std::string& sdp_str);

    std::string get_ice_ufrag() const { return ice_ufrag; }
    std::string get_ice_pwd() const { return ice_pwd; }

    void set_ice_ufrag(const std::string& u) { ice_ufrag = u; }
    void set_ice_pwd(const std::string& p) { ice_pwd = p; }
private:
    srs_error_t parse_attr(const std::string& line);
};

enum SrsRtcSessionStateType
{
    INIT = -1,
    WAITING_STUN = 1,
    DOING_DTLS_HANDSHAKE = 2,
    ESTABLISHED = 3,
    CLOSED = 4,
};

class SrsDtlsSession
{
private:
	SSL* dtls;
    BIO* bio_in;
    BIO* bio_out;

    std::string client_key;
    std::string server_key;

    srtp_t srtp_send;
    srtp_t srtp_recv;

    bool handshake_done;

public:
    SrsDtlsSession();
    virtual ~SrsDtlsSession();

    srs_error_t on_dtls(SrsUdpRemuxSocket* udp_remux_socket);
    srs_error_t on_dtls_handshake_done();
    srs_error_t on_dtls_application_data(const char* data, const int len);

    void send_client_hello(SrsUdpRemuxSocket* udp_remux_socket);
    srs_error_t handshake(SrsUdpRemuxSocket* udp_remux_socket);
 
private:
    srs_error_t srtp_init();
    srs_error_t srtp_sender_side_init();
    srs_error_t srtp_receiver_side_init();
};

class SrsRtcServer;

class SrsRtcSession
{
private:
    SrsRtcServer* rtc_server;
    SrsSdp  remote_sdp;
    SrsSdp  local_sdp;
    SrsRtcSessionStateType session_state;
    SrsDtlsSession* dtls_session;
public:
    SrsRtcSession(SrsRtcServer* svr);
    virtual ~SrsRtcSession();
public:
    SrsSdp* get_local_sdp() { return &local_sdp; }
    SrsSdp* get_remote_sdp() { return &remote_sdp; }
    SrsRtcSessionStateType get_session_state() { return session_state; }

    void set_local_sdp(const SrsSdp& sdp) { local_sdp = sdp; }
    void set_remote_sdp(const SrsSdp& sdp) { remote_sdp = sdp; }
    void set_session_state(SrsRtcSessionStateType state) { session_state = state; }
public:
    srs_error_t on_stun(SrsUdpRemuxSocket* udp_remux_socket, SrsStunPacket* stun_req);
    srs_error_t on_dtls(SrsUdpRemuxSocket* udp_remux_socket);
public:
    srs_error_t send_client_hello(SrsUdpRemuxSocket* udp_remux_socket);
private:
    srs_error_t on_binding_request(SrsUdpRemuxSocket* udp_remux_socket, SrsStunPacket* stun_req);
};

class SrsRtcServer : public ISrsUdpRemuxHandler
{
private:
    std::map<std::string, SrsRtcSession*> map_username_session; // key: username(local_ufrag + ":" + remote_ufrag)
    std::map<std::string, SrsRtcSession*> map_id_session; // key: peerip(ip + ":" + port)
public:
    SrsRtcServer();
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();

    virtual srs_error_t on_udp_packet(SrsUdpRemuxSocket* udp_remux_socket);

    SrsRtcSession* create_rtc_session(const SrsSdp& remote_sdp, SrsSdp& local_sdp);
    bool insert_into_id_sessions(const std::string& peer_id, SrsRtcSession* rtc_session);
private:
    srs_error_t on_stun(SrsUdpRemuxSocket* udp_remux_socket);
    srs_error_t on_dtls(SrsUdpRemuxSocket* udp_remux_socket);
    srs_error_t on_rtp_or_rtcp(SrsUdpRemuxSocket* udp_remux_socket);
private:
    SrsRtcSession* find_rtc_session_by_username(const std::string& ufrag);
    SrsRtcSession* find_rtc_session_by_peer_id(const std::string& peer_id);
};

#endif

