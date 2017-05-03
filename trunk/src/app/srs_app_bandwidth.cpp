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

#include <srs_app_bandwidth.hpp>

#include <arpa/inet.h>
#include <sstream>

using namespace std;

#include <srs_rtmp_stack.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_config.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_st.hpp>

#define _SRS_BANDWIDTH_LIMIT_INTERVAL_MS 100

// default sample duration, in ms
#define _SRS_BANDWIDTH_SAMPLE_DURATION_MS 3000

// wait for a while for flash to got all packets.
#define _SRS_BANDWIDTH_FINAL_WAIT_MS 600

SrsBandwidthSample::SrsBandwidthSample()
{
    duration_ms = _SRS_BANDWIDTH_SAMPLE_DURATION_MS;
    kbps = interval_ms = actual_duration_ms = bytes = 0;
}

SrsBandwidthSample::~SrsBandwidthSample()
{
}

void SrsBandwidthSample::calc_kbps(int _bytes, int _duration)
{
    bytes = _bytes;
    actual_duration_ms = _duration;
    
    if (actual_duration_ms <= 0) {
        return;
    }
    
    kbps = bytes * 8 / actual_duration_ms;
}

/**
 * recv bandwidth helper.
 */
typedef bool (*_CheckPacketType)(SrsBandwidthPacket* pkt);
bool _bandwidth_is_final(SrsBandwidthPacket* pkt)
{
    return pkt->is_final();
}
bool _bandwidth_is_starting_play(SrsBandwidthPacket* pkt)
{
    return pkt->is_starting_play();
}
bool _bandwidth_is_stopped_play(SrsBandwidthPacket* pkt)
{
    return pkt->is_stopped_play();
}
bool _bandwidth_is_starting_publish(SrsBandwidthPacket* pkt)
{
    return pkt->is_starting_publish();
}
bool _bandwidth_is_stopped_publish(SrsBandwidthPacket* pkt)
{
    return pkt->is_stopped_publish();
}
int _srs_expect_bandwidth_packet(SrsRtmpServer* rtmp, _CheckPacketType pfn)
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
        srs_info("get bwtc message success.");
        
        if (pfn(pkt)) {
            return ret;
        }
    }
    
    return ret;
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
    
    srs_update_system_time_ms();
    int64_t time_now = srs_get_system_time_ms();
    // reject the connection in the interval window.
    if (last_check_time > 0 && time_now - last_check_time < interval_ms) {
        ret = ERROR_SYSTEM_BANDWIDTH_DENIED;
        srs_trace("reject, "
                  "last_check=%" PRId64 ", now=%" PRId64 ", interval=%d",
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
    
    // timeout for a packet.
    _rtmp->set_send_timeout(play_sample.duration_ms * 2);
    _rtmp->set_recv_timeout(publish_sample.duration_ms * 2);
    
    // start test.
    srs_update_system_time_ms();
    int64_t start_time = srs_get_system_time_ms();
    
    // sample play
    srs_info("start play test.");
    
    if ((ret = play_start(&play_sample, limit)) != ERROR_SUCCESS) {
        srs_error("bandwidth play check failed. ret=%d", ret);
        return ret;
    }
    if ((ret = play_checking(&play_sample, limit)) != ERROR_SUCCESS) {
        srs_error("bandwidth play check failed. ret=%d", ret);
        return ret;
    }
    if ((ret = play_stop(&play_sample, limit)) != ERROR_SUCCESS) {
        srs_error("bandwidth play check failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("stop play test. kbps=%d", play_sample.kbps);
    
    // sample publish
    srs_info("start publish test.");
    
    if ((ret = publish_start(&publish_sample, limit)) != ERROR_SUCCESS) {
        srs_error("bandwidth publish check failed. ret=%d", ret);
        return ret;
    }
    if ((ret = publish_checking(&publish_sample, limit)) != ERROR_SUCCESS) {
        srs_error("bandwidth publish check failed. ret=%d", ret);
        return ret;
    }
    if ((ret = publish_stop(&publish_sample, limit)) != ERROR_SUCCESS) {
        srs_error("bandwidth publish check failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("stop publish test. kbps=%d", publish_sample.kbps);
    
    // stop test.
    srs_update_system_time_ms();
    int64_t end_time = srs_get_system_time_ms();
    
    srs_trace("bandwidth ok. duartion=%dms(%d+%d), play=%dkbps, publish=%dkbps",
              (int)(end_time - start_time), play_sample.actual_duration_ms,
              publish_sample.actual_duration_ms, play_sample.kbps,
              publish_sample.kbps);
    
    if ((ret = finial(play_sample, publish_sample, start_time, end_time)) != ERROR_SUCCESS) {
        return ret;
    }
    
    st_usleep(_SRS_BANDWIDTH_FINAL_WAIT_MS * 1000);
    srs_info("BW check finished.");
    
    return ret;
}

int SrsBandwidth::play_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        // send start play command to client
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_play();
        
        pkt->data->set("limit_kbps", SrsAmf0Any::number(limit->limit_kbps()));
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check play begin.");
    
    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_starting_play)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv play begin response.");
    
    return ret;
}

int SrsBandwidth::play_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;
    
    // send play data to client
    int size = 1024; // TODO: FIXME: magic number
    char random_data[size];
    memset(random_data, 'A', size);
    
    int data_count = 1;
    srs_update_system_time_ms();
    int64_t starttime = srs_get_system_time_ms();
    while ((srs_get_system_time_ms() - starttime) < sample->duration_ms) {
        st_usleep(sample->interval_ms);
        
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
        
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check play messages failed. ret=%d", ret);
            return ret;
        }
        
        limit->send_limit();
    }
    srs_update_system_time_ms();
    sample->calc_kbps(_rtmp->get_send_bytes(), srs_get_system_time_ms() - starttime);
    srs_info("BW check send play bytes over.");
    
    return ret;
}

int SrsBandwidth::play_stop(SrsBandwidthSample* sample, SrsKbpsLimit* /*limit*/)
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        // notify client to stop play
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_play();
        
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        pkt->data->set("duration_delta", SrsAmf0Any::number(sample->actual_duration_ms));
        pkt->data->set("bytes_delta", SrsAmf0Any::number(sample->bytes));
        
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check stop play message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check stop play bytes.");
    
    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_stopped_play)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv stop play response.");
    
    return ret;
}

int SrsBandwidth::publish_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        // notify client to start publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_publish();
        
        pkt->data->set("limit_kbps", SrsAmf0Any::number(limit->limit_kbps()));
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        
        if ((ret = _rtmp->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send bandwidth check start publish message failed. ret=%d", ret);
            return ret;
        }
    }
    srs_info("BW check publish begin.");
    
    if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_starting_publish)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_info("BW check recv publish begin response.");
    
    return ret;
}

int SrsBandwidth::publish_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    int ret = ERROR_SUCCESS;
    
    // recv publish msgs until @duration_ms ms
    srs_update_system_time_ms();
    int64_t starttime = srs_get_system_time_ms();
    while ((srs_get_system_time_ms() - starttime) < sample->duration_ms) {
        SrsCommonMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            return ret;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        srs_info("get publish message success.");
        
        // client requires to stop.
        if (pkt->is_stop_publish()) {
            break;
        }
        
        limit->recv_limit();
    }
    srs_update_system_time_ms();
    sample->calc_kbps(_rtmp->get_recv_bytes(), srs_get_system_time_ms() - starttime);
    srs_info("BW check recv publish data over.");
    
    return ret;
}

int SrsBandwidth::publish_stop(SrsBandwidthSample* sample, SrsKbpsLimit* /*limit*/)
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        // notify client to stop publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_publish();
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        pkt->data->set("duration_delta", SrsAmf0Any::number(sample->actual_duration_ms));
        pkt->data->set("bytes_delta", SrsAmf0Any::number(sample->bytes));
        
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
    if (!is_flash) {
        if ((ret = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_stopped_publish)) != ERROR_SUCCESS) {
            return ret;
        }
        srs_info("BW check recv stop publish response.");
    }
    
    return ret;
}

int SrsBandwidth::finial(SrsBandwidthSample& play_sample, SrsBandwidthSample& publish_sample, int64_t start_time, int64_t& end_time)
{
    int ret = ERROR_SUCCESS;
    
    // send finished msg,
    // flash client will close connection when got this packet,
    // for the publish queue may contains packets.
    SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_finish();
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
    if (!is_flash) {
        // ignore any error.
        _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_final);
        srs_info("BW check recv flash final response.");
    }
    
    srs_info("BW check finished.");
    
    return ret;
}

SrsKbpsLimit::SrsKbpsLimit(SrsKbps* kbps, int limit_kbps)
{
    _kbps = kbps;
    _limit_kbps = limit_kbps;
}

SrsKbpsLimit::~SrsKbpsLimit()
{
}

int SrsKbpsLimit::limit_kbps()
{
    return _limit_kbps;
}

void SrsKbpsLimit::recv_limit()
{
    _kbps->sample();
    
    while (_kbps->get_recv_kbps() > _limit_kbps) {
        _kbps->sample();
        
        st_usleep(_SRS_BANDWIDTH_LIMIT_INTERVAL_MS * 1000);
    }
}

void SrsKbpsLimit::send_limit()
{
    _kbps->sample();
    
    while (_kbps->get_send_kbps() > _limit_kbps) {
        _kbps->sample();
        
        st_usleep(_SRS_BANDWIDTH_LIMIT_INTERVAL_MS * 1000);
    }
}

