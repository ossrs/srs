//
// Copyright (c) 2013-2020 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_THREADS_HPP
#define SRS_APP_THREADS_HPP

#include <srs_core.hpp>

#include <srs_app_hourglass.hpp>

// Protect server in high load.
class SrsCircuitBreaker : public ISrsFastTimer
{
private:
    // The config for high/critical water level.
    bool enabled_;
    int high_threshold_;
    int high_pulse_;
    int critical_threshold_;
    int critical_pulse_;
    int dying_threshold_;
    int dying_pulse_;
private:
    // Reset the water-level when CPU is low for N times.
    // @note To avoid the CPU change rapidly.
    int hybrid_high_water_level_;
    int hybrid_critical_water_level_;
    int hybrid_dying_water_level_;
public:
    SrsCircuitBreaker();
    virtual ~SrsCircuitBreaker();
public:
    srs_error_t initialize();
public:
    // Whether hybrid server water-level is high.
    bool hybrid_high_water_level();
    bool hybrid_critical_water_level();
    bool hybrid_dying_water_level();
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

extern SrsCircuitBreaker* _srs_circuit_breaker;

// Initialize global or thread-local variables.
extern srs_error_t srs_thread_initialize();

#endif

