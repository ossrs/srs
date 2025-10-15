//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_CONN_HPP
#define SRS_APP_RTC_CONN_HPP

#include <srs_app_async_call.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_core.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_rtc_queue.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_st.hpp>

#include <map>
#include <string>
#include <sys/socket.h>
#include <vector>

class SrsUdpMuxSocket;
class SrsLiveConsumer;
class SrsStunPacket;
class SrsServer;
class SrsRtcConnection;
class SrsMediaPacket;
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
class SrsEphemeralDelta;
class SrsRtcNetworks;
class SrsRtcUdpNetwork;
class ISrsRtcNetwork;
class SrsRtcTcpNetwork;
class SrsStreamPublishToken;
class ISrsHttpHooks;
class ISrsAppConfig;
class ISrsStatistic;
class ISrsExecRtcAsyncTask;
class ISrsSrtSourceManager;
class ISrsLiveSourceManager;
class SrsRtcPublisherNegotiator;
class SrsRtcPlayerNegotiator;

const uint8_t kSR = 200;
const uint8_t kRR = 201;
const uint8_t kSDES = 202;
const uint8_t kBye = 203;
const uint8_t kApp = 204;

// @see: https://tools.ietf.org/html/rfc4585#section-6.1
const uint8_t kRtpFb = 205;
const uint8_t kPsFb = 206;
const uint8_t kXR = 207;

// The transport for RTC connection.
class ISrsRtcTransport : public ISrsDtlsCallback
{
public:
    ISrsRtcTransport();
    virtual ~ISrsRtcTransport();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg) = 0;
    virtual srs_error_t start_active_handshake() = 0;
    virtual srs_error_t on_dtls(char *data, int nb_data) = 0;
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;

public:
    // Encrypt the packet(paintext) to cipher, which is aso the packet ptr.
    // The nb_cipher should be initialized to the size of cipher, with some paddings.
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher) = 0;
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher) = 0;
    // Decrypt the packet(cipher) to plaintext, which is also the packet ptr.
    // The nb_plaintext should be initialized to the size of cipher.
    virtual srs_error_t unprotect_rtp(void *packet, int *nb_plaintext) = 0;
    virtual srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext) = 0;
};

// The security transport, use DTLS/SRTP to protect the data.
class SrsSecurityTransport : public ISrsRtcTransport
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcNetwork *network_;
    ISrsDtls *dtls_;
    ISrsSRTP *srtp_;
    bool handshake_done_;

public:
    SrsSecurityTransport(ISrsRtcNetwork *s);
    virtual ~SrsSecurityTransport();

    srs_error_t initialize(SrsSessionConfig *cfg);
    // When play role of dtls client, it send handshake.
    srs_error_t start_active_handshake();
    srs_error_t on_dtls(char *data, int nb_data);
    srs_error_t on_dtls_alert(std::string type, std::string desc);

public:
    // Encrypt the packet(paintext) to cipher, which is aso the packet ptr.
    // The nb_cipher should be initialized to the size of cipher, with some paddings.
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    // Decrypt the packet(cipher) to plaintext, which is also the packet ptr.
    // The nb_plaintext should be initialized to the size of cipher.
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
    // implement ISrsDtlsCallback
public:
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t write_dtls_data(void *data, int size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t srtp_initialize();
};

// Semi security transport, setup DTLS and SRTP, with SRTP decrypt, without SRTP encrypt.
class SrsSemiSecurityTransport : public SrsSecurityTransport
{
public:
    SrsSemiSecurityTransport(ISrsRtcNetwork *s);
    virtual ~SrsSemiSecurityTransport();

public:
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
};

// Plaintext transport, without DTLS or SRTP.
class SrsPlaintextTransport : public ISrsRtcTransport
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcNetwork *network_;

public:
    SrsPlaintextTransport(ISrsRtcNetwork *s);
    virtual ~SrsPlaintextTransport();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t write_dtls_data(void *data, int size);

public:
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
};

// The handler for PLI worker coroutine.
class ISrsRtcPliWorkerHandler
{
public:
    ISrsRtcPliWorkerHandler();
    virtual ~ISrsRtcPliWorkerHandler();

public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid) = 0;
};

// A worker coroutine to request the PLI.
class SrsRtcPliWorker : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    ISrsCond *wait_;
    ISrsRtcPliWorkerHandler *handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Key is SSRC, value is the CID of subscriber which requests PLI.
    std::map<uint32_t, SrsContextId>
        plis_;

public:
    SrsRtcPliWorker(ISrsRtcPliWorkerHandler *h);
    virtual ~SrsRtcPliWorker();

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
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    ISrsRequest *req_;
    ISrsHttpHooks *hooks_;
    ISrsContext *context_;
    ISrsAppConfig *config_;

public:
    SrsRtcAsyncCallOnStop(SrsContextId c, ISrsRequest *r);
    virtual ~SrsRtcAsyncCallOnStop();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// A RTC play stream, client pull and play stream from SRS.
class SrsRtcPlayStream : public ISrsCoroutineHandler, public ISrsReloadHandler, public ISrsRtcPliWorkerHandler, public ISrsRtcSourceChangeCallback
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsExecRtcAsyncTask *exec_;
    ISrsExpire *expire_;
    ISrsRtcPacketSender *sender_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    SrsFastCoroutine *trd_;
    SrsRtcPliWorker *pli_worker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    SrsSharedPtr<SrsRtcSource> source_;
    // key: publish_ssrc, value: send track to process rtp/rtcp
    std::map<uint32_t, SrsRtcAudioSendTrack *> audio_tracks_;
    std::map<uint32_t, SrsRtcVideoSendTrack *> video_tracks_;
    // The pithy print for special stage.
    SrsErrorPithyPrint *nack_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Fast cache for tracks.
    uint32_t cache_ssrc0_;
    uint32_t cache_ssrc1_;
    uint32_t cache_ssrc2_;
    SrsRtcSendTrack *cache_track0_;
    SrsRtcSendTrack *cache_track1_;
    SrsRtcSendTrack *cache_track2_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // For merged-write messages.
    int mw_msgs_;
    bool realtime_;
    // Whether enabled nack.
    bool nack_enabled_;
    bool nack_no_copy_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Whether player started.
    bool is_started_;

public:
    SrsRtcPlayStream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketSender *sender, const SrsContextId &cid);
    virtual ~SrsRtcPlayStream();

public:
    srs_error_t initialize(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations);
    // Interface ISrsRtcSourceChangeCallback
public:
    void on_stream_change(SrsRtcSourceDescription *desc);

public:
    virtual const SrsContextId &context_id();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t send_packet(SrsRtpPacket *&pkt);

public:
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);

public:
    srs_error_t on_rtcp(SrsRtcpCommon *rtcp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_rtcp_xr(SrsRtcpXr *rtcp);
    srs_error_t on_rtcp_nack(SrsRtcpNack *rtcp);
    srs_error_t on_rtcp_ps_feedback(SrsRtcpFbCommon *rtcp);
    srs_error_t on_rtcp_rr(SrsRtcpRR *rtcp);
    uint32_t get_video_publish_ssrc(uint32_t play_ssrc);
    // Interface ISrsRtcPliWorkerHandler
public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);
};

// The RTC RTCP sender interface.
class ISrsRtcRtcpSender
{
public:
    ISrsRtcRtcpSender();
    virtual ~ISrsRtcRtcpSender();

public:
    virtual bool is_sender_started() = 0;
    virtual srs_error_t send_rtcp_rr() = 0;
    virtual srs_error_t send_rtcp_xr_rrtr() = 0;

public:
    virtual bool is_sender_twcc_enabled() = 0;
    virtual srs_error_t send_periodic_twcc() = 0;
};

// A fast timer for publish stream, for RTCP feedback.
class SrsRtcPublishRtcpTimer : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcRtcpSender *sender_;
    srs_mutex_t lock_;

public:
    SrsRtcPublishRtcpTimer(ISrsRtcRtcpSender *sender);
    virtual ~SrsRtcPublishRtcpTimer();
    // interface ISrsFastTimerHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

// A fast timer for publish stream, for TWCC feedback.
class SrsRtcPublishTwccTimer : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcRtcpSender *sender_;
    srs_mutex_t lock_;

public:
    SrsRtcPublishTwccTimer(ISrsRtcRtcpSender *sender);
    virtual ~SrsRtcPublishTwccTimer();
    // interface ISrsFastTimerHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

// the rtc on_unpublish async call.
class SrsRtcAsyncCallOnUnpublish : public ISrsAsyncCallTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHttpHooks *hooks_;
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    ISrsRequest *req_;

public:
    SrsRtcAsyncCallOnUnpublish(SrsContextId c, ISrsRequest *r);
    virtual ~SrsRtcAsyncCallOnUnpublish();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// A RTC publish stream, client push and publish stream to SRS.
class SrsRtcPublishStream : public ISrsRtpPacketDecodeHandler, public ISrsRtcPublishStream, public ISrsRtcPliWorkerHandler, public ISrsRtcRtcpSender
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsExecRtcAsyncTask *exec_;
    ISrsExpire *expire_;
    ISrsRtcPacketReceiver *receiver_;
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsAppConfig *config_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsLiveSourceManager *live_sources_;
    ISrsSrtSourceManager *srt_sources_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsRtcPublishRtcpTimer;
    friend class SrsRtcPublishTwccTimer;
    SrsRtcPublishRtcpTimer *timer_rtcp_;
    SrsRtcPublishTwccTimer *timer_twcc_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    uint64_t nn_audio_frames_;
    SrsRtcPliWorker *pli_worker_;
    SrsErrorPithyPrint *twcc_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint16_t pt_to_drop_;
    // Whether enabled nack.
    bool nack_enabled_;
    bool nack_no_copy_;
    bool twcc_enabled_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool request_keyframe_;
    SrsErrorPithyPrint *pli_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    SrsSharedPtr<SrsRtcSource> source_;
    // Simulators.
    int nn_simulate_nack_drop_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // track vector
    std::vector<SrsRtcAudioRecvTrack *>
        audio_tracks_;
    std::vector<SrsRtcVideoRecvTrack *> video_tracks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int twcc_id_;
    uint8_t twcc_fb_count_;
    SrsRtcpTWCC rtcp_twcc_;
    SrsRtpExtensionTypes extension_types_;
    bool is_sender_started_;
    srs_utime_t last_time_send_twcc_;

public:
    SrsRtcPublishStream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketReceiver *receiver, const SrsContextId &cid);
    virtual ~SrsRtcPublishStream();

public:
    srs_error_t initialize(ISrsRequest *req, SrsRtcSourceDescription *stream_desc);
    srs_error_t start();
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
    virtual const SrsContextId &context_id();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_sender_started();
    bool is_sender_twcc_enabled();
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();

public:
    srs_error_t on_rtp_cipher(char *buf, int nb_buf);
    srs_error_t on_rtp_plaintext(char *buf, int nb_buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_on_rtp_plaintext(SrsRtpPacket *&pkt, SrsBuffer *buf);

public:
    srs_error_t check_send_nacks();

public:
    virtual void on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t send_periodic_twcc();

public:
    srs_error_t on_rtcp(SrsRtcpCommon *rtcp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_rtcp_sr(SrsRtcpSR *rtcp);
    srs_error_t on_rtcp_xr(SrsRtcpXr *rtcp);

public:
    void request_keyframe(uint32_t ssrc, SrsContextId cid);
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);

public:
    void simulate_nack_drop(int nn);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void simulate_drop_packet(SrsRtpHeader *h, int nn_bytes);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_twcc(uint16_t sn);
    SrsRtcAudioRecvTrack *get_audio_track(uint32_t ssrc);
    SrsRtcVideoRecvTrack *get_video_track(uint32_t ssrc);
    void update_rtt(uint32_t ssrc, int rtt);
    void update_send_report_time(uint32_t ssrc, const SrsNtp &ntp, uint32_t rtp_time);
};

// The handler for RTC connection nack timer.
class ISrsRtcConnectionNackTimerHandler
{
public:
    ISrsRtcConnectionNackTimerHandler();
    virtual ~ISrsRtcConnectionNackTimerHandler();

public:
    virtual srs_error_t do_check_send_nacks() = 0;
};

// A fast timer for conntion, for NACK feedback.
class SrsRtcConnectionNackTimer : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSharedTimer *shared_timer_;
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcConnectionNackTimerHandler *handler_;
    srs_mutex_t lock_;

public:
    SrsRtcConnectionNackTimer(ISrsRtcConnectionNackTimerHandler *handler);
    virtual ~SrsRtcConnectionNackTimer();

public:
    virtual srs_error_t initialize();

    // interface ISrsFastTimerHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

// The interface for RTC async task.
class ISrsExecRtcAsyncTask
{
public:
    ISrsExecRtcAsyncTask();
    virtual ~ISrsExecRtcAsyncTask();

public:
    virtual srs_error_t exec_rtc_async_work(ISrsAsyncCallTask *t) = 0;
};

// The interface for RTC connection.
class ISrsRtcConnection : public ISrsResource, // It's a resource.
                          public ISrsDisposingHandler,
                          public ISrsExpire,
                          public ISrsRtcPacketSender,
                          public ISrsRtcPacketReceiver,
                          public ISrsRtcConnectionNackTimerHandler
{
public:
    ISrsRtcConnection();
    virtual ~ISrsRtcConnection();

public:
    // DTLS callbacks.
    virtual srs_error_t on_dtls_handshake_done() = 0;
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
    // RTP/RTCP packet handling.
    virtual srs_error_t on_rtp_cipher(char *data, int nb_data) = 0;
    virtual srs_error_t on_rtp_plaintext(char *data, int nb_data) = 0;
    virtual srs_error_t on_rtcp(char *data, int nb_data) = 0;
    // STUN binding request.
    virtual srs_error_t on_binding_request(SrsStunPacket *r, std::string &ice_pwd) = 0;
    // Network access.
    virtual ISrsRtcNetwork *udp() = 0;
    virtual ISrsRtcNetwork *tcp() = 0;
    // Keep alive.
    virtual void alive() = 0;
    virtual bool is_alive() = 0;
    virtual bool is_disposing() = 0;
    // Context switching.
    virtual void switch_to_context() = 0;
    // Session management.
    virtual srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp) = 0;
    virtual srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp) = 0;
    virtual void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status) = 0;
    // SDP management.
    virtual void set_remote_sdp(const SrsSdp &sdp) = 0;
    virtual void set_local_sdp(const SrsSdp &sdp) = 0;
    virtual void set_state_as_waiting_stun() = 0;
    // Initialization.
    virtual srs_error_t initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username) = 0;
    // Username and token access.
    virtual std::string username() = 0;
    virtual std::string token() = 0;
    virtual void set_publish_token(SrsSharedPtr<SrsStreamPublishToken> publish_token) = 0;
    // Simulation for testing.
    virtual void simulate_nack_drop(int nn) = 0;
};

// A RTC Peer Connection, SDP level object.
//
// For performance, we use non-public from resource,
// see https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
class SrsRtcConnection : public ISrsRtcConnection
{
    friend class SrsSecurityTransport;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCircuitBreaker *circuit_breaker_;
    ISrsResourceManager *conn_manager_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtcConnectionNackTimer *timer_nack_;
    ISrsExecRtcAsyncTask *exec_;
    SrsRtcPublisherNegotiator *publisher_negotiator_;
    SrsRtcPlayerNegotiator *player_negotiator_;

public:
    bool disposing_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    iovec *cache_iov_;
    SrsBuffer *cache_buffer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // key: stream id
    std::map<std::string, SrsRtcPlayStream *>
        players_;
    // key: player track's ssrc
    std::map<uint32_t, SrsRtcPlayStream *> players_ssrc_map_;
    // key: stream id
    std::map<std::string, SrsRtcPublishStream *> publishers_;
    // key: publisher track's ssrc
    std::map<uint32_t, SrsRtcPublishStream *> publishers_ssrc_map_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The local:remote username, such as m5x0n128:jvOm where local name is m5x0n128.
    std::string username_;
    // The random token to verify the WHIP DELETE request etc.
    std::string token_;
    // A group of networks, each has its own DTLS and SRTP context.
    SrsRtcNetworks *networks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // TODO: FIXME: Rename it.
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t session_timeout_;
    // TODO: FIXME: Rename it.
    srs_utime_t last_stun_time_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // For each RTC session, we use a specified cid for debugging logs.
    SrsContextId cid_;
    ISrsRequest *req_;
    SrsSdp remote_sdp_;
    SrsSdp local_sdp_;
    SrsSharedPtr<SrsStreamPublishToken> publish_token_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // twcc handler
    int twcc_id_;
    // Simulators.
    int nn_simulate_player_nack_drop_;
    // Pithy print for PLI request.
    SrsErrorPithyPrint *pli_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool nack_enabled_;

public:
    SrsRtcConnection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid);
    void assemble(); // Construct object, to avoid call function in constructor.
    virtual ~SrsRtcConnection();

    // interface ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource *c);
    virtual void on_disposing(ISrsResource *c);

public:
    // TODO: FIXME: save only connection info.
    SrsSdp *get_local_sdp();
    void set_local_sdp(const SrsSdp &sdp);
    SrsSdp *get_remote_sdp();
    void set_remote_sdp(const SrsSdp &sdp);
    // Change network to waiting stun state.
    void set_state_as_waiting_stun();
    // Get username pair for this connection, used as ID of session.
    std::string username();
    // Get the token for verify this session, for example, when delete session by WHIP API.
    std::string token();
    // Set the publish token for this session if publisher.
    void set_publish_token(SrsSharedPtr<SrsStreamPublishToken> publish_token);

public:
    virtual ISrsKbpsDelta *delta();
    // Interface ISrsResource.
public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    // Interface ISrsExpire.
public:
    virtual void expire();

public:
    void switch_to_context();
    const SrsContextId &context_id();

public:
    srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);

public:
    // Before initialize, user must set the local SDP, which is used to inititlize DTLS.
    srs_error_t initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username);
    srs_error_t on_rtp_cipher(char *data, int nb_data);
    srs_error_t on_rtp_plaintext(char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Decode the RTP header from buf, find the publisher by SSRC.
    srs_error_t
    find_publisher(char *buf, int size, SrsRtcPublishStream **ppublisher);

public:
    srs_error_t on_rtcp(char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t dispatch_rtcp(SrsRtcpCommon *rtcp);

public:
    srs_error_t on_rtcp_feedback_twcc(char *buf, int nb_buf);
    srs_error_t on_rtcp_feedback_remb(SrsRtcpFbCommon *rtcp);

public:
    srs_error_t on_dtls_handshake_done();
    srs_error_t on_dtls_alert(std::string type, std::string desc);
    bool is_alive();
    bool is_disposing();
    void alive();

public:
    ISrsRtcNetwork *udp();
    ISrsRtcNetwork *tcp();

public:
    // send rtcp
    srs_error_t send_rtcp(char *data, int nb_data);
    void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);

    // interface ISrsRtcConnectionNackTimerHandler
public:
    virtual srs_error_t do_check_send_nacks();

public:
    // Simulate the NACK to drop nn packets.
    void simulate_nack_drop(int nn);
    void simulate_player_drop_packet(SrsRtpHeader *h, int nn_bytes);
    srs_error_t do_send_packet(SrsRtpPacket *pkt);
    // Directly set the status of play track, generally for init to set the default value.
    void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status);

public:
    // Notify by specified network.
    srs_error_t on_binding_request(SrsStunPacket *r, std::string &ice_pwd);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t create_player(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations);
    srs_error_t create_publisher(ISrsRequest *request, SrsRtcSourceDescription *stream_desc);
};

// Negotiate via SDP exchange for WebRTC publisher.
class SrsRtcPublisherNegotiator
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsAppConfig *config_;

public:
    SrsRtcPublisherNegotiator();
    virtual ~SrsRtcPublisherNegotiator();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    // publish media capabilitiy negotiate
    srs_error_t negotiate_publish_capability(SrsRtcUserConfig *ruc, SrsRtcSourceDescription *stream_desc);
    srs_error_t generate_publish_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video);
    srs_error_t generate_publish_local_sdp_for_audio(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc);
    srs_error_t generate_publish_local_sdp_for_video(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan);
};

// Negotiate via SDP exchange for WebRTC player.
class SrsRtcPlayerNegotiator
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    ISrsAppConfig *config_;
    ISrsRtcSourceManager *rtc_sources_;

public:
    SrsRtcPlayerNegotiator();
    virtual ~SrsRtcPlayerNegotiator();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    // play media capabilitiy negotiate
    // TODO: Use StreamDescription to negotiate and remove first negotiate_play_capability function
    srs_error_t negotiate_play_capability(SrsRtcUserConfig *ruc, std::map<uint32_t, SrsRtcTrackDescription *> &sub_relations);
    srs_error_t generate_play_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video);
    srs_error_t generate_play_local_sdp_for_audio(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, std::string cname);
    srs_error_t generate_play_local_sdp_for_video(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, std::string cname);
};

#endif
