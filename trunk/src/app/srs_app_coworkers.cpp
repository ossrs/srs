/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

using namespace std;

#include <srs_protocol_json.hpp>
#include <srs_kernel_error.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_service_utility.hpp>

SrsCoWorkers* SrsCoWorkers::_instance = NULL;

SrsCoWorkers::SrsCoWorkers()
{
}

SrsCoWorkers::~SrsCoWorkers()
{
    map<string, SrsRequest*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); ++it) {
        SrsRequest* r = it->second;
        srs_freep(r);
    }
    vhosts.clear();
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
    
    vector<string>& ips = srs_get_local_ips();
    if (ips.empty()) {
        return SrsJsonAny::null();
    }
    
    SrsJsonArray* arr = SrsJsonAny::array();
    for (int i = 0; i < (int)ips.size(); i++) {
        arr->append(SrsJsonAny::object()
                    ->set("ip", SrsJsonAny::str(ips.at(i).c_str()))
                    ->set("vhost", SrsJsonAny::str(r->vhost.c_str()))
                    ->set("self", SrsJsonAny::boolean(true)));
    }
    
    return arr;
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
    map<string, SrsRequest*>::iterator it = vhosts.find(url);
    if (it == vhosts.end()) {
        return NULL;
    }
    
    return it->second;
}

srs_error_t SrsCoWorkers::on_publish(SrsSource* s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    
    string url = r->get_stream_url();
    
    // Delete the previous stream informations.
    map<string, SrsRequest*>::iterator it = vhosts.find(url);
    if (it != vhosts.end()) {
        srs_freep(it->second);
    }
    
    // Always use the latest one.
    vhosts[url] = r->copy();
    
    return err;
}

void SrsCoWorkers::on_unpublish(SrsSource* s, SrsRequest* r)
{
    string url = r->get_stream_url();
    
    map<string, SrsRequest*>::iterator it = vhosts.find(url);
    if (it != vhosts.end()) {
        srs_freep(it->second);
        vhosts.erase(it);
    }
}

