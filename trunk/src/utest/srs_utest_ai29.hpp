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

public:
    MockAppConfigForVodStream();
    virtual ~MockAppConfigForVodStream();

public:
    // Resolve every vhost to a directive named canonical, as an alias or wildcard vhost does.
    void resolve_vhost_as(std::string canonical);

public:
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true);
    virtual bool get_hls_ctx_enabled(std::string vhost);
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

#endif
