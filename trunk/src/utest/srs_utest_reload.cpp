//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_utest_reload.hpp>

using namespace std;

#include <srs_kernel_error.hpp>

MockReloadHandler::MockReloadHandler()
{
    reset();
}

MockReloadHandler::~MockReloadHandler()
{
}

void MockReloadHandler::reset()
{
    listen_reloaded = false;
    pithy_print_reloaded = false;
    vhost_added_reloaded = false;
    vhost_removed_reloaded = false;
    vhost_play_reloaded = false;
    vhost_forward_reloaded = false;
    vhost_hls_reloaded = false;
    vhost_dvr_reloaded = false;
    vhost_transcode_reloaded = false;
    ingest_removed_reloaded = false;
    ingest_added_reloaded = false;
    ingest_updated_reloaded = false;
}

int MockReloadHandler::count_total()
{
    return 56 - 31;
}

int MockReloadHandler::count_true()
{
    int count_true = 0;
    
    if (listen_reloaded) count_true++;
    if (pithy_print_reloaded) count_true++;
    if (vhost_added_reloaded) count_true++;
    if (vhost_removed_reloaded) count_true++;
    if (vhost_play_reloaded) count_true++;
    if (vhost_forward_reloaded) count_true++;
    if (vhost_hls_reloaded) count_true++;
    if (vhost_dvr_reloaded) count_true++;
    if (vhost_transcode_reloaded) count_true++;
    if (ingest_removed_reloaded) count_true++;
    if (ingest_added_reloaded) count_true++;
    if (ingest_updated_reloaded) count_true++;
    
    return count_true;
}

int MockReloadHandler::count_false()
{
    int count_false = 0;
    
    if (!listen_reloaded) count_false++;
    if (!pithy_print_reloaded) count_false++;
    if (!vhost_added_reloaded) count_false++;
    if (!vhost_removed_reloaded) count_false++;
    if (!vhost_play_reloaded) count_false++;
    if (!vhost_forward_reloaded) count_false++;
    if (!vhost_hls_reloaded) count_false++;
    if (!vhost_dvr_reloaded) count_false++;
    if (!vhost_transcode_reloaded) count_false++;
    if (!ingest_removed_reloaded) count_false++;
    if (!ingest_added_reloaded) count_false++;
    if (!ingest_updated_reloaded) count_false++;
    
    return count_false;
}

bool MockReloadHandler::all_false()
{
    return count_true() == 0;
}

bool MockReloadHandler::all_true()
{
    return count_true() == count_total();
}

srs_error_t MockReloadHandler::on_reload_listen()
{
    listen_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_pithy_print()
{
    pithy_print_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_added(string /*vhost*/)
{
    vhost_added_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_removed(string /*vhost*/)
{
    vhost_removed_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_play(string /*vhost*/)
{
    vhost_play_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_forward(string /*vhost*/)
{
    vhost_forward_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_hls(string /*vhost*/)
{
    vhost_hls_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_hds(string /*vhost*/)
{
    vhost_hls_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_dvr(string /*vhost*/)
{
    vhost_dvr_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_vhost_transcode(string /*vhost*/)
{
    vhost_transcode_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_ingest_removed(string /*vhost*/, string /*ingest_id*/)
{
    ingest_removed_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_ingest_added(string /*vhost*/, string /*ingest_id*/)
{
    ingest_added_reloaded = true;
    return srs_success;
}

srs_error_t MockReloadHandler::on_reload_ingest_updated(string /*vhost*/, string /*ingest_id*/)
{
    ingest_updated_reloaded = true;
    return srs_success;
}

MockSrsReloadConfig::MockSrsReloadConfig()
{
}

MockSrsReloadConfig::~MockSrsReloadConfig()
{
}

srs_error_t MockSrsReloadConfig::do_reload(string buf)
{
    srs_error_t err = srs_success;
    
    MockSrsReloadConfig conf;
    if ((err = conf.parse(buf)) != srs_success) {
        return srs_error_wrap(err, "parse");
    }

    if ((err = MockSrsConfig::reload_conf(&conf)) != srs_success) {
        return srs_error_wrap(err, "reload conf");
    }
    
    return err;
}

VOID TEST(ConfigReloadTest, ReloadEmpty)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_FAILED(conf.parse(""));
    HELPER_EXPECT_FAILED(conf.do_reload(""));
    EXPECT_TRUE(handler.all_false());
}

VOID TEST(ConfigReloadTest, ReloadListen)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse("listen 1935;"));
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1935;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1936;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1936;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1936 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1935 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload("listen 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadPithyPrint)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"pithy_print_ms 1000;"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"pithy_print_ms 1000;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"pithy_print_ms 2000;"));
    EXPECT_TRUE(handler.pithy_print_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"pithy_print_ms 1000;"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostAdded)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_TRUE(handler.vhost_added_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostRemoved)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{enabled off;}"));
    EXPECT_TRUE(handler.vhost_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostRemoved2)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{} vhost b{}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.vhost_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostAtc)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{atc off;}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{atc off;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{atc on;}"));
    EXPECT_TRUE(handler.vhost_play_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{atc off;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostGopCache)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{gop_cache off;}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{gop_cache off;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{gop_cache on;}"));
    EXPECT_TRUE(handler.vhost_play_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{gop_cache off;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostQueueLength)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{queue_length 10;}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{queue_length 10;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{queue_length 20;}"));
    EXPECT_TRUE(handler.vhost_play_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{queue_length 10;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostTimeJitter)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{time_jitter full;}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{time_jitter full;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{time_jitter zero;}"));
    EXPECT_TRUE(handler.vhost_play_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{time_jitter full;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostForward)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1936;}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1936;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1937;}"));
    EXPECT_TRUE(handler.vhost_forward_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1936;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostHls)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{hls {enabled on;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{hls {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{hls {enabled off;}}"));
    EXPECT_TRUE(handler.vhost_hls_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{hls {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostDvr)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{dvr {enabled on;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{dvr {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{dvr {enabled off;}}"));
    EXPECT_TRUE(handler.vhost_dvr_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{dvr {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostTranscode)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{transcode {enabled on;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{transcode {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{transcode {enabled off;}}"));
    EXPECT_TRUE(handler.vhost_transcode_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{transcode {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestAdded)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(handler.ingest_added_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestAdded2)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{ingest a {enabled on;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest a {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest a {enabled on;} ingest b {enabled on;}}"));
    EXPECT_TRUE(handler.ingest_added_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest a {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestRemoved)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.ingest_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestRemoved2)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled off;}}"));
    EXPECT_TRUE(handler.ingest_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestUpdated)
{
    srs_error_t err = srs_success;

    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    HELPER_EXPECT_SUCCESS(conf.parse(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg;}}"));
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg1;}}"));
    EXPECT_TRUE(handler.ingest_updated_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    HELPER_EXPECT_SUCCESS(conf.do_reload(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

