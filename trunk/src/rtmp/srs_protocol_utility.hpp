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

#ifndef SRS_RTMP_PROTOCOL_CONSTS_HPP
#define SRS_RTMP_PROTOCOL_CONSTS_HPP

/*
#include <srs_protocol_utility.hpp>
*/
#include <srs_core.hpp>

#include <string>

#include <srs_kernel_consts.hpp>

/**
* parse the tcUrl, output the schema, host, vhost, app and port.
* @param tcUrl, the input tcUrl, for example, 
*       rtmp://192.168.1.10:19350/live?vhost=vhost.ossrs.net
* @param schema, for example, rtmp
* @param host, for example, 192.168.1.10
* @param vhost, for example, vhost.ossrs.net.
*       vhost default to host, when user not set vhost in query of app.
* @param app, for example, live
* @param port, for example, 19350
*       default to 1935 if not specified.
* param param, for example, vhost=vhost.ossrs.net
*/
extern void srs_discovery_tc_url(
    std::string tcUrl, 
    std::string& schema, std::string& host, std::string& vhost, 
    std::string& app, std::string& port, std::string& param
);

/**
* resolve the vhost in query string
* @pram vhost, update the vhost if query contains the vhost.
* @param app, may contains the vhost in query string format:
*   app?vhost=request_vhost
*   app...vhost...request_vhost
* @param param, the query, for example, ?vhost=xxx
*/ 
extern void srs_vhost_resolve(std::string& vhost, std::string& app, std::string& param);

/**
* generate ramdom data for handshake.
*/
extern void srs_random_generate(char* bytes, int size);

/**
* generate the tcUrl.
* @param param, the app parameters in tcUrl. for example, ?key=xxx,vhost=xxx
* @return the tcUrl generated from ip/vhost/app/port.
* @remark when vhost equals to __defaultVhost__, use ip as vhost.
* @remark ignore port if port equals to default port 1935.
*/
extern std::string srs_generate_tc_url(
    std::string ip, std::string vhost, std::string app, std::string port,
    std::string param
);

/**
* compare the memory in bytes.
* @return true if completely equal; otherwise, false.
*/
extern bool srs_bytes_equals(void* pa, void* pb, int size);

#endif
