//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_edge.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

using namespace std;

#include <srs_app_caster_flv.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_pithy_print.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

// when edge timeout, retry next.
#define SRS_EDGE_INGESTER_TIMEOUT (5 * SRS_UTIME_SECONDS)

// when edge error, wait for quit
#define SRS_EDGE_FORWARDER_TIMEOUT (150 * SRS_UTIME_MILLISECONDS)

ISrsEdgeUpstream::ISrsEdgeUpstream()
{
}

ISrsEdgeUpstream::~ISrsEdgeUpstream()
{
}

SrsEdgeRtmpUpstream::SrsEdgeRtmpUpstream(string r)
{
    redirect_ = r;
    sdk_ = NULL;
    selected_port_ = 0;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsEdgeRtmpUpstream::~SrsEdgeRtmpUpstream()
{
    close();

    config_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsEdgeRtmpUpstream::connect(ISrsRequest *r, ISrsLbRoundRobin *lb)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = r;

    std::string url;
    if (true) {
        SrsConfDirective *conf = config_->get_vhost_edge_origin(req->vhost_);

        // when origin is error, for instance, server is shutdown,
        // then user remove the vhost then reload, the conf is empty.
        if (!conf) {
            return srs_error_new(ERROR_EDGE_VHOST_REMOVED, "vhost %s removed", req->vhost_.c_str());
        }

        // select the origin.
        std::string server = lb->select(conf->args_);
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        srs_net_split_hostport(server, server, port);

        // override the origin info by redirect.
        if (!redirect_.empty()) {
            string schema, vhost, app, stream, param, host;
            srs_net_url_parse_tcurl(redirect_, schema, host, vhost, app, stream, port, param);
            server = host;
        }

        // Remember the current selected server.
        selected_ip_ = server;
        selected_port_ = port;

        // support vhost tranform for edge,
        std::string vhost = config_->get_vhost_edge_transform_vhost(req->vhost_);
        vhost = srs_strings_replace(vhost, "[vhost]", req->vhost_);

        url = srs_net_url_encode_rtmp_url(server, port, req->host_, vhost, req->app_, req->stream_, req->param_);
    }

    srs_freep(sdk_);
    srs_utime_t cto = SRS_EDGE_INGESTER_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    // Use factory to create client.
    sdk_ = app_factory_->create_rtmp_client(url, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        return srs_error_wrap(err, "edge pull %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk_->play(config_->get_chunk_size(req->vhost_), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "edge pull %s stream failed", url.c_str());
    }

    srs_trace("edge-pull publish url %s, stream=%s%s as %s", url.c_str(), req->stream_.c_str(), req->param_.c_str(), stream.c_str());

    return err;
}

srs_error_t SrsEdgeRtmpUpstream::recv_message(SrsRtmpCommonMessage **pmsg)
{
    return sdk_->recv_message(pmsg);
}

srs_error_t SrsEdgeRtmpUpstream::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    return sdk_->decode_message(msg, ppacket);
}

void SrsEdgeRtmpUpstream::close()
{
    srs_freep(sdk_);
}

void SrsEdgeRtmpUpstream::selected(string &server, int &port)
{
    server = selected_ip_;
    port = selected_port_;
}

void SrsEdgeRtmpUpstream::set_recv_timeout(srs_utime_t tm)
{
    sdk_->set_recv_timeout(tm);
}

void SrsEdgeRtmpUpstream::kbps_sample(const char *label, srs_utime_t age)
{
    sdk_->kbps_sample(label, age);
}

SrsEdgeFlvUpstream::SrsEdgeFlvUpstream(std::string schema)
{
    schema_ = schema;
    selected_port_ = 0;

    sdk_ = NULL;
    hr_ = NULL;
    reader_ = NULL;
    decoder_ = NULL;
    req_ = NULL;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsEdgeFlvUpstream::~SrsEdgeFlvUpstream()
{
    close();

    config_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsEdgeFlvUpstream::connect(ISrsRequest *r, ISrsLbRoundRobin *lb)
{
    // Because we might modify the r, which cause retry fail, so we must copy it.
    ISrsRequest *cp = r->copy();

    // Free the request when close upstream.
    srs_freep(req_);
    req_ = cp;

    return do_connect(cp, lb, 0);
}

srs_error_t SrsEdgeFlvUpstream::do_connect(ISrsRequest *r, ISrsLbRoundRobin *lb, int redirect_depth)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = r;

    if (redirect_depth == 0) {
        SrsConfDirective *conf = config_->get_vhost_edge_origin(req->vhost_);

        // when origin is error, for instance, server is shutdown,
        // then user remove the vhost then reload, the conf is empty.
        if (!conf) {
            return srs_error_new(ERROR_EDGE_VHOST_REMOVED, "vhost %s removed", req->vhost_.c_str());
        }

        // select the origin.
        std::string server = lb->select(conf->args_);
        int port = SRS_DEFAULT_HTTP_PORT;
        if (schema_ == "https") {
            port = SRS_DEFAULT_HTTPS_PORT;
        }
        srs_net_split_hostport(server, server, port);

        // Remember the current selected server.
        selected_ip_ = server;
        selected_port_ = port;
    } else {
        // If HTTP redirect, use the server in location.
        schema_ = req->schema_;
        selected_ip_ = req->host_;
        selected_port_ = req->port_;
    }

    srs_freep(sdk_);
    sdk_ = app_factory_->create_http_client();

    string path = "/" + req->app_ + "/" + req->stream_;
    if (!srs_strings_ends_with(req->stream_, ".flv")) {
        path += ".flv";
    }
    if (!req->param_.empty()) {
        path += req->param_;
    }

    string url = schema_ + "://" + selected_ip_ + ":" + srs_strconv_format_int(selected_port_);
    url += path;

    srs_utime_t cto = SRS_EDGE_INGESTER_TIMEOUT;
    if ((err = sdk_->initialize(schema_, selected_ip_, selected_port_, cto)) != srs_success) {
        return srs_error_wrap(err, "edge pull %s failed, cto=%dms.", url.c_str(), srsu2msi(cto));
    }

    srs_freep(hr_);
    if ((err = sdk_->get(path, "", &hr_)) != srs_success) {
        return srs_error_wrap(err, "edge get %s failed, path=%s", url.c_str(), path.c_str());
    }

    if (hr_->status_code() == 404) {
        return srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "Connect to %s, status=%d", url.c_str(), hr_->status_code());
    }

    string location;
    if (hr_->status_code() == 302) {
        location = hr_->header()->get("Location");
    }
    srs_trace("Edge: Connect to %s ok, status=%d, location=%s", url.c_str(), hr_->status_code(), location.c_str());

    if (hr_->status_code() == 302) {
        if (redirect_depth >= 3) {
            return srs_error_new(ERROR_HTTP_302_INVALID, "redirect to %s fail, depth=%d", location.c_str(), redirect_depth);
        }

        string app;
        string stream_name;
        if (true) {
            string tcUrl;
            srs_net_url_parse_rtmp_url(location, tcUrl, stream_name);

            int port;
            string schema, host, vhost, param;
            srs_net_url_parse_tcurl(tcUrl, schema, host, vhost, app, stream_name, port, param);

            r->schema_ = schema;
            r->host_ = host;
            r->port_ = port;
            r->app_ = app;
            r->stream_ = stream_name;
            r->param_ = param;
        }
        return do_connect(r, lb, redirect_depth + 1);
    }

    srs_freep(reader_);
    reader_ = app_factory_->create_http_file_reader(hr_->body_reader());

    srs_freep(decoder_);
    decoder_ = app_factory_->create_flv_decoder();

    if ((err = decoder_->initialize(reader_)) != srs_success) {
        return srs_error_wrap(err, "init decoder");
    }

    char header[9];
    if ((err = decoder_->read_header(header)) != srs_success) {
        return srs_error_wrap(err, "read header");
    }

    char pps[4];
    if ((err = decoder_->read_previous_tag_size(pps)) != srs_success) {
        return srs_error_wrap(err, "read pts");
    }

    return err;
}

srs_error_t SrsEdgeFlvUpstream::recv_message(SrsRtmpCommonMessage **pmsg)
{
    srs_error_t err = srs_success;

    char type;
    int32_t size;
    uint32_t time;
    if ((err = decoder_->read_tag_header(&type, &size, &time)) != srs_success) {
        return srs_error_wrap(err, "read tag header");
    }

    char *data = new char[size];
    if ((err = decoder_->read_tag_data(data, size)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "read tag data");
    }

    char pps[4];
    if ((err = decoder_->read_previous_tag_size(pps)) != srs_success) {
        return srs_error_wrap(err, "read pts");
    }

    int stream_id = 1;
    SrsRtmpCommonMessage *msg = NULL;
    if ((err = srs_rtmp_create_msg(type, time, data, size, stream_id, &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }

    *pmsg = msg;

    return err;
}

srs_error_t SrsEdgeFlvUpstream::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    srs_error_t err = srs_success;

    SrsRtmpCommand *packet = NULL;
    SrsBuffer stream(msg->payload(), msg->size());
    SrsMessageHeader &header = msg->header_;

    if (header.is_amf0_data() || header.is_amf3_data()) {
        std::string command;
        if ((err = srs_amf0_read_string(&stream, command)) != srs_success) {
            return srs_error_wrap(err, "decode command name");
        }

        stream.skip(-1 * stream.pos());

        if (command == SRS_CONSTS_RTMP_SET_DATAFRAME) {
            *ppacket = packet = new SrsOnMetaDataPacket();
            return packet->decode(&stream);
        } else if (command == SRS_CONSTS_RTMP_ON_METADATA) {
            *ppacket = packet = new SrsOnMetaDataPacket();
            return packet->decode(&stream);
        }
    }

    return err;
}

void SrsEdgeFlvUpstream::close()
{
    srs_freep(sdk_);
    srs_freep(hr_);
    srs_freep(reader_);
    srs_freep(decoder_);
    srs_freep(req_);
}

void SrsEdgeFlvUpstream::selected(string &server, int &port)
{
    server = selected_ip_;
    port = selected_port_;
}

void SrsEdgeFlvUpstream::set_recv_timeout(srs_utime_t tm)
{
    sdk_->set_recv_timeout(tm);
}

void SrsEdgeFlvUpstream::kbps_sample(const char *label, srs_utime_t age)
{
    sdk_->kbps_sample(label, age);
}

ISrsEdgeIngester::ISrsEdgeIngester()
{
}

ISrsEdgeIngester::~ISrsEdgeIngester()
{
}

SrsEdgeIngester::SrsEdgeIngester()
{
    source_ = NULL;
    edge_ = NULL;
    req_ = NULL;

    upstream_ = new SrsEdgeRtmpUpstream("");
    lb_ = new SrsLbRoundRobin();
    trd_ = new SrsDummyCoroutine();

    config_ = _srs_config;
}

SrsEdgeIngester::~SrsEdgeIngester()
{
    stop();

    srs_freep(upstream_);
    srs_freep(lb_);
    srs_freep(trd_);

    config_ = NULL;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsEdgeIngester::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPlayEdge *e, ISrsRequest *r)
{
    // Because source references to this object, so we should directly use the source ptr.
    source_ = s.get();

    edge_ = e;
    req_ = r;

    return srs_success;
}

srs_error_t SrsEdgeIngester::start()
{
    srs_error_t err = srs_success;

    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "notify source");
    }

    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("edge-igs", this);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

void SrsEdgeIngester::stop()
{
    trd_->stop();
    upstream_->close();

    // Notify source to un-publish if exists.
    if (source_) {
        source_->on_unpublish();
    }
}

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsEdgeIngester::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "edge ingester");
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("EdgeIngester: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        // Check whether coroutine is stopped, see https://github.com/ossrs/srs/issues/2901
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "edge ingester");
        }

        srs_usleep(SRS_EDGE_INGESTER_CIMS);
    }

    return err;
}

srs_error_t SrsEdgeIngester::do_cycle()
{
    srs_error_t err = srs_success;

    std::string redirect;
    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "do cycle pull");
        }

        // Use protocol in config.
        string edge_protocol = config_->get_vhost_edge_protocol(req_->vhost_);

        // If follow client protocol, change to protocol of client.
        bool follow_client = config_->get_vhost_edge_follow_client(req_->vhost_);
        if (follow_client && !req_->protocol_.empty()) {
            edge_protocol = req_->protocol_;
        }

        // Create object by protocol.
        srs_freep(upstream_);
        if (edge_protocol == "flv" || edge_protocol == "flvs") {
            upstream_ = new SrsEdgeFlvUpstream(edge_protocol == "flv" ? "http" : "https");
        } else {
            upstream_ = new SrsEdgeRtmpUpstream(redirect);
        }

        if ((err = source_->on_source_id_changed(_srs_context->get_id())) != srs_success) {
            return srs_error_wrap(err, "on source id changed");
        }

        if ((err = upstream_->connect(req_, lb_)) != srs_success) {
            return srs_error_wrap(err, "connect upstream");
        }

        if ((err = edge_->on_ingest_play()) != srs_success) {
            return srs_error_wrap(err, "notify edge play");
        }

        // set to larger timeout to read av data from origin.
        upstream_->set_recv_timeout(SRS_EDGE_INGESTER_TIMEOUT);

        err = ingest(redirect);

        // retry for rtmp 302 immediately.
        if (srs_error_code(err) == ERROR_CONTROL_REDIRECT) {
            int port;
            string server;
            upstream_->selected(server, port);

            string url = req_->get_stream_url();
            srs_warn("RTMP redirect %s from %s:%d to %s", url.c_str(), server.c_str(), port, redirect.c_str());

            srs_freep(err);
            continue;
        }

        if (srs_is_client_gracefully_close(err)) {
            srs_warn("origin disconnected, retry, error %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        break;
    }

    return err;
}

srs_error_t SrsEdgeIngester::ingest(string &redirect)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_edge());

    // we only use the redict once.
    // reset the redirect to empty, for maybe the origin changed.
    redirect = "";

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "thread quit");
        }

        pprint->elapse();

        // pithy print
        if (pprint->can_print()) {
            upstream_->kbps_sample(SRS_CONSTS_LOG_EDGE_PLAY, pprint->age());
        }

        // read from client.
        SrsRtmpCommonMessage *msg_raw = NULL;
        if ((err = upstream_->recv_message(&msg_raw)) != srs_success) {
            return srs_error_wrap(err, "recv message");
        }

        srs_assert(msg_raw);
        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);

        if ((err = process_publish_message(msg.get(), redirect)) != srs_success) {
            return srs_error_wrap(err, "process message");
        }
    }

    return err;
}

srs_error_t SrsEdgeIngester::process_publish_message(SrsRtmpCommonMessage *msg, string &redirect)
{
    srs_error_t err = srs_success;

    // process audio packet
    if (msg->header_.is_audio()) {
        if ((err = source_->on_audio(msg)) != srs_success) {
            return srs_error_wrap(err, "source consume audio");
        }
    }

    // process video packet
    if (msg->header_.is_video()) {
        if ((err = source_->on_video(msg)) != srs_success) {
            return srs_error_wrap(err, "source consume video");
        }
    }

    // process aggregate packet
    if (msg->header_.is_aggregate()) {
        if ((err = source_->on_aggregate(msg)) != srs_success) {
            return srs_error_wrap(err, "source consume aggregate");
        }
        return err;
    }

    // process onMetaData
    if (msg->header_.is_amf0_data() || msg->header_.is_amf3_data()) {
        SrsRtmpCommand *pkt_raw = NULL;
        if ((err = upstream_->decode_message(msg, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

        if (dynamic_cast<SrsOnMetaDataPacket *>(pkt.get())) {
            SrsOnMetaDataPacket *metadata = dynamic_cast<SrsOnMetaDataPacket *>(pkt.get());
            if ((err = source_->on_meta_data(msg, metadata)) != srs_success) {
                return srs_error_wrap(err, "source consume metadata");
            }
            return err;
        }

        return err;
    }

    // call messages, for example, reject, redirect.
    if (msg->header_.is_amf0_command() || msg->header_.is_amf3_command()) {
        SrsRtmpCommand *pkt_raw = NULL;
        if ((err = upstream_->decode_message(msg, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

        // RTMP 302 redirect
        if (dynamic_cast<SrsCallPacket *>(pkt.get())) {
            SrsCallPacket *call = dynamic_cast<SrsCallPacket *>(pkt.get());
            if (!call->arguments_->is_object()) {
                return err;
            }

            SrsAmf0Any *prop = NULL;
            SrsAmf0Object *evt = call->arguments_->to_object();

            if ((prop = evt->ensure_property_string("level")) == NULL) {
                return err;
            } else if (prop->to_str() != StatusLevelError) {
                return err;
            }

            if ((prop = evt->get_property("ex")) == NULL || !prop->is_object()) {
                return err;
            }
            SrsAmf0Object *ex = prop->to_object();

            // The redirect is tcUrl while redirect2 is RTMP URL.
            // https://github.com/ossrs/srs/issues/1575#issuecomment-574999798
            if ((prop = ex->ensure_property_string("redirect2")) == NULL) {
                // TODO: FIXME: Remove it when SRS3 released, it's temporarily support for SRS3 alpha versions(a0 to a8).
                if ((prop = ex->ensure_property_string("redirect")) == NULL) {
                    return err;
                }
            }
            redirect = prop->to_str();

            return srs_error_new(ERROR_CONTROL_REDIRECT, "RTMP 302 redirect to %s", redirect.c_str());
        }
    }

    return err;
}

ISrsEdgeForwarder::ISrsEdgeForwarder()
{
}

ISrsEdgeForwarder::~ISrsEdgeForwarder()
{
}

SrsEdgeForwarder::SrsEdgeForwarder()
{
    edge_ = NULL;
    req_ = NULL;
    send_error_code_ = ERROR_SUCCESS;
    source_ = NULL;

    sdk_ = NULL;
    lb_ = new SrsLbRoundRobin();
    trd_ = new SrsDummyCoroutine();
    queue_ = new SrsMessageQueue();

    config_ = _srs_config;
}

SrsEdgeForwarder::~SrsEdgeForwarder()
{
    stop();

    srs_freep(lb_);
    srs_freep(trd_);
    srs_freep(queue_);

    config_ = NULL;
}

void SrsEdgeForwarder::set_queue_size(srs_utime_t queue_size)
{
    return queue_->set_queue_size(queue_size);
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsEdgeForwarder::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r)
{
    // Because source references to this object, so we should directly use the source ptr.
    source_ = s.get();

    edge_ = e;
    req_ = r;

    return srs_success;
}

srs_error_t SrsEdgeForwarder::start()
{
    srs_error_t err = srs_success;

    // reset the error code.
    send_error_code_ = ERROR_SUCCESS;

    std::string url;
    if (true) {
        SrsConfDirective *conf = config_->get_vhost_edge_origin(req_->vhost_);
        srs_assert(conf);

        // select the origin.
        std::string server = lb_->select(conf->args_);
        int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        srs_net_split_hostport(server, server, port);

        // support vhost tranform for edge,
        std::string vhost = config_->get_vhost_edge_transform_vhost(req_->vhost_);
        vhost = srs_strings_replace(vhost, "[vhost]", req_->vhost_);

        url = srs_net_url_encode_rtmp_url(server, port, req_->host_, vhost, req_->app_, req_->stream_, req_->param_);
    }

    // We must stop the coroutine before disposing the sdk.
    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("edge-fwr", this, _srs_context->get_id());

    // open socket.
    srs_freep(sdk_);
    srs_utime_t cto = SRS_EDGE_FORWARDER_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_TIMEOUT;
    sdk_ = new SrsSimpleRtmpClient(url, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        return srs_error_wrap(err, "sdk connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk_->publish(config_->get_chunk_size(req_->vhost_), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "sdk publish");
    }

    // Start the forwarding coroutine.
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    srs_trace("edge-fwr publish url %s, stream=%s%s as %s", url.c_str(), req_->stream_.c_str(), req_->param_.c_str(), stream.c_str());

    return err;
}

void SrsEdgeForwarder::stop()
{
    // Make sure the coroutine is stopped before disposing the sdk,
    // for sdk is used by coroutine.
    trd_->stop();
    srs_freep(sdk_);

    queue_->clear();
}

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_FORWARDER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsEdgeForwarder::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "thread pull");
        }

        if ((err = do_cycle()) != srs_success) {
            // If cycle stopping, we should always set the quit error code.
            if (send_error_code_ == 0) {
                send_error_code_ = srs_error_code(err);
            }

            return srs_error_wrap(err, "do cycle");
        }

        srs_usleep(SRS_EDGE_FORWARDER_CIMS);
    }

    return err;
}

#define SYS_MAX_EDGE_SEND_MSGS 128

srs_error_t SrsEdgeForwarder::do_cycle()
{
    srs_error_t err = srs_success;

    sdk_->set_recv_timeout(SRS_CONSTS_RTMP_PULSE);

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_edge());
    SrsMessageArray msgs(SYS_MAX_EDGE_SEND_MSGS);

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "edge forward pull");
        }

        if (send_error_code_ != ERROR_SUCCESS) {
            srs_usleep(SRS_EDGE_FORWARDER_TIMEOUT);
            continue;
        }

        // read from client.
        if (true) {
            SrsRtmpCommonMessage *msg = NULL;
            err = sdk_->recv_message(&msg);

            if (err != srs_success && srs_error_code(err) != ERROR_SOCKET_TIMEOUT) {
                srs_error("edge push get server control message failed. err=%s", srs_error_desc(err).c_str());
                send_error_code_ = srs_error_code(err);
                srs_freep(err);
                continue;
            }

            srs_freep(err);
            srs_freep(msg);
        }

        // forward all messages.
        // each msg in msgs.msgs_ must be free, for the SrsMessageArray never free them.
        int count = 0;
        if ((err = queue_->dump_packets(msgs.max_, msgs.msgs_, count)) != srs_success) {
            return srs_error_wrap(err, "queue dumps packets");
        }

        pprint->elapse();

        // pithy print
        if (pprint->can_print()) {
            sdk_->kbps_sample(SRS_CONSTS_LOG_EDGE_PUBLISH, pprint->age(), count);
        }

        // ignore when no messages.
        if (count <= 0) {
            srs_verbose("edge no packets to push.");
            continue;
        }

        // sendout messages, all messages are freed by send_and_free_messages().
        if ((err = sdk_->send_and_free_messages(msgs.msgs_, count)) != srs_success) {
            return srs_error_wrap(err, "send messages");
        }
    }

    return err;
}

srs_error_t SrsEdgeForwarder::proxy(SrsRtmpCommonMessage *msg)
{
    srs_error_t err = srs_success;

    if (send_error_code_ != ERROR_SUCCESS) {
        return srs_error_new(send_error_code_, "edge forwarder");
    }

    // the msg is auto free by source,
    // so we just ignore, or copy then send it.
    if (msg->size() <= 0 || msg->header_.is_set_chunk_size() || msg->header_.is_window_ackledgement_size() || msg->header_.is_ackledgement()) {
        return err;
    }

    SrsMediaPacket copy;
    msg->to_msg(&copy);

    copy.stream_id_ = sdk_->sid();
    if ((err = queue_->enqueue(copy.copy())) != srs_success) {
        return srs_error_wrap(err, "enqueue message");
    }

    return err;
}

ISrsPlayEdge::ISrsPlayEdge()
{
}

ISrsPlayEdge::~ISrsPlayEdge()
{
}

SrsPlayEdge::SrsPlayEdge()
{
    state_ = SrsEdgeStateInit;
    ingester_ = new SrsEdgeIngester();
}

SrsPlayEdge::~SrsPlayEdge()
{
    srs_freep(ingester_);
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsPlayEdge::initialize(SrsSharedPtr<SrsLiveSource> source, ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if ((err = ingester_->initialize(source, this, req)) != srs_success) {
        return srs_error_wrap(err, "ingester(pull)");
    }

    return err;
}

srs_error_t SrsPlayEdge::on_client_play()
{
    srs_error_t err = srs_success;

    // start ingest when init state.
    if (state_ == SrsEdgeStateInit) {
        state_ = SrsEdgeStatePlay;
        err = ingester_->start();
    } else if (state_ == SrsEdgeStateIngestStopping) {
        return srs_error_new(ERROR_RTMP_EDGE_PLAY_STATE, "state is stopping");
    }

    return err;
}

void SrsPlayEdge::on_all_client_stop()
{
    // when all client disconnected,
    // and edge is ingesting origin stream, abort it.
    if (state_ == SrsEdgeStatePlay || state_ == SrsEdgeStateIngestConnected) {
        SrsEdgeState pstate = state_;
        state_ = SrsEdgeStateIngestStopping;

        ingester_->stop();

        state_ = SrsEdgeStateInit;
        srs_trace("edge change from %d to %d then %d (init).", pstate, SrsEdgeStateIngestStopping, state_);

        return;
    }
}

srs_error_t SrsPlayEdge::on_ingest_play()
{
    srs_error_t err = srs_success;

    // when already connected(for instance, reconnect for error), ignore.
    if (state_ == SrsEdgeStateIngestConnected) {
        return err;
    }

    srs_assert(state_ == SrsEdgeStatePlay);

    SrsEdgeState pstate = state_;
    state_ = SrsEdgeStateIngestConnected;
    srs_trace("edge change from %d to state %d (pull).", pstate, state_);

    return err;
}

ISrsPublishEdge::ISrsPublishEdge()
{
}

ISrsPublishEdge::~ISrsPublishEdge()
{
}

SrsPublishEdge::SrsPublishEdge()
{
    state_ = SrsEdgeStateInit;
    forwarder_ = new SrsEdgeForwarder();
}

SrsPublishEdge::~SrsPublishEdge()
{
    srs_freep(forwarder_);
}

void SrsPublishEdge::set_queue_size(srs_utime_t queue_size)
{
    return forwarder_->set_queue_size(queue_size);
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsPublishEdge::initialize(SrsSharedPtr<SrsLiveSource> source, ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if ((err = forwarder_->initialize(source, this, req)) != srs_success) {
        return srs_error_wrap(err, "forwarder(push)");
    }

    return err;
}

bool SrsPublishEdge::can_publish()
{
    return state_ != SrsEdgeStatePublish;
}

srs_error_t SrsPublishEdge::on_client_publish()
{
    srs_error_t err = srs_success;

    // error when not init state.
    if (state_ != SrsEdgeStateInit) {
        return srs_error_new(ERROR_RTMP_EDGE_PUBLISH_STATE, "invalid state");
    }

    // to avoid multiple publish the same stream on the same edge,
    // directly enter the publish stage.
    if (true) {
        SrsEdgeState pstate = state_;
        state_ = SrsEdgeStatePublish;
        srs_trace("edge change from %d to state %d (push).", pstate, state_);
    }

    // start to forward stream to origin.
    err = forwarder_->start();

    // when failed, revert to init
    if (err != srs_success) {
        SrsEdgeState pstate = state_;
        state_ = SrsEdgeStateInit;
        srs_trace("edge revert from %d to state %d (push), error %s", pstate, state_, srs_error_desc(err).c_str());
    }

    return err;
}

srs_error_t SrsPublishEdge::on_proxy_publish(SrsRtmpCommonMessage *msg)
{
    return forwarder_->proxy(msg);
}

void SrsPublishEdge::on_proxy_unpublish()
{
    if (state_ == SrsEdgeStatePublish) {
        forwarder_->stop();
    }

    SrsEdgeState pstate = state_;
    state_ = SrsEdgeStateInit;
    srs_trace("edge change from %d to state %d (init).", pstate, state_);
}
