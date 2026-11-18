//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsAppConfig;
class ISrsAppFactory;

class ISrsSrtClientHandler
{
public:
    ISrsSrtClientHandler();
    virtual ~ISrsSrtClientHandler();

public:
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd) = 0;
};

class ISrsSrtAcceptor
{
public:
    ISrsSrtAcceptor();
    virtual ~ISrsSrtAcceptor();

public:
    virtual srs_error_t listen(std::string ip, int port) = 0;
};

class SrsSrtAcceptor : public ISrsSrtAcceptor, public ISrsSrtHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string ip_;
    int port_;
    ISrsSrtClientHandler *srt_handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;
    ISrsSrtOptions *srt_options_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtListener *listener_;

public:
    SrsSrtAcceptor(ISrsSrtClientHandler *srt_handler);
    virtual ~SrsSrtAcceptor();

public:
    virtual srs_error_t listen(std::string ip, int port);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t set_srt_opt();

public:
    virtual srs_error_t on_srt_client(srs_srt_t srt_fd);
};

class ISrsSrtEventLoop
{
public:
    ISrsSrtEventLoop();
    virtual ~ISrsSrtEventLoop();

public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t start() = 0;
    virtual ISrsSrtPoller *poller() = 0;
};

class SrsSrtEventLoop : public ISrsSrtEventLoop, public ISrsCoroutineHandler
{
public:
    SrsSrtEventLoop();
    virtual ~SrsSrtEventLoop();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t start();
    virtual ISrsSrtPoller *poller();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtPoller *srt_poller_;
    ISrsCoroutine *trd_;
};

extern SrsSrtEventLoop *_srs_srt_eventloop;

#endif
