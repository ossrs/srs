//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_RTC_NETWORK_HPP
#define SRS_APP_RTC_NETWORK_HPP

#include <srs_core.hpp>

#include <string>
#include <map>

#include <srs_protocol_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_io.hpp>

class ISrsResourceManager;
class SrsCoroutine;
class SrsNetworkDelta;
class SrsTcpConnection;
class ISrsKbpsDelta;
class SrsUdpMuxSocket;
class SrsErrorPithyPrint;
class ISrsRtcTransport;
class SrsEphemeralDelta;
class ISrsKbpsDelta;
class SrsRtcUdpNetwork;
class ISrsRtcNetwork;
class SrsRtcTcpNetwork;
class SrsRtcDummyNetwork;
class SrsRtcTcpConn;

// The network stat.
enum SrsRtcNetworkState
{
    SrsRtcNetworkStateInit = -1,
    SrsRtcNetworkStateWaitingAnswer = 1,
    SrsRtcNetworkStateWaitingStun = 2,
    SrsRtcNetworkStateDtls = 3,
    SrsRtcNetworkStateEstablished = 4,
    SrsRtcNetworkStateClosed = 5,
};

// A group of networks, each has its own DTLS and SRTP context.
class SrsRtcNetworks
{
private:
    // Network over UDP.
    SrsRtcUdpNetwork* udp_;
    // Network over TCP
    SrsRtcTcpNetwork* tcp_;
    // Network over dummy
    SrsRtcDummyNetwork* dummy_;
private:
    // WebRTC session object.
    SrsRtcConnection* conn_;
    // Delta object for statistics.
    SrsEphemeralDelta* delta_;
public:
    SrsRtcNetworks(SrsRtcConnection* conn);
    virtual ~SrsRtcNetworks();
// DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig* cfg, bool dtls, bool srtp);
public:
    // Connection level state machine, for ARQ of UDP packets.
    void set_state(SrsRtcNetworkState state);
    // Get the UDP network object.
    SrsRtcUdpNetwork* udp();
    SrsRtcTcpNetwork* tcp();
    // Get an available network.
    ISrsRtcNetwork* available();
public:
    // Get the delta object for statistics.
    virtual ISrsKbpsDelta* delta();
};

// For DTLS or Session to call network service.
class ISrsRtcNetwork : public ISrsStreamWriter
{
public:
    ISrsRtcNetwork();
    virtual ~ISrsRtcNetwork();
public:
    // Callback when DTLS connected.
    virtual srs_error_t on_dtls_handshake_done() = 0;
    // Callback when DTLS disconnected.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
public:
    // Protect RTP packet by SRTP context.
    virtual srs_error_t protect_rtp(void* packet, int* nb_cipher) = 0;
    // Protect RTCP packet by SRTP context.
    virtual srs_error_t protect_rtcp(void* packet, int* nb_cipher) = 0;
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
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
public:
    virtual srs_error_t protect_rtp(void* packet, int* nb_cipher);
    virtual srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    virtual bool is_establelished();
// Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
};

// The WebRTC over UDP network.
class SrsRtcUdpNetwork : public ISrsRtcNetwork
{
private:
    // WebRTC session object.
    SrsRtcConnection* conn_;
    // Delta object for statistics.
    SrsEphemeralDelta* delta_;
    SrsRtcNetworkState state_;
private:
    // Pithy print for address change, use port as error code.
    SrsErrorPithyPrint* pp_address_change_;
    // The peer address, client maybe use more than one address, it's the current selected one.
    SrsUdpMuxSocket* sendonly_skt_;
    // The address list, client may use multiple addresses.
    std::map<std::string, SrsUdpMuxSocket*> peer_addresses_;
    // The DTLS transport over this network.
    ISrsRtcTransport* transport_;
public:
    SrsRtcUdpNetwork(SrsRtcConnection* conn, SrsEphemeralDelta* delta);
    virtual ~SrsRtcUdpNetwork();
public:
    // Update the UDP connection.
    void update_sendonly_socket(SrsUdpMuxSocket* skt);
    // When got STUN ping message. The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsStunPacket* r, char* data, int nb_data);
private:
    srs_error_t on_binding_request(SrsStunPacket* r, std::string ice_pwd);
// DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig* cfg, bool dtls, bool srtp);
    virtual srs_error_t on_dtls(char* data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    srs_error_t on_dtls_handshake_done();
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
// When got data from socket.
public:
    srs_error_t on_rtcp(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
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
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
};

class SrsRtcTcpNetwork: public ISrsRtcNetwork
{
private:
    SrsRtcConnection* conn_;
    SrsEphemeralDelta* delta_;
    ISrsProtocolReadWriter* sendonly_skt_;
private:
     // The DTLS transport over this network.
    ISrsRtcTransport* transport_;
    SrsRtcTcpConn* owner_;
private:
    std::string peer_ip_;
    int peer_port_;
    SrsRtcNetworkState state_;
public:
    SrsRtcTcpNetwork(SrsRtcConnection* conn, SrsEphemeralDelta* delta);
    virtual ~SrsRtcTcpNetwork();
public:
    void set_owner(SrsRtcTcpConn* v) { owner_ = v; }
    SrsRtcTcpConn* owner() { return owner_; }
    void update_sendonly_socket(ISrsProtocolReadWriter* skt);
//ISrsRtcNetwork 
public:
    // Callback when DTLS connected.
    virtual srs_error_t on_dtls_handshake_done();
    // Callback when DTLS disconnected.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    // Protect RTP packet by SRTP context.
    virtual srs_error_t protect_rtp(void* packet, int* nb_cipher);
    // Protect RTCP packet by SRTP context.
    virtual srs_error_t protect_rtcp(void* packet, int* nb_cipher);

    // When got STUN ping message. The peer address may change, we can identify that by STUN messages.
    srs_error_t on_stun(SrsStunPacket* r, char* data, int nb_data);
private:
    srs_error_t on_binding_request(SrsStunPacket* r, std::string ice_pwd);
// DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig* cfg, bool dtls, bool srtp);
    virtual srs_error_t on_dtls(char* data, int nb_data);
// When got data from socket.
public:
    srs_error_t on_rtcp(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
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
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
public:
    void set_peer_id(const std::string& ip, int port);
    void dispose();
};

// For WebRTC over TCP.
class SrsRtcTcpConn : public ISrsConnection, public ISrsStartable, public ISrsCoroutineHandler, public ISrsDisposingHandler
{
private:
    // The manager object to manage the connection.
    ISrsResourceManager* manager_;
    // Use a coroutine to serve the TCP connection.
    SrsCoroutine* trd_;
    // The ip and port of client.
    std::string ip_;
    int port_;
    // The delta for statistic.
    SrsNetworkDelta* delta_;
    // WebRTC session object.
    SrsRtcConnection* session_;
    ISrsProtocolReadWriter* skt_;
    // Packet cache.
    char* pkt_;
public:
    SrsRtcTcpConn(ISrsProtocolReadWriter* skt, std::string cip, int port, ISrsResourceManager* cm);
    virtual ~SrsRtcTcpConn();
public:
    ISrsKbpsDelta* delta();
// Interface ISrsResource.
public:
    virtual std::string desc();
    virtual const SrsContextId& get_id();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
// Interface ISrsStartable
public:
    virtual srs_error_t start();
// Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
    srs_error_t handshake();
    srs_error_t read_packet(char* pkt, int* nb_pkt);
    srs_error_t on_stun(char* pkt, int nb_pkt);
    srs_error_t on_tcp_pkt(char* pkt, int nb_pkt);
// Interface of ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource* c);
    virtual void on_disposing(ISrsResource* c);
};

#endif

