//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_http_static.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sstream>
using namespace std;

#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_protocol_log.hpp>

#define SRS_CONTEXT_IN_HLS "hls_ctx"

SrsHlsVirtualConn::SrsHlsVirtualConn()
{
    req = NULL;
    interrupt = false;
}

SrsHlsVirtualConn::~SrsHlsVirtualConn()
{
    srs_freep(req);
}

void SrsHlsVirtualConn::expire()
{
    interrupt = true;

    // remove statistic quickly
    SrsStatistic* stat = SrsStatistic::instance();
    stat->on_disconnect(ctx, srs_success);
}

SrsHlsStream::SrsHlsStream()
{
    _srs_hybrid->timer5s()->subscribe(this);
}

SrsHlsStream::~SrsHlsStream()
{
    _srs_hybrid->timer5s()->unsubscribe(this);

    std::map<std::string, SrsHlsVirtualConn*>::iterator it;
    for (it = map_ctx_info_.begin(); it != map_ctx_info_.end(); ++it) {
        SrsHlsVirtualConn* info = it->second;
        srs_freep(info);
    }
    map_ctx_info_.clear();
}

srs_error_t SrsHlsStream::serve_m3u8_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, ISrsFileReaderFactory* factory, string fullpath, SrsRequest* req, bool* served)
{
    srs_error_t err = srs_success;

    string ctx = r->query_get(SRS_CONTEXT_IN_HLS);

    // If HLS stream is disabled, use SrsHttpFileServer to serve HLS, which is normal file server.
    if (!_srs_config->get_hls_ctx_enabled(req->vhost)) {
        *served = false;
        return srs_success;
    }

    // Correct the app and stream by path, which is created from template.
    // @remark Be careful that the stream has extension now, might cause identify fail.
    req->stream = srs_path_basename(r->path());

    // Served by us.
    *served = true;

    // Already exists context, response with rebuilt m3u8 content.
    if (!ctx.empty() && ctx_is_exist(ctx)) {
        // If HLS stream is disabled, use SrsHttpFileServer to serve HLS, which is normal file server.
        if (!_srs_config->get_hls_ts_ctx_enabled(req->vhost)) {
            *served = false;
            return srs_success;
        }

        if (is_interrupt(ctx)) {
            srs_warn("Reject: HLS stream is EOF, ctx=%s", ctx.c_str());
            return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound, srs_fmt("HLS stream %s is EOF", ctx.c_str()));
        }

        err = serve_exists_session(w, r, factory, fullpath);
    } else {
        // Create a m3u8 in memory, contains the session id(ctx).
        err = serve_new_session(w, r, req, ctx);
    }

    // Always make the ctx alive now.
    alive(ctx, req);

    return err;
}

void SrsHlsStream::on_serve_ts_ctx(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    string ctx = r->query_get(SRS_CONTEXT_IN_HLS);
    if (ctx.empty() || !ctx_is_exist(ctx)) {
        return;
    }

    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    srs_assert(hr);

    SrsHttpConn* hc = dynamic_cast<SrsHttpConn*>(hr->connection());
    srs_assert(hc);

    ISrsKbpsDelta* delta = hc->delta();
    srs_assert(delta);

    // Only update the delta, because SrsServer will sample it. Note that SrsServer also does the stat for all clients
    // including this one, but it should be ignored because the id is not matched, and instead we use the hls_ctx as
    // session id to match the client.
    SrsStatistic::instance()->kbps_add_delta(ctx, delta);
}

srs_error_t SrsHlsStream::serve_new_session(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, SrsRequest* req, std::string& ctx)
{
    srs_error_t err = srs_success;

    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    srs_assert(hr);

    if (ctx.empty()) {
        // make sure unique
        do {
            ctx = srs_random_str(8);  // the same as cid
        } while (ctx_is_exist(ctx));
    }

    SrsContextRestore(_srs_context->get_id());
    _srs_context->set_id(SrsContextId().set_value(ctx));

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(ctx, req, NULL, SrsHlsPlay)) != srs_success) {
        return srs_error_wrap(err, "stat on client");
    }

    // We must do hook after stat, because depends on it.
    if ((err = http_hooks_on_play(req)) != srs_success) {
        return srs_error_wrap(err, "HLS: http_hooks_on_play");
    }

    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF;
    ss << "#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1" << SRS_CONSTS_LF;
    ss << hr->path() << "?" << SRS_CONTEXT_IN_HLS << "=" << ctx;
    if (!hr->query().empty() && hr->query_get(SRS_CONTEXT_IN_HLS).empty()) {
        ss << "&" << hr->query();
    }

    std::string res = ss.str();
    int length = res.length();

    w->header()->set_content_length(length);
    w->header()->set_content_type("application/vnd.apple.mpegurl");
    w->write_header(SRS_CONSTS_HTTP_OK);

    if ((err = w->write((char*)res.c_str(), length)) != srs_success) {
        return srs_error_wrap(err, "write  bytes=%d", length);
    }

    if ((err = w->final_request()) != srs_success) {
        return srs_error_wrap(err, "final request");
    }

    return err;
}

srs_error_t SrsHlsStream::serve_exists_session(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, ISrsFileReaderFactory* factory, std::string fullpath)
{
    srs_error_t err = srs_success;

    // Read m3u8 content.
    SrsFileReader* fs = factory->create_file_reader();
    SrsAutoFree(SrsFileReader, fs);

    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "open %s", fullpath.c_str());
    }

    string content;
    if ((err = srs_ioutil_read_all(fs, content)) != srs_success) {
        return srs_error_wrap(err, "read %s", fullpath.c_str());
    }

    // Rebuild the m3u8 content, make .ts with hls_ctx.
    size_t pos_ts = content.find(".ts");
    static string QUERY_PREFIX = string(".ts?") + string(SRS_CONTEXT_IN_HLS) + string("=");

    if (pos_ts != string::npos) {
        string ctx = r->query_get(SRS_CONTEXT_IN_HLS);
        string query = QUERY_PREFIX + ctx;

        size_t pos_query = content.find(".ts?");
        if (pos_query != string::npos) {
            query += "&";
            content = srs_string_replace(content, ".ts?", query);
        } else {
            content = srs_string_replace(content, ".ts", query);
        }
    }

    // Response with rebuilt content.
    w->header()->set_content_type("application/vnd.apple.mpegurl");
    w->header()->set_content_length(content.length());
    w->write_header(SRS_CONSTS_HTTP_OK);
    if (!content.empty()) {
        w->write((char*)content.data(), content.length());
    }

    if ((err = w->final_request()) != srs_success) {
        return srs_error_wrap(err, "final request");
    }

    return err;
}

bool SrsHlsStream::ctx_is_exist(std::string ctx)
{
    return (map_ctx_info_.find(ctx) != map_ctx_info_.end());
}

void SrsHlsStream::alive(std::string ctx, SrsRequest* req)
{
    std::map<std::string, SrsHlsVirtualConn*>::iterator it = map_ctx_info_.find(ctx);

    // Create new context.
    if (it == map_ctx_info_.end()) {
        SrsHlsVirtualConn* conn = new SrsHlsVirtualConn();
        conn->req = req->copy();
        conn->ctx = ctx;
        conn->request_time = srs_get_system_time();
        map_ctx_info_.insert(make_pair(ctx, conn));

        // Update the conn of stat client, which is used for receiving the event of kickoff.
        SrsStatistic* stat = SrsStatistic::instance();
        SrsStatisticClient* client = stat->find_client(ctx);
        if (client) {
            client->conn = conn;
        }

        return;
    }

    // Update alive time of context for virtual connection.
    SrsHlsVirtualConn* conn = it->second;
    if (!conn->interrupt) {
        conn->request_time = srs_get_system_time();
    }
}

srs_error_t SrsHlsStream::http_hooks_on_play(SrsRequest* req)
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_play(req->vhost);

        if (!conf) {
            return err;
        }

        hooks = conf->args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_play(url, req)) != srs_success) {
            return srs_error_wrap(err, "http on_play %s", url.c_str());
        }
    }

    return err;
}

void SrsHlsStream::http_hooks_on_stop(SrsRequest* req)
{
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_stop(req->vhost);

        if (!conf) {
            srs_info("ignore the empty http callback: on_stop");
            return;
        }

        hooks = conf->args;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_stop(url, req);
    }

    return;
}

srs_error_t SrsHlsStream::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsHlsVirtualConn*>::iterator it;
    for (it = map_ctx_info_.begin(); it != map_ctx_info_.end(); ++it) {
        string ctx = it->first;
        SrsHlsVirtualConn* info = it->second;

        srs_utime_t hls_window = _srs_config->get_hls_window(info->req->vhost);
        if (info->request_time + (2 * hls_window) < srs_get_system_time()) {
            SrsContextRestore(_srs_context->get_id());
            _srs_context->set_id(SrsContextId().set_value(ctx));

            http_hooks_on_stop(info->req);

            SrsStatistic* stat = SrsStatistic::instance();
            // TODO: FIXME: Should finger out the err.
            stat->on_disconnect(ctx, srs_success);

            map_ctx_info_.erase(it);
            srs_freep(info);

            break;
        }
    }

    return err;
}

bool SrsHlsStream::is_interrupt(std::string id) {
    std::map<std::string, SrsHlsVirtualConn*>::iterator it = map_ctx_info_.find(id);
    if (it != map_ctx_info_.end()) {
        return it->second->interrupt;
    }
    return false;
}

SrsVodStream::SrsVodStream(string root_dir) : SrsHttpFileServer(root_dir)
{
}

SrsVodStream::~SrsVodStream()
{
}

srs_error_t SrsVodStream::serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int64_t offset)
{
    srs_error_t err = srs_success;
    
    SrsFileReader* fs = fs_factory->create_file_reader();
    SrsAutoFree(SrsFileReader, fs);
    
    // open flv file
    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "open file");
    }
    
    if (offset > fs->filesize()) {
        return srs_error_new(ERROR_HTTP_REMUX_OFFSET_OVERFLOW, "http flv streaming %s overflow. size=%" PRId64 ", offset=%" PRId64,
            fullpath.c_str(), fs->filesize(), offset);
    }
    
    SrsFlvVodStreamDecoder ffd;
    
    // open fast decoder
    if ((err = ffd.initialize(fs)) != srs_success) {
        return srs_error_wrap(err, "init ffd");
    }
    
    // save header, send later.
    char flv_header[13];
    
    // send flv header
    if ((err = ffd.read_header_ext(flv_header)) != srs_success) {
        return srs_error_wrap(err, "ffd read header");
    }
    
    // save sequence header, send later
    char* sh_data = NULL;
    int sh_size = 0;
    
    if (true) {
        // send sequence header
        int64_t start = 0;
        if ((err = ffd.read_sequence_header_summary(&start, &sh_size)) != srs_success) {
            return srs_error_wrap(err, "ffd read sps");
        }
        if (sh_size <= 0) {
            return srs_error_new(ERROR_HTTP_REMUX_SEQUENCE_HEADER, "no sequence, size=%d", sh_size);
        }
    }
    sh_data = new char[sh_size];
    SrsAutoFreeA(char, sh_data);
    if ((err = fs->read(sh_data, sh_size, NULL)) != srs_success) {
        return srs_error_wrap(err, "fs read");
    }
    
    // seek to data offset
    int64_t left = fs->filesize() - offset;
    
    // write http header for ts.
    w->header()->set_content_length(sizeof(flv_header) + sh_size + left);
    w->header()->set_content_type("video/x-flv");
    w->write_header(SRS_CONSTS_HTTP_OK);
    
    // write flv header and sequence header.
    if ((err = w->write(flv_header, sizeof(flv_header))) != srs_success) {
        return srs_error_wrap(err, "write flv header");
    }
    if (sh_size > 0 && (err = w->write(sh_data, sh_size)) != srs_success) {
        return srs_error_wrap(err, "write sequence");
    }
    
    // write body.
    if ((err = ffd.seek2(offset)) != srs_success) {
        return srs_error_wrap(err, "ffd seek");
    }
    
    // send data
    if ((err = copy(w, fs, r, left)) != srs_success) {
        return srs_error_wrap(err, "read flv=%s size=%" PRId64, fullpath.c_str(), left);
    }
    
    return err;
}

srs_error_t SrsVodStream::serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int64_t start, int64_t end)
{
    srs_error_t err = srs_success;
    
    srs_assert(start >= 0);
    srs_assert(end == -1 || end >= 0);
    
    SrsFileReader* fs = fs_factory->create_file_reader();
    SrsAutoFree(SrsFileReader, fs);
    
    // open flv file
    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "fs open");
    }
    
    // parse -1 to whole file.
    if (end == -1) {
        end = fs->filesize() - 1;
    }
    
    if (end > fs->filesize() || start > end || end < 0) {
        return srs_error_new(ERROR_HTTP_REMUX_OFFSET_OVERFLOW, "http mp4 streaming %s overflow. size=%" PRId64 ", offset=%d",
            fullpath.c_str(), fs->filesize(), start);
    }
    
    // seek to data offset, [start, end] for range.
    int64_t left = end - start + 1;
    
    // write http header for ts.
    w->header()->set_content_length(left);
    w->header()->set_content_type("video/mp4");
    w->write_header(SRS_CONSTS_HTTP_PartialContent);
    
    // response the content range header.
    // https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Range_requests
    std::stringstream content_range;
    content_range << "bytes " << start << "-" << end << "/" << fs->filesize();
    w->header()->set("Content-Range", content_range.str());
    
    // write body.
    fs->seek2(start);
    
    // send data
    if ((err = copy(w, fs, r, left)) != srs_success) {
        return srs_error_wrap(err, "read mp4=%s size=%" PRId64, fullpath.c_str(), left);
    }
    
    return err;
}

srs_error_t SrsVodStream::serve_m3u8_ctx(ISrsHttpResponseWriter * w, ISrsHttpMessage * r, std::string fullpath)
{
    srs_error_t err = srs_success;

    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    srs_assert(hr);

    SrsRequest* req = hr->to_request(hr->host())->as_http();
    SrsAutoFree(SrsRequest, req);

    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(req->vhost);
    if (parsed_vhost) {
        req->vhost = parsed_vhost->arg0();
    }

    // Try to serve by HLS streaming.
    bool served = false;
    if ((err = hls_.serve_m3u8_ctx(w, r, fs_factory, fullpath, req, &served)) != srs_success) {
        return srs_error_wrap(err, "hls ctx");
    }

    // Serve by default HLS handler.
    if (!served) {
        return SrsHttpFileServer::serve_m3u8_ctx(w, r, fullpath);
    }

    return err;
}

srs_error_t SrsVodStream::serve_ts_ctx(ISrsHttpResponseWriter * w, ISrsHttpMessage * r, std::string fullpath)
{
    srs_error_t err = srs_success;

    // SrsServer also stat all HTTP connections including this one, but it should be ignored because the id is not
    // matched to any exists client. And we will do stat for the HLS streaming by session in hls_ctx.
    SrsHttpMessage* hr = dynamic_cast<SrsHttpMessage*>(r);
    SrsHttpConn* hc = dynamic_cast<SrsHttpConn*>(hr->connection());
    SrsHttpxConn* hxc = dynamic_cast<SrsHttpxConn*>(hc->handler());

    // Note that we never enable the stat for the HTTP connection, because we always stat the pseudo HLS streaming
    // session identified by hls_ctx, which served by an SrsHlsStream object.
    hxc->set_enable_stat(false);

    // Serve by default HLS handler.
    err = SrsHttpFileServer::serve_ts_ctx(w, r, fullpath);

    // Notify the HLS to stat the ts after serving.
    hls_.on_serve_ts_ctx(w, r);

    return err;
}

SrsHttpStaticServer::SrsHttpStaticServer(SrsServer* svr)
{
    server = svr;
    _srs_config->subscribe(this);
}

SrsHttpStaticServer::~SrsHttpStaticServer()
{
    _srs_config->unsubscribe(this);
}

srs_error_t SrsHttpStaticServer::initialize()
{
    srs_error_t err = srs_success;
    
    bool default_root_exists = false;
    
    // http static file and flv vod stream mount for each vhost.
    SrsConfDirective* root = _srs_config->get_root();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        string pmount;
        string vhost = conf->arg0();
        if ((err = mount_vhost(vhost, pmount)) != srs_success) {
            return srs_error_wrap(err, "mount vhost");
        }
        
        if (pmount == "/") {
            default_root_exists = true;
            std::string dir = _srs_config->get_vhost_http_dir(vhost);
            srs_warn("http: root mount to %s", dir.c_str());
        }
    }
    
    if (!default_root_exists) {
        // add root
        std::string dir = _srs_config->get_http_stream_dir();
        if ((err = mux.handle("/", new SrsVodStream(dir))) != srs_success) {
            return srs_error_wrap(err, "mount root dir=%s", dir.c_str());
        }
        srs_trace("http: root mount to %s", dir.c_str());
    }
    
    return err;
}

srs_error_t SrsHttpStaticServer::mount_vhost(string vhost, string& pmount)
{
    srs_error_t err = srs_success;
    
    // when vhost disabled, ignore.
    if (!_srs_config->get_vhost_enabled(vhost)) {
        return err;
    }
    
    // when vhost http_static disabled, ignore.
    if (!_srs_config->get_vhost_http_enabled(vhost)) {
        return err;
    }
    
    std::string mount = _srs_config->get_vhost_http_mount(vhost);
    std::string dir = _srs_config->get_vhost_http_dir(vhost);
    
    // replace the vhost variable
    mount = srs_string_replace(mount, "[vhost]", vhost);
    dir = srs_string_replace(dir, "[vhost]", vhost);
    
    // remove the default vhost mount
    mount = srs_string_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST"/", "/");
    
    // the dir mount must always ends with "/"
    if (mount != "/" && !srs_string_ends_with(mount, "/")) {
        mount += "/";
    }
    
    // mount the http of vhost.
    if ((err = mux.handle(mount, new SrsVodStream(dir))) != srs_success) {
        return srs_error_wrap(err, "mux handle");
    }
    srs_trace("http: vhost=%s mount to %s at %s", vhost.c_str(), mount.c_str(), dir.c_str());
    
    pmount = mount;
    
    return err;
}

srs_error_t SrsHttpStaticServer::on_reload_vhost_added(string vhost)
{
    srs_error_t err = srs_success;
    
    string pmount;
    if ((err = mount_vhost(vhost, pmount)) != srs_success) {
        return srs_error_wrap(err, "mount vhost");
    }
    
    return err;
}

