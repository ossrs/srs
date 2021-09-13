//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RELOAD_HPP
#define SRS_APP_RELOAD_HPP

#include <srs_core.hpp>

#include <string>

// The handler for config reload.
// When reload callback, the config is updated yet.
//
// Features not support reload,
// @see: https://github.com/ossrs/srs/wiki/v1_CN_Reload#notsupportedfeatures
class ISrsReloadHandler
{
public:
    ISrsReloadHandler();
    virtual ~ISrsReloadHandler();
public:
    virtual srs_error_t on_reload_utc_time();
    virtual srs_error_t on_reload_max_conns();
    virtual srs_error_t on_reload_listen();
    virtual srs_error_t on_reload_pid();
    virtual srs_error_t on_reload_log_tank();
    virtual srs_error_t on_reload_log_level();
    virtual srs_error_t on_reload_log_file();
    virtual srs_error_t on_reload_pithy_print();
    virtual srs_error_t on_reload_http_api_enabled();
    virtual srs_error_t on_reload_http_api_disabled();
    virtual srs_error_t on_reload_https_api_enabled();
    virtual srs_error_t on_reload_https_api_disabled();
    virtual srs_error_t on_reload_http_api_crossdomain();
    virtual srs_error_t on_reload_http_api_raw_api();
    virtual srs_error_t on_reload_http_stream_enabled();
    virtual srs_error_t on_reload_http_stream_disabled();
    virtual srs_error_t on_reload_http_stream_updated();
    virtual srs_error_t on_reload_http_stream_crossdomain();
    virtual srs_error_t on_reload_rtc_server();
public:
    // TODO: FIXME: should rename to http_static
    virtual srs_error_t on_reload_vhost_http_updated();
    virtual srs_error_t on_reload_vhost_http_remux_updated(std::string vhost);
    virtual srs_error_t on_reload_vhost_added(std::string vhost);
    virtual srs_error_t on_reload_vhost_removed(std::string vhost);
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_forward(std::string vhost);
    virtual srs_error_t on_reload_vhost_dash(std::string vhost);
    virtual srs_error_t on_reload_vhost_hls(std::string vhost);
    virtual srs_error_t on_reload_vhost_hds(std::string vhost);
    virtual srs_error_t on_reload_vhost_dvr(std::string vhost);
    virtual srs_error_t on_reload_vhost_publish(std::string vhost);
    virtual srs_error_t on_reload_vhost_tcp_nodelay(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
    virtual srs_error_t on_reload_vhost_chunk_size(std::string vhost);
    virtual srs_error_t on_reload_vhost_transcode(std::string vhost);
    virtual srs_error_t on_reload_vhost_exec(std::string vhost);
    virtual srs_error_t on_reload_ingest_removed(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_added(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_ingest_updated(std::string vhost, std::string ingest_id);
    virtual srs_error_t on_reload_user_info();
};

#endif
