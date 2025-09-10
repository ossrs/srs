//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_kbps.hpp>

#include <srs_kernel_utility.hpp>

SrsKbpsSlice::SrsKbpsSlice(SrsWallClock *c)
{
    clk_ = c;
    starttime_ = 0;
    bytes_ = 0;
}

SrsKbpsSlice::~SrsKbpsSlice()
{
}

void SrsKbpsSlice::sample()
{
    srs_utime_t now = clk_->now();

    if (sample_30s_.time_ < 0) {
        sample_30s_.update(bytes_, now, 0);
    }
    if (sample_1m_.time_ < 0) {
        sample_1m_.update(bytes_, now, 0);
    }
    if (sample_5m_.time_ < 0) {
        sample_5m_.update(bytes_, now, 0);
    }
    if (sample_60m_.time_ < 0) {
        sample_60m_.update(bytes_, now, 0);
    }

    if (now - sample_30s_.time_ >= 30 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_30s_.total_) * 8 / srsu2ms(now - sample_30s_.time_));
        sample_30s_.update(bytes_, now, kbps);
    }
    if (now - sample_1m_.time_ >= 60 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_1m_.total_) * 8 / srsu2ms(now - sample_1m_.time_));
        sample_1m_.update(bytes_, now, kbps);
    }
    if (now - sample_5m_.time_ >= 300 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_5m_.total_) * 8 / srsu2ms(now - sample_5m_.time_));
        sample_5m_.update(bytes_, now, kbps);
    }
    if (now - sample_60m_.time_ >= 3600 * SRS_UTIME_SECONDS) {
        int kbps = (int)((bytes_ - sample_60m_.total_) * 8 / srsu2ms(now - sample_60m_.time_));
        sample_60m_.update(bytes_, now, kbps);
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

void SrsEphemeralDelta::remark(int64_t *in, int64_t *out)
{
    if (in)
        *in = in_;
    if (out)
        *out = out_;
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

void SrsNetworkDelta::set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out)
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

void SrsNetworkDelta::remark(int64_t *in, int64_t *out)
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

SrsKbps::SrsKbps(SrsWallClock *c)
{
    clk_ = c ? c : _srs_clock;
    is_ = new SrsKbpsSlice(clk_);
    os_ = new SrsKbpsSlice(clk_);
}

SrsKbps::~SrsKbps()
{
    srs_freep(is_);
    srs_freep(os_);
}

int SrsKbps::get_send_kbps()
{
    int duration = srsu2ms(clk_->now() - is_->starttime_);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = get_send_bytes();
    return (int)(bytes * 8 / duration);
}

int SrsKbps::get_recv_kbps()
{
    int duration = srsu2ms(clk_->now() - os_->starttime_);
    if (duration <= 0) {
        return 0;
    }

    int64_t bytes = get_recv_bytes();
    return (int)(bytes * 8 / duration);
}

int SrsKbps::get_send_kbps_30s()
{
    return os_->sample_30s_.rate_;
}

int SrsKbps::get_recv_kbps_30s()
{
    return is_->sample_30s_.rate_;
}

int SrsKbps::get_send_kbps_5m()
{
    return os_->sample_5m_.rate_;
}

int SrsKbps::get_recv_kbps_5m()
{
    return is_->sample_5m_.rate_;
}

void SrsKbps::add_delta(ISrsKbpsDelta *delta)
{
    if (!delta)
        return;

    int64_t in, out;
    delta->remark(&in, &out);
    add_delta(in, out);
}

void SrsKbps::add_delta(int64_t in, int64_t out)
{
    // update the total bytes
    is_->bytes_ += in;
    os_->bytes_ += out;

    // we donot sample, please use sample() to do resample.
}

void SrsKbps::sample()
{
    is_->sample();
    os_->sample();
}

int64_t SrsKbps::get_send_bytes()
{
    return os_->bytes_;
}

int64_t SrsKbps::get_recv_bytes()
{
    return is_->bytes_;
}

SrsNetworkKbps::SrsNetworkKbps(SrsWallClock *clock)
{
    delta_ = new SrsNetworkDelta();
    kbps_ = new SrsKbps(clock);
}

SrsNetworkKbps::~SrsNetworkKbps()
{
    srs_freep(kbps_);
    srs_freep(delta_);
}

void SrsNetworkKbps::set_io(ISrsProtocolStatistic *in, ISrsProtocolStatistic *out)
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
