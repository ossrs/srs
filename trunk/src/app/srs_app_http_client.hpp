/*
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

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

#ifndef SRS_APP_HTTP_CLIENT_HPP
#define SRS_APP_HTTP_CLIENT_HPP

/*
#include <srs_app_http_client.hpp>
*/
#include <srs_core.hpp>

#include <string>

#ifdef SRS_AUTO_HTTP_CORE

#include <srs_app_st.hpp>

class SrsHttpUri;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsStSocket;

// the default timeout for http client.
#define SRS_HTTP_CLIENT_TIMEOUT_US (int64_t)(30*1000*1000LL)

/**
* http client to GET/POST/PUT/DELETE uri
*/
class SrsHttpClient
{
private:
    SrsTcpClient* transport;
    SrsHttpParser* parser;
private:
    int64_t timeout_us;
    // host name or ip.
    std::string host;
    int port;
public:
    SrsHttpClient();
    virtual ~SrsHttpClient();
public:
    /**
    * initialize the client, connect to host and port.
    */
    virtual int initialize(std::string h, int p, int64_t t_us = SRS_HTTP_CLIENT_TIMEOUT_US);
public:
    /**
    * to post data to the uri.
    * @param the path to request on.
    * @param req the data post to uri. empty string to ignore.
    * @param ppmsg output the http message to read the response.
    */
    virtual int post(std::string path, std::string req, ISrsHttpMessage** ppmsg);
    /**
    * to get data from the uri.
    * @param the path to request on.
    * @param req the data post to uri. empty string to ignore.
    * @param ppmsg output the http message to read the response.
    */
    virtual int get(std::string path, std::string req, ISrsHttpMessage** ppmsg);
private:
    virtual void disconnect();
    virtual int connect();
};

#endif

#endif

