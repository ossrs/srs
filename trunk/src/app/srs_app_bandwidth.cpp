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

#include <srs_app_bandwidth.hpp>

#include <arpa/inet.h>
#include <sstream>

using namespace std;

#include <srs_protocol_rtmp.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>

SrsBandwidth::SrsBandwidth()
{
}

SrsBandwidth::~SrsBandwidth()
{
}

int SrsBandwidth::bandwidth_test(SrsRequest* _req, st_netfd_t stfd, SrsRtmpServer* _rtmp)
{
    int ret = ERROR_SUCCESS;
    
    rtmp = _rtmp;
    req = _req;
    
    if (!_srs_config->get_bw_check_enabled(req->vhost)) {
        return ret;
    }

    // validate the bandwidth check key
    std::string key = "key=" + _srs_config->get_bw_check_key(req->vhost);
    if (req->tcUrl.find(key) == std::string::npos) {
        ret = ERROR_SYSTEM_BANDWIDTH_KEY;
        srs_error("check the vhost=%s %s failed, tcUrl=%s, ret=%d", 
            req->vhost.c_str(), key.c_str(), req->tcUrl.c_str(), ret);
        return ret;
    }
    
    // shared global last check time,
    // to avoid attach by bandwidth check,
    // if client request check in the window(specifeid by interval),
    // directly reject the request.
    static int64_t last_check_time = 0;
    int interval_ms = _srs_config->get_bw_check_interval_ms(req->vhost);
    
    int64_t time_now = srs_get_system_time_ms();
    // reject the connection in the interval window.
    if (last_check_time > 0 && time_now - last_check_time < interval_ms) {
        ret = ERROR_SYSTEM_BANDWIDTH_DENIED;
        srs_trace("bandcheck denied, "
            "last_check=%"PRId64", now=%"PRId64", interval=%d",
            last_check_time, time_now, interval_ms);
            
        rtmp->response_connect_reject(req, "bandcheck rejected");
        return ret;
    }
    
    // accept and do bandwidth check.
    last_check_time = time_now;
    
    std::string local_ip = srs_get_local_ip(st_netfd_fileno(stfd));
    if ((ret = rtmp->response_connect_app(req, local_ip.c_str())) != ERROR_SUCCESS) {
        srs_error("response connect app failed. ret=%d", ret);
        return ret;
    }

    return do_bandwidth_check();
}

int SrsBandwidth::do_bandwidth_check()
{
    int ret = ERROR_SUCCESS;
    
    SrsProtocol* protocol = rtmp->get_protocol();

    int play_duration_ms        = 3000;
    int play_interval_ms        = 0;
    int play_actual_duration_ms = 0;
    int play_bytes              = 0;

    int publish_duration_ms        = 3000;
    int publish_interval_ms        = 0;
    int publish_actual_duration_ms = 0;
    int publish_bytes              = 0;
    
    int limit_kbps = _srs_config->get_bw_check_limit_kbps(req->vhost);

    int64_t start_time = srs_get_system_time_ms();
    
    ret = check_play(play_duration_ms, 
        play_interval_ms, play_actual_duration_ms, play_bytes, limit_kbps);
    if (ret != ERROR_SUCCESS) {
        srs_error("band width play check failed. ret=%d", ret);
        return ret;
    }
    
    ret = check_publish(publish_duration_ms, 
        publish_interval_ms, publish_actual_duration_ms, publish_bytes, limit_kbps);
    if (ret != ERROR_SUCCESS) {
        srs_error("band width publish check failed. ret=%d", ret);
        return ret;
    }

    int64_t end_time = srs_get_system_time_ms();
    int play_kbps = play_bytes * 8 / play_actual_duration_ms;
    int publish_kbps = publish_bytes * 8 / publish_actual_duration_ms;

    srs_trace("bandwidth check finished. start=%"PRId64"ms, end=%"PRId64"ms, "
        "duartion=%dms, play=%dkbps, publish=%dkbps, tcUrl=%s, ret=%#x", 
        start_time, end_time, (int)(end_time - start_time), play_kbps, publish_kbps, 
        req->tcUrl.c_str(), ret);

    // send finished msg
    SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_finish();
    pkt->data->set("code",           SrsAmf0Any::number(ERROR_SUCCESS));
    pkt->data->set("start_time",     SrsAmf0Any::number(start_time));
    pkt->data->set("end_time",       SrsAmf0Any::number(end_time));
    pkt->data->set("play_kbps",      SrsAmf0Any::number(play_kbps));
    pkt->data->set("publish_kbps",   SrsAmf0Any::number(publish_kbps));
    pkt->data->set("play_bytes",     SrsAmf0Any::number(play_bytes));
    pkt->data->set("play_time",      SrsAmf0Any::number(play_actual_duration_ms));
    pkt->data->set("publish_bytes",  SrsAmf0Any::number(publish_bytes));
    pkt->data->set("publish_time",   SrsAmf0Any::number(publish_actual_duration_ms));

    if ((ret = rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send bandwidth check finish message failed. ret=%d", ret);
        return ret;
    }
    
    // if flash, we notice the result, and expect a final packet.
    while (true) {
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            // info level to ignore and return success.
            srs_info("expect final message failed. ret=%d", ret);
            return ERROR_SUCCESS;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get final message success.");
        
        if (pkt->is_flash_final()) {
            srs_info("BW check recv flash final response.");
            break;
        }
    }
    
    srs_info("BW check finished.");

    return ret;
}

int SrsBandwidth::check_play(
    int duration_ms, int interval_ms, int& actual_duration_ms,
    int& play_bytes, int max_play_kbps)
{
    int ret = ERROR_SUCCESS;
    
    SrsProtocol* protocol = rtmp->get_protocol();

    if (true) {
        // send start play command to client
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_play();
    
        pkt->data->set("duration_ms", SrsAmf0Any::number(duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(interval_ms));
    
        if ((ret = rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start play message failed. ret=%d", ret);
            return ret;
        }
        srs_info("BW check begin.");
    }

    while (true) {
        // recv client's starting play response
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_starting_play()) {
            srs_info("BW check recv play begin response.");
            break;
        }
    }

    // send play data to client
    int64_t current_time = srs_get_system_time_ms();
    int size = 1024; // TODO: FIXME: magic number
    char random_data[size];
    memset(random_data, 'A', size);

    int interval = 0;
    int data_count = 1;
    while ( (srs_get_system_time_ms() - current_time) < duration_ms ) {
        st_usleep(interval);
        
        // TODO: FIXME: use shared ptr message.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_playing();

        // TODO: FIXME: magic number
        for (int i = 0; i < data_count; ++i) {
            std::stringstream seq;
            seq << i;
            std::string play_data = "SrS band check data from server's playing......";
            pkt->data->set(seq.str(), SrsAmf0Any::str(play_data.c_str()));
        }
        data_count += 2;

        // get length from the rtmp protocol stack.
        play_bytes = rtmp->get_send_bytes();

        if ((ret = rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check play messages failed. ret=%d", ret);
            return ret;
        }

        // sleep while current kbps <= max_play_kbps
        int kbps = 0;
        while (true) {
            if(srs_get_system_time_ms() - current_time != 0)
                kbps = play_bytes * 8 / (srs_get_system_time_ms() - current_time);

            if (kbps > max_play_kbps) {
                st_usleep(500);
            } else {
                break;
            }
        }
    }
    actual_duration_ms = srs_get_system_time_ms() - current_time;
    srs_info("BW check send play bytes over.");

    if (true) {
        // notify client to stop play
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_play();
        
        pkt->data->set("duration_ms", SrsAmf0Any::number(duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(interval_ms));
        pkt->data->set("duration_delta", SrsAmf0Any::number(actual_duration_ms));
        pkt->data->set("bytes_delta", SrsAmf0Any::number(play_bytes));

        if ((ret = rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop play message failed. ret=%d", ret);
            return ret;
        }
        srs_info("BW check stop play bytes.");
    }

    while (true) {
        // recv client's stop play response.
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_stopped_play()) {
            srs_info("BW check recv stop play response.");
            break;
        }
    }

    return ret;
}

int SrsBandwidth::check_publish(
    int duration_ms, int interval_ms, int& actual_duration_ms,
    int& publish_bytes, int max_pub_kbps)
{
    int ret = ERROR_SUCCESS;
    
    SrsProtocol* protocol = rtmp->get_protocol();

    if (true) {
        // notify client to start publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_publish();
    
        pkt->data->set("duration_ms", SrsAmf0Any::number(duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(interval_ms));
    
        if ((ret = rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start publish message failed. ret=%d", ret);
            return ret;
        }
        srs_info("BW check publish begin.");
    }

    while (true) {
        // read client's notification of starting publish
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_starting_publish()) {
            srs_info("BW check recv publish begin response.");
            break;
        }
    }

    // recv publish msgs until @duration_ms ms
    int64_t current_time = srs_get_system_time_ms();
    while ( (srs_get_system_time_ms() - current_time) < duration_ms ) {
        st_usleep(0);
        
        SrsMessage* msg = NULL;
        if ((ret = rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
            srs_error("recv message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);

        publish_bytes = rtmp->get_recv_bytes();

        int kbps = 0;
        while (true) {
            if(srs_get_system_time_ms() - current_time != 0)
                kbps = publish_bytes * 8 / (srs_get_system_time_ms() - current_time);

            if (kbps > max_pub_kbps) {
                st_usleep(500);
            } else {
                break;
            }
        }
    }
    actual_duration_ms = srs_get_system_time_ms() -  current_time;
    srs_info("BW check recv publish data over.");

    if (true) {
        // notify client to stop publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_publish();
        pkt->data->set("duration_ms", SrsAmf0Any::number(duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(interval_ms));
        pkt->data->set("duration_delta", SrsAmf0Any::number(actual_duration_ms));
        pkt->data->set("bytes_delta", SrsAmf0Any::number(publish_bytes));

        if ((ret = rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop publish message failed. ret=%d", ret);
            return ret;
        }
        srs_info("BW check stop publish bytes.");
    }

    // expect client to stop publish
    // if flash client, we never expect the client stop publish bytes, 
    // for the flash send call packet to test publish bandwidth,
    // there are many many packets in the queue.
    // we just ignore the packet and send the bandwidth test data.
    // TODO: FIXME: check whether flash client.
    while (false) {
        // recv client's stop publish response.
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_stopped_publish()) {
            srs_info("BW check recv stop publish response.");
            break;
        }
    }

    return ret;
}
