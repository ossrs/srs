//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HEARTBEAT_HPP
#define SRS_APP_HEARTBEAT_HPP

#include <srs_core.hpp>

class ISrsAppConfig;
class ISrsAppFactory;

// The http heartbeat to api-server to notice api that the information of SRS.
class SrsHttpHeartbeat
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

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
