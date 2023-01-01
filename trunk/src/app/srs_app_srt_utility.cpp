//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_srt_utility.hpp>

using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>

// See streamid of https://github.com/ossrs/srs/issues/2893
// TODO: FIMXE: We should parse SRT streamid to URL object, rather than a HTTP url subpath.
bool srs_srt_streamid_info(const std::string& streamid, SrtMode& mode, std::string& vhost, std::string& url_subpath)
{
    mode = SrtModePull;

    size_t pos = streamid.find("#!::");
    if (pos != 0) {
        pos = streamid.find("/");
        if (pos == streamid.npos) {
            url_subpath = _srs_config->get_default_app_name() + "/" + streamid;
            return true;
        }
        url_subpath = streamid;
        return true;
    }

    //SRT url supports multiple QueryStrings, which are passed to RTMP to realize authentication and other capabilities
    //@see https://github.com/ossrs/srs/issues/2893
    std::string params;
    std::string real_streamid;
    real_streamid = streamid.substr(4);

    // Compatible with previous auth querystring, like this one:
    //      srt://127.0.0.1:10080?streamid=#!::h=live/livestream?secret=xxx,m=publish
    real_streamid = srs_string_replace(real_streamid, "?", ",");

    std::map<std::string, std::string> query;
    srs_parse_query_string(real_streamid, query);
    for (std::map<std::string, std::string>::iterator it = query.begin(); it != query.end(); ++it) {
        if (it->first == "h") {
            std::string host = it->second;

            size_t r0 = host.find("/");
            size_t r1 = host.rfind("/");
            if (r0 != std::string::npos && r0 != std::string::npos) {
                // Compatible with previous style, see https://github.com/ossrs/srs/issues/2893#compatible
                //      srt://127.0.0.1:10080?streamid=#!::h=live/livestream,m=publish
                //      srt://127.0.0.1:10080?streamid=#!::h=live/livestream,m=request
                //      srt://127.0.0.1:10080?streamid=#!::h=srs.srt.com.cn/live/livestream,m=publish
                if (r0 != r1) {
                    // We got vhost in host.
                    url_subpath = host.substr(r0 + 1);
                    host = host.substr(0, r0);

                    params.append("vhost=");
                    params.append(host);
                    params.append("&");
                    vhost = host;
                } else {
                    // Only stream in host.
                    url_subpath = host;
                }
            } else {
                // New URL style, see https://github.com/ossrs/srs/issues/2893#solution
                //      srt://host.com:10080?streamid=#!::h=host.com,r=app/stream,key1=value1,key2=value2
                //      srt://1.2.3.4:10080?streamid=#!::h=host.com,r=app/stream,key1=value1,key2=value2
                //      srt://1.2.3.4:10080?streamid=#!::r=app/stream,key1=value1,key2=value2
                params.append("vhost=");
                params.append(host);
                params.append("&");
                vhost = host;
            }
        } else if (it->first == "r") {
            url_subpath = it->second;
        } else if (it->first == "m") {
            std::string mode_str = it->second; // support m=publish or m=request
            std::transform(it->second.begin(), it->second.end(), mode_str.begin(), ::tolower);
            if (mode_str == "publish") {
                mode = SrtModePush;
            }  else if (mode_str == "request") {
                mode = SrtModePull;
            }  else {
                srs_warn("unknown mode_str:%s", mode_str.c_str());
                return false;
            }
        } else {
            params.append(it->first);
            params.append("=");
            params.append(it->second);
            params.append("&");
        }
    }

    if (url_subpath.empty()) {
        return false;
    }

    if (!params.empty()) {
        url_subpath.append("?");
        url_subpath.append(params);
        url_subpath = url_subpath.substr(0, url_subpath.length() - 1); // remove last '&'
    }

    return true;
}

bool srs_srt_streamid_to_request(const std::string& streamid, SrtMode& mode, SrsRequest* request)
{
    string url_subpath = "";
    bool ret = srs_srt_streamid_info(streamid, mode, request->vhost, url_subpath);
    if (! ret) {
        return ret;
    }

    size_t pos = url_subpath.find("/");
    string stream_with_params = "";
    if (pos == string::npos) {
        request->app = _srs_config->get_default_app_name();
        stream_with_params = url_subpath;
    } else {
        request->app = url_subpath.substr(0, pos);
        stream_with_params = url_subpath.substr(pos + 1);
    }

    pos = stream_with_params.find("?");
    if (pos == string::npos) {
        request->stream = stream_with_params;
    } else {
        request->stream = stream_with_params.substr(0, pos);
        request->param = stream_with_params.substr(pos + 1);
    }

    request->host = srs_get_public_internet_address();
    if (request->vhost.empty()) request->vhost = request->host;
    request->tcUrl = srs_generate_tc_url("srt", request->host, request->vhost, request->app, request->port);

    return ret;
}
