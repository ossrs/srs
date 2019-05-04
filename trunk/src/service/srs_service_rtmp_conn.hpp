/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_SERVICE_RTMP_CONN_HPP
#define SRS_SERVICE_RTMP_CONN_HPP

#include <srs_core.hpp>

#include <string>

class SrsRequest;
class SrsTcpClient;
class SrsRtmpClient;
class SrsCommonMessage;
class SrsSharedPtrMessage;
class SrsPacket;
class SrsKbps;
class SrsWallClock;

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
    SrsKbps* kbps;
    SrsWallClock* clk;
    int stream_id;
public:
    // Constructor.
    // @param u The RTMP url, for example, rtmp://ip:port/app/stream?domain=vhost
    // @param ctm The timeout in srs_utime_t to connect to server.
    // @param stm The timeout in srs_utime_t to delivery A/V stream.
    SrsBasicRtmpClient(std::string u, srs_utime_t ctm, srs_utime_t stm);
    virtual ~SrsBasicRtmpClient();
public:
    // Connect, handshake and connect app to RTMP server.
    // @remark We always close the transport.
    virtual srs_error_t connect();
    virtual void close();
protected:
    virtual srs_error_t connect_app();
    virtual srs_error_t do_connect_app(std::string local_ip, bool debug);
public:
    virtual srs_error_t publish(int chunk_size);
    virtual srs_error_t play(int chunk_size);
    virtual void kbps_sample(const char* label, int64_t age);
    virtual void kbps_sample(const char* label, int64_t age, int msgs);
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

