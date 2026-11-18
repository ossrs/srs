//
// Copyright (c) 2013-2026 The SRS Authors
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

#define SRS_RTSP_CR SRS_CONSTS_CR
#define SRS_RTSP_LF SRS_CONSTS_LF
#define SRS_RTSP_SP ' '

// 4 RTSP Message, @see rfc2326-1998-rtsp.pdf, page 37
// Lines end with CRLF, but receivers also take CR or LF alone as a line end.
#define SRS_RTSP_CRLF "\r\n"
#define SRS_RTSP_CRLFCRLF "\r\n\r\n"

#define SRS_RTSP_TOKEN_CSEQ "CSeq"
#define SRS_RTSP_TOKEN_PUBLIC "Public"
#define SRS_RTSP_TOKEN_CONTENT_TYPE "Content-Type"
#define SRS_RTSP_TOKEN_CONTENT_LENGTH "Content-Length"
#define SRS_RTSP_TOKEN_TRANSPORT "Transport"
#define SRS_RTSP_TOKEN_SESSION "Session"
#define SRS_RTSP_TOKEN_ACCEPT "Accept"
#define SRS_RTSP_TOKEN_USER_AGENT "User-Agent"
#define SRS_RTSP_TOKEN_RANGE "Range"

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

#define SRS_RTSP_VERSION "RTSP/1.0"

// 10 Method Definitions, @see rfc2326-1998-rtsp.pdf, page 57
// The method is case-sensitive.
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

enum SrsRtspTokenState {
    SrsRtspTokenStateError = 100,
    // The token ends with SP.
    SrsRtspTokenStateNormal = 101,
    // The token ends with CR or LF.
    SrsRtspTokenStateEOF = 102,
};

// 12.39 Transport, @see rfc2326-1998-rtsp.pdf, page 115
class SrsRtspTransport
{
public:
    // The transport specifier is transport/profile/lower-transport.
    std::string transport_;
    std::string profile_;
    std::string lower_transport_;
    // unicast or multicast; the default is multicast.
    std::string cast_type_;
    // The channel of the $ frames that mix the media with the RTSP connection, or a range of
    // channels, such as interleaved=4-5.
    std::string interleaved_;
    int interleaved_min_;
    int interleaved_max_;
    // PLAY or RECORD; the default is PLAY.
    std::string mode_;
    // The client's RTP and RTCP ports, such as client_port=3456-3457.
    int client_port_min_;
    int client_port_max_;

public:
    SrsRtspTransport();
    virtual ~SrsRtspTransport();

public:
    virtual srs_error_t parse(std::string attr);
    virtual void copy(SrsRtspTransport *src);
};

// 6 Request, @see rfc2326-1998-rtsp.pdf, page 39
class SrsRtspRequest
{
public:
    // 6.1 Request-Line = Method SP Request-URI SP RTSP-Version CRLF
    std::string method_;
    std::string uri_;
    std::string version_;
    // 12.17 CSeq
    // The response has the CSeq of its request, and a retransmitted request keeps its CSeq.
    long seq_;
    // 12.16 Content-Type, @see rfc2326-1998-rtsp.pdf, page 99
    std::string content_type_;
    // 12.14 Content-Length, @see rfc2326-1998-rtsp.pdf, page 99
    // Unlike HTTP, every message with a body must have it; when it is missing, it is zero.
    long content_length_;
    std::string session_;

    // NULL when the request has no Transport header.
    SrsRtspTransport *transport_;
    int stream_id_;

    std::string accept_;
    std::string user_agent_;
    std::string range_;

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

// 7 Response, @see rfc2326-1998-rtsp.pdf, page 43
class SrsRtspResponse
{
public:
    // 7.1 Status-Line = RTSP-Version SP Status-Code SP Reason-Phrase CRLF
    int status_;
    // 12.17 CSeq, @see rfc2326-1998-rtsp.pdf, page 99
    long seq_;
    std::string session_;

public:
    SrsRtspResponse(int cseq);
    virtual ~SrsRtspResponse();

public:
    virtual srs_error_t encode(std::stringstream &ss);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.1 OPTIONS, @see rfc2326-1998-rtsp.pdf, page 59
class SrsRtspOptionsResponse : public SrsRtspResponse
{
public:
    // An OR of SrsRtspMethod flags.
    SrsRtspMethod methods_;

public:
    SrsRtspOptionsResponse(int cseq);
    virtual ~SrsRtspOptionsResponse();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.2 DESCRIBE, @see rfc2326-1998-rtsp.pdf, page 61
class SrsRtspDescribeResponse : public SrsRtspResponse
{
public:
    std::string sdp_;

public:
    SrsRtspDescribeResponse(int cseq);
    virtual ~SrsRtspDescribeResponse();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.4 SETUP, @see rfc2326-1998-rtsp.pdf, page 65
class SrsRtspSetupResponse : public SrsRtspResponse
{
public:
    int client_port_min_;
    int client_port_max_;
    int local_port_min_;
    int local_port_max_;

    SrsRtspTransport *transport_;
    std::string ssrc_;

public:
    SrsRtspSetupResponse(int cseq);
    virtual ~SrsRtspSetupResponse();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_header(std::stringstream &ss);
};

// 10.5 PLAY, @see rfc2326-1998-rtsp.pdf, page 67
class SrsRtspPlayResponse : public SrsRtspResponse
{
public:
    SrsRtspPlayResponse(int cseq);
    virtual ~SrsRtspPlayResponse();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t encode_header(std::stringstream &ss);
};

class ISrsRtspStack
{
public:
    ISrsRtspStack();
    virtual ~ISrsRtspStack();

public:
    // The caller frees *preq. An empty request line fails with ERROR_RTSP_REQUEST_HEADER_EOF.
    virtual srs_error_t recv_message(SrsRtspRequest **preq) = 0;
    virtual srs_error_t send_message(SrsRtspResponse *res) = 0;
};

class SrsRtspStack : public ISrsRtspStack
{
private:
    SrsSimpleStream *buf_;
    ISrsProtocolReadWriter *skt_;

public:
    SrsRtspStack(ISrsProtocolReadWriter *s);
    virtual ~SrsRtspStack();

public:
    virtual srs_error_t recv_message(SrsRtspRequest **preq);
    virtual srs_error_t try_consume_rtcp_frame();
    virtual srs_error_t send_message(SrsRtspResponse *res);

private:
    virtual srs_error_t do_recv_message(SrsRtspRequest *req);
    virtual srs_error_t skip_body(int64_t size);
    virtual srs_error_t recv_token_normal(std::string &token);
    virtual srs_error_t recv_token_eof(std::string &token);
    virtual srs_error_t recv_line(std::string &line);
    virtual srs_error_t recv_token(std::string &token, SrsRtspTokenState &state, char normal_ch = SRS_RTSP_SP, int *pconsumed = NULL);
};

#endif
