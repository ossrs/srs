//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_network.hpp>

using namespace std;

#include <srs_app_http_static.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_sped.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtc_stun.hpp>

#if defined(SRS_OSX) || defined(_WIN32)
// The be32toh() is identical to ntohl().
// @see https://linux.die.net/man/3/be32toh
#define be32toh ntohl
#endif

// The longest a send to a WebRTC client over TCP waits when the client's buffer is full. Media that waits longer is
// too late to play, and every such send holds up the coroutine that makes it, such as the shared timer.
#define SRS_RTC_TCP_SEND_TIMEOUT (1 * SRS_UTIME_SECONDS)

extern bool srs_is_stun(const uint8_t *data, size_t size);
extern bool srs_is_dtls(const uint8_t *data, size_t len);
extern bool srs_is_rtp_or_rtcp(const uint8_t *data, size_t len);
extern bool srs_is_rtcp(const uint8_t *data, size_t len);

SrsRtcHandshakeDrop::SrsRtcHandshakeDrop()
{
    stun_ = 0;
    dtls_ = 0;
    finished_ = 0;
}

SrsRtcHandshakeDrop::~SrsRtcHandshakeDrop()
{
}

void SrsRtcHandshakeDrop::arm(int stun, int dtls, int finished)
{
    stun_ = srs_max(0, stun);
    dtls_ = srs_max(0, dtls);
    finished_ = srs_max(0, finished);
}

bool SrsRtcHandshakeDrop::drop_stun(int size)
{
    if (stun_ <= 0) {
        return false;
    }

    srs_warn("RTC: handshake simulator #%d drops a STUN binding response, %d bytes", stun_, size);
    stun_--;
    return true;
}

bool SrsRtcHandshakeDrop::drop_dtls(void *data, size_t size)
{
    if ((dtls_ <= 0 && finished_ <= 0) || !srs_is_dtls((const uint8_t *)data, size)) {
        return false;
    }

    if (finished_ > 0 && is_last_flight((const uint8_t *)data, size)) {
        srs_warn("RTC: handshake simulator #%d drops a DTLS last flight, %d bytes", finished_, (int)size);
        finished_--;
        return true;
    }

    if (dtls_ <= 0) {
        return false;
    }

    srs_warn("RTC: handshake simulator #%d drops a DTLS datagram, %d bytes", dtls_, (int)size);
    dtls_--;
    return true;
}

// A DTLS 1.2 record header is the content type(1), the version(2), the epoch(2), the sequence number(6) and the
// length(2), see RFC 6347 section 4.1. The server's last flight has the ChangeCipherSpec(20), and its Finished is the
// first handshake record(22) of epoch 1; the first flight is all epoch 0. A DTLS 1.3 encrypted record has a unified
// header with no epoch field, so it ends the walk.
bool SrsRtcHandshakeDrop::is_last_flight(const uint8_t *data, size_t size)
{
    for (size_t pos = 0; pos + 13 <= size;) {
        uint8_t content_type = data[pos];
        uint16_t epoch = (uint16_t(data[pos + 3]) << 8) | data[pos + 4];
        if (content_type == 20 || (content_type == 22 && epoch != 0)) {
            return true;
        }
        if (content_type < 20 || content_type > 26) {
            return false;
        }
        pos += 13 + ((size_t(data[pos + 11]) << 8) | data[pos + 12]);
    }
    return false;
}

ISrsRtcNetworks::ISrsRtcNetworks()
{
}

ISrsRtcNetworks::~ISrsRtcNetworks()
{
}

SrsRtcNetworks::SrsRtcNetworks(ISrsRtcConnection *conn)
{
    conn_ = conn;
    delta_ = new SrsEphemeralDelta();
    drop_ = new SrsRtcHandshakeDrop();

    SrsRtcUdpNetwork *udp = new SrsRtcUdpNetwork(conn_, delta_);
    udp->set_handshake_drop(drop_);
    udp_ = udp;

    SrsRtcTcpNetwork *tcp = new SrsRtcTcpNetwork(conn_, delta_);
    tcp->set_handshake_drop(drop_);
    tcp_ = tcp;

    dummy_ = new SrsRtcDummyNetwork();
}

SrsRtcNetworks::~SrsRtcNetworks()
{
    srs_freep(udp_);
    srs_freep(tcp_);
    srs_freep(dummy_);
    srs_freep(delta_);
    srs_freep(drop_);
}

srs_error_t SrsRtcNetworks::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
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

ISrsRtcNetwork *SrsRtcNetworks::udp()
{
    return udp_;
}

ISrsRtcNetwork *SrsRtcNetworks::tcp()
{
    return tcp_;
}

ISrsRtcNetwork *SrsRtcNetworks::available()
{
    if (udp_->is_establelished()) {
        return udp_;
    }

    if (tcp_->is_establelished()) {
        return tcp_;
    }
    return dummy_;
}

ISrsKbpsDelta *SrsRtcNetworks::delta()
{
    return delta_;
}

void SrsRtcNetworks::simulate_handshake_drop(int stun, int dtls, int finished)
{
    drop_->arm(stun, dtls, finished);
}

ISrsRtcNetwork::ISrsRtcNetwork()
{
}

ISrsRtcNetwork::~ISrsRtcNetwork()
{
}

SrsRtcDummyNetwork::SrsRtcDummyNetwork()
{
}

SrsRtcDummyNetwork::~SrsRtcDummyNetwork()
{
}

srs_error_t SrsRtcDummyNetwork::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
{
    return srs_success;
}

void SrsRtcDummyNetwork::set_state(SrsRtcNetworkState state)
{
}

bool SrsRtcDummyNetwork::is_establelished()
{
    return true;
}

SrsSpedController *SrsRtcDummyNetwork::sped()
{
    return NULL;
}

srs_error_t SrsRtcDummyNetwork::on_dtls_handshake_done()
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::on_dtls(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::protect_rtp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::protect_rtcp(void *packet, int *nb_cipher)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::on_stun(SrsStunPacket *r, char *data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::on_rtp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::on_rtcp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsRtcDummyNetwork::write(void *buf, size_t size, ssize_t *nwrite)
{
    return srs_success;
}

SrsRtcUdpNetwork::SrsRtcUdpNetwork(ISrsRtcConnection *conn, ISrsEphemeralDelta *delta)
{
    state_ = SrsRtcNetworkStateInit;
    conn_ = conn;
    delta_ = delta;
    sendonly_skt_ = NULL;
    nominated_at_ = 0;
    pp_address_change_ = new SrsErrorPithyPrint();
    transport_ = new SrsSecurityTransport(this);
    drop_ = NULL;

    conn_manager_ = _srs_conn_manager;
    blackhole_ = _srs_blackhole;
    clk_ = _srs_clock;
}

SrsRtcUdpNetwork::~SrsRtcUdpNetwork()
{
    // Free the transport first, as it depends on the socket.
    srs_freep(transport_);

    // The sendonly_skt_ points to one of these, so it is not freed on its own.
    map<string, ISrsUdpMuxSocket *>::iterator it;
    for (it = peer_addresses_.begin(); it != peer_addresses_.end(); ++it) {
        ISrsUdpMuxSocket *addr = it->second;
        srs_freep(addr);
    }

    srs_freep(pp_address_change_);

    conn_manager_ = NULL;
    blackhole_ = NULL;
    clk_ = NULL;
}

srs_error_t SrsRtcUdpNetwork::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
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

srs_error_t SrsRtcUdpNetwork::on_dtls(char *data, int nb_data)
{
    delta_->add_delta(nb_data, 0);

    return transport_->on_dtls(data, nb_data);
}

srs_error_t SrsRtcUdpNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return conn_->on_dtls_alert(type, desc);
}

srs_error_t SrsRtcUdpNetwork::on_dtls_handshake_done()
{
    srs_error_t err = srs_success;

    // The DTLS ARQ may report the handshake done more than once.
    if (SrsRtcNetworkStateEstablished == state_) {
        return err;
    }

    if ((err = conn_->on_dtls_handshake_done()) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    state_ = SrsRtcNetworkStateEstablished;
    return err;
}

srs_error_t SrsRtcUdpNetwork::protect_rtp(void *packet, int *nb_cipher)
{
    return transport_->protect_rtp(packet, nb_cipher);
}

srs_error_t SrsRtcUdpNetwork::protect_rtcp(void *packet, int *nb_cipher)
{
    return transport_->protect_rtcp(packet, nb_cipher);
}

srs_error_t SrsRtcUdpNetwork::on_rtcp(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    delta_->add_delta(nb_data, 0);

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtcp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect");
    }

    char *unprotected_buf = data;
    blackhole_->sendto(unprotected_buf, nb_unprotected_buf);

    if ((err = conn_->on_rtcp(unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::on_rtp(char *data, int nb_data)
{
    srs_error_t err = srs_success;

#ifdef SRS_NACK_DEBUG_DROP_ENABLED
    static int nn_pkts = 0;
    bool drop = false;
    uint32_t ssrc = srs_rtp_fast_parse_ssrc(data, nb_data);
    uint16_t seq = srs_rtp_fast_parse_seq(data, nb_data);
    uint8_t pt = srs_rtp_fast_parse_pt(data, nb_data);
    if (pt && ssrc && pt == SRS_NACK_DEBUG_DROP_PACKET_PT && (nn_pkts++) < 100) {
        drop = (nn_pkts == SRS_NACK_DEBUG_DROP_PACKET_N);
        srs_trace(">>> #%d%s rtp seq=%u, pt=%u, ssrc=%u", nn_pkts, drop ? " (dropped)" : "", seq, pt, ssrc);
    }
    if (drop) {
        return err;
    }
#endif

    delta_->add_delta(nb_data, 0);

    // The cipher hook may drop the packet, for the NACK simulator or a payload type configured to drop, before it is
    // decrypted; a dropped packet is neither decrypted nor delivered.
    bool dropped = false;
    if ((err = conn_->on_rtp_cipher(data, nb_data, &dropped)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }
    if (dropped) {
        return err;
    }

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtp unprotect");
    }

    char *unprotected_buf = data;
    blackhole_->sendto(unprotected_buf, nb_unprotected_buf);

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

bool SrsRtcUdpNetwork::is_establelished()
{
    return state_ == SrsRtcNetworkStateEstablished;
}

SrsSpedController *SrsRtcUdpNetwork::sped()
{
    return transport_->sped();
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

void SrsRtcUdpNetwork::set_handshake_drop(SrsRtcHandshakeDrop *drop)
{
    drop_ = drop;
}

void SrsRtcUdpNetwork::update_sendonly_socket(ISrsUdpMuxSocket *skt)
{
    // TODO: FIXME: Refine performance.
    string prev_peer_id, peer_id = skt->peer_id();
    if (sendonly_skt_) {
        prev_peer_id = sendonly_skt_->peer_id();
    }

    if (prev_peer_id == peer_id) {
        return;
    }

    if (prev_peer_id.empty()) {
        srs_trace("RTC: session address init %s", peer_id.c_str());
    } else {
        uint32_t nn = 0;
        if (pp_address_change_->can_print(skt->get_peer_port(), &nn)) {
            srs_trace("RTC: session address change %s -> %s, cached=%d, nn_change=%u/%u, nn_address=%u", prev_peer_id.c_str(),
                      peer_id.c_str(), (peer_addresses_.count(peer_id) ? 1 : 0), pp_address_change_->nn_count_, nn, peer_addresses_.size());
        }
    }

    sendonly_skt_ = cache_peer_address(skt);
}

ISrsUdpMuxSocket *SrsRtcUdpNetwork::cache_peer_address(ISrsUdpMuxSocket *skt)
{
    string peer_id = skt->peer_id();
    map<string, ISrsUdpMuxSocket *>::iterator it = peer_addresses_.find(peer_id);
    if (it != peer_addresses_.end()) {
        ISrsUdpMuxSocket *addr = it->second;
        return addr;
    }

    ISrsUdpMuxSocket *addr = skt->copy_sendonly();
    peer_addresses_[peer_id] = addr;
    conn_manager_->add_with_id(peer_id, conn_);

    uint64_t fast_id = skt->fast_id();
    if (fast_id) {
        conn_manager_->add_with_fast_id(fast_id, conn_);
    }

    return addr;
}

srs_error_t SrsRtcUdpNetwork::on_stun(SrsStunPacket *r, char *data, int nb_data)
{
    return on_stun(NULL, r, data, nb_data);
}

srs_error_t SrsRtcUdpNetwork::on_stun(ISrsUdpMuxSocket *skt, SrsStunPacket *r, char *data, int nb_data)
{
    srs_error_t err = srs_success;

    blackhole_->sendto(data, nb_data);

    if (!r->is_binding_request()) {
        return err;
    }

    string ice_pwd;
    if ((err = conn_->on_binding_request(r, ice_pwd)) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    ISrsUdpMuxSocket *from = sendonly_skt_;
    if (skt) {
        // ICE-lite, see #4796: a browser puts USE-CANDIDATE on each check of its nominated pair and checks its other
        // pairs without it, while a client like pion nominates only once, so its checks after a NAT rebind carry none.
        bool nominated = r->get_use_candidate();
        bool selected = sendonly_skt_ && sendonly_skt_->peer_id() == skt->peer_id();
        srs_utime_t now = clk_->now();
        bool held = nominated_at_ > 0 && now >= nominated_at_ && now - nominated_at_ <= SRS_RTC_NOMINATION_TIMEOUT;
        if (!sendonly_skt_ || nominated || selected || !held) {
            update_sendonly_socket(skt);
            if (nominated) {
                nominated_at_ = now;
            } else if (!selected) {
                nominated_at_ = 0;
            }
        }
        from = cache_peer_address(skt);
    }

    if ((err = on_binding_request(r, ice_pwd, from)) != srs_success) {
        return srs_error_wrap(err, "stun binding request failed");
    }

    return err;
}

// Take the SPED attributes of a binding request: the controller takes its ACKs, and DTLS reads
// its embedded DTLS, which counts as received bytes, as raw DTLS does.
static srs_error_t sped_take_request(SrsSpedController *sped, SrsStunPacket *r, ISrsEphemeralDelta *delta, ISrsRtcTransport *transport)
{
    srs_error_t err = srs_success;

    string data = r->get_dtls_in_stun_data();
    if (!sped->on_stun_received(r->has_dtls_in_stun_data(), data, r->has_dtls_in_stun_ack(), r->get_dtls_in_stun_acks())) {
        return err;
    }

    delta->add_delta(data.size(), 0);
    if ((err = transport->on_embedded_dtls(&data[0], (int)data.size())) != srs_success) {
        return srs_error_wrap(err, "embedded dtls");
    }

    return err;
}

// Put the ACK list and the next datagram of the flight in a binding response.
static void sped_embed_in_response(SrsSpedController *sped, SrsStunPacket *response)
{
    vector<uint32_t> acks;
    if (sped->acks_to_embed(BindingResponse, acks)) {
        response->set_dtls_in_stun_acks(acks);
    }

    // A datagram that does not fit still went raw, when DTLS wrote it.
    string datagram;
    if (sped->data_to_embed(BindingResponse, datagram) && response->dtls_in_stun_data_fits((int)datagram.size())) {
        response->set_dtls_in_stun_data(datagram);
        sped->on_data_embedded();
    }
}

srs_error_t SrsRtcUdpNetwork::on_binding_request(SrsStunPacket *r, string ice_pwd, ISrsUdpMuxSocket *from)
{
    srs_error_t err = srs_success;

    SrsSpedController *sped = transport_->sped();
    if (sped && (err = on_sped_request(sped, r)) != srs_success) {
        return srs_error_wrap(err, "sped");
    }

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(buf, sizeof(buf)));

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(r->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(r->get_local_ufrag());
    stun_binding_response.set_transcation_id(r->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(from->get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(from->get_peer_port());

    if (sped) {
        sped_embed_in_response(sped, &stun_binding_response);
    }

    if ((err = stun_binding_response.encode(ice_pwd, stream.get())) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    bool dropped = drop_ && drop_->drop_stun(stream->pos());
    if (!dropped) {
        delta_->add_delta(0, stream->pos());
        if ((err = from->sendto(stream->data(), stream->pos(), SRS_UTIME_NO_TIMEOUT)) != srs_success) {
            return srs_error_wrap(err, "stun binding response send failed");
        }
    }

    if ((err = on_stun_done()) != srs_success) {
        return srs_error_wrap(err, "stun done");
    }

    blackhole_->sendto(stream->data(), stream->pos());

    return err;
}

srs_error_t SrsRtcUdpNetwork::on_stun_done()
{
    srs_error_t err = srs_success;

    if (state_ != SrsRtcNetworkStateWaitingStun) {
        return err;
    }

    state_ = SrsRtcNetworkStateDtls;
    // TODO: FIXME: Add cost.
    srs_trace("RTC: session STUN done, waiting DTLS handshake.");

    if ((err = transport_->start_active_handshake()) != srs_success) {
        return srs_error_wrap(err, "fail to dtls handshake");
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::on_sped_request(SrsSpedController *sped, SrsStunPacket *r)
{
    srs_error_t err = srs_success;

    if ((err = on_stun_done()) != srs_success) {
        return srs_error_wrap(err, "stun done");
    }

    if ((err = sped_take_request(sped, r, delta_, transport_)) != srs_success) {
        return srs_error_wrap(err, "take request");
    }

    return err;
}

srs_error_t SrsRtcUdpNetwork::write(void *buf, size_t size, ssize_t *nwrite)
{
    if (nwrite)
        *nwrite = size;

    if (drop_ && drop_->drop_dtls(buf, size)) {
        return srs_success;
    }

    delta_->add_delta(0, size);
    return sendonly_skt_->sendto(buf, size, SRS_UTIME_NO_TIMEOUT);
}

ISrsRtcTcpNetwork::ISrsRtcTcpNetwork()
{
}

ISrsRtcTcpNetwork::~ISrsRtcTcpNetwork()
{
}

SrsRtcTcpNetwork::SrsRtcTcpNetwork(ISrsRtcConnection *conn, ISrsEphemeralDelta *delta)
{
    conn_ = conn;
    delta_ = delta;
    sendonly_skt_ = NULL;
    writers_ = 0;
    writers_done_ = srs_cond_new();
    transport_ = new SrsSecurityTransport(this);
    drop_ = NULL;
    peer_port_ = 0;
    state_ = SrsRtcNetworkStateInit;

    blackhole_ = _srs_blackhole;
}

SrsRtcTcpNetwork::~SrsRtcTcpNetwork()
{
    // The conn's coroutine may wait in a send of this network, under the transport, so let the rest of its sends fail
    // at once, and wait for it to leave before freeing the transport.
    sendonly_skt_ = NULL;
    if (owner_.get()) {
        owner_->interrupt();
    }
    srs_freep(transport_);
    srs_cond_destroy(writers_done_);

    blackhole_ = NULL;
}

void SrsRtcTcpNetwork::set_handshake_drop(SrsRtcHandshakeDrop *drop)
{
    drop_ = drop;
}

void SrsRtcTcpNetwork::update_sendonly_socket(ISrsProtocolReadWriter *skt)
{
    sendonly_skt_ = skt;

    // A send to a client that stops reading must not wait forever, because the shared timer sends RTCP to every
    // session.
    if (sendonly_skt_) {
        sendonly_skt_->set_send_timeout(SRS_RTC_TCP_SEND_TIMEOUT);
    }
}

void SrsRtcTcpNetwork::stop_sends()
{
    sendonly_skt_ = NULL;

    // A send waits at most for the send timeout.
    while (writers_ > 0) {
        srs_cond_wait(writers_done_);
    }
}

srs_error_t SrsRtcTcpNetwork::on_dtls_handshake_done()
{
    srs_error_t err = srs_success;

    // The DTLS ARQ may report the handshake done more than once.
    if (SrsRtcNetworkStateEstablished == state_) {
        return err;
    }

    if ((err = conn_->on_dtls_handshake_done()) != srs_success) {
        return srs_error_wrap(err, "udp");
    }

    state_ = SrsRtcNetworkStateEstablished;
    return err;
}

srs_error_t SrsRtcTcpNetwork::on_dtls_alert(std::string type, std::string desc)
{
    return conn_->on_dtls_alert(type, desc);
}

srs_error_t SrsRtcTcpNetwork::protect_rtp(void *packet, int *nb_cipher)
{
    return transport_->protect_rtp(packet, nb_cipher);
}

srs_error_t SrsRtcTcpNetwork::protect_rtcp(void *packet, int *nb_cipher)
{
    return transport_->protect_rtcp(packet, nb_cipher);
}

srs_error_t SrsRtcTcpNetwork::on_stun(SrsStunPacket *r, char *data, int nb_data)
{
    srs_error_t err = srs_success;

    blackhole_->sendto(data, nb_data);

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

srs_error_t SrsRtcTcpNetwork::on_binding_request(SrsStunPacket *r, std::string ice_pwd)
{
    srs_error_t err = srs_success;

    SrsSpedController *sped = transport_->sped();
    if (sped && (err = on_sped_request(sped, r)) != srs_success) {
        return srs_error_wrap(err, "sped");
    }

    SrsStunPacket stun_binding_response;
    char buf[kRtpPacketSize];
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(buf, sizeof(buf)));

    stun_binding_response.set_message_type(BindingResponse);
    stun_binding_response.set_local_ufrag(r->get_remote_ufrag());
    stun_binding_response.set_remote_ufrag(r->get_local_ufrag());
    stun_binding_response.set_transcation_id(r->get_transcation_id());
    // FIXME: inet_addr is deprecated, IPV6 support
    stun_binding_response.set_mapped_address(be32toh(inet_addr(get_peer_ip().c_str())));
    stun_binding_response.set_mapped_port(get_peer_port());

    if (sped) {
        sped_embed_in_response(sped, &stun_binding_response);
    }

    if ((err = stun_binding_response.encode(ice_pwd, stream.get())) != srs_success) {
        return srs_error_wrap(err, "stun binding response encode failed");
    }

    bool dropped = drop_ && drop_->drop_stun(stream->pos());
    if (!dropped && (err = write(stream->data(), stream->pos(), NULL)) != srs_success) {
        return srs_error_wrap(err, "stun binding response send failed");
    }

    if ((err = on_stun_done()) != srs_success) {
        return srs_error_wrap(err, "stun done");
    }

    blackhole_->sendto(stream->data(), stream->pos());

    return err;
}

srs_error_t SrsRtcTcpNetwork::on_stun_done()
{
    srs_error_t err = srs_success;

    if (state_ != SrsRtcNetworkStateWaitingStun) {
        return err;
    }

    state_ = SrsRtcNetworkStateDtls;
    // TODO: FIXME: Add cost.
    srs_trace("RTC: session STUN done, waiting DTLS handshake.");

    if ((err = transport_->start_active_handshake()) != srs_success) {
        return srs_error_wrap(err, "fail to dtls handshake");
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::on_sped_request(SrsSpedController *sped, SrsStunPacket *r)
{
    srs_error_t err = srs_success;

    if ((err = on_stun_done()) != srs_success) {
        return srs_error_wrap(err, "stun done");
    }

    if ((err = sped_take_request(sped, r, delta_, transport_)) != srs_success) {
        return srs_error_wrap(err, "take request");
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
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

srs_error_t SrsRtcTcpNetwork::on_dtls(char *data, int nb_data)
{
    delta_->add_delta(nb_data, 0);

    return transport_->on_dtls(data, nb_data);
}

srs_error_t SrsRtcTcpNetwork::on_rtcp(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    delta_->add_delta(nb_data, 0);

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtcp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtcp unprotect");
    }

    char *unprotected_buf = data;
    blackhole_->sendto(unprotected_buf, nb_unprotected_buf);

    if ((err = conn_->on_rtcp(unprotected_buf, nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }

    return err;
}

srs_error_t SrsRtcTcpNetwork::on_rtp(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    delta_->add_delta(nb_data, 0);

    // The cipher hook may drop the packet, for the NACK simulator or a payload type configured to drop, before it is
    // decrypted; a dropped packet is neither decrypted nor delivered.
    bool dropped = false;
    if ((err = conn_->on_rtp_cipher(data, nb_data, &dropped)) != srs_success) {
        return srs_error_wrap(err, "cipher=%d", nb_data);
    }
    if (dropped) {
        return err;
    }

    int nb_unprotected_buf = nb_data;
    if ((err = transport_->unprotect_rtp(data, &nb_unprotected_buf)) != srs_success) {
        return srs_error_wrap(err, "rtp unprotect");
    }

    char *unprotected_buf = data;
    blackhole_->sendto(unprotected_buf, nb_unprotected_buf);

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

bool SrsRtcTcpNetwork::is_establelished()
{
    return state_ == SrsRtcNetworkStateEstablished;
}

SrsSpedController *SrsRtcTcpNetwork::sped()
{
    return transport_->sped();
}

std::string SrsRtcTcpNetwork::get_peer_ip()
{
    return peer_ip_;
}

int SrsRtcTcpNetwork::get_peer_port()
{
    return peer_port_;
}

srs_error_t SrsRtcTcpNetwork::write(void *buf, size_t size, ssize_t *nwrite)
{
    srs_error_t err = srs_success;

    // No socket when no TCP connection owns this network, for example, it closed before DTLS done.
    if (!sendonly_skt_) {
        return srs_error_new(ERROR_SOCKET_CLOSED, "rtc tcp no socket");
    }

    if (drop_ && drop_->drop_dtls(buf, size)) {
        if (nwrite) {
            *nwrite = size;
        }
        return err;
    }

    // Send 2 bytes size, in network order, and the data in one write, so the data never waits for the peer
    // to acknowledge the size, as it may with Nagle's algorithm.
    srs_assert(size <= 65535);
    uint8_t b[2] = {uint8_t(size >> 8), uint8_t(size)};

    iovec iovs[2];
    iovs[0].iov_base = (char *)b;
    iovs[0].iov_len = sizeof(b);
    iovs[1].iov_base = buf;
    iovs[1].iov_len = size;

    ssize_t n = 0;
    writers_++;
    err = sendonly_skt_->writev(iovs, 2, &n);
    writers_--;
    srs_cond_signal(writers_done_);

    // A failed send, such as a timeout, may leave part of a packet on the stream, so the client can parse no later
    // packet. Drop the socket, so later sends fail at once instead of waiting for the timeout again.
    if (err != srs_success) {
        sendonly_skt_ = NULL;
        return srs_error_wrap(err, "rtc tcp write len(%d) and body", size);
    }

    if (nwrite) {
        *nwrite = n - sizeof(b);
    }

    return err;
}

void SrsRtcTcpNetwork::set_peer_id(const std::string &ip, int port)
{
    peer_ip_ = ip;
    peer_port_ = port;
}

void SrsRtcTcpNetwork::dispose()
{
    state_ = SrsRtcNetworkStateClosed;
}

#define SRS_RTC_TCP_PACKET_MAX 1500

void SrsRtcTcpConn::setup()
{
    wrapper_ = NULL;
    owner_coroutine_ = NULL;
    owner_cid_ = NULL;

    pkt_ = NULL;
    delta_ = NULL;
    skt_ = NULL;

    handling_ = false;
    waiting_ = false;
    handled_ = srs_cond_new();

    conn_manager_ = _srs_conn_manager;
    stat_ = _srs_stat;
    context_ = _srs_context;
}

ISrsRtcTcpConn::ISrsRtcTcpConn()
{
}

ISrsRtcTcpConn::~ISrsRtcTcpConn()
{
}

SrsRtcTcpConn::SrsRtcTcpConn()
{
    setup();
}

SrsRtcTcpConn::SrsRtcTcpConn(ISrsProtocolReadWriter *skt, std::string cip, int port)
{
    setup();

    ip_ = cip;
    port_ = port;
    skt_ = skt;
    delta_ = new SrsNetworkDelta();
    session_ = NULL;
    pkt_ = new char[SRS_RTC_TCP_PACKET_MAX];
}

void SrsRtcTcpConn::assemble()
{
    cid_ = context_->get_id();

    if (delta_) {
        delta_->set_io(skt_, skt_);
    }
}

SrsRtcTcpConn::~SrsRtcTcpConn()
{
    srs_freepa(pkt_);
    srs_freep(delta_);
    srs_freep(skt_);
    srs_cond_destroy(handled_);

    conn_manager_ = NULL;
    stat_ = NULL;
    context_ = NULL;
}

void SrsRtcTcpConn::setup_owner(SrsSharedResource<ISrsRtcTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
    wrapper_ = wrapper;
    owner_coroutine_ = owner_coroutine;
    owner_cid_ = owner_cid;
}

ISrsKbpsDelta *SrsRtcTcpConn::delta()
{
    return delta_;
}

void SrsRtcTcpConn::interrupt()
{
    session_ = NULL;
    if (owner_coroutine_)
        owner_coroutine_->interrupt();

    // The session is freed after this, so wait for the coroutine to return from it, for example from a DTLS send
    // that waits for room in the socket.
    waiting_ = true;
    while (handling_) {
        srs_cond_wait(handled_);
    }
    waiting_ = false;
}

std::string SrsRtcTcpConn::desc()
{
    return "Tcp";
}

const SrsContextId &SrsRtcTcpConn::get_id()
{
    return cid_;
}

std::string SrsRtcTcpConn::remote_ip()
{
    return ip_;
}

void SrsRtcTcpConn::on_executor_done(ISrsInterruptable *executor)
{
    owner_coroutine_ = NULL;
}

srs_error_t SrsRtcTcpConn::cycle()
{
    srs_error_t err = do_cycle();

    stat_->on_disconnect(get_id().c_str(), err);
    stat_->kbps_add_delta(get_id().c_str(), delta_);

    if (session_) {
        if (session_->tcp()->is_establelished()) {
            // Only expire the session when this network is established, because the client might use UDP.
            session_->tcp()->set_state(SrsRtcNetworkStateClosed);
            session_->expire();
        } else {
            // Not established, so release the network to allow the client to reconnect over TCP, and never
            // send by the socket of this connection, which is freed with it. Another coroutine, such as the DTLS
            // client's ARQ, may still wait in a send on that socket, and closing a socket with a waiter aborts.
            ISrsRtcTcpNetwork *network = dynamic_cast<ISrsRtcTcpNetwork *>(session_->tcp());
            if (network && network->owner().get() == this) {
                network->stop_sends();
                network->set_owner(SrsSharedResource<ISrsRtcTcpConn>());
            }
        }
    }

    if (srs_error_code(err) == ERROR_SOCKET_TIMEOUT) {
        srs_freep(err);
        return srs_success;
    }

    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsRtcTcpConn::do_cycle()
{
    srs_error_t err = srs_success;

    context_->set_id(cid_);
    owner_cid_->set_cid(cid_);

    if ((err = handshake()) != srs_success) {
        return srs_error_wrap(err, "process rtc tcp pkt");
    }

    // TODO: FIXME: Handle all bytes of TCP Connection.
    while (true) {
        if (!owner_coroutine_)
            return err;
        if ((err = owner_coroutine_->pull()) != srs_success) {
            return srs_error_wrap(err, "rtc tcp conn");
        }

        int npkt = SRS_RTC_TCP_PACKET_MAX;
        if ((err = read_packet(pkt_, &npkt)) != srs_success) {
            return srs_error_wrap(err, "process rtc tcp pkt");
        }

        handling_ = true;
        err = on_tcp_pkt(pkt_, npkt);
        handling_ = false;
        if (waiting_) {
            srs_cond_signal(handled_);
        }
        if (err != srs_success) {
            return srs_error_wrap(err, "process rtc tcp pkt");
        }
    }

    return err;
}

srs_error_t SrsRtcTcpConn::handshake()
{
    srs_error_t err = srs_success;

    int npkt = SRS_RTC_TCP_PACKET_MAX;
    if ((err = read_packet(pkt_, &npkt)) != srs_success) {
        return srs_error_wrap(err, "process rtc tcp pkt");
    }

    bool is_stun = srs_is_stun((uint8_t *)pkt_, npkt);
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t *)pkt_, npkt);
    if (!is_stun) {
        return srs_error_new(ERROR_RTC_TCP_PACKET, "invalid packet stun=%d, rtp/rtcp=%d, pkt=%s",
                             is_stun, is_rtp_or_rtcp, srs_strings_dumps_hex(pkt_, npkt, 8).c_str());
    }

    SrsStunPacket ping;
    if ((err = ping.decode(pkt_, npkt)) != srs_success) {
        return srs_error_wrap(err, "decode stun packet failed");
    }

    srs_assert(!session_);
    ISrsRtcConnection *session = dynamic_cast<ISrsRtcConnection *>(conn_manager_->find_by_name(ping.get_username()));
    // TODO: FIXME: For ICE trickle, we may get STUN packets before SDP answer, so maybe should response it.
    if (!session) {
        return srs_error_new(ERROR_RTC_TCP_STUN, "no session, stun username=%s", ping.get_username().c_str());
    }

    session->switch_to_context();
    srs_trace("recv stun packet from %s:%d, use-candidate=%d, ice-controlled=%d, ice-controlling=%d",
              ip_.c_str(), port_, ping.get_use_candidate(), ping.get_ice_controlled(), ping.get_ice_controlling());

    // Only one TCP candidate is supported.
    ISrsRtcTcpNetwork *network = dynamic_cast<ISrsRtcTcpNetwork *>(session->tcp());
    if (network->owner().get()) {
        return srs_error_new(ERROR_RTC_TCP_UNIQUE, "only support one network");
    }
    network->set_owner(*wrapper_);
    session_ = session;

    if (ping.is_binding_request()) {
        network->update_sendonly_socket(skt_);
        network->set_peer_id(ip_, port_);
    }

    handling_ = true;
    err = network->on_stun(&ping, pkt_, npkt);
    handling_ = false;
    if (waiting_) {
        srs_cond_signal(handled_);
    }

    return err;
}

srs_error_t SrsRtcTcpConn::read_packet(char *pkt, int *nb_pkt)
{
    srs_error_t err = srs_success;

    // Each packet has a 2-byte length before it, @doc: https://www.rfc-editor.org/rfc/rfc4571#section-2
    ssize_t nread = 0;
    uint8_t b[2];
    if ((err = skt_->read_fully((char *)b, sizeof(b), &nread)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp conn read len");
    }

    uint16_t npkt = uint16_t(b[0]) << 8 | uint16_t(b[1]);
    if (npkt > *nb_pkt) {
        return srs_error_new(ERROR_RTC_TCP_SIZE, "invalid size=%u exceed %d", npkt, *nb_pkt);
    }

    if ((err = skt_->read_fully(pkt, npkt, &nread)) != srs_success) {
        return srs_error_wrap(err, "rtc tcp conn read body");
    }

    *nb_pkt = npkt;

    return err;
}

srs_error_t SrsRtcTcpConn::on_tcp_pkt(char *pkt, int nb_pkt)
{
    srs_error_t err = srs_success;

    // The session is destroyed.
    if (!session_)
        return err;

    bool is_stun = srs_is_stun((uint8_t *)pkt, nb_pkt);
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t *)pkt, nb_pkt);
    bool is_rtcp = srs_is_rtcp((uint8_t *)pkt, nb_pkt);

    session_->alive();

    if (is_stun) {
        SrsStunPacket ping;
        if ((err = ping.decode(pkt, nb_pkt)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        return session_->tcp()->on_stun(&ping, pkt, nb_pkt);
    }

    if (is_rtp_or_rtcp && !is_rtcp) {
        return session_->tcp()->on_rtp(pkt, nb_pkt);
    }

    if (is_rtp_or_rtcp && is_rtcp) {
        return session_->tcp()->on_rtcp(pkt, nb_pkt);
    }

    if (srs_is_dtls((uint8_t *)pkt, nb_pkt)) {
        return session_->tcp()->on_dtls(pkt, nb_pkt);
    }

    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}
