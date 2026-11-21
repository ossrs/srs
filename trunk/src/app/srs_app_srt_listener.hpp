//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SRT_LISTENER_HPP
#define SRS_APP_SRT_LISTENER_HPP

#include <srs_app_st.hpp>
#include <srs_core.hpp>
#include <srs_protocol_srt.hpp>

#include <string>

class ISrsSrtEventLoop;
class ISrsAppFactory;

class ISrsSrtHandler
{
public:
    ISrsSrtHandler();
    virtual ~ISrsSrtHandler();

public:
    virtual srs_error_t on_srt_client(srs_srt_t srt_fd) = 0;
};

class ISrsSrtListener
{
public:
    ISrsSrtListener();
    virtual ~ISrsSrtListener();

public:
    virtual srs_srt_t fd() = 0;
    virtual srs_error_t create_socket() = 0;
    virtual srs_error_t listen() = 0;
};

class SrsSrtListener : public ISrsSrtListener, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_srt_t lfd_;
    SrsSrtSocket *srt_skt_;
    ISrsCoroutine *trd_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtHandler *handler_;
    std::string ip_;
    int port_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSrtEventLoop *srt_eventloop_;
    ISrsAppFactory *app_factory_;

public:
    SrsSrtListener(ISrsSrtHandler *h, std::string i, int p);
    virtual ~SrsSrtListener();

public:
    virtual srs_srt_t fd();

public:
    // A separate step from listen, because some SRT options must be set before listen.
    virtual srs_error_t create_socket();
    virtual srs_error_t listen();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
};

#endif
