/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2017 SRS(ossrs)
 
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
        _status_map[SRS_CONSTS_HTTP_Continue] = SRS_CONSTS_HTTP_Continue_str;
        _status_map[SRS_CONSTS_HTTP_SwitchingProtocols] = SRS_CONSTS_HTTP_SwitchingProtocols_str;
        _status_map[SRS_CONSTS_HTTP_OK] = SRS_CONSTS_HTTP_OK_str;
        _status_map[SRS_CONSTS_HTTP_Created] = SRS_CONSTS_HTTP_Created_str;
        _status_map[SRS_CONSTS_HTTP_Accepted] = SRS_CONSTS_HTTP_Accepted_str;
        _status_map[SRS_CONSTS_HTTP_NonAuthoritativeInformation] = SRS_CONSTS_HTTP_NonAuthoritativeInformation_str;
        _status_map[SRS_CONSTS_HTTP_NoContent] = SRS_CONSTS_HTTP_NoContent_str;
        _status_map[SRS_CONSTS_HTTP_ResetContent] = SRS_CONSTS_HTTP_ResetContent_str;
        _status_map[SRS_CONSTS_HTTP_PartialContent] = SRS_CONSTS_HTTP_PartialContent_str;
        _status_map[SRS_CONSTS_HTTP_MultipleChoices] = SRS_CONSTS_HTTP_MultipleChoices_str;
        _status_map[SRS_CONSTS_HTTP_MovedPermanently] = SRS_CONSTS_HTTP_MovedPermanently_str;
        _status_map[SRS_CONSTS_HTTP_Found] = SRS_CONSTS_HTTP_Found_str;
        _status_map[SRS_CONSTS_HTTP_SeeOther] = SRS_CONSTS_HTTP_SeeOther_str;
        _status_map[SRS_CONSTS_HTTP_NotModified] = SRS_CONSTS_HTTP_NotModified_str;
        _status_map[SRS_CONSTS_HTTP_UseProxy] = SRS_CONSTS_HTTP_UseProxy_str;
        _status_map[SRS_CONSTS_HTTP_TemporaryRedirect] = SRS_CONSTS_HTTP_TemporaryRedirect_str;
        _status_map[SRS_CONSTS_HTTP_BadRequest] = SRS_CONSTS_HTTP_BadRequest_str;
        _status_map[SRS_CONSTS_HTTP_Unauthorized] = SRS_CONSTS_HTTP_Unauthorized_str;
        _status_map[SRS_CONSTS_HTTP_PaymentRequired] = SRS_CONSTS_HTTP_PaymentRequired_str;
        _status_map[SRS_CONSTS_HTTP_Forbidden] = SRS_CONSTS_HTTP_Forbidden_str;
        _status_map[SRS_CONSTS_HTTP_NotFound] = SRS_CONSTS_HTTP_NotFound_str;
        _status_map[SRS_CONSTS_HTTP_MethodNotAllowed] = SRS_CONSTS_HTTP_MethodNotAllowed_str;
        _status_map[SRS_CONSTS_HTTP_NotAcceptable] = SRS_CONSTS_HTTP_NotAcceptable_str;
        _status_map[SRS_CONSTS_HTTP_ProxyAuthenticationRequired] = SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str;
        _status_map[SRS_CONSTS_HTTP_RequestTimeout] = SRS_CONSTS_HTTP_RequestTimeout_str;
        _status_map[SRS_CONSTS_HTTP_Conflict] = SRS_CONSTS_HTTP_Conflict_str;
        _status_map[SRS_CONSTS_HTTP_Gone] = SRS_CONSTS_HTTP_Gone_str;
        _status_map[SRS_CONSTS_HTTP_LengthRequired] = SRS_CONSTS_HTTP_LengthRequired_str;
        _status_map[SRS_CONSTS_HTTP_PreconditionFailed] = SRS_CONSTS_HTTP_PreconditionFailed_str;
        _status_map[SRS_CONSTS_HTTP_RequestEntityTooLarge] = SRS_CONSTS_HTTP_RequestEntityTooLarge_str;
        _status_map[SRS_CONSTS_HTTP_RequestURITooLarge] = SRS_CONSTS_HTTP_RequestURITooLarge_str;
        _status_map[SRS_CONSTS_HTTP_UnsupportedMediaType] = SRS_CONSTS_HTTP_UnsupportedMediaType_str;
        _status_map[SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable] = SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str;
        _status_map[SRS_CONSTS_HTTP_ExpectationFailed] = SRS_CONSTS_HTTP_ExpectationFailed_str;
        _status_map[SRS_CONSTS_HTTP_InternalServerError] = SRS_CONSTS_HTTP_InternalServerError_str;
        _status_map[SRS_CONSTS_HTTP_NotImplemented] = SRS_CONSTS_HTTP_NotImplemented_str;
        _status_map[SRS_CONSTS_HTTP_BadGateway] = SRS_CONSTS_HTTP_BadGateway_str;
        _status_map[SRS_CONSTS_HTTP_ServiceUnavailable] = SRS_CONSTS_HTTP_ServiceUnavailable_str;
        _status_map[SRS_CONSTS_HTTP_GatewayTimeout] = SRS_CONSTS_HTTP_GatewayTimeout_str;
        _status_map[SRS_CONSTS_HTTP_HTTPVersionNotSupported] = SRS_CONSTS_HTTP_HTTPVersionNotSupported_str;
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
    set("Content-Length", srs_int2str(size));
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
        std::string ext = srs_path_filext(fullpath);
        
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

SrsHttpCorsMux::SrsHttpCorsMux()
{
    next = NULL;
    enabled = false;
    required = false;
}

SrsHttpCorsMux::~SrsHttpCorsMux()
{
}

int SrsHttpCorsMux::initialize(ISrsHttpServeMux* worker, bool cros_enabled)
{
    next = worker;
    enabled = cros_enabled;
    
    return ERROR_SUCCESS;
}

int SrsHttpCorsMux::serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    // method is OPTIONS and enable crossdomain, required crossdomain header.
    if (r->is_http_options() && enabled) {
        required = true;
    }
    
    // whenever crossdomain required, set crossdomain header.
    if (required) {
        w->header()->set("Access-Control-Allow-Origin", "*");
        w->header()->set("Access-Control-Allow-Methods", "GET, POST, HEAD, PUT, DELETE");
        w->header()->set("Access-Control-Allow-Headers", "Cache-Control,X-Proxy-Authorization,X-Requested-With,Content-Type");
    }
    
    // handle the http options.
    if (r->is_http_options()) {
        w->header()->set_content_length(0);
        if (enabled) {
            w->write_header(SRS_CONSTS_HTTP_OK);
        } else {
            w->write_header(SRS_CONSTS_HTTP_MethodNotAllowed);
        }
        return w->final_request();
    }
    
    srs_assert(next);
    return next->serve_http(w, r);
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

/* Based on src/http/ngx_http_parse.c from NGINX copyright Igor Sysoev
*
* Additional changes are licensed under the same terms as NGINX and
* copyright Joyent, Inc. and other Node contributors. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to
* deal in the Software without restriction, including without limitation the
* rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
* sell copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/
//#include "http_parser.h"
#include <assert.h>
#include <stddef.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef ULLONG_MAX
# define ULLONG_MAX ((uint64_t) -1) /* 2^64-1 */
#endif

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef BIT_AT
# define BIT_AT(a, i)                                                \
(!!((unsigned int) (a)[(unsigned int) (i) >> 3] &                  \
(1 << ((unsigned int) (i) & 7))))
#endif

#ifndef ELEM_AT
# define ELEM_AT(a, i, v) ((unsigned int) (i) < ARRAY_SIZE(a) ? (a)[(i)] : (v))
#endif

#define SET_ERRNO(e)                                                 \
do {                                                                 \
parser->http_errno = (e);                                          \
} while(0)


/* Run the notify callback FOR, returning ER if it fails */
#define CALLBACK_NOTIFY_(FOR, ER)                                    \
do {                                                                 \
assert(HTTP_PARSER_ERRNO(parser) == HPE_OK);                       \
\
if (settings->on_##FOR) {                                          \
if (0 != settings->on_##FOR(parser)) {                           \
SET_ERRNO(HPE_CB_##FOR);                                       \
}                                                                \
\
/* We either errored above or got paused; get out */             \
if (HTTP_PARSER_ERRNO(parser) != HPE_OK) {                       \
return (ER);                                                   \
}                                                                \
}                                                                  \
} while (0)

/* Run the notify callback FOR and consume the current byte */
#define CALLBACK_NOTIFY(FOR)            CALLBACK_NOTIFY_(FOR, p - data + 1)

/* Run the notify callback FOR and don't consume the current byte */
#define CALLBACK_NOTIFY_NOADVANCE(FOR)  CALLBACK_NOTIFY_(FOR, p - data)

/* Run data callback FOR with LEN bytes, returning ER if it fails */
#define CALLBACK_DATA_(FOR, LEN, ER)                                 \
do {                                                                 \
assert(HTTP_PARSER_ERRNO(parser) == HPE_OK);                       \
\
if (FOR##_mark) {                                                  \
if (settings->on_##FOR) {                                        \
if (0 != settings->on_##FOR(parser, FOR##_mark, (LEN))) {      \
SET_ERRNO(HPE_CB_##FOR);                                     \
}                                                              \
\
/* We either errored above or got paused; get out */           \
if (HTTP_PARSER_ERRNO(parser) != HPE_OK) {                     \
return (ER);                                                 \
}                                                              \
}                                                                \
FOR##_mark = NULL;                                               \
}                                                                  \
} while (0)

/* Run the data callback FOR and consume the current byte */
#define CALLBACK_DATA(FOR)                                           \
CALLBACK_DATA_(FOR, p - FOR##_mark, p - data + 1)

/* Run the data callback FOR and don't consume the current byte */
#define CALLBACK_DATA_NOADVANCE(FOR)                                 \
CALLBACK_DATA_(FOR, p - FOR##_mark, p - data)

/* Set the mark FOR; non-destructive if mark is already set */
#define MARK(FOR)                                                    \
do {                                                                 \
if (!FOR##_mark) {                                                 \
FOR##_mark = p;                                                  \
}                                                                  \
} while (0)


#define PROXY_CONNECTION "proxy-connection"
#define CONNECTION "connection"
#define CONTENT_LENGTH "content-length"
#define TRANSFER_ENCODING "transfer-encoding"
#define UPGRADE "upgrade"
#define CHUNKED "chunked"
#define KEEP_ALIVE "keep-alive"
#define CLOSE "close"


static const char *method_strings[] =
{
#define XX(num, name, string) #string,
    HTTP_METHOD_MAP(XX)
#undef XX
};


/* Tokens as defined by rfc 2616. Also lowercases them.
 *        token       = 1*<any CHAR except CTLs or separators>
 *     separators     = "(" | ")" | "<" | ">" | "@"
 *                    | "," | ";" | ":" | "\" | <">
 *                    | "/" | "[" | "]" | "?" | "="
 *                    | "{" | "}" | SP | HT
 */
static const char tokens[256] = {
    /*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
    0,      '!',      0,      '#',     '$',     '%',     '&',    '\'',
    /*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
    0,       0,      '*',     '+',      0,      '-',     '.',      0,
    /*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
    '0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
    /*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
    '8',     '9',      0,       0,       0,       0,       0,       0,
    /*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
    0,      'a',     'b',     'c',     'd',     'e',     'f',     'g',
    /*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
    'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
    /*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
    'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
    /*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
    'x',     'y',     'z',      0,       0,       0,      '^',     '_',
    /*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
    '`',     'a',     'b',     'c',     'd',     'e',     'f',     'g',
    /* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
    'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
    /* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
    'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
    /* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
    'x',     'y',     'z',      0,      '|',      0,      '~',       0 };


static const int8_t unhex[256] =
{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    , 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1
    ,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1
    ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    ,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1
    ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};


#if HTTP_PARSER_STRICT
# define T(v) 0
#else
# define T(v) v
#endif


static const uint8_t normal_url_char[32] = {
    /*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
    0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
    /*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
    0    | T(2)   |   0    |   0    | T(16)  |   0    |   0    |   0,
    /*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
    0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
    /*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
    0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
    /*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
    0    |   2    |   4    |   0    |   16   |   32   |   64   |  128,
    /*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |   0,
    /*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
    /* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
    1    |   2    |   4    |   8    |   16   |   32   |   64   |   0, };

#undef T

enum state
{ s_dead = 1 /* important that this is > 0 */
    
    , s_start_req_or_res
    , s_res_or_resp_H
    , s_start_res
    , s_res_H
    , s_res_HT
    , s_res_HTT
    , s_res_HTTP
    , s_res_first_http_major
    , s_res_http_major
    , s_res_first_http_minor
    , s_res_http_minor
    , s_res_first_status_code
    , s_res_status_code
    , s_res_status
    , s_res_line_almost_done
    
    , s_start_req
    
    , s_req_method
    , s_req_spaces_before_url
    , s_req_schema
    , s_req_schema_slash
    , s_req_schema_slash_slash
    , s_req_server_start
    , s_req_server
    , s_req_server_with_at
    , s_req_path
    , s_req_query_string_start
    , s_req_query_string
    , s_req_fragment_start
    , s_req_fragment
    , s_req_http_start
    , s_req_http_H
    , s_req_http_HT
    , s_req_http_HTT
    , s_req_http_HTTP
    , s_req_first_http_major
    , s_req_http_major
    , s_req_first_http_minor
    , s_req_http_minor
    , s_req_line_almost_done
    
    , s_header_field_start
    , s_header_field
    , s_header_value_start
    , s_header_value
    , s_header_value_lws
    
    , s_header_almost_done
    
    , s_chunk_size_start
    , s_chunk_size
    , s_chunk_parameters
    , s_chunk_size_almost_done
    
    , s_headers_almost_done
    , s_headers_done
    
    /* Important: 's_headers_done' must be the last 'header' state. All
     * states beyond this must be 'body' states. It is used for overflow
     * checking. See the PARSING_HEADER() macro.
     */
    
    , s_chunk_data
    , s_chunk_data_almost_done
    , s_chunk_data_done
    
    , s_body_identity
    , s_body_identity_eof
    
    , s_message_done
};


#define PARSING_HEADER(state) (state <= s_headers_done)


enum header_states
{ h_general = 0
    , h_C
    , h_CO
    , h_CON
    
    , h_matching_connection
    , h_matching_proxy_connection
    , h_matching_content_length
    , h_matching_transfer_encoding
    , h_matching_upgrade
    
    , h_connection
    , h_content_length
    , h_transfer_encoding
    , h_upgrade
    
    , h_matching_transfer_encoding_chunked
    , h_matching_connection_keep_alive
    , h_matching_connection_close
    
    , h_transfer_encoding_chunked
    , h_connection_keep_alive
    , h_connection_close
};

enum http_host_state
{
    s_http_host_dead = 1
    , s_http_userinfo_start
    , s_http_userinfo
    , s_http_host_start
    , s_http_host_v6_start
    , s_http_host
    , s_http_host_v6
    , s_http_host_v6_end
    , s_http_host_port_start
    , s_http_host_port
};

/* Macros for character classes; depends on strict-mode  */
#define CR                  '\r'
#define LF                  '\n'
#define LOWER(c)            (unsigned char)(c | 0x20)
#define IS_ALPHA(c)         (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)      (IS_ALPHA(c) || IS_NUM(c))
#define IS_HEX(c)           (IS_NUM(c) || (LOWER(c) >= 'a' && LOWER(c) <= 'f'))
#define IS_MARK(c)          ((c) == '-' || (c) == '_' || (c) == '.' || \
(c) == '!' || (c) == '~' || (c) == '*' || (c) == '\'' || (c) == '(' || \
(c) == ')')
#define IS_USERINFO_CHAR(c) (IS_ALPHANUM(c) || IS_MARK(c) || (c) == '%' || \
(c) == ';' || (c) == ':' || (c) == '&' || (c) == '=' || (c) == '+' || \
(c) == '$' || (c) == ',')

#if HTTP_PARSER_STRICT
#define TOKEN(c)            (tokens[(unsigned char)c])
#define IS_URL_CHAR(c)      (BIT_AT(normal_url_char, (unsigned char)c))
#define IS_HOST_CHAR(c)     (IS_ALPHANUM(c) || (c) == '.' || (c) == '-')
#else
#define TOKEN(c)            ((c == ' ') ? ' ' : tokens[(unsigned char)c])
#define IS_URL_CHAR(c)                                                         \
(BIT_AT(normal_url_char, (unsigned char)c) || ((c) & 0x80))
#define IS_HOST_CHAR(c)                                                        \
(IS_ALPHANUM(c) || (c) == '.' || (c) == '-' || (c) == '_')
#endif


#define start_state (parser->type == HTTP_REQUEST ? s_start_req : s_start_res)


#if HTTP_PARSER_STRICT
# define STRICT_CHECK(cond)                                          \
do {                                                                 \
if (cond) {                                                        \
SET_ERRNO(HPE_STRICT);                                           \
goto error;                                                      \
}                                                                  \
} while (0)
# define NEW_MESSAGE() (http_should_keep_alive(parser) ? start_state : s_dead)
#else
# define STRICT_CHECK(cond)
# define NEW_MESSAGE() start_state
#endif


/* Map errno values to strings for human-readable output */
#define HTTP_STRERROR_GEN(n, s) { "HPE_" #n, s },
static struct {
    const char *name;
    const char *description;
} http_strerror_tab[] = {
    HTTP_ERRNO_MAP(HTTP_STRERROR_GEN)
};
#undef HTTP_STRERROR_GEN

int http_message_needs_eof(const http_parser *parser);

/* Our URL parser.
 *
 * This is designed to be shared by http_parser_execute() for URL validation,
 * hence it has a state transition + byte-for-byte interface. In addition, it
 * is meant to be embedded in http_parser_parse_url(), which does the dirty
 * work of turning state transitions URL components for its API.
 *
 * This function should only be invoked with non-space characters. It is
 * assumed that the caller cares about (and can detect) the transition between
 * URL and non-URL states by looking for these.
 */
static enum state
parse_url_char(enum state s, const char ch)
{
    if (ch == ' ' || ch == '\r' || ch == '\n') {
        return s_dead;
    }
    
#if HTTP_PARSER_STRICT
    if (ch == '\t' || ch == '\f') {
        return s_dead;
    }
#endif
    
    switch (s) {
        case s_req_spaces_before_url:
            /* Proxied requests are followed by scheme of an absolute URI (alpha).
             * All methods except CONNECT are followed by '/' or '*'.
             */
            
            if (ch == '/' || ch == '*') {
                return s_req_path;
            }
            
            if (IS_ALPHA(ch)) {
                return s_req_schema;
            }
            
            break;
            
        case s_req_schema:
            if (IS_ALPHA(ch)) {
                return s;
            }
            
            if (ch == ':') {
                return s_req_schema_slash;
            }
            
            break;
            
        case s_req_schema_slash:
            if (ch == '/') {
                return s_req_schema_slash_slash;
            }
            
            break;
            
        case s_req_schema_slash_slash:
            if (ch == '/') {
                return s_req_server_start;
            }
            
            break;
            
        case s_req_server_with_at:
            if (ch == '@') {
                return s_dead;
            }
            
            /* FALLTHROUGH */
        case s_req_server_start:
        case s_req_server:
            if (ch == '/') {
                return s_req_path;
            }
            
            if (ch == '?') {
                return s_req_query_string_start;
            }
            
            if (ch == '@') {
                return s_req_server_with_at;
            }
            
            if (IS_USERINFO_CHAR(ch) || ch == '[' || ch == ']') {
                return s_req_server;
            }
            
            break;
            
        case s_req_path:
            if (IS_URL_CHAR(ch)) {
                return s;
            }
            
            switch (ch) {
                case '?':
                    return s_req_query_string_start;
                    
                case '#':
                    return s_req_fragment_start;
            }
            
            break;
            
        case s_req_query_string_start:
        case s_req_query_string:
            if (IS_URL_CHAR(ch)) {
                return s_req_query_string;
            }
            
            switch (ch) {
                case '?':
                    /* allow extra '?' in query string */
                    return s_req_query_string;
                    
                case '#':
                    return s_req_fragment_start;
            }
            
            break;
            
        case s_req_fragment_start:
            if (IS_URL_CHAR(ch)) {
                return s_req_fragment;
            }
            
            switch (ch) {
                case '?':
                    return s_req_fragment;
                    
                case '#':
                    return s;
            }
            
            break;
            
        case s_req_fragment:
            if (IS_URL_CHAR(ch)) {
                return s;
            }
            
            switch (ch) {
                case '?':
                case '#':
                    return s;
            }
            
            break;
            
        default:
            break;
    }
    
    /* We should never fall out of the switch above unless there's an error */
    return s_dead;
}

size_t http_parser_execute (http_parser *parser,
                            const http_parser_settings *settings,
                            const char *data,
                            size_t len)
{
    char c, ch;
    int8_t unhex_val;
    const char *p = data;
    const char *header_field_mark = 0;
    const char *header_value_mark = 0;
    const char *url_mark = 0;
    const char *body_mark = 0;
    
    /* We're in an error state. Don't bother doing anything. */
    if (HTTP_PARSER_ERRNO(parser) != HPE_OK) {
        return 0;
    }
    
    if (len == 0) {
        switch (parser->state) {
            case s_body_identity_eof:
                /* Use of CALLBACK_NOTIFY() here would erroneously return 1 byte read if
                 * we got paused.
                 */
                CALLBACK_NOTIFY_NOADVANCE(message_complete);
                return 0;
                
            case s_dead:
            case s_start_req_or_res:
            case s_start_res:
            case s_start_req:
                return 0;
                
            default:
                SET_ERRNO(HPE_INVALID_EOF_STATE);
                return 1;
        }
    }
    
    
    if (parser->state == s_header_field)
        header_field_mark = data;
    if (parser->state == s_header_value)
        header_value_mark = data;
    switch (parser->state) {
        case s_req_path:
        case s_req_schema:
        case s_req_schema_slash:
        case s_req_schema_slash_slash:
        case s_req_server_start:
        case s_req_server:
        case s_req_server_with_at:
        case s_req_query_string_start:
        case s_req_query_string:
        case s_req_fragment_start:
        case s_req_fragment:
            url_mark = data;
            break;
    }
    
    for (p=data; p != data + len; p++) {
        ch = *p;
        
        if (PARSING_HEADER(parser->state)) {
            ++parser->nread;
            /* Buffer overflow attack */
            if (parser->nread > HTTP_MAX_HEADER_SIZE) {
                SET_ERRNO(HPE_HEADER_OVERFLOW);
                goto error;
            }
        }
        
    reexecute_byte:
        switch (parser->state) {
                
            case s_dead:
                /* this state is used after a 'Connection: close' message
                 * the parser will error out if it reads another message
                 */
                if (ch == CR || ch == LF)
                    break;
                
                SET_ERRNO(HPE_CLOSED_CONNECTION);
                goto error;
                
            case s_start_req_or_res:
            {
                if (ch == CR || ch == LF)
                    break;
                parser->flags = 0;
                parser->content_length = ULLONG_MAX;
                
                if (ch == 'H') {
                    parser->state = s_res_or_resp_H;
                    
                    CALLBACK_NOTIFY(message_begin);
                } else {
                    parser->type = HTTP_REQUEST;
                    parser->state = s_start_req;
                    goto reexecute_byte;
                }
                
                break;
            }
                
            case s_res_or_resp_H:
                if (ch == 'T') {
                    parser->type = HTTP_RESPONSE;
                    parser->state = s_res_HT;
                } else {
                    if (ch != 'E') {
                        SET_ERRNO(HPE_INVALID_CONSTANT);
                        goto error;
                    }
                    
                    parser->type = HTTP_REQUEST;
                    parser->method = HTTP_HEAD;
                    parser->index = 2;
                    parser->state = s_req_method;
                }
                break;
                
            case s_start_res:
            {
                parser->flags = 0;
                parser->content_length = ULLONG_MAX;
                
                switch (ch) {
                    case 'H':
                        parser->state = s_res_H;
                        break;
                        
                    case CR:
                    case LF:
                        break;
                        
                    default:
                        SET_ERRNO(HPE_INVALID_CONSTANT);
                        goto error;
                }
                
                CALLBACK_NOTIFY(message_begin);
                break;
            }
                
            case s_res_H:
                STRICT_CHECK(ch != 'T');
                parser->state = s_res_HT;
                break;
                
            case s_res_HT:
                STRICT_CHECK(ch != 'T');
                parser->state = s_res_HTT;
                break;
                
            case s_res_HTT:
                STRICT_CHECK(ch != 'P');
                parser->state = s_res_HTTP;
                break;
                
            case s_res_HTTP:
                STRICT_CHECK(ch != '/');
                parser->state = s_res_first_http_major;
                break;
                
            case s_res_first_http_major:
                if (ch < '0' || ch > '9') {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_major = ch - '0';
                parser->state = s_res_http_major;
                break;
                
                /* major HTTP version or dot */
            case s_res_http_major:
            {
                if (ch == '.') {
                    parser->state = s_res_first_http_minor;
                    break;
                }
                
                if (!IS_NUM(ch)) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_major *= 10;
                parser->http_major += ch - '0';
                
                if (parser->http_major > 999) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                break;
            }
                
                /* first digit of minor HTTP version */
            case s_res_first_http_minor:
                if (!IS_NUM(ch)) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_minor = ch - '0';
                parser->state = s_res_http_minor;
                break;
                
                /* minor HTTP version or end of request line */
            case s_res_http_minor:
            {
                if (ch == ' ') {
                    parser->state = s_res_first_status_code;
                    break;
                }
                
                if (!IS_NUM(ch)) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_minor *= 10;
                parser->http_minor += ch - '0';
                
                if (parser->http_minor > 999) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                break;
            }
                
            case s_res_first_status_code:
            {
                if (!IS_NUM(ch)) {
                    if (ch == ' ') {
                        break;
                    }
                    
                    SET_ERRNO(HPE_INVALID_STATUS);
                    goto error;
                }
                parser->status_code = ch - '0';
                parser->state = s_res_status_code;
                break;
            }
                
            case s_res_status_code:
            {
                if (!IS_NUM(ch)) {
                    switch (ch) {
                        case ' ':
                            parser->state = s_res_status;
                            break;
                        case CR:
                            parser->state = s_res_line_almost_done;
                            break;
                        case LF:
                            parser->state = s_header_field_start;
                            break;
                        default:
                            SET_ERRNO(HPE_INVALID_STATUS);
                            goto error;
                    }
                    break;
                }
                
                parser->status_code *= 10;
                parser->status_code += ch - '0';
                
                if (parser->status_code > 999) {
                    SET_ERRNO(HPE_INVALID_STATUS);
                    goto error;
                }
                
                break;
            }
                
            case s_res_status:
                /* the human readable status. e.g. "NOT FOUND"
                 * we are not humans so just ignore this */
                if (ch == CR) {
                    parser->state = s_res_line_almost_done;
                    break;
                }
                
                if (ch == LF) {
                    parser->state = s_header_field_start;
                    break;
                }
                break;
                
            case s_res_line_almost_done:
                STRICT_CHECK(ch != LF);
                parser->state = s_header_field_start;
                CALLBACK_NOTIFY(status_complete);
                break;
                
            case s_start_req:
            {
                if (ch == CR || ch == LF)
                    break;
                parser->flags = 0;
                parser->content_length = ULLONG_MAX;
                
                if (!IS_ALPHA(ch)) {
                    SET_ERRNO(HPE_INVALID_METHOD);
                    goto error;
                }
                
                parser->method = (enum http_method) 0;
                parser->index = 1;
                switch (ch) {
                    case 'C': parser->method = HTTP_CONNECT; /* or COPY, CHECKOUT */ break;
                    case 'D': parser->method = HTTP_DELETE; break;
                    case 'G': parser->method = HTTP_GET; break;
                    case 'H': parser->method = HTTP_HEAD; break;
                    case 'L': parser->method = HTTP_LOCK; break;
                    case 'M': parser->method = HTTP_MKCOL; /* or MOVE, MKACTIVITY, MERGE, M-SEARCH */ break;
                    case 'N': parser->method = HTTP_NOTIFY; break;
                    case 'O': parser->method = HTTP_OPTIONS; break;
                    case 'P': parser->method = HTTP_POST;
                        /* or PROPFIND|PROPPATCH|PUT|PATCH|PURGE */
                        break;
                    case 'R': parser->method = HTTP_REPORT; break;
                    case 'S': parser->method = HTTP_SUBSCRIBE; /* or SEARCH */ break;
                    case 'T': parser->method = HTTP_TRACE; break;
                    case 'U': parser->method = HTTP_UNLOCK; /* or UNSUBSCRIBE */ break;
                    default:
                        SET_ERRNO(HPE_INVALID_METHOD);
                        goto error;
                }
                parser->state = s_req_method;
                
                CALLBACK_NOTIFY(message_begin);
                
                break;
            }
                
            case s_req_method:
            {
                const char *matcher;
                if (ch == '\0') {
                    SET_ERRNO(HPE_INVALID_METHOD);
                    goto error;
                }
                
                matcher = method_strings[parser->method];
                if (ch == ' ' && matcher[parser->index] == '\0') {
                    parser->state = s_req_spaces_before_url;
                } else if (ch == matcher[parser->index]) {
                    ; /* nada */
                } else if (parser->method == HTTP_CONNECT) {
                    if (parser->index == 1 && ch == 'H') {
                        parser->method = HTTP_CHECKOUT;
                    } else if (parser->index == 2  && ch == 'P') {
                        parser->method = HTTP_COPY;
                    } else {
                        goto error;
                    }
                } else if (parser->method == HTTP_MKCOL) {
                    if (parser->index == 1 && ch == 'O') {
                        parser->method = HTTP_MOVE;
                    } else if (parser->index == 1 && ch == 'E') {
                        parser->method = HTTP_MERGE;
                    } else if (parser->index == 1 && ch == '-') {
                        parser->method = HTTP_MSEARCH;
                    } else if (parser->index == 2 && ch == 'A') {
                        parser->method = HTTP_MKACTIVITY;
                    } else {
                        goto error;
                    }
                } else if (parser->method == HTTP_SUBSCRIBE) {
                    if (parser->index == 1 && ch == 'E') {
                        parser->method = HTTP_SEARCH;
                    } else {
                        goto error;
                    }
                } else if (parser->index == 1 && parser->method == HTTP_POST) {
                    if (ch == 'R') {
                        parser->method = HTTP_PROPFIND; /* or HTTP_PROPPATCH */
                    } else if (ch == 'U') {
                        parser->method = HTTP_PUT; /* or HTTP_PURGE */
                    } else if (ch == 'A') {
                        parser->method = HTTP_PATCH;
                    } else {
                        goto error;
                    }
                } else if (parser->index == 2) {
                    if (parser->method == HTTP_PUT) {
                        if (ch == 'R') parser->method = HTTP_PURGE;
                    } else if (parser->method == HTTP_UNLOCK) {
                        if (ch == 'S') parser->method = HTTP_UNSUBSCRIBE;
                    }
                } else if (parser->index == 4 && parser->method == HTTP_PROPFIND && ch == 'P') {
                    parser->method = HTTP_PROPPATCH;
                } else {
                    SET_ERRNO(HPE_INVALID_METHOD);
                    goto error;
                }
                
                ++parser->index;
                break;
            }
                
            case s_req_spaces_before_url:
            {
                if (ch == ' ') break;
                
                MARK(url);
                if (parser->method == HTTP_CONNECT) {
                    parser->state = s_req_server_start;
                }
                
                parser->state = parse_url_char((enum state)parser->state, ch);
                if (parser->state == s_dead) {
                    SET_ERRNO(HPE_INVALID_URL);
                    goto error;
                }
                
                break;
            }
                
            case s_req_schema:
            case s_req_schema_slash:
            case s_req_schema_slash_slash:
            case s_req_server_start:
            {
                switch (ch) {
                        /* No whitespace allowed here */
                    case ' ':
                    case CR:
                    case LF:
                        SET_ERRNO(HPE_INVALID_URL);
                        goto error;
                    default:
                        parser->state = parse_url_char((enum state)parser->state, ch);
                        if (parser->state == s_dead) {
                            SET_ERRNO(HPE_INVALID_URL);
                            goto error;
                        }
                }
                
                break;
            }
                
            case s_req_server:
            case s_req_server_with_at:
            case s_req_path:
            case s_req_query_string_start:
            case s_req_query_string:
            case s_req_fragment_start:
            case s_req_fragment:
            {
                switch (ch) {
                    case ' ':
                        parser->state = s_req_http_start;
                        CALLBACK_DATA(url);
                        break;
                    case CR:
                    case LF:
                        parser->http_major = 0;
                        parser->http_minor = 9;
                        parser->state = (ch == CR) ?
                        s_req_line_almost_done :
                        s_header_field_start;
                        CALLBACK_DATA(url);
                        break;
                    default:
                        parser->state = parse_url_char((enum state)parser->state, ch);
                        if (parser->state == s_dead) {
                            SET_ERRNO(HPE_INVALID_URL);
                            goto error;
                        }
                }
                break;
            }
                
            case s_req_http_start:
                switch (ch) {
                    case 'H':
                        parser->state = s_req_http_H;
                        break;
                    case ' ':
                        break;
                    default:
                        SET_ERRNO(HPE_INVALID_CONSTANT);
                        goto error;
                }
                break;
                
            case s_req_http_H:
                STRICT_CHECK(ch != 'T');
                parser->state = s_req_http_HT;
                break;
                
            case s_req_http_HT:
                STRICT_CHECK(ch != 'T');
                parser->state = s_req_http_HTT;
                break;
                
            case s_req_http_HTT:
                STRICT_CHECK(ch != 'P');
                parser->state = s_req_http_HTTP;
                break;
                
            case s_req_http_HTTP:
                STRICT_CHECK(ch != '/');
                parser->state = s_req_first_http_major;
                break;
                
                /* first digit of major HTTP version */
            case s_req_first_http_major:
                if (ch < '1' || ch > '9') {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_major = ch - '0';
                parser->state = s_req_http_major;
                break;
                
                /* major HTTP version or dot */
            case s_req_http_major:
            {
                if (ch == '.') {
                    parser->state = s_req_first_http_minor;
                    break;
                }
                
                if (!IS_NUM(ch)) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_major *= 10;
                parser->http_major += ch - '0';
                
                if (parser->http_major > 999) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                break;
            }
                
                /* first digit of minor HTTP version */
            case s_req_first_http_minor:
                if (!IS_NUM(ch)) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_minor = ch - '0';
                parser->state = s_req_http_minor;
                break;
                
                /* minor HTTP version or end of request line */
            case s_req_http_minor:
            {
                if (ch == CR) {
                    parser->state = s_req_line_almost_done;
                    break;
                }
                
                if (ch == LF) {
                    parser->state = s_header_field_start;
                    break;
                }
                
                /* XXX allow spaces after digit? */
                
                if (!IS_NUM(ch)) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                parser->http_minor *= 10;
                parser->http_minor += ch - '0';
                
                if (parser->http_minor > 999) {
                    SET_ERRNO(HPE_INVALID_VERSION);
                    goto error;
                }
                
                break;
            }
                
                /* end of request line */
            case s_req_line_almost_done:
            {
                if (ch != LF) {
                    SET_ERRNO(HPE_LF_EXPECTED);
                    goto error;
                }
                
                parser->state = s_header_field_start;
                break;
            }
                
            case s_header_field_start:
            {
                if (ch == CR) {
                    parser->state = s_headers_almost_done;
                    break;
                }
                
                if (ch == LF) {
                    /* they might be just sending \n instead of \r\n so this would be
                     * the second \n to denote the end of headers*/
                    parser->state = s_headers_almost_done;
                    goto reexecute_byte;
                }
                
                c = TOKEN(ch);
                
                if (!c) {
                    SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
                    goto error;
                }
                
                MARK(header_field);
                
                parser->index = 0;
                parser->state = s_header_field;
                
                switch (c) {
                    case 'c':
                        parser->header_state = h_C;
                        break;
                        
                    case 'p':
                        parser->header_state = h_matching_proxy_connection;
                        break;
                        
                    case 't':
                        parser->header_state = h_matching_transfer_encoding;
                        break;
                        
                    case 'u':
                        parser->header_state = h_matching_upgrade;
                        break;
                        
                    default:
                        parser->header_state = h_general;
                        break;
                }
                break;
            }
                
            case s_header_field:
            {
                c = TOKEN(ch);
                
                if (c) {
                    switch (parser->header_state) {
                        case h_general:
                            break;
                            
                        case h_C:
                            parser->index++;
                            parser->header_state = (c == 'o' ? h_CO : h_general);
                            break;
                            
                        case h_CO:
                            parser->index++;
                            parser->header_state = (c == 'n' ? h_CON : h_general);
                            break;
                            
                        case h_CON:
                            parser->index++;
                            switch (c) {
                                case 'n':
                                    parser->header_state = h_matching_connection;
                                    break;
                                case 't':
                                    parser->header_state = h_matching_content_length;
                                    break;
                                default:
                                    parser->header_state = h_general;
                                    break;
                            }
                            break;
                            
                            /* connection */
                            
                        case h_matching_connection:
                            parser->index++;
                            if (parser->index > sizeof(CONNECTION)-1
                                || c != CONNECTION[parser->index]) {
                                parser->header_state = h_general;
                            } else if (parser->index == sizeof(CONNECTION)-2) {
                                parser->header_state = h_connection;
                            }
                            break;
                            
                            /* proxy-connection */
                            
                        case h_matching_proxy_connection:
                            parser->index++;
                            if (parser->index > sizeof(PROXY_CONNECTION)-1
                                || c != PROXY_CONNECTION[parser->index]) {
                                parser->header_state = h_general;
                            } else if (parser->index == sizeof(PROXY_CONNECTION)-2) {
                                parser->header_state = h_connection;
                            }
                            break;
                            
                            /* content-length */
                            
                        case h_matching_content_length:
                            parser->index++;
                            if (parser->index > sizeof(CONTENT_LENGTH)-1
                                || c != CONTENT_LENGTH[parser->index]) {
                                parser->header_state = h_general;
                            } else if (parser->index == sizeof(CONTENT_LENGTH)-2) {
                                parser->header_state = h_content_length;
                            }
                            break;
                            
                            /* transfer-encoding */
                            
                        case h_matching_transfer_encoding:
                            parser->index++;
                            if (parser->index > sizeof(TRANSFER_ENCODING)-1
                                || c != TRANSFER_ENCODING[parser->index]) {
                                parser->header_state = h_general;
                            } else if (parser->index == sizeof(TRANSFER_ENCODING)-2) {
                                parser->header_state = h_transfer_encoding;
                            }
                            break;
                            
                            /* upgrade */
                            
                        case h_matching_upgrade:
                            parser->index++;
                            if (parser->index > sizeof(UPGRADE)-1
                                || c != UPGRADE[parser->index]) {
                                parser->header_state = h_general;
                            } else if (parser->index == sizeof(UPGRADE)-2) {
                                parser->header_state = h_upgrade;
                            }
                            break;
                            
                        case h_connection:
                        case h_content_length:
                        case h_transfer_encoding:
                        case h_upgrade:
                            if (ch != ' ') parser->header_state = h_general;
                            break;
                            
                        default:
                            assert(0 && "Unknown header_state");
                            break;
                    }
                    break;
                }
                
                if (ch == ':') {
                    parser->state = s_header_value_start;
                    CALLBACK_DATA(header_field);
                    break;
                }
                
                if (ch == CR) {
                    parser->state = s_header_almost_done;
                    CALLBACK_DATA(header_field);
                    break;
                }
                
                if (ch == LF) {
                    parser->state = s_header_field_start;
                    CALLBACK_DATA(header_field);
                    break;
                }
                
                SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
                goto error;
            }
                
            case s_header_value_start:
            {
                if (ch == ' ' || ch == '\t') break;
                
                MARK(header_value);
                
                parser->state = s_header_value;
                parser->index = 0;
                
                if (ch == CR) {
                    parser->header_state = h_general;
                    parser->state = s_header_almost_done;
                    CALLBACK_DATA(header_value);
                    break;
                }
                
                if (ch == LF) {
                    parser->state = s_header_field_start;
                    CALLBACK_DATA(header_value);
                    break;
                }
                
                c = LOWER(ch);
                
                switch (parser->header_state) {
                    case h_upgrade:
                        parser->flags |= F_UPGRADE;
                        parser->header_state = h_general;
                        break;
                        
                    case h_transfer_encoding:
                        /* looking for 'Transfer-Encoding: chunked' */
                        if ('c' == c) {
                            parser->header_state = h_matching_transfer_encoding_chunked;
                        } else {
                            parser->header_state = h_general;
                        }
                        break;
                        
                    case h_content_length:
                        if (!IS_NUM(ch)) {
                            SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                            goto error;
                        }
                        
                        parser->content_length = ch - '0';
                        break;
                        
                    case h_connection:
                        /* looking for 'Connection: keep-alive' */
                        if (c == 'k') {
                            parser->header_state = h_matching_connection_keep_alive;
                            /* looking for 'Connection: close' */
                        } else if (c == 'c') {
                            parser->header_state = h_matching_connection_close;
                        } else {
                            parser->header_state = h_general;
                        }
                        break;
                        
                    default:
                        parser->header_state = h_general;
                        break;
                }
                break;
            }
                
            case s_header_value:
            {
                
                if (ch == CR) {
                    parser->state = s_header_almost_done;
                    CALLBACK_DATA(header_value);
                    break;
                }
                
                if (ch == LF) {
                    parser->state = s_header_almost_done;
                    CALLBACK_DATA_NOADVANCE(header_value);
                    goto reexecute_byte;
                }
                
                c = LOWER(ch);
                
                switch (parser->header_state) {
                    case h_general:
                        break;
                        
                    case h_connection:
                    case h_transfer_encoding:
                        assert(0 && "Shouldn't get here.");
                        break;
                        
                    case h_content_length:
                    {
                        uint64_t t;
                        
                        if (ch == ' ') break;
                        
                        if (!IS_NUM(ch)) {
                            SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                            goto error;
                        }
                        
                        t = parser->content_length;
                        t *= 10;
                        t += ch - '0';
                        
                        /* Overflow? */
                        if (t < parser->content_length || t == ULLONG_MAX) {
                            SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                            goto error;
                        }
                        
                        parser->content_length = t;
                        break;
                    }
                        
                        /* Transfer-Encoding: chunked */
                    case h_matching_transfer_encoding_chunked:
                        parser->index++;
                        if (parser->index > sizeof(CHUNKED)-1
                            || c != CHUNKED[parser->index]) {
                            parser->header_state = h_general;
                        } else if (parser->index == sizeof(CHUNKED)-2) {
                            parser->header_state = h_transfer_encoding_chunked;
                        }
                        break;
                        
                        /* looking for 'Connection: keep-alive' */
                    case h_matching_connection_keep_alive:
                        parser->index++;
                        if (parser->index > sizeof(KEEP_ALIVE)-1
                            || c != KEEP_ALIVE[parser->index]) {
                            parser->header_state = h_general;
                        } else if (parser->index == sizeof(KEEP_ALIVE)-2) {
                            parser->header_state = h_connection_keep_alive;
                        }
                        break;
                        
                        /* looking for 'Connection: close' */
                    case h_matching_connection_close:
                        parser->index++;
                        if (parser->index > sizeof(CLOSE)-1 || c != CLOSE[parser->index]) {
                            parser->header_state = h_general;
                        } else if (parser->index == sizeof(CLOSE)-2) {
                            parser->header_state = h_connection_close;
                        }
                        break;
                        
                    case h_transfer_encoding_chunked:
                    case h_connection_keep_alive:
                    case h_connection_close:
                        if (ch != ' ') parser->header_state = h_general;
                        break;
                        
                    default:
                        parser->state = s_header_value;
                        parser->header_state = h_general;
                        break;
                }
                break;
            }
                
            case s_header_almost_done:
            {
                STRICT_CHECK(ch != LF);
                
                parser->state = s_header_value_lws;
                
                switch (parser->header_state) {
                    case h_connection_keep_alive:
                        parser->flags |= F_CONNECTION_KEEP_ALIVE;
                        break;
                    case h_connection_close:
                        parser->flags |= F_CONNECTION_CLOSE;
                        break;
                    case h_transfer_encoding_chunked:
                        parser->flags |= F_CHUNKED;
                        break;
                    default:
                        break;
                }
                
                break;
            }
                
            case s_header_value_lws:
            {
                if (ch == ' ' || ch == '\t')
                    parser->state = s_header_value_start;
                else
                {
                    parser->state = s_header_field_start;
                    goto reexecute_byte;
                }
                break;
            }
                
            case s_headers_almost_done:
            {
                STRICT_CHECK(ch != LF);
                
                if (parser->flags & F_TRAILING) {
                    /* End of a chunked request */
                    parser->state = NEW_MESSAGE();
                    CALLBACK_NOTIFY(message_complete);
                    break;
                }
                
                parser->state = s_headers_done;
                
                /* Set this here so that on_headers_complete() callbacks can see it */
                parser->upgrade =
                (parser->flags & F_UPGRADE || parser->method == HTTP_CONNECT);
                
                /* Here we call the headers_complete callback. This is somewhat
                 * different than other callbacks because if the user returns 1, we
                 * will interpret that as saying that this message has no body. This
                 * is needed for the annoying case of recieving a response to a HEAD
                 * request.
                 *
                 * We'd like to use CALLBACK_NOTIFY_NOADVANCE() here but we cannot, so
                 * we have to simulate it by handling a change in errno below.
                 */
                if (settings->on_headers_complete) {
                    switch (settings->on_headers_complete(parser)) {
                        case 0:
                            break;
                            
                        case 1:
                            parser->flags |= F_SKIPBODY;
                            break;
                            
                        default:
                            SET_ERRNO(HPE_CB_headers_complete);
                            return p - data; /* Error */
                    }
                }
                
                if (HTTP_PARSER_ERRNO(parser) != HPE_OK) {
                    return p - data;
                }
                
                goto reexecute_byte;
            }
                
            case s_headers_done:
            {
                STRICT_CHECK(ch != LF);
                
                parser->nread = 0;
                
                /* Exit, the rest of the connect is in a different protocol. */
                if (parser->upgrade) {
                    parser->state = NEW_MESSAGE();
                    CALLBACK_NOTIFY(message_complete);
                    return (p - data) + 1;
                }
                
                if (parser->flags & F_SKIPBODY) {
                    parser->state = NEW_MESSAGE();
                    CALLBACK_NOTIFY(message_complete);
                } else if (parser->flags & F_CHUNKED) {
                    /* chunked encoding - ignore Content-Length header */
                    parser->state = s_chunk_size_start;
                } else {
                    if (parser->content_length == 0) {
                        /* Content-Length header given but zero: Content-Length: 0\r\n */
                        parser->state = NEW_MESSAGE();
                        CALLBACK_NOTIFY(message_complete);
                    } else if (parser->content_length != ULLONG_MAX) {
                        /* Content-Length header given and non-zero */
                        parser->state = s_body_identity;
                    } else {
                        if (parser->type == HTTP_REQUEST ||
                            !http_message_needs_eof(parser)) {
                            /* Assume content-length 0 - read the next */
                            parser->state = NEW_MESSAGE();
                            CALLBACK_NOTIFY(message_complete);
                        } else {
                            /* Read body until EOF */
                            parser->state = s_body_identity_eof;
                        }
                    }
                }
                
                break;
            }
                
            case s_body_identity:
            {
                uint64_t to_read = MIN(parser->content_length,
                                       (uint64_t) ((data + len) - p));
                
                assert(parser->content_length != 0
                       && parser->content_length != ULLONG_MAX);
                
                /* The difference between advancing content_length and p is because
                 * the latter will automaticaly advance on the next loop iteration.
                 * Further, if content_length ends up at 0, we want to see the last
                 * byte again for our message complete callback.
                 */
                MARK(body);
                parser->content_length -= to_read;
                p += to_read - 1;
                
                if (parser->content_length == 0) {
                    parser->state = s_message_done;
                    
                    /* Mimic CALLBACK_DATA_NOADVANCE() but with one extra byte.
                     *
                     * The alternative to doing this is to wait for the next byte to
                     * trigger the data callback, just as in every other case. The
                     * problem with this is that this makes it difficult for the test
                     * harness to distinguish between complete-on-EOF and
                     * complete-on-length. It's not clear that this distinction is
                     * important for applications, but let's keep it for now.
                     */
                    CALLBACK_DATA_(body, p - body_mark + 1, p - data);
                    goto reexecute_byte;
                }
                
                break;
            }
                
                /* read until EOF */
            case s_body_identity_eof:
                MARK(body);
                p = data + len - 1;
                
                break;
                
            case s_message_done:
                parser->state = NEW_MESSAGE();
                CALLBACK_NOTIFY(message_complete);
                break;
                
            case s_chunk_size_start:
            {
                assert(parser->nread == 1);
                assert(parser->flags & F_CHUNKED);
                
                unhex_val = unhex[(unsigned char)ch];
                if (unhex_val == -1) {
                    SET_ERRNO(HPE_INVALID_CHUNK_SIZE);
                    goto error;
                }
                
                parser->content_length = unhex_val;
                parser->state = s_chunk_size;
                break;
            }
                
            case s_chunk_size:
            {
                uint64_t t;
                
                assert(parser->flags & F_CHUNKED);
                
                if (ch == CR) {
                    parser->state = s_chunk_size_almost_done;
                    break;
                }
                
                unhex_val = unhex[(unsigned char)ch];
                
                if (unhex_val == -1) {
                    if (ch == ';' || ch == ' ') {
                        parser->state = s_chunk_parameters;
                        break;
                    }
                    
                    SET_ERRNO(HPE_INVALID_CHUNK_SIZE);
                    goto error;
                }
                
                t = parser->content_length;
                t *= 16;
                t += unhex_val;
                
                /* Overflow? */
                if (t < parser->content_length || t == ULLONG_MAX) {
                    SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                    goto error;
                }
                
                parser->content_length = t;
                break;
            }
                
            case s_chunk_parameters:
            {
                assert(parser->flags & F_CHUNKED);
                /* just ignore this shit. TODO check for overflow */
                if (ch == CR) {
                    parser->state = s_chunk_size_almost_done;
                    break;
                }
                break;
            }
                
            case s_chunk_size_almost_done:
            {
                assert(parser->flags & F_CHUNKED);
                STRICT_CHECK(ch != LF);
                
                parser->nread = 0;
                
                if (parser->content_length == 0) {
                    parser->flags |= F_TRAILING;
                    parser->state = s_header_field_start;
                } else {
                    parser->state = s_chunk_data;
                }
                break;
            }
                
            case s_chunk_data:
            {
                uint64_t to_read = MIN(parser->content_length,
                                       (uint64_t) ((data + len) - p));
                
                assert(parser->flags & F_CHUNKED);
                assert(parser->content_length != 0
                       && parser->content_length != ULLONG_MAX);
                
                /* See the explanation in s_body_identity for why the content
                 * length and data pointers are managed this way.
                 */
                MARK(body);
                parser->content_length -= to_read;
                p += to_read - 1;
                
                if (parser->content_length == 0) {
                    parser->state = s_chunk_data_almost_done;
                }
                
                break;
            }
                
            case s_chunk_data_almost_done:
                assert(parser->flags & F_CHUNKED);
                assert(parser->content_length == 0);
                STRICT_CHECK(ch != CR);
                parser->state = s_chunk_data_done;
                CALLBACK_DATA(body);
                break;
                
            case s_chunk_data_done:
                assert(parser->flags & F_CHUNKED);
                STRICT_CHECK(ch != LF);
                parser->nread = 0;
                parser->state = s_chunk_size_start;
                break;
                
            default:
                assert(0 && "unhandled state");
                SET_ERRNO(HPE_INVALID_INTERNAL_STATE);
                goto error;
        }
    }
    
    /* Run callbacks for any marks that we have leftover after we ran our of
     * bytes. There should be at most one of these set, so it's OK to invoke
     * them in series (unset marks will not result in callbacks).
     *
     * We use the NOADVANCE() variety of callbacks here because 'p' has already
     * overflowed 'data' and this allows us to correct for the off-by-one that
     * we'd otherwise have (since CALLBACK_DATA() is meant to be run with a 'p'
     * value that's in-bounds).
     */
    
    assert(((header_field_mark ? 1 : 0) +
            (header_value_mark ? 1 : 0) +
            (url_mark ? 1 : 0)  +
            (body_mark ? 1 : 0)) <= 1);
    
    CALLBACK_DATA_NOADVANCE(header_field);
    CALLBACK_DATA_NOADVANCE(header_value);
    CALLBACK_DATA_NOADVANCE(url);
    CALLBACK_DATA_NOADVANCE(body);
    
    return len;
    
error:
    if (HTTP_PARSER_ERRNO(parser) == HPE_OK) {
        SET_ERRNO(HPE_UNKNOWN);
    }
    
    return (p - data);
}


/* Does the parser need to see an EOF to find the end of the message? */
int
http_message_needs_eof (const http_parser *parser)
{
    if (parser->type == HTTP_REQUEST) {
        return 0;
    }
    
    /* See RFC 2616 section 4.4 */
    if (parser->status_code / 100 == 1 || /* 1xx e.g. Continue */
        parser->status_code == 204 ||     /* No Content */
        parser->status_code == 304 ||     /* Not Modified */
        parser->flags & F_SKIPBODY) {     /* response to a HEAD request */
        return 0;
    }
    
    if ((parser->flags & F_CHUNKED) || parser->content_length != ULLONG_MAX) {
        return 0;
    }
    
    return 1;
}


int
http_should_keep_alive (const http_parser *parser)
{
    if (parser->http_major > 0 && parser->http_minor > 0) {
        /* HTTP/1.1 */
        if (parser->flags & F_CONNECTION_CLOSE) {
            return 0;
        }
    } else {
        /* HTTP/1.0 or earlier */
        if (!(parser->flags & F_CONNECTION_KEEP_ALIVE)) {
            return 0;
        }
    }
    
    return !http_message_needs_eof(parser);
}


const char *
http_method_str (enum http_method m)
{
    return ELEM_AT(method_strings, m, "<unknown>");
}


void
http_parser_init (http_parser *parser, enum http_parser_type t)
{
    void *data = parser->data; /* preserve application data */
    memset(parser, 0, sizeof(*parser));
    parser->data = data;
    parser->type = t;
    parser->state = (t == HTTP_REQUEST ? s_start_req : (t == HTTP_RESPONSE ? s_start_res : s_start_req_or_res));
    parser->http_errno = HPE_OK;
}

const char *
http_errno_name(enum http_errno err) {
    assert(err < (sizeof(http_strerror_tab)/sizeof(http_strerror_tab[0])));
    return http_strerror_tab[err].name;
}

const char *
http_errno_description(enum http_errno err) {
    assert(err < (sizeof(http_strerror_tab)/sizeof(http_strerror_tab[0])));
    return http_strerror_tab[err].description;
}

static enum http_host_state
http_parse_host_char(enum http_host_state s, const char ch) {
    switch(s) {
        case s_http_userinfo:
        case s_http_userinfo_start:
            if (ch == '@') {
                return s_http_host_start;
            }
            
            if (IS_USERINFO_CHAR(ch)) {
                return s_http_userinfo;
            }
            break;
            
        case s_http_host_start:
            if (ch == '[') {
                return s_http_host_v6_start;
            }
            
            if (IS_HOST_CHAR(ch)) {
                return s_http_host;
            }
            
            break;
            
        case s_http_host:
            if (IS_HOST_CHAR(ch)) {
                return s_http_host;
            }
            
            /* FALLTHROUGH */
        case s_http_host_v6_end:
            if (ch == ':') {
                return s_http_host_port_start;
            }
            
            break;
            
        case s_http_host_v6:
            if (ch == ']') {
                return s_http_host_v6_end;
            }
            
            /* FALLTHROUGH */
        case s_http_host_v6_start:
            if (IS_HEX(ch) || ch == ':' || ch == '.') {
                return s_http_host_v6;
            }
            
            break;
            
        case s_http_host_port:
        case s_http_host_port_start:
            if (IS_NUM(ch)) {
                return s_http_host_port;
            }
            
            break;
            
        default:
            break;
    }
    return s_http_host_dead;
}

static int
http_parse_host(const char * buf, struct http_parser_url *u, int found_at) {
    enum http_host_state s;
    
    const char *p;
    size_t buflen = u->field_data[UF_HOST].off + u->field_data[UF_HOST].len;
    
    u->field_data[UF_HOST].len = 0;
    
    s = found_at ? s_http_userinfo_start : s_http_host_start;
    
    for (p = buf + u->field_data[UF_HOST].off; p < buf + buflen; p++) {
        enum http_host_state new_s = http_parse_host_char(s, *p);
        
        if (new_s == s_http_host_dead) {
            return 1;
        }
        
        switch(new_s) {
            case s_http_host:
                if (s != s_http_host) {
                    u->field_data[UF_HOST].off = p - buf;
                }
                u->field_data[UF_HOST].len++;
                break;
                
            case s_http_host_v6:
                if (s != s_http_host_v6) {
                    u->field_data[UF_HOST].off = p - buf;
                }
                u->field_data[UF_HOST].len++;
                break;
                
            case s_http_host_port:
                if (s != s_http_host_port) {
                    u->field_data[UF_PORT].off = p - buf;
                    u->field_data[UF_PORT].len = 0;
                    u->field_set |= (1 << UF_PORT);
                }
                u->field_data[UF_PORT].len++;
                break;
                
            case s_http_userinfo:
                if (s != s_http_userinfo) {
                    u->field_data[UF_USERINFO].off = p - buf;
                    u->field_data[UF_USERINFO].len = 0;
                    u->field_set |= (1 << UF_USERINFO);
                }
                u->field_data[UF_USERINFO].len++;
                break;
                
            default:
                break;
        }
        s = new_s;
    }
    
    /* Make sure we don't end somewhere unexpected */
    switch (s) {
        case s_http_host_start:
        case s_http_host_v6_start:
        case s_http_host_v6:
        case s_http_host_port_start:
        case s_http_userinfo:
        case s_http_userinfo_start:
            return 1;
        default:
            break;
    }
    
    return 0;
}

int
http_parser_parse_url(const char *buf, size_t buflen, int is_connect,
                      struct http_parser_url *u)
{
    enum state s;
    const char *p;
    enum http_parser_url_fields uf, old_uf;
    int found_at = 0;
    
    u->port = u->field_set = 0;
    s = is_connect ? s_req_server_start : s_req_spaces_before_url;
    uf = old_uf = UF_MAX;
    
    for (p = buf; p < buf + buflen; p++) {
        s = parse_url_char(s, *p);
        
        /* Figure out the next field that we're operating on */
        switch (s) {
            case s_dead:
                return 1;
                
                /* Skip delimeters */
            case s_req_schema_slash:
            case s_req_schema_slash_slash:
            case s_req_server_start:
            case s_req_query_string_start:
            case s_req_fragment_start:
                continue;
                
            case s_req_schema:
                uf = UF_SCHEMA;
                break;
                
            case s_req_server_with_at:
                found_at = 1;
                
                /* FALLTROUGH */
            case s_req_server:
                uf = UF_HOST;
                break;
                
            case s_req_path:
                uf = UF_PATH;
                break;
                
            case s_req_query_string:
                uf = UF_QUERY;
                break;
                
            case s_req_fragment:
                uf = UF_FRAGMENT;
                break;
                
            default:
                assert(!"Unexpected state");
                return 1;
        }
        
        /* Nothing's changed; soldier on */
        if (uf == old_uf) {
            u->field_data[uf].len++;
            continue;
        }
        
        u->field_data[uf].off = p - buf;
        u->field_data[uf].len = 1;
        
        u->field_set |= (1 << uf);
        old_uf = uf;
    }
    
    /* host must be present if there is a schema */
    /* parsing http:///toto will fail */
    if ((u->field_set & ((1 << UF_SCHEMA) | (1 << UF_HOST))) != 0) {
        if (http_parse_host(buf, u, found_at) != 0) {
            return 1;
        }
    }
    
    /* CONNECT requests can only contain "hostname:port" */
    if (is_connect && u->field_set != ((1 << UF_HOST)|(1 << UF_PORT))) {
        return 1;
    }
    
    if (u->field_set & (1 << UF_PORT)) {
        /* Don't bother with endp; we've already validated the string */
        unsigned long v = strtoul(buf + u->field_data[UF_PORT].off, NULL, 10);
        
        /* Ports have a max value of 2^16 */
        if (v > 0xffff) {
            return 1;
        }
        
        u->port = (uint16_t) v;
    }
    
    return 0;
}

void
http_parser_pause(http_parser *parser, int paused) {
    /* Users should only be pausing/unpausing a parser that is not in an error
     * state. In non-debug builds, there's not much that we can do about this
     * other than ignore it.
     */
    if (HTTP_PARSER_ERRNO(parser) == HPE_OK ||
        HTTP_PARSER_ERRNO(parser) == HPE_PAUSED) {
        SET_ERRNO((paused) ? HPE_PAUSED : HPE_OK);
    } else {
        assert(0 && "Attempting to pause parser in error state");
    }
}

int
http_body_is_final(const struct http_parser *parser) {
    return parser->state == s_message_done;
}

/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2017 SRS(ossrs)
 
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
    
    port = 0;
    schema = host = path = query = "";
    
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
	if(port<=0){
		port = 80;
	}
    
    path = get_uri_field(url, &hp_u, UF_PATH);
    srs_info("parse url %s success", purl);
    
    query = get_uri_field(url, &hp_u, UF_QUERY);
    srs_info("parse query %s success", query.c_str());
    
    return ret;
}

string SrsHttpUri::get_url()
{
    return url;
}

string SrsHttpUri::get_schema()
{
    return schema;
}

string SrsHttpUri::get_host()
{
    return host;
}

int SrsHttpUri::get_port()
{
    return port;
}

string SrsHttpUri::get_path()
{
    return path;
}

string SrsHttpUri::get_query()
{
    return query;
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


