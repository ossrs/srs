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

#ifndef SRS_PROTOCOL_RTSP_STACK_HPP
#define SRS_PROTOCOL_RTSP_STACK_HPP

/*
#include <srs_rtsp_stack.hpp>
*/

#include <srs_core.hpp>

#include <string>
#include <sstream>

#include <srs_kernel_consts.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

class SrsSimpleBuffer;
class ISrsProtocolReaderWriter;

// rtsp specification
// CR             = <US-ASCII CR, carriage return (13)>
#define __SRS_RTSP_CR SRS_CONSTS_CR // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define __SRS_RTSP_LF SRS_CONSTS_LF // 0x0A
// SP             = <US-ASCII SP, space (32)>
#define __SRS_RTSP_SP ' ' // 0x20

// 4 RTSP Message, @see rtsp-rfc2326-1998.pdf, page 37
// Lines are terminated by CRLF, but
// receivers should be prepared to also interpret CR and LF by
// themselves as line terminators.
#define __SRS_RTSP_CRLF "\r\n" // 0x0D0A
#define __SRS_RTSP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// RTSP token
#define __SRS_TOKEN_CSEQ "CSeq"
#define __SRS_TOKEN_PUBLIC "Public"
#define __SRS_TOKEN_CONTENT_TYPE "Content-Type"
#define __SRS_TOKEN_CONTENT_LENGTH "Content-Length"
#define __SRS_TOKEN_TRANSPORT "Transport"
#define __SRS_TOKEN_SESSION "Session"

// RTSP methods
#define __SRS_METHOD_OPTIONS            "OPTIONS"
#define __SRS_METHOD_DESCRIBE           "DESCRIBE"
#define __SRS_METHOD_ANNOUNCE           "ANNOUNCE"
#define __SRS_METHOD_SETUP              "SETUP"
#define __SRS_METHOD_PLAY               "PLAY"
#define __SRS_METHOD_PAUSE              "PAUSE"
#define __SRS_METHOD_TEARDOWN           "TEARDOWN"
#define __SRS_METHOD_GET_PARAMETER      "GET_PARAMETER"
#define __SRS_METHOD_SET_PARAMETER      "SET_PARAMETER"
#define __SRS_METHOD_REDIRECT           "REDIRECT"
#define __SRS_METHOD_RECORD             "RECORD"
// Embedded (Interleaved) Binary Data

// RTSP-Version
#define __SRS_VERSION "RTSP/1.0"

/**
* the rtsp sdp parse state.
*/
enum SrsRtspSdpState
{
    /**
    * other sdp properties.
    */
    SrsRtspSdpStateOthers,
    /**
    * parse sdp audio state.
    */
    SrsRtspSdpStateAudio,
    /**
    * parse sdp video state.
    */
    SrsRtspSdpStateVideo,
};

/**
* 10 Method Definitions, @see rtsp-rfc2326-1998.pdf, page 57
* The method token indicates the method to be performed on the resource
* identified by the Request-URI. The method is case-sensitive. New
* methods may be defined in the future. Method names may not start with
* a $ character (decimal 24) and must be a token. Methods are
* summarized in Table 2.
* Notes on Table 2: PAUSE is recommended, but not required in that a
* fully functional server can be built that does not support this
* method, for example, for live feeds. If a server does not support a
* particular method, it MUST return "501 Not Implemented" and a client
* SHOULD not try this method again for this server.
*/
enum SrsRtspMethod
{
    SrsRtspMethodDescribe           = 0x0001,
    SrsRtspMethodAnnounce           = 0x0002,
    SrsRtspMethodGetParameter       = 0x0004,
    SrsRtspMethodOptions            = 0x0008,
    SrsRtspMethodPause              = 0x0010,
    SrsRtspMethodPlay               = 0x0020,
    SrsRtspMethodRecord             = 0x0040,
    SrsRtspMethodRedirect           = 0x0080,
    SrsRtspMethodSetup              = 0x0100,
    SrsRtspMethodSetParameter       = 0x0200,
    SrsRtspMethodTeardown           = 0x0400,
};

/**
* the state of rtsp token.
*/
enum SrsRtspTokenState
{
    /**
    * parse token failed, default state.
    */
    SrsRtspTokenStateError = 100,
    /**
    * when SP follow the token.
    */
    SrsRtspTokenStateNormal = 101,
    /**
    * when CRLF follow the token.
    */
    SrsRtspTokenStateEOF = 102,
};

/**
* the sdp in announce, @see rtsp-rfc2326-1998.pdf, page 159
* Appendix C: Use of SDP for RTSP Session Descriptions
* The Session Description Protocol (SDP, RFC 2327 [6]) may be used to
* describe streams or presentations in RTSP.
*/
class SrsRtspSdp
{
private:
    SrsRtspSdpState state;
public:
    /**
    * the version of sdp.
    */
    std::string version;
    /**
    * the owner/creator of sdp.
    */
    std::string owner_username;
    std::string owner_session_id;
    std::string owner_session_version;
    std::string owner_network_type;
    std::string owner_address_type;
    std::string owner_address;
    /**
    * the session name of sdp.
    */
    std::string session_name;
    /**
    * the connection info of sdp.
    */
    std::string connection_network_type;
    std::string connection_address_type;
    std::string connection_address;
    /**
    * the tool attribute of sdp.
    */
    std::string tool;
    /**
    * the video attribute of sdp.
    */
    std::string video_port;
    std::string video_protocol;
    std::string video_transport_format;
    std::string video_bandwidth_kbps;
    std::string video_codec;
    std::string video_sample_rate;
    std::string video_stream_id;
    // fmtp
    std::string video_packetization_mode;
    std::string video_sps; // sequence header: sps.
    std::string video_pps; // sequence header: pps.
    /**
    * the audio attribute of sdp.
    */
    std::string audio_port;
    std::string audio_protocol;
    std::string audio_transport_format;
    std::string audio_bandwidth_kbps;
    std::string audio_codec;
    std::string audio_sample_rate;
    std::string audio_channel;
    std::string audio_stream_id;
    // fmtp
    std::string audio_profile_level_id;
    std::string audio_mode;
    std::string audio_size_length;
    std::string audio_index_length;
    std::string audio_index_delta_length;
    std::string audio_sh; // sequence header.
public:
    SrsRtspSdp();
    virtual ~SrsRtspSdp();
public:
    /**
    * parse a line of token for sdp.
    */
    virtual int parse(std::string token);
private:
    /**
    * generally, the fmtp is the sequence header for video or audio.
    */
    virtual int parse_fmtp_attribute(std::string attr);
    /**
    * generally, the control is the stream info for video or audio.
    */
    virtual int parse_control_attribute(std::string attr);
    /**
    * decode the string by base64.
    */
    virtual std::string base64_decode(std::string value);
};

/**
* the rtsp transport.
* 12.39 Transport, @see rtsp-rfc2326-1998.pdf, page 115
* This request header indicates which transport protocol is to be used
* and configures its parameters such as destination address,
* compression, multicast time-to-live and destination port for a single
* stream. It sets those values not already determined by a presentation
* description.
*/
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
    // for each.
    std::string cast_type;
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
    /**
    * parse a line of token for transport.
    */
    virtual int parse(std::string attr);
};

/**
* the rtsp request message.
* 6 Request, @see rtsp-rfc2326-1998.pdf, page 39
* A request message from a client to a server or vice versa includes,
* within the first line of that message, the method to be applied to
* the resource, the identifier of the resource, and the protocol
* version in use.
* Request = Request-Line ; Section 6.1
*           *( general-header ; Section 5
*           | request-header ; Section 6.2
*           | entity-header ) ; Section 8.1
*           CRLF
*           [ message-body ] ; Section 4.3
*/
class SrsRtspRequest
{
public:
    /**
    * 6.1 Request Line
    * Request-Line = Method SP Request-URI SP RTSP-Version CRLF
    */
    std::string method;
    std::string uri;
    std::string version;
    /**
    * 12.17 CSeq
    * The CSeq field specifies the sequence number for an RTSP requestresponse
    * pair. This field MUST be present in all requests and
    * responses. For every RTSP request containing the given sequence
    * number, there will be a corresponding response having the same
    * number. Any retransmitted request must contain the same sequence
    * number as the original (i.e. the sequence number is not incremented
    * for retransmissions of the same request).
    */
    long seq;
    /**
    * 12.16 Content-Type, @see rtsp-rfc2326-1998.pdf, page 99
    * See [H14.18]. Note that the content types suitable for RTSP are
    * likely to be restricted in practice to presentation descriptions and
    * parameter-value types.
    */
    std::string content_type;
    /**
    * 12.14 Content-Length, @see rtsp-rfc2326-1998.pdf, page 99
    * This field contains the length of the content of the method (i.e.
    * after the double CRLF following the last header). Unlike HTTP, it
    * MUST be included in all messages that carry content beyond the header
    * portion of the message. If it is missing, a default value of zero is
    * assumed. It is interpreted according to [H14.14].
    */
    long content_length;
    /**
    * the session id.
    */
    std::string session;

    /**
    * the sdp in announce, NULL for no sdp.
    */
    SrsRtspSdp* sdp;
    /**
    * the transport in setup, NULL for no transport.
    */
    SrsRtspTransport* transport;
    /**
    * for setup message, parse the stream id from uri.
    */
    int stream_id;
public:
    SrsRtspRequest();
    virtual ~SrsRtspRequest();
public:
    virtual bool is_options();
    virtual bool is_announce();
    virtual bool is_setup();
    virtual bool is_record();
};

/**
* the rtsp response message.
* 7 Response, @see rtsp-rfc2326-1998.pdf, page 43
* [H6] applies except that HTTP-Version is replaced by RTSP-Version.
* Also, RTSP defines additional status codes and does not define some
* HTTP codes. The valid response codes and the methods they can be used
* with are defined in Table 1.
* After receiving and interpreting a request message, the recipient
* responds with an RTSP response message.
*       Response = Status-Line ; Section 7.1
*                   *( general-header ; Section 5
*                   | response-header ; Section 7.1.2
*                   | entity-header ) ; Section 8.1
*                   CRLF
*                   [ message-body ] ; Section 4.3
*/
class SrsRtspResponse
{
public:
    /**
    * 7.1 Status-Line
    * The first line of a Response message is the Status-Line, consisting
    * of the protocol version followed by a numeric status code, and the
    * textual phrase associated with the status code, with each element
    * separated by SP characters. No CR or LF is allowed except in the
    * final CRLF sequence.
    *       Status-Line = RTSP-Version SP Status-Code SP Reason-Phrase CRLF
    */
    // @see about the version of rtsp, see __SRS_VERSION
    // @see about the status of rtsp, see SRS_CONSTS_RTSP_OK
    int status;
    /**
    * 12.17 CSeq, @see rtsp-rfc2326-1998.pdf, page 99
    * The CSeq field specifies the sequence number for an RTSP requestresponse
    * pair. This field MUST be present in all requests and
    * responses. For every RTSP request containing the given sequence
    * number, there will be a corresponding response having the same
    * number. Any retransmitted request must contain the same sequence
    * number as the original (i.e. the sequence number is not incremented
    * for retransmissions of the same request).
    */
    long seq;
    /**
    * the session id.
    */
    std::string session;
public:
    SrsRtspResponse(int cseq);
    virtual ~SrsRtspResponse();
public:
    /**
    * encode message to string.
    */
    virtual int encode(std::stringstream& ss);
protected:
    /**
    * sub classes override this to encode the headers.
    */
    virtual int encode_header(std::stringstream& ss);
};

/**
* 10.1 OPTIONS, @see rtsp-rfc2326-1998.pdf, page 59
* The behavior is equivalent to that described in [H9.2]. An OPTIONS
* request may be issued at any time, e.g., if the client is about to
* try a nonstandard request. It does not influence server state.
*/
class SrsRtspOptionsResponse : public SrsRtspResponse
{
public:
    /**
    * join of SrsRtspMethod
    */
    SrsRtspMethod methods;
public:
    SrsRtspOptionsResponse(int cseq);
    virtual ~SrsRtspOptionsResponse();
protected:
    virtual int encode_header(std::stringstream& ss);
};

/**
* 10.4 SETUP, @see rtsp-rfc2326-1998.pdf, page 65
* The SETUP request for a URI specifies the transport mechanism to be
* used for the streamed media. A client can issue a SETUP request for a
* stream that is already playing to change transport parameters, which
* a server MAY allow. If it does not allow this, it MUST respond with
* error "455 Method Not Valid In This State". For the benefit of any
* intervening firewalls, a client must indicate the transport
* parameters even if it has no influence over these parameters, for
* example, where the server advertises a fixed multicast address.
*/
class SrsRtspSetupResponse : public SrsRtspResponse
{
public:
    // the client specified port.
    int client_port_min;
    int client_port_max;
    // client will use the port in:
    //      [local_port_min, local_port_max)
    int local_port_min;
    int local_port_max;
    // session.
    std::string session;
public:
    SrsRtspSetupResponse(int cseq);
    virtual ~SrsRtspSetupResponse();
protected:
    virtual int encode_header(std::stringstream& ss);
};

/**
* the rtsp protocol stack to parse the rtsp packets.
*/
class SrsRtspStack
{
private:
    /**
    * cached bytes buffer.
    */
    SrsSimpleBuffer* buf;
    /**
    * underlayer socket object, send/recv bytes.
    */
    ISrsProtocolReaderWriter* skt;
public:
    SrsRtspStack(ISrsProtocolReaderWriter* s);
    virtual ~SrsRtspStack();
public:
    /**
    * recv rtsp message from underlayer io.
    * @param preq the output rtsp request message, which user must free it.
    * @return an int error code. 
    *       ERROR_RTSP_REQUEST_HEADER_EOF indicates request header EOF.
    */
    virtual int recv_message(SrsRtspRequest** preq);
    /**
    * send rtsp message over underlayer io.
    * @param res the rtsp response message, which user should never free it.
    * @return an int error code.
    */
    virtual int send_message(SrsRtspResponse* res);
private:
    /**
    * recv the rtsp message.
    */
    virtual int do_recv_message(SrsRtspRequest* req);
    /**
    * read a normal token from io, error when token state is not normal.
    */
    virtual int recv_token_normal(std::string& token);
    /**
    * read a normal token from io, error when token state is not eof.
    */
    virtual int recv_token_eof(std::string& token);
    /**
    * read the token util got eof, for example, to read the response status Reason-Phrase
    * @param pconsumed, output the token parsed length. NULL to ignore.
    */
    virtual int recv_token_util_eof(std::string& token, int* pconsumed = NULL);
    /**
    * read a token from io, split by SP, endswith CRLF:
    *       token1 SP token2 SP ... tokenN CRLF
    * @param token, output the read token.
    * @param state, output the token parse state.
    * @param normal_ch, the char to indicates the normal token. 
    *       the SP use to indicates the normal token, @see __SRS_RTSP_SP
    *       the 0x00 use to ignore normal token flag. @see recv_token_util_eof
    * @param pconsumed, output the token parsed length. NULL to ignore.
    */
    virtual int recv_token(std::string& token, SrsRtspTokenState& state, char normal_ch = __SRS_RTSP_SP, int* pconsumed = NULL);
};

#endif

#endif

