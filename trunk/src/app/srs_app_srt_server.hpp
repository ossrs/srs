//
// Copyright (c) 2013-2021 The SRS Authors
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

enum SrsSrtListenerType
{
    SrsSrtListenerMpegts = 1,
};

// A common srt acceptor, for SRT server.
class SrsSrtAcceptor
{
protected:
    SrsSrtListenerType type_;
protected:
    std::string ip_;
    int port_;
    SrsSrtServer* srt_server_;
public:
    SrsSrtAcceptor(SrsSrtServer* srt_server, SrsSrtListenerType listen_type);
    virtual ~SrsSrtAcceptor();
public:
    virtual SrsSrtListenerType listen_type();
    virtual srs_error_t listen(std::string ip, int port) = 0;
};

// A srt messge acceptor.
class SrsSrtMessageAcceptor : public SrsSrtAcceptor, public ISrsSrtHandler
{
private:
    SrsSrtListener* listener_;
public:
    SrsSrtMessageAcceptor(SrsSrtServer* srt_server, SrsSrtListenerType listen_type);
    virtual ~SrsSrtMessageAcceptor();
public:
    virtual srs_error_t listen(std::string i, int p);
    virtual srs_error_t set_srt_opt();
// Interface ISrsSrtHandler
public:
    virtual srs_error_t on_srt_client(SRTSOCKET srt_fd);
};

// SRS SRT server, initialize and listen, start connection service thread, destroy client.
class SrsSrtServer : public ISrsResourceManager
{
private:
    SrsResourceManager* conn_manager_;
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
    // Close the listeners for specified type,
    // Remove the listen object from manager.
    virtual void close_listeners(SrsSrtListenerType type);
// For internal only
public:
    // When listener got a fd, notice server to accept it.
    // @param type, the client type, used to create concrete connection,
    //       for instance SRT connection to serve client.
    // @param srt_fd, the client fd in srt boxed, the underlayer fd.
    virtual srs_error_t accept_srt_client(SrsSrtListenerType type, SRTSOCKET srt_fd);
private:
    virtual srs_error_t fd_to_resource(SrsSrtListenerType type, SRTSOCKET srt_fd, ISrsStartableConneciton** pr);
// Interface ISrsResourceManager
public:
    // A callback for connection to remove itself.
    // When connection thread cycle terminated, callback this to delete connection.
    virtual void remove(ISrsResource* c);
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

