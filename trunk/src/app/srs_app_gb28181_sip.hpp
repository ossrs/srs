/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Lixin
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

#ifndef SRS_APP_GB28181_SIP_HPP
#define SRS_APP_GB28181_SIP_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

#include <srs_app_log.hpp>
#include <srs_sip_stack.hpp>
#include <srs_app_gb28181.hpp>


class SrsConfDirective;
class SrsSipRequest;
class SrsGb28181Config;
class SrsSipStack;
class SrsGb28181SipService;

enum SrsGb28281SipSessionStatusType{
     SrsGb28181SipSessionUnkonw = 0,
     SrsGb28181SipSessionRegisterOk = 1,
     SrsGb28181SipSessionAliveOk = 2,
     SrsGb28181SipSessionInviteOk = 3,
     SrsGb28181SipSessionTrying = 4,
     SrsGb28181SipSessionBye = 5,
};

class SrsGb28181SipSession
{
private:
    //SrsSipRequest *req;
    SrsGb28181SipService *caster;
    std::string   session_id;
private:
    SrsGb28281SipSessionStatusType _register_status;
    SrsGb28281SipSessionStatusType _alive_status;
    SrsGb28281SipSessionStatusType _invite_status;
    srs_utime_t _register_time;
    srs_utime_t _alive_time;
    srs_utime_t _invite_time;
    srs_utime_t _recv_rtp_time;
    int  _reg_expires;

    std::string _peer_ip;
    int _peer_port;

    sockaddr *_from;
    int _fromlen;
    SrsSipRequest *req;

public:
    void set_register_status(SrsGb28281SipSessionStatusType s) { _register_status = s;}
    void set_alive_status(SrsGb28281SipSessionStatusType s) { _alive_status = s;}
    void set_invite_status(SrsGb28281SipSessionStatusType s) { _invite_status = s;}
    void set_register_time(srs_utime_t t) { _register_time = t;}
    void set_alive_time(srs_utime_t t) { _alive_time = t;}
    void set_invite_time(srs_utime_t t) { _invite_time = t;}
    void set_recv_rtp_time(srs_utime_t t) { _recv_rtp_time = t;}
    void set_reg_expires(int e) { _reg_expires = e;}
    void set_peer_ip(std::string i) { _peer_ip = i;}
    void set_peer_port(int o) { _peer_port = o;}
    void set_sockaddr(sockaddr *f) { _from = f;}
    void set_sockaddr_len(int l) { _fromlen = l;}
    void set_request(SrsSipRequest *r) { req->copy(r);}

    SrsGb28281SipSessionStatusType register_status() { return _register_status;}
    SrsGb28281SipSessionStatusType alive_status() { return  _alive_status;}
    SrsGb28281SipSessionStatusType invite_status() { return  _invite_status;}
    srs_utime_t register_time() { return  _register_time;}
    srs_utime_t alive_time() { return _alive_time;}
    srs_utime_t invite_time() { return _invite_time;}
    srs_utime_t recv_rtp_time() { return _recv_rtp_time;}
    int reg_expires() { return _reg_expires;}
    std::string peer_ip() { return _peer_ip;}
    int peer_port() { return _peer_port;}
    sockaddr* sockaddr_from() { return _from;}
    int sockaddr_fromlen() { return _fromlen;}
    SrsSipRequest request() { return *req;}

public:
    SrsGb28181SipSession(SrsGb28181SipService *c, SrsSipRequest* r);
    virtual ~SrsGb28181SipSession();

};

class SrsGb28181SipService : public ISrsUdpHandler
{
private:
    SrsSipStack *sip;
    SrsGb28181Config *config;
    srs_netfd_t lfd;

    std::map<std::string, SrsGb28181SipSession*> sessions;
public:
    SrsGb28181SipService(SrsConfDirective* c);
    virtual ~SrsGb28181SipService();

    // Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
    virtual void set_stfd(srs_netfd_t fd);
private:
    void destroy();
    srs_error_t on_udp_sip(std::string host, int port, char* buf, int nb_buf, sockaddr* from, int fromlen);
public:
    int send_message(sockaddr* f, int l, std::stringstream& ss);
   
    int send_ack(SrsSipRequest *req, sockaddr *f, int l);
    int send_status(SrsSipRequest *req, sockaddr *f, int l);

    int send_invite(SrsSipRequest *req, std::string ip, int port, uint32_t ssrc);
    int send_bye(SrsSipRequest *req);

    // The SIP command is transmitted through HTTP API, 
    // and the body content is transmitted to the device, 
    // mainly for testing and debugging, For example, here is HTTP body:
    // BYE sip:34020000001320000003@3402000000 SIP/2.0
    // Via: SIP/2.0/UDP 39.100.155.146:15063;rport;branch=z9hG4bK34205410
    // From: <sip:34020000002000000001@3402000000>;tag=512355410
    // To: <sip:34020000001320000003@3402000000>;tag=680367414
    // Call-ID: 200003304
    // CSeq: 21 BYE
    // Max-Forwards: 70
    // User-Agent: SRS/4.0.4(Leo)
    // Content-Length: 0
    //
    //
    int send_sip_raw_data(SrsSipRequest *req, std::string data);

    SrsGb28181SipSession* create_sip_session(SrsSipRequest *req);
    SrsGb28181SipSession* fetch(std::string id);
    void remove_session(std::string id);
};

#endif

