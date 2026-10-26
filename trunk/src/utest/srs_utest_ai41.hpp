//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI41_HPP
#define SRS_UTEST_AI41_HPP

/*
#include <srs_utest_ai41.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>

#include <sys/socket.h>

#include <string>

// How far the test origin of an edge pull or a forwarder push goes before it stops.
enum RtmpConnOriginStep {
    // Not an RTMP server: an HTTP server answers the handshake with an HTTP error.
    RtmpConnOriginNotRtmp,
    // Refuses the app with an _error onStatus and closes, as an RTMP server does for an app
    // it does not serve.
    RtmpConnOriginRejectsApp,
    // Answers connect, then exits, as an origin that restarts at that moment.
    RtmpConnOriginExitsAfterConnect,
    // Answers createStream, then the connection is reset.
    RtmpConnOriginResetAfterCreateStream,
    // Serves the play or publish of the client.
    RtmpConnOriginServes,
    // Serves the play with the metadata and a key frame, then closes its sending side, as an
    // origin whose publisher stopped.
    RtmpConnOriginServesThenCloses,
};

// An RTMP origin on a random local port for one connection of an edge or a forwarder,
// which stops at the step the test picks.
class RtmpConnTestOrigin : public SrsHttpTestServer
{
public:
    RtmpConnOriginStep step_;
    std::string tc_url_;
    SrsRtmpConnType type_;
    std::string stream_;
    std::string video_;
    std::string error_;

public:
    RtmpConnTestOrigin(RtmpConnOriginStep step) : SrsHttpTestServer("")
    {
        step_ = step;
        type_ = SrsRtmpConnUnknown;
    }

public:
    virtual srs_error_t start()
    {
        srs_error_t err = SrsHttpTestServer::start();
        // Let State Threads run once, so its clock is fresh before the origin sets its
        // timeouts, as RawReplyTcpServer does.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    virtual srs_error_t do_cycle(srs_netfd_t cfd)
    {
        srs_error_t err = do_serve(cfd);
        if (err != srs_success) {
            error_ = srs_error_desc(err);
        }
        return err;
    }

private:
    srs_error_t do_serve(srs_netfd_t cfd)
    {
        srs_error_t err = srs_success;

        SrsStSocket skt(cfd);
        skt.set_recv_timeout(3 * SRS_UTIME_SECONDS);
        skt.set_send_timeout(3 * SRS_UTIME_SECONDS);

        if (step_ == RtmpConnOriginNotRtmp) {
            char buf[1537];
            if ((err = skt.read(buf, sizeof(buf), NULL)) != srs_success) {
                return srs_error_wrap(err, "read c0c1");
            }
            std::string res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            return skt.write((void *)res.data(), res.size(), NULL);
        }

        SrsRtmpServer rtmp(&skt);
        if ((err = rtmp.handshake()) != srs_success) {
            return srs_error_wrap(err, "handshake");
        }

        SrsRequest req;
        if ((err = rtmp.connect_app(&req)) != srs_success) {
            return srs_error_wrap(err, "connect app");
        }
        tc_url_ = req.tcUrl_;

        if (step_ == RtmpConnOriginRejectsApp) {
            rtmp.response_connect_reject(&req, "app not allowed");
            return err;
        }

        if ((err = rtmp.response_connect_app(&req, "127.0.0.1")) != srs_success) {
            return srs_error_wrap(err, "response connect app");
        }
        if (step_ == RtmpConnOriginExitsAfterConnect) {
            return err;
        }

        if (step_ == RtmpConnOriginResetAfterCreateStream) {
            SrsRtmpCommonMessage *msg_raw = NULL;
            SrsCreateStreamPacket *pkt_raw = NULL;
            if ((err = rtmp.expect_message<SrsCreateStreamPacket>(&msg_raw, &pkt_raw)) != srs_success) {
                return srs_error_wrap(err, "expect create stream");
            }
            SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
            SrsUniquePtr<SrsCreateStreamPacket> pkt(pkt_raw);
            if ((err = rtmp.send_and_free_packet(new SrsCreateStreamResPacket(pkt->transaction_id_, 1), 0)) != srs_success) {
                return srs_error_wrap(err, "response create stream");
            }
            // Close with a reset, as a load balancer in front of the origin that drops the
            // connection does.
            struct linger lg;
            lg.l_onoff = 1;
            lg.l_linger = 0;
            setsockopt(srs_netfd_fileno(cfd), SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
            return err;
        }

        srs_utime_t duration = 0;
        if ((err = rtmp.identify_client(1, type_, stream_, duration)) != srs_success) {
            return srs_error_wrap(err, "identify client");
        }

        if (type_ == SrsRtmpConnPlay) {
            if ((err = rtmp.start_play(1)) != srs_success) {
                return srs_error_wrap(err, "start play");
            }

            SrsOnMetaDataPacket *meta = new SrsOnMetaDataPacket();
            meta->metadata_->set("width", SrsAmf0Any::number(1280));
            meta->metadata_->set("height", SrsAmf0Any::number(720));
            if ((err = rtmp.send_and_free_packet(meta, 1)) != srs_success) {
                return srs_error_wrap(err, "send metadata");
            }

            if ((err = rtmp.send_and_free_message(rtmp_conn_video_packet(), 1)) != srs_success) {
                return srs_error_wrap(err, "send video");
            }

            // Close the sending side only, and keep reading what the edge sends: a close with unread data in the
            // receive buffer resets the connection on Linux, and the reset drops what the edge has not read yet.
            if (step_ == RtmpConnOriginServesThenCloses) {
                shutdown(srs_netfd_fileno(cfd), SHUT_WR);
            }

            // Serve until the edge leaves.
            while (true) {
                SrsRtmpCommonMessage *msg = NULL;
                if ((err = rtmp.recv_message(&msg)) != srs_success) {
                    srs_freep(err);
                    return err;
                }
                srs_freep(msg);
            }
        }

        if ((err = rtmp.start_flash_publish(1)) != srs_success) {
            return srs_error_wrap(err, "start publish");
        }
        while (video_.empty()) {
            SrsRtmpCommonMessage *msg_raw = NULL;
            if ((err = rtmp.recv_message(&msg_raw)) != srs_success) {
                return srs_error_wrap(err, "recv media");
            }
            SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
            if (msg->header_.is_video()) {
                video_.assign(msg->payload(), msg->size());
            }
        }

        return err;
    }

public:
    // An AVC key frame of 4 KB, larger than the 128-byte default chunk.
    static SrsMediaPacket *rtmp_conn_video_packet()
    {
        SrsMediaPacket *msg = new SrsMediaPacket();
        msg->timestamp_ = 40;
        msg->message_type_ = SrsFrameTypeVideo;
        char *data = new char[4096];
        for (int i = 0; i < 4096; i++) {
            data[i] = (char)(i % 251);
        }
        data[0] = 0x17;
        data[1] = 0x01;
        msg->wrap(data, 4096);
        return msg;
    }

    std::string rtmp_url(std::string stream)
    {
        return "rtmp://127.0.0.1:" + srs_strconv_format_int(get_port()) + "/live/" + stream;
    }
};

#endif
