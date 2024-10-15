//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtmp_conn.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_config.hpp>
#include <srs_app_refer.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_st.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_recv_thread.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_security.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_tencentcloud.hpp>
#include <srs_app_srt_source.hpp>

// the timeout in srs_utime_t to wait encoder to republish
// if timeout, close the connection.
#define SRS_REPUBLISH_SEND_TIMEOUT (3 * SRS_UTIME_MINUTES)
// if timeout, close the connection.
#define SRS_REPUBLISH_RECV_TIMEOUT (3 * SRS_UTIME_MINUTES)

// the timeout in srs_utime_t to wait client data, when client paused
// if timeout, close the connection.
#define SRS_PAUSED_SEND_TIMEOUT (3 * SRS_UTIME_MINUTES)
// if timeout, close the connection.
#define SRS_PAUSED_RECV_TIMEOUT (3 * SRS_UTIME_MINUTES)

// when edge timeout, retry next.
#define SRS_EDGE_TOKEN_TRAVERSE_TIMEOUT (3 * SRS_UTIME_SECONDS)

SrsSimpleRtmpClient::SrsSimpleRtmpClient(string u, srs_utime_t ctm, srs_utime_t stm) : SrsBasicRtmpClient(u, ctm, stm)
{
}

SrsSimpleRtmpClient::~SrsSimpleRtmpClient()
{
}

srs_error_t SrsSimpleRtmpClient::connect_app()
{
    std::vector<SrsIPAddress*>& ips = srs_get_local_ips();
    srs_assert(_srs_config->get_stats_network() < (int)ips.size());
    SrsIPAddress* local_ip = ips[_srs_config->get_stats_network()];
    
    bool debug_srs_upnode = _srs_config->get_debug_srs_upnode(req->vhost);
    
    return do_connect_app(local_ip->ip, debug_srs_upnode);
}

SrsClientInfo::SrsClientInfo()
{
    edge = false;
    req = new SrsRequest();
    res = new SrsResponse();
    type = SrsRtmpConnUnknown;
}

SrsClientInfo::~SrsClientInfo()
{
    srs_freep(req);
    srs_freep(res);
}

SrsRtmpConn::SrsRtmpConn(SrsServer* svr, srs_netfd_t c, string cip, int cport)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    server = svr;

    stfd = c;
    skt = new SrsTcpConnection(c);
    manager = svr;
    ip = cip;
    port = cport;
    create_time = srsu2ms(srs_get_system_time());
#ifdef SRS_APM
    span_main_ = _srs_apm->dummy();
    span_connect_ = _srs_apm->dummy();
    span_client_ = _srs_apm->dummy();
#endif
    trd = new SrsSTCoroutine("rtmp", this, _srs_context->get_id());

    kbps = new SrsNetworkKbps();
    kbps->set_io(skt, skt);
    delta_ = new SrsNetworkDelta();
    delta_->set_io(skt, skt);
    
    rtmp = new SrsRtmpServer(skt);
    refer = new SrsRefer();
    security = new SrsSecurity();
    duration = 0;
    wakable = NULL;
    
    mw_sleep = SRS_PERF_MW_SLEEP;
    mw_msgs = 0;
    realtime = SRS_PERF_MIN_LATENCY_ENABLED;
    send_min_interval = 0;
    tcp_nodelay = false;
    info = new SrsClientInfo();

    publish_1stpkt_timeout = 0;
    publish_normal_timeout = 0;
    
    _srs_config->subscribe(this);
}

SrsRtmpConn::~SrsRtmpConn()
{
    _srs_config->unsubscribe(this);

    trd->interrupt();
    // wakeup the handler which need to notice.
    if (wakable) {
        wakable->wakeup();
    }
    srs_freep(trd);

    srs_freep(kbps);
    srs_freep(delta_);
    srs_freep(skt);
    
    srs_freep(info);
    srs_freep(rtmp);
    srs_freep(refer);
    srs_freep(security);
#ifdef SRS_APM
    srs_freep(span_main_);
    srs_freep(span_connect_);
    srs_freep(span_client_);
#endif
}

std::string SrsRtmpConn::desc()
{
    return "RtmpConn";
}

std::string srs_ipv4_string(uint32_t rip)
{
    return srs_fmt("%d.%d.%d.%d", uint8_t(rip>>24), uint8_t(rip>>16), uint8_t(rip>>8), uint8_t(rip));
}

// TODO: return detail message when error for client.
srs_error_t SrsRtmpConn::do_cycle()
{
    srs_error_t err = srs_success;

#ifdef SRS_APM
    // We should keep the root span to alive util connection closed.
    // Note that we use producer and consumer span because RTMP connection is long polling connection.
    // Note that we also store this span in coroutine context, so that edge could load it.
    srs_freep(span_main_);
    span_main_ = _srs_apm->span("rtmp")->set_kind(SrsApmKindServer)->attr("cip", ip)
        ->attr("cid", _srs_context->get_id().c_str());
#endif

#ifdef SRS_APM
    srs_trace("RTMP client ip=%s:%d, fd=%d, trace=%s, span=%s", ip.c_str(), port, srs_netfd_fileno(stfd),
        span_main_->format_trace_id(), span_main_->format_span_id()
    );
#else
    srs_trace("RTMP client ip=%s:%d, fd=%d", ip.c_str(), port, srs_netfd_fileno(stfd));
#endif

    rtmp->set_recv_timeout(SRS_CONSTS_RTMP_TIMEOUT);
    rtmp->set_send_timeout(SRS_CONSTS_RTMP_TIMEOUT);

    if ((err = rtmp->handshake()) != srs_success) {
        return srs_error_wrap(err, "rtmp handshake");
    }

    uint32_t rip = rtmp->proxy_real_ip();
    std::string rips = srs_ipv4_string(rip);
    if (rip > 0) {
        srs_trace("RTMP proxy real client ip=%s", rips.c_str());
    }

#ifdef SRS_APM
    // Update the real IP of client, also set the HTTP fields.
    span_main_->attr("rip", rip ? rips : ip)->attr("http.client_ip", rip ? rips : ip);

    // The span for RTMP connecting to application.
    srs_freep(span_connect_);
    span_connect_ = _srs_apm->span("connect")->as_child(span_main_);
#endif

    SrsRequest* req = info->req;
    if ((err = rtmp->connect_app(req)) != srs_success) {
        return srs_error_wrap(err, "rtmp connect tcUrl");
    }

    // set client ip to request.
    req->ip = ip;
    
    srs_trace("connect app, tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%d, app=%s, args=%s",
        req->tcUrl.c_str(), req->pageUrl.c_str(), req->swfUrl.c_str(),
        req->schema.c_str(), req->vhost.c_str(), req->port,
        req->app.c_str(), (req->args? "(obj)":"null"));
    
    // show client identity
    if(req->args) {
        std::string srs_version;
        std::string srs_server_ip;
        int srs_pid = 0;
        int srs_id = 0;
        
        SrsAmf0Any* prop = NULL;
        if ((prop = req->args->ensure_property_string("srs_version")) != NULL) {
            srs_version = prop->to_str();
        }
        if ((prop = req->args->ensure_property_string("srs_server_ip")) != NULL) {
            srs_server_ip = prop->to_str();
        }
        if ((prop = req->args->ensure_property_number("srs_pid")) != NULL) {
            srs_pid = (int)prop->to_number();
        }
        if ((prop = req->args->ensure_property_number("srs_id")) != NULL) {
            srs_id = (int)prop->to_number();
        }
        
        if (srs_pid > 0) {
            srs_trace("edge-srs ip=%s, version=%s, pid=%d, id=%d",
                srs_server_ip.c_str(), srs_version.c_str(), srs_pid, srs_id);
        }

#ifdef SRS_APM
        // Load the span from the AMF0 object propagator.
        // Note that we will update the trace id, so please make sure no spans are ended before this.
        _srs_apm->extract(span_main_, req->args);
#endif
    }
    
    if ((err = service_cycle()) != srs_success) {
        err = srs_error_wrap(err, "service cycle");
    }
    
    srs_error_t r0 = srs_success;
    if ((r0 = on_disconnect()) != srs_success) {
        err = srs_error_wrap(err, "on disconnect %s", srs_error_desc(r0).c_str());
        srs_freep(r0);
    }
    
    // If client is redirect to other servers, we already logged the event.
    if (srs_error_code(err) == ERROR_CONTROL_REDIRECT) {
        srs_error_reset(err);
    }
    
    return err;
}

srs_error_t SrsRtmpConn::on_reload_vhost_removed(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    // if the vhost connected is removed, disconnect the client.
    srs_trace("vhost %s removed/disabled, close client url=%s",
              vhost.c_str(), req->get_stream_url().c_str());
    
    // should never close the fd in another thread,
    // one fd should managed by one thread, we should use interrupt instead.
    // so we just ignore the vhost enabled event.
    //srs_close_stfd(stfd);
    
    return err;
}

srs_error_t SrsRtmpConn::on_reload_vhost_play(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    // send_min_interval
    if (true) {
        srs_utime_t v = _srs_config->get_send_min_interval(vhost);
        if (v != send_min_interval) {
            srs_trace("apply smi %d=>%d ms", srsu2msi(send_min_interval), srsu2msi(v));
            send_min_interval = v;
        }
    }

    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime);
    mw_sleep = _srs_config->get_mw_sleep(req->vhost);
    skt->set_socket_buffer(mw_sleep);
    
    return err;
}

srs_error_t SrsRtmpConn::on_reload_vhost_tcp_nodelay(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    set_sock_options();
    
    return err;
}

srs_error_t SrsRtmpConn::on_reload_vhost_realtime(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    bool realtime_enabled = _srs_config->get_realtime_enabled(req->vhost);
    if (realtime_enabled != realtime) {
        srs_trace("realtime changed %d=>%d", realtime, realtime_enabled);
        realtime = realtime_enabled;
    }

    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime);
    mw_sleep = _srs_config->get_mw_sleep(req->vhost);
    skt->set_socket_buffer(mw_sleep);
    
    return err;
}

srs_error_t SrsRtmpConn::on_reload_vhost_publish(string vhost)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    srs_utime_t p1stpt = _srs_config->get_publish_1stpkt_timeout(req->vhost);
    if (p1stpt != publish_1stpkt_timeout) {
        srs_trace("p1stpt changed %d=>%d", srsu2msi(publish_1stpkt_timeout), srsu2msi(p1stpt));
        publish_1stpkt_timeout = p1stpt;
    }
    
    srs_utime_t pnt = _srs_config->get_publish_normal_timeout(req->vhost);
    if (pnt != publish_normal_timeout) {
        srs_trace("pnt changed %d=>%d", srsu2msi(publish_normal_timeout), srsu2msi(pnt));
        publish_normal_timeout = pnt;
    }
    
    return err;
}

ISrsKbpsDelta* SrsRtmpConn::delta()
{
    return delta_;
}

srs_error_t SrsRtmpConn::service_cycle()
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    int out_ack_size = _srs_config->get_out_ack_size(req->vhost);
    if (out_ack_size && (err = rtmp->set_window_ack_size(out_ack_size)) != srs_success) {
        return srs_error_wrap(err, "rtmp: set out window ack size");
    }
    
    int in_ack_size = _srs_config->get_in_ack_size(req->vhost);
    if (in_ack_size && (err = rtmp->set_in_window_ack_size(in_ack_size)) != srs_success) {
        return srs_error_wrap(err, "rtmp: set in window ack size");
    }
    
    if ((err = rtmp->set_peer_bandwidth((int)(2.5 * 1000 * 1000), 2)) != srs_success) {
        return srs_error_wrap(err, "rtmp: set peer bandwidth");
    }
    
    // get the ip which client connected.
    std::string local_ip = srs_get_local_ip(srs_netfd_fileno(stfd));
    
    // set chunk size to larger.
    // set the chunk size before any larger response greater than 128,
    // to make OBS happy, @see https://github.com/ossrs/srs/issues/454
    int chunk_size = _srs_config->get_chunk_size(req->vhost);
    if ((err = rtmp->set_chunk_size(chunk_size)) != srs_success) {
        return srs_error_wrap(err, "rtmp: set chunk size %d", chunk_size);
    }
    
    // response the client connect ok.
    if ((err = rtmp->response_connect_app(req, local_ip.c_str())) != srs_success) {
        return srs_error_wrap(err, "rtmp: response connect app");
    }

#ifdef SRS_APM
    // Must be a connecting application span.
    span_connect_->end();
#endif
    
    if ((err = rtmp->on_bw_done()) != srs_success) {
        return srs_error_wrap(err, "rtmp: on bw down");
    }
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtmp: thread quit");
        }
        
        err = stream_service_cycle();
        
        // stream service must terminated with error, never success.
        // when terminated with success, it's user required to stop.
        // TODO: FIXME: Support RTMP client timeout, https://github.com/ossrs/srs/issues/1134
        if (err == srs_success) {
            continue;
        }
        
        // when not system control error, fatal error, return.
        if (!srs_is_system_control_error(err)) {
            return srs_error_wrap(err, "rtmp: stream service");
        }
        
        // for republish, continue service
        if (srs_error_code(err) == ERROR_CONTROL_REPUBLISH) {
            // set timeout to a larger value, wait for encoder to republish.
            rtmp->set_send_timeout(SRS_REPUBLISH_RECV_TIMEOUT);
            rtmp->set_recv_timeout(SRS_REPUBLISH_SEND_TIMEOUT);
            
            srs_info("rtmp: retry for republish");
            srs_freep(err);
            continue;
        }
        
        // for "some" system control error,
        // logical accept and retry stream service.
        if (srs_error_code(err) == ERROR_CONTROL_RTMP_CLOSE) {
            // TODO: FIXME: use ping message to anti-death of socket.
            // set timeout to a larger value, for user paused.
            rtmp->set_recv_timeout(SRS_PAUSED_RECV_TIMEOUT);
            rtmp->set_send_timeout(SRS_PAUSED_SEND_TIMEOUT);
            
            srs_trace("rtmp: retry for close");
            srs_freep(err);
            continue;
        }
        
        // for other system control message, fatal error.
        return srs_error_wrap(err, "rtmp: reject");
    }
    
    return err;
}

srs_error_t SrsRtmpConn::stream_service_cycle()
{
    srs_error_t err = srs_success;

    SrsRequest* req = info->req;
    if ((err = rtmp->identify_client(info->res->stream_id, info->type, req->stream, req->duration)) != srs_success) {
        return srs_error_wrap(err, "rtmp: identify client");
    }
    
    srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->stream, req->port, req->param);

    // guess stream name
    if (req->stream.empty()) {
        string app = req->app, param = req->param;
        srs_guess_stream_by_app(req->app, req->param, req->stream);
        srs_trace("Guessing by app=%s, param=%s to app=%s, param=%s, stream=%s", app.c_str(), param.c_str(), req->app.c_str(), req->param.c_str(), req->stream.c_str());
    }

    req->strip();
    srs_trace("client identified, type=%s, vhost=%s, app=%s, stream=%s, param=%s, duration=%dms",
        srs_client_type_string(info->type).c_str(), req->vhost.c_str(), req->app.c_str(), req->stream.c_str(), req->param.c_str(), srsu2msi(req->duration));

#ifdef SRS_APM
    // Start APM only when client is identified, because it might republish.
    srs_freep(span_client_);
    span_client_ = _srs_apm->span("client")->as_child(span_connect_)->attr("type", srs_client_type_string(info->type))
        ->attr("url", req->get_stream_url())->attr("http.url", req->get_stream_url());
    // We store the span to coroutine context, for edge to load it.
    _srs_apm->store(span_client_);
#endif
    
    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(req->vhost);
    if (parsed_vhost) {
        req->vhost = parsed_vhost->arg0();
    }
#ifdef SRS_APM
    span_client_->attr("vhost", req->vhost)->attr("http.host", req->host)->attr("http.server_name", req->vhost)
        ->attr("http.target", srs_fmt("/%s/%s", req->app.c_str(), req->stream.c_str()));
#endif

    if (req->schema.empty() || req->vhost.empty() || req->port == 0 || req->app.empty()) {
        return srs_error_new(ERROR_RTMP_REQ_TCURL, "discovery tcUrl failed, tcUrl=%s, schema=%s, vhost=%s, port=%d, app=%s",
            req->tcUrl.c_str(), req->schema.c_str(), req->vhost.c_str(), req->port, req->app.c_str());
    }

    // check vhost, allow default vhost.
    if ((err = check_vhost(true)) != srs_success) {
        return srs_error_wrap(err, "check vhost");
    }

    srs_trace("connected stream, tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%d, app=%s, stream=%s, param=%s, args=%s",
        req->tcUrl.c_str(), req->pageUrl.c_str(), req->swfUrl.c_str(), req->schema.c_str(), req->vhost.c_str(), req->port,
        req->app.c_str(), req->stream.c_str(), req->param.c_str(), (req->args? "(obj)":"null"));
    
    // do token traverse before serve it.
    // @see https://github.com/ossrs/srs/pull/239
    if (true) {
        info->edge = _srs_config->get_vhost_is_edge(req->vhost);
        bool edge_traverse = _srs_config->get_vhost_edge_token_traverse(req->vhost);
        if (info->edge && edge_traverse) {
            if ((err = check_edge_token_traverse_auth()) != srs_success) {
                return srs_error_wrap(err, "rtmp: check token traverse");
            }
        }
    }

    // security check
    if ((err = security->check(info->type, ip, req)) != srs_success) {
        return srs_error_wrap(err, "rtmp: security check");
    }
    
    // Never allow the empty stream name, for HLS may write to a file with empty name.
    // @see https://github.com/ossrs/srs/issues/834
    if (req->stream.empty()) {
        return srs_error_new(ERROR_RTMP_STREAM_NAME_EMPTY, "rtmp: empty stream");
    }

    // client is identified, set the timeout to service timeout.
    rtmp->set_recv_timeout(SRS_CONSTS_RTMP_TIMEOUT);
    rtmp->set_send_timeout(SRS_CONSTS_RTMP_TIMEOUT);
    
    // find a source to serve.
    SrsSharedPtr<SrsLiveSource> live_source;
    if ((err = _srs_sources->fetch_or_create(req, server, live_source)) != srs_success) {
        return srs_error_wrap(err, "rtmp: fetch source");
    }
    srs_assert(live_source.get() != NULL);

    bool enabled_cache = _srs_config->get_gop_cache(req->vhost);
    int gcmf = _srs_config->get_gop_cache_max_frames(req->vhost);
    srs_trace("source url=%s, ip=%s, cache=%d/%d, is_edge=%d, source_id=%s/%s",
        req->get_stream_url().c_str(), ip.c_str(), enabled_cache, gcmf, info->edge, live_source->source_id().c_str(),
              live_source->pre_source_id().c_str());
    live_source->set_cache(enabled_cache);
    live_source->set_gop_cache_max_frames(gcmf);
    
    switch (info->type) {
        case SrsRtmpConnPlay: {
            // response connection start play
            if ((err = rtmp->start_play(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start play");
            }

            // We must do stat the client before hooks, because hooks depends on it.
            SrsStatistic* stat = SrsStatistic::instance();
            if ((err = stat->on_client(_srs_context->get_id().c_str(), req, this, info->type)) != srs_success) {
                return srs_error_wrap(err, "rtmp: stat client");
            }

            // We must do hook after stat, because depends on it.
            if ((err = http_hooks_on_play()) != srs_success) {
                return srs_error_wrap(err, "rtmp: callback on play");
            }

#ifdef SRS_APM
            // Must be a client span.
            span_client_->set_name("play")->end();
            // We end the connection span because it's a producer and only trace the established.
            span_main_->end();
#endif
            
            err = playing(live_source);
            http_hooks_on_stop();
            
            return err;
        }
        case SrsRtmpConnFMLEPublish: {
            if ((err = rtmp->start_fmle_publish(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start FMLE publish");
            }

#ifdef SRS_APM
            // Must be a client span.
            span_client_->set_name("publish")->end();
            // We end the connection span because it's a producer and only trace the established.
            span_main_->end();
#endif
            
            return publishing(live_source);
        }
        case SrsRtmpConnHaivisionPublish: {
            if ((err = rtmp->start_haivision_publish(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start HAIVISION publish");
            }

#ifdef SRS_APM
            // Must be a client span.
            span_client_->set_name("publish")->end();
            // We end the connection span because it's a producer and only trace the established.
            span_main_->end();
#endif
            
            return publishing(live_source);
        }
        case SrsRtmpConnFlashPublish: {
            if ((err = rtmp->start_flash_publish(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start FLASH publish");
            }

#ifdef SRS_APM
            // Must be a client span.
            span_client_->set_name("publish")->end();
            // We end the connection span because it's a producer and only trace the established.
            span_main_->end();
#endif
            
            return publishing(live_source);
        }
        default: {
            return srs_error_new(ERROR_SYSTEM_CLIENT_INVALID, "rtmp: unknown client type=%d", info->type);
        }
    }
    
    return err;
}

srs_error_t SrsRtmpConn::check_vhost(bool try_default_vhost)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    srs_assert(req != NULL);
    
    SrsConfDirective* vhost = _srs_config->get_vhost(req->vhost, try_default_vhost);
    if (vhost == NULL) {
        return srs_error_new(ERROR_RTMP_VHOST_NOT_FOUND, "rtmp: no vhost %s", req->vhost.c_str());
    }
    
    if (!_srs_config->get_vhost_enabled(req->vhost)) {
        return srs_error_new(ERROR_RTMP_VHOST_NOT_FOUND, "rtmp: vhost %s disabled", req->vhost.c_str());
    }
    
    if (req->vhost != vhost->arg0()) {
        srs_trace("vhost change from %s to %s", req->vhost.c_str(), vhost->arg0().c_str());
        req->vhost = vhost->arg0();
    }
    
    if (_srs_config->get_refer_enabled(req->vhost)) {
        if ((err = refer->check(req->pageUrl, _srs_config->get_refer_all(req->vhost))) != srs_success) {
            return srs_error_wrap(err, "rtmp: referer check");
        }
    }
    
    if ((err = http_hooks_on_connect()) != srs_success) {
        return srs_error_wrap(err, "rtmp: callback on connect");
    }
    
    return err;
}

srs_error_t SrsRtmpConn::playing(SrsSharedPtr<SrsLiveSource> source)
{
    srs_error_t err = srs_success;
    
    // Check page referer of player.
    SrsRequest* req = info->req;
    if (_srs_config->get_refer_enabled(req->vhost)) {
        if ((err = refer->check(req->pageUrl, _srs_config->get_refer_play(req->vhost))) != srs_success) {
            return srs_error_wrap(err, "rtmp: referer check");
        }
    }
    
    // When origin cluster enabled, try to redirect to the origin which is active.
    // A active origin is a server which is delivering stream.
    if (!info->edge && _srs_config->get_vhost_origin_cluster(req->vhost) && source->inactive()) {
        vector<string> coworkers = _srs_config->get_vhost_coworkers(req->vhost);
        for (int i = 0; i < (int)coworkers.size(); i++) {
            // TODO: FIXME: User may config the server itself as coworker, we must identify and ignore it.
            string host; int port = 0; string coworker = coworkers.at(i);

            string url = "http://" + coworker + "/api/v1/clusters?"
                + "vhost=" + req->vhost + "&ip=" + req->host + "&app=" + req->app + "&stream=" + req->stream
                + "&coworker=" + coworker;
            if ((err = SrsHttpHooks::discover_co_workers(url, host, port)) != srs_success) {
                // If failed to discovery stream in this coworker, we should request the next one util the last.
                // @see https://github.com/ossrs/srs/issues/1223
                if (i < (int)coworkers.size() - 1) {
                    continue;
                }
                return srs_error_wrap(err, "discover coworkers, url=%s", url.c_str());
            }

            string rurl = srs_generate_rtmp_url(host, port, req->host, req->vhost, req->app, req->stream, req->param);
            srs_trace("rtmp: redirect in cluster, from=%s:%d, target=%s:%d, url=%s, rurl=%s",
                req->host.c_str(), req->port, host.c_str(), port, url.c_str(), rurl.c_str());

            // Ignore if host or port is invalid.
            if (host.empty() || port == 0) {
                continue;
            }
            
            bool accepted = false;
            if ((err = rtmp->redirect(req, rurl, accepted)) != srs_success) {
                srs_error_reset(err);
            } else {
                return srs_error_new(ERROR_CONTROL_REDIRECT, "redirected");
            }
        }
        
        return srs_error_new(ERROR_OCLUSTER_REDIRECT, "no origin");
    }
    
    // Set the socket options for transport.
    set_sock_options();
    
    // Create a consumer of source.
    SrsLiveConsumer* consumer_raw = NULL;
    if ((err = source->create_consumer(consumer_raw)) != srs_success) {
        return srs_error_wrap(err, "rtmp: create consumer");
    }
    SrsUniquePtr<SrsLiveConsumer> consumer(consumer_raw);

    if ((err = source->consumer_dumps(consumer.get())) != srs_success) {
        return srs_error_wrap(err, "rtmp: dumps consumer");
    }
    
    // Use receiving thread to receive packets from peer.
    SrsQueueRecvThread trd(consumer.get(), rtmp, SRS_PERF_MW_SLEEP, _srs_context->get_id());
    
    if ((err = trd.start()) != srs_success) {
        return srs_error_wrap(err, "rtmp: start receive thread");
    }
    
    // Deliver packets to peer.
    wakable = consumer.get();
    err = do_playing(source, consumer.get(), &trd);
    wakable = NULL;
    
    trd.stop();
    
    // Drop all packets in receiving thread.
    if (!trd.empty()) {
        srs_warn("drop the received %d messages", trd.size());
    }
    
    return err;
}

srs_error_t SrsRtmpConn::do_playing(SrsSharedPtr<SrsLiveSource> source, SrsLiveConsumer* consumer, SrsQueueRecvThread* rtrd)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    srs_assert(req);
    srs_assert(consumer);
    
    // initialize other components
    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_rtmp_play());

    SrsMessageArray msgs(SRS_PERF_MW_MSGS);
    bool user_specified_duration_to_stop = (req->duration > 0);
    int64_t starttime = -1;

    // setup the realtime.
    realtime = _srs_config->get_realtime_enabled(req->vhost);
    // setup the mw config.
    // when mw_sleep changed, resize the socket send buffer.
    mw_msgs = _srs_config->get_mw_msgs(req->vhost, realtime);
    mw_sleep = _srs_config->get_mw_sleep(req->vhost);
    skt->set_socket_buffer(mw_sleep);
    // initialize the send_min_interval
    send_min_interval = _srs_config->get_send_min_interval(req->vhost);
    
    srs_trace("start play smi=%dms, mw_sleep=%d, mw_msgs=%d, realtime=%d, tcp_nodelay=%d",
        srsu2msi(send_min_interval), srsu2msi(mw_sleep), mw_msgs, realtime, tcp_nodelay);

#ifdef SRS_APM
    SrsUniquePtr<ISrsApmSpan> span(_srs_apm->span("play-cycle")->set_kind(SrsApmKindProducer)->as_child(span_client_)
        ->attr("realtime", srs_fmt("%d", realtime))->end());
#endif
    
    while (true) {
        // when source is set to expired, disconnect it.
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtmp: thread quit");
        }

        // collect elapse for pithy print.
        pprint->elapse();

        // to use isolate thread to recv, can improve about 33% performance.
        while (!rtrd->empty()) {
            SrsCommonMessage* msg = rtrd->pump();
            if ((err = process_play_control_msg(consumer, msg)) != srs_success) {
                return srs_error_wrap(err, "rtmp: play control message");
            }
        }
        
        // quit when recv thread error.
        if ((err = rtrd->error_code()) != srs_success) {
            return srs_error_wrap(err, "rtmp: recv thread");
        }
        
#ifdef SRS_PERF_QUEUE_COND_WAIT
        // wait for message to incoming.
        // @see https://github.com/ossrs/srs/issues/257
        consumer->wait(mw_msgs, mw_sleep);
#endif
        
        // get messages from consumer.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        // @remark when enable send_min_interval, only fetch one message a time.
        int count = (send_min_interval > 0)? 1 : 0;
        if ((err = consumer->dump_packets(&msgs, count)) != srs_success) {
            return srs_error_wrap(err, "rtmp: consumer dump packets");
        }

        // reportable
        if (pprint->can_print()) {
            kbps->sample();
            srs_trace("-> " SRS_CONSTS_LOG_PLAY " time=%d, msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d, mw=%d/%d",
                (int)pprint->age(), count, kbps->get_send_kbps(), kbps->get_send_kbps_30s(), kbps->get_send_kbps_5m(),
                kbps->get_recv_kbps(), kbps->get_recv_kbps_30s(), kbps->get_recv_kbps_5m(), srsu2msi(mw_sleep), mw_msgs);

#ifdef SRS_APM
            // TODO: Do not use pithy print for frame span.
            ISrsApmSpan* sample = _srs_apm->span("play-frame")->set_kind(SrsApmKindConsumer)->as_child(span.get())
                ->attr("msgs", srs_fmt("%d", count))->attr("kbps", srs_fmt("%d", kbps->get_send_kbps_30s()));
            srs_freep(sample);
#endif
        }
        
        if (count <= 0) {
#ifndef SRS_PERF_QUEUE_COND_WAIT
            srs_usleep(mw_sleep);
#endif
            // ignore when nothing got.
            continue;
        }
        
        // only when user specifies the duration,
        // we start to collect the durations for each message.
        if (user_specified_duration_to_stop) {
            for (int i = 0; i < count; i++) {
                SrsSharedPtrMessage* msg = msgs.msgs[i];
                
                // foreach msg, collect the duration.
                // @remark: never use msg when sent it, for the protocol sdk will free it.
                if (starttime < 0 || starttime > msg->timestamp) {
                    starttime = msg->timestamp;
                }
                duration += (msg->timestamp - starttime) * SRS_UTIME_MILLISECONDS;
                starttime = msg->timestamp;
            }
        }
        
        // sendout messages, all messages are freed by send_and_free_messages().
        // no need to assert msg, for the rtmp will assert it.
        if (count > 0 && (err = rtmp->send_and_free_messages(msgs.msgs, count, info->res->stream_id)) != srs_success) {
            return srs_error_wrap(err, "rtmp: send %d messages", count);
        }
        
        // if duration specified, and exceed it, stop play live.
        // @see: https://github.com/ossrs/srs/issues/45
        if (user_specified_duration_to_stop) {
            if (duration >= req->duration) {
                return srs_error_new(ERROR_RTMP_DURATION_EXCEED, "rtmp: time %d up %d", srsu2msi(duration), srsu2msi(req->duration));
            }
        }
        
        // apply the minimal interval for delivery stream in srs_utime_t.
        if (send_min_interval > 0) {
            srs_usleep(send_min_interval);
        }

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777437476
        srs_thread_yield();
    }
    
    return err;
}

srs_error_t SrsRtmpConn::publishing(SrsSharedPtr<SrsLiveSource> source)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (_srs_config->get_refer_enabled(req->vhost)) {
        if ((err = refer->check(req->pageUrl, _srs_config->get_refer_publish(req->vhost))) != srs_success) {
            return srs_error_wrap(err, "rtmp: referer check");
        }
    }

    // We must do stat the client before hooks, because hooks depends on it.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(_srs_context->get_id().c_str(), req, this, info->type)) != srs_success) {
        return srs_error_wrap(err, "rtmp: stat client");
    }

    // We must do hook after stat, because depends on it.
    if ((err = http_hooks_on_publish()) != srs_success) {
        return srs_error_wrap(err, "rtmp: callback on publish");
    }
    
    // TODO: FIXME: Should refine the state of publishing.
    srs_error_t acquire_err = acquire_publish(source);
    if ((err = acquire_err) == srs_success) {
        // use isolate thread to recv,
        // @see: https://github.com/ossrs/srs/issues/237
        SrsPublishRecvThread rtrd(rtmp, req, srs_netfd_fileno(stfd), 0, this, source, _srs_context->get_id());
        err = do_publishing(source, &rtrd);
        rtrd.stop();
    }
    
    // Release and callback when acquire publishing success, if not, we should ignore, because the source
    // is not published by this session.
    if (acquire_err == srs_success) {
        release_publish(source);
        http_hooks_on_unpublish();
    }
    
    return err;
}

srs_error_t SrsRtmpConn::do_publishing(SrsSharedPtr<SrsLiveSource> source, SrsPublishRecvThread* rtrd)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_rtmp_publish());

    // start isolate recv thread.
    // TODO: FIXME: Pass the callback here.
    if ((err = rtrd->start()) != srs_success) {
        return srs_error_wrap(err, "rtmp: receive thread");
    }
    
    // initialize the publish timeout.
    publish_1stpkt_timeout = _srs_config->get_publish_1stpkt_timeout(req->vhost);
    publish_normal_timeout = _srs_config->get_publish_normal_timeout(req->vhost);
    srs_utime_t publish_kickoff_for_idle = _srs_config->get_publish_kickoff_for_idle(req->vhost);
    
    // set the sock options.
    set_sock_options();
    
    if (true) {
        bool mr = _srs_config->get_mr_enabled(req->vhost);
        srs_utime_t mr_sleep = _srs_config->get_mr_sleep(req->vhost);
        srs_trace("start publish mr=%d/%d, p1stpt=%d, pnt=%d, tcp_nodelay=%d", mr, srsu2msi(mr_sleep), srsu2msi(publish_1stpkt_timeout), srsu2msi(publish_normal_timeout), tcp_nodelay);
    }

#ifdef SRS_APM
    SrsUniquePtr<ISrsApmSpan> span(_srs_apm->span("publish-cycle")->set_kind(SrsApmKindProducer)->as_child(span_client_)
        ->attr("timeout", srs_fmt("%d", srsu2msi(publish_normal_timeout)))->end());
#endif

    // Response the start publishing message, let client start to publish messages.
    if ((err = rtmp->start_publishing(info->res->stream_id)) != srs_success) {
        return srs_error_wrap(err, "start publishing");
    }
    
    int64_t nb_msgs = 0;
    uint64_t nb_frames = 0;
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtmp: thread quit");
        }

        // Kick off the publisher when idle for a period of timeout.
        if (source->publisher_is_idle_for(publish_kickoff_for_idle)) {
            return srs_error_new(ERROR_KICKOFF_FOR_IDLE, "kicked for idle, url=%s, timeout=%ds", req->tcUrl.c_str(), srsu2si(publish_kickoff_for_idle));
        }

        pprint->elapse();

        // cond wait for timeout.
        if (nb_msgs == 0) {
            // when not got msgs, wait for a larger timeout.
            rtrd->wait(publish_1stpkt_timeout);
        } else {
            rtrd->wait(publish_normal_timeout);
        }
        
        // check the thread error code.
        if ((err = rtrd->error_code()) != srs_success) {
            return srs_error_wrap(err, "rtmp: receive thread");
        }
        
        // when not got any messages, timeout.
        if (rtrd->nb_msgs() <= nb_msgs) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "rtmp: publish timeout %dms, nb_msgs=%d",
                nb_msgs? srsu2msi(publish_normal_timeout) : srsu2msi(publish_1stpkt_timeout), (int)nb_msgs);
        }
        nb_msgs = rtrd->nb_msgs();
        
        // Update the stat for video fps.
        // @remark https://github.com/ossrs/srs/issues/851
        SrsStatistic* stat = SrsStatistic::instance();
        if ((err = stat->on_video_frames(req, (int)(rtrd->nb_video_frames() - nb_frames))) != srs_success) {
            return srs_error_wrap(err, "rtmp: stat video frames");
        }
        nb_frames = rtrd->nb_video_frames();

        // reportable
        if (pprint->can_print()) {
            kbps->sample();
            bool mr = _srs_config->get_mr_enabled(req->vhost);
            srs_utime_t mr_sleep = _srs_config->get_mr_sleep(req->vhost);
            srs_trace("<- " SRS_CONSTS_LOG_CLIENT_PUBLISH " time=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d, mr=%d/%d, p1stpt=%d, pnt=%d",
                (int)pprint->age(), kbps->get_send_kbps(), kbps->get_send_kbps_30s(), kbps->get_send_kbps_5m(),
                kbps->get_recv_kbps(), kbps->get_recv_kbps_30s(), kbps->get_recv_kbps_5m(), mr, srsu2msi(mr_sleep),
                srsu2msi(publish_1stpkt_timeout), srsu2msi(publish_normal_timeout));

#ifdef SRS_APM
            // TODO: Do not use pithy print for frame span.
            ISrsApmSpan* sample = _srs_apm->span("publish-frame")->set_kind(SrsApmKindConsumer)->as_child(span.get())
                ->attr("msgs", srs_fmt("%" PRId64, nb_frames))->attr("kbps", srs_fmt("%d", kbps->get_recv_kbps_30s()));
            srs_freep(sample);
#endif

        }
    }
    
    return err;
}

srs_error_t SrsRtmpConn::acquire_publish(SrsSharedPtr<SrsLiveSource> source)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;

    // Check whether RTMP stream is busy.
    if (!source->can_publish(info->edge)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtmp: stream %s is busy", req->get_stream_url().c_str());
    }

    // Check whether RTC stream is busy.
#ifdef SRS_RTC
    SrsSharedPtr<SrsRtcSource> rtc;
    bool rtc_server_enabled = _srs_config->get_rtc_server_enabled();
    bool rtc_enabled = _srs_config->get_rtc_enabled(req->vhost);
    if (rtc_server_enabled && rtc_enabled && !info->edge) {
        if ((err = _srs_rtc_sources->fetch_or_create(req, rtc)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        if (!rtc->can_publish()) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtc stream %s busy", req->get_stream_url().c_str());
        }
    }
#endif

    // Check whether SRT stream is busy.
#ifdef SRS_SRT
    bool srt_server_enabled = _srs_config->get_srt_enabled();
    bool srt_enabled = _srs_config->get_srt_enabled(req->vhost);
    if (srt_server_enabled && srt_enabled && !info->edge) {
        SrsSharedPtr<SrsSrtSource> srt;
        if ((err = _srs_srt_sources->fetch_or_create(req, srt)) != srs_success) {
            return srs_error_wrap(err, "create source");
        }

        if (!srt->can_publish()) {
            return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "srt stream %s busy", req->get_stream_url().c_str());
        }
    }
#endif

    // Bridge to RTC streaming.
#if defined(SRS_RTC) && defined(SRS_FFMPEG_FIT)
    if (rtc.get() && _srs_config->get_rtc_from_rtmp(req->vhost)) {
        SrsCompositeBridge* bridge = new SrsCompositeBridge();
        bridge->append(new SrsFrameToRtcBridge(rtc));

        if ((err = bridge->initialize(req)) != srs_success) {
            srs_freep(bridge);
            return srs_error_wrap(err, "bridge init");
        }

        source->set_bridge(bridge);
    }
#endif

    // Start publisher now.
    if (info->edge) {
        err = source->on_edge_start_publish();
    } else {
        err = source->on_publish();
    }

    return err;
}

void SrsRtmpConn::release_publish(SrsSharedPtr<SrsLiveSource> source)
{
    // when edge, notice edge to change state.
    // when origin, notice all service to unpublish.
    if (info->edge) {
        source->on_edge_proxy_unpublish();
    } else {
        source->on_unpublish();
    }
}

srs_error_t SrsRtmpConn::handle_publish_message(SrsSharedPtr<SrsLiveSource>& source, SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    // process publish event.
    if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
        SrsPacket* pkt_raw = NULL;
        if ((err = rtmp->decode_message(msg, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "rtmp: decode message");
        }
        SrsUniquePtr<SrsPacket> pkt(pkt_raw);

        // for flash, any packet is republish.
        if (info->type == SrsRtmpConnFlashPublish) {
            // flash unpublish.
            // TODO: maybe need to support republish.
            srs_trace("flash flash publish finished.");
            return srs_error_new(ERROR_CONTROL_REPUBLISH, "rtmp: republish");
        }
        
        // for fmle, drop others except the fmle start packet.
        if (dynamic_cast<SrsFMLEStartPacket*>(pkt.get())) {
            SrsFMLEStartPacket* unpublish = dynamic_cast<SrsFMLEStartPacket*>(pkt.get());
            if ((err = rtmp->fmle_unpublish(info->res->stream_id, unpublish->transaction_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: republish");
            }
            return srs_error_new(ERROR_CONTROL_REPUBLISH, "rtmp: republish");
        }
        
        srs_trace("fmle ignore AMF0/AMF3 command message.");
        return err;
    }
    
    // video, audio, data message
    if ((err = process_publish_message(source, msg)) != srs_success) {
        return srs_error_wrap(err, "rtmp: consume message");
    }
    
    return err;
}

srs_error_t SrsRtmpConn::process_publish_message(SrsSharedPtr<SrsLiveSource>& source, SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    // for edge, directly proxy message to origin.
    if (info->edge) {
        if ((err = source->on_edge_proxy_publish(msg)) != srs_success) {
            return srs_error_wrap(err, "rtmp: proxy publish");
        }
        return err;
    }
    
    // process audio packet
    if (msg->header.is_audio()) {
        if ((err = source->on_audio(msg)) != srs_success) {
            return srs_error_wrap(err, "rtmp: consume audio");
        }
        return err;
    }
    // process video packet
    if (msg->header.is_video()) {
        if ((err = source->on_video(msg)) != srs_success) {
            return srs_error_wrap(err, "rtmp: consume video");
        }
        return err;
    }
    
    // process aggregate packet
    if (msg->header.is_aggregate()) {
        if ((err = source->on_aggregate(msg)) != srs_success) {
            return srs_error_wrap(err, "rtmp: consume aggregate");
        }
        return err;
    }
    
    // process onMetaData
    if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
        SrsPacket* pkt_raw = NULL;
        if ((err = rtmp->decode_message(msg, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "rtmp: decode message");
        }
        SrsUniquePtr<SrsPacket> pkt(pkt_raw);

        if (dynamic_cast<SrsOnMetaDataPacket*>(pkt.get())) {
            SrsOnMetaDataPacket* metadata = dynamic_cast<SrsOnMetaDataPacket*>(pkt.get());
            if ((err = source->on_meta_data(msg, metadata)) != srs_success) {
                return srs_error_wrap(err, "rtmp: consume metadata");
            }
            return err;
        }
        return err;
    }
    
    return err;
}

srs_error_t SrsRtmpConn::process_play_control_msg(SrsLiveConsumer* consumer, SrsCommonMessage* msg_raw)
{
    srs_error_t err = srs_success;
    
    if (!msg_raw) {
        return err;
    }
    SrsUniquePtr<SrsCommonMessage> msg(msg_raw);

    if (!msg->header.is_amf0_command() && !msg->header.is_amf3_command()) {
        return err;
    }
    
    SrsPacket* pkt_raw = NULL;
    if ((err = rtmp->decode_message(msg.get(), &pkt_raw)) != srs_success) {
        return srs_error_wrap(err, "rtmp: decode message");
    }
    SrsUniquePtr<SrsPacket> pkt(pkt_raw);

    // for jwplayer/flowplayer, which send close as pause message.
    SrsCloseStreamPacket* close = dynamic_cast<SrsCloseStreamPacket*>(pkt.get());
    if (close) {
        return srs_error_new(ERROR_CONTROL_RTMP_CLOSE, "rtmp: close stream");
    }
    
    // call msg,
    // support response null first,
    // TODO: FIXME: response in right way, or forward in edge mode.
    SrsCallPacket* call = dynamic_cast<SrsCallPacket*>(pkt.get());
    if (call) {
        // only response it when transaction id not zero,
        // for the zero means donot need response.
        if (call->transaction_id > 0) {
            SrsCallResPacket* res = new SrsCallResPacket(call->transaction_id);
            res->command_object = SrsAmf0Any::null();
            res->response = SrsAmf0Any::null();
            if ((err = rtmp->send_and_free_packet(res, 0)) != srs_success) {
                return srs_error_wrap(err, "rtmp: send packets");
            }
        }
        return err;
    }
    
    // pause
    SrsPausePacket* pause = dynamic_cast<SrsPausePacket*>(pkt.get());
    if (pause) {
        if ((err = rtmp->on_play_client_pause(info->res->stream_id, pause->is_pause)) != srs_success) {
            return srs_error_wrap(err, "rtmp: pause");
        }
        if ((err = consumer->on_play_client_pause(pause->is_pause)) != srs_success) {
            return srs_error_wrap(err, "rtmp: pause");
        }
        return err;
    }
    
    // other msg.
    return err;
}

void SrsRtmpConn::set_sock_options()
{
    SrsRequest* req = info->req;
    
    bool nvalue = _srs_config->get_tcp_nodelay(req->vhost);
    if (nvalue != tcp_nodelay) {
        tcp_nodelay = nvalue;
        
        srs_error_t err = skt->set_tcp_nodelay(tcp_nodelay);
        if (err != srs_success) {
            srs_warn("ignore err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
}

srs_error_t SrsRtmpConn::check_edge_token_traverse_auth()
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    srs_assert(req);
    
    vector<string> args = _srs_config->get_vhost_edge_origin(req->vhost)->args;
    if (args.empty()) {
        return err;
    }
    
    for (int i = 0; i < (int)args.size(); i++) {
        string hostport = args.at(i);
        
        // select the origin.
        string server;
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        srs_parse_hostport(hostport, server, port);

        SrsUniquePtr<SrsTcpClient> transport(new SrsTcpClient(server, port, SRS_EDGE_TOKEN_TRAVERSE_TIMEOUT));
        if ((err = transport->connect()) != srs_success) {
            srs_warn("Illegal edge token, tcUrl=%s, %s", req->tcUrl.c_str(), srs_error_desc(err).c_str());
            srs_freep(err);
            continue;
        }

        SrsUniquePtr<SrsRtmpClient> client(new SrsRtmpClient(transport.get()));
        return do_token_traverse_auth(client.get());
    }
    
    return srs_error_new(ERROR_EDGE_PORT_INVALID, "rtmp: Illegal edge token, server=%d", (int)args.size());
}

srs_error_t SrsRtmpConn::do_token_traverse_auth(SrsRtmpClient* client)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    srs_assert(client);
    
    client->set_recv_timeout(SRS_CONSTS_RTMP_TIMEOUT);
    client->set_send_timeout(SRS_CONSTS_RTMP_TIMEOUT);
    
    if ((err = client->handshake()) != srs_success) {
        return srs_error_wrap(err, "rtmp: handshake");
    }
    
    // for token tranverse, always take the debug info(which carries token).
    SrsServerInfo si;
    if ((err = client->connect_app(req->app, req->tcUrl, req, true, &si)) != srs_success) {
        return srs_error_wrap(err, "rtmp: connect tcUrl");
    }
    
    srs_trace("edge token auth ok, tcUrl=%s", req->tcUrl.c_str());
    return err;
}

srs_error_t SrsRtmpConn::on_disconnect()
{
    srs_error_t err = srs_success;
    
    http_hooks_on_close();
    
    // TODO: FIXME: Implements it.
    
    return err;
}

srs_error_t SrsRtmpConn::http_hooks_on_connect()
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_connect(req->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_connect(url, req)) != srs_success) {
            return srs_error_wrap(err, "rtmp on_connect %s", url.c_str());
        }
    }
    
    return err;
}

void SrsRtmpConn::http_hooks_on_close()
{
    SrsRequest* req = info->req;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_close(req->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_close(url, req, skt->get_send_bytes(), skt->get_recv_bytes());
    }
}

srs_error_t SrsRtmpConn::http_hooks_on_publish()
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return err;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_publish(req->vhost);
        
        if (!conf) {
            return err;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = SrsHttpHooks::on_publish(url, req)) != srs_success) {
            return srs_error_wrap(err, "rtmp on_publish %s", url.c_str());
        }
    }
    
    return err;
}

void SrsRtmpConn::http_hooks_on_unpublish()
{
    SrsRequest* req = info->req;
    
    if (!_srs_config->get_vhost_http_hooks_enabled(req->vhost)) {
        return;
    }
    
    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;
    
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_on_unpublish(req->vhost);
        
        if (!conf) {
            return;
        }
        
        hooks = conf->args;
    }
    
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        SrsHttpHooks::on_unpublish(url, req);
    }
}

srs_error_t SrsRtmpConn::http_hooks_on_play()
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
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
            return srs_error_wrap(err, "rtmp on_play %s", url.c_str());
        }
    }
    
    return err;
}

void SrsRtmpConn::http_hooks_on_stop()
{
    SrsRequest* req = info->req;
    
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

srs_error_t SrsRtmpConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsRtmpConn::cycle()
{
    srs_error_t err = srs_success;

    // Serve the client.
    err = do_cycle();

#ifdef SRS_APM
    // Final APM span, parent is the last span, not the root span. Note that only client or server kind will be filtered
    // for error or exception report.
    SrsUniquePtr<ISrsApmSpan> span_final(_srs_apm->span("final")->set_kind(SrsApmKindServer)->as_child(span_client_));
    if (srs_error_code(err) != 0) {
        span_final->record_error(err)->set_status(SrsApmStatusError, srs_fmt("fail code=%d", srs_error_code(err)));
    }
#endif

    // Update statistic when done.
    SrsStatistic* stat = SrsStatistic::instance();
    stat->kbps_add_delta(get_id().c_str(), delta_);
    stat->on_disconnect(get_id().c_str(), err);

    // Notify manager to remove it.
    // Note that we create this object, so we use manager to remove it.
    manager->remove(this);

    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

string SrsRtmpConn::remote_ip()
{
    return ip;
}

const SrsContextId& SrsRtmpConn::get_id()
{
    return trd->cid();
}

void SrsRtmpConn::expire()
{
    trd->interrupt();
}

