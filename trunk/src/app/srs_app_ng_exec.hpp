//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_NG_EXEC_HPP
#define SRS_APP_NG_EXEC_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_st.hpp>

class SrsRequest;
class SrsPithyPrint;
class SrsProcess;

// The ng-exec is the exec feature introduced by nginx-rtmp,
// @see https://github.com/arut/nginx-rtmp-module/wiki/Directives#exec_push
// @see https://github.com/ossrs/srs/issues/367
class SrsNgExec : public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd;
    SrsPithyPrint* pprint;
    std::string input_stream_name;
    std::vector<SrsProcess*> exec_publishs;
public:
    SrsNgExec();
    virtual ~SrsNgExec();
public:
    virtual srs_error_t on_publish(SrsRequest* req);
    virtual void on_unpublish();
// Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
private:
    virtual srs_error_t parse_exec_publish(SrsRequest* req);
    virtual void clear_exec_publish();
    virtual void show_exec_log_message();
    virtual std::string parse(SrsRequest* req, std::string tmpl);
};

#endif

