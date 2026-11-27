//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI46_HPP
#define SRS_UTEST_AI46_HPP

/*
#include <srs_utest_ai46.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_srt_server.hpp>
#include <srs_kernel_io.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_manual_config.hpp>

#include <string>

// The server's SRT acceptor listening on loopback as `srt_server { listen }` makes it, with the listener and its accept
// coroutine from the real factory, handing each caller to a server that keeps the accepted sockets.
class SrtListenerUtestServer
{
public:
    MockSrsConfig conf_;
    MockSrtClientHandlerForAcceptor server_;
    SrsSrtAcceptor *acceptor_;
    int port_;

public:
    SrtListenerUtestServer()
    {
        acceptor_ = new SrsSrtAcceptor(&server_);
        port_ = 0;
    }
    virtual ~SrtListenerUtestServer()
    {
        // Stop accepting first, then close what the server was handed, as a server that quits does.
        srs_freep(acceptor_);
        for (int i = 0; i < (int)server_.fds_.size(); i++) {
            srs_srt_close(server_.fds_[i]);
        }
    }

public:
    // Listen with the srt_server directives, such as "bonding on;", besides enabled and listen.
    srs_error_t listen(std::string ip, std::string directives = "")
    {
        srs_error_t err = srs_success;

        int port = 0;
        if ((err = srs_utest_free_port(SOCK_DGRAM, &port)) != srs_success) {
            return srs_error_wrap(err, "port");
        }
        return listen_at(ip, port, directives);
    }

    // Listen on the port, such as a second listener of the port for the other IP family.
    srs_error_t listen_at(std::string ip, int port, std::string directives = "")
    {
        srs_error_t err = srs_success;

        port_ = port;
        std::string conf = _MIN_OK_CONF "srt_server { enabled on; listen 10080; " + directives + " }";
        if ((err = conf_.mock_parse(conf)) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        acceptor_->config_ = &conf_;

        return acceptor_->listen(ip, port_);
    }

    // Wait until the server was handed n callers, as long as a caller's handshake may take on loopback.
    void wait_for_callers(int n)
    {
        for (int i = 0; i < 300 && (int)server_.fds_.size() < n; i++) {
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        }
    }

    std::string streamid_of(int index)
    {
        std::string streamid;
        srs_error_t err = srs_srt_get_streamid(server_.fds_[index], streamid);
        srs_freep(err);
        return streamid;
    }
};

// An SRT caller such as ffmpeg or OBS, with its stream id, connecting to the server's SRT port.
class SrtListenerUtestCaller
{
public:
    SrsSrtSocket *skt_;

public:
    SrtListenerUtestCaller()
    {
        skt_ = NULL;
    }
    virtual ~SrtListenerUtestCaller()
    {
        srs_freep(skt_);
    }

public:
    srs_error_t connect(int port, std::string streamid)
    {
        srs_error_t err = srs_success;

        srs_srt_t fd = srs_srt_socket_invalid();
        if ((err = srs_srt_socket_with_default_option(&fd)) != srs_success) {
            return srs_error_wrap(err, "socket");
        }
        skt_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), fd);
        skt_->set_send_timeout(3 * SRS_UTIME_SECONDS);
        skt_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
        if ((err = srs_srt_set_streamid(fd, streamid)) != srs_success) {
            return srs_error_wrap(err, "streamid");
        }
        return skt_->connect("127.0.0.1", port);
    }
};

// A TS writer keeping the TS packets an encoder muxes.
class SrtSourceUtestTsWriter : public ISrsStreamWriter
{
public:
    std::string data_;

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        data_.append((const char *)buf, size);
        if (nwrite) {
            *nwrite = (ssize_t)size;
        }
        return srs_success;
    }
};

// An H.264 keyframe in annexb, its SPS, PPS and IDR slice, as an SRT encoder sends it; a P slice; and an ADTS frame
// of AAC-LC.
extern std::string srt_source_utest_h264_key();
extern std::string srt_source_utest_h264_p();
extern std::string srt_source_utest_adts_frame();

#endif
