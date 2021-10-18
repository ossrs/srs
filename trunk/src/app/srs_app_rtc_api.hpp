//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_API_HPP
#define SRS_APP_RTC_API_HPP

#include <srs_core.hpp>
#include <srs_app_security.hpp>
#include <srs_app_refer.hpp>
#include <srs_http_stack.hpp>

class SrsRtcServer;
class SrsRequest;
class SrsSdp;

class SrsRtcAccessControl
{
private:
    SrsSecurity* security;
    SrsRefer* refer;
public:
    SrsRtcAccessControl();
    virtual ~SrsRtcAccessControl();
protected:
    virtual srs_error_t http_hooks_on_play(SrsRequest* req);
    virtual srs_error_t http_hooks_on_publish(SrsRequest* req);
protected:
    virtual srs_error_t security_check(SrsRtmpConnType type, std::string ip, SrsRequest* req);
protected:
    virtual srs_error_t refer_check_play(SrsRequest* req);
    virtual srs_error_t refer_check_publish(SrsRequest* req);

};

class SrsGoApiRtcPlay : public ISrsHttpHandler, public SrsRtcAccessControl
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
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
};

class SrsGoApiRtcPublish : public ISrsHttpHandler, public SrsRtcAccessControl
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
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
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

