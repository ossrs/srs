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

#ifdef SRS_OSX
// These functions are similar to the older byteorder(3) family of functions.
// For example, be32toh() is identical to ntohl().
// @see https://linux.die.net/man/3/be32toh
#define be32toh ntohl
#endif

SrsRtcNetworks::SrsRtcNetworks(SrsRtcConnection* conn)
{
    conn_ = conn;
    delta_ = new SrsEphemeralDelta();
    udp_ = new SrsRtcUdpNetwork(conn_, delta_);
}

SrsRtcNetworks::~SrsRtcNetworks()
{
    srs_freep(udp_);
    srs_freep(delta_);
}

srs_error_t SrsRtcNetworks::initialize(SrsSessionConfig* cfg, bool dtls, bool srtp)
{
    srs_error_t err = srs_success;

    if ((err = udp_->initialize(cfg, dtls, srtp)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    return err;
}

void SrsRtcNetworks::set_state(SrsRtcNetworkState state)
{
    udp_->set_state(state);
}

SrsRtcUdpNetwork* SrsRtcNetworks::udp()
{
    return udp_;
}

ISrsRtcNetwork* SrsRtcNetworks::available()
{
    return udp_;
}

ISrsKbpsDelta* SrsRtcNetworks::delta()
{
    return delta_;
}

ISrsRtcNetwork::ISrsRtcNetwork()
{
}

ISrsRtcNetwork::~ISrsRtcNetwork()
{
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

SrsRtcTcpConn::SrsRtcTcpConn(ISrsProtocolReadWriter* skt, std::string cip, int port, ISrsResourceManager* cm)
{
    manager_ = cm;
    ip_ = cip;
    port_ = port;
    skt_ = skt;
    delta_ = new SrsNetworkDelta();
    delta_->set_io(skt_, skt_);
    trd_ = new SrsSTCoroutine("tcp", this, _srs_context->get_id());
}

SrsRtcTcpConn::~SrsRtcTcpConn()
{
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

    // TODO: FIXME: Handle all bytes of TCP Connection.

    return err;
}

