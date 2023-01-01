//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_rtc_server.hpp>

#include <set>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_app_server.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_log.hpp>
#include <srs_app_rtc_network.hpp>

extern SrsPps* _srs_pps_rpkts;
SrsPps* _srs_pps_rstuns = NULL;
SrsPps* _srs_pps_rrtps = NULL;
SrsPps* _srs_pps_rrtcps = NULL;
extern SrsPps* _srs_pps_addrs;
extern SrsPps* _srs_pps_fast_addrs;

extern SrsPps* _srs_pps_spkts;
extern SrsPps* _srs_pps_sstuns;
extern SrsPps* _srs_pps_srtcps;
extern SrsPps* _srs_pps_srtps;

extern SrsPps* _srs_pps_ids;
extern SrsPps* _srs_pps_fids;
extern SrsPps* _srs_pps_fids_level0;

extern SrsPps* _srs_pps_pli;
extern SrsPps* _srs_pps_twcc;
extern SrsPps* _srs_pps_rr;

extern SrsPps* _srs_pps_snack;
extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_sanack;
extern SrsPps* _srs_pps_svnack;

extern SrsPps* _srs_pps_rnack;
extern SrsPps* _srs_pps_rnack2;
extern SrsPps* _srs_pps_rhnack;
extern SrsPps* _srs_pps_rmnack;

SrsRtcBlackhole::SrsRtcBlackhole()
{
    blackhole = false;
    blackhole_addr = NULL;
    blackhole_stfd = NULL;
}

SrsRtcBlackhole::~SrsRtcBlackhole()
{
    srs_close_stfd(blackhole_stfd);
    srs_freep(blackhole_addr);
}

srs_error_t SrsRtcBlackhole::initialize()
{
    srs_error_t err = srs_success;

    blackhole = _srs_config->get_rtc_server_black_hole();
    if (!blackhole) {
        return err;
    }

    string blackhole_ep = _srs_config->get_rtc_server_black_hole_addr();
    if (blackhole_ep.empty()) {
        blackhole = false;
        srs_warn("disable black hole for no endpoint");
        return err;
    }

    string host; int port;
    srs_parse_hostport(blackhole_ep, host, port);

    srs_freep(blackhole_addr);
    blackhole_addr = new sockaddr_in();
    blackhole_addr->sin_family = AF_INET;
    blackhole_addr->sin_addr.s_addr = inet_addr(host.c_str());
    blackhole_addr->sin_port = htons(port);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    blackhole_stfd = srs_netfd_open_socket(fd);
    srs_assert(blackhole_stfd);

    srs_trace("RTC blackhole %s:%d, fd=%d", host.c_str(), port, fd);

    return err;
}

void SrsRtcBlackhole::sendto(void* data, int len)
{
    if (!blackhole) {
        return;
    }

    // For blackhole, we ignore any error.
    srs_sendto(blackhole_stfd, data, len, (sockaddr*)blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
}

SrsRtcBlackhole* _srs_blackhole = NULL;

// @global dtls certficate for rtc module.
SrsDtlsCertificate* _srs_rtc_dtls_certificate = NULL;

// TODO: Should support error response.
// For STUN packet, 0x00 is binding request, 0x01 is binding success response.
bool srs_is_stun(const uint8_t* data, size_t size)
{
    return size > 0 && (data[0] == 0 || data[0] == 1);
}

// change_cipher_spec(20), alert(21), handshake(22), application_data(23)
// @see https://tools.ietf.org/html/rfc2246#section-6.2.1
bool srs_is_dtls(const uint8_t* data, size_t len)
{
    return (len >= 13 && (data[0] > 19 && data[0] < 64));
}

// For RTP or RTCP, the V=2 which is in the high 2bits, 0xC0 (1100 0000)
bool srs_is_rtp_or_rtcp(const uint8_t* data, size_t len)
{
    return (len >= 12 && (data[0] & 0xC0) == 0x80);
}

// For RTCP, PT is [128, 223] (or without marker [0, 95]).
// Literally, RTCP starts from 64 not 0, so PT is [192, 223] (or without marker [64, 95]).
// @note For RTP, the PT is [96, 127], or [224, 255] with marker.
bool srs_is_rtcp(const uint8_t* data, size_t len)
{
    return (len >= 12) && (data[0] & 0x80) && (data[1] >= 192 && data[1] <= 223);
}

srs_error_t api_server_as_candidates(string api, set<string>& candidate_ips)
{
    srs_error_t err = srs_success;

    if (api.empty() || !_srs_config->get_api_as_candidates()) {
        return err;
    }

    string hostname = api;
    if (hostname.empty() || hostname == SRS_CONSTS_LOCALHOST_NAME) {
        return err;
    }
    if (hostname == SRS_CONSTS_LOCALHOST || hostname == SRS_CONSTS_LOOPBACK || hostname == SRS_CONSTS_LOOPBACK6) {
        return err;
    }

    // Whether add domain name.
    if (!srs_is_ipv4(hostname) && _srs_config->get_keep_api_domain()) {
        candidate_ips.insert(hostname);
    }

    // Try to parse the domain name if not IP.
    if (!srs_is_ipv4(hostname) && _srs_config->get_resolve_api_domain()) {
        int family = 0;
        string ip = srs_dns_resolve(hostname, family);
        if (ip.empty() || ip == SRS_CONSTS_LOCALHOST || ip == SRS_CONSTS_LOOPBACK || ip == SRS_CONSTS_LOOPBACK6) {
            return err;
        }

        // Try to add the API server ip as candidates.
        candidate_ips.insert(ip);
    }

    // If hostname is IP, use it.
    if (srs_is_ipv4(hostname)) {
        candidate_ips.insert(hostname);
    }

    return err;
}

static set<string> discover_candidates(SrsRtcUserConfig* ruc)
{
    srs_error_t err = srs_success;

    // Try to discover the eip as candidate, specified by user.
    set<string> candidate_ips;
    if (!ruc->eip_.empty()) {
        candidate_ips.insert(ruc->eip_);
    }

    // Try to discover from api of request, if api_as_candidates enabled.
    if ((err = api_server_as_candidates(ruc->req_->host, candidate_ips)) != srs_success) {
        srs_warn("ignore discovering ip from api %s, err %s", ruc->req_->host.c_str(), srs_error_summary(err).c_str());
        srs_freep(err);
    }

    // If not * or 0.0.0.0, use the candidate as exposed IP.
    string candidate = _srs_config->get_rtc_server_candidates();
    if (candidate != "*" && candidate != "0.0.0.0") {
        candidate_ips.insert(candidate);
        return candidate_ips;
    }

    // All automatically detected IP list.
    vector<SrsIPAddress*>& ips = srs_get_local_ips();
    if (ips.empty()) {
        return candidate_ips;
    }

    // Discover from local network interface addresses.
    if (_srs_config->get_use_auto_detect_network_ip()) {
        // We try to find the best match candidates, no loopback.
        string family = _srs_config->get_rtc_server_ip_family();
        for (int i = 0; i < (int) ips.size(); ++i) {
            SrsIPAddress* ip = ips[i];
            if (ip->is_loopback) {
                continue;
            }

            if (family == "ipv4" && !ip->is_ipv4) {
                continue;
            }
            if (family == "ipv6" && ip->is_ipv4) {
                continue;
            }

            candidate_ips.insert(ip->ip);
            srs_trace("Best matched ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        }
    }

    if (!candidate_ips.empty()) {
        return candidate_ips;
    }

    // Then, we use the ipv4 address.
    for (int i = 0; i < (int)ips.size(); ++i) {
        SrsIPAddress* ip = ips[i];
        if (!ip->is_ipv4) {
            continue;
        }

        candidate_ips.insert(ip->ip);
        srs_trace("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        return candidate_ips;
    }

    // We use the first one, to make sure there will be at least one CANDIDATE.
    if (candidate_ips.empty()) {
        SrsIPAddress* ip = ips[0];
        candidate_ips.insert(ip->ip);
        srs_warn("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        return candidate_ips;
    }

    return candidate_ips;
}

SrsRtcUserConfig::SrsRtcUserConfig()
{
    req_ = new SrsRequest();
    publish_ = false;
    dtls_ = srtp_ = true;
    audio_before_video_ = false;
}

SrsRtcUserConfig::~SrsRtcUserConfig()
{
    srs_freep(req_);
}

SrsRtcServer::SrsRtcServer()
{
    async = new SrsAsyncCallWorker();

    _srs_config->subscribe(this);
}

SrsRtcServer::~SrsRtcServer()
{
    _srs_config->unsubscribe(this);

    if (true) {
        vector<SrsUdpMuxListener*>::iterator it;
        for (it = listeners.begin(); it != listeners.end(); ++it) {
            SrsUdpMuxListener* listener = *it;
            srs_freep(listener);
        }
    }

    async->stop();
    srs_freep(async);
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    // The RTC server start a timer, do routines of RTC server.
    // @see SrsRtcServer::on_timer()
    _srs_hybrid->timer5s()->subscribe(this);

    // Initialize the black hole.
    if ((err = _srs_blackhole->initialize()) != srs_success) {
        return srs_error_wrap(err, "black hole");
    }

    async->start();

    return err;
}

srs_error_t SrsRtcServer::on_reload_rtc_server()
{
    return srs_success;
}

srs_error_t SrsRtcServer::exec_async_work(ISrsAsyncCallTask * t)
{
    return async->execute(t);
}

srs_error_t SrsRtcServer::listen_udp()
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_rtc_server_enabled()) {
        return err;
    }

    int port = _srs_config->get_rtc_server_listen();
    if (port <= 0) {
        return srs_error_new(ERROR_RTC_PORT, "invalid port=%d", port);
    }

    string ip = srs_any_address_for_listener();
    srs_assert(listeners.empty());

    int nn_listeners = _srs_config->get_rtc_server_reuseport();
    for (int i = 0; i < nn_listeners; i++) {
        SrsUdpMuxListener* listener = new SrsUdpMuxListener(this, ip, port);

        if ((err = listener->listen()) != srs_success) {
            srs_freep(listener);
            return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
        }

        srs_trace("rtc listen at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());
        listeners.push_back(listener);
    }

    return err;
}

srs_error_t SrsRtcServer::on_udp_packet(SrsUdpMuxSocket* skt)
{
    srs_error_t err = srs_success;

    SrsRtcConnection* session = NULL;
    char* data = skt->data(); int size = skt->size();
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t*)data, size);
    bool is_rtcp = srs_is_rtcp((uint8_t*)data, size);

    uint64_t fast_id = skt->fast_id();
    // Try fast id first, if not found, search by long peer id.
    if (fast_id) {
        session = (SrsRtcConnection*)_srs_rtc_manager->find_by_fast_id(fast_id);
    }
    if (!session) {
        string peer_id = skt->peer_id();
        session = (SrsRtcConnection*)_srs_rtc_manager->find_by_id(peer_id);
    }

    if (session) {
        // When got any packet, the session is alive now.
        session->alive();
    }

    // For STUN, the peer address may change.
    if (!is_rtp_or_rtcp && srs_is_stun((uint8_t*)data, size)) {
        ++_srs_pps_rstuns->sugar;
        string peer_id = skt->peer_id();

        // TODO: FIXME: Should support ICE renomination, to switch network between candidates.
        SrsStunPacket ping;
        if ((err = ping.decode(data, size)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        if (!session) {
            session = find_session_by_username(ping.get_username());
        }
        if (session) {
            session->switch_to_context();
        }

        srs_info("recv stun packet from %s, fast=%" PRId64 ", use-candidate=%d, ice-controlled=%d, ice-controlling=%d",
            peer_id.c_str(), fast_id, ping.get_use_candidate(), ping.get_ice_controlled(), ping.get_ice_controlling());

        // TODO: FIXME: For ICE trickle, we may get STUN packets before SDP answer, so maybe should response it.
        if (!session) {
            return srs_error_new(ERROR_RTC_STUN, "no session, stun username=%s, peer_id=%s, fast=%" PRId64,
                ping.get_username().c_str(), peer_id.c_str(), fast_id);
        }

        // For each binding request, update the UDP socket.
        if (ping.is_binding_request()) {
            session->udp()->update_sendonly_socket(skt);
        }

        return session->udp()->on_stun(&ping, data, size);
    }

    // For DTLS, RTCP or RTP, which does not support peer address changing.
    if (!session) {
        string peer_id = skt->peer_id();
        return srs_error_new(ERROR_RTC_STUN, "no session, peer_id=%s, fast=%" PRId64, peer_id.c_str(), fast_id);
    }

    // Note that we don't(except error) switch to the context of session, for performance issue.
    if (is_rtp_or_rtcp && !is_rtcp) {
        ++_srs_pps_rrtps->sugar;

        err = session->udp()->on_rtp(data, size);
        if (err != srs_success) {
            session->switch_to_context();
        }
        return err;
    }

    session->switch_to_context();
    if (is_rtp_or_rtcp && is_rtcp) {
        ++_srs_pps_rrtcps->sugar;

        return session->udp()->on_rtcp(data, size);
    }
    if (srs_is_dtls((uint8_t*)data, size)) {
        ++_srs_pps_rstuns->sugar;

        return session->udp()->on_dtls(data, size);
    }
    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}

srs_error_t SrsRtcServer::listen_api()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Fetch api from hybrid manager, not from SRS.
    ISrsHttpServeMux* http_api_mux = _srs_hybrid->srs()->instance()->api_server();

    if ((err = http_api_mux->handle("/rtc/v1/play/", new SrsGoApiRtcPlay(this))) != srs_success) {
        return srs_error_wrap(err, "handle play");
    }

    if ((err = http_api_mux->handle("/rtc/v1/publish/", new SrsGoApiRtcPublish(this))) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    // Generally, WHIP is a publishing protocol, but it can be also used as playing.
    if ((err = http_api_mux->handle("/rtc/v1/whip/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whip");
    }

    // We create another mount, to support play with the same query string as publish.
    if ((err = http_api_mux->handle("/rtc/v1/whip-play/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whip play");
    }

#ifdef SRS_SIMULATOR
    if ((err = http_api_mux->handle("/rtc/v1/nack/", new SrsGoApiRtcNACK(this))) != srs_success) {
        return srs_error_wrap(err, "handle nack");
    }
#endif

    return err;
}

srs_error_t SrsRtcServer::create_session(SrsRtcUserConfig* ruc, SrsSdp& local_sdp, SrsRtcConnection** psession)
{
    srs_error_t err = srs_success;

    SrsContextId cid = _srs_context->get_id();

    SrsRequest* req = ruc->req_;

    SrsRtcSource* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if (ruc->publish_ && !source->can_publish()) {
        return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req->get_stream_url().c_str());
    }

    // TODO: FIXME: add do_create_session to error process.
    SrsRtcConnection* session = new SrsRtcConnection(this, cid);
    if ((err = do_create_session(ruc, local_sdp, session)) != srs_success) {
        srs_freep(session);
        return srs_error_wrap(err, "create session");
    }

    *psession = session;

    return err;
}

srs_error_t SrsRtcServer::do_create_session(SrsRtcUserConfig* ruc, SrsSdp& local_sdp, SrsRtcConnection* session)
{
    srs_error_t err = srs_success;

    SrsRequest* req = ruc->req_;

    // first add publisher/player for negotiate sdp media info
    if (ruc->publish_) {
        if ((err = session->add_publisher(ruc, local_sdp)) != srs_success) {
            return srs_error_wrap(err, "add publisher");
        }
    } else {
        if ((err = session->add_player(ruc, local_sdp)) != srs_success) {
            return srs_error_wrap(err, "add player");
        }
    }

    // All tracks default as inactive, so we must enable them.
    session->set_all_tracks_status(req->get_stream_url(), ruc->publish_, true);

    std::string local_pwd = srs_random_str(32);
    std::string local_ufrag = "";
    // TODO: FIXME: Rename for a better name, it's not an username.
    std::string username = "";
    while (true) {
        local_ufrag = srs_random_str(8);

        username = local_ufrag + ":" + ruc->remote_sdp_.get_ice_ufrag();
        if (!_srs_rtc_manager->find_by_name(username)) {
            break;
        }
    }

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(_srs_rtc_dtls_certificate->get_fingerprint());

    // We allows to mock the eip of server.
    if (true) {
        int udp_port = _srs_config->get_rtc_server_listen();
        int tcp_port = _srs_config->get_rtc_server_tcp_listen();
        string protocol = _srs_config->get_rtc_server_protocol();

        set<string> candidates = discover_candidates(ruc);
        for (set<string>::iterator it = candidates.begin(); it != candidates.end(); ++it) {
            string hostname;
            int uport = udp_port; srs_parse_hostport(*it, hostname, uport);
            int tport = tcp_port; srs_parse_hostport(*it, hostname, tport);

            if (protocol == "udp") {
                local_sdp.add_candidate("udp", hostname, uport, "host");
            } else if (protocol == "tcp") {
                local_sdp.add_candidate("tcp", hostname, tport, "host");
            } else {
                local_sdp.add_candidate("udp", hostname, uport, "host");
                local_sdp.add_candidate("tcp", hostname, tport, "host");
            }
        }

        vector<string> v = vector<string>(candidates.begin(), candidates.end());
        srs_trace("RTC: Use candidates %s, protocol=%s", srs_join_vector_string(v, ", ").c_str(), protocol.c_str());
    }

    // Setup the negotiate DTLS by config.
    local_sdp.session_negotiate_ = local_sdp.session_config_;

    // Setup the negotiate DTLS role.
    if (ruc->remote_sdp_.get_dtls_role() == "active") {
        local_sdp.session_negotiate_.dtls_role = "passive";
    } else if (ruc->remote_sdp_.get_dtls_role() == "passive") {
        local_sdp.session_negotiate_.dtls_role = "active";
    } else if (ruc->remote_sdp_.get_dtls_role() == "actpass") {
        local_sdp.session_negotiate_.dtls_role = local_sdp.session_config_.dtls_role;
    } else {
        // @see: https://tools.ietf.org/html/rfc4145#section-4.1
        // The default value of the setup attribute in an offer/answer exchange
        // is 'active' in the offer and 'passive' in the answer.
        local_sdp.session_negotiate_.dtls_role = "passive";
    }
    local_sdp.set_dtls_role(local_sdp.session_negotiate_.dtls_role);

    session->set_remote_sdp(ruc->remote_sdp_);
    // We must setup the local SDP, then initialize the session object.
    session->set_local_sdp(local_sdp);
    session->set_state_as_waiting_stun();

    // Before session initialize, we must setup the local SDP.
    if ((err = session->initialize(req, ruc->dtls_, ruc->srtp_, username)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    // We allows username is optional, but it never empty here.
    _srs_rtc_manager->add_with_name(username, session);

    return err;
}

SrsRtcConnection* SrsRtcServer::find_session_by_username(const std::string& username)
{
    ISrsResource* conn = _srs_rtc_manager->find_by_name(username);
    return dynamic_cast<SrsRtcConnection*>(conn);
}

srs_error_t SrsRtcServer::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    // Alive RTC sessions, for stat.
    int nn_rtc_conns = 0;

    // Check all sessions and dispose the dead sessions.
    for (int i = 0; i < (int)_srs_rtc_manager->size(); i++) {
        SrsRtcConnection* session = dynamic_cast<SrsRtcConnection*>(_srs_rtc_manager->at(i));
        // Ignore not session, or already disposing.
        if (!session || session->disposing_) {
            continue;
        }

        // Update stat if session is alive.
        if (session->is_alive()) {
            nn_rtc_conns++;
            SrsStatistic::instance()->kbps_add_delta(session->get_id().c_str(), session->delta());
            continue;
        }

        SrsContextRestore(_srs_context->get_id());
        session->switch_to_context();

        string username = session->username();
        srs_trace("RTC: session destroy by timeout, username=%s", username.c_str());

        // Use manager to free session and notify other objects.
        _srs_rtc_manager->remove(session);
    }

    // Ignore stats if no RTC connections.
    if (!nn_rtc_conns) {
        return err;
    }
    static char buf[128];

    string rpkts_desc;
    _srs_pps_rpkts->update(); _srs_pps_rrtps->update(); _srs_pps_rstuns->update(); _srs_pps_rrtcps->update();
    if (_srs_pps_rpkts->r10s() || _srs_pps_rrtps->r10s() || _srs_pps_rstuns->r10s() || _srs_pps_rrtcps->r10s()) {
        snprintf(buf, sizeof(buf), ", rpkts=(%d,rtp:%d,stun:%d,rtcp:%d)", _srs_pps_rpkts->r10s(), _srs_pps_rrtps->r10s(), _srs_pps_rstuns->r10s(), _srs_pps_rrtcps->r10s());
        rpkts_desc = buf;
    }

    string spkts_desc;
    _srs_pps_spkts->update(); _srs_pps_srtps->update(); _srs_pps_sstuns->update(); _srs_pps_srtcps->update();
    if (_srs_pps_spkts->r10s() || _srs_pps_srtps->r10s() || _srs_pps_sstuns->r10s() || _srs_pps_srtcps->r10s()) {
        snprintf(buf, sizeof(buf), ", spkts=(%d,rtp:%d,stun:%d,rtcp:%d)", _srs_pps_spkts->r10s(), _srs_pps_srtps->r10s(), _srs_pps_sstuns->r10s(), _srs_pps_srtcps->r10s());
        spkts_desc = buf;
    }

    string rtcp_desc;
    _srs_pps_pli->update(); _srs_pps_twcc->update(); _srs_pps_rr->update();
    if (_srs_pps_pli->r10s() || _srs_pps_twcc->r10s() || _srs_pps_rr->r10s()) {
        snprintf(buf, sizeof(buf), ", rtcp=(pli:%d,twcc:%d,rr:%d)", _srs_pps_pli->r10s(), _srs_pps_twcc->r10s(), _srs_pps_rr->r10s());
        rtcp_desc = buf;
    }

    string snk_desc;
    _srs_pps_snack->update(); _srs_pps_snack2->update(); _srs_pps_sanack->update(); _srs_pps_svnack->update();
    if (_srs_pps_snack->r10s() || _srs_pps_sanack->r10s() || _srs_pps_svnack->r10s() || _srs_pps_snack2->r10s()) {
        snprintf(buf, sizeof(buf), ", snk=(%d,a:%d,v:%d,h:%d)", _srs_pps_snack->r10s(), _srs_pps_sanack->r10s(), _srs_pps_svnack->r10s(), _srs_pps_snack2->r10s());
        snk_desc = buf;
    }

    string rnk_desc;
    _srs_pps_rnack->update(); _srs_pps_rnack2->update(); _srs_pps_rhnack->update(); _srs_pps_rmnack->update();
    if (_srs_pps_rnack->r10s() || _srs_pps_rnack2->r10s() || _srs_pps_rhnack->r10s() || _srs_pps_rmnack->r10s()) {
        snprintf(buf, sizeof(buf), ", rnk=(%d,%d,h:%d,m:%d)", _srs_pps_rnack->r10s(), _srs_pps_rnack2->r10s(), _srs_pps_rhnack->r10s(), _srs_pps_rmnack->r10s());
        rnk_desc = buf;
    }

    string loss_desc;
    SrsSnmpUdpStat* s = srs_get_udp_snmp_stat();
    if (s->rcv_buf_errors_delta || s->snd_buf_errors_delta) {
        snprintf(buf, sizeof(buf), ", loss=(r:%d,s:%d)", s->rcv_buf_errors_delta, s->snd_buf_errors_delta);
        loss_desc = buf;
    }

    string fid_desc;
    _srs_pps_ids->update(); _srs_pps_fids->update(); _srs_pps_fids_level0->update(); _srs_pps_addrs->update(); _srs_pps_fast_addrs->update();
    if (_srs_pps_ids->r10s(), _srs_pps_fids->r10s(), _srs_pps_fids_level0->r10s(), _srs_pps_addrs->r10s(), _srs_pps_fast_addrs->r10s()) {
        snprintf(buf, sizeof(buf), ", fid=(id:%d,fid:%d,ffid:%d,addr:%d,faddr:%d)", _srs_pps_ids->r10s(), _srs_pps_fids->r10s(), _srs_pps_fids_level0->r10s(), _srs_pps_addrs->r10s(), _srs_pps_fast_addrs->r10s());
        fid_desc = buf;
    }

    srs_trace("RTC: Server conns=%u%s%s%s%s%s%s%s",
        nn_rtc_conns,
        rpkts_desc.c_str(), spkts_desc.c_str(), rtcp_desc.c_str(), snk_desc.c_str(), rnk_desc.c_str(), loss_desc.c_str(), fid_desc.c_str()
    );

    return err;
}

RtcServerAdapter::RtcServerAdapter()
{
    rtc = new SrsRtcServer();
}

RtcServerAdapter::~RtcServerAdapter()
{
    srs_freep(rtc);
}

srs_error_t RtcServerAdapter::initialize()
{
    srs_error_t err = srs_success;

    if ((err = _srs_rtc_dtls_certificate->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    if ((err = rtc->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc server initialize");
    }

    return err;
}

srs_error_t RtcServerAdapter::run(SrsWaitGroup* wg)
{
    srs_error_t err = srs_success;

    if ((err = rtc->listen_udp()) != srs_success) {
        return srs_error_wrap(err, "listen udp");
    }

    if ((err = rtc->listen_api()) != srs_success) {
        return srs_error_wrap(err, "listen api");
    }

    if ((err = _srs_rtc_manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }

    return err;
}

void RtcServerAdapter::stop()
{
}

SrsResourceManager* _srs_rtc_manager = NULL;

