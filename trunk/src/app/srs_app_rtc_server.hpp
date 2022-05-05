//
// Copyright (c) 2013-2021 John
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_SERVER_HPP
#define SRS_APP_RTC_SERVER_HPP

#include <srs_core.hpp>

#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_async_call.hpp>

#include <string>

class SrsRtcServer;
class SrsHourGlass;
class SrsRtcConnection;
class SrsRequest;
class SrsSdp;
class SrsRtcSource;
class SrsResourceManager;
class SrsWaitGroup;

// The UDP black hole, for developer to use wireshark to catch plaintext packets.
// For example, server receive UDP packets at udp://8000, and forward the plaintext packet to black hole,
// we can use wireshark to capture the plaintext.
class SrsRtcBlackhole
{
public:
    bool blackhole;
private:
    sockaddr_in* blackhole_addr;
    srs_netfd_t blackhole_stfd;
public:
    SrsRtcBlackhole();
    virtual ~SrsRtcBlackhole();
public:
    srs_error_t initialize();
    void sendto(void* data, int len);
};

extern SrsRtcBlackhole* _srs_blackhole;

// The handler for RTC server to call.
class ISrsRtcServerHandler
{
public:
    ISrsRtcServerHandler();
    virtual ~ISrsRtcServerHandler();
public:
    // When server detect the timeout for session object.
    virtual void on_timeout(SrsRtcConnection* session) = 0;
};

// The hijacker to hook server.
class ISrsRtcServerHijacker
{
public:
    ISrsRtcServerHijacker();
    virtual ~ISrsRtcServerHijacker();
public:
    // If consumed set to true, server will ignore the packet.
    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket* skt, SrsRtcConnection* session, bool* pconsumed) = 0;
};

// The user config for RTC publish or play.
class SrsRtcUserConfig
{
public:
    // Original variables from API.
    SrsSdp remote_sdp_;
    std::string eip_;
    std::string codec_;
    std::string api_;

    // Generated data.
    SrsRequest* req_;
    bool publish_;
    bool dtls_;
    bool srtp_;
public:
    SrsRtcUserConfig();
    virtual ~SrsRtcUserConfig();
};

// The RTC server instance, listen UDP port, handle UDP packet, manage RTC connections.
class SrsRtcServer : public ISrsUdpMuxHandler, public ISrsFastTimer, public ISrsReloadHandler
{
private:
    std::vector<SrsUdpMuxListener*> listeners;
    ISrsRtcServerHandler* handler;
    ISrsRtcServerHijacker* hijacker;
    SrsAsyncCallWorker* async;
public:
    SrsRtcServer();
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_rtc_server();
public:
    // Set the handler for server events.
    void set_handler(ISrsRtcServerHandler* h);
    void set_hijacker(ISrsRtcServerHijacker* h);
    srs_error_t exec_async_work(ISrsAsyncCallTask* t);
public:
    // TODO: FIXME: Support gracefully quit.
    // TODO: FIXME: Support reload.
    srs_error_t listen_udp();
    virtual srs_error_t on_udp_packet(SrsUdpMuxSocket* skt);
    srs_error_t listen_api();
public:
    // Peer start offering, we answer it.
    srs_error_t create_session(SrsRtcUserConfig* ruc, SrsSdp& local_sdp, SrsRtcConnection** psession);
private:
    srs_error_t do_create_session(SrsRtcUserConfig* ruc, SrsSdp& local_sdp, SrsRtcConnection* session);
public:
    SrsRtcConnection* find_session_by_username(const std::string& ufrag);
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
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
    virtual srs_error_t run(SrsWaitGroup* wg);
    virtual void stop();
};

// Manager for RTC connections.
extern SrsResourceManager* _srs_rtc_manager;

#endif

