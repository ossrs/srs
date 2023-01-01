//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_LATEST_VERSION_HPP
#define SRS_APP_LATEST_VERSION_HPP

/*
#include <srs_app_latest_version.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>

#include <string>

class SrsLatestVersion : public ISrsCoroutineHandler
{
private:
    SrsCoroutine* trd_;
    std::string server_id_;
    std::string session_id_;
private:
    std::string match_version_;
    std::string stable_version_;
public:
    SrsLatestVersion();
    virtual ~SrsLatestVersion();
public:
    virtual srs_error_t start();
// interface ISrsEndlessThreadHandler.
public:
    virtual srs_error_t cycle();
private:
    srs_error_t query_latest_version(std::string& url);
};

#endif

