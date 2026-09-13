//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_forward.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_st.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

ISrsForwarder::ISrsForwarder()
{
}

ISrsForwarder::~ISrsForwarder()
{
}

SrsForwarder::SrsForwarder(ISrsOriginHub *h)
{
    hub_ = h;

    req_ = NULL;
    sh_video_ = sh_audio_ = NULL;

    sdk_ = NULL;
    trd_ = new SrsDummyCoroutine();
    queue_ = new SrsMessageQueue();
    jitter_ = new SrsRtmpJitter();

    app_factory_ = _srs_app_factory;
    config_ = _srs_config;
}

SrsForwarder::~SrsForwarder()
{
    srs_freep(sdk_);
    srs_freep(trd_);
    srs_freep(queue_);
    srs_freep(jitter_);

    srs_freep(sh_video_);
    srs_freep(sh_audio_);

    srs_freep(req_);

    app_factory_ = NULL;
    config_ = NULL;
}

srs_error_t SrsForwarder::initialize(ISrsRequest *r, string ep)
{
    srs_error_t err = srs_success;

    // it's ok to use the request object,
    // SrsLiveSource already copy it and never delete it.
    req_ = r->copy();

    // the ep(endpoint) to forward to
    ep_forward_ = ep;

    // Check if the forward destination is RTMPS URL
    // SRS forward only supports plain RTMP protocol, not RTMPS (RTMP over SSL/TLS)
    if (ep_forward_.find("rtmps://") != string::npos) {
        return srs_error_new(ERROR_NOT_SUPPORTED, "forward does not support RTMPS destination=%s", ep_forward_.c_str());
    }

    // Remember the source context id.
    source_cid_ = _srs_context->get_id();

    return err;
}

void SrsForwarder::set_queue_size(srs_utime_t queue_size)
{
    queue_->set_queue_size(queue_size);
}

srs_error_t SrsForwarder::on_publish()
{
    srs_error_t err = srs_success;

    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("forward", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }

    return err;
}

void SrsForwarder::on_unpublish()
{
    trd_->stop();
    if (sdk_)
        sdk_->close();
}

srs_error_t SrsForwarder::on_meta_data(SrsMediaPacket *shared_metadata)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *metadata = shared_metadata->copy();

    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = jitter_->correct(metadata, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }

    if ((err = queue_->enqueue(metadata)) != srs_success) {
        return srs_error_wrap(err, "enqueue metadata");
    }

    return err;
}

srs_error_t SrsForwarder::on_audio(SrsMediaPacket *shared_audio)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *msg = shared_audio->copy();

    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = jitter_->correct(msg, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }

    if (SrsFlvAudio::sh(msg->payload(), msg->size())) {
        srs_freep(sh_audio_);
        sh_audio_ = msg->copy();
    }

    if ((err = queue_->enqueue(msg)) != srs_success) {
        return srs_error_wrap(err, "enqueue audio");
    }

    return err;
}

srs_error_t SrsForwarder::on_video(SrsMediaPacket *shared_video)
{
    srs_error_t err = srs_success;

    SrsMediaPacket *msg = shared_video->copy();

    // TODO: FIXME: config the jitter of Forwarder.
    if ((err = jitter_->correct(msg, SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "jitter");
    }

    if (SrsFlvVideo::sh(msg->payload(), msg->size())) {
        srs_freep(sh_video_);
        sh_video_ = msg->copy();
    }

    if ((err = queue_->enqueue(msg)) != srs_success) {
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
              req_->get_stream_url().c_str(), source_cid_.c_str(), ep_forward_.c_str());

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "forwarder");
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("Forwarder: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        // Never wait if thread error, fast quit.
        // @see https://github.com/ossrs/srs/pull/2284
        if ((err = trd_->pull()) != srs_success) {
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
        srs_net_split_hostport(ep_forward_, server, port);

        // generate url
        url = srs_net_url_encode_rtmp_url(server, port, req_->host_, req_->vhost_, req_->app_, req_->stream_, req_->param_);
    }

    srs_freep(sdk_);
    srs_utime_t cto = SRS_FORWARDER_CIMS;
    srs_utime_t sto = SRS_CONSTS_RTMP_TIMEOUT;
    sdk_ = app_factory_->create_rtmp_client(url, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        return srs_error_wrap(err, "sdk connect url=%s, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk_->publish(config_->get_chunk_size(req_->vhost_), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "sdk publish");
    }

    if ((err = hub_->on_forwarder_start(this)) != srs_success) {
        return srs_error_wrap(err, "notify hub start");
    }

    if ((err = forward()) != srs_success) {
        return srs_error_wrap(err, "forward");
    }

    srs_trace("forward publish url %s, stream=%s%s as %s", url.c_str(), req_->stream_.c_str(), req_->param_.c_str(), stream.c_str());

    return err;
}

#define SYS_MAX_FORWARD_SEND_MSGS 128
srs_error_t SrsForwarder::forward()
{
    srs_error_t err = srs_success;

    sdk_->set_recv_timeout(SRS_CONSTS_RTMP_PULSE);

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_forwarder());

    SrsMessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);

    // update sequence header
    // TODO: FIXME: maybe need to zero the sequence header timestamp.
    if (sh_video_) {
        if ((err = sdk_->send_and_free_message(sh_video_->copy())) != srs_success) {
            return srs_error_wrap(err, "send video sh");
        }
    }
    if (sh_audio_) {
        if ((err = sdk_->send_and_free_message(sh_audio_->copy())) != srs_success) {
            return srs_error_wrap(err, "send audio sh");
        }
    }

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "thread quit");
        }

        pprint->elapse();

        // read from client.
        if (true) {
            SrsRtmpCommonMessage *msg = NULL;
            err = sdk_->recv_message(&msg);

            if (err != srs_success && srs_error_code(err) != ERROR_SOCKET_TIMEOUT) {
                return srs_error_wrap(err, "receive control message");
            }
            srs_freep(err);

            srs_freep(msg);
        }

        // forward all messages.
        // each msg in msgs.msgs_ must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = queue_->dump_packets(msgs.max_, msgs.msgs_, count)) != srs_success) {
            return srs_error_wrap(err, "dump packets");
        }

        // pithy print
        if (pprint->can_print()) {
            sdk_->kbps_sample(SRS_CONSTS_LOG_FOWARDER, pprint->age(), count);
        }

        // ignore when no messages.
        if (count <= 0) {
            continue;
        }

        // sendout messages, all messages are freed by send_and_free_messages().
        if ((err = sdk_->send_and_free_messages(msgs.msgs_, count)) != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }

    return err;
}
