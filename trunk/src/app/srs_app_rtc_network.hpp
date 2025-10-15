//
// Copyright (c) 2013-2025 The SRS Authors
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

// The network stat.
enum SrsRtcNetworkState {
    SrsRtcNetworkStateInit = -1,
    SrsRtcNetworkStateWaitingAnswer = 1,
    SrsRtcNetworkStateWaitingStun = 2,
    SrsRtcNetworkStateDtls = 3,
    SrsRtcNetworkStateEstablished = 4,
    SrsRtcNetworkStateClosed = 5,
};

// The network manager interface.
class ISrsRtcNetworks
{
public:
    ISrsRtcNetworks();
    virtual ~ISrsRtcNetworks();

public:
};

// A group of networks, each has its own DTLS and SRTP context.
class SrsRtcNetworks : public ISrsRtcNetworks
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Network over UDP.
    ISrsRtcNetwork *udp_;
    // Network over TCP
    ISrsRtcNetwork *tcp_;
    // Network over dummy
    ISrsRtcNetwork *dummy_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // WebRTC session object.
    ISrsRtcConnection *conn_;
    // Delta object for statistics.
    ISrsEphemeralDelta *delta_;

public:
    SrsRtcNetworks(ISrsRtcConnection *conn);
    virtual ~SrsRtcNetworks();
    // DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);

public:
    // Connection level state machine, for ARQ of UDP packets.
    void set_state(SrsRtcNetworkState state);
    // Get the UDP network object.
    ISrsRtcNetwork *udp();
    ISrsRtcNetwork *tcp();
    // Get an available network.
    ISrsRtcNetwork *available();

public:
    // Get the delta object for statistics.
    virtual ISrsKbpsDelta *delta();
};

// For DTLS or Session to call network service.
class ISrsRtcNetwork : public ISrsStreamWriter
{
public:
    ISrsRtcNetwork();
    virtual ~ISrsRtcNetwork();

public:
    // Initialize the network with DTLS and SRTP configuration.
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp) = 0;
    // Set the network state.
    virtual void set_state(SrsRtcNetworkState state) = 0;

public:
    // Callback when DTLS connected.
    virtual srs_error_t on_dtls_handshake_done() = 0;
    // Callback when DTLS disconnected.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
    // Handle DTLS data.
    virtual srs_error_t on_dtls(char *data, int nb_data) = 0;

public:
    // Protect RTP packet by SRTP context.
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher) = 0;
    // Protect RTCP packet by SRTP context.
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher) = 0;

public:
    // Handle STUN packet.
    virtual srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data) = 0;
    // Handle RTP packet.
    virtual srs_error_t on_rtp(char *data, int nb_data) = 0;
    // Handle RTCP packet.
    virtual srs_error_t on_rtcp(char *data, int nb_data) = 0;

public:
    virtual bool is_establelished() = 0;
};

// Dummy networks
class SrsRtcDummyNetwork : public ISrsRtcNetwork
{
public:
    SrsRtcDummyNetwork();
    virtual ~SrsRtcDummyNetwork();

    // The interface of ISrsRtcNetwork
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
    // Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

// The WebRTC over UDP network.
class SrsRtcUdpNetwork : public ISrsRtcNetwork
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *conn_manager_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // WebRTC session object.
    ISrsRtcConnection *conn_;
    // Delta object for statistics.
    ISrsEphemeralDelta *delta_;
    SrsRtcNetworkState state_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Pithy print for address change, use port as error code.
    SrsErrorPithyPrint *pp_address_change_;
    // The peer address, client maybe use more than one address, it's the current selected one.
    ISrsUdpMuxSocket *sendonly_skt_;
    // The address list, client may use multiple addresses.
    std::map<std::string, ISrsUdpMuxSocket *> peer_addresses_;
    // The DTLS transport over this network.
    ISrsRtcTransport *transport_;

public:
    SrsRtcUdpNetwork(ISrsRtcConnection *conn, ISrsEphemeralDelta *delta);
    virtual ~SrsRtcUdpNetwork();

public:
    // Update the UDP connection.
    void update_sendonly_socket(ISrsUdpMuxSocket *skt);
    // When got STUN ping message. The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_binding_request(SrsStunPacket *r, std::string ice_pwd);
    // DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    srs_error_t on_dtls_handshake_done();
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    // When got data from socket.
public:
    srs_error_t on_rtcp(char *data, int nb_data);
    srs_error_t on_rtp(char *data, int nb_data);
    // Other functions.
public:
    // Connection level state machine, for ARQ of UDP packets.
    void set_state(SrsRtcNetworkState state);
    virtual bool is_establelished();
    // ICE reflexive address functions.
    std::string get_peer_ip();
    int get_peer_port();
    // Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

class SrsRtcTcpNetwork : public ISrsRtcNetwork
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcConnection *conn_;
    ISrsEphemeralDelta *delta_;
    ISrsProtocolReadWriter *sendonly_skt_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The DTLS transport over this network.
    ISrsRtcTransport *transport_;
    SrsSharedResource<ISrsRtcTcpConn> owner_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string peer_ip_;
    int peer_port_;
    SrsRtcNetworkState state_;

public:
    SrsRtcTcpNetwork(ISrsRtcConnection *conn, ISrsEphemeralDelta *delta);
    virtual ~SrsRtcTcpNetwork();

public:
    void set_owner(SrsSharedResource<ISrsRtcTcpConn> v) { owner_ = v; }
    SrsSharedResource<ISrsRtcTcpConn> owner() { return owner_; }
    void update_sendonly_socket(ISrsProtocolReadWriter *skt);
    // ISrsRtcNetwork
public:
    // Callback when DTLS connected.
    virtual srs_error_t on_dtls_handshake_done();
    // Callback when DTLS disconnected.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    // Protect RTP packet by SRTP context.
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    // Protect RTCP packet by SRTP context.
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);

    // When got STUN ping message. The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_binding_request(SrsStunPacket *r, std::string ice_pwd);
    // DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual srs_error_t on_dtls(char *data, int nb_data);
    // When got data from socket.
public:
    srs_error_t on_rtcp(char *data, int nb_data);
    srs_error_t on_rtp(char *data, int nb_data);
    // Other functions.
public:
    // Connection level state machine, for ARQ of UDP packets.
    void set_state(SrsRtcNetworkState state);
    virtual bool is_establelished();
    // ICE reflexive address functions.
    std::string get_peer_ip();
    int get_peer_port();
    // Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);

public:
    void set_peer_id(const std::string &ip, int port);
    void dispose();
};

// The interface for TCP connection.
class ISrsRtcTcpConn : public ISrsConnection, public ISrsCoroutineHandler, public ISrsExecutorHandler
{
public:
    ISrsRtcTcpConn();
    virtual ~ISrsRtcTcpConn();

public:
    // Interrupt the TCP connection.
    virtual void interrupt() = 0;
};

// For WebRTC over TCP.
class SrsRtcTcpConn : public ISrsRtcTcpConn
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *conn_manager_;
    ISrsStatistic *stat_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Because session references to this object, so we should directly use the session ptr.
    ISrsRtcConnection *session_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The ip and port of client.
    std::string ip_;
    int port_;
    // The delta for statistic.
    ISrsNetworkDelta *delta_;
    ISrsProtocolReadWriter *skt_;
    // Packet cache.
    char *pkt_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The shared resource which own this object, we should never free it because it's managed by shared ptr.
    SrsSharedResource<ISrsRtcTcpConn> *wrapper_;
    // The owner coroutine, allow user to interrupt the loop.
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
    // Setup the owner, the wrapper is the shared ptr, the interruptable object is the coroutine, and the cid is the context id.
    void setup_owner(SrsSharedResource<ISrsRtcTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);

public:
    ISrsKbpsDelta *delta();
    // Interrupt transport by session.
    void interrupt();
    // Interface ISrsResource.
public:
    virtual std::string desc();
    virtual const SrsContextId &get_id();
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    // Interface ISrsExecutorHandler
public:
    virtual void on_executor_done(ISrsInterruptable *executor);
    // Interface ISrsCoroutineHandler
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
