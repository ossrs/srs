//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_LATEST_VERSION_HPP
#define SRS_APP_LATEST_VERSION_HPP

/*
#include <srs_app_latest_version.hpp>
*/

#include <srs_core.hpp>

#include <srs_app_st.hpp>

#include <sstream>
#include <string>

class ISrsAppFactory;

// Build features string for version query
extern void srs_build_features(std::stringstream &ss);

class SrsLatestVersion : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    std::string server_id_;
    std::string session_id_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
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

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t query_latest_version(std::string &url);
};

#endif
