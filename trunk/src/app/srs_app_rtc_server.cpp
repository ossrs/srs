//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_server.hpp>

#include <netdb.h>

#include <set>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_utility.hpp>

extern SrsPps *_srs_pps_rpkts;
extern SrsPps *_srs_pps_rstuns;
extern SrsPps *_srs_pps_rrtps;
extern SrsPps *_srs_pps_rrtcps;
extern SrsPps *_srs_pps_addrs;
extern SrsPps *_srs_pps_fast_addrs;

extern SrsPps *_srs_pps_spkts;
extern SrsPps *_srs_pps_sstuns;
extern SrsPps *_srs_pps_srtcps;
extern SrsPps *_srs_pps_srtps;

extern SrsPps *_srs_pps_ids;
extern SrsPps *_srs_pps_fids;
extern SrsPps *_srs_pps_fids_level0;

extern SrsPps *_srs_pps_pli;
extern SrsPps *_srs_pps_twcc;
extern SrsPps *_srs_pps_rr;

extern SrsPps *_srs_pps_snack;
extern SrsPps *_srs_pps_snack2;
extern SrsPps *_srs_pps_sanack;
extern SrsPps *_srs_pps_svnack;

extern SrsPps *_srs_pps_rnack;
extern SrsPps *_srs_pps_rnack2;
extern SrsPps *_srs_pps_rhnack;
extern SrsPps *_srs_pps_rmnack;

// TODO: FIXME: Replace by ST dns resolve.
string srs_dns_resolve(string host, int &family)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;

    addrinfo *r_raw = NULL;
    if (getaddrinfo(host.c_str(), NULL, &hints, &r_raw)) {
        return "";
    }
    SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

    char shost[64];
    memset(shost, 0, sizeof(shost));
    if (getnameinfo(r->ai_addr, r->ai_addrlen, shost, sizeof(shost), NULL, 0, NI_NUMERICHOST)) {
        return "";
    }

    family = r->ai_family;
    return string(shost);
}

// LCOV_EXCL_START
SrsRtcBlackhole::SrsRtcBlackhole()
{
    blackhole_ = false;
    blackhole_addr_ = NULL;
    blackhole_stfd_ = NULL;
}

SrsRtcBlackhole::~SrsRtcBlackhole()
{
    srs_close_stfd(blackhole_stfd_);
    srs_freep(blackhole_addr_);
}

srs_error_t SrsRtcBlackhole::initialize()
{
    srs_error_t err = srs_success;

    blackhole_ = _srs_config->get_rtc_server_black_hole();
    if (!blackhole_) {
        return err;
    }

    string blackhole_ep = _srs_config->get_rtc_server_black_hole_addr();
    if (blackhole_ep.empty()) {
        blackhole_ = false;
        srs_warn("disable black hole for no endpoint");
        return err;
    }

    string host;
    int port;
    srs_net_split_hostport(blackhole_ep, host, port);

    srs_freep(blackhole_addr_);
    blackhole_addr_ = new sockaddr_in();
    blackhole_addr_->sin_family = AF_INET;
    blackhole_addr_->sin_addr.s_addr = inet_addr(host.c_str());
    blackhole_addr_->sin_port = htons(port);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    blackhole_stfd_ = srs_netfd_open_socket(fd);
    srs_assert(blackhole_stfd_);

    srs_trace("RTC blackhole %s:%d, fd=%d", host.c_str(), port, fd);

    return err;
}

void SrsRtcBlackhole::sendto(void *data, int len)
{
    if (!blackhole_) {
        return;
    }

    // For blackhole, we ignore any error.
    srs_sendto(blackhole_stfd_, data, len, (sockaddr *)blackhole_addr_, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
}
// LCOV_EXCL_STOP

SrsRtcBlackhole *_srs_blackhole = NULL;

// @global dtls certficate for rtc module.
SrsDtlsCertificate *_srs_rtc_dtls_certificate = NULL;

// TODO: Should support error response.
// For STUN packet, 0x00 is binding request, 0x01 is binding success response.
bool srs_is_stun(const uint8_t *data, size_t size)
{
    return size > 0 && (data[0] == 0 || data[0] == 1);
}

// change_cipher_spec(20), alert(21), handshake(22), application_data(23)
// @see https://tools.ietf.org/html/rfc2246#section-6.2.1
bool srs_is_dtls(const uint8_t *data, size_t len)
{
    return (len >= 13 && (data[0] > 19 && data[0] < 64));
}

// For RTP or RTCP, the V=2 which is in the high 2bits, 0xC0 (1100 0000)
bool srs_is_rtp_or_rtcp(const uint8_t *data, size_t len)
{
    return (len >= 12 && (data[0] & 0xC0) == 0x80);
}

// For RTCP, PT is [128, 223] (or without marker [0, 95]).
// Literally, RTCP starts from 64 not 0, so PT is [192, 223] (or without marker [64, 95]).
// @note For RTP, the PT is [96, 127], or [224, 255] with marker.
bool srs_is_rtcp(const uint8_t *data, size_t len)
{
    return (len >= 12) && (data[0] & 0x80) && (data[1] >= 192 && data[1] <= 223);
}

srs_error_t api_server_as_candidates(ISrsAppConfig *config, string api, set<string> &candidate_ips)
{
    srs_error_t err = srs_success;

    if (api.empty() || !config->get_api_as_candidates()) {
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
    if (!srs_net_is_ipv4(hostname) && config->get_keep_api_domain()) {
        candidate_ips.insert(hostname);
    }

    // LCOV_EXCL_START
    // Try to parse the domain name if not IP.
    if (!srs_net_is_ipv4(hostname) && config->get_resolve_api_domain()) {
        int family = 0;
        string ip = srs_dns_resolve(hostname, family);
        if (ip.empty() || ip == SRS_CONSTS_LOCALHOST || ip == SRS_CONSTS_LOOPBACK || ip == SRS_CONSTS_LOOPBACK6) {
            return err;
        }

        // Try to add the API server ip as candidates.
        candidate_ips.insert(ip);
    }
    // LCOV_EXCL_STOP

    // If hostname is IP, use it.
    if (srs_net_is_ipv4(hostname)) {
        candidate_ips.insert(hostname);
    }

    return err;
}

set<string> discover_candidates(ISrsProtocolUtility *utility, ISrsAppConfig *config, SrsRtcUserConfig *ruc)
{
    srs_error_t err = srs_success;

    // Try to discover the eip as candidate, specified by user.
    set<string> candidate_ips;
    if (!ruc->eip_.empty()) {
        candidate_ips.insert(ruc->eip_);
    }

    // Try to discover from api of request, if api_as_candidates enabled.
    if ((err = api_server_as_candidates(config, ruc->req_->host_, candidate_ips)) != srs_success) {
        srs_warn("ignore discovering ip from api %s, err %s", ruc->req_->host_.c_str(), srs_error_summary(err).c_str());
        srs_freep(err);
    }

    // If not * or 0.0.0.0, use the candidate as exposed IP.
    string candidate = config->get_rtc_server_candidates();
    if (candidate != "*" && candidate != "0.0.0.0") {
        candidate_ips.insert(candidate);
        return candidate_ips;
    }

    // All automatically detected IP list.
    vector<SrsIPAddress *> &ips = utility->local_ips();
    if (ips.empty()) {
        return candidate_ips;
    }

    // Discover from local network interface addresses.
    if (config->get_use_auto_detect_network_ip()) {
        // We try to find the best match candidates, no loopback.
        string family = config->get_rtc_server_ip_family();
        for (int i = 0; i < (int)ips.size(); ++i) {
            SrsIPAddress *ip = ips[i];
            if (ip->is_loopback_) {
                continue;
            }

            if (family == "ipv4" && !ip->is_ipv4_) {
                continue;
            }
            if (family == "ipv6" && ip->is_ipv4_) {
                continue;
            }

            candidate_ips.insert(ip->ip_);
            srs_trace("Best matched ip=%s, ifname=%s", ip->ip_.c_str(), ip->ifname_.c_str());
        }
    }

    if (!candidate_ips.empty()) {
        return candidate_ips;
    }

    // LCOV_EXCL_START
    // Then, we use the ipv4 address.
    for (int i = 0; i < (int)ips.size(); ++i) {
        SrsIPAddress *ip = ips[i];
        if (!ip->is_ipv4_) {
            continue;
        }

        candidate_ips.insert(ip->ip_);
        srs_trace("No best matched, use first ip=%s, ifname=%s", ip->ip_.c_str(), ip->ifname_.c_str());
        return candidate_ips;
    }

    // We use the first one, to make sure there will be at least one CANDIDATE.
    if (candidate_ips.empty()) {
        SrsIPAddress *ip = ips[0];
        candidate_ips.insert(ip->ip_);
        srs_warn("No best matched, use first ip=%s, ifname=%s", ip->ip_.c_str(), ip->ifname_.c_str());
        return candidate_ips;
    }

    return candidate_ips;
    // LCOV_EXCL_STOP
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

SrsRtcSessionManager::SrsRtcSessionManager()
{
    rtc_async_ = new SrsAsyncCallWorker();

    conn_manager_ = _srs_conn_manager;
    stream_publish_tokens_ = _srs_stream_publish_tokens;
    rtc_sources_ = _srs_rtc_sources;
    dtls_certificate_ = _srs_rtc_dtls_certificate;
    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsRtcSessionManager::~SrsRtcSessionManager()
{
    rtc_async_->stop();
    srs_freep(rtc_async_);

    conn_manager_ = NULL;
    stream_publish_tokens_ = NULL;
    rtc_sources_ = NULL;
    dtls_certificate_ = NULL;
    config_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsRtcSessionManager::initialize()
{
    srs_error_t err = srs_success;

    if ((err = rtc_async_->start()) != srs_success) {
        return srs_error_wrap(err, "start async worker");
    }

    return err;
}

// LCOV_EXCL_START
ISrsRtcConnection *SrsRtcSessionManager::find_rtc_session_by_username(const std::string &username)
{
    ISrsResource *conn = conn_manager_->find_by_name(username);
    return dynamic_cast<ISrsRtcConnection *>(conn);
}

srs_error_t SrsRtcSessionManager::create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = ruc->req_;

    // Acquire stream publish token to prevent race conditions across all protocols.
    SrsStreamPublishToken *publish_token_raw = NULL;
    if (ruc->publish_ && (err = stream_publish_tokens_->acquire_token(req, publish_token_raw)) != srs_success) {
        return srs_error_wrap(err, "acquire stream publish token");
    }
    SrsSharedPtr<ISrsStreamPublishToken> publish_token(publish_token_raw);
    if (publish_token.get()) {
        srs_trace("stream publish token acquired, type=rtc, url=%s", req->get_stream_url().c_str());
    }

    SrsSharedPtr<SrsRtcSource> source;
    if ((err = rtc_sources_->fetch_or_create(req, source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if (ruc->publish_ && !source->can_publish()) {
        return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req->get_stream_url().c_str());
    }

    // TODO: FIXME: add do_create_session to error process.
    SrsContextId cid = _srs_context->get_id();
    ISrsRtcConnection *session = app_factory_->create_rtc_connection(this, cid);

    if ((err = do_create_rtc_session(ruc, local_sdp, session)) != srs_success) {
        srs_freep(session);
        return srs_error_wrap(err, "create session");
    }

    // Update publish token for publisher.
    if (ruc->publish_) {
        session->set_publish_token(publish_token);
    }

    *psession = session;

    return err;
}

srs_error_t SrsRtcSessionManager::do_create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection *session)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = ruc->req_;

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

    // Generate local SDP other fields.
    string username;
    if ((err = session->generate_local_sdp(ruc, local_sdp, username)) != srs_success) {
        return srs_error_wrap(err, "generate local sdp");
    }

    session->set_remote_sdp(ruc->remote_sdp_);
    // We must setup the local SDP, then initialize the session object.
    session->set_local_sdp(local_sdp);
    session->set_state_as_waiting_stun();

    // Before session initialize, we must setup the local SDP.
    if ((err = session->initialize(req, ruc->dtls_, ruc->srtp_, username)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    // We allows username is optional, but it never empty here.
    conn_manager_->add_with_name(username, session);

    return err;
}
// LCOV_EXCL_STOP

void SrsRtcSessionManager::srs_update_rtc_sessions()
{
    // Alive RTC sessions, for stat.
    int nn_rtc_conns = 0;

    // Check all sessions and dispose the dead sessions.
    for (int i = 0; i < (int)conn_manager_->size(); i++) {
        ISrsRtcConnection *session = dynamic_cast<ISrsRtcConnection *>(conn_manager_->at(i));
        // Ignore not session, or already disposing.
        if (!session || session->is_disposing()) {
            continue;
        }

        // Update stat if session is alive.
        if (session->is_alive()) {
            nn_rtc_conns++;
            continue;
        }

        SrsContextRestore(_srs_context->get_id());
        session->switch_to_context();

        string username = session->username();
        srs_trace("RTC: session destroy by timeout, username=%s", username.c_str());

        // Use manager to free session and notify other objects.
        conn_manager_->remove(session);
    }

    // Ignore stats if no RTC connections.
    if (!nn_rtc_conns) {
        return;
    }

    static char buf[128];

    string loss_desc;
    SrsSnmpUdpStat *s = srs_get_udp_snmp_stat();
    if (s->rcv_buf_errors_delta_ || s->snd_buf_errors_delta_) {
        snprintf(buf, sizeof(buf), ", loss=(r:%d,s:%d)", s->rcv_buf_errors_delta_, s->snd_buf_errors_delta_);
        loss_desc = buf;
    }

    SrsKbsRtcStats stats;
    srs_global_rtc_update(&stats);

    srs_trace("RTC: Server conns=%u%s%s%s%s%s%s%s",
              nn_rtc_conns,
              stats.rpkts_desc_.c_str(), stats.spkts_desc_.c_str(), stats.rtcp_desc_.c_str(), stats.snk_desc_.c_str(),
              stats.rnk_desc_.c_str(), loss_desc.c_str(), stats.fid_desc_.c_str());
}

// LCOV_EXCL_START
srs_error_t SrsRtcSessionManager::exec_rtc_async_work(ISrsAsyncCallTask *t)
{
    return rtc_async_->execute(t);
}

srs_error_t SrsRtcSessionManager::on_udp_packet(ISrsUdpMuxSocket *skt)
{
    srs_error_t err = srs_success;

    ISrsRtcConnection *session = NULL;
    char *data = skt->data();
    int size = skt->size();
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t *)data, size);
    bool is_rtcp = srs_is_rtcp((uint8_t *)data, size);

    uint64_t fast_id = skt->fast_id();
    // Try fast id first, if not found, search by long peer id.
    if (fast_id) {
        session = (ISrsRtcConnection *)conn_manager_->find_by_fast_id(fast_id);
    }
    if (!session) {
        string peer_id = skt->peer_id();
        session = (ISrsRtcConnection *)conn_manager_->find_by_id(peer_id);
    }

    if (session) {
        // When got any packet, the session is alive now.
        session->alive();
    }

    // For STUN, the peer address may change.
    if (!is_rtp_or_rtcp && srs_is_stun((uint8_t *)data, size)) {
        ++_srs_pps_rstuns->sugar_;
        string peer_id = skt->peer_id();

        // TODO: FIXME: Should support ICE renomination, to switch network between candidates.
        SrsStunPacket ping;
        if ((err = ping.decode(data, size)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        if (!session) {
            session = find_rtc_session_by_username(ping.get_username());
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
            SrsRtcUdpNetwork *udp_network = dynamic_cast<SrsRtcUdpNetwork *>(session->udp());
            srs_assert(udp_network);
            udp_network->update_sendonly_socket(skt);
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
        ++_srs_pps_rrtps->sugar_;

        err = session->udp()->on_rtp(data, size);
        if (err != srs_success) {
            session->switch_to_context();
        }
        return err;
    }

    session->switch_to_context();
    if (is_rtp_or_rtcp && is_rtcp) {
        ++_srs_pps_rrtcps->sugar_;

        return session->udp()->on_rtcp(data, size);
    }
    if (srs_is_dtls((uint8_t *)data, size)) {
        ++_srs_pps_rstuns->sugar_;

        return session->udp()->on_dtls(data, size);
    }
    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}
// LCOV_EXCL_STOP
