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

#ifndef SRS_APP_BANDWIDTH_HPP
#define SRS_APP_BANDWIDTH_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>

class SrsKbps;
class SrsRequest;
class SrsRtmpServer;
class SrsKbpsLimit;
class ISrsProtocolStatistic;

/**
 * bandwidth check/test sample.
 */
class SrsBandwidthSample
{
public:
    /**
     * the plan, how long to do the test, in ms,
     * if exceed the duration, abort the test.
     */
    int duration_ms;
    /**
     * the plan, interval for each check/test packet, in ms
     */
    int interval_ms;
public:
    /**
     * the actual test duration, in ms.
     */
    int actual_duration_ms;
    /**
     * the actual test bytes
     */
    int bytes;
    /**
     * the actual test kbps
     */
    int kbps;
public:
    SrsBandwidthSample();
    virtual ~SrsBandwidthSample();
public:
    /**
     * update the bytes and actual duration, then calc the kbps.
     * @param _bytes update the sample bytes.
     * @param _duration update the actual duration, in ms.
     */
    virtual void calc_kbps(int _bytes, int _duration);
};

/**
 * bandwidth test agent which provides the interfaces for bandwidth check.
 * 1. if vhost disabled bandwidth check, ignore.
 * 2. otherwise, check the key, error if verify failed.
 * 3. check the interval limit, error if bandwidth in the interval window.
 * 4. check the bandwidth under the max kbps.
 * 5. send the bandwidth data to client.
 * bandwidth workflow:
 *  +------------+             +----------+
 *  |  Client    |             |  Server  |
 *  +-----+------+             +-----+----+
 *        |                          |
 *        |  connect vhost------>    | if vhost enable bandwidth,
 *        |  <-----result(success)   | do bandwidth check.
 *        |                          |
 *        |  <----call(start play)   | onSrsBandCheckStartPlayBytes
 *        |  result(playing)----->   | onSrsBandCheckStartingPlayBytes
 *        |  <-------data(playing)   | onSrsBandCheckStartingPlayBytes
 *        |  <-----call(stop play)   | onSrsBandCheckStopPlayBytes
 *        |  result(stopped)----->   | onSrsBandCheckStoppedPlayBytes
 *        |                          |
 *        |  <-call(start publish)   | onSrsBandCheckStartPublishBytes
 *        |  result(publishing)-->   | onSrsBandCheckStartingPublishBytes
 *        |  data(publishing)(3)->   | onSrsBandCheckStartingPublishBytes
 *        |  <--call(stop publish)   | onSrsBandCheckStopPublishBytes
 *        |  result(stopped)(1)-->   | onSrsBandCheckStoppedPublishBytes
 *        |                          |
 *        |  <--------------report   |
 *        |  final(2)------------>   | finalClientPacket
 *        |          <END>           |
 *
 * 1. when flash client, server never wait the stop publish response,
 *   for the flash client queue is fullfill with other packets.
 * 2. when flash client, server never wait the final packet,
 *   for the flash client directly close when got report packet.
 * 3. for linux client, it will send the publish data then send a stop publish,
 *   for the linux client donot know when to stop the publish.
 *   when server got publishing and stop publish, stop publish.
 */
class SrsBandwidth
{
private:
    SrsRequest* _req;
    SrsRtmpServer* _rtmp;
public:
    SrsBandwidth();
    virtual ~SrsBandwidth();
public:
    /**
     * do the bandwidth check.
     * @param rtmp, server RTMP protocol object, send/recv RTMP packet to/from client.
     * @param io_stat, the underlayer io statistic, provides send/recv bytes count.
     * @param req, client request object, specifies the request info from client.
     * @param local_ip, the ip of server which client connected at
     */
    virtual srs_error_t bandwidth_check(SrsRtmpServer* rtmp, ISrsProtocolStatistic* io_stat, SrsRequest* req, std::string local_ip);
private:
    /**
     * used to process band width check from client.
     * @param limit, the bandwidth limit object, to slowdown if exceed the kbps.
     */
    virtual srs_error_t do_bandwidth_check(SrsKbpsLimit* limit);
    // play check/test, downloading bandwidth kbps.
private:
    /**
     * start play/download bandwidth check/test,
     * send start-play command to client, client must response starting-play
     * to start the test.
     */
    virtual srs_error_t play_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    /**
     * do play/download bandwidth check/test,
     * server send call messages to client in specified time,
     * calc the time and bytes sent, then we got the kbps.
     */
    virtual srs_error_t play_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    /**
     * stop play/download bandwidth check/test,
     * send stop-play command to client, client must response stopped-play
     * to stop the test.
     */
    virtual srs_error_t play_stop(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    // publish check/test, publishing bandwidth kbps.
private:
    /**
     * start publish/upload bandwidth check/test,
     * send start-publish command to client, client must response starting-publish
     * to start the test.
     */
    virtual srs_error_t publish_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    /**
     * do publish/upload bandwidth check/test,
     * client send call messages to client in specified time,
     * server calc the time and bytes received, then we got the kbps.
     * @remark, for linux client, it will send a stop publish client, server will stop publishing.
     *       then enter the publish-stop stage with client.
     * @remark, for flash client, it will send many many call messages, that is,
     *       the send queue is fullfill with call messages, so we should never expect the
     *       response message in the publish-stop stage.
     */
    virtual srs_error_t publish_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
    /**
     * stop publish/upload bandwidth check/test,
     * send stop-publish command to client,
     * for linux client, always expect a stopped-publish response from client,
     * for flash client, the sent queue is fullfill with publishing call messages,
     *       so server never expect the stopped-publish from it.
     */
    virtual srs_error_t publish_stop(SrsBandwidthSample* sample, SrsKbpsLimit* limit);
private:
    /**
     * report and final packet
     * report a finish packet, with the bytes/time/kbps bandwidth check/test result,
     * for linux client, server always expect a final packet from client,
     * for flash client, the sent queue is fullfill with publishing call messages,
     *       so server never expect the final packet from it.
     */
    virtual srs_error_t do_final(SrsBandwidthSample& play_sample, SrsBandwidthSample& publish_sample, srs_utime_t start_time, srs_utime_t& end_time);
};

/**
 * the kbps limit, if exceed the kbps, slow down.
 */
class SrsKbpsLimit
{
private:
    int _limit_kbps;
    SrsKbps* _kbps;
public:
    SrsKbpsLimit(SrsKbps* kbps, int limit_kbps);
    virtual ~SrsKbpsLimit();
public:
    /**
     * get the system limit kbps.
     */
    virtual int limit_kbps();
    /**
     * limit the recv bandwidth.
     */
    virtual void recv_limit();
    /**
     * limit the send bandwidth.
     */
    virtual void send_limit();
};

#endif
