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

#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_socket.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_app_kbps.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_msg_array.hpp>

// when error, forwarder sleep for a while and retry.
#define SRS_FORWARDER_SLEEP_US (int64_t)(3*1000*1000LL)

SrsForwarder::SrsForwarder(SrsSource* _source)
{
    source = _source;
    
    io = NULL;
    client = NULL;
    stfd = NULL;
    kbps = new SrsKbps();
    stream_id = 0;

    pthread = new SrsThread(this, SRS_FORWARDER_SLEEP_US, true);
    queue = new SrsMessageQueue();
    jitter = new SrsRtmpJitter();
}

SrsForwarder::~SrsForwarder()
{
    on_unpublish();
    
    srs_freep(pthread);
    srs_freep(queue);
    srs_freep(jitter);
    srs_freep(kbps);
}

void SrsForwarder::set_queue_size(double queue_size)
{
    queue->set_queue_size(queue_size);
}

int SrsForwarder::on_publish(SrsRequest* req, std::string forward_server)
{
    int ret = ERROR_SUCCESS;
    
    // forward app
    app = req->app;
    
    stream_name = req->stream;
    server = forward_server;
    std::string s_port = RTMP_DEFAULT_PORT;
    port = ::atoi(RTMP_DEFAULT_PORT);
    
    // TODO: FIXME: parse complex params
    size_t pos = forward_server.find(":");
    if (pos != std::string::npos) {
        s_port = forward_server.substr(pos + 1);
        server = forward_server.substr(0, pos);
    }
    // discovery vhost
    std::string vhost = req->vhost;
    srs_vhost_resolve(vhost, s_port);
    port = ::atoi(s_port.c_str());
    
    // generate tcUrl
    tc_url = srs_generate_tc_url(forward_server, vhost, req->app, s_port);
    
    // dead loop check
    std::string source_ep = "rtmp://";
    source_ep += req->host;
    source_ep += ":";
    source_ep += req->port;
    source_ep += "?vhost=";
    source_ep += req->vhost;
    
    std::string dest_ep = "rtmp://";
    if (forward_server == "127.0.0.1") {
        dest_ep += req->host;
    } else {
        dest_ep += forward_server;
    }
    dest_ep += ":";
    dest_ep += s_port;
    dest_ep += "?vhost=";
    dest_ep += vhost;
    
    if (source_ep == dest_ep) {
        ret = ERROR_SYSTEM_FORWARD_LOOP;
        srs_warn("forward loop detected. src=%s, dest=%s, ret=%d", 
            source_ep.c_str(), dest_ep.c_str(), ret);
        return ret;
    }
    srs_trace("start forward %s to %s, tcUrl=%s, stream=%s", 
        source_ep.c_str(), dest_ep.c_str(), tc_url.c_str(), 
        stream_name.c_str());
    
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
    
    if ((ret = queue->enqueue(msg)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsForwarder::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = connect_server()) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(client);

    client->set_recv_timeout(SRS_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_SEND_TIMEOUT_US);
    
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = client->connect_app(app, tc_url)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, tcUrl=%s. ret=%d", tc_url.c_str(), ret);
        return ret;
    }
    if ((ret = client->create_stream(stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream_id=%d. ret=%d", stream_id, ret);
        return ret;
    }
    
    if ((ret = client->publish(stream_name, stream_id)) != ERROR_SUCCESS) {
        srs_error("connect with server failed, stream_name=%s, stream_id=%d. ret=%d", 
            stream_name.c_str(), stream_id, ret);
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

int SrsForwarder::connect_server()
{
    int ret = ERROR_SUCCESS;
    
    // reopen
    close_underlayer_socket();
    
    // open socket.
    srs_trace("forward stream=%s, tcUrl=%s to server=%s, port=%d",
        stream_name.c_str(), tc_url.c_str(), server.c_str(), port);

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
    kbps->set_io(io, io);
    
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
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), SRS_FORWARDER_SLEEP_US) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), port, ret);
        return ret;
    }
    srs_trace("connect to server success. server=%s, ip=%s, port=%d", server.c_str(), ip.c_str(), port);
    
    return ret;
}

#define SYS_MAX_FORWARD_SEND_MSGS 128
int SrsForwarder::forward()
{
    int ret = ERROR_SUCCESS;
    
    client->set_recv_timeout(SRS_PULSE_TIMEOUT_US);
    
    SrsPithyPrint pithy_print(SRS_STAGE_FORWARDER);

    SrsSharedPtrMessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);
    
    while (pthread->can_loop()) {
        // switch to other st-threads.
        st_usleep(0);

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
            srs_trace("-> "SRS_LOG_ID_FOWARDER
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
            SrsSharedPtrMessage* msg = msgs.msgs[i];
            
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

