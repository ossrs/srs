//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_rtc_network.hpp>

#include <arpa/inet.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_app_http_static.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_utility.hpp>

#ifdef SRS_OSX
// These functions are similar to the older byteorder(3) family of functions.
// For example, be32toh() is identical to ntohl().
// @see https://linux.die.net/man/3/be32toh
#define be32toh ntohl
#endif

extern bool srs_is_stun(const uint8_t* data, size_t size);
extern bool srs_is_dtls(const uint8_t* data, size_t len);
extern bool srs_is_rtp_or_rtcp(const uint8_t* data, size_t len);
extern bool srs_is_rtcp(const uint8_t* data, size_t len);

SrsRtcNetworks::SrsRtcNetworks(SrsRtcConnection* conn)
{
    conn_ = conn;
    delta_ = new SrsEphemeralDelta();
    udp_ = new SrsRtcUdpNetwork(conn_, delta_);
    tcp_ = new SrsRtcTcpNetwork(conn_, delta_);
    dummy_ = new SrsRtcDummyNetwork();
}

SrsRtcNetworks::~SrsRtcNetworks()
{
    srs_freep(udp_);
    srs_freep(tcp_);
    srs_freep(dummy_);
    srs_freep(delta_);
}

srs_error_t SrsRtcNetworks::initialize(SrsSessionConfig* cfg, bool dtls, bool srtp)
{
    srs_error_t err = srs_success;

    if ((err = udp_->initialize(cfg, dtls, srtp)) != srs_success) {
        return srs_error_wrap(err, "udp init");
    }

    if ((err = tcp_->initialize(cfg, dtls, srtp)) != srs_success) {
        return srs_error_wrap(err, "tcp init");
    }

    return err;
}

void SrsRtcNetworks::set_state(SrsRtcNetworkState state)
{
    udp_->set_state(state);
    tcp_->set_state(state);
}

SrsRtcUdpNetwork* SrsRtcNetworks::udp()
{
    return udp_;
}

SrsRtcTcpNetwork* SrsRtcNetworks::tcp()
{
    return tcp_;
}

ISrsRtcNetwork* SrsRtcNetworks::available()
{
    if(udp_->is_establelished()) {
        return udp_;
    }

    if(tcp_->is_establelished()) {
        return tcp_;
    }
    return dummy_;
}

ISrsKbpsDelta* SrsRtcNetworks::delta()
{
    return delta_;
}

ISrsRtcNetwork::ISrsRtcNetwork()
{
    establelished_ = false;
}

ISrsRtcNetwork::~ISrsRtcNetwork()
{
}

bool ISrsRtcNetwork::is_establelished()
{
    return establelished_;
}

SrsRtcDummyNetwork::SrsRtcDummyNetwork()
{   
    establelished_ = true;
}

SrsRtcDummyNetwork::~SrsRtcDummyNetwork()
{
}

srs_error_t SrsRtcDummyNetwork::on_connection_established()
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::protect_rtp(void* packet, int* nb_cipher)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::protect_rtcp(void* packet, int* nb_cipher)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::write(void* buf, size_t size, ssize_t* nwrite)
{
    return srs_success;
}

SrsRtcUdpNetwork::SrsRtcUdpNetwork(SrsRtcConnection* conn, SrsEphemeralDelta* delta)
{
    state_ = SrsRtcNetworkStateInit;
    conn_ = conn;
    delta_ = delta;
    sendonly_skt_ = NULL;
    pp_address_change_ = new SrsErrorPithyPrint();
    transport_ = new SrsSecurityTransport(this);
}

SrsRtcUdpNetwork::~SrsRtcUdpNetwork()
{
    // Free transport first, which depends on socket.
    srs_freep(transport_);

    // Note that we should never delete the sendonly_skt,
    // it's just point to the object in peer_addresses_.
    map<string, SrsUdpMuxSocket*>::iterator it;
    for (it = peer_addresses_.begin(); it != peer_addresses_.end(); ++it) {
        SrsUdpMuxSocket* addr = it->second;
        srs_freep(addr);
    }

    srs_freep(pp_address_change_);
}

srs_error_t SrsRtcUdpNetwork::initialize(SrsSessionConfig* cfg, bool dtls, bool srtp)
{
    srs_error_t err = srs_success;

    if (!srtp) {
        srs_freep(transport_);
        if (dtls) {
            transport_ = new SrsSemiSecurityTransport(this);
        } else {
            transport_ = new SrsPlaintextTransport(this);
        }
    }

    if ((err = transport_->initialize(cfg)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::start_active_handshake()
{
    return transport_->start_active_handshake();
}

srs_error_t SrsRtcUdpNetwork::on_dtls(char* data, int nb_data)
{
    establelished_ = true;
    // Update stat when we received data.
    delta_->add_delta(nb_data, 0);

    return transport_->on_dtls(data, nb_data);
}

srs_error_t SrsRtcUdpNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return conn_->on_dtls_alert(type, desc);
}

srs_error_t SrsRtcUdpNetwork::on_connection_established()
{
    srs_error_t err = srs_success;

    // If DTLS done packet received many times, such as ARQ, ignore.
    if(SrsRtcNetworkStateClosed == state_) {
        return err;
    }

    if ((err = conn_->on_connection_established()) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    state_ = SrsRtcNetworkStateClosed;
    return err;
}

srs_error_t SrsRtcUdpNetwork::protect_rtp(void* packet, int* nb_cipher)
{
    return transport_->protect_rtp(packet, nb_cipher);
}

srs_error_t SrsRtcUdpNetwork::protect_rtcp(void* packet, int* nb_cipher)
{
    return transport_->protect_rtcp(packet, nb_cipher);
}

srs_error_t SrsRtcUdpNetwork::on_rtcp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // Update stat when we received data.
    delta_->add_delta(nb_data, 0);

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtcp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect");
    }

    char* unprotected_buf = data;
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(unprotected_buf, nb_unprotected_buf);
    }

    if ((err = conn_->on_rtcp(unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::on_rtp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // Update stat when we received data.
    delta_->add_delta(nb_data, 0);

    if ((err = conn_->on_rtp_cipher(data, nb_data)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtp unprotect");
    }

    char* unprotected_buf = data;
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(unprotected_buf, nb_unprotected_buf);
    }

    if ((err = conn_->on_rtp_plaintext(unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    return err;
}

void SrsRtcUdpNetwork::set_state(SrsRtcNetworkState state)
{
    if (state_ > state) {
        srs_warn("RTC: Ignore setting state=%d, now=%d", state, state_);
        return;
    }

    state_ = state;
}

string SrsRtcUdpNetwork::get_peer_ip()
{
    srs_assert(sendonly_skt_);
    return sendonly_skt_->get_peer_ip();
}

int SrsRtcUdpNetwork::get_peer_port()
{
    srs_assert(sendonly_skt_);
    return sendonly_skt_->get_peer_port();
}

void SrsRtcUdpNetwork::update_sendonly_socket(SrsUdpMuxSocket* skt)
{
    // TODO: FIXME: Refine performance.
    string prev_peer_id, peer_id = skt->peer_id();
    if (sendonly_skt_) {
        prev_peer_id = sendonly_skt_->peer_id();
    }

    // Ignore if same address.
    if (prev_peer_id == peer_id) {
        return;
    }

    // Find object from cache.
    SrsUdpMuxSocket* addr_cache = NULL;
    if (true) {
        map<string, SrsUdpMuxSocket*>::iterator it = peer_addresses_.find(peer_id);
        if (it != peer_addresses_.end()) {
            addr_cache = it->second;
        }
    }

    // Show address change log.
    if (prev_peer_id.empty()) {
        srs_trace("RTC: session address init %s", peer_id.c_str());
    } else {
        uint32_t nn = 0;
        if (pp_address_change_->can_print(skt->get_peer_port(), &nn)) {
            srs_trace("RTC: session address change %s -> %s, cached=%d, nn_change=%u/%u, nn_address=%u", prev_peer_id.c_str(),
                      peer_id.c_str(), (addr_cache? 1:0), pp_address_change_->nn_count, nn, peer_addresses_.size());
        }
    }

    // If no cache, build cache and setup the relations in connection.
    if (!addr_cache) {
        peer_addresses_[peer_id] = addr_cache = skt->copy_sendonly();
        _srs_rtc_manager->add_with_id(peer_id, conn_);

        uint64_t fast_id = skt->fast_id();
        if (fast_id) {
            _srs_rtc_manager->add_with_fast_id(fast_id, conn_);
        }
    }

    // Update the transport.
    sendonly_skt_ = addr_cache;
}

srs_error_t SrsRtcUdpNetwork::on_stun(SrsStunPacket* r, char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // Write STUN messages to blackhole.
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(data, nb_data);
    }

    if (!r->is_binding_request()) {
        return err;
    }

    string ice_pwd;
    if ((err = conn_->on_binding_request(r, ice_pwd)) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    if ((err = on_binding_request(r, ice_pwd)) != srs_success) {
        return srs_error_wrap(err, "stun binding request failed");
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::on_binding_request(SrsStunPacket* r, string ice_pwd)
{
    srs_error_t err = srs_success;

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(r->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(r->get_local_ufrag());
    stun_binding_response.set_transcation_id(r->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(get_peer_port());

    if ((err = stun_binding_response.encode(ice_pwd, stream)) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if ((err = write(stream->data(), stream->pos(), NULL)) != srs_success) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (state_ == SrsRtcNetworkStateWaitingStun) {
        state_ = SrsRtcNetworkStateDtls;
        // TODO: FIXME: Add cost.
        srs_trace("RTC: session STUN done, waiting DTLS handshake.");

        if((err = start_active_handshake()) != srs_success) {
            return srs_error_wrap(err, "fail to dtls handshake");
        }
    }

    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(stream->data(), stream->pos());
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::write(void* buf, size_t size, ssize_t* nwrite)
{
    // Update stat when we sending data.
    delta_->add_delta(0, size);

    if (nwrite) *nwrite = size;
    return sendonly_skt_->sendto(buf, size, SRS_UTIME_NO_TIMEOUT);
}

SrsRtcTcpNetwork::SrsRtcTcpNetwork(SrsRtcConnection* conn, SrsEphemeralDelta* delta)
{
    conn_ = conn;
    delta_ = delta;
    sendonly_skt_ = NULL;
    transport_ = new SrsSecurityTransport(this);
    peer_port_ = 0;
    state_ = SrsRtcNetworkStateInit;
}

SrsRtcTcpNetwork::~SrsRtcTcpNetwork()
{
    srs_freep(transport_);
}

void SrsRtcTcpNetwork::update_sendonly_socket(ISrsProtocolReadWriter* skt)
{
    sendonly_skt_ = skt;
}

srs_error_t SrsRtcTcpNetwork::on_connection_established()
{
    srs_error_t err = srs_success;

    // If DTLS done packet received many times, such as ARQ, ignore.
    if(SrsRtcNetworkStateClosed == state_) {
        return err;
    }

    if ((err = conn_->on_connection_established()) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    state_ = SrsRtcNetworkStateClosed;
    return err;
}

srs_error_t SrsRtcTcpNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return conn_->on_dtls_alert(type, desc);
}

srs_error_t SrsRtcTcpNetwork::protect_rtp(void* packet, int* nb_cipher)
{
    return transport_->protect_rtp(packet, nb_cipher);
}

srs_error_t SrsRtcTcpNetwork::protect_rtcp(void* packet, int* nb_cipher)
{
    return transport_->protect_rtcp(packet, nb_cipher);
}

srs_error_t SrsRtcTcpNetwork::on_stun(SrsStunPacket* r, char* data, int nb_data)
{
   srs_error_t err = srs_success;

    // Write STUN messages to blackhole.
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(data, nb_data);
    }

    if (!r->is_binding_request()) {
        return err;
    }

    string ice_pwd;
    if ((err = conn_->on_binding_request(r, ice_pwd)) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    if ((err = on_binding_request(r, ice_pwd)) != srs_success) {
        return srs_error_wrap(err, "stun binding request failed");
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::on_binding_request(SrsStunPacket* r, std::string ice_pwd)
{
    srs_error_t err = srs_success;

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsBuffer* stream = new SrsBuffer(buf, sizeof(buf));
    SrsAutoFree(SrsBuffer, stream);

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(r->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(r->get_local_ufrag());
    stun_binding_response.set_transcation_id(r->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(get_peer_port());

    if ((err = stun_binding_response.encode(ice_pwd, stream)) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    if ((err = write(stream->data(), stream->pos(), NULL)) != srs_success) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if (state_ == SrsRtcNetworkStateWaitingStun) {
        state_ = SrsRtcNetworkStateDtls;
        // TODO: FIXME: Add cost.
        srs_trace("RTC: session STUN done, waiting DTLS handshake.");

        if((err = start_active_handshake()) != srs_success) {
            return srs_error_wrap(err, "fail to dtls handshake");
        }
    }

    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(stream->data(), stream->pos());
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::initialize(SrsSessionConfig* cfg, bool dtls, bool srtp)
{
    srs_error_t err = srs_success;

    if (!srtp) {
        srs_freep(transport_);
        if (dtls) {
            transport_ = new SrsSemiSecurityTransport(this);
        } else {
            transport_ = new SrsPlaintextTransport(this);
        }
    }

    if ((err = transport_->initialize(cfg)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::start_active_handshake()
{
    return transport_->start_active_handshake();
}

srs_error_t SrsRtcTcpNetwork::on_dtls(char* data, int nb_data)
{
    establelished_ = true;
    // Update stat when we received data.
    delta_->add_delta(nb_data, 0);

    return transport_->on_dtls(data, nb_data);
}

srs_error_t SrsRtcTcpNetwork::on_rtcp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // Update stat when we received data.
    delta_->add_delta(nb_data, 0);

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtcp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect");
    }

    char* unprotected_buf = data;
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(unprotected_buf, nb_unprotected_buf);
    }

    if ((err = conn_->on_rtcp(unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::on_rtp(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // Update stat when we received data.
    delta_->add_delta(nb_data, 0);

    if ((err = conn_->on_rtp_cipher(data, nb_data)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtp unprotect");
    }

    char* unprotected_buf = data;
    if (_srs_blackhole->blackhole) {
        _srs_blackhole->sendto(unprotected_buf, nb_unprotected_buf);
    }

    if ((err = conn_->on_rtp_plaintext(unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    return err;
}

void SrsRtcTcpNetwork::set_state(SrsRtcNetworkState state)
{
    if (state_ > state) {
        srs_warn("RTC: Ignore setting state=%d, now=%d", state, state_);
        return;
    }

    state_ = state;
}

std::string SrsRtcTcpNetwork::get_peer_ip()
{
    return peer_ip_;
}

int SrsRtcTcpNetwork::get_peer_port()
{
    return peer_port_;
}

srs_error_t SrsRtcTcpNetwork::write(void* buf, size_t size, ssize_t* nwrite)
{
    srs_assert(size <= 65535);
    srs_error_t err = srs_success;
    
    char len_str[2];
    SrsBuffer buf_len(len_str, sizeof(len_str));
    buf_len.write_2bytes(size);

    ssize_t n = 0;

    if((err = sendonly_skt_->write(buf_len.data(), sizeof(len_str), &n)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp write len(%d)", size);
    }

    if(nwrite) {
        *nwrite = n;
    }

    // TODO: FIXME: maybe need to send by a few times
    if((err = sendonly_skt_->write(buf, size, &n)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp write body");
    }

    if(nwrite) {
        *nwrite += n;
    }

    return err;
}

void SrsRtcTcpNetwork::set_peer_id(const std::string& ip, int port)
{
    peer_ip_ = ip;
    peer_port_ = port;
}

SrsRtcTcpConn::SrsRtcTcpConn(ISrsProtocolReadWriter* skt, std::string cip, int port, ISrsResourceManager* cm)
{
    manager_ = cm;
    ip_ = cip;
    port_ = port;
    skt_ = skt;
    delta_ = new SrsNetworkDelta();
    delta_->set_io(skt_, skt_);
    trd_ = new SrsSTCoroutine("tcp", this, _srs_context->get_id());
    session_ = NULL;
    disposing_ = false;
}

SrsRtcTcpConn::~SrsRtcTcpConn()
{
    _srs_rtc_manager->unsubscribe(this);
    trd_->interrupt();
    srs_freep(trd_);

    srs_freep(delta_);
    srs_freep(skt_);
}

ISrsKbpsDelta* SrsRtcTcpConn::delta()
{
    return delta_;
}

std::string SrsRtcTcpConn::desc()
{
    return "Tcp";
}

const SrsContextId& SrsRtcTcpConn::get_id()
{
    return trd_->cid();
}

std::string SrsRtcTcpConn::remote_ip()
{
    return ip_;
}

srs_error_t SrsRtcTcpConn::start()
{
    _srs_rtc_manager->subscribe(this);
    return trd_->start();
}

srs_error_t SrsRtcTcpConn::cycle()
{
    srs_error_t err = do_cycle();

    // Only stat the HTTP streaming clients, ignore all API clients.
    SrsStatistic::instance()->on_disconnect(get_id().c_str());
    SrsStatistic::instance()->kbps_add_delta(get_id().c_str(), delta_);

    // Because we use manager to manage this object,
    // not the http connection object, so we must remove it here.
    manager_->remove(this);

    // For HTTP-API timeout, we think it's done successfully,
    // because there may be no request or response for HTTP-API.
    if (srs_error_code(err) == ERROR_SOCKET_TIMEOUT) {
        srs_freep(err);
        return srs_success;
    }

    return err;
}

srs_error_t SrsRtcTcpConn::do_cycle()
{
    srs_error_t err = srs_success;

    char* pkt = new char[1500];
    SrsAutoFreeA(char, pkt);

    // TODO: FIXME: Handle all bytes of TCP Connection.
    while(!disposing_) {
        if((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc tcp conn");
        }

        // Read length in 2 bytes @doc: https://www.rfc-editor.org/rfc/rfc4571#section-2
        ssize_t nread = 0; uint8_t b[2];
        if((err = skt_->read((char*)b, sizeof(b), &nread)) != srs_success) {
            return srs_error_wrap(err, "rtc tcp conn read len");
        }

        uint16_t npkt = uint16_t(b[0])<<8 | uint16_t(b[1]);
        if (npkt > 1500) {
            return srs_error_new(ERROR_RTC_TCP_SIZE, "invalid size=%u", npkt);
        }

        // Read a RTC pkt such as STUN, DTLS or RTP/RTCP
        if((err = skt_->read_fully(pkt, npkt, &nread)) != srs_success) {
            return srs_error_wrap(err, "rtc tcp conn read body");
        }

        // Ready to be destroyed, not need to process new packet
        if(disposing_) {
            return err;
        }

        if((err = on_tcp_pkt(pkt, npkt)) != srs_success) {
            return srs_error_wrap(err, "process rtc tcp pkt");
        }
    }

    return err;
}

srs_error_t SrsRtcTcpConn::on_tcp_pkt(char* pkt, int nb_pkt)
{
    srs_error_t err = srs_success;

    bool is_stun = srs_is_stun((uint8_t*)pkt, nb_pkt);
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t*)pkt, nb_pkt);
    bool is_rtcp = srs_is_rtcp((uint8_t*)pkt, nb_pkt);

    if(!is_stun && !session_) {
        srs_warn("rtc tcp received a mess pkt. %d[%s]", nb_pkt, srs_string_dumps_hex(pkt, nb_pkt, 8).c_str());
        return err;
    }

    if (session_) {
        // When got any packet, the session is alive now.
        session_->alive();
    }

    if(is_stun) {
        SrsStunPacket ping;
        if ((err = ping.decode(pkt, nb_pkt)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        if (!session_) {
            session_ = dynamic_cast<SrsRtcConnection*>(_srs_rtc_manager->find_by_name(ping.get_username()));
        }
        if (session_) {
            session_->switch_to_context();
        }

        srs_trace("recv stun packet from %s:%d, use-candidate=%d, ice-controlled=%d, ice-controlling=%d",
            ip_.c_str(), port_, ping.get_use_candidate(), ping.get_ice_controlled(), ping.get_ice_controlling());

        // TODO: FIXME: For ICE trickle, we may get STUN packets before SDP answer, so maybe should response it.
        if (!session_) {
            return srs_error_new(ERROR_RTC_STUN, "no session, stun username=%s",
                ping.get_username().c_str());
        }

        // For each binding request, update the TCP socket.
        if (ping.is_binding_request()) {
            session_->tcp()->update_sendonly_socket(skt_);
            session_->tcp()->set_peer_id(ip_, port_);
        }
        return session_->tcp()->on_stun(&ping, pkt, nb_pkt);
    }

    // For DTLS, RTCP or RTP, which does not support peer address changing.
    if (!session_) {
        return srs_error_new(ERROR_RTC_STUN, "no session peer=%s:%d", ip_.c_str(), port_);
    }

    // Note that we don't(except error) switch to the context of session, for performance issue.
    if (is_rtp_or_rtcp && !is_rtcp) {
        err = session_->tcp()->on_rtp(pkt, nb_pkt);
        if (err != srs_success) {
            return srs_error_wrap(err, "rtc tcp rtp");
        }
        return err;
    }

    if (is_rtp_or_rtcp && is_rtcp) {
        return session_->tcp()->on_rtcp(pkt, nb_pkt);
    }
    if (srs_is_dtls((uint8_t*)pkt, nb_pkt)) {
        srs_trace("receive a dtls pkt");
        return session_->tcp()->on_dtls(pkt, nb_pkt);
    }
    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}

void SrsRtcTcpConn::on_before_dispose(ISrsResource* c)
{
    if(!session_ || disposing_) {
        return;
    }

    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection*>(c);
    if(conn == session_) {
        // the related rtc connection will be disposed
        srs_trace("RTC: tcp conn diposing, because of rtc connection");
        session_ = NULL;
        disposing_ = true;
    }
}

void SrsRtcTcpConn::on_disposing(ISrsResource* c)
{
    return;
}

