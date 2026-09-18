//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_API_HPP
#define SRS_APP_RTC_API_HPP

#include <srs_app_security.hpp>
#include <srs_core.hpp>
#include <srs_protocol_http_stack.hpp>

class SrsServer;
class ISrsRequest;
class SrsSdp;
class SrsRtcUserConfig;
class ISrsRtcApiServer;
class ISrsSecurity;
class ISrsAppConfig;
class ISrsStatistic;
class ISrsRtcSourceManager;
class ISrsLiveSourceManager;
class ISrsHttpHooks;

class SrsGoApiRtcPlay : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsRtcSourceManager *rtc_sources_;
    ISrsLiveSourceManager *live_sources_;
    ISrsHttpHooks *hooks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcApiServer *server_;
    ISrsSecurity *security_;

public:
    SrsGoApiRtcPlay(ISrsRtcApiServer *server);
    virtual ~SrsGoApiRtcPlay();

public:
    // @deprecated The JSON play API, which answers a JSON body carrying an SDP. Use the standard
    // WHEP API served by SrsGoApiRtcWhip instead. Kept only for existing clients; do not extend it,
    // and add new playback behavior to the WHEP path.
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // @deprecated Body of the JSON play API above. See the note on serve_http().
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsJsonObject *res);

public:
    // The shared negotiation used by both the deprecated JSON API and the standard WHEP API, which
    // reaches it through SrsGoApiRtcWhip. This overload is not deprecated.
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t check_remote_sdp(const SrsSdp &remote_sdp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t http_hooks_on_play(ISrsRequest *req);
};

class SrsGoApiRtcPublish : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    ISrsHttpHooks *hooks_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcApiServer *server_;
    ISrsSecurity *security_;

public:
    SrsGoApiRtcPublish(ISrsRtcApiServer *server);
    virtual ~SrsGoApiRtcPublish();

public:
    // @deprecated The JSON publish API, which answers a JSON body carrying an SDP. Use the standard
    // WHIP API served by SrsGoApiRtcWhip instead. Kept only for existing clients; do not extend it,
    // and add new publish behavior to the WHIP path.
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // @deprecated Body of the JSON publish API above. See the note on serve_http().
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsJsonObject *res);

public:
    // The shared negotiation used by both the deprecated JSON API and the standard WHIP API, which
    // reaches it through SrsGoApiRtcWhip. This overload is not deprecated.
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t check_remote_sdp(const SrsSdp &remote_sdp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t http_hooks_on_publish(ISrsRequest *req);
};

// See https://datatracker.ietf.org/doc/draft-ietf-wish-whip/
class SrsGoApiRtcWhip : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcApiServer *server_;
    SrsGoApiRtcPublish *publish_;
    SrsGoApiRtcPlay *play_;

public:
    SrsGoApiRtcWhip(ISrsRtcApiServer *server);
    virtual ~SrsGoApiRtcWhip();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual srs_error_t do_serve_http_with(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc);
};

class SrsGoApiRtcNACK : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtcApiServer *server_;

public:
    SrsGoApiRtcNACK(ISrsRtcApiServer *server);
    virtual ~SrsGoApiRtcNACK();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsJsonObject *res);
};

#endif
