//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_edge.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_io.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_app_source.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_app_caster_flv.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_tencentcloud.hpp>

// when edge timeout, retry next.
#define SRS_EDGE_INGESTER_TIMEOUT (5 * SRS_UTIME_SECONDS)

// when edge error, wait for quit
#define SRS_EDGE_FORWARDER_TIMEOUT (150 * SRS_UTIME_MILLISECONDS)

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
    selected_port = 0;
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
            
            server = _host;
            port = _port;
        }

        // Remember the current selected server.
        selected_ip = server;
        selected_port = port;
        
        // support vhost tranform for edge,
        std::string vhost = _srs_config->get_vhost_edge_transform_vhost(req->vhost);
        vhost = srs_string_replace(vhost, "[vhost]", req->vhost);
        
        url = srs_generate_rtmp_url(server, port, req->host, vhost, req->app, req->stream, req->param);
    }
    
    srs_freep(sdk);
    srs_utime_t cto = SRS_EDGE_INGESTER_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);

#ifdef SRS_APM
    // Create a client span and store it to an AMF0 propagator.
    ISrsApmSpan* span_client = _srs_apm->inject(_srs_apm->span("edge-pull")->set_kind(SrsApmKindClient)->as_child(_srs_apm->load()), sdk->extra_args());
    SrsAutoFree(ISrsApmSpan, span_client);
#endif

    if ((err = sdk->connect()) != srs_success) {
        return srs_error_wrap(err, "edge pull %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk->play(_srs_config->get_chunk_size(req->vhost), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "edge pull %s stream failed", url.c_str());
    }

    srs_trace("edge-pull publish url %s, stream=%s%s as %s", url.c_str(), req->stream.c_str(), req->param.c_str(), stream.c_str());
    
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

void SrsEdgeRtmpUpstream::selected(string& server, int& port)
{
    server = selected_ip;
    port = selected_port;
}

void SrsEdgeRtmpUpstream::set_recv_timeout(srs_utime_t tm)
{
    sdk->set_recv_timeout(tm);
}

void SrsEdgeRtmpUpstream::kbps_sample(const char* label, srs_utime_t age)
{
    sdk->kbps_sample(label, age);
}

SrsEdgeFlvUpstream::SrsEdgeFlvUpstream(std::string schema)
{
    schema_ = schema;
    selected_port = 0;

    sdk_ = NULL;
    hr_ = NULL;
    reader_ = NULL;
    decoder_ = NULL;
    req_ = NULL;
}

SrsEdgeFlvUpstream::~SrsEdgeFlvUpstream()
{
    close();
}

srs_error_t SrsEdgeFlvUpstream::connect(SrsRequest* r, SrsLbRoundRobin* lb)
{
    // Because we might modify the r, which cause retry fail, so we must copy it.
    SrsRequest* cp = r->copy();

    // Free the request when close upstream.
    srs_freep(req_);
    req_ = cp;

    return do_connect(cp, lb, 0);
}

srs_error_t SrsEdgeFlvUpstream::do_connect(SrsRequest* r, SrsLbRoundRobin* lb, int redirect_depth)
{
    srs_error_t err = srs_success;

    SrsRequest* req = r;

    if (redirect_depth == 0) {
        SrsConfDirective* conf = _srs_config->get_vhost_edge_origin(req->vhost);

        // when origin is error, for instance, server is shutdown,
        // then user remove the vhost then reload, the conf is empty.
        if (!conf) {
            return srs_error_new(ERROR_EDGE_VHOST_REMOVED, "vhost %s removed", req->vhost.c_str());
        }

        // select the origin.
        std::string server = lb->select(conf->args);
        int port = SRS_DEFAULT_HTTP_PORT;
        if (schema_ == "https") {
            port = SRS_DEFAULT_HTTPS_PORT;
        }
        srs_parse_hostport(server, server, port);

        // Remember the current selected server.
        selected_ip = server;
        selected_port = port;
    } else {
        // If HTTP redirect, use the server in location.
        schema_ = req->schema;
        selected_ip = req->host;
        selected_port = req->port;
    }

    srs_freep(sdk_);
    sdk_ = new SrsHttpClient();

    string path = "/" + req->app + "/" + req->stream;
    if (!srs_string_ends_with(req->stream, ".flv")) {
        path += ".flv";
    }
    if (!req->param.empty()) {
        path += req->param;
    }

    string url = schema_ + "://" + selected_ip + ":" + srs_int2str(selected_port);
    url += path;

    srs_utime_t cto = SRS_EDGE_INGESTER_TIMEOUT;
    if ((err = sdk_->initialize(schema_, selected_ip, selected_port, cto)) != srs_success) {
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
            srs_parse_rtmp_url(location, tcUrl, stream_name);

            int port;
            string schema, host, vhost, param;
            srs_discovery_tc_url(tcUrl, schema, host, vhost, app, stream_name, port, param);

            r->schema = schema; r->host = host; r->port = port;
            r->app = app; r->stream = stream_name; r->param = param;
        }
        return do_connect(r, lb, redirect_depth + 1);
    }

    srs_freep(reader_);
    reader_ = new SrsHttpFileReader(hr_->body_reader());

    srs_freep(decoder_);
    decoder_ = new SrsFlvDecoder();

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

srs_error_t SrsEdgeFlvUpstream::recv_message(SrsCommonMessage** pmsg)
{
    srs_error_t err = srs_success;

    char type;
    int32_t size;
    uint32_t time;
    if ((err = decoder_->read_tag_header(&type, &size, &time)) != srs_success) {
        return srs_error_wrap(err, "read tag header");
    }

    char* data = new char[size];
    if ((err = decoder_->read_tag_data(data, size)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "read tag data");
    }

    char pps[4];
    if ((err = decoder_->read_previous_tag_size(pps)) != srs_success) {
        return srs_error_wrap(err, "read pts");
    }

    int stream_id = 1;
    SrsCommonMessage* msg = NULL;
    if ((err = srs_rtmp_create_msg(type, time, data, size, stream_id, &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }

    *pmsg = msg;

    return err;
}

srs_error_t SrsEdgeFlvUpstream::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    srs_error_t err = srs_success;

    SrsPacket* packet = NULL;
    SrsBuffer stream(msg->payload, msg->size);
    SrsMessageHeader& header = msg->header;

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

void SrsEdgeFlvUpstream::selected(string& server, int& port)
{
    server = selected_ip;
    port = selected_port;
}

void SrsEdgeFlvUpstream::set_recv_timeout(srs_utime_t tm)
{
    sdk_->set_recv_timeout(tm);
}

void SrsEdgeFlvUpstream::kbps_sample(const char* label, srs_utime_t age)
{
    sdk_->kbps_sample(label, age);
}

SrsEdgeIngester::SrsEdgeIngester()
{
    source = NULL;
    edge = NULL;
    req = NULL;
#ifdef SRS_APM
    span_main_ = NULL;
#endif
    
    upstream = new SrsEdgeRtmpUpstream("");
    lb = new SrsLbRoundRobin();
    trd = new SrsDummyCoroutine();
}

SrsEdgeIngester::~SrsEdgeIngester()
{
    stop();

#ifdef SRS_APM
    srs_freep(span_main_);
#endif
    srs_freep(upstream);
    srs_freep(lb);
    srs_freep(trd);
}

srs_error_t SrsEdgeIngester::initialize(SrsLiveSource* s, SrsPlayEdge* e, SrsRequest* r)
{
    source = s;
    edge = e;
    req = r;

#ifdef SRS_APM
    // We create a dedicate span for edge ingester, and all players will link to this one.
    // Note that we use a producer span and end it immediately.
    srs_assert(!span_main_);
    span_main_ = _srs_apm->span("edge")->set_kind(SrsApmKindProducer)->end();
#endif

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

    // Notify source to un-publish if exists.
    if (source) {
        source->on_unpublish();
    }
}

string SrsEdgeIngester::get_curr_origin()
{
    return lb->selected();
}

#ifdef SRS_APM
ISrsApmSpan* SrsEdgeIngester::span()
{
    srs_assert(span_main_);
    return span_main_;
}
#endif

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_INGESTER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsEdgeIngester::cycle()
{
    srs_error_t err = srs_success;

#ifdef SRS_APM
    // Save span from parent coroutine to current coroutine context, so that we can load if in this coroutine, for
    // example to use it in SrsEdgeRtmpUpstream which use RTMP or FLV client to connect to upstream server.
    _srs_apm->store(span_main_);
    srs_assert(span_main_);
#endif
    
    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "edge ingester");
        }

#ifdef SRS_APM
        srs_assert(span_main_);
        ISrsApmSpan* start = _srs_apm->span("edge-start")->set_kind(SrsApmKindConsumer)->as_child(span_main_)->end();
        srs_freep(start);
#endif

        if ((err = do_cycle()) != srs_success) {
            srs_warn("EdgeIngester: Ignore error, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

#ifdef SRS_APM
        srs_assert(span_main_);
        ISrsApmSpan* stop = _srs_apm->span("edge-stop")->set_kind(SrsApmKindConsumer)->as_child(span_main_)->end();
        srs_freep(stop);
#endif

        // Check whether coroutine is stopped, see https://github.com/ossrs/srs/issues/2901
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

    std::string redirect;
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "do cycle pull");
        }

        // Use protocol in config.
        string edge_protocol = _srs_config->get_vhost_edge_protocol(req->vhost);

        // If follow client protocol, change to protocol of client.
        bool follow_client = _srs_config->get_vhost_edge_follow_client(req->vhost);
        if (follow_client && !req->protocol.empty()) {
            edge_protocol = req->protocol;
        }

        // Create object by protocol.
        srs_freep(upstream);
        if (edge_protocol == "flv" || edge_protocol == "flvs") {
            upstream = new SrsEdgeFlvUpstream(edge_protocol == "flv"? "http" : "https");
        } else {
            upstream = new SrsEdgeRtmpUpstream(redirect);
        }
        
        if ((err = source->on_source_id_changed(_srs_context->get_id())) != srs_success) {
            return srs_error_wrap(err, "on source id changed");
        }
        
        if ((err = upstream->connect(req, lb)) != srs_success) {
            return srs_error_wrap(err, "connect upstream");
        }
        
        if ((err = edge->on_ingest_play()) != srs_success) {
            return srs_error_wrap(err, "notify edge play");
        }

        // set to larger timeout to read av data from origin.
        upstream->set_recv_timeout(SRS_EDGE_INGESTER_TIMEOUT);
        
        err = ingest(redirect);
        
        // retry for rtmp 302 immediately.
        if (srs_error_code(err) == ERROR_CONTROL_REDIRECT) {
            int port;
            string server;
            upstream->selected(server, port);

            string url = req->get_stream_url();
            srs_warn("RTMP redirect %s from %s:%d to %s", url.c_str(), server.c_str(), port, redirect.c_str());

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

srs_error_t SrsEdgeIngester::ingest(string& redirect)
{
    srs_error_t err = srs_success;
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_edge();
    SrsAutoFree(SrsPithyPrint, pprint);

    // we only use the redict once.
    // reset the redirect to empty, for maybe the origin changed.
    redirect = "";
    
    while (true) {
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
        
        if ((err = process_publish_message(msg, redirect)) != srs_success) {
            return srs_error_wrap(err, "process message");
        }
    }
    
    return err;
}

srs_error_t SrsEdgeIngester::process_publish_message(SrsCommonMessage* msg, string& redirect)
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

SrsEdgeForwarder::SrsEdgeForwarder()
{
    edge = NULL;
    req = NULL;
    send_error_code = ERROR_SUCCESS;
    source = NULL;
    
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

void SrsEdgeForwarder::set_queue_size(srs_utime_t queue_size)
{
    return queue->set_queue_size(queue_size);
}

srs_error_t SrsEdgeForwarder::initialize(SrsLiveSource* s, SrsPublishEdge* e, SrsRequest* r)
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
        std::string vhost = _srs_config->get_vhost_edge_transform_vhost(req->vhost);
        vhost = srs_string_replace(vhost, "[vhost]", req->vhost);
        
        url = srs_generate_rtmp_url(server, port, req->host, vhost, req->app, req->stream, req->param);
    }
    
    // open socket.
    srs_freep(sdk);
    srs_utime_t cto = SRS_EDGE_FORWARDER_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_TIMEOUT;
    sdk = new SrsSimpleRtmpClient(url, cto, sto);

#ifdef SRS_APM
    // Create a client span and store it to an AMF0 propagator.
    // Note that we are able to load the span from coroutine context because in the same coroutine.
    ISrsApmSpan* span_client = _srs_apm->inject(_srs_apm->span("edge-push")->set_kind(SrsApmKindClient)->as_child(_srs_apm->load()), sdk->extra_args());
    SrsAutoFree(ISrsApmSpan, span_client);
#endif
    
    if ((err = sdk->connect()) != srs_success) {
        return srs_error_wrap(err, "sdk connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    // For RTMP client, we pass the vhost in tcUrl when connecting,
    // so we publish without vhost in stream.
    string stream;
    if ((err = sdk->publish(_srs_config->get_chunk_size(req->vhost), false, &stream)) != srs_success) {
        return srs_error_wrap(err, "sdk publish");
    }
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("edge-fwr", this, _srs_context->get_id());
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    srs_trace("edge-fwr publish url %s, stream=%s%s as %s", url.c_str(), req->stream.c_str(), req->param.c_str(), stream.c_str());
    
    return err;
}

void SrsEdgeForwarder::stop()
{
    trd->stop();
    queue->clear();
    srs_freep(sdk);
}

// when error, edge ingester sleep for a while and retry.
#define SRS_EDGE_FORWARDER_CIMS (3 * SRS_UTIME_SECONDS)

srs_error_t SrsEdgeForwarder::cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        // We always check status first.
        // @see https://github.com/ossrs/srs/issues/1634#issuecomment-597571561
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "thread pull");
        }

        if ((err = do_cycle()) != srs_success) {
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
    
    sdk->set_recv_timeout(SRS_CONSTS_RTMP_PULSE);
    
    SrsPithyPrint* pprint = SrsPithyPrint::create_edge();
    SrsAutoFree(SrsPithyPrint, pprint);
    
    SrsMessageArray msgs(SYS_MAX_EDGE_SEND_MSGS);
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "edge forward pull");
        }
        
        if (send_error_code != ERROR_SUCCESS) {
            srs_usleep(SRS_EDGE_FORWARDER_TIMEOUT);
            continue;
        }
        
        // read from client.
        if (true) {
            SrsCommonMessage* msg = NULL;
            err = sdk->recv_message(&msg);
            
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

srs_error_t SrsPlayEdge::initialize(SrsLiveSource* source, SrsRequest* req)
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
    } else if (state == SrsEdgeStateIngestStopping) {
        return srs_error_new(ERROR_RTMP_EDGE_PLAY_STATE, "state is stopping");
    }

#ifdef SRS_APM
    // APM bind client span to edge span, which fetch stream from upstream server.
    // We create a new span to link the two span, because these two spans might be ended.
    if (ingester->span() && _srs_apm->load()) {
        ISrsApmSpan* from = _srs_apm->span("play-link")->as_child(_srs_apm->load());
        ISrsApmSpan* to = _srs_apm->span("edge-link")->as_child(ingester->span())->link(from);
        srs_freep(from); srs_freep(to);
    }
#endif
    
    return err;
}

void SrsPlayEdge::on_all_client_stop()
{
    // when all client disconnected,
    // and edge is ingesting origin stream, abort it.
    if (state == SrsEdgeStatePlay || state == SrsEdgeStateIngestConnected) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStateIngestStopping;

        ingester->stop();

        state = SrsEdgeStateInit;
        srs_trace("edge change from %d to %d then %d (init).", pstate, SrsEdgeStateIngestStopping, state);
        
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

void SrsPublishEdge::set_queue_size(srs_utime_t queue_size)
{
    return forwarder->set_queue_size(queue_size);
}

srs_error_t SrsPublishEdge::initialize(SrsLiveSource* source, SrsRequest* req)
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
    
    // to avoid multiple publish the same stream on the same edge,
    // directly enter the publish stage.
    if (true) {
        SrsEdgeState pstate = state;
        state = SrsEdgeStatePublish;
        srs_trace("edge change from %d to state %d (push).", pstate, state);
    }
    
    // start to forward stream to origin.
    err = forwarder->start();
    
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

