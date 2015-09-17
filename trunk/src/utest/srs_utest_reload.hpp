/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#ifndef SRS_UTEST_RELOAD_HPP
#define SRS_UTEST_RELOAD_HPP

/*
#include <srs_utest_reload.hpp>
*/
#include <srs_core.hpp>

#include <srs_utest_config.hpp>
#include <srs_app_reload.hpp>

class MockReloadHandler : public ISrsReloadHandler
{
public:
    bool listen_reloaded;
    bool pid_reloaded;
    bool log_tank_reloaded;
    bool log_level_reloaded;
    bool log_file_reloaded;
    bool pithy_print_reloaded;
    bool http_api_enabled_reloaded;
    bool http_api_disabled_reloaded;
    bool http_stream_enabled_reloaded;
    bool http_stream_disabled_reloaded;
    bool http_stream_updated_reloaded;
    bool vhost_http_updated_reloaded;
    bool vhost_added_reloaded;
    bool vhost_removed_reloaded;
    bool vhost_atc_reloaded;
    bool vhost_gop_cache_reloaded;
    bool vhost_queue_length_reloaded;
    bool vhost_time_jitter_reloaded;
    bool vhost_forward_reloaded;
    bool vhost_hls_reloaded;
    bool vhost_dvr_reloaded;
    bool vhost_transcode_reloaded;
    bool ingest_removed_reloaded;
    bool ingest_added_reloaded;
    bool ingest_updated_reloaded;
public:
    MockReloadHandler();
    virtual ~MockReloadHandler();
public:
    virtual void reset();
    virtual bool all_false();
    virtual bool all_true();
    virtual int count_total();
    virtual int count_true();
    virtual int count_false();
public:
    virtual int on_reload_listen();
    virtual int on_reload_pid();
    virtual int on_reload_log_tank();
    virtual int on_reload_log_level();
    virtual int on_reload_log_file();
    virtual int on_reload_pithy_print();
    virtual int on_reload_http_api_enabled();
    virtual int on_reload_http_api_disabled();
    virtual int on_reload_http_stream_enabled();
    virtual int on_reload_http_stream_disabled();
    virtual int on_reload_http_stream_updated();
    virtual int on_reload_vhost_http_updated();
    virtual int on_reload_vhost_added(std::string vhost);
    virtual int on_reload_vhost_removed(std::string vhost);
    virtual int on_reload_vhost_play(std::string vhost);
    virtual int on_reload_vhost_forward(std::string vhost);
    virtual int on_reload_vhost_hls(std::string vhost);
    virtual int on_reload_vhost_hds(std::string vhost);
    virtual int on_reload_vhost_dvr(std::string vhost);
    virtual int on_reload_vhost_transcode(std::string vhost);
    virtual int on_reload_ingest_removed(std::string vhost, std::string ingest_id);
    virtual int on_reload_ingest_added(std::string vhost, std::string ingest_id);
    virtual int on_reload_ingest_updated(std::string vhost, std::string ingest_id);
};

class MockSrsReloadConfig : public MockSrsConfig
{
public:
    MockSrsReloadConfig();
    virtual ~MockSrsReloadConfig();
public:
    virtual int do_reload(std::string buf);
};

#endif

