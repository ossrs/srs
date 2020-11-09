/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_RTC_API_HPP
#define SRS_APP_RTC_API_HPP

#include <srs_core.hpp>

#include <srs_http_stack.hpp>

class SrsRtcServer;
class SrsRequest;
class SrsSdp;

class SrsGoApiRtcPlay : public ISrsHttpHandler
{
public:
    static uint32_t ssrc_num;
private:
    SrsRtcServer* server_;
public:
    SrsGoApiRtcPlay(SrsRtcServer* server);
    virtual ~SrsGoApiRtcPlay();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res);
    srs_error_t exchange_sdp(SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp);
    srs_error_t check_remote_sdp(const SrsSdp& remote_sdp);
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
public:
    static uint32_t ssrc_num;
private:
    SrsRtcServer* server_;
public:
    SrsGoApiRtcPublish(SrsRtcServer* server);
    virtual ~SrsGoApiRtcPublish();
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsJsonObject* res);
    srs_error_t exchange_sdp(SrsRequest* req, const SrsSdp& remote_sdp, SrsSdp& local_sdp);
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

