/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_rtc_server.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_rtc_stun_stack.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_server.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_service_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_protocol_utility.hpp>

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

SrsRtcBlackhole* _srs_blackhole = new SrsRtcBlackhole();

// @global dtls certficate for rtc module.
SrsDtlsCertificate* _srs_rtc_dtls_certificate = new SrsDtlsCertificate();

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

static std::vector<std::string> get_candidate_ips()
{
    std::vector<std::string> candidate_ips;

    string candidate = _srs_config->get_rtc_server_candidates();
    if (candidate != "*" && candidate != "0.0.0.0") {
        candidate_ips.push_back(candidate);
        return candidate_ips;
    }

    // For * or 0.0.0.0, auto discovery expose ip addresses.
    std::vector<SrsIPAddress*>& ips = srs_get_local_ips();
    if (ips.empty()) {
        return candidate_ips;
    }

    // We try to find the best match candidates, no loopback.
    string family = _srs_config->get_rtc_server_ip_family();
    for (int i = 0; i < (int)ips.size(); ++i) {
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

        candidate_ips.push_back(ip->ip);
        srs_trace("Best matched ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
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

        candidate_ips.push_back(ip->ip);
        srs_trace("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        return candidate_ips;
    }

    // We use the first one.
    if (candidate_ips.empty()) {
        SrsIPAddress* ip = ips[0];
        candidate_ips.push_back(ip->ip);
        srs_warn("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        return candidate_ips;
    }

    return candidate_ips;
}

ISrsRtcServerHandler::ISrsRtcServerHandler()
{
}

ISrsRtcServerHandler::~ISrsRtcServerHandler()
{
}

SrsRtcServer::SrsRtcServer()
{
    handler = NULL;
    timer = new SrsHourGlass(this, 1 * SRS_UTIME_SECONDS);
}

SrsRtcServer::~SrsRtcServer()
{
    srs_freep(timer);

    if (true) {
        vector<SrsUdpMuxListener*>::iterator it;
        for (it = listeners.begin(); it != listeners.end(); ++it) {
            SrsUdpMuxListener* listener = *it;
            srs_freep(listener);
        }
    }

    if (true) {
        std::vector<SrsRtcConnection*>::iterator it;
        for (it = zombies_.begin(); it != zombies_.end(); ++it) {
            SrsRtcConnection* session = *it;
            srs_freep(session);
        }
    }
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    if ((err = timer->tick(1 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "hourglass tick");
    }

    if ((err = timer->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = _srs_blackhole->initialize()) != srs_success) {
        return srs_error_wrap(err, "black hole");
    }

    srs_trace("RTC server init ok");

    return err;
}

void SrsRtcServer::set_handler(ISrsRtcServerHandler* h)
{
    handler = h;
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

    string peer_id = skt->peer_id();
    char* data = skt->data(); int size = skt->size();

    SrsRtcConnection* session = NULL;
    if (true) {
        map<string, SrsRtcConnection*>::iterator it = map_id_session.find(peer_id);
        if (it != map_id_session.end()) {
            session = it->second;

            // Switch to the session to write logs to the context.
            if (session) {
                session->switch_to_context();
            }
        }
    }

    // For STUN, the peer address may change.
    if (srs_is_stun((uint8_t*)data, size)) {
        SrsStunPacket ping;
        if ((err = ping.decode(data, size)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        srs_info("recv stun packet from %s, use-candidate=%d, ice-controlled=%d, ice-controlling=%d",
            peer_id.c_str(), ping.get_use_candidate(), ping.get_ice_controlled(), ping.get_ice_controlling());

        if (!session) {
            session = find_session_by_username(ping.get_username());

            // Switch to the session to write logs to the context.
            if (session) {
                session->switch_to_context();
            }
        }

        // TODO: FIXME: For ICE trickle, we may get STUN packets before SDP answer, so maybe should response it.
        if (!session) {
            return srs_error_new(ERROR_RTC_STUN, "no session, stun username=%s, peer_id=%s",
                ping.get_username().c_str(), peer_id.c_str());
        }

        return session->on_stun(skt, &ping);
    }

    // For DTLS, RTCP or RTP, which does not support peer address changing.
    if (!session) {
        return srs_error_new(ERROR_RTC_STUN, "no session, peer_id=%s", peer_id.c_str());
    }

    if (srs_is_dtls((uint8_t*)data, size)) {
        return session->on_dtls(data, size);
    } else if (srs_is_rtp_or_rtcp((uint8_t*)data, size)) {
        if (srs_is_rtcp((uint8_t*)data, size)) {
            return session->on_rtcp(data, size);
        }
        return session->on_rtp(data, size);
    }

    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}

srs_error_t SrsRtcServer::listen_api()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Fetch api from hybrid manager, not from SRS.
    SrsHttpServeMux* http_api_mux = _srs_hybrid->srs()->instance()->api_server();

    if ((err = http_api_mux->handle("/rtc/v1/play/", new SrsGoApiRtcPlay(this))) != srs_success) {
        return srs_error_wrap(err, "handle play");
    }

    if ((err = http_api_mux->handle("/rtc/v1/publish/", new SrsGoApiRtcPublish(this))) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

#ifdef SRS_SIMULATOR
    if ((err = http_api_mux->handle("/rtc/v1/nack/", new SrsGoApiRtcNACK(this))) != srs_success) {
        return srs_error_wrap(err, "handle nack");
    }
#endif

    return err;
}

srs_error_t SrsRtcServer::create_session(
    SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const std::string& mock_eip,
    bool publish, bool dtls, bool srtp,
    SrsRtcConnection** psession
) {
    srs_error_t err = srs_success;

    SrsContextId cid = _srs_context->get_id();

    SrsRtcStream* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    // TODO: FIXME: Refine the API for stream status manage.
    if (publish && !source->can_publish(false)) {
        return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req->get_stream_url().c_str());
    }

    // TODO: FIXME: add do_create_session to error process.
    SrsRtcConnection* session = new SrsRtcConnection(this, cid);
    if ((err = do_create_session(session, req, remote_sdp, local_sdp, mock_eip, publish, dtls, srtp, source)) != srs_success) {
        srs_freep(session);
        return srs_error_wrap(err, "create session");
    }

    *psession = session;

    return err;
}

srs_error_t SrsRtcServer::do_create_session(
    SrsRtcConnection* session, SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const std::string& mock_eip,
    bool publish, bool dtls, bool srtp, SrsRtcStream* source
)
{
    srs_error_t err = srs_success;

    // first add publisher/player for negotiate sdp media info
    if (publish) {
        if ((err = session->add_publisher(req, remote_sdp, local_sdp)) != srs_success) {
            return srs_error_wrap(err, "add publisher");
        }
    } else {
        if ((err = session->add_player(req, remote_sdp, local_sdp)) != srs_success) {
            return srs_error_wrap(err, "add player");
        }
    }

    // All tracks default as inactive, so we must enable them.
    session->set_all_tracks_status(true);

    std::string local_pwd = srs_random_str(32);
    std::string local_ufrag = "";
    // TODO: FIXME: Rename for a better name, it's not an username.
    std::string username = "";
    while (true) {
        local_ufrag = srs_random_str(8);

        username = local_ufrag + ":" + remote_sdp.get_ice_ufrag();
        if (!map_username_session.count(username)) {
            break;
        }
    }

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(_srs_rtc_dtls_certificate->get_fingerprint());

    // We allows to mock the eip of server.
    if (!mock_eip.empty()) {
        local_sdp.add_candidate(mock_eip, _srs_config->get_rtc_server_listen(), "host");
        srs_trace("RTC: Use candidate mock_eip %s", mock_eip.c_str());
    } else {
        std::vector<string> candidate_ips = get_candidate_ips();
        for (int i = 0; i < (int)candidate_ips.size(); ++i) {
            local_sdp.add_candidate(candidate_ips[i], _srs_config->get_rtc_server_listen(), "host");
        }
        srs_trace("RTC: Use candidates %s", srs_join_vector_string(candidate_ips, ", ").c_str());
    }

    if (remote_sdp.get_dtls_role() == "active") {
        local_sdp.set_dtls_role("passive");
    } else if (remote_sdp.get_dtls_role() == "passive") {
        local_sdp.set_dtls_role("active");
    } else if (remote_sdp.get_dtls_role() == "actpass") {
        local_sdp.set_dtls_role(local_sdp.session_config_.dtls_role);
    } else {
        // @see: https://tools.ietf.org/html/rfc4145#section-4.1
        // The default value of the setup attribute in an offer/answer exchange
        // is 'active' in the offer and 'passive' in the answer.
        local_sdp.set_dtls_role("passive");
    }

    session->set_remote_sdp(remote_sdp);
    // We must setup the local SDP, then initialize the session object.
    session->set_local_sdp(local_sdp);
    session->set_state(WAITING_STUN);

    // Before session initialize, we must setup the local SDP.
    if ((err = session->initialize(source, req, publish, dtls, srtp, username)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    map_username_session.insert(make_pair(username, session));
    return err;
}

srs_error_t SrsRtcServer::create_session2(SrsRequest* req, SrsSdp& local_sdp, const std::string& mock_eip, SrsRtcConnection** psession)
{
    srs_error_t err = srs_success;

    SrsContextId cid = _srs_context->get_id();

    std::string local_pwd = srs_random_str(32);
    // TODO: FIXME: Collision detect.
    std::string local_ufrag = srs_random_str(8);

    SrsRtcConnection* session = new SrsRtcConnection(this, cid);
    // first add player for negotiate local sdp media info
    if ((err = session->add_player2(req, local_sdp)) != srs_success) {
        srs_freep(session);
        return srs_error_wrap(err, "add player2");
    }
    *psession = session;

    local_sdp.set_dtls_role("actpass");
    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(_srs_rtc_dtls_certificate->get_fingerprint());

    // We allows to mock the eip of server.
    if (!mock_eip.empty()) {
        local_sdp.add_candidate(mock_eip, _srs_config->get_rtc_server_listen(), "host");
        srs_trace("RTC: Use candidate mock_eip %s", mock_eip.c_str());
    } else {
        std::vector<string> candidate_ips = get_candidate_ips();
        for (int i = 0; i < (int)candidate_ips.size(); ++i) {
            local_sdp.add_candidate(candidate_ips[i], _srs_config->get_rtc_server_listen(), "host");
        }
        srs_trace("RTC: Use candidates %s", srs_join_vector_string(candidate_ips, ", ").c_str());
    }

    session->set_local_sdp(local_sdp);
    session->set_state(WAITING_ANSWER);

    return err;
}

srs_error_t SrsRtcServer::setup_session2(SrsRtcConnection* session, SrsRequest* req, const SrsSdp& remote_sdp)
{
    srs_error_t err = srs_success;

    if (session->state() != WAITING_ANSWER) {
        return err;
    }

    SrsRtcStream* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    // TODO: FIXME: Collision detect.
    string username = session->get_local_sdp()->get_ice_ufrag() + ":" + remote_sdp.get_ice_ufrag();

    if ((err = session->initialize(source, req, false, true, true, username)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    map_username_session.insert(make_pair(username, session));

    session->set_remote_sdp(remote_sdp);
    session->set_state(WAITING_STUN);

    return err;
}

void SrsRtcServer::destroy(SrsRtcConnection* session)
{
    if (session->disposing_) {
        return;
    }
    session->disposing_ = true;

    std::map<std::string, SrsRtcConnection*>::iterator it;

    // We allows username is optional.
    string username = session->username();
    if (!username.empty() && (it = map_username_session.find(username)) != map_username_session.end()) {
        map_username_session.erase(it);
    }

    vector<SrsUdpMuxSocket*> addresses = session->peer_addresses();
    for (int i = 0; i < (int)addresses.size(); i++) {
        SrsUdpMuxSocket* addr = addresses.at(i);
        map_id_session.erase(addr->peer_id());
    }

    SrsContextRestore(_srs_context->get_id());
    session->switch_to_context();
    srs_trace("RTC: session destroy, username=%s, summary: %s", username.c_str(), session->stat_->summary().c_str());

    zombies_.push_back(session);
}

bool SrsRtcServer::insert_into_id_sessions(const string& peer_id, SrsRtcConnection* session)
{
    return map_id_session.insert(make_pair(peer_id, session)).second;
}

void SrsRtcServer::check_and_clean_timeout_session()
{
    map<string, SrsRtcConnection*>::iterator iter = map_username_session.begin();
    while (iter != map_username_session.end()) {
        SrsRtcConnection* session = iter->second;
        srs_assert(session);

        if (!session->is_stun_timeout()) {
            ++iter;
            continue;
        }

        // Now, we got the RTC session to cleanup, switch to its context
        // to make all logs write to the "correct" pid+cid.
        session->switch_to_context();
        srs_trace("RTC: session STUN timeout, summary: %s", session->stat_->summary().c_str());

        session->disposing_ = true;
        zombies_.push_back(session);

        // Use C++98 style: https://stackoverflow.com/a/4636230
        map_username_session.erase(iter++);

        vector<SrsUdpMuxSocket*> addresses = session->peer_addresses();
        for (int i = 0; i < (int)addresses.size(); i++) {
            SrsUdpMuxSocket* addr = addresses.at(i);
            map_id_session.erase(addr->peer_id());
        }

        if (handler) {
            handler->on_timeout(session);
        }
    }
}

SrsRtcConnection* SrsRtcServer::find_session_by_username(const std::string& username)
{
    map<string, SrsRtcConnection*>::iterator iter = map_username_session.find(username);
    if (iter == map_username_session.end()) {
        return NULL;
    }

    return iter->second;
}

srs_error_t SrsRtcServer::notify(int type, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    // Check session timeout, put to zombies queue.
    check_and_clean_timeout_session();

    // Cleanup zombie sessions.
    if (zombies_.empty()) {
        return err;
    }

    std::vector<SrsRtcConnection*> zombies;
    zombies.swap(zombies_);

    std::vector<SrsRtcConnection*>::iterator it;
    for (it = zombies.begin(); it != zombies.end(); ++it) {
        SrsRtcConnection* session = *it;
        srs_freep(session);
    }

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

srs_error_t RtcServerAdapter::run()
{
    srs_error_t err = srs_success;

    if ((err = rtc->listen_udp()) != srs_success) {
        return srs_error_wrap(err, "listen udp");
    }

    if ((err = rtc->listen_api()) != srs_success) {
        return srs_error_wrap(err, "listen api");
    }

    return err;
}

void RtcServerAdapter::stop()
{
}

