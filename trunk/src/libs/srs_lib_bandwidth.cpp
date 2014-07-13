/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_lib_bandwidth.hpp>

#include <srs_kernel_error.hpp>
#include <srs_protocol_stack.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>

/**
* recv bandwidth helper.
*/
typedef bool (*_CheckPacketType)(SrsBandwidthPacket* pkt);
bool _bandwidth_is_start_play(SrsBandwidthPacket* pkt)
{
    return pkt->is_start_play();
}
bool _bandwidth_is_stop_play(SrsBandwidthPacket* pkt)
{
    return pkt->is_stop_play();
}
bool _bandwidth_is_start_publish(SrsBandwidthPacket* pkt)
{
    return pkt->is_start_publish();
}
bool _bandwidth_is_finish(SrsBandwidthPacket* pkt)
{
    return pkt->is_finish();
}
int _srs_expect_bandwidth_packet(SrsRtmpClient* rtmp, _CheckPacketType pfn)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get final message success.");
        
        if (pfn(pkt)) {
            return ret;
        }
    }
    
    return ret;
}

SrsBandwidthClient::SrsBandwidthClient()
{
    _rtmp = NULL;
}

SrsBandwidthClient::~SrsBandwidthClient()
{
}

int SrsBandwidthClient::initialize(SrsRtmpClient* rtmp)
{
    _rtmp = rtmp;

    return ERROR_SUCCESS;
}

int SrsBandwidthClient::bandwidth_check(
    char srs_server[128], char srs_primary_authors[128], 
    char srs_id[64], char srs_pid[64], char srs_server_ip[128],
    int64_t* start_time, int64_t* end_time, 
    int* play_kbps, int* publish_kbps,
    int* play_bytes, int* publish_bytes,
    int* play_duration, int* publish_duration
) {
    int ret = ERROR_SUCCESS;

    *start_time = srs_get_system_time_ms();
    
    // play
    if ((ret = play_start()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = play_checking()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = play_stop()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // publish
    if ((ret = publish_start()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = publish_checking()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = publish_stop()) != ERROR_SUCCESS) {
        return ret;
    }

    *end_time = srs_get_system_time_ms();
    
    return ret;
}

int SrsBandwidthClient::play_start()
{
    int ret = ERROR_SUCCESS;

    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_start_play)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv play begin request.");
    
    if (true) {
        // send start play response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_starting_play();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check play begin.");
    
    return ret;
}

int SrsBandwidthClient::play_checking()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsBandwidthClient::play_stop()
{
    int ret = ERROR_SUCCESS;

    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_stop_play)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv play stop request.");
    
    if (true) {
        // send stop play response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_play();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check play stop.");
    
    return ret;
}

int SrsBandwidthClient::publish_start()
{
    int ret = ERROR_SUCCESS;

    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_start_publish)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv publish begin request.");
    
    if (true) {
        // send start publish response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_starting_publish();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check publish begin.");
    
    return ret;
}

int SrsBandwidthClient::publish_checking()
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsBandwidthClient::publish_stop()
{
    int ret = ERROR_SUCCESS;

    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_start_publish)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv publish stop request.");
    
    if (true) {
        // send start publish response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_starting_publish();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check publish stop.");
    
    return ret;
}

int SrsBandwidthClient::finial()
{
    int ret = ERROR_SUCCESS;

    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_finish)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv finish/report request.");
    
    if (true) {
        // send final response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_final();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check final message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check final.");
    
    return ret;
}
