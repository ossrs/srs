/*
The MIT License (MIT)

Copyright (c) 2013-2014 wenjiegit

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_CORE_BANDWIDTH_HPP
#define SRS_CORE_BANDWIDTH_HPP

/*
#include <srs_core_bandwidth.hpp>
*/
#include <srs_core.hpp>

class SrsRequest;
class SrsRtmp;

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
*        |  data(publishing)---->   | onSrsBandCheckStartingPublishBytes
*        |  <--call(stop publish)   | onSrsBandCheckStopPublishBytes
*        |  result(stopped)(1)-->   | onSrsBandCheckStoppedPublishBytes
*        |                          |
*        |  <--------------report   |
*        |  final(2)------------>   | finalClientPacket
*        |          <END>           |
*
* 1. when flash client, server ignore the publish stopped result,
*   and directly send the report to flash client.
* 2. flash client only. when got report, flash client should send out
*   a final packet and close the connection immediately.
*/
class SrsBandwidth
{
private:
    SrsRequest* req;
    SrsRtmp* rtmp;
public:
    SrsBandwidth();
    virtual ~SrsBandwidth();
public:
    /**
    * do the bandwidth test.
    */
    virtual int bandwidth_test(SrsRequest* _req, st_netfd_t stfd, SrsRtmp* _rtmp);
private:
    virtual int get_local_ip(st_netfd_t stfd, char *&local_ip);
    /**
    * used to process band width check from client.
    */
    virtual int do_bandwidth_check();
    virtual int check_play(int duration_ms, int interval_ms, int& actual_duration_ms, int& play_bytes, int max_play_kbps);
    virtual int check_publish(int duration_ms, int interval_ms, int& actual_duration_ms, int& publish_bytes, int max_pub_kbps);
};

#endif