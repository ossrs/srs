//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SECURITY_HPP
#define SRS_APP_SECURITY_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

class SrsConfDirective;
class ISrsAppConfig;

class ISrsSecurity
{
public:
    ISrsSecurity();
    virtual ~ISrsSecurity();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req) = 0;
};

class SrsSecurity : public ISrsSecurity
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

public:
    SrsSecurity();
    virtual ~SrsSecurity();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_check(SrsConfDirective *rules, SrsRtmpConnType type, std::string ip, ISrsRequest *req);
    virtual srs_error_t allow_check(SrsConfDirective *rules, SrsRtmpConnType type, std::string ip);
    virtual srs_error_t deny_check(SrsConfDirective *rules, SrsRtmpConnType type, std::string ip);
};

class SrsEnhancedSecurity : public ISrsSecurity
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsSecurity *security_;

public:
    SrsEnhancedSecurity();
    virtual ~SrsEnhancedSecurity();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t system_check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);
};

#endif
