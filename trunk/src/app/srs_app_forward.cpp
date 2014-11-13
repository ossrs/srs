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

#include <srs_app_forward.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#include <srs_app_source.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_kbps.hpp>
#include <srs_protocol_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_codec.hpp>

// when error, forwarder sleep for a while and retry.
#define SRS_FORWARDER_SLEEP_US (int64_t)(3*1000*1000LL)

SrsForwarder::SrsForwarder(SrsSource* _source)
{
    source = _source;
    
    _req = NULL;
    io = NULL;
    client = NULL;
    stfd = NULL;
    kbps = new SrsKbps();
    stream_id = 0;

    pthread = new SrsThread(this, SRS_FORWARDER_SLEEP_US, true);
    queue = new SrsMessageQueue();
    jitter = new SrsRtmpJitter();
    
    sh_video = sh_audio = NULL;
}

SrsForwarder::~SrsForwarder()
{
    on_unpublish();
    
    srs_freep(pthread);
    srs_freep(queue);
    srs_freep(jitter);
    srs_freep(kbps);
    
    srs_freep(sh_video);
    srs_freep(sh_audio);
}

int SrsForwarder::initialize(SrsRequest* req, string ep_forward)
{
    int ret = ERROR_SUCCESS;
    
    // it's ok to use the request object,
    // SrsSource already copy it and never delete it.
    _req = req;
    
    // the ep(endpoint) to forward to
    _ep_forward = ep_forward;
    
    return ret;
}

void SrsForwarder::set_queue_size(double queue_size)
{
    queue->set_queue_size(queue_size);
}

int SrsForwarder::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    SrsRequest* req = _req;
    
    // discovery the server port and tcUrl from req and ep_forward.
    std::string server, port, tc_url;
    discovery_ep(server, port, tc_url);
    
    // dead loop check
    std::string source_ep = "rtmp://";
    source_ep += req->host;
    source_ep += ":";
    source_ep += req->port;
    source_ep += "?vhost=";
    source_ep += req->vhost;
    
    std::string dest_ep = "rtmp://";
    if (_ep_forward == SRS_CONSTS_LOCALHOST) {
        dest_ep += req->host;
    } else {
        dest_ep += _ep_forward;
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
        source_ep.c_str(), dest_ep.c_str(), tc_url.c_str(), 
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
    
    close_underlayer_socket();
    
    srs_freep(client);
    srs_freep(io);
    kbps->set_io(NULL, NULL);
}

int SrsForwarder::on_meta_data(SrsSharedPtrMessage* metadata)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = jitter->correct(metadata, 0, 0, SrsRtmpJitterAlgorithmFULL)) != ERROR_SUCCESS) {
        srs_freep(metadata);
        return ret;
    }
    
    if ((ret = queue->enqueue(metadata)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsForwarder::on_audio(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = jitter->correct(msg, 0, 0, SrsRtmpJitterAlgorithmFULL)) != ERROR_SUCCESS) {
        srs_freep(msg);
        return ret;
    }
    
    if (SrsFlvCodec::audio_is_sequence_header(msg->payload, msg->size)) {
        srs_freep(sh_audio);
        sh_audio = msg->copy();
    }
    
    if ((ret = queue->enqueue(msg)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsForwarder::on_video(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = jitter->correct(msg, 0, 0, SrsRtmpJitterAlgorithmFULL)) != ERROR_SUCCESS) {
        srs_freep(msg);
        return ret;
    }
    
    if (SrsFlvCodec::video_is_sequence_header(msg->payload, msg->size)) {
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
    
    std::string ep_server, ep_port;
    if ((ret = connect_server(ep_server, ep_port)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(client);

    client->set_recv_timeout(SRS_CONSTS_RTMP_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_CONSTS_RTMP_SEND_TIMEOUT_US);
    
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
    
    if ((ret = client->publish(_req->stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream_name=%s, stream_id=%d. ret=%d", 
            _req->stream.c_str(), stream_id, ret);
        return ret;
    }
    
    if ((ret = source->on_forwarder_start(this)) != ERROR_SUCCESS) {
        srs_error("callback the source to feed the sequence header failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = forward()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsForwarder::close_underlayer_socket()
{
    srs_close_stfd(stfd);
}

void SrsForwarder::discovery_ep(string& server, string& port, string& tc_url)
{
    SrsRequest* req = _req;
    
    server = _ep_forward;
    port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    
    // TODO: FIXME: parse complex params
    size_t pos = _ep_forward.find(":");
    if (pos != std::string::npos) {
        port = _ep_forward.substr(pos + 1);
        server = _ep_forward.substr(0, pos);
    }
    
    // generate tcUrl
    tc_url = srs_generate_tc_url(server, req->vhost, req->app, port, req->param);
}

int SrsForwarder::connect_server(string& ep_server, string& ep_port)
{
    int ret = ERROR_SUCCESS;
    
    // reopen
    close_underlayer_socket();
    
    // discovery the server port and tcUrl from req and ep_forward.
    std::string server, s_port, tc_url;
    discovery_ep(server, s_port, tc_url);
    int port = ::atoi(s_port.c_str());
    
    // output the connected server and port.
    ep_server = server;
    ep_port = s_port;
    
    // open socket.
    int64_t timeout = SRS_FORWARDER_SLEEP_US;
    if ((ret = srs_socket_connect(ep_server, port, timeout, &stfd)) != ERROR_SUCCESS) {
        srs_warn("forward failed, stream=%s, tcUrl=%s to server=%s, port=%d, timeout=%"PRId64", ret=%d",
            _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port, timeout, ret);
        return ret;
    }
    
    srs_freep(client);
    srs_freep(io);
    
    srs_assert(stfd);
    io = new SrsStSocket(stfd);
    client = new SrsRtmpClient(io);
    
    kbps->set_io(io, io);
    
    srs_trace("forward connected, stream=%s, tcUrl=%s to server=%s, port=%d",
        _req->stream.c_str(), _req->tcUrl.c_str(), server.c_str(), port);
    
    return ret;
}

// TODO: FIXME: refine the connect_app.
int SrsForwarder::connect_app(string ep_server, string ep_port)
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

#define SYS_MAX_FORWARD_SEND_MSGS 128
int SrsForwarder::forward()
{
    int ret = ERROR_SUCCESS;
    
    client->set_recv_timeout(SRS_CONSTS_RTMP_PULSE_TIMEOUT_US);
    
    SrsPithyPrint pithy_print(SRS_CONSTS_STAGE_FORWARDER);

    SrsMessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);
    
    // update sequence header
    // TODO: FIXME: maybe need to zero the sequence header timestamp.
    if (sh_video) {
        if ((ret = client->send_and_free_message(sh_video->copy(), stream_id)) != ERROR_SUCCESS) {
            srs_error("forwarder send sh_video to server failed. ret=%d", ret);
            return ret;
        }
    }
    if (sh_audio) {
        if ((ret = client->send_and_free_message(sh_audio->copy(), stream_id)) != ERROR_SUCCESS) {
            srs_error("forwarder send sh_audio to server failed. ret=%d", ret);
            return ret;
        }
    }
    
    while (pthread->can_loop()) {
        pithy_print.elapse();

        // read from client.
        if (true) {
            SrsMessage* msg = NULL;
            ret = client->recv_message(&msg);
            
            srs_verbose("play loop recv message. ret=%d", ret);
            if (ret != ERROR_SUCCESS && ret != ERROR_SOCKET_TIMEOUT) {
                srs_error("recv server control message failed. ret=%d", ret);
                return ret;
            }
            
            srs_freep(msg);
        }
        
        // forward all messages.
        int count = 0;
        if ((ret = queue->dump_packets(msgs.size, msgs.msgs, count)) != ERROR_SUCCESS) {
            srs_error("get message to forward failed. ret=%d", ret);
            return ret;
        }
        
        // pithy print
        if (pithy_print.can_print()) {
            kbps->sample();
            srs_trace("-> "SRS_CONSTS_LOG_FOWARDER
                " time=%"PRId64", msgs=%d, okbps=%d,%d,%d, ikbps=%d,%d,%d", 
                pithy_print.age(), count,
                kbps->get_send_kbps(), kbps->get_send_kbps_30s(), kbps->get_send_kbps_5m(),
                kbps->get_recv_kbps(), kbps->get_recv_kbps_30s(), kbps->get_recv_kbps_5m());
        }
        
        // ignore when no messages.
        if (count <= 0) {
            srs_verbose("no packets to forward.");
            continue;
        }
    
        // all msgs to forward.
        // @remark, becareful, all msgs must be free explicitly,
        //      free by send_and_free_message or srs_freep.
        for (int i = 0; i < count; i++) {
            SrsMessage* msg = msgs.msgs[i];
            
            srs_assert(msg);
            msgs.msgs[i] = NULL;
            
            if ((ret = client->send_and_free_message(msg, stream_id)) != ERROR_SUCCESS) {
                srs_error("forwarder send message to server failed. ret=%d", ret);
                return ret;
            }
        }
    }
    
    return ret;
}


