//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HEARTBEAT_HPP
#define SRS_APP_HEARTBEAT_HPP

#include <srs_core.hpp>

class ISrsAppConfig;
class ISrsAppFactory;
class ISrsStatistic;

// The interface of the http heartbeat.
class ISrsHttpHeartbeat
{
public:
    ISrsHttpHeartbeat();
    virtual ~ISrsHttpHeartbeat();

public:
    virtual void heartbeat() = 0;
};

// The http heartbeat to api-server to notice api that the information of SRS.
class SrsHttpHeartbeat : public ISrsHttpHeartbeat
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;
    ISrsStatistic *stat_;

public:
    SrsHttpHeartbeat();
    virtual ~SrsHttpHeartbeat();

public:
    virtual void heartbeat();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_heartbeat();
};

#endif
