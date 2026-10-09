//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_forward.hpp>

#include <algorithm>
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
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_json.hpp>
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

    state_ = "idle";
    state_at_ = 0;
    last_error_at_ = 0;
    connects_ = failures_ = 0;
    send_bytes_ = dropped_frames_ = 0;
    wait_keyframe_ = false;

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

    bool is_overflow = false;
    if ((err = queue_->enqueue(msg, &is_overflow)) != srs_success) {
        return srs_error_wrap(err, "enqueue audio");
    }

    // The queue drops the frames when overflow, so wait for a keyframe to recover.
    if (is_overflow && sh_video_) {
        wait_keyframe_ = true;
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

    bool is_overflow = false;
    if ((err = queue_->enqueue(msg, &is_overflow)) != srs_success) {
        return srs_error_wrap(err, "enqueue video");
    }

    // The queue drops the frames when overflow, so wait for a keyframe to recover.
    if (is_overflow && sh_video_) {
        wait_keyframe_ = true;
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
            // Not a failure if stopped, for example, the destination is removed or the stream is unpublished.
            srs_error_t r0 = trd_->pull();
            if (r0 != srs_success) {
                srs_freep(err);
                return srs_error_wrap(r0, "forwarder");
            }

            failures_++;
            last_error_ = srs_forward_redact_error(srs_error_summary(err));
            last_error_at_ = srs_time_now_realtime();
            set_state("retrying");

            srs_warn("Forwarder: Ignore error, failures=%" PRId64 ", retry in %dms, %s", failures_,
                     srsu2msi(SRS_FORWARDER_CIMS), srs_forward_redact_error(srs_error_desc(err)).c_str());
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

    set_state("connecting");

    srs_freep(sdk_);
    srs_utime_t cto = SRS_FORWARDER_CIMS;
    srs_utime_t sto = SRS_CONSTS_RTMP_TIMEOUT;
    sdk_ = app_factory_->create_rtmp_client(url, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        return srs_error_wrap(err, "sdk connect url=%s, cto=%dms, sto=%dms.", srs_forward_redact_url(url).c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk_->publish(config_->get_chunk_size(req_->vhost_), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "sdk publish");
    }

    // The packets queued while disconnected are stale, drop them instead of sending them in a burst.
    queue_->clear();

    if ((err = hub_->on_forwarder_start(this)) != srs_success) {
        return srs_error_wrap(err, "notify hub start");
    }

    connects_++;
    set_state("forwarding");
    srs_trace("Forwarder: Publish to %s, connects=%" PRId64 ", failures=%" PRId64, srs_forward_redact_url(url).c_str(), connects_, failures_);

    if ((err = forward()) != srs_success) {
        return srs_error_wrap(err, "forward");
    }

    return err;
}

#define SYS_MAX_FORWARD_SEND_MSGS 128
srs_error_t SrsForwarder::forward()
{
    srs_error_t err = srs_success;

    sdk_->set_recv_timeout(SRS_CONSTS_RTMP_PULSE);

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_forwarder());

    SrsMessageArray msgs(SYS_MAX_FORWARD_SEND_MSGS);

    // Drop the video frames until a keyframe, so the destination is able to decode from the first frame.
    wait_keyframe_ = (sh_video_ != NULL);

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

        // Drop the video frames before the keyframe, and audio is not dropped.
        if (wait_keyframe_ && count > 0) {
            int nn_msgs = 0;
            for (int i = 0; i < count; i++) {
                SrsMediaPacket *msg = msgs.msgs_[i];
                if (wait_keyframe_ && msg->is_video() && !SrsFlvVideo::sh(msg->payload(), msg->size())) {
                    if (!SrsFlvVideo::keyframe(msg->payload(), msg->size())) {
                        dropped_frames_++;
                        srs_freep(msg);
                        continue;
                    }
                    wait_keyframe_ = false;
                }
                msgs.msgs_[nn_msgs++] = msg;
            }
            count = nn_msgs;
        }

        // ignore when no messages.
        if (count <= 0) {
            continue;
        }

        for (int i = 0; i < count; i++) {
            send_bytes_ += msgs.msgs_[i]->size();
        }

        // sendout messages, all messages are freed by send_and_free_messages().
        if ((err = sdk_->send_and_free_messages(msgs.msgs_, count)) != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }

    return err;
}

void SrsForwarder::dumps(SrsJsonObject *obj)
{
    obj->set("state", SrsJsonAny::str(state_.c_str()));
    obj->set("state_at", SrsJsonAny::integer(srsu2ms(state_at_)));
    obj->set("connects", SrsJsonAny::integer(connects_));
    obj->set("failures", SrsJsonAny::integer(failures_));
    obj->set("last_error", SrsJsonAny::str(last_error_.c_str()));
    obj->set("last_error_at", SrsJsonAny::integer(srsu2ms(last_error_at_)));
    obj->set("send_bytes", SrsJsonAny::integer(send_bytes_));
    obj->set("dropped_frames", SrsJsonAny::integer(dropped_frames_));
}

void SrsForwarder::set_state(std::string state)
{
    state_ = state;
    state_at_ = srs_time_now_realtime();
}

std::string srs_forward_redact_url(std::string url)
{
    size_t pos = url.find("://");
    if (pos == string::npos) {
        return "***";
    }

    // Keep the schema and host, for example, rtmp://host:port/
    size_t host_end = url.find("/", pos + 3);
    if (host_end == string::npos) {
        return url.substr(0, url.find("?")) + "/***";
    }

    // Keep the app without query, and hide the stream and query.
    size_t app_end = url.find("/", host_end + 1);
    if (app_end == string::npos) {
        return url.substr(0, host_end + 1) + "***";
    }

    string app = url.substr(host_end + 1, app_end - host_end - 1);
    app = app.substr(0, app.find("?"));
    return url.substr(0, host_end + 1) + app + "/***";
}

std::string srs_forward_redact_error(std::string msg)
{
    std::string key = "stream=";

    size_t pos = 0;
    while ((pos = msg.find(key, pos)) != string::npos) {
        size_t start = pos + key.size();
        size_t end = msg.find_first_of(", \t\r\n", start);
        if (end == string::npos) {
            end = msg.size();
        }

        msg = msg.substr(0, start) + "***" + msg.substr(end);
        pos = start + 3;
    }

    return msg;
}

SrsForwardDestination::SrsForwardDestination()
{
    created_at_ = 0;
}

SrsForwardDestination::~SrsForwardDestination()
{
}

std::string SrsForwardDestination::stream_url()
{
    return srs_net_url_encode_sid(vhost_, app_, stream_);
}

SrsForwardDestination *SrsForwardDestination::copy()
{
    SrsForwardDestination *cp = new SrsForwardDestination();
    cp->id_ = id_;
    cp->vhost_ = vhost_;
    cp->app_ = app_;
    cp->stream_ = stream_;
    cp->url_ = url_;
    cp->created_at_ = created_at_;
    return cp;
}

ISrsForwardDestinationHandler::ISrsForwardDestinationHandler()
{
}

ISrsForwardDestinationHandler::~ISrsForwardDestinationHandler()
{
}

ISrsForwardDestinations::ISrsForwardDestinations()
{
}

ISrsForwardDestinations::~ISrsForwardDestinations()
{
}

ISrsForwardDestinations *_srs_forward_destinations = NULL;

SrsForwardDestinations::SrsForwardDestinations()
{
    rand_ = new SrsRand();
}

SrsForwardDestinations::~SrsForwardDestinations()
{
    std::vector<SrsForwardDestination *>::iterator it;
    for (it = dests_.begin(); it != dests_.end(); ++it) {
        SrsForwardDestination *dest = *it;
        srs_freep(dest);
    }
    dests_.clear();

    srs_freep(rand_);
}

srs_error_t SrsForwardDestinations::add(SrsForwardDestination *dest, bool *created, SrsForwardDestination **pstored)
{
    srs_error_t err = srs_success;

    *created = false;
    if (pstored) {
        *pstored = NULL;
    }

    if ((err = check(dest)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // Ignore if exists, so the caller is able to apply the destinations again.
    SrsForwardDestination *exists = find(dest->id_);
    if (exists) {
        if (exists->stream_url() != dest->stream_url() || exists->url_ != dest->url_) {
            return srs_error_new(ERROR_FORWARD_DEST_EXISTS, "id=%s exists for stream=%s", dest->id_.c_str(), exists->stream_url().c_str());
        }
        if (pstored) {
            *pstored = exists;
        }
        return err;
    }

    if ((int)dests_.size() >= SRS_FORWARD_DESTINATIONS_MAX) {
        return srs_error_new(ERROR_FORWARD_DEST_LIMIT, "exceed max %d destinations", SRS_FORWARD_DESTINATIONS_MAX);
    }

    SrsForwardDestination *cp = dest->copy();
    if (cp->id_.empty()) {
        do {
            cp->id_ = rand_->gen_str(8);
        } while (find(cp->id_));
    }
    cp->created_at_ = srs_time_now_realtime();
    dests_.push_back(cp);

    // Start forwarding if the stream is publishing, rollback if failed.
    ISrsForwardDestinationHandler *handler = handler_of(cp->stream_url());
    if (handler && (err = handler->on_forward_destination_add(cp)) != srs_success) {
        std::vector<SrsForwardDestination *>::iterator it = std::find(dests_.begin(), dests_.end(), cp);
        if (it != dests_.end()) {
            dests_.erase(it);
        }
        srs_freep(cp);
        return srs_error_wrap(err, "start forward");
    }

    *created = true;
    if (pstored) {
        *pstored = cp;
    }
    srs_trace("Forward: Add destination id=%s, stream=%s, url=%s, publishing=%d, total=%d", cp->id_.c_str(),
              cp->stream_url().c_str(), srs_forward_redact_url(cp->url_).c_str(), handler != NULL, (int)dests_.size());

    return err;
}

srs_error_t SrsForwardDestinations::remove(std::string id)
{
    srs_error_t err = srs_success;

    SrsForwardDestination *dest = find(id);
    if (!dest) {
        return srs_error_new(ERROR_FORWARD_DEST_NOT_FOUND, "id=%s", id.c_str());
    }

    // Remove it before stopping the forwarder, which switches the coroutine context.
    std::vector<SrsForwardDestination *>::iterator it = std::find(dests_.begin(), dests_.end(), dest);
    dests_.erase(it);
    SrsUniquePtr<SrsForwardDestination> dest_uptr(dest);

    ISrsForwardDestinationHandler *handler = handler_of(dest->stream_url());
    srs_trace("Forward: Remove destination id=%s, stream=%s, url=%s, publishing=%d, total=%d", dest->id_.c_str(),
              dest->stream_url().c_str(), srs_forward_redact_url(dest->url_).c_str(), handler != NULL, (int)dests_.size());

    if (handler) {
        handler->on_forward_destination_remove(id);
    }

    return err;
}

SrsForwardDestination *SrsForwardDestinations::find(std::string id)
{
    if (id.empty()) {
        return NULL;
    }

    std::vector<SrsForwardDestination *>::iterator it;
    for (it = dests_.begin(); it != dests_.end(); ++it) {
        SrsForwardDestination *dest = *it;
        if (dest->id_ == id) {
            return dest;
        }
    }

    return NULL;
}

void SrsForwardDestinations::fetch(std::string stream_url, std::vector<SrsForwardDestination *> &dests)
{
    std::vector<SrsForwardDestination *>::iterator it;
    for (it = dests_.begin(); it != dests_.end(); ++it) {
        SrsForwardDestination *dest = *it;
        if (stream_url.empty() || dest->stream_url() == stream_url) {
            dests.push_back(dest);
        }
    }
}

void SrsForwardDestinations::dumps(SrsForwardDestination *dest, SrsJsonObject *obj)
{
    obj->set("id", SrsJsonAny::str(dest->id_.c_str()));
    obj->set("vhost", SrsJsonAny::str(dest->vhost_.c_str()));
    obj->set("app", SrsJsonAny::str(dest->app_.c_str()));
    obj->set("stream", SrsJsonAny::str(dest->stream_.c_str()));
    // Never expose the stream key of destination.
    obj->set("url", SrsJsonAny::str(srs_forward_redact_url(dest->url_).c_str()));
    obj->set("created_at", SrsJsonAny::integer(srsu2ms(dest->created_at_)));

    ISrsForwardDestinationHandler *handler = handler_of(dest->stream_url());
    if (handler) {
        handler->on_forward_destination_dumps(dest->id_, obj);
    } else {
        obj->set("state", SrsJsonAny::str("idle"));
    }
}

void SrsForwardDestinations::subscribe(std::string stream_url, ISrsForwardDestinationHandler *handler)
{
    handlers_[stream_url] = handler;
}

void SrsForwardDestinations::unsubscribe(std::string stream_url, ISrsForwardDestinationHandler *handler)
{
    std::map<std::string, ISrsForwardDestinationHandler *>::iterator it = handlers_.find(stream_url);
    if (it != handlers_.end() && it->second == handler) {
        handlers_.erase(it);
    }
}

srs_error_t SrsForwardDestinations::check(SrsForwardDestination *dest)
{
    srs_error_t err = srs_success;

    // The id is used in the API path, so only allow the safe chars.
    if (dest->id_.size() > 64) {
        return srs_error_new(ERROR_FORWARD_DEST_INVALID, "id too long %d", (int)dest->id_.size());
    }
    for (int i = 0; i < (int)dest->id_.size(); i++) {
        char ch = dest->id_.at(i);
        if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9') && ch != '-' && ch != '_') {
            return srs_error_new(ERROR_FORWARD_DEST_INVALID, "invalid id=%s", dest->id_.c_str());
        }
    }

    if (dest->app_.empty() || dest->stream_.empty()) {
        return srs_error_new(ERROR_FORWARD_DEST_INVALID, "no stream app=%s, stream=%s", dest->app_.c_str(), dest->stream_.c_str());
    }

    // Only support RTMP, for example, rtmp://host[:port]/app/stream[?param]
    SrsHttpUri uri;
    if ((err = uri.initialize(dest->url_)) != srs_success) {
        srs_freep(err);
        return srs_error_new(ERROR_FORWARD_DEST_INVALID, "invalid url=%s", srs_forward_redact_url(dest->url_).c_str());
    }
    if (uri.get_schema() != "rtmp" || uri.get_host().empty()) {
        return srs_error_new(ERROR_FORWARD_DEST_INVALID, "not rtmp url=%s", srs_forward_redact_url(dest->url_).c_str());
    }

    SrsPath path;
    std::string app = path.filepath_dir(uri.get_path());
    std::string stream = path.filepath_base(uri.get_path());
    if (app.empty() || app == "/" || stream.empty()) {
        return srs_error_new(ERROR_FORWARD_DEST_INVALID, "no app or stream url=%s", srs_forward_redact_url(dest->url_).c_str());
    }

    return err;
}

ISrsForwardDestinationHandler *SrsForwardDestinations::handler_of(std::string stream_url)
{
    std::map<std::string, ISrsForwardDestinationHandler *>::iterator it = handlers_.find(stream_url);
    if (it == handlers_.end()) {
        return NULL;
    }
    return it->second;
}
