//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_HOURGLASS_HPP
#define SRS_KERNEL_HOURGLASS_HPP

#include <srs_core.hpp>

#include <srs_kernel_st.hpp>

#include <deque>
#include <map>
#include <string>
#include <vector>

class ISrsCoroutine;
class ISrsContext;
class ISrsKernelFactory;

class ISrsHourGlassHandler
{
public:
    ISrsHourGlassHandler();
    virtual ~ISrsHourGlassHandler();

public:
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick) = 0;
};

class ISrsHourGlass
{
public:
    ISrsHourGlass();
    virtual ~ISrsHourGlass();

public:
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
    virtual srs_error_t tick(srs_utime_t interval) = 0;
    virtual srs_error_t tick(int event, srs_utime_t interval) = 0;
    virtual void untick(int event) = 0;
};

class SrsHourGlass : public ISrsCoroutineHandler, public ISrsHourGlass
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string label_;
    ISrsCoroutine *trd_;
    ISrsHourGlassHandler *handler_;
    srs_utime_t resolution_;
    ISrsTime *time_;
    ISrsKernelFactory *factory_;
    ISrsContext *context_;
    std::map<int, srs_utime_t> ticks_;
    srs_utime_t total_elapse_;

public:
    // TODO: FIXME: Refine to SrsHourGlass(std::string label);
    SrsHourGlass(std::string label, ISrsHourGlassHandler *h, srs_utime_t resolution);
    virtual ~SrsHourGlass();

public:
    void assemble(); // Creates the members, to avoid calling functions in the constructor.

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    // TODO: FIXME: Refine to tick with handler. Remove the tick(interval).
    virtual srs_error_t tick(srs_utime_t interval);
    virtual srs_error_t tick(int event, srs_utime_t interval);
    void untick(int event);

public:
    virtual srs_error_t cycle();
};

class ISrsFastTimerHandler
{
public:
    ISrsFastTimerHandler();
    virtual ~ISrsFastTimerHandler();

public:
    virtual srs_error_t on_timer(srs_utime_t interval) = 0;
};

class ISrsFastTimer
{
public:
    ISrsFastTimer();
    virtual ~ISrsFastTimer();

public:
    virtual srs_error_t start() = 0;
    virtual void subscribe(ISrsFastTimerHandler *timer) = 0;
    virtual void unsubscribe(ISrsFastTimerHandler *timer) = 0;
};

// Shared by many objects for performance: the server starts one timer, not one per connection,
// publisher or player.
class SrsFastTimer : public ISrsCoroutineHandler, public ISrsFastTimer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    srs_utime_t interval_;
    std::string label_;
    std::vector<ISrsFastTimerHandler *> handlers_;
    std::deque<ISrsFastTimerHandler *> pending_;
    ISrsTime *time_;
    ISrsKernelFactory *factory_;
    ISrsContext *context_;

public:
    SrsFastTimer(std::string label, srs_utime_t interval);
    virtual ~SrsFastTimer();

public:
    void assemble(); // Creates the members, to avoid calling functions in the constructor.

public:
    srs_error_t start();

public:
    void subscribe(ISrsFastTimerHandler *timer);
    void unsubscribe(ISrsFastTimerHandler *timer);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t cycle();
};

// Monitors the timer deviation by the system wall clock.
class SrsClockWallMonitor : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTime *time_;
    ISrsKernelFactory *factory_;

public:
    SrsClockWallMonitor();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsClockWallMonitor();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

class ISrsSharedTimer
{
public:
    ISrsSharedTimer();
    virtual ~ISrsSharedTimer();

public:
    virtual ISrsFastTimer *timer20ms() = 0;
    virtual ISrsFastTimer *timer100ms() = 0;
    virtual ISrsFastTimer *timer1s() = 0;
    virtual ISrsFastTimer *timer5s() = 0;
};

class SrsSharedTimer : public ISrsSharedTimer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFastTimer *timer20ms_;
    ISrsFastTimer *timer100ms_;
    ISrsFastTimer *timer1s_;
    ISrsFastTimer *timer5s_;
    ISrsFastTimerHandler *clock_monitor_;
    ISrsKernelFactory *factory_;

public:
    SrsSharedTimer();
    virtual ~SrsSharedTimer();

public:
    srs_error_t initialize();

public:
    ISrsFastTimer *timer20ms();
    ISrsFastTimer *timer100ms();
    ISrsFastTimer *timer1s();
    ISrsFastTimer *timer5s();
};

extern SrsSharedTimer *_srs_shared_timer;

#endif
