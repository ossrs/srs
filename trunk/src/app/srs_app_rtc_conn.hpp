/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
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
#include <srs_rtmp_stack.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_sdp.hpp>
#include <srs_app_reload.hpp>

#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <srtp2/srtp.h>

class SrsUdpMuxSocket;
class SrsConsumer;
class SrsStunPacket;
class SrsRtcServer;
class SrsRtcSession;
class SrsSharedPtrMessage;
class SrsSource;
class SrsRtpPacket2;
class ISrsUdpSender;

const uint8_t kSR   = 200;
const uint8_t kRR   = 201;
const uint8_t kSDES = 202;
const uint8_t kBye  = 203;
const uint8_t kApp  = 204;

// @see: https://tools.ietf.org/html/rfc4585#section-6.1
const uint8_t kRtpFb = 205;
const uint8_t kPsFb  = 206;

// @see: https://tools.ietf.org/html/rfc4585#section-6.3
const uint8_t kPLI  = 1;
const uint8_t kSLI  = 2;
const uint8_t kRPSI = 3;
const uint8_t kAFB  = 15;

enum SrsRtcSessionStateType
{
    // TODO: FIXME: Should prefixed by enum name.
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

    srs_error_t initialize(const SrsRequest& req);

    srs_error_t on_dtls(SrsUdpMuxSocket* skt);
    srs_error_t on_dtls_handshake_done(SrsUdpMuxSocket* skt);
    srs_error_t on_dtls_application_data(const char* data, const int len);
public:
    srs_error_t protect_rtp(char* protected_buf, const char* ori_buf, int& nb_protected_buf);
    srs_error_t protect_rtp2(void* rtp_hdr, int* len_ptr);
    srs_error_t unprotect_rtp(char* unprotected_buf, const char* ori_buf, int& nb_unprotected_buf);
    srs_error_t protect_rtcp(char* protected_buf, const char* ori_buf, int& nb_protected_buf);
    srs_error_t unprotect_rtcp(char* unprotected_buf, const char* ori_buf, int& nb_unprotected_buf);
private:
    srs_error_t handshake(SrsUdpMuxSocket* skt);
private:
    srs_error_t srtp_initialize();
    srs_error_t srtp_send_init();
    srs_error_t srtp_recv_init();
};

// A group of RTP packets.
class SrsRtcPackets
{
public:
    bool use_gso;
    bool should_merge_nalus;
public:
#if defined(SRS_DEBUG)
    // Debug id.
    uint32_t debug_id;
#endif
public:
    // The total bytes of AVFrame packets.
    int nn_bytes;
    // The total bytes of RTP packets.
    int nn_rtp_bytes;
    // The total padded bytes.
    int nn_padding_bytes;
public:
    // The RTP packets send out by sendmmsg or sendmsg. Note that if many packets group to
    // one msghdr by GSO, it's only one RTP packet, because we only send once.
    int nn_rtp_pkts;
    // For video, the samples or NALUs.
    int nn_samples;
    // For audio, the generated extra audio packets.
    // For example, when transcoding AAC to opus, may many extra payloads for a audio.
    int nn_extras;
    // The original audio messages.
    int nn_audios;
    // The original video messages.
    int nn_videos;
    // The number of padded packet.
    int nn_paddings;
    // The number of dropped messages.
    int nn_dropped;
private:
    int cursor;
    int nn_cache;
    SrsRtpPacket2* cache;
public:
    SrsRtcPackets(int nn_cache_max);
    virtual ~SrsRtcPackets();
public:
    void reset(bool gso, bool merge_nalus);
    SrsRtpPacket2* fetch();
    SrsRtpPacket2* back();
    int size();
    int capacity();
    SrsRtpPacket2* at(int index);
};

class SrsRtcSenderThread : virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler
{
protected:
    SrsCoroutine* trd;
    int _parent_cid;
private:
    SrsRtcSession* rtc_session;
    uint32_t video_ssrc;
    uint32_t audio_ssrc;
    uint16_t video_payload_type;
    uint16_t audio_payload_type;
private:
    // TODO: FIXME: How to handle timestamp overflow?
    uint32_t audio_timestamp;
    uint16_t audio_sequence;
private:
    uint16_t video_sequence;
public:
    SrsUdpMuxSocket* sendonly_ukt;
private:
    ISrsUdpSender* sender;
private:
    bool merge_nalus;
    bool gso;
    int max_padding;
private:
    srs_utime_t mw_sleep;
    int mw_msgs;
    bool realtime;
public:
    SrsRtcSenderThread(SrsRtcSession* s, SrsUdpMuxSocket* u, int parent_cid);
    virtual ~SrsRtcSenderThread();
public:
    srs_error_t initialize(const uint32_t& vssrc, const uint32_t& assrc, const uint16_t& v_pt, const uint16_t& a_pt);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_rtc_server();
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
public:
    virtual int cid();
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void stop_loop();
public:
    void update_sendonly_socket(SrsUdpMuxSocket* skt);
public:
    virtual srs_error_t cycle();
private:
    srs_error_t send_messages(SrsSource* source, SrsSharedPtrMessage** msgs, int nb_msgs, SrsRtcPackets& packets);
    srs_error_t messages_to_packets(SrsSource* source, SrsSharedPtrMessage** msgs, int nb_msgs, SrsRtcPackets& packets);
    srs_error_t send_packets(SrsRtcPackets& packets);
    srs_error_t send_packets_gso(SrsRtcPackets& packets);
private:
    srs_error_t packet_opus(SrsSample* sample, SrsRtcPackets& packets, int nn_max_payload);
private:
    srs_error_t packet_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, SrsRtcPackets& packets);
    srs_error_t packet_nalus(SrsSharedPtrMessage* msg, SrsRtcPackets& packets);
    srs_error_t packet_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, SrsRtcPackets& packets);
    srs_error_t packet_stap_a(SrsSource* source, SrsSharedPtrMessage* msg, SrsRtcPackets& packets);
};

class SrsRtcSession
{
    friend class SrsRtcSenderThread;
private:
    SrsRtcServer* rtc_server;
    SrsSdp  remote_sdp;
    SrsSdp  local_sdp;
    SrsRtcSessionStateType session_state;
    SrsDtlsSession* dtls_session;
    SrsRtcSenderThread* strd;
    std::string username;
    std::string peer_id;
    srs_utime_t last_stun_time;
private:
    // For each RTC session, we use a specified cid for debugging logs.
    int cid;
    // For each RTC session, whether requires encrypt.
    //      Read config value, rtc_server.encrypt, default to on.
    //      Sepcifies by HTTP API, query encrypt, optional.
    // TODO: FIXME: Support reload.
    bool encrypt;
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t sessionStunTimeout;
public:
    SrsRequest request;
    SrsSource* source;
public:
    SrsRtcSession(SrsRtcServer* rtc_svr, const SrsRequest& req, const std::string& un, int context_id);
    virtual ~SrsRtcSession();
public:
    SrsSdp* get_local_sdp() { return &local_sdp; }
    void set_local_sdp(const SrsSdp& sdp);

    SrsSdp* get_remote_sdp() { return &remote_sdp; }
    void set_remote_sdp(const SrsSdp& sdp) { remote_sdp = sdp; }

    SrsRtcSessionStateType get_session_state() { return session_state; }
    void set_session_state(SrsRtcSessionStateType state) { session_state = state; }

    std::string id() const { return peer_id + "_" + username; }

    std::string get_peer_id() const { return peer_id; }
    void set_peer_id(const std::string& id) { peer_id = id; }

    void set_encrypt(bool v) { encrypt = v; }

    void switch_to_context();
public:
    srs_error_t on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* stun_req);
    srs_error_t on_dtls(SrsUdpMuxSocket* skt);
    srs_error_t on_rtcp(SrsUdpMuxSocket* skt);
public:
    srs_error_t send_client_hello(SrsUdpMuxSocket* skt);
    srs_error_t on_connection_established(SrsUdpMuxSocket* skt);
    srs_error_t start_play(SrsUdpMuxSocket* skt);
public:
    bool is_stun_timeout();
private:
    srs_error_t check_source();
private:
    srs_error_t on_binding_request(SrsUdpMuxSocket* skt, SrsStunPacket* stun_req);
private:
    srs_error_t on_rtcp_feedback(char* buf, int nb_buf, SrsUdpMuxSocket* skt);
    srs_error_t on_rtcp_ps_feedback(char* buf, int nb_buf, SrsUdpMuxSocket* skt);
    srs_error_t on_rtcp_receiver_report(char* buf, int nb_buf, SrsUdpMuxSocket* skt);
};

class SrsUdpMuxSender : virtual public ISrsUdpSender, virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler
{
private:
    srs_netfd_t lfd;
    SrsRtcServer* server;
    SrsCoroutine* trd;
private:
    srs_cond_t cond;
    bool waiting_msgs;
    bool gso;
    int nn_senders;
private:
    // Hotspot msgs, we are working on it.
    // @remark We will wait util all messages are ready.
    std::vector<mmsghdr> hotspot;
    // Cache msgs, for other coroutines to fill it.
    std::vector<mmsghdr> cache;
    int cache_pos;
    // The max number of messages for sendmmsg. If 1, we use sendmsg to send.
    int max_sendmmsg;
    // The total queue length, for each sender.
    int queue_length;
    // The extra queue ratio.
    int extra_ratio;
    int extra_queue;
public:
    SrsUdpMuxSender(SrsRtcServer* s);
    virtual ~SrsUdpMuxSender();
public:
    virtual srs_error_t initialize(srs_netfd_t fd, int senders);
private:
    void free_mhdrs(std::vector<mmsghdr>& mhdrs);
public:
    virtual srs_error_t fetch(mmsghdr** pphdr);
    virtual srs_error_t sendmmsg(mmsghdr* hdr);
    virtual bool overflow();
    virtual void set_extra_ratio(int r);
public:
    virtual srs_error_t cycle();
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_rtc_server();
};

class SrsRtcServer : virtual public ISrsUdpMuxHandler, virtual public ISrsHourGlass
{
private:
    SrsHourGlass* timer;
    std::vector<SrsUdpMuxListener*> listeners;
    std::vector<SrsUdpMuxSender*> senders;
private:
    std::map<std::string, SrsRtcSession*> map_username_session; // key: username(local_ufrag + ":" + remote_ufrag)
    std::map<std::string, SrsRtcSession*> map_id_session; // key: peerip(ip + ":" + port)
public:
    SrsRtcServer();
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();
public:
    // TODO: FIXME: Support gracefully quit.
    // TODO: FIXME: Support reload.
    virtual srs_error_t listen_udp();
    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket* skt);
public:
    virtual srs_error_t listen_api();
    SrsRtcSession* create_rtc_session(const SrsRequest& req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const std::string& mock_eip);
    bool insert_into_id_sessions(const std::string& peer_id, SrsRtcSession* rtc_session);
    void check_and_clean_timeout_session();
    int nn_sessions() { return (int)map_username_session.size(); }
private:
    srs_error_t on_stun(SrsUdpMuxSocket* skt);
    srs_error_t on_dtls(SrsUdpMuxSocket* skt);
    srs_error_t on_rtp_or_rtcp(SrsUdpMuxSocket* skt);
private:
    SrsRtcSession* find_rtc_session_by_username(const std::string& ufrag);
    SrsRtcSession* find_rtc_session_by_peer_id(const std::string& peer_id);
// interface ISrsHourGlass
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
};

// The RTC server adapter.
class RtcServerAdapter : public ISrsHybridServer
{
private:
    SrsRtcServer* rtc;
public:
    RtcServerAdapter();
    virtual ~RtcServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();
};

#endif

