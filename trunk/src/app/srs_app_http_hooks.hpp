//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_HTTP_HOOKS_HPP
#define SRS_APP_HTTP_HOOKS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsHttpUri;
class SrsStSocket;
class SrsRequest;
class SrsHttpParser;
class SrsHttpClient;

// the http hooks, http callback api,
// for some event, such as on_connect, call
// a http api(hooks).
// TODO: Refine to global variable.
class SrsHttpHooks
{
private:
    SrsHttpHooks();
public:
    virtual ~SrsHttpHooks();
public:
    // The on_connect hook, when client connect to srs.
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    static srs_error_t on_connect(std::string url, SrsRequest* req);
    // The on_close hook, when client disconnect to srs, where client is valid by on_connect.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    static void on_close(std::string url, SrsRequest* req, int64_t send_bytes, int64_t recv_bytes);
    // The on_publish hook, when client(encoder) start to publish stream
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    static srs_error_t on_publish(std::string url, SrsRequest* req);
    // The on_unpublish hook, when client(encoder) stop publish stream.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    static void on_unpublish(std::string url, SrsRequest* req);
    // The on_play hook, when client start to play stream.
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    static srs_error_t on_play(std::string url, SrsRequest* req);
    // The on_stop hook, when client stop to play the stream.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    static void on_stop(std::string url, SrsRequest* req);
    // The on_dvr hook, when reap a dvr file.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    // @param file the file path, can be relative or absolute path.
    // @param cid the source connection cid, for the on_dvr is async call.
    static srs_error_t on_dvr(SrsContextId cid, std::string url, SrsRequest* req, std::string file);
    // When hls reap segment, callback.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    // @param file the ts file path, can be relative or absolute path.
    // @param ts_url the ts url, which used for m3u8.
    // @param m3u8 the m3u8 file path, can be relative or absolute path.
    // @param m3u8_url the m3u8 url, which is used for the http mount path.
    // @param sn the seq_no, the sequence number of ts in hls/m3u8.
    // @param duration the segment duration in srs_utime_t.
    // @param cid the source connection cid, for the on_dvr is async call.
    static srs_error_t on_hls(SrsContextId cid, std::string url, SrsRequest* req, std::string file, std::string ts_url,
        std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration);
    // When hls reap segment, callback.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    // @param ts_url the ts uri, used to replace the variable [ts_url] in url.
    // @param nb_notify the max bytes to read from notify server.
    // @param cid the source connection cid, for the on_dvr is async call.
    static srs_error_t on_hls_notify(SrsContextId cid, std::string url, SrsRequest* req, std::string ts_url, int nb_notify);
    // Discover co-workers for origin cluster.
    static srs_error_t discover_co_workers(std::string url, std::string& host, int& port);
    // The on_forward_backend hook, when publish stream start to forward
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    static srs_error_t on_forward_backend(std::string url, SrsRequest* req, std::vector<std::string>& rtmp_urls);
private:
    static srs_error_t do_post(SrsHttpClient* hc, std::string url, std::string req, int& code, std::string& res);
};

#endif

