//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_RTMP_CONN_HPP
#define SRS_PROTOCOL_RTMP_CONN_HPP

#include <srs_core.hpp>

#include <string>

class SrsRequest;
class SrsTcpClient;
class SrsRtmpClient;
class SrsCommonMessage;
class SrsSharedPtrMessage;
class SrsPacket;
class SrsNetworkKbps;
class SrsWallClock;
class SrsAmf0Object;

// The simple RTMP client, provides friendly APIs.
// @remark Should never use client when closed.
// Usage:
//      SrsBasicRtmpClient client("rtmp://127.0.0.1:1935/live/livestream", 3000, 9000);
//      client.connect();
//      client.play();
//      client.close();
class SrsBasicRtmpClient
{
private:
    std::string url;
    srs_utime_t connect_timeout;
    srs_utime_t stream_timeout;
protected:
    SrsRequest* req;
private:
    SrsTcpClient* transport;
    SrsRtmpClient* client;
    SrsNetworkKbps* kbps;
    int stream_id;
public:
    // Constructor.
    // @param r The RTMP url, for example, rtmp://ip:port/app/stream?domain=vhost
    // @param ctm The timeout in srs_utime_t to connect to server.
    // @param stm The timeout in srs_utime_t to delivery A/V stream.
    SrsBasicRtmpClient(std::string r, srs_utime_t ctm, srs_utime_t stm);
    virtual ~SrsBasicRtmpClient();
public:
    // Get extra args to carry more information.
    SrsAmf0Object* extra_args();
public:
    // Connect, handshake and connect app to RTMP server.
    // @remark We always close the transport.
    virtual srs_error_t connect();
    virtual void close();
protected:
    virtual srs_error_t connect_app();
    virtual srs_error_t do_connect_app(std::string local_ip, bool debug);
public:
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string* pstream = NULL);
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string* pstream = NULL);
    virtual void kbps_sample(const char* label, srs_utime_t age);
    virtual void kbps_sample(const char* label, srs_utime_t age, int msgs);
    virtual int sid();
public:
    virtual srs_error_t recv_message(SrsCommonMessage** pmsg);
    virtual srs_error_t decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    virtual srs_error_t send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs);
    virtual srs_error_t send_and_free_message(SrsSharedPtrMessage* msg);
public:
    virtual void set_recv_timeout(srs_utime_t timeout);
};

#endif

