//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_latest_version.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_uuid.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_tencentcloud.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

// Whether we are in docker, defined in main module.
extern bool _srs_in_docker;

// Whether setup config by environment variables.
extern bool _srs_config_by_env;

// Check the feature by cond
#define SRS_CHECK_FEATURE(cond, ss) if (cond) ss << "&" << #cond << "=1"
#define SRS_CHECK_FEATURE2(cond, key, ss) if (cond) ss << "&" << key << "=1"
#define SRS_CHECK_FEATURE3(cond, key, value, ss) if (cond) ss << "&" << key << "=" << value

// @see https://github.com/ossrs/srs/issues/2424
// @see https://github.com/ossrs/srs/issues/2508
void srs_build_features(stringstream& ss)
{
    if (SRS_OSX_BOOL) {
        ss << "&os=mac";
    } else if (SRS_CYGWIN64_BOOL) {
        ss << "&os=windows";
    } else {
        ss << "&os=linux";
    }

#if defined(__amd64__) || defined(__x86_64__) || defined(__i386__)
    ss << "&x86=1";
#elif defined(__arm__) || defined(__aarch64__)
    ss << "&arm=1";
#elif defined(__riscv)
    ss << "&riscv=1";
#elif defined(__mips__)
    ss << "&mips=1";
#elif defined(__loongarch__)
    ss << "&loong=1";
#endif

    SRS_CHECK_FEATURE2(_srs_in_docker, "docker", ss);
    SRS_CHECK_FEATURE3(!string(SRS_PACKAGER).empty(), "packager", SRS_PACKAGER, ss);
    SRS_CHECK_FEATURE2(SRS_CROSSBUILD_BOOL, "cross", ss);

    SRS_CHECK_FEATURE2(SRS_RTC_BOOL && _srs_config->get_rtc_server_enabled(), "rtc", ss);
    SRS_CHECK_FEATURE2(SRS_SRT_BOOL && _srs_config->get_srt_enabled(), "srt", ss);
    SRS_CHECK_FEATURE2(_srs_config->get_http_api_enabled(), "api", ss);
    SRS_CHECK_FEATURE2(_srs_config->get_https_api_enabled(), "https", ss);
    SRS_CHECK_FEATURE2(_srs_config->get_raw_api(), "raw", ss);
    SRS_CHECK_FEATURE2(_srs_config->get_exporter_enabled(), "prom", ss);

    string platform = srs_getenv("SRS_PLATFORM");
    SRS_CHECK_FEATURE3(!string(platform).empty(), "plat", platform, ss);
    string region = srs_getenv("SRS_REGION");
    SRS_CHECK_FEATURE3(!string(region).empty(), "region", region, ss);
    string source = srs_getenv("SRS_SOURCE");
    SRS_CHECK_FEATURE3(!string(source).empty(), "source", source, ss);

    int nn_vhosts = 0;
    bool gb28181 = false, forward = false, ingest = false, edge = false, hls = false, dvr = false, flv = false;
    bool hooks = false, dash = false, hds = false, exec = false, transcode = false, security = false;
    bool flv2 = false, oc = false;

    SrsConfDirective* root = _srs_config->get_root();
    // Note that we limit the loop, never detect all configs.
    for (int i = 0; i < (int)root->directives.size() && i < 128; i++) {
        SrsConfDirective* conf = root->at(i);

        if (!gb28181 && conf->is_stream_caster() && _srs_config->get_stream_caster_enabled(conf)) {
            string engine = _srs_config->get_stream_caster_engine(conf);
            if (engine == "gb28181") {
                gb28181 = true;
            } else if (engine == "flv") {
                flv2 = true;
            }
        }

        if (conf->is_vhost() && _srs_config->get_vhost_enabled(conf)) {
            nn_vhosts++;

            if (!forward && _srs_config->get_forward_enabled(conf)) {
                forward = true;
            }
            if (!edge && _srs_config->get_vhost_is_edge(conf)) {
                edge = true;
            }
            if (!oc && _srs_config->get_vhost_origin_cluster(conf)) {
                oc = true;
            }
            if (!hls && _srs_config->get_hls_enabled(conf)) {
                hls = true;
            }
            if (!dvr && _srs_config->get_dvr_enabled(conf)) {
                dvr = true;
            }
            if (!flv && _srs_config->get_vhost_http_remux_enabled(conf)) {
                flv = true;
            }
            if (!hooks && _srs_config->get_vhost_http_hooks_enabled(conf)) {
                hooks = true;
            }
            if (!dash && _srs_config->get_dash_enabled(conf)) {
                dash = true;
            }
            if (!hds && _srs_config->get_hds_enabled(conf)) {
                hds = true;
            }
            if (!exec && _srs_config->get_exec_enabled(conf)) {
                exec = true;
            }
            if (!security && _srs_config->get_security_enabled(conf)) {
                security = true;
            }

            for (int j = 0; j < (int)conf->directives.size() && j < 64; j++) {
                SrsConfDirective* prop = conf->directives.at(j);

                if (!ingest && prop->name == "ingest" && _srs_config->get_ingest_enabled(prop)) {
                    ingest = true;
                }
                if (!transcode && prop->name == "transcode" && _srs_config->get_transcode_enabled(prop)) {
                    transcode = true;
                }

                if (ingest && transcode) {
                    break;
                }
            }
        }
    }

    SRS_CHECK_FEATURE2(nn_vhosts, "vhosts", ss);
    SRS_CHECK_FEATURE(gb28181, ss);
    SRS_CHECK_FEATURE(flv2, ss);
    SRS_CHECK_FEATURE(forward, ss);
    SRS_CHECK_FEATURE(ingest, ss);
    SRS_CHECK_FEATURE(edge, ss);
    SRS_CHECK_FEATURE(oc, ss);
    SRS_CHECK_FEATURE(hls, ss);
    SRS_CHECK_FEATURE(dvr, ss);
    SRS_CHECK_FEATURE(flv, ss);
    SRS_CHECK_FEATURE(hooks, ss);
    SRS_CHECK_FEATURE(dash, ss);
    SRS_CHECK_FEATURE(hds, ss);
    SRS_CHECK_FEATURE(exec, ss);
    SRS_CHECK_FEATURE(transcode, ss);
    SRS_CHECK_FEATURE(security, ss);
    SRS_CHECK_FEATURE2(_srs_config_by_env, "env", ss);

#ifdef SRS_APM
    SRS_CHECK_FEATURE2(_srs_cls->enabled(), "cls", ss);
    SRS_CHECK_FEATURE3(_srs_cls->nn_logs(), "logs", _srs_cls->nn_logs(), ss);

    SRS_CHECK_FEATURE2(_srs_apm->enabled(), "apm", ss);
    SRS_CHECK_FEATURE3(_srs_apm->nn_spans(), "spans", _srs_apm->nn_spans(), ss);
#endif
}

SrsLatestVersion::SrsLatestVersion()
{
    trd_ = new SrsSTCoroutine("signal", this);
}

SrsLatestVersion::~SrsLatestVersion()
{
    srs_freep(trd_);
}

srs_error_t SrsLatestVersion::start()
{
    // @see https://github.com/ossrs/srs/issues/2424
    // @see https://github.com/ossrs/srs/issues/2508
    if (!_srs_config->whether_query_latest_version()) {
        return srs_success;
    }

    server_id_ = SrsStatistic::instance()->server_id();
    session_id_ = srs_generate_stat_vid();

    return trd_->start();
}

srs_error_t SrsLatestVersion::cycle()
{
    srs_error_t err = srs_success;

    if (true) {
        srs_utime_t first_wait_for_qlv = _srs_config->first_wait_for_qlv();
        string pip = srs_get_public_internet_address();
        srs_trace("Startup query id=%s, session=%s, eip=%s, wait=%ds", server_id_.c_str(), session_id_.c_str(), pip.c_str(), srsu2msi(first_wait_for_qlv) / 1000);
        srs_usleep(first_wait_for_qlv);
    }

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            break;
        }

        string url;
        srs_utime_t starttime = srs_update_system_time();
        if ((err = query_latest_version(url)) != srs_success) {
            srs_trace("query release err %s", srs_error_summary(err).c_str());
            srs_freep(err); // Ignore any error.
        }

        srs_trace("Finish query id=%s, session=%s, eip=%s, match=%s, stable=%s, cost=%dms, url=%s",
            server_id_.c_str(), session_id_.c_str(), srs_get_public_internet_address().c_str(), match_version_.c_str(),
            stable_version_.c_str(), srsu2msi(srs_update_system_time() - starttime), url.c_str());

        srs_usleep(3600 * SRS_UTIME_SECONDS); // Every an hour.
    }

    return err;
}

srs_error_t SrsLatestVersion::query_latest_version(string& url)
{
    srs_error_t err = srs_success;

    // Generate uri and parse to object.
    stringstream ss;
    ss << "http://api.ossrs.net/service/v1/releases?"
          << "version=v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION
          << "&id=" << server_id_ << "&session=" << session_id_ << "&role=srs"
          << "&eip=" << srs_get_public_internet_address()
          << "&ts=" << srs_get_system_time()
          << "&alive=" << srsu2ms(srs_get_system_time() - srs_get_system_startup_time()) / 1000;
    srs_build_features(ss);
    SrsStatistic::instance()->dumps_hints_kv(ss);
    url = ss.str();

    SrsHttpUri uri;
    if ((err = uri.initialize(url)) != srs_success) {
        return srs_error_wrap(err, "http: post failed. url=%s", url.c_str());
    }

    // Start HTTP request and read response.
    SrsHttpClient http;
    if ((err = http.initialize(uri.get_schema(), uri.get_host(), uri.get_port())) != srs_success) {
        return err;
    }

    // Path with query.
    string path = uri.get_path();
    path += "?";
    path += uri.get_query();

    ISrsHttpMessage* msg = NULL;
    if ((err = http.get(path, "", &msg)) != srs_success) {
        return err;
    }
    SrsAutoFree(ISrsHttpMessage, msg);

    string res;
    int code = msg->status_code();
    if ((err = msg->body_read_all(res)) != srs_success) {
        return err;
    }

    // Check the response code and content.
    if (code != SRS_CONSTS_HTTP_OK) {
        return srs_error_new(ERROR_HTTP_STATUS_INVALID, "invalid response status=%d", code);
    }

    if (res.empty()) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "invalid empty response");
    }

    // Response in json object.
    SrsJsonAny* jres = SrsJsonAny::loads((char*)res.c_str());
    if (!jres || !jres->is_object()) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "invalid response %s", res.c_str());
    }
    SrsAutoFree(SrsJsonAny, jres);

    SrsJsonObject* obj = jres->to_object();
    SrsJsonAny* prop = NULL;

    // Parse fields of response.
    if ((prop = obj->ensure_property_string("match_version")) == NULL) {
        return srs_error_new(ERROR_RESPONSE_CODE, "no match_version");
    }
    match_version_ = prop->to_str();

    if ((prop = obj->ensure_property_string("stable_version")) == NULL) {
        return srs_error_new(ERROR_RESPONSE_CODE, "no stable_version");
    }
    stable_version_ = prop->to_str();

    return err;
}

