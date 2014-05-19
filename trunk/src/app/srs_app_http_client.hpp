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

#ifndef SRS_APP_HTTP_CLIENT_HPP
#define SRS_APP_HTTP_CLIENT_HPP

/*
#include <srs_app_http_client.hpp>
*/
#include <srs_core.hpp>

#ifdef SRS_AUTO_HTTP_PARSER

#include <srs_app_st.hpp>

class SrsHttpUri;
class SrsHttpParser;

/**
* http client to GET/POST/PUT/DELETE uri
*/
class SrsHttpClient
{
private:
    bool connected;
    st_netfd_t stfd;
    SrsHttpParser* parser;
public:
    SrsHttpClient();
    virtual ~SrsHttpClient();
public:
    /**
    * to post data to the uri.
    * @param req the data post to uri.
    * @param res the response data from server.
    */
    virtual int post(SrsHttpUri* uri, std::string req, std::string& res);
private:
    virtual void disconnect();
    virtual int connect(SrsHttpUri* uri);
};

#endif

#endif
