//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_http_stack.hpp>

#include <algorithm>
#include <sstream>
#include <stdlib.h>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_utility.hpp>

#define SRS_HTTP_DEFAULT_PAGE "index.html"

// @see ISrsHttpMessage._http_ts_send_buffer
#define SRS_HTTP_TS_SEND_BUFFER_SIZE 4096

#define SRS_HTTP_AUTH_SCHEME_BASIC "Basic"
#define SRS_HTTP_AUTH_PREFIX_BASIC SRS_HTTP_AUTH_SCHEME_BASIC " "

// Calculate the output size needed to base64-encode x bytes to a null-terminated string.
#define SRS_AV_BASE64_SIZE(x) (((x) + 2) / 3 * 4 + 1)

// We use the standard encoding:
//      var StdEncoding = NewEncoding(encodeStd)
// StdEncoding is the standard base64 encoding, as defined in RFC 4648.
namespace
{
char padding = '=';
string encoder = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace
// @see golang encoding/base64/base64.go
srs_error_t srs_av_base64_decode(string cipher, string &plaintext)
{
    srs_error_t err = srs_success;

    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));

    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }

    // decode is like Decode but returns an additional 'end' value, which
    // indicates if end-of-message padding or a partial quantum was encountered
    // and thus any additional data is an error.
    int si = 0;

    // skip over newlines
    for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
    }

    for (bool end = false; si < (int)cipher.length() && !end;) {
        // Decode quantum using the base64 alphabet
        uint8_t dbuf[4];
        memset(dbuf, 0x00, sizeof(dbuf));

        int dinc = 3;
        int dlen = 4;
        srs_assert(dinc > 0);

        for (int j = 0; j < (int)sizeof(dbuf); j++) {
            if (si == (int)cipher.length()) {
                if (padding != -1 || j < 2) {
                    return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }

                dinc = j - 1;
                dlen = j;
                end = true;
                break;
            }

            char in = cipher.at(si);

            si++;
            // skip over newlines
            for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
            }

            if (in == padding) {
                // We've reached the end and there's padding
                switch (j) {
                case 0:
                case 1:
                    // incorrect padding
                    return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                case 2:
                    // "==" is expected, the first "=" is already consumed.
                    if (si == (int)cipher.length()) {
                        return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                    }
                    if (cipher.at(si) != padding) {
                        // incorrect padding
                        return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                    }

                    si++;
                    // skip over newlines
                    for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
                    }
                }

                if (si < (int)cipher.length()) {
                    // trailing garbage
                    err = srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }
                dinc = 3;
                dlen = j;
                end = true;
                break;
            }

            dbuf[j] = decodeMap[(uint8_t)in];
            if (dbuf[j] == 0xff) {
                return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
            }
        }

        // Convert 4x 6bit source bytes into 3 bytes
        uint32_t val = uint32_t(dbuf[0]) << 18 | uint32_t(dbuf[1]) << 12 | uint32_t(dbuf[2]) << 6 | uint32_t(dbuf[3]);
        if (dlen >= 2) {
            plaintext.append(1, char(val >> 16));
        }
        if (dlen >= 3) {
            plaintext.append(1, char(val >> 8));
        }
        if (dlen >= 4) {
            plaintext.append(1, char(val));
        }
    }

    return err;
}

// @see golang encoding/base64/base64.go
srs_error_t srs_av_base64_encode(std::string plaintext, std::string &cipher)
{
    srs_error_t err = srs_success;
    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));

    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }
    cipher.clear();

    uint32_t val = 0;
    int si = 0;
    int n = (plaintext.length() / 3) * 3;
    uint8_t *p = (uint8_t *)plaintext.c_str();
    while (si < n) {
        // Convert 3x 8bit source bytes into 4 bytes
        val = (uint32_t(p[si + 0]) << 16) | (uint32_t(p[si + 1]) << 8) | uint32_t(p[si + 2]);

        cipher += encoder[val >> 18 & 0x3f];
        cipher += encoder[val >> 12 & 0x3f];
        cipher += encoder[val >> 6 & 0x3f];
        cipher += encoder[val & 0x3f];

        si += 3;
    }

    int remain = plaintext.length() - si;
    if (0 == remain) {
        return err;
    }

    val = uint32_t(p[si + 0]) << 16;
    if (2 == remain) {
        val |= uint32_t(p[si + 1]) << 8;
    }

    cipher += encoder[val >> 18 & 0x3f];
    cipher += encoder[val >> 12 & 0x3f];

    switch (remain) {
    case 2:
        cipher += encoder[val >> 6 & 0x3f];
        cipher += padding;
        break;
    case 1:
        cipher += padding;
        cipher += padding;
        break;
    }

    return err;
}

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
    if (status >= SRS_CONSTS_HTTP_Continue && status < SRS_CONSTS_HTTP_OK) {
        return false;
    } else if (status == SRS_CONSTS_HTTP_NoContent || status == SRS_CONSTS_HTTP_NotModified) {
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
string srs_go_http_detect(char *data, int size)
{
    // TODO: Implement the request content-type detecting.
    return "application/octet-stream"; // fallback
}

srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code)
{
    return srs_go_http_error(w, code, srs_generate_http_status_text(code));
}

srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code, string error)
{
    srs_error_t err = srs_success;

    w->header()->set_content_type("text/plain; charset=utf-8");
    w->header()->set_content_length(error.length());
    w->write_header(code);

    if ((err = w->write((char *)error.data(), (int)error.length())) != srs_success) {
        return srs_error_wrap(err, "http write");
    }

    return err;
}

SrsHttpHeader::SrsHttpHeader()
{
}

SrsHttpHeader::~SrsHttpHeader()
{
}

void SrsHttpHeader::set(string key, string value)
{
    // Convert to UpperCamelCase, for example:
    //      transfer-encoding
    // transform to:
    //      Transfer-Encoding
    char pchar = 0;
    for (int i = 0; i < (int)key.length(); i++) {
        char ch = key.at(i);

        if (i == 0 || pchar == '-') {
            if (ch >= 'a' && ch <= 'z') {
                ((char *)key.data())[i] = ch - 32;
            }
        }
        pchar = ch;
    }

    if (headers.find(key) == headers.end()) {
        keys_.push_back(key);
    }

    headers[key] = value;
}

string SrsHttpHeader::get(string key)
{
    std::string v;

    map<string, string>::iterator it = headers.find(key);
    if (it != headers.end()) {
        v = it->second;
    }

    return v;
}

void SrsHttpHeader::del(string key)
{
    if (true) {
        vector<string>::iterator it = std::find(keys_.begin(), keys_.end(), key);
        if (it != keys_.end()) {
            it = keys_.erase(it);
        }
    }

    if (true) {
        map<string, string>::iterator it = headers.find(key);
        if (it != headers.end()) {
            headers.erase(it);
        }
    }
}

int SrsHttpHeader::count()
{
    return (int)headers.size();
}

void SrsHttpHeader::dumps(SrsJsonObject *o)
{
    vector<string>::iterator it;
    for (it = keys_.begin(); it != keys_.end(); ++it) {
        const string &key = *it;
        const string &value = headers[key];
        o->set(key, SrsJsonAny::str(value.c_str()));
    }
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
    set("Content-Length", srs_strconv_format_int(size));
}

string SrsHttpHeader::content_type()
{
    return get("Content-Type");
}

void SrsHttpHeader::set_content_type(string ct)
{
    set("Content-Type", ct);
}

void SrsHttpHeader::write(stringstream &ss)
{
    vector<string>::iterator it;
    for (it = keys_.begin(); it != keys_.end(); ++it) {
        const string &key = *it;
        const string &value = headers[key];
        ss << key << ": " << value << SRS_HTTP_CRLF;
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

ISrsHttpRequestWriter::ISrsHttpRequestWriter()
{
}

ISrsHttpRequestWriter::~ISrsHttpRequestWriter()
{
}

ISrsHttpHandler::ISrsHttpHandler()
{
    entry_ = NULL;
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

srs_error_t SrsHttpRedirectHandler::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    string location = url;
    if (!r->query().empty()) {
        location += "?" + r->query();
    }

    string msg = "Redirect to " + location;

    w->header()->set_content_type("text/plain; charset=utf-8");
    w->header()->set_content_length(msg.length());
    w->header()->set("Location", location);
    w->write_header(code);

    w->write((char *)msg.data(), (int)msg.length());
    w->final_request();

    srs_info("redirect to %s.", location.c_str());
    return srs_success;
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

srs_error_t SrsHttpNotFoundHandler::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound);
}

string srs_http_fs_fullpath(string dir, string pattern, string upath)
{
    // add default pages.
    if (srs_strings_ends_with(upath, "/")) {
        upath += SRS_HTTP_DEFAULT_PAGE;
    }

    // Remove the virtual directory.
    // For example:
    //      pattern=/api, the virtual directory is api, upath=/api/index.html, fullpath={dir}/index.html
    //      pattern=/api, the virtual directory is api, upath=/api/views/index.html, fullpath={dir}/views/index.html
    // The vhost prefix is ignored, for example:
    //      pattern=ossrs.net/api, the vhost is ossrs.net, the pattern equals to /api under this vhost,
    //      so the virtual directory is also api
    size_t pos = pattern.find("/");
    string filename = upath;
    if (upath.length() > pattern.length() && pos != string::npos) {
        filename = upath.substr(pattern.length() - pos);
    }

    string fullpath = srs_strings_trim_end(dir, "/");
    if (!srs_strings_starts_with(filename, "/")) {
        fullpath += "/";
    }
    fullpath += filename;

    return fullpath;
}

SrsHttpFileServer::SrsHttpFileServer(string root_dir)
{
    dir = root_dir;
    fs_factory = new ISrsFileReaderFactory();
    path_ = new SrsPath();
}

SrsHttpFileServer::~SrsHttpFileServer()
{
    srs_freep(fs_factory);
    srs_freep(path_);
}

void SrsHttpFileServer::set_fs_factory(ISrsFileReaderFactory *f)
{
    srs_freep(fs_factory);
    fs_factory = f;
}

void SrsHttpFileServer::set_path(SrsPath *v)
{
    srs_freep(path_);
    path_ = v;
}

srs_error_t SrsHttpFileServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_assert(entry_);

    // For each HTTP session, we use short-term HTTP connection.
    SrsHttpHeader *hdr = w->header();
    hdr->set("Connection", "Close");

    string upath = r->path();
    string fullpath = srs_http_fs_fullpath(dir, entry_->pattern, upath);
    SrsPath path;
    string basename = path.filepath_base(upath);

    // stat current dir, if exists, return error.
    if (!path_->exists(fullpath)) {
        srs_warn("http miss file=%s, pattern=%s, upath=%s",
                 fullpath.c_str(), entry_->pattern.c_str(), upath.c_str());
        return SrsHttpNotFoundHandler().serve_http(w, r);
    }
    srs_trace("http match file=%s, pattern=%s, upath=%s",
              fullpath.c_str(), entry_->pattern.c_str(), upath.c_str());

    // handle file according to its extension.
    // use vod stream for .flv/.fhv
    if (srs_strings_ends_with(upath, ".flv", ".fhv")) {
        return serve_flv_file(w, r, fullpath);
    } else if (srs_strings_ends_with(upath, ".m3u8")) {
        return serve_m3u8_ctx(w, r, fullpath);
    } else if (srs_strings_ends_with(upath, ".ts", ".m4s") || basename == "init.mp4") {
        return serve_ts_ctx(w, r, fullpath);
    } else if (srs_strings_ends_with(upath, ".mp4")) {
        return serve_mp4_file(w, r, fullpath);
    }

    // serve common static file.
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsFileReader> fs(fs_factory->create_file_reader());

    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "open file %s", fullpath.c_str());
    }

    // The length of bytes we could response to.
    int64_t length = fs->filesize() - fs->tellg();

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
        // For MPEG-DASH.
        _mime[".mpd"] = "application/dash+xml";
        _mime[".m4s"] = "video/iso.segment";
        _mime[".mp4v"] = "video/mp4";
    }

    if (true) {
        SrsPath path;
        std::string ext = path.filepath_ext(fullpath);

        if (_mime.find(ext) == _mime.end()) {
            w->header()->set_content_type("application/octet-stream");
        } else {
            w->header()->set_content_type(_mime[ext]);
        }
    }

    // Enter chunked mode, because we didn't set the content-length.
    w->write_header(SRS_CONSTS_HTTP_OK);

    // write body.
    int64_t left = length;
    if ((err = copy(w, fs.get(), r, left)) != srs_success) {
        return srs_error_wrap(err, "copy file=%s size=%" PRId64, fullpath.c_str(), left);
    }

    if ((err = w->final_request()) != srs_success) {
        return srs_error_wrap(err, "final request");
    }

    return err;
}

srs_error_t SrsHttpFileServer::serve_flv_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath)
{
    std::string start = r->query_get("start");
    if (start.empty()) {
        return serve_file(w, r, fullpath);
    }

    int64_t offset = ::atoll(start.c_str());
    if (offset <= 0) {
        return serve_file(w, r, fullpath);
    }

    return serve_flv_stream(w, r, fullpath, offset);
}

srs_error_t SrsHttpFileServer::serve_mp4_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath)
{
    // for flash to request mp4 range in query string.
    std::string range = r->query_get("range");
    // or, use bytes to request range.
    if (range.empty()) {
        range = r->query_get("bytes");
    }

    // Fetch range from header.
    SrsHttpHeader *h = r->header();
    if (range.empty() && h) {
        range = h->get("Range");
        if (range.find("bytes=") == 0) {
            range = range.substr(6);
        }
    }

    // rollback to serve whole file.
    size_t pos = string::npos;
    if (range.empty() || (pos = range.find("-")) == string::npos) {
        return serve_file(w, r, fullpath);
    }

    // parse the start in query string
    int64_t start = 0;
    if (pos > 0) {
        start = ::atoll(range.substr(0, pos).c_str());
    }

    // parse end in query string.
    int64_t end = -1;
    if (pos < range.length() - 1) {
        end = ::atoll(range.substr(pos + 1).c_str());
    }

    // invalid param, serve as whole mp4 file.
    if (start < 0 || (end != -1 && start > end)) {
        return serve_file(w, r, fullpath);
    }

    return serve_mp4_stream(w, r, fullpath, start, end);
}

srs_error_t SrsHttpFileServer::serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath, int64_t offset)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    // TODO: FIXME: Support range in header https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Range_requests
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath, int64_t start, int64_t end)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    // TODO: FIXME: Support range in header https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Range_requests
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::copy(ISrsHttpResponseWriter *w, SrsFileReader *fs, ISrsHttpMessage *r, int64_t size)
{
    srs_error_t err = srs_success;

    int64_t left = size;
    SrsUniquePtr<char[]> buf(new char[SRS_HTTP_TS_SEND_BUFFER_SIZE]);

    while (left > 0) {
        ssize_t nread = -1;
        int max_read = srs_min(left, SRS_HTTP_TS_SEND_BUFFER_SIZE);
        if ((err = fs->read(buf.get(), max_read, &nread)) != srs_success) {
            return srs_error_wrap(err, "read limit=%d, left=%" PRId64, max_read, left);
        }

        left -= nread;
        if ((err = w->write(buf.get(), (int)nread)) != srs_success) {
            return srs_error_wrap(err, "write limit=%d, bytes=%d, left=%" PRId64, max_read, (int)nread, left);
        }
    }

    return err;
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

ISrsHttpDynamicMatcher::ISrsHttpDynamicMatcher()
{
}

ISrsHttpDynamicMatcher::~ISrsHttpDynamicMatcher()
{
}

ISrsCommonHttpHandler::ISrsCommonHttpHandler()
{
}

ISrsCommonHttpHandler::~ISrsCommonHttpHandler()
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
    std::map<std::string, SrsHttpMuxEntry *>::iterator it;
    for (it = static_matchers_.begin(); it != static_matchers_.end(); ++it) {
        SrsHttpMuxEntry *entry = it->second;
        srs_freep(entry);
    }
    static_matchers_.clear();

    vhosts_.clear();
    dynamic_matchers_.clear();
}

srs_error_t SrsHttpServeMux::initialize()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Implements it.

    return err;
}

void SrsHttpServeMux::add_dynamic_matcher(ISrsHttpDynamicMatcher *h)
{
    std::vector<ISrsHttpDynamicMatcher *>::iterator it;
    it = std::find(dynamic_matchers_.begin(), dynamic_matchers_.end(), h);
    if (it != dynamic_matchers_.end()) {
        return;
    }
    dynamic_matchers_.push_back(h);
}

void SrsHttpServeMux::remove_dynamic_matcher(ISrsHttpDynamicMatcher *h)
{
    std::vector<ISrsHttpDynamicMatcher *>::iterator it;
    it = std::find(dynamic_matchers_.begin(), dynamic_matchers_.end(), h);
    if (it == dynamic_matchers_.end()) {
        return;
    }
    it = dynamic_matchers_.erase(it);
}

srs_error_t SrsHttpServeMux::handle(std::string pattern, ISrsHttpHandler *handler)
{
    srs_assert(handler);

    if (pattern.empty()) {
        return srs_error_new(ERROR_HTTP_PATTERN_EMPTY, "empty pattern");
    }

    if (static_matchers_.find(pattern) != static_matchers_.end()) {
        SrsHttpMuxEntry *exists = static_matchers_[pattern];
        if (exists->explicit_match) {
            return srs_error_new(ERROR_HTTP_PATTERN_DUPLICATED, "pattern=%s exists", pattern.c_str());
        }
    }

    std::string vhost = pattern;
    if (pattern.at(0) != '/') {
        if (pattern.find("/") != string::npos) {
            vhost = pattern.substr(0, pattern.find("/"));
        }
        vhosts_[vhost] = handler;
    }

    if (true) {
        SrsHttpMuxEntry *entry = new SrsHttpMuxEntry();
        entry->explicit_match = true;
        entry->handler = handler;
        entry->pattern = pattern;
        entry->handler->entry_ = entry;

        if (static_matchers_.find(pattern) != static_matchers_.end()) {
            SrsHttpMuxEntry *exists = static_matchers_[pattern];
            srs_freep(exists);
        }
        static_matchers_[pattern] = entry;
    }

    // Helpful behavior:
    // If pattern is /tree/, insert an implicit permanent redirect for /tree.
    // It can be overridden by an explicit registration.
    if (pattern != "/" && !pattern.empty() && pattern.at(pattern.length() - 1) == '/') {
        std::string rpattern = pattern.substr(0, pattern.length() - 1);
        SrsHttpMuxEntry *entry = NULL;

        // free the exists implicit entry
        if (static_matchers_.find(rpattern) != static_matchers_.end()) {
            entry = static_matchers_[rpattern];
        }

        // create implicit redirect.
        if (!entry || !entry->explicit_match) {
            srs_freep(entry);

            entry = new SrsHttpMuxEntry();
            entry->explicit_match = false;
            entry->handler = new SrsHttpRedirectHandler(pattern, SRS_CONSTS_HTTP_Found);
            entry->pattern = pattern;
            entry->handler->entry_ = entry;

            static_matchers_[rpattern] = entry;
        }
    }

    return srs_success;
}

void SrsHttpServeMux::unhandle(std::string pattern, ISrsHttpHandler *handler)
{
    if (true) {
        std::map<std::string, SrsHttpMuxEntry *>::iterator it = static_matchers_.find(pattern);
        if (it != static_matchers_.end()) {
            SrsHttpMuxEntry *entry = it->second;
            static_matchers_.erase(it);

            // We don't free the handler, because user should free it.
            if (entry->handler == handler) {
                entry->handler = NULL;
            }

            // Should always free the entry.
            srs_freep(entry);
        }
    }

    std::string vhost = pattern;
    if (pattern.at(0) != '/') {
        if (pattern.find("/") != string::npos) {
            vhost = pattern.substr(0, pattern.find("/"));
        }

        std::map<std::string, ISrsHttpHandler *>::iterator it = vhosts_.find(vhost);
        if (it != vhosts_.end())
            vhosts_.erase(it);
    }
}

srs_error_t SrsHttpServeMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    ISrsHttpHandler *h = NULL;
    if ((err = find_handler(r, &h)) != srs_success) {
        return srs_error_wrap(err, "find handler");
    }

    srs_assert(h);
    if ((err = h->serve_http(w, r)) != srs_success) {
        return srs_error_wrap(err, "serve http");
    }

    return err;
}

srs_error_t SrsHttpServeMux::find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: support the path . and ..
    if (r->url().find("..") != std::string::npos) {
        return srs_error_new(ERROR_HTTP_URL_NOT_CLEAN, "url %s not canonical", r->url().c_str());
    }

    if ((err = match(r, ph)) != srs_success) {
        return srs_error_wrap(err, "http match");
    }

    // always try to handle by dynamic matchers.
    if (!dynamic_matchers_.empty()) {
        // notify all dynamic matchers unless matching failed.
        std::vector<ISrsHttpDynamicMatcher *>::iterator it;
        for (it = dynamic_matchers_.begin(); it != dynamic_matchers_.end(); ++it) {
            ISrsHttpDynamicMatcher *matcher = *it;
            if ((err = matcher->dynamic_match(r, ph)) != srs_success) {
                return srs_error_wrap(err, "http dynamic match");
            }
        }
    }

    static ISrsHttpHandler *h404 = new SrsHttpNotFoundHandler();
    if (*ph == NULL) {
        *ph = h404;
    }

    return err;
}

srs_error_t SrsHttpServeMux::match(ISrsHttpMessage *r, ISrsHttpHandler **ph)
{
    std::string path = r->path();

    // Host-specific pattern takes precedence over generic ones
    if (!vhosts_.empty() && vhosts_.find(r->host()) != vhosts_.end()) {
        path = r->host() + path;
    }

    int nb_matched = 0;
    ISrsHttpHandler *h = NULL;

    std::map<std::string, SrsHttpMuxEntry *>::iterator it;
    for (it = static_matchers_.begin(); it != static_matchers_.end(); ++it) {
        std::string pattern = it->first;
        SrsHttpMuxEntry *entry = it->second;

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

    return srs_success;
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

ISrsHttpCorsMux::ISrsHttpCorsMux()
{
}

ISrsHttpCorsMux::~ISrsHttpCorsMux()
{
}

SrsHttpCorsMux::SrsHttpCorsMux(ISrsHttpHandler *h)
{
    enabled = false;
    required = false;
    next_ = h;
}

SrsHttpCorsMux::~SrsHttpCorsMux()
{
}

srs_error_t SrsHttpCorsMux::initialize(bool cros_enabled)
{
    enabled = cros_enabled;
    return srs_success;
}

srs_error_t SrsHttpCorsMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    // If CORS enabled, and there is a "Origin" header, it's CORS.
    if (enabled) {
        SrsHttpHeader *h = r->header();
        required = !h->get("Origin").empty();
    }

    // When CORS required, set the CORS headers.
    if (required) {
        SrsHttpHeader *h = w->header();
        // SRS does not need cookie or credentials, so we disable CORS credentials, and use * for CORS origin,
        // headers, expose headers and methods.
        h->set("Access-Control-Allow-Origin", "*");
        // See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Headers
        h->set("Access-Control-Allow-Headers", "*");
        // See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Methods
        h->set("Access-Control-Allow-Methods", "*");
        // See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Expose-Headers
        // Only the CORS-safelisted response headers are exposed by default. That is Cache-Control, Content-Language,
        // Content-Length, Content-Type, Expires, Last-Modified, Pragma.
        // See https://developer.mozilla.org/en-US/docs/Glossary/CORS-safelisted_response_header
        h->set("Access-Control-Expose-Headers", "*");
        // https://stackoverflow.com/a/24689738/17679565
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Credentials
        h->set("Access-Control-Allow-Credentials", "false");
        // CORS header for private network access, starting in Chrome 104
        h->set("Access-Control-Request-Private-Network", "true");
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

    return next_->serve_http(w, r);
}

ISrsHttpAuthMux::ISrsHttpAuthMux()
{
}

ISrsHttpAuthMux::~ISrsHttpAuthMux()
{
}

SrsHttpAuthMux::SrsHttpAuthMux(ISrsHttpHandler *h)
{
    next_ = h;
    enabled_ = false;
}

SrsHttpAuthMux::~SrsHttpAuthMux()
{
}

srs_error_t SrsHttpAuthMux::initialize(bool enabled, std::string username, std::string password)
{
    enabled_ = enabled;
    username_ = username;
    password_ = password;

    return srs_success;
}

srs_error_t SrsHttpAuthMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err;
    if ((err = do_auth(w, r)) != srs_success) {
        srs_warn("do_auth %s", srs_error_desc(err).c_str());
        srs_freep(err);
        w->write_header(SRS_CONSTS_HTTP_Unauthorized);
        return w->final_request();
    }

    srs_assert(next_);
    return next_->serve_http(w, r);
}

srs_error_t SrsHttpAuthMux::do_auth(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    if (!enabled_) {
        return err;
    }

    // We only apply for api starts with /api/ for HTTP API.
    // We don't apply for other apis such as /rtc/, for which we use http callback.
    if (r->path().find("/api/") == std::string::npos) {
        return err;
    }

    std::string auth = r->header()->get("Authorization");
    if (auth.empty()) {
        w->header()->set("WWW-Authenticate", SRS_HTTP_AUTH_SCHEME_BASIC);
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "empty Authorization");
    }

    if (!srs_strings_contains(auth, SRS_HTTP_AUTH_PREFIX_BASIC)) {
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "invalid auth %s, should start with %s", auth.c_str(), SRS_HTTP_AUTH_PREFIX_BASIC);
    }

    std::string token = srs_erase_first_substr(auth, SRS_HTTP_AUTH_PREFIX_BASIC);
    if (token.empty()) {
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "empty token from auth %s", auth.c_str());
    }

    std::string plaintext;
    if ((err = srs_av_base64_decode(token, plaintext)) != srs_success) {
        return srs_error_wrap(err, "decode token %s", token.c_str());
    }

    // The token format must be username:password
    std::vector<std::string> user_pwd = srs_strings_split(plaintext, ":");
    if (user_pwd.size() != 2) {
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "invalid token %s", plaintext.c_str());
    }

    if (username_ != user_pwd[0] || password_ != user_pwd[1]) {
        w->header()->set("WWW-Authenticate", SRS_HTTP_AUTH_SCHEME_BASIC);
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "invalid token %s:%s", user_pwd[0].c_str(), user_pwd[1].c_str());
    }

    return err;
}

ISrsHttpMessage::ISrsHttpMessage()
{
}

ISrsHttpMessage::~ISrsHttpMessage()
{
}

SrsHttpUri::SrsHttpUri()
{
    port_ = 0;
}

SrsHttpUri::~SrsHttpUri()
{
}

srs_error_t SrsHttpUri::initialize(string url)
{
    schema_ = host_ = path_ = query_ = fragment_ = "";
    url_ = url;

    // Replace the default vhost to a domain like string, or parse failed.
    string parsing_url = url;
    size_t pos_default_vhost = url.find("://__defaultVhost__");
    if (pos_default_vhost != string::npos) {
        parsing_url = srs_strings_replace(parsing_url, "://__defaultVhost__", "://safe.vhost.default.ossrs.io");
    }

    // Simple URL parser to replace http-parser URL parsing
    srs_error_t err = srs_success;
    if ((err = parse_url_simple(parsing_url, schema_, host_, port_, path_, query_, fragment_, username_, password_)) != srs_success) {
        return srs_error_wrap(err, "parse url %s as %s failed", url.c_str(), parsing_url.c_str());
    }

    // Restore the default vhost.
    if (pos_default_vhost != string::npos) {
        host_ = SRS_CONSTS_RTMP_DEFAULT_VHOST;
    }

    // Set default ports if not specified
    if (port_ <= 0) {
        if (schema_ == "https") {
            port_ = SRS_DEFAULT_HTTPS_PORT;
        } else if (schema_ == "rtmp") {
            port_ = SRS_CONSTS_RTMP_DEFAULT_PORT;
        } else if (schema_ == "redis") {
            port_ = SRS_DEFAULT_REDIS_PORT;
        } else {
            port_ = SRS_DEFAULT_HTTP_PORT;
        }
    }

    return parse_query();
}

void SrsHttpUri::set_schema(std::string v)
{
    schema_ = v;

    // Update url with new schema.
    size_t pos = url_.find("://");
    if (pos != string::npos) {
        url_ = schema_ + "://" + url_.substr(pos + 3);
    }
}

string SrsHttpUri::get_url()
{
    return url_;
}

string SrsHttpUri::get_schema()
{
    return schema_;
}

string SrsHttpUri::get_host()
{
    return host_;
}

int SrsHttpUri::get_port()
{
    return port_;
}

string SrsHttpUri::get_path()
{
    return path_;
}

string SrsHttpUri::get_query()
{
    return query_;
}

string SrsHttpUri::get_query_by_key(std::string key)
{
    map<string, string>::iterator it = query_values_.find(key);
    if (it == query_values_.end()) {
        return "";
    }
    return it->second;
}

std::string SrsHttpUri::get_fragment()
{
    return fragment_;
}

std::string SrsHttpUri::username()
{
    return username_;
}

std::string SrsHttpUri::password()
{
    return password_;
}

srs_error_t SrsHttpUri::parse_url_simple(const string &url, string &schema, string &host, int &port,
                                         string &path, string &query, string &fragment, string &username, string &password)
{
    // Simple URL parser: scheme://[userinfo@]host[:port][/path][?query][#fragment]
    schema = host = path = query = fragment = username = password = "";
    port = 0;

    size_t pos = 0;

    // Parse schema
    size_t schema_end = url.find("://", pos);
    if (schema_end == string::npos) {
        return srs_error_new(ERROR_HTTP_PARSE_URI, "invalid url, no schema: %s", url.c_str());
    }
    schema = url.substr(pos, schema_end);
    pos = schema_end + 3;

    // Find the end of authority (host:port or userinfo@host:port)
    size_t authority_end = url.find_first_of("/?#", pos);
    if (authority_end == string::npos) {
        authority_end = url.length();
    }
    string authority = url.substr(pos, authority_end - pos);
    pos = authority_end;

    // Parse userinfo if present
    size_t at_pos = authority.find('@');
    if (at_pos != string::npos) {
        string userinfo = authority.substr(0, at_pos);
        authority = authority.substr(at_pos + 1);

        size_t colon_pos = userinfo.find(':');
        if (colon_pos != string::npos) {
            username = userinfo.substr(0, colon_pos);
            password = userinfo.substr(colon_pos + 1);
        } else {
            username = userinfo;
        }
    }

    // Parse host and port
    if (authority.empty()) {
        return srs_error_new(ERROR_HTTP_PARSE_URI, "invalid url, no host: %s", url.c_str());
    }

    size_t colon_pos = authority.rfind(':');
    if (colon_pos != string::npos && colon_pos > 0) {
        // Check if this is actually a port (all digits after colon)
        string port_str = authority.substr(colon_pos + 1);
        bool is_port = !port_str.empty();
        for (size_t i = 0; i < port_str.length(); i++) {
            if (!isdigit(port_str[i])) {
                is_port = false;
                break;
            }
        }

        if (is_port) {
            host = authority.substr(0, colon_pos);
            port = atoi(port_str.c_str());
        } else {
            host = authority;
        }
    } else {
        host = authority;
    }

    // Parse path
    if (pos < url.length() && url[pos] == '/') {
        size_t path_end = url.find_first_of("?#", pos);
        if (path_end == string::npos) {
            path_end = url.length();
        }
        path = url.substr(pos, path_end - pos);
        pos = path_end;
    }

    // Parse query
    if (pos < url.length() && url[pos] == '?') {
        pos++; // skip '?'
        size_t query_end = url.find('#', pos);
        if (query_end == string::npos) {
            query_end = url.length();
        }
        query = url.substr(pos, query_end - pos);
        pos = query_end;
    }

    // Parse fragment
    if (pos < url.length() && url[pos] == '#') {
        pos++; // skip '#'
        fragment = url.substr(pos);
    }

    return srs_success;
}

srs_error_t SrsHttpUri::parse_query()
{
    srs_error_t err = srs_success;
    if (query_.empty()) {
        return err;
    }

    size_t begin = query_.find("?");
    if (string::npos != begin) {
        begin++;
    } else {
        begin = 0;
    }
    string query_str = query_.substr(begin);
    query_values_.clear();
    srs_net_url_parse_query(query_str, query_values_);

    return err;
}

// @see golang net/url/url.go
namespace
{
enum EncodeMode {
    encodePath,
    encodePathSegment,
    encodeHost,
    encodeZone,
    encodeUserPassword,
    encodeQueryComponent,
    encodeFragment,
};

bool should_escape(uint8_t c, EncodeMode mode)
{
    // §2.3 Unreserved characters (alphanum)
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
        return false;
    }

    if (encodeHost == mode || encodeZone == mode) {
        // §3.2.2 Host allows
        //	sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
        // as part of reg-name.
        // We add : because we include :port as part of host.
        // We add [ ] because we include [ipv6]:port as part of host.
        // We add < > because they're the only characters left that
        // we could possibly allow, and Parse will reject them if we
        // escape them (because hosts can't use %-encoding for
        // ASCII bytes).
        switch (c) {
        case '!':
        case '$':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case ';':
        case '=':
        case ':':
        case '[':
        case ']':
        case '<':
        case '>':
        case '"':
            return false;
        }
    }

    switch (c) {
    case '-':
    case '_':
    case '.':
    case '~': // §2.3 Unreserved characters (mark)
        return false;
    case '$':
    case '&':
    case '+':
    case ',':
    case '/':
    case ':':
    case ';':
    case '=':
    case '?':
    case '@': // §2.2 Reserved characters (reserved)
        // Different sections of the URL allow a few of
        // the reserved characters to appear unescaped.
        switch (mode) {
        case encodePath: // §3.3
            // The RFC allows : @ & = + $ but saves / ; , for assigning
            // meaning to individual path segments. This package
            // only manipulates the path as a whole, so we allow those
            // last three as well. That leaves only ? to escape.
            return c == '?';

        case encodePathSegment: // §3.3
            // The RFC allows : @ & = + $ but saves / ; , for assigning
            // meaning to individual path segments.
            return c == '/' || c == ';' || c == ',' || c == '?';

        case encodeUserPassword: // §3.2.1
            // The RFC allows ';', ':', '&', '=', '+', '$', and ',' in
            // userinfo, so we must escape only '@', '/', and '?'.
            // The parsing of userinfo treats ':' as special so we must escape
            // that too.
            return c == '@' || c == '/' || c == '?' || c == ':';

        case encodeQueryComponent: // §3.4
            // The RFC reserves (so we must escape) everything.
            return true;

        case encodeFragment: // §4.1
            // The RFC text is silent but the grammar allows
            // everything, so escape nothing.
            return false;
        default:
            break;
        }
    }

    if (mode == encodeFragment) {
        // RFC 3986 §2.2 allows not escaping sub-delims. A subset of sub-delims are
        // included in reserved from RFC 2396 §2.2. The remaining sub-delims do not
        // need to be escaped. To minimize potential breakage, we apply two restrictions:
        // (1) we always escape sub-delims outside of the fragment, and (2) we always
        // escape single quote to avoid breaking callers that had previously assumed that
        // single quotes would be escaped. See issue #19917.
        switch (c) {
        case '!':
        case '(':
        case ')':
        case '*':
            return false;
        }
    }

    // Everything else must be escaped.
    return true;
}

bool ishex(uint8_t c)
{
    if ('0' <= c && c <= '9') {
        return true;
    } else if ('a' <= c && c <= 'f') {
        return true;
    } else if ('A' <= c && c <= 'F') {
        return true;
    }
    return false;
}

uint8_t hex_to_num(uint8_t c)
{
    if ('0' <= c && c <= '9') {
        return c - '0';
    } else if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    } else if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

srs_error_t unescapse(string s, string &value, EncodeMode mode)
{
    srs_error_t err = srs_success;
    int n = 0;
    bool has_plus = false;
    int i = 0;
    // Count %, check that they're well-formed.
    while (i < (int)s.length()) {
        switch (s.at(i)) {
        case '%': {
            n++;
            if ((i + 2) >= (int)s.length() || !ishex(s.at(i + 1)) || !ishex(s.at(i + 2))) {
                string msg = s.substr(i);
                if (msg.length() > 3) {
                    msg = msg.substr(0, 3);
                }
                return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escape: %s", msg.c_str());
            }

            // Per https://tools.ietf.org/html/rfc3986#page-21
            // in the host component %-encoding can only be used
            // for non-ASCII bytes.
            // But https://tools.ietf.org/html/rfc6874#section-2
            // introduces %25 being allowed to escape a percent sign
            // in IPv6 scoped-address literals. Yay.
            if (encodeHost == mode && hex_to_num(s.at(i + 1)) < 8 && s.substr(i, 3) != "%25") {
                return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escap: %s", s.substr(i, 3).c_str());
            }

            if (encodeZone == mode) {
                // RFC 6874 says basically "anything goes" for zone identifiers
                // and that even non-ASCII can be redundantly escaped,
                // but it seems prudent to restrict %-escaped bytes here to those
                // that are valid host name bytes in their unescaped form.
                // That is, you can use escaping in the zone identifier but not
                // to introduce bytes you couldn't just write directly.
                // But Windows puts spaces here! Yay.
                uint8_t v = (hex_to_num(s.at(i + 1)) << 4) | (hex_to_num(s.at(i + 2)));
                if ("%25" != s.substr(i, 3) && ' ' != v && should_escape(v, encodeHost)) {
                    return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escap: %s", s.substr(i, 3).c_str());
                }
            }
            i += 3;
        } break;
        case '+':
            has_plus = encodeQueryComponent == mode;
            i++;
            break;
        default:
            if ((encodeHost == mode || encodeZone == mode) && ((uint8_t)s.at(i) < 0x80) && should_escape(s.at(i), mode)) {
                return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid character %u in host name", s.at(i));
            }
            i++;
            break;
        }
    }

    if (0 == n && !has_plus) {
        value = s;
        return err;
    }

    value.clear();
    // value.resize(s.length() - 2*n);
    for (int i = 0; i < (int)s.length(); ++i) {
        switch (s.at(i)) {
        case '%':
            value += (hex_to_num(s.at(i + 1)) << 4 | hex_to_num(s.at(i + 2)));
            i += 2;
            break;
        case '+':
            if (encodeQueryComponent == mode) {
                value += " ";
            } else {
                value += "+";
            }
            break;
        default:
            value += s.at(i);
            break;
        }
    }

    return srs_success;
}

string escape(string s, EncodeMode mode)
{
    int space_count = 0;
    int hex_count = 0;
    for (int i = 0; i < (int)s.length(); ++i) {
        uint8_t c = s.at(i);
        if (should_escape(c, mode)) {
            if (' ' == c && encodeQueryComponent == mode) {
                space_count++;
            } else {
                hex_count++;
            }
        }
    }

    if (0 == space_count && 0 == hex_count) {
        return s;
    }

    string value;
    if (0 == hex_count) {
        value = s;
        for (int i = 0; i < (int)s.length(); ++i) {
            if (' ' == s.at(i)) {
                value[i] = '+';
            }
        }
        return value;
    }

    // value.resize(s.length() + 2*hex_count);
    const char escape_code[] = "0123456789ABCDEF";
    // int j = 0;
    for (int i = 0; i < (int)s.length(); ++i) {
        uint8_t c = s.at(i);
        if (' ' == c && encodeQueryComponent == mode) {
            value += '+';
        } else if (should_escape(c, mode)) {
            value += '%';
            value += escape_code[c >> 4];
            value += escape_code[c & 15];
            // j += 3;
        } else {
            value += s[i];
        }
    }

    return value;
}

} // namespace

string SrsHttpUri::query_escape(std::string s)
{
    return escape(s, encodeQueryComponent);
}

string SrsHttpUri::path_escape(std::string s)
{
    return escape(s, encodePathSegment);
}

srs_error_t SrsHttpUri::query_unescape(std::string s, std::string &value)
{
    return unescapse(s, value, encodeQueryComponent);
}

srs_error_t SrsHttpUri::path_unescape(std::string s, std::string &value)
{
    return unescapse(s, value, encodePathSegment);
}
