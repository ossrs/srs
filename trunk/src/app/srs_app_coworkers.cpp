/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#include <srs_app_coworkers.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_protocol_json.hpp>
#include <srs_kernel_error.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_service_utility.hpp>
#include <srs_kernel_utility.hpp>

SrsCoWorkers* SrsCoWorkers::_instance = NULL;

SrsCoWorkers::SrsCoWorkers()
{
}

SrsCoWorkers::~SrsCoWorkers()
{
    map<string, SrsRequest*>::iterator it;
    for (it = streams.begin(); it != streams.end(); ++it) {
        SrsRequest* r = it->second;
        srs_freep(r);
    }
    streams.clear();
}

SrsCoWorkers* SrsCoWorkers::instance()
{
    if (!_instance) {
        _instance = new SrsCoWorkers();
    }
    return _instance;
}

SrsJsonAny* SrsCoWorkers::dumps(string vhost, string app, string stream)
{
    SrsRequest* r = find_stream_info(vhost, app, stream);
    if (!r) {
        // TODO: FIXME: Find stream from our origin util return to the start point.
        return SrsJsonAny::null();
    }
    
    vector<string> service_ports = _srs_config->get_listens();
    if (service_ports.empty()) {
        return SrsJsonAny::null();
    }
    
    string service_ip = srs_get_public_internet_address();
    string service_hostport = service_ports.at(0);
    
    int service_port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    if (service_hostport.find(":") != string::npos) {
        string service_host;
        srs_parse_hostport(service_hostport, service_host, service_port);
    } else {
        service_port = ::atoi(service_hostport.c_str());
    }
    
    string backend = _srs_config->get_http_api_listen();
    if (backend.find(":") == string::npos) {
        backend = service_ip + ":" + backend;
    }
    
    // The routers to detect loop and identify path.
    SrsJsonArray* routers = SrsJsonAny::array()->append(SrsJsonAny::str(backend.c_str()));
    
    return SrsJsonAny::object()
        ->set("ip", SrsJsonAny::str(service_ip.c_str()))
        ->set("port", SrsJsonAny::integer(service_port))
        ->set("vhost", SrsJsonAny::str(r->vhost.c_str()))
        ->set("api", SrsJsonAny::str(backend.c_str()))
        ->set("routers", routers);
}

SrsRequest* SrsCoWorkers::find_stream_info(string vhost, string app, string stream)
{
    // First, we should parse the vhost, if not exists, try default vhost instead.
    SrsConfDirective* conf = _srs_config->get_vhost(vhost, true);
    if (!conf) {
        return NULL;
    }
    
    // Get stream information from local cache.
    string url = srs_generate_stream_url(conf->arg0(), app, stream);
    map<string, SrsRequest*>::iterator it = streams.find(url);
    if (it == streams.end()) {
        return NULL;
    }
    
    return it->second;
}

srs_error_t SrsCoWorkers::on_publish(SrsSource* s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    string url = r->get_stream_url();
    
    // Delete the previous stream informations.
    map<string, SrsRequest*>::iterator it = streams.find(url);
    if (it != streams.end()) {
        srs_freep(it->second);
    }
    
    // Always use the latest one.
    streams[url] = r->copy();
    
    return err;
}

void SrsCoWorkers::on_unpublish(SrsSource* s, SrsRequest* r)
{
    string url = r->get_stream_url();
    
    map<string, SrsRequest*>::iterator it = streams.find(url);
    if (it != streams.end()) {
        srs_freep(it->second);
        streams.erase(it);
    }
}

