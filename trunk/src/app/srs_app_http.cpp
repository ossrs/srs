/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include <srs_app_http.hpp>

#ifdef SRS_AUTO_HTTP_PARSER

#include <stdlib.h>
#include <sys/stat.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_rtmp_buffer.hpp>
#include <srs_kernel_file.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_buffer.hpp>

#define SRS_DEFAULT_HTTP_PORT 80

// for http parser macros
#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

#define SRS_HTTP_DEFAULT_PAGE "index.html"

int srs_go_http_response_json(ISrsHttpResponseWriter* w, string data)
{
    w->header()->set_content_length(data.length());
    w->header()->set_content_type("application/json");
    
    return w->write((char*)data.data(), data.length());
}

// get the status text of code.
string srs_generate_http_status_text(int status)
{
    static std::map<int, std::string> _status_map;
    if (_status_map.empty()) {
        _status_map[SRS_CONSTS_HTTP_Continue                       ] = SRS_CONSTS_HTTP_Continue_str                     ;      
        _status_map[SRS_CONSTS_HTTP_SwitchingProtocols             ] = SRS_CONSTS_HTTP_SwitchingProtocols_str           ;      
        _status_map[SRS_CONSTS_HTTP_OK                             ] = SRS_CONSTS_HTTP_OK_str                           ;      
        _status_map[SRS_CONSTS_HTTP_Created                        ] = SRS_CONSTS_HTTP_Created_str                      ;      
        _status_map[SRS_CONSTS_HTTP_Accepted                       ] = SRS_CONSTS_HTTP_Accepted_str                     ;      
        _status_map[SRS_CONSTS_HTTP_NonAuthoritativeInformation    ] = SRS_CONSTS_HTTP_NonAuthoritativeInformation_str  ;      
        _status_map[SRS_CONSTS_HTTP_NoContent                      ] = SRS_CONSTS_HTTP_NoContent_str                    ;      
        _status_map[SRS_CONSTS_HTTP_ResetContent                   ] = SRS_CONSTS_HTTP_ResetContent_str                 ;      
        _status_map[SRS_CONSTS_HTTP_PartialContent                 ] = SRS_CONSTS_HTTP_PartialContent_str               ;      
        _status_map[SRS_CONSTS_HTTP_MultipleChoices                ] = SRS_CONSTS_HTTP_MultipleChoices_str              ;      
        _status_map[SRS_CONSTS_HTTP_MovedPermanently               ] = SRS_CONSTS_HTTP_MovedPermanently_str             ;      
        _status_map[SRS_CONSTS_HTTP_Found                          ] = SRS_CONSTS_HTTP_Found_str                        ;      
        _status_map[SRS_CONSTS_HTTP_SeeOther                       ] = SRS_CONSTS_HTTP_SeeOther_str                     ;      
        _status_map[SRS_CONSTS_HTTP_NotModified                    ] = SRS_CONSTS_HTTP_NotModified_str                  ;      
        _status_map[SRS_CONSTS_HTTP_UseProxy                       ] = SRS_CONSTS_HTTP_UseProxy_str                     ;      
        _status_map[SRS_CONSTS_HTTP_TemporaryRedirect              ] = SRS_CONSTS_HTTP_TemporaryRedirect_str            ;      
        _status_map[SRS_CONSTS_HTTP_BadRequest                     ] = SRS_CONSTS_HTTP_BadRequest_str                   ;      
        _status_map[SRS_CONSTS_HTTP_Unauthorized                   ] = SRS_CONSTS_HTTP_Unauthorized_str                 ;      
        _status_map[SRS_CONSTS_HTTP_PaymentRequired                ] = SRS_CONSTS_HTTP_PaymentRequired_str              ;      
        _status_map[SRS_CONSTS_HTTP_Forbidden                      ] = SRS_CONSTS_HTTP_Forbidden_str                    ;      
        _status_map[SRS_CONSTS_HTTP_NotFound                       ] = SRS_CONSTS_HTTP_NotFound_str                     ;      
        _status_map[SRS_CONSTS_HTTP_MethodNotAllowed               ] = SRS_CONSTS_HTTP_MethodNotAllowed_str             ;      
        _status_map[SRS_CONSTS_HTTP_NotAcceptable                  ] = SRS_CONSTS_HTTP_NotAcceptable_str                ;      
        _status_map[SRS_CONSTS_HTTP_ProxyAuthenticationRequired    ] = SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str  ;      
        _status_map[SRS_CONSTS_HTTP_RequestTimeout                 ] = SRS_CONSTS_HTTP_RequestTimeout_str               ;      
        _status_map[SRS_CONSTS_HTTP_Conflict                       ] = SRS_CONSTS_HTTP_Conflict_str                     ;      
        _status_map[SRS_CONSTS_HTTP_Gone                           ] = SRS_CONSTS_HTTP_Gone_str                         ;      
        _status_map[SRS_CONSTS_HTTP_LengthRequired                 ] = SRS_CONSTS_HTTP_LengthRequired_str               ;      
        _status_map[SRS_CONSTS_HTTP_PreconditionFailed             ] = SRS_CONSTS_HTTP_PreconditionFailed_str           ;      
        _status_map[SRS_CONSTS_HTTP_RequestEntityTooLarge          ] = SRS_CONSTS_HTTP_RequestEntityTooLarge_str        ;      
        _status_map[SRS_CONSTS_HTTP_RequestURITooLarge             ] = SRS_CONSTS_HTTP_RequestURITooLarge_str           ;      
        _status_map[SRS_CONSTS_HTTP_UnsupportedMediaType           ] = SRS_CONSTS_HTTP_UnsupportedMediaType_str         ;      
        _status_map[SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable   ] = SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str ;      
        _status_map[SRS_CONSTS_HTTP_ExpectationFailed              ] = SRS_CONSTS_HTTP_ExpectationFailed_str            ;      
        _status_map[SRS_CONSTS_HTTP_InternalServerError            ] = SRS_CONSTS_HTTP_InternalServerError_str          ;      
        _status_map[SRS_CONSTS_HTTP_NotImplemented                 ] = SRS_CONSTS_HTTP_NotImplemented_str               ;      
        _status_map[SRS_CONSTS_HTTP_BadGateway                     ] = SRS_CONSTS_HTTP_BadGateway_str                   ;      
        _status_map[SRS_CONSTS_HTTP_ServiceUnavailable             ] = SRS_CONSTS_HTTP_ServiceUnavailable_str           ;      
        _status_map[SRS_CONSTS_HTTP_GatewayTimeout                 ] = SRS_CONSTS_HTTP_GatewayTimeout_str               ;      
        _status_map[SRS_CONSTS_HTTP_HTTPVersionNotSupported        ] = SRS_CONSTS_HTTP_HTTPVersionNotSupported_str      ;      
    }
    
    std::string status_text;
    if (_status_map.find(status) == _status_map.end()) {
        status_text = "Status Unknown";
    } else {
        status_text = _status_map[status];
    }
    
    return status_text;
}

// bodyAllowedForStatus reports whether a given response status code
// permits a body.  See RFC2616, section 4.4.
bool srs_go_http_body_allowd(int status)
{
    if (status >= 100 && status <= 199) {
        return false;
    } else if (status == 204 || status == 304) {
        return false;
    }
    
	return true;
}

// DetectContentType implements the algorithm described
// at http://mimesniff.spec.whatwg.org/ to determine the
// Content-Type of the given data.  It considers at most the
// first 512 bytes of data.  DetectContentType always returns
// a valid MIME type: if it cannot determine a more specific one, it
// returns "application/octet-stream".
string srs_go_http_detect(char* data, int size)
{
    // detect only when data specified.
    if (data) {
    }
    return "application/octet-stream"; // fallback
}

// Error replies to the request with the specified error message and HTTP code.
// The error message should be plain text.
int srs_go_http_error(ISrsHttpResponseWriter* w, int code, string error)
{
    int ret = ERROR_SUCCESS;
    
    w->header()->set_content_type("text/plain; charset=utf-8");
    w->header()->set_content_length(error.length());
    w->write_header(code);
    w->write((char*)error.data(), (int)error.length());
    
    return ret;
}

SrsHttpHeader::SrsHttpHeader()
{
}

SrsHttpHeader::~SrsHttpHeader()
{
}

void SrsHttpHeader::set(string key, string value)
{
    headers[key] = value;
}

string SrsHttpHeader::get(string key)
{
    std::string v;
    
    if (headers.find(key) != headers.end()) {
        v = headers[key];
    }
    
    return v;
}

int64_t SrsHttpHeader::content_length()
{
    std::string cl = get("Content-Length");
    
    if (cl.empty()) {
        return -1;
    }
    
    return (int64_t)::atof(cl.c_str());
}

void SrsHttpHeader::set_content_length(int64_t size)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%"PRId64, size);
    set("Content-Length", buf);
}

string SrsHttpHeader::content_type()
{
    return get("Content-Type");
}

void SrsHttpHeader::set_content_type(string ct)
{
    set("Content-Type", ct);
}

void SrsHttpHeader::write(stringstream& ss)
{
    std::map<std::string, std::string>::iterator it;
    for (it = headers.begin(); it != headers.end(); ++it) {
        ss << it->first << ": " << it->second << __SRS_HTTP_CRLF;
    }
}

ISrsHttpResponseWriter::ISrsHttpResponseWriter()
{
}

ISrsHttpResponseWriter::~ISrsHttpResponseWriter()
{
}

ISrsHttpResponseReader::ISrsHttpResponseReader()
{
}

ISrsHttpResponseReader::~ISrsHttpResponseReader()
{
}

ISrsHttpHandler::ISrsHttpHandler()
{
    entry = NULL;
}

ISrsHttpHandler::~ISrsHttpHandler()
{
}

SrsHttpRedirectHandler::SrsHttpRedirectHandler(string u, int c)
{
    url = u;
    code = c;
}

SrsHttpRedirectHandler::~SrsHttpRedirectHandler()
{
}

int SrsHttpRedirectHandler::serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

SrsHttpNotFoundHandler::SrsHttpNotFoundHandler()
{
}

SrsHttpNotFoundHandler::~SrsHttpNotFoundHandler()
{
}

int SrsHttpNotFoundHandler::serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r)
{
    return srs_go_http_error(w, 
        SRS_CONSTS_HTTP_NotFound, SRS_CONSTS_HTTP_NotFound_str);
}

SrsHttpFileServer::SrsHttpFileServer(string root_dir)
{
    dir = root_dir;
}

SrsHttpFileServer::~SrsHttpFileServer()
{
}

int SrsHttpFileServer::serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r)
{
    string upath = r->path();
    
    // add default pages.
    if (srs_string_ends_with(upath, "/")) {
        upath += SRS_HTTP_DEFAULT_PAGE;
    }

    string fullpath = dir + "/";
    
    // remove the virtual directory.
    srs_assert(entry);
    size_t pos = entry->pattern.find("/");
    if (upath.length() > entry->pattern.length() && pos != string::npos) {
        fullpath += upath.substr(entry->pattern.length() - pos);
    } else {
        fullpath += upath;
    }
    
    // stat current dir, if exists, return error.
    if (!srs_path_exists(fullpath)) {
        srs_warn("http miss file=%s, pattern=%s, upath=%s", 
            fullpath.c_str(), entry->pattern.c_str(), upath.c_str());
        return SrsHttpNotFoundHandler().serve_http(w, r);
    }
    srs_trace("http match file=%s, pattern=%s, upath=%s", 
        fullpath.c_str(), entry->pattern.c_str(), upath.c_str());
    
    // handle file according to its extension.
    // use vod stream for .flv/.fhv
    if (srs_string_ends_with(fullpath, ".flv") || srs_string_ends_with(fullpath, ".fhv")) {
        return serve_flv_file(w, r, fullpath);
    } else if (srs_string_ends_with(fullpath, ".mp4")) {
        return serve_mp4_file(w, r, fullpath);
    }

    // serve common static file.
    return serve_file(w, r, fullpath);
}

int SrsHttpFileServer::serve_file(ISrsHttpResponseWriter* w, SrsHttpMessage* r, string fullpath)
{
    int ret = ERROR_SUCCESS;
    
    // open the target file.
    SrsFileReader fs;
    
    if ((ret = fs.open(fullpath)) != ERROR_SUCCESS) {
        srs_warn("open file %s failed, ret=%d", fullpath.c_str(), ret);
        return ret;
    }

    int64_t length = fs.filesize();
    
    // unset the content length to encode in chunked encoding.
    w->header()->set_content_length(length);
    
    static std::map<std::string, std::string> _mime;
    if (_mime.empty()) {
        _mime[".ts"] = "video/MP2T";
        _mime[".flv"] = "video/x-flv";
        _mime[".m4v"] = "video/x-m4v";
        _mime[".3gpp"] = "video/3gpp";
        _mime[".3gp"] = "video/3gpp";
        _mime[".mp4"] = "video/mp4";
        _mime[".aac"] = "audio/x-aac";
        _mime[".mp3"] = "audio/mpeg";
        _mime[".m4a"] = "audio/x-m4a";
        _mime[".ogg"] = "audio/ogg";
        // @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 5.
        _mime[".m3u8"] = "application/vnd.apple.mpegurl"; // application/x-mpegURL
        _mime[".rss"] = "application/rss+xml";
        _mime[".json"] = "application/json";
        _mime[".swf"] = "application/x-shockwave-flash";
        _mime[".doc"] = "application/msword";
        _mime[".zip"] = "application/zip";
        _mime[".rar"] = "application/x-rar-compressed";
        _mime[".xml"] = "text/xml";
        _mime[".html"] = "text/html";
        _mime[".js"] = "text/javascript";
        _mime[".css"] = "text/css";
        _mime[".ico"] = "image/x-icon";
        _mime[".png"] = "image/png";
        _mime[".jpeg"] = "image/jpeg";
        _mime[".jpg"] = "image/jpeg";
        _mime[".gif"] = "image/gif";
    }
    
    if (true) {
        size_t pos;
        std::string ext = fullpath;
        if ((pos = ext.rfind(".")) != string::npos) {
            ext = ext.substr(pos);
        }
        
        if (_mime.find(ext) == _mime.end()) {
            w->header()->set_content_type("application/octet-stream");
        } else {
            w->header()->set_content_type(_mime[ext]);
        }
    }
    
    // write body.
    int64_t left = length;
    if ((ret = copy(w, &fs, r, left)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("read file=%s size=%d failed, ret=%d", fullpath.c_str(), left, ret);
        }
        return ret;
    }
    
    return w->final_request();
}

int SrsHttpFileServer::serve_flv_file(ISrsHttpResponseWriter* w, SrsHttpMessage* r, string fullpath)
{
    std::string start = r->query_get("start");
    if (start.empty()) {
        return serve_file(w, r, fullpath);
    }

    int offset = ::atoi(start.c_str());
    if (offset <= 0) {
        return serve_file(w, r, fullpath);
    }
        
    return serve_flv_stream(w, r, fullpath, offset);
}

int SrsHttpFileServer::serve_mp4_file(ISrsHttpResponseWriter* w, SrsHttpMessage* r, string fullpath)
{
    // for flash to request mp4 range in query string.
    // for example, http://digitalprimates.net/dash/DashTest.html?url=http://dashdemo.edgesuite.net/digitalprimates/nexus/oops-20120802-manifest.mpd
    std::string range = r->query_get("range");
    // or, use bytes to request range,
    // for example, http://dashas.castlabs.com/demo/try.html
    if (range.empty()) {
        range = r->query_get("bytes");
    }

    // rollback to serve whole file.
    size_t pos = string::npos;
    if (range.empty() || (pos = range.find("-")) == string::npos) {
        return serve_file(w, r, fullpath);
    }

    // parse the start in query string
    int start = 0;
    if (pos > 0) {
        start = ::atoi(range.substr(0, pos).c_str());
    }

    // parse end in query string.
    int end = -1;
    if (pos < range.length() - 1) {
        end = ::atoi(range.substr(pos + 1).c_str());
    }

    // invalid param, serve as whole mp4 file.
    if (start < 0 || (end != -1 && start > end)) {
        return serve_file(w, r, fullpath);
    }
        
    return serve_mp4_stream(w, r, fullpath, start, end);
}

int SrsHttpFileServer::serve_flv_stream(ISrsHttpResponseWriter* w, SrsHttpMessage* r, string fullpath, int offset)
{
    return serve_file(w, r, fullpath);
}

int SrsHttpFileServer::serve_mp4_stream(ISrsHttpResponseWriter* w, SrsHttpMessage* r, string fullpath, int start, int end)
{
    return serve_file(w, r, fullpath);
}

int SrsHttpFileServer::copy(ISrsHttpResponseWriter* w, SrsFileReader* fs, SrsHttpMessage* r, int size)
{
    int ret = ERROR_SUCCESS;
    
    int left = size;
    char* buf = r->http_ts_send_buffer();
    
    while (left > 0) {
        ssize_t nread = -1;
        int max_read = srs_min(left, __SRS_HTTP_TS_SEND_BUFFER_SIZE);
        if ((ret = fs->read(buf, max_read, &nread)) != ERROR_SUCCESS) {
            break;
        }
        
        left -= nread;
        if ((ret = w->write(buf, nread)) != ERROR_SUCCESS) {
            break;
        }
    }
    
    return ret;
}

SrsHttpMuxEntry::SrsHttpMuxEntry()
{
    enabled = true;
    explicit_match = false;
    handler = NULL;
}

SrsHttpMuxEntry::~SrsHttpMuxEntry()
{
    srs_freep(handler);
}

SrsHttpServeMux::SrsHttpServeMux()
{
}

SrsHttpServeMux::~SrsHttpServeMux()
{
    std::map<std::string, SrsHttpMuxEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        SrsHttpMuxEntry* entry = it->second;
        srs_freep(entry);
    }
    entries.clear();
    
    vhosts.clear();
}

int SrsHttpServeMux::initialize()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

int SrsHttpServeMux::handle(std::string pattern, ISrsHttpHandler* handler)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(handler);
    
    if (pattern.empty()) {
        ret = ERROR_HTTP_PATTERN_EMPTY;
        srs_error("http: empty pattern. ret=%d", ret);
        return ret;
    }
    
    if (entries.find(pattern) != entries.end()) {
        SrsHttpMuxEntry* exists = entries[pattern];
        if (exists->explicit_match) {
            ret = ERROR_HTTP_PATTERN_DUPLICATED;
            srs_error("http: multiple registrations for %s. ret=%d", pattern.c_str(), ret);
            return ret;
        }
    }
    
    std::string vhost = pattern;
    if (pattern.at(0) != '/') {
        if (pattern.find("/") != string::npos) {
            vhost = pattern.substr(0, pattern.find("/"));
        }
        vhosts[vhost] = handler;
    }
    
    if (true) {
        SrsHttpMuxEntry* entry = new SrsHttpMuxEntry();
        entry->explicit_match = true;
        entry->handler = handler;
        entry->pattern = pattern;
        entry->handler->entry = entry;
        
        if (entries.find(pattern) != entries.end()) {
            SrsHttpMuxEntry* exists = entries[pattern];
            srs_freep(exists);
        }
        entries[pattern] = entry;
    }

    // Helpful behavior:
    // If pattern is /tree/, insert an implicit permanent redirect for /tree.
    // It can be overridden by an explicit registration.
    if (pattern != "/" && !pattern.empty() && pattern.at(pattern.length() - 1) == '/') {
        std::string rpattern = pattern.substr(0, pattern.length() - 1);
        SrsHttpMuxEntry* entry = NULL;
        
        // free the exists not explicit entry
        if (entries.find(rpattern) != entries.end()) {
            SrsHttpMuxEntry* exists = entries[rpattern];
            if (!exists->explicit_match) {
                entry = exists;
            }
        }

        // create implicit redirect.
        if (!entry || entry->explicit_match) {
            srs_freep(entry);
            
            entry = new SrsHttpMuxEntry();
            entry->explicit_match = false;
            entry->handler = new SrsHttpRedirectHandler(pattern, SRS_CONSTS_HTTP_MovedPermanently);
            entry->pattern = pattern;
            entry->handler->entry = entry;
            
            entries[rpattern] = entry;
        }
    }
    
    return ret;
}

int SrsHttpServeMux::serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    ISrsHttpHandler* h = NULL;
    if ((ret = find_handler(r, &h)) != ERROR_SUCCESS) {
        srs_error("find handler failed. ret=%d", ret);
        return ret;
    }
    
    srs_assert(h);
    if ((ret = h->serve_http(w, r)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("handler serve http failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

int SrsHttpServeMux::find_handler(SrsHttpMessage* r, ISrsHttpHandler** ph)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: support the path . and ..
    if (r->url().find("..") != std::string::npos) {
        ret = ERROR_HTTP_URL_NOT_CLEAN;
        srs_error("htt url not canonical, url=%s. ret=%d", r->url().c_str(), ret);
        return ret;
    }
    
    if ((ret = match(r, ph)) != ERROR_SUCCESS) {
        srs_error("http match handler failed. ret=%d", ret);
        return ret;
    }

    if (*ph == NULL) {
        *ph = new SrsHttpNotFoundHandler();
    }
    
    return ret;
}

int SrsHttpServeMux::match(SrsHttpMessage* r, ISrsHttpHandler** ph)
{
    int ret = ERROR_SUCCESS;
    
    std::string path = r->path();
    
    // Host-specific pattern takes precedence over generic ones
    if (!vhosts.empty() && vhosts.find(r->host()) != vhosts.end()) {
        path = r->host() + path;
    }
    
    int nb_matched = 0;
    ISrsHttpHandler* h = NULL;
    
    std::map<std::string, SrsHttpMuxEntry*>::iterator it;
    for (it = entries.begin(); it != entries.end(); ++it) {
        std::string pattern = it->first;
        SrsHttpMuxEntry* entry = it->second;
        
        if (!entry->enabled) {
            continue;
        }
        
        if (!path_match(pattern, path)) {
            continue;
        }
        
        if (!h || (int)pattern.length() > nb_matched) {
            nb_matched = (int)pattern.length();
            h = entry->handler;
        }
    }
    
    *ph = h;
    
    return ret;
}

bool SrsHttpServeMux::path_match(string pattern, string path)
{
    if (pattern.empty()) {
        return false;
    }
    
    int n = pattern.length();
    
    // not endswith '/', exactly match.
    if (pattern.at(n - 1) != '/') {
        return pattern == path;
    }
    
    // endswith '/', match any,
    // for example, '/api/' match '/api/[N]'
    if ((int)path.length() >= n) {
        if (memcmp(pattern.data(), path.data(), n) == 0) {
            return true;
        }
    }
    
    return false;
}

SrsHttpResponseWriter::SrsHttpResponseWriter(SrsStSocket* io)
{
    skt = io;
    hdr = new SrsHttpHeader();
    header_wrote = false;
    status = SRS_CONSTS_HTTP_OK;
    content_length = -1;
    written = 0;
    header_sent = false;
}

SrsHttpResponseWriter::~SrsHttpResponseWriter()
{
    srs_freep(hdr);
}

int SrsHttpResponseWriter::final_request()
{
    // complete the chunked encoding.
    if (content_length == -1) {
        std::stringstream ss;
        ss << 0 << __SRS_HTTP_CRLF << __SRS_HTTP_CRLF;
        std::string ch = ss.str();
        return skt->write((void*)ch.data(), (int)ch.length(), NULL);
    }
    
    // flush when send with content length
    return write(NULL, 0);
}

SrsHttpHeader* SrsHttpResponseWriter::header()
{
    return hdr;
}

int SrsHttpResponseWriter::write(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (!header_wrote) {
        write_header(SRS_CONSTS_HTTP_OK);
    }
    
    written += size;
    if (content_length != -1 && written > content_length) {
        ret = ERROR_HTTP_CONTENT_LENGTH;
        srs_error("http: exceed content length. ret=%d", ret);
        return ret;
    }
    
    if ((ret = send_header(data, size)) != ERROR_SUCCESS) {
        srs_error("http: send header failed. ret=%d", ret);
        return ret;
    }

    // ignore NULL content.
    if (!data) {
        return ret;
    }
    
    // directly send with content length
    if (content_length != -1) {
        return skt->write((void*)data, size, NULL);
    }
    
    // send in chunked encoding.
    std::stringstream ss;
    ss << hex << size << __SRS_HTTP_CRLF;
    std::string ch = ss.str();
    if ((ret = skt->write((void*)ch.data(), (int)ch.length(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = skt->write((void*)data, size, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = skt->write((void*)__SRS_HTTP_CRLF, 2, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsHttpResponseWriter::write_header(int code)
{
    if (header_wrote) {
        srs_warn("http: multiple write_header calls, code=%d", code);
        return;
    }
    
    header_wrote = true;
    status = code;
    
    // parse the content length from header.
    content_length = hdr->content_length();
}

int SrsHttpResponseWriter::send_header(char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (header_sent) {
        return ret;
    }
    header_sent = true;
    
    std::stringstream ss;
    
    // status_line
    ss << "HTTP/1.1 " << status << " " 
        << srs_generate_http_status_text(status) << __SRS_HTTP_CRLF;
        
    // detect content type
    if (srs_go_http_body_allowd(status)) {
        if (hdr->content_type().empty()) {
            hdr->set_content_type(srs_go_http_detect(data, size));
        }
    }
    
    // set server if not set.
    if (hdr->get("Server").empty()) {
        hdr->set("Server", RTMP_SIG_SRS_KEY"/"RTMP_SIG_SRS_VERSION);
    }
    
    // chunked encoding
    if (content_length == -1) {
        hdr->set("Transfer-Encoding", "chunked");
    }

    // keep alive to make vlc happy.
    hdr->set("Connection", "Keep-Alive");
    
    // write headers
    hdr->write(ss);
    
    // header_eof
    ss << __SRS_HTTP_CRLF;
    
    std::string buf = ss.str();
    return skt->write((void*)buf.c_str(), buf.length(), NULL);
}

SrsHttpResponseReader::SrsHttpResponseReader(SrsHttpMessage* msg, SrsStSocket* io)
{
    skt = io;
    owner = msg;
    is_eof = false;
    nb_read = 0;
    buffer = NULL;
}

SrsHttpResponseReader::~SrsHttpResponseReader()
{
}

int SrsHttpResponseReader::initialize(SrsFastBuffer* body)
{
    int ret = ERROR_SUCCESS;
    
    buffer = body;
    
    return ret;
}

bool SrsHttpResponseReader::eof()
{
    return is_eof;
}

int SrsHttpResponseReader::read(std::string& data)
{
    int ret = ERROR_SUCCESS;
    
    if (is_eof) {
        ret = ERROR_HTTP_RESPONSE_EOF;
        srs_error("http: response EOF. ret=%d", ret);
        return ret;
    }
    
    // chunked encoding.
    if (owner->is_chunked()) {
        return read_chunked(data);
    }
    
    // read by specified content-length
    int max = (int)owner->content_length() - nb_read;
    if (max <= 0) {
        is_eof = true;
        return ret;
    }
    return read_specified(max, data);
}

int SrsHttpResponseReader::read_chunked(std::string& data)
{
    int ret = ERROR_SUCCESS;
    
    // parse the chunk length first.
    char* at = NULL;
    int length = 0;
    while (!at) {
        // find the CRLF of chunk header end.
        char* start = buffer->bytes();
        char* end = start + buffer->size();
        for (char* p = start; p < end - 1; p++) {
            if (p[0] == __SRS_HTTP_CR && p[1] == __SRS_HTTP_LF) {
                // invalid chunk, ignore.
                if (p == start) {
                    ret = ERROR_HTTP_INVALID_CHUNK_HEADER;
                    srs_error("chunk header start with CRLF. ret=%d", ret);
                    return ret;
                }
                length = p - start + 2;
                at = buffer->read_slice(length);
                break;
            }
        }
        
        // got at, ok.
        if (at) {
            break;
        }
        
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((ret = buffer->grow(skt, buffer->size() + 1)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("read body from server failed. ret=%d", ret);
            }
            return ret;
        }
    }
    srs_assert(length >= 3);
    
    // it's ok to set the pos and pos+1 to NULL.
    at[length - 1] = NULL;
    at[length - 2] = NULL;
    
    // size is the bytes size, excludes the chunk header and end CRLF.
    int ilength = ::strtol(at, NULL, 16);
    if (ilength < 0) {
        ret = ERROR_HTTP_INVALID_CHUNK_HEADER;
        srs_error("chunk header negative, length=%d. ret=%d", ilength, ret);
        return ret;
    }
    
    // when empty, only grow 1bytes, but the buffer will cache more.
    if ((ret = buffer->grow(skt, ilength + 2)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("read body from server failed. ret=%d", ret);
        }
        return ret;
    }
    srs_info("http: read %d chunk", ilength);
    
    // read payload when length specifies some payload.
    if (ilength <= 0) {
        is_eof = true;
    } else {
        srs_assert(ilength);
        data.append(buffer->read_slice(ilength), ilength);
        nb_read += ilength;
    }
    
    // the CRLF of chunk payload end.
    buffer->read_slice(2);

    return ret;
}

int SrsHttpResponseReader::read_specified(int max, std::string& data)
{
    int ret = ERROR_SUCCESS;
    
    if (buffer->size() <= 0) {
        // when empty, only grow 1bytes, but the buffer will cache more.
        if ((ret = buffer->grow(skt, 1)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("read body from server failed. ret=%d", ret);
            }
            return ret;
        }
    }
    
    int nb_bytes = srs_min(max, buffer->size());
    
    srs_assert(nb_bytes);
    data.append(buffer->read_slice(nb_bytes), nb_bytes);
    nb_read += nb_bytes;
    
    // when read completed, eof.
    if (nb_read >= (int)owner->content_length()) {
        is_eof = true;
    }
    
    return ret;
}

SrsHttpMessage::SrsHttpMessage(SrsStSocket* io)
{
    chunked = false;
    _uri = new SrsHttpUri();
    _body = new SrsHttpResponseReader(this, io);
    _http_ts_send_buffer = new char[__SRS_HTTP_TS_SEND_BUFFER_SIZE];
}

SrsHttpMessage::~SrsHttpMessage()
{
    srs_freep(_body);
    srs_freep(_uri);
    srs_freep(_http_ts_send_buffer);
}

int SrsHttpMessage::update(string url, http_parser* header, SrsFastBuffer* body, vector<SrsHttpHeaderField>& headers)
{
    int ret = ERROR_SUCCESS;

    _url = url;
    _header = *header;
    _headers = headers;

    // whether chunked.
    std::string transfer_encoding = get_request_header("Transfer-Encoding");
    chunked = (transfer_encoding == "chunked");
    
    // set the buffer.
    if ((ret = _body->initialize(body)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // parse uri from url.
    std::string host = get_request_header("Host");

    // donot parse the empty host for uri, 
    // for example, the response contains no host,
    // ignore it is ok.
    if (host.empty()) {
        return ret;
    }
    
    // parse uri to schema/server:port/path?query
    std::string uri = "http://" + host + _url;
    if ((ret = _uri->initialize(uri)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // must format as key=value&...&keyN=valueN
    std::string q = _uri->get_query();
    size_t pos = string::npos;
    while (!q.empty()) {
        std::string k = q;
        if ((pos = q.find("=")) != string::npos) {
            k = q.substr(0, pos);
            q = q.substr(pos + 1);
        } else {
            q = "";
        }
        
        std::string v = q;
        if ((pos = q.find("&")) != string::npos) {
            v = q.substr(0, pos);
            q = q.substr(pos + 1);
        } else {
            q = "";
        }
        
        _query[k] = v;
    }
    
    return ret;
}

char* SrsHttpMessage::http_ts_send_buffer()
{
    return _http_ts_send_buffer;
}

u_int8_t SrsHttpMessage::method()
{
    return (u_int8_t)_header.method;
}

u_int16_t SrsHttpMessage::status_code()
{
    return (u_int16_t)_header.status_code;
}

string SrsHttpMessage::method_str()
{
    if (is_http_get()) {
        return "GET";
    }
    if (is_http_put()) {
        return "PUT";
    }
    if (is_http_post()) {
        return "POST";
    }
    if (is_http_delete()) {
        return "DELETE";
    }
    if (is_http_options()) {
        return "OPTIONS";
    }
    
    return "OTHER";
}

bool SrsHttpMessage::is_http_get()
{
    return _header.method == SRS_CONSTS_HTTP_GET;
}

bool SrsHttpMessage::is_http_put()
{
    return _header.method == SRS_CONSTS_HTTP_PUT;
}

bool SrsHttpMessage::is_http_post()
{
    return _header.method == SRS_CONSTS_HTTP_POST;
}

bool SrsHttpMessage::is_http_delete()
{
    return _header.method == SRS_CONSTS_HTTP_DELETE;
}

bool SrsHttpMessage::is_http_options()
{
    return _header.method == SRS_CONSTS_HTTP_OPTIONS;
}

bool SrsHttpMessage::is_chunked()
{
    return chunked;
}

string SrsHttpMessage::uri()
{
    std::string uri = _uri->get_schema();
    if (uri.empty()) {
        uri += "http://";
    }
    
    uri += host();
    uri += path();
    return uri;
}

string SrsHttpMessage::url()
{
    return _uri->get_url();
}

string SrsHttpMessage::host()
{
    return _uri->get_host();
}

string SrsHttpMessage::path()
{
    return _uri->get_path();
}

int SrsHttpMessage::body_read_all(string& body)
{
    int ret = ERROR_SUCCESS;
    
    // whatever, read util EOF.
    while (!_body->eof()) {
        if ((ret = _body->read(body)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

ISrsHttpResponseReader* SrsHttpMessage::body_reader()
{
    return _body;
}

int64_t SrsHttpMessage::content_length()
{
    return _header.content_length;
}

string SrsHttpMessage::query_get(string key)
{
    std::string v;
    
    if (_query.find(key) != _query.end()) {
        v = _query[key];
    }
    
    return v;
}

int SrsHttpMessage::request_header_count()
{
    return (int)_headers.size();
}

string SrsHttpMessage::request_header_key_at(int index)
{
    srs_assert(index < request_header_count());
    SrsHttpHeaderField item = _headers[index];
    return item.first;
}

string SrsHttpMessage::request_header_value_at(int index)
{
    srs_assert(index < request_header_count());
    SrsHttpHeaderField item = _headers[index];
    return item.second;
}

string SrsHttpMessage::get_request_header(string name)
{
    std::vector<SrsHttpHeaderField>::iterator it;
    
    for (it = _headers.begin(); it != _headers.end(); ++it) {
        SrsHttpHeaderField& elem = *it;
        std::string key = elem.first;
        std::string value = elem.second;
        if (key == name) {
            return value;
        }
    }
    
    return "";
}

SrsHttpParser::SrsHttpParser()
{
    buffer = new SrsFastBuffer();
}

SrsHttpParser::~SrsHttpParser()
{
    srs_freep(buffer);
}

int SrsHttpParser::initialize(enum http_parser_type type)
{
    int ret = ERROR_SUCCESS;
    
    memset(&settings, 0, sizeof(settings));
    settings.on_message_begin = on_message_begin;
    settings.on_url = on_url;
    settings.on_header_field = on_header_field;
    settings.on_header_value = on_header_value;
    settings.on_headers_complete = on_headers_complete;
    settings.on_body = on_body;
    settings.on_message_complete = on_message_complete;
    
    http_parser_init(&parser, type);
    // callback object ptr.
    parser.data = (void*)this;
    
    return ret;
}

int SrsHttpParser::parse_message(SrsStSocket* skt, SrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    // reset request data.
    filed_name = "";
    field_value = "";
    expect_filed_name = true;
    state = SrsHttpParseStateInit;
    header = http_parser();
    url = "";
    headers.clear();
    body_parsed = 0;
    
    // do parse
    if ((ret = parse_message_imp(skt)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("parse http msg failed. ret=%d", ret);
        }
        return ret;
    }

    // create msg
    SrsHttpMessage* msg = new SrsHttpMessage(skt);
    
    // initalize http msg, parse url.
    if ((ret = msg->update(url, &header, buffer, headers)) != ERROR_SUCCESS) {
        srs_error("initialize http msg failed. ret=%d", ret);
        srs_freep(msg);
        return ret;
    }
    
    // parse ok, return the msg.
    *ppmsg = msg;
    
    return ret;
}

int SrsHttpParser::parse_message_imp(SrsStSocket* skt)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        ssize_t nparsed = 0;
        
        // when buffer not empty, parse it.
        if (buffer->size() > 0) {
            nparsed = http_parser_execute(&parser, &settings, buffer->bytes(), buffer->size());
            srs_info("buffer=%d, nparsed=%d, body=%d", buffer->size(), (int)nparsed, body_parsed);
        }
        
        // consume the parsed bytes.
        if (nparsed && nparsed - body_parsed > 0) {
            buffer->read_slice(nparsed - body_parsed);
        }

        // ok atleast header completed,
        // never wait for body completed, for maybe chunked.
        if (state == SrsHttpParseStateHeaderComplete || state == SrsHttpParseStateMessageComplete) {
            break;
        }
        
        // when nothing parsed, read more to parse.
        if (nparsed == 0) {
            // when requires more, only grow 1bytes, but the buffer will cache more.
            if ((ret = buffer->grow(skt, buffer->size() + 1)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("read body from server failed. ret=%d", ret);
                }
                return ret;
            }
        }
    }

    // parse last header.
    if (!filed_name.empty() && !field_value.empty()) {
        headers.push_back(std::make_pair(filed_name, field_value));
    }

    return ret;
}

int SrsHttpParser::on_message_begin(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    obj->state = SrsHttpParseStateStart;
    
    srs_info("***MESSAGE BEGIN***");
    
    return 0;
}

int SrsHttpParser::on_headers_complete(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    obj->header = *parser;
    // save the parser when header parse completed.
    obj->state = SrsHttpParseStateHeaderComplete;
    
    srs_info("***HEADERS COMPLETE***");
    
    // see http_parser.c:1570, return 1 to skip body.
    return 0;
}

int SrsHttpParser::on_message_complete(http_parser* parser)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    // save the parser when body parse completed.
    obj->state = SrsHttpParseStateMessageComplete;
    
    srs_info("***MESSAGE COMPLETE***\n");
    
    return 0;
}

int SrsHttpParser::on_url(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    if (length > 0) {
        obj->url.append(at, (int)length);
    }
    
    srs_info("Method: %d, Url: %.*s", parser->method, (int)length, at);
    
    return 0;
}

int SrsHttpParser::on_header_field(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    // field value=>name, reap the field.
    if (!obj->expect_filed_name) {
        obj->headers.push_back(std::make_pair(obj->filed_name, obj->field_value));
        
        // reset the field name when parsed.
        obj->filed_name = "";
        obj->field_value = "";
    }
    obj->expect_filed_name = true;
    
    if (length > 0) {
        obj->filed_name.append(at, (int)length);
    }
    
    srs_info("Header field(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_header_value(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    if (length > 0) {
        obj->field_value.append(at, (int)length);
    }
    obj->expect_filed_name = false;
    
    srs_info("Header value(%d bytes): %.*s", (int)length, (int)length, at);
    return 0;
}

int SrsHttpParser::on_body(http_parser* parser, const char* at, size_t length)
{
    SrsHttpParser* obj = (SrsHttpParser*)parser->data;
    srs_assert(obj);
    
    obj->body_parsed += length;
    
    srs_info("Body: %.*s", (int)length, at);

    return 0;
}

SrsHttpUri::SrsHttpUri()
{
    port = SRS_DEFAULT_HTTP_PORT;
}

SrsHttpUri::~SrsHttpUri()
{
}

int SrsHttpUri::initialize(string _url)
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
    
    query = get_uri_field(url, &hp_u, UF_QUERY);
    srs_info("parse query %s success", query.c_str());
    
    return ret;
}

const char* SrsHttpUri::get_url()
{
    return url.data();
}

const char* SrsHttpUri::get_schema()
{
    return schema.data();
}

const char* SrsHttpUri::get_host()
{
    return host.data();
}

int SrsHttpUri::get_port()
{
    return port;
}

const char* SrsHttpUri::get_path()
{
    return path.data();
}

const char* SrsHttpUri::get_query()
{
    return query.data();
}

string SrsHttpUri::get_uri_field(string uri, http_parser_url* hp_u, http_parser_url_fields field)
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

#endif

