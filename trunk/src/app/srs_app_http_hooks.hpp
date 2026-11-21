//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_HOOKS_HPP
#define SRS_APP_HTTP_HOOKS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_protocol_rtmp_stack.hpp>

class SrsHttpUri;
class SrsStSocket;
class ISrsRequest;
class SrsHttpParser;
class SrsHttpClient;
class ISrsAppFactory;
class ISrsHttpClient;
class ISrsStatistic;
class ISrsAppConfig;

// A hook that runs later from a queue, such as the on_unpublish and on_stop
// of a WebRTC client, needs this copy: by the time it is sent, the stats may
// have dropped the client and the stream, so the hook would send no stream_id.
class SrsStatisticStreamSnapshot
{
public:
    std::string id_;
    // Counts the client that stopped.
    int clients_;

public:
    SrsStatisticStreamSnapshot();
    SrsStatisticStreamSnapshot(ISrsStatistic *stat, ISrsRequest *req);
    virtual ~SrsStatisticStreamSnapshot();
};

// The param of every hook is the client's query with a leading ?, such as ?token=abc, for every
// protocol, though an SRT streamid or a WHIP URL gives SRS its query without one.
// The type of a hook is the client type that /api/v1/clients lists.
class ISrsHttpHooks
{
public:
    ISrsHttpHooks();
    virtual ~ISrsHttpHooks();

public:
    virtual srs_error_t on_publish(std::string url, ISrsRequest *req, SrsRtmpConnType type) = 0;
    virtual void on_unpublish(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream) = 0;
    virtual srs_error_t on_play(std::string url, ISrsRequest *req, SrsRtmpConnType type) = 0;
    virtual void on_stop(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream) = 0;

public:
    // Sent from a queue, so cid is the context of the source connection, and the
    // queue only logs an error. The stream is taken when the file closes.
    virtual srs_error_t on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, const SrsStatisticStreamSnapshot &stream) = 0;

public:
    // Both are sent from a queue, so cid is the context of the source connection,
    // and the queue only logs an error.
    virtual srs_error_t on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                               std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration) = 0;
    virtual srs_error_t on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify) = 0;

public:
    virtual srs_error_t discover_co_workers(std::string url, std::string &host, int &port) = 0;

public:
    virtual srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls) = 0;

    // Deprecated hooks.
public:
    virtual srs_error_t on_connect(std::string url, ISrsRequest *req) = 0;
    virtual void on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes) = 0;
};

class SrsHttpHooks : public ISrsHttpHooks
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *factory_;
    ISrsStatistic *stat_;
    ISrsAppConfig *config_;

public:
    SrsHttpHooks();
    virtual ~SrsHttpHooks();

public:
    srs_error_t on_connect(std::string url, ISrsRequest *req);
    void on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes);
    srs_error_t on_publish(std::string url, ISrsRequest *req, SrsRtmpConnType type);
    void on_unpublish(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream);
    srs_error_t on_play(std::string url, ISrsRequest *req, SrsRtmpConnType type);
    void on_stop(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream);
    srs_error_t on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, const SrsStatisticStreamSnapshot &stream);
    srs_error_t on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                       std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration);
    srs_error_t on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify);
    srs_error_t discover_co_workers(std::string url, std::string &host, int &port);
    srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_post(ISrsHttpClient *hc, std::string url, std::string req, int &code, std::string &res);
};

extern ISrsHttpHooks *_srs_hooks;

#endif
