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

#include <srs_rtsp_stack.hpp>

#include <stdlib.h>
#include <map>
using namespace std;

#include <srs_rtmp_io.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_consts.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

#define __SRS_RTSP_BUFFER 4096

// get the status text of code.
string srs_generate_rtsp_status_text(int status)
{
    static std::map<int, std::string> _status_map;
    if (_status_map.empty()) {
        _status_map[SRS_CONSTS_RTSP_Continue                       ] = SRS_CONSTS_RTSP_Continue_str                        ;      
        _status_map[SRS_CONSTS_RTSP_OK                             ] = SRS_CONSTS_RTSP_OK_str                              ;      
        _status_map[SRS_CONSTS_RTSP_Created                        ] = SRS_CONSTS_RTSP_Created_str                         ;      
        _status_map[SRS_CONSTS_RTSP_LowOnStorageSpace              ] = SRS_CONSTS_RTSP_LowOnStorageSpace_str               ;      
        _status_map[SRS_CONSTS_RTSP_MultipleChoices                ] = SRS_CONSTS_RTSP_MultipleChoices_str                 ;      
        _status_map[SRS_CONSTS_RTSP_MovedPermanently               ] = SRS_CONSTS_RTSP_MovedPermanently_str                ;      
        _status_map[SRS_CONSTS_RTSP_MovedTemporarily               ] = SRS_CONSTS_RTSP_MovedTemporarily_str                ;      
        _status_map[SRS_CONSTS_RTSP_SeeOther                       ] = SRS_CONSTS_RTSP_SeeOther_str                        ;      
        _status_map[SRS_CONSTS_RTSP_NotModified                    ] = SRS_CONSTS_RTSP_NotModified_str                     ;      
        _status_map[SRS_CONSTS_RTSP_UseProxy                       ] = SRS_CONSTS_RTSP_UseProxy_str                        ;      
        _status_map[SRS_CONSTS_RTSP_BadRequest                     ] = SRS_CONSTS_RTSP_BadRequest_str                      ;      
        _status_map[SRS_CONSTS_RTSP_Unauthorized                   ] = SRS_CONSTS_RTSP_Unauthorized_str                    ;      
        _status_map[SRS_CONSTS_RTSP_PaymentRequired                ] = SRS_CONSTS_RTSP_PaymentRequired_str                 ;      
        _status_map[SRS_CONSTS_RTSP_Forbidden                      ] = SRS_CONSTS_RTSP_Forbidden_str                       ;      
        _status_map[SRS_CONSTS_RTSP_NotFound                       ] = SRS_CONSTS_RTSP_NotFound_str                        ;      
        _status_map[SRS_CONSTS_RTSP_MethodNotAllowed               ] = SRS_CONSTS_RTSP_MethodNotAllowed_str                ;      
        _status_map[SRS_CONSTS_RTSP_NotAcceptable                  ] = SRS_CONSTS_RTSP_NotAcceptable_str                   ;      
        _status_map[SRS_CONSTS_RTSP_ProxyAuthenticationRequired    ] = SRS_CONSTS_RTSP_ProxyAuthenticationRequired_str     ;      
        _status_map[SRS_CONSTS_RTSP_RequestTimeout                 ] = SRS_CONSTS_RTSP_RequestTimeout_str                  ;      
        _status_map[SRS_CONSTS_RTSP_Gone                           ] = SRS_CONSTS_RTSP_Gone_str                            ;      
        _status_map[SRS_CONSTS_RTSP_LengthRequired                 ] = SRS_CONSTS_RTSP_LengthRequired_str                  ;      
        _status_map[SRS_CONSTS_RTSP_PreconditionFailed             ] = SRS_CONSTS_RTSP_PreconditionFailed_str              ;      
        _status_map[SRS_CONSTS_RTSP_RequestEntityTooLarge          ] = SRS_CONSTS_RTSP_RequestEntityTooLarge_str           ;      
        _status_map[SRS_CONSTS_RTSP_RequestURITooLarge             ] = SRS_CONSTS_RTSP_RequestURITooLarge_str              ;      
        _status_map[SRS_CONSTS_RTSP_UnsupportedMediaType           ] = SRS_CONSTS_RTSP_UnsupportedMediaType_str            ;      
        _status_map[SRS_CONSTS_RTSP_ParameterNotUnderstood         ] = SRS_CONSTS_RTSP_ParameterNotUnderstood_str          ;      
        _status_map[SRS_CONSTS_RTSP_ConferenceNotFound             ] = SRS_CONSTS_RTSP_ConferenceNotFound_str              ;      
        _status_map[SRS_CONSTS_RTSP_NotEnoughBandwidth             ] = SRS_CONSTS_RTSP_NotEnoughBandwidth_str              ;      
        _status_map[SRS_CONSTS_RTSP_SessionNotFound                ] = SRS_CONSTS_RTSP_SessionNotFound_str                 ;      
        _status_map[SRS_CONSTS_RTSP_MethodNotValidInThisState      ] = SRS_CONSTS_RTSP_MethodNotValidInThisState_str       ;      
        _status_map[SRS_CONSTS_RTSP_HeaderFieldNotValidForResource ] = SRS_CONSTS_RTSP_HeaderFieldNotValidForResource_str  ;      
        _status_map[SRS_CONSTS_RTSP_InvalidRange                   ] = SRS_CONSTS_RTSP_InvalidRange_str                    ;      
        _status_map[SRS_CONSTS_RTSP_ParameterIsReadOnly            ] = SRS_CONSTS_RTSP_ParameterIsReadOnly_str             ;      
        _status_map[SRS_CONSTS_RTSP_AggregateOperationNotAllowed   ] = SRS_CONSTS_RTSP_AggregateOperationNotAllowed_str    ;      
        _status_map[SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed  ] = SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed_str   ;      
        _status_map[SRS_CONSTS_RTSP_UnsupportedTransport           ] = SRS_CONSTS_RTSP_UnsupportedTransport_str            ;      
        _status_map[SRS_CONSTS_RTSP_DestinationUnreachable         ] = SRS_CONSTS_RTSP_DestinationUnreachable_str          ;      
        _status_map[SRS_CONSTS_RTSP_InternalServerError            ] = SRS_CONSTS_RTSP_InternalServerError_str             ;      
        _status_map[SRS_CONSTS_RTSP_NotImplemented                 ] = SRS_CONSTS_RTSP_NotImplemented_str                  ;      
        _status_map[SRS_CONSTS_RTSP_BadGateway                     ] = SRS_CONSTS_RTSP_BadGateway_str                      ;     
        _status_map[SRS_CONSTS_RTSP_ServiceUnavailable             ] = SRS_CONSTS_RTSP_ServiceUnavailable_str              ;     
        _status_map[SRS_CONSTS_RTSP_GatewayTimeout                 ] = SRS_CONSTS_RTSP_GatewayTimeout_str                  ;     
        _status_map[SRS_CONSTS_RTSP_RTSPVersionNotSupported        ] = SRS_CONSTS_RTSP_RTSPVersionNotSupported_str         ;     
        _status_map[SRS_CONSTS_RTSP_OptionNotSupported             ] = SRS_CONSTS_RTSP_OptionNotSupported_str              ;        
    }
    
    std::string status_text;
    if (_status_map.find(status) == _status_map.end()) {
        status_text = "Status Unknown";
    } else {
        status_text = _status_map[status];
    }
    
    return status_text;
}

std::string srs_generate_rtsp_method_str(SrsRtspMethod method) 
{
    switch (method) {
        case SrsRtspMethodDescribe: return __SRS_METHOD_DESCRIBE;
        case SrsRtspMethodAnnounce: return __SRS_METHOD_ANNOUNCE;
        case SrsRtspMethodGetParameter: return __SRS_METHOD_GET_PARAMETER;
        case SrsRtspMethodOptions: return __SRS_METHOD_OPTIONS;
        case SrsRtspMethodPause: return __SRS_METHOD_PAUSE;
        case SrsRtspMethodPlay: return __SRS_METHOD_PLAY;
        case SrsRtspMethodRecord: return __SRS_METHOD_RECORD;
        case SrsRtspMethodRedirect: return __SRS_METHOD_REDIRECT;
        case SrsRtspMethodSetup: return __SRS_METHOD_SETUP;
        case SrsRtspMethodSetParameter: return __SRS_METHOD_SET_PARAMETER;
        case SrsRtspMethodTeardown: return __SRS_METHOD_TEARDOWN;
        default: return "Unknown";
    }
}

SrsRtspRequest::SrsRtspRequest()
{
    seq = 0;
}

SrsRtspRequest::~SrsRtspRequest()
{
}

bool SrsRtspRequest::is_options()
{
    return method == __SRS_METHOD_OPTIONS;
}

SrsRtspResponse::SrsRtspResponse(int cseq)
{
    seq = cseq;
    status = SRS_CONSTS_RTSP_OK;
}

SrsRtspResponse::~SrsRtspResponse()
{
}

stringstream& SrsRtspResponse::encode(stringstream& ss)
{
    // status line
    ss << __SRS_VERSION << __SRS_RTSP_SP 
        << status << __SRS_RTSP_SP 
        << srs_generate_rtsp_status_text(status) << __SRS_RTSP_CRLF;

    // cseq
    ss << __SRS_TOKEN_CSEQ << ":" << __SRS_RTSP_SP << seq << __SRS_RTSP_CRLF;

    // others.
    ss << "Cache-Control: no-store" << __SRS_RTSP_CRLF
        << "Pragma: no-cache" << __SRS_RTSP_CRLF
        << "Server: " << RTMP_SIG_SRS_SERVER << __SRS_RTSP_CRLF;

    return ss;
}

SrsRtspOptionsResponse::SrsRtspOptionsResponse(int cseq) : SrsRtspResponse(cseq)
{
    methods = (SrsRtspMethod)(SrsRtspMethodDescribe | SrsRtspMethodOptions 
        | SrsRtspMethodPause | SrsRtspMethodPlay | SrsRtspMethodSetup | SrsRtspMethodTeardown
        | SrsRtspMethodAnnounce | SrsRtspMethodRecord);
}

SrsRtspOptionsResponse::~SrsRtspOptionsResponse()
{
}

stringstream& SrsRtspOptionsResponse::encode(stringstream& ss)
{
    SrsRtspResponse::encode(ss);

    SrsRtspMethod __methods[] = {
        SrsRtspMethodDescribe,
        SrsRtspMethodAnnounce,
        SrsRtspMethodGetParameter,
        SrsRtspMethodOptions,
        SrsRtspMethodPause,
        SrsRtspMethodPlay,
        SrsRtspMethodRecord,
        SrsRtspMethodRedirect,
        SrsRtspMethodSetup,
        SrsRtspMethodSetParameter,
        SrsRtspMethodTeardown,
    };

    ss << __SRS_TOKEN_PUBLIC << ":" << __SRS_RTSP_SP;

    bool appended = false;
    int nb_methods = (int)(sizeof(__methods) / sizeof(SrsRtspMethod));
    for (int i = 0; i < nb_methods; i++) {
        SrsRtspMethod method = __methods[i];
        if (((int)methods & (int)method) != (int)method) {
            continue;
        }

        if (appended) {
            ss << ", ";
        }
        ss << srs_generate_rtsp_method_str(method);
        appended = true;
    }
    ss << __SRS_RTSP_CRLF;

    // eof header.
    ss << __SRS_RTSP_CRLF;

    return ss;
}

SrsRtspStack::SrsRtspStack(ISrsProtocolReaderWriter* s)
{
    buf = new SrsSimpleBuffer();
    skt = s;
}

SrsRtspStack::~SrsRtspStack()
{
    srs_freep(buf);
}

int SrsRtspStack::recv_message(SrsRtspRequest** preq)
{
    int ret = ERROR_SUCCESS;

    SrsRtspRequest* req = new SrsRtspRequest();
    if ((ret = do_recv_message(req)) != ERROR_SUCCESS) {
        srs_freep(req);
        return ret;
    }

    *preq = req;

    return ret;
}

int SrsRtspStack::send_message(SrsRtspResponse* res)
{
    int ret = ERROR_SUCCESS;

    std::stringstream ss;
    // encode the message to string.
    res->encode(ss);

    std::string str = ss.str();
    srs_assert(!str.empty());

    if ((ret = skt->write((char*)str.c_str(), (int)str.length(), NULL)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: send response failed. ret=%d", ret);
        }
        return ret;
    }
    srs_info("rtsp: send response ok");

    return ret;
}

int SrsRtspStack::do_recv_message(SrsRtspRequest* req)
{
    int ret = ERROR_SUCCESS;

    // parse request line.
    if ((ret = recv_token_normal(req->method)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: parse method failed. ret=%d", ret);
        }
        return ret;
    }

    if ((ret = recv_token_normal(req->uri)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: parse uri failed. ret=%d", ret);
        }
        return ret;
    }

    if ((ret = recv_token_eof(req->version)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: parse version failed. ret=%d", ret);
        }
        return ret;
    }

    // parse headers.
    for (;;) {
        // parse the header name
        std::string token;
        if ((ret = recv_token_normal(token)) != ERROR_SUCCESS) {
            if (ret == ERROR_RTSP_REQUEST_HEADER_EOF) {
                ret = ERROR_SUCCESS;
                srs_info("rtsp: message header parsed");
                break;
            }
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("rtsp: parse token failed. ret=%d", ret);
            }
            return ret;
        }

        // parse the header value according by header name
        if (token == __SRS_TOKEN_CSEQ) {
            std::string seq;
            if ((ret = recv_token_eof(seq)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: parse seq failed. ret=%d", ret);
                }
                return ret;
            }
            req->seq = ::atoi(seq.c_str());
        } else {
            // unknown header name, parse util EOF.
            SrsRtspTokenState state = SrsRtspTokenStateNormal;
            while (state == SrsRtspTokenStateNormal) {
                std::string value;
                if ((ret = recv_token(value, state)) != ERROR_SUCCESS) {
                    if (!srs_is_client_gracefully_close(ret)) {
                        srs_error("rtsp: parse token failed. ret=%d", ret);
                    }
                    return ret;
                }
                srs_trace("rtsp: ignore header %s=%s", token.c_str(), value.c_str());
            }
        }
    }

    // parse body.

    return ret;
}

int SrsRtspStack::recv_token_normal(std::string& token)
{
    int ret = ERROR_SUCCESS;

    SrsRtspTokenState state;

    if ((ret = recv_token(token, state)) != ERROR_SUCCESS) {
        if (ret == ERROR_RTSP_REQUEST_HEADER_EOF) {
            return ret;
        }
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: parse token failed. ret=%d", ret);
        }
        return ret;
    }

    if (state != SrsRtspTokenStateNormal) {
        ret = ERROR_RTSP_TOKEN_NOT_NORMAL;
        srs_error("rtsp: parse normal token failed, state=%d. ret=%d", state, ret);
        return ret;
    }

    return ret;
}

int SrsRtspStack::recv_token_eof(std::string& token)
{
    int ret = ERROR_SUCCESS;

    SrsRtspTokenState state;

    if ((ret = recv_token(token, state)) != ERROR_SUCCESS) {
        if (ret == ERROR_RTSP_REQUEST_HEADER_EOF) {
            return ret;
        }
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: parse token failed. ret=%d", ret);
        }
        return ret;
    }

    if (state != SrsRtspTokenStateEOF) {
        ret = ERROR_RTSP_TOKEN_NOT_NORMAL;
        srs_error("rtsp: parse eof token failed, state=%d. ret=%d", state, ret);
        return ret;
    }

    return ret;
}

int SrsRtspStack::recv_token_util_eof(std::string& token)
{
    int ret = ERROR_SUCCESS;

    SrsRtspTokenState state;

    // use 0x00 as ignore the normal token flag.
    if ((ret = recv_token(token, state, 0x00)) != ERROR_SUCCESS) {
        if (ret == ERROR_RTSP_REQUEST_HEADER_EOF) {
            return ret;
        }
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("rtsp: parse token failed. ret=%d", ret);
        }
        return ret;
    }

    if (state != SrsRtspTokenStateEOF) {
        ret = ERROR_RTSP_TOKEN_NOT_NORMAL;
        srs_error("rtsp: parse eof token failed, state=%d. ret=%d", state, ret);
        return ret;
    }

    return ret;
}

int SrsRtspStack::recv_token(std::string& token, SrsRtspTokenState& state, char normal_ch)
{
    int ret = ERROR_SUCCESS;

    // whatever, default to error state.
    state = SrsRtspTokenStateError;

    // when buffer is empty, append bytes first.
    bool append_bytes = buf->length() == 0;

    // parse util token.
    for (;;) {
        // append bytes if required.
        if (append_bytes) {
            append_bytes = false;

            char buffer[__SRS_RTSP_BUFFER];
            ssize_t nb_read = 0;
            if ((ret = skt->read(buffer, __SRS_RTSP_BUFFER, &nb_read)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("rtsp: io read failed. ret=%d", ret);
                }
                return ret;
            }
            srs_info("rtsp: io read %d bytes", nb_read);

            buf->append(buffer, nb_read);
        }

        // parse one by one.
        char* start = buf->bytes();
        char* end = start + buf->length();
        char* p = start;

        // find util SP/CR/LF, max 2 EOF, to finger out the EOF of message.
        for (; p < end && p[0] != normal_ch && p[0] != __SRS_RTSP_CR && p[0] != __SRS_RTSP_LF; p++) {
        }

        // matched.
        if (p < end) {
            // finger out the state.
            if (p[0] == normal_ch) {
                state = SrsRtspTokenStateNormal;
            } else {
                state = SrsRtspTokenStateEOF;
            }
            
            // got the token.
            int nb_token = p - start;
            // trim last ':' character.
            if (nb_token && p[-1] == ':') {
                nb_token--;
            }
            if (nb_token) {
                token.append(start, nb_token);
            } else {
                ret = ERROR_RTSP_REQUEST_HEADER_EOF;
            }

            // ignore SP/CR/LF
            for (int i = 0; i < 2 && p < end && (p[0] == normal_ch || p[0] == __SRS_RTSP_CR || p[0] == __SRS_RTSP_LF); p++, i++) {
            }

            // consume the token bytes.
            srs_assert(p - start);
            buf->erase(p - start);
            break;
        }

        // append more and parse again.
        append_bytes = true;
    }

    return ret;
}

#endif

