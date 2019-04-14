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

#define _SRS_BANDWIDTH_LIMIT_INTERVAL 100 * SRS_UTIME_MILLISECONDS

// default sample duration, in ms
#define _SRS_BANDWIDTH_SAMPLE_DURATION 3000 * SRS_UTIME_MILLISECONDS

// wait for a while for flash to got all packets.
#define _SRS_BANDWIDTH_FINAL_WAIT 600 * SRS_UTIME_MILLISECONDS

SrsBandwidthSample::SrsBandwidthSample()
{
    duration_ms = _SRS_BANDWIDTH_SAMPLE_DURATION;
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
srs_error_t _srs_expect_bandwidth_packet(SrsRtmpServer* rtmp, _CheckPacketType pfn)
{
    srs_error_t err = srs_success;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((err = rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "expect message");
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsBandwidthPacket, pkt);
        
        if (pfn(pkt)) {
            return err;
        }
    }
    
    return err;
}

SrsBandwidth::SrsBandwidth()
{
    _req = NULL;
    _rtmp = NULL;
}

SrsBandwidth::~SrsBandwidth()
{
}

srs_error_t SrsBandwidth::bandwidth_check(SrsRtmpServer* rtmp, ISrsProtocolStatistic* io_stat, SrsRequest* req, string local_ip)
{
    srs_error_t err = srs_success;
    
    _rtmp = rtmp;
    _req = req;
    
    if (!_srs_config->get_bw_check_enabled(_req->vhost)) {
        return err;
    }
    
    // validate the bandwidth check key
    std::string key = "key=" + _srs_config->get_bw_check_key(_req->vhost);
    if (_req->tcUrl.find(key) == std::string::npos) {
        return srs_error_new(ERROR_SYSTEM_BANDWIDTH_KEY, "check the vhost=%s %s failed, tcUrl=%s",
            _req->vhost.c_str(), key.c_str(), _req->tcUrl.c_str());
    }
    
    // shared global last check time,
    // to prevent bandwidth check attack,
    // if client request check in the window(specifeid by interval),
    // directly reject the request.
    static srs_utime_t last_check_time = 0;
    srs_utime_t interval = _srs_config->get_bw_check_interval(_req->vhost);
    
    srs_utime_t time_now = srs_update_system_time();
    // reject the connection in the interval window.
    if (last_check_time > 0 && time_now - last_check_time < interval) {
        _rtmp->response_connect_reject(_req, "bandcheck rejected");
        return srs_error_new(ERROR_SYSTEM_BANDWIDTH_DENIED, "reject, last_check=%" PRId64 ", now=%" PRId64 ", interval=%d", last_check_time, time_now, interval);
    }
    
    // accept and do bandwidth check.
    last_check_time = time_now;
    
    if ((err = _rtmp->response_connect_app(_req, local_ip.c_str())) != srs_success) {
        return srs_error_wrap(err, "response connect app");
    }
    
    // create a limit object.
    SrsWallClock clk;
    SrsKbps kbps(&clk);
    kbps.set_io(io_stat, io_stat);
    
    int limit_kbps = _srs_config->get_bw_check_limit_kbps(_req->vhost);
    SrsKbpsLimit limit(&kbps, limit_kbps);
    
    return do_bandwidth_check(&limit);
}

srs_error_t SrsBandwidth::do_bandwidth_check(SrsKbpsLimit* limit)
{
    srs_error_t err = srs_success;
    
    SrsBandwidthSample play_sample;
    SrsBandwidthSample publish_sample;
    
    // timeout for a packet.
    _rtmp->set_send_timeout(play_sample.duration_ms * 2);
    _rtmp->set_recv_timeout(publish_sample.duration_ms * 2);
    
    // start test.
    srs_utime_t start_time = srs_update_system_time();
    
    // sample play
    if ((err = play_start(&play_sample, limit)) != srs_success) {
        return srs_error_wrap(err, "play start");
    }
    if ((err = play_checking(&play_sample, limit)) != srs_success) {
        return srs_error_wrap(err, "play check");
    }
    if ((err = play_stop(&play_sample, limit)) != srs_success) {
        return srs_error_wrap(err, "play stop");
    }
    
    // sample publish
    if ((err = publish_start(&publish_sample, limit)) != srs_success) {
        return srs_error_wrap(err, "publish start");
    }
    if ((err = publish_checking(&publish_sample, limit)) != srs_success) {
        return srs_error_wrap(err, "publish check");
    }
    if ((err = publish_stop(&publish_sample, limit)) != srs_success) {
        return srs_error_wrap(err, "publish stop");
    }
    
    // stop test.
    srs_utime_t end_time = srs_update_system_time();
    
    if ((err = do_final(play_sample, publish_sample, start_time, end_time)) != srs_success) {
        return srs_error_wrap(err, "final");
    }

    srs_trace("bandwidth ok. duartion=%dms(%d+%d), play=%dkbps, publish=%dkbps",
		srsu2msi(end_time - start_time), play_sample.actual_duration_ms,
		publish_sample.actual_duration_ms, play_sample.kbps,
		publish_sample.kbps);
    
    srs_usleep(_SRS_BANDWIDTH_FINAL_WAIT);
    
    return err;
}

srs_error_t SrsBandwidth::play_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    srs_error_t err = srs_success;
    
    if (true) {
        // send start play command to client
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_play();
        
        pkt->data->set("limit_kbps", SrsAmf0Any::number(limit->limit_kbps()));
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        
        if ((err = _rtmp->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }
    
    if ((err = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_starting_play)) != srs_success) {
        return srs_error_wrap(err, "expect bandwidth");
    }
    
    return err;
}

srs_error_t SrsBandwidth::play_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    srs_error_t err = srs_success;
    
    // send play data to client
    int size = 1024; // TODO: FIXME: magic number
    char random_data[size];
    memset(random_data, 'A', size);
    
    int data_count = 1;
    srs_utime_t starttime = srs_update_system_time();
    while (int64_t(srsu2ms(srs_get_system_time() - starttime)) < sample->duration_ms) {
        srs_usleep(sample->interval_ms * SRS_UTIME_MILLISECONDS);
        
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
        
        if ((err = _rtmp->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
        
        limit->send_limit();
    }
    srs_update_system_time();
    sample->calc_kbps((int)_rtmp->get_send_bytes(), srsu2msi(srs_get_system_time() - starttime));
    
    return err;
}

srs_error_t SrsBandwidth::play_stop(SrsBandwidthSample* sample, SrsKbpsLimit* /*limit*/)
{
    srs_error_t err = srs_success;
    
    if (true) {
        // notify client to stop play
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_play();
        
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        pkt->data->set("duration_delta", SrsAmf0Any::number(sample->actual_duration_ms));
        pkt->data->set("bytes_delta", SrsAmf0Any::number(sample->bytes));
        
        if ((err = _rtmp->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }
    
    if ((err = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_stopped_play)) != srs_success) {
        return srs_error_wrap(err, "expect bandwidth");
    }
    
    return err;
}

srs_error_t SrsBandwidth::publish_start(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    srs_error_t err = srs_success;
    
    if (true) {
        // notify client to start publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_start_publish();
        
        pkt->data->set("limit_kbps", SrsAmf0Any::number(limit->limit_kbps()));
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        
        if ((err = _rtmp->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }
    
    if ((err = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_starting_publish)) != srs_success) {
        return srs_error_wrap(err, "expect packet");
    }
    
    return err;
}

srs_error_t SrsBandwidth::publish_checking(SrsBandwidthSample* sample, SrsKbpsLimit* limit)
{
    srs_error_t err = srs_success;
    
    // recv publish msgs until @duration_ms ms
    srs_utime_t starttime = srs_update_system_time();
    while (int64_t(srsu2ms(srs_get_system_time() - starttime)) < sample->duration_ms) {
        SrsCommonMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((err = _rtmp->expect_message<SrsBandwidthPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "expect message");
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
    srs_update_system_time();
    sample->calc_kbps((int)_rtmp->get_recv_bytes(), srsu2msi(srs_get_system_time() - starttime));
    
    return err;
}

srs_error_t SrsBandwidth::publish_stop(SrsBandwidthSample* sample, SrsKbpsLimit* /*limit*/)
{
    srs_error_t err = srs_success;
    
    if (true) {
        // notify client to stop publish
        SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_stop_publish();
        pkt->data->set("duration_ms", SrsAmf0Any::number(sample->duration_ms));
        pkt->data->set("interval_ms", SrsAmf0Any::number(sample->interval_ms));
        pkt->data->set("duration_delta", SrsAmf0Any::number(sample->actual_duration_ms));
        pkt->data->set("bytes_delta", SrsAmf0Any::number(sample->bytes));
        
        if ((err = _rtmp->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }
    
    // expect client to stop publish
    // if flash client, we never expect the client stop publish bytes,
    // for the flash send call packet to test publish bandwidth,
    // there are many many packets in the queue.
    // we just ignore the packet and send the bandwidth test data.
    bool is_flash = (_req->swfUrl != "");
    if (!is_flash) {
        if ((err = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_stopped_publish)) != srs_success) {
            return srs_error_wrap(err, "expect bandwidth");
        }
    }
    
    return err;
}

srs_error_t SrsBandwidth::do_final(SrsBandwidthSample& play_sample, SrsBandwidthSample& publish_sample, srs_utime_t start_time, srs_utime_t& end_time)
{
    srs_error_t err = srs_success;
    
    // send finished msg,
    // flash client will close connection when got this packet,
    // for the publish queue may contains packets.
    SrsBandwidthPacket* pkt = SrsBandwidthPacket::create_finish();
    pkt->data->set("start_time",     SrsAmf0Any::number(srsu2ms(start_time)));
    pkt->data->set("end_time",       SrsAmf0Any::number(srsu2ms(end_time)));
    pkt->data->set("play_kbps",      SrsAmf0Any::number(play_sample.kbps));
    pkt->data->set("publish_kbps",   SrsAmf0Any::number(publish_sample.kbps));
    pkt->data->set("play_bytes",     SrsAmf0Any::number(play_sample.bytes));
    pkt->data->set("publish_bytes",  SrsAmf0Any::number(publish_sample.bytes));
    pkt->data->set("play_time",      SrsAmf0Any::number(play_sample.actual_duration_ms));
    pkt->data->set("publish_time",   SrsAmf0Any::number(publish_sample.actual_duration_ms));
    
    if ((err = _rtmp->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send packet");
    }
    
    // we notice the result, and expect a final packet if not flash.
    // if flash client, client will disconnect when got finish packet.
    bool is_flash = (_req->swfUrl != "");
    if (!is_flash) {
        // ignore any error.
        err = _srs_expect_bandwidth_packet(_rtmp, _bandwidth_is_final);
        srs_error_reset(err);
    }
    
    return err;
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
        
        srs_usleep(_SRS_BANDWIDTH_LIMIT_INTERVAL);
    }
}

void SrsKbpsLimit::send_limit()
{
    _kbps->sample();
    
    while (_kbps->get_send_kbps() > _limit_kbps) {
        _kbps->sample();
        
        srs_usleep(_SRS_BANDWIDTH_LIMIT_INTERVAL);
    }
}

