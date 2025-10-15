//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_circuit_breaker.hpp>

#include <srs_app_config.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_utility.hpp>

#include <srs_app_statistic.hpp>
extern SrsPps *_srs_pps_snack2;
extern SrsPps *_srs_pps_snack3;
extern SrsPps *_srs_pps_snack4;

extern SrsServer *_srs_server;

using namespace std;

ISrsCircuitBreaker::ISrsCircuitBreaker()
{
}

ISrsCircuitBreaker::~ISrsCircuitBreaker()
{
}

SrsCircuitBreaker::SrsCircuitBreaker()
{
    enabled_ = false;
    high_threshold_ = 0;
    high_pulse_ = 0;
    critical_threshold_ = 0;
    critical_pulse_ = 0;
    dying_threshold_ = 0;
    dying_pulse_ = 0;

    hybrid_high_water_level_ = 0;
    hybrid_critical_water_level_ = 0;
    hybrid_dying_water_level_ = 0;

    host_ = new SrsHost();

    config_ = _srs_config;
    shared_timer_ = _srs_shared_timer;
}

SrsCircuitBreaker::~SrsCircuitBreaker()
{
    srs_freep(host_);

    config_ = NULL;
}

srs_error_t SrsCircuitBreaker::initialize()
{
    srs_error_t err = srs_success;

    enabled_ = config_->get_circuit_breaker();
    high_threshold_ = config_->get_high_threshold();
    high_pulse_ = config_->get_high_pulse();
    critical_threshold_ = config_->get_critical_threshold();
    critical_pulse_ = config_->get_critical_pulse();
    dying_threshold_ = config_->get_dying_threshold();
    dying_pulse_ = config_->get_dying_pulse();

    // Update the water level for circuit breaker.
    // @see SrsCircuitBreaker::on_timer()
    shared_timer_->timer1s()->subscribe(this);

    srs_trace("CircuitBreaker: enabled=%d, high=%dx%d, critical=%dx%d, dying=%dx%d", enabled_,
              high_pulse_, high_threshold_, critical_pulse_, critical_threshold_,
              dying_pulse_, dying_threshold_);

    return err;
}

bool SrsCircuitBreaker::hybrid_high_water_level()
{
    return enabled_ && (hybrid_critical_water_level() || hybrid_high_water_level_);
}

bool SrsCircuitBreaker::hybrid_critical_water_level()
{
    return enabled_ && (hybrid_dying_water_level() || hybrid_critical_water_level_);
}

bool SrsCircuitBreaker::hybrid_dying_water_level()
{
    return enabled_ && dying_pulse_ && hybrid_dying_water_level_ >= dying_pulse_;
}

srs_error_t SrsCircuitBreaker::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    // Update the CPU usage.
    srs_update_proc_stat();
    SrsProcSelfStat *stat = host_->self_proc_stat();

    // Reset the high water-level when CPU is low for N times.
    if (stat->percent_ * 100 > high_threshold_) {
        hybrid_high_water_level_ = high_pulse_;
    } else if (hybrid_high_water_level_ > 0) {
        hybrid_high_water_level_--;
    }

    // Reset the critical water-level when CPU is low for N times.
    if (stat->percent_ * 100 > critical_threshold_) {
        hybrid_critical_water_level_ = critical_pulse_;
    } else if (hybrid_critical_water_level_ > 0) {
        hybrid_critical_water_level_--;
    }

    // Reset the dying water-level when CPU is low for N times.
    if (stat->percent_ * 100 > dying_threshold_) {
        hybrid_dying_water_level_ = srs_min(dying_pulse_ + 1, hybrid_dying_water_level_ + 1);
    } else if (hybrid_dying_water_level_ > 0) {
        hybrid_dying_water_level_ = 0;
    }

    // Show statistics for RTC server.
    SrsProcSelfStat *u = host_->self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss_ * 4 / 1024);

    // The hybrid thread cpu and memory.
    float thread_percent = stat->percent_ * 100;

    string snk_desc;
    static char buf[128];
    if (_srs_pps_snack2->r10s()) {
        snprintf(buf, sizeof(buf), ", snk=%d,%d,%d",
                 _srs_pps_snack2->r10s(), _srs_pps_snack3->r10s(), _srs_pps_snack4->r10s() // NACK packet,seqs sent.
        );
        snk_desc = buf;
    }

    if (enabled_ && (hybrid_high_water_level() || hybrid_critical_water_level())) {
        srs_trace("CircuitBreaker: cpu=%.2f%%,%dMB, break=%d,%d,%d, cond=%.2f%%%s",
                  u->percent_ * 100, memory,
                  hybrid_high_water_level(), hybrid_critical_water_level(), hybrid_dying_water_level(), // Whether Circuit-Break is enable.
                  thread_percent,                                                                       // The conditions to enable Circuit-Breaker.
                  snk_desc.c_str());
    }

    return err;
}

ISrsCircuitBreaker *_srs_circuit_breaker = NULL;
