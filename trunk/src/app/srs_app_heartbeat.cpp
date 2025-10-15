//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_heartbeat.hpp>

#include <sstream>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>

SrsHttpHeartbeat::SrsHttpHeartbeat()
{
    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsHttpHeartbeat::~SrsHttpHeartbeat()
{
    config_ = NULL;
    app_factory_ = NULL;
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

    std::string url = config_->get_heartbeat_url();

    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http uri parse hartbeart url failed. url=%s", url.c_str());
    }

    string ip;
    std::string device_id = config_->get_heartbeat_device_id();

    // Try to load the ip from the environment variable.
    ip = srs_getenv("srs.device.ip"); // SRS_DEVICE_IP
    if (ip.empty()) {
        // Use the local ip address specified by the stats.network config.
        SrsProtocolUtility utility;
        vector<SrsIPAddress *> &ips = utility.local_ips();
        if (!ips.empty()) {
            ip = ips[config_->get_stats_network() % (int)ips.size()]->ip_;
        }
    }

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());

    obj->set("device_id", SrsJsonAny::str(device_id.c_str()));
    obj->set("ip", SrsJsonAny::str(ip.c_str()));

    SrsStatistic *stat = _srs_stat;
    obj->set("server", SrsJsonAny::str(stat->server_id().c_str()));
    obj->set("service", SrsJsonAny::str(stat->service_id().c_str()));
    obj->set("pid", SrsJsonAny::str(stat->service_pid().c_str()));

    if (config_->get_heartbeat_summaries()) {
        SrsJsonObject *summaries = SrsJsonAny::object();
        obj->set("summaries", summaries);

        srs_api_dump_summaries(summaries);
    }

    if (config_->get_heartbeat_ports()) {
        // For RTMP listen endpoints.
        if (true) {
            SrsJsonArray *o = SrsJsonAny::array();
            obj->set("rtmp", o);

            vector<string> endpoints = config_->get_listens();
            for (int i = 0; i < (int)endpoints.size(); i++) {
                o->append(SrsJsonAny::str(endpoints.at(i).c_str()));
            }
        }

        // For HTTP Stream listen endpoints.
        if (config_->get_http_stream_enabled()) {
            SrsJsonArray *o = SrsJsonAny::array();
            obj->set("http", o);

            vector<string> endpoints = config_->get_http_stream_listens();
            for (int i = 0; i < (int)endpoints.size(); i++) {
                o->append(SrsJsonAny::str(endpoints.at(i).c_str()));
            }
        }

        // For HTTP API listen endpoints.
        if (config_->get_http_api_enabled()) {
            SrsJsonArray *o = SrsJsonAny::array();
            obj->set("api", o);

            vector<string> endpoints = config_->get_http_api_listens();
            for (int i = 0; i < (int)endpoints.size(); i++) {
                o->append(SrsJsonAny::str(endpoints.at(i).c_str()));
            }
        }

        // For SRT listen endpoints.
        if (config_->get_srt_enabled()) {
            SrsJsonArray *o = SrsJsonAny::array();
            obj->set("srt", o);

            vector<string> endpoints = config_->get_srt_listens();
            for (int i = 0; i < (int)endpoints.size(); i++) {
                o->append(SrsJsonAny::str(endpoints.at(i).c_str()));
            }
        }

        // For RTSP listen endpoints.
        if (config_->get_rtsp_server_enabled()) {
            SrsJsonArray *o = SrsJsonAny::array();
            obj->set("rtsp", o);

            vector<string> endpoints = config_->get_rtsp_server_listens();
            for (int i = 0; i < (int)endpoints.size(); i++) {
                o->append(SrsJsonAny::str(endpoints.at(i).c_str()));
            }
        }

        // For WebRTC listen endpoints.
        if (config_->get_rtc_server_enabled()) {
            SrsJsonArray *o = SrsJsonAny::array();
            obj->set("rtc", o);

            vector<string> endpoints = config_->get_rtc_server_listens();
            for (int i = 0; i < (int)endpoints.size(); i++) {
                string endpoint = srs_fmt_sprintf("udp://%s", endpoints.at(i).c_str());
                o->append(SrsJsonAny::str(endpoint.c_str()));
            }

            if (config_->get_rtc_server_tcp_enabled()) {
                vector<string> endpoints = config_->get_rtc_server_tcp_listens();
                for (int i = 0; i < (int)endpoints.size(); i++) {
                    string endpoint = srs_fmt_sprintf("tcp://%s", endpoints.at(i).c_str());
                    o->append(SrsJsonAny::str(endpoint.c_str()));
                }
            }
        }
    }

    SrsUniquePtr<ISrsHttpClient> http(app_factory_->create_http_client());
    if ((err = http->initialize(uri.get_schema(), uri.get_host(), uri.get_port())) != srs_success) {
        return srs_error_wrap(err, "init uri=%s", uri.get_url().c_str());
    }

    std::string req = obj->dumps();
    ISrsHttpMessage *msg_raw = NULL;
    if ((err = http->post(uri.get_path(), req, &msg_raw)) != srs_success) {
        return srs_error_wrap(err, "http post hartbeart uri failed. url=%s, request=%s", url.c_str(), req.c_str());
    }

    SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

    std::string res;
    if ((err = msg->body_read_all(res)) != srs_success) {
        return srs_error_wrap(err, "read body");
    }

    return err;
}
