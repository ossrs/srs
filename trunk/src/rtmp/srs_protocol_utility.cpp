/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_protocol_utility.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

void srs_discovery_tc_url(
    string tcUrl, 
    string& schema, string& host, string& vhost, 
    string& app, string& port, std::string& param
) {
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

    port = RTMP_DEFAULT_PORT;
    if ((pos = host.find(":")) != std::string::npos) {
        port = host.substr(pos + 1);
        host = host.substr(0, pos);
        srs_info("discovery host=%s, port=%s", host.c_str(), port.c_str());
    }
    
    app = url;
    vhost = host;
    srs_vhost_resolve(vhost, app, param);
}

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
    app = srs_string_replace(app, "=", "?");
    
    if ((pos = app.find("?")) == std::string::npos) {
        return;
    }
    
    std::string query = app.substr(pos + 1);
    app = app.substr(0, pos);
    
    if ((pos = query.find("vhost?")) != std::string::npos) {
        query = query.substr(pos + 6);
        if (!query.empty()) {
            vhost = query;
        }
        if ((pos = vhost.find("?")) != std::string::npos) {
            vhost = vhost.substr(0, pos);
        }
    }
}

void srs_random_generate(char* bytes, int size)
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        srand(0);
        _random_initialized = true;
        srs_trace("srand initialized the random.");
    }
    
    static char cdata[]  = { 
        0x73, 0x69, 0x6d, 0x70, 0x6c, 0x65, 0x2d, 0x72, 0x74, 0x6d, 0x70, 0x2d, 0x73, 0x65, 
        0x72, 0x76, 0x65, 0x72, 0x2d, 0x77, 0x69, 0x6e, 0x6c, 0x69, 0x6e, 0x2d, 0x77, 0x69, 
        0x6e, 0x74, 0x65, 0x72, 0x73, 0x65, 0x72, 0x76, 0x65, 0x72, 0x40, 0x31, 0x32, 0x36, 
        0x2e, 0x63, 0x6f, 0x6d
    };
    for (int i = 0; i < size; i++) {
        bytes[i] = cdata[rand() % (sizeof(cdata) - 1)];
    }
}

string srs_generate_tc_url(string ip, string vhost, string app, string port, string param)
{
    string tcUrl = "rtmp://";
    
    if (vhost == RTMP_VHOST_DEFAULT) {
        tcUrl += ip;
    } else {
        tcUrl += vhost;
    }
    
    if (port != RTMP_DEFAULT_PORT) {
        tcUrl += ":";
        tcUrl += port;
    }
    
    tcUrl += "/";
    tcUrl += app;
    tcUrl += param;
    
    return tcUrl;
}

/**
* compare the memory in bytes.
*/
bool srs_bytes_equals(void* pa, void* pb, int size)
{
    u_int8_t* a = (u_int8_t*)pa;
    u_int8_t* b = (u_int8_t*)pb;
    
    if (!a && !b) {
        return true;
    }
    
    if (!a || !b) {
        return false;
    }
    
    for(int i = 0; i < size; i++){
        if(a[i] != b[i]){
            return false;
        }
    }

    return true;
}
