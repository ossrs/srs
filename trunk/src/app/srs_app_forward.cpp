/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_app_forward.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#include <srs_app_source.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtmp_conn.hpp>

// when error, forwarder sleep for a while and retry.
#define SRS_FORWARDER_CIMS (3000)

SrsForwarder::SrsForwarder(SrsOriginHub* h)
{
    hub = h;
    
    req = NULL;
    sh_video = sh_audio = NULL;
    
    sdk = NULL;
    pthread = new SrsReusableThread("forward", this, SRS_FORWARDER_CIMS);
    queue = new SrsMessageQueue();
    jitter = new SrsRtmpJitter();
}

SrsForwarder::~SrsForwarder()
{
    srs_freep(sdk);
    srs_freep(pthread);
    srs_freep(queue);
    srs_freep(jitter);
    
    srs_freep(sh_video);
    srs_freep(sh_audio);
}

int SrsForwarder::initialize(SrsRequest* r, string ep)
{
    int ret = ERROR_SUCCESS;
    
    // it's ok to use the request object,
    // SrsSource already copy it and never delete it.
    req = r;
    
    // the ep(endpoint) to forward to
    ep_forward = ep;
    
    return ret;
}

void SrsForwarder::set_queue_size(double queue_size)
{
    queue->set_queue_size(queue_size);
}

int SrsForwarder::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // discovery the server port and tcUrl from req and ep_forward.
    std::string server;
    std::string tcUrl;
    int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    if (true) {
        // parse host:port from hostport.
        srs_parse_hostport(ep_forward, server, port);
        
        // generate tcUrl
        tcUrl = srs_generate_tc_url(server, req->vhost, req->app, port, req->param);
    }
    
    // dead loop check
    std::string source_ep = "rtmp://";
    source_ep += req->host;
    source_ep += ":";
    source_ep += req->port;
    source_ep += "?vhost=";
    source_ep += req->vhost;
    
    std::string dest_ep = "rtmp://";
    if (ep_forward == SRS_CONSTS_LOCALHOST) {
        dest_ep += req->host;
    } else {
        dest_ep += server;
    }
    dest_ep += ":";
    dest_ep += port;
    dest_ep += "?vhost=";
    dest_ep += req->vhost;
    
    if (source_ep == dest_ep) {
        ret = ERROR_SYSTEM_FORWARD_LOOP;
        srs_warn("forward loop detected. src=%s, dest=%s, ret=%d",
                 source_ep.c_str(), dest_ep.c_str(), ret);
        return ret;
    }
    srs_trace("start forward %s to %s, tcUrl=%s, stream=%s",
              source_ep.c_str(), dest_ep.c_str(), tcUrl.c_str(),
              req->stream.c_str());
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("start srs thread failed. ret=%d", ret);
        return ret;
    }
    srs_trace("forward thread cid=%d, current_cid=%d", pthread->cid(), _srs_context->get_id());
    
    return ret;
}

void SrsForwarder::on_unpublish()
{
    pthread->stop();
    sdk->close();
}

int SrsForwarder::on_meta_data(SrsSharedPtrMessage* shared_metadata)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* metadata = shared_metadata->copy();
    
    // TODO: FIXME: config the jitter of Forwarder.
    if ((ret = jitter->correct(metadata, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_freep(metadata);
        return ret;
    }
    
    if ((ret = queue->enqueue(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsForwarder::on_audio(SrsSharedPtrMessage* shared_audio)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = shared_audio->copy();
    
    // TODO: FIXME: config the jitter of Forwarder.
    if ((ret = jitter->correct(msg, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_freep(msg);
        return ret;
    }
    
    if (SrsFlvAudio::sh(msg->payload, msg->size)) {
        srs_freep(sh_audio);
        sh_audio = msg->copy();
    }
    
    if ((ret = queue->enqueue(msg)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsForwarder::on_video(SrsSharedPtrMessage* shared_video)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = shared_video->copy();
    
    // TODO: FIXME: config the jitter of Forwarder.
    if ((ret = jitter->correct(msg, SrsRtmpJitterAlgorithmOFF)) != ERROR_SUCCESS) {
        srs_freep(msg);
        return ret;
    }
    
    if (SrsFlvVideo::sh(msg->payload, msg->size)) {
        srs_freep(sh_video);
        sh_video = msg->copy();
    }
    
    if ((ret = queue->enqueue(msg)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsForwarder::cycle()
{
    int ret = ERROR_SUCCESS;
    
    std::string url;
    if (true) {
        std::string server;
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        
        // parse host:port from hostport.
        srs_parse_hostport(ep_forward, server, port);
        
        // generate url
        url = srs_generate_rtmp_url(server, port, req->vhost, req->app, req->stream);
    }
    
    srs_freep(sdk);
    int64_t cto = SRS_FORWARDER_CIMS;
    int64_t sto = SRS_CONSTS_RTMP_TMMS;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((ret = sdk->connect()) != ERROR_SUCCESS) {
        srs_warn("forward failed, url=%s, cto=%" PRId64 ", sto=%" PRId64 ". ret=%d", url.c_str(), cto, sto, ret);
        return ret;
    }
    
    if ((ret = sdk->publish()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = hub->on_forwarder_start(this)) != ERROR_SUCCESS) {
        srs_error("callback the source to feed the sequence header failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = forward()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

#define SYS_MAX_FORWARD_SEND_MSGS 128
int SrsForwarder::forward()
{
    int ret = ERROR_SUCCESS;
    
    sdk->set_recv_timeout(SRS_CONSTS_RTMP_PULSE_TMMS);
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_forwarder();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);
    
    // update sequence header
    // TODO: FIXME: maybe need to zero the sequence header timestamp.
    if (sh_video) {
        if ((ret = sdk->send_and_free_message(sh_video->copy())) != ERROR_SUCCESS) {
            srs_error("forwarder send sh_video to server failed. ret=%d", ret);
            return ret;
        }
    }
    if (sh_audio) {
        if ((ret = sdk->send_and_free_message(sh_audio->copy())) != ERROR_SUCCESS) {
            srs_error("forwarder send sh_audio to server failed. ret=%d", ret);
            return ret;
        }
    }
    
    while (!pthread->interrupted()) {
        pprint->elapse();
        
        // read from client.
        if (true) {
            SrsCommonMessage* msg = NULL;
            ret = sdk->recv_message(&msg);
            
            srs_verbose("play loop recv message. ret=%d", ret);
            if (ret != ERROR_SUCCESS && ret != ERROR_SOCKET_TIMEOUT) {
                srs_error("recv server control message failed. ret=%d", ret);
                return ret;
            }
            
            srs_freep(msg);
        }
        
        // forward all messages.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((ret = queue->dump_packets(msgs.max, msgs.msgs, count)) != ERROR_SUCCESS) {
            srs_error("get message to forward failed. ret=%d", ret);
            return ret;
        }
        
        // pithy print
        if (pprint->can_print()) {
            sdk->kbps_sample(SRS_CONSTS_LOG_FOWARDER, pprint->age(), count);
        }
        
        // ignore when no messages.
        if (count <= 0) {
            srs_verbose("no packets to forward.");
            continue;
        }
        
        // sendout messages, all messages are freed by send_and_free_messages().
        if ((ret = sdk->send_and_free_messages(msgs.msgs, count)) != ERROR_SUCCESS) {
            srs_error("forwarder messages to server failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}


