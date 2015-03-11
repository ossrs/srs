/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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
    pid_reloaded = false;
    log_tank_reloaded = false;
    log_level_reloaded = false;
    log_file_reloaded = false;
    pithy_print_reloaded = false;
    http_api_enabled_reloaded = false;
    http_api_disabled_reloaded = false;
    http_stream_enabled_reloaded = false;
    http_stream_disabled_reloaded = false;
    http_stream_updated_reloaded = false;
    vhost_http_updated_reloaded = false;
    vhost_added_reloaded = false;
    vhost_removed_reloaded = false;
    vhost_atc_reloaded = false;
    vhost_gop_cache_reloaded = false;
    vhost_queue_length_reloaded = false;
    vhost_time_jitter_reloaded = false;
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
    int count_true  = 0;
    
    if (listen_reloaded) count_true++;
    if (pid_reloaded) count_true++;
    if (log_tank_reloaded) count_true++;
    if (log_level_reloaded) count_true++;
    if (log_file_reloaded) count_true++;
    if (pithy_print_reloaded) count_true++;
    if (http_api_enabled_reloaded) count_true++;
    if (http_api_disabled_reloaded) count_true++;
    if (http_stream_enabled_reloaded) count_true++;
    if (http_stream_disabled_reloaded) count_true++;
    if (http_stream_updated_reloaded) count_true++;
    if (vhost_http_updated_reloaded) count_true++;
    if (vhost_added_reloaded) count_true++;
    if (vhost_removed_reloaded) count_true++;
    if (vhost_atc_reloaded) count_true++;
    if (vhost_gop_cache_reloaded) count_true++;
    if (vhost_queue_length_reloaded) count_true++;
    if (vhost_time_jitter_reloaded) count_true++;
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
    int count_false  = 0;
    
    if (!listen_reloaded) count_false++;
    if (!pid_reloaded) count_false++;
    if (!log_tank_reloaded) count_false++;
    if (!log_level_reloaded) count_false++;
    if (!log_file_reloaded) count_false++;
    if (!pithy_print_reloaded) count_false++;
    if (!http_api_enabled_reloaded) count_false++;
    if (!http_api_disabled_reloaded) count_false++;
    if (!http_stream_enabled_reloaded) count_false++;
    if (!http_stream_disabled_reloaded) count_false++;
    if (!http_stream_updated_reloaded) count_false++;
    if (!vhost_http_updated_reloaded) count_false++;
    if (!vhost_added_reloaded) count_false++;
    if (!vhost_removed_reloaded) count_false++;
    if (!vhost_atc_reloaded) count_false++;
    if (!vhost_gop_cache_reloaded) count_false++;
    if (!vhost_queue_length_reloaded) count_false++;
    if (!vhost_time_jitter_reloaded) count_false++;
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

int MockReloadHandler::on_reload_listen()
{
    listen_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_pid()
{
    pid_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_log_tank()
{
    log_tank_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_log_level()
{
    log_level_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_log_file()
{
    log_file_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_pithy_print()
{
    pithy_print_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_http_api_enabled()
{
    http_api_enabled_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_http_api_disabled()
{
    http_api_disabled_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_http_stream_enabled()
{
    http_stream_enabled_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_http_stream_disabled()
{
    http_stream_disabled_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_http_stream_updated()
{
    http_stream_updated_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_http_updated()
{
    vhost_http_updated_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_added(string /*vhost*/)
{
    vhost_added_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_removed(string /*vhost*/)
{
    vhost_removed_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_atc(string /*vhost*/)
{
    vhost_atc_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_gop_cache(string /*vhost*/)
{
    vhost_gop_cache_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_queue_length(string /*vhost*/)
{
    vhost_queue_length_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_time_jitter(string /*vhost*/)
{
    vhost_time_jitter_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_forward(string /*vhost*/)
{
    vhost_forward_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_hls(string /*vhost*/)
{
    vhost_hls_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_dvr(string /*vhost*/)
{
    vhost_dvr_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_vhost_transcode(string /*vhost*/)
{
    vhost_transcode_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_ingest_removed(string /*vhost*/, string /*ingest_id*/)
{
    ingest_removed_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_ingest_added(string /*vhost*/, string /*ingest_id*/)
{
    ingest_added_reloaded = true;
    return ERROR_SUCCESS;
}

int MockReloadHandler::on_reload_ingest_updated(string /*vhost*/, string /*ingest_id*/)
{
    ingest_updated_reloaded = true;
    return ERROR_SUCCESS;
}

MockSrsReloadConfig::MockSrsReloadConfig()
{
}

MockSrsReloadConfig::~MockSrsReloadConfig()
{
}

int MockSrsReloadConfig::reload(string buf)
{
    int ret = ERROR_SUCCESS;
    
    MockSrsReloadConfig conf;
    if ((ret = conf.parse(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return reload_conf(&conf);
}

#ifdef ENABLE_UTEST_RELOAD

VOID TEST(ConfigReloadTest, ReloadEmpty)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_FALSE(ERROR_SUCCESS == conf.parse(""));
    EXPECT_FALSE(ERROR_SUCCESS == conf.reload(""));
    EXPECT_TRUE(handler.all_false());
}

VOID TEST(ConfigReloadTest, ReloadListen)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse("listen 1935;"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1935;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1936;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1936;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1936 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1935 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload("listen 1935;"));
    EXPECT_TRUE(handler.listen_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadPid)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"pid srs.pid;"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"pid srs.pid;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"pid srs1.pid;"));
    EXPECT_TRUE(handler.pid_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"pid srs.pid;"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadLogTank)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_tank console;"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_tank console;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_tank file;"));
    EXPECT_TRUE(handler.log_tank_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_tank console;"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadLogLevel)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_level trace;"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_level trace;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_level warn;"));
    EXPECT_TRUE(handler.log_level_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_level trace;"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadLogFile)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_file srs.log;"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_file srs.log;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_file srs1.log;"));
    EXPECT_TRUE(handler.log_file_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"srs_log_file srs.log;"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadPithyPrint)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"pithy_print_ms 1000;"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"pithy_print_ms 1000;"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"pithy_print_ms 2000;"));
    EXPECT_TRUE(handler.pithy_print_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"pithy_print_ms 1000;"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadHttpApiEnabled)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_api {enabled off;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_api {enabled off;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_api {enabled on;}"));
    EXPECT_TRUE(handler.http_api_enabled_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_api {enabled off;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadHttpApiDisabled)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_api {enabled on;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_api {enabled on;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_api {enabled off;}"));
    EXPECT_TRUE(handler.http_api_disabled_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_api {enabled on;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadHttpStreamEnabled)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_stream {enabled off;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled off;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled on;}"));
    EXPECT_TRUE(handler.http_stream_enabled_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled off;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadHttpStreamDisabled)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_stream {enabled on;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled on;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled off;}"));
    EXPECT_TRUE(handler.http_stream_disabled_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled on;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadHttpStreamUpdated)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_stream {enabled on; listen 8080;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled on; listen 8080;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled on; listen 8000;}"));
    EXPECT_TRUE(handler.http_stream_updated_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"http_stream {enabled on; listen 8080;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostHttpUpdated)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost http.srs.com {http {enabled on;mount /hls;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost http.srs.com {http {enabled on;mount /hls;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost http.srs.com {http {enabled on;mount /hls1;}}"));
    EXPECT_TRUE(handler.vhost_http_updated_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost http.srs.com {http {enabled on;mount /hls;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostAdded)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_TRUE(handler.vhost_added_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostRemoved)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{enabled off;}"));
    EXPECT_TRUE(handler.vhost_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostRemoved2)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.vhost_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{} vhost b{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostAtc)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{atc off;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{atc off;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{atc on;}"));
    EXPECT_TRUE(handler.vhost_atc_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{atc off;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostGopCache)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{gop_cache off;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{gop_cache off;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{gop_cache on;}"));
    EXPECT_TRUE(handler.vhost_gop_cache_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{gop_cache off;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostQueueLength)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{queue_length 10;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{queue_length 10;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{queue_length 20;}"));
    EXPECT_TRUE(handler.vhost_queue_length_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{queue_length 10;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostTimeJitter)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{time_jitter full;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{time_jitter full;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{time_jitter zero;}"));
    EXPECT_TRUE(handler.vhost_time_jitter_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{time_jitter full;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostForward)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1936;}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1936;}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1937;}"));
    EXPECT_TRUE(handler.vhost_forward_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{forward 127.0.0.1:1936;}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostHls)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{hls {enabled on;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{hls {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{hls {enabled off;}}"));
    EXPECT_TRUE(handler.vhost_hls_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{hls {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostDvr)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{dvr {enabled on;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{dvr {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{dvr {enabled off;}}"));
    EXPECT_TRUE(handler.vhost_dvr_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{dvr {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostTranscode)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{transcode {enabled on;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{transcode {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{transcode {enabled off;}}"));
    EXPECT_TRUE(handler.vhost_transcode_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{transcode {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestAdded)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(handler.ingest_added_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestAdded2)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{ingest a {enabled on;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest a {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest a {enabled on;} ingest b {enabled on;}}"));
    EXPECT_TRUE(handler.ingest_added_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest a {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestRemoved)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{}"));
    EXPECT_TRUE(handler.ingest_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestRemoved2)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled off;}}"));
    EXPECT_TRUE(handler.ingest_removed_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

VOID TEST(ConfigReloadTest, ReloadVhostIngestUpdated)
{
    MockReloadHandler handler;
    MockSrsReloadConfig conf;
    
    conf.subscribe(&handler);
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg;}}"));
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg;}}"));
    EXPECT_TRUE(handler.all_false());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg1;}}"));
    EXPECT_TRUE(handler.ingest_updated_reloaded);
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
    
    EXPECT_TRUE(ERROR_SUCCESS == conf.reload(_MIN_OK_CONF"vhost a{ingest {enabled on;ffmpeg ffmpeg;}}"));
    EXPECT_EQ(1, handler.count_true());
    handler.reset();
}

#endif

