//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_NETWORK_HPP
#define SRS_APP_RTC_NETWORK_HPP

#include <srs_core.hpp>

#include <map>
#include <string>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_io.hpp>
#include <srs_protocol_conn.hpp>

class ISrsResourceManager;
class ISrsCoroutine;
class SrsNetworkDelta;
class ISrsNetworkDelta;
class SrsTcpConnection;
class ISrsTcpConnection;
class ISrsKbpsDelta;
class SrsUdpMuxSocket;
class ISrsUdpMuxSocket;
class SrsErrorPithyPrint;
class ISrsRtcTransport;
class SrsEphemeralDelta;
class ISrsEphemeralDelta;
class ISrsKbpsDelta;
class SrsRtcUdpNetwork;
class ISrsRtcUdpNetwork;
class ISrsRtcNetwork;
class SrsRtcTcpNetwork;
class SrsRtcDummyNetwork;
class SrsRtcTcpConn;
class ISrsRtcTcpConn;
class ISrsContext;
class ISrsRtcBlackhole;
class ISrsClock;
class SrsSpedController;

enum SrsRtcNetworkState {
    SrsRtcNetworkStateInit = -1,
    SrsRtcNetworkStateWaitingAnswer = 1,
    SrsRtcNetworkStateWaitingStun = 2,
    SrsRtcNetworkStateDtls = 3,
    SrsRtcNetworkStateEstablished = 4,
    SrsRtcNetworkStateClosed = 5,
};

// The handshake simulator loses what SRS sends a client in its handshake, as a lossy network
// does, so a test sees how the client recovers. The --simulator=on build arms it from the WHIP
// or WHEP request, since a client starts its checks as soon as it has the answer.
class SrsRtcHandshakeDrop
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int stun_;
    int dtls_;
    int finished_;

public:
    SrsRtcHandshakeDrop();
    virtual ~SrsRtcHandshakeDrop();

public:
    // Lose the next stun STUN binding responses, the next dtls DTLS datagrams, and the next finished
    // datagrams that carry the DTLS 1.2 server's last flight, ChangeCipherSpec and Finished.
    void arm(int stun, int dtls, int finished);
    // Whether to lose this STUN binding response.
    bool drop_stun(int size);
    // Whether to lose this packet, which is lost only when it is DTLS.
    bool drop_dtls(void *data, size_t size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static bool is_last_flight(const uint8_t *data, size_t size);
};

class ISrsRtcNetworks
{
public:
    ISrsRtcNetworks();
    virtual ~ISrsRtcNetworks();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp) = 0;
    virtual void set_state(SrsRtcNetworkState state) = 0;
    virtual ISrsRtcNetwork *udp() = 0;
    virtual ISrsRtcNetwork *tcp() = 0;
    virtual ISrsRtcNetwork *available() = 0;
    virtual ISrsKbpsDelta *delta() = 0;
    virtual void simulate_handshake_drop(int stun, int dtls, int finished) = 0;
};

class SrsRtcNetworks : public ISrsRtcNetworks
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcNetwork *udp_;
    ISrsRtcNetwork *tcp_;
    ISrsRtcNetwork *dummy_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcConnection *conn_;
    ISrsEphemeralDelta *delta_;
    // Shared by the UDP and TCP networks, as the drops are of the session.
    SrsRtcHandshakeDrop *drop_;

public:
    SrsRtcNetworks(ISrsRtcConnection *conn);
    virtual ~SrsRtcNetworks();

public:
    srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);

public:
    void set_state(SrsRtcNetworkState state);
    ISrsRtcNetwork *udp();
    ISrsRtcNetwork *tcp();
    ISrsRtcNetwork *available();

public:
    virtual ISrsKbpsDelta *delta();
    virtual void simulate_handshake_drop(int stun, int dtls, int finished);
};

class ISrsRtcNetwork : public ISrsStreamWriter
{
public:
    ISrsRtcNetwork();
    virtual ~ISrsRtcNetwork();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp) = 0;
    virtual void set_state(SrsRtcNetworkState state) = 0;

public:
    virtual srs_error_t on_dtls_handshake_done() = 0;
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
    virtual srs_error_t on_dtls(char *data, int nb_data) = 0;

public:
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher) = 0;
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher) = 0;

public:
    virtual srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data) = 0;
    virtual srs_error_t on_rtp(char *data, int nb_data) = 0;
    virtual srs_error_t on_rtcp(char *data, int nb_data) = 0;

public:
    virtual bool is_establelished() = 0;
    // Get the SPED controller of the network's DTLS, NULL without SPED.
    virtual SrsSpedController *sped() = 0;
};

class SrsRtcDummyNetwork : public ISrsRtcNetwork
{
public:
    SrsRtcDummyNetwork();
    virtual ~SrsRtcDummyNetwork();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual void set_state(SrsRtcNetworkState state);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls(char *data, int nb_data);

public:
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);

public:
    virtual srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);
    virtual srs_error_t on_rtp(char *data, int nb_data);
    virtual srs_error_t on_rtcp(char *data, int nb_data);

public:
    virtual bool is_establelished();
    virtual SrsSpedController *sped();

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

// Chrome repeats USE-CANDIDATE on its selected pair at least every 2.5s, and RFC 7675 consent checks come at most 6s
// apart.
#define SRS_RTC_NOMINATION_TIMEOUT (6 * SRS_UTIME_SECONDS)

class SrsRtcUdpNetwork : public ISrsRtcNetwork
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *conn_manager_;
    ISrsRtcBlackhole *blackhole_;
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcConnection *conn_;
    ISrsEphemeralDelta *delta_;
    SrsRtcNetworkState state_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsErrorPithyPrint *pp_address_change_;
    ISrsUdpMuxSocket *sendonly_skt_;
    srs_utime_t nominated_at_;
    // A client may send from more than one address.
    std::map<std::string, ISrsUdpMuxSocket *> peer_addresses_;
    ISrsRtcTransport *transport_;
    // The session's, not owned.
    SrsRtcHandshakeDrop *drop_;

public:
    SrsRtcUdpNetwork(ISrsRtcConnection *conn, ISrsEphemeralDelta *delta);
    virtual ~SrsRtcUdpNetwork();

public:
    void set_handshake_drop(SrsRtcHandshakeDrop *drop);
    void update_sendonly_socket(ISrsUdpMuxSocket *skt);
    srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);
    // The skt is the peer address of the request. An authenticated binding request moves media to it when the
    // request nominates it, or when no nomination came within SRS_RTC_NOMINATION_TIMEOUT.
    srs_error_t on_stun(ISrsUdpMuxSocket *skt, SrsStunPacket *r, char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsUdpMuxSocket *cache_peer_address(ISrsUdpMuxSocket *skt);
    srs_error_t on_binding_request(SrsStunPacket *r, std::string ice_pwd, ISrsUdpMuxSocket *from);
    // Start the DTLS handshake on the first binding request.
    srs_error_t on_stun_done();
    // Take the SPED attributes of a request: its ACKs, and its DTLS, which DTLS reads before the
    // response is built, so the response carries the ACK and the flight DTLS writes.
    srs_error_t on_sped_request(SrsSpedController *sped, SrsStunPacket *r);

public:
    srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    srs_error_t on_dtls_handshake_done();
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);

public:
    srs_error_t on_rtcp(char *data, int nb_data);
    srs_error_t on_rtp(char *data, int nb_data);

public:
    void set_state(SrsRtcNetworkState state);
    virtual bool is_establelished();
    virtual SrsSpedController *sped();
    std::string get_peer_ip();
    int get_peer_port();

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

class ISrsRtcTcpNetwork : public ISrsRtcNetwork
{
public:
    ISrsRtcTcpNetwork();
    virtual ~ISrsRtcTcpNetwork();

public:
    virtual void set_owner(SrsSharedResource<ISrsRtcTcpConn> v) = 0;
    virtual SrsSharedResource<ISrsRtcTcpConn> owner() = 0;
    virtual void update_sendonly_socket(ISrsProtocolReadWriter *skt) = 0;
    virtual void set_peer_id(const std::string &ip, int port) = 0;
    virtual void stop_sends() = 0;
};

class SrsRtcTcpNetwork : public ISrsRtcTcpNetwork
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcConnection *conn_;
    ISrsEphemeralDelta *delta_;
    ISrsProtocolReadWriter *sendonly_skt_;
    ISrsRtcBlackhole *blackhole_;
    // The sends in progress, from coroutines such as the shared timer or a player.
    int writers_;
    srs_cond_t writers_done_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcTransport *transport_;
    SrsSharedResource<ISrsRtcTcpConn> owner_;
    // The session's, not owned.
    SrsRtcHandshakeDrop *drop_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string peer_ip_;
    int peer_port_;
    SrsRtcNetworkState state_;

public:
    SrsRtcTcpNetwork(ISrsRtcConnection *conn, ISrsEphemeralDelta *delta);
    virtual ~SrsRtcTcpNetwork();

public:
    virtual void set_owner(SrsSharedResource<ISrsRtcTcpConn> v) { owner_ = v; }
    virtual SrsSharedResource<ISrsRtcTcpConn> owner() { return owner_; }
    void set_handshake_drop(SrsRtcHandshakeDrop *drop);
    virtual void update_sendonly_socket(ISrsProtocolReadWriter *skt);
    virtual void stop_sends();

public:
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);

    srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_binding_request(SrsStunPacket *r, std::string ice_pwd);
    // Start the DTLS handshake on the first binding request.
    srs_error_t on_stun_done();
    // Take the SPED attributes of a request: its ACKs, and its DTLS, which DTLS reads before the
    // response is built, so the response carries the ACK and the flight DTLS writes.
    srs_error_t on_sped_request(SrsSpedController *sped, SrsStunPacket *r);

public:
    srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual srs_error_t on_dtls(char *data, int nb_data);

public:
    srs_error_t on_rtcp(char *data, int nb_data);
    srs_error_t on_rtp(char *data, int nb_data);

public:
    void set_state(SrsRtcNetworkState state);
    virtual bool is_establelished();
    virtual SrsSpedController *sped();
    std::string get_peer_ip();
    int get_peer_port();

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);

public:
    virtual void set_peer_id(const std::string &ip, int port);
    void dispose();
};

class ISrsRtcTcpConn : public ISrsConnection, public ISrsCoroutineHandler, public ISrsExecutorHandler
{
public:
    ISrsRtcTcpConn();
    virtual ~ISrsRtcTcpConn();

public:
    virtual void interrupt() = 0;
};

class SrsRtcTcpConn : public ISrsRtcTcpConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *conn_manager_;
    ISrsStatistic *stat_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A raw pointer, not a shared one, because the session refers to this object.
    ISrsRtcConnection *session_;
    bool handling_;
    bool waiting_;
    srs_cond_t handled_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string ip_;
    int port_;
    ISrsNetworkDelta *delta_;
    ISrsProtocolReadWriter *skt_;
    char *pkt_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The shared resource that owns this object. Never free it, as the shared pointer manages it.
    SrsSharedResource<ISrsRtcTcpConn> *wrapper_;
    ISrsInterruptable *owner_coroutine_;
    ISrsContextIdSetter *owner_cid_;
    SrsContextId cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void setup();

public:
    SrsRtcTcpConn();
    SrsRtcTcpConn(ISrsProtocolReadWriter *skt, std::string cip, int port);
    virtual ~SrsRtcTcpConn();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    void setup_owner(SrsSharedResource<ISrsRtcTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);

public:
    ISrsKbpsDelta *delta();
    void interrupt();

public:
    virtual std::string desc();
    virtual const SrsContextId &get_id();

public:
    virtual std::string remote_ip();

public:
    virtual void on_executor_done(ISrsInterruptable *executor);

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    srs_error_t handshake();
    srs_error_t read_packet(char *pkt, int *nb_pkt);
    srs_error_t on_stun(char *pkt, int nb_pkt);
    srs_error_t on_tcp_pkt(char *pkt, int nb_pkt);
};

#endif
