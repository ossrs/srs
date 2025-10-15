//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_HOOKS_HPP
#define SRS_APP_HTTP_HOOKS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsHttpUri;
class SrsStSocket;
class ISrsRequest;
class SrsHttpParser;
class SrsHttpClient;
class ISrsAppFactory;
class ISrsHttpClient;
class ISrsStatistic;
class ISrsAppConfig;

// HTTP hooks interface for SRS server event callbacks.
//
// This interface defines the contract for handling various server events
// through HTTP callback APIs. Implementations can send HTTP requests to
// external services to notify them of events like client connections,
// stream publishing/playing, DVR recording, HLS segment generation, etc.
//
// All hook methods that return srs_error_t are used for validation and
// can reject the operation by returning an error. Methods that return void
// are notification-only and cannot prevent the operation.
class ISrsHttpHooks
{
public:
    ISrsHttpHooks();
    virtual ~ISrsHttpHooks();

public:
    // Stream start publishing validation hook.
    // Called when a client (encoder) attempts to start publishing a stream.
    // This hook can be used to authorize stream publishing or apply publishing policies.
    // @param url The HTTP callback URL for publish validation. If empty, hook is ignored.
    // @param req The publish request information including stream details.
    // @return srs_success if publishing is allowed, error otherwise to reject publishing.
    virtual srs_error_t on_publish(std::string url, ISrsRequest *req) = 0;

    // Stream stop publishing notification hook.
    // Called when a client (encoder) stops publishing a stream.
    // This is a notification-only hook that cannot prevent the unpublishing.
    // @param url The HTTP callback URL for unpublish notification. If empty, hook is ignored.
    // @param req The unpublish request information.
    virtual void on_unpublish(std::string url, ISrsRequest *req) = 0;

    // Stream start playing validation hook.
    // Called when a client attempts to start playing/subscribing to a stream.
    // This hook can be used to authorize stream access or apply viewing policies.
    // @param url The HTTP callback URL for play validation. If empty, hook is ignored.
    // @param req The play request information including stream details.
    // @return srs_success if playing is allowed, error otherwise to reject playing.
    virtual srs_error_t on_play(std::string url, ISrsRequest *req) = 0;

    // Stream stop playing notification hook.
    // Called when a client stops playing/subscribing to a stream.
    // This is a notification-only hook that cannot prevent the stop operation.
    // @param url The HTTP callback URL for stop notification. If empty, hook is ignored.
    // @param req The stop request information.
    virtual void on_stop(std::string url, ISrsRequest *req) = 0;

public:
    // DVR file completion notification hook.
    // Called when a DVR recording file is completed and ready for processing.
    // This hook can be used to trigger post-processing, upload, or notification workflows.
    // Note: This is an asynchronous callback, so the connection context may differ.
    // @param cid The source connection context ID for tracking the original stream.
    // @param url The HTTP callback URL for DVR notification. If empty, hook is ignored.
    // @param req The original stream request information.
    // @param file The completed DVR file path (can be relative or absolute).
    // @return srs_success if processing succeeds, error otherwise (logged but doesn't affect DVR).
    virtual srs_error_t on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file) = 0;

public:
    // HLS segment completion notification hook.
    // Called when an HLS segment (.ts file) is generated and the playlist is updated.
    // This hook can be used for CDN synchronization, analytics, or custom HLS workflows.
    // Note: This is an asynchronous callback, so the connection context may differ.
    // @param cid The source connection context ID for tracking the original stream.
    // @param url The HTTP callback URL for HLS notification. If empty, hook is ignored.
    // @param req The original stream request information.
    // @param file The generated TS segment file path (can be relative or absolute).
    // @param ts_url The TS segment URL as it appears in the M3U8 playlist.
    // @param m3u8 The M3U8 playlist file path (can be relative or absolute).
    // @param m3u8_url The M3U8 playlist URL for HTTP access.
    // @param sn The sequence number of the TS segment in the HLS playlist.
    // @param duration The segment duration in microseconds (srs_utime_t).
    // @return srs_success if processing succeeds, error otherwise (logged but doesn't affect HLS).
    virtual srs_error_t on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                               std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration) = 0;

    // HLS segment notification hook with custom URL template.
    // Called when an HLS segment is generated, allowing custom URL template processing.
    // The callback URL can contain variables like [ts_url] that will be replaced.
    // Note: This is an asynchronous callback, so the connection context may differ.
    // @param cid The source connection context ID for tracking the original stream.
    // @param url The HTTP callback URL template for HLS notification. If empty, hook is ignored.
    // @param req The original stream request information.
    // @param ts_url The TS segment URL to replace [ts_url] variable in the callback URL.
    // @param nb_notify Maximum bytes to read from the notification server response.
    // @return srs_success if processing succeeds, error otherwise (logged but doesn't affect HLS).
    virtual srs_error_t on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify) = 0;

public:
    // Origin cluster co-worker discovery hook.
    // Called to discover available co-worker servers in an origin cluster setup.
    // This enables dynamic load balancing and failover in clustered deployments.
    // @param url The HTTP callback URL for co-worker discovery. If empty, hook is ignored.
    // @param host Output parameter to receive the discovered co-worker host/IP.
    // @param port Output parameter to receive the discovered co-worker port.
    // @return srs_success if co-worker is discovered, error otherwise.
    virtual srs_error_t discover_co_workers(std::string url, std::string &host, int &port) = 0;

public:
    // Stream forwarding backend discovery hook.
    // Called when a published stream needs to be forwarded to backend servers.
    // This hook can dynamically determine which RTMP URLs to forward the stream to.
    // @param url The HTTP callback URL for backend discovery. If empty, hook is ignored.
    // @param req The publish request information.
    // @param rtmp_urls Output parameter to receive the list of RTMP URLs for forwarding.
    // @return srs_success if backends are discovered, error otherwise.
    virtual srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls) = 0;

    // Deprecated hooks.
public:
    // Client connection validation hook.
    // Called when a client attempts to connect to the SRS server.
    // This hook can be used to authenticate or authorize client connections.
    // @param url The HTTP callback URL for client validation. If empty, hook is ignored.
    // @param req The client request information including IP, vhost, app, stream, etc.
    // @return srs_success if connection is allowed, error otherwise to reject connection.
    virtual srs_error_t on_connect(std::string url, ISrsRequest *req) = 0;

    // Client disconnection notification hook.
    // Called when a client disconnects from the SRS server.
    // This is a notification-only hook that cannot prevent the disconnection.
    // @param url The HTTP callback URL for disconnect notification. If empty, hook is ignored.
    // @param req The client request information.
    // @param send_bytes Total bytes sent to the client during the session.
    // @param recv_bytes Total bytes received from the client during the session.
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
    srs_error_t on_publish(std::string url, ISrsRequest *req);
    void on_unpublish(std::string url, ISrsRequest *req);
    srs_error_t on_play(std::string url, ISrsRequest *req);
    void on_stop(std::string url, ISrsRequest *req);
    srs_error_t on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file);
    srs_error_t on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                       std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration);
    srs_error_t on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify);
    srs_error_t discover_co_workers(std::string url, std::string &host, int &port);
    srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_post(ISrsHttpClient *hc, std::string url, std::string req, int &code, std::string &res);
};

// Global HTTP hooks instance
extern ISrsHttpHooks *_srs_hooks;

#endif
