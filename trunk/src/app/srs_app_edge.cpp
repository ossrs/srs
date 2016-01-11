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

#include <srs_app_edge.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_io.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_app_rtmp_conn.hpp>

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_SLEEP_US (int64_t)(3*1000*1000LL)

// when edge timeout, retry next.
#define SRS_EDGE_INGESTER_TIMEOUT_US (int64_t)(5*1000*1000LL)

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_FORWARDER_SLEEP_US (int64_t)(3*1000*1000LL)

// when edge timeout, retry next.
#define SRS_EDGE_FORWARDER_TIMEOUT_US (int64_t)(5*1000*1000LL)

// when edge error, wait for quit
#define SRS_EDGE_FORWARDER_ERROR_US (int64_t)(50*1000LL)

SrsEdgeUpstream::SrsEdgeUpstream()
{
}

SrsEdgeUpstream::~SrsEdgeUpstream()
{
}

SrsEdgeRtmpUpstream::SrsEdgeRtmpUpstream(string r)
{
    redirect = r;
    sdk = new SrsSimpleRtmpClient();
}

SrsEdgeRtmpUpstream::~SrsEdgeRtmpUpstream()
{
    close();
    
    srs_freep(sdk);
}

int SrsEdgeRtmpUpstream::connect(SrsRequest* r, SrsLbRoundRobin* lb)
{
    int ret = ERROR_SUCCESS;
    
    SrsRequest* req = r;
    
    std::string url;
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(req->vhost);
        
        // @see https://github.com/ossrs/srs/issues/79
        // when origin is error, for instance, server is shutdown,
        // then user remove the vhost then reload, the conf is empty.
        if (!conf) {
            ret = ERROR_EDGE_VHOST_REMOVED;
            srs_warn("vhost %s removed. ret=%d", req->vhost.c_str(), ret);
            return ret;
        }
        
        // select the origin.
        std::string server = lb->select(conf->args);
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        srs_parse_hostport(server, server, port);
        
        // override the origin info by redirect.
        if (!redirect.empty()) {
            int _port;
            string _schema, _vhost, _app, _param, _host;
            srs_discovery_tc_url(redirect, _schema, _host, _vhost, _app, _port, _param);
            
            srs_warn("RTMP redirect %s:%d to %s:%d", server.c_str(), port, _host.c_str(), _port);
            server = _host;
            port = _port;
        }
        
        // support vhost tranform for edge,
        // @see https://github.com/ossrs/srs/issues/372
        std::string vhost = _srs_config->get_vhost_edge_transform_vhost(req->vhost);
        vhost = srs_string_replace(vhost, "[vhost]", req->vhost);
        
        url = srs_generate_rtmp_url(server, port, vhost, req->app, req->stream);
    }
    
    int64_t cto = SRS_EDGE_INGESTER_TIMEOUT_US;
    int64_t sto = SRS_CONSTS_RTMP_PULSE_TIMEOUT_US;
    if ((ret = sdk->connect(url, cto, sto)) != ERROR_SUCCESS) {
        srs_error("edge pull %s failed, cto=%"PRId64", sto=%"PRId64". ret=%d", url.c_str(), cto, sto, ret);
        return ret;
    }
    
    if ((ret = sdk->play()) != ERROR_SUCCESS) {
        srs_error("edge pull %s stream failed. ret=%d", url.c_str(), ret);
        return ret;
    }
    
    return ret;
}

int SrsEdgeRtmpUpstream::recv_message(SrsCommonMessage** pmsg)
{
    return sdk->recv_message(pmsg);
}

int SrsEdgeRtmpUpstream::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    return sdk->decode_message(msg, ppacket);
}

void SrsEdgeRtmpUpstream::close()
{
    sdk->close();
}

void SrsEdgeRtmpUpstream::set_recv_timeout(int64_t timeout)
{
    sdk->set_recv_timeout(timeout);
}

void SrsEdgeRtmpUpstream::kbps_sample(const char* label, int64_t age)
{
    sdk->kbps_sample(label, age);
}

SrsEdgeIngester::SrsEdgeIngester()
{
    source = NULL;
    edge = NULL;
    req = NULL;
    
    upstream = new SrsEdgeRtmpUpstream(redirect);
    lb = new SrsLbRoundRobin();
    pthread = new SrsReusableThread2("edge-igs", this, SRS_EDGE_INGESTER_SLEEP_US);
}

SrsEdgeIngester::~SrsEdgeIngester()
{   
    stop();
    
    srs_freep(upstream);
    srs_freep(lb);
    srs_freep(pthread);
}

int SrsEdgeIngester::initialize(SrsSource* s, SrsPlayEdge* e, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    source = s;
    edge = e;
    req = r;
    
    return ret;
}

int SrsEdgeIngester::start()
{
    int ret = ERROR_SUCCESS;

    if ((ret = source->on_publish()) != ERROR_SUCCESS) {
        srs_error("edge pull stream then publish to edge failed. ret=%d", ret);
        return ret;
    }

    return pthread->start();
}

void SrsEdgeIngester::stop()
{
    pthread->stop();
    upstream->close();
    
    // notice to unpublish.
    source->on_unpublish();
}

string SrsEdgeIngester::get_curr_origin()
{
    return lb->selected();
}

int SrsEdgeIngester::cycle()
{
    int ret = ERROR_SUCCESS;
    
    for (;;) {
        srs_freep(upstream);
        upstream = new SrsEdgeRtmpUpstream(redirect);
        
        // we only use the redict once.
        // reset the redirect to empty, for maybe the origin changed.
        redirect = "";
        
        if ((ret = source->on_source_id_changed(_srs_context->get_id())) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = upstream->connect(req, lb)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = edge->on_ingest_play()) != ERROR_SUCCESS) {
            return ret;
        }
        
        ret = ingest();
        
        // retry for rtmp 302 immediately.
        if (ret == ERROR_CONTROL_REDIRECT) {
            ret = ERROR_SUCCESS;
            continue;
        }
        
        if (srs_is_client_gracefully_close(ret)) {
            srs_warn("origin disconnected, retry. ret=%d", ret);
            ret = ERROR_SUCCESS;
        }
        break;
    }
    
    return ret;
}

int SrsEdgeIngester::ingest()
{
    int ret = ERROR_SUCCESS;
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_edge();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    // set to larger timeout to read av data from origin.
    upstream->set_recv_timeout(SRS_EDGE_INGESTER_TIMEOUT_US);
    
    while (!pthread->interrupted()) {
        pprint->elapse();
        
        // pithy print
        if (pprint->can_print()) {
            upstream->kbps_sample(SRS_CONSTS_LOG_EDGE_PLAY, pprint->age());
        }
        
        // read from client.
        SrsCommonMessage* msg = NULL;
        if ((ret = upstream->recv_message(&msg)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("pull origin server message failed. ret=%d", ret);
            }
            return ret;
        }
        srs_verbose("edge loop recv message. ret=%d", ret);
        
        srs_assert(msg);
        SrsAutoFree(SrsCommonMessage, msg);
        
        if ((ret = process_publish_message(msg)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsEdgeIngester::process_publish_message(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
        
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
        if ((ret = upstream->decode_message(msg, &pkt)) != ERROR_SUCCESS) {
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
    
    // call messages, for example, reject, redirect.
    if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
        SrsPacket* pkt = NULL;
        if ((ret = upstream->decode_message(msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("decode call message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsPacket, pkt);
        
        // RTMP 302 redirect
        if (dynamic_cast<SrsCallPacket*>(pkt)) {
            SrsCallPacket* call = dynamic_cast<SrsCallPacket*>(pkt);
            if (!call->arguments->is_object()) {
                return ret;
            }
            
            SrsAmf0Any* prop = NULL;
            SrsAmf0Object* evt = call->arguments->to_object();
            
            if ((prop = evt->ensure_property_string("level")) == NULL) {
                return ret;
            } else if (prop->to_str() != StatusLevelError) {
                return ret;
            }
            
            if ((prop = evt->get_property("ex")) == NULL || !prop->is_object()) {
                return ret;
            }
            SrsAmf0Object* ex = prop->to_object();
            
            if ((prop = ex->ensure_property_string("redirect")) == NULL) {
                return ret;
            }
            redirect = prop->to_str();
            
            ret = ERROR_CONTROL_REDIRECT;
            srs_info("RTMP 302 redirect to %s, ret=%d", redirect.c_str(), ret);
            return ret;
        }
    }
    
    return ret;
}

SrsEdgeForwarder::SrsEdgeForwarder()
{
    edge = NULL;
    req = NULL;
    send_error_code = ERROR_SUCCESS;
    
    sdk = new SrsSimpleRtmpClient();
    lb = new SrsLbRoundRobin();
    pthread = new SrsReusableThread2("edge-fwr", this, SRS_EDGE_FORWARDER_SLEEP_US);
    queue = new SrsMessageQueue();
}

SrsEdgeForwarder::~SrsEdgeForwarder()
{
    stop();
    
    srs_freep(sdk);
    srs_freep(lb);
    srs_freep(pthread);
    srs_freep(queue);
}

void SrsEdgeForwarder::set_queue_size(double queue_size)
{
    return queue->set_queue_size(queue_size);
}

int SrsEdgeForwarder::initialize(SrsSource* s, SrsPublishEdge* e, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    source = s;
    edge = e;
    req = r;
    
    return ret;
}

int SrsEdgeForwarder::start()
{
    int ret = ERROR_SUCCESS;
    
    // reset the error code.
    send_error_code = ERROR_SUCCESS;
    
    std::string url;
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(req->vhost);
        srs_assert(conf);
        
        // select the origin.
        std::string server = lb->select(conf->args);
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        srs_parse_hostport(server, server, port);
        
        // support vhost tranform for edge,
        // @see https://github.com/ossrs/srs/issues/372
        std::string vhost = _srs_config->get_vhost_edge_transform_vhost(req->vhost);
        vhost = srs_string_replace(vhost, "[vhost]", req->vhost);
        
        url = srs_generate_rtmp_url(server, port, vhost, req->app, req->stream);
    }
    
    // open socket.
    int64_t cto = SRS_EDGE_FORWARDER_TIMEOUT_US;
    int64_t sto = SRS_CONSTS_RTMP_TIMEOUT_US;
    if ((ret = sdk->connect(url, cto, sto)) != ERROR_SUCCESS) {
        srs_warn("edge push %s failed, cto=%"PRId64", sto=%"PRId64". ret=%d", url.c_str(), cto, sto, ret);
        return ret;
    }
    
    if ((ret = sdk->publish()) != ERROR_SUCCESS) {
        srs_error("edge push publish failed. ret=%d", ret);
        return ret;
    }
    
    return pthread->start();
}

void SrsEdgeForwarder::stop()
{
    pthread->stop();
    sdk->close();
    queue->clear();
}

#define SYS_MAX_EDGE_SEND_MSGS 128

int SrsEdgeForwarder::cycle()
{
    int ret = ERROR_SUCCESS;
    
    sdk->set_recv_timeout(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_edge();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SYS_MAX_EDGE_SEND_MSGS);

    while (!pthread->interrupted()) {
        if (send_error_code != ERROR_SUCCESS) {
            st_usleep(SRS_EDGE_FORWARDER_ERROR_US);
            continue;
        }

        // read from client.
        if (true) {
            SrsCommonMessage* msg = NULL;
            ret = sdk->recv_message(&msg);
            
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
        
        pprint->elapse();
        
        // pithy print
        if (pprint->can_print()) {
            sdk->kbps_sample(SRS_CONSTS_LOG_EDGE_PUBLISH, pprint->age(), count);
        }
        
        // ignore when no messages.
        if (count <= 0) {
            srs_verbose("edge no packets to push.");
            continue;
        }
    
        // sendout messages, all messages are freed by send_and_free_messages().
        if ((ret = sdk->send_and_free_messages(msgs.msgs, count)) != ERROR_SUCCESS) {
            srs_error("edge publish push message to server failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsEdgeForwarder::proxy(SrsCommonMessage* msg)
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
    
    copy.stream_id = sdk->sid();
    if ((ret = queue->enqueue(copy.copy())) != ERROR_SUCCESS) {
        srs_error("enqueue edge publish msg failed. ret=%d", ret);
    }
    
    return ret;
}

SrsPlayEdge::SrsPlayEdge()
{
    state = SrsEdgeStateInit;
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

string SrsPlayEdge::get_curr_origin()
{
    return ingester->get_curr_origin();
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

bool SrsPublishEdge::can_publish()
{
    return state != SrsEdgeStatePublish;
}

int SrsPublishEdge::on_client_publish()
{
    int ret = ERROR_SUCCESS;
    
    // error when not init state.
    if (state != SrsEdgeStateInit) {
        ret = ERROR_RTMP_EDGE_PUBLISH_STATE;
        srs_error("invalid state for client to publish stream on edge. "
            "state=%d, ret=%d", state, ret);
        return ret;
    }
    
    // @see https://github.com/ossrs/srs/issues/180
    // to avoid multiple publish the same stream on the same edge,
    // directly enter the publish stage.
    if (true) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStatePublish;
        srs_trace("edge change from %d to state %d (push).", pstate, state);
    }
    
    // start to forward stream to origin.
    ret = forwarder->start();
    
    // @see https://github.com/ossrs/srs/issues/180
    // when failed, revert to init
    if (ret != ERROR_SUCCESS) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStateInit;
        srs_trace("edge revert from %d to state %d (push). ret=%d", pstate, state, ret);
    }
    
    return ret;
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

