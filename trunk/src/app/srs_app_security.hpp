//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SECURITY_HPP
#define SRS_APP_SECURITY_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_rtmp_stack.hpp>

class SrsConfDirective;

// The security apply on vhost.
class SrsSecurity
{
public:
    SrsSecurity();
    virtual ~SrsSecurity();
public:
    // Security check the client apply by vhost security strategy
    // @param type the client type, publish or play.
    // @param ip the ip address of client.
    // @param req the request object of client.
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, SrsRequest* req);
private:
    virtual srs_error_t do_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip, SrsRequest* req);
    virtual srs_error_t allow_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip);
    virtual srs_error_t deny_check(SrsConfDirective* rules, SrsRtmpConnType type, std::string ip);
};

#endif

