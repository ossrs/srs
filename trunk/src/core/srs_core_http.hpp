/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#ifndef SRS_CORE_HTTP_HPP
#define SRS_CORE_HTTP_HPP

/*
#include <srs_core_http.hpp>
*/
#include <srs_core.hpp>

class SrsRequest;

#include <string>

#ifdef SRS_HTTP

#include <http_parser.h>

/**
* used to resolve the http uri.
*/
class SrsHttpUri
{
private:
    std::string url;
    std::string schema;
    std::string host;
    int port;
    std::string path;
public:
	SrsHttpUri();
	virtual ~SrsHttpUri();
public:
	/**
	* initialize the http uri.
	*/
	virtual int initialize(std::string _url);
public:
    virtual const char* get_url();
    virtual const char* get_schema();
    virtual const char* get_host();
    virtual int get_port();
private:
    /**
    * get the parsed url field.
    * @return return empty string if not set.
    */
    virtual std::string get_uri_field(std::string uri, http_parser_url* hp_u, http_parser_url_fields field);
};

/**
* http client to GET/POST/PUT/DELETE uri
*/
class SrsHttpClient
{
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
};

/**
* the http hooks, http callback api,
* for some event, such as on_connect, call
* a http api(hooks).
*/
class SrsHttpHooks
{
public:
	SrsHttpHooks();
	virtual ~SrsHttpHooks();
public:
	/**
	* on_connect hook,
	* @param url the api server url, to valid the client. 
	* 		ignore if empty.
	* @return valid failed or connect to the url failed.
	*/
	virtual int on_connect(std::string url, std::string ip, SrsRequest* req);
};

#endif

#endif