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
#include <srs_kernel_utility.hpp>

#include <string>
#include <map>
#include <vector>

#include <openssl/ssl.h>
#include <srtp2/srtp.h>

class SrsUdpMuxSocket;
class SrsServer;
class SrsConsumer;
class SrsStunPacket;
class SrsRtcServer;
class SrsRtcSession;
class SrsSharedPtrMessage;

const uint8_t kSR = 200;
const uint8_t kRR = 201;
const uint8_t kSDES = 202;
const uint8_t kBye = 203;
const uint8_t kApp = 204;

const srs_utime_t kSrsRtcSessionStunTimeoutUs = 10*1000*1000LL;

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
    SrsRtcSession* rtc_session;

	SSL* dtls;
    BIO* bio_in;
    BIO* bio_out;

    std::string client_key;
    std::string server_key;

    srtp_t srtp_send;
    srtp_t srtp_recv;

    bool handshake_done;

public:
    SrsDtlsSession(SrsRtcSession* s);
    virtual ~SrsDtlsSession();

    srs_error_t on_dtls(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t on_dtls_handshake_done(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t on_dtls_application_data(const char* data, const int len);

    void send_client_hello(SrsUdpMuxSocket* udp_mux_skt);
public:
    srs_error_t protect_rtp(char* protected_buf, const char* ori_buf, int& nb_protected_buf);
    srs_error_t unprotect_rtp(char* unprotected_buf, const char* ori_buf, int& nb_unprotected_buf);
    srs_error_t protect_rtcp(char* protected_buf, const char* ori_buf, int& nb_protected_buf);
    srs_error_t unprotect_rtcp(char* unprotected_buf, const char* ori_buf, int& nb_unprotected_buf);
private:
    srs_error_t handshake(SrsUdpMuxSocket* udp_mux_skt);
private:
    srs_error_t srtp_initialize();
    srs_error_t srtp_send_init();
    srs_error_t srtp_recv_init();
};

class SrsRtcSenderThread : public ISrsCoroutineHandler
{
protected:
    SrsCoroutine* trd;
    int _parent_cid;
private:
    SrsRtcSession* rtc_session;
    SrsUdpMuxSocket ukt;
public:
    // Constructor.
    // @param tm The receive timeout in srs_utime_t.
    SrsRtcSenderThread(SrsRtcSession* s, SrsUdpMuxSocket* u, int parent_cid);
    virtual ~SrsRtcSenderThread();
public:
    virtual int cid();
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void stop_loop();
public:
    virtual srs_error_t cycle();
private:
    void send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, SrsUdpMuxSocket* udp_mux_skt);
};

class SrsRtcSession
{
    friend class SrsRtcSenderThread;
private:
    SrsServer* server;
    SrsRtcServer* rtc_server;
    SrsSdp  remote_sdp;
    SrsSdp  local_sdp;
    SrsRtcSessionStateType session_state;
    SrsDtlsSession* dtls_session;
    SrsRtcSenderThread* strd;
    std::string username;
    std::string peer_id;
    srs_utime_t last_stun_time;
public:
    std::string app;
    std::string stream;
public:
    SrsRtcSession(SrsServer* svr, SrsRtcServer* rtc_svr, const std::string& un);
    virtual ~SrsRtcSession();
public:
    SrsSdp* get_local_sdp() { return &local_sdp; }
    void set_local_sdp(const SrsSdp& sdp) { local_sdp = sdp; }

    SrsSdp* get_remote_sdp() { return &remote_sdp; }
    void set_remote_sdp(const SrsSdp& sdp) { remote_sdp = sdp; }

    SrsRtcSessionStateType get_session_state() { return session_state; }
    void set_session_state(SrsRtcSessionStateType state) { session_state = state; }

    std::string id() const { return peer_id + "_" + username; }

    void set_app_stream(const std::string& a, const std::string& s) { app = a; stream = s; }

    std::string get_peer_id() const { return peer_id; }
    void set_peer_id(const std::string& id) { peer_id = id; }
public:
    srs_error_t on_stun(SrsUdpMuxSocket* udp_mux_skt, SrsStunPacket* stun_req);
    srs_error_t on_dtls(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t on_rtp(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t on_rtcp(SrsUdpMuxSocket* udp_mux_skt);
public:
    srs_error_t send_client_hello(SrsUdpMuxSocket* udp_mux_skt);
    void on_connection_established(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t start_play(SrsUdpMuxSocket* udp_mux_skt);
public:
    bool is_stun_timeout() { return last_stun_time + kSrsRtcSessionStunTimeoutUs < srs_get_system_time(); }
private:
    srs_error_t on_binding_request(SrsUdpMuxSocket* udp_mux_skt, SrsStunPacket* stun_req);
private:
    srs_error_t do_playing(SrsConsumer* consumer, SrsUdpMuxSocket* udp_mux_skt);
};

// XXX: is there any other timer thread?
class SrsRtcTimerThread : public ISrsCoroutineHandler
{
protected:
    SrsCoroutine* trd;
    int _parent_cid;
private:
    SrsRtcServer* rtc_server;
public:
    // Constructor.
    // @param tm The receive timeout in srs_utime_t.
    SrsRtcTimerThread(SrsRtcServer* rtc_svr, int parent_cid);
    virtual ~SrsRtcTimerThread();
public:
    virtual int cid();
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void stop_loop();
public:
    virtual srs_error_t cycle();
};

class SrsRtcServer : public ISrsUdpMuxHandler
{
private:
    SrsServer* server;
    SrsRtcTimerThread* rttrd;
private:
    std::map<std::string, SrsRtcSession*> map_username_session; // key: username(local_ufrag + ":" + remote_ufrag)
    std::map<std::string, SrsRtcSession*> map_id_session; // key: peerip(ip + ":" + port)
public:
    SrsRtcServer(SrsServer* svr);
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();

    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket* udp_mux_skt);

    SrsRtcSession* create_rtc_session(const SrsSdp& remote_sdp, SrsSdp& local_sdp);
    bool insert_into_id_sessions(const std::string& peer_id, SrsRtcSession* rtc_session);
    void check_and_clean_timeout_session();
private:
    srs_error_t on_stun(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t on_dtls(SrsUdpMuxSocket* udp_mux_skt);
    srs_error_t on_rtp_or_rtcp(SrsUdpMuxSocket* udp_mux_skt);
private:
    SrsRtcSession* find_rtc_session_by_username(const std::string& ufrag);
    SrsRtcSession* find_rtc_session_by_peer_id(const std::string& peer_id);
};

#endif

