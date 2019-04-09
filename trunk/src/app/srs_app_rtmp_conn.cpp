/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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
#include <srs_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_config.hpp>
#include <srs_app_refer.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_bandwidth.hpp>
#include <srs_app_st.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_utility.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_recv_thread.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_security.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_kafka.hpp>

// when stream is busy, for example, streaming is already
// publishing, when a new client to request to publish,
// sleep a while and close the connection.
#define SRS_STREAM_BUSY_CIMS (3000)

// the timeout in ms to wait encoder to republish
// if timeout, close the connection.
#define SRS_REPUBLISH_SEND_TMMS (3 * SRS_UTIME_MINUTES)
// if timeout, close the connection.
#define SRS_REPUBLISH_RECV_TMMS (3 * SRS_UTIME_MINUTES)

// the timeout in ms to wait client data, when client paused
// if timeout, close the connection.
#define SRS_PAUSED_SEND_TMMS (3 * SRS_UTIME_MINUTES)
// if timeout, close the connection.
#define SRS_PAUSED_RECV_TMMS (3 * SRS_UTIME_MINUTES)

// when edge timeout, retry next.
#define SRS_EDGE_TOKEN_TRAVERSE_TMMS (3000)

SrsSimpleRtmpClient::SrsSimpleRtmpClient(string u, int64_t ctm, int64_t stm) : SrsBasicRtmpClient(u, ctm, stm)
{
}

SrsSimpleRtmpClient::~SrsSimpleRtmpClient()
{
}

srs_error_t SrsSimpleRtmpClient::connect_app()
{
    std::vector<std::string> ips = srs_get_local_ips();
    assert(_srs_config->get_stats_network() < (int)ips.size());
    std::string local_ip = ips[_srs_config->get_stats_network()];
    
    bool debug_srs_upnode = _srs_config->get_debug_srs_upnode(req->vhost);
    
    return do_connect_app(local_ip, debug_srs_upnode);
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

SrsRtmpConn::SrsRtmpConn(SrsServer* svr, srs_netfd_t c, string cip) : SrsConnection(svr, c, cip)
{
    server = svr;
    
    rtmp = new SrsRtmpServer(skt);
    refer = new SrsRefer();
    bandwidth = new SrsBandwidth();
    security = new SrsSecurity();
    duration = 0;
    wakable = NULL;
    
    mw_sleep = SRS_PERF_MW_SLEEP;
    mw_enabled = false;
    realtime = SRS_PERF_MIN_LATENCY_ENABLED;
    send_min_interval = 0;
    tcp_nodelay = false;
    info = new SrsClientInfo();
    
    _srs_config->subscribe(this);
}

SrsRtmpConn::~SrsRtmpConn()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(info);
    srs_freep(rtmp);
    srs_freep(refer);
    srs_freep(bandwidth);
    srs_freep(security);
}

void SrsRtmpConn::dispose()
{
    SrsConnection::dispose();
    
    // wakeup the handler which need to notice.
    if (wakable) {
        wakable->wakeup();
    }
}

// TODO: return detail message when error for client.
srs_error_t SrsRtmpConn::do_cycle()
{
    srs_error_t err = srs_success;
    
    srs_trace("RTMP client ip=%s, fd=%d", ip.c_str(), srs_netfd_fileno(stfd));
    
    // notify kafka cluster.
#ifdef SRS_AUTO_KAFKA
    if ((err = _srs_kafka->on_client(srs_id(), SrsListenerRtmpStream, ip)) != srs_success) {
        return srs_error_wrap(err, "kafka on client");
    }
#endif
    
    rtmp->set_recv_timeout(SRS_CONSTS_RTMP_TMMS);
    rtmp->set_send_timeout(SRS_CONSTS_RTMP_TMMS);

    if ((err = rtmp->handshake()) != srs_success) {
        return srs_error_wrap(err, "rtmp handshake");
    }
    
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
        double v = _srs_config->get_send_min_interval(vhost);
        if (v != send_min_interval) {
            srs_trace("apply smi %.2f=>%.2f", send_min_interval, v);
            send_min_interval = v;
        }
    }
    
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

void SrsRtmpConn::remark(int64_t* in, int64_t* out)
{
    kbps->remark(in, out);
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
    
    // do bandwidth test if connect to the vhost which is for bandwidth check.
    if (_srs_config->get_bw_check_enabled(req->vhost)) {
        if ((err = bandwidth->bandwidth_check(rtmp, skt, req, local_ip)) != srs_success) {
            return srs_error_wrap(err, "rtmp: bandwidth check");
        }
        return err;
    }
    
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
    
    if ((err = rtmp->on_bw_done()) != srs_success) {
        return srs_error_wrap(err, "rtmp: on bw down");
    }
    
    while (true) {
        srs_error_t err = srs_success;
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtmp: thread quit");
        }
        
        err = stream_service_cycle();
        
        // stream service must terminated with error, never success.
        // when terminated with success, it's user required to stop.
        if (srs_error_code(err) == ERROR_SUCCESS) {
            srs_freep(err);
            continue;
        }
        
        // when not system control error, fatal error, return.
        if (!srs_is_system_control_error(err)) {
            return srs_error_wrap(err, "rtmp: stream service");
        }
        
        // for republish, continue service
        if (srs_error_code(err) == ERROR_CONTROL_REPUBLISH) {
            // set timeout to a larger value, wait for encoder to republish.
            rtmp->set_send_timeout(SRS_REPUBLISH_RECV_TMMS);
            rtmp->set_recv_timeout(SRS_REPUBLISH_SEND_TMMS);
            
            srs_trace("rtmp: retry for republish");
            srs_freep(err);
            continue;
        }
        
        // for "some" system control error,
        // logical accept and retry stream service.
        if (srs_error_code(err) == ERROR_CONTROL_RTMP_CLOSE) {
            // TODO: FIXME: use ping message to anti-death of socket.
            // @see: https://github.com/ossrs/srs/issues/39
            // set timeout to a larger value, for user paused.
            rtmp->set_recv_timeout(SRS_PAUSED_RECV_TMMS);
            rtmp->set_send_timeout(SRS_PAUSED_SEND_TMMS);
            
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
    req->strip();
    srs_trace("client identified, type=%s, vhost=%s, app=%s, stream=%s, param=%s, duration=%.2f",
        srs_client_type_string(info->type).c_str(), req->vhost.c_str(), req->app.c_str(), req->stream.c_str(), req->param.c_str(), req->duration);
    
    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(req->vhost);
    if (parsed_vhost) {
        req->vhost = parsed_vhost->arg0();
    }

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
    rtmp->set_recv_timeout(SRS_CONSTS_RTMP_TMMS);
    rtmp->set_send_timeout(SRS_CONSTS_RTMP_TMMS);
    
    // find a source to serve.
    SrsSource* source = NULL;
    if ((err = SrsSource::fetch_or_create(req, server, &source)) != srs_success) {
        return srs_error_wrap(err, "rtmp: fetch source");
    }
    srs_assert(source != NULL);
    
    // update the statistic when source disconveried.
    SrsStatistic* stat = SrsStatistic::instance();
    if ((err = stat->on_client(_srs_context->get_id(), req, this, info->type)) != srs_success) {
        return srs_error_wrap(err, "rtmp: stat client");
    }
    
    bool enabled_cache = _srs_config->get_gop_cache(req->vhost);
    srs_trace("source url=%s, ip=%s, cache=%d, is_edge=%d, source_id=%d[%d]",
        req->get_stream_url().c_str(), ip.c_str(), enabled_cache, info->edge, source->source_id(), source->source_id());
    source->set_cache(enabled_cache);
    
    switch (info->type) {
        case SrsRtmpConnPlay: {
            // response connection start play
            if ((err = rtmp->start_play(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start play");
            }
            if ((err = http_hooks_on_play()) != srs_success) {
                return srs_error_wrap(err, "rtmp: callback on play");
            }
            
            err = playing(source);
            http_hooks_on_stop();
            
            return err;
        }
        case SrsRtmpConnFMLEPublish: {
            if ((err = rtmp->start_fmle_publish(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start FMLE publish");
            }
            
            return publishing(source);
        }
        case SrsRtmpConnHaivisionPublish: {
            if ((err = rtmp->start_haivision_publish(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start HAIVISION publish");
            }
            
            return publishing(source);
        }
        case SrsRtmpConnFlashPublish: {
            if ((err = rtmp->start_flash_publish(info->res->stream_id)) != srs_success) {
                return srs_error_wrap(err, "rtmp: start FLASH publish");
            }
            
            return publishing(source);
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

srs_error_t SrsRtmpConn::playing(SrsSource* source)
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
            int port;
            string host;
            string url = "http://" + coworkers.at(i) + "/api/v1/clusters?"
                + "vhost=" + req->vhost + "&ip=" + req->host + "&app=" + req->app + "&stream=" + req->stream;
            if ((err = SrsHttpHooks::discover_co_workers(url, host, port)) != srs_success) {
                return srs_error_wrap(err, "discover coworkers, url=%s", url.c_str());
            }
            srs_trace("rtmp: redirect in cluster, url=%s, target=%s:%d", url.c_str(), host.c_str(), port);
            
            bool accepted = false;
            if ((err = rtmp->redirect(req, host, port, accepted)) != srs_success) {
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
    SrsConsumer* consumer = NULL;
    if ((err = source->create_consumer(this, consumer)) != srs_success) {
        return srs_error_wrap(err, "rtmp: create consumer");
    }
    SrsAutoFree(SrsConsumer, consumer);
    
    // Use receiving thread to receive packets from peer.
    // @see: https://github.com/ossrs/srs/issues/217
    SrsQueueRecvThread trd(consumer, rtmp, srsu2msi(SRS_PERF_MW_SLEEP));
    
    if ((err = trd.start()) != srs_success) {
        return srs_error_wrap(err, "rtmp: start receive thread");
    }
    
    // Deliver packets to peer.
    wakable = consumer;
    err = do_playing(source, consumer, &trd);
    wakable = NULL;
    
    trd.stop();
    
    // Drop all packets in receiving thread.
    if (!trd.empty()) {
        srs_warn("drop the received %d messages", trd.size());
    }
    
    return err;
}

srs_error_t SrsRtmpConn::do_playing(SrsSource* source, SrsConsumer* consumer, SrsQueueRecvThread* rtrd)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    srs_assert(req);
    srs_assert(consumer);
    
    // initialize other components
    SrsPithyPrint* pprint = SrsPithyPrint::create_rtmp_play();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SRS_PERF_MW_MSGS);
    bool user_specified_duration_to_stop = (req->duration > 0);
    int64_t starttime = -1;
    
    // setup the realtime.
    realtime = _srs_config->get_realtime_enabled(req->vhost);
    // setup the mw config.
    // when mw_sleep changed, resize the socket send buffer.
    mw_enabled = true;
    change_mw_sleep(_srs_config->get_mw_sleep(req->vhost));
    // initialize the send_min_interval
    send_min_interval = _srs_config->get_send_min_interval(req->vhost);
    
    srs_trace("start play smi=%.2f, mw_sleep=%d, mw_enabled=%d, realtime=%d, tcp_nodelay=%d",
        send_min_interval, srsu2msi(mw_sleep), mw_enabled, realtime, tcp_nodelay);
    
    while (true) {
        // collect elapse for pithy print.
        pprint->elapse();
        
        // when source is set to expired, disconnect it.
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtmp: thread quit");
        }
        
        // to use isolate thread to recv, can improve about 33% performance.
        // @see: https://github.com/ossrs/srs/issues/196
        // @see: https://github.com/ossrs/srs/issues/217
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
        // @see https://github.com/ossrs/srs/issues/251
        // @see https://github.com/ossrs/srs/issues/257
        if (realtime) {
            // for realtime, min required msgs is 0, send when got one+ msgs.
            consumer->wait(0, srsu2msi(mw_sleep));
        } else {
            // for no-realtime, got some msgs then send.
            consumer->wait(SRS_PERF_MW_MIN_MSGS, srsu2msi(mw_sleep));
        }
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
            srs_trace("-> " SRS_CONSTS_LOG_PLAY " time=%d, msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d, mw=%d",
                (int)pprint->age(), count, kbps->get_send_kbps(), kbps->get_send_kbps_30s(), kbps->get_send_kbps_5m(),
                kbps->get_recv_kbps(), kbps->get_recv_kbps_30s(), kbps->get_recv_kbps_5m(), srsu2msi(mw_sleep));
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
                duration += msg->timestamp - starttime;
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
            if (duration >= (int64_t)req->duration) {
                return srs_error_new(ERROR_RTMP_DURATION_EXCEED, "rtmp: time %d up %d", (int)duration, (int)req->duration);
            }
        }
        
        // apply the minimal interval for delivery stream in ms.
        if (send_min_interval > 0) {
            srs_usleep((int64_t)(send_min_interval * 1000));
        }
    }
    
    return err;
}

srs_error_t SrsRtmpConn::publishing(SrsSource* source)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (_srs_config->get_refer_enabled(req->vhost)) {
        if ((err = refer->check(req->pageUrl, _srs_config->get_refer_publish(req->vhost))) != srs_success) {
            return srs_error_wrap(err, "rtmp: referer check");
        }
    }
    
    if ((err = http_hooks_on_publish()) != srs_success) {
        return srs_error_wrap(err, "rtmp: callback on publish");
    }
    
    // TODO: FIXME: Should refine the state of publishing.
    if ((err = acquire_publish(source)) == srs_success) {
        // use isolate thread to recv,
        // @see: https://github.com/ossrs/srs/issues/237
        SrsPublishRecvThread rtrd(rtmp, req, srs_netfd_fileno(stfd), 0, this, source);
        err = do_publishing(source, &rtrd);
        rtrd.stop();
    }
    
    // whatever the acquire publish, always release publish.
    // when the acquire error in the midlle-way, the publish state changed,
    // but failed, so we must cleanup it.
    // @see https://github.com/ossrs/srs/issues/474
    // @remark when stream is busy, should never release it.
    if (srs_error_code(err) != ERROR_SYSTEM_STREAM_BUSY) {
        release_publish(source);
    }
    
    http_hooks_on_unpublish();
    
    return err;
}

srs_error_t SrsRtmpConn::do_publishing(SrsSource* source, SrsPublishRecvThread* rtrd)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    SrsPithyPrint* pprint = SrsPithyPrint::create_rtmp_publish();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    // start isolate recv thread.
    if ((err = rtrd->start()) != srs_success) {
        return srs_error_wrap(err, "rtmp: receive thread");
    }
    
    // change the isolate recv thread context id,
    // merge its log to current thread.
    int receive_thread_cid = rtrd->get_cid();
    rtrd->set_cid(_srs_context->get_id());
    
    // initialize the publish timeout.
    publish_1stpkt_timeout = _srs_config->get_publish_1stpkt_timeout(req->vhost);
    publish_normal_timeout = _srs_config->get_publish_normal_timeout(req->vhost);
    
    // set the sock options.
    set_sock_options();
    
    if (true) {
        bool mr = _srs_config->get_mr_enabled(req->vhost);
        srs_utime_t mr_sleep = _srs_config->get_mr_sleep(req->vhost);
        srs_trace("start publish mr=%d/%d, p1stpt=%d, pnt=%d, tcp_nodelay=%d, rtcid=%d",
            mr, srsu2msi(mr_sleep), srsu2msi(publish_1stpkt_timeout), srsu2msi(publish_normal_timeout),
            tcp_nodelay, receive_thread_cid);
    }
    
    int64_t nb_msgs = 0;
    uint64_t nb_frames = 0;
    while (true) {
        pprint->elapse();
        
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "rtmp: thread quit");
        }
        
        // cond wait for timeout.
        if (nb_msgs == 0) {
            // when not got msgs, wait for a larger timeout.
            // @see https://github.com/ossrs/srs/issues/441
            rtrd->wait(srsu2msi(publish_1stpkt_timeout));
        } else {
            rtrd->wait(srsu2msi(publish_normal_timeout));
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
        }
    }
    
    return err;
}

srs_error_t SrsRtmpConn::acquire_publish(SrsSource* source)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    
    if (!source->can_publish(info->edge)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtmp: stream %s is busy", req->get_stream_url().c_str());
    }
    
    // when edge, ignore the publish event, directly proxy it.
    if (info->edge) {
        if ((err = source->on_edge_start_publish()) != srs_success) {
            return srs_error_wrap(err, "rtmp: edge start publish");
        }
    } else {
        if ((err = source->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtmp: source publish");
        }
    }
    
    return err;
}

void SrsRtmpConn::release_publish(SrsSource* source)
{
    // when edge, notice edge to change state.
    // when origin, notice all service to unpublish.
    if (info->edge) {
        source->on_edge_proxy_unpublish();
    } else {
        source->on_unpublish();
    }
}

srs_error_t SrsRtmpConn::handle_publish_message(SrsSource* source, SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    // process publish event.
    if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
        SrsPacket* pkt = NULL;
        if ((err = rtmp->decode_message(msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "rtmp: decode message");
        }
        SrsAutoFree(SrsPacket, pkt);
        
        // for flash, any packet is republish.
        if (info->type == SrsRtmpConnFlashPublish) {
            // flash unpublish.
            // TODO: maybe need to support republish.
            srs_trace("flash flash publish finished.");
            return srs_error_new(ERROR_CONTROL_REPUBLISH, "rtmp: republish");
        }
        
        // for fmle, drop others except the fmle start packet.
        if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
            SrsFMLEStartPacket* unpublish = dynamic_cast<SrsFMLEStartPacket*>(pkt);
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

srs_error_t SrsRtmpConn::process_publish_message(SrsSource* source, SrsCommonMessage* msg)
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
        SrsPacket* pkt = NULL;
        if ((err = rtmp->decode_message(msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "rtmp: decode message");
        }
        SrsAutoFree(SrsPacket, pkt);
        
        if (dynamic_cast<SrsOnMetaDataPacket*>(pkt)) {
            SrsOnMetaDataPacket* metadata = dynamic_cast<SrsOnMetaDataPacket*>(pkt);
            if ((err = source->on_meta_data(msg, metadata)) != srs_success) {
                return srs_error_wrap(err, "rtmp: consume metadata");
            }
            return err;
        }
        return err;
    }
    
    return err;
}

srs_error_t SrsRtmpConn::process_play_control_msg(SrsConsumer* consumer, SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    if (!msg) {
        return err;
    }
    SrsAutoFree(SrsCommonMessage, msg);
    
    if (!msg->header.is_amf0_command() && !msg->header.is_amf3_command()) {
        return err;
    }
    
    SrsPacket* pkt = NULL;
    if ((err = rtmp->decode_message(msg, &pkt)) != srs_success) {
        return srs_error_wrap(err, "rtmp: decode message");
    }
    SrsAutoFree(SrsPacket, pkt);
    
    // for jwplayer/flowplayer, which send close as pause message.
    // @see https://github.com/ossrs/srs/issues/6
    SrsCloseStreamPacket* close = dynamic_cast<SrsCloseStreamPacket*>(pkt);
    if (close) {
        return srs_error_new(ERROR_CONTROL_RTMP_CLOSE, "rtmp: close stream");
    }
    
    // call msg,
    // support response null first,
    // @see https://github.com/ossrs/srs/issues/106
    // TODO: FIXME: response in right way, or forward in edge mode.
    SrsCallPacket* call = dynamic_cast<SrsCallPacket*>(pkt);
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
    SrsPausePacket* pause = dynamic_cast<SrsPausePacket*>(pkt);
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

void SrsRtmpConn::change_mw_sleep(srs_utime_t sleep_v)
{
    if (!mw_enabled) {
        return;
    }
    
    set_socket_buffer(sleep_v);
    mw_sleep = sleep_v;
}

void SrsRtmpConn::set_sock_options()
{
    SrsRequest* req = info->req;
    
    bool nvalue = _srs_config->get_tcp_nodelay(req->vhost);
    if (nvalue != tcp_nodelay) {
        tcp_nodelay = nvalue;
        
        srs_error_t err = set_tcp_nodelay(tcp_nodelay);
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
        
        SrsTcpClient* transport = new SrsTcpClient(server, port, SRS_EDGE_TOKEN_TRAVERSE_TMMS);
        SrsAutoFree(SrsTcpClient, transport);
        
        if ((err = transport->connect()) != srs_success) {
            srs_warn("Illegal edge token, tcUrl=%s, %s", req->tcUrl.c_str(), srs_error_desc(err).c_str());
            srs_freep(err);
            continue;
        }
        
        SrsRtmpClient* client = new SrsRtmpClient(transport);
        SrsAutoFree(SrsRtmpClient, client);
        return do_token_traverse_auth(client);
    }
    
    return srs_error_new(ERROR_EDGE_PORT_INVALID, "rtmp: Illegal edge token, server=%d", (int)args.size());
}

srs_error_t SrsRtmpConn::do_token_traverse_auth(SrsRtmpClient* client)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = info->req;
    srs_assert(client);
    
    client->set_recv_timeout(SRS_CONSTS_RTMP_TMMS);
    client->set_send_timeout(SRS_CONSTS_RTMP_TMMS);
    
    if ((err = client->handshake()) != srs_success) {
        return srs_error_wrap(err, "rtmp: handshake");
    }
    
    // for token tranverse, always take the debug info(which carries token).
    if ((err = client->connect_app(req->app, req->tcUrl, req, true, NULL)) != srs_success) {
        return srs_error_wrap(err, "rtmp: connect tcUrl");
    }
    
    srs_trace("edge token auth ok, tcUrl=%s", req->tcUrl.c_str());
    return err;
}

srs_error_t SrsRtmpConn::on_disconnect()
{
    srs_error_t err = srs_success;
    
    http_hooks_on_close();
    
#ifdef SRS_AUTO_KAFKA
    if ((err = _srs_kafka->on_close(srs_id())) != srs_success) {
        return srs_error_wrap(err, "kafka on close");
    }
#endif
    
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
        SrsHttpHooks::on_close(url, req, kbps->get_send_bytes(), kbps->get_recv_bytes());
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

