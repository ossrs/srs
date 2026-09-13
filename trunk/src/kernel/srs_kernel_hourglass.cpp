//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_hourglass.hpp>

#include <algorithm>
using namespace std;

#include <srs_core_time.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_factory.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

SrsPps *_srs_pps_timer = NULL;
SrsPps *_srs_pps_conn = NULL;
SrsPps *_srs_pps_pub = NULL;

extern SrsPps *_srs_pps_clock_15ms;
extern SrsPps *_srs_pps_clock_20ms;
extern SrsPps *_srs_pps_clock_25ms;
extern SrsPps *_srs_pps_clock_30ms;
extern SrsPps *_srs_pps_clock_35ms;
extern SrsPps *_srs_pps_clock_40ms;
extern SrsPps *_srs_pps_clock_80ms;
extern SrsPps *_srs_pps_clock_160ms;
extern SrsPps *_srs_pps_timer_s;

ISrsHourGlassHandler::ISrsHourGlassHandler()
{
}

ISrsHourGlassHandler::~ISrsHourGlassHandler()
{
}

ISrsHourGlass::ISrsHourGlass()
{
}

ISrsHourGlass::~ISrsHourGlass()
{
}

SrsHourGlass::SrsHourGlass(string label, ISrsHourGlassHandler *h, srs_utime_t resolution)
{
    label_ = label;
    handler_ = h;
    resolution_ = resolution;
    total_elapse_ = 0;
    trd_ = _srs_kernel_factory->create_coroutine("timer-" + label, this, _srs_context->get_id());
    time_ = _srs_kernel_factory->create_time();
}

SrsHourGlass::~SrsHourGlass()
{
    srs_freep(trd_);
    srs_freep(time_);
}

srs_error_t SrsHourGlass::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    return err;
}

void SrsHourGlass::stop()
{
    trd_->stop();
}

srs_error_t SrsHourGlass::tick(srs_utime_t interval)
{
    return tick(0, interval);
}

srs_error_t SrsHourGlass::tick(int event, srs_utime_t interval)
{
    srs_error_t err = srs_success;

    if (resolution_ > 0 && (interval % resolution_) != 0) {
        return srs_error_new(ERROR_SYSTEM_HOURGLASS_RESOLUTION,
                             "invalid interval=%dms, resolution=%dms", srsu2msi(interval), srsu2msi(resolution_));
    }

    ticks_[event] = interval;

    return err;
}

void SrsHourGlass::untick(int event)
{
    map<int, srs_utime_t>::iterator it = ticks_.find(event);
    if (it != ticks_.end()) {
        ticks_.erase(it);
    }
}

srs_error_t SrsHourGlass::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "quit");
        }

        map<int, srs_utime_t>::iterator it;
        for (it = ticks_.begin(); it != ticks_.end(); ++it) {
            int event = it->first;
            srs_utime_t interval = it->second;

            if (interval == 0 || (total_elapse_ % interval) == 0) {
                ++_srs_pps_timer->sugar_;

                if ((err = handler_->notify(event, interval, total_elapse_)) != srs_success) {
                    return srs_error_wrap(err, "notify");
                }
            }
        }

        // TODO: FIXME: Maybe we should use wallclock.
        total_elapse_ += resolution_;
        time_->usleep(resolution_);
    }

    return err;
}

ISrsFastTimerHandler::ISrsFastTimerHandler()
{
}

ISrsFastTimerHandler::~ISrsFastTimerHandler()
{
}

ISrsFastTimer::ISrsFastTimer()
{
}

ISrsFastTimer::~ISrsFastTimer()
{
}

SrsFastTimer::SrsFastTimer(std::string label, srs_utime_t interval)
{
    interval_ = interval;
    trd_ = _srs_kernel_factory->create_coroutine(label, this, _srs_context->get_id());
    time_ = _srs_kernel_factory->create_time();
}

SrsFastTimer::~SrsFastTimer()
{
    srs_freep(trd_);
    srs_freep(time_);
}

srs_error_t SrsFastTimer::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    return err;
}

void SrsFastTimer::subscribe(ISrsFastTimerHandler *timer)
{
    if (std::find(handlers_.begin(), handlers_.end(), timer) == handlers_.end()) {
        handlers_.push_back(timer);
    }
}

void SrsFastTimer::unsubscribe(ISrsFastTimerHandler *timer)
{
    vector<ISrsFastTimerHandler *>::iterator it = std::find(handlers_.begin(), handlers_.end(), timer);
    if (it != handlers_.end()) {
        handlers_.erase(it);
    }
}

srs_error_t SrsFastTimer::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "quit");
        }

        ++_srs_pps_timer->sugar_;

        for (int i = 0; i < (int)handlers_.size(); i++) {
            ISrsFastTimerHandler *timer = handlers_.at(i);

            if ((err = timer->on_timer(interval_)) != srs_success) {
                srs_freep(err); // Ignore any error for shared timer.
            }
        }

        time_->usleep(interval_);
    }

    return err;
}

SrsClockWallMonitor::SrsClockWallMonitor()
{
    time_ = _srs_kernel_factory->create_time();
}

SrsClockWallMonitor::~SrsClockWallMonitor()
{
    srs_freep(time_);
}

srs_error_t SrsClockWallMonitor::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    static srs_utime_t clock = 0;

    srs_utime_t now = srs_time_now_realtime();
    if (!clock) {
        clock = now;
        return err;
    }

    srs_utime_t elapsed = now - clock;
    clock = now;

    if (elapsed <= 15 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_15ms->sugar_;
    } else if (elapsed <= 21 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_20ms->sugar_;
    } else if (elapsed <= 25 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_25ms->sugar_;
    } else if (elapsed <= 30 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_30ms->sugar_;
    } else if (elapsed <= 35 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_35ms->sugar_;
    } else if (elapsed <= 40 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_40ms->sugar_;
    } else if (elapsed <= 80 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_80ms->sugar_;
    } else if (elapsed <= 160 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_160ms->sugar_;
    } else {
        ++_srs_pps_timer_s->sugar_;
    }

    return err;
}

ISrsSharedTimer::ISrsSharedTimer()
{
}

ISrsSharedTimer::~ISrsSharedTimer()
{
}

SrsSharedTimer::SrsSharedTimer()
{
    timer20ms_ = NULL;
    timer100ms_ = NULL;
    timer1s_ = NULL;
    timer5s_ = NULL;
    clock_monitor_ = NULL;
}

SrsSharedTimer::~SrsSharedTimer()
{
    srs_freep(timer20ms_);
    srs_freep(timer100ms_);
    srs_freep(timer1s_);
    srs_freep(timer5s_);
    srs_freep(clock_monitor_);
}

srs_error_t SrsSharedTimer::initialize()
{
    srs_error_t err = srs_success;

    // Initialize global shared timers
    timer20ms_ = new SrsFastTimer("shared", 20 * SRS_UTIME_MILLISECONDS);
    timer100ms_ = new SrsFastTimer("shared", 100 * SRS_UTIME_MILLISECONDS);
    timer1s_ = new SrsFastTimer("shared", 1 * SRS_UTIME_SECONDS);
    timer5s_ = new SrsFastTimer("shared", 5 * SRS_UTIME_SECONDS);
    clock_monitor_ = new SrsClockWallMonitor();

    // Start all timers
    if ((err = timer20ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer20ms");
    }

    if ((err = timer100ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer100ms");
    }

    if ((err = timer1s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer1s");
    }

    if ((err = timer5s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer5s");
    }

    // Register clock monitor to 20ms timer
    timer20ms_->subscribe(clock_monitor_);

    return err;
}

ISrsFastTimer *SrsSharedTimer::timer20ms()
{
    return timer20ms_;
}

ISrsFastTimer *SrsSharedTimer::timer100ms()
{
    return timer100ms_;
}

ISrsFastTimer *SrsSharedTimer::timer1s()
{
    return timer1s_;
}

ISrsFastTimer *SrsSharedTimer::timer5s()
{
    return timer5s_;
}

SrsSharedTimer *_srs_shared_timer = NULL;
