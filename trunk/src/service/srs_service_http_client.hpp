/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#ifndef SRS_SERVICE_HTTP_CLIENT_HPP
#define SRS_SERVICE_HTTP_CLIENT_HPP

#include <srs_core.hpp>

#include <string>
#include <map>

#include <srs_service_st.hpp>
#include <srs_http_stack.hpp>

class SrsHttpUri;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsStSocket;
class SrsKbps;
class SrsWallClock;
class SrsTcpClient;

// The default timeout for http client.
#define SRS_HTTP_CLIENT_TIMEOUT (30 * SRS_UTIME_SECONDS)

// The client to GET/POST/PUT/DELETE over HTTP.
// @remark We will reuse the TCP transport until initialize or channel error,
//      such as send/recv failed.
// Usage:
//      SrsHttpClient hc;
//      hc.initialize("127.0.0.1", 80, 9000);
//      hc.post("/api/v1/version", "Hello world!", NULL);
class SrsHttpClient
{
private:
    // The underlayer TCP transport, set to NULL when disconnect, or never not NULL when connected.
    // We will disconnect transport when initialize or channel error, such as send/recv error.
    SrsTcpClient* transport;
    SrsHttpParser* parser;
    std::map<std::string, std::string> headers;
    SrsKbps* kbps;
    SrsWallClock* clk;
private:
    // The timeout in srs_utime_t.
    srs_utime_t timeout;
    // The host name or ip.
    std::string host;
    int port;
public:
    SrsHttpClient();
    virtual ~SrsHttpClient();
public:
    // Initliaze the client, disconnect the transport, renew the HTTP parser.
    // @param tm The underlayer TCP transport timeout in srs_utime_t.
    // @remark we will set default values in headers, which can be override by set_header.
    virtual srs_error_t initialize(std::string h, int p, srs_utime_t tm = SRS_HTTP_CLIENT_TIMEOUT);
    // Set HTTP request header in header[k]=v.
    // @return the HTTP client itself.
    virtual SrsHttpClient* set_header(std::string k, std::string v);
public:
    // Post data to the uri.
    // @param the path to request on.
    // @param req the data post to uri. empty string to ignore.
    // @param ppmsg output the http message to read the response.
    // @remark user must free the ppmsg if not NULL.
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage** ppmsg);
    // Get data from the uri.
    // @param the path to request on.
    // @param req the data post to uri. empty string to ignore.
    // @param ppmsg output the http message to read the response.
    // @remark user must free the ppmsg if not NULL.
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage** ppmsg);
private:
    virtual void set_recv_timeout(srs_utime_t tm);
public:
    virtual void kbps_sample(const char* label, int64_t age);
private:
    virtual void disconnect();
    virtual srs_error_t connect();
};

#endif

