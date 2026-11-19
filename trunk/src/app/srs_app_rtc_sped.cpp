//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_sped.hpp>

#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>

#include <algorithm>

using namespace std;

// RFC 9443 demultiplexes a datagram whose first byte is 20 to 63 as DTLS.
static bool srs_sped_is_dtls(const char *data, int size)
{
    if (size <= 0) {
        return false;
    }

    uint8_t first = (uint8_t)data[0];
    return first >= 20 && first <= 63;
}

// A handshake on a LAN takes a few milliseconds, finer than the cached time, so SPED reads
// the real time, twice per session.
class SrsSpedRealtimeClock : public ISrsClock
{
public:
    virtual srs_utime_t now()
    {
        return srs_time_now_realtime();
    }
};

static SrsSpedRealtimeClock _srs_sped_clock;

const char *srs_sped_state_str(SrsSpedState state)
{
    switch (state) {
    case SrsSpedStateTentative:
        return "tentative";
    case SrsSpedStateConfirmed:
        return "confirmed";
    case SrsSpedStatePending:
        return "pending";
    case SrsSpedStateComplete:
        return "complete";
    case SrsSpedStateOff:
        return "off";
    default:
        return "unknown";
    }
}

ISrsSpedHandler::ISrsSpedHandler()
{
}

ISrsSpedHandler::~ISrsSpedHandler()
{
}

SrsSpedController::SrsSpedController()
{
    state_ = SrsSpedStateTentative;
    next_ = 0;
    flight_done_ = false;
    wait_for_peer_ = false;
    handler_ = NULL;

    clk_ = &_srs_sped_clock;
    nn_embedded_sent_ = 0;
    nn_embedded_recv_ = 0;
    nn_raw_recv_ = 0;
    first_stun_at_ = 0;
    handshake_at_ = 0;
}

SrsSpedController::~SrsSpedController()
{
    clk_ = NULL;
    handler_ = NULL;
}

SrsSpedState SrsSpedController::state()
{
    return state_;
}

void SrsSpedController::set_handler(ISrsSpedHandler *handler)
{
    handler_ = handler;
}

void SrsSpedController::capture(const char *data, int size)
{
    if (!is_active() || !srs_sped_is_dtls(data, size)) {
        return;
    }

    if (flight_done_) {
        clear_flight();
    }

    flight_.push_back(string(data, size));
    flight_crcs_.push_back(srs_crc32_ieee(data, size));
}

void SrsSpedController::capture_retransmission(const char *data, int size)
{
    if (flight_done_ && !flight_.empty()) {
        return;
    }

    capture(data, size);
}

void SrsSpedController::flush()
{
    flight_done_ = true;
}

bool SrsSpedController::data_to_embed(SrsStunMessageType type, std::string &data)
{
    if (!is_active() || !is_binding(type) || flight_.empty()) {
        return false;
    }

    next_ %= flight_.size();
    data = flight_[next_];
    next_ = (next_ + 1) % flight_.size();
    return true;
}

void SrsSpedController::on_data_embedded()
{
    if (!is_active()) {
        return;
    }

    nn_embedded_sent_++;
}

bool SrsSpedController::acks_to_embed(SrsStunMessageType type, std::vector<uint32_t> &acks)
{
    if (!is_active() || !is_binding(type)) {
        return false;
    }

    acks = acks_;
    return true;
}

bool SrsSpedController::on_stun_received(bool has_data, const std::string &data, bool has_acks, const std::vector<uint32_t> &acks)
{
    if (!is_active()) {
        return false;
    }

    if (!first_stun_at_) {
        first_stun_at_ = clk_->now();
    }

    if (!has_data && !has_acks) {
        if (state_ == SrsSpedStateTentative) {
            stop(SrsSpedStateOff);
        } else if (state_ == SrsSpedStatePending) {
            stop(SrsSpedStateComplete);
        }
        return false;
    }

    if (state_ == SrsSpedStateTentative) {
        state_ = SrsSpedStateConfirmed;
    }

    if (!flight_.empty()) {
        prune(acks);
        if (flight_.empty() && state_ == SrsSpedStatePending) {
            stop(SrsSpedStateComplete);
            return false;
        }
    }

    if (!has_data || !srs_sped_is_dtls(data.data(), (int)data.size())) {
        return false;
    }

    nn_embedded_recv_++;
    ack(data.data(), (int)data.size());
    return true;
}

void SrsSpedController::on_dtls_received(const char *data, int size)
{
    if (!is_active() || !srs_sped_is_dtls(data, size)) {
        return;
    }

    nn_raw_recv_++;
    ack(data, size);
}

void SrsSpedController::on_handshake_done(bool clear_flight, bool wait_for_peer)
{
    if (!is_active()) {
        return;
    }

    if (clear_flight) {
        this->clear_flight();
    }

    handshake_at_ = clk_->now();
    state_ = SrsSpedStatePending;
    wait_for_peer_ = wait_for_peer;
}

void SrsSpedController::on_srtp_received()
{
    if (state_ == SrsSpedStatePending) {
        stop(SrsSpedStateComplete);
    }
}

srs_utime_t SrsSpedController::handshake_cost()
{
    if (!first_stun_at_ || !handshake_at_) {
        return -1;
    }

    return handshake_at_ - first_stun_at_;
}

void SrsSpedController::dumps(SrsJsonObject *obj)
{
    srs_utime_t cost = handshake_cost();

    obj->set("state", SrsJsonAny::str(srs_sped_state_str(state_)));
    obj->set("embedded_sent", SrsJsonAny::integer(nn_embedded_sent_));
    obj->set("embedded_recv", SrsJsonAny::integer(nn_embedded_recv_));
    obj->set("raw_recv", SrsJsonAny::integer(nn_raw_recv_));
    obj->set("handshake_ms", SrsJsonAny::integer(cost < 0 ? -1 : srsu2ms(cost)));
}

bool SrsSpedController::is_active()
{
    return state_ != SrsSpedStateComplete && state_ != SrsSpedStateOff;
}

bool SrsSpedController::waits_for_peer()
{
    return state_ == SrsSpedStatePending && wait_for_peer_;
}

bool SrsSpedController::is_binding(SrsStunMessageType type)
{
    return type == BindingRequest || type == BindingResponse;
}

void SrsSpedController::prune(const std::vector<uint32_t> &acks)
{
    for (size_t i = 0; i < flight_.size();) {
        uint32_t crc = flight_crcs_[i];
        if (std::find(acks.begin(), acks.end(), crc) == acks.end()) {
            i++;
            continue;
        }

        flight_.erase(flight_.begin() + i);
        flight_crcs_.erase(flight_crcs_.begin() + i);
        if (next_ > i) {
            next_--;
        }
    }
}

void SrsSpedController::ack(const char *data, int size)
{
    uint32_t crc = srs_crc32_ieee(data, size);
    if (std::find(acks_.begin(), acks_.end(), crc) != acks_.end()) {
        return;
    }

    if ((int)acks_.size() >= kStunDtlsInStunMaxAcks) {
        acks_.erase(acks_.begin());
    }
    acks_.push_back(crc);
}

void SrsSpedController::clear_flight()
{
    flight_.clear();
    flight_crcs_.clear();
    next_ = 0;
    flight_done_ = false;
}

void SrsSpedController::stop(SrsSpedState state)
{
    state_ = state;
    clear_flight();
    acks_.clear();

    srs_utime_t cost = handshake_cost();
    srs_trace("RTC: SPED %s, embedded sent=%d, embedded recv=%d, raw recv=%d, handshake=%dms",
              srs_sped_state_str(state_), nn_embedded_sent_, nn_embedded_recv_, nn_raw_recv_, cost < 0 ? -1 : srsu2msi(cost));

    if (state_ == SrsSpedStateComplete && handler_) {
        handler_->on_sped_complete();
    }
}

ISrsSpedSource::ISrsSpedSource()
{
}

ISrsSpedSource::~ISrsSpedSource()
{
}
