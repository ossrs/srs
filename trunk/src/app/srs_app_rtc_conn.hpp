//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_CONN_HPP
#define SRS_APP_RTC_CONN_HPP

#include <srs_app_async_call.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_sped.hpp>
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
#include <vector>

class SrsUdpMuxSocket;
class SrsLiveConsumer;
class SrsStunPacket;
class SrsSpedController;
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
class ISrsRtcPublishStream;
class SrsEphemeralDelta;
class SrsRtcNetworks;
class ISrsRtcNetworks;
class SrsRtcUdpNetwork;
class ISrsRtcNetwork;
class SrsRtcTcpNetwork;
class SrsStreamPublishToken;
class ISrsStreamPublishToken;
class ISrsHttpHooks;
class ISrsAppConfig;
class ISrsStatistic;
class ISrsExecRtcAsyncTask;
class ISrsSrtSourceManager;
class ISrsLiveSourceManager;
class SrsRtcPublisherNegotiator;
class ISrsRtcPublisherNegotiator;
class SrsRtcPlayerNegotiator;
class ISrsRtcPlayerNegotiator;
class ISrsAppFactory;
class ISrsCoroutine;
class ISrsDtlsCertificate;
class SrsRtcRecvTrack;
class ISrsRtcPlayStream;
class ISrsRtcFormat;
class ISrsRtcBlackhole;
class ISrsRand;

const uint8_t kSR = 200;
const uint8_t kRR = 201;
const uint8_t kSDES = 202;
const uint8_t kBye = 203;
const uint8_t kApp = 204;

// @see: https://tools.ietf.org/html/rfc4585#section-6.1
const uint8_t kRtpFb = 205;
const uint8_t kPsFb = 206;
const uint8_t kXR = 207;

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
    // The SPED controller of the DTLS handshake, or NULL when SPED is not negotiated.
    virtual SrsSpedController *sped() = 0;
    // Feed DTLS a datagram that came inside a STUN message, which SPED has already ACKed.
    virtual srs_error_t on_embedded_dtls(char *data, int nb_data) = 0;

public:
    // Encrypts in place, so the buffer needs some room after the packet. The nb_cipher is the
    // plaintext size in and the cipher size out.
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher) = 0;
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher) = 0;
    // Decrypts in place. The nb_plaintext is the cipher size in and the plaintext size out.
    virtual srs_error_t unprotect_rtp(void *packet, int *nb_plaintext) = 0;
    virtual srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext) = 0;
};

class SrsSecurityTransport : public ISrsRtcTransport
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcNetwork *network_;
    ISrsDtls *dtls_;
    ISrsSRTP *srtp_;
    bool handshake_done_;
    ISrsRtcBlackhole *blackhole_;
    std::string remote_fingerprint_algo_;
    std::string remote_fingerprint_;

public:
    SrsSecurityTransport(ISrsRtcNetwork *s);
    virtual ~SrsSecurityTransport();

    srs_error_t initialize(SrsSessionConfig *cfg);
    srs_error_t start_active_handshake();
    srs_error_t on_dtls(char *data, int nb_data);
    srs_error_t on_dtls_alert(std::string type, std::string desc);
    SrsSpedController *sped();
    srs_error_t on_embedded_dtls(char *data, int nb_data);

public:
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);

public:
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t write_dtls_data(void *data, int size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t srtp_initialize();
    srs_error_t verify_peer_fingerprint();
    void on_srtp_received();
};

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
    virtual SrsSpedController *sped();
    virtual srs_error_t on_embedded_dtls(char *data, int nb_data);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t write_dtls_data(void *data, int size);

public:
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
};

class ISrsRtcPliWorkerHandler
{
public:
    ISrsRtcPliWorkerHandler();
    virtual ~ISrsRtcPliWorkerHandler();

public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid) = 0;
};

class ISrsRtcPliWorker : public ISrsCoroutineHandler
{
public:
    ISrsRtcPliWorker();
    virtual ~ISrsRtcPliWorker();

public:
    virtual srs_error_t start() = 0;
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid) = 0;
    virtual void stop() = 0;
};

class SrsRtcPliWorker : public ISrsRtcPliWorker
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    ISrsCond *wait_;
    ISrsRtcPliWorkerHandler *handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<uint32_t, SrsContextId>
        plis_;

public:
    SrsRtcPliWorker(ISrsRtcPliWorkerHandler *h);
    virtual ~SrsRtcPliWorker();

public:
    virtual srs_error_t start();
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid);
    virtual void stop();

public:
    virtual srs_error_t cycle();
};

class SrsRtcAsyncCallOnStop : public ISrsAsyncCallTask
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    ISrsRequest *req_;
    // The stream when the player stopped, which the stats may drop before the hook runs.
    SrsStatisticStreamSnapshot stream_;
    ISrsHttpHooks *hooks_;
    ISrsContext *context_;
    ISrsAppConfig *config_;

public:
    SrsRtcAsyncCallOnStop(SrsContextId c, ISrsRequest *r, const SrsStatisticStreamSnapshot &stream);
    virtual ~SrsRtcAsyncCallOnStop();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

class ISrsRtcPlayStream : public ISrsCoroutineHandler, public ISrsRtcPliWorkerHandler, public ISrsRtcSourceChangeCallback
{
public:
    ISrsRtcPlayStream();
    virtual ~ISrsRtcPlayStream();

public:
    virtual srs_error_t initialize(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations) = 0;
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
    virtual void set_all_tracks_status(bool status) = 0;
    virtual srs_error_t on_rtcp(SrsRtcpCommon *rtcp) = 0;
};

class SrsRtcPlayStream : public ISrsRtcPlayStream, public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSharedTimer *shared_timer_;
    // Held by the timer while it sends, which may yield, so the stream is not freed meanwhile.
    srs_mutex_t lock_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsExecRtcAsyncTask *exec_;
    ISrsExpire *expire_;
    ISrsRtcPacketSender *sender_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    ISrsCoroutine *trd_;
    ISrsRtcPliWorker *pli_worker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    SrsSharedPtr<SrsRtcSource> source_;
    // Keyed by the publisher's SSRC, not the player's.
    std::map<uint32_t, SrsRtcAudioSendTrack *> audio_tracks_;
    std::map<uint32_t, SrsRtcVideoSendTrack *> video_tracks_;
    SrsErrorPithyPrint *nack_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Fast cache for tracks, keyed by the SSRC of the packets from the source. There is no RTX slot,
    // because RTX is unwrapped before a packet reaches the source, and the RTX packet for a NACK
    // is built by the send track itself and sent directly, never looked up here.
    uint32_t cache_ssrc0_;
    uint32_t cache_ssrc1_;
    uint32_t cache_ssrc2_;
    SrsRtcSendTrack *cache_track0_;
    SrsRtcSendTrack *cache_track1_;
    SrsRtcSendTrack *cache_track2_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int mw_msgs_;
    bool realtime_;
    bool nack_enabled_;
    bool nack_no_copy_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_started_;

public:
    SrsRtcPlayStream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketSender *sender, const SrsContextId &cid);
    virtual ~SrsRtcPlayStream();

public:
    srs_error_t initialize(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations);

public:
    void on_stream_change(SrsRtcSourceDescription *desc);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_stream_changed(SrsRtcSourceDescription *desc);

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
    void set_all_tracks_status(bool status);

public:
    srs_error_t on_rtcp(SrsRtcpCommon *rtcp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_rtcp_xr(SrsRtcpXr *rtcp);
    srs_error_t on_rtcp_nack(SrsRtcpNack *rtcp);
    srs_error_t on_rtcp_ps_feedback(SrsRtcpFbCommon *rtcp);
    srs_error_t on_rtcp_rr(SrsRtcpRR *rtcp);
    SrsRtcSendTrack *find_track(uint32_t ssrc, bool is_audio);
    uint32_t get_video_publish_ssrc(uint32_t play_ssrc);

public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

class ISrsRtcRtcpSender
{
public:
    ISrsRtcRtcpSender();
    virtual ~ISrsRtcRtcpSender();

public:
    virtual bool is_sender_started() = 0;
    virtual srs_error_t send_rtcp_rr() = 0;
    virtual srs_error_t send_rtcp_xr_rrtr() = 0;
    virtual void report_frame_stats() = 0;

public:
    virtual bool is_sender_twcc_enabled() = 0;
    virtual srs_error_t send_periodic_twcc() = 0;
};

class ISrsRtcPublishRtcpTimer : public ISrsFastTimerHandler
{
public:
    ISrsRtcPublishRtcpTimer();
    virtual ~ISrsRtcPublishRtcpTimer();

public:
    virtual srs_error_t initialize() = 0;
};

class SrsRtcPublishRtcpTimer : public ISrsRtcPublishRtcpTimer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSharedTimer *shared_timer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcRtcpSender *sender_;
    srs_mutex_t lock_;

public:
    SrsRtcPublishRtcpTimer(ISrsRtcRtcpSender *sender);
    virtual ~SrsRtcPublishRtcpTimer();

public:
    virtual srs_error_t initialize();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

class ISrsRtcPublishTwccTimer : public ISrsFastTimerHandler
{
public:
    ISrsRtcPublishTwccTimer();
    virtual ~ISrsRtcPublishTwccTimer();

public:
    virtual srs_error_t initialize() = 0;
};

class SrsRtcPublishTwccTimer : public ISrsRtcPublishTwccTimer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSharedTimer *shared_timer_;
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcRtcpSender *sender_;
    srs_mutex_t lock_;

public:
    SrsRtcPublishTwccTimer(ISrsRtcRtcpSender *sender);
    virtual ~SrsRtcPublishTwccTimer();

public:
    virtual srs_error_t initialize();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

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
    // The stream when the publisher stopped, which the stats may drop before the hook runs.
    SrsStatisticStreamSnapshot stream_;

public:
    SrsRtcAsyncCallOnUnpublish(SrsContextId c, ISrsRequest *r, const SrsStatisticStreamSnapshot &stream);
    virtual ~SrsRtcAsyncCallOnUnpublish();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

class ISrsRtcPublishStream : public ISrsRtpPacketDecodeHandler, public ISrsRtcPliWorkerHandler, public ISrsRtcRtcpSender
{
public:
    ISrsRtcPublishStream();
    virtual ~ISrsRtcPublishStream();

public:
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid) = 0;
    virtual const SrsContextId &context_id() = 0;
    virtual srs_error_t initialize(ISrsRequest *req, SrsRtcSourceDescription *stream_desc) = 0;
    virtual srs_error_t on_rtcp(SrsRtcpCommon *rtcp) = 0;
    virtual srs_error_t on_rtp_cipher(char *buf, int nb_buf, bool *dropped) = 0;
    virtual srs_error_t on_rtp_plaintext(char *buf, int nb_buf) = 0;
    virtual srs_error_t start() = 0;
    virtual srs_error_t check_send_nacks() = 0;
    virtual void simulate_nack_drop(int nn) = 0;
    virtual void set_all_tracks_status(bool status) = 0;
};

class SrsRtcPublishStream : public ISrsRtcPublishStream
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsStatistic *stat_;
    ISrsAppConfig *config_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsLiveSourceManager *live_sources_;
    ISrsSrtSourceManager *srt_sources_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsExecRtcAsyncTask *exec_;
    ISrsExpire *expire_;
    ISrsRtcPacketReceiver *receiver_;
    ISrsCircuitBreaker *circuit_breaker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsRtcPublishRtcpTimer;
    friend class SrsRtcPublishTwccTimer;
    ISrsRtcPublishRtcpTimer *timer_rtcp_;
    ISrsRtcPublishTwccTimer *timer_twcc_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    int nn_audio_frames_;
    int nn_video_frames_;
    ISrsRtcFormat *format_;
    ISrsRtcPliWorker *pli_worker_;
    SrsErrorPithyPrint *twcc_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint16_t pt_to_drop_;
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
    int nn_simulate_nack_drop_publisher_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsRtcAudioRecvTrack *> audio_tracks_;
    std::vector<SrsRtcVideoRecvTrack *> video_tracks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t cache_ssrc0_;
    bool cache_is_audio0_;
    uint32_t cache_ssrc1_;
    bool cache_is_audio1_;
    uint32_t cache_ssrc2_;
    bool cache_is_audio2_;
    SrsRtcRecvTrack *cache_track0_;
    SrsRtcRecvTrack *cache_track1_;
    SrsRtcRecvTrack *cache_track2_;
    // Fast cache for the RTX SSRC of each video track, kept apart from the media cache: RTX packets are rare beside
    // media, so they must never take a media slot, and a hit here tells the caller the packet is RTX.
    uint32_t cache_rtx_ssrc0_;
    uint32_t cache_rtx_ssrc1_;
    SrsRtcRecvTrack *cache_rtx_track0_;
    SrsRtcRecvTrack *cache_rtx_track1_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int twcc_id_;
    uint8_t twcc_fb_count_;
    SrsRtcpTWCC *rtcp_twcc_;
    SrsRtpExtensionTypes extension_types_;
    bool is_sender_started_;
    srs_utime_t last_time_send_twcc_;

public:
    SrsRtcPublishStream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketReceiver *receiver, const SrsContextId &cid);
    virtual ~SrsRtcPublishStream();

public:
    srs_error_t initialize(ISrsRequest *req, SrsRtcSourceDescription *stream_desc);
    srs_error_t start();
    void stop();
    void set_all_tracks_status(bool status);
    virtual const SrsContextId &context_id();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_sender_started();
    bool is_sender_twcc_enabled();
    srs_error_t send_rtcp_rr();
    srs_error_t send_rtcp_xr_rrtr();
    void report_frame_stats();

public:
    srs_error_t on_rtp_cipher(char *buf, int nb_buf, bool *dropped);
    srs_error_t on_rtp_plaintext(char *buf, int nb_buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_on_rtp_plaintext(SrsRtpPacket *&pkt, SrsBuffer *buf);
    void update_rtp_frame_stats(bool is_audio);

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
    SrsRtcRecvTrack *find_track(uint32_t ssrc, bool &is_audio, bool &is_rtx);
    SrsRtcAudioRecvTrack *get_audio_track(uint32_t ssrc);
    SrsRtcVideoRecvTrack *get_video_track(uint32_t ssrc);
    void update_rtt(uint32_t ssrc, int rtt);
    void update_send_report_time(uint32_t ssrc, const SrsNtp &ntp, uint32_t rtp_time);
};

class ISrsRtcConnectionNackTimerHandler
{
public:
    ISrsRtcConnectionNackTimerHandler();
    virtual ~ISrsRtcConnectionNackTimerHandler();

public:
    virtual srs_error_t do_check_send_nacks() = 0;
};

class ISrsRtcConnectionNackTimer : public ISrsFastTimerHandler
{
public:
    ISrsRtcConnectionNackTimer();
    virtual ~ISrsRtcConnectionNackTimer();

public:
    virtual srs_error_t initialize() = 0;
};

class SrsRtcConnectionNackTimer : public ISrsRtcConnectionNackTimer
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

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

class ISrsExecRtcAsyncTask
{
public:
    ISrsExecRtcAsyncTask();
    virtual ~ISrsExecRtcAsyncTask();

public:
    virtual srs_error_t exec_rtc_async_work(ISrsAsyncCallTask *t) = 0;
};

class ISrsRtcConnection : public ISrsResource,
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
    virtual srs_error_t on_dtls_handshake_done() = 0;
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
    virtual srs_error_t on_rtp_cipher(char *data, int nb_data, bool *dropped) = 0;
    virtual srs_error_t on_rtp_plaintext(char *data, int nb_data) = 0;
    virtual srs_error_t on_rtcp(char *data, int nb_data) = 0;
    virtual srs_error_t on_binding_request(SrsStunPacket *r, std::string &ice_pwd) = 0;
    virtual ISrsRtcNetwork *udp() = 0;
    virtual ISrsRtcNetwork *tcp() = 0;
    virtual void alive() = 0;
    virtual bool is_alive() = 0;
    virtual bool is_disposing() = 0;
    virtual void switch_to_context() = 0;
    virtual srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp) = 0;
    virtual srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp) = 0;
    virtual void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status) = 0;
    virtual srs_error_t generate_local_sdp(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, std::string &username) = 0;
    virtual void set_remote_sdp(const SrsSdp &sdp) = 0;
    virtual void set_local_sdp(const SrsSdp &sdp) = 0;
    virtual void set_state_as_waiting_stun() = 0;
    virtual srs_error_t initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username) = 0;
    virtual std::string username() = 0;
    virtual std::string token() = 0;
    virtual void set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> publish_token) = 0;
    virtual void simulate_nack_drop(int nn) = 0;
    // Lose the next stun STUN binding responses, the next dtls DTLS datagrams and the next finished DTLS 1.2
    // last flights to the client.
    virtual void simulate_handshake_drop(int stun, int dtls, int finished) = 0;
};

// ISrsResource is not a virtual base, so a static_cast from it is allowed, see
// https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
class SrsRtcConnection : public ISrsRtcConnection, public ISrsSpedSource, public ISrsSpedHandler
{
    friend class SrsSecurityTransport;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCircuitBreaker *circuit_breaker_;
    ISrsResourceManager *conn_manager_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsAppConfig *config_;
    ISrsDtlsCertificate *dtls_certificate_;
    ISrsAppFactory *app_factory_;
    ISrsRtcBlackhole *blackhole_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcConnectionNackTimer *timer_nack_;
    ISrsExecRtcAsyncTask *exec_;
    ISrsRtcPublisherNegotiator *publisher_negotiator_;
    ISrsRtcPlayerNegotiator *player_negotiator_;

public:
    bool disposing_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    iovec *cache_iov_;
    SrsBuffer *cache_buffer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<std::string, ISrsRtcPlayStream *> players_;
    std::map<uint32_t, ISrsRtcPlayStream *> players_ssrc_map_;
    std::map<std::string, ISrsRtcPublishStream *> publishers_;
    std::map<uint32_t, ISrsRtcPublishStream *> publishers_ssrc_map_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The local:remote username, such as m5x0n128:jvOm where local name is m5x0n128.
    std::string username_;
    // The random token to verify the WHIP DELETE request etc.
    std::string token_;
    ISrsRand *rand_;
    ISrsRtcNetworks *networks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // TODO: FIXME: Rename it.
    // The session is kept alive by STUN binding requests.
    srs_utime_t session_timeout_;
    // TODO: FIXME: Rename it.
    srs_utime_t last_stun_time_;
    // Whether the players start only when SPED is complete.
    bool players_wait_sped_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;
    ISrsRequest *req_;
    SrsSdp remote_sdp_;
    SrsSdp local_sdp_;
    SrsSharedPtr<ISrsStreamPublishToken> publish_token_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int twcc_id_;
    int nn_simulate_nack_drop_player_;
    SrsErrorPithyPrint *pli_epp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool nack_enabled_;

public:
    SrsRtcConnection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid);
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsRtcConnection();

public:
    virtual void on_before_dispose(ISrsResource *c);
    virtual void on_disposing(ISrsResource *c);

public:
    // TODO: FIXME: save only connection info.
    SrsSdp *get_local_sdp();
    void set_local_sdp(const SrsSdp &sdp);
    SrsSdp *get_remote_sdp();
    void set_remote_sdp(const SrsSdp &sdp);
    void set_state_as_waiting_stun();
    std::string username();
    std::string token();
    void set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> publish_token);

public:
    virtual ISrsKbpsDelta *delta();

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();

public:
    virtual void expire();

public:
    void switch_to_context();
    const SrsContextId &context_id();

public:
    srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    srs_error_t generate_local_sdp(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, std::string &username);

public:
    // Set the local SDP before initialize, which uses it to set up DTLS.
    srs_error_t initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username);
    srs_error_t on_rtp_cipher(char *data, int nb_data, bool *dropped);
    srs_error_t on_rtp_plaintext(char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t find_publisher(char *buf, int size, ISrsRtcPublishStream **ppublisher);

public:
    srs_error_t on_rtcp(char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t dispatch_rtcp(SrsRtcpCommon *rtcp);

public:
    srs_error_t on_rtcp_feedback_twcc(char *buf, int nb_buf);
    srs_error_t on_rtcp_feedback_remb(SrsRtcpFbCommon *rtcp);

public:
    // A stream that fails to start ends the session, so the client never keeps a connected peer
    // that doesn't publish or play.
    srs_error_t on_dtls_handshake_done();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t start_streams();
    srs_error_t start_players();
    bool sped_waits_for_peer();

public:
    srs_error_t on_dtls_alert(std::string type, std::string desc);
    bool is_alive();
    bool is_disposing();
    void alive();

public:
    ISrsRtcNetwork *udp();
    ISrsRtcNetwork *tcp();

    // Interface ISrsSpedSource
public:
    virtual SrsSpedController *sped();

    // Interface ISrsSpedHandler
public:
    virtual void on_sped_complete();

public:
    srs_error_t send_rtcp(char *data, int nb_data);
    void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);

public:
    virtual srs_error_t do_check_send_nacks();

public:
    void simulate_nack_drop(int nn);
    void simulate_handshake_drop(int stun, int dtls, int finished);
    void simulate_player_drop_packet(SrsRtpHeader *h, int nn_bytes);
    srs_error_t do_send_packet(SrsRtpPacket *pkt);
    void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status);

public:
    srs_error_t on_binding_request(SrsStunPacket *r, std::string &ice_pwd);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, std::map<uint32_t, SrsRtcTrackDescription *> &play_sub_relations);
    srs_error_t create_player(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations);
    srs_error_t create_publisher(ISrsRequest *request, SrsRtcSourceDescription *stream_desc);
};

class ISrsRtcPublisherNegotiator
{
public:
    ISrsRtcPublisherNegotiator();
    virtual ~ISrsRtcPublisherNegotiator();

public:
    virtual srs_error_t negotiate_publish_capability(SrsRtcUserConfig *ruc, SrsRtcSourceDescription *stream_desc) = 0;
    virtual srs_error_t generate_publish_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video) = 0;
};

// The sess-id of the SDP o= line, which JSEP requires to be a number less than 2^63-1.
extern std::string srs_rtc_sdp_session_id(ISrsRand *rand);

class SrsRtcPublisherNegotiator : public ISrsRtcPublisherNegotiator
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsRand *rand_;

public:
    SrsRtcPublisherNegotiator();
    virtual ~SrsRtcPublisherNegotiator();

public:
    srs_error_t negotiate_publish_capability(SrsRtcUserConfig *ruc, SrsRtcSourceDescription *stream_desc);
    srs_error_t generate_publish_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t generate_publish_local_sdp_for_audio(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc);
    srs_error_t generate_publish_local_sdp_for_video(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan);
};

class ISrsRtcPlayerNegotiator
{
public:
    ISrsRtcPlayerNegotiator();
    virtual ~ISrsRtcPlayerNegotiator();

public:
    virtual srs_error_t negotiate_play_capability(SrsRtcUserConfig *ruc, std::map<uint32_t, SrsRtcTrackDescription *> &sub_relations) = 0;
    virtual srs_error_t generate_play_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video) = 0;
};

class SrsRtcPlayerNegotiator : public ISrsRtcPlayerNegotiator
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsRtcSSRCGenerator *ssrc_generator_;
    ISrsRand *rand_;

public:
    SrsRtcPlayerNegotiator();
    virtual ~SrsRtcPlayerNegotiator();

public:
    // TODO: Use StreamDescription to negotiate and remove first negotiate_play_capability function
    srs_error_t negotiate_play_capability(SrsRtcUserConfig *ruc, std::map<uint32_t, SrsRtcTrackDescription *> &sub_relations);
    srs_error_t generate_play_local_sdp(ISrsRequest *req, SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, bool audio_before_video);
    srs_error_t generate_play_local_sdp_for_audio(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, std::string cname);
    srs_error_t generate_play_local_sdp_for_video(SrsSdp &local_sdp, SrsRtcSourceDescription *stream_desc, bool unified_plan, std::string cname);
};

#endif
