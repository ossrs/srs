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

// when edge timeout, retry next.
#define SRS_EDGE_INGESTER_TMMS (5 * SRS_UTIME_MILLISECONDS)

// when edge error, wait for quit
#define SRS_EDGE_FORWARDER_TMMS (150 * SRS_UTIME_MILLISECONDS)

SrsEdgeUpstream::SrsEdgeUpstream()
{
}

SrsEdgeUpstream::~SrsEdgeUpstream()
{
}

SrsEdgeRtmpUpstream::SrsEdgeRtmpUpstream(string r)
{
    redirect = r;
    sdk = NULL;
}

SrsEdgeRtmpUpstream::~SrsEdgeRtmpUpstream()
{
    close();
}

srs_error_t SrsEdgeRtmpUpstream::connect(SrsRequest* r, SrsLbRoundRobin* lb)
{
    srs_error_t err = srs_success;
    
    SrsRequest* req = r;
    
    std::string url;
    if (true) {
        SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(req->vhost);
        
        // @see https://github.com/ossrs/srs/issues/79
        // when origin is error, for instance, server is shutdown,
        // then user remove the vhost then reload, the conf is empty.
        if (!conf) {
            return srs_error_new(ERROR_EDGE_VHOST_REMOVED, "vhost %s removed", req->vhost.c_str());
        }
        
        // select the origin.
        std::string server = lb->select(conf->args);
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        srs_parse_hostport(server, server, port);
        
        // override the origin info by redirect.
        if (!redirect.empty()) {
            int _port;
            string _schema, _vhost, _app, _stream, _param, _host;
            srs_discovery_tc_url(redirect, _schema, _host, _vhost, _app, _stream, _port, _param);
            
            srs_warn("RTMP redirect %s:%d to %s:%d stream=%s", server.c_str(), port, _host.c_str(), _port, _stream.c_str());
            server = _host;
            port = _port;
        }
        
        // support vhost tranform for edge,
        // @see https://github.com/ossrs/srs/issues/372
        std::string vhost = _srs_config->get_vhost_edge_transform_vhost(req->vhost);
        vhost = srs_string_replace(vhost, "[vhost]", req->vhost);
        
        url = srs_generate_rtmp_url(server, port, req->host, vhost, req->app, req->stream, req->param);
    }
    
    srs_freep(sdk);
    int64_t cto = srsu2ms(SRS_EDGE_INGESTER_TMMS);
    int64_t sto = SRS_CONSTS_RTMP_PULSE_TMMS;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        return srs_error_wrap(err, "edge pull %s failed, cto=%" PRId64 ", sto=%" PRId64, url.c_str(), cto, sto);
    }
    
    if ((err = sdk->play(_srs_config->get_chunk_size(req->vhost))) != srs_success) {
        return srs_error_wrap(err, "edge pull %s stream failed", url.c_str());
    }
    
    return err;
}

srs_error_t SrsEdgeRtmpUpstream::recv_message(SrsCommonMessage** pmsg)
{
    return sdk->recv_message(pmsg);
}

srs_error_t SrsEdgeRtmpUpstream::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    return sdk->decode_message(msg, ppacket);
}

void SrsEdgeRtmpUpstream::close()
{
    srs_freep(sdk);
}

void SrsEdgeRtmpUpstream::set_recv_timeout(int64_t tm)
{
    sdk->set_recv_timeout(tm);
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
    trd = new SrsDummyCoroutine();
}

SrsEdgeIngester::~SrsEdgeIngester()
{
    stop();
    
    srs_freep(upstream);
    srs_freep(lb);
    srs_freep(trd);
}

srs_error_t SrsEdgeIngester::initialize(SrsSource* s, SrsPlayEdge* e, SrsRequest* r)
{
    source = s;
    edge = e;
    req = r;
    
    return srs_success;
}

srs_error_t SrsEdgeIngester::start()
{
    srs_error_t err = srs_success;
    
    if ((err = source->on_publish()) != srs_success) {
        return srs_error_wrap(err, "notify source");
    }
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("edge-igs", this);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }
    
    return err;
}

void SrsEdgeIngester::stop()
{
    trd->stop();
    upstream->close();
    
    // notice to unpublish.
    if (source) {
        source->on_unpublish();
    }
}

string SrsEdgeIngester::get_curr_origin()
{
    return lb->selected();
}

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_CIMS (3 * SRS_UTIME_MILLISECONDS)

srs_error_t SrsEdgeIngester::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = do_cycle()) != srs_success) {
            srs_warn("EdgeIngester: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "edge ingester");
        }
        
        srs_usleep(SRS_EDGE_INGESTER_CIMS);
    }
    
    return err;
}

srs_error_t SrsEdgeIngester::do_cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "do cycle pull");
        }
        
        srs_freep(upstream);
        upstream = new SrsEdgeRtmpUpstream(redirect);
        
        // we only use the redict once.
        // reset the redirect to empty, for maybe the origin changed.
        redirect = "";
        
        if ((err = source->on_source_id_changed(_srs_context->get_id())) != srs_success) {
            return srs_error_wrap(err, "on source id changed");
        }
        
        if ((err = upstream->connect(req, lb)) != srs_success) {
            return srs_error_wrap(err, "connect upstream");
        }
        
        if ((err = edge->on_ingest_play()) != srs_success) {
            return srs_error_wrap(err, "notify edge play");
        }
        
        err = ingest();
        
        // retry for rtmp 302 immediately.
        if (srs_error_code(err) == ERROR_CONTROL_REDIRECT) {
            srs_error_reset(err);
            continue;
        }
        
        if (srs_is_client_gracefully_close(err)) {
            srs_warn("origin disconnected, retry, error %s", srs_error_desc(err).c_str());
            srs_error_reset(err);
        }
        break;
    }
    
    return err;
}

srs_error_t SrsEdgeIngester::ingest()
{
    srs_error_t err = srs_success;
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_edge();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    // set to larger timeout to read av data from origin.
    upstream->set_recv_timeout(srsu2ms(SRS_EDGE_INGESTER_TMMS));
    
    while (true) {
        srs_error_t err = srs_success;
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "thread quit");
        }
        
        pprint->elapse();
        
        // pithy print
        if (pprint->can_print()) {
            upstream->kbps_sample(SRS_CONSTS_LOG_EDGE_PLAY, pprint->age());
        }
        
        // read from client.
        SrsCommonMessage* msg = NULL;
        if ((err = upstream->recv_message(&msg)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }
        
        srs_assert(msg);
        SrsAutoFree(SrsCommonMessage, msg);
        
        if ((err = process_publish_message(msg)) != srs_success) {
            return srs_error_wrap(err, "process message");
        }
    }
    
    return err;
}

srs_error_t SrsEdgeIngester::process_publish_message(SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    // process audio packet
    if (msg->header.is_audio()) {
        if ((err = source->on_audio(msg)) != srs_success) {
            return srs_error_wrap(err, "source consume audio");
        }
    }
    
    // process video packet
    if (msg->header.is_video()) {
        if ((err = source->on_video(msg)) != srs_success) {
            return srs_error_wrap(err, "source consume video");
        }
    }
    
    // process aggregate packet
    if (msg->header.is_aggregate()) {
        if ((err = source->on_aggregate(msg)) != srs_success) {
            return srs_error_wrap(err, "source consume aggregate");
        }
        return err;
    }
    
    // process onMetaData
    if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
        SrsPacket* pkt = NULL;
        if ((err = upstream->decode_message(msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        SrsAutoFree(SrsPacket, pkt);
        
        if (dynamic_cast<SrsOnMetaDataPacket*>(pkt)) {
            SrsOnMetaDataPacket* metadata = dynamic_cast<SrsOnMetaDataPacket*>(pkt);
            if ((err = source->on_meta_data(msg, metadata)) != srs_success) {
                return srs_error_wrap(err, "source consume metadata");
            }
            return err;
        }
        
        return err;
    }
    
    // call messages, for example, reject, redirect.
    if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
        SrsPacket* pkt = NULL;
        if ((err = upstream->decode_message(msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        SrsAutoFree(SrsPacket, pkt);
        
        // RTMP 302 redirect
        if (dynamic_cast<SrsCallPacket*>(pkt)) {
            SrsCallPacket* call = dynamic_cast<SrsCallPacket*>(pkt);
            if (!call->arguments->is_object()) {
                return err;
            }
            
            SrsAmf0Any* prop = NULL;
            SrsAmf0Object* evt = call->arguments->to_object();
            
            if ((prop = evt->ensure_property_string("level")) == NULL) {
                return err;
            } else if (prop->to_str() != StatusLevelError) {
                return err;
            }
            
            if ((prop = evt->get_property("ex")) == NULL || !prop->is_object()) {
                return err;
            }
            SrsAmf0Object* ex = prop->to_object();
            
            if ((prop = ex->ensure_property_string("redirect")) == NULL) {
                return err;
            }
            redirect = prop->to_str();
            
            return srs_error_new(ERROR_CONTROL_REDIRECT, "RTMP 302 redirect to %s", redirect.c_str());
        }
    }
    
    return err;
}

SrsEdgeForwarder::SrsEdgeForwarder()
{
    edge = NULL;
    req = NULL;
    send_error_code = ERROR_SUCCESS;
    
    sdk = NULL;
    lb = new SrsLbRoundRobin();
    trd = new SrsDummyCoroutine();
    queue = new SrsMessageQueue();
}

SrsEdgeForwarder::~SrsEdgeForwarder()
{
    stop();
    
    srs_freep(lb);
    srs_freep(trd);
    srs_freep(queue);
}

void SrsEdgeForwarder::set_queue_size(double queue_size)
{
    return queue->set_queue_size(queue_size);
}

srs_error_t SrsEdgeForwarder::initialize(SrsSource* s, SrsPublishEdge* e, SrsRequest* r)
{
    source = s;
    edge = e;
    req = r;
    
    return srs_success;
}

srs_error_t SrsEdgeForwarder::start()
{
    srs_error_t err = srs_success;
    
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
        
        url = srs_generate_rtmp_url(server, port, req->host, vhost, req->app, req->stream, req->param);
    }
    
    // open socket.
    srs_freep(sdk);
    int64_t cto = srsu2ms(SRS_EDGE_FORWARDER_TMMS);
    int64_t sto = SRS_CONSTS_RTMP_TMMS;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        return srs_error_wrap(err, "sdk connect %s failed, cto=%" PRId64 ", sto=%" PRId64, url.c_str(), cto, sto);
    }
    
    if ((err = sdk->publish(_srs_config->get_chunk_size(req->vhost))) != srs_success) {
        return srs_error_wrap(err, "sdk publish");
    }
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("edge-fwr", this);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }
    srs_trace("edge-fwr publish url %s", url.c_str());
    
    return err;
}

void SrsEdgeForwarder::stop()
{
    trd->stop();
    queue->clear();
    srs_freep(sdk);
}

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_FORWARDER_CIMS (3 * SRS_UTIME_MILLISECONDS)

srs_error_t SrsEdgeForwarder::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = do_cycle()) != srs_success) {
            return srs_error_wrap(err, "do cycle");
        }
        
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "thread pull");
        }
    
        srs_usleep(SRS_EDGE_FORWARDER_CIMS);
    }
    
    return err;
}

#define SYS_MAX_EDGE_SEND_MSGS 128

srs_error_t SrsEdgeForwarder::do_cycle()
{
    srs_error_t err = srs_success;
    
    sdk->set_recv_timeout(SRS_CONSTS_RTMP_PULSE_TMMS);
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_edge();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SYS_MAX_EDGE_SEND_MSGS);
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "edge forward pull");
        }
        
        if (send_error_code != ERROR_SUCCESS) {
            srs_usleep(SRS_EDGE_FORWARDER_TMMS);
            continue;
        }
        
        // read from client.
        if (true) {
            SrsCommonMessage* msg = NULL;
            err = sdk->recv_message(&msg);
            
            srs_verbose("edge loop recv message. ret=%d", ret);
            if (err != srs_success && srs_error_code(err) != ERROR_SOCKET_TIMEOUT) {
                srs_error("edge push get server control message failed. err=%s", srs_error_desc(err).c_str());
                send_error_code = srs_error_code(err);
                srs_error_reset(err);
                continue;
            }
            
            srs_error_reset(err);
            srs_freep(msg);
        }
        
        // forward all messages.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = queue->dump_packets(msgs.max, msgs.msgs, count)) != srs_success) {
            return srs_error_wrap(err, "queue dumps packets");
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
        if ((err = sdk->send_and_free_messages(msgs.msgs, count)) != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }
    
    return err;
}

srs_error_t SrsEdgeForwarder::proxy(SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    if (send_error_code != ERROR_SUCCESS) {
        return srs_error_new(send_error_code, "edge forwarder");
    }
    
    // the msg is auto free by source,
    // so we just ignore, or copy then send it.
    if (msg->size <= 0
        || msg->header.is_set_chunk_size()
        || msg->header.is_window_ackledgement_size()
        || msg->header.is_ackledgement()) {
        return err;
    }
    
    SrsSharedPtrMessage copy;
    if ((err = copy.create(msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    
    copy.stream_id = sdk->sid();
    if ((err = queue->enqueue(copy.copy())) != srs_success) {
        return srs_error_wrap(err, "enqueue message");
    }
    
    return err;
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

srs_error_t SrsPlayEdge::initialize(SrsSource* source, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    if ((err = ingester->initialize(source, this, req)) != srs_success) {
        return srs_error_wrap(err, "ingester(pull)");
    }
    
    return err;
}

srs_error_t SrsPlayEdge::on_client_play()
{
    srs_error_t err = srs_success;
    
    // start ingest when init state.
    if (state == SrsEdgeStateInit) {
        state = SrsEdgeStatePlay;
        err = ingester->start();
    }
    
    return err;
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

srs_error_t SrsPlayEdge::on_ingest_play()
{
    srs_error_t err = srs_success;
    
    // when already connected(for instance, reconnect for error), ignore.
    if (state == SrsEdgeStateIngestConnected) {
        return err;
    }
    
    srs_assert(state == SrsEdgeStatePlay);
    
    SrsEdgeState pstate = state;
    state = SrsEdgeStateIngestConnected;
    srs_trace("edge change from %d to state %d (pull).", pstate, state);
    
    return err;
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

srs_error_t SrsPublishEdge::initialize(SrsSource* source, SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    if ((err = forwarder->initialize(source, this, req)) != srs_success) {
        return srs_error_wrap(err, "forwarder(push)");
    }
    
    return err;
}

bool SrsPublishEdge::can_publish()
{
    return state != SrsEdgeStatePublish;
}

srs_error_t SrsPublishEdge::on_client_publish()
{
    srs_error_t err = srs_success;
    
    // error when not init state.
    if (state != SrsEdgeStateInit) {
        return srs_error_new(ERROR_RTMP_EDGE_PUBLISH_STATE, "invalid state");
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
    err = forwarder->start();
    
    // @see https://github.com/ossrs/srs/issues/180
    // when failed, revert to init
    if (err != srs_success) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStateInit;
        srs_trace("edge revert from %d to state %d (push), error %s", pstate, state, srs_error_desc(err).c_str());
    }
    
    return err;
}

srs_error_t SrsPublishEdge::on_proxy_publish(SrsCommonMessage* msg)
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

