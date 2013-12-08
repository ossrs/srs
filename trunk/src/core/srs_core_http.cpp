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

#ifdef SRS_HTTP

#include <sstream>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <srs_core_error.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_log.hpp>
#include <srs_core_socket.hpp>

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

const char* SrsHttpUri::get_path()
{
    return path.c_str();
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
	connected = false;
	stfd = NULL;
}

SrsHttpClient::~SrsHttpClient()
{
	disconnect();
}

int SrsHttpClient::post(SrsHttpUri* uri, std::string req, std::string& res)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = connect(uri)) != ERROR_SUCCESS) {
		srs_error("http connect server failed. ret=%d", ret);
		return ret;
	}
	
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << uri->get_path() << " "
        << "HTTP/1.1\r\n"
        << "Host: " << uri->get_host() << "\r\n"
        << "Connection: Keep-Alive" << "\r\n"
        << "Content-Length: " << std::dec << req.length() << "\r\n"
        << "User-Agent: " << RTMP_SIG_SRS_NAME << RTMP_SIG_SRS_VERSION << "\r\n"
        << "Content-Type: text/html" << "\r\n"
        << "\r\n"
        << req;
    
    SrsSocket skt(stfd);
    
    std::string data = ss.str();
    ssize_t nwrite;
    if ((ret = skt.write(data.c_str(), data.length(), &nwrite)) != ERROR_SUCCESS) {
        // disconnect when error.
        disconnect();
        
        srs_error("write http post failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = parse_response(uri, &skt, &res)) != ERROR_SUCCESS) {
        srs_error("parse http post response failed. ret=%d", ret);
        return ret;
    }
    srs_info("parse http post response success.");
	
	return ret;
}

void SrsHttpClient::disconnect()
{
	connected = false;
	
	if (stfd) {
		int fd = st_netfd_fileno(stfd);
		st_netfd_close(stfd);
		stfd = NULL;
		
		// st does not close it sometimes, 
		// close it manually.
		::close(fd);
	}
}

int SrsHttpClient::connect(SrsHttpUri* uri)
{
	int ret = ERROR_SUCCESS;
	
	if (connected) {
		return ret;
	}
	
	disconnect();
	
	std::string ip = srs_dns_resolve(uri->get_host());
	if (ip.empty()) {
		ret = ERROR_SYSTEM_IP_INVALID;
		srs_error("dns resolve server error, ip empty. ret=%d", ret);
		return ret;
	}

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }
    
    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }
	
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uri->get_port());
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. "
        	"ip=%s, port=%d, ret=%d", ip.c_str(), uri->get_port(), ret);
        return ret;
    }
    srs_info("connect to server success. "
    	"http url=%s, server=%s, ip=%s, port=%d", 
    	uri->get_url(), uri->get_host(), ip.c_str(), uri->get_port());
    
    connected = true;
    
	return ret;
}

int SrsHttpClient::parse_response(SrsHttpUri* uri, SrsSocket* skt, std::string* response)
{
    int ret = ERROR_SUCCESS;

    int body_received = 0;
    if ((ret = parse_response_header(skt, response, body_received)) != ERROR_SUCCESS) {
        srs_error("parse response header failed. ret=%d", ret);
        return ret;
    }

    if ((ret = parse_response_body(uri, skt, response, body_received)) != ERROR_SUCCESS) {
        srs_error("parse response body failed. ret=%d", ret);
        return ret;
    }

    srs_info("url %s download, body size=%"PRId64, uri->get_url(), http_header.content_length);
    
    return ret;
}

int SrsHttpClient::parse_response_header(SrsSocket* skt, std::string* response, int& body_received)
{
    int ret = ERROR_SUCCESS;

    http_parser_settings settings;
    
    memset(&settings, 0, sizeof(settings));
    settings.on_headers_complete = on_headers_complete;
    
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    // callback object ptr.
    parser.data = (void*)this;
    
    // reset response header.
    memset(&http_header, 0, sizeof(http_header));
    
    // parser header.
    char buf[SRS_HTTP_HEADER_BUFFER];
    for (;;) {
        ssize_t nread;
        if ((ret = skt->read(buf, (size_t)sizeof(buf), &nread)) != ERROR_SUCCESS) {
            srs_error("read body from server failed. ret=%d", ret);
            return ret;
        }
        
        ssize_t nparsed = http_parser_execute(&parser, &settings, buf, nread);
        srs_info("read_size=%d, nparsed=%d", (int)nread, (int)nparsed);

        // check header size.
        if (http_header.nread != 0) {
            body_received = nread - nparsed;
            
            srs_info("http header parsed, size=%d, content-length=%"PRId64", body-received=%d", 
                http_header.nread, http_header.content_length, body_received);
                
            if(response != NULL && body_received > 0){
                response->append(buf + nparsed, body_received);
            }

            return ret;
        }
        
        if (nparsed != nread) {
            ret = ERROR_HTTP_PARSE_HEADER;
            srs_error("parse response error, parsed(%d)!=read(%d), ret=%d", (int)nparsed, (int)nread, ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsHttpClient::parse_response_body(SrsHttpUri* uri, SrsSocket* skt, std::string* response, int body_received)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(uri != NULL);
    
    uint64_t body_left = http_header.content_length - body_received;
    
    if (body_left <= 0) {
        return ret;
    }
    
    if (response != NULL) {
        char buf[SRS_HTTP_BODY_BUFFER];
        
        return parse_response_body_data(
        	uri, skt, response, (size_t)body_left, 
        	(const void*)buf, (size_t)SRS_HTTP_BODY_BUFFER
        );
    } else {
        // if ignore response, use shared fast memory.
        static char buf[SRS_HTTP_BODY_BUFFER];
        
        return parse_response_body_data(
			uri, skt, response, (size_t)body_left, 
			(const void*)buf, (size_t)SRS_HTTP_BODY_BUFFER
		);
    }
    
    return ret;
}

int SrsHttpClient::parse_response_body_data(SrsHttpUri* uri, SrsSocket* skt, std::string* response, size_t body_left, const void* buf, size_t size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(uri != NULL);
    
    while (body_left > 0) {
        ssize_t nread;
		int size_to_read = srs_min(size, body_left);
        if ((ret = skt->read(buf, size_to_read, &nread)) != ERROR_SUCCESS) {
            srs_error("read header from server failed. ret=%d", ret);
            return ret;
        }
        
        if (response != NULL && nread > 0) {
            response->append((char*)buf, nread);
        }
        
        body_left -= nread;
        srs_info("read url(%s) content partial %"PRId64"/%"PRId64"", 
            uri->get_url(), http_header.content_length - body_left, http_header.content_length);
    }
    
    return ret;
}

int SrsHttpClient::on_headers_complete(http_parser* parser)
{
    SrsHttpClient* obj = (SrsHttpClient*)parser->data;
    obj->comple_header(parser);
    
    // see http_parser.c:1570, return 1 to skip body.
    return 1;
}

void SrsHttpClient::comple_header(http_parser* parser)
{
    // save the parser status when header parse completed.
    memcpy(&http_header, parser, sizeof(http_header));
}

SrsHttpHooks::SrsHttpHooks()
{
}

SrsHttpHooks::~SrsHttpHooks()
{
}

int SrsHttpHooks::on_connect(std::string url, int client_id, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_error("http uri parse on_connect url failed. "
			"client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
		return ret;
	}
	
	/**
	{
		"action": "on_connect",
		"client_id": 1985,
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"pageUrl": "http://www.test.com/live.html"
	}
    */
	std::stringstream ss;
	ss << "{"
		// action
		<< '"' << "action" << '"' << ':'
		<< '"' << "on_connect" << '"'
		<< ','
		// client_id
		<< '"' << "client_id" << '"' << ':'
		<< std::dec << client_id
		<< ','
		// ip
		<< '"' << "ip" << '"' << ':'
		<< '"' << ip << '"'
		<< ','
		// vhost
		<< '"' << "vhost" << '"' << ':'
		<< '"' << req->vhost << '"'
		<< ','
		// app
		<< '"' << "app" << '"' << ':'
		<< '"' << req->app << '"'
		<< ','
		// pageUrl
		<< '"' << "pageUrl" << '"' << ':'
		<< '"' << req->pageUrl << '"'
		//<< ','
		<< "}";
	std::string data = ss.str();
	std::string res;
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_error("http post on_connect uri failed. "
			"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
			client_id, url.c_str(), data.c_str(), res.c_str(), ret);
		return ret;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_error("http hook on_connect validate failed. "
			"client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
		return ret;
	}
	
	srs_trace("http hook on_connect success. "
		"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
		client_id, url.c_str(), data.c_str(), res.c_str(), ret);
	
	return ret;
}

void SrsHttpHooks::on_close(std::string url, int client_id, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_warn("http uri parse on_close url failed, ignored. "
			"client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
		return;
	}
	
	/**
	{
		"action": "on_close",
		"client_id": 1985,
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"stream": "livestream"
	}
    */
	std::stringstream ss;
	ss << "{"
		// action
		<< '"' << "action" << '"' << ':'
		<< '"' << "on_close" << '"'
		<< ','
		// client_id
		<< '"' << "client_id" << '"' << ':'
		<< std::dec << client_id
		<< ','
		// ip
		<< '"' << "ip" << '"' << ':'
		<< '"' << ip << '"'
		<< ','
		// vhost
		<< '"' << "vhost" << '"' << ':'
		<< '"' << req->vhost << '"'
		<< ','
		// app
		<< '"' << "app" << '"' << ':'
		<< '"' << req->app << '"'
		//<< ','
		<< "}";
	std::string data = ss.str();
	std::string res;
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_warn("http post on_close uri failed, ignored. "
			"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
			client_id, url.c_str(), data.c_str(), res.c_str(), ret);
		return;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_warn("http hook on_close validate failed, ignored. "
			"client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
		return;
	}
	
	srs_trace("http hook on_close success. "
		"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
		client_id, url.c_str(), data.c_str(), res.c_str(), ret);
	
	return;
}

int SrsHttpHooks::on_publish(std::string url, int client_id, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_error("http uri parse on_publish url failed. "
			"client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
		return ret;
	}
	
	/**
	{
		"action": "on_publish",
		"client_id": 1985,
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"stream": "livestream"
	}
    */
	std::stringstream ss;
	ss << "{"
		// action
		<< '"' << "action" << '"' << ':'
		<< '"' << "on_publish" << '"'
		<< ','
		// client_id
		<< '"' << "client_id" << '"' << ':'
		<< std::dec << client_id
		<< ','
		// ip
		<< '"' << "ip" << '"' << ':'
		<< '"' << ip << '"'
		<< ','
		// vhost
		<< '"' << "vhost" << '"' << ':'
		<< '"' << req->vhost << '"'
		<< ','
		// app
		<< '"' << "app" << '"' << ':'
		<< '"' << req->app << '"'
		<< ','
		// stream
		<< '"' << "stream" << '"' << ':'
		<< '"' << req->stream << '"'
		//<< ','
		<< "}";
	std::string data = ss.str();
	std::string res;
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_error("http post on_publish uri failed. "
			"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
			client_id, url.c_str(), data.c_str(), res.c_str(), ret);
		return ret;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_error("http hook on_publish validate failed. "
			"client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
		return ret;
	}
	
	srs_trace("http hook on_publish success. "
		"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
		client_id, url.c_str(), data.c_str(), res.c_str(), ret);
	
	return ret;
}

void SrsHttpHooks::on_unpublish(std::string url, int client_id, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_warn("http uri parse on_unpublish url failed, ignored. "
			"client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
		return;
	}
	
	/**
	{
		"action": "on_unpublish",
		"client_id": 1985,
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"stream": "livestream"
	}
    */
	std::stringstream ss;
	ss << "{"
		// action
		<< '"' << "action" << '"' << ':'
		<< '"' << "on_unpublish" << '"'
		<< ','
		// client_id
		<< '"' << "client_id" << '"' << ':'
		<< std::dec << client_id
		<< ','
		// ip
		<< '"' << "ip" << '"' << ':'
		<< '"' << ip << '"'
		<< ','
		// vhost
		<< '"' << "vhost" << '"' << ':'
		<< '"' << req->vhost << '"'
		<< ','
		// app
		<< '"' << "app" << '"' << ':'
		<< '"' << req->app << '"'
		<< ','
		// stream
		<< '"' << "stream" << '"' << ':'
		<< '"' << req->stream << '"'
		//<< ','
		<< "}";
	std::string data = ss.str();
	std::string res;
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_warn("http post on_unpublish uri failed, ignored. "
			"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
			client_id, url.c_str(), data.c_str(), res.c_str(), ret);
		return;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_warn("http hook on_unpublish validate failed, ignored. "
			"client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
		return;
	}
	
	srs_trace("http hook on_unpublish success. "
		"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
		client_id, url.c_str(), data.c_str(), res.c_str(), ret);
	
	return;
}

int SrsHttpHooks::on_play(std::string url, int client_id, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_error("http uri parse on_play url failed. "
			"client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
		return ret;
	}
	
	/**
	{
		"action": "on_play",
		"client_id": 1985,
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"stream": "livestream"
	}
    */
	std::stringstream ss;
	ss << "{"
		// action
		<< '"' << "action" << '"' << ':'
		<< '"' << "on_play" << '"'
		<< ','
		// client_id
		<< '"' << "client_id" << '"' << ':'
		<< std::dec << client_id
		<< ','
		// ip
		<< '"' << "ip" << '"' << ':'
		<< '"' << ip << '"'
		<< ','
		// vhost
		<< '"' << "vhost" << '"' << ':'
		<< '"' << req->vhost << '"'
		<< ','
		// app
		<< '"' << "app" << '"' << ':'
		<< '"' << req->app << '"'
		<< ','
		// stream
		<< '"' << "stream" << '"' << ':'
		<< '"' << req->stream << '"'
		//<< ','
		<< "}";
	std::string data = ss.str();
	std::string res;
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_error("http post on_play uri failed. "
			"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
			client_id, url.c_str(), data.c_str(), res.c_str(), ret);
		return ret;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_error("http hook on_play validate failed. "
			"client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
		return ret;
	}
	
	srs_trace("http hook on_play success. "
		"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
		client_id, url.c_str(), data.c_str(), res.c_str(), ret);
	
	return ret;
}

void SrsHttpHooks::on_stop(std::string url, int client_id, std::string ip, SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	SrsHttpUri uri;
	if ((ret = uri.initialize(url)) != ERROR_SUCCESS) {
		srs_warn("http uri parse on_stop url failed, ignored. "
			"client_id=%d, url=%s, ret=%d", client_id, url.c_str(), ret);
		return;
	}
	
	/**
	{
		"action": "on_stop",
		"client_id": 1985,
		"ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
		"stream": "livestream"
	}
    */
	std::stringstream ss;
	ss << "{"
		// action
		<< '"' << "action" << '"' << ':'
		<< '"' << "on_stop" << '"'
		<< ','
		// client_id
		<< '"' << "client_id" << '"' << ':'
		<< std::dec << client_id
		<< ','
		// ip
		<< '"' << "ip" << '"' << ':'
		<< '"' << ip << '"'
		<< ','
		// vhost
		<< '"' << "vhost" << '"' << ':'
		<< '"' << req->vhost << '"'
		<< ','
		// app
		<< '"' << "app" << '"' << ':'
		<< '"' << req->app << '"'
		<< ','
		// stream
		<< '"' << "stream" << '"' << ':'
		<< '"' << req->stream << '"'
		//<< ','
		<< "}";
	std::string data = ss.str();
	std::string res;
	
	SrsHttpClient http;
	if ((ret = http.post(&uri, data, res)) != ERROR_SUCCESS) {
		srs_warn("http post on_stop uri failed, ignored. "
			"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
			client_id, url.c_str(), data.c_str(), res.c_str(), ret);
		return;
	}
	
	if (res.empty() || res != SRS_HTTP_RESPONSE_OK) {
		ret = ERROR_HTTP_DATA_INVLIAD;
		srs_warn("http hook on_stop validate failed, ignored. "
			"client_id=%d, res=%s, ret=%d", client_id, res.c_str(), ret);
		return;
	}
	
	srs_trace("http hook on_stop success. "
		"client_id=%d, url=%s, request=%s, response=%s, ret=%d",
		client_id, url.c_str(), data.c_str(), res.c_str(), ret);
	
	return;
}

#endif
