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
#include <srs_app_pithy_print.hpp>


class SrsConfDirective;
class SrsSipRequest;
class SrsGb28181Config;
class SrsSipStack;
class SrsGb28181SipService;
class SrsGb28181Device;

enum SrsGb28181SipSessionStatusType{
     SrsGb28181SipSessionUnkonw = 0,
     SrsGb28181SipSessionRegisterOk = 1,
     SrsGb28181SipSessionAliveOk = 2,
     SrsGb28181SipSessionInviteOk = 3,
     SrsGb28181SipSessionTrying = 4,
     SrsGb28181SipSessionBye = 5,
};

class SrsGb28181Device
{
public:
    SrsGb28181Device();
    virtual ~SrsGb28181Device();
public:
    std::string device_id;
    std::string device_status;
    SrsGb28181SipSessionStatusType  invite_status; 
    srs_utime_t  invite_time;
    SrsSipRequest  req_inivate;
};

class SrsGb28181SipSession: public ISrsCoroutineHandler, public ISrsConnection
{
private:
    //SrsSipRequest *req;
    SrsGb28181SipService *servcie;
    std::string   _session_id;
    SrsCoroutine* trd;
    SrsPithyPrint* pprint;
private:
    SrsGb28181SipSessionStatusType _register_status;
    SrsGb28181SipSessionStatusType _alive_status;
    SrsGb28181SipSessionStatusType _invite_status;
    srs_utime_t _register_time;
    srs_utime_t _alive_time;
    srs_utime_t _invite_time;
    srs_utime_t _reg_expires;
    srs_utime_t _query_catalog_time;

    std::string _peer_ip;
    int _peer_port;

    sockaddr  _from;
    int _fromlen;
    SrsSipRequest *req;

    std::map<std::string, SrsGb28181Device*> _device_list;
    //std::map<std::string, int> _device_status;
    int _sip_cseq;

public:
    SrsGb28181SipSession(SrsGb28181SipService *c, SrsSipRequest* r);
    virtual ~SrsGb28181SipSession();

private:
    void destroy();

public:
    void set_register_status(SrsGb28181SipSessionStatusType s) { _register_status = s;}
    void set_alive_status(SrsGb28181SipSessionStatusType s) { _alive_status = s;}
    void set_invite_status(SrsGb28181SipSessionStatusType s) { _invite_status = s;}
    void set_register_time(srs_utime_t t) { _register_time = t;}
    void set_alive_time(srs_utime_t t) { _alive_time = t;}
    void set_invite_time(srs_utime_t t) { _invite_time = t;}
    //void set_recv_rtp_time(srs_utime_t t) { _recv_rtp_time = t;}
    void set_reg_expires(int e) { _reg_expires = e*SRS_UTIME_SECONDS;}
    void set_peer_ip(std::string i) { _peer_ip = i;}
    void set_peer_port(int o) { _peer_port = o;}
    void set_sockaddr(sockaddr  f) { _from = f;}
    void set_sockaddr_len(int l) { _fromlen = l;}
    void set_request(SrsSipRequest *r) { req->copy(r);}

    SrsGb28181SipSessionStatusType register_status() { return _register_status;}
    SrsGb28181SipSessionStatusType alive_status() { return  _alive_status;}
    SrsGb28181SipSessionStatusType invite_status() { return  _invite_status;}
    srs_utime_t register_time() { return  _register_time;}
    srs_utime_t alive_time() { return _alive_time;}
    srs_utime_t invite_time() { return _invite_time;}
    //srs_utime_t recv_rtp_time() { return _recv_rtp_time;}
    int reg_expires() { return _reg_expires;}
    std::string peer_ip() { return _peer_ip;}
    int peer_port() { return _peer_port;}
    sockaddr  sockaddr_from() { return _from;}
    int sockaddr_fromlen() { return _fromlen;}
    SrsSipRequest request() { return *req;}
    int sip_cseq(){ return _sip_cseq++;}

    std::string session_id() { return _session_id;}
public:
    void update_device_list(std::map<std::string, std::string> devlist);
    SrsGb28181Device *get_device_info(std::string chid);
    void dumps(SrsJsonObject* obj);

public:
    virtual srs_error_t serve();
    
// Interface ISrsOneCycleThreadHandler
public:
    virtual srs_error_t cycle();   
    virtual std::string remote_ip();
private:
    virtual srs_error_t do_cycle();
};

class SrsGb28181SipService : public ISrsUdpHandler
{
private:
    SrsSipStack *sip;
    SrsGb28181Config *config;
    srs_netfd_t lfd;

    std::map<std::string, SrsGb28181SipSession*> sessions;
    std::map<std::string, SrsGb28181SipSession*> sessions_by_callid;
public:
    SrsGb28181SipService(SrsConfDirective* c);
    virtual ~SrsGb28181SipService();

    // Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
    virtual void set_stfd(srs_netfd_t fd);
private:
    void destroy();
    srs_error_t on_udp_sip(std::string host, int port, std::string recv_msg, sockaddr* from, int fromlen);
public:
    int send_message(sockaddr* f, int l, std::stringstream& ss);
   
    int send_ack(SrsSipRequest *req, sockaddr *f, int l);
    int send_status(SrsSipRequest *req, sockaddr *f, int l);

    srs_error_t send_invite(SrsSipRequest *req, std::string ip, int port, uint32_t ssrc, std::string chid);
    srs_error_t send_bye(SrsSipRequest *req, std::string chid);
    srs_error_t send_query_catalog(SrsSipRequest *req);
    srs_error_t send_ptz(SrsSipRequest *req, std::string chid, std::string cmd, uint8_t speed, int priority);

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
    srs_error_t send_sip_raw_data(SrsSipRequest *req, std::string data);
    srs_error_t query_sip_session(std::string sid,  SrsJsonArray* arr);

public:
    srs_error_t fetch_or_create_sip_session(SrsSipRequest *req,  SrsGb28181SipSession** sess);
    SrsGb28181SipSession* fetch(std::string id);
    void remove_session(std::string id);
    SrsGb28181Config* get_config();
    
    void sip_session_map_by_callid(SrsGb28181SipSession *sess, std::string call_id);
    void sip_session_unmap_by_callid(std::string call_id);
    SrsGb28181SipSession* fetch_session_by_callid(std::string call_id);
};

#endif

