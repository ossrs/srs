//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_SRT_SERVER_HPP
#define SRS_APP_SRT_SERVER_HPP

#include <srs_core.hpp>

#include <srs_protocol_srt.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_listener.hpp>

class SrsSrtServer;
class SrsHourGlass;

// A common srt acceptor, for SRT server.
class SrsSrtAcceptor : public ISrsSrtHandler
{
private:
    std::string ip_;
    int port_;
    SrsSrtServer* srt_server_;
private:
    SrsSrtListener* listener_;
public:
    SrsSrtAcceptor(SrsSrtServer* srt_server);
    virtual ~SrsSrtAcceptor();
public:
    virtual srs_error_t listen(std::string ip, int port);
private:
    virtual srs_error_t set_srt_opt();
// Interface ISrsSrtHandler
public:
    virtual srs_error_t on_srt_client(srs_srt_t srt_fd);
};

// SRS SRT server, initialize and listen, start connection service thread, destroy client.
class SrsSrtServer : public ISrsResourceManager, public ISrsHourGlass
{
private:
    SrsResourceManager* conn_manager_;
    SrsHourGlass* timer_;
private:
    std::vector<SrsSrtAcceptor*> acceptors_;
public:
    SrsSrtServer();
    virtual ~SrsSrtServer();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t listen();
private:
    // listen at specified srt protocol.
    virtual srs_error_t listen_srt_mpegts();
    // Close the listeners and remove the listen object from manager.
    virtual void close_listeners();
// For internal only
public:
    // When listener got a fd, notice server to accept it.
    // @param srt_fd, the client fd in srt boxed, the underlayer fd.
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd);
private:
    virtual srs_error_t fd_to_resource(srs_srt_t srt_fd, ISrsResource** pr);
// Interface ISrsResourceManager
public:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    virtual void remove(ISrsResource* c);
// interface ISrsHourGlass
private:
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);
private:
    virtual void resample_kbps();
};

// The srt server adapter, the master server.
class SrsSrtServerAdapter : public ISrsHybridServer
{
private:
    SrsSrtServer* srt_server_;
public:
    SrsSrtServerAdapter();
    virtual ~SrsSrtServerAdapter();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run(SrsWaitGroup* wg);
    virtual void stop();
public:
    virtual SrsSrtServer* instance();
};

// Start a coroutine to drive the SRT events with state-threads.
class SrsSrtEventLoop : public ISrsCoroutineHandler
{
public:
    SrsSrtEventLoop();
    virtual ~SrsSrtEventLoop();
public:
    ISrsSrtPoller* poller() { return srt_poller_; }
public:
    srs_error_t initialize();
    srs_error_t start();
// Interface ISrsCoroutineHandler.
public:
    virtual srs_error_t cycle();
private:
    ISrsSrtPoller* srt_poller_;
    SrsCoroutine* trd_;
};

// SrsSrtEventLoop is global singleton instance.
extern SrsSrtEventLoop* _srt_eventloop;

#endif

