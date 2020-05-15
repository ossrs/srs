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

#ifndef SRS_APP_RTC_SERVER_HPP
#define SRS_APP_RTC_SERVER_HPP

#include <srs_core.hpp>

#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_hybrid.hpp>

#include <string>

class SrsRtcServer;
class SrsHourGlass;
class SrsRtcSession;
class SrsRequest;
class SrsSdp;

class SrsUdpMuxSender : virtual public ISrsUdpSender, virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler
{
private:
    srs_netfd_t lfd;
    SrsRtcServer* server;
    SrsCoroutine* trd;
private:
    srs_cond_t cond;
    bool waiting_msgs;
    bool gso;
    int nn_senders;
private:
    // Hotspot msgs, we are working on it.
    // @remark We will wait util all messages are ready.
    std::vector<srs_mmsghdr> hotspot;
    // Cache msgs, for other coroutines to fill it.
    std::vector<srs_mmsghdr> cache;
    int cache_pos;
    // The max number of messages for sendmmsg. If 1, we use sendmsg to send.
    int max_sendmmsg;
    // The total queue length, for each sender.
    int queue_length;
    // The extra queue ratio.
    int extra_ratio;
    int extra_queue;
public:
    SrsUdpMuxSender(SrsRtcServer* s);
    virtual ~SrsUdpMuxSender();
public:
    virtual srs_error_t initialize(srs_netfd_t fd, int senders);
private:
    void free_mhdrs(std::vector<srs_mmsghdr>& mhdrs);
public:
    virtual srs_error_t fetch(srs_mmsghdr** pphdr);
    virtual srs_error_t sendmmsg(srs_mmsghdr* hdr);
    virtual bool overflow();
    virtual void set_extra_ratio(int r);
public:
    virtual srs_error_t cycle();
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_rtc_server();
};

class SrsRtcServer : virtual public ISrsUdpMuxHandler, virtual public ISrsHourGlass
{
private:
    SrsHourGlass* timer;
    std::vector<SrsUdpMuxListener*> listeners;
    std::vector<SrsUdpMuxSender*> senders;
private:
    std::map<std::string, SrsRtcSession*> map_username_session; // key: username(local_ufrag + ":" + remote_ufrag)
    std::map<std::string, SrsRtcSession*> map_id_session; // key: peerip(ip + ":" + port)
public:
    SrsRtcServer();
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();
public:
    // TODO: FIXME: Support gracefully quit.
    // TODO: FIXME: Support reload.
    srs_error_t listen_udp();
    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket* skt);
    srs_error_t listen_api();
public:
    // Peer start offering, we answer it.
    srs_error_t create_session(
        SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp, const std::string& mock_eip, bool publish,
        SrsRtcSession** psession
    );
    // We start offering, create_session2 to generate offer, setup_session2 to handle answer.
    srs_error_t create_session2(SrsSdp& local_sdp, SrsRtcSession** psession);
    srs_error_t setup_session2(SrsRtcSession* session, SrsRequest* req, const SrsSdp& remote_sdp);
public:
    bool insert_into_id_sessions(const std::string& peer_id, SrsRtcSession* session);
    void check_and_clean_timeout_session();
    int nn_sessions();
    SrsRtcSession* find_session_by_username(const std::string& ufrag);
private:
    SrsRtcSession* find_session_by_peer_id(const std::string& peer_id);
// interface ISrsHourGlass
public:
    virtual srs_error_t notify(int type, srs_utime_t interval, srs_utime_t tick);
};

// The RTC server adapter.
class RtcServerAdapter : public ISrsHybridServer
{
private:
    SrsRtcServer* rtc;
public:
    RtcServerAdapter();
    virtual ~RtcServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();
};

#endif

