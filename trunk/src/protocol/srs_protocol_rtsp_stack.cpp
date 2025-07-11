//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtsp_stack.hpp>
#include <srs_protocol_io.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>

#include <stdlib.h>
#include <map>
using namespace std;

#define SRS_RTSP_BUFFER 4096

// Forward declaration of RTCP detection function
extern bool srs_is_rtcp(const uint8_t* data, size_t len);

// get the status text of code.
string srs_generate_rtsp_status_text(int status)
{
    static std::map<int, std::string> _status_map;
    if (_status_map.empty()) {
        _status_map[SRS_CONSTS_RTSP_Continue] = SRS_CONSTS_RTSP_Continue_str;
        _status_map[SRS_CONSTS_RTSP_OK] = SRS_CONSTS_RTSP_OK_str;
        _status_map[SRS_CONSTS_RTSP_Created] = SRS_CONSTS_RTSP_Created_str;
        _status_map[SRS_CONSTS_RTSP_LowOnStorageSpace] = SRS_CONSTS_RTSP_LowOnStorageSpace_str;
        _status_map[SRS_CONSTS_RTSP_MultipleChoices] = SRS_CONSTS_RTSP_MultipleChoices_str;
        _status_map[SRS_CONSTS_RTSP_MovedPermanently] = SRS_CONSTS_RTSP_MovedPermanently_str;
        _status_map[SRS_CONSTS_RTSP_MovedTemporarily] = SRS_CONSTS_RTSP_MovedTemporarily_str;
        _status_map[SRS_CONSTS_RTSP_SeeOther] = SRS_CONSTS_RTSP_SeeOther_str;
        _status_map[SRS_CONSTS_RTSP_NotModified] = SRS_CONSTS_RTSP_NotModified_str;
        _status_map[SRS_CONSTS_RTSP_UseProxy] = SRS_CONSTS_RTSP_UseProxy_str;
        _status_map[SRS_CONSTS_RTSP_BadRequest] = SRS_CONSTS_RTSP_BadRequest_str;
        _status_map[SRS_CONSTS_RTSP_Unauthorized] = SRS_CONSTS_RTSP_Unauthorized_str;
        _status_map[SRS_CONSTS_RTSP_PaymentRequired] = SRS_CONSTS_RTSP_PaymentRequired_str;
        _status_map[SRS_CONSTS_RTSP_Forbidden] = SRS_CONSTS_RTSP_Forbidden_str;
        _status_map[SRS_CONSTS_RTSP_NotFound] = SRS_CONSTS_RTSP_NotFound_str;
        _status_map[SRS_CONSTS_RTSP_MethodNotAllowed] = SRS_CONSTS_RTSP_MethodNotAllowed_str;
        _status_map[SRS_CONSTS_RTSP_NotAcceptable] = SRS_CONSTS_RTSP_NotAcceptable_str;
        _status_map[SRS_CONSTS_RTSP_ProxyAuthenticationRequired] = SRS_CONSTS_RTSP_ProxyAuthenticationRequired_str;
        _status_map[SRS_CONSTS_RTSP_RequestTimeout] = SRS_CONSTS_RTSP_RequestTimeout_str;
        _status_map[SRS_CONSTS_RTSP_Gone] = SRS_CONSTS_RTSP_Gone_str;
        _status_map[SRS_CONSTS_RTSP_LengthRequired] = SRS_CONSTS_RTSP_LengthRequired_str;
        _status_map[SRS_CONSTS_RTSP_PreconditionFailed] = SRS_CONSTS_RTSP_PreconditionFailed_str;
        _status_map[SRS_CONSTS_RTSP_RequestEntityTooLarge] = SRS_CONSTS_RTSP_RequestEntityTooLarge_str;
        _status_map[SRS_CONSTS_RTSP_RequestURITooLarge] = SRS_CONSTS_RTSP_RequestURITooLarge_str;
        _status_map[SRS_CONSTS_RTSP_UnsupportedMediaType] = SRS_CONSTS_RTSP_UnsupportedMediaType_str;
        _status_map[SRS_CONSTS_RTSP_ParameterNotUnderstood] = SRS_CONSTS_RTSP_ParameterNotUnderstood_str;
        _status_map[SRS_CONSTS_RTSP_ConferenceNotFound] = SRS_CONSTS_RTSP_ConferenceNotFound_str;
        _status_map[SRS_CONSTS_RTSP_NotEnoughBandwidth] = SRS_CONSTS_RTSP_NotEnoughBandwidth_str;
        _status_map[SRS_CONSTS_RTSP_SessionNotFound] = SRS_CONSTS_RTSP_SessionNotFound_str;
        _status_map[SRS_CONSTS_RTSP_MethodNotValidInThisState] = SRS_CONSTS_RTSP_MethodNotValidInThisState_str;
        _status_map[SRS_CONSTS_RTSP_HeaderFieldNotValidForResource] = SRS_CONSTS_RTSP_HeaderFieldNotValidForResource_str;
        _status_map[SRS_CONSTS_RTSP_InvalidRange] = SRS_CONSTS_RTSP_InvalidRange_str;
        _status_map[SRS_CONSTS_RTSP_ParameterIsReadOnly] = SRS_CONSTS_RTSP_ParameterIsReadOnly_str;
        _status_map[SRS_CONSTS_RTSP_AggregateOperationNotAllowed] = SRS_CONSTS_RTSP_AggregateOperationNotAllowed_str;
        _status_map[SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed] = SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed_str;
        _status_map[SRS_CONSTS_RTSP_UnsupportedTransport] = SRS_CONSTS_RTSP_UnsupportedTransport_str;
        _status_map[SRS_CONSTS_RTSP_DestinationUnreachable] = SRS_CONSTS_RTSP_DestinationUnreachable_str;
        _status_map[SRS_CONSTS_RTSP_InternalServerError] = SRS_CONSTS_RTSP_InternalServerError_str;
        _status_map[SRS_CONSTS_RTSP_NotImplemented] = SRS_CONSTS_RTSP_NotImplemented_str;
        _status_map[SRS_CONSTS_RTSP_BadGateway] = SRS_CONSTS_RTSP_BadGateway_str;
        _status_map[SRS_CONSTS_RTSP_ServiceUnavailable] = SRS_CONSTS_RTSP_ServiceUnavailable_str;
        _status_map[SRS_CONSTS_RTSP_GatewayTimeout] = SRS_CONSTS_RTSP_GatewayTimeout_str;
        _status_map[SRS_CONSTS_RTSP_RTSPVersionNotSupported] = SRS_CONSTS_RTSP_RTSPVersionNotSupported_str;
        _status_map[SRS_CONSTS_RTSP_OptionNotSupported] = SRS_CONSTS_RTSP_OptionNotSupported_str;
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
        case SrsRtspMethodDescribe: return SRS_RTSP_METHOD_DESCRIBE;
        case SrsRtspMethodAnnounce: return SRS_RTSP_METHOD_ANNOUNCE;
        case SrsRtspMethodGetParameter: return SRS_RTSP_METHOD_GET_PARAMETER;
        case SrsRtspMethodOptions: return SRS_RTSP_METHOD_OPTIONS;
        case SrsRtspMethodPause: return SRS_RTSP_METHOD_PAUSE;
        case SrsRtspMethodPlay: return SRS_RTSP_METHOD_PLAY;
        case SrsRtspMethodRecord: return SRS_RTSP_METHOD_RECORD;
        case SrsRtspMethodRedirect: return SRS_RTSP_METHOD_REDIRECT;
        case SrsRtspMethodSetup: return SRS_RTSP_METHOD_SETUP;
        case SrsRtspMethodSetParameter: return SRS_RTSP_METHOD_SET_PARAMETER;
        case SrsRtspMethodTeardown: return SRS_RTSP_METHOD_TEARDOWN;
        default: return "Unknown";
    }
}

SrsRtspTransport::SrsRtspTransport()
{
    client_port_min = 0;
    client_port_max = 0;
    interleaved_min = 0;
    interleaved_max = 0;
}

SrsRtspTransport::~SrsRtspTransport()
{
}

srs_error_t SrsRtspTransport::parse(string attr)
{
    srs_error_t err = srs_success;
    
    size_t pos = string::npos;
    std::string token = attr;
    
    while (!token.empty()) {
        std::string item = token;
        if ((pos = item.find(";")) != string::npos) {
            item = token.substr(0, pos);
            token = token.substr(pos + 1);
        } else {
            token = "";
        }
        
        std::string item_key = item, item_value;
        if ((pos = item.find("=")) != string::npos) {
            item_key = item.substr(0, pos);
            item_value = item.substr(pos + 1);
        }
        
        if (transport.empty() && item.find("=") == string::npos && item_key != "unicast" && item_key != "multicast") {
            transport = item_key;
            if ((pos = transport.find("/")) != string::npos) {
                profile = transport.substr(pos + 1);
                transport = transport.substr(0, pos);
            }
            if ((pos = profile.find("/")) != string::npos) {
                lower_transport = profile.substr(pos + 1);
                profile = profile.substr(0, pos);
            }
        }
        
        if (item_key == "unicast" || item_key == "multicast") {
            cast_type = item_key;
        } else if (item_key == "interleaved") {
            interleaved = item_value;
            if ((pos = interleaved.find("-")) != string::npos) {
                interleaved_min = ::atoi(interleaved.substr(0, pos).c_str());
                interleaved_max = ::atoi(interleaved.substr(pos + 1).c_str());
            }
        } else if (item_key == "mode") {
            mode = item_value;
        } else if (item_key == "client_port") {
            std::string sport = item_value;
            std::string eport = item_value;
            if ((pos = eport.find("-")) != string::npos) {
                sport = eport.substr(0, pos);
                eport = eport.substr(pos + 1);
            }
            client_port_min = ::atoi(sport.c_str());
            client_port_max = ::atoi(eport.c_str());
        }
    }
    
    return err;
}

void SrsRtspTransport::copy(SrsRtspTransport *src)
{
    transport = src->transport;
    profile = src->profile;
    lower_transport = src->lower_transport;
    cast_type = src->cast_type;
    interleaved = src->interleaved;
    mode = src->mode;
}

SrsRtspRequest::SrsRtspRequest()
{
    seq = 0;
    content_length = 0;
    stream_id = 0;
    transport = NULL;
}

SrsRtspRequest::~SrsRtspRequest()
{
    srs_freep(transport);
}

bool SrsRtspRequest::is_options()
{
    return method == SRS_RTSP_METHOD_OPTIONS;
}

bool SrsRtspRequest::is_describe()
{
    return method == SRS_RTSP_METHOD_DESCRIBE;
}

bool SrsRtspRequest::is_setup()
{
    return method == SRS_RTSP_METHOD_SETUP;
}

bool SrsRtspRequest::is_play()
{
    return method == SRS_RTSP_METHOD_PLAY;
}

bool SrsRtspRequest::is_teardown()
{
    return method == SRS_RTSP_METHOD_TEARDOWN;
}

SrsRtspResponse::SrsRtspResponse(int cseq)
{
    seq = cseq;
    status = SRS_CONSTS_RTSP_OK;
}

SrsRtspResponse::~SrsRtspResponse()
{
}

srs_error_t SrsRtspResponse::encode(stringstream& ss)
{
    srs_error_t err = srs_success;
    
    // status line
    ss << SRS_RTSP_VERSION << SRS_RTSP_SP
    << status << SRS_RTSP_SP
    << srs_generate_rtsp_status_text(status) << SRS_RTSP_CRLF;
    
    // cseq
    ss << SRS_RTSP_TOKEN_CSEQ << ":" << SRS_RTSP_SP << seq << SRS_RTSP_CRLF;
    
    // others.
    ss << "Cache-Control: no-store" << SRS_RTSP_CRLF
    << "Pragma: no-cache" << SRS_RTSP_CRLF
    << "Server: " << RTMP_SIG_SRS_SERVER << SRS_RTSP_CRLF;
    
    // session if specified.
    if (!session.empty()) {
        ss << SRS_RTSP_TOKEN_SESSION << ":" << SRS_RTSP_SP << session << SRS_RTSP_CRLF;
    }
    
    if ((err = encode_header(ss)) != srs_success) {
        return srs_error_wrap(err, "encode header");
    };
    
    // header EOF.
    ss << SRS_RTSP_CRLF;
    
    return err;
}

srs_error_t SrsRtspResponse::encode_header(std::stringstream& ss)
{
    return srs_success;
}

SrsRtspOptionsResponse::SrsRtspOptionsResponse(int cseq) : SrsRtspResponse(cseq)
{
    methods = (SrsRtspMethod)(SrsRtspMethodDescribe | SrsRtspMethodOptions
        | SrsRtspMethodPlay | SrsRtspMethodSetup | SrsRtspMethodTeardown);
}

SrsRtspOptionsResponse::~SrsRtspOptionsResponse()
{
}

srs_error_t SrsRtspOptionsResponse::encode_header(stringstream& ss)
{
    static const SrsRtspMethod rtsp_methods[] = {
        SrsRtspMethodDescribe,
        SrsRtspMethodGetParameter,
        SrsRtspMethodOptions,
        SrsRtspMethodPause,
        SrsRtspMethodPlay,
        SrsRtspMethodRedirect,
        SrsRtspMethodSetup,
        SrsRtspMethodSetParameter,
        SrsRtspMethodTeardown,
    };
    
    ss << SRS_RTSP_TOKEN_PUBLIC << ":" << SRS_RTSP_SP;
    
    bool appended = false;
    int nb_methods = (int)(sizeof(rtsp_methods) / sizeof(SrsRtspMethod));
    for (int i = 0; i < nb_methods; i++) {
        SrsRtspMethod method = rtsp_methods[i];
        if (((int)methods & (int)method) == 0) {
            continue;
        }
        
        if (appended) {
            ss << ", ";
        }
        ss << srs_generate_rtsp_method_str(method);
        appended = true;
    }
    ss << SRS_RTSP_CRLF;
    
    return srs_success;
}

SrsRtspDescribeResponse::SrsRtspDescribeResponse(int cseq) : SrsRtspResponse(cseq)
{
}

SrsRtspDescribeResponse::~SrsRtspDescribeResponse()
{
}

srs_error_t SrsRtspDescribeResponse::encode_header(stringstream& ss)
{
    ss << SRS_RTSP_TOKEN_CONTENT_TYPE << ":" << SRS_RTSP_SP << "application/sdp" << SRS_RTSP_CRLF;
    // WILL add CRLF to the end of sdp in SrsRtspResponse::encode, so add 2.
    ss << SRS_RTSP_TOKEN_CONTENT_LENGTH << ":" << SRS_RTSP_SP << sdp.length() + 2 << SRS_RTSP_CRLF;
    ss << SRS_RTSP_CRLF;
    ss << sdp;
    return srs_success;
}

SrsRtspSetupResponse::SrsRtspSetupResponse(int seq) : SrsRtspResponse(seq)
{
    transport = new SrsRtspTransport();
    local_port_min = 0;
    local_port_max = 0;

    client_port_min = 0;
    client_port_max = 0;
}

SrsRtspSetupResponse::~SrsRtspSetupResponse()
{
    srs_freep(transport);
}

srs_error_t SrsRtspSetupResponse::encode_header(stringstream& ss)
{
    ss << SRS_RTSP_TOKEN_TRANSPORT << ":" << SRS_RTSP_SP;
    ss << transport->transport << "/" << transport->profile;
    if (!transport->lower_transport.empty()) {
        ss << "/" << transport->lower_transport;
    }
    
    if (!transport->cast_type.empty()) {
        ss << ";" << transport->cast_type;
    }

    if (!transport->interleaved.empty()) {
        ss << ";interleaved=" << transport->interleaved;
    }

    if (transport->lower_transport != "TCP") {
        ss << ";client_port=" << client_port_min << "-" << client_port_max;
        ss << ";server_port=" << local_port_min << "-" << local_port_max;
    }

    ss << ";ssrc=" << ssrc << ";mode=\"play\"";

    ss << SRS_RTSP_CRLF;

    return srs_success;
}

SrsRtspPlayResponse::SrsRtspPlayResponse(int cseq) : SrsRtspResponse(cseq)
{
}

SrsRtspPlayResponse::~SrsRtspPlayResponse()
{
}

srs_error_t SrsRtspPlayResponse::encode_header(stringstream& ss)
{
    return srs_success;
}

SrsRtspStack::SrsRtspStack(ISrsProtocolReadWriter* s)
{
    buf = new SrsSimpleStream();
    skt = s;
}

SrsRtspStack::~SrsRtspStack()
{
    srs_freep(buf);
}

srs_error_t SrsRtspStack::recv_message(SrsRtspRequest** preq)
{
    srs_error_t err = srs_success;
    
    SrsRtspRequest* req = new SrsRtspRequest();
    if ((err = do_recv_message(req)) != srs_success) {
        srs_freep(req);
        return srs_error_wrap(err, "recv message");
    }
    
    *preq = req;
    
    return err;
}

srs_error_t SrsRtspStack::send_message(SrsRtspResponse* res)
{
    srs_error_t err = srs_success;
    
    std::stringstream ss;
    // encode the message to string.
    if ((err = res->encode(ss)) != srs_success) {
        return srs_error_wrap(err, "encode message");
    }
    
    std::string str = ss.str();
    srs_assert(!str.empty());
    
    if ((err = skt->write((char*)str.c_str(), (int)str.length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "write message");
    }
    
    return err;
}

srs_error_t SrsRtspStack::do_recv_message(SrsRtspRequest* req)
{
    srs_error_t err = srs_success;

    // Parse RTSP request line: "METHOD URI VERSION"
    // Example: "PLAY rtsp://example.com/stream RTSP/1.0"

    // Parse the RTSP method (PLAY, SETUP, DESCRIBE, etc.)
    if ((err = recv_token_normal(req->method)) != srs_success) {
        return srs_error_wrap(err, "method");
    }

    // Parse the request URI (resource path or full URL)
    if ((err = recv_token_normal(req->uri)) != srs_success) {
        return srs_error_wrap(err, "uri");
    }

    // Parse the RTSP version (typically "RTSP/1.0")
    if ((err = recv_token_eof(req->version)) != srs_success) {
        return srs_error_wrap(err, "version");
    }
    
    // Parse RTSP headers in "Name: Value" format
    // Example headers:
    //   CSeq: 1
    //   Content-Type: application/sdp
    //   Content-Length: 460
    //   Transport: RTP/AVP;unicast;client_port=8000-8001
    //   Session: 12345678
    for (;;) {
        // Parse the header name (before the colon)
        std::string token;
        if ((err = recv_token_normal(token)) != srs_success) {
            if (srs_error_code(err) == ERROR_RTSP_REQUEST_HEADER_EOF) {
                srs_error_reset(err);
                break; // End of headers reached (empty line)
            }
            return srs_error_wrap(err, "recv token");
        }

        // Parse the header value (after the colon) based on header name
        if (token == SRS_RTSP_TOKEN_CSEQ) {
            // CSeq: sequence number for request/response matching
            std::string seq;
            if ((err = recv_token_eof(seq)) != srs_success) {
                return srs_error_wrap(err, "seq");
            }
            req->seq = ::atoll(seq.c_str());
        } else if (token == SRS_RTSP_TOKEN_CONTENT_TYPE) {
            // Content-Type: MIME type of the message body (e.g., application/sdp)
            std::string ct;
            if ((err = recv_token_eof(ct)) != srs_success) {
                return srs_error_wrap(err, "ct");
            }
            req->content_type = ct;
        } else if (token == SRS_RTSP_TOKEN_CONTENT_LENGTH) {
            // Content-Length: size of the message body in bytes
            std::string cl;
            if ((err = recv_token_eof(cl)) != srs_success) {
                return srs_error_wrap(err, "cl");
            }
            req->content_length = ::atoll(cl.c_str());
        } else if (token == SRS_RTSP_TOKEN_TRANSPORT) {
            // Transport: RTP transport parameters (protocol, ports, etc.)
            std::string transport;
            if ((err = recv_token_eof(transport)) != srs_success) {
                return srs_error_wrap(err, "transport");
            }
            if (!req->transport) {
                req->transport = new SrsRtspTransport();
            }
            if ((err = req->transport->parse(transport)) != srs_success) {
                return srs_error_wrap(err, "parse transport=%s", transport.c_str());
            }
        } else if (token == SRS_RTSP_TOKEN_SESSION) {
            // Session: session identifier for maintaining state
            if ((err = recv_token_eof(req->session)) != srs_success) {
                return srs_error_wrap(err, "session");
            }
        } else if (token == SRS_RTSP_TOKEN_ACCEPT) {
            // Accept: acceptable media types for the response
            if ((err = recv_token_eof(req->accept)) != srs_success) {
                return srs_error_wrap(err, "accept");
            }
        } else if (token == SRS_RTSP_TOKEN_USER_AGENT) {
            // User-Agent: client software identification
            if ((err = recv_token_util_eof(req->user_agent)) != srs_success) {
                return srs_error_wrap(err, "user_agent");
            }
        } else if (token == SRS_RTSP_TOKEN_RANGE) {
            // Range: time range for playback (e.g., npt=0-30)
            if ((err = recv_token_eof(req->range)) != srs_success) {
                return srs_error_wrap(err, "range");
            }
        } else {
            // unknown header name, parse util EOF.
            std::string value;
            if ((err = recv_token_util_eof(value)) != srs_success) {
                return srs_error_wrap(err, "state");
            }
            srs_trace("rtsp: ignore header %s=%s", token.c_str(), value.c_str());
        }
    }

    // for setup, parse the stream id from uri.
    if (req->is_setup()) {
        size_t pos = string::npos;
        std::string stream_id = srs_path_basename(req->uri);
        if ((pos = stream_id.find("=")) != string::npos) {
            stream_id = stream_id.substr(pos + 1);
        }
        req->stream_id = ::atoi(stream_id.c_str());
        srs_info("rtsp: setup stream id=%d", req->stream_id);
    }
    
    return err;
}

srs_error_t SrsRtspStack::recv_token_normal(std::string& token)
{
    srs_error_t err = srs_success;
    
    SrsRtspTokenState state;
    
    if ((err = recv_token(token, state)) != srs_success) {
        if (srs_error_code(err) == ERROR_RTSP_REQUEST_HEADER_EOF) {
            return srs_error_wrap(err, "EOF");
        }
        return srs_error_wrap(err, "recv token");
    }
    
    if (state != SrsRtspTokenStateNormal) {
        return srs_error_new(ERROR_RTSP_TOKEN_NOT_NORMAL, "invalid state=%d", state);
    }
    
    return err;
}

srs_error_t SrsRtspStack::recv_token_eof(std::string& token)
{
    srs_error_t err = srs_success;
    
    SrsRtspTokenState state;
    
    if ((err = recv_token(token, state)) != srs_success) {
        if (srs_error_code(err) == ERROR_RTSP_REQUEST_HEADER_EOF) {
            return srs_error_wrap(err, "EOF");
        }
        return srs_error_wrap(err, "recv token");
    }
    
    if (state != SrsRtspTokenStateEOF) {
        return srs_error_new(ERROR_RTSP_TOKEN_NOT_NORMAL, "invalid state=%d", state);
    }
    
    return err;
}

srs_error_t SrsRtspStack::recv_token_util_eof(std::string& token, int* pconsumed)
{
    srs_error_t err = srs_success;
    
    SrsRtspTokenState state;
    
    // use 0x00 as ignore the normal token flag.
    if ((err = recv_token(token, state, 0x00, pconsumed)) != srs_success) {
        if (srs_error_code(err) == ERROR_RTSP_REQUEST_HEADER_EOF) {
            return srs_error_wrap(err, "EOF");
        }
        return srs_error_wrap(err, "recv token");
    }
    
    if (state != SrsRtspTokenStateEOF) {
        return srs_error_new(ERROR_RTSP_TOKEN_NOT_NORMAL, "invalid state=%d", state);
    }
    
    return err;
}

srs_error_t SrsRtspStack::recv_token(std::string& token, SrsRtspTokenState& state, char normal_ch, int* pconsumed)
{
    srs_error_t err = srs_success;
    
    // whatever, default to error state.
    state = SrsRtspTokenStateError;
    
    // when buffer is empty, append bytes first.
    bool append_bytes = buf->length() == 0;
    
    // parse util token.
    for (;;) {
        // append bytes if required.
        if (append_bytes) {
            append_bytes = false;

            char buffer[SRS_RTSP_BUFFER];
            ssize_t nb_read = 0;
            if ((err = skt->read(buffer, SRS_RTSP_BUFFER, &nb_read)) != srs_success) {
                return srs_error_wrap(err, "recv data");
            }

            buf->append(buffer, (int)nb_read);
        }

        // Try to detect and consume any RTCP frames from the buffer
        while (buf->length() > 0) {
            srs_error_t rtcp_err = try_consume_rtcp_frame();

            if (rtcp_err == srs_success) {
                // Successfully consumed an RTCP frame, continue to check for more
                continue;
            } else if (srs_error_code(rtcp_err) == ERROR_RTSP_NEED_MORE_DATA) {
                // Need more data to complete RTCP frame, let the outer loop read more
                srs_freep(rtcp_err);
                append_bytes = true;
                break;
            } else {
                // Not an RTCP frame or other error, break and try RTSP parsing
                srs_freep(rtcp_err);
                break;
            }
        }
        
        // parse one by one.
        char* start = buf->bytes();
        char* end = start + buf->length();
        char* p = start;
        
        // find util SP/CR/LF, max 2 EOF, to finger out the EOF of message.
        for (; p < end && p[0] != normal_ch && p[0] != SRS_RTSP_CR && p[0] != SRS_RTSP_LF; p++) {
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
            int nb_token = (int)(p - start);
            // trim last ':' character.
            if (nb_token && p[-1] == ':') {
                nb_token--;
            }
            if (nb_token) {
                token.append(start, nb_token);
            } else {
                err = srs_error_new(ERROR_RTSP_REQUEST_HEADER_EOF, "EOF");
            }
            
            // ignore SP/CR/LF
            for (int i = 0; i < 2 && p < end && (p[0] == normal_ch || p[0] == SRS_RTSP_CR || p[0] == SRS_RTSP_LF); p++, i++) {
            }
            
            // consume the token bytes.
            srs_assert(p - start);
            buf->erase((int)(p - start));
            if (pconsumed) {
                *pconsumed = (int)(p - start);
            }
            break;
        }
        
        // append more and parse again.
        append_bytes = true;
    }
    
    return err;
}

srs_error_t SrsRtspStack::try_consume_rtcp_frame()
{
    // Need at least 4 bytes for RTCP over TCP header: $ + channel + length
    if (buf->length() < 4) {
        // Not enough data, let caller read more
        return srs_error_new(ERROR_RTSP_NEED_MORE_DATA, "need more data for rtcp header");
    }

    char* data = buf->bytes();

    // Check for RTCP over TCP format: $ + channel + length(2 bytes)
    if (data[0] == '$') {
        uint8_t channel = (uint8_t)data[1];
        uint16_t payload_length = (uint16_t(data[2]) << 8) | uint16_t(data[3]);
        int total_frame_size = 4 + payload_length; // 4-byte header + payload

        // Check if we have the complete frame
        if (buf->length() < total_frame_size) {
            // Not enough data for complete frame, let caller read more
            return srs_error_new(ERROR_RTSP_NEED_MORE_DATA, "need more data for complete rtcp frame");
        }

        // Check if the payload is RTCP (starts at offset 4)
        if (payload_length >= 8 && srs_is_rtcp((const uint8_t*)(data + 4), payload_length)) {
            // This is an RTCP packet in RTSP over TCP format
            srs_trace("RTSP: Consuming RTCP packet(%d), channel=%d, size=%d bytes",
                     (uint8_t)data[5], channel, payload_length);
            buf->erase(total_frame_size);
            return srs_success;
        } else {
            // Unknown interleaved frame, consume it anyway to avoid blocking RTSP parsing
            srs_trace("RTSP: Consuming unknown interleaved frame, channel=%d, size=%d bytes",
                     channel, payload_length);
            buf->erase(total_frame_size);
            return srs_success;
        }
    }

    // Not an interleaved frame (RTP/RTCP over TCP)
    return srs_error_new(ERROR_RTSP_TOKEN_NOT_NORMAL, "not interleaved frame");
}
