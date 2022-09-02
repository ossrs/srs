//
// Copyright (c) 2013-2022 The SRS Authors
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

// For DTLS to call network service.
class ISrsRtcNetwork : public ISrsStreamWriter
{
public:
    ISrsRtcNetwork();
    virtual ~ISrsRtcNetwork();
public:
    // Callback when DTLS connected.
    virtual srs_error_t on_connection_established() = 0;
    // Callback when DTLS disconnected.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
};

// The UDP network, default for WebRTC.
class SrsRtcNetwork : public ISrsRtcNetwork
{
private:
    friend class SrsRtcUdpNetwork;
private:
    // WebRTC session object.
    SrsRtcConnection* conn_;
    // Network over UDP.
    SrsRtcUdpNetwork* udp_;
    // Delta object for statistics.
    SrsEphemeralDelta* delta_;
public:
    SrsRtcNetwork(SrsRtcConnection* conn);
    virtual ~SrsRtcNetwork();
// DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig* cfg, bool dtls, bool srtp);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char* data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    srs_error_t on_connection_established();
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    srs_error_t unprotect_rtp(void* packet, int* nb_plaintext);
    srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext);
// When got data from socket.
public:
    srs_error_t on_rtcp(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
// Other functions.
public:
    // ICE reflexive address functions.
    std::string get_peer_ip();
    int get_peer_port();
    // Get the UDP network object.
    SrsRtcUdpNetwork* udp();
    // Get the delta object for statistics.
    virtual ISrsKbpsDelta* delta();
// Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
};

// The WebRTC over UDP network.
class SrsRtcUdpNetwork : public ISrsRtcNetwork
{
private:
    SrsRtcNetwork* network_;
private:
    // Pithy print for address change, use port as error code.
    SrsErrorPithyPrint* pp_address_change_;
    // The peer address, client maybe use more than one address, it's the current selected one.
    SrsUdpMuxSocket* sendonly_skt;
    // The address list, client may use multiple addresses.
    std::map<std::string, SrsUdpMuxSocket*> peer_addresses_;
    // The DTLS transport over this network.
    ISrsRtcTransport* transport_;
public:
    SrsRtcUdpNetwork(SrsRtcNetwork* network);
    virtual ~SrsRtcUdpNetwork();
public:
    // Update the UDP connection.
    void update_sendonly_socket(SrsUdpMuxSocket* skt);
// DTLS transport functions.
public:
    srs_error_t initialize(SrsSessionConfig* cfg, bool dtls, bool srtp);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char* data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    srs_error_t on_connection_established();
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    srs_error_t unprotect_rtp(void* packet, int* nb_plaintext);
    srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext);
// When got data from socket.
public:
    srs_error_t on_rtcp(char* data, int nb_data);
    srs_error_t on_rtp(char* data, int nb_data);
// Other functions.
public:
    // ICE reflexive address functions.
    std::string get_peer_ip();
    int get_peer_port();
// Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
};

// For WebRTC over TCP.
class SrsRtcTcpConn : public ISrsConnection, public ISrsStartable, public ISrsCoroutineHandler
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
    // TCP Transport object.
    SrsTcpConnection* skt_;
public:
    SrsRtcTcpConn(srs_netfd_t fd, std::string cip, int port, ISrsResourceManager* cm);
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
};

#endif

