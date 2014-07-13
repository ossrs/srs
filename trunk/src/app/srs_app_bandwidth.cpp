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
#include <srs_protocol_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_kbps.hpp>

// default sample duration, in ms
#define _SRS_BANDWIDTH_SAMPLE_DURATION_MS 3000

SrsBandwidthSample::SrsBandwidthSample()
{
    duration_ms = _SRS_BANDWIDTH_SAMPLE_DURATION_MS;
    kbps = interval_ms = actual_duration_ms = bytes = 0;
}

SrsBandwidthSample::~SrsBandwidthSample()
{
}

void SrsBandwidthSample::calc_kbps()
{
    if (actual_duration_ms <= 0) {
        return;
    }
    
    kbps = bytes * 8 / actual_duration_ms;
}

SrsBandwidth::SrsBandwidth()
{
    _req = NULL;
    _rtmp = NULL;
}

SrsBandwidth::~SrsBandwidth()
{
}

int SrsBandwidth::bandwidth_check(SrsRtmpServer* rtmp, ISrsProtocolStatistic* io_stat, SrsRequest* req, string local_ip)
{
    int ret = ERROR_SUCCESS;
    
    _rtmp = rtmp;
    _req = req;
    
    if (!_srs_config->get_bw_check_enabled(_req->vhost)) {
        return ret;
    }

    // validate the bandwidth check key
    std::string key = "key=" + _srs_config->get_bw_check_key(_req->vhost);
    if (_req->tcUrl.find(key) == std::string::npos) {
        ret = ERROR_SYSTEM_BANDWIDTH_KEY;
        srs_error("check the vhost=%s %s failed, tcUrl=%s, ret=%d", 
            _req->vhost.c_str(), key.c_str(), _req->tcUrl.c_str(), ret);
        return ret;
    }
    
    // shared global last check time,
    // to prevent bandwidth check attack,
    // if client request check in the window(specifeid by interval),
    // directly reject the request.
    static int64_t last_check_time = 0;
    int interval_ms = _srs_config->get_bw_check_interval_ms(_req->vhost);
    
    int64_t time_now = srs_get_system_time_ms();
    // reject the connection in the interval window.
    if (last_check_time > 0 && time_now - last_check_time < interval_ms) {
        ret = ERROR_SYSTEM_BANDWIDTH_DENIED;
        srs_trace("reject, "
            "last_check=%"PRId64", now=%"PRId64", interval=%d",
            last_check_time, time_now, interval_ms);
            
        _rtmp->response_connect_reject(_req, "bandcheck rejected");
        return ret;
    }
    
    // accept and do bandwidth check.
    last_check_time = time_now;
    
    if ((ret = _rtmp->response_connect_app(_req, local_ip.c_str())) != ERROR_SUCCESS) {
        srs_error("response connect app failed. ret=%d", ret);
        return ret;
    }
    
    // create a limit object.
    SrsKbps kbps;
    kbps.set_io(io_stat, io_stat);

    int limit_kbps = _srs_config->get_bw_check_limit_kbps(_req->vhost);
    SrsKbpsLimit limit(&kbps, limit_kbps);
    
    return do_bandwidth_check(&limit);
}

int SrsBandwidth::do_bandwidth_check(SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;

    SrsBandwidthSample play_sample;
    SrsBandwidthSample publish_sample;

    int64_t start_time = srs_get_system_time_ms();
    
    // sample play
    srs_info("start play test.");
    
    if ((ret = check_play(&play_sample, limit)) != ERROR_SUCCESS) {
        srs_error("band width play check failed. ret=%d", ret);
        return ret;
    }
    
    play_sample.calc_kbps();
    srs_info("stop play test. kbps=%d", play_sample.kbps);
    
    // sample publish
    srs_info("start publish test.");
    
    if ((ret = check_publish(&publish_sample, limit)) != ERROR_SUCCESS) {
        srs_error("band width publish check failed. ret=%d", ret);
        return ret;
    }
    
    publish_sample.calc_kbps();
    srs_info("stop publish test. kbps=%d", publish_sample.kbps);

    // stop test.
    int64_t end_time = srs_get_system_time_ms();

    srs_trace("bandwidth ok. duartion=%dms(%d+%d), play=%dkbps, publish=%dkbps", 
        (int)(end_time - start_time), play_sample.actual_duration_ms, 
        publish_sample.actual_duration_ms, play_sample.kbps, 
        publish_sample.kbps);

    // send finished msg,
    // flash client will close connection when got this packet,
    // for the publish queue may contains packets.
    SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_finish();
    pkt->data->set("code",           SrsAmf0Any::number(ERROR_SUCCESS));
    pkt->data->set("start_time",     SrsAmf0Any::number(start_time));
    pkt->data->set("end_time",       SrsAmf0Any::number(end_time));
    pkt->data->set("play_kbps",      SrsAmf0Any::number(play_sample.kbps));
    pkt->data->set("publish_kbps",   SrsAmf0Any::number(publish_sample.kbps));
    pkt->data->set("play_bytes",     SrsAmf0Any::number(play_sample.bytes));
    pkt->data->set("publish_bytes",  SrsAmf0Any::number(publish_sample.bytes));
    pkt->data->set("play_time",      SrsAmf0Any::number(play_sample.actual_duration_ms));
    pkt->data->set("publish_time",   SrsAmf0Any::number(publish_sample.actual_duration_ms));

    if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send bandwidth check finish message failed. ret=%d", ret);
        return ret;
    }
    srs_info("send finish packet.");
    
    // we notice the result, and expect a final packet if not flash.
    // if flash client, client will disconnect when got finish packet.
    bool is_flash = (_req->swfUrl != "");
    while (!is_flash) {
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
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

int SrsBandwidth::check_play(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;

    if (true) {
        // send start play command to client
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_play();
    
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check begin.");

    while (true) {
        // recv client's starting play response
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_starting_play()) {
            break;
        }
    }
    srs_info("BW check recv play begin response.");

    // send play data to client
    int64_t current_time = srs_get_system_time_ms();
    int size = 1024; // TODO: FIXME: magic number
    char random_data[size];
    memset(random_data, 'A', size);

    int interval = 0;
    int data_count = 1;
    while ((srs_get_system_time_ms() - current_time) < duration_ms) {
        st_usleep(interval);
        
        // TODO: FIXME: use shared ptr message.
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_playing();

        // TODO: FIXME: magic number
        for (int i = 0; i < data_count; ++i) {
            std::stringstream seq;
            seq << i;
            std::string play_data = "SRS band check data from server's playing......";
            pkt->data->set(seq.str(), SrsAmf0Any::str(play_data.c_str()));
        }
        data_count += 2;

        // get length from the rtmp protocol stack.
        play_bytes = _rtmp->get_send_bytes();

        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
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

        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check stop play bytes.");

    while (true) {
        // recv client's stop play response.
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_stopped_play()) {
            break;
        }
    }
    srs_info("BW check recv stop play response.");

    return ret;
}

int SrsBandwidth::check_publish(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;

    if (true) {
        // notify client to start publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_publish();
    
        pkt->data->set("duration_ms", SrsAmf0Any::number(duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(interval_ms));
    
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check publish begin.");

    while (true) {
        // read client's notification of starting publish
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_starting_publish()) {
            break;
        }
    }
    srs_info("BW check recv publish begin response.");

    // recv publish msgs until @duration_ms ms
    int64_t current_time = srs_get_system_time_ms();
    while ((srs_get_system_time_ms() - current_time) < duration_ms) {
        st_usleep(0);
        
        SrsMessage* msg = NULL;
        if ((ret = _rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
            srs_error("recv message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);

        publish_bytes = _rtmp->get_recv_bytes();

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

        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check stop publish bytes.");

    // expect client to stop publish
    // if flash client, we never expect the client stop publish bytes, 
    // for the flash send call packet to test publish bandwidth,
    // there are many many packets in the queue.
    // we just ignore the packet and send the bandwidth test data.
    bool is_flash = (_req->swfUrl != "");
    while (!is_flash) {
        // recv client's stop publish response.
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect bandwidth message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get bandwidth message succes.");
        
        if (pkt->is_stopped_publish()) {
            break;
        }
    }
    srs_info("BW check recv stop publish response.");

    return ret;
}
