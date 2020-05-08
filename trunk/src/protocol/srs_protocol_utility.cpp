/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#include <srs_protocol_utility.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <stdlib.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_protocol_io.hpp>

/**
 * resolve the vhost in query string
 * @pram vhost, update the vhost if query contains the vhost.
 * @param app, may contains the vhost in query string format:
 *   app?vhost=request_vhost
 *   app...vhost...request_vhost
 * @param param, the query, for example, ?vhost=xxx
 */
void srs_vhost_resolve(string& vhost, string& app, string& param)
{
    // get original param
    size_t pos = 0;
    if ((pos = app.find("?")) != std::string::npos) {
        param = app.substr(pos);
    }
    
    // filter tcUrl
    app = srs_string_replace(app, ",", "?");
    app = srs_string_replace(app, "...", "?");
    app = srs_string_replace(app, "&&", "?");
    app = srs_string_replace(app, "&", "?");
    app = srs_string_replace(app, "=", "?");
    
    if (srs_string_ends_with(app, "/_definst_")) {
        app = srs_erase_last_substr(app, "/_definst_");
    }
    
    if ((pos = app.find("?")) != std::string::npos) {
        std::string query = app.substr(pos + 1);
        app = app.substr(0, pos);
        
        if ((pos = query.find("vhost?")) != std::string::npos) {
            query = query.substr(pos + 6);
            if (!query.empty()) {
                vhost = query;
            }
        }
    }

    // vhost with params.
    if ((pos = vhost.find("?")) != std::string::npos) {
        vhost = vhost.substr(0, pos);
    }
    
    /* others */
}

void srs_discovery_tc_url(string tcUrl, string& schema, string& host, string& vhost, string& app, string& stream, int& port, string& param)
{
    size_t pos = std::string::npos;
    std::string url = tcUrl;
    
    if ((pos = url.find("://")) != std::string::npos) {
        schema = url.substr(0, pos);
        url = url.substr(schema.length() + 3);
        srs_info("discovery schema=%s", schema.c_str());
    }
    
    if ((pos = url.find("/")) != std::string::npos) {
        host = url.substr(0, pos);
        url = url.substr(host.length() + 1);
        srs_info("discovery host=%s", host.c_str());
    }
    
    port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    if ((pos = host.find(":")) != std::string::npos) {
        srs_parse_hostport(host, host, port);
        srs_info("discovery host=%s, port=%d", host.c_str(), port);
    }
    
    if (url.empty()) {
        app = SRS_CONSTS_RTMP_DEFAULT_APP;
    } else {
        app = url;
    }
    
    vhost = host;
    srs_vhost_resolve(vhost, app, param);
    srs_vhost_resolve(vhost, stream, param);
    
    // Ignore when the param only contains the default vhost.
    if (param == "?vhost=" SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        param = "";
    }
}

void srs_parse_query_string(string q, map<string,string>& query)
{
    // query string flags.
    static vector<string> flags;
    if (flags.empty()) {
        flags.push_back("=");
        flags.push_back(",");
        flags.push_back("&&");
        flags.push_back("&");
        flags.push_back(";");
    }
    
    vector<string> kvs = srs_string_split(q, flags);
    for (int i = 0; i < (int)kvs.size(); i+=2) {
        string k = kvs.at(i);
        string v = (i < (int)kvs.size() - 1)? kvs.at(i+1):"";
        
        query[k] = v;
    }
}

void srs_random_generate(char* bytes, int size)
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        srand(0);
        _random_initialized = true;
    }
    
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (rand() % (256 - 0x0f - 0x0f));
    }
}

string srs_generate_tc_url(string host, string vhost, string app, int port)
{
    string tcUrl = "rtmp://";
    
    if (vhost == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += host;
    } else {
        tcUrl += vhost;
    }
    
    if (port != SRS_CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":" + srs_int2str(port);
    }
    
    tcUrl += "/" + app;
    
    return tcUrl;
}

string srs_generate_stream_with_query(string host, string vhost, string stream, string param)
{
    string url = stream;
    string query = param;
    
    // If no vhost in param, try to append one.
    string guessVhost;
    if (query.find("vhost=") == string::npos) {
        if (vhost != SRS_CONSTS_RTMP_DEFAULT_VHOST) {
            guessVhost = vhost;
        } else if (!srs_is_ipv4(host)) {
            guessVhost = host;
        }
    }
    
    // Well, if vhost exists, always append in query string.
    if (!guessVhost.empty()) {
        query += "&vhost=" + guessVhost;
    }
    
    // Remove the start & when param is empty.
    query = srs_string_trim_start(query, "&");

    // Prefix query with ?.
    if (!query.empty() && !srs_string_starts_with(query, "?")) {
        url += "?";
    }
    
    // Append query to url.
    if (!query.empty()) {
        url += query;
    }
    
    return url;
}

template<typename T>
srs_error_t srs_do_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, T** ppmsg)
{
    srs_error_t err = srs_success;
    
    *ppmsg = NULL;
    T* msg = NULL;
    
    if (type == SrsFrameTypeAudio) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeVideo) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else if (type == SrsFrameTypeScript) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, stream_id);
        
        msg = new T();
        if ((err = msg->create(&header, data, size)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "create message");
        }
    } else {
        return srs_error_new(ERROR_STREAM_CASTER_FLV_TAG, "unknown tag=%#x", (uint8_t)type);
    }
    
    *ppmsg = msg;
    
    return err;
}

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    srs_error_t err = srs_success;
    
    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }
    
    return err;
}

srs_error_t srs_rtmp_create_msg(char type, uint32_t timestamp, char* data, int size, int stream_id, SrsCommonMessage** ppmsg)
{
    srs_error_t err = srs_success;
    
    // only when failed, we must free the data.
    if ((err = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "create message");
    }
    
    return err;
}

string srs_generate_stream_url(string vhost, string app, string stream)
{
    std::string url = "";
    
    if (SRS_CONSTS_RTMP_DEFAULT_VHOST != vhost){
        url += vhost;
    }
    url += "/";
    url += app;
    url += "/";
    url += stream;
    
    return url;
}

void srs_parse_rtmp_url(string url, string& tcUrl, string& stream)
{
    size_t pos;
    
    if ((pos = url.rfind("/")) != string::npos) {
        stream = url.substr(pos + 1);
        tcUrl = url.substr(0, pos);
    } else {
        tcUrl = url;
    }
}

string srs_generate_rtmp_url(string server, int port, string host, string vhost, string app, string stream, string param)
{
    string tcUrl = "rtmp://" + server + ":" + srs_int2str(port) + "/"  + app;
    string streamWithQuery = srs_generate_stream_with_query(host, vhost, stream, param);
    string url = tcUrl + "/" + streamWithQuery;
    return url;
}

srs_error_t srs_write_large_iovs(ISrsProtocolReadWriter* skt, iovec* iovs, int size, ssize_t* pnwrite)
{
    srs_error_t err = srs_success;
    
    // the limits of writev iovs.
    // for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
    // for linux, generally it's 1024.
    static int limits = (int)sysconf(_SC_IOV_MAX);
#else
    static int limits = 1024;
#endif
    
    // send in a time.
    if (size <= limits) {
        if ((err = skt->writev(iovs, size, pnwrite)) != srs_success) {
            return srs_error_wrap(err, "writev");
        }
        return err;
    }
   
    // send in multiple times.
    int cur_iov = 0;
    ssize_t nwrite = 0;
    while (cur_iov < size) {
        int cur_count = srs_min(limits, size - cur_iov);
        if ((err = skt->writev(iovs + cur_iov, cur_count, &nwrite)) != srs_success) {
            return srs_error_wrap(err, "writev");
        }
        cur_iov += cur_count;
        if (pnwrite) {
            *pnwrite += nwrite;
        }
    }
    
    return err;
}

string srs_join_vector_string(vector<string>& vs, string separator)
{
    string str = "";
    
    for (int i = 0; i < (int)vs.size(); i++) {
        str += vs.at(i);
        if (i != (int)vs.size() - 1) {
            str += separator;
        }
    }
    
    return str;
}

bool srs_is_ipv4(string domain)
{
    for (int i = 0; i < (int)domain.length(); i++) {
        char ch = domain.at(i);
        if (ch == '.') {
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            continue;
        }
        
        return false;
    }
    
    return true;
}

uint32_t srs_ipv4_to_num(string ip) {
    int a, b, c, d;
    uint32_t addr = 0;

    if (!srs_is_ipv4(ip)) {
        return 0;
    }

    if (sscanf(ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return 0;
    }

    addr = a << 24;
    addr |= b << 16;
    addr |= c << 8;
    addr |= d;

    return addr;
}

bool srs_ipv4_within_mask(string ip, string network, string mask) {
    uint32_t ip_addr = srs_ipv4_to_num(ip);
    uint32_t mask_addr = srs_ipv4_to_num(mask);
    uint32_t network_addr = srs_ipv4_to_num(network);

    uint32_t net_lower = (network_addr & mask_addr);
    uint32_t net_upper = (net_lower | (~mask_addr));

    if (ip_addr >= net_lower && ip_addr <= net_upper) {
        return true;
    }
    return false;
}

static struct CIDR_VALUE {
    size_t length;
    std::string mask;
} CIDR_VALUES[32] = {
    { .length = 1,  .mask = "128.0.0.0" },
    { .length = 2,  .mask = "192.0.0.0" },
    { .length = 3,  .mask = "224.0.0.0" },
    { .length = 4,  .mask = "240.0.0.0" },
    { .length = 5,  .mask = "248.0.0.0" },
    { .length = 6,  .mask = "252.0.0.0" },
    { .length = 7,  .mask = "254.0.0.0" },
    { .length = 8,  .mask = "255.0.0.0" },
    { .length = 9,  .mask = "255.128.0.0" },
    { .length = 10, .mask = "255.192.0.0" },
    { .length = 11, .mask = "255.224.0.0" },
    { .length = 12, .mask = "255.240.0.0" },
    { .length = 13, .mask = "255.248.0.0" },
    { .length = 14, .mask = "255.252.0.0" },
    { .length = 15, .mask = "255.254.0.0" },
    { .length = 16, .mask = "255.255.0.0" },
    { .length = 17, .mask = "255.255.128.0" },
    { .length = 18, .mask = "255.255.192.0" },
    { .length = 19, .mask = "255.255.224.0" },
    { .length = 20, .mask = "255.255.240.0" },
    { .length = 21, .mask = "255.255.248.0" },
    { .length = 22, .mask = "255.255.252.0" },
    { .length = 23, .mask = "255.255.254.0" },
    { .length = 24, .mask = "255.255.255.0" },
    { .length = 25, .mask = "255.255.255.128" },
    { .length = 26, .mask = "255.255.255.192" },
    { .length = 27, .mask = "255.255.255.224" },
    { .length = 28, .mask = "255.255.255.240" },
    { .length = 29, .mask = "255.255.255.248" },
    { .length = 30, .mask = "255.255.255.252" },
    { .length = 31, .mask = "255.255.255.254" },
    { .length = 32, .mask = "255.255.255.255" },
};

string srs_get_cidr_mask(string network_address) {
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_is_ipv4(network_address)) {
            return CIDR_VALUES[32-1].mask;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string is_ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_is_ipv4(is_ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    size_t cidr_length_num = 31;
    try {
        cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return CIDR_VALUES[cidr_length_num-1].mask;
}

string srs_get_cidr_ipv4(string network_address) {
    string delimiter = "/";

    size_t delimiter_position = network_address.find(delimiter);
    if (delimiter_position == string::npos) {
        // Even if it does not have "/N", it can be a valid IP, by default "/32".
        if (srs_is_ipv4(network_address)) {
            return network_address;
        }
        return "";
    }

    // Change here to include IPv6 support.
    string ipv4_address = network_address.substr(0, delimiter_position);
    if (!srs_is_ipv4(ipv4_address)) {
        return "";
    }

    size_t cidr_length_position = delimiter_position + delimiter.length();
    if (cidr_length_position >= network_address.length()) {
        return "";
    }

    string cidr_length = network_address.substr(cidr_length_position, network_address.length());
    if (cidr_length.length() <= 0) {
        return "";
    }

    try {
        size_t cidr_length_num = atoi(cidr_length.c_str());
        if (cidr_length_num <= 0) {
            return "";
        }
    } catch (...) {
        return "";
    }

    return ipv4_address;
}