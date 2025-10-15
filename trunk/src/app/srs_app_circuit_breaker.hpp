//
// Copyright (c) 2013-2025 The SRS Authors
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

// Interface for circuit breaker functionality to protect server in high load conditions.
// The circuit breaker monitors CPU usage and enables different levels of protection:
// - High water level: Disables some unnecessary features to reduce CPU load
// - Critical water level: Disables more features to high-level protections
// - Dying water level: Disables even normal features should be disabled to prevent server crash
class ISrsCircuitBreaker
{
public:
    ISrsCircuitBreaker();
    virtual ~ISrsCircuitBreaker();

public:
    // Initialize the circuit breaker with configuration settings.
    // @return srs_success on success, error code otherwise.
    virtual srs_error_t initialize() = 0;

public:
    // Check if server is in high water level state.
    // When true, some unnecessary features should be disabled to reduce CPU load.
    // @return true if high water level is active, false otherwise.
    virtual bool hybrid_high_water_level() = 0;

    // Check if server is in critical water level state.
    // When true, more features should be disabled to reduce CPU load.
    // This includes all protections from high water level.
    // @return true if critical water level is active, false otherwise.
    virtual bool hybrid_critical_water_level() = 0;

    // Check if server is in dying water level state.
    // When true, even normal features should be disabled to prevent server crash.
    // This is the most severe protection level.
    // @return true if dying water level is active, false otherwise.
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
