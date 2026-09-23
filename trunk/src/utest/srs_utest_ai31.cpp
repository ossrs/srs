//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai31.hpp>

#include <srs_app_http_conn.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai16.hpp>

using namespace std;

MockHttpHandlerForHttpServer::MockHttpHandlerForHttpServer()
{
}

MockHttpHandlerForHttpServer::~MockHttpHandlerForHttpServer()
{
}

srs_error_t MockHttpHandlerForHttpServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_success;
}

MockHttpServeMuxForHttpServer::MockHttpServeMuxForHttpServer()
{
    handle_error_ = 0;
    serve_http_count_ = 0;
    find_handler_count_ = 0;
    not_found_ = new SrsHttpNotFoundHandler();
    handler_ = not_found_;
    find_handler_error_ = 0;
}

MockHttpServeMuxForHttpServer::~MockHttpServeMuxForHttpServer()
{
    for (int i = 0; i < (int)handlers_.size(); i++) {
        ISrsHttpHandler *handler = handlers_[i];
        srs_freep(handler);
    }

    srs_freep(not_found_);
}

srs_error_t MockHttpServeMuxForHttpServer::handle(string pattern, ISrsHttpHandler *handler)
{
    patterns_.push_back(pattern);
    handlers_.push_back(handler);

    if (handle_error_) {
        return srs_error_new(handle_error_, "mock mux");
    }

    return srs_success;
}

srs_error_t MockHttpServeMuxForHttpServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    serve_http_count_++;
    return srs_success;
}

srs_error_t MockHttpServeMuxForHttpServer::find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph)
{
    find_handler_count_++;

    if (find_handler_error_) {
        return srs_error_new(find_handler_error_, "mock mux");
    }

    *ph = handler_;
    return srs_success;
}

void MockHttpServeMuxForHttpServer::unhandle(string pattern, ISrsHttpHandler *handler)
{
}

MockHttpStreamServerForHttpServer::MockHttpStreamServerForHttpServer()
{
    mux_ = new MockHttpServeMuxForHttpServer();
    assemble_count_ = 0;
    initialize_count_ = 0;
    initialize_error_ = 0;
}

MockHttpStreamServerForHttpServer::~MockHttpStreamServerForHttpServer()
{
    srs_freep(mux_);
}

void MockHttpStreamServerForHttpServer::assemble()
{
    assemble_count_++;
}

srs_error_t MockHttpStreamServerForHttpServer::initialize()
{
    initialize_count_++;

    if (initialize_error_) {
        return srs_error_new(initialize_error_, "mock stream");
    }

    return srs_success;
}

srs_error_t MockHttpStreamServerForHttpServer::http_mount(ISrsRequest *r)
{
    mounted_.push_back(r);
    return srs_success;
}

void MockHttpStreamServerForHttpServer::http_unmount(ISrsRequest *r)
{
    unmounted_.push_back(r);
}

ISrsHttpServeMux *MockHttpStreamServerForHttpServer::mux()
{
    return mux_;
}

srs_error_t MockHttpStreamServerForHttpServer::dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph)
{
    return srs_success;
}

MockHttpStaticServerForHttpServer::MockHttpStaticServerForHttpServer()
{
    mux_ = new MockHttpServeMuxForHttpServer();
    initialize_count_ = 0;
    initialize_error_ = 0;
}

MockHttpStaticServerForHttpServer::~MockHttpStaticServerForHttpServer()
{
    srs_freep(mux_);
}

srs_error_t MockHttpStaticServerForHttpServer::initialize()
{
    initialize_count_++;

    if (initialize_error_) {
        return srs_error_new(initialize_error_, "mock static");
    }

    return srs_success;
}

ISrsHttpServeMux *MockHttpStaticServerForHttpServer::mux()
{
    return mux_;
}

srs_error_t MockHttpStaticServerForHttpServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_success;
}

// Replace the two servers the http server allocated for itself with mocks the test drives.
static void inject_mocks(SrsHttpServer *server, MockHttpStreamServerForHttpServer *stream, MockHttpStaticServerForHttpServer *statics)
{
    srs_freep(server->http_stream_);
    server->http_stream_ = stream;

    srs_freep(server->http_static_);
    server->http_static_ = statics;
}

// The mocks are borrowed, so drop them before the http server destructor frees what it holds.
static void release_mocks(SrsHttpServer *server)
{
    server->http_stream_ = NULL;
    server->http_static_ = NULL;
}

// SrsHttpServer is the composition of the two servers behind one HTTP port: the live stream server and the static
// file server. It owns both, initializes both, and decides which of them answers each request. Nothing it does
// needs a socket, a file or a config, so all of it is testable once an owner can put its own two servers in place.

// GOAL: the stream server is assembled by the owner calling assemble(), not by the constructor, so a test can put
// its own stream server in place first. Assembling from the constructor registers the dynamic matcher on the
// stream server the constructor made, which an owner then throws away.
VOID TEST(HttpServerTest, AssembleAssemblesTheStreamServer)
{
    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    EXPECT_EQ(0, stream.assemble_count_);

    server.assemble();
    release_mocks(&server);

    EXPECT_EQ(1, stream.assemble_count_);
}

// The versions API is mounted on the static server, so SRS go-sharp can detect an HTTP-FLV cluster node.
VOID TEST(HttpServerTest, InitializeMountsTheVersionsApiOnTheStaticServer)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.initialize());
    release_mocks(&server);

    ASSERT_EQ(1, (int)statics.mux_->patterns_.size());
    EXPECT_STREQ("/api/v1/versions", statics.mux_->patterns_[0].c_str());
    EXPECT_EQ(0, (int)stream.mux_->patterns_.size());
}

// Both halves are initialized.
VOID TEST(HttpServerTest, InitializeInitializesBothServers)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.initialize());
    release_mocks(&server);

    EXPECT_EQ(1, stream.initialize_count_);
    EXPECT_EQ(1, statics.initialize_count_);
}

// A static server that refuses the versions API fails the whole initialization, before either half is initialized.
VOID TEST(HttpServerTest, InitializeFailsWhenTheVersionsApiIsRefused)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    statics.mux_->handle_error_ = ERROR_HTTP_PATTERN_DUPLICATED;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_FAILED(server.initialize());
    release_mocks(&server);

    EXPECT_EQ(0, stream.initialize_count_);
    EXPECT_EQ(0, statics.initialize_count_);
}

// The stream server is initialized first, so its failure stops the static server from being initialized at all.
VOID TEST(HttpServerTest, InitializeFailsBeforeTheStaticServerWhenTheStreamServerFails)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    stream.initialize_error_ = ERROR_HTTP_HANDLER_INVALID;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_FAILED(server.initialize());
    release_mocks(&server);

    EXPECT_EQ(1, stream.initialize_count_);
    EXPECT_EQ(0, statics.initialize_count_);
}

// A static server that cannot initialize fails the whole initialization too.
VOID TEST(HttpServerTest, InitializeFailsWhenTheStaticServerFails)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    statics.initialize_error_ = ERROR_HTTP_HANDLER_INVALID;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_FAILED(server.initialize());
    release_mocks(&server);

    EXPECT_EQ(1, stream.initialize_count_);
    EXPECT_EQ(1, statics.initialize_count_);
}

// A handler registered on the http server lands on the static server, which is where the API and console live.
VOID TEST(HttpServerTest, HandleRegistersThePatternOnTheStaticServer)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.handle("/api/v1/clients", new SrsHttpNotFoundHandler()));
    release_mocks(&server);

    ASSERT_EQ(1, (int)statics.mux_->patterns_.size());
    EXPECT_STREQ("/api/v1/clients", statics.mux_->patterns_[0].c_str());
    EXPECT_EQ(0, (int)stream.mux_->patterns_.size());
}

// An API request goes to the static server without ever asking the stream server, because a stream can never be
// mounted under /api/.
VOID TEST(HttpServerTest, ServeHttpRoutesTheApiToTheStaticServer)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    MockHttpMessageForDynamicMatch msg;
    msg.path_ = "/api/v1/versions";

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.serve_http(NULL, &msg));
    release_mocks(&server);

    EXPECT_EQ(1, statics.mux_->serve_http_count_);
    EXPECT_EQ(0, stream.mux_->find_handler_count_);
    EXPECT_EQ(0, stream.mux_->serve_http_count_);
}

// The console is served the same way, from the static server only.
VOID TEST(HttpServerTest, ServeHttpRoutesTheConsoleToTheStaticServer)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    MockHttpMessageForDynamicMatch msg;
    msg.path_ = "/console/index.html";

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.serve_http(NULL, &msg));
    release_mocks(&server);

    EXPECT_EQ(1, statics.mux_->serve_http_count_);
    EXPECT_EQ(0, stream.mux_->find_handler_count_);
}

// The API shortcut needs the whole "/api/" prefix. A path that is only as long as "/api" is a normal path, so the
// stream server is asked about it like any other.
VOID TEST(HttpServerTest, ServeHttpDoesNotTakeAShortPathForTheApi)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    MockHttpMessageForDynamicMatch msg;
    msg.path_ = "/api";

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.serve_http(NULL, &msg));
    release_mocks(&server);

    EXPECT_EQ(1, stream.mux_->find_handler_count_);
    EXPECT_EQ(1, statics.mux_->serve_http_count_);
}

// A path a stream is mounted at is served by the stream server.
VOID TEST(HttpServerTest, ServeHttpRoutesAMountedStreamToTheStreamServer)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    MockHttpHandlerForHttpServer found;
    stream.mux_->handler_ = &found;

    MockHttpMessageForDynamicMatch msg;
    msg.path_ = "/live/livestream.flv";

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    // The handler the stream mux found is not the not found handler, so the stream server serves the request.
    HELPER_EXPECT_SUCCESS(server.serve_http(NULL, &msg));
    release_mocks(&server);

    EXPECT_EQ(1, stream.mux_->find_handler_count_);
    EXPECT_EQ(1, stream.mux_->serve_http_count_);
    EXPECT_EQ(0, statics.mux_->serve_http_count_);
}

// A path no stream is mounted at falls back to the static server, which serves the files.
VOID TEST(HttpServerTest, ServeHttpFallsBackToTheStaticServerWhenNoStreamMatches)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    MockHttpMessageForDynamicMatch msg;
    msg.path_ = "/live/livestream.m3u8";

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.serve_http(NULL, &msg));
    release_mocks(&server);

    EXPECT_EQ(1, stream.mux_->find_handler_count_);
    EXPECT_EQ(0, stream.mux_->serve_http_count_);
    EXPECT_EQ(1, statics.mux_->serve_http_count_);
}

// A failed lookup fails the request instead of falling back, so a broken stream mux is not hidden by the static
// server answering 404.
VOID TEST(HttpServerTest, ServeHttpFailsWhenTheStreamLookupFails)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    stream.mux_->find_handler_error_ = ERROR_HTTP_HANDLER_MATCH_URL;

    MockHttpMessageForDynamicMatch msg;
    msg.path_ = "/live/livestream.flv";

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_FAILED(server.serve_http(NULL, &msg));
    release_mocks(&server);

    EXPECT_EQ(0, stream.mux_->serve_http_count_);
    EXPECT_EQ(0, statics.mux_->serve_http_count_);
}

// Mounting and unmounting a stream reaches the stream server, which is the only half that serves live streams.
VOID TEST(HttpServerTest, MountAndUnmountReachTheStreamServer)
{
    srs_error_t err = srs_success;

    MockHttpStreamServerForHttpServer stream;
    MockHttpStaticServerForHttpServer statics;
    SrsRequest req;

    SrsHttpServer server;
    inject_mocks(&server, &stream, &statics);

    HELPER_EXPECT_SUCCESS(server.http_mount(&req));
    server.http_unmount(&req);
    release_mocks(&server);

    ASSERT_EQ(1, (int)stream.mounted_.size());
    EXPECT_TRUE(&req == stream.mounted_[0]);
    ASSERT_EQ(1, (int)stream.unmounted_.size());
    EXPECT_TRUE(&req == stream.unmounted_[0]);
}
