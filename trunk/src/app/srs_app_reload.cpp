//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_reload.hpp>

using namespace std;

#include <srs_kernel_error.hpp>

ISrsReloadHandler::ISrsReloadHandler()
{
}

ISrsReloadHandler::~ISrsReloadHandler()
{
}

srs_error_t ISrsReloadHandler::on_reload_listen()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_max_conns()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_pithy_print()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_api_raw_api()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_rtc_server()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_added(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_removed(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_play(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_forward(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_dash(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_hls(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_hds(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_dvr(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_publish(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_tcp_nodelay(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_realtime(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_chunk_size(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_transcode(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_exec(string /*vhost*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_ingest_removed(string /*vhost*/, string /*ingest_id*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_ingest_added(string /*vhost*/, string /*ingest_id*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_ingest_updated(string /*vhost*/, string /*ingest_id*/)
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_user_info()
{
    return srs_success;
}

