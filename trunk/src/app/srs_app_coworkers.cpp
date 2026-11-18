//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_coworkers.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

ISrsCoWorkers::ISrsCoWorkers()
{
}

ISrsCoWorkers::~ISrsCoWorkers()
{
}

SrsCoWorkers *_srs_coworkers = NULL;

SrsCoWorkers *SrsCoWorkers::instance_ = NULL;

SrsCoWorkers::SrsCoWorkers()
{
    config_ = _srs_config;
}

SrsCoWorkers::~SrsCoWorkers()
{
    map<string, ISrsRequest *>::iterator it;
    for (it = streams_.begin(); it != streams_.end(); ++it) {
        ISrsRequest *r = it->second;
        srs_freep(r);
    }
    streams_.clear();

    config_ = NULL;
}

SrsCoWorkers *SrsCoWorkers::instance()
{
    if (!instance_) {
        instance_ = new SrsCoWorkers();
    }
    return instance_;
}

SrsJsonAny *SrsCoWorkers::dumps(string vhost, string coworker, string app, string stream)
{
    ISrsRequest *r = find_stream_info(vhost, app, stream);
    if (!r) {
        // TODO: FIXME: Find stream from our origin until return to the start point.
        return SrsJsonAny::null();
    }

    string listen_host;
    int listen_port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    vector<string> listen_hostports = config_->get_listens();
    if (!listen_hostports.empty()) {
        string list_hostport = listen_hostports.at(0);

        if (list_hostport.find(":") != string::npos) {
            srs_net_split_hostport(list_hostport, listen_host, listen_port);
        } else {
            listen_port = ::atoi(list_hostport.c_str());
        }
    }

    // The server may be behind a NAT (192.x.x.x) while its own ip is a docker ip (172.x.x.x), so the
    // NAT address in the coworker host of the request is the exposed ip.
    // @see https://github.com/ossrs/srs/issues/1501
    string service_ip;
    if (listen_host != SRS_CONSTS_LOCALHOST && listen_host != SRS_CONSTS_LOOPBACK && listen_host != SRS_CONSTS_LOOPBACK6) {
        service_ip = listen_host;
    }
    if (service_ip.empty()) {
        int coworker_port;
        string coworker_host = coworker;
        if (coworker.find(":") != string::npos) {
            srs_net_split_hostport(coworker, coworker_host, coworker_port);
        }

        service_ip = coworker_host;
    }
    if (service_ip.empty()) {
        SrsProtocolUtility utility;
        service_ip = utility.public_internet_address();
    }

    string backend = config_->get_http_api_listens().at(0);
    if (backend.find(":") == string::npos) {
        backend = service_ip + ":" + backend;
    }

    // For the callers to detect a loop and identify the path.
    SrsJsonArray *routers = SrsJsonAny::array()->append(SrsJsonAny::str(backend.c_str()));

    srs_trace("Redirect vhost=%s, path=%s/%s to ip=%s, port=%d, api=%s",
              vhost.c_str(), app.c_str(), stream.c_str(), service_ip.c_str(), listen_port, backend.c_str());

    return SrsJsonAny::object()
        ->set("ip", SrsJsonAny::str(service_ip.c_str()))
        ->set("port", SrsJsonAny::integer(listen_port))
        ->set("vhost", SrsJsonAny::str(r->vhost_.c_str()))
        ->set("api", SrsJsonAny::str(backend.c_str()))
        ->set("routers", routers);
}

ISrsRequest *SrsCoWorkers::find_stream_info(string vhost, string app, string stream)
{
    SrsConfDirective *conf = config_->get_vhost(vhost, true);
    if (!conf) {
        return NULL;
    }

    string url = srs_net_url_encode_sid(conf->arg0(), app, stream);
    map<string, ISrsRequest *>::iterator it = streams_.find(url);
    if (it == streams_.end()) {
        return NULL;
    }

    return it->second;
}

srs_error_t SrsCoWorkers::on_publish(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    string url = r->get_stream_url();

    map<string, ISrsRequest *>::iterator it = streams_.find(url);
    if (it != streams_.end()) {
        srs_freep(it->second);
    }

    streams_[url] = r->copy();

    return err;
}

void SrsCoWorkers::on_unpublish(ISrsRequest *r)
{
    string url = r->get_stream_url();

    map<string, ISrsRequest *>::iterator it = streams_.find(url);
    if (it != streams_.end()) {
        srs_freep(it->second);
        streams_.erase(it);
    }
}
