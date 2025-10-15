//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_HOURGLASS_HPP
#define SRS_KERNEL_HOURGLASS_HPP

#include <srs_core.hpp>

#include <srs_kernel_st.hpp>

#include <map>
#include <string>
#include <vector>

class ISrsCoroutine;

// The handler for the tick.
class ISrsHourGlassHandler
{
public:
    ISrsHourGlassHandler();
    virtual ~ISrsHourGlassHandler();

public:
    // When time is ticked, this function is called.
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick) = 0;
};

// The interface for hourglass.
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

// The hourglass(timer or SrsTimer) for special tasks,
// while these tasks are attached to some intervals, for example,
// there are N=3 tasks bellow:
//          1. A heartbeat every 3s.
//          2. A print message every 5s.
//          3. A notify backend every 7s.
// The hourglass will call back when ticks:
//          1. Got notify(event=1, time=3)
//          2. Got notify(event=2, time=5)
//          3. Got notify(event=1, time=6)
//          4. Got notify(event=3, time=7)
//          5. Got notify(event=1, time=9)
//          6. Got notify(event=2, time=10)
// It's a complex and high-performance timer.
//
// Usage:
//      SrsHourGlass* hg = new SrsHourGlass("nack", handler, 100 * SRS_UTIME_MILLISECONDS);
//
//      hg->tick(1, 300 * SRS_UTIME_MILLISECONDS);
//      hg->tick(2, 500 * SRS_UTIME_MILLISECONDS);
//      hg->tick(3, 700 * SRS_UTIME_MILLISECONDS);
//
//      // The hg will create a thread for timer.
//      hg->start();
class SrsHourGlass : public ISrsCoroutineHandler, public ISrsHourGlass
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string label_;
    ISrsCoroutine *trd_;
    ISrsHourGlassHandler *handler_;
    srs_utime_t resolution_;
    ISrsTime *time_;
    // The ticks:
    //      key: the event of tick.
    //      value: the interval of tick.
    std::map<int, srs_utime_t> ticks_;
    // The total elapsed time,
    // for each cycle, we increase it with a resolution.
    srs_utime_t total_elapse_;

public:
    // TODO: FIMXE: Refine to SrsHourGlass(std::string label);
    SrsHourGlass(std::string label, ISrsHourGlassHandler *h, srs_utime_t resolution);
    virtual ~SrsHourGlass();

public:
    // Start or stop the hourglass.
    virtual srs_error_t start();
    virtual void stop();

public:
    // TODO: FIXME: Refine to tick with handler. Remove the tick(interval).
    // Add a pair of tick(event, interval).
    // @param event the event of tick, default is 0.
    // @param interval the interval in srs_utime_t of tick.
    virtual srs_error_t tick(srs_utime_t interval);
    virtual srs_error_t tick(int event, srs_utime_t interval);
    // Remove the tick by event.
    void untick(int event);

public:
    // Cycle the hourglass, which will sleep resolution every time.
    // and call handler when ticked.
    virtual srs_error_t cycle();
};

// The handler for fast timer.
class ISrsFastTimerHandler
{
public:
    ISrsFastTimerHandler();
    virtual ~ISrsFastTimerHandler();

public:
    // Tick when timer is active.
    virtual srs_error_t on_timer(srs_utime_t interval) = 0;
};

// The interface for timer.
class ISrsFastTimer
{
public:
    ISrsFastTimer();
    virtual ~ISrsFastTimer();

public:
    virtual void subscribe(ISrsFastTimerHandler *timer) = 0;
    virtual void unsubscribe(ISrsFastTimerHandler *timer) = 0;
};

// The fast timer, shared by objects, for high performance.
// For example, we should never start a timer for each connection or publisher or player,
// instead, we should start only one fast timer in server.
class SrsFastTimer : public ISrsCoroutineHandler, public ISrsFastTimer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    srs_utime_t interval_;
    std::vector<ISrsFastTimerHandler *> handlers_;
    ISrsTime *time_;

public:
    SrsFastTimer(std::string label, srs_utime_t interval);
    virtual ~SrsFastTimer();

public:
    srs_error_t start();

public:
    void subscribe(ISrsFastTimerHandler *timer);
    void unsubscribe(ISrsFastTimerHandler *timer);
    // Interface ISrsCoroutineHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Cycle the hourglass, which will sleep resolution every time.
    // and call handler when ticked.
    virtual srs_error_t
    cycle();
};

// To monitor the system wall clock timer deviation.
class SrsClockWallMonitor : public ISrsFastTimerHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTime *time_;

public:
    SrsClockWallMonitor();
    virtual ~SrsClockWallMonitor();
    // interface ISrsFastTimerHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_timer(srs_utime_t interval);
};

// The interface for shared timer.
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

// Global shared timer manager
class SrsSharedTimer : public ISrsSharedTimer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsFastTimer *timer20ms_;
    SrsFastTimer *timer100ms_;
    SrsFastTimer *timer1s_;
    SrsFastTimer *timer5s_;
    SrsClockWallMonitor *clock_monitor_;

public:
    SrsSharedTimer();
    virtual ~SrsSharedTimer();

public:
    // Initialize and start all timers
    srs_error_t initialize();

public:
    // Access to global shared timers
    ISrsFastTimer *timer20ms();
    ISrsFastTimer *timer100ms();
    ISrsFastTimer *timer1s();
    ISrsFastTimer *timer5s();
};

// Global shared timer instance
extern SrsSharedTimer *_srs_shared_timer;

#endif
