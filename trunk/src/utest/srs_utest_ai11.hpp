//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI11_HPP
#define SRS_UTEST_AI11_HPP

/*
#include <srs_utest_ai11.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_manual_protocol3.hpp>

// Mock ISrsResourceManager for testing SrsGbMediaTcpConn::bind_session
class MockResourceManagerForBindSession : public ISrsResourceManager
{
public:
    ISrsResource *session_to_return_;

public:
    MockResourceManagerForBindSession();
    virtual ~MockResourceManagerForBindSession();

public:
    virtual srs_error_t start();
    virtual bool empty();
    virtual size_t size();
    virtual void add(ISrsResource *conn, bool *exists = NULL);
    virtual void add_with_id(const std::string &id, ISrsResource *conn);
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn);
    virtual void add_with_name(const std::string &name, ISrsResource *conn);
    virtual ISrsResource *at(int index);
    virtual ISrsResource *find_by_id(std::string id);
    virtual ISrsResource *find_by_fast_id(uint64_t id);
    virtual ISrsResource *find_by_name(std::string name);
    virtual void remove(ISrsResource *c);
    virtual void subscribe(ISrsDisposingHandler *h);
    virtual void unsubscribe(ISrsDisposingHandler *h);
    void reset();
};

// Mock ISrsEphemeralDelta for testing SrsRtcUdpNetwork
class MockEphemeralDelta : public ISrsEphemeralDelta
{
public:
    int64_t in_bytes_;
    int64_t out_bytes_;

public:
    MockEphemeralDelta();
    virtual ~MockEphemeralDelta();

public:
    virtual void add_delta(int64_t in, int64_t out);
    virtual void remark(int64_t *in, int64_t *out);
    void reset();
};

// Mock ISrsUdpMuxSocket for testing SrsRtcUdpNetwork STUN handling
class MockUdpMuxSocket : public ISrsUdpMuxSocket
{
public:
    srs_error_t sendto_error_;
    int sendto_called_count_;
    int last_sendto_size_;
    std::string peer_ip_;
    int peer_port_;
    std::string peer_id_;
    uint64_t fast_id_;
    char *data_;
    int size_;

public:
    MockUdpMuxSocket();
    virtual ~MockUdpMuxSocket();

public:
    virtual srs_error_t sendto(void *data, int size, srs_utime_t timeout);
    virtual std::string get_peer_ip() const;
    virtual int get_peer_port() const;
    virtual std::string peer_id();
    virtual uint64_t fast_id();
    virtual SrsUdpMuxSocket *copy_sendonly();
    virtual int recvfrom(srs_utime_t timeout);
    virtual char *data();
    virtual int size();

public:
    void reset();
    void set_sendto_error(srs_error_t err);
};

// Mock DTLS implementation for testing SrsSecurityTransport
class MockDtls : public ISrsDtls
{
public:
    srs_error_t initialize_error_;
    srs_error_t start_active_handshake_error_;
    srs_error_t on_dtls_error_;
    srs_error_t get_srtp_key_error_;
    std::string last_role_;
    std::string last_version_;
    std::string recv_key_;
    std::string send_key_;
    int initialize_count_;
    int start_active_handshake_count_;
    int on_dtls_count_;
    int get_srtp_key_count_;

public:
    MockDtls();
    virtual ~MockDtls();

public:
    virtual srs_error_t initialize(std::string role, std::string version);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);

public:
    void reset();
    void set_initialize_error(srs_error_t err);
    void set_start_active_handshake_error(srs_error_t err);
    void set_on_dtls_error(srs_error_t err);
    void set_get_srtp_key_error(srs_error_t err);
    void set_srtp_keys(const std::string &recv_key, const std::string &send_key);
};

// Mock RTC Network for testing SrsSecurityTransport
class MockRtcNetwork : public ISrsRtcNetwork
{
public:
    srs_error_t on_dtls_handshake_done_error_;
    srs_error_t on_dtls_alert_error_;
    srs_error_t protect_rtp_error_;
    srs_error_t protect_rtcp_error_;
    srs_error_t write_error_;
    std::string last_alert_type_;
    std::string last_alert_desc_;
    int on_dtls_handshake_done_count_;
    int on_dtls_alert_count_;
    int protect_rtp_count_;
    int protect_rtcp_count_;
    int write_count_;
    bool is_established_;

public:
    MockRtcNetwork();
    virtual ~MockRtcNetwork();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual void set_state(SrsRtcNetworkState state);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    virtual srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);
    virtual srs_error_t on_rtp(char *data, int nb_data);
    virtual srs_error_t on_rtcp(char *data, int nb_data);
    virtual bool is_establelished();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);

public:
    void reset();
    void set_on_dtls_handshake_done_error(srs_error_t err);
    void set_on_dtls_alert_error(srs_error_t err);
    void set_protect_rtp_error(srs_error_t err);
    void set_protect_rtcp_error(srs_error_t err);
    void set_write_error(srs_error_t err);
    void set_established(bool established);
};

// Mock SRTP implementation for testing SrsSecurityTransport
class MockSrtp : public ISrsSRTP
{
public:
    srs_error_t initialize_error_;
    srs_error_t protect_rtp_error_;
    srs_error_t protect_rtcp_error_;
    srs_error_t unprotect_rtp_error_;
    srs_error_t unprotect_rtcp_error_;
    std::string last_recv_key_;
    std::string last_send_key_;
    int initialize_count_;
    int protect_rtp_count_;
    int protect_rtcp_count_;
    int unprotect_rtp_count_;
    int unprotect_rtcp_count_;

public:
    MockSrtp();
    virtual ~MockSrtp();

public:
    virtual srs_error_t initialize(std::string recv_key, std::string send_key);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    virtual srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    virtual srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);

public:
    void reset();
    void set_initialize_error(srs_error_t err);
    void set_protect_rtp_error(srs_error_t err);
    void set_protect_rtcp_error(srs_error_t err);
    void set_unprotect_rtp_error(srs_error_t err);
    void set_unprotect_rtcp_error(srs_error_t err);
};

// Mock PLI Worker Handler for testing SrsRtcPliWorker
class MockRtcPliWorkerHandler : public ISrsRtcPliWorkerHandler
{
public:
    srs_error_t do_request_keyframe_error_;
    std::vector<std::pair<uint32_t, SrsContextId> > keyframe_requests_;
    int do_request_keyframe_count_;

public:
    MockRtcPliWorkerHandler();
    virtual ~MockRtcPliWorkerHandler();

public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);

public:
    void reset();
    void set_do_request_keyframe_error(srs_error_t err);
    bool has_keyframe_request(uint32_t ssrc, const SrsContextId &cid);
    int get_keyframe_request_count();
};

// Mock PLI Worker for testing SrsRtcPlayStream::on_stream_change
class MockRtcPliWorker : public SrsRtcPliWorker
{
public:
    std::vector<std::pair<uint32_t, SrsContextId> > keyframe_requests_;
    int request_keyframe_count_;

public:
    MockRtcPliWorker(ISrsRtcPliWorkerHandler *h);
    virtual ~MockRtcPliWorker();

public:
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid);

public:
    void reset();
    bool has_keyframe_request(uint32_t ssrc, const SrsContextId &cid);
    int get_keyframe_request_count();
};

// Mock context for testing SrsRtcAsyncCallOnStop
class MockContext : public ISrsContext
{
public:
    SrsContextId current_id_;
    SrsContextId get_id_result_;

public:
    MockContext();
    virtual ~MockContext();
    virtual SrsContextId generate_id();
    virtual const SrsContextId &get_id();
    virtual const SrsContextId &set_id(const SrsContextId &v);
    void set_current_id(const SrsContextId &id);
};

// Mock RTC send track for testing SrsRtcPlayStream::send_packet
class MockRtcSendTrack : public SrsRtcSendTrack
{
public:
    srs_error_t on_rtp_error_;
    srs_error_t on_nack_error_;
    int on_rtp_count_;
    int on_nack_count_;
    SrsRtpPacket *last_rtp_packet_;
    SrsRtpPacket **last_nack_packet_;
    bool nack_set_to_null_;

public:
    MockRtcSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio);
    virtual ~MockRtcSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt);
    virtual srs_error_t on_nack(SrsRtpPacket **ppkt);
    void set_on_rtp_error(srs_error_t err);
    void set_on_nack_error(srs_error_t err);
    void set_nack_set_to_null(bool v);
    void reset();
};

// Mock RTCP classes for testing SrsRtcPlayStream::on_rtcp
class MockRtcpCommon : public SrsRtcpCommon
{
public:
    uint8_t mock_type_;

public:
    MockRtcpCommon(uint8_t type);
    virtual ~MockRtcpCommon();
    virtual uint8_t type() const;
};

class MockRtcpRR : public SrsRtcpRR
{
public:
    MockRtcpRR(uint32_t sender_ssrc = 0);
    virtual ~MockRtcpRR();
};

class MockRtcpNack : public SrsRtcpNack
{
public:
    MockRtcpNack(uint32_t sender_ssrc = 0);
    virtual ~MockRtcpNack();
};

class MockRtcpFbCommon : public SrsRtcpFbCommon
{
public:
    uint8_t mock_rc_;

public:
    MockRtcpFbCommon(uint8_t rc = kPLI);
    virtual ~MockRtcpFbCommon();
    virtual uint8_t get_rc() const;
};

class MockRtcpXr : public SrsRtcpXr
{
public:
    MockRtcpXr(uint32_t ssrc = 0);
    virtual ~MockRtcpXr();
};

// Mock RTC send track with NACK response capability for testing on_rtcp_nack
class MockRtcSendTrackForNack : public SrsRtcSendTrack
{
public:
    srs_error_t on_recv_nack_error_;
    int on_recv_nack_count_;
    std::vector<uint16_t> last_lost_seqs_;
    uint32_t test_ssrc_;
    bool track_enabled_;

public:
    MockRtcSendTrackForNack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio, uint32_t ssrc);
    virtual ~MockRtcSendTrackForNack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt);
    virtual srs_error_t on_recv_nack(const std::vector<uint16_t> &lost_seqs);
    virtual bool has_ssrc(uint32_t ssrc);
    virtual bool get_track_status();

public:
    void set_track_enabled(bool enabled);
    void set_on_recv_nack_error(srs_error_t err);
    void reset();
};

// Mock RTCP sender for testing SrsRtcPublishRtcpTimer
class MockRtcRtcpSender : public ISrsRtcRtcpSender
{
public:
    bool is_sender_started_;
    bool is_sender_twcc_enabled_;
    srs_error_t send_rtcp_rr_error_;
    srs_error_t send_rtcp_xr_rrtr_error_;
    srs_error_t send_periodic_twcc_error_;
    int send_rtcp_rr_count_;
    int send_rtcp_xr_rrtr_count_;
    int send_periodic_twcc_count_;

public:
    MockRtcRtcpSender();
    virtual ~MockRtcRtcpSender();

public:
    virtual bool is_sender_started();
    virtual srs_error_t send_rtcp_rr();
    virtual srs_error_t send_rtcp_xr_rrtr();
    virtual bool is_sender_twcc_enabled();
    virtual srs_error_t send_periodic_twcc();

public:
    void set_sender_started(bool started);
    void set_sender_twcc_enabled(bool enabled);
    void set_send_rtcp_rr_error(srs_error_t err);
    void set_send_rtcp_xr_rrtr_error(srs_error_t err);
    void set_send_periodic_twcc_error(srs_error_t err);
    void reset();
};

// Mock expire for testing SrsRtcPublishStream
class MockRtcExpire : public ISrsExpire
{
public:
    bool expired_;

public:
    MockRtcExpire();
    virtual ~MockRtcExpire();

public:
    virtual void expire();
    void reset();
};

#endif
