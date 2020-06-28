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
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_dtls.hpp>

#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>

class SrsUdpMuxSocket;
class SrsConsumer;
class SrsStunPacket;
class SrsRtcServer;
class SrsRtcSession;
class SrsSharedPtrMessage;
class SrsRtcSource;
class SrsRtpPacket2;
class ISrsCodec;
class SrsRtpNackForReceiver;
class SrsRtpIncommingVideoFrame;
class SrsRtpRingBuffer;
class SrsRtcConsumer;

const uint8_t kSR   = 200;
const uint8_t kRR   = 201;
const uint8_t kSDES = 202;
const uint8_t kBye  = 203;
const uint8_t kApp  = 204;

// @see: https://tools.ietf.org/html/rfc4585#section-6.1
const uint8_t kRtpFb = 205;
const uint8_t kPsFb  = 206;
const uint8_t kXR    = 207;

// @see: https://tools.ietf.org/html/rfc4585#section-6.3
const uint8_t kPLI  = 1;
const uint8_t kSLI  = 2;
const uint8_t kRPSI = 3;
const uint8_t kAFB  = 15;

// TODO: FIXME: Move to utility.
extern std::string gen_random_str(int len);

class SrsNtp
{
public:
    uint64_t system_ms_;
    uint64_t ntp_;
    uint32_t ntp_second_;
    uint32_t ntp_fractions_;
public:
    SrsNtp();
    virtual ~SrsNtp();
public:
    static SrsNtp from_time_ms(uint64_t ms);
    static SrsNtp to_time_ms(uint64_t ntp);
public:
    static uint64_t kMagicNtpFractionalUnit;
};

enum SrsRtcSessionStateType
{
    // TODO: FIXME: Should prefixed by enum name.
    INIT = -1,
    WAITING_ANSWER = 1,
    WAITING_STUN = 2,
    DOING_DTLS_HANDSHAKE = 3,
    ESTABLISHED = 4,
    CLOSED = 5,
};

class SrsSecurityTransport : public ISrsDtlsCallback
{
private:
    SrsRtcSession* session_;
    SrsDtls* dtls_;
    SrsSRTP* srtp_;
    bool handshake_done;
public:
    SrsSecurityTransport(SrsRtcSession* s);
    virtual ~SrsSecurityTransport();

    srs_error_t initialize(SrsSessionConfig* cfg);
    // When play role of dtls client, it send handshake. 
    srs_error_t start_active_handshake();
    srs_error_t on_dtls(char* data, int nb_data);
public:
    // Encrypt the input plaintext to output cipher with nb_cipher bytes.
    // @remark Note that the nb_cipher is the size of input plaintext, and 
    // it also is the length of output cipher when return.
    srs_error_t protect_rtp(const char* plaintext, char* cipher, int& nb_cipher);
    srs_error_t protect_rtcp(const char* plaintext, char* cipher, int& nb_cipher);
    // Encrypt the input rtp_hdr with *len_ptr bytes.
    // @remark the input plaintext and out cipher reuse rtp_hdr.
    srs_error_t protect_rtp2(void* rtp_hdr, int* len_ptr);
    // Decrypt the input cipher to output cipher with nb_cipher bytes.
    // @remark Note that the nb_plaintext is the size of input cipher, and 
    // it also is the length of output plaintext when return.
    srs_error_t unprotect_rtp(const char* cipher, char* plaintext, int& nb_plaintext);
    srs_error_t unprotect_rtcp(const char* cipher, char* plaintext, int& nb_plaintext);
// implement ISrsDtlsCallback
public:
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char* data, const int len);
    virtual srs_error_t write_dtls_data(void* data, int size);
private:
    srs_error_t srtp_initialize();
};

// A group of RTP packets for outgoing(send to players).
// TODO: FIXME: Rename to stat for RTP packets.
class SrsRtcOutgoingInfo
{
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
    // TODO: FIXME: Remove it because we may don't know.
    int nn_samples;
    // For audio, the generated extra audio packets.
    // For example, when transcoding AAC to opus, may many extra payloads for a audio.
    // TODO: FIXME: Remove it because we may don't know.
    int nn_extras;
    // The original audio messages.
    int nn_audios;
    // The original video messages.
    int nn_videos;
    // The number of padded packet.
    int nn_paddings;
public:
    SrsRtcOutgoingInfo();
    virtual ~SrsRtcOutgoingInfo();
};

class SrsRtcPlayer : virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler
{
protected:
    std::string _parent_cid;
    SrsCoroutine* trd;
    SrsRtcSession* session_;
private:
    // TODO: FIXME: How to handle timestamp overflow?
    // Information for audio.
    uint32_t audio_ssrc;
    uint16_t audio_payload_type;
    // Information for video.
    uint16_t video_payload_type;
    uint32_t video_ssrc;
    // NACK ARQ ring buffer.
    SrsRtpRingBuffer* audio_queue_;
    SrsRtpRingBuffer* video_queue_;
    // Simulators.
    int nn_simulate_nack_drop;
private:
    // For merged-write messages.
    int mw_msgs;
    bool realtime;
    // Whether enabled nack.
    bool nack_enabled_;
public:
    SrsRtcPlayer(SrsRtcSession* s, std::string parent_cid);
    virtual ~SrsRtcPlayer();
public:
    srs_error_t initialize(const uint32_t& vssrc, const uint32_t& assrc, const uint16_t& v_pt, const uint16_t& a_pt);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
public:
    virtual std::string cid();
public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void stop_loop();
public:
    virtual srs_error_t cycle();
private:
    srs_error_t send_packets(SrsRtcSource* source, const std::vector<SrsRtpPacket2*>& pkts, SrsRtcOutgoingInfo& info);
    srs_error_t do_send_packets(const std::vector<SrsRtpPacket2*>& pkts, SrsRtcOutgoingInfo& info);
public:
    void nack_fetch(std::vector<SrsRtpPacket2*>& pkts, uint32_t ssrc, uint16_t seq);
    void simulate_nack_drop(int nn);
private:
    void simulate_drop_packet(SrsRtpHeader* h, int nn_bytes);
public:
    srs_error_t on_rtcp(char* data, int nb_data);
private:
    srs_error_t on_rtcp_sr(char* buf, int nb_buf);
    srs_error_t on_rtcp_xr(char* buf, int nb_buf);
    srs_error_t on_rtcp_feedback(char* data, int nb_data);
    srs_error_t on_rtcp_ps_feedback(char* data, int nb_data);
    srs_error_t on_rtcp_rr(char* data, int nb_data);
};

class SrsRtcPublisher : virtual public ISrsHourGlass, virtual public ISrsRtpPacketDecodeHandler, virtual public ISrsRtcPublisher
{
private:
    SrsHourGlass* report_timer;
    uint64_t nn_audio_frames;
private:
    SrsRtcSession* session_;
    uint32_t video_ssrc;
    uint32_t audio_ssrc;
private:
    bool request_keyframe_;
    SrsRtpRingBuffer* video_queue_;
    SrsRtpNackForReceiver* video_nack_;
    SrsRtpRingBuffer* audio_queue_;
    SrsRtpNackForReceiver* audio_nack_;
private:
    SrsRequest* req;
    SrsRtcSource* source;
    // Whether enabled nack.
    bool nack_enabled_;
    // Simulators.
    int nn_simulate_nack_drop;
private:
    std::map<uint32_t, uint64_t> last_sender_report_sys_time;
    std::map<uint32_t, SrsNtp> last_sender_report_ntp;
private:
    srs_utime_t last_twcc_feedback_time_;
    uint8_t twcc_ext_id_;
    uint8_t twcc_fb_count_;
    SrsRtcpTWCC rtcp_twcc_;
    SrsRtpExtensionTypes extension_types_;
public:
    SrsRtcPublisher(SrsRtcSession* session);
    virtual ~SrsRtcPublisher();
public:
    srs_error_t initialize(uint32_t vssrc, uint32_t assrc, uint8_t twcc_ext_id, SrsRequest* req);
private:
    void check_send_nacks(SrsRtpNackForReceiver* nack, uint32_t ssrc);
    srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer* rtp_queue);
    srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    srs_error_t send_rtcp_fb_pli(uint32_t ssrc);
public:
    srs_error_t on_rtp(char* buf, int nb_buf);
    virtual void on_before_decode_payload(SrsRtpPacket2* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload);
private:
    srs_error_t on_audio(SrsRtpPacket2* pkt);
    srs_error_t on_video(SrsRtpPacket2* pkt);
    srs_error_t on_nack(SrsRtpPacket2* pkt);
    srs_error_t send_periodic_twcc();
public:
    srs_error_t on_rtcp(char* data, int nb_data);
private:
    srs_error_t on_rtcp_sr(char* buf, int nb_buf);
    srs_error_t on_rtcp_xr(char* buf, int nb_buf);
    srs_error_t on_rtcp_feedback(char* data, int nb_data);
    srs_error_t on_rtcp_ps_feedback(char* data, int nb_data);
    srs_error_t on_rtcp_rr(char* data, int nb_data);
public:
    void request_keyframe();
// interface ISrsHourGlass
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
public:
    void simulate_nack_drop(int nn);
private:
    void simulate_drop_packet(SrsRtpHeader* h, int nn_bytes);
private:
    srs_error_t on_twcc(uint16_t sn);
};

class SrsRtcSession
{
    friend class SrsSecurityTransport;
    friend class SrsRtcPlayer;
    friend class SrsRtcPublisher;
public:
    bool disposing_;
private:
    SrsRtcServer* server_;
    SrsRtcSessionStateType state_;
    SrsSecurityTransport* transport_;
    SrsRtcPlayer* player_;
    SrsRtcPublisher* publisher_;
    bool is_publisher_;
private:
    SrsUdpMuxSocket* sendonly_skt;
    std::string username_;
    std::string peer_id_;
private:
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t sessionStunTimeout;
    srs_utime_t last_stun_time;
private:
    // For each RTC session, we use a specified cid for debugging logs.
    std::string cid;
    // For each RTC session, whether requires encrypt.
    //      Read config value, rtc_server.encrypt, default to on.
    //      Sepcifies by HTTP API, query encrypt, optional.
    // TODO: FIXME: Support reload.
    bool encrypt;
    SrsRequest* req;
    SrsRtcSource* source_;
    SrsSdp remote_sdp;
    SrsSdp local_sdp;
public:
    // User debugging parameters, overwrite config.
    std::string sequence_startup;
    std::string sequence_delta;
    std::string sequence_keep;
private:
    bool blackhole;
    sockaddr_in* blackhole_addr;
    srs_netfd_t blackhole_stfd;
public:
    SrsRtcSession(SrsRtcServer* s);
    virtual ~SrsRtcSession();
public:
    SrsSdp* get_local_sdp();
    void set_local_sdp(const SrsSdp& sdp);
    SrsSdp* get_remote_sdp();
    void set_remote_sdp(const SrsSdp& sdp);
    SrsRtcSessionStateType state();
    void set_state(SrsRtcSessionStateType state);
    std::string id();
    std::string peer_id();
    void set_peer_id(std::string v);
    std::string username();
    void set_encrypt(bool v);
    void switch_to_context();
    std::string context_id();
public:
    // Before initialize, user must set the local SDP, which is used to inititlize DTLS.
    srs_error_t initialize(SrsRtcSource* source, SrsRequest* r, bool is_publisher, std::string username, std::string context_id);
    // The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* r);
    srs_error_t on_dtls(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
    srs_error_t on_rtcp(char* data, int nb_data);
public:
    srs_error_t on_connection_established();
    srs_error_t start_play();
    srs_error_t start_publish();
    bool is_stun_timeout();
    void update_sendonly_socket(SrsUdpMuxSocket* skt);
public:
    // Simulate the NACK to drop nn packets.
    void simulate_nack_drop(int nn);
private:
    srs_error_t on_binding_request(SrsStunPacket* r);
};

class ISrsRtcHijacker
{
public:
    ISrsRtcHijacker();
    virtual ~ISrsRtcHijacker();
public:
    // When start publisher by RTC.
    virtual srs_error_t on_start_publish(SrsRtcSession* session, SrsRtcPublisher* publisher, SrsRequest* req) = 0;
    // When got RTP plaintext packet.
    virtual srs_error_t on_rtp_packet(SrsRtcSession* session, SrsRtcPublisher* publisher, SrsRequest* req, SrsRtpPacket2* pkt) = 0;
    // When start player by RTC.
    virtual srs_error_t on_start_play(SrsRtcSession* session, SrsRtcPlayer* player, SrsRequest* req) = 0;
    // When start consuming for player for RTC.
    virtual srs_error_t on_start_consume(SrsRtcSession* session, SrsRtcPlayer* player, SrsRequest* req, SrsRtcConsumer* consumer) = 0;
};

extern ISrsRtcHijacker* _srs_rtc_hijacker;

#endif

