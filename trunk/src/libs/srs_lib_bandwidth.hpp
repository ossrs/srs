/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#ifndef SRS_LIB_BANDWIDTH_HPP
#define SRS_LIB_BANDWIDTH_HPP

/*
#include <srs_lib_bandwidth.hpp>
*/

#include <srs_core.hpp>

class SrsRtmpClient;
class SrsBandwidthPacket;

/**
* bandwith client library for srs-librtmp.
*/
class SrsBandwidthClient
{
private:
    SrsRtmpClient* _rtmp;
public:
    SrsBandwidthClient();
    virtual ~SrsBandwidthClient();
public:
    /**
    * initialize the bandwidth check client.
    */
    virtual int initialize(SrsRtmpClient* rtmp);
    /**
    * do bandwidth check.
    * 
    * bandwidth info:
    * @param start_time, output the start time, in ms.
    * @param end_time, output the end time, in ms.
    * @param play_kbps, output the play/download kbps.
    * @param publish_kbps, output the publish/upload kbps.
    * @param play_bytes, output the play/download bytes.
    * @param publish_bytes, output the publish/upload bytes.
    * @param play_duration, output the play/download test duration, in ms.
    * @param publish_duration, output the publish/upload test duration, in ms.
    */
    virtual int bandwidth_check(
        int64_t* start_time, int64_t* end_time, 
        int* play_kbps, int* publish_kbps,
        int* play_bytes, int* publish_bytes,
        int* play_duration, int* publish_duration
    );
private:
    /**
    * play check/test, downloading bandwidth kbps.
    */
    virtual int play_start();
    virtual int play_checking();
    virtual int play_stop();
    /**
    * publish check/test, publishing bandwidth kbps.
    */
    virtual int publish_start(int& duration_ms, int& play_kbps);
    virtual int publish_checking(int duration_ms, int play_kbps);
    virtual int publish_stop();
    /**
    * report and final packet
    */
    virtual int final(SrsBandwidthPacket** ppkt);
};

#endif

