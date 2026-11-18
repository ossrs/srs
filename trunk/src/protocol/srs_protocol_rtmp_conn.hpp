//
// Copyright (c) 2013-2026 The SRS Authors
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

class ISrsBasicRtmpClient
{
public:
    ISrsBasicRtmpClient();
    virtual ~ISrsBasicRtmpClient();

public:
    virtual srs_error_t connect() = 0;
    virtual void close() = 0;

public:
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string *pstream = NULL) = 0;
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string *pstream = NULL) = 0;
    virtual void kbps_sample(const char *label, srs_utime_t age) = 0;
    virtual void kbps_sample(const char *label, srs_utime_t age, int msgs) = 0;
    virtual int sid() = 0;

public:
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg) = 0;
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket) = 0;
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs) = 0;
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg) = 0;

public:
    virtual void set_recv_timeout(srs_utime_t timeout) = 0;
};

// Never use the client after close().
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
    // The r is an RTMP url, such as rtmp://ip:port/app/stream?domain=vhost
    SrsBasicRtmpClient(std::string r, srs_utime_t ctm, srs_utime_t stm);
    virtual ~SrsBasicRtmpClient();

public:
    SrsAmf0Object *extra_args();

public:
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
