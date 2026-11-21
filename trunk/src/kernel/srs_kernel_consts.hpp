//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_CONSTS_HPP
#define SRS_KERNEL_CONSTS_HPP

#include <srs_core.hpp>

#define SRS_CONSTS_RTMP_DEFAULT_PORT 1935
#define SRS_DEFAULT_HTTP_PORT 80
#define SRS_DEFAULT_HTTPS_PORT 443
#define SRS_DEFAULT_REDIS_PORT 6379

#define SRS_CONSTS_RTMP_DEFAULT_VHOST "__defaultVhost__"
#define SRS_CONSTS_RTMP_DEFAULT_APP "__defaultApp__"

#define SRS_CONSTS_RTMP_SRS_CHUNK_SIZE 60000
// RTMP spec 6. Chunking: the default chunk size.
#define SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE 128

// RTMP spec 6. Chunking: a Set Chunk Size message (section 7.1) sets the chunk size, from 128
// to 65536 bytes, for each direction on its own. Larger chunks use less CPU but can delay
// other content on low bandwidth connections; small chunks are bad for high bitrate streams.
#define SRS_CONSTS_RTMP_MIN_CHUNK_SIZE 128
#define SRS_CONSTS_RTMP_MAX_CHUNK_SIZE 65536

// TODO: FIXME: Maybe change to smaller value, such as 3s?
#define SRS_CONSTS_RTMP_TIMEOUT (30 * SRS_UTIME_SECONDS)
#define SRS_CONSTS_RTMP_PULSE (500 * SRS_UTIME_MILLISECONDS)

// A 1-byte basic header, an 11-byte message header and a 4-byte extended timestamp.
#define SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE 16
// A 1-byte basic header and a 4-byte extended timestamp.
#define SRS_CONSTS_RTMP_MAX_FMT3_HEADER_SIZE 5

// At a 64KB chunk size, each message fits in one chunk, which takes 2 iovecs.
#define SRS_CONSTS_IOVS_MAX (SRS_PERF_MW_MSGS * 2)
// At a 64KB chunk size, each message fits in one chunk with one header of up to 16 bytes.
// The cache is twice that, since it cannot be reallocated and a dry cache costs another send loop.
#define SRS_CONSTS_C0C3_HEADERS_MAX (SRS_PERF_MW_MSGS * 32)

#define SRS_CONSTS_NULL_FILE "/dev/null"
#define SRS_CONSTS_LOCALHOST "127.0.0.1"
#define SRS_CONSTS_LOCALHOST_NAME "localhost"
#define SRS_CONSTS_LOOPBACK "0.0.0.0"
#define SRS_CONSTS_LOOPBACK6 "::"

#define SRS_SIGNAL_RELOAD SIGHUP
#define SRS_SIGNAL_REOPEN_LOG SIGUSR1
// Start a new SRS and gracefully quit the old one.
// @see https://github.com/ossrs/srs/issues/1579
// TODO: Not implemented.
#define SRS_SIGNAL_UPGRADE SIGUSR2
#define SRS_SIGNAL_FAST_QUIT SIGTERM
// @see https://github.com/ossrs/srs/issues/1579
#define SRS_SIGNAL_GRACEFULLY_QUIT SIGQUIT
#define SRS_SIGNAL_ASSERT_ABORT SIGABRT

// Writes the config in memory to the config file. Not an OS signal, so no handler is installed for it.
// @see https://github.com/ossrs/srs/issues/319#issuecomment-134993922
#define SRS_SIGNAL_PERSISTENCE_CONFIG 1000

#define SRS_CONSTS_LOG_EDGE_PLAY "EIG"
#define SRS_CONSTS_LOG_EDGE_PUBLISH "EPB"
#define SRS_CONSTS_LOG_FOWARDER "FWR"
#define SRS_CONSTS_LOG_PLAY "PLA"
#define SRS_CONSTS_LOG_CLIENT_PUBLISH "CPB"
#define SRS_CONSTS_LOG_WEB_PUBLISH "WPB"
#define SRS_CONSTS_LOG_INGESTER "IGS"
#define SRS_CONSTS_LOG_HLS "HLS"
#define SRS_CONSTS_LOG_ENCODER "ENC"
#define SRS_CONSTS_LOG_HTTP_STREAM "HTS"
#define SRS_CONSTS_LOG_HTTP_STREAM_CACHE "HTC"
#define SRS_CONSTS_LOG_STREAM_CASTER "SCS"
// For the nginx exec.
#define SRS_CONSTS_LOG_EXEC "EXE"
#define SRS_CONSTS_LOG_RTC "RTC"
#define SRS_CONSTS_LOG_SRT_PLAY "SRT_PLA"
#define SRS_CONSTS_LOG_SRT_PUBLISH "SRT_CPB"

#define SRS_CONSTS_RTMP_SET_DATAFRAME "@setDataFrame"
#define SRS_CONSTS_RTMP_ON_METADATA "onMetaData"

// @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 4
// Lines are terminated by either a single LF character or a CR
// character followed by an LF character.
#define SRS_CONSTS_CR '\r'
#define SRS_CONSTS_LF '\n'
#define SRS_CONSTS_SP ' '
#define SRS_CONSTS_SE ';'
#define SRS_CONSTS_LB '{'
#define SRS_CONSTS_RB '}'

#define SRS_CONSTS_HTTP_DEFAULT_PORT 80
#define SRS_CONSTS_HTTP_PATH_SEP '/'
#define SRS_CONSTS_HTTP_QUERY_SEP '?'

#define SRS_HTTP_RECV_TIMEOUT (15 * SRS_UTIME_SECONDS)

// 6.1.1 Status Code and Reason Phrase
#define SRS_CONSTS_HTTP_Continue 100
#define SRS_CONSTS_HTTP_SwitchingProtocols 101
#define SRS_CONSTS_HTTP_OK 200
#define SRS_CONSTS_HTTP_Created 201
#define SRS_CONSTS_HTTP_Accepted 202
#define SRS_CONSTS_HTTP_NonAuthoritativeInformation 203
#define SRS_CONSTS_HTTP_NoContent 204
#define SRS_CONSTS_HTTP_ResetContent 205
#define SRS_CONSTS_HTTP_PartialContent 206
#define SRS_CONSTS_HTTP_MultipleChoices 300
#define SRS_CONSTS_HTTP_MovedPermanently 301
#define SRS_CONSTS_HTTP_Found 302
#define SRS_CONSTS_HTTP_SeeOther 303
#define SRS_CONSTS_HTTP_NotModified 304
#define SRS_CONSTS_HTTP_UseProxy 305
#define SRS_CONSTS_HTTP_TemporaryRedirect 307
#define SRS_CONSTS_HTTP_BadRequest 400
#define SRS_CONSTS_HTTP_Unauthorized 401
#define SRS_CONSTS_HTTP_PaymentRequired 402
#define SRS_CONSTS_HTTP_Forbidden 403
#define SRS_CONSTS_HTTP_NotFound 404
#define SRS_CONSTS_HTTP_MethodNotAllowed 405
#define SRS_CONSTS_HTTP_NotAcceptable 406
#define SRS_CONSTS_HTTP_ProxyAuthenticationRequired 407
#define SRS_CONSTS_HTTP_RequestTimeout 408
#define SRS_CONSTS_HTTP_Conflict 409
#define SRS_CONSTS_HTTP_Gone 410
#define SRS_CONSTS_HTTP_LengthRequired 411
#define SRS_CONSTS_HTTP_PreconditionFailed 412
#define SRS_CONSTS_HTTP_RequestEntityTooLarge 413
#define SRS_CONSTS_HTTP_RequestURITooLarge 414
#define SRS_CONSTS_HTTP_UnsupportedMediaType 415
#define SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable 416
#define SRS_CONSTS_HTTP_ExpectationFailed 417
#define SRS_CONSTS_HTTP_InternalServerError 500
#define SRS_CONSTS_HTTP_NotImplemented 501
#define SRS_CONSTS_HTTP_BadGateway 502
#define SRS_CONSTS_HTTP_ServiceUnavailable 503
#define SRS_CONSTS_HTTP_GatewayTimeout 504
#define SRS_CONSTS_HTTP_HTTPVersionNotSupported 505

#define SRS_CONSTS_HTTP_Continue_str "Continue"
#define SRS_CONSTS_HTTP_SwitchingProtocols_str "Switching Protocols"
#define SRS_CONSTS_HTTP_OK_str "OK"
#define SRS_CONSTS_HTTP_Created_str "Created"
#define SRS_CONSTS_HTTP_Accepted_str "Accepted"
#define SRS_CONSTS_HTTP_NonAuthoritativeInformation_str "Non Authoritative Information"
#define SRS_CONSTS_HTTP_NoContent_str "No Content"
#define SRS_CONSTS_HTTP_ResetContent_str "Reset Content"
#define SRS_CONSTS_HTTP_PartialContent_str "Partial Content"
#define SRS_CONSTS_HTTP_MultipleChoices_str "Multiple Choices"
#define SRS_CONSTS_HTTP_MovedPermanently_str "Moved Permanently"
#define SRS_CONSTS_HTTP_Found_str "Found"
#define SRS_CONSTS_HTTP_SeeOther_str "See Other"
#define SRS_CONSTS_HTTP_NotModified_str "Not Modified"
#define SRS_CONSTS_HTTP_UseProxy_str "Use Proxy"
#define SRS_CONSTS_HTTP_TemporaryRedirect_str "Temporary Redirect"
#define SRS_CONSTS_HTTP_BadRequest_str "Bad Request"
#define SRS_CONSTS_HTTP_Unauthorized_str "Unauthorized"
#define SRS_CONSTS_HTTP_PaymentRequired_str "Payment Required"
#define SRS_CONSTS_HTTP_Forbidden_str "Forbidden"
#define SRS_CONSTS_HTTP_NotFound_str "Not Found"
#define SRS_CONSTS_HTTP_MethodNotAllowed_str "Method Not Allowed"
#define SRS_CONSTS_HTTP_NotAcceptable_str "Not Acceptable"
#define SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str "Proxy Authentication Required"
#define SRS_CONSTS_HTTP_RequestTimeout_str "Request Timeout"
#define SRS_CONSTS_HTTP_Conflict_str "Conflict"
#define SRS_CONSTS_HTTP_Gone_str "Gone"
#define SRS_CONSTS_HTTP_LengthRequired_str "Length Required"
#define SRS_CONSTS_HTTP_PreconditionFailed_str "Precondition Failed"
#define SRS_CONSTS_HTTP_RequestEntityTooLarge_str "Request Entity Too Large"
#define SRS_CONSTS_HTTP_RequestURITooLarge_str "Request URI Too Large"
#define SRS_CONSTS_HTTP_UnsupportedMediaType_str "Unsupported Media Type"
#define SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str "Requested Range Not Satisfiable"
#define SRS_CONSTS_HTTP_ExpectationFailed_str "Expectation Failed"
#define SRS_CONSTS_HTTP_InternalServerError_str "Internal Server Error"
#define SRS_CONSTS_HTTP_NotImplemented_str "Not Implemented"
#define SRS_CONSTS_HTTP_BadGateway_str "Bad Gateway"
#define SRS_CONSTS_HTTP_ServiceUnavailable_str "Service Unavailable"
#define SRS_CONSTS_HTTP_GatewayTimeout_str "Gateway Timeout"
#define SRS_CONSTS_HTTP_HTTPVersionNotSupported_str "HTTP Version Not Supported"

// 7.1.1 Status Code and Reason Phrase
#define SRS_CONSTS_RTSP_Continue 100
#define SRS_CONSTS_RTSP_OK 200
#define SRS_CONSTS_RTSP_Created 201
#define SRS_CONSTS_RTSP_LowOnStorageSpace 250
#define SRS_CONSTS_RTSP_MultipleChoices 300
#define SRS_CONSTS_RTSP_MovedPermanently 301
#define SRS_CONSTS_RTSP_MovedTemporarily 302
#define SRS_CONSTS_RTSP_SeeOther 303
#define SRS_CONSTS_RTSP_NotModified 304
#define SRS_CONSTS_RTSP_UseProxy 305
#define SRS_CONSTS_RTSP_BadRequest 400
#define SRS_CONSTS_RTSP_Unauthorized 401
#define SRS_CONSTS_RTSP_PaymentRequired 402
#define SRS_CONSTS_RTSP_Forbidden 403
#define SRS_CONSTS_RTSP_NotFound 404
#define SRS_CONSTS_RTSP_MethodNotAllowed 405
#define SRS_CONSTS_RTSP_NotAcceptable 406
#define SRS_CONSTS_RTSP_ProxyAuthenticationRequired 407
#define SRS_CONSTS_RTSP_RequestTimeout 408
#define SRS_CONSTS_RTSP_Gone 410
#define SRS_CONSTS_RTSP_LengthRequired 411
#define SRS_CONSTS_RTSP_PreconditionFailed 412
#define SRS_CONSTS_RTSP_RequestEntityTooLarge 413
#define SRS_CONSTS_RTSP_RequestURITooLarge 414
#define SRS_CONSTS_RTSP_UnsupportedMediaType 415
#define SRS_CONSTS_RTSP_ParameterNotUnderstood 451
#define SRS_CONSTS_RTSP_ConferenceNotFound 452
#define SRS_CONSTS_RTSP_NotEnoughBandwidth 453
#define SRS_CONSTS_RTSP_SessionNotFound 454
#define SRS_CONSTS_RTSP_MethodNotValidInThisState 455
#define SRS_CONSTS_RTSP_HeaderFieldNotValidForResource 456
#define SRS_CONSTS_RTSP_InvalidRange 457
#define SRS_CONSTS_RTSP_ParameterIsReadOnly 458
#define SRS_CONSTS_RTSP_AggregateOperationNotAllowed 459
#define SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed 460
#define SRS_CONSTS_RTSP_UnsupportedTransport 461
#define SRS_CONSTS_RTSP_DestinationUnreachable 462
#define SRS_CONSTS_RTSP_InternalServerError 500
#define SRS_CONSTS_RTSP_NotImplemented 501
#define SRS_CONSTS_RTSP_BadGateway 502
#define SRS_CONSTS_RTSP_ServiceUnavailable 503
#define SRS_CONSTS_RTSP_GatewayTimeout 504
#define SRS_CONSTS_RTSP_RTSPVersionNotSupported 505
#define SRS_CONSTS_RTSP_OptionNotSupported 551

#define SRS_CONSTS_RTSP_Continue_str "Continue"
#define SRS_CONSTS_RTSP_OK_str "OK"
#define SRS_CONSTS_RTSP_Created_str "Created"
#define SRS_CONSTS_RTSP_LowOnStorageSpace_str "Low on Storage Space"
#define SRS_CONSTS_RTSP_MultipleChoices_str "Multiple Choices"
#define SRS_CONSTS_RTSP_MovedPermanently_str "Moved Permanently"
#define SRS_CONSTS_RTSP_MovedTemporarily_str "Moved Temporarily"
#define SRS_CONSTS_RTSP_SeeOther_str "See Other"
#define SRS_CONSTS_RTSP_NotModified_str "Not Modified"
#define SRS_CONSTS_RTSP_UseProxy_str "Use Proxy"
#define SRS_CONSTS_RTSP_BadRequest_str "Bad Request"
#define SRS_CONSTS_RTSP_Unauthorized_str "Unauthorized"
#define SRS_CONSTS_RTSP_PaymentRequired_str "Payment Required"
#define SRS_CONSTS_RTSP_Forbidden_str "Forbidden"
#define SRS_CONSTS_RTSP_NotFound_str "Not Found"
#define SRS_CONSTS_RTSP_MethodNotAllowed_str "Method Not Allowed"
#define SRS_CONSTS_RTSP_NotAcceptable_str "Not Acceptable"
#define SRS_CONSTS_RTSP_ProxyAuthenticationRequired_str "Proxy Authentication Required"
#define SRS_CONSTS_RTSP_RequestTimeout_str "Request Timeout"
#define SRS_CONSTS_RTSP_Gone_str "Gone"
#define SRS_CONSTS_RTSP_LengthRequired_str "Length Required"
#define SRS_CONSTS_RTSP_PreconditionFailed_str "Precondition Failed"
#define SRS_CONSTS_RTSP_RequestEntityTooLarge_str "Request Entity Too Large"
#define SRS_CONSTS_RTSP_RequestURITooLarge_str "Request URI Too Large"
#define SRS_CONSTS_RTSP_UnsupportedMediaType_str "Unsupported Media Type"
#define SRS_CONSTS_RTSP_ParameterNotUnderstood_str "Invalid parameter"
#define SRS_CONSTS_RTSP_ConferenceNotFound_str "Illegal Conference Identifier"
#define SRS_CONSTS_RTSP_NotEnoughBandwidth_str "Not Enough Bandwidth"
#define SRS_CONSTS_RTSP_SessionNotFound_str "Session Not Found"
#define SRS_CONSTS_RTSP_MethodNotValidInThisState_str "Method Not Valid In This State"
#define SRS_CONSTS_RTSP_HeaderFieldNotValidForResource_str "Header Field Not Valid"
#define SRS_CONSTS_RTSP_InvalidRange_str "Invalid Range"
#define SRS_CONSTS_RTSP_ParameterIsReadOnly_str "Parameter Is Read-Only"
#define SRS_CONSTS_RTSP_AggregateOperationNotAllowed_str "Aggregate Operation Not Allowed"
#define SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed_str "Only Aggregate Operation Allowed"
#define SRS_CONSTS_RTSP_UnsupportedTransport_str "Unsupported Transport"
#define SRS_CONSTS_RTSP_DestinationUnreachable_str "Destination Unreachable"
#define SRS_CONSTS_RTSP_InternalServerError_str "Internal Server Error"
#define SRS_CONSTS_RTSP_NotImplemented_str "Not Implemented"
#define SRS_CONSTS_RTSP_BadGateway_str "Bad Gateway"
#define SRS_CONSTS_RTSP_ServiceUnavailable_str "Service Unavailable"
#define SRS_CONSTS_RTSP_GatewayTimeout_str "Gateway Timeout"
#define SRS_CONSTS_RTSP_RTSPVersionNotSupported_str "RTSP Version Not Supported"
#define SRS_CONSTS_RTSP_OptionNotSupported_str "Option not support"

#endif
