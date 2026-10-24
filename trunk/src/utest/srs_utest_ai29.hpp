//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI29_HPP
#define SRS_UTEST_AI29_HPP

/*
#include <srs_utest_ai29.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>

#include <map>
#include <string>
#include <vector>

// The vhosts of a config file, and the http static settings the static server reads for each of them.
class MockAppConfigForHttpStaticServer : public MockAppConfig
{
public:
    SrsConfDirective *root_;
    std::string http_stream_dir_;
    std::map<std::string, bool> enabled_;
    std::map<std::string, bool> http_enabled_;
    std::map<std::string, std::string> mounts_;
    std::map<std::string, std::string> dirs_;

public:
    MockAppConfigForHttpStaticServer();
    virtual ~MockAppConfigForHttpStaticServer();

public:
    // Add an enabled vhost to the root, serving dir at mount.
    void add_vhost(std::string vhost, std::string mount, std::string dir);
    // Add a directive to the root that is not a vhost.
    void add_directive(std::string name);

public:
    virtual SrsConfDirective *get_root();
    virtual std::string get_http_stream_dir();
    virtual bool get_vhost_enabled(std::string vhost);
    virtual bool get_vhost_enabled(SrsConfDirective *conf);
    virtual bool get_vhost_http_enabled(std::string vhost);
    virtual std::string get_vhost_http_mount(std::string vhost);
    virtual std::string get_vhost_http_dir(std::string vhost);
};

// Records every mount and owns the handlers it is given, as SrsHttpServeMux does.
class MockHttpServeMuxForHttpStaticServer : public ISrsHttpServeMux
{
public:
    std::vector<std::string> patterns_;
    std::vector<ISrsHttpHandler *> handlers_;
    // When set, handle() fails with this error code, after taking the handler.
    int handle_error_;

public:
    MockHttpServeMuxForHttpStaticServer();
    virtual ~MockHttpServeMuxForHttpStaticServer();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual srs_error_t find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph);
    virtual void unhandle(std::string pattern, ISrsHttpHandler *handler);
};

// Resolves the vhost of a VOD request, and records what the VOD and HLS streams asked about.
class MockAppConfigForVodStream : public MockAppConfig
{
public:
    // The vhosts SrsVodStream asked to resolve, in order.
    std::vector<std::string> resolved_vhosts_;
    // The vhosts SrsHlsStream was asked about, in order, after the resolution.
    std::vector<std::string> hls_ctx_vhosts_;
    // Whether the HLS stream serves a playlist as an HLS session; false by default.
    bool hls_ctx_enabled_;
    // The hls_m3u8_file of every vhost; [app]/[stream].m3u8 by default.
    std::string hls_m3u8_file_;

public:
    MockAppConfigForVodStream();
    virtual ~MockAppConfigForVodStream();

public:
    // Resolve every vhost to a directive named canonical, as an alias or wildcard vhost does.
    void resolve_vhost_as(std::string canonical);

public:
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true);
    virtual bool get_hls_ctx_enabled(std::string vhost);
    virtual std::string get_hls_m3u8_file(std::string vhost);
};

// The VOD config of an HLS session with on_play and on_stop hooks, each to one URL.
class MockAppConfigForVodStreamHooks : public MockAppConfigForVodStream
{
public:
    SrsConfDirective *on_play_directive_;
    SrsConfDirective *on_stop_directive_;
    srs_utime_t hls_window_;

public:
    MockAppConfigForVodStreamHooks();
    virtual ~MockAppConfigForVodStreamHooks();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost);
    virtual srs_utime_t get_hls_window(std::string vhost);
};

// Records the app/stream each on_play and on_stop names, and refuses the on_play of one stream, as a
// hook keyed on the stream name does.
class MockHttpHooksForVodStream : public MockHttpHooksForLiveStream
{
public:
    std::vector<std::string> on_play_streams_;
    std::vector<std::string> on_stop_streams_;
    // The stream whose on_play is refused; none when empty.
    std::string refuse_stream_;

public:
    MockHttpHooksForVodStream();
    virtual ~MockHttpHooksForVodStream();

public:
    virtual srs_error_t on_play(std::string url, ISrsRequest *req, SrsRtmpConnType type);
    virtual void on_stop(std::string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream);
};

// Records the app/stream each security check names.
class MockSecurityForVodStream : public ISrsSecurity
{
public:
    std::vector<std::string> checked_streams_;

public:
    MockSecurityForVodStream();
    virtual ~MockSecurityForVodStream();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);
};

// Serves the VOD file from memory, for the range tests of SrsVodStream.
class MockFileReaderFactoryForVodStream : public ISrsFileReaderFactory
{
public:
    std::string content_;

public:
    MockFileReaderFactoryForVodStream(std::string content);
    virtual ~MockFileReaderFactoryForVodStream();

public:
    virtual SrsFileReader *create_file_reader();
};

// Keeps the Content-Range header, which MockResponseWriter drops, so the range tests can assert it.
class MockResponseWriterForVodStream : public MockResponseWriter
{
public:
    MockResponseWriterForVodStream();
    virtual ~MockResponseWriterForVodStream();

public:
    virtual srs_error_t filter(SrsHttpHeader *h);
};

// Every path exists, and is a directory or not as the test says, for the directory tests of SrsHttpFileServer.
class MockSrsPathForHttpFileServer : public SrsPath
{
public:
    bool is_dir_;
    std::vector<std::string> dir_paths_;

public:
    MockSrsPathForHttpFileServer(bool is_dir);
    virtual ~MockSrsPathForHttpFileServer();

public:
    virtual bool exists(std::string path);
    virtual bool is_dir(std::string path);
};

#endif
