//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_http_static.hpp>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sstream>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>

#define SRS_CONTEXT_IN_HLS "hls_ctx"

SrsHlsVirtualConn::SrsHlsVirtualConn()
{
    req_ = NULL;
    interrupt_ = false;

    stat_ = _srs_stat;
}

SrsHlsVirtualConn::~SrsHlsVirtualConn()
{
    srs_freep(req_);
}

void SrsHlsVirtualConn::expire()
{
    interrupt_ = true;

    // remove statistic quickly
    stat_->on_disconnect(ctx_, srs_success);
}

SrsHlsStream::SrsHlsStream()
{
    security_ = new SrsSecurity();

    config_ = _srs_config;
    stat_ = _srs_stat;
    hooks_ = _srs_hooks;
    shared_timer_ = _srs_shared_timer;
    rand_ = new SrsRand();
}

void SrsHlsStream::assemble()
{
    shared_timer_->timer5s()->subscribe(this);
}

SrsHlsStream::~SrsHlsStream()
{
    if (shared_timer_) {
        shared_timer_->timer5s()->unsubscribe(this);
    }

    std::map<std::string, SrsHlsVirtualConn *>::iterator it;
    for (it = map_ctx_info_.begin(); it != map_ctx_info_.end(); ++it) {
        SrsHlsVirtualConn *info = it->second;
        srs_freep(info);
    }
    map_ctx_info_.clear();
    srs_freep(security_);
    srs_freep(rand_);

    config_ = NULL;
    stat_ = NULL;
    hooks_ = NULL;
    shared_timer_ = NULL;
}

srs_error_t SrsHlsStream::serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, ISrsFileReaderFactory *factory, string fullpath, ISrsRequest *req, bool *served)
{
    srs_error_t err = srs_success;

    string ctx = r->query_get(SRS_CONTEXT_IN_HLS);

    // If HLS stream is disabled, use SrsHttpFileServer to serve HLS, which is normal file server.
    if (!config_->get_hls_ctx_enabled(req->vhost_)) {
        *served = false;
        return srs_success;
    }

    // The stream of req has no extension, as SrsHttpMessage::to_request() removed it, so the hooks
    // and the API name the stream of an HLS session as they do for every other player.

    // Served by us.
    *served = true;

    // A ctx names the session of one stream. The ctx of a session of another stream starts a new session
    // of this stream with a new ctx, as a request with no ctx does, so the security check and on_play of
    // this stream run, and the session of the ctx goes on as before.
    std::map<std::string, SrsHlsVirtualConn *>::iterator it = map_ctx_info_.find(ctx);
    if (it != map_ctx_info_.end()) {
        SrsHlsVirtualConn *conn = it->second;
        if (conn->req_->get_stream_url() != req->get_stream_url()) {
            ctx = "";
        }
    }

    // Already exists context, response with rebuilt m3u8 content.
    if (!ctx.empty() && ctx_is_exist(ctx)) {
        if (is_interrupt(ctx)) {
            srs_warn("Reject: HLS stream is EOF, ctx=%s", ctx.c_str());
            return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound, srs_fmt_sprintf("HLS stream %s is EOF", ctx.c_str()));
        }

        // Without ctx in the ts urls, use SrsHttpFileServer to serve the m3u8 as a normal file, but
        // the refresh still keeps the session alive.
        if (!config_->get_hls_ts_ctx_enabled(req->vhost_)) {
            alive(ctx, req);
            *served = false;
            return srs_success;
        }

        err = serve_exists_session(w, r, factory, fullpath);
    } else {
        // Create a m3u8 in memory, contains the session id(ctx).
        err = serve_new_session(w, r, req, ctx);

        // Never keep the ctx of a refused viewer alive, because the client may choose the ctx, and a
        // retry with it would be served as an existing session, which skips the security check and
        // the on_play hook. The viewer was added to statistic before the checks and has no session
        // to expire, so remove it now.
        if (err != srs_success) {
            stat_->on_disconnect(ctx, err);
            srs_http_stream_serve_error(w, err);
            return err;
        }
    }

    // Always make the ctx alive now.
    alive(ctx, req);

    // This is the root of HLS streaming, so it owns telling the viewer why it was refused. Never
    // respond in the branch that raised the error, see srs_http_stream_serve_error.
    srs_http_stream_serve_error(w, err);

    return err;
}

void SrsHlsStream::on_serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    string ctx = r->query_get(SRS_CONTEXT_IN_HLS);
    if (ctx.empty() || !ctx_is_exist(ctx)) {
        return;
    }

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    srs_assert(hr);

    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(hr->connection());
    srs_assert(hc);

    ISrsKbpsDelta *delta = hc->delta();
    srs_assert(delta);

    // Only update the delta, because SrsServer will sample it. Note that SrsServer also does the stat for all clients
    // including this one, but it should be ignored because the id is not matched, and instead we use the hls_ctx as
    // session id to match the client.
    stat_->kbps_add_delta(ctx, delta);
}

srs_error_t SrsHlsStream::serve_new_session(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, ISrsRequest *req, std::string &ctx)
{
    srs_error_t err = srs_success;

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    srs_assert(hr);

    if (ctx.empty()) {
        // make sure unique
        do {
            ctx = rand_->gen_str(8); // the same as cid
        } while (ctx_is_exist(ctx));
    }

    SrsContextRestore(_srs_context->get_id());
    _srs_context->set_id(SrsContextId().set_value(ctx));

    // We must do stat the client before hooks, because hooks depends on it.
    if ((err = stat_->on_client(ctx, req, NULL, SrsHlsPlay)) != srs_success) {
        return srs_error_wrap(err, "stat on client");
    }

    if ((err = security_->check(SrsHlsPlay, req->ip_, req)) != srs_success) {
        return srs_error_wrap(err, "HLS: security check");
    }

    // We must do hook after stat, because depends on it. Transform to ERROR_SYSTEM_AUTH, so that
    // the root error handler in serve_m3u8_ctx() answers 401 rather than 500; a refused viewer is
    // not a server fault.
    if ((err = http_hooks_on_play(req)) != srs_success) {
        return srs_error_transform(ERROR_SYSTEM_AUTH, err, "HLS: http_hooks_on_play");
    }

    // Determine the media playlist URL path in master playlist based on configuration.
    // When hls_master_m3u8_path_relative is on, use relative path (just filename) for better
    // compatibility with reverse proxies that do path rewriting.
    // For example, convert "/live/livestream.m3u8" to "livestream.m3u8"
    // When off (default), use absolute path for backward compatibility.
    std::string media_playlist_url = hr->path();
    if (config_->get_hls_master_m3u8_path_relative(req->vhost_)) {
        SrsPath path;
        media_playlist_url = path.filepath_base(hr->path());
    }

    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF;
    ss << "#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1" << SRS_CONSTS_LF;
    ss << media_playlist_url << "?" << SRS_CONTEXT_IN_HLS << "=" << ctx;
    if (!hr->query().empty() && hr->query_get(SRS_CONTEXT_IN_HLS).empty()) {
        ss << "&" << hr->query();
    }
    ss << SRS_CONSTS_LF;

    std::string res = ss.str();
    int length = res.length();

    w->header()->set_content_length(length);
    w->header()->set_content_type("application/vnd.apple.mpegurl");
    w->write_header(SRS_CONSTS_HTTP_OK);

    if ((err = w->write((char *)res.c_str(), length)) != srs_success) {
        return srs_error_wrap(err, "write  bytes=%d", length);
    }

    if ((err = w->final_request()) != srs_success) {
        return srs_error_wrap(err, "final request");
    }

    return err;
}

srs_error_t SrsHlsStream::serve_exists_session(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, ISrsFileReaderFactory *factory, std::string fullpath)
{
    srs_error_t err = srs_success;

    // Read m3u8 content.
    SrsUniquePtr<SrsFileReader> fs(factory->create_file_reader());

    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "open %s", fullpath.c_str());
    }

    string content;
    if ((err = srs_io_readall(fs.get(), content)) != srs_success) {
        return srs_error_wrap(err, "read %s", fullpath.c_str());
    }

    // Rebuild the m3u8 content, make .ts with hls_ctx.
    static string QUERY_PREFIX = string(".ts?") + string(SRS_CONTEXT_IN_HLS) + string("=");
    static string M4S_QUERY_PREFIX = string(".m4s?") + string(SRS_CONTEXT_IN_HLS) + string("=");
    static string INIT_MP4_QUERY_PREFIX = string("init.mp4?") + string(SRS_CONTEXT_IN_HLS) + string("=");

    if (content.find(".ts") != string::npos) {
        string ctx = r->query_get(SRS_CONTEXT_IN_HLS);
        string query = QUERY_PREFIX + ctx;

        size_t pos_query = content.find(".ts?");
        if (pos_query != string::npos) {
            query += "&";
            content = srs_strings_replace(content, ".ts?", query);
        } else {
            content = srs_strings_replace(content, ".ts", query);
        }
    } else if (content.find(".m4s") != string::npos) {
        string ctx = r->query_get(SRS_CONTEXT_IN_HLS);
        string query = M4S_QUERY_PREFIX + ctx;

        size_t pos_query = content.find(".m4s?");
        if (pos_query != string::npos) {
            query += "&";
            content = srs_strings_replace(content, ".m4s?", query);
        } else {
            content = srs_strings_replace(content, ".m4s", query);
        }
    } else if (content.find("init.mp4") != string::npos) {
        string ctx = r->query_get(SRS_CONTEXT_IN_HLS);
        string query = INIT_MP4_QUERY_PREFIX + ctx;

        size_t pos_query = content.find("init.mp4?");
        if (pos_query != string::npos) {
            query += "&";
            content = srs_strings_replace(content, "init.mp4?", query);
        } else {
            content = srs_strings_replace(content, "init.mp4", query);
        }
    }

    // Response with rebuilt content.
    w->header()->set_content_type("application/vnd.apple.mpegurl");
    w->header()->set_content_length(content.length());
    w->write_header(SRS_CONSTS_HTTP_OK);
    if (!content.empty()) {
        w->write((char *)content.data(), content.length());
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

void SrsHlsStream::alive(std::string ctx, ISrsRequest *req)
{
    std::map<std::string, SrsHlsVirtualConn *>::iterator it = map_ctx_info_.find(ctx);

    // Create new context.
    if (it == map_ctx_info_.end()) {
        SrsHlsVirtualConn *conn = new SrsHlsVirtualConn();
        conn->req_ = req->copy();
        conn->ctx_ = ctx;
        conn->request_time_ = srs_time_now_cached();
        map_ctx_info_.insert(make_pair(ctx, conn));

        // Update the conn of stat client, which is used for receiving the event of kickoff.
        SrsStatisticClient *client = stat_->find_client(ctx);
        if (client) {
            client->conn_ = conn;
        }

        return;
    }

    // Update alive time of context for virtual connection.
    SrsHlsVirtualConn *conn = it->second;
    if (!conn->interrupt_) {
        conn->request_time_ = srs_time_now_cached();
    }
}

srs_error_t SrsHlsStream::http_hooks_on_play(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_play(req->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_play(url, req, SrsHlsPlay)) != srs_success) {
            return srs_error_wrap(err, "http on_play %s", url.c_str());
        }
    }

    return err;
}

void SrsHlsStream::http_hooks_on_stop(ISrsRequest *req)
{
    if (!config_->get_vhost_http_hooks_enabled(req->vhost_)) {
        return;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_stop(req->vhost_);

        if (!conf) {
            srs_info("ignore the empty http callback: on_stop");
            return;
        }

        hooks = conf->args_;
    }

    SrsStatisticStreamSnapshot stream(stat_, req);
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        hooks_->on_stop(url, req, SrsHlsPlay, stream);
    }

    return;
}

srs_error_t SrsHlsStream::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsHlsVirtualConn *>::iterator it;
    for (it = map_ctx_info_.begin(); it != map_ctx_info_.end();) {
        string ctx = it->first;
        SrsHlsVirtualConn *info = it->second;

        srs_utime_t hls_window = config_->get_hls_window(info->req_->vhost_);
        if (info->request_time_ + (2 * hls_window) < srs_time_now_cached()) {
            SrsContextRestore(_srs_context->get_id());
            _srs_context->set_id(SrsContextId().set_value(ctx));

            http_hooks_on_stop(info->req_);

            // TODO: FIXME: Should finger out the err.
            stat_->on_disconnect(ctx, srs_success);

            srs_freep(info);
            map_ctx_info_.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}

bool SrsHlsStream::is_interrupt(std::string id)
{
    std::map<std::string, SrsHlsVirtualConn *>::iterator it = map_ctx_info_.find(id);
    if (it != map_ctx_info_.end()) {
        return it->second->interrupt_;
    }
    return false;
}

SrsVodStream::SrsVodStream(string root_dir) : SrsHttpFileServer(root_dir)
{
    config_ = _srs_config;
}

void SrsVodStream::assemble()
{
    hls_.assemble();
}

SrsVodStream::~SrsVodStream()
{
    config_ = NULL;
}

// Refuse a range or start that has no byte of the file in it: 416 with the file size in Content-Range, as RFC 9110 has
// it, so the client knows the size, instead of closing the connection with no answer. Like every file answer, it
// carries Connection: Close.
static srs_error_t srs_vod_range_not_satisfiable(ISrsHttpResponseWriter *w, string fullpath, int64_t filesize, int64_t start)
{
    srs_warn("http vod %s range not satisfiable, size=%" PRId64 ", start=%" PRId64, fullpath.c_str(), filesize, start);

    std::stringstream content_range;
    content_range << "bytes */" << filesize;
    w->header()->set("Content-Range", content_range.str());

    return srs_go_http_error(w, SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable);
}

srs_error_t SrsVodStream::serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath, int64_t offset)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsFileReader> fs(fs_factory->create_file_reader());

    // open flv file
    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "open file");
    }

    // A start at or past the file size has no tag to serve.
    if (offset >= fs->filesize()) {
        return srs_vod_range_not_satisfiable(w, fullpath, fs->filesize(), offset);
    }

    SrsFlvVodStreamDecoder ffd;

    // open fast decoder
    if ((err = ffd.initialize(fs.get())) != srs_success) {
        return srs_error_wrap(err, "init ffd");
    }

    // save header, send later.
    char flv_header[13];

    // send flv header
    if ((err = ffd.read_header_ext(flv_header)) != srs_success) {
        return srs_error_wrap(err, "ffd read header");
    }

    // save sequence header, send later
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
        // A file cut inside its sequence header tags has fewer bytes than the tags claim.
        if (start + sh_size > fs->filesize()) {
            return srs_error_new(ERROR_HTTP_REMUX_SEQUENCE_HEADER, "sequence past file end, start=%" PRId64 ", size=%d, filesize=%" PRId64, start, sh_size, fs->filesize());
        }
    }

    SrsUniquePtr<char[]> sh_data(new char[sh_size]);
    if ((err = fs->read(sh_data.get(), sh_size, NULL)) != srs_success) {
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
    if (sh_size > 0 && (err = w->write(sh_data.get(), sh_size)) != srs_success) {
        return srs_error_wrap(err, "write sequence");
    }

    // write body.
    if ((err = ffd.seek2(offset)) != srs_success) {
        return srs_error_wrap(err, "ffd seek");
    }

    // send data
    if ((err = copy(w, fs.get(), r, left)) != srs_success) {
        return srs_error_wrap(err, "read flv=%s size=%" PRId64, fullpath.c_str(), left);
    }

    return err;
}

srs_error_t SrsVodStream::serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath, int64_t start, int64_t end)
{
    srs_error_t err = srs_success;

    srs_assert(start >= -1);
    srs_assert(end == -1 || end >= 0);

    SrsUniquePtr<SrsFileReader> fs(fs_factory->create_file_reader());

    // open flv file
    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "fs open");
    }

    // start == -1 means the range names the last end bytes of the file.
    if (start == -1) {
        start = srs_max((int64_t)0, fs->filesize() - end);
        end = fs->filesize() - 1;
    }

    // parse -1 to whole file.
    if (end == -1) {
        end = fs->filesize() - 1;
    }

    // The end is the last byte position and it is inclusive, so the last byte a client may ask for is filesize-1.
    // Clamp an end that reaches or passes the end of the file, as the clients expect the bytes that do exist.
    if (end >= fs->filesize()) {
        end = fs->filesize() - 1;
    }

    // A start at or past the file size, or any range of an empty file, has no byte to serve.
    if (start > end || end < 0) {
        return srs_vod_range_not_satisfiable(w, fullpath, fs->filesize(), start);
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
    if ((err = copy(w, fs.get(), r, left)) != srs_success) {
        return srs_error_wrap(err, "read mp4=%s size=%" PRId64, fullpath.c_str(), left);
    }

    return err;
}

srs_error_t SrsVodStream::serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath)
{
    srs_error_t err = srs_success;

    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    srs_assert(hr);

    SrsUniquePtr<ISrsRequest> req(hr->to_request(hr->host())->as_http());

    // discovery vhost, resolve the vhost from config
    SrsConfDirective *parsed_vhost = config_->get_vhost(req->vhost_);
    if (parsed_vhost) {
        req->vhost_ = parsed_vhost->arg0();
    }

    // The file server opened the playlist by its percent-decoded path, so the session takes its names
    // from that path too, else the security check, the hooks and the stats would name another stream
    // than the one served, such as %6Civestream for /live/%6Civestream.m3u8. The names are split from
    // the decoded path, never parsed as a URL again, so a decoded ? or # is in the name, as in the file.
    string path;
    if ((err = SrsHttpUri::path_unescape(hr->path(), path)) != srs_success) {
        return srs_error_wrap(err, "unescape path=%s", hr->path().c_str());
    }
    path = srs_strings_trim_start(path, "/");

    // The URL's dir and file name are the app and stream only for a playlist of [app]/[stream].m3u8,
    // so take them from the hls_m3u8_file the playlist was written by, such as the app live and the
    // stream livestream of /live/livestream/index.m3u8 for [app]/[stream]/index.m3u8. A playlist
    // not of the template is split as an HTTP path, as SrsHttpMessage::to_request() does.
    string app, stream;
    if (!srs_path_parse_stream(config_->get_hls_m3u8_file(req->vhost_), req->vhost_, path, app, stream)) {
        srs_net_url_parse_http_path("/" + path, app, stream);
    }
    req->app_ = app;
    req->stream_ = stream;
    req->strip();

    // The request's tcUrl was parsed as RTMP, with the RTMP default port that as_http() leaves out,
    // so set the session's own: the URL of its app on the HTTP server it asked, as an HTTP-FLV
    // player's.
    req->tcUrl_ = hr->app_url(req->app_);

    // Try to serve by HLS streaming.
    bool served = false;
    if ((err = hls_.serve_m3u8_ctx(w, r, fs_factory, fullpath, req.get(), &served)) != srs_success) {
        return srs_error_wrap(err, "hls ctx");
    }

    // Serve by default HLS handler.
    if (!served) {
        return SrsHttpFileServer::serve_m3u8_ctx(w, r, fullpath);
    }

    return err;
}

srs_error_t SrsVodStream::serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath)
{
    srs_error_t err = srs_success;

    // SrsServer also stat all HTTP connections including this one, but it should be ignored because the id is not
    // matched to any exists client. And we will do stat for the HLS streaming by session in hls_ctx.
    SrsHttpMessage *hr = dynamic_cast<SrsHttpMessage *>(r);
    SrsHttpConn *hc = dynamic_cast<SrsHttpConn *>(hr->connection());
    SrsHttpxConn *hxc = dynamic_cast<SrsHttpxConn *>(hc->handler());

    // Note that we never enable the stat for the HTTP connection, because we always stat the pseudo HLS streaming
    // session identified by hls_ctx, which served by an SrsHlsStream object.
    hxc->set_enable_stat(false);

    // Serve by default HLS handler.
    err = SrsHttpFileServer::serve_ts_ctx(w, r, fullpath);

    // Notify the HLS to stat the ts after serving.
    hls_.on_serve_ts_ctx(w, r);

    return err;
}

ISrsHttpStaticServer::ISrsHttpStaticServer()
{
}

ISrsHttpStaticServer::~ISrsHttpStaticServer()
{
}

SrsHttpStaticServer::SrsHttpStaticServer()
{
    mux_ = new SrsHttpServeMux();

    config_ = _srs_config;
}

SrsHttpStaticServer::~SrsHttpStaticServer()
{
    srs_freep(mux_);

    config_ = NULL;
}

srs_error_t SrsHttpStaticServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return mux_->serve_http(w, r);
}

srs_error_t SrsHttpStaticServer::initialize()
{
    srs_error_t err = srs_success;

    bool default_root_exists = false;

    // http static file and flv vod stream mount for each vhost.
    SrsConfDirective *root = config_->get_root();
    for (int i = 0; i < (int)root->directives_.size(); i++) {
        SrsConfDirective *conf = root->at(i);

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
            std::string dir = config_->get_vhost_http_dir(vhost);
            srs_warn("http: root mount to %s", dir.c_str());
        }
    }

    if (!default_root_exists) {
        // add root
        std::string dir = config_->get_http_stream_dir();
        SrsVodStream *stream = new SrsVodStream(dir);
        stream->assemble();
        if ((err = mux_->handle("/", stream)) != srs_success) {
            return srs_error_wrap(err, "mount root dir=%s", dir.c_str());
        }
        srs_trace("http: root mount to %s", dir.c_str());
    }

    return err;
}

ISrsHttpServeMux *SrsHttpStaticServer::mux()
{
    return mux_;
}

srs_error_t SrsHttpStaticServer::mount_vhost(string vhost, string &pmount)
{
    srs_error_t err = srs_success;

    // when vhost disabled, ignore.
    if (!config_->get_vhost_enabled(vhost)) {
        return err;
    }

    // when vhost http_static disabled, ignore.
    if (!config_->get_vhost_http_enabled(vhost)) {
        return err;
    }

    std::string mount = config_->get_vhost_http_mount(vhost);
    std::string dir = config_->get_vhost_http_dir(vhost);

    // replace the vhost variable
    mount = srs_strings_replace(mount, "[vhost]", vhost);
    dir = srs_strings_replace(dir, "[vhost]", vhost);

    // remove the default vhost mount
    mount = srs_strings_replace(mount, SRS_CONSTS_RTMP_DEFAULT_VHOST "/", "/");

    // the dir mount must always ends with "/"
    if (mount != "/" && !srs_strings_ends_with(mount, "/")) {
        mount += "/";
    }

    // mount the http of vhost.
    SrsVodStream *stream = new SrsVodStream(dir);
    stream->assemble();
    if ((err = mux_->handle(mount, stream)) != srs_success) {
        return srs_error_wrap(err, "mux handle");
    }
    srs_trace("http: vhost=%s mount to %s at %s", vhost.c_str(), mount.c_str(), dir.c_str());

    pmount = mount;

    return err;
}
