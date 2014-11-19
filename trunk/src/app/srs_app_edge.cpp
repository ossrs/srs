/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_app_edge.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_io.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_kbps.hpp>
#include <srs_protocol_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_SLEEP_US (int64_t)(1*1000*1000LL)

// when edge timeout, retry next.
#define SRS_EDGE_INGESTER_TIMEOUT_US (int64_t)(3*1000*1000LL)

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_FORWARDER_SLEEP_US (int64_t)(1*1000*1000LL)

// when edge timeout, retry next.
#define SRS_EDGE_FORWARDER_TIMEOUT_US (int64_t)(3*1000*1000LL)

// when edge error, wait for quit
#define SRS_EDGE_FORWARDER_ERROR_US (int64_t)(50*1000LL)

SrsEdgeIngester::SrsEdgeIngester()
{
    io = NULL;
    kbps = new SrsKbps();
    client = NULL;
    _edge = NULL;
    _req = NULL;
    origin_index = 0;
    stream_id = 0;
    stfd = NULL;
    pthread = new SrsThread(this, SRS_EDGE_INGESTER_SLEEP_US, true);
}

SrsEdgeIngester::~SrsEdgeIngester()
{
    stop();
    
    srs_freep(pthread);
    srs_freep(kbps);
}

int SrsEdgeIngester::initialize(SrsSource* source, SrsPlayEdge* edge, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    _source = source;
    _edge = edge;
    _req = req;
    
    return ret;
}

int SrsEdgeIngester::start()
{
    return pthread->start();
}

void SrsEdgeIngester::stop()
{
    pthread->stop();
    
    close_underlayer_socket();
    
    srs_freep(client);
    srs_freep(io);
    kbps->set_io(NULL, NULL);
    
    // notice to unpublish.
    _source->on_unpublish();
}

int SrsEdgeIngester::cycle()
{
    int ret = ERROR_SUCCESS;
    
    std::string ep_server, ep_port;
    if ((ret = connect_server(ep_server, ep_port)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(client);

    client->set_recv_timeout(SRS_CONSTS_RTMP_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_CONSTS_RTMP_SEND_TIMEOUT_US);

    SrsRequest* req = _req;
    
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = connect_app(ep_server, ep_port)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = client->create_stream(stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream_id=%d. ret=%d", stream_id, ret);
        return ret;
    }
    
    if ((ret = client->play(req->stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream=%s, stream_id=%d. ret=%d", 
            req->stream.c_str(), stream_id, ret);
        return ret;
    }
    
    if ((ret = _source->on_publish()) != ERROR_SUCCESS) {
        srs_error("edge pull stream then publish to edge failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = _edge->on_ingest_play()) != ERROR_SUCCESS) {
        return ret;
    }
    
    ret = ingest();
    if (srs_is_client_gracefully_close(ret)) {
        srs_warn("origin disconnected, retry. ret=%d", ret);
        ret = ERROR_SUCCESS;
    }
    
    return ret;
}

int SrsEdgeIngester::ingest()
{
    int ret = ERROR_SUCCESS;
    
    client->set_recv_timeout(SRS_EDGE_INGESTER_TIMEOUT_US);
    
    SrsPithyPrint pithy_print(SRS_CONSTS_STAGE_EDGE);

    while (pthread->can_loop()) {
        pithy_print.elapse();
        
        // pithy print
        if (pithy_print.can_print()) {
            kbps->sample();
            srs_trace("<- "SRS_CONSTS_LOG_EDGE_PLAY
                " time=%"PRId64", okbps=%d,%d,%d, ikbps=%d,%d,%d", 
                pithy_print.age(),
                kbps->get_send_kbps(), kbps->get_send_kbps_30s(), kbps->get_send_kbps_5m(),
                kbps->get_recv_kbps(), kbps->get_recv_kbps_30s(), kbps->get_recv_kbps_5m());
        }

        // read from client.
        SrsMessage* msg = NULL;
        if ((ret = client->recv_message(&msg)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("pull origin server message failed. ret=%d", ret);
            }
            return ret;
        }
        srs_verbose("edge loop recv message. ret=%d", ret);
        
        srs_assert(msg);
        SrsAutoFree(SrsMessage, msg);
        
        if ((ret = process_publish_message(msg)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

// TODO: FIXME: refine the connect_app.
int SrsEdgeIngester::connect_app(string ep_server, string ep_port)
{
    int ret = ERROR_SUCCESS;
    
    SrsRequest* req = _req;
    
    // args of request takes the srs info.
    if (req->args == NULL) {
        req->args = SrsAmf0Any::object();
    }
    
    // notify server the edge identity,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/147
    SrsAmf0Object* data = req->args;
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_role", SrsAmf0Any::str(RTMP_SIG_SRS_ROLE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    data->set("srs_site", SrsAmf0Any::str(RTMP_SIG_SRS_WEB));
    data->set("srs_email", SrsAmf0Any::str(RTMP_SIG_SRS_EMAIL));
    data->set("srs_copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("srs_primary_authors", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY_AUTHROS));
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::number(_srs_context->get_id()));
    
    // local ip of edge
    std::vector<std::string> ips = srs_get_local_ipv4_ips();
    assert(_srs_config->get_stats_network() < (int)ips.size());
    std::string local_ip = ips[_srs_config->get_stats_network()];
    data->set("srs_server_ip", SrsAmf0Any::str(local_ip.c_str()));
    
    // generate the tcUrl
    std::string param = "";
    std::string tc_url = srs_generate_tc_url(ep_server, req->vhost, req->app, ep_port, param);
    
    // upnode server identity will show in the connect_app of client.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/160
    // the debug_srs_upnode is config in vhost and default to true.
    bool debug_srs_upnode = _srs_config->get_debug_srs_upnode(req->vhost);
    if ((ret = client->connect_app(req->app, tc_url, req, debug_srs_upnode)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, tcUrl=%s, dsu=%d. ret=%d", 
            tc_url.c_str(), debug_srs_upnode, ret);
        return ret;
    }
    
    return ret;
}

int SrsEdgeIngester::process_publish_message(SrsMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    SrsSource* source = _source;
        
    // process audio packet
    if (msg->header.is_audio()) {
        if ((ret = source->on_audio(msg)) != ERROR_SUCCESS) {
            srs_error("source process audio message failed. ret=%d", ret);
            return ret;
        }
    }
    
    // process video packet
    if (msg->header.is_video()) {
        if ((ret = source->on_video(msg)) != ERROR_SUCCESS) {
            srs_error("source process video message failed. ret=%d", ret);
            return ret;
        }
    }
    
    // process aggregate packet
    if (msg->header.is_aggregate()) {
        if ((ret = source->on_aggregate(msg)) != ERROR_SUCCESS) {
            srs_error("source process aggregate message failed. ret=%d", ret);
            return ret;
        }
        return ret;
    }

    // process onMetaData
    if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
        SrsPacket* pkt = NULL;
        if ((ret = client->decode_message(msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("decode onMetaData message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsPacket, pkt);
    
        if (dynamic_cast<SrsOnMetaDataPacket*>(pkt)) {
            SrsOnMetaDataPacket* metadata = dynamic_cast<SrsOnMetaDataPacket*>(pkt);
            if ((ret = source->on_meta_data(msg, metadata)) != ERROR_SUCCESS) {
                srs_error("source process onMetaData message failed. ret=%d", ret);
                return ret;
            }
            srs_info("process onMetaData message success.");
            return ret;
        }
        
        srs_info("ignore AMF0/AMF3 data message.");
        return ret;
    }
    
    return ret;
}

void SrsEdgeIngester::close_underlayer_socket()
{
    srs_close_stfd(stfd);
}

int SrsEdgeIngester::connect_server(string& ep_server, string& ep_port)
{
    int ret = ERROR_SUCCESS;
    
    // reopen
    close_underlayer_socket();
    
    SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(_req->vhost);
    
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/79
    // when origin is error, for instance, server is shutdown,
    // then user remove the vhost then reload, the conf is empty.
    if (!conf) {
        ret = ERROR_EDGE_VHOST_REMOVED;
        srs_warn("vhost %s removed. ret=%d", _req->vhost.c_str(), ret);
        return ret;
    }
    
    // select the origin.
    std::string server = conf->args.at(origin_index % conf->args.size());
    origin_index = (origin_index + 1) % conf->args.size();
    
    std::string s_port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    int port = ::atoi(SRS_CONSTS_RTMP_DEFAULT_PORT);
    size_t pos = server.find(":");
    if (pos != std::string::npos) {
        s_port = server.substr(pos + 1);
        server = server.substr(0, pos);
        port = ::atoi(s_port.c_str());
    }
    
    // output the connected server and port.
    ep_server = server;
    ep_port = s_port;
    
    // open socket.
    int64_t timeout = SRS_EDGE_INGESTER_TIMEOUT_US;
    if ((ret = srs_socket_connect(server, port, timeout, &stfd)) != ERROR_SUCCESS) {
        srs_warn("edge pull failed, stream=%s, tcUrl=%s to server=%s, port=%d, timeout=%"PRId64", ret=%d",
            _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port, timeout, ret);
        return ret;
    }
    
    srs_freep(client);
    srs_freep(io);
    
    srs_assert(stfd);
    io = new SrsStSocket(stfd);
    client = new SrsRtmpClient(io);
    
    kbps->set_io(io, io);
    
    srs_trace("edge pull connected, can_publish=%d, url=%s/%s, server=%s:%d",
        _source->can_publish(), _req->tcUrl.c_str(), _req->stream.c_str(), server.c_str(), port);
    
    return ret;
}

SrsEdgeForwarder::SrsEdgeForwarder()
{
    io = NULL;
    kbps = new SrsKbps();
    client = NULL;
    _edge = NULL;
    _req = NULL;
    origin_index = 0;
    stream_id = 0;
    stfd = NULL;
    pthread = new SrsThread(this, SRS_EDGE_FORWARDER_SLEEP_US, true);
    queue = new SrsMessageQueue();
    send_error_code = ERROR_SUCCESS;
}

SrsEdgeForwarder::~SrsEdgeForwarder()
{
    stop();
    
    srs_freep(pthread);
    srs_freep(queue);
    srs_freep(kbps);
}

void SrsEdgeForwarder::set_queue_size(double queue_size)
{
    return queue->set_queue_size(queue_size);
}

int SrsEdgeForwarder::initialize(SrsSource* source, SrsPublishEdge* edge, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    _source = source;
    _edge = edge;
    _req = req;
    
    return ret;
}

int SrsEdgeForwarder::start()
{
    int ret = ERROR_SUCCESS;
    
    send_error_code = ERROR_SUCCESS;
    
    std::string ep_server, ep_port;
    if ((ret = connect_server(ep_server, ep_port)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(client);

    client->set_recv_timeout(SRS_CONSTS_RTMP_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_CONSTS_RTMP_SEND_TIMEOUT_US);

    SrsRequest* req = _req;
    
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = connect_app(ep_server, ep_port)) != ERROR_SUCCESS) {
        srs_error("connect with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = client->create_stream(stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream_id=%d. ret=%d", stream_id, ret);
        return ret;
    }
    
    if ((ret = client->publish(req->stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream=%s, stream_id=%d. ret=%d", 
            req->stream.c_str(), stream_id, ret);
        return ret;
    }
    
    return pthread->start();
}

void SrsEdgeForwarder::stop()
{
    pthread->stop();
    
    close_underlayer_socket();
    
    srs_freep(client);
    srs_freep(io);
    kbps->set_io(NULL, NULL);
}

#define SYS_MAX_EDGE_SEND_MSGS 128
int SrsEdgeForwarder::cycle()
{
    int ret = ERROR_SUCCESS;
    
    client->set_recv_timeout(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
    
    SrsPithyPrint pithy_print(SRS_CONSTS_STAGE_EDGE);
    
    SrsMessageArray msgs(SYS_MAX_EDGE_SEND_MSGS);

    while (pthread->can_loop()) {
        if (send_error_code != ERROR_SUCCESS) {
            st_usleep(SRS_EDGE_FORWARDER_ERROR_US);
            continue;
        }

        // read from client.
        if (true) {
            SrsMessage* msg = NULL;
            ret = client->recv_message(&msg);
            
            srs_verbose("edge loop recv message. ret=%d", ret);
            if (ret != ERROR_SUCCESS && ret != ERROR_SOCKET_TIMEOUT) {
                srs_error("edge push get server control message failed. ret=%d", ret);
                send_error_code = ret;
                continue;
            }
            
            srs_freep(msg);
        }
        
        // forward all messages.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((ret = queue->dump_packets(msgs.max, msgs.msgs, count)) != ERROR_SUCCESS) {
            srs_error("get message to push to origin failed. ret=%d", ret);
            return ret;
        }
        
        pithy_print.elapse();
        
        // pithy print
        if (pithy_print.can_print()) {
            kbps->sample();
            srs_trace("-> "SRS_CONSTS_LOG_EDGE_PUBLISH
                " time=%"PRId64", msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d", 
                pithy_print.age(), count,
                kbps->get_send_kbps(), kbps->get_send_kbps_30s(), kbps->get_send_kbps_5m(),
                kbps->get_recv_kbps(), kbps->get_recv_kbps_30s(), kbps->get_recv_kbps_5m());
        }
        
        // ignore when no messages.
        if (count <= 0) {
            srs_verbose("no packets to push.");
            continue;
        }
    
        // sendout messages, all messages are freed by send_and_free_messages().
        if ((ret = client->send_and_free_messages(msgs.msgs, count, stream_id)) != ERROR_SUCCESS) {
            srs_error("edge publish push message to server failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsEdgeForwarder::proxy(SrsMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = send_error_code) != ERROR_SUCCESS) {
        srs_error("publish edge proxy thread send error, ret=%d", ret);
        return ret;
    }
    
    // the msg is auto free by source,
    // so we just ignore, or copy then send it.
    if (msg->size <= 0
        || msg->header.is_set_chunk_size()
        || msg->header.is_window_ackledgement_size()
        || msg->header.is_ackledgement()
    ) {
        return ret;
    }
    
    SrsSharedPtrMessage copy;
    if ((ret = copy.create(msg)) != ERROR_SUCCESS) {
        srs_error("initialize the msg failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr msg success.");
    
    copy.header.stream_id = stream_id;
    if ((ret = queue->enqueue(copy.copy())) != ERROR_SUCCESS) {
        srs_error("enqueue edge publish msg failed. ret=%d", ret);
    }
    
    return ret;
}

void SrsEdgeForwarder::close_underlayer_socket()
{
    srs_close_stfd(stfd);
}

int SrsEdgeForwarder::connect_server(string& ep_server, string& ep_port)
{
    int ret = ERROR_SUCCESS;
    
    // reopen
    close_underlayer_socket();
    
    SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(_req->vhost);
    srs_assert(conf);
    
    // select the origin.
    std::string server = conf->args.at(origin_index % conf->args.size());
    origin_index = (origin_index + 1) % conf->args.size();
    
    std::string s_port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    int port = ::atoi(SRS_CONSTS_RTMP_DEFAULT_PORT);
    size_t pos = server.find(":");
    if (pos != std::string::npos) {
        s_port = server.substr(pos + 1);
        server = server.substr(0, pos);
        port = ::atoi(s_port.c_str());
    }
    
    // output the connected server and port.
    ep_server = server;
    ep_port = s_port;
    
    // open socket.
    int64_t timeout = SRS_EDGE_FORWARDER_TIMEOUT_US;
    if ((ret = srs_socket_connect(server, port, timeout, &stfd)) != ERROR_SUCCESS) {
        srs_warn("edge push failed, stream=%s, tcUrl=%s to server=%s, port=%d, timeout=%"PRId64", ret=%d",
            _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port, timeout, ret);
        return ret;
    }
    
    srs_freep(client);
    srs_freep(io);
    
    srs_assert(stfd);
    io = new SrsStSocket(stfd);
    client = new SrsRtmpClient(io);
    
    kbps->set_io(io, io);
    
    // open socket.
    srs_trace("edge push connected, stream=%s, tcUrl=%s to server=%s, port=%d",
        _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port);
    
    return ret;
}

// TODO: FIXME: refine the connect_app.
int SrsEdgeForwarder::connect_app(string ep_server, string ep_port)
{
    int ret = ERROR_SUCCESS;
    
    SrsRequest* req = _req;
    
    // args of request takes the srs info.
    if (req->args == NULL) {
        req->args = SrsAmf0Any::object();
    }
    
    // notify server the edge identity,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/147
    SrsAmf0Object* data = req->args;
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_role", SrsAmf0Any::str(RTMP_SIG_SRS_ROLE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    data->set("srs_site", SrsAmf0Any::str(RTMP_SIG_SRS_WEB));
    data->set("srs_email", SrsAmf0Any::str(RTMP_SIG_SRS_EMAIL));
    data->set("srs_copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("srs_primary_authors", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY_AUTHROS));
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::number(_srs_context->get_id()));
    
    // local ip of edge
    std::vector<std::string> ips = srs_get_local_ipv4_ips();
    assert(_srs_config->get_stats_network() < (int)ips.size());
    std::string local_ip = ips[_srs_config->get_stats_network()];
    data->set("srs_server_ip", SrsAmf0Any::str(local_ip.c_str()));
    
    // generate the tcUrl
    std::string param = "";
    std::string tc_url = srs_generate_tc_url(ep_server, req->vhost, req->app, ep_port, param);
    
    // upnode server identity will show in the connect_app of client.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/160
    // the debug_srs_upnode is config in vhost and default to true.
    bool debug_srs_upnode = _srs_config->get_debug_srs_upnode(req->vhost);
    if ((ret = client->connect_app(req->app, tc_url, req, debug_srs_upnode)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, tcUrl=%s, dsu=%d. ret=%d", 
            tc_url.c_str(), debug_srs_upnode, ret);
        return ret;
    }
    
    return ret;
}

SrsPlayEdge::SrsPlayEdge()
{
    state = SrsEdgeStateInit;
    user_state = SrsEdgeUserStateInit;
    ingester = new SrsEdgeIngester();
}

SrsPlayEdge::~SrsPlayEdge()
{
    srs_freep(ingester);
}

int SrsPlayEdge::initialize(SrsSource* source, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = ingester->initialize(source, this, req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsPlayEdge::on_client_play()
{
    int ret = ERROR_SUCCESS;
    
    // error state.
    if (user_state != SrsEdgeUserStateInit) {
        ret = ERROR_RTMP_EDGE_PLAY_STATE;
        srs_error("invalid state for client to pull stream on edge. "
            "state=%d, user_state=%d, ret=%d", state, user_state, ret);
        return ret;
    }
    
    // start ingest when init state.
    if (state == SrsEdgeStateInit) {
        state = SrsEdgeStatePlay;
        return ingester->start();
    }

    return ret;
}

void SrsPlayEdge::on_all_client_stop()
{
    // when all client disconnected,
    // and edge is ingesting origin stream, abort it.
    if (state == SrsEdgeStatePlay || state == SrsEdgeStateIngestConnected) {
        ingester->stop();
    
        SrsEdgeState pstate = state;
        state = SrsEdgeStateInit;
        srs_trace("edge change from %d to state %d (init).", pstate, state);
        
        return;
    }
}

int SrsPlayEdge::on_ingest_play()
{
    int ret = ERROR_SUCCESS;
    
    // when already connected(for instance, reconnect for error), ignore.
    if (state == SrsEdgeStateIngestConnected) {
        return ret;
    }
    
    srs_assert(state == SrsEdgeStatePlay);
    
    SrsEdgeState pstate = state;
    state = SrsEdgeStateIngestConnected;
    srs_trace("edge change from %d to state %d (pull).", pstate, state);
    
    return ret;
}

SrsPublishEdge::SrsPublishEdge()
{
    state = SrsEdgeStateInit;
    user_state = SrsEdgeUserStateInit;
    forwarder = new SrsEdgeForwarder();
}

SrsPublishEdge::~SrsPublishEdge()
{
    srs_freep(forwarder);
}

void SrsPublishEdge::set_queue_size(double queue_size)
{
    return forwarder->set_queue_size(queue_size);
}

int SrsPublishEdge::initialize(SrsSource* source, SrsRequest* req)
{
    int ret = ERROR_SUCCESS;

    if ((ret = forwarder->initialize(source, this, req)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsPublishEdge::on_client_publish()
{
    int ret = ERROR_SUCCESS;
    
    // error state.
    if (user_state != SrsEdgeUserStateInit) {
        ret = ERROR_RTMP_EDGE_PUBLISH_STATE;
        srs_error("invalid state for client to publish stream on edge. "
            "state=%d, user_state=%d, ret=%d", state, user_state, ret);
        return ret;
    }
    
    // error when not init state.
    if (state != SrsEdgeStateInit) {
        ret = ERROR_RTMP_EDGE_PUBLISH_STATE;
        srs_error("invalid state for client to publish stream on edge. "
            "state=%d, user_state=%d, ret=%d", state, user_state, ret);
        return ret;
    }
    
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/180
    // to avoid multiple publish the same stream on the same edge,
    // directly enter the publish stage.
    if (true) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStatePublish;
        srs_trace("edge change from %d to state %d (push).", pstate, state);
    }
    
    // start to forward stream to origin.
    ret = forwarder->start();
    
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/180
    // when failed, revert to init
    if (ret != ERROR_SUCCESS) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStateInit;
        srs_trace("edge revert from %d to state %d (push). ret=%d", pstate, state, ret);
    }
    
    return ret;
}

int SrsPublishEdge::on_proxy_publish(SrsMessage* msg)
{
    return forwarder->proxy(msg);
}

void SrsPublishEdge::on_proxy_unpublish()
{
    if (state == SrsEdgeStatePublish) {
        forwarder->stop();
    }
    
    SrsEdgeState pstate = state;
    state = SrsEdgeStateInit;
    srs_trace("edge change from %d to state %d (init).", pstate, state);
}

