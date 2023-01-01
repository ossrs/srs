//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_kbps.hpp>

#include <srs_kernel_utility.hpp>

SrsKbpsSlice::SrsKbpsSlice(SrsWallClock* c)
{
    clk = c;
    starttime = 0;
    bytes = 0;
}

SrsKbpsSlice::~SrsKbpsSlice()
{
}

void SrsKbpsSlice::sample()
{
    srs_utime_t now = clk->now();

    if (sample_30s.time < 0) {
        sample_30s.update(bytes, now, 0);
    }
    if (sample_1m.time < 0) {
        sample_1m.update(bytes, now, 0);
    }
    if (sample_5m.time < 0) {
        sample_5m.update(bytes, now, 0);
    }
    if (sample_60m.time < 0) {
        sample_60m.update(bytes, now, 0);
    }
    
    if (now - sample_30s.time >= 30 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes - sample_30s.total) * 8 / srsu2ms(now - sample_30s.time));
        sample_30s.update(bytes, now, kbps);
    }
    if (now - sample_1m.time >= 60 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes - sample_1m.total) * 8 / srsu2ms(now - sample_1m.time));
        sample_1m.update(bytes, now, kbps);
    }
    if (now - sample_5m.time >= 300 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes - sample_5m.total) * 8 / srsu2ms(now - sample_5m.time));
        sample_5m.update(bytes, now, kbps);
    }
    if (now - sample_60m.time >= 3600 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes - sample_60m.total) * 8 / srsu2ms(now - sample_60m.time));
        sample_60m.update(bytes, now, kbps);
    }
}

ISrsKbpsDelta::ISrsKbpsDelta()
{
}

ISrsKbpsDelta::~ISrsKbpsDelta()
{
}

SrsEphemeralDelta::SrsEphemeralDelta()
{
    in_ = out_ = 0;
}

SrsEphemeralDelta::~SrsEphemeralDelta()
{
}

void SrsEphemeralDelta::add_delta(int64_t in, int64_t out)
{
    in_ += in;
    out_ += out;
}

void SrsEphemeralDelta::remark(int64_t* in, int64_t* out)
{
    if (in) *in = in_;
    if (out) *out = out_;
    in_ = out_ = 0;
}

SrsNetworkDelta::SrsNetworkDelta()
{
    in_ = out_ = NULL;
    in_base_ = in_delta_ = 0;
    out_base_ = out_delta_ = 0;
}

SrsNetworkDelta::~SrsNetworkDelta()
{
}

void SrsNetworkDelta::set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out)
{
    if (in_) {
        in_delta_ += in_->get_recv_bytes() - in_base_;
    }
    if (in) {
        in_base_ = in->get_recv_bytes();
        in_delta_ += in_base_;
    }
    in_ = in;

    if (out_) {
        out_delta_ += out_->get_send_bytes() - out_base_;
    }
    if (out) {
        out_base_ = out->get_send_bytes();
        out_delta_ += out_base_;
    }
    out_ = out;
}

void SrsNetworkDelta::remark(int64_t* in, int64_t* out)
{
    if (in_) {
        in_delta_ += in_->get_recv_bytes() - in_base_;
        in_base_ = in_->get_recv_bytes();
    }
    if (out_) {
        out_delta_ += out_->get_send_bytes() - out_base_;
        out_base_ = out_->get_send_bytes();
    }

    *in = in_delta_;
    *out = out_delta_;
    in_delta_ = out_delta_ = 0;
}

SrsKbps::SrsKbps(SrsWallClock* c)
{
    clk = c ? c : _srs_clock;
    is = new SrsKbpsSlice(clk);
    os = new SrsKbpsSlice(clk);
}

SrsKbps::~SrsKbps()
{
    srs_freep(is);
    srs_freep(os);
}

int SrsKbps::get_send_kbps()
{
    int duration = srsu2ms(clk->now() - is->starttime);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = get_send_bytes();
    return (int)(bytes * 8 / duration);
}

int SrsKbps::get_recv_kbps()
{
    int duration = srsu2ms(clk->now() - os->starttime);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = get_recv_bytes();
    return (int)(bytes * 8 / duration);
}

int SrsKbps::get_send_kbps_30s()
{
    return os->sample_30s.rate;
}

int SrsKbps::get_recv_kbps_30s()
{
    return is->sample_30s.rate;
}

int SrsKbps::get_send_kbps_5m()
{
    return os->sample_5m.rate;
}

int SrsKbps::get_recv_kbps_5m()
{
    return is->sample_5m.rate;
}

void SrsKbps::add_delta(ISrsKbpsDelta* delta)
{
    if (!delta) return;

    int64_t in, out;
    delta->remark(&in, &out);
    add_delta(in, out);
}

void SrsKbps::add_delta(int64_t in, int64_t out)
{
    // update the total bytes
    is->bytes += in;
    os->bytes += out;
    
    // we donot sample, please use sample() to do resample.
}

void SrsKbps::sample()
{
    is->sample();
    os->sample();
}

int64_t SrsKbps::get_send_bytes()
{
    return os->bytes;
}

int64_t SrsKbps::get_recv_bytes()
{
    return is->bytes;
}

SrsNetworkKbps::SrsNetworkKbps(SrsWallClock* clock)
{
    delta_ = new SrsNetworkDelta();
    kbps_ = new SrsKbps(clock);
}

SrsNetworkKbps::~SrsNetworkKbps()
{
    srs_freep(kbps_);
    srs_freep(delta_);
}

void SrsNetworkKbps::set_io(ISrsProtocolStatistic* in, ISrsProtocolStatistic* out)
{
    delta_->set_io(in, out);
}

void SrsNetworkKbps::sample()
{
    kbps_->add_delta(delta_);
    kbps_->sample();
}

int SrsNetworkKbps::get_send_kbps()
{
    return kbps_->get_send_kbps();
}

int SrsNetworkKbps::get_recv_kbps()
{
    return kbps_->get_recv_kbps();
}

int SrsNetworkKbps::get_send_kbps_30s()
{
    return kbps_->get_send_kbps_30s();
}

int SrsNetworkKbps::get_recv_kbps_30s()
{
    return kbps_->get_recv_kbps_30s();
}

int SrsNetworkKbps::get_send_kbps_5m()
{
    return kbps_->get_send_kbps_5m();
}

int SrsNetworkKbps::get_recv_kbps_5m()
{
    return kbps_->get_recv_kbps_5m();
}

int64_t SrsNetworkKbps::get_send_bytes()
{
    return kbps_->get_send_bytes();
}

int64_t SrsNetworkKbps::get_recv_bytes()
{
    return kbps_->get_recv_bytes();
}

