/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_stack.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>

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
bool _bandwidth_is_stop_publish(SrsBandwidthPacket* pkt)
{
    return pkt->is_stop_publish();
}
bool _bandwidth_is_finish(SrsBandwidthPacket* pkt)
{
    return pkt->is_finish();
}
int _srs_expect_bandwidth_packet(SrsRtmpClient* rtmp, _CheckPacketType pfn)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            return ret;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get final message success.");
        
        if (pfn(pkt)) {
            return ret;
        }
    }
    
    return ret;
}
int _srs_expect_bandwidth_packet2(SrsRtmpClient* rtmp, _CheckPacketType pfn, SrsBandwidthPacket** ppkt)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            return ret;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        srs_info("get final message success.");
        
        if (pfn(pkt)) {
            *ppkt = pkt;
            return ret;
        }
        
        srs_freep(pkt);
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
    int64_t* start_time, int64_t* end_time, 
    int* play_kbps, int* publish_kbps,
    int* play_bytes, int* publish_bytes,
    int* play_duration, int* publish_duration
) {
    int ret = ERROR_SUCCESS;

    srs_update_system_time_ms();
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
    int duration_ms = 0;
    int actual_play_kbps = 0;
    if ((ret = publish_start(duration_ms, actual_play_kbps)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = publish_checking(duration_ms, actual_play_kbps)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = publish_stop()) != ERROR_SUCCESS) {
        return ret;
    }
    
    SrsBandwidthPacket* pkt = NULL;
    if ((ret = final(&pkt)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(SrsBandwidthPacket, pkt);
    
    // get data
    if (true ) {
        SrsAmf0Any* prop = NULL;
        if ((prop = pkt->data->ensure_property_number("play_kbps")) != NULL) {
            *play_kbps = (int)prop->to_number();
        }
        if ((prop = pkt->data->ensure_property_number("publish_kbps")) != NULL) {
            *publish_kbps = (int)prop->to_number();
        }
        if ((prop = pkt->data->ensure_property_number("play_bytes")) != NULL) {
            *play_bytes = (int)prop->to_number();
        }
        if ((prop = pkt->data->ensure_property_number("publish_bytes")) != NULL) {
            *publish_bytes = (int)prop->to_number();
        }
        if ((prop = pkt->data->ensure_property_number("play_time")) != NULL) {
            *play_duration = (int)prop->to_number();
        }
        if ((prop = pkt->data->ensure_property_number("publish_time")) != NULL) {
            *publish_duration = (int)prop->to_number();
        }
    }

    srs_update_system_time_ms();
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
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stopped_play();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check play stop.");
    
    return ret;
}

int SrsBandwidthClient::publish_start(int& duration_ms, int& play_kbps)
{
    int ret = ERROR_SUCCESS;

    if (true) {
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _srs_expect_bandwidth_packet2(_rtmp, _bandwidth_is_start_publish, &pkt)) != ERROR_SUCCESS) {
            return ret;
        }
        SrsAutoFree(SrsBandwidthPacket, pkt);
        
        SrsAmf0Any* prop = NULL;
        if ((prop = pkt->data->ensure_property_number("duration_ms")) != NULL) {
            duration_ms = (int)prop->to_number();
        }
        if ((prop = pkt->data->ensure_property_number("limit_kbps")) != NULL) {
            play_kbps = (int)prop->to_number();
        }
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

int SrsBandwidthClient::publish_checking(int duration_ms, int play_kbps)
{
    int ret = ERROR_SUCCESS;
    
    if (duration_ms <= 0) {
        ret = ERROR_RTMP_BWTC_DATA;
        srs_error("server must specifies the duration, ret=%d", ret);
        return ret;
    }
    
    if (play_kbps <= 0) {
        ret = ERROR_RTMP_BWTC_DATA;
        srs_error("server must specifies the play kbp, ret=%d", ret);
        return ret;
    }

    int data_count = 1;
    srs_update_system_time_ms();
    int64_t starttime = srs_get_system_time_ms();
    while ((srs_get_system_time_ms() - starttime) < duration_ms) {
        // TODO: FIXME: use shared ptr message.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_publishing();

        // TODO: FIXME: magic number
        for (int i = 0; i < data_count; ++i) {
            std::stringstream seq;
            seq << i;
            std::string play_data = "SRS band check data from server's publishing......";
            pkt->data->set(seq.str(), SrsAmf0Any::str(play_data.c_str()));
        }
        data_count += 2;

        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check publish messages failed. ret=%d", ret);
            return ret;
        }
        
        // use the play kbps to control the publish
        srs_update_system_time_ms();
        int elaps = (int)(srs_get_system_time_ms() - starttime);
        if (elaps > 0) {
            int current_kbps = (int)(_rtmp->get_send_bytes() * 8 / elaps);
            while (current_kbps > play_kbps) {
                srs_update_system_time_ms();
                elaps = (int)(srs_get_system_time_ms() - starttime);
                current_kbps = (int)(_rtmp->get_send_bytes() * 8 / elaps);
                usleep(100 * 1000); // TODO: FIXME: magic number.
            }
        }
    }
    srs_info("BW check send publish bytes over.");
    
    return ret;
}

int SrsBandwidthClient::publish_stop()
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        // send start publish response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_publish();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW client stop publish request.");

    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_stop_publish)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv publish stop request.");
    
    if (true) {
        // send start publish response to server.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stopped_publish();
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check publish stop.");
    
    return ret;
}

int SrsBandwidthClient::final(SrsBandwidthPacket** ppkt)
{
    int ret = ERROR_SUCCESS;

    if ((ret = _srs_expect_bandwidth_packet2(_rtmp, _bandwidth_is_finish, ppkt)) != ERROR_SUCCESS) {
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

