/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2015 SRS(ossrs)
 
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

#include <srs_http_stack.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

#include <stdlib.h>
#include <sstream>
#include <algorithm>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_protocol_json.hpp>

#define SRS_HTTP_DEFAULT_PAGE "index.html"

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

int srs_go_http_error(ISrsHttpResponseWriter* w, int code)
{
    return srs_go_http_error(w, code, srs_generate_http_status_text(code));
}

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
        ss << it->first << ": " << it->second << SRS_HTTP_CRLF;
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

bool ISrsHttpHandler::is_not_found()
{
    return false;
}

SrsHttpRedirectHandler::SrsHttpRedirectHandler(string u, int c)
{
    url = u;
    code = c;
}

SrsHttpRedirectHandler::~SrsHttpRedirectHandler()
{
}

int SrsHttpRedirectHandler::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    string location = url;
    if (!r->query().empty()) {
        location += "?" + r->query();
    }
    
    string msg = "Redirect to" + location;

    w->header()->set_content_type("text/plain; charset=utf-8");
    w->header()->set_content_length(msg.length());
    w->header()->set("Location", location);
    w->write_header(code);

    w->write((char*)msg.data(), (int)msg.length());
    w->final_request();

    srs_info("redirect to %s.", location.c_str());
    return ret;
}

SrsHttpNotFoundHandler::SrsHttpNotFoundHandler()
{
}

SrsHttpNotFoundHandler::~SrsHttpNotFoundHandler()
{
}

bool SrsHttpNotFoundHandler::is_not_found()
{
    return true;
}

int SrsHttpNotFoundHandler::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound);
}

SrsHttpFileServer::SrsHttpFileServer(string root_dir)
{
    dir = root_dir;
}

SrsHttpFileServer::~SrsHttpFileServer()
{
}

int SrsHttpFileServer::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
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

int SrsHttpFileServer::serve_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath)
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
    if ((ret = copy(w, &fs, r, (int)left)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("read file=%s size=%d failed, ret=%d", fullpath.c_str(), left, ret);
        }
        return ret;
    }
    
    return w->final_request();
}

int SrsHttpFileServer::serve_flv_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath)
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

int SrsHttpFileServer::serve_mp4_file(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath)
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

int SrsHttpFileServer::serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int offset)
{
    return serve_file(w, r, fullpath);
}

int SrsHttpFileServer::serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, string fullpath, int start, int end)
{
    return serve_file(w, r, fullpath);
}

int SrsHttpFileServer::copy(ISrsHttpResponseWriter* w, SrsFileReader* fs, ISrsHttpMessage* r, int size)
{
    int ret = ERROR_SUCCESS;
    
    int left = size;
    char* buf = r->http_ts_send_buffer();
    
    while (left > 0) {
        ssize_t nread = -1;
        int max_read = srs_min(left, SRS_HTTP_TS_SEND_BUFFER_SIZE);
        if ((ret = fs->read(buf, max_read, &nread)) != ERROR_SUCCESS) {
            break;
        }
        
        left -= nread;
        if ((ret = w->write(buf, (int)nread)) != ERROR_SUCCESS) {
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

ISrsHttpMatchHijacker::ISrsHttpMatchHijacker()
{
}

ISrsHttpMatchHijacker::~ISrsHttpMatchHijacker()
{
}

ISrsHttpServeMux::ISrsHttpServeMux()
{
}

ISrsHttpServeMux::~ISrsHttpServeMux()
{
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
    hijackers.clear();
}

int SrsHttpServeMux::initialize()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

void SrsHttpServeMux::hijack(ISrsHttpMatchHijacker* h)
{
    std::vector<ISrsHttpMatchHijacker*>::iterator it = ::find(hijackers.begin(), hijackers.end(), h);
    if (it != hijackers.end()) {
        return;
    }
    hijackers.push_back(h);
}

void SrsHttpServeMux::unhijack(ISrsHttpMatchHijacker* h)
{
    std::vector<ISrsHttpMatchHijacker*>::iterator it = ::find(hijackers.begin(), hijackers.end(), h);
    if (it == hijackers.end()) {
        return;
    }
    hijackers.erase(it);
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
            entry->handler = new SrsHttpRedirectHandler(pattern, SRS_CONSTS_HTTP_Found);
            entry->pattern = pattern;
            entry->handler->entry = entry;
            
            entries[rpattern] = entry;
        }
    }
    
    return ret;
}

bool SrsHttpServeMux::can_serve(ISrsHttpMessage* r)
{
    int ret = ERROR_SUCCESS;
    
    ISrsHttpHandler* h = NULL;
    if ((ret = find_handler(r, &h)) != ERROR_SUCCESS) {
        return false;
    }
    
    srs_assert(h);
    return !h->is_not_found();
}

int SrsHttpServeMux::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
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

int SrsHttpServeMux::find_handler(ISrsHttpMessage* r, ISrsHttpHandler** ph)
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
    
    // always hijack.
    if (!hijackers.empty()) {
        // notice all hijacker the match failed.
        std::vector<ISrsHttpMatchHijacker*>::iterator it;
        for (it = hijackers.begin(); it != hijackers.end(); ++it) {
            ISrsHttpMatchHijacker* hijacker = *it;
            if ((ret = hijacker->hijack(r, ph)) != ERROR_SUCCESS) {
                srs_error("hijacker match failed. ret=%d", ret);
                return ret;
            }
        }
    }
    
    static ISrsHttpHandler* h404 = new SrsHttpNotFoundHandler();
    if (*ph == NULL) {
        *ph = h404;
    }
    
    return ret;
}

int SrsHttpServeMux::match(ISrsHttpMessage* r, ISrsHttpHandler** ph)
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
    
    int n = (int)pattern.length();
    
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

ISrsHttpMessage::ISrsHttpMessage()
{
    _http_ts_send_buffer = new char[SRS_HTTP_TS_SEND_BUFFER_SIZE];
}

ISrsHttpMessage::~ISrsHttpMessage()
{
    srs_freepa(_http_ts_send_buffer);
}

char* ISrsHttpMessage::http_ts_send_buffer()
{
    return _http_ts_send_buffer;
}

#endif

