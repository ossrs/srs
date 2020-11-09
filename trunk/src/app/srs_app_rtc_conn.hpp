//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

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
#include <srs_service_conn.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_async_call.hpp>

#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>

class SrsUdpMuxSocket;
class SrsLiveConsumer;
class SrsStunPacket;
class SrsRtcServer;
class SrsRtcConnection;
class SrsSharedPtrMessage;
class SrsRtcSource;
class SrsRtpPacket;
class ISrsCodec;
class SrsRtpNackForReceiver;
class SrsRtpIncommingVideoFrame;
class SrsRtpRingBuffer;
class SrsRtcConsumer;
class SrsRtcAudioSendTrack;
class SrsRtcVideoSendTrack;
class SrsErrorPithyPrint;
class SrsPithyPrint;
class SrsStatistic;
class SrsRtcUserConfig;
class SrsRtcSendTrack;
class SrsRtcPublishStream;

#ifdef SRS_SCTP
class SrsSctp;
#endif

const uint8_t kSR   = 200;
const uint8_t kRR   = 201;
const uint8_t kSDES = 202;
const uint8_t kBye  = 203;
const uint8_t kApp  = 204;

// @see: https://tools.ietf.org/html/rfc4585#section-6.1
const uint8_t kRtpFb = 205;
const uint8_t kPsFb  = 206;
const uint8_t kXR    = 207;

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

// The transport for RTC connection.
class ISrsRtcTransport : public ISrsDtlsCallback
{
public:
    ISrsRtcTransport();
    virtual ~ISrsRtcTransport();
public:
    virtual srs_error_t initialize(SrsSessionConfig* cfg) = 0;
    virtual srs_error_t start_active_handshake() = 0;
    virtual srs_error_t on_dtls(char* data, int nb_data) = 0;
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
public:
    // Encrypt the packet(paintext) to cipher, which is aso the packet ptr.
    // The nb_cipher should be initialized to the size of cipher, with some paddings.
    virtual srs_error_t protect_rtp(void* packet, int* nb_cipher) = 0;
    virtual srs_error_t protect_rtcp(void* packet, int* nb_cipher) = 0;
    // Decrypt the packet(cipher) to plaintext, which is also the packet ptr.
    // The nb_plaintext should be initialized to the size of cipher.
    virtual srs_error_t unprotect_rtp(void* packet, int* nb_plaintext) = 0;
    virtual srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext) = 0;
};

// The security transport, use DTLS/SRTP to protect the data.
class SrsSecurityTransport : public ISrsRtcTransport
{
private:
    SrsRtcConnection* session_;
    SrsDtls* dtls_;
    SrsSRTP* srtp_;
#ifdef SRS_SCTP
    SrsSctp* sctp_;
#endif
    bool handshake_done;
public:
    SrsSecurityTransport(SrsRtcConnection* s);
    virtual ~SrsSecurityTransport();

    srs_error_t initialize(SrsSessionConfig* cfg);
    // When play role of dtls client, it send handshake. 
    srs_error_t start_active_handshake();
    srs_error_t on_dtls(char* data, int nb_data);
    srs_error_t on_dtls_alert(std::string type, std::string desc);
public:
    // Encrypt the packet(paintext) to cipher, which is aso the packet ptr.
    // The nb_cipher should be initialized to the size of cipher, with some paddings.
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    // Decrypt the packet(cipher) to plaintext, which is also the packet ptr.
    // The nb_plaintext should be initialized to the size of cipher.
    srs_error_t unprotect_rtp(void* packet, int* nb_plaintext);
    srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext);
// implement ISrsDtlsCallback
public:
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char* data, const int len);
    virtual srs_error_t write_dtls_data(void* data, int size);
private:
    srs_error_t srtp_initialize();
};

// Semi security transport, setup DTLS and SRTP, with SRTP decrypt, without SRTP encrypt.
class SrsSemiSecurityTransport : public SrsSecurityTransport
{
public:
    SrsSemiSecurityTransport(SrsRtcConnection* s);
    virtual ~SrsSemiSecurityTransport();
public:
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
};

// Plaintext transport, without DTLS or SRTP.
class SrsPlaintextTransport : public ISrsRtcTransport
{
private:
    SrsRtcConnection* session_;
public:
    SrsPlaintextTransport(SrsRtcConnection* s);
    virtual ~SrsPlaintextTransport();
public:
    virtual srs_error_t initialize(SrsSessionConfig* cfg);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char* data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char* data, const int len);
    virtual srs_error_t write_dtls_data(void* data, int size);
public:
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    srs_error_t unprotect_rtp(void* packet, int* nb_plaintext);
    srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext);
};

// The handler for PLI worker coroutine.
class ISrsRtcPLIWorkerHandler
{
public:
    ISrsRtcPLIWorkerHandler();
    virtual ~ISrsRtcPLIWorkerHandler();
public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid) = 0;
};

// A worker coroutine to request the PLI.
class SrsRtcPLIWorker : public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd_;
    srs_cond_t wait_;
    ISrsRtcPLIWorkerHandler* handler_;
private:
    // Key is SSRC, value is the CID of subscriber which requests PLI.
    std::map<uint32_t, SrsContextId> plis_;
public:
    SrsRtcPLIWorker(ISrsRtcPLIWorkerHandler* h);
    virtual ~SrsRtcPLIWorker();
public:
    virtual srs_error_t start();
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid);
// interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
};

// the rtc on_stop async call.
class SrsRtcAsyncCallOnStop : public ISrsAsyncCallTask
{
private:
    SrsContextId cid;
    SrsRequest* req;
public:
    SrsRtcAsyncCallOnStop(SrsContextId c, SrsRequest* r);
    virtual ~SrsRtcAsyncCallOnStop();
public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// A RTC play stream, client pull and play stream from SRS.
class SrsRtcPlayStream : public ISrsCoroutineHandler, public ISrsReloadHandler
    , public ISrsRtcPLIWorkerHandler, public ISrsRtcSourceChangeCallback
{
private:
    SrsContextId cid_;
    SrsFastCoroutine* trd_;
    SrsRtcConnection* session_;
    SrsRtcPLIWorker* pli_worker_;
private:
    SrsRequest* req_;
    SrsRtcSource* source_;
    // key: publish_ssrc, value: send track to process rtp/rtcp
    std::map<uint32_t, SrsRtcAudioSendTrack*> audio_tracks_;
    std::map<uint32_t, SrsRtcVideoSendTrack*> video_tracks_;
    // The pithy print for special stage.
    SrsErrorPithyPrint* nack_epp;
private:
    // Fast cache for tracks.
    uint32_t cache_ssrc0_;
    uint32_t cache_ssrc1_;
    uint32_t cache_ssrc2_;
    SrsRtcSendTrack* cache_track0_;
    SrsRtcSendTrack* cache_track1_;
    SrsRtcSendTrack* cache_track2_;
private:
    // For merged-write messages.
    int mw_msgs;
    bool realtime;
    // Whether enabled nack.
    bool nack_enabled_;
    bool nack_no_copy_;
private:
    // Whether player started.
    bool is_started;
public:
    SrsRtcPlayStream(SrsRtcConnection* s, const SrsContextId& cid);
    virtual ~SrsRtcPlayStream();
public:
    srs_error_t initialize(SrsRequest* request, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations);
// Interface ISrsRtcSourceChangeCallback
public:
    void on_stream_change(SrsRtcSourceDescription* desc);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
    virtual const SrsContextId& context_id();
public:
    virtual srs_error_t start();
    virtual void stop();
public:
    virtual srs_error_t cycle();
private:
    srs_error_t send_packet(SrsRtpPacket*& pkt);
public:
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
public:
    srs_error_t on_rtcp(SrsRtcpCommon* rtcp);
private:
    srs_error_t on_rtcp_xr(SrsRtcpXr* rtcp);
    srs_error_t on_rtcp_nack(SrsRtcpNack* rtcp);
    srs_error_t on_rtcp_ps_feedback(SrsRtcpPsfbCommon* rtcp);
    srs_error_t on_rtcp_rr(SrsRtcpRR* rtcp);
    uint32_t get_video_publish_ssrc(uint32_t play_ssrc);
// Interface ISrsRtcPLIWorkerHandler
public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);
};

// A fast timer for publish stream, for RTCP feedback.
class SrsRtcPublishRtcpTimer : public ISrsFastTimer
{
private:
    SrsRtcPublishStream* p_;
public:
    SrsRtcPublishRtcpTimer(SrsRtcPublishStream* p);
    virtual ~SrsRtcPublishRtcpTimer();
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

// A fast timer for publish stream, for TWCC feedback.
class SrsRtcPublishTwccTimer : public ISrsFastTimer
{
private:
    SrsRtcPublishStream* p_;
public:
    SrsRtcPublishTwccTimer(SrsRtcPublishStream* p);
    virtual ~SrsRtcPublishTwccTimer();
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

// the rtc on_unpublish async call.
class SrsRtcAsyncCallOnUnpublish : public ISrsAsyncCallTask
{
private:
    SrsContextId cid;
    SrsRequest* req;
public:
    SrsRtcAsyncCallOnUnpublish(SrsContextId c, SrsRequest* r);
    virtual ~SrsRtcAsyncCallOnUnpublish();
public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// A RTC publish stream, client push and publish stream to SRS.
class SrsRtcPublishStream : public ISrsRtspPacketDecodeHandler
    , public ISrsRtcPublishStream, public ISrsRtcPLIWorkerHandler
{
private:
    friend class SrsRtcPublishRtcpTimer;
    friend class SrsRtcPublishTwccTimer;
    SrsRtcPublishRtcpTimer* timer_rtcp_;
    SrsRtcPublishTwccTimer* timer_twcc_;
private:
    SrsContextId cid_;
    uint64_t nn_audio_frames;
    SrsRtcPLIWorker* pli_worker_;
    SrsErrorPithyPrint* twcc_epp_;
private:
    SrsRtcConnection* session_;
    uint16_t pt_to_drop_;
    // Whether enabled nack.
    bool nack_enabled_;
    bool nack_no_copy_;
    bool twcc_enabled_;
private:
    bool request_keyframe_;
    SrsErrorPithyPrint* pli_epp;
private:
    SrsRequest* req_;
    SrsRtcSource* source;
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
    srs_utime_t last_time_send_twcc_;
public:
    SrsRtcPublishStream(SrsRtcConnection* session, const SrsContextId& cid);
    virtual ~SrsRtcPublishStream();
public:
    srs_error_t initialize(SrsRequest* req, SrsRtcSourceDescription* stream_desc);
    srs_error_t start();
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
    virtual const SrsContextId& context_id();
private:
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();
public:
    srs_error_t on_rtp(char* buf, int nb_buf);
private:
    // @remark We copy the plaintext, user should free it.
    srs_error_t on_rtp_plaintext(char* plaintext, int nb_plaintext);
private:
    srs_error_t do_on_rtp_plaintext(SrsRtpPacket*& pkt, SrsBuffer* buf);
public:
    srs_error_t check_send_nacks();
public:
    virtual void on_before_decode_payload(SrsRtpPacket* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtspPacketPayloadType* ppt);
private:
    srs_error_t send_periodic_twcc();
public:
    srs_error_t on_rtcp(SrsRtcpCommon* rtcp);
private:
    srs_error_t on_rtcp_sr(SrsRtcpSR* rtcp);
    srs_error_t on_rtcp_xr(SrsRtcpXr* rtcp);
public:
    void request_keyframe(uint32_t ssrc);
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);
public:
    void simulate_nack_drop(int nn);
private:
    void simulate_drop_packet(SrsRtpHeader* h, int nn_bytes);
private:
    srs_error_t on_twcc(uint16_t sn);
    SrsRtcAudioRecvTrack* get_audio_track(uint32_t ssrc);
    SrsRtcVideoRecvTrack* get_video_track(uint32_t ssrc);
    void update_rtt(uint32_t ssrc, int rtt);
    void update_send_report_time(uint32_t ssrc, const SrsNtp& ntp, uint32_t rtp_time);
};

// Callback for RTC connection.
class ISrsRtcConnectionHijacker
{
public:
    ISrsRtcConnectionHijacker();
    virtual ~ISrsRtcConnectionHijacker();
public:
    virtual srs_error_t on_dtls_done() = 0;
};

// A fast timer for conntion, for NACK feedback.
class SrsRtcConnectionNackTimer : public ISrsFastTimer
{
private:
    SrsRtcConnection* p_;
public:
    SrsRtcConnectionNackTimer(SrsRtcConnection* p);
    virtual ~SrsRtcConnectionNackTimer();
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

// A RTC Peer Connection, SDP level object.
//
// For performance, we use non-public from resource,
// see https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
class SrsRtcConnection : public ISrsResource, public ISrsDisposingHandler, public ISrsExpire
{
    friend class SrsSecurityTransport;
    friend class SrsRtcPlayStream;
    friend class SrsRtcPublishStream;
private:
    friend class SrsRtcConnectionNackTimer;
    SrsRtcConnectionNackTimer* timer_nack_;
public:
    bool disposing_;
    ISrsRtcConnectionHijacker* hijacker_;
private:
    SrsRtcServer* server_;
    SrsRtcConnectionStateType state_;
    ISrsRtcTransport* transport_;
private:
    iovec* cache_iov_;
    SrsBuffer* cache_buffer_;
private:
    // key: stream id
    std::map<std::string, SrsRtcPlayStream*> players_;
    //key: player track's ssrc
    std::map<uint32_t, SrsRtcPlayStream*> players_ssrc_map_;
    // key: stream id
    std::map<std::string, SrsRtcPublishStream*> publishers_;
    // key: publisher track's ssrc
    std::map<uint32_t, SrsRtcPublishStream*> publishers_ssrc_map_;
private:
    // The local:remote username, such as m5x0n128:jvOm where local name is m5x0n128.
    std::string username_;
    // The peer address, client maybe use more than one address, it's the current selected one.
    SrsUdpMuxSocket* sendonly_skt;
    // The address list, client may use multiple addresses.
    std::map<std::string, SrsUdpMuxSocket*> peer_addresses_;
private:
    // TODO: FIXME: Rename it.
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t session_timeout;
    // TODO: FIXME: Rename it.
    srs_utime_t last_stun_time;
private:
    // For each RTC session, we use a specified cid for debugging logs.
    SrsContextId cid_;
    SrsRequest* req_;
    SrsSdp remote_sdp;
    SrsSdp local_sdp;
private:
    // twcc handler
    int twcc_id_;
    // Simulators.
    int nn_simulate_player_nack_drop;
    // Pithy print for address change, use port as error code.
    SrsErrorPithyPrint* pp_address_change;
    // Pithy print for PLI request.
    SrsErrorPithyPrint* pli_epp;
private:
    bool nack_enabled_;
public:
    SrsRtcConnection(SrsRtcServer* s, const SrsContextId& cid);
    virtual ~SrsRtcConnection();
// interface ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource* c);
    virtual void on_disposing(ISrsResource* c);
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
// Interface ISrsResource.
public:
    virtual const SrsContextId& get_id();
    virtual std::string desc();
// Interface ISrsExpire.
public:
    virtual void expire();
public:
    void switch_to_context();
    const SrsContextId& context_id();
public:
    srs_error_t add_publisher(SrsRtcUserConfig* ruc, SrsSdp& local_sdp);
    srs_error_t add_player(SrsRtcUserConfig* ruc, SrsSdp& local_sdp);
public:
    // Before initialize, user must set the local SDP, which is used to inititlize DTLS.
    srs_error_t initialize(SrsRequest* r, bool dtls, bool srtp, std::string username);
    // The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsUdpMuxSocket* skt, SrsStunPacket* r);
    srs_error_t on_dtls(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
private:
    // Decode the RTP header from buf, find the publisher by SSRC.
    srs_error_t find_publisher(char* buf, int size, SrsRtcPublishStream** ppublisher);
public:
    srs_error_t on_rtcp(char* data, int nb_data);
private:
    srs_error_t dispatch_rtcp(SrsRtcpCommon* rtcp);
public:
    srs_error_t on_rtcp_feedback_twcc(char* buf, int nb_buf);
    srs_error_t on_rtcp_feedback_remb(SrsRtcpPsfbCommon *rtcp);
public:
    void set_hijacker(ISrsRtcConnectionHijacker* h);
public:
    srs_error_t on_connection_established();
    srs_error_t on_dtls_alert(std::string type, std::string desc);
    srs_error_t start_play(std::string stream_uri);
    srs_error_t start_publish(std::string stream_uri);
    bool is_alive();
    void alive();
    void update_sendonly_socket(SrsUdpMuxSocket* skt);
public:
    // send rtcp
    srs_error_t send_rtcp(char *data, int nb_data);
    void check_send_nacks(SrsRtpNackForReceiver* nack, uint32_t ssrc, uint32_t& sent_nacks, uint32_t& timeout_nacks);
    srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer* rtp_queue, const uint64_t& last_send_systime, const SrsNtp& last_send_ntp);
    srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId& cid_of_subscriber);
public:
    // Simulate the NACK to drop nn packets.
    void simulate_nack_drop(int nn);
    void simulate_player_drop_packet(SrsRtpHeader* h, int nn_bytes);
    srs_error_t do_send_packet(SrsRtpPacket* pkt);
    // Directly set the status of play track, generally for init to set the default value.
    void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status);
private:
    srs_error_t on_binding_request(SrsStunPacket* r);
    // publish media capabilitiy negotiate
    srs_error_t negotiate_publish_capability(SrsRtcUserConfig* ruc, SrsRtcSourceDescription* stream_desc);
    srs_error_t generate_publish_local_sdp(SrsRequest* req, SrsSdp& local_sdp, SrsRtcSourceDescription* stream_desc, bool unified_plan);
    // play media capabilitiy negotiate
    //TODO: Use StreamDescription to negotiate and remove first negotiate_play_capability function
    srs_error_t negotiate_play_capability(SrsRtcUserConfig* ruc, std::map<uint32_t, SrsRtcTrackDescription*>& sub_relations);
    srs_error_t generate_play_local_sdp(SrsRequest* req, SrsSdp& local_sdp, SrsRtcSourceDescription* stream_desc, bool unified_plan);
    srs_error_t create_player(SrsRequest* request, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations);
    srs_error_t create_publisher(SrsRequest* request, SrsRtcSourceDescription* stream_desc);
};

class ISrsRtcHijacker
{
public:
    ISrsRtcHijacker();
    virtual ~ISrsRtcHijacker();
public:
    // Initialize the hijacker.
    virtual srs_error_t initialize() = 0;
    // When create publisher, SDP is done, DTLS is not ready.
    virtual srs_error_t on_create_publish(SrsRtcConnection* session, SrsRtcPublishStream* publisher, SrsRequest* req) = 0;
    // When start publisher by RTC, SDP and DTLS are done.
    virtual srs_error_t on_start_publish(SrsRtcConnection* session, SrsRtcPublishStream* publisher, SrsRequest* req) = 0;
    // When stop publish by RTC.
    virtual void on_stop_publish(SrsRtcConnection* session, SrsRtcPublishStream* publisher, SrsRequest* req) = 0;
    // When got RTP plaintext packet.
    virtual srs_error_t on_rtp_packet(SrsRtcConnection* session, SrsRtcPublishStream* publisher, SrsRequest* req, SrsRtpPacket* pkt) = 0;
    // When before play by RTC. (wait source to ready in cascade scenario)
    virtual srs_error_t on_before_play(SrsRtcConnection* session, SrsRequest* req) = 0;
    // When start player by RTC.
    virtual srs_error_t on_start_play(SrsRtcConnection* session, SrsRtcPlayStream* player, SrsRequest* req) = 0;
    // When stop player by RTC.
    virtual void on_stop_play(SrsRtcConnection* session, SrsRtcPlayStream* player, SrsRequest* req) = 0;
    // When start consuming for player for RTC.
    virtual srs_error_t on_start_consume(SrsRtcConnection* session, SrsRtcPlayStream* player, SrsRequest* req, SrsRtcConsumer* consumer) = 0;
};

extern ISrsRtcHijacker* _srs_rtc_hijacker;

#endif

