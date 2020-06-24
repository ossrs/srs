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
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_api.hpp>

using namespace std;

static bool is_stun(const uint8_t* data, const int size)
{
    return data != NULL && size > 0 && (data[0] == 0 || data[0] == 1);
}

static bool is_dtls(const uint8_t* data, size_t len)
{
      return (len >= 13 && (data[0] > 19 && data[0] < 64));
}

static bool is_rtp_or_rtcp(const uint8_t* data, size_t len)
{
      return (len >= 12 && (data[0] & 0xC0) == 0x80);
}

static bool is_rtcp(const uint8_t* data, size_t len)
{
    return (len >= 12) && (data[0] & 0x80) && (data[1] >= 200 && data[1] <= 209);
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
        std::vector<SrsRtcSession*>::iterator it;
        for (it = zombies_.begin(); it != zombies_.end(); ++it) {
            SrsRtcSession* session = *it;
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

    char* data = skt->data(); int size = skt->size();
    SrsRtcSession* session = find_session_by_peer_id(skt->peer_id());

    if (session) {
        // Now, we got the RTC session to handle the packet, switch to its context
        // to make all logs write to the "correct" pid+cid.
        session->switch_to_context();
    }

    // For STUN, the peer address may change.
    if (is_stun((uint8_t*)data, size)) {
        SrsStunPacket ping;
        if ((err = ping.decode(data, size)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        srs_verbose("recv stun packet from %s, use-candidate=%d, ice-controlled=%d, ice-controlling=%d",
            skt->peer_id().c_str(), ping.get_use_candidate(), ping.get_ice_controlled(), ping.get_ice_controlling());

        // TODO: FIXME: For ICE trickle, we may get STUN packets before SDP answer, so maybe should response it.
        if (!session) {
            session = find_session_by_username(ping.get_username());
            if (session) {
                session->switch_to_context();
            }
        }
        if (session == NULL) {
            return srs_error_new(ERROR_RTC_STUN, "can not find session, stun username=%s, peer_id=%s",
                ping.get_username().c_str(), skt->peer_id().c_str());
        }

        return session->on_stun(skt, &ping);
    }

    // For DTLS, RTCP or RTP, which does not support peer address changing.
    if (session == NULL) {
        return srs_error_new(ERROR_RTC_STUN, "can not find session, peer_id=%s", skt->peer_id().c_str());
    }

    if (is_dtls((uint8_t*)data, size)) {
        return session->on_dtls(data, size);
    } else if (is_rtp_or_rtcp((uint8_t*)data, size)) {
        if (is_rtcp((uint8_t*)data, size)) {
            return session->on_rtcp(data, size);
        }
        return session->on_rtp(data, size);
    }

    return srs_error_new(ERROR_RTC_UDP, "unknown udp packet type");
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
    SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const std::string& mock_eip, bool publish,
    SrsRtcSession** psession
) {
    srs_error_t err = srs_success;

    SrsRtcSource* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    // TODO: FIXME: Refine the API for stream status manage.
    if (publish && !source->can_publish(false)) {
        return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req->get_stream_url().c_str());
    }

    std::string local_pwd = gen_random_str(32);
    std::string local_ufrag = "";
    // TODO: FIXME: Rename for a better name, it's not an username.
    std::string username = "";
    while (true) {
        local_ufrag = gen_random_str(8);

        username = local_ufrag + ":" + remote_sdp.get_ice_ufrag();
        if (!map_username_session.count(username)) {
            break;
        }
    }

    std::string cid = _srs_context->get_id();
    SrsRtcSession* session = new SrsRtcSession(this);
    if ((err = session->initialize(source, req, publish, username, cid)) != srs_success) {
        srs_freep(session);
        return srs_error_wrap(err, "init");
    }

    map_username_session.insert(make_pair(username, session));
    *psession = session;

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(_rtc_dtls_certificate->get_fingerprint());

    // We allows to mock the eip of server.
    if (!mock_eip.empty()) {
        local_sdp.add_candidate(mock_eip, _srs_config->get_rtc_server_listen(), "host");
    } else {
        std::vector<string> candidate_ips = get_candidate_ips();
        for (int i = 0; i < (int)candidate_ips.size(); ++i) {
            local_sdp.add_candidate(candidate_ips[i], _srs_config->get_rtc_server_listen(), "host");
        }
    }

    session->set_remote_sdp(remote_sdp);
    session->set_local_sdp(local_sdp);
    session->set_state(WAITING_STUN);

    return err;
}

srs_error_t SrsRtcServer::create_session2(SrsSdp& local_sdp, SrsRtcSession** psession)
{
    srs_error_t err = srs_success;

    std::string local_pwd = gen_random_str(32);
    // TODO: FIXME: Collision detect.
    std::string local_ufrag = gen_random_str(8);

    SrsRtcSession* session = new SrsRtcSession(this);
    *psession = session;

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(_rtc_dtls_certificate->get_fingerprint());

    // We allows to mock the eip of server.
    std::vector<string> candidate_ips = get_candidate_ips();
    for (int i = 0; i < (int)candidate_ips.size(); ++i) {
        local_sdp.add_candidate(candidate_ips[i], _srs_config->get_rtc_server_listen(), "host");
    }

    session->set_local_sdp(local_sdp);
    session->set_state(WAITING_ANSWER);

    return err;
}

srs_error_t SrsRtcServer::setup_session2(SrsRtcSession* session, SrsRequest* req, const SrsSdp& remote_sdp)
{
    srs_error_t err = srs_success;

    if (session->state() != WAITING_ANSWER) {
        return err;
    }

    SrsRtcSource* source = NULL;
    if ((err = _srs_rtc_sources->fetch_or_create(req, &source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    // TODO: FIXME: Collision detect.
    string username = session->get_local_sdp()->get_ice_ufrag() + ":" + remote_sdp.get_ice_ufrag();

    std::string cid = _srs_context->get_id();
    if ((err = session->initialize(source, req, false, username, cid)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    map_username_session.insert(make_pair(username, session));

    session->set_remote_sdp(remote_sdp);
    session->set_state(WAITING_STUN);

    return err;
}

void SrsRtcServer::destroy(SrsRtcSession* session)
{
    if (session->disposing_) {
        return;
    }
    session->disposing_ = true;

    std::map<std::string, SrsRtcSession*>::iterator it;

    if ((it = map_username_session.find(session->username())) != map_username_session.end()) {
        map_username_session.erase(it);
    }

    if ((it = map_id_session.find(session->peer_id())) != map_id_session.end()) {
        map_id_session.erase(it);
    }

    zombies_.push_back(session);
}

bool SrsRtcServer::insert_into_id_sessions(const string& peer_id, SrsRtcSession* session)
{
    return map_id_session.insert(make_pair(peer_id, session)).second;
}

void SrsRtcServer::check_and_clean_timeout_session()
{
    map<string, SrsRtcSession*>::iterator iter = map_username_session.begin();
    while (iter != map_username_session.end()) {
        SrsRtcSession* session = iter->second;
        srs_assert(session);

        if (!session->is_stun_timeout()) {
            ++iter;
            continue;
        }

        // Now, we got the RTC session to cleanup, switch to its context
        // to make all logs write to the "correct" pid+cid.
        session->switch_to_context();
        srs_trace("rtc session=%s, STUN timeout", session->id().c_str());

        session->disposing_ = true;
        zombies_.push_back(session);

        // Use C++98 style: https://stackoverflow.com/a/4636230
        map_username_session.erase(iter++);
        map_id_session.erase(session->peer_id());

        if (handler) {
            handler->on_timeout(session);
        }
    }
}

int SrsRtcServer::nn_sessions()
{
    return (int)map_username_session.size();
}

SrsRtcSession* SrsRtcServer::find_session_by_peer_id(const string& peer_id)
{
    map<string, SrsRtcSession*>::iterator iter = map_id_session.find(peer_id);
    if (iter == map_id_session.end()) {
        return NULL;
    }

    return iter->second;
}

SrsRtcSession* SrsRtcServer::find_session_by_username(const std::string& username)
{
    map<string, SrsRtcSession*>::iterator iter = map_username_session.find(username);
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

    std::vector<SrsRtcSession*> zombies;
    zombies.swap(zombies_);

    std::vector<SrsRtcSession*>::iterator it;
    for (it = zombies.begin(); it != zombies.end(); ++it) {
        SrsRtcSession* session = *it;
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
    
    if (_rtc_dtls_certificate) {
        srs_freep(_rtc_dtls_certificate);
    }
}

srs_error_t RtcServerAdapter::initialize()
{
    srs_error_t err = srs_success;

    if ((err = _rtc_dtls_certificate->initialize()) != srs_success) {
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

