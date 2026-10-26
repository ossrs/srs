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
#include <srs_kernel_utility.hpp>
#include <srs_protocol_st.hpp>

// An H.264 768x320 stream and an AAC 44.1 kHz stereo stream, as FLV tags: the sequence headers, a key frame, an inter
// frame and an audio frame.
static const uint8_t flv_utest_avc_sh[] = {
    0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac,
    0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18,
    0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
static const uint8_t flv_utest_avc_idr[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
static const uint8_t flv_utest_avc_inter[] = {0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21, 0x6c, 0x42};
static const uint8_t flv_utest_aac_sh[] = {0xaf, 0x00, 0x12, 0x10};
static const uint8_t flv_utest_aac_frame[] = {0xaf, 0x01, 0x21, 0x11, 0x45, 0x00};

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

        at_once(create_stage());
    }
    // Swaps the stages only, for a stage whose id is known later, such as a listener's fd; the test then calls at_once.
    PithyPrintAtOnceScope()
    {
        saved_ = _srs_stages;
        _srs_stages = &stages_;
    }
    // Prints the stage of this pithy print at once, and frees the pithy print.
    void at_once(SrsPithyPrint *stage)
    {
        SrsUniquePtr<SrsPithyPrint> pprint(stage);
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

// An HTTP server on a random local port for one request, such as an HTTP-FLV origin for an edge or a heartbeat
// collector: it keeps the request, answers with reply_, the response header and as much of the body as the test
// wants, then closes.
class HttpUtestOneReplyServer : public SrsHttpTestServer
{
public:
    string reply_;
    string request_;

public:
    HttpUtestOneReplyServer(string reply) : SrsHttpTestServer("")
    {
        reply_ = reply;
    }

public:
    virtual srs_error_t start()
    {
        srs_error_t err = SrsHttpTestServer::start();
        // Let State Threads run once, so its clock is fresh, as RtmpConnTestOrigin does.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    virtual srs_error_t do_cycle(srs_netfd_t cfd)
    {
        srs_error_t err = srs_success;

        SrsStSocket skt(cfd);
        skt.set_recv_timeout(1 * SRS_UTIME_SECONDS);
        skt.set_send_timeout(1 * SRS_UTIME_SECONDS);

        char buf[1024];
        ssize_t nn = 0;
        if ((err = skt.read(buf, sizeof(buf), &nn)) != srs_success) {
            return err;
        }
        request_.assign(buf, nn);

        return skt.write((void *)reply_.data(), reply_.size(), NULL);
    }
    string server()
    {
        return "127.0.0.1:" + srs_strconv_format_int(get_port());
    }
};

#endif
