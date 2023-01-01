//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_SRT_LISTENER_HPP
#define SRS_APP_SRT_LISTENER_HPP

#include <srs_core.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_srt.hpp>

#include <string>

// The srt connection handler.
class ISrsSrtHandler
{
public:
    ISrsSrtHandler();
    virtual ~ISrsSrtHandler();
public:
    // When got srt client.
    virtual srs_error_t on_srt_client(srs_srt_t srt_fd) = 0;
};

// Bind and listen SRT(udp) port, use handler to process the client.
class SrsSrtListener : public ISrsCoroutineHandler
{
private:
    srs_srt_t lfd_;
    SrsSrtSocket* srt_skt_;
    SrsCoroutine* trd_;
private:
    ISrsSrtHandler* handler_;
    std::string ip_;
    int port_;
public:
    SrsSrtListener(ISrsSrtHandler* h, std::string i, int p);
    virtual ~SrsSrtListener();
public:
    virtual srs_srt_t fd();
public:
    // Create srt socket, separate this step because of srt have some option must set before listen.
    virtual srs_error_t create_socket();
    virtual srs_error_t listen();
// Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();
};

#endif

