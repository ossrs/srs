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

#include <srs_app_rtmp_conn.hpp>

#include <stdlib.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_config.hpp>
#include <srs_app_refer.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_http.hpp>
#include <srs_app_bandwidth.hpp>
#include <srs_app_socket.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_edge.hpp>

// when stream is busy, for example, streaming is already
// publishing, when a new client to request to publish,
// sleep a while and close the connection.
#define SRS_STREAM_BUSY_SLEEP_US (int64_t)(3*1000*1000LL)

// the timeout to wait encoder to republish
// if timeout, close the connection.
#define SRS_REPUBLISH_SEND_TIMEOUT_US (int64_t)(3*60*1000*1000LL)
// if timeout, close the connection.
#define SRS_REPUBLISH_RECV_TIMEOUT_US (int64_t)(3*60*1000*1000LL)

// the timeout to wait client data, when client paused
// if timeout, close the connection.
#define SRS_PAUSED_SEND_TIMEOUT_US (int64_t)(30*60*1000*1000LL)
// if timeout, close the connection.
#define SRS_PAUSED_RECV_TIMEOUT_US (int64_t)(30*60*1000*1000LL)

SrsRtmpConn::SrsRtmpConn(SrsServer* srs_server, st_netfd_t client_stfd)
    : SrsConnection(srs_server, client_stfd)
{
    req = new SrsRequest();
    res = new SrsResponse();
    skt = new SrsSocket(client_stfd);
    rtmp = new SrsRtmpServer(skt);
    refer = new SrsRefer();
    bandwidth = new SrsBandwidth();
    duration = 0;
    
    _srs_config->subscribe(this);
}

SrsRtmpConn::~SrsRtmpConn()
{
    _srs_config->unsubscribe(this);
    
    srs_freep(req);
    srs_freep(res);
    srs_freep(rtmp);
    srs_freep(skt);
    srs_freep(refer);
    srs_freep(bandwidth);
}

// TODO: return detail message when error for client.
int SrsRtmpConn::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = get_peer_ip()) != ERROR_SUCCESS) {
        srs_error("get peer ip failed. ret=%d", ret);
        return ret;
    }
    srs_trace("rtmp get peer ip success. ip=%s, send_to=%"PRId64"us, recv_to=%"PRId64"us", 
        ip, SRS_SEND_TIMEOUT_US, SRS_RECV_TIMEOUT_US);

    rtmp->set_recv_timeout(SRS_RECV_TIMEOUT_US);
    rtmp->set_send_timeout(SRS_SEND_TIMEOUT_US);
    
    if ((ret = rtmp->handshake()) != ERROR_SUCCESS) {
        srs_error("rtmp handshake failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("rtmp handshake success");
    
    if ((ret = rtmp->connect_app(req)) != ERROR_SUCCESS) {
        srs_error("rtmp connect vhost/app failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("rtmp connect app success");
    
    // discovery vhost, resolve the vhost from config
    SrsConfDirective* parsed_vhost = _srs_config->get_vhost(req->vhost);
    if (parsed_vhost) {
        req->vhost = parsed_vhost->arg0();
    }
    
    srs_info("discovery app success. schema=%s, vhost=%s, port=%s, app=%s",
        req->schema.c_str(), req->vhost.c_str(), req->port.c_str(), req->app.c_str());
    
    if (req->schema.empty() || req->vhost.empty() || req->port.empty() || req->app.empty()) {
        ret = ERROR_RTMP_REQ_TCURL;
        srs_error("discovery tcUrl failed. "
            "tcUrl=%s, schema=%s, vhost=%s, port=%s, app=%s, ret=%d",
            req->tcUrl.c_str(), req->schema.c_str(), req->vhost.c_str(), req->port.c_str(), req->app.c_str(), ret);
        return ret;
    }
    
    // check vhost
    if ((ret = check_vhost()) != ERROR_SUCCESS) {
        srs_error("check vhost failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("check vhost success.");
    
    srs_trace("rtmp connect app success. "
        "tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%s, app=%s", 
        req->tcUrl.c_str(), req->pageUrl.c_str(), req->swfUrl.c_str(), 
        req->schema.c_str(), req->vhost.c_str(), req->port.c_str(),
        req->app.c_str());
    
    ret = service_cycle();
    on_close();
    
    return ret;
}

int SrsRtmpConn::on_reload_vhost_removed(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
    // if the vhost connected is removed, disconnect the client.
    srs_trace("vhost %s removed/disabled, close client url=%s", 
        vhost.c_str(), req->get_stream_url().c_str());
        
    srs_close_stfd(stfd);
    
    return ret;
}
    
int SrsRtmpConn::service_cycle()
{    
    int ret = ERROR_SUCCESS;
    
    if ((ret = rtmp->set_window_ack_size(2.5 * 1000 * 1000)) != ERROR_SUCCESS) {
        srs_error("set window acknowledgement size failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("set window acknowledgement size success");
        
    if ((ret = rtmp->set_peer_bandwidth(2.5 * 1000 * 1000, 2)) != ERROR_SUCCESS) {
        srs_error("set peer bandwidth failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("set peer bandwidth success");

    // do bandwidth test if connect to the vhost which is for bandwidth check.
    if (_srs_config->get_bw_check_enabled(req->vhost)) {
        return bandwidth->bandwidth_test(req, stfd, rtmp);
    }
    
    if ((ret = rtmp->response_connect_app(req)) != ERROR_SUCCESS) {
        srs_error("response connect app failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("response connect app success");
        
    if ((ret = rtmp->on_bw_done()) != ERROR_SUCCESS) {
        srs_error("on_bw_done failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("on_bw_done success");
    
    while (true) {
        ret = stream_service_cycle();
        
        // stream service must terminated with error, never success.
        srs_assert(ret != ERROR_SUCCESS);
        
        // when not system control error, fatal error, return.
        if (!srs_is_system_control_error(ret)) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("stream service cycle failed. ret=%d", ret);
            }
            return ret;
        }
        
        // for republish, continue service
        if (ret == ERROR_CONTROL_REPUBLISH) {
            // set timeout to a larger value, wait for encoder to republish.
            rtmp->set_send_timeout(SRS_REPUBLISH_RECV_TIMEOUT_US);
            rtmp->set_recv_timeout(SRS_REPUBLISH_SEND_TIMEOUT_US);
            
            srs_trace("control message(unpublish) accept, retry stream service.");
            continue;
        }
        
        // for "some" system control error, 
        // logical accept and retry stream service.
        if (ret == ERROR_CONTROL_RTMP_CLOSE) {
            // TODO: FIXME: use ping message to anti-death of socket.
            // @see: https://github.com/winlinvip/simple-rtmp-server/issues/39
            // set timeout to a larger value, for user paused.
            rtmp->set_recv_timeout(SRS_PAUSED_RECV_TIMEOUT_US);
            rtmp->set_send_timeout(SRS_PAUSED_SEND_TIMEOUT_US);
            
            srs_trace("control message(close) accept, retry stream service.");
            continue;
        }
        
        // for other system control message, fatal error.
        srs_error("control message(%d) reject as error. ret=%d", ret, ret);
        return ret;
    }
    
    return ret;
}

int SrsRtmpConn::stream_service_cycle()
{
    int ret = ERROR_SUCCESS;
        
    SrsRtmpConnType type;
    if ((ret = rtmp->identify_client(res->stream_id, type, req->stream, req->duration)) != ERROR_SUCCESS) {
        srs_error("identify client failed. ret=%d", ret);
        return ret;
    }
    req->strip();
    srs_trace("identify client success. type=%s, stream_name=%s, duration=%.2f", 
        srs_client_type_string(type).c_str(), req->stream.c_str(), req->duration);

    // client is identified, set the timeout to service timeout.
    rtmp->set_recv_timeout(SRS_RECV_TIMEOUT_US);
    rtmp->set_send_timeout(SRS_SEND_TIMEOUT_US);
    
    // set chunk size to larger.
    int chunk_size = _srs_config->get_chunk_size(req->vhost);
    if ((ret = rtmp->set_chunk_size(chunk_size)) != ERROR_SUCCESS) {
        srs_error("set chunk_size=%d failed. ret=%d", chunk_size, ret);
        return ret;
    }
    srs_trace("set chunk_size=%d success", chunk_size);
    
    // find a source to serve.
    SrsSource* source = NULL;
    if ((ret = SrsSource::find(req, &source)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_assert(source != NULL);
    
    // check publish available.
    if (type != SrsRtmpConnPlay && !source->can_publish()) {
        ret = ERROR_SYSTEM_STREAM_BUSY;
        srs_warn("stream %s is already publishing. ret=%d", 
            req->get_stream_url().c_str(), ret);
        // to delay request
        st_usleep(SRS_STREAM_BUSY_SLEEP_US);
        return ret;
    }
    
    bool vhost_is_edge = _srs_config->get_vhost_is_edge(req->vhost);
    bool enabled_cache = _srs_config->get_gop_cache(req->vhost);
    srs_trace("source found, url=%s, enabled_cache=%d, edge=%d", 
        req->get_stream_url().c_str(), enabled_cache, vhost_is_edge);
    source->set_cache(enabled_cache);
    
    switch (type) {
        case SrsRtmpConnPlay: {
            srs_verbose("start to play stream %s.", req->stream.c_str());
            
            if (vhost_is_edge) {
                if ((ret = source->on_edge_start_play()) != ERROR_SUCCESS) {
                    srs_error("notice edge start play stream failed. ret=%d", ret);
                    return ret;
                }
            }
            
            if ((ret = rtmp->start_play(res->stream_id)) != ERROR_SUCCESS) {
                srs_error("start to play stream failed. ret=%d", ret);
                return ret;
            }
            if ((ret = on_play()) != ERROR_SUCCESS) {
                srs_error("http hook on_play failed. ret=%d", ret);
                return ret;
            }
            
            srs_info("start to play stream %s success", req->stream.c_str());
            ret = playing(source);
            on_stop();
            
            return ret;
        }
        case SrsRtmpConnFMLEPublish: {
            srs_verbose("FMLE start to publish stream %s.", req->stream.c_str());
            
            if (vhost_is_edge) {
                if ((ret = source->on_edge_start_publish()) != ERROR_SUCCESS) {
                    srs_error("notice edge start publish stream failed. ret=%d", ret);
                    return ret;
                }
            }
            
            if ((ret = rtmp->start_fmle_publish(res->stream_id)) != ERROR_SUCCESS) {
                srs_error("start to publish stream failed. ret=%d", ret);
                return ret;
            }
            
            SrsEdgeProxyContext context;
            context.edge_io = skt;
            context.edge_stream_id = res->stream_id;
            context.edge_rtmp = rtmp;
            if (vhost_is_edge) {
                return source->on_edge_proxy_publish(&context);
            }
            
            if ((ret = on_publish()) != ERROR_SUCCESS) {
                srs_error("http hook on_publish failed. ret=%d", ret);
                return ret;
            }
            srs_info("start to publish stream %s success", req->stream.c_str());
            ret = fmle_publish(source);
            source->on_unpublish();
            on_unpublish();
            return ret;
        }
        case SrsRtmpConnFlashPublish: {
            srs_verbose("flash start to publish stream %s.", req->stream.c_str());
            
            if (vhost_is_edge) {
                if ((ret = source->on_edge_start_publish()) != ERROR_SUCCESS) {
                    srs_error("notice edge start publish stream failed. ret=%d", ret);
                    return ret;
                }
            }
            
            if ((ret = rtmp->start_flash_publish(res->stream_id)) != ERROR_SUCCESS) {
                srs_error("flash start to publish stream failed. ret=%d", ret);
                return ret;
            }
            
            SrsEdgeProxyContext context;
            context.edge_io = skt;
            context.edge_stream_id = res->stream_id;
            context.edge_rtmp = rtmp;
            if (vhost_is_edge) {
                return source->on_edge_proxy_publish(&context);
            }
            
            if ((ret = on_publish()) != ERROR_SUCCESS) {
                srs_error("http hook on_publish failed. ret=%d", ret);
                return ret;
            }
            srs_info("flash start to publish stream %s success", req->stream.c_str());
            ret = flash_publish(source);
            source->on_unpublish();
            on_unpublish();
            return ret;
        }
        default: {
            ret = ERROR_SYSTEM_CLIENT_INVALID;
            srs_info("invalid client type=%d. ret=%d", type, ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsRtmpConn::check_vhost()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(req != NULL);
    
    SrsConfDirective* vhost = _srs_config->get_vhost(req->vhost);
    if (vhost == NULL) {
        ret = ERROR_RTMP_VHOST_NOT_FOUND;
        srs_error("vhost %s not found. ret=%d", req->vhost.c_str(), ret);
        return ret;
    }
    
    if (!_srs_config->get_vhost_enabled(req->vhost)) {
        ret = ERROR_RTMP_VHOST_NOT_FOUND;
        srs_error("vhost %s disabled. ret=%d", req->vhost.c_str(), ret);
        return ret;
    }
    
    if (req->vhost != vhost->arg0()) {
        srs_trace("vhost change from %s to %s", req->vhost.c_str(), vhost->arg0().c_str());
        req->vhost = vhost->arg0();
    }
    
    if ((ret = refer->check(req->pageUrl, _srs_config->get_refer(req->vhost))) != ERROR_SUCCESS) {
        srs_error("check refer failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("check refer success.");
    
    if ((ret = on_connect()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsRtmpConn::playing(SrsSource* source)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = refer->check(req->pageUrl, _srs_config->get_refer_play(req->vhost))) != ERROR_SUCCESS) {
        srs_error("check play_refer failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("check play_refer success.");
    
    SrsConsumer* consumer = NULL;
    if ((ret = source->create_consumer(consumer)) != ERROR_SUCCESS) {
        srs_error("create consumer failed. ret=%d", ret);
        return ret;
    }
    
    srs_assert(consumer != NULL);
    SrsAutoFree(SrsConsumer, consumer, false);
    srs_verbose("consumer created success.");
    
    rtmp->set_recv_timeout(SRS_PULSE_TIMEOUT_US);
    
    SrsPithyPrint pithy_print(SRS_STAGE_PLAY_USER);

    int64_t starttime = -1;
    while (true) {
        // switch to other st-threads.
        st_usleep(0);
        
        pithy_print.elapse();

        // read from client.
        int ctl_msg_ret = ERROR_SUCCESS;
        if (true) {
            SrsCommonMessage* msg = NULL;
            ctl_msg_ret = ret = rtmp->recv_message(&msg);
            
            srs_verbose("play loop recv message. ret=%d", ret);
            if (ret != ERROR_SUCCESS && ret != ERROR_SOCKET_TIMEOUT) {
                if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                    srs_error("recv client control message failed. ret=%d", ret);
                }
                return ret;
            }
            if ((ret = process_play_control_msg(consumer, msg)) != ERROR_SUCCESS) {
                if (!srs_is_system_control_error(ret)) {
                    srs_error("process play control message failed. ret=%d", ret);
                }
                return ret;
            }
        }
        
        // get messages from consumer.
        SrsSharedPtrMessage** msgs = NULL;
        int count = 0;
        if ((ret = consumer->get_packets(0, msgs, count)) != ERROR_SUCCESS) {
            srs_error("get messages from consumer failed. ret=%d", ret);
            return ret;
        }

        // reportable
        if (pithy_print.can_print()) {
            srs_trace("-> time=%"PRId64", duration=%"PRId64", cmr=%d, msgs=%d, obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
                pithy_print.age(), duration, ctl_msg_ret, count, rtmp->get_send_bytes(), rtmp->get_recv_bytes(), rtmp->get_send_kbps(), rtmp->get_recv_kbps());
        }
        
        if (count <= 0) {
            srs_verbose("no packets in queue.");
            continue;
        }
        SrsAutoFree(SrsSharedPtrMessage*, msgs, true);
        
        // sendout messages
        for (int i = 0; i < count; i++) {
            SrsSharedPtrMessage* msg = msgs[i];
            
            // the send_message will free the msg, 
            // so set the msgs[i] to NULL.
            msgs[i] = NULL;
            
            if ((ret = rtmp->send_message(msg)) != ERROR_SUCCESS) {
                srs_error("send message to client failed. ret=%d", ret);
                return ret;
            }
            
            // foreach msg, collect the duration.
            if (starttime < 0 || starttime > msg->header.timestamp) {
                starttime = msg->header.timestamp;
            }
            duration += msg->header.timestamp - starttime;
            starttime = msg->header.timestamp;
        }
        
        // if duration specified, and exceed it, stop play live.
        // @see: https://github.com/winlinvip/simple-rtmp-server/issues/45
        if (req->duration > 0 && duration >= (int64_t)req->duration) {
            ret = ERROR_RTMP_DURATION_EXCEED;
            srs_trace("stop live for duration exceed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsRtmpConn::fmle_publish(SrsSource* source)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = refer->check(req->pageUrl, _srs_config->get_refer_publish(req->vhost))) != ERROR_SUCCESS) {
        srs_error("fmle check publish_refer failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("fmle check publish_refer success.");
    
    SrsPithyPrint pithy_print(SRS_STAGE_PUBLISH_USER);
    
    // notify the hls to prepare when publish start.
    if ((ret = source->on_publish()) != ERROR_SUCCESS) {
        srs_error("fmle hls on_publish failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("fmle hls on_publish success.");
    
    while (true) {
        // switch to other st-threads.
        st_usleep(0);
        
        SrsCommonMessage* msg = NULL;
        if ((ret = rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
            srs_error("fmle recv identify client message failed. ret=%d", ret);
            return ret;
        }

        srs_assert(msg);
        SrsAutoFree(SrsCommonMessage, msg, false);
        
        pithy_print.elapse();

        // reportable
        if (pithy_print.can_print()) {
            srs_trace("<- time=%"PRId64", obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
                pithy_print.age(), rtmp->get_send_bytes(), rtmp->get_recv_bytes(), rtmp->get_send_kbps(), rtmp->get_recv_kbps());
        }
    
        // process UnPublish event.
        if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
            if ((ret = msg->decode_packet(rtmp->get_protocol())) != ERROR_SUCCESS) {
                srs_error("fmle decode unpublish message failed. ret=%d", ret);
                return ret;
            }
        
            SrsPacket* pkt = msg->get_packet();
            if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
                SrsFMLEStartPacket* unpublish = dynamic_cast<SrsFMLEStartPacket*>(pkt);
                if ((ret = rtmp->fmle_unpublish(res->stream_id, unpublish->transaction_id)) != ERROR_SUCCESS) {
                    return ret;
                }
                return ERROR_CONTROL_REPUBLISH;
            }
            
            srs_trace("fmle ignore AMF0/AMF3 command message.");
            continue;
        }

        // video, audio, data message
        if ((ret = process_publish_message(source, msg)) != ERROR_SUCCESS) {
            srs_error("fmle process publish message failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsRtmpConn::flash_publish(SrsSource* source)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = refer->check(req->pageUrl, _srs_config->get_refer_publish(req->vhost))) != ERROR_SUCCESS) {
        srs_error("flash check publish_refer failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("flash check publish_refer success.");
    
    SrsPithyPrint pithy_print(SRS_STAGE_PUBLISH_USER);
    
    // notify the hls to prepare when publish start.
    if ((ret = source->on_publish()) != ERROR_SUCCESS) {
        srs_error("flash hls on_publish failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("flash hls on_publish success.");
    
    while (true) {
        // switch to other st-threads.
        st_usleep(0);
        
        SrsCommonMessage* msg = NULL;
        if ((ret = rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("flash recv identify client message failed. ret=%d", ret);
            }
            return ret;
        }

        SrsAutoFree(SrsCommonMessage, msg, false);
        
        pithy_print.elapse();

        // reportable
        if (pithy_print.can_print()) {
            srs_trace("<- time=%"PRId64", obytes=%"PRId64", ibytes=%"PRId64", okbps=%d, ikbps=%d", 
                pithy_print.age(), rtmp->get_send_bytes(), rtmp->get_recv_bytes(), rtmp->get_send_kbps(), rtmp->get_recv_kbps());
        }
    
        // process UnPublish event.
        if (msg->header.is_amf0_command() || msg->header.is_amf3_command()) {
            if ((ret = msg->decode_packet(rtmp->get_protocol())) != ERROR_SUCCESS) {
                srs_error("flash decode unpublish message failed. ret=%d", ret);
                return ret;
            }
            
            // flash unpublish.
            // TODO: maybe need to support republish.
            srs_trace("flash flash publish finished.");
            return ERROR_CONTROL_REPUBLISH;
        }

        // video, audio, data message
        if ((ret = process_publish_message(source, msg)) != ERROR_SUCCESS) {
            srs_error("flash process publish message failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsRtmpConn::process_publish_message(SrsSource* source, SrsCommonMessage* msg)
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
    
    // process onMetaData
    if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
        if ((ret = msg->decode_packet(rtmp->get_protocol())) != ERROR_SUCCESS) {
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

int SrsRtmpConn::process_play_control_msg(SrsConsumer* consumer, SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if (!msg) {
        srs_verbose("ignore all empty message.");
        return ret;
    }
    SrsAutoFree(SrsCommonMessage, msg, false);
    
    if (!msg->header.is_amf0_command() && !msg->header.is_amf3_command()) {
        srs_info("ignore all message except amf0/amf3 command.");
        return ret;
    }
    
    if ((ret = msg->decode_packet(rtmp->get_protocol())) != ERROR_SUCCESS) {
        srs_error("decode the amf0/amf3 command packet failed. ret=%d", ret);
        return ret;
    }
    srs_info("decode the amf0/amf3 command packet success.");
    
    // for jwplayer/flowplayer, which send close as pause message.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/6
    SrsCloseStreamPacket* close = dynamic_cast<SrsCloseStreamPacket*>(msg->get_packet());
    if (close) {
        ret = ERROR_CONTROL_RTMP_CLOSE;
        srs_trace("system control message: rtmp close stream. ret=%d", ret);
        return ret;
    }
    
    SrsPausePacket* pause = dynamic_cast<SrsPausePacket*>(msg->get_packet());
    if (!pause) {
        srs_info("ignore all amf0/amf3 command except pause.");
        return ret;
    }
    
    if ((ret = rtmp->on_play_client_pause(res->stream_id, pause->is_pause)) != ERROR_SUCCESS) {
        srs_error("rtmp process play client pause failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = consumer->on_play_client_pause(pause->is_pause)) != ERROR_SUCCESS) {
        srs_error("consumer process play client pause failed. ret=%d", ret);
        return ret;
    }
    srs_info("process pause success, is_pause=%d, time=%d.", pause->is_pause, pause->time_ms);
    
    return ret;
}

int SrsRtmpConn::on_connect()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK
    // HTTP: on_connect 
    SrsConfDirective* on_connect = _srs_config->get_vhost_on_connect(req->vhost);
    if (!on_connect) {
        srs_info("ignore the empty http callback: on_connect");
        return ret;
    }
    
    for (int i = 0; i < (int)on_connect->args.size(); i++) {
        std::string url = on_connect->args.at(i);
        if ((ret = SrsHttpHooks::on_connect(url, connection_id, ip, req)) != ERROR_SUCCESS) {
            srs_error("hook client on_connect failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
#endif

    return ret;
}

void SrsRtmpConn::on_close()
{
#ifdef SRS_AUTO_HTTP_CALLBACK
    // whatever the ret code, notify the api hooks.
    // HTTP: on_close 
    SrsConfDirective* on_close = _srs_config->get_vhost_on_close(req->vhost);
    if (!on_close) {
        srs_info("ignore the empty http callback: on_close");
        return;
    }
    
    for (int i = 0; i < (int)on_close->args.size(); i++) {
        std::string url = on_close->args.at(i);
        SrsHttpHooks::on_close(url, connection_id, ip, req);
    }
#endif
}

int SrsRtmpConn::on_publish()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK    
    // HTTP: on_publish 
    SrsConfDirective* on_publish = _srs_config->get_vhost_on_publish(req->vhost);
    if (!on_publish) {
        srs_info("ignore the empty http callback: on_publish");
        return ret;
    }
    
    for (int i = 0; i < (int)on_publish->args.size(); i++) {
        std::string url = on_publish->args.at(i);
        if ((ret = SrsHttpHooks::on_publish(url, connection_id, ip, req)) != ERROR_SUCCESS) {
            srs_error("hook client on_publish failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
#endif

    return ret;
}

void SrsRtmpConn::on_unpublish()
{
#ifdef SRS_AUTO_HTTP_CALLBACK
    // whatever the ret code, notify the api hooks.
    // HTTP: on_unpublish 
    SrsConfDirective* on_unpublish = _srs_config->get_vhost_on_unpublish(req->vhost);
    if (!on_unpublish) {
        srs_info("ignore the empty http callback: on_unpublish");
        return;
    }
    
    for (int i = 0; i < (int)on_unpublish->args.size(); i++) {
        std::string url = on_unpublish->args.at(i);
        SrsHttpHooks::on_unpublish(url, connection_id, ip, req);
    }
#endif
}

int SrsRtmpConn::on_play()
{
    int ret = ERROR_SUCCESS;
    
#ifdef SRS_AUTO_HTTP_CALLBACK    
    // HTTP: on_play 
    SrsConfDirective* on_play = _srs_config->get_vhost_on_play(req->vhost);
    if (!on_play) {
        srs_info("ignore the empty http callback: on_play");
        return ret;
    }
    
    for (int i = 0; i < (int)on_play->args.size(); i++) {
        std::string url = on_play->args.at(i);
        if ((ret = SrsHttpHooks::on_play(url, connection_id, ip, req)) != ERROR_SUCCESS) {
            srs_error("hook client on_play failed. url=%s, ret=%d", url.c_str(), ret);
            return ret;
        }
    }
#endif

    return ret;
}

void SrsRtmpConn::on_stop()
{
#ifdef SRS_AUTO_HTTP_CALLBACK
    // whatever the ret code, notify the api hooks.
    // HTTP: on_stop 
    SrsConfDirective* on_stop = _srs_config->get_vhost_on_stop(req->vhost);
    if (!on_stop) {
        srs_info("ignore the empty http callback: on_stop");
        return;
    }
    
    for (int i = 0; i < (int)on_stop->args.size(); i++) {
        std::string url = on_stop->args.at(i);
        SrsHttpHooks::on_stop(url, connection_id, ip, req);
    }
#endif

    return;
}
