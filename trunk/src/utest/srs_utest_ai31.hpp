//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI31_HPP
#define SRS_UTEST_AI31_HPP

/*
#include <srs_utest_ai31.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_http_static.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_protocol_http_stack.hpp>

#include <string>
#include <vector>

// A handler that matched a request, as opposed to the not found handler a mux answers with when nothing matched.
class MockHttpHandlerForHttpServer : public ISrsHttpHandler
{
public:
    MockHttpHandlerForHttpServer();
    virtual ~MockHttpHandlerForHttpServer();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// Records every mount and lookup one of the two muxes of the http server received.
class MockHttpServeMuxForHttpServer : public ISrsHttpServeMux
{
public:
    // The patterns handled, in order, and the handlers this mux took ownership of.
    std::vector<std::string> patterns_;
    std::vector<ISrsHttpHandler *> handlers_;
    // When set, handle() fails with this error code, after taking the handler.
    int handle_error_;
    // How many requests this mux was asked to serve.
    int serve_http_count_;
    // How many handler lookups this mux answered.
    int find_handler_count_;
    // The not found handler this mux answers with when nothing matches, as SrsHttpServeMux does.
    SrsHttpNotFoundHandler *not_found_;
    // The handler find_handler() returns. Borrowed, and defaults to not_found_.
    ISrsHttpHandler *handler_;
    // When set, find_handler() fails with this error code.
    int find_handler_error_;

public:
    MockHttpServeMuxForHttpServer();
    virtual ~MockHttpServeMuxForHttpServer();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual srs_error_t find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph);
    virtual void unhandle(std::string pattern, ISrsHttpHandler *handler);
};

// The live stream half of the http server: records the assembly, the initialization and every mount.
class MockHttpStreamServerForHttpServer : public ISrsHttpStreamServer
{
public:
    MockHttpServeMuxForHttpServer *mux_;
    // How many times the owner assembled this server.
    int assemble_count_;
    // How many times the owner initialized it, and the error initialize() fails with.
    int initialize_count_;
    int initialize_error_;
    // The requests mounted and unmounted, in order.
    std::vector<ISrsRequest *> mounted_;
    std::vector<ISrsRequest *> unmounted_;

public:
    MockHttpStreamServerForHttpServer();
    virtual ~MockHttpStreamServerForHttpServer();

public:
    virtual void assemble();
    virtual srs_error_t initialize();
    virtual srs_error_t http_mount(ISrsRequest *r);
    virtual void http_unmount(ISrsRequest *r);
    virtual ISrsHttpServeMux *mux();
    virtual srs_error_t dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph);
};

// The static file half of the http server: records the initialization and serves through its mux.
class MockHttpStaticServerForHttpServer : public ISrsHttpStaticServer
{
public:
    MockHttpServeMuxForHttpServer *mux_;
    // How many times the owner initialized it, and the error initialize() fails with.
    int initialize_count_;
    int initialize_error_;

public:
    MockHttpStaticServerForHttpServer();
    virtual ~MockHttpStaticServerForHttpServer();

public:
    virtual srs_error_t initialize();
    virtual ISrsHttpServeMux *mux();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

#endif
