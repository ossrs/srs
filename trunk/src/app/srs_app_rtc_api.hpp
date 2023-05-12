//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_RTC_API_HPP
#define SRS_APP_RTC_API_HPP

#include <srs_core.hpp>

#include <srs_protocol_http_stack.hpp>

class SrsRtcServer;
class SrsRequest;
class SrsSdp;
class SrsRtcUserConfig;

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
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsRtcUserConfig* ruc);
private:
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
private:
    virtual srs_error_t http_hooks_on_play(SrsRequest* req);
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
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsRtcUserConfig* ruc);
private:
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
private:
    virtual srs_error_t http_hooks_on_publish(SrsRequest* req);
};

// See https://datatracker.ietf.org/doc/draft-ietf-wish-whip/
class SrsGoApiRtcWhip : public ISrsHttpHandler
{
private:
    SrsRtcServer* server_;
    SrsGoApiRtcPublish* publish_;
    SrsGoApiRtcPlay* play_;
public:
    SrsGoApiRtcWhip(SrsRtcServer* server);
    virtual ~SrsGoApiRtcWhip();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsRtcUserConfig* ruc);
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

