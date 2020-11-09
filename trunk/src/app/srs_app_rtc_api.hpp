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
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
private:
    virtual srs_error_t http_hooks_on_play(SrsRequest* req);
};

#ifdef SRS_SCTP
class SrsGoApiRtcDataChannel : public ISrsHttpHandler
{
private:
    SrsRtcServer* server_;
public:
    SrsGoApiRtcDataChannel(SrsRtcServer* server);
    virtual ~SrsGoApiRtcDataChannel();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res);
    srs_error_t exchange_sdp(SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp);
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
};
#endif

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
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
private:
    virtual srs_error_t http_hooks_on_publish(SrsRequest* req);
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

