//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_RTSP_HPP
#define SRS_PROTOCOL_RTSP_HPP

#include <srs_core.hpp>
#include <srs_kernel_consts.hpp>

#include <sstream>
#include <string>

class SrsBuffer;
class SrsSimpleStream;
class SrsParsedAudioPacket;
class ISrsProtocolReadWriter;

// From rtsp specification
// CR = <US-ASCII CR, carriage return (13)>
#define SRS_RTSP_CR SRS_CONSTS_CR // 0x0D
// LF = <US-ASCII LF, linefeed (10)>
#define SRS_RTSP_LF SRS_CONSTS_LF // 0x0A
// SP = <US-ASCII SP, space (32)>
#define SRS_RTSP_SP ' ' // 0x20

// 4 RTSP Message, @see rfc2326-1998-rtsp.pdf, page 37
// Lines are terminated by CRLF, but
// receivers should be prepared to also interpret CR and LF by
// themselves as line terminators.
#define SRS_RTSP_CRLF "\r\n"         // 0x0D0A
#define SRS_RTSP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// RTSP token
#define SRS_RTSP_TOKEN_CSEQ "CSeq"
#define SRS_RTSP_TOKEN_PUBLIC "Public"
#define SRS_RTSP_TOKEN_CONTENT_TYPE "Content-Type"
#define SRS_RTSP_TOKEN_CONTENT_LENGTH "Content-Length"
#define SRS_RTSP_TOKEN_TRANSPORT "Transport"
#define SRS_RTSP_TOKEN_SESSION "Session"
#define SRS_RTSP_TOKEN_ACCEPT "Accept"
#define SRS_RTSP_TOKEN_USER_AGENT "User-Agent"
#define SRS_RTSP_TOKEN_RANGE "Range"

// RTSP methods
#define SRS_RTSP_METHOD_OPTIONS "OPTIONS"
#define SRS_RTSP_METHOD_DESCRIBE "DESCRIBE"
#define SRS_RTSP_METHOD_ANNOUNCE "ANNOUNCE"
#define SRS_RTSP_METHOD_SETUP "SETUP"
#define SRS_RTSP_METHOD_PLAY "PLAY"
#define SRS_RTSP_METHOD_PAUSE "PAUSE"
#define SRS_RTSP_METHOD_TEARDOWN "TEARDOWN"
#define SRS_RTSP_METHOD_GET_PARAMETER "GET_PARAMETER"
#define SRS_RTSP_METHOD_SET_PARAMETER "SET_PARAMETER"
#define SRS_RTSP_METHOD_REDIRECT "REDIRECT"
#define SRS_RTSP_METHOD_RECORD "RECORD"

// RTSP-Version
#define SRS_RTSP_VERSION "RTSP/1.0"

// 10 Method Definitions, @see rfc2326-1998-rtsp.pdf, page 57
// The method token indicates the method to be performed on the resource
// identified by the Request-URI. The method is case-sensitive. New
// methods may be defined in the future. Method names may not start with
// a $ character (decimal 24) and must be a token. Methods are
// summarized in Table 2.
// Notes on Table 2: PAUSE is recommended, but not required in that a
// fully functional server can be built that does not support this
// method, for example, for live feeds. If a server does not support a
// particular method, it MUST return "501 Not Implemented" and a client
// SHOULD not try this method again for this server.
enum SrsRtspMethod {
    SrsRtspMethodDescribe = 0x0001,
    SrsRtspMethodAnnounce = 0x0002,
    SrsRtspMethodGetParameter = 0x0004,
    SrsRtspMethodOptions = 0x0008,
    SrsRtspMethodPause = 0x0010,
    SrsRtspMethodPlay = 0x0020,
    SrsRtspMethodRecord = 0x0040,
    SrsRtspMethodRedirect = 0x0080,
    SrsRtspMethodSetup = 0x0100,
    SrsRtspMethodSetParameter = 0x0200,
    SrsRtspMethodTeardown = 0x0400,
};

// The state of rtsp token.
enum SrsRtspTokenState {
    // Parse token failed, default state.
    SrsRtspTokenStateError = 100,
    // When SP follow the token.
    SrsRtspTokenStateNormal = 101,
    // When CRLF follow the token.
    SrsRtspTokenStateEOF = 102,
};

// The rtsp transport.
// 12.39 Transport, @see rfc2326-1998-rtsp.pdf, page 115
// This request header indicates which transport protocol is to be used
// and configures its parameters such as destination address,
// compression, multicast time-to-live and destination port for a single
// stream. It sets those values not already determined by a presentation
// description.
class SrsRtspTransport
{
public:
    // The syntax for the transport specifier is
    //      transport/profile/lower-transport
    std::string transport;
    std::string profile;
    std::string lower_transport;
    // unicast | multicast
    // mutually exclusive indication of whether unicast or multicast
    // delivery will be attempted. Default value is multicast.
    // Clients that are capable of handling both unicast and
    // multicast transmission MUST indicate such capability by
    // including two full transport-specs with separate parameters
    // For each.
    std::string cast_type;
    // The interleaved parameter implies mixing the media stream with
    // the control stream in whatever protocol is being used by the
    // control stream, using the mechanism defined in Section 10.12.
    // The argument provides the channel number to be used in the $
    // statement. This parameter may be specified as a range, e.g.,
    // interleaved=4-5 in cases where the transport choice for the
    // media stream requires it.
    std::string interleaved;
    int interleaved_min;
    int interleaved_max;
    // The mode parameter indicates the methods to be supported for
    // this session. Valid values are PLAY and RECORD. If not
    // provided, the default is PLAY.
    std::string mode;
    // This parameter provides the unicast RTP/RTCP port pair on
    // which the client has chosen to receive media data and control
    // information. It is specified as a range, e.g.,
    //      client_port=3456-3457.
    // where client will use port in:
    //      [client_port_min, client_port_max)
    int client_port_min;
    int client_port_max;

public:
    SrsRtspTransport();
    virtual ~SrsRtspTransport();

public:
    // Parse a line of token for transport.
    virtual srs_error_t parse(std::string attr);
    // Copy the transport from src.
    virtual void copy(SrsRtspTransport *src);
};

// The rtsp request message.
// 6 Request, @see rfc2326-1998-rtsp.pdf, page 39
// A request message from a client to a server or vice versa includes,
// within the first line of that message, the method to be applied to
// The resource, the identifier of the resource, and the protocol
// version in use.
// Request = Request-Line ; Section 6.1
//          // ( general-header ; Section 5
//           | request-header ; Section 6.2
//           | entity-header ) ; Section 8.1
//           CRLF
//           [ message-body ] ; Section 4.3
class SrsRtspRequest
{
public:
    // 6.1 Request Line
    // Request-Line = Method SP Request-URI SP RTSP-Version CRLF
    std::string method;
    std::string uri;
    std::string version;
    // 12.17 CSeq
    // The CSeq field specifies the sequence number for an RTSP requestresponse
    // pair. This field MUST be present in all requests and
    // responses. For every RTSP request containing the given sequence
    // number, there will be a corresponding response having the same
    // number. Any retransmitted request must contain the same sequence
    // number as the original (i.e. the sequence number is not incremented
    // For retransmissions of the same request).
    long seq;
    // 12.16 Content-Type, @see rfc2326-1998-rtsp.pdf, page 99
    // See [H14.18]. Note that the content types suitable for RTSP are
    // likely to be restricted in practice to presentation descriptions and
    // parameter-value types.
    std::string content_type;
    // 12.14 Content-Length, @see rfc2326-1998-rtsp.pdf, page 99
    // This field contains the length of the content of the method (i.e.
    // after the double CRLF following the last header). Unlike HTTP, it
    // MUST be included in all messages that carry content beyond the header
    // portion of the message. If it is missing, a default value of zero is
    // assumed. It is interpreted according to [H14.14].
    long content_length;
    // The session id.
    std::string session;

    // The transport in setup, NULL for no transport.
    SrsRtspTransport *transport;
    // For setup message, parse the stream id from uri.
    int stream_id;

    std::string accept;
    std::string user_agent;
    std::string range;

public:
    SrsRtspRequest();
    virtual ~SrsRtspRequest();

public:
    virtual bool is_options();
    virtual bool is_describe();
    virtual bool is_setup();
    virtual bool is_play();
    virtual bool is_teardown();
};

// The rtsp response message.
// 7 Response, @see rfc2326-1998-rtsp.pdf, page 43
// [H6] applies except that HTTP-Version is replaced by RTSP-Version.
// Also, RTSP defines additional status codes and does not define some
// HTTP codes. The valid response codes and the methods they can be used
// with are defined in Table 1.
// After receiving and interpreting a request message, the recipient
// responds with an RTSP response message.
//       Response = Status-Line ; Section 7.1
//                  // ( general-header ; Section 5
//                   | response-header ; Section 7.1.2
//                   | entity-header ) ; Section 8.1
//                   CRLF
//                   [ message-body ] ; Section 4.3
class SrsRtspResponse
{
public:
    // 7.1 Status-Line
    // The first line of a Response message is the Status-Line, consisting
    // of the protocol version followed by a numeric status code, and the
    // textual phrase associated with the status code, with each element
    // separated by SP characters. No CR or LF is allowed except in the
    // final CRLF sequence.
    //       Status-Line = RTSP-Version SP Status-Code SP Reason-Phrase CRLF
    // @see about the version of rtsp, see SRS_RTSP_VERSION
    // @see about the status of rtsp, see SRS_CONSTS_RTSP_OK
    int status;
    // 12.17 CSeq, @see rfc2326-1998-rtsp.pdf, page 99
    // The CSeq field specifies the sequence number for an RTSP requestresponse
    // pair. This field MUST be present in all requests and
    // responses. For every RTSP request containing the given sequence
    // number, there will be a corresponding response having the same
    // number. Any retransmitted request must contain the same sequence
    // number as the original (i.e. the sequence number is not incremented
    // For retransmissions of the same request).
    long seq;
    // The session id.
    std::string session;

public:
    SrsRtspResponse(int cseq);
    virtual ~SrsRtspResponse();

public:
    // Encode message to string.
    virtual srs_error_t encode(std::stringstream &ss);

protected:
    // Sub classes override this to encode the headers.
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.1 OPTIONS, @see rfc2326-1998-rtsp.pdf, page 59
// The behavior is equivalent to that described in [H9.2]. An OPTIONS
// request may be issued at any time, e.g., if the client is about to
// try a nonstandard request. It does not influence server state.
class SrsRtspOptionsResponse : public SrsRtspResponse
{
public:
    // Join of SrsRtspMethod
    SrsRtspMethod methods;

public:
    SrsRtspOptionsResponse(int cseq);
    virtual ~SrsRtspOptionsResponse();

protected:
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.2 DESCRIBE, @see rfc2326-1998-rtsp.pdf, page 61
class SrsRtspDescribeResponse : public SrsRtspResponse
{
public:
    // The sdp in describe.
    std::string sdp;

public:
    SrsRtspDescribeResponse(int cseq);
    virtual ~SrsRtspDescribeResponse();

protected:
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.4 SETUP, @see rfc2326-1998-rtsp.pdf, page 65
// The SETUP request for a URI specifies the transport mechanism to be
// used for the streamed media. A client can issue a SETUP request for a
// stream that is already playing to change transport parameters, which
// a server MAY allow. If it does not allow this, it MUST respond with
// error "455 Method Not Valid In This State". For the benefit of any
// intervening firewalls, a client must indicate the transport
// parameters even if it has no influence over these parameters, for
// example, where the server advertises a fixed multicast address.
class SrsRtspSetupResponse : public SrsRtspResponse
{
public:
    // The client specified port.
    int client_port_min;
    int client_port_max;
    // The client will use the port in:
    //      [local_port_min, local_port_max)
    int local_port_min;
    int local_port_max;

    SrsRtspTransport *transport;
    // The ssrc of the stream.
    std::string ssrc;

public:
    SrsRtspSetupResponse(int cseq);
    virtual ~SrsRtspSetupResponse();

protected:
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.5 PLAY, @see rfc2326-1998-rtsp.pdf, page 67
class SrsRtspPlayResponse : public SrsRtspResponse
{
public:
    SrsRtspPlayResponse(int cseq);
    virtual ~SrsRtspPlayResponse();

protected:
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// The rtsp protocol stack to parse the rtsp packets.
class SrsRtspStack
{
private:
    // The cached bytes buffer.
    SrsSimpleStream *buf;
    // The underlayer socket object, send/recv bytes.
    ISrsProtocolReadWriter *skt;

public:
    SrsRtspStack(ISrsProtocolReadWriter *s);
    virtual ~SrsRtspStack();

public:
    // Recv rtsp message from underlayer io.
    // @param preq the output rtsp request message, which user must free it.
    // @return an int error code.
    //       ERROR_RTSP_REQUEST_HEADER_EOF indicates request header EOF.
    virtual srs_error_t recv_message(SrsRtspRequest **preq);
    // Try to detect and consume RTCP frame from buffered data.
    // @return srs_success if RTCP frame is consumed successfully.
    //         ERROR_RTSP_NEED_MORE_DATA if more data is needed to complete the frame.
    //         ERROR_RTSP_TOKEN_NOT_NORMAL if the data is not an RTCP interleaved frame.
    virtual srs_error_t try_consume_rtcp_frame();
    // Send rtsp message over underlayer io.
    // @param res the rtsp response message, which user should never free it.
    // @return an int error code.
    virtual srs_error_t send_message(SrsRtspResponse *res);

private:
    // Recv the rtsp message.
    virtual srs_error_t do_recv_message(SrsRtspRequest *req);
    // Read a normal token from io, error when token state is not normal.
    virtual srs_error_t recv_token_normal(std::string &token);
    // Read a normal token from io, error when token state is not eof.
    virtual srs_error_t recv_token_eof(std::string &token);
    // Read the token util got eof, for example, to read the response status Reason-Phrase
    // @param pconsumed, output the token parsed length. NULL to ignore.
    virtual srs_error_t recv_token_util_eof(std::string &token, int *pconsumed = NULL);
    // Read a token from io, split by SP, endswith CRLF:
    //       token1 SP token2 SP ... tokenN CRLF
    // @param token, output the read token.
    // @param state, output the token parse state.
    // @param normal_ch, the char to indicates the normal token.
    //       the SP use to indicates the normal token, @see SRS_RTSP_SP
    //       the 0x00 use to ignore normal token flag. @see recv_token_util_eof
    // @param pconsumed, output the token parsed length. NULL to ignore.
    virtual srs_error_t recv_token(std::string &token, SrsRtspTokenState &state, char normal_ch = SRS_RTSP_SP, int *pconsumed = NULL);
};

#endif
