//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_HEARTBEAT_HPP
#define SRS_APP_HEARTBEAT_HPP

#include <srs_core.hpp>

// The http heartbeat to api-server to notice api that the information of SRS.
class SrsHttpHeartbeat
{
public:
    SrsHttpHeartbeat();
    virtual ~SrsHttpHeartbeat();
public:
    virtual void heartbeat();
private:
    virtual srs_error_t do_heartbeat();
};

#endif

