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

#ifndef SRS_CORE_HTTP_HPP
#define SRS_CORE_HTTP_HPP

/*
#include <srs_core_http.hpp>
*/
#include <srs_core.hpp>

#ifdef SRS_HTTP

class SrsRequest;
class SrsSocket;

#include <string>

#include <http_parser.h>

#define SRS_HTTP_HEADER_BUFFER		1024
#define SRS_HTTP_BODY_BUFFER		32 * 1024

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
    virtual const char* get_path();
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
private:
	bool connected;
	st_netfd_t stfd;
private:
    http_parser http_header;
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
private:
    virtual int parse_response(SrsHttpUri* uri, SrsSocket* skt, std::string* response);
    virtual int parse_response_header(SrsSocket* skt, std::string* response, int& body_received);
    virtual int parse_response_body(SrsHttpUri* uri, SrsSocket* skt, std::string* response, int body_received);
    virtual int parse_response_body_data(SrsHttpUri* uri, SrsSocket* skt, std::string* response, size_t body_left, const void* buf, size_t size);
private:
    static int on_headers_complete(http_parser* parser);
    virtual void comple_header(http_parser* parser);
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
	* on_connect hook, when client connect to srs.
	* @param client_id the id of client on server.
	* @param url the api server url, to valid the client. 
	* 		ignore if empty.
	* @return valid failed or connect to the url failed.
	*/
	virtual int on_connect(std::string url, int client_id, std::string ip, SrsRequest* req);
	/**
	* on_close hook, when client disconnect to srs, where client is valid by on_connect.
	* @param client_id the id of client on server.
	* @param url the api server url, to process the event. 
	* 		ignore if empty.
	*/
	virtual void on_close(std::string url, int client_id, std::string ip, SrsRequest* req);
	/**
	* on_publish hook, when client(encoder) start to publish stream
	* @param client_id the id of client on server.
	* @param url the api server url, to valid the client. 
	* 		ignore if empty.
	* @return valid failed or connect to the url failed.
	*/
	virtual int on_publish(std::string url, int client_id, std::string ip, SrsRequest* req);
	/**
	* on_unpublish hook, when client(encoder) stop publish stream.
	* @param client_id the id of client on server.
	* @param url the api server url, to process the event. 
	* 		ignore if empty.
	*/
	virtual void on_unpublish(std::string url, int client_id, std::string ip, SrsRequest* req);
	/**
	* on_play hook, when client start to play stream.
	* @param client_id the id of client on server.
	* @param url the api server url, to valid the client. 
	* 		ignore if empty.
	* @return valid failed or connect to the url failed.
	*/
	virtual int on_play(std::string url, int client_id, std::string ip, SrsRequest* req);
	/**
	* on_stop hook, when client stop to play the stream.
	* @param client_id the id of client on server.
	* @param url the api server url, to process the event. 
	* 		ignore if empty.
	*/
	virtual void on_stop(std::string url, int client_id, std::string ip, SrsRequest* req);
};

#endif

#endif