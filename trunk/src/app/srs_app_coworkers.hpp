//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsAppConfig;

// The origin cluster coworkers, which record the published streams and dump the origin of a stream.
class ISrsCoWorkers
{
public:
    ISrsCoWorkers();
    virtual ~ISrsCoWorkers();

public:
    virtual SrsJsonAny *dumps(std::string vhost, std::string coworker, std::string app, std::string stream) = 0;
    virtual srs_error_t on_publish(ISrsRequest *r) = 0;
    virtual void on_unpublish(ISrsRequest *r) = 0;
};

// For origin cluster.
class SrsCoWorkers : public ISrsCoWorkers
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    static SrsCoWorkers *instance_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
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

// The global coworkers, the SrsCoWorkers::instance() singleton, set by srs_global_initialize().
extern SrsCoWorkers *_srs_coworkers;

#endif
