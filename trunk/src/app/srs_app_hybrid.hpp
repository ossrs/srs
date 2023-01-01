//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_HYBRID_HPP
#define SRS_APP_HYBRID_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_hourglass.hpp>

class SrsServer;
class SrsServerAdapter;
class SrsWaitGroup;

// The hibrid server interfaces, we could register many servers.
class ISrsHybridServer
{
public:
    ISrsHybridServer();
    virtual ~ISrsHybridServer();
public:
    // Only ST initialized before each server, we could fork processes as such.
    virtual srs_error_t initialize() = 0;
    // Run each server, should never block except the SRS master server.
    virtual srs_error_t run(SrsWaitGroup* wg) = 0;
    // Stop each server, should do cleanup, for example, kill processes forked by server.
    virtual void stop() = 0;
};

// The hybrid server manager.
class SrsHybridServer : public ISrsFastTimer
{
private:
    std::vector<ISrsHybridServer*> servers;
    SrsFastTimer* timer20ms_;
    SrsFastTimer* timer100ms_;
    SrsFastTimer* timer1s_;
    SrsFastTimer* timer5s_;
    SrsClockWallMonitor* clock_monitor_;
public:
    SrsHybridServer();
    virtual ~SrsHybridServer();
public:
    virtual void register_server(ISrsHybridServer* svr);
public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();
public:
    virtual SrsServerAdapter* srs();
    SrsFastTimer* timer20ms();
    SrsFastTimer* timer100ms();
    SrsFastTimer* timer1s();
    SrsFastTimer* timer5s();
// interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);
};

extern SrsHybridServer* _srs_hybrid;

#endif
