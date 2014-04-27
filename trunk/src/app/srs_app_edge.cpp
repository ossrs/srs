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

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_io.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_app_socket.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_socket.hpp>

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_SLEEP_US (int64_t)(1*1000*1000LL)

// when edge timeout, retry next.
#define SRS_EDGE_INGESTER_TIMEOUT_US (int64_t)(3*1000*1000LL)

SrsEdgeIngester::SrsEdgeIngester()
{
    io = NULL;
    client = NULL;
    _edge = NULL;
    _req = NULL;
    origin_index = 0;
    stream_id = 0;
    stfd = NULL;
    pthread = new SrsThread(this, SRS_EDGE_INGESTER_SLEEP_US);
}

SrsEdgeIngester::~SrsEdgeIngester()
{
    stop();
    
    srs_freep(pthread);
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
}

int SrsEdgeIngester::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = connect_server()) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(client);

    client->set_recv_timeout(SRS_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_SEND_TIMEOUT_US);

    SrsRequest* req = _req;
    
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = client->connect_app(req->app, req->tcUrl)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, tcUrl=%s. ret=%d", req->tcUrl.c_str(), ret);
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
        srs_error("edge ingester play stream then publish to edge failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = _edge->on_ingest_play()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = ingest()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsEdgeIngester::ingest()
{
    int ret = ERROR_SUCCESS;
    
    client->set_recv_timeout(SRS_EDGE_INGESTER_TIMEOUT_US);
    
    SrsPithyPrint pithy_print(SRS_STAGE_EDGE);

    while (pthread->can_loop()) {
        // switch to other st-threads.
        st_usleep(0);
        
        pithy_print.elapse();
        
        // pithy print
        if (pithy_print.can_print()) {
            srs_trace("<- time=%"PRId64", obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
                pithy_print.age(), client->get_send_bytes(), client->get_recv_bytes(), client->get_send_kbps(), client->get_recv_kbps());
        }

        // read from client.
        SrsCommonMessage* msg = NULL;
        if ((ret = client->recv_message(&msg)) != ERROR_SUCCESS) {
            srs_error("ingest recv origin server message failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("edge loop recv message. ret=%d", ret);
        
        srs_assert(msg);
        SrsAutoFree(SrsCommonMessage, msg, false);
        
        if ((ret = process_publish_message(msg)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsEdgeIngester::process_publish_message(SrsCommonMessage* msg)
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

    // process onMetaData
    if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
        if ((ret = msg->decode_packet(client->get_protocol())) != ERROR_SUCCESS) {
            srs_error("decode onMetaData message failed. ret=%d", ret);
            return ret;
        }
    
        SrsPacket* pkt = msg->get_packet();
        if (dynamic_cast<SrsOnMetaDataPacket*>(pkt)) {
            SrsOnMetaDataPacket* metadata = dynamic_cast<SrsOnMetaDataPacket*>(pkt);
            if ((ret = source->on_meta_data(msg, metadata)) != ERROR_SUCCESS) {
                srs_error("source process onMetaData message failed. ret=%d", ret);
                return ret;
            }
            srs_trace("process onMetaData message success.");
            return ret;
        }
        
        srs_trace("ignore AMF0/AMF3 data message.");
        return ret;
    }
    
    return ret;
}

void SrsEdgeIngester::close_underlayer_socket()
{
    srs_close_stfd(stfd);
}

int SrsEdgeIngester::connect_server()
{
    int ret = ERROR_SUCCESS;
    
    // reopen
    close_underlayer_socket();
    
    // TODO: FIXME: support reload
    SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(_req->vhost);
    srs_assert(conf);
    
    // select the origin.
    std::string server = conf->args.at(origin_index % conf->args.size());
    origin_index = (origin_index + 1) % conf->args.size();
    
    std::string s_port = RTMP_DEFAULT_PORT;
    int port = ::atoi(RTMP_DEFAULT_PORT);
    size_t pos = server.find(":");
    if (pos != std::string::npos) {
        s_port = server.substr(pos + 1);
        server = server.substr(0, pos);
        port = ::atoi(s_port.c_str());
    }
    
    // open socket.
    srs_trace("connect edge stream=%s, tcUrl=%s to server=%s, port=%d",
        _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port);

    // TODO: FIXME: extract utility method
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    srs_assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
    
    srs_freep(client);
    srs_freep(io);
    
    io = new SrsSocket(stfd);
    client = new SrsRtmpClient(io);
    
    // connect to server.
    std::string ip = srs_dns_resolve(server);
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        return ret;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_trace("connect to server success. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
    return ret;
}

SrsEdgeForwarder::SrsEdgeForwarder()
{
    io = NULL;
    client = NULL;
    _edge = NULL;
    _req = NULL;
    origin_index = 0;
    stream_id = 0;
    stfd = NULL;
}

SrsEdgeForwarder::~SrsEdgeForwarder()
{
    stop();
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
    
    if ((ret = connect_server()) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(client);

    client->set_recv_timeout(SRS_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_SEND_TIMEOUT_US);

    SrsRequest* req = _req;
    
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = client->connect_app(req->app, req->tcUrl)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, tcUrl=%s. ret=%d", req->tcUrl.c_str(), ret);
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
    
    return ret;
}

void SrsEdgeForwarder::stop()
{
    close_underlayer_socket();
    
    srs_freep(client);
    srs_freep(io);
}

int SrsEdgeForwarder::proxy(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    // the msg is auto free by source,
    // so we just ignore, or copy then send it.
    if (msg->size <= 0
        || msg->header.is_set_chunk_size()
        || msg->header.is_window_ackledgement_size()
        || msg->header.is_ackledgement()
    ) {
        return ret;
    }
    
    SrsSharedPtrMessage* copy = new SrsSharedPtrMessage();
    SrsAutoFree(SrsSharedPtrMessage, copy, false);
    if ((ret = copy->initialize(msg)) != ERROR_SUCCESS) {
        srs_error("initialize the msg failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("initialize shared ptr msg success.");
    
    copy->header.stream_id = stream_id;
    if ((ret = client->send_message(copy->copy())) != ERROR_SUCCESS) {
        srs_error("send client message to origin failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

void SrsEdgeForwarder::close_underlayer_socket()
{
    srs_close_stfd(stfd);
}

int SrsEdgeForwarder::connect_server()
{
    int ret = ERROR_SUCCESS;
    
    // reopen
    close_underlayer_socket();
    
    // TODO: FIXME: support reload
    SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(_req->vhost);
    srs_assert(conf);
    
    // select the origin.
    std::string server = conf->args.at(origin_index % conf->args.size());
    origin_index = (origin_index + 1) % conf->args.size();
    
    std::string s_port = RTMP_DEFAULT_PORT;
    int port = ::atoi(RTMP_DEFAULT_PORT);
    size_t pos = server.find(":");
    if (pos != std::string::npos) {
        s_port = server.substr(pos + 1);
        server = server.substr(0, pos);
        port = ::atoi(s_port.c_str());
    }
    
    // open socket.
    srs_trace("connect edge stream=%s, tcUrl=%s to server=%s, port=%d",
        _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port);

    // TODO: FIXME: extract utility method
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    srs_assert(!stfd);
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
    
    srs_freep(client);
    srs_freep(io);
    
    io = new SrsSocket(stfd);
    client = new SrsRtmpClient(io);
    
    // connect to server.
    std::string ip = srs_dns_resolve(server);
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        return ret;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_trace("connect to server success. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
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
        srs_error("invalid state for client to play stream on edge. "
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
    srs_trace("edge change from %d to state %d (ingest connected).", pstate, state);
    
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
    
    SrsEdgeState pstate = state;
    state = SrsEdgeStatePublish;
    srs_trace("edge change from %d to state %d (forward publish).", pstate, state);
    
    return forwarder->start();
}

int SrsPublishEdge::on_proxy_publish(SrsCommonMessage* msg)
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
