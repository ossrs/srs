//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SRT_SERVER_HPP
#define SRS_APP_SRT_SERVER_HPP

#include <srs_core.hpp>

#include <srs_app_srt_listener.hpp>
#include <srs_protocol_srt.hpp>

class SrsSrtServer;
class SrsHourGlass;
class ISrsSrtClientHandler;

// Interface for SRT client acceptance
class ISrsSrtClientHandler
{
public:
    ISrsSrtClientHandler();
    virtual ~ISrsSrtClientHandler();

public:
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd) = 0;
};

// A common srt acceptor, for SRT server.
class SrsSrtAcceptor : public ISrsSrtHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string ip_;
    int port_;
    ISrsSrtClientHandler *srt_handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSrtListener *listener_;

public:
    SrsSrtAcceptor(ISrsSrtClientHandler *srt_handler);
    virtual ~SrsSrtAcceptor();

public:
    virtual srs_error_t listen(std::string ip, int port);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t set_srt_opt();
    // Interface ISrsSrtHandler
public:
    virtual srs_error_t on_srt_client(srs_srt_t srt_fd);
};

// Start a coroutine to drive the SRT events with state-threads.
class SrsSrtEventLoop : public ISrsCoroutineHandler
{
public:
    SrsSrtEventLoop();
    virtual ~SrsSrtEventLoop();

public:
    ISrsSrtPoller *poller() { return srt_poller_; }

public:
    srs_error_t initialize();
    srs_error_t start();
    // Interface ISrsCoroutineHandler.
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtPoller *srt_poller_;
    ISrsCoroutine *trd_;
};

// SrsSrtEventLoop is global singleton instance.
extern SrsSrtEventLoop *_srt_eventloop;

#endif
