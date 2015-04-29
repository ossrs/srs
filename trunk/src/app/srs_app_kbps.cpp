/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#include <srs_app_kbps.hpp>

#include <srs_kernel_utility.hpp>
#include <srs_app_st.hpp>

#define _SRS_BANDWIDTH_LIMIT_INTERVAL_MS 100

SrsKbpsSample::SrsKbpsSample()
{
    bytes = time = 0;
    kbps = 0;
}

SrsKbpsSlice::SrsKbpsSlice()
{
    io.in = NULL;
    io.out = NULL;
    last_bytes = io_bytes_base = starttime = bytes = delta_bytes = 0;
}

SrsKbpsSlice::~SrsKbpsSlice()
{
}

int64_t SrsKbpsSlice::get_total_bytes()
{
    return bytes + last_bytes - io_bytes_base;
}

void SrsKbpsSlice::sample()
{
    int64_t now = srs_get_system_time_ms();
    int64_t total_bytes = get_total_bytes();
    
    if (sample_30s.time <= 0) {
        sample_30s.kbps = 0;
        sample_30s.time = now;
        sample_30s.bytes = total_bytes;
    }
    if (sample_1m.time <= 0) {
        sample_1m.kbps = 0;
        sample_1m.time = now;
        sample_1m.bytes = total_bytes;
    }
    if (sample_5m.time <= 0) {
        sample_5m.kbps = 0;
        sample_5m.time = now;
        sample_5m.bytes = total_bytes;
    }
    if (sample_60m.time <= 0) {
        sample_60m.kbps = 0;
        sample_60m.time = now;
        sample_60m.bytes = total_bytes;
    }
    
    if (now - sample_30s.time > 30 * 1000) {
        sample_30s.kbps = (total_bytes - sample_30s.bytes) * 8 / (now - sample_30s.time);
        sample_30s.time = now;
        sample_30s.bytes = total_bytes;
    }
    if (now - sample_1m.time > 60 * 1000) {
        sample_1m.kbps = (total_bytes - sample_1m.bytes) * 8 / (now - sample_1m.time);
        sample_1m.time = now;
        sample_1m.bytes = total_bytes;
    }
    if (now - sample_5m.time > 300 * 1000) {
        sample_5m.kbps = (total_bytes - sample_5m.bytes) * 8 / (now - sample_5m.time);
        sample_5m.time = now;
        sample_5m.bytes = total_bytes;
    }
    if (now - sample_60m.time > 3600 * 1000) {
        sample_60m.kbps = (total_bytes - sample_60m.bytes) * 8 / (now - sample_60m.time);
        sample_60m.time = now;
        sample_60m.bytes = total_bytes;
    }
}

IKbpsDelta::IKbpsDelta()
{
}

IKbpsDelta::~IKbpsDelta()
{
}

SrsKbps::SrsKbps()
{
}

SrsKbps::~SrsKbps()
{
}

void SrsKbps::set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out)
{
    // set input stream
    // now, set start time.
    if (is.starttime == 0) {
        is.starttime = srs_get_system_time_ms();
    }
    // save the old in bytes.
    if (is.io.in) {
        is.bytes += is.last_bytes - is.io_bytes_base;
    }
    // use new io.
    is.io.in = in;
    is.last_bytes = is.io_bytes_base = 0;
    if (in) {
        is.last_bytes = is.io_bytes_base = in->get_recv_bytes();
    }
    // resample
    is.sample();
    
    // set output stream
    // now, set start time.
    if (os.starttime == 0) {
        os.starttime = srs_get_system_time_ms();
    }
    // save the old in bytes.
    if (os.io.out) {
        os.bytes += os.last_bytes - os.io_bytes_base;
    }
    // use new io.
    os.io.out = out;
    os.last_bytes = os.io_bytes_base = 0;
    if (out) {
        os.last_bytes = os.io_bytes_base = out->get_send_bytes();
    }
    // resample
    os.sample();
}

int SrsKbps::get_send_kbps()
{
    int64_t duration = srs_get_system_time_ms() - is.starttime;
    if (duration <= 0) {
        return 0;
    }
    int64_t bytes = get_send_bytes();
    return bytes * 8 / duration;
}

int SrsKbps::get_recv_kbps()
{
    int64_t duration = srs_get_system_time_ms() - os.starttime;
    if (duration <= 0) {
        return 0;
    }
    int64_t bytes = get_recv_bytes();
    return bytes * 8 / duration;
}

int SrsKbps::get_send_kbps_30s()
{
    return os.sample_30s.kbps;
}

int SrsKbps::get_recv_kbps_30s()
{
    return is.sample_30s.kbps;
}

int SrsKbps::get_send_kbps_5m()
{
    return os.sample_5m.kbps;
}

int SrsKbps::get_recv_kbps_5m()
{
    return is.sample_5m.kbps;
}

int64_t SrsKbps::get_send_bytes()
{
    return os.get_total_bytes();
}

int64_t SrsKbps::get_recv_bytes()
{
    return is.get_total_bytes();
}

void SrsKbps::resample()
{
    sample();
}

int64_t SrsKbps::get_send_bytes_delta()
{
    int64_t delta = os.get_total_bytes() - os.delta_bytes;
    return delta;
}

int64_t SrsKbps::get_recv_bytes_delta()
{
    int64_t delta = is.get_total_bytes() - is.delta_bytes;
    return delta;
}

void SrsKbps::cleanup()
{
    os.delta_bytes = os.get_total_bytes();
    is.delta_bytes = is.get_total_bytes();
}

void SrsKbps::add_delta(IKbpsDelta* delta)
{
    srs_assert(delta);
    
    // update the total bytes
    is.last_bytes += delta->get_recv_bytes_delta();
    os.last_bytes += delta->get_send_bytes_delta();
    
    // we donot sample, please use sample() to do resample.
}

void SrsKbps::sample()
{
    // update the total bytes
    if (os.io.out) {
        os.last_bytes = os.io.out->get_send_bytes();
    }
    
    if (is.io.in) {
        is.last_bytes = is.io.in->get_recv_bytes();
    }
    
    // resample
    is.sample();
    os.sample();
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


