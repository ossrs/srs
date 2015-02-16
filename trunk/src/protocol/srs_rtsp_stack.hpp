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

// 4 RTSP Message
// Lines are terminated by CRLF, but
// receivers should be prepared to also interpret CR and LF by
// themselves as line terminators.
#define __SRS_RTSP_CRLF "\r\n" // 0x0D0A
#define __SRS_RTSP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// RTSP token
#define __SRS_TOKEN_CSEQ "CSeq"
#define __SRS_TOKEN_PUBLIC "Public"

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
* the rtsp request message.
* 6 Request
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
    int seq;
public:
    SrsRtspRequest();
    virtual ~SrsRtspRequest();
public:
    virtual bool is_options();
};

/**
* the rtsp response message.
* 7 Response
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
    * 12.17 CSeq
    * The CSeq field specifies the sequence number for an RTSP requestresponse
    * pair. This field MUST be present in all requests and
    * responses. For every RTSP request containing the given sequence
    * number, there will be a corresponding response having the same
    * number. Any retransmitted request must contain the same sequence
    * number as the original (i.e. the sequence number is not incremented
    * for retransmissions of the same request).
    */
    int seq;
public:
    SrsRtspResponse(int cseq);
    virtual ~SrsRtspResponse();
public:
    /**
    * encode message to string.
    */
    virtual std::stringstream& encode(std::stringstream& ss);
};

/**
* 10 Method Definitions
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
* 10.1 OPTIONS
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
public:
    virtual std::stringstream& encode(std::stringstream& ss);
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
    */
    virtual int recv_token_util_eof(std::string& token);
    /**
    * read a token from io, split by SP, endswith CRLF:
    *       token1 SP token2 SP ... tokenN CRLF
    * @param normal_ch, the char to indicates the normal token. 
    *       the SP use to indicates the normal token, @see __SRS_RTSP_SP
    *       the 0x00 use to ignore normal token flag. @see recv_token_util_eof
    * @param token, output the read token.
    * @param state, output the token parse state.
    */
    virtual int recv_token(std::string& token, SrsRtspTokenState& state, char normal_ch = __SRS_RTSP_SP);
};

#endif

#endif

