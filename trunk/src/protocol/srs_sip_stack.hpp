/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#ifndef SRS_PROTOCOL_SIP_HPP
#define SRS_PROTOCOL_SIP_HPP

#include <srs_core.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

#include <string>
#include <sstream>

#include <srs_kernel_consts.hpp>
#include <srs_rtsp_stack.hpp>

class SrsBuffer;
class SrsSimpleStream;
class SrsAudioFrame;

// SIP methods
#define SRS_SIP_METHOD_REGISTER       "REGISTER"
#define SRS_SIP_METHOD_MESSAGE        "MESSAGE"
#define SRS_SIP_METHOD_INVITE         "INVITE"
#define SRS_SIP_METHOD_ACK            "ACK"
#define SRS_SIP_METHOD_BYE            "BYE"

// SIP-Version
#define SRS_SIP_VERSION "SIP/2.0"
#define SRS_SIP_USER_AGENT RTMP_SIG_SRS_SERVER


enum SrsSipCmdType{
    SrsSipCmdRequest=0,
    SrsSipCmdRespone=1
};

class SrsSipRequest
{
public:
    //sip header member
    std::string method;
    std::string uri;
    std::string version;
    std::string status;

    std::string via;
    std::string from;
    std::string to;
    std::string from_tag;
    std::string to_tag;
    std::string branch;
    
    std::string call_id;
    long seq;

    std::string contact;
    std::string user_agent;

    std::string content_type;
    long content_length;

    long expires;
    int max_forwards;

public:
    std::string serial;
    std::string realm;
    std::string sip_auth_id;
    std::string sip_auth_pwd;
    std::string sip_username;
    std::string peer_ip;
    int peer_port;
    std::string host;
    int host_port;
    SrsSipCmdType cmdtype;

public:
    SrsRtspSdp* sdp;
    SrsRtspTransport* transport;
public:
    SrsSipRequest();
    virtual ~SrsSipRequest();
public:
    virtual bool is_register();
    virtual bool is_invite();
    virtual bool is_message();
    virtual bool is_ack();
    virtual bool is_bye();
   
    virtual void copy(SrsSipRequest* src);
public:
    virtual std::string get_cmdtype_str();
};

// The gb28181 sip protocol stack.
class SrsSipStack
{
private:
    // The cached bytes buffer.
    SrsSimpleStream* buf;
public:
    SrsSipStack();
    virtual ~SrsSipStack();
public:
    virtual srs_error_t parse_request(SrsSipRequest** preq, const char *recv_msg, int nb_buf);
protected:
    virtual srs_error_t do_parse_request(SrsSipRequest* req, const char *recv_msg);

public:
    virtual srs_error_t resp_status(std::stringstream& ss, SrsSipRequest *req);
    virtual srs_error_t resp_keepalive(std::stringstream& ss, SrsSipRequest *req);
    virtual srs_error_t resp_ack(std::stringstream& ss, SrsSipRequest *req);
     
    virtual srs_error_t req_invite(std::stringstream& ss, SrsSipRequest *req, int port);
    virtual srs_error_t req_bye(std::stringstream& ss, SrsSipRequest *req);
   
};

#endif

#endif

