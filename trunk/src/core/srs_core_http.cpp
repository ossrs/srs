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

#include <srs_core_http.hpp>

#include <stdlib.h>

#include <srs_core_error.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_log.hpp>

#ifdef SRS_HTTP

#define SRS_DEFAULT_HTTP_PORT 80
#define SRS_HTTP_RESPONSE_OK "0"

SrsHttpUri::SrsHttpUri()
{
	port = SRS_DEFAULT_HTTP_PORT;
}

SrsHttpUri::~SrsHttpUri()
{
}

int SrsHttpUri::initialize(std::string _url)
{
	int ret = ERROR_SUCCESS;
    
    url = _url;
    const char* purl = url.c_str();
    
    http_parser_url hp_u;
    if((ret = http_parser_parse_url(purl, url.length(), 0, &hp_u)) != 0){
        int code = ret;
        ret = ERROR_HTTP_PARSE_URI;
        
        srs_error("parse url %s failed, code=%d, ret=%d", purl, code, ret);
        return ret;
    }
    
	std::string field = get_uri_field(url, &hp_u, UF_SCHEMA);
    if(!field.empty()){
        schema = field;
    }
    
    host = get_uri_field(url, &hp_u, UF_HOST);
    
    field = get_uri_field(url, &hp_u, UF_PORT);
    if(!field.empty()){
        port = atoi(field.c_str());
    }
    
    path = get_uri_field(url, &hp_u, UF_PATH);
    srs_info("parse url %s success", purl);
    
	return ret;
}

const char* SrsHttpUri::get_url()
{
    return url.c_str();
}

const char* SrsHttpUri::get_schema()
{
    return schema.c_str();
}

const char* SrsHttpUri::get_host()
{
    return host.c_str();
}

int SrsHttpUri::get_port()
{
    return port;
}

std::string SrsHttpUri::get_uri_field(std::string uri, http_parser_url* hp_u, http_parser_url_fields field)
{
    if((hp_u->field_set & (1 << field)) == 0){
        return "";
    }
    
    srs_verbose("uri field matched, off=%d, len=%d, value=%.*s", 
        hp_u->field_data[field].off, 
        hp_u->field_data[field].len, 
        hp_u->field_data[field].len, 
        uri.c_str() + hp_u->field_data[field].off);
    
    int offset = hp_u->field_data[field].off;
    int len = hp_u->field_data[field].len;
    
    return uri.substr(offset, len);
}

SrsHttpClient::SrsHttpClient()
{
}

SrsHttpClient::~SrsHttpClient()
{
}

int SrsHttpClient::post(SrsHttpUri* uri, std::string req, std::string& res)
{
	int ret = ERROR_SUCCESS;
	return ret;
}

SrsHttpHooks::SrsHttpHooks()
{
}

SrsHttpHooks::~SrsHttpHooks()
{
}

int SrsHttpHooks::on_connect(std::string url, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_error("http uri parse url failed. "
			"url=%s, ret=%d", url.c_str(), ret);
		return ret;
	}
	
	std::string res;
	std::string data;
	/**
	{
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"pageUrl": "http://www.test.com/live.html"
	}
    */
	data += "{";
	// ip
	data += "\"ip\":";
	data += "\"" + ip + "\"";
	data += ",";
	// vhost
	data += "\"vhost\":";
	data += "\"" + req->vhost + "\"";
	data += ",";
	data += ",";
	// app
	data += "\"vhost\":";
	data += "\"" + req->app + "\"";
	data += ",";
	// pageUrl
	data += "\"vhost\":";
	data += "\"" + req->pageUrl + "\"";
	//data += ",";
	data += "}";
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_error("http post uri failed. "
			"url=%s, request=%s, response=%s, ret=%d",
			url.c_str(), data.c_str(), res.c_str(), ret);
		return ret;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_error("http hook validate failed. "
			"res=%s, ret=%d", res.c_str(), ret);
		return ret;
	}
	
	srs_trace("http hook on_connect success. "
		"url=%s, request=%s, response=%s, ret=%d",
		url.c_str(), data.c_str(), res.c_str(), ret);
	
	return ret;
}

#endif
