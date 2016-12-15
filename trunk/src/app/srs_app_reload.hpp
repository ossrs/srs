/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#ifndef SRS_APP_RELOAD_HPP
#define SRS_APP_RELOAD_HPP

/*
#include <srs_app_reload.hpp>
*/
#include <srs_core.hpp>

#include <string>

/**
* the handler for config reload.
* when reload callback, the config is updated yet.
* 
* features not support reload, 
* @see: https://github.com/ossrs/srs/wiki/v1_CN_Reload#notsupportedfeatures
*/
class ISrsReloadHandler
{
public:
    ISrsReloadHandler();
    virtual ~ISrsReloadHandler();
public:
    virtual int on_reload_utc_time();
    virtual int on_reload_max_conns();
    virtual int on_reload_listen();
    virtual int on_reload_pid();
    virtual int on_reload_log_tank();
    virtual int on_reload_log_level();
    virtual int on_reload_log_file();
    virtual int on_reload_pithy_print();
    virtual int on_reload_http_api_enabled();
    virtual int on_reload_http_api_disabled();
    virtual int on_reload_http_api_crossdomain();
    virtual int on_reload_http_api_raw_api();
    virtual int on_reload_http_stream_enabled();
    virtual int on_reload_http_stream_disabled();
    virtual int on_reload_http_stream_updated();
    virtual int on_reload_http_stream_crossdomain();
public:
    // TODO: FIXME: should rename to http_static
    virtual int on_reload_vhost_http_updated();
    virtual int on_reload_vhost_http_remux_updated(std::string vhost);
    virtual int on_reload_vhost_added(std::string vhost);
    virtual int on_reload_vhost_removed(std::string vhost);
    virtual int on_reload_vhost_play(std::string vhost);
    virtual int on_reload_vhost_forward(std::string vhost);
    virtual int on_reload_vhost_hls(std::string vhost);
    virtual int on_reload_vhost_hds(std::string vhost);
    virtual int on_reload_vhost_dvr(std::string vhost);
    virtual int on_reload_vhost_dvr_apply(std::string vhost);
    virtual int on_reload_vhost_publish(std::string vhost);
    virtual int on_reload_vhost_tcp_nodelay(std::string vhost);
    virtual int on_reload_vhost_realtime(std::string vhost);
    virtual int on_reload_vhost_chunk_size(std::string vhost);
    virtual int on_reload_vhost_transcode(std::string vhost);
    virtual int on_reload_vhost_exec(std::string vhost);
    virtual int on_reload_ingest_removed(std::string vhost, std::string ingest_id);
    virtual int on_reload_ingest_added(std::string vhost, std::string ingest_id);
    virtual int on_reload_ingest_updated(std::string vhost, std::string ingest_id);
    virtual int on_reload_user_info();
};

#endif
