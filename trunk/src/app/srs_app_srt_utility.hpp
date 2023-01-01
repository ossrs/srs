//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_SRT_UTILITY_HPP
#define SRS_APP_SRT_UTILITY_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>

class SrsRequest;

enum SrtMode 
{
    SrtModePull = 1,
    SrtModePush = 2,
};

// Get SRT streamid info.
extern bool srs_srt_streamid_info(const std::string& streamid, SrtMode& mode, std::string& vhost, std::string& url_subpath);

// SRT streamid to request.
extern bool srs_srt_streamid_to_request(const std::string& streamid, SrtMode& mode, SrsRequest* request);

#endif

