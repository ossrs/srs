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
#include <srs_protocol_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_utility.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

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
    if (!_srs_config->whether_query_latest_version()) {
        return srs_success;
    }

    server_id_ = srs_random_str(16);

    return trd_->start();
}

srs_error_t SrsLatestVersion::cycle()
{
    srs_error_t err = srs_success;

    srs_utime_t starttime = srs_update_system_time();
    if ((err = query_latest_version()) != srs_success) {
        srs_warn("query err %s", srs_error_desc(err).c_str());
        srs_freep(err); // Ignore any error.
    }

    srs_utime_t first_random_wait = 0;
    srs_random_generate((char*)&first_random_wait, 8);
    first_random_wait = srs_utime_t(uint64_t((first_random_wait + starttime + getpid())) % (60 * 60)) * SRS_UTIME_SECONDS; // in s.

    srs_trace("Startup query id=%s, eip=%s, match=%s, stable=%s, wait=%ds, cost=%dms", server_id_.c_str(), srs_get_public_internet_address().c_str(), match_version_.c_str(), stable_version_.c_str(), srsu2msi(first_random_wait)/1000, srsu2msi(srs_update_system_time() - starttime));
    srs_usleep(first_random_wait);

    while (true) {
        starttime = srs_update_system_time();
        if ((err = query_latest_version()) != srs_success) {
            srs_warn("query err %s", srs_error_desc(err).c_str());
            srs_freep(err); // Ignore any error.
        }

        srs_trace("Finish query id=%s, eip=%s, match=%s, stable=%s, cost=%dms", server_id_.c_str(), srs_get_public_internet_address().c_str(), match_version_.c_str(), stable_version_.c_str(), srsu2msi(srs_update_system_time() - starttime));
        srs_usleep(3600 * SRS_UTIME_SECONDS); // Every an hour.
    }

    return err;
}

srs_error_t SrsLatestVersion::query_latest_version()
{
    srs_error_t err = srs_success;

    // Generate uri and parse to object.
    stringstream ss;
    ss << "http://api.ossrs.net/service/v1/releases?"
          << "version=v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION
          << "&id=" << server_id_ << "&role=srs"
          << "&eip=" << srs_get_public_internet_address()
          << "&ts=" << srsu2ms(srs_get_system_time());
    string url = ss.str();

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

