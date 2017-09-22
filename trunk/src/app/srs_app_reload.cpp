/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

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

srs_error_t ISrsReloadHandler::on_reload_utc_time()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_max_conns()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_pid()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_log_tank()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_log_level()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_log_file()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_pithy_print()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_api_enabled()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_api_disabled()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_api_crossdomain()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_api_raw_api()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_stream_enabled()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_stream_disabled()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_stream_updated()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_http_stream_crossdomain()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_http_updated()
{
    return srs_success;
}

srs_error_t ISrsReloadHandler::on_reload_vhost_http_remux_updated(string vhost)
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

srs_error_t ISrsReloadHandler::on_reload_vhost_dvr_apply(string /*vhost*/)
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

