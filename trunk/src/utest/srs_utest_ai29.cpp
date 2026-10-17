//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai29.hpp>

#include <srs_app_http_static.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_ai06.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_manual_kernel.hpp>

#include <stdio.h>
#include <unistd.h>

using namespace std;

// The http static server mounts one directory per vhost, and a root mount for the vhosts that do not claim "/". Every
// mount is read from config, so the tests drive it with a config mock and observe the mounts on a mux mock.

MockAppConfigForHttpStaticServer::MockAppConfigForHttpStaticServer()
{
    root_ = new SrsConfDirective();
    http_stream_dir_ = "./objs/nginx/html";
}

MockAppConfigForHttpStaticServer::~MockAppConfigForHttpStaticServer()
{
    srs_freep(root_);
}

void MockAppConfigForHttpStaticServer::add_vhost(string vhost, string mount, string dir)
{
    root_->get_or_create("vhost", vhost);
    enabled_[vhost] = true;
    http_enabled_[vhost] = true;
    mounts_[vhost] = mount;
    dirs_[vhost] = dir;
}

void MockAppConfigForHttpStaticServer::add_directive(string name)
{
    root_->get_or_create(name);
}

SrsConfDirective *MockAppConfigForHttpStaticServer::get_root()
{
    return root_;
}

string MockAppConfigForHttpStaticServer::get_http_stream_dir()
{
    return http_stream_dir_;
}

bool MockAppConfigForHttpStaticServer::get_vhost_enabled(string vhost)
{
    return enabled_.count(vhost) ? enabled_[vhost] : false;
}

bool MockAppConfigForHttpStaticServer::get_vhost_enabled(SrsConfDirective *conf)
{
    return get_vhost_enabled(conf->arg0());
}

bool MockAppConfigForHttpStaticServer::get_vhost_http_enabled(string vhost)
{
    return http_enabled_.count(vhost) ? http_enabled_[vhost] : false;
}

string MockAppConfigForHttpStaticServer::get_vhost_http_mount(string vhost)
{
    return mounts_.count(vhost) ? mounts_[vhost] : "";
}

string MockAppConfigForHttpStaticServer::get_vhost_http_dir(string vhost)
{
    return dirs_.count(vhost) ? dirs_[vhost] : "";
}

MockHttpServeMuxForHttpStaticServer::MockHttpServeMuxForHttpStaticServer()
{
    handle_error_ = 0;
}

MockHttpServeMuxForHttpStaticServer::~MockHttpServeMuxForHttpStaticServer()
{
    for (int i = 0; i < (int)handlers_.size(); i++) {
        ISrsHttpHandler *handler = handlers_[i];
        srs_freep(handler);
    }
}

srs_error_t MockHttpServeMuxForHttpStaticServer::handle(string pattern, ISrsHttpHandler *handler)
{
    patterns_.push_back(pattern);
    handlers_.push_back(handler);

    if (handle_error_) {
        return srs_error_new(handle_error_, "mock mux");
    }

    return srs_success;
}

srs_error_t MockHttpServeMuxForHttpStaticServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_success;
}

srs_error_t MockHttpServeMuxForHttpStaticServer::find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph)
{
    return srs_success;
}

void MockHttpServeMuxForHttpStaticServer::unhandle(string pattern, ISrsHttpHandler *handler)
{
}

static void inject_mocks(SrsHttpStaticServer *server, MockAppConfigForHttpStaticServer *config, MockHttpServeMuxForHttpStaticServer *mux)
{
    server->config_ = config;
    srs_freep(server->mux_);
    server->mux_ = mux;
}

static void release_mocks(SrsHttpStaticServer *server)
{
    server->config_ = NULL;
    server->mux_ = NULL;
}

// The dir served at a mount, read from the vod stream the static server mounted there.
static string mounted_dir(MockHttpServeMuxForHttpStaticServer *mux, string pattern)
{
    for (int i = 0; i < (int)mux->patterns_.size(); i++) {
        if (mux->patterns_[i] != pattern) {
            continue;
        }

        SrsHttpFileServer *fs = dynamic_cast<SrsHttpFileServer *>(mux->handlers_[i]);
        return fs ? fs->dir : "";
    }

    return "";
}

// A vhost that is disabled is not mounted at all.
VOID TEST(HttpStaticServerTest, MountVhostSkipsDisabledVhost)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls/", "./objs/nginx/html/hls");
    config.enabled_["ossrs.net"] = false;

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_SUCCESS(server.mount_vhost("ossrs.net", pmount));
    release_mocks(&server);

    EXPECT_STREQ("", pmount.c_str());
    EXPECT_EQ(0, (int)mux.patterns_.size());
}

// A vhost with http static disabled is not mounted either.
VOID TEST(HttpStaticServerTest, MountVhostSkipsVhostWithHttpDisabled)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls/", "./objs/nginx/html/hls");
    config.http_enabled_["ossrs.net"] = false;

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_SUCCESS(server.mount_vhost("ossrs.net", pmount));
    release_mocks(&server);

    EXPECT_STREQ("", pmount.c_str());
    EXPECT_EQ(0, (int)mux.patterns_.size());
}

// The configured mount serves the configured dir, and the mount is reported back to the caller.
VOID TEST(HttpStaticServerTest, MountVhostMountsTheConfiguredDir)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls/", "./objs/nginx/html/hls");

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_SUCCESS(server.mount_vhost("ossrs.net", pmount));
    release_mocks(&server);

    EXPECT_STREQ("/hls/", pmount.c_str());
    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/hls/", mux.patterns_[0].c_str());
    EXPECT_STREQ("./objs/nginx/html/hls", mounted_dir(&mux, "/hls/").c_str());
}

// The [vhost] variable is replaced by the vhost name, in both the mount and the dir.
VOID TEST(HttpStaticServerTest, MountVhostReplacesTheVhostVariable)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/[vhost]/hls/", "./objs/nginx/html/[vhost]");

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_SUCCESS(server.mount_vhost("ossrs.net", pmount));
    release_mocks(&server);

    EXPECT_STREQ("/ossrs.net/hls/", pmount.c_str());
    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/ossrs.net/hls/", mux.patterns_[0].c_str());
    EXPECT_STREQ("./objs/nginx/html/ossrs.net", mounted_dir(&mux, "/ossrs.net/hls/").c_str());
}

// A mount that does not end with a slash gets one, because it mounts a dir.
VOID TEST(HttpStaticServerTest, MountVhostEndsTheMountWithASlash)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls", "./objs/nginx/html/hls");

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_SUCCESS(server.mount_vhost("ossrs.net", pmount));
    release_mocks(&server);

    EXPECT_STREQ("/hls/", pmount.c_str());
    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/hls/", mux.patterns_[0].c_str());
}

// The default vhost is not part of the URL, so its default mount becomes the root mount.
VOID TEST(HttpStaticServerTest, MountVhostStripsTheDefaultVhostFromTheMount)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost(SRS_CONSTS_RTMP_DEFAULT_VHOST, "[vhost]/", "./objs/nginx/html");

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_SUCCESS(server.mount_vhost(SRS_CONSTS_RTMP_DEFAULT_VHOST, pmount));
    release_mocks(&server);

    EXPECT_STREQ("/", pmount.c_str());
    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/", mux.patterns_[0].c_str());
}

// A mux that refuses the mount fails the vhost, and nothing is reported as mounted.
VOID TEST(HttpStaticServerTest, MountVhostFailsWhenTheMuxRefusesTheMount)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls/", "./objs/nginx/html/hls");
    mux.handle_error_ = ERROR_HTTP_PATTERN_DUPLICATED;

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    string pmount;
    HELPER_EXPECT_FAILED(server.mount_vhost("ossrs.net", pmount));
    release_mocks(&server);

    EXPECT_STREQ("", pmount.c_str());
}

// Every vhost is mounted, and because none of them claims the root, the http stream dir is mounted at "/".
VOID TEST(HttpStaticServerTest, InitializeMountsEveryVhostAndTheRoot)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls/", "./objs/nginx/html/hls");
    config.add_vhost("srs.io", "/vod/", "./objs/nginx/html/vod");
    config.http_stream_dir_ = "./objs/nginx/html";

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    HELPER_EXPECT_SUCCESS(server.initialize());
    release_mocks(&server);

    ASSERT_EQ(3, (int)mux.patterns_.size());
    EXPECT_STREQ("/hls/", mux.patterns_[0].c_str());
    EXPECT_STREQ("/vod/", mux.patterns_[1].c_str());
    EXPECT_STREQ("/", mux.patterns_[2].c_str());
    EXPECT_STREQ("./objs/nginx/html", mounted_dir(&mux, "/").c_str());
}

// A vhost that claims the root owns it, so no second root mount is added.
VOID TEST(HttpStaticServerTest, InitializeKeepsTheVhostThatMountsTheRoot)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/", "./objs/nginx/html/ossrs.net");
    config.http_stream_dir_ = "./objs/nginx/html";

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    HELPER_EXPECT_SUCCESS(server.initialize());
    release_mocks(&server);

    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/", mux.patterns_[0].c_str());
    EXPECT_STREQ("./objs/nginx/html/ossrs.net", mounted_dir(&mux, "/").c_str());
}

// Directives that are not vhosts are ignored, and the root is still mounted.
VOID TEST(HttpStaticServerTest, InitializeIgnoresDirectivesThatAreNotVhosts)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_directive("http_server");
    config.add_directive("listen");

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    HELPER_EXPECT_SUCCESS(server.initialize());
    release_mocks(&server);

    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/", mux.patterns_[0].c_str());
}

// A vhost that cannot be mounted fails the whole initialization.
VOID TEST(HttpStaticServerTest, InitializeFailsWhenAVhostCannotBeMounted)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStaticServer config;
    MockHttpServeMuxForHttpStaticServer mux;
    config.add_vhost("ossrs.net", "/hls/", "./objs/nginx/html/hls");
    mux.handle_error_ = ERROR_HTTP_PATTERN_DUPLICATED;

    SrsHttpStaticServer server;
    inject_mocks(&server, &config, &mux);

    HELPER_EXPECT_FAILED(server.initialize());
    release_mocks(&server);

    ASSERT_EQ(1, (int)mux.patterns_.size());
    EXPECT_STREQ("/hls/", mux.patterns_[0].c_str());
}

MockAppConfigForVodStream::MockAppConfigForVodStream()
{
    hls_ctx_enabled_ = false;
    hls_m3u8_file_ = "[app]/[stream].m3u8";
}

MockAppConfigForVodStream::~MockAppConfigForVodStream()
{
}

void MockAppConfigForVodStream::resolve_vhost_as(string canonical)
{
    srs_freep(default_vhost_);

    default_vhost_ = new SrsConfDirective();
    default_vhost_->name_ = "vhost";
    default_vhost_->args_.push_back(canonical);
}

SrsConfDirective *MockAppConfigForVodStream::get_vhost(string vhost, bool try_default_vhost)
{
    resolved_vhosts_.push_back(vhost);
    return default_vhost_;
}

bool MockAppConfigForVodStream::get_hls_ctx_enabled(string vhost)
{
    hls_ctx_vhosts_.push_back(vhost);
    return hls_ctx_enabled_;
}

string MockAppConfigForVodStream::get_hls_m3u8_file(string /*vhost*/)
{
    return hls_m3u8_file_;
}

MockAppConfigForVodStreamHooks::MockAppConfigForVodStreamHooks()
{
    hls_ctx_enabled_ = true;

    on_play_directive_ = new SrsConfDirective();
    on_play_directive_->name_ = "on_play";
    on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/sessions");

    on_stop_directive_ = new SrsConfDirective();
    on_stop_directive_->name_ = "on_stop";
    on_stop_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/sessions");

    hls_window_ = 1 * SRS_UTIME_SECONDS;
}

MockAppConfigForVodStreamHooks::~MockAppConfigForVodStreamHooks()
{
    srs_freep(on_play_directive_);
    srs_freep(on_stop_directive_);
}

bool MockAppConfigForVodStreamHooks::get_vhost_http_hooks_enabled(string /*vhost*/)
{
    return true;
}

SrsConfDirective *MockAppConfigForVodStreamHooks::get_vhost_on_play(string /*vhost*/)
{
    return on_play_directive_;
}

SrsConfDirective *MockAppConfigForVodStreamHooks::get_vhost_on_stop(string /*vhost*/)
{
    return on_stop_directive_;
}

srs_utime_t MockAppConfigForVodStreamHooks::get_hls_window(string /*vhost*/)
{
    return hls_window_;
}

MockHttpHooksForVodStream::MockHttpHooksForVodStream()
{
}

MockHttpHooksForVodStream::~MockHttpHooksForVodStream()
{
}

srs_error_t MockHttpHooksForVodStream::on_play(string url, ISrsRequest *req, SrsRtmpConnType type)
{
    on_play_streams_.push_back(req->app_ + "/" + req->stream_);
    if (!refuse_stream_.empty() && req->stream_ == refuse_stream_) {
        return srs_error_new(ERROR_SYSTEM_AUTH, "refuse stream %s", req->stream_.c_str());
    }
    return MockHttpHooksForLiveStream::on_play(url, req, type);
}

void MockHttpHooksForVodStream::on_stop(string url, ISrsRequest *req, SrsRtmpConnType type, const SrsStatisticStreamSnapshot &stream)
{
    on_stop_streams_.push_back(req->app_ + "/" + req->stream_);
    MockHttpHooksForLiveStream::on_stop(url, req, type, stream);
}

MockSecurityForVodStream::MockSecurityForVodStream()
{
}

MockSecurityForVodStream::~MockSecurityForVodStream()
{
}

srs_error_t MockSecurityForVodStream::check(SrsRtmpConnType /*type*/, string /*ip*/, ISrsRequest *req)
{
    checked_streams_.push_back(req->app_ + "/" + req->stream_);
    return srs_success;
}

MockFileReaderFactoryForVodStream::MockFileReaderFactoryForVodStream(string content)
{
    content_ = content;
}

MockFileReaderFactoryForVodStream::~MockFileReaderFactoryForVodStream()
{
}

SrsFileReader *MockFileReaderFactoryForVodStream::create_file_reader()
{
    return new MockSrsFileReader(content_.data(), (int)content_.length());
}

MockResponseWriterForVodStream::MockResponseWriterForVodStream()
{
}

MockResponseWriterForVodStream::~MockResponseWriterForVodStream()
{
}

srs_error_t MockResponseWriterForVodStream::filter(SrsHttpHeader *h)
{
    h->del("Content-Type");
    h->del("Server");
    h->del("Connection");
    h->del("Access-Control-Allow-Origin");
    h->del("Access-Control-Allow-Methods");
    h->del("Access-Control-Expose-Headers");
    h->del("Access-Control-Allow-Headers");
    return srs_success;
}

// Serve the file of url, MP4 or FLV, with the range or start of url, and the Range header if range is not empty, and
// return the raw HTTP response in resp.
static srs_error_t mock_vod_serve(string content, string url, string &resp, string range = "")
{
    srs_error_t err = srs_success;

    SrsHttpMuxEntry entry;
    entry.pattern = "/";

    SrsVodStream stream("/tmp");
    stream.set_fs_factory(new MockFileReaderFactoryForVodStream(content));
    stream.set_path(new MockSrsPathAlwaysExists());
    stream.entry_ = &entry;

    MockResponseWriterForVodStream w;
    SrsHttpMessage r(NULL, NULL);
    if (!range.empty()) {
        SrsHttpHeader h;
        h.set("Range", range);
        r.set_header(&h, false);
    }
    if ((err = r.set_url(url, false)) != srs_success) {
        return srs_error_wrap(err, "set url");
    }

    if ((err = stream.serve_http(&w, &r)) != srs_success) {
        return srs_error_wrap(err, "serve http");
    }

    resp = HELPER_BUFFER2STR(&w.io.out_buffer);

    return err;
}

// The last byte position of a range is inclusive, so an end at the file size is one byte past the last byte. Clamp it
// to the last byte, instead of promising a byte that does not exist and reading past the end of the file.
VOID TEST(VodStreamRangeTest, RangeEndAtFilesizeServesTheWholeFile)
{
    srs_error_t err = srs_success;

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4?bytes=0-13", resp));

    EXPECT_PRED2(is_string_contain, "206 Partial Content", resp);
    EXPECT_PRED2(is_string_contain, "Content-Length: 13", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes 0-12/13", resp);
    EXPECT_PRED2(is_string_contain, "Hello, world!", resp);
}

// A range end far beyond the file is clamped to the last byte as well.
VOID TEST(VodStreamRangeTest, RangeEndBeyondFilesizeIsClamped)
{
    srs_error_t err = srs_success;

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4?bytes=7-100", resp));

    EXPECT_PRED2(is_string_contain, "206 Partial Content", resp);
    EXPECT_PRED2(is_string_contain, "Content-Length: 6", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes 7-12/13", resp);
    EXPECT_PRED2(is_string_contain, "world!", resp);
}

// A range inside the file is served as before, with the last byte position it asked for.
VOID TEST(VodStreamRangeTest, RangeInsideTheFileIsUnchanged)
{
    srs_error_t err = srs_success;

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4?bytes=2-5", resp));

    EXPECT_PRED2(is_string_contain, "206 Partial Content", resp);
    EXPECT_PRED2(is_string_contain, "Content-Length: 4", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes 2-5/13", resp);
    EXPECT_PRED2(is_string_contain, "llo,", resp);
}

// A suffix range names bytes from the end of the file, including when the requested suffix is longer than the file.
VOID TEST(VodStreamRangeTest, SuffixRangeServesTheLastBytes)
{
    srs_error_t err = srs_success;

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4?bytes=-6", resp));

    EXPECT_PRED2(is_string_contain, "206 Partial Content", resp);
    EXPECT_PRED2(is_string_contain, "Content-Length: 6", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes 7-12/13", resp);
    EXPECT_PRED2(is_string_contain, "world!", resp);
}

VOID TEST(VodStreamRangeTest, SuffixRangeLongerThanFileServesTheWholeFile)
{
    srs_error_t err = srs_success;

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4?bytes=-20", resp));

    EXPECT_PRED2(is_string_contain, "206 Partial Content", resp);
    EXPECT_PRED2(is_string_contain, "Content-Length: 13", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes 0-12/13", resp);
    EXPECT_PRED2(is_string_contain, "Hello, world!", resp);
}

// A suffix range of zero bytes names no byte of the file, so it is refused with 416 and the file size in
// Content-Range, as RFC 9110 has it, not served as the first byte.
VOID TEST(VodStreamRangeTest, SuffixRangeOfZeroBytesIsUnsatisfiable)
{
    srs_error_t err = srs_success;

    const char *urls[] = {"/index.mp4?bytes=-0", "/index.mp4?range=-0"};
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        string resp;
        HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", urls[i], resp));

        EXPECT_PRED2(is_string_contain, "416 Requested Range Not Satisfiable", resp) << urls[i];
        EXPECT_PRED2(is_string_contain, "Content-Range: bytes */13", resp) << urls[i];
        EXPECT_FALSE(is_string_contain("Hello", resp)) << urls[i];
    }

    // The Range header is refused the same way.
    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4", resp, "bytes=-0"));
    EXPECT_PRED2(is_string_contain, "416 Requested Range Not Satisfiable", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes */13", resp);
}

// A range that starts at or past the file size has no byte to serve, so it is refused with 416 and the file size in
// Content-Range, as RFC 9110 has it, instead of closing the connection with no answer. The request still succeeds, and
// the answer carries Connection: Close like every file answer.
VOID TEST(VodStreamRangeTest, RangeStartBeyondTheLastByteIsUnsatisfiable)
{
    srs_error_t err = srs_success;

    const char *urls[] = {"/index.mp4?bytes=13-20", "/index.mp4?bytes=13-", "/index.mp4?range=23-33", "/index.mp4?bytes=100-"};
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        string resp;
        HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", urls[i], resp));

        EXPECT_PRED2(is_string_contain, "416 Requested Range Not Satisfiable", resp) << urls[i];
        EXPECT_PRED2(is_string_contain, "Content-Range: bytes */13", resp) << urls[i];
        EXPECT_FALSE(is_string_contain("Hello", resp)) << urls[i];
    }
}

// The Range header is refused the same way as the range in the query.
VOID TEST(VodStreamRangeTest, RangeHeaderStartBeyondTheLastByteIsUnsatisfiable)
{
    srs_error_t err = srs_success;

    const char *ranges[] = {"bytes=13-", "bytes=23-33"};
    for (int i = 0; i < (int)(sizeof(ranges) / sizeof(ranges[0])); i++) {
        string resp;
        HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4", resp, ranges[i]));

        EXPECT_PRED2(is_string_contain, "416 Requested Range Not Satisfiable", resp) << ranges[i];
        EXPECT_PRED2(is_string_contain, "Content-Range: bytes */13", resp) << ranges[i];
    }

    // The last byte is still served.
    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", "/index.mp4", resp, "bytes=12-"));
    EXPECT_PRED2(is_string_contain, "206 Partial Content", resp);
    EXPECT_PRED2(is_string_contain, "Content-Range: bytes 12-12/13", resp);
}

// An empty file has no byte to serve for any range.
VOID TEST(VodStreamRangeTest, RangeOfEmptyFileIsUnsatisfiable)
{
    srs_error_t err = srs_success;

    const char *urls[] = {"/index.mp4?bytes=0-", "/index.mp4?bytes=-5"};
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        string resp;
        HELPER_ASSERT_SUCCESS(mock_vod_serve("", urls[i], resp));

        EXPECT_PRED2(is_string_contain, "416 Requested Range Not Satisfiable", resp) << urls[i];
        EXPECT_PRED2(is_string_contain, "Content-Range: bytes */0", resp) << urls[i];
    }
}

// An FLV start at or past the file size has no tag to serve, so it is refused with 416 and the file size in
// Content-Range, like an MP4 range, instead of closing the connection with no answer, or sending the FLV header and
// sequence headers and then failing to seek.
VOID TEST(VodStreamRangeTest, FlvStartBeyondTheLastByteIsUnsatisfiable)
{
    srs_error_t err = srs_success;

    const char *urls[] = {"/index.flv?start=13", "/index.flv?start=14", "/index.flv?start=1013"};
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        string resp;
        HELPER_ASSERT_SUCCESS(mock_vod_serve("Hello, world!", urls[i], resp));

        EXPECT_PRED2(is_string_contain, "416 Requested Range Not Satisfiable", resp) << urls[i];
        EXPECT_PRED2(is_string_contain, "Content-Range: bytes */13", resp) << urls[i];
        EXPECT_FALSE(is_string_contain("Hello", resp)) << urls[i];
    }
}

// Serve the playlist at url, with config deciding the vhost, and return the raw HTTP response in resp.
static srs_error_t mock_vod_serve_m3u8(MockAppConfigForVodStream *config, string url, string &resp)
{
    srs_error_t err = srs_success;

    SrsHttpMuxEntry entry;
    entry.pattern = "/";

    SrsVodStream stream("/tmp");
    stream.set_fs_factory(new MockFileReaderFactoryForVodStream("#EXTM3U\n"));
    stream.set_path(new MockSrsPathAlwaysExists());
    stream.entry_ = &entry;

    // The VOD stream resolves the vhost of the request, then the HLS stream it delegates to decides
    // whether to serve the request as an HLS session. Both read the same config.
    stream.config_ = config;
    stream.hls_.config_ = config;

    MockResponseWriterForVodStream w;
    SrsHttpMessage r(NULL, NULL);
    if ((err = r.set_url(url, false)) == srs_success) {
        err = stream.serve_http(&w, &r);
    }

    resp = HELPER_BUFFER2STR(&w.io.out_buffer);

    stream.config_ = NULL;
    stream.hls_.config_ = NULL;

    return err;
}

// A vhost the config resolves to another name, such as an alias or a wildcard vhost, must reach the
// HLS stream under the resolved name. Otherwise the HLS settings of the vhost that actually owns the
// stream are never read, and the playlist is served with the settings of a vhost that does not exist.
VOID TEST(VodStreamVhostTest, M3u8CtxServesTheVhostResolvedByConfig)
{
    srs_error_t err = srs_success;

    MockAppConfigForVodStream config;
    config.resolve_vhost_as("srs.io");

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve_m3u8(&config, "/live/stream.m3u8?vhost=ossrs.net", resp));

    // The vhost of the request is the one asked about.
    ASSERT_EQ(1, (int)config.resolved_vhosts_.size());
    EXPECT_STREQ("ossrs.net", config.resolved_vhosts_[0].c_str());

    // GOAL: the HLS stream is asked about the resolved vhost, not the one from the URL.
    ASSERT_EQ(1, (int)config.hls_ctx_vhosts_.size());
    EXPECT_STREQ("srs.io", config.hls_ctx_vhosts_[0].c_str());
}

// A vhost the config does not resolve keeps the name from the URL, and is still served as a plain
// file by the default HLS handler, because this config disables the HLS ctx sessions.
VOID TEST(VodStreamVhostTest, M3u8CtxKeepsTheRequestVhostWhenConfigResolvesNone)
{
    srs_error_t err = srs_success;

    MockAppConfigForVodStream config;

    string resp;
    HELPER_ASSERT_SUCCESS(mock_vod_serve_m3u8(&config, "/live/stream.m3u8?vhost=ossrs.net", resp));

    // GOAL: the vhost is resolved through the injected config, not through the process global.
    ASSERT_EQ(1, (int)config.resolved_vhosts_.size());
    EXPECT_STREQ("ossrs.net", config.resolved_vhosts_[0].c_str());

    ASSERT_EQ(1, (int)config.hls_ctx_vhosts_.size());
    EXPECT_STREQ("ossrs.net", config.hls_ctx_vhosts_[0].c_str());

    EXPECT_PRED2(is_string_contain, "#EXTM3U", resp);
}

// Start an HLS session of the playlist url, with the hls_m3u8_file m3u8_file, asked of the HTTP server
// by the Host header host, and leave the session's request in stat. The stat refuses the client, so
// the session stops there.
static void mock_vod_hls_session(MockStatisticForLiveStream *stat, string m3u8_file, bool https, string host, string url)
{
    MockAppConfigForVodStream config;
    config.hls_ctx_enabled_ = true;
    config.hls_m3u8_file_ = m3u8_file;

    stat->on_client_error_ = srs_error_new(ERROR_SYSTEM_AUTH, "refused by the test");

    SrsHttpMuxEntry entry;
    entry.pattern = "/";

    SrsVodStream stream("/tmp");
    stream.set_fs_factory(new MockFileReaderFactoryForVodStream("#EXTM3U\n"));
    stream.set_path(new MockSrsPathAlwaysExists());
    stream.entry_ = &entry;
    stream.config_ = &config;
    stream.hls_.config_ = &config;
    stream.hls_.stat_ = stat;

    MockResponseWriterForVodStream w;
    SrsHttpMessage r(NULL, NULL);
    SrsHttpHeader header;
    header.set("Host", host);
    r.set_header(&header, false);
    r.set_https(https);
    srs_error_t err = r.set_url(url, false);
    srs_freep(err);
    err = stream.serve_http(&w, &r);
    srs_freep(err);

    stream.config_ = NULL;
    stream.hls_.config_ = NULL;
    stream.hls_.stat_ = NULL;
    srs_freep(stat->on_client_error_);

    EXPECT_EQ(1, stat->on_client_count_);
}

// Start an HLS session of the playlist /live/stream.m3u8, asked of the HTTP server by the Host header
// host, and return the tcUrl the stat got for the session.
static string mock_vod_hls_session_tcurl(bool https, string host)
{
    MockStatisticForLiveStream stat;
    mock_vod_hls_session(&stat, "[app]/[stream].m3u8", https, host, "/live/stream.m3u8");
    return stat.on_client_tc_url_;
}

// An HLS session is listed with the URL of its app on the HTTP server it asked, with the server's
// port, as an HTTP-FLV viewer is, not with the RTMP default port left out.
VOID TEST(VodStreamHlsSessionTest, TcUrlHasTheHttpServerPort)
{
    EXPECT_STREQ("http://127.0.0.1:8080/live", mock_vod_hls_session_tcurl(false, "127.0.0.1:8080").c_str());
}

// An HLS session over HTTPS has an https:// tcUrl, and an IPv6 host keeps its brackets.
VOID TEST(VodStreamHlsSessionTest, HttpsTcUrlKeepsTheHost)
{
    EXPECT_STREQ("https://[::1]:8088/live", mock_vod_hls_session_tcurl(true, "[::1]:8088").c_str());
}

// With a playlist in a dir per stream, the session of /live/livestream/index.m3u8 is of the app live
// and the stream livestream, the names the playlist was written with, not the app live/livestream and
// the stream index, which every stream's session would share.
VOID TEST(VodStreamHlsSessionTest, DirPerStreamNamesTheAppAndStream)
{
    MockStatisticForLiveStream stat;
    mock_vod_hls_session(&stat, "[app]/[stream]/index.m3u8", false, "127.0.0.1:8080", "/live/livestream/index.m3u8");
    EXPECT_STREQ("live", stat.on_client_app_.c_str());
    EXPECT_STREQ("livestream", stat.on_client_stream_.c_str());
    EXPECT_STREQ("http://127.0.0.1:8080/live", stat.on_client_tc_url_.c_str());
}

// An app with a dir keeps it, and the last dir before the playlist is the stream.
VOID TEST(VodStreamHlsSessionTest, DirPerStreamKeepsTheAppDirs)
{
    MockStatisticForLiveStream stat;
    mock_vod_hls_session(&stat, "[app]/[stream]/index.m3u8", false, "127.0.0.1:8080", "/live/sub/s1/index.m3u8?token=a");
    EXPECT_STREQ("live/sub", stat.on_client_app_.c_str());
    EXPECT_STREQ("s1", stat.on_client_stream_.c_str());
    EXPECT_STREQ("http://127.0.0.1:8080/live/sub", stat.on_client_tc_url_.c_str());
}

// A playlist the template doesn't name, such as a static file elsewhere, keeps the URL's names, as
// does a playlist of the default template.
VOID TEST(VodStreamHlsSessionTest, PlaylistNotOfTheTemplateKeepsTheUrlNames)
{
    if (true) {
        MockStatisticForLiveStream stat;
        mock_vod_hls_session(&stat, "[app]/[stream]/index.m3u8", false, "127.0.0.1:8080", "/live/stream.m3u8");
        EXPECT_STREQ("live", stat.on_client_app_.c_str());
        EXPECT_STREQ("stream", stat.on_client_stream_.c_str());
    }

    if (true) {
        MockStatisticForLiveStream stat;
        mock_vod_hls_session(&stat, "[app]/[stream].m3u8", false, "127.0.0.1:8080", "/live/sub/stream.m3u8");
        EXPECT_STREQ("live/sub", stat.on_client_app_.c_str());
        EXPECT_STREQ("stream", stat.on_client_stream_.c_str());
    }
}

// An SrsVodStream that serves HLS sessions with on_play and on_stop hooks, a security check and a
// stat, which all record the names they get. Each new session's ctx is ctx00001.
class MockVodHlsSessions
{
public:
    MockAppConfigForVodStreamHooks config_;
    MockStatisticForLiveStream stat_;
    MockHttpHooksForVodStream hooks_;
    MockSecurityForVodStream security_;
    MockRandForHandshake rand_;
    SrsHttpMuxEntry entry_;
    SrsVodStream stream_;

public:
    MockVodHlsSessions(string m3u8_file) : stream_("/tmp")
    {
        config_.hls_m3u8_file_ = m3u8_file;
        rand_.gen_str_values_.push_back("ctx00001");
        entry_.pattern = "/";

        stream_.set_fs_factory(new MockFileReaderFactoryForVodStream("#EXTM3U\n"));
        stream_.set_path(new MockSrsPathAlwaysExists());
        stream_.entry_ = &entry_;
        stream_.config_ = &config_;
        stream_.hls_.config_ = &config_;
        stream_.hls_.stat_ = &stat_;
        stream_.hls_.hooks_ = &hooks_;
        srs_freep(stream_.hls_.security_);
        stream_.hls_.security_ = &security_;
        srs_freep(stream_.hls_.rand_);
        stream_.hls_.rand_ = &rand_;
        stream_.hls_.shared_timer_ = NULL;
    }
    virtual ~MockVodHlsSessions()
    {
        stream_.config_ = NULL;
        stream_.hls_.config_ = NULL;
        stream_.hls_.stat_ = NULL;
        stream_.hls_.hooks_ = NULL;
        stream_.hls_.security_ = NULL;
        stream_.hls_.rand_ = NULL;
    }

public:
    // Ask for url, and return the raw HTTP response.
    string get(string url)
    {
        MockResponseWriterForVodStream w;
        SrsHttpMessage r(NULL, NULL);
        SrsHttpHeader header;
        header.set("Host", "127.0.0.1:8080");
        r.set_header(&header, false);
        srs_error_t err = r.set_url(url, false);
        srs_freep(err);
        err = stream_.serve_http(&w, &r);
        srs_freep(err);
        return HELPER_BUFFER2STR(&w.io.out_buffer);
    }

    // End every session, as the HLS timer does once its viewer has been gone for twice the window.
    void expire()
    {
        std::map<std::string, SrsHlsVirtualConn *>::iterator it;
        for (it = stream_.hls_.map_ctx_info_.begin(); it != stream_.hls_.map_ctx_info_.end(); ++it) {
            SrsHlsVirtualConn *conn = it->second;
            conn->request_time_ -= 3 * config_.hls_window_;
        }
        srs_error_t err = stream_.hls_.on_timer(5 * SRS_UTIME_SECONDS);
        srs_freep(err);
    }
};

// A playlist asked for by a percent-encoded name is the file of its decoded name, so the session is of
// that stream at every step: the security check, the stat and on_play when it starts, the playlist
// of the session, then on_stop and the stat's disconnect when it ends. None of them names the
// encoded stream, which no publisher has. With a playlist in a dir per stream too.
VOID TEST(VodStreamHlsSessionTest, EncodedPlaylistNamesTheDecodedStream)
{
    struct {
        string m3u8_file;
        string url;
    } cases[] = {
        {"[app]/[stream].m3u8", "/live/%6Civestream.m3u8"},
        {"[app]/[stream].m3u8", "/%6Cive/livestream.m3u8"},
        {"[app]/[stream].m3u8", "/live/livestream%2Em3u8"},
        {"[app]/[stream]/index.m3u8", "/live/%6Civestream/index.m3u8"},
        {"[app]/[stream]/index.m3u8", "/live/livestream/%69ndex.m3u8"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        string url = cases[i].url;
        MockVodHlsSessions s(cases[i].m3u8_file);

        // The session starts: checked, counted and hooked as live/livestream.
        string resp = s.get(url);
        EXPECT_PRED2(is_string_contain, "HTTP/1.1 200", resp) << url;
        EXPECT_PRED2(is_string_contain, "hls_ctx=ctx00001", resp) << url;
        ASSERT_EQ(1, (int)s.security_.checked_streams_.size()) << url;
        EXPECT_STREQ("live/livestream", s.security_.checked_streams_[0].c_str()) << url;
        EXPECT_EQ(1, s.stat_.on_client_count_) << url;
        EXPECT_STREQ("live", s.stat_.on_client_app_.c_str()) << url;
        EXPECT_STREQ("livestream", s.stat_.on_client_stream_.c_str()) << url;
        EXPECT_STREQ("http://127.0.0.1:8080/live", s.stat_.on_client_tc_url_.c_str()) << url;
        ASSERT_EQ(1, (int)s.hooks_.on_play_streams_.size()) << url;
        EXPECT_STREQ("live/livestream", s.hooks_.on_play_streams_[0].c_str()) << url;
        EXPECT_EQ(0, (int)s.hooks_.on_stop_streams_.size()) << url;
        EXPECT_EQ(0, s.stat_.on_disconnect_count_) << url;
        ASSERT_TRUE(s.stream_.hls_.ctx_is_exist("ctx00001")) << url;

        // The player asks for the playlist of its session: no second check, count or hook.
        resp = s.get(url + "?hls_ctx=ctx00001");
        EXPECT_PRED2(is_string_contain, "HTTP/1.1 200", resp) << url;
        EXPECT_PRED2(is_string_contain, "#EXTM3U", resp) << url;
        EXPECT_EQ(1, (int)s.security_.checked_streams_.size()) << url;
        EXPECT_EQ(1, s.stat_.on_client_count_) << url;
        EXPECT_EQ(1, (int)s.hooks_.on_play_streams_.size()) << url;
        EXPECT_EQ(0, (int)s.hooks_.on_stop_streams_.size()) << url;
        EXPECT_EQ(0, s.stat_.on_disconnect_count_) << url;

        // The session ends: on_stop of live/livestream, and the stat drops the client.
        s.expire();
        EXPECT_FALSE(s.stream_.hls_.ctx_is_exist("ctx00001")) << url;
        ASSERT_EQ(1, (int)s.hooks_.on_stop_streams_.size()) << url;
        EXPECT_STREQ("live/livestream", s.hooks_.on_stop_streams_[0].c_str()) << url;
        ASSERT_EQ(1, s.stat_.on_disconnect_count_) << url;
        EXPECT_STREQ("ctx00001", s.stat_.on_disconnect_ids_[0].c_str()) << url;
        EXPECT_EQ(1, (int)s.hooks_.on_play_streams_.size()) << url;
        EXPECT_EQ(1, s.stat_.on_client_count_) << url;
    }
}

// A hook that refuses the stream livestream refuses its playlist asked for by a percent-encoded name,
// with 401, as it does by the plain name: the viewer is counted and dropped, and no session is left
// to end with an on_stop.
VOID TEST(VodStreamHlsSessionTest, EncodedPlaylistOfARefusedStreamIsRefused)
{
    string urls[] = {"/live/livestream.m3u8", "/live/%6Civestream.m3u8", "/live/livestream%2Em3u8"};

    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        string url = urls[i];
        MockVodHlsSessions s("[app]/[stream].m3u8");
        s.hooks_.refuse_stream_ = "livestream";

        string resp = s.get(url);
        EXPECT_PRED2(is_string_contain, "HTTP/1.1 401", resp) << url;
        EXPECT_EQ(string::npos, resp.find("hls_ctx=")) << url;
        ASSERT_EQ(1, (int)s.hooks_.on_play_streams_.size()) << url;
        EXPECT_STREQ("live/livestream", s.hooks_.on_play_streams_[0].c_str()) << url;
        EXPECT_EQ(1, s.stat_.on_client_count_) << url;
        EXPECT_STREQ("livestream", s.stat_.on_client_stream_.c_str()) << url;
        ASSERT_EQ(1, s.stat_.on_disconnect_count_) << url;
        EXPECT_STREQ("ctx00001", s.stat_.on_disconnect_ids_[0].c_str()) << url;
        EXPECT_FALSE(s.stream_.hls_.ctx_is_exist("ctx00001")) << url;

        s.expire();
        EXPECT_EQ(0, (int)s.hooks_.on_stop_streams_.size()) << url;
        EXPECT_EQ(1, s.stat_.on_disconnect_count_) << url;
    }
}

// The ctx of a session of livestream doesn't serve the playlist of another stream, which the hook
// refuses: that playlist asked for with the ctx is a new session of the other stream, refused with 401
// by its own on_play, and the session of livestream goes on as before, until it ends with its on_stop.
VOID TEST(VodStreamHlsSessionTest, CtxOfAnotherStreamIsCheckedAsANewSession)
{
    MockVodHlsSessions s("[app]/[stream].m3u8");
    s.rand_.gen_str_values_.push_back("ctx00002");
    s.hooks_.refuse_stream_ = "other";

    // The session of livestream starts.
    string resp = s.get("/live/livestream.m3u8");
    EXPECT_PRED2(is_string_contain, "hls_ctx=ctx00001", resp);
    ASSERT_EQ(1, (int)s.hooks_.on_play_streams_.size());
    EXPECT_EQ(1, s.stat_.on_client_count_);

    // The playlist of other with the ctx of livestream: checked, counted and hooked as live/other,
    // refused, and the refused viewer dropped by a ctx of its own.
    resp = s.get("/live/other.m3u8?hls_ctx=ctx00001");
    EXPECT_PRED2(is_string_contain, "HTTP/1.1 401", resp);
    EXPECT_EQ(string::npos, resp.find("#EXTM3U"));
    ASSERT_EQ(2, (int)s.security_.checked_streams_.size());
    EXPECT_STREQ("live/other", s.security_.checked_streams_[1].c_str());
    EXPECT_EQ(2, s.stat_.on_client_count_);
    EXPECT_STREQ("other", s.stat_.on_client_stream_.c_str());
    ASSERT_EQ(2, (int)s.hooks_.on_play_streams_.size());
    EXPECT_STREQ("live/other", s.hooks_.on_play_streams_[1].c_str());
    ASSERT_EQ(1, s.stat_.on_disconnect_count_);
    EXPECT_STREQ("ctx00002", s.stat_.on_disconnect_ids_[0].c_str());
    EXPECT_FALSE(s.stream_.hls_.ctx_is_exist("ctx00002"));

    // The session of livestream is unchanged, and still serves its own playlist.
    ASSERT_TRUE(s.stream_.hls_.ctx_is_exist("ctx00001"));
    SrsHlsVirtualConn *conn = s.stream_.hls_.map_ctx_info_["ctx00001"];
    EXPECT_STREQ("livestream", conn->req_->stream_.c_str());
    EXPECT_FALSE(conn->interrupt_);
    resp = s.get("/live/livestream.m3u8?hls_ctx=ctx00001");
    EXPECT_PRED2(is_string_contain, "HTTP/1.1 200", resp);
    EXPECT_PRED2(is_string_contain, "#EXTM3U", resp);
    EXPECT_EQ(2, (int)s.hooks_.on_play_streams_.size());
    EXPECT_EQ(2, s.stat_.on_client_count_);

    // The session of livestream ends with its own on_stop; other had no session to end.
    s.expire();
    ASSERT_EQ(1, (int)s.hooks_.on_stop_streams_.size());
    EXPECT_STREQ("live/livestream", s.hooks_.on_stop_streams_[0].c_str());
    ASSERT_EQ(2, s.stat_.on_disconnect_count_);
    EXPECT_STREQ("ctx00001", s.stat_.on_disconnect_ids_[1].c_str());
}

// A player that asks for the playlist of another stream with the ctx of its session, which that
// stream's hook allows, gets a new session of that stream with a new ctx, as a player with no ctx
// does. Each session ends with the on_stop of its own stream.
VOID TEST(VodStreamHlsSessionTest, CtxOfAnotherStreamStartsItsOwnSession)
{
    MockVodHlsSessions s("[app]/[stream].m3u8");
    s.rand_.gen_str_values_.push_back("ctx00002");

    string resp = s.get("/live/livestream.m3u8");
    EXPECT_PRED2(is_string_contain, "hls_ctx=ctx00001", resp);

    // A new session of other, with the new ctx in its master playlist.
    resp = s.get("/live/other.m3u8?hls_ctx=ctx00001");
    EXPECT_PRED2(is_string_contain, "HTTP/1.1 200", resp);
    EXPECT_PRED2(is_string_contain, "/live/other.m3u8?hls_ctx=ctx00002", resp);
    EXPECT_EQ(string::npos, resp.find("hls_ctx=ctx00001"));
    ASSERT_EQ(2, (int)s.security_.checked_streams_.size());
    EXPECT_STREQ("live/other", s.security_.checked_streams_[1].c_str());
    ASSERT_EQ(2, (int)s.hooks_.on_play_streams_.size());
    EXPECT_STREQ("live/other", s.hooks_.on_play_streams_[1].c_str());
    EXPECT_EQ(2, s.stat_.on_client_count_);
    EXPECT_EQ(0, s.stat_.on_disconnect_count_);

    // Each session keeps its own stream.
    ASSERT_TRUE(s.stream_.hls_.ctx_is_exist("ctx00001"));
    ASSERT_TRUE(s.stream_.hls_.ctx_is_exist("ctx00002"));
    SrsHlsVirtualConn *a = s.stream_.hls_.map_ctx_info_["ctx00001"];
    EXPECT_STREQ("livestream", a->req_->stream_.c_str());
    SrsHlsVirtualConn *b = s.stream_.hls_.map_ctx_info_["ctx00002"];
    EXPECT_STREQ("other", b->req_->stream_.c_str());

    // The playlist of other with its own ctx is of its session: no second check or hook.
    resp = s.get("/live/other.m3u8?hls_ctx=ctx00002");
    EXPECT_PRED2(is_string_contain, "HTTP/1.1 200", resp);
    EXPECT_PRED2(is_string_contain, "#EXTM3U", resp);
    EXPECT_EQ(2, (int)s.hooks_.on_play_streams_.size());
    EXPECT_EQ(2, s.stat_.on_client_count_);

    s.expire();
    ASSERT_EQ(2, (int)s.hooks_.on_stop_streams_.size());
    std::set<std::string> stopped(s.hooks_.on_stop_streams_.begin(), s.hooks_.on_stop_streams_.end());
    EXPECT_TRUE(stopped.count("live/livestream"));
    EXPECT_TRUE(stopped.count("live/other"));
    EXPECT_EQ(2, s.stat_.on_disconnect_count_);
}

// The app and stream of a path built by srs_path_build_stream from a template.
VOID TEST(PathParseStreamTest, ParsesTheAppAndStreamOfTheTemplate)
{
    string app, stream;

    EXPECT_TRUE(srs_path_parse_stream("[app]/[stream].m3u8", "v", "live/livestream.m3u8", app, stream));
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("livestream", stream.c_str());

    EXPECT_TRUE(srs_path_parse_stream("[app]/[stream]/index.m3u8", "v", "live/livestream/index.m3u8", app, stream));
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("livestream", stream.c_str());

    // The app may have dirs, the stream has none.
    EXPECT_TRUE(srs_path_parse_stream("[app]/[stream]/index.m3u8", "v", "a/b/c/index.m3u8", app, stream));
    EXPECT_STREQ("a/b", app.c_str());
    EXPECT_STREQ("c", stream.c_str());

    // A stream with dots, as a playlist of the default template.
    EXPECT_TRUE(srs_path_parse_stream("[app]/[stream].m3u8", "v", "live/a.b.m3u8", app, stream));
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("a.b", stream.c_str());

    // The [vhost] is the vhost.
    EXPECT_TRUE(srs_path_parse_stream("[vhost]/[app]/[stream]/index.m3u8", "ossrs.net", "ossrs.net/live/s/index.m3u8", app, stream));
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("s", stream.c_str());

    // A variable used twice has one value.
    EXPECT_TRUE(srs_path_parse_stream("[app]/[stream]/[stream].m3u8", "v", "live/s/s.m3u8", app, stream));
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("s", stream.c_str());
}

// A path the template doesn't build, or a template without both names, parses nothing.
VOID TEST(PathParseStreamTest, RefusesAPathNotOfTheTemplate)
{
    string app, stream;

    EXPECT_FALSE(srs_path_parse_stream("[app]/[stream]/index.m3u8", "v", "live/livestream.m3u8", app, stream));
    EXPECT_FALSE(srs_path_parse_stream("[app]/[stream]/index.m3u8", "v", "live/s/playlist.m3u8", app, stream));
    EXPECT_FALSE(srs_path_parse_stream("[vhost]/[app]/[stream].m3u8", "ossrs.net", "srs.io/live/s.m3u8", app, stream));
    EXPECT_FALSE(srs_path_parse_stream("[app]/[stream]/[stream].m3u8", "v", "live/s/t.m3u8", app, stream));
    EXPECT_FALSE(srs_path_parse_stream("[app]/[stream].m3u8", "v", "/s.m3u8", app, stream));
    EXPECT_FALSE(srs_path_parse_stream("[app]/index.m3u8", "v", "live/index.m3u8", app, stream));
    EXPECT_FALSE(srs_path_parse_stream("hls/[stream].m3u8", "v", "hls/s.m3u8", app, stream));
}

MockSrsPathForHttpFileServer::MockSrsPathForHttpFileServer(bool is_dir)
{
    is_dir_ = is_dir;
}

MockSrsPathForHttpFileServer::~MockSrsPathForHttpFileServer()
{
}

bool MockSrsPathForHttpFileServer::exists(string /*path*/)
{
    return true;
}

bool MockSrsPathForHttpFileServer::is_dir(string path)
{
    dir_paths_.push_back(path);
    return is_dir_;
}

// Serve url from the static dir /tmp mounted at pattern, whose every path is a directory or a file of content, and
// return the raw HTTP response in resp.
static srs_error_t mock_file_server_serve(MockSrsPathForHttpFileServer *path, string content, string url, string &resp, vector<string> *dir_paths = NULL, string pattern = "/")
{
    srs_error_t err = srs_success;

    SrsHttpMuxEntry entry;
    entry.pattern = pattern;

    SrsHttpFileServer server("/tmp");
    server.set_fs_factory(new MockFileReaderFactoryForVodStream(content));
    server.set_path(path);
    server.entry_ = &entry;

    MockResponseWriterForVodStream w;
    SrsHttpMessage r(NULL, NULL);
    if ((err = r.set_url(url, false)) == srs_success) {
        err = server.serve_http(&w, &r);
    }

    resp = HELPER_BUFFER2STR(&w.io.out_buffer);

    // The server owns and frees the path, so copy what it was asked.
    if (dir_paths) {
        *dir_paths = path->dir_paths_;
    }

    return err;
}

// A directory asked for without its slash is redirected to the path with the slash, as web servers do, so its
// index.html is served and its relative links resolve. Reading the directory as a file fails and answers nothing.
VOID TEST(HttpFileServerDirTest, DirWithoutSlashRedirectsToTheSlash)
{
    srs_error_t err = srs_success;

    MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(true);
    string resp;
    HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "dir", "/live/sub", resp));

    EXPECT_PRED2(is_string_contain, "HTTP/1.1 301 Moved Permanently", resp);
    EXPECT_PRED2(is_string_contain, "Location: /live/sub/\r\n", resp);
}

// The query is kept, and a path whose name looks like a stream or a file, or ends with a dot, is still a directory.
VOID TEST(HttpFileServerDirTest, DirRedirectKeepsTheQuery)
{
    srs_error_t err = srs_success;

    if (true) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(true);
        string resp;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "dir", "/sub?v=1&k=x", resp));
        EXPECT_PRED2(is_string_contain, "HTTP/1.1 301 Moved Permanently", resp);
        EXPECT_PRED2(is_string_contain, "Location: /sub/?v=1&k=x\r\n", resp);
    }

    const char *urls[] = {"/sub.flv", "/sub.mp4", "/sub.m3u8", "/sub.ts", "/sub/."};
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(true);
        string resp;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "dir", urls[i], resp));
        EXPECT_PRED2(is_string_contain, "HTTP/1.1 301 Moved Permanently", resp) << urls[i];
        EXPECT_PRED2(is_string_contain, string("Location: ") + urls[i] + "/\r\n", resp) << urls[i];
    }
}

// A path that starts with several slashes redirects to a location with one: a browser reads "//sub/" as the host sub,
// not a path. A backslash is escaped, since a browser reads "/\sub/" as "//sub/" too.
VOID TEST(HttpFileServerDirTest, DirRedirectStaysOnTheHost)
{
    srs_error_t err = srs_success;

    const char *urls[][2] = {
        {"//sub", "/sub/"},
        {"///live/sub?v=1", "/live/sub/?v=1"},
        {"//evil.com", "/evil.com/"},
        {"/\\evil.com", "/%5Cevil.com/"},
        {"/live/a\\b", "/live/a%5Cb/"},
    };
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(true);
        string resp;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "dir", urls[i][0], resp));
        EXPECT_PRED2(is_string_contain, "HTTP/1.1 301 Moved Permanently", resp) << urls[i][0];
        EXPECT_PRED2(is_string_contain, string("Location: ") + urls[i][1] + "\r\n", resp) << urls[i][0];
    }
}

// A directory at the default page of a path with its slash, such as a dir named index.html, has no page to serve: 404,
// not a redirect that adds another slash, and not a read of the directory.
VOID TEST(HttpFileServerDirTest, DirAtTheDefaultPageIsNotFound)
{
    srs_error_t err = srs_success;

    MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(true);
    string resp;
    HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "dir", "/sub/", resp));

    EXPECT_PRED2(is_string_contain, "HTTP/1.1 404 Not Found", resp);
    EXPECT_FALSE(is_string_contain("Location:", resp));
}

// A file is served as before, and the server asks whether its full path is a directory.
VOID TEST(HttpFileServerDirTest, FileIsServed)
{
    srs_error_t err = srs_success;

    MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(false);
    string resp;
    vector<string> dir_paths;
    HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "Hello, world!", "/live/a.dat", resp, &dir_paths));

    EXPECT_PRED2(is_string_contain, "HTTP/1.1 200 OK", resp);
    EXPECT_PRED2(is_string_contain, "Hello, world!", resp);
    ASSERT_EQ(1, (int)dir_paths.size());
    EXPECT_STREQ("/tmp/live/a.dat", dir_paths[0].c_str());
}

// SrsPath tells a directory from a file and a missing path.
VOID TEST(HttpFileServerDirTest, PathIsDir)
{
    SrsPath path;

    EXPECT_TRUE(path.is_dir("."));
    EXPECT_TRUE(path.is_dir("./"));
    EXPECT_FALSE(path.is_dir("./srs-utest-no-such-path"));

    FILE *f = fopen("/tmp/srs-utest-path-is-dir.txt", "w");
    ASSERT_TRUE(f != NULL);
    fclose(f);
    EXPECT_TRUE(path.exists("/tmp/srs-utest-path-is-dir.txt"));
    EXPECT_FALSE(path.is_dir("/tmp/srs-utest-path-is-dir.txt"));
    unlink("/tmp/srs-utest-path-is-dir.txt");
}

// A file whose name needs escaping is asked for percent-encoded, as a client must send it: the server looks for the
// decoded name. A plus is a plus in a path, not a space.
VOID TEST(HttpFileServerEscapeTest, EscapedNameIsDecoded)
{
    srs_error_t err = srs_success;

    const char *cases[][2] = {
        {"/live/a%20b.dat", "/tmp/live/a b.dat"},
        {"/live/100%25.dat", "/tmp/live/100%.dat"},
        {"/live/a+b.dat", "/tmp/live/a+b.dat"},
        {"/live/a%2edat", "/tmp/live/a.dat"},
        {"/live/a%2e%2eb.dat", "/tmp/live/a..b.dat"},
        {"/live/%E5%90%8D.dat", "/tmp/live/\xE5\x90\x8D.dat"},
        {"/live%2fa.dat", "/tmp/live/a.dat"},
        {"/live/a%20b.dat?v=a%20b", "/tmp/live/a b.dat"},
    };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(false);
        string resp;
        vector<string> dir_paths;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "Hello, world!", cases[i][0], resp, &dir_paths));

        EXPECT_PRED2(is_string_contain, "HTTP/1.1 200 OK", resp) << cases[i][0];
        EXPECT_PRED2(is_string_contain, "Hello, world!", resp) << cases[i][0];
        ASSERT_EQ(1, (int)dir_paths.size()) << cases[i][0];
        EXPECT_STREQ(cases[i][1], dir_paths[0].c_str()) << cases[i][0];
    }
}

// A directory whose name needs escaping is redirected to its encoded path with the slash, which is a valid URL.
VOID TEST(HttpFileServerEscapeTest, EscapedDirRedirectsToTheEncodedPath)
{
    srs_error_t err = srs_success;

    MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(true);
    string resp;
    vector<string> dir_paths;
    HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "dir", "/live/a%20b", resp, &dir_paths));

    EXPECT_PRED2(is_string_contain, "HTTP/1.1 301 Moved Permanently", resp);
    EXPECT_PRED2(is_string_contain, "Location: /live/a%20b/\r\n", resp);
    ASSERT_EQ(1, (int)dir_paths.size());
    EXPECT_STREQ("/tmp/live/a b", dir_paths[0].c_str());
}

// A path whose dots or slashes are percent-encoded to make a ".." segment would leave the dir once decoded: 404, and
// no file is looked up. A name with two dots in it is not a ".." segment, see EscapedNameIsDecoded.
VOID TEST(HttpFileServerEscapeTest, EncodedDotDotIsNotServed)
{
    srs_error_t err = srs_success;

    const char *urls[] = {
        "/%2e%2e/%2e%2e/secret",
        "/%2E%2E/%2E%2E/secret",
        "/%2e%2e%2f%2e%2e%2fsecret",
        "/live/.%2e/secret",
        "/live/%2e%2e",
        "/live/%2e%2e/",
        "/live/..%2fsecret",
    };
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(false);
        string resp;
        vector<string> dir_paths;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "secret", urls[i], resp, &dir_paths));

        EXPECT_PRED2(is_string_contain, "HTTP/1.1 404 Not Found", resp) << urls[i];
        EXPECT_FALSE(is_string_contain("secret", resp)) << urls[i];
        EXPECT_TRUE(dir_paths.empty()) << urls[i];
    }
}

// A path with a malformed escape, or an escaped NUL that would cut the file name short, is a bad request: 400, and no
// file is looked up.
VOID TEST(HttpFileServerEscapeTest, BadEscapeIsBadRequest)
{
    srs_error_t err = srs_success;

    const char *urls[] = {"/live/a%zz.dat", "/live/a%2", "/live/100%.dat", "/live/a%00.dat", "/live/a.dat%00.flv"};
    for (int i = 0; i < (int)(sizeof(urls) / sizeof(urls[0])); i++) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(false);
        string resp;
        vector<string> dir_paths;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "Hello, world!", urls[i], resp, &dir_paths));

        EXPECT_PRED2(is_string_contain, "HTTP/1.1 400 Bad Request", resp) << urls[i];
        EXPECT_FALSE(is_string_contain("Hello, world!", resp)) << urls[i];
        EXPECT_TRUE(dir_paths.empty()) << urls[i];
    }
}

// The mux matches a mount to the path as the URL encodes it, so a mount with an escape, such as /a%20b/, serves the
// file of the rest of the path, decoded, under its dir. A vhost's mount, such as ossrs.net/hls/, takes its path, not
// its vhost, off the URL path, also for a short file name.
VOID TEST(HttpFileServerEscapeTest, EscapedMountServesTheFileUnderIt)
{
    srs_error_t err = srs_success;

    const char *cases[][3] = {
        {"/a%20b/", "/a%20b/f.dat", "/tmp/f.dat"},
        {"/a%20b/", "/a%20b/live/c%20d.dat", "/tmp/live/c d.dat"},
        {"/a%20b/", "/a%20b/", "/tmp/index.html"},
        {"/100%25/", "/100%25/f.dat", "/tmp/f.dat"},
        {"/%E5%90%8D/", "/%E5%90%8D/f.dat", "/tmp/f.dat"},
        {"/hls/", "/hls/a%20b.dat", "/tmp/a b.dat"},
        {"ossrs.net/a%20b/", "/a%20b/f.dat", "/tmp/f.dat"},
        {"ossrs.net/hls/", "/hls/a%20b.dat", "/tmp/a b.dat"},
        {"ossrs.net/hls/", "/hls/f.dat", "/tmp/f.dat"},
        {"ossrs.net/hls/", "/hls/live/livestream-0.ts", "/tmp/live/livestream-0.ts"},
    };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        MockSrsPathForHttpFileServer *path = new MockSrsPathForHttpFileServer(false);
        string resp;
        vector<string> dir_paths;
        HELPER_ASSERT_SUCCESS(mock_file_server_serve(path, "Hello, world!", cases[i][1], resp, &dir_paths, cases[i][0]));

        EXPECT_PRED2(is_string_contain, "HTTP/1.1 200 OK", resp) << cases[i][0] << " " << cases[i][1];
        ASSERT_EQ(1, (int)dir_paths.size()) << cases[i][0] << " " << cases[i][1];
        EXPECT_STREQ(cases[i][2], dir_paths[0].c_str()) << cases[i][0] << " " << cases[i][1];
    }
}

// Authenticate a request to the HTTP API with the Authorization header auth, against the user admin and the password
// real-pass, and return the description of the refusal, which the auth mux writes to the log, and the
// WWW-Authenticate of the answer in challenge.
static string mock_basic_auth_refusal(string auth, string *challenge = NULL)
{
    SrsHttpBasicAuthenticator a("admin", "real-pass");

    MockResponseWriter w;
    SrsHttpMessage r(NULL, NULL);
    r.set_basic(HTTP_REQUEST, HTTP_GET, (llhttp_status_t)200, -1);
    srs_error_t err = r.set_url("/api/v1/versions", false);
    srs_freep(err);

    SrsHttpHeader h;
    h.set("Authorization", auth);
    r.set_header(&h, false);

    err = a.authenticate(&w, &r);
    string desc = srs_error_desc(err);
    srs_freep(err);
    if (challenge) {
        *challenge = w.header()->get("WWW-Authenticate");
    }
    return desc;
}

// A refused password, often a near-miss of the real one, is not logged: the refusal names the user only.
VOID TEST(HttpBasicAuthLogTest, WrongPasswordLogsTheUserOnly)
{
    // admin:guess-pass
    string desc = mock_basic_auth_refusal("Basic YWRtaW46Z3Vlc3MtcGFzcw==");
    EXPECT_FALSE(desc.empty());
    EXPECT_FALSE(is_string_contain("guess-pass", desc)) << desc;
    EXPECT_FALSE(is_string_contain("YWRtaW46Z3Vlc3MtcGFzcw==", desc)) << desc;
    EXPECT_PRED2(is_string_contain, "user admin", desc);
}

// Credentials that don't parse are not logged either: neither the header, nor its token, nor the decoded text.
VOID TEST(HttpBasicAuthLogTest, MalformedCredentialsAreNotLogged)
{
    const char *refused[][2] = {
        // The decoded token guess-pass has no colon.
        {"Basic Z3Vlc3MtcGFzcw==", "guess-pass"},
        // A token that isn't base64.
        {"Basic guess-pass!", "guess-pass"},
        // Credentials of another scheme.
        {"Token guess-pass", "guess-pass"},
        {"Basicguess-pass", "guess-pass"},
    };
    for (int i = 0; i < (int)(sizeof(refused) / sizeof(refused[0])); i++) {
        string desc = mock_basic_auth_refusal(refused[i][0]);
        EXPECT_FALSE(desc.empty()) << refused[i][0];
        EXPECT_FALSE(is_string_contain(refused[i][1], desc)) << refused[i][0] << ": " << desc;
        EXPECT_FALSE(is_string_contain("Z3Vlc3MtcGFzcw", desc)) << refused[i][0] << ": " << desc;
    }
}

// Every refusal challenges with the Basic scheme and the realm it requires (RFC 7617 2), so a strict client prompts
// for the credentials and retries; an accepted request gets no challenge.
VOID TEST(HttpBasicAuthChallengeTest, EveryRefusalNamesTheRealm)
{
    const char *refused[] = {
        "",                               // No credentials.
        "Basic YWRtaW46Z3Vlc3MtcGFzcw==", // admin:guess-pass
        "Basic Z3Vlc3MtcGFzcw==",         // guess-pass, no colon.
        "Basic guess-pass!",              // Not base64.
        "Basic ",                         // No token.
        "Bearer real-pass",               // Another scheme.
    };
    for (int i = 0; i < (int)(sizeof(refused) / sizeof(refused[0])); i++) {
        string challenge;
        EXPECT_STRNE("Success", mock_basic_auth_refusal(refused[i], &challenge).c_str()) << refused[i];
        EXPECT_STREQ("Basic realm=\"SRS\"", challenge.c_str()) << refused[i];
    }

    // admin:real-pass
    string challenge;
    EXPECT_STREQ("Success", mock_basic_auth_refusal("Basic YWRtaW46cmVhbC1wYXNz", &challenge).c_str());
    EXPECT_TRUE(challenge.empty());
}
