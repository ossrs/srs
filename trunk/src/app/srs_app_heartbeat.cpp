/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 SRS(ossrs)
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

#include <srs_app_heartbeat.hpp>

#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_client.hpp>
#include <srs_protocol_json.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_protocol_amf0.hpp>

SrsHttpHeartbeat::SrsHttpHeartbeat()
{
}

SrsHttpHeartbeat::~SrsHttpHeartbeat()
{
}

void SrsHttpHeartbeat::heartbeat()
{
    int ret = ERROR_SUCCESS;
    
    std::string url = _srs_config->get_heartbeat_url();
    
    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http uri parse hartbeart url failed. url=%s, ret=%d", url.c_str(), ret);
        return;
    }
    
    std::string ip = "";
    std::string device_id = _srs_config->get_heartbeat_device_id();
    
    vector<string>& ips = srs_get_local_ipv4_ips();
    if (!ips.empty()) {
        ip = ips[_srs_config->get_stats_network() % (int)ips.size()];
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("device_id", SrsJsonAny::str(device_id.c_str()));
    obj->set("ip", SrsJsonAny::str(ip.c_str()));
    
    if (_srs_config->get_heartbeat_summaries()) {
        SrsJsonObject* summaries = SrsJsonAny::object();
        obj->set("summaries", summaries);
        
        srs_api_dump_summaries(summaries);
    }
    
    SrsHttpClient http;
    if ((ret = http.initialize(uri.get_host(), uri.get_port())) != ERROR_SUCCESS) {
        return;
    }
    
    std::string req = obj->dumps();
    ISrsHttpMessage* msg = NULL;
    if ((ret = http.post(uri.get_path(), req, &msg)) != ERROR_SUCCESS) {
        srs_info("http post hartbeart uri failed. url=%s, request=%s, ret=%d",
                 url.c_str(), req.c_str(), ret);
        return;
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    std::string res;
    if ((ret = msg->body_read_all(res)) != ERROR_SUCCESS) {
        return;
    }
    
    srs_info("http hook hartbeart success. url=%s, request=%s, response=%s, ret=%d",
             url.c_str(), req.c_str(), res.c_str(), ret);
    
    return;
}

