//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI42_HPP
#define SRS_UTEST_AI42_HPP

/*
#include <srs_utest_ai42.hpp>
*/
#include <srs_utest.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_protocol_st.hpp>

// The pithy print stages, with one stage set to print at each call, so its line, such as the kbps line of an edge pull
// or push, is logged at once, not after the configured 10s. Objects of the stage must be made inside the scope.
class PithyPrintAtOnceScope
{
public:
    SrsStageManager stages_;
    SrsStageManager *saved_;

public:
    PithyPrintAtOnceScope(SrsPithyPrint *(*create_stage)())
    {
        saved_ = _srs_stages;
        _srs_stages = &stages_;

        SrsUniquePtr<SrsPithyPrint> pprint(create_stage());
        pprint->elapse();
        pprint->cache_->interval_ = 0;
    }
    ~PithyPrintAtOnceScope()
    {
        _srs_stages = saved_;
    }
};

// A player connected to a server port over loopback: the listen socket, the player's socket, and the
// server's side of the connection as the listener accepts it.
class StUtestTcpPair
{
public:
    srs_netfd_t lfd_;
    srs_netfd_t client_;
    srs_netfd_t server_;

public:
    StUtestTcpPair()
    {
        lfd_ = client_ = server_ = NULL;
    }
    ~StUtestTcpPair()
    {
        srs_close_stfd(client_);
        srs_close_stfd(server_);
        srs_close_stfd(lfd_);
    }

public:
    srs_error_t open()
    {
        srs_error_t err = srs_success;

        int port = 0;
        if ((err = srs_utest_tcp_listen("127.0.0.1", &port, &lfd_)) != srs_success) {
            return srs_error_wrap(err, "listen");
        }

        if ((err = srs_tcp_connect("127.0.0.1", port, 3 * SRS_UTIME_SECONDS, &client_)) != srs_success) {
            return srs_error_wrap(err, "connect");
        }

        if ((server_ = srs_accept(lfd_, NULL, NULL, 3 * SRS_UTIME_SECONDS)) == NULL) {
            return srs_error_new(ERROR_SOCKET_ACCEPT, "accept");
        }

        return err;
    }
    // The server's connection takes the accepted socket, and closes it when it goes.
    srs_netfd_t take_server()
    {
        srs_netfd_t fd = server_;
        server_ = NULL;
        return fd;
    }
};

#endif
