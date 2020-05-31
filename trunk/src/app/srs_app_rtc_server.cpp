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
        if (!ip->is_loopback) {
            continue;
        }

        if (family == "ipv4" && !ip->is_ipv4) {
            continue;
        }
        if (family == "ipv6" && ip->is_ipv4) {
            continue;
        }

        candidate_ips.push_back(ip->ip);
        srs_warn("Best matched ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
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
        srs_warn("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
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

SrsUdpMuxSender::SrsUdpMuxSender(SrsRtcServer* s)
{
    lfd = NULL;
    server = s;

    waiting_msgs = false;
    cond = srs_cond_new();
    trd = new SrsDummyCoroutine();

    cache_pos = 0;
    max_sendmmsg = 0;
    queue_length = 0;
    extra_ratio = 0;
    extra_queue = 0;
    gso = false;
    nn_senders = 0;

    _srs_config->subscribe(this);
}

SrsUdpMuxSender::~SrsUdpMuxSender()
{
    _srs_config->unsubscribe(this);

    srs_freep(trd);
    srs_cond_destroy(cond);

    free_mhdrs(hotspot);
    hotspot.clear();

    free_mhdrs(cache);
    cache.clear();
}

srs_error_t SrsUdpMuxSender::initialize(srs_netfd_t fd, int senders)
{
    srs_error_t err = srs_success;

    lfd = fd;

    srs_freep(trd);
    trd = new SrsSTCoroutine("udp", this);
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    max_sendmmsg = _srs_config->get_rtc_server_sendmmsg();
    gso = _srs_config->get_rtc_server_gso();
    queue_length = srs_max(128, _srs_config->get_rtc_server_queue_length());
    nn_senders = senders;

    // For no GSO, we need larger queue.
    if (!gso) {
        queue_length *= 2;
    }

    srs_trace("RTC sender #%d init ok, max_sendmmsg=%d, gso=%d, queue_max=%dx%d, extra_ratio=%d/%d", srs_netfd_fileno(fd),
        max_sendmmsg, gso, queue_length, nn_senders, extra_ratio, extra_queue);

    return err;
}

void SrsUdpMuxSender::free_mhdrs(std::vector<srs_mmsghdr>& mhdrs)
{
    int nn_mhdrs = (int)mhdrs.size();
    for (int i = 0; i < nn_mhdrs; i++) {
        // @see https://linux.die.net/man/2/sendmmsg
        // @see https://linux.die.net/man/2/sendmsg
        srs_mmsghdr* hdr = &mhdrs[i];

        // Free control for GSO.
        char* msg_control = (char*)hdr->msg_hdr.msg_control;
        srs_freepa(msg_control);

        // Free iovec.
        for (int j = SRS_PERF_RTC_GSO_MAX - 1; j >= 0 ; j--) {
            iovec* iov = hdr->msg_hdr.msg_iov + j;
            char* data = (char*)iov->iov_base;
            srs_freepa(data);
            srs_freepa(iov);
        }
    }
    mhdrs.clear();
}

srs_error_t SrsUdpMuxSender::fetch(srs_mmsghdr** pphdr)
{
    // TODO: FIXME: Maybe need to shrink?
    if (cache_pos >= (int)cache.size()) {
        // @see https://linux.die.net/man/2/sendmmsg
        // @see https://linux.die.net/man/2/sendmsg
        srs_mmsghdr mhdr;

        mhdr.msg_len = 0;
        mhdr.msg_hdr.msg_flags = 0;
        mhdr.msg_hdr.msg_control = NULL;

        mhdr.msg_hdr.msg_iovlen = SRS_PERF_RTC_GSO_MAX;
        mhdr.msg_hdr.msg_iov = new iovec[mhdr.msg_hdr.msg_iovlen];
        memset((void*)mhdr.msg_hdr.msg_iov, 0, sizeof(iovec) * mhdr.msg_hdr.msg_iovlen);

        for (int i = 0; i < SRS_PERF_RTC_GSO_IOVS; i++) {
            iovec* p = mhdr.msg_hdr.msg_iov + i;
            p->iov_base = new char[kRtpPacketSize];
        }

        cache.push_back(mhdr);
    }

    *pphdr = &cache[cache_pos++];
    return srs_success;
}

bool SrsUdpMuxSender::overflow()
{
    return cache_pos > queue_length + extra_queue;
}

void SrsUdpMuxSender::set_extra_ratio(int r)
{
    // We use the larger extra ratio, because all vhosts shares the senders.
    if (extra_ratio > r) {
        return;
    }

    extra_ratio = r;
    extra_queue = queue_length * r / 100;

    srs_trace("RTC sender #%d extra queue, max_sendmmsg=%d, gso=%d, queue_max=%dx%d, extra_ratio=%d/%d, cache=%d/%d/%d", srs_netfd_fileno(lfd),
        max_sendmmsg, gso, queue_length, nn_senders, extra_ratio, extra_queue, cache_pos, (int)cache.size(), (int)hotspot.size());
}

srs_error_t SrsUdpMuxSender::sendmmsg(srs_mmsghdr* hdr)
{
    if (waiting_msgs) {
        waiting_msgs = false;
        srs_cond_signal(cond);
    }

    return srs_success;
}

srs_error_t SrsUdpMuxSender::cycle()
{
    srs_error_t err = srs_success;

    uint64_t nn_msgs = 0; uint64_t nn_msgs_last = 0; int nn_msgs_max = 0;
    uint64_t nn_bytes = 0; int nn_bytes_max = 0;
    uint64_t nn_gso_msgs = 0; uint64_t nn_gso_iovs = 0; int nn_gso_msgs_max = 0; int nn_gso_iovs_max = 0;
    int nn_loop = 0; int nn_wait = 0;
    srs_utime_t time_last = srs_get_system_time();

    bool stat_enabled = _srs_config->get_rtc_server_perf_stat();
    SrsStatistic* stat = SrsStatistic::instance();

    SrsPithyPrint* pprint = SrsPithyPrint::create_rtc_send(srs_netfd_fileno(lfd));
    SrsAutoFree(SrsPithyPrint, pprint);

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return err;
        }

        nn_loop++;

        int pos = cache_pos;
        int gso_iovs = 0;
        if (pos <= 0) {
            waiting_msgs = true;
            nn_wait++;
            srs_cond_wait(cond);
            continue;
        }

        // We are working on hotspot now.
        cache.swap(hotspot);
        cache_pos = 0;

        int gso_pos = 0;
        int nn_writen = 0;
        if (pos > 0) {
            // Send out all messages.
            // @see https://linux.die.net/man/2/sendmmsg
            // @see https://linux.die.net/man/2/sendmsg
            srs_mmsghdr* p = &hotspot[0]; srs_mmsghdr* end = p + pos;
            for (p = &hotspot[0]; p < end; p += max_sendmmsg) {
                int vlen = (int)(end - p);
                vlen = srs_min(max_sendmmsg, vlen);

                int r0 = srs_sendmmsg(lfd, p, (unsigned int)vlen, 0, SRS_UTIME_NO_TIMEOUT);
                if (r0 != vlen) {
                    srs_warn("sendmmsg %d msgs, %d done", vlen, r0);
                }

                if (stat_enabled) {
                    stat->perf_on_sendmmsg_packets(vlen);
                }
            }

            // Collect informations for GSO.
            if (stat_enabled) {
                // Stat the messages, iovs and bytes.
                // @see https://linux.die.net/man/2/sendmmsg
                // @see https://linux.die.net/man/2/sendmsg
                for (int i = 0; i < pos; i++) {
                    srs_mmsghdr* mhdr = &hotspot[i];

                    nn_writen += (int)mhdr->msg_len;

                    int real_iovs = mhdr->msg_hdr.msg_iovlen;
                    gso_pos++; nn_gso_msgs++; nn_gso_iovs += real_iovs;
                    gso_iovs += real_iovs;
                }
            }
        }

        if (!stat_enabled) {
            continue;
        }

        // Increase total messages.
        nn_msgs += pos + gso_iovs;
        nn_msgs_max = srs_max(pos, nn_msgs_max);
        nn_bytes += nn_writen;
        nn_bytes_max = srs_max(nn_bytes_max, nn_writen);
        nn_gso_msgs_max = srs_max(gso_pos, nn_gso_msgs_max);
        nn_gso_iovs_max = srs_max(gso_iovs, nn_gso_iovs_max);

        pprint->elapse();
        if (pprint->can_print()) {
            // TODO: FIXME: Extract a PPS calculator.
            int pps_average = 0; int pps_last = 0;
            if (true) {
                if (srs_get_system_time() > srs_get_system_startup_time()) {
                    pps_average = (int)(nn_msgs * SRS_UTIME_SECONDS / (srs_get_system_time() - srs_get_system_startup_time()));
                }
                if (srs_get_system_time() > time_last) {
                    pps_last = (int)((nn_msgs - nn_msgs_last) * SRS_UTIME_SECONDS / (srs_get_system_time() - time_last));
                }
            }

            string pps_unit = "";
            if (pps_last > 10000 || pps_average > 10000) {
                pps_unit = "(w)"; pps_last /= 10000; pps_average /= 10000;
            } else if (pps_last > 1000 || pps_average > 1000) {
                pps_unit = "(k)"; pps_last /= 1000; pps_average /= 1000;
            }

            int nn_cache = 0;
            int nn_hotspot_size = (int)hotspot.size();
            for (int i = 0; i < nn_hotspot_size; i++) {
                srs_mmsghdr* hdr = &hotspot[i];
                nn_cache += hdr->msg_hdr.msg_iovlen;
            }

            srs_trace("-> RTC SEND #%d, sessions %d, udp %d/%d/%" PRId64 ", gso %d/%d/%" PRId64 ", iovs %d/%d/%" PRId64 ", pps %d/%d%s, cache %d/%d, bytes %d/%" PRId64,
                srs_netfd_fileno(lfd), (int)server->nn_sessions(), pos, nn_msgs_max, nn_msgs, gso_pos, nn_gso_msgs_max, nn_gso_msgs, gso_iovs,
                nn_gso_iovs_max, nn_gso_iovs, pps_average, pps_last, pps_unit.c_str(), (int)hotspot.size(), nn_cache, nn_bytes_max, nn_bytes);
            nn_msgs_last = nn_msgs; time_last = srs_get_system_time();
            nn_loop = nn_wait = nn_msgs_max = 0;
            nn_gso_msgs_max = 0; nn_gso_iovs_max = 0;
            nn_bytes_max = 0;
        }
    }

    return err;
}

srs_error_t SrsUdpMuxSender::on_reload_rtc_server()
{
    if (true) {
        int v = _srs_config->get_rtc_server_sendmmsg();
        if (max_sendmmsg != v) {
            srs_trace("Reload max_sendmmsg %d=>%d", max_sendmmsg, v);
            max_sendmmsg = v;
        }
    }

    return srs_success;
}

SrsRtcServer::SrsRtcServer()
{
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
        vector<SrsUdpMuxSender*>::iterator it;
        for (it = senders.begin(); it != senders.end(); ++it) {
            SrsUdpMuxSender* sender = *it;
            srs_freep(sender);
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
        SrsUdpMuxSender* sender = new SrsUdpMuxSender(this);
        SrsUdpMuxListener* listener = new SrsUdpMuxListener(this, sender, ip, port);

        if ((err = listener->listen()) != srs_success) {
            srs_freep(listener);
            return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
        }

        if ((err = sender->initialize(listener->stfd(), nn_listeners)) != srs_success) {
            return srs_error_wrap(err, "init sender");
        }

        srs_trace("rtc listen at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());
        listeners.push_back(listener);
        senders.push_back(sender);
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

    int cid = _srs_context->get_id();
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
    local_sdp.set_fingerprint(SrsDtls::instance()->get_fingerprint());

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
    local_sdp.set_fingerprint(SrsDtls::instance()->get_fingerprint());

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

    int cid = _srs_context->get_id();
    if ((err = session->initialize(source, req, false, username, cid)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    map_username_session.insert(make_pair(username, session));

    session->set_remote_sdp(remote_sdp);
    session->set_state(WAITING_STUN);

    return err;
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
        if (session == NULL) {
            map_username_session.erase(iter++);
            continue;
        }

        if (session->is_stun_timeout()) {
            // Now, we got the RTC session to cleanup, switch to its context
            // to make all logs write to the "correct" pid+cid.
            session->switch_to_context();

            srs_trace("rtc session=%s, STUN timeout", session->id().c_str());
            map_username_session.erase(iter++);
            map_id_session.erase(session->peer_id());
            delete session;
            continue;
        }

        ++iter;
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
    check_and_clean_timeout_session();
    return srs_success;
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

