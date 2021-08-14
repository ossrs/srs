/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_latest_version.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_rtmp_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_uuid.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

SrsLatestVersion::SrsLatestVersion()
{
    trd_ = new SrsEndlessThread("signal", this);
}

SrsLatestVersion::~SrsLatestVersion()
{
    srs_freep(trd_);
}

int SrsLatestVersion::start()
{
    if (!_srs_config->whether_query_latest_version()) {
        return ERROR_SUCCESS;
    }

    if (true) {
        uuid_t uuid;
        uuid_generate_time(uuid);

        char buf[32];
        for (int i = 0; i < 16; i++) {
            snprintf(buf + i * 2, sizeof(buf), "%02x", uuid[i]);
        }
        server_id_ = string(buf, sizeof(buf));
    }

    return trd_->start();
}

int SrsLatestVersion::cycle()
{
    int ret = ERROR_SUCCESS;

    uint64_t first_random_wait = 0;
    srs_random_generate((char*)&first_random_wait, 8);
    first_random_wait = (first_random_wait + srs_update_system_time_ms() + getpid()) % (5 * 60); // in s.

    // Only report after 5+ minutes.
    first_random_wait += 5 * 60;

    srs_trace("Startup query id=%s, eip=%s, wait=%ds", server_id_.c_str(), srs_get_public_internet_address().c_str(), (int)first_random_wait);
    st_usleep(first_random_wait * 1000 * 1000);

    while (true) {
        int64_t starttime = srs_update_system_time_ms();
        ret = query_latest_version(); // Ignore any error.
        srs_trace("Finish query id=%s, eip=%s, match=%s, stable=%s, cost=%dms, ret=%d", server_id_.c_str(), srs_get_public_internet_address().c_str(), match_version_.c_str(), stable_version_.c_str(), (int)(srs_update_system_time_ms() - starttime), ret);

        st_usleep(3600 * 1000 * 1000LL); // Every an hour.
    }

    return ret;
}

int SrsLatestVersion::query_latest_version()
{
    int ret = ERROR_SUCCESS;

    // Generate uri and parse to object.
    stringstream ss;
    ss << "http://api.ossrs.net/service/v1/releases?"
          << "version=v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION
          << "&id=" << server_id_
          << "&eip=" << srs_get_public_internet_address()
          << "&ts=" << srs_get_system_time_ms()
          << "&alive=" << (srs_get_system_time_ms() - srs_get_system_startup_time_ms()) / 1000;
    string url = ss.str();

    SrsHttpUri uri;
    if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
        srs_error("http: post failed. url=%s, ret=%d", url.c_str(), ret);
        return ret;
    }

    // Start HTTP request and read response.
    SrsHttpClient http;
    if ((ret = http.initialize(uri.get_host(), uri.get_port())) != ERROR_SUCCESS) {
        return ret;
    }

    // Path with query.
    string path = uri.get_path();
    path += "?";
    path += uri.get_query();

    ISrsHttpMessage* msg = NULL;
    if ((ret = http.get(path, "", &msg)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(ISrsHttpMessage, msg);

    string res;
    int code = msg->status_code();
    if ((ret = msg->body_read_all(res)) != ERROR_SUCCESS) {
        return ret;
    }

    // Check the response code and content.
    if (code != SRS_CONSTS_HTTP_OK) {
        ret = ERROR_HTTP_STATUS_INVALID;
        srs_error("invalid response status=%d. ret=%d", code, ret);
        return ret;
    }

    if (res.empty()) {
        ret = ERROR_HTTP_DATA_INVALID;
        srs_error("invalid empty response. ret=%d", ret);
        return ret;
    }

    // Response in json object.
    SrsJsonAny* jres = SrsJsonAny::loads((char*)res.c_str());
    if (!jres || !jres->is_object()) {
        ret = ERROR_HTTP_DATA_INVALID;
        srs_error("invalid response %s. ret=%d", res.c_str(), ret);
        return ret;
    }
    SrsAutoFree(SrsJsonAny, jres);

    SrsJsonObject* obj = jres->to_object();
    SrsJsonAny* prop = NULL;

    // Parse fields of response.
    if ((prop = obj->ensure_property_string("match_version")) == NULL) {
        ret = ERROR_RESPONSE_CODE;
        srs_error("invalid response without match_version, ret=%d", ret);
        return ret;
    }
    match_version_ = prop->to_str();

    if ((prop = obj->ensure_property_string("stable_version")) == NULL) {
        ret = ERROR_RESPONSE_CODE;
        srs_error("invalid response without stable_version, ret=%d", ret);
        return ret;
    }
    stable_version_ = prop->to_str();

    return ret;
}

