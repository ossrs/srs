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
    // Serve a caller the listener accepted from the address ip and port.
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd, std::string ip, int port) = 0;
    // A link joined a socket group a listener accepted before.
    virtual void on_srt_links_joined() = 0;
};

// The socket groups the listeners accepted, each the connection of a bonded publisher, so a link that joins one
// later, which no listener hands, is still bounded: libsrt takes any number of links into a group.
class SrsSrtGroups
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<srs_srt_t> groups_;

public:
    SrsSrtGroups();
    virtual ~SrsSrtGroups();

public:
    void add(srs_srt_t group);
    // Close the links of each group past the most a group may have, the ones that joined last.
    void limit_links();
    int size();
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
    virtual srs_error_t on_srt_client(srs_srt_t srt_fd, std::string ip, int port);
    virtual void on_srt_links_joined();
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
