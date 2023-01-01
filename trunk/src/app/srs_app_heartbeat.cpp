//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

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
    srs_error_t err = do_heartbeat();
    if (err != srs_success) {
        srs_warn("heartbeat err=%s", srs_error_desc(err).c_str());
    }
    srs_freep(err);
    return;
}

srs_error_t SrsHttpHeartbeat::do_heartbeat()
{
    srs_error_t err = srs_success;
    
    std::string url = _srs_config->get_heartbeat_url();
    
    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http uri parse hartbeart url failed. url=%s", url.c_str());
    }
    
    SrsIPAddress* ip = NULL;
    std::string device_id = _srs_config->get_heartbeat_device_id();
    
    vector<SrsIPAddress*>& ips = srs_get_local_ips();
    if (!ips.empty()) {
        ip = ips[_srs_config->get_stats_network() % (int)ips.size()];
    }
    
    SrsJsonObject* obj = SrsJsonAny::object();
    SrsAutoFree(SrsJsonObject, obj);
    
    obj->set("device_id", SrsJsonAny::str(device_id.c_str()));
    obj->set("ip", SrsJsonAny::str(ip->ip.c_str()));
    
    if (_srs_config->get_heartbeat_summaries()) {
        SrsJsonObject* summaries = SrsJsonAny::object();
        obj->set("summaries", summaries);
        
        srs_api_dump_summaries(summaries);
    }
    
    SrsHttpClient http;
    if ((err = http.initialize(uri.get_schema(), uri.get_host(), uri.get_port())) != srs_success) {
        return srs_error_wrap(err, "init uri=%s", uri.get_url().c_str());
    }
    
    std::string req = obj->dumps();
    ISrsHttpMessage* msg = NULL;
    if ((err = http.post(uri.get_path(), req, &msg)) != srs_success) {
        return srs_error_wrap(err, "http post hartbeart uri failed. url=%s, request=%s", url.c_str(), req.c_str());
    }
    SrsAutoFree(ISrsHttpMessage, msg);
    
    std::string res;
    if ((err = msg->body_read_all(res)) != srs_success) {
        return srs_error_wrap(err, "read body");
    }
    
    return err;
}

