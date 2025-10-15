//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_COWORKERS_HPP
#define SRS_APP_COWORKERS_HPP

#include <srs_core.hpp>

#include <map>
#include <string>

class SrsJsonAny;
class ISrsRequest;
class SrsLiveSource;

// For origin cluster.
class SrsCoWorkers
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static SrsCoWorkers *instance_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<std::string, ISrsRequest *> streams_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsCoWorkers();
    virtual ~SrsCoWorkers();

public:
    static SrsCoWorkers *instance();

public:
    virtual SrsJsonAny *dumps(std::string vhost, std::string coworker, std::string app, std::string stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual ISrsRequest *find_stream_info(std::string vhost, std::string app, std::string stream);

public:
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish(ISrsRequest *r);
};

#endif
