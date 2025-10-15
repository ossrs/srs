//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_RTMP_CONN_HPP
#define SRS_PROTOCOL_RTMP_CONN_HPP

#include <srs_core.hpp>

#include <string>

class ISrsRequest;
class SrsTcpClient;
class SrsRtmpClient;
class SrsRtmpCommonMessage;
class SrsMediaPacket;
class SrsRtmpCommand;
class SrsNetworkKbps;
class SrsWallClock;
class SrsAmf0Object;

// The basic RTMP client interface.
class ISrsBasicRtmpClient
{
public:
    ISrsBasicRtmpClient();
    virtual ~ISrsBasicRtmpClient();

public:
    // Connect, handshake and connect app to RTMP server.
    virtual srs_error_t connect() = 0;
    // Close the connection.
    virtual void close() = 0;

public:
    // Publish stream to RTMP server.
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string *pstream = NULL) = 0;
    // Play stream from RTMP server.
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string *pstream = NULL) = 0;
    // Sample kbps for statistics.
    virtual void kbps_sample(const char *label, srs_utime_t age) = 0;
    // Get stream ID.
    virtual int sid() = 0;

public:
    // Receive RTMP message from server.
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg) = 0;
    // Decode RTMP message to packet.
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket) = 0;
    // Send media messages to server.
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs) = 0;
    // Send media message to server.
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg) = 0;

public:
    // Set receive timeout.
    virtual void set_recv_timeout(srs_utime_t timeout) = 0;
};

// The simple RTMP client, provides friendly APIs.
// @remark Should never use client when closed.
// Usage:
//      SrsBasicRtmpClient client("rtmp://127.0.0.1:1935/live/livestream", 3000, 9000);
//      client.connect();
//      client.play();
//      client.close();
class SrsBasicRtmpClient : public ISrsBasicRtmpClient
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string url_;
    srs_utime_t connect_timeout_;
    srs_utime_t stream_timeout_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsRequest *req_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTcpClient *transport_;
    SrsRtmpClient *client_;
    SrsNetworkKbps *kbps_;
    int stream_id_;

public:
    // Constructor.
    // @param r The RTMP url, for example, rtmp://ip:port/app/stream?domain=vhost
    // @param ctm The timeout in srs_utime_t to connect to server.
    // @param stm The timeout in srs_utime_t to delivery A/V stream.
    SrsBasicRtmpClient(std::string r, srs_utime_t ctm, srs_utime_t stm);
    virtual ~SrsBasicRtmpClient();

public:
    // Get extra args to carry more information.
    SrsAmf0Object *extra_args();

public:
    // Connect, handshake and connect app to RTMP server.
    // @remark We always close the transport.
    virtual srs_error_t connect();
    virtual void close();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t connect_app();
    virtual srs_error_t do_connect_app(std::string local_ip, bool debug);

public:
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual void kbps_sample(const char *label, srs_utime_t age);
    virtual void kbps_sample(const char *label, srs_utime_t age, int msgs);
    virtual int sid();

public:
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs);
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg);

public:
    virtual void set_recv_timeout(srs_utime_t timeout);
};

#endif
