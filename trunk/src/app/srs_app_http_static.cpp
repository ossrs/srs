//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
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
#include <srs_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_mp3.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_service_utility.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_service_log.hpp>

#define SRS_CONTEXT_IN_HLS "hls_ctx"

SrsVodStream::SrsVodStream(string root_dir) : SrsHttpFileServer(root_dir)
{
    _srs_hybrid->timer5s()->subscribe(this);
}

SrsVodStream::~SrsVodStream()
{
    _srs_hybrid->timer5s()->unsubscribe(this);
    std::map<std::string, SrsM3u8CtxInfo>::iterator it;
    for (it = map_ctx_info_.begin(); it != map_ctx_info_.end(); ++it) {
        srs_freep(it->second.req);
    }
    map_ctx_info_.clear();
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
    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(req->vhost);
    if (parsed_vhost) {
        req->vhost = parsed_vhost->arg0();
    }

    SrsAutoFree(SrsRequest, req);

    string ctx = hr->query_get(SRS_CONTEXT_IN_HLS);
    if (!ctx.empty() && ctx_is_exist(ctx)) {
        alive(ctx, NULL);
        return SrsHttpFileServer::serve_m3u8_ctx(w, r, fullpath);
    }

    if (ctx.empty()) {
        // make sure unique
        do {
            ctx = srs_random_str(8);  // the same as cid
        } while (ctx_is_exist(ctx));
    }
    
    SrsContextRestore(_srs_context->get_id());
    _srs_context->set_id(SrsContextId().set_value(ctx));

    if ((err = http_hooks_on_play(req)) != srs_success) {
        return srs_error_wrap(err, "HLS: http_hooks_on_play");
    }

    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF;
    ss << "#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1" << SRS_CONSTS_LF;
    ss << hr->path() << "?" << SRS_CONTEXT_IN_HLS << "=" << ctx;
    if (!hr->query().empty() && hr->query_get(SRS_CONTEXT_IN_HLS).empty())
    {
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

    alive(ctx, req->copy());

    // update the statistic when source disconveried.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(ctx, req, NULL, SrsRtmpConnPlay)) != srs_success) {
        return srs_error_wrap(err, "stat on client");
    }

    return err;
}

bool SrsVodStream::ctx_is_exist(std::string ctx)
{
    return (map_ctx_info_.find(ctx) != map_ctx_info_.end());
}

void SrsVodStream::alive(std::string ctx, SrsRequest* req)
{
    std::map<std::string, SrsM3u8CtxInfo>::iterator it;
    if ((it = map_ctx_info_.find(ctx)) != map_ctx_info_.end()) {
        it->second.request_time = srs_get_system_time();
    } else {
        SrsM3u8CtxInfo info;
        info.req = req;
        info.request_time = srs_get_system_time();
        map_ctx_info_.insert(make_pair(ctx, info));
    }
}

srs_error_t SrsVodStream::http_hooks_on_play(SrsRequest* req)
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

void SrsVodStream::http_hooks_on_stop(SrsRequest* req)
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

srs_error_t SrsVodStream::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsM3u8CtxInfo>::iterator it;
    for (it = map_ctx_info_.begin(); it != map_ctx_info_.end(); ++it) {
        string ctx = it->first;
        SrsRequest* req = it->second.req;
        srs_utime_t hls_window = _srs_config->get_hls_window(req->vhost);
        if (it->second.request_time + (2 * hls_window) < srs_get_system_time()) {
            SrsContextRestore(_srs_context->get_id());
            _srs_context->set_id(SrsContextId().set_value(ctx));

            http_hooks_on_stop(req);
            srs_freep(req);

            SrsStatistic* stat = SrsStatistic::instance();
            stat->on_disconnect(ctx);
            map_ctx_info_.erase(it);

            break;
        }
    }

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

srs_error_t SrsHttpStaticServer::on_reload_vhost_http_updated()
{
    // TODO: FIXME: implements it.
    return srs_success;
}

