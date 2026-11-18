//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_CIRCUIT_BREAKER_HPP
#define SRS_APP_CIRCUIT_BREAKER_HPP

#include <srs_core.hpp>

#include <srs_kernel_hourglass.hpp>

class ISrsAppConfig;
class ISrsSharedTimer;
class ISrsHost;

// CPU load levels, so callers can drop work before the overloaded server crashes.
class ISrsCircuitBreaker
{
public:
    ISrsCircuitBreaker();
    virtual ~ISrsCircuitBreaker();

public:
    virtual srs_error_t initialize() = 0;

public:
    virtual bool hybrid_high_water_level() = 0;
    virtual bool hybrid_critical_water_level() = 0;
    virtual bool hybrid_dying_water_level() = 0;
};

class SrsCircuitBreaker : public ISrsCircuitBreaker, public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsSharedTimer *shared_timer_;
    ISrsHost *host_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool enabled_;
    int high_threshold_;
    int high_pulse_;
    int critical_threshold_;
    int critical_pulse_;
    int dying_threshold_;
    int dying_pulse_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int hybrid_high_water_level_;
    int hybrid_critical_water_level_;
    int hybrid_dying_water_level_;

public:
    SrsCircuitBreaker();
    virtual ~SrsCircuitBreaker();

public:
    srs_error_t initialize();

public:
    bool hybrid_high_water_level();
    bool hybrid_critical_water_level();
    bool hybrid_dying_water_level();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

extern ISrsCircuitBreaker *_srs_circuit_breaker;

#endif
