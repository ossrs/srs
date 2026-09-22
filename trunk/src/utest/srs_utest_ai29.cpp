//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai29.hpp>

#include <srs_app_http_static.hpp>
#include <srs_kernel_error.hpp>

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
