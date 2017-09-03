/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

/**
 * The simple RTMP client, provides friendly APIs.
 * @remark Should never use client when closed.
 * Usage:
 *      SrsBasicRtmpClient client("rtmp://127.0.0.1:1935/live/livestream", 3000, 9000);
 *      client.connect();
 *      client.play();
 *      client.close();
 */
class SrsBasicRtmpClient
{
private:
    std::string url;
    int64_t connect_timeout;
    int64_t stream_timeout;
protected:
    SrsRequest* req;
private:
    SrsTcpClient* transport;
    SrsRtmpClient* client;
    SrsKbps* kbps;
    int stream_id;
public:
    // Constructor.
    // @param u The RTMP url, for example, rtmp://ip:port/app/stream?domain=vhost
    // @param ctm The timeout in ms to connect to server.
    // @param stm The timeout in ms to delivery A/V stream.
    SrsBasicRtmpClient(std::string u, int64_t ctm, int64_t stm);
    virtual ~SrsBasicRtmpClient();
public:
    // Connect, handshake and connect app to RTMP server.
    // @remark We always close the transport.
    virtual int connect();
    virtual void close();
protected:
    virtual int connect_app();
    virtual int do_connect_app(std::string local_ip, bool debug);
public:
    virtual int publish();
    virtual int play();
    virtual void kbps_sample(const char* label, int64_t age);
    virtual void kbps_sample(const char* label, int64_t age, int msgs);
    virtual int sid();
public:
    virtual int recv_message(SrsCommonMessage** pmsg);
    virtual int decode_message(SrsCommonMessage* msg, SrsPacket** ppacket);
    virtual int send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs);
    virtual int send_and_free_message(SrsSharedPtrMessage* msg);
public:
    virtual void set_recv_timeout(int64_t timeout);
};

#endif

