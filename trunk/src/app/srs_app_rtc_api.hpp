//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_API_HPP
#define SRS_APP_RTC_API_HPP

#include <srs_core.hpp>

#include <srs_http_stack.hpp>

class SrsRtcServer;
class SrsRequest;
class SrsSdp;

class SrsGoApiRtcPlay : public ISrsHttpHandler
{
private:
    SrsRtcServer* server_;
public:
    SrsGoApiRtcPlay(SrsRtcServer* server);
    virtual ~SrsGoApiRtcPlay();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res);
};

class SrsGoApiRtcPublish : public ISrsHttpHandler
{
private:
    SrsRtcServer* server_;
public:
    SrsGoApiRtcPublish(SrsRtcServer* server);
    virtual ~SrsGoApiRtcPublish();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res);
};

class SrsGoApiRtcNACK : public ISrsHttpHandler
{
private:
    SrsRtcServer* server_;
public:
    SrsGoApiRtcNACK(SrsRtcServer* server);
    virtual ~SrsGoApiRtcNACK();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res);
};

#endif

