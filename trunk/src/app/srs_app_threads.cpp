/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_app_threads.hpp>

#include <srs_app_config.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>

#include <string>
using namespace std;

#include <srs_protocol_kbps.hpp>

extern SrsPps* _srs_pps_snack2;
extern SrsPps* _srs_pps_snack3;
extern SrsPps* _srs_pps_snack4;

SrsPps* _srs_pps_aloss2 = new SrsPps();

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
}

SrsCircuitBreaker::~SrsCircuitBreaker()
{
}

srs_error_t SrsCircuitBreaker::initialize()
{
    srs_error_t err = srs_success;

    enabled_ = _srs_config->get_circuit_breaker();
    high_threshold_ = _srs_config->get_high_threshold();
    high_pulse_ = _srs_config->get_high_pulse();
    critical_threshold_ = _srs_config->get_critical_threshold();
    critical_pulse_ = _srs_config->get_critical_pulse();
    dying_threshold_ = _srs_config->get_dying_threshold();
    dying_pulse_ = _srs_config->get_dying_pulse();

    // Update the water level for circuit breaker.
    // @see SrsCircuitBreaker::on_timer()
    _srs_hybrid->timer1s()->subscribe(this);

    srs_trace("CircuitBreaker: enabled=%d, high=%dx%d, critical=%dx%d, dying=%dx%d", enabled_,
        high_pulse_, high_threshold_, critical_pulse_, critical_threshold_,
        dying_pulse_, dying_threshold_);

    return err;
}

bool SrsCircuitBreaker::hybrid_high_water_level()
{
    return enabled_ && hybrid_critical_water_level() || hybrid_high_water_level_;
}

bool SrsCircuitBreaker::hybrid_critical_water_level()
{
    return enabled_ && hybrid_dying_water_level() || hybrid_critical_water_level_;
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
    SrsProcSelfStat* stat = srs_get_self_proc_stat();

    // Reset the high water-level when CPU is low for N times.
    if (stat->percent * 100 > high_threshold_) {
        hybrid_high_water_level_ = high_pulse_;
    } else if (hybrid_high_water_level_ > 0) {
        hybrid_high_water_level_--;
    }

    // Reset the critical water-level when CPU is low for N times.
    if (stat->percent * 100 > critical_threshold_) {
        hybrid_critical_water_level_ = critical_pulse_;
    } else if (hybrid_critical_water_level_ > 0) {
        hybrid_critical_water_level_--;
    }

    // Reset the dying water-level when CPU is low for N times.
    if (stat->percent * 100 > dying_threshold_) {
        hybrid_dying_water_level_ = srs_min(dying_pulse_ + 1, hybrid_dying_water_level_ + 1);
    } else if (hybrid_dying_water_level_ > 0) {
        hybrid_dying_water_level_ = 0;
    }

    // Show statistics for RTC server.
    SrsProcSelfStat* u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    // The hybrid thread cpu and memory.
    float thread_percent = stat->percent * 100;

    if (enabled_ && hybrid_high_water_level() || hybrid_critical_water_level() || _srs_pps_snack2->r10s()) {
        srs_trace("CircuitBreaker: cpu=%.2f%%,%dMB, break=%d,%d,%d, cond=%.2f%%, snk=%d,%d,%d",
            u->percent * 100, memory,
            hybrid_high_water_level(), hybrid_critical_water_level(), hybrid_dying_water_level(), // Whether Circuit-Break is enable.
            thread_percent, // The conditions to enable Circuit-Breaker.
            _srs_pps_snack2->r10s(), _srs_pps_snack3->r10s(), _srs_pps_snack4->r10s() // NACK packet,seqs sent.
        );
    }

    return err;
}

SrsCircuitBreaker* _srs_circuit_breaker = new SrsCircuitBreaker();

