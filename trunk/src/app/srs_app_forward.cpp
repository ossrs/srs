//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

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
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_rtmp_conn.hpp>

SrsForwarder::SrsForwarder(SrsOriginHub* h)
{
    hub = h;
    
    req = NULL;
    sh_video = sh_audio = NULL;
    
    sdk = NULL;
    trd = new SrsDummyCoroutine();
    queue = new SrsMessageQueue();
    jitter = new SrsRtmpJitter();
}

SrsForwarder::~SrsForwarder()
{
    srs_freep(sdk);
    srs_freep(trd);
    srs_freep(queue);
    srs_freep(jitter);
    
    srs_freep(sh_video);
    srs_freep(sh_audio);
    
    srs_freep(req);
}

srs_error_t SrsForwarder::initialize(SrsRequest* r, string ep)
{
    srs_error_t err = srs_success;
    
    // it's ok to use the request object,
    // SrsLiveSource already copy it and never delete it.
    req = r->copy();
    
    // the ep(endpoint) to forward to
    ep_forward = ep;

    // Remember the source context id.
    source_cid_ = _srs_context->get_id();
    
    return err;
}

void SrsForwarder::set_queue_size(srs_utime_t queue_size)
{
    queue->set_queue_size(queue_size);
}

srs_error_t SrsForwarder::on_publish()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("forward", this);
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }
    
    return err;
}

void SrsForwarder::on_unpublish()
{
    trd->stop();
    if (sdk) sdk->close();
}

srs_error_t SrsForwarder::on_meta_data(SrsSharedPtrMessage* shared_metadata)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* metadata = shared_metadata->copy();
    
    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = jitter->correct(metadata, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    if ((err = queue->enqueue(metadata)) != srs_success) {
        return srs_error_wrap(err, "enqueue metadata");
    }
    
    return err;
}

srs_error_t SrsForwarder::on_audio(SrsSharedPtrMessage* shared_audio)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* msg = shared_audio->copy();
    
    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = jitter->correct(msg, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    if (SrsFlvAudio::sh(msg->payload, msg->size)) {
        srs_freep(sh_audio);
        sh_audio = msg->copy();
    }
    
    if ((err = queue->enqueue(msg)) != srs_success) {
        return srs_error_wrap(err, "enqueue audio");
    }
    
    return err;
}

srs_error_t SrsForwarder::on_video(SrsSharedPtrMessage* shared_video)
{
    srs_error_t err = srs_success;
    
    SrsSharedPtrMessage* msg = shared_video->copy();
    
    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = jitter->correct(msg, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }
    
    if (SrsFlvVideo::sh(msg->payload, msg->size)) {
        srs_freep(sh_video);
        sh_video = msg->copy();
    }
    
    if ((err = queue->enqueue(msg)) != srs_success) {
        return srs_error_wrap(err, "enqueue video");
    }
    
    return err;
}

// when error, forwarder sleep for a while and retry.
#define SRS_FORWARDER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsForwarder::cycle()
{
    srs_error_t err = srs_success;

    srs_trace("Forwarder: Start forward %s of source=[%s] to %s",
        req->get_stream_url().c_str(), source_cid_.c_str(), ep_forward.c_str());

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "forwarder");
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("Forwarder: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        // Never wait if thread error, fast quit.
        // @see https://github.com/ossrs/srs/pull/2284
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "forwarder");
        }

        srs_usleep(SRS_FORWARDER_CIMS);
    }
    
    return err;
}

srs_error_t SrsForwarder::do_cycle()
{
    srs_error_t err = srs_success;
    
    std::string url;
    if (true) {
        std::string server;
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        
        // parse host:port from hostport.
        srs_parse_hostport(ep_forward, server, port);
        
        // generate url
        url = srs_generate_rtmp_url(server, port, req->host, req->vhost, req->app, req->stream, req->param);
    }
    
    srs_freep(sdk);
    srs_utime_t cto = SRS_FORWARDER_CIMS;
    srs_utime_t sto = SRS_CONSTS_RTMP_TIMEOUT;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        return srs_error_wrap(err, "sdk connect url=%s, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk->publish(_srs_config->get_chunk_size(req->vhost), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "sdk publish");
    }
    
    if ((err = hub->on_forwarder_start(this)) != srs_success) {
        return srs_error_wrap(err, "notify hub start");
    }
    
    if ((err = forward()) != srs_success) {
        return srs_error_wrap(err, "forward");
    }

    srs_trace("forward publish url %s, stream=%s%s as %s", url.c_str(), req->stream.c_str(), req->param.c_str(), stream.c_str());
    
    return err;
}

#define SYS_MAX_FORWARD_SEND_MSGS 128
srs_error_t SrsForwarder::forward()
{
    srs_error_t err = srs_success;
    
    sdk->set_recv_timeout(SRS_CONSTS_RTMP_PULSE);

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_forwarder());

    SrsMessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);
    
    // update sequence header
    // TODO: FIXME: maybe need to zero the sequence header timestamp.
    if (sh_video) {
        if ((err = sdk->send_and_free_message(sh_video->copy())) != srs_success) {
            return srs_error_wrap(err, "send video sh");
        }
    }
    if (sh_audio) {
        if ((err = sdk->send_and_free_message(sh_audio->copy())) != srs_success) {
            return srs_error_wrap(err, "send audio sh");
        }
    }
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "thread quit");
        }
        
        pprint->elapse();
        
        // read from client.
        if (true) {
            SrsCommonMessage* msg = NULL;
            err = sdk->recv_message(&msg);
            
            if (err != srs_success && srs_error_code(err) != ERROR_SOCKET_TIMEOUT) {
                return srs_error_wrap(err, "receive control message");
            }
            srs_error_reset(err);
            
            srs_freep(msg);
        }
        
        // forward all messages.
        // each msg in msgs.msgs must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = queue->dump_packets(msgs.max, msgs.msgs, count)) != srs_success) {
            return srs_error_wrap(err, "dump packets");
        }
        
        // pithy print
        if (pprint->can_print()) {
            sdk->kbps_sample(SRS_CONSTS_LOG_FOWARDER, pprint->age(), count);
        }
        
        // ignore when no messages.
        if (count <= 0) {
            continue;
        }
        
        // sendout messages, all messages are freed by send_and_free_messages().
        if ((err = sdk->send_and_free_messages(msgs.msgs, count)) != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }
    
    return err;
}


