//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_RELOAD_HPP
#define SRS_UTEST_RELOAD_HPP

/*
#include <srs_utest_reload.hpp>
*/
#include <srs_utest.hpp>

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
    bool vhost_play_reloaded;
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
    virtual srs_error_t on_reload_listen();
    virtual srs_error_t on_reload_pid();
    virtual srs_error_t on_reload_log_tank();
    virtual srs_error_t on_reload_log_level();
    virtual srs_error_t on_reload_log_file();
    virtual srs_error_t on_reload_pithy_print();
    virtual srs_error_t on_reload_http_api_enabled();
    virtual srs_error_t on_reload_http_api_disabled();
    virtual srs_error_t on_reload_http_stream_enabled();
    virtual srs_error_t on_reload_http_stream_disabled();
    virtual srs_error_t on_reload_http_stream_updated();
    virtual srs_error_t on_reload_vhost_http_updated();
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_removed(std::string vhost);
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_forward(std::string vhost);
    virtual srs_error_t on_reload_vhost_hls(std::string vhost);
    virtual srs_error_t on_reload_vhost_hds(std::string vhost);
    virtual srs_error_t on_reload_vhost_dvr(std::string vhost);
    virtual srs_error_t on_reload_vhost_transcode(std::string vhost);
    virtual srs_error_t on_reload_ingest_removed(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_added(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_updated(std::string vhost, std::string ingest_id);
};

class MockSrsReloadConfig : public MockSrsConfig
{
public:
    MockSrsReloadConfig();
    virtual ~MockSrsReloadConfig();
public:
    virtual srs_error_t do_reload(std::string buf);
};

#endif

