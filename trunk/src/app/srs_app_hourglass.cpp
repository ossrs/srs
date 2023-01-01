//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_hourglass.hpp>

#include <algorithm>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

#include <srs_protocol_kbps.hpp>

SrsPps* _srs_pps_timer = NULL;
SrsPps* _srs_pps_conn = NULL;
SrsPps* _srs_pps_pub = NULL;

extern SrsPps* _srs_pps_clock_15ms;
extern SrsPps* _srs_pps_clock_20ms;
extern SrsPps* _srs_pps_clock_25ms;
extern SrsPps* _srs_pps_clock_30ms;
extern SrsPps* _srs_pps_clock_35ms;
extern SrsPps* _srs_pps_clock_40ms;
extern SrsPps* _srs_pps_clock_80ms;
extern SrsPps* _srs_pps_clock_160ms;
extern SrsPps* _srs_pps_timer_s;

ISrsHourGlass::ISrsHourGlass()
{
}

ISrsHourGlass::~ISrsHourGlass()
{
}

SrsHourGlass::SrsHourGlass(string label, ISrsHourGlass* h, srs_utime_t resolution)
{
    label_ = label;
    handler = h;
    _resolution = resolution;
    total_elapse = 0;
    trd = new SrsSTCoroutine("timer-" + label, this, _srs_context->get_id());
}

SrsHourGlass::~SrsHourGlass()
{
    srs_freep(trd);
}

srs_error_t SrsHourGlass::start()
{
    srs_error_t err = srs_success;

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    return err;
}

void SrsHourGlass::stop()
{
    trd->stop();
}

srs_error_t SrsHourGlass::tick(srs_utime_t interval)
{
    return tick(0, interval);
}

srs_error_t SrsHourGlass::tick(int event, srs_utime_t interval)
{
    srs_error_t err = srs_success;
    
    if (_resolution > 0 && (interval % _resolution) != 0) {
        return srs_error_new(ERROR_SYSTEM_HOURGLASS_RESOLUTION,
            "invalid interval=%dms, resolution=%dms", srsu2msi(interval), srsu2msi(_resolution));
    }
    
    ticks[event] = interval;
    
    return err;
}

void SrsHourGlass::untick(int event)
{
    map<int, srs_utime_t>::iterator it = ticks.find(event);
    if (it != ticks.end()) {
        ticks.erase(it);
    }
}

srs_error_t SrsHourGlass::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "quit");
        }
    
        map<int, srs_utime_t>::iterator it;
        for (it = ticks.begin(); it != ticks.end(); ++it) {
            int event = it->first;
            srs_utime_t interval = it->second;

            if (interval == 0 || (total_elapse % interval) == 0) {
                ++_srs_pps_timer->sugar;

                if ((err = handler->notify(event, interval, total_elapse)) != srs_success) {
                    return srs_error_wrap(err, "notify");
                }
            }
        }

        // TODO: FIXME: Maybe we should use wallclock.
        total_elapse += _resolution;
        srs_usleep(_resolution);
    }
    
    return err;
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
    trd_ = new SrsSTCoroutine(label, this, _srs_context->get_id());
}

SrsFastTimer::~SrsFastTimer()
{
    srs_freep(trd_);
}

srs_error_t SrsFastTimer::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    return err;
}

void SrsFastTimer::subscribe(ISrsFastTimer* timer)
{
    if (std::find(handlers_.begin(), handlers_.end(), timer) == handlers_.end()) {
        handlers_.push_back(timer);
    }
}

void SrsFastTimer::unsubscribe(ISrsFastTimer* timer)
{
    vector<ISrsFastTimer*>::iterator it = std::find(handlers_.begin(), handlers_.end(), timer);
    if (it != handlers_.end()) {
        it = handlers_.erase(it);
    }
}

srs_error_t SrsFastTimer::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "quit");
        }

        ++_srs_pps_timer->sugar;

        for (int i = 0; i < (int)handlers_.size(); i++) {
            ISrsFastTimer* timer = handlers_.at(i);

            if ((err = timer->on_timer(interval_)) != srs_success) {
                srs_freep(err); // Ignore any error for shared timer.
            }
        }

        srs_usleep(interval_);
    }

    return err;
}

SrsClockWallMonitor::SrsClockWallMonitor()
{
}

SrsClockWallMonitor::~SrsClockWallMonitor()
{
}

srs_error_t SrsClockWallMonitor::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    static srs_utime_t clock = 0;

    srs_utime_t now = srs_update_system_time();
    if (!clock) {
        clock = now;
        return err;
    }

    srs_utime_t elapsed = now - clock;
    clock = now;

    if (elapsed <= 15 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_15ms->sugar;
    } else if (elapsed <= 21 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_20ms->sugar;
    } else if (elapsed <= 25 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_25ms->sugar;
    } else if (elapsed <= 30 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_30ms->sugar;
    } else if (elapsed <= 35 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_35ms->sugar;
    } else if (elapsed <= 40 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_40ms->sugar;
    } else if (elapsed <= 80 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_80ms->sugar;
    } else if (elapsed <= 160 * SRS_UTIME_MILLISECONDS) {
        ++_srs_pps_clock_160ms->sugar;
    } else {
        ++_srs_pps_timer_s->sugar;
    }

    return err;
}

