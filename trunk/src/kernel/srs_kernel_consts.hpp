/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_KERNEL_CONSTS_HPP
#define SRS_KERNEL_CONSTS_HPP

#include <srs_core.hpp>

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// RTMP consts values
///////////////////////////////////////////////////////////
// default vhost of rtmp
#define SRS_CONSTS_RTMP_DEFAULT_VHOST "__defaultVhost__"
#define SRS_CONSTS_RTMP_DEFAULT_APP "__defaultApp__"
// default port of rtmp
#define SRS_CONSTS_RTMP_DEFAULT_PORT 1935

// the default chunk size for system.
#define SRS_CONSTS_RTMP_SRS_CHUNK_SIZE 60000
// 6. Chunking, RTMP protocol default chunk size.
#define SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE 128

/**
 * 6. Chunking
 * The chunk size is configurable. It can be set using a control
 * message(Set Chunk Size) as described in section 7.1. The maximum
 * chunk size can be 65536 bytes and minimum 128 bytes. Larger values
 * reduce CPU usage, but also commit to larger writes that can delay
 * other content on lower bandwidth connections. Smaller chunks are not
 * good for high-bit rate streaming. Chunk size is maintained
 * independently for each direction.
 */
#define SRS_CONSTS_RTMP_MIN_CHUNK_SIZE 128
#define SRS_CONSTS_RTMP_MAX_CHUNK_SIZE 65536


// the following is the timeout for rtmp protocol,
// to avoid death connection.

// Never timeout in ms
// @remake Rename from SRS_CONSTS_NO_TIMEOUT
// @see ST_UTIME_NO_TIMEOUT
#define SRS_CONSTS_NO_TMMS ((int64_t) -1LL)

// the common io timeout, for both recv and send.
// TODO: FIXME: use ms for timeout.
#define SRS_CONSTS_RTMP_TMMS (30*1000)

// the timeout to wait for client control message,
// if timeout, we generally ignore and send the data to client,
// generally, it's the pulse time for data seding.
// @remark, recomment to 500ms.
#define SRS_CONSTS_RTMP_PULSE_TMMS (500)

/**
 * max rtmp header size:
 *     1bytes basic header,
 *     11bytes message header,
 *     4bytes timestamp header,
 * that is, 1+11+4=16bytes.
 */
#define SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE 16
/**
 * max rtmp header size:
 *     1bytes basic header,
 *     4bytes timestamp header,
 * that is, 1+4=5bytes.
 */
// always use fmt0 as cache.
#define SRS_CONSTS_RTMP_MAX_FMT3_HEADER_SIZE 5

/**
 * for performance issue,
 * the iovs cache, @see https://github.com/ossrs/srs/issues/194
 * iovs cache for multiple messages for each connections.
 * suppose the chunk size is 64k, each message send in a chunk which needs only 2 iovec,
 * so the iovs max should be (SRS_PERF_MW_MSGS * 2)
 *
 * @remark, SRS will realloc when the iovs not enough.
 */
#define SRS_CONSTS_IOVS_MAX (SRS_PERF_MW_MSGS * 2)
/**
 * for performance issue,
 * the c0c3 cache, @see https://github.com/ossrs/srs/issues/194
 * c0c3 cache for multiple messages for each connections.
 * each c0 <= 16byes, suppose the chunk size is 64k,
 * each message send in a chunk which needs only a c0 header,
 * so the c0c3 cache should be (SRS_PERF_MW_MSGS * 16)
 *
 * @remark, SRS will try another loop when c0c3 cache dry, for we cannot realloc it.
 *       so we use larger c0c3 cache, that is (SRS_PERF_MW_MSGS * 32)
 */
#define SRS_CONSTS_C0C3_HEADERS_MAX (SRS_PERF_MW_MSGS * 32)

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// SRS consts values
///////////////////////////////////////////////////////////
#define SRS_CONSTS_NULL_FILE "/dev/null"
#define SRS_CONSTS_LOCALHOST "127.0.0.1"

// signal defines.
// reload the config file and apply new config.
#define SRS_SIGNAL_RELOAD SIGHUP
// reopen the log file.
#define SRS_SIGNAL_REOPEN_LOG SIGUSR1
// srs should gracefully quit, do dispose then exit.
#define SRS_SIGNAL_GRACEFULLY_QUIT SIGTERM

// application level signals.
// persistence the config in memory to config file.
// @see https://github.com/ossrs/srs/issues/319#issuecomment-134993922
// @remark we actually don't handle the signal for it's not a valid os signal.
#define SRS_SIGNAL_PERSISTENCE_CONFIG 1000

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// log consts values
///////////////////////////////////////////////////////////
// downloading speed-up, play to edge, ingest from origin
#define SRS_CONSTS_LOG_EDGE_PLAY "EIG"
// uploading speed-up, publish to edge, foward to origin
#define SRS_CONSTS_LOG_EDGE_PUBLISH "EFW"
// edge/origin forwarder.
#define SRS_CONSTS_LOG_FOWARDER "FWR"
// play stream on edge/origin.
#define SRS_CONSTS_LOG_PLAY "PLA"
// client publish to edge/origin
#define SRS_CONSTS_LOG_CLIENT_PUBLISH "CPB"
// web/flash publish to edge/origin
#define SRS_CONSTS_LOG_WEB_PUBLISH "WPB"
// ingester for edge(play)/origin
#define SRS_CONSTS_LOG_INGESTER "IGS"
// hls log id.
#define SRS_CONSTS_LOG_HLS "HLS"
// encoder log id.
#define SRS_CONSTS_LOG_ENCODER "ENC"
// http stream log id.
#define SRS_CONSTS_LOG_HTTP_STREAM "HTS"
// http stream cache log id.
#define SRS_CONSTS_LOG_HTTP_STREAM_CACHE "HTC"
// stream caster log id.
#define SRS_CONSTS_LOG_STREAM_CASTER "SCS"
// the nginx exec log id.
#define SRS_CONSTS_LOG_EXEC "EXE"

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////
// RTMP consts values
///////////////////////////////////////////////////////////
#define SRS_CONSTS_RTMP_SET_DATAFRAME            "@setDataFrame"
#define SRS_CONSTS_RTMP_ON_METADATA              "onMetaData"

///////////////////////////////////////////////////////////
// HTTP/HLS consts values
///////////////////////////////////////////////////////////
// @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 4
// Lines are terminated by either a single LF character or a CR
// character followed by an LF character.
// CR             = <US-ASCII CR, carriage return (13)>
#define SRS_CONSTS_CR '\r' // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define SRS_CONSTS_LF '\n' // 0x0A
// SP             = <US-ASCII SP, space>
#define SRS_CONSTS_SP ' ' // 0x20
// SE             = <US-ASCII SE, semicolon>
#define SRS_CONSTS_SE ';' // 0x3b
// LB             = <US-ASCII SE, left-brace>
#define SRS_CONSTS_LB '{' // 0x7b
// RB             = <US-ASCII SE, right-brace>
#define SRS_CONSTS_RB '}' // 0x7d

///////////////////////////////////////////////////////////
// HTTP consts values
///////////////////////////////////////////////////////////
// the default http port.
#define SRS_CONSTS_HTTP_DEFAULT_PORT 80
// linux path seprator
#define SRS_CONSTS_HTTP_PATH_SEP '/'
// query string seprator
#define SRS_CONSTS_HTTP_QUERY_SEP '?'

// the default recv timeout.
#define SRS_HTTP_RECV_TMMS (60 * 1000)

// 6.1.1 Status Code and Reason Phrase
#define SRS_CONSTS_HTTP_Continue                       100
#define SRS_CONSTS_HTTP_SwitchingProtocols             101
#define SRS_CONSTS_HTTP_OK                             200
#define SRS_CONSTS_HTTP_Created                        201
#define SRS_CONSTS_HTTP_Accepted                       202
#define SRS_CONSTS_HTTP_NonAuthoritativeInformation    203
#define SRS_CONSTS_HTTP_NoContent                      204
#define SRS_CONSTS_HTTP_ResetContent                   205
#define SRS_CONSTS_HTTP_PartialContent                 206
#define SRS_CONSTS_HTTP_MultipleChoices                300
#define SRS_CONSTS_HTTP_MovedPermanently               301
#define SRS_CONSTS_HTTP_Found                          302
#define SRS_CONSTS_HTTP_SeeOther                       303
#define SRS_CONSTS_HTTP_NotModified                    304
#define SRS_CONSTS_HTTP_UseProxy                       305
#define SRS_CONSTS_HTTP_TemporaryRedirect              307
#define SRS_CONSTS_HTTP_BadRequest                     400
#define SRS_CONSTS_HTTP_Unauthorized                   401
#define SRS_CONSTS_HTTP_PaymentRequired                402
#define SRS_CONSTS_HTTP_Forbidden                      403
#define SRS_CONSTS_HTTP_NotFound                       404
#define SRS_CONSTS_HTTP_MethodNotAllowed               405
#define SRS_CONSTS_HTTP_NotAcceptable                  406
#define SRS_CONSTS_HTTP_ProxyAuthenticationRequired    407
#define SRS_CONSTS_HTTP_RequestTimeout                 408
#define SRS_CONSTS_HTTP_Conflict                       409
#define SRS_CONSTS_HTTP_Gone                           410
#define SRS_CONSTS_HTTP_LengthRequired                 411
#define SRS_CONSTS_HTTP_PreconditionFailed             412
#define SRS_CONSTS_HTTP_RequestEntityTooLarge          413
#define SRS_CONSTS_HTTP_RequestURITooLarge             414
#define SRS_CONSTS_HTTP_UnsupportedMediaType           415
#define SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable   416
#define SRS_CONSTS_HTTP_ExpectationFailed              417
#define SRS_CONSTS_HTTP_InternalServerError            500
#define SRS_CONSTS_HTTP_NotImplemented                 501
#define SRS_CONSTS_HTTP_BadGateway                     502
#define SRS_CONSTS_HTTP_ServiceUnavailable             503
#define SRS_CONSTS_HTTP_GatewayTimeout                 504
#define SRS_CONSTS_HTTP_HTTPVersionNotSupported        505

#define SRS_CONSTS_HTTP_Continue_str                           "Continue"
#define SRS_CONSTS_HTTP_SwitchingProtocols_str                 "Switching Protocols"
#define SRS_CONSTS_HTTP_OK_str                                 "OK"
#define SRS_CONSTS_HTTP_Created_str                            "Created"
#define SRS_CONSTS_HTTP_Accepted_str                           "Accepted"
#define SRS_CONSTS_HTTP_NonAuthoritativeInformation_str        "Non Authoritative Information"
#define SRS_CONSTS_HTTP_NoContent_str                          "No Content"
#define SRS_CONSTS_HTTP_ResetContent_str                       "Reset Content"
#define SRS_CONSTS_HTTP_PartialContent_str                     "Partial Content"
#define SRS_CONSTS_HTTP_MultipleChoices_str                    "Multiple Choices"
#define SRS_CONSTS_HTTP_MovedPermanently_str                   "Moved Permanently"
#define SRS_CONSTS_HTTP_Found_str                              "Found"
#define SRS_CONSTS_HTTP_SeeOther_str                           "See Other"
#define SRS_CONSTS_HTTP_NotModified_str                        "Not Modified"
#define SRS_CONSTS_HTTP_UseProxy_str                           "Use Proxy"
#define SRS_CONSTS_HTTP_TemporaryRedirect_str                  "Temporary Redirect"
#define SRS_CONSTS_HTTP_BadRequest_str                         "Bad Request"
#define SRS_CONSTS_HTTP_Unauthorized_str                       "Unauthorized"
#define SRS_CONSTS_HTTP_PaymentRequired_str                    "Payment Required"
#define SRS_CONSTS_HTTP_Forbidden_str                          "Forbidden"
#define SRS_CONSTS_HTTP_NotFound_str                           "Not Found"
#define SRS_CONSTS_HTTP_MethodNotAllowed_str                   "Method Not Allowed"
#define SRS_CONSTS_HTTP_NotAcceptable_str                      "Not Acceptable"
#define SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str        "Proxy Authentication Required"
#define SRS_CONSTS_HTTP_RequestTimeout_str                     "Request Timeout"
#define SRS_CONSTS_HTTP_Conflict_str                           "Conflict"
#define SRS_CONSTS_HTTP_Gone_str                               "Gone"
#define SRS_CONSTS_HTTP_LengthRequired_str                     "Length Required"
#define SRS_CONSTS_HTTP_PreconditionFailed_str                 "Precondition Failed"
#define SRS_CONSTS_HTTP_RequestEntityTooLarge_str              "Request Entity Too Large"
#define SRS_CONSTS_HTTP_RequestURITooLarge_str                 "Request URI Too Large"
#define SRS_CONSTS_HTTP_UnsupportedMediaType_str               "Unsupported Media Type"
#define SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str       "Requested Range Not Satisfiable"
#define SRS_CONSTS_HTTP_ExpectationFailed_str                  "Expectation Failed"
#define SRS_CONSTS_HTTP_InternalServerError_str                "Internal Server Error"
#define SRS_CONSTS_HTTP_NotImplemented_str                     "Not Implemented"
#define SRS_CONSTS_HTTP_BadGateway_str                         "Bad Gateway"
#define SRS_CONSTS_HTTP_ServiceUnavailable_str                 "Service Unavailable"
#define SRS_CONSTS_HTTP_GatewayTimeout_str                     "Gateway Timeout"
#define SRS_CONSTS_HTTP_HTTPVersionNotSupported_str            "HTTP Version Not Supported"

///////////////////////////////////////////////////////////
// RTSP consts values
///////////////////////////////////////////////////////////
// 7.1.1 Status Code and Reason Phrase
#define SRS_CONSTS_RTSP_Continue                       100
#define SRS_CONSTS_RTSP_OK                             200
#define SRS_CONSTS_RTSP_Created                        201
#define SRS_CONSTS_RTSP_LowOnStorageSpace              250
#define SRS_CONSTS_RTSP_MultipleChoices                300
#define SRS_CONSTS_RTSP_MovedPermanently               301
#define SRS_CONSTS_RTSP_MovedTemporarily               302
#define SRS_CONSTS_RTSP_SeeOther                       303
#define SRS_CONSTS_RTSP_NotModified                    304
#define SRS_CONSTS_RTSP_UseProxy                       305
#define SRS_CONSTS_RTSP_BadRequest                     400
#define SRS_CONSTS_RTSP_Unauthorized                   401
#define SRS_CONSTS_RTSP_PaymentRequired                402
#define SRS_CONSTS_RTSP_Forbidden                      403
#define SRS_CONSTS_RTSP_NotFound                       404
#define SRS_CONSTS_RTSP_MethodNotAllowed               405
#define SRS_CONSTS_RTSP_NotAcceptable                  406
#define SRS_CONSTS_RTSP_ProxyAuthenticationRequired    407
#define SRS_CONSTS_RTSP_RequestTimeout                 408
#define SRS_CONSTS_RTSP_Gone                           410
#define SRS_CONSTS_RTSP_LengthRequired                 411
#define SRS_CONSTS_RTSP_PreconditionFailed             412
#define SRS_CONSTS_RTSP_RequestEntityTooLarge          413
#define SRS_CONSTS_RTSP_RequestURITooLarge             414
#define SRS_CONSTS_RTSP_UnsupportedMediaType           415
#define SRS_CONSTS_RTSP_ParameterNotUnderstood         451
#define SRS_CONSTS_RTSP_ConferenceNotFound             452
#define SRS_CONSTS_RTSP_NotEnoughBandwidth             453
#define SRS_CONSTS_RTSP_SessionNotFound                454
#define SRS_CONSTS_RTSP_MethodNotValidInThisState      455
#define SRS_CONSTS_RTSP_HeaderFieldNotValidForResource 456
#define SRS_CONSTS_RTSP_InvalidRange                   457
#define SRS_CONSTS_RTSP_ParameterIsReadOnly            458
#define SRS_CONSTS_RTSP_AggregateOperationNotAllowed   459
#define SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed  460
#define SRS_CONSTS_RTSP_UnsupportedTransport           461
#define SRS_CONSTS_RTSP_DestinationUnreachable         462
#define SRS_CONSTS_RTSP_InternalServerError            500
#define SRS_CONSTS_RTSP_NotImplemented                 501
#define SRS_CONSTS_RTSP_BadGateway                     502
#define SRS_CONSTS_RTSP_ServiceUnavailable             503
#define SRS_CONSTS_RTSP_GatewayTimeout                 504
#define SRS_CONSTS_RTSP_RTSPVersionNotSupported        505
#define SRS_CONSTS_RTSP_OptionNotSupported             551

#define SRS_CONSTS_RTSP_Continue_str                            "Continue"
#define SRS_CONSTS_RTSP_OK_str                                  "OK"
#define SRS_CONSTS_RTSP_Created_str                             "Created"
#define SRS_CONSTS_RTSP_LowOnStorageSpace_str                   "Low on Storage Space"
#define SRS_CONSTS_RTSP_MultipleChoices_str                     "Multiple Choices"
#define SRS_CONSTS_RTSP_MovedPermanently_str                    "Moved Permanently"
#define SRS_CONSTS_RTSP_MovedTemporarily_str                    "Moved Temporarily"
#define SRS_CONSTS_RTSP_SeeOther_str                            "See Other"
#define SRS_CONSTS_RTSP_NotModified_str                         "Not Modified"
#define SRS_CONSTS_RTSP_UseProxy_str                            "Use Proxy"
#define SRS_CONSTS_RTSP_BadRequest_str                          "Bad Request"
#define SRS_CONSTS_RTSP_Unauthorized_str                        "Unauthorized"
#define SRS_CONSTS_RTSP_PaymentRequired_str                     "Payment Required"
#define SRS_CONSTS_RTSP_Forbidden_str                           "Forbidden"
#define SRS_CONSTS_RTSP_NotFound_str                            "Not Found"
#define SRS_CONSTS_RTSP_MethodNotAllowed_str                    "Method Not Allowed"
#define SRS_CONSTS_RTSP_NotAcceptable_str                       "Not Acceptable"
#define SRS_CONSTS_RTSP_ProxyAuthenticationRequired_str         "Proxy Authentication Required"
#define SRS_CONSTS_RTSP_RequestTimeout_str                      "Request Timeout"
#define SRS_CONSTS_RTSP_Gone_str                                "Gone"
#define SRS_CONSTS_RTSP_LengthRequired_str                      "Length Required"
#define SRS_CONSTS_RTSP_PreconditionFailed_str                  "Precondition Failed"
#define SRS_CONSTS_RTSP_RequestEntityTooLarge_str               "Request Entity Too Large"
#define SRS_CONSTS_RTSP_RequestURITooLarge_str                  "Request URI Too Large"
#define SRS_CONSTS_RTSP_UnsupportedMediaType_str                "Unsupported Media Type"
#define SRS_CONSTS_RTSP_ParameterNotUnderstood_str              "Invalid parameter"
#define SRS_CONSTS_RTSP_ConferenceNotFound_str                  "Illegal Conference Identifier"
#define SRS_CONSTS_RTSP_NotEnoughBandwidth_str                  "Not Enough Bandwidth"
#define SRS_CONSTS_RTSP_SessionNotFound_str                     "Session Not Found"
#define SRS_CONSTS_RTSP_MethodNotValidInThisState_str           "Method Not Valid In This State"
#define SRS_CONSTS_RTSP_HeaderFieldNotValidForResource_str      "Header Field Not Valid"
#define SRS_CONSTS_RTSP_InvalidRange_str                        "Invalid Range"
#define SRS_CONSTS_RTSP_ParameterIsReadOnly_str                 "Parameter Is Read-Only"
#define SRS_CONSTS_RTSP_AggregateOperationNotAllowed_str        "Aggregate Operation Not Allowed"
#define SRS_CONSTS_RTSP_OnlyAggregateOperationAllowed_str       "Only Aggregate Operation Allowed"
#define SRS_CONSTS_RTSP_UnsupportedTransport_str                "Unsupported Transport"
#define SRS_CONSTS_RTSP_DestinationUnreachable_str              "Destination Unreachable"
#define SRS_CONSTS_RTSP_InternalServerError_str                 "Internal Server Error"
#define SRS_CONSTS_RTSP_NotImplemented_str                      "Not Implemented"
#define SRS_CONSTS_RTSP_BadGateway_str                          "Bad Gateway"
#define SRS_CONSTS_RTSP_ServiceUnavailable_str                  "Service Unavailable"
#define SRS_CONSTS_RTSP_GatewayTimeout_str                      "Gateway Timeout"
#define SRS_CONSTS_RTSP_RTSPVersionNotSupported_str             "RTSP Version Not Supported"
#define SRS_CONSTS_RTSP_OptionNotSupported_str                  "Option not support"

///////////////////////////////////////////////////////////
// KAFKA consts values
///////////////////////////////////////////////////////////
#define SRS_CONSTS_KAFKA_DEFAULT_PORT 9092

// the common io timeout, for both recv and send.
#define SRS_CONSTS_KAFKA_TMMS (30*1000)

#endif

