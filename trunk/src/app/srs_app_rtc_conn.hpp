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
class SrsRtcConnection;
class SrsSharedPtrMessage;
class SrsRtcStream;
class SrsRtpPacket2;
class ISrsCodec;
class SrsRtpNackForReceiver;
class SrsRtpIncommingVideoFrame;
class SrsRtpRingBuffer;
class SrsRtcConsumer;
class SrsRtcAudioSendTrack;
class SrsRtcVideoSendTrack;
class SrsErrorPithyPrint;

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

enum SrsRtcConnectionStateType
{
    // TODO: FIXME: Should prefixed by enum name.
    INIT = -1,
    WAITING_ANSWER = 1,
    WAITING_STUN = 2,
    DOING_DTLS_HANDSHAKE = 3,
    ESTABLISHED = 4,
    CLOSED = 5,
};

// The security transport, use DTLS/SRTP to protect the data.
class SrsSecurityTransport : public ISrsDtlsCallback
{
private:
    SrsRtcConnection* session_;
    SrsDtls* dtls_;
    SrsSRTP* srtp_;
    bool handshake_done;
public:
    SrsSecurityTransport(SrsRtcConnection* s);
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
class SrsRtcPlayStreamStatistic
{
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
    SrsRtcPlayStreamStatistic();
    virtual ~SrsRtcPlayStreamStatistic();
};

// A RTC play stream, client pull and play stream from SRS.
class SrsRtcPlayStream : virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler, virtual public ISrsHourGlass
{
private:
    SrsContextId _parent_cid;
    SrsCoroutine* trd;
    SrsRtcConnection* session_;
private:
    SrsHourGlass* timer_;
    // key: publish_ssrc, value: send track to process rtp/rtcp
    std::map<uint32_t, SrsRtcAudioSendTrack*> audio_tracks_;
    std::map<uint32_t, SrsRtcVideoSendTrack*> video_tracks_;
private:
    // For merged-write messages.
    int mw_msgs;
    bool realtime;
    // Whether enabled nack.
    bool nack_enabled_;
private:
    // Whether palyer started.
    bool is_started;
    // The statistic for consumer to send packets to player.
    SrsRtcPlayStreamStatistic info;
public:
    SrsRtcPlayStream(SrsRtcConnection* s, SrsContextId parent_cid);
    virtual ~SrsRtcPlayStream();
public:
    srs_error_t initialize(SrsRequest* request, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
public:
    virtual SrsContextId cid();
public:
    virtual srs_error_t start();
    virtual void stop();
public:
    virtual srs_error_t cycle();
private:
    srs_error_t send_packets(SrsRtcStream* source, const std::vector<SrsRtpPacket2*>& pkts, SrsRtcPlayStreamStatistic& info);
public:
    void nack_fetch(std::vector<SrsRtpPacket2*>& pkts, uint32_t ssrc, uint16_t seq);
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
// interface ISrsHourGlass
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
public:
    srs_error_t on_rtcp(char* data, int nb_data);
private:
    srs_error_t on_rtcp_sr(char* buf, int nb_buf);
    srs_error_t on_rtcp_xr(char* buf, int nb_buf);
    srs_error_t on_rtcp_feedback(char* data, int nb_data);
    srs_error_t on_rtcp_ps_feedback(char* data, int nb_data);
    srs_error_t on_rtcp_rr(char* data, int nb_data);
    uint32_t get_video_publish_ssrc(uint32_t play_ssrc);
};

// A RTC publish stream, client push and publish stream to SRS.
class SrsRtcPublishStream : virtual public ISrsHourGlass, virtual public ISrsRtpPacketDecodeHandler, virtual public ISrsRtcPublishStream
{
private:
    SrsHourGlass* timer_;
    uint64_t nn_audio_frames;
private:
    SrsRtcConnection* session_;
    uint16_t pt_to_drop_;
    // Whether enabled nack.
    bool nack_enabled_;
private:
    bool request_keyframe_;
private:
    SrsRequest* req;
    SrsRtcStream* source;
    // Simulators.
    int nn_simulate_nack_drop;
private:
    // track vector
    std::vector<SrsRtcAudioRecvTrack*> audio_tracks_;
    std::vector<SrsRtcVideoRecvTrack*> video_tracks_;
private:
    int twcc_id_;
    uint8_t twcc_fb_count_;
    SrsRtcpTWCC rtcp_twcc_;
    SrsRtpExtensionTypes extension_types_;
    bool is_started;
public:
    SrsRtcPublishStream(SrsRtcConnection* session);
    virtual ~SrsRtcPublishStream();
public:
    srs_error_t initialize(SrsRequest* req, SrsRtcStreamDescription* stream_desc);
    srs_error_t start();
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
private:
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();
public:
    srs_error_t on_rtp(char* buf, int nb_buf);
private:
    srs_error_t do_on_rtp(char* plaintext, int nb_plaintext);
public:
    virtual void on_before_decode_payload(SrsRtpPacket2* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload);
private:
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
    void request_keyframe(uint32_t ssrc);
// interface ISrsHourGlass
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
public:
    void simulate_nack_drop(int nn);
private:
    void simulate_drop_packet(SrsRtpHeader* h, int nn_bytes);
private:
    srs_error_t on_twcc(uint16_t sn);
    SrsRtcAudioRecvTrack* get_audio_track(uint32_t ssrc);
    SrsRtcVideoRecvTrack* get_video_track(uint32_t ssrc);
    void update_rtt(uint32_t ssrc, int rtt);
    void update_send_report_time(uint32_t ssrc, const SrsNtp& ntp);
};

// The statistics for RTC connection.
class SrsRtcConnectionStatistic
{
public:
    int nn_publishers; int nn_subscribers;
    int nn_rr; int nn_xr; int nn_sr; int nn_nack; int nn_pli;
    uint64_t nn_in_twcc; uint64_t nn_in_rtp; uint64_t nn_in_audios; uint64_t nn_in_videos;
    uint64_t nn_out_twcc; uint64_t nn_out_rtp; uint64_t nn_out_audios; uint64_t nn_out_videos;
private:
    srs_utime_t born;
    srs_utime_t dead;
public:
    SrsRtcConnectionStatistic();
    virtual ~SrsRtcConnectionStatistic();
public:
    std::string summary();
};

// A RTC Peer Connection, SDP level object.
class SrsRtcConnection : virtual public ISrsHourGlass
{
    friend class SrsSecurityTransport;
    friend class SrsRtcPlayStream;
    friend class SrsRtcPublishStream;
public:
    bool disposing_;
    SrsRtcConnectionStatistic* stat_;
private:
    SrsRtcServer* server_;
    SrsRtcConnectionStateType state_;
    SrsSecurityTransport* transport_;
    SrsRtcPlayStream* player_;
    SrsRtcPublishStream* publisher_;
    bool is_publisher_;
    SrsHourGlass* timer_;
private:
    // The local:remote username, such as m5x0n128:jvOm where local name is m5x0n128.
    std::string username_;
    // The peer address, client maybe use more than one address, it's the current selected one.
    SrsUdpMuxSocket* sendonly_skt;
    // The address list, client may use multiple addresses.
    std::map<std::string, SrsUdpMuxSocket*> peer_addresses_;
private:
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t session_timeout;
    srs_utime_t last_stun_time;
private:
    // For each RTC session, we use a specified cid for debugging logs.
    SrsContextId cid;
    // For each RTC session, whether requires encrypt.
    //      Read config value, rtc_server.encrypt, default to on.
    //      Sepcifies by HTTP API, query encrypt, optional.
    // TODO: FIXME: Support reload.
    bool encrypt;
    SrsRequest* req;
    SrsRtcStream* source_;
    SrsSdp remote_sdp;
    SrsSdp local_sdp;
private:
    // twcc handler
    int twcc_id_;
    // Simulators.
    int nn_simulate_player_nack_drop;
    // Pithy print for address change, use port as error code.
    SrsErrorPithyPrint* pp_address_change;
public:
    SrsRtcConnection(SrsRtcServer* s, SrsContextId context_id);
    virtual ~SrsRtcConnection();
public:
    // TODO: FIXME: save only connection info.
    SrsSdp* get_local_sdp();
    void set_local_sdp(const SrsSdp& sdp);
    SrsSdp* get_remote_sdp();
    void set_remote_sdp(const SrsSdp& sdp);
    // Connection level state machine, for ARQ of UDP packets.
    SrsRtcConnectionStateType state();
    void set_state(SrsRtcConnectionStateType state);
    // Get username pair for this connection, used as ID of session.
    std::string username();
    // Get all addresses client used.
    std::vector<SrsUdpMuxSocket*> peer_addresses();
public:
    void set_encrypt(bool v);
    void switch_to_context();
    SrsContextId context_id();
public:
    srs_error_t add_publisher(SrsRequest* request, const SrsSdp& remote_sdp, SrsSdp& local_sdp);
    srs_error_t add_player(SrsRequest* request, const SrsSdp& remote_sdp, SrsSdp& local_sdp);
    // server send offer sdp to client, local sdp derivate from source stream desc.
    srs_error_t add_player2(SrsRequest* request, SrsSdp& local_sdp);
public:
    // Before initialize, user must set the local SDP, which is used to inititlize DTLS.
    srs_error_t initialize(SrsRtcStream* source, SrsRequest* r, bool is_publisher, std::string username);
    // The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* r);
    srs_error_t on_dtls(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
    srs_error_t on_rtcp(char* data, int nb_data);
    srs_error_t on_rtcp_feedback(char* buf, int nb_buf);
public:
    srs_error_t on_connection_established();
    srs_error_t start_play();
    srs_error_t start_publish();
    bool is_stun_timeout();
    void update_sendonly_socket(SrsUdpMuxSocket* skt);
// interface ISrsHourGlass
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
public:
    // send rtcp
    void check_send_nacks(SrsRtpNackForReceiver* nack, uint32_t ssrc, uint32_t& sent_nacks);
    srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer* rtp_queue, const uint64_t& last_send_systime, const SrsNtp& last_send_ntp);
    srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    srs_error_t send_rtcp_fb_pli(uint32_t ssrc);
public:
    // Simulate the NACK to drop nn packets.
    void simulate_nack_drop(int nn);
    void simulate_player_drop_packet(SrsRtpHeader* h, int nn_bytes);
    srs_error_t do_send_packets(const std::vector<SrsRtpPacket2*>& pkts, SrsRtcPlayStreamStatistic& info);
    // Directly set the status of play track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
private:
    srs_error_t on_binding_request(SrsStunPacket* r);
    // publish media capabilitiy negotiate
    srs_error_t negotiate_publish_capability(SrsRequest* req, const SrsSdp& remote_sdp, SrsRtcStreamDescription* stream_desc);
    srs_error_t generate_publish_local_sdp(SrsRequest* req, SrsSdp& local_sdp, SrsRtcStreamDescription* stream_desc);
    // play media capabilitiy negotiate
    srs_error_t negotiate_play_capability(SrsRequest* req, const SrsSdp& remote_sdp, std::map<uint32_t, SrsRtcTrackDescription*>& sub_relations);
    srs_error_t fetch_source_capability(SrsRequest* req, std::map<uint32_t, SrsRtcTrackDescription*>& sub_relations);
    srs_error_t generate_play_local_sdp(SrsRequest* req, SrsSdp& local_sdp, SrsRtcStreamDescription* stream_desc);
    srs_error_t create_player(SrsRequest* request, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations);
    srs_error_t create_publisher(SrsRequest* request, SrsRtcStreamDescription* stream_desc);
};

class ISrsRtcHijacker
{
public:
    ISrsRtcHijacker();
    virtual ~ISrsRtcHijacker();
public:
    // When start publisher by RTC.
    virtual srs_error_t on_start_publish(SrsRtcConnection* session, SrsRtcPublishStream* publisher, SrsRequest* req) = 0;
    // When got RTP plaintext packet.
    virtual srs_error_t on_rtp_packet(SrsRtcConnection* session, SrsRtcPublishStream* publisher, SrsRequest* req, SrsRtpPacket2* pkt) = 0;
    // When start player by RTC.
    virtual srs_error_t on_start_play(SrsRtcConnection* session, SrsRtcPlayStream* player, SrsRequest* req) = 0;
    // When start consuming for player for RTC.
    virtual srs_error_t on_start_consume(SrsRtcConnection* session, SrsRtcPlayStream* player, SrsRequest* req, SrsRtcConsumer* consumer) = 0;
};

extern ISrsRtcHijacker* _srs_rtc_hijacker;

#endif

