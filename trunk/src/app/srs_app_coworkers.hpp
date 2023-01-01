//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_COWORKERS_HPP
#define SRS_APP_COWORKERS_HPP

#include <srs_core.hpp>

#include <string>
#include <map>

class SrsJsonAny;
class SrsRequest;
class SrsLiveSource;

// For origin cluster.
class SrsCoWorkers
{
private:
    static SrsCoWorkers* _instance;
private:
    std::map<std::string, SrsRequest*> streams;
private:
    SrsCoWorkers();
    virtual ~SrsCoWorkers();
public:
    static SrsCoWorkers* instance();
public:
    virtual SrsJsonAny* dumps(std::string vhost, std::string coworker, std::string app, std::string stream);
private:
    virtual SrsRequest* find_stream_info(std::string vhost, std::string app, std::string stream);
public:
    virtual srs_error_t on_publish(SrsLiveSource* s, SrsRequest* r);
    virtual void on_unpublish(SrsLiveSource* s, SrsRequest* r);
};

#endif
