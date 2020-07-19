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
class SrsRtcConnection;
class SrsRequest;
class SrsSdp;
class SrsRtcStream;

class ISrsRtcServerHandler
{
public:
    ISrsRtcServerHandler();
    virtual ~ISrsRtcServerHandler();
public:
    // When server detect the timeout for session object.
    virtual void on_timeout(SrsRtcConnection* session) = 0;
};

class SrsRtcServer : virtual public ISrsUdpMuxHandler, virtual public ISrsHourGlass
{
private:
    SrsHourGlass* timer;
    std::vector<SrsUdpMuxListener*> listeners;
    ISrsRtcServerHandler* handler;
private:
    std::map<std::string, SrsRtcConnection*> map_username_session; // key: username(local_ufrag + ":" + remote_ufrag)
    std::map<std::string, SrsRtcConnection*> map_id_session; // key: peerip(ip + ":" + port)
    // The zombie sessions, we will free them.
    std::vector<SrsRtcConnection*> zombies_;
public:
    SrsRtcServer();
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();
    // Set the handler for server events.
    void set_handler(ISrsRtcServerHandler* h);
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
        SrsRtcConnection** psession
    );
private:
    srs_error_t do_create_session(
        SrsRtcConnection* session, SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp,
        const std::string& mock_eip, bool publish, SrsRtcStream* source
    );
public:
    // We start offering, create_session2 to generate offer, setup_session2 to handle answer.
    srs_error_t create_session2(SrsRequest* req, SrsSdp& local_sdp, const std::string& mock_eip, SrsRtcConnection** psession);
    srs_error_t setup_session2(SrsRtcConnection* session, SrsRequest* req, const SrsSdp& remote_sdp);
    // Destroy the session from server.
    void destroy(SrsRtcConnection* session);
public:
    bool insert_into_id_sessions(const std::string& peer_id, SrsRtcConnection* session);
    void check_and_clean_timeout_session();
    int nn_sessions();
    SrsRtcConnection* find_session_by_username(const std::string& ufrag);
private:
    SrsRtcConnection* find_session_by_peer_id(const std::string& peer_id);
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

