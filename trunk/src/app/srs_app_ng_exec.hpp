//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_NG_EXEC_HPP
#define SRS_APP_NG_EXEC_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_st.hpp>

class ISrsRequest;
class SrsPithyPrint;
class SrsProcess;
class ISrsAppConfig;

// The ng-exec interface.
class ISrsNgExec
{
public:
    ISrsNgExec();
    virtual ~ISrsNgExec();

public:
    virtual srs_error_t on_publish(ISrsRequest *req) = 0;
    virtual void on_unpublish() = 0;
    virtual srs_error_t cycle() = 0;
};

// The ng-exec is the exec feature introduced by nginx-rtmp,
// @see https://github.com/arut/nginx-rtmp-module/wiki/Directives#exec_push
// @see https://github.com/ossrs/srs/issues/367
class SrsNgExec : public ISrsCoroutineHandler, public ISrsNgExec
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    SrsPithyPrint *pprint_;
    std::string input_stream_name_;
    std::vector<SrsProcess *> exec_publishs_;

public:
    SrsNgExec();
    virtual ~SrsNgExec();

public:
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual void on_unpublish();
    // Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t parse_exec_publish(ISrsRequest *req);
    virtual void clear_exec_publish();
    virtual void show_exec_log_message();
    virtual std::string parse(ISrsRequest *req, std::string tmpl);
};

#endif
