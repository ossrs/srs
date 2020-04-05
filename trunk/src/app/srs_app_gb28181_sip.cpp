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

#include <srs_app_gb28181_sip.hpp>

#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>

#include <srs_kernel_log.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_sip_stack.hpp>
#include <srs_app_gb28181.hpp>


std::string srs_get_sip_session_status_str(SrsGb28181SipSessionStatusType status)
{
    switch(status){
        case SrsGb28181SipSessionRegisterOk:
            return "RegisterOk";
        case SrsGb28181SipSessionAliveOk:
            return "AliveOk";
        case SrsGb28181SipSessionInviteOk:
            return "InviteOk";
        case SrsGb28181SipSessionTrying:
            return "InviteTrying";
        case SrsGb28181SipSessionBye:
            return "InviteBye";
        default:
            return "Unknow";
    }
}

SrsGb28181SipSession::SrsGb28181SipSession(SrsGb28181SipService *c, SrsSipRequest* r)
{
    servcie = c;
    req = new SrsSipRequest();
    req->copy(r);
    _session_id = req->sip_auth_id;
    _reg_expires = 3600 * SRS_UTIME_SECONDS;

    trd = new SrsSTCoroutine("gb28181sip", this);
    pprint = SrsPithyPrint::create_caster();

    _register_status = SrsGb28181SipSessionUnkonw;
    _alive_status = SrsGb28181SipSessionUnkonw;
    _invite_status = SrsGb28181SipSessionUnkonw;
    _register_time = 0;
    _alive_time = 0;
    _invite_time = 0;
  
    _peer_ip = "";
    _peer_port = 0;

    _fromlen = 0;
}

SrsGb28181SipSession::~SrsGb28181SipSession()
{
    srs_freep(req);
    srs_freep(trd);
    srs_freep(pprint);
}

srs_error_t SrsGb28181SipSession::serve()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "gb28181sip");
    }
    
    return err;
}

srs_error_t SrsGb28181SipSession::do_cycle()
{
    srs_error_t err = srs_success;
    _register_time = srs_get_system_time();
    _alive_time = srs_get_system_time();
    _invite_time = srs_get_system_time();
  
    while (true) {

        pprint->elapse();

        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "gb28181 sip session cycle");
        }

        srs_utime_t now = srs_get_system_time();
        srs_utime_t reg_duration = now - _register_time;
        srs_utime_t alive_duration = now - _alive_time;
        srs_utime_t invite_duration = now - _invite_time;
        SrsGb28181Config *config = servcie->get_config();

        
        if (_register_status == SrsGb28181SipSessionRegisterOk &&
            reg_duration > _reg_expires){
            srs_trace("gb28181: sip session=%s register expire", _session_id.c_str());
            break;
        }

        if (_register_status == SrsGb28181SipSessionRegisterOk &&
            _alive_status == SrsGb28181SipSessionAliveOk &&
            alive_duration > config->sip_keepalive_timeout){
            srs_trace("gb28181: sip session=%s keepalive timeout", _session_id.c_str());
            break;
        }

        if (_invite_status == SrsGb28181SipSessionTrying &&
            invite_duration > config->sip_ack_timeout){
                _invite_status == SrsGb28181SipSessionUnkonw;
            }

        if (pprint->can_print()){
            srs_trace("gb28181: sip session=%s peer(%s, %d) status(%s,%s,%s) duration(%u,%u,%u)",
                _session_id.c_str(), _peer_ip.c_str(), _peer_port, 
                srs_get_sip_session_status_str(_register_status).c_str(),
                srs_get_sip_session_status_str(_alive_status).c_str(),
                srs_get_sip_session_status_str(_invite_status).c_str(),
                (reg_duration / SRS_UTIME_SECONDS), 
                (alive_duration / SRS_UTIME_SECONDS),
                (invite_duration / SRS_UTIME_SECONDS));
            
            //It is possible that the camera head keeps pushing and opening, 
            //and the duration will be very large. It will take 1 day to update
            if (invite_duration > 24 * SRS_UTIME_HOURS){
                _invite_time = srs_get_system_time();
            }
        }
        srs_usleep(5* SRS_UTIME_SECONDS);
    }
    
    return err;
}

std::string SrsGb28181SipSession::remote_ip()
{
    return _peer_ip;
}

srs_error_t SrsGb28181SipSession::cycle()
{
    srs_error_t err = do_cycle();
    
    servcie->remove_session(_session_id);
    srs_trace("gb28181: client id=%s sip session is remove", _session_id.c_str());
    
    if (err == srs_success) {
        srs_trace("gb28181: sip client finished.");
    } else if (srs_is_client_gracefully_close(err)) {
        srs_warn("gb28181: sip client disconnect code=%d", srs_error_code(err));
        srs_freep(err);
    }
   
    return err;
}

//gb28181 sip Service
SrsGb28181SipService::SrsGb28181SipService(SrsConfDirective* c)
{
    // TODO: FIXME: support reload.
    config = new SrsGb28181Config(c);
    sip = new SrsSipStack();

    if (_srs_gb28181){
        _srs_gb28181->set_sip_service(this);
    }
}

SrsGb28181SipService::~SrsGb28181SipService()
{
    destroy();
    srs_freep(sip);
    srs_freep(config);
}

SrsGb28181Config* SrsGb28181SipService::get_config()
{
    return config;
}

void SrsGb28181SipService::set_stfd(srs_netfd_t fd)
{
    lfd = fd;
}

srs_error_t SrsGb28181SipService::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    char address_string[64];
    char port_string[16];
    if(getnameinfo(from, fromlen, 
                   (char*)&address_string, sizeof(address_string),
                   (char*)&port_string, sizeof(port_string),
                   NI_NUMERICHOST|NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);
    
    srs_error_t err = on_udp_sip(peer_ip, peer_port, buf, nb_buf, (sockaddr*)from, fromlen);
    if (err != srs_success) {
        return srs_error_wrap(err, "process udp");
    }
    return err;
}

srs_error_t SrsGb28181SipService::on_udp_sip(string peer_ip, int peer_port, 
        char* buf, int nb_buf, sockaddr* from, const int fromlen)
{
    srs_error_t err = srs_success;

    srs_info("gb28181: request peer(%s, %d) nbbuf=%d", peer_ip.c_str(), peer_port, nb_buf);
    srs_info("gb28181: request recv message=%s", buf);
    
    if (nb_buf < 10) {
        return err;
    }

    SrsSipRequest* req = NULL;

    if ((err = sip->parse_request(&req, buf, nb_buf)) != srs_success) {
        return srs_error_wrap(err, "parse sip request");
    }
 
    req->peer_ip = peer_ip;
    req->peer_port = peer_port;
    SrsAutoFree(SrsSipRequest, req);

    std::string session_id = req->sip_auth_id;

    if (req->is_register()) {
        std::vector<std::string> serial =  srs_string_split(srs_string_replace(req->uri,"sip:", ""), "@");
        if (serial.at(0) != config->sip_serial){
            srs_warn("gb28181: client:%s request serial and server serial inconformity(%s:%s)",
             req->sip_auth_id.c_str(), serial.at(0).c_str(), config->sip_serial.c_str());
            return  err;
        }

        srs_trace("gb28181: request client id=%s peer(%s, %d)", req->sip_auth_id.c_str(), peer_ip.c_str(), peer_port);
        srs_trace("gb28181: request %s method=%s, uri=%s, version=%s  expires=%d", 
            req->get_cmdtype_str().c_str(), req->method.c_str(),
            req->uri.c_str(), req->version.c_str(), req->expires);

        SrsGb28181SipSession* sip_session = NULL;
        if ((err = fetch_or_create_sip_session(req, &sip_session)) != srs_success) {
            srs_error_wrap(err, "create sip session error!");
            return err;
        }
        srs_assert(sip_session);
        
        send_status(req, from, fromlen);
        sip_session->set_register_status(SrsGb28181SipSessionRegisterOk);
        sip_session->set_register_time(srs_get_system_time());
        sip_session->set_reg_expires(req->expires);
        sip_session->set_sockaddr((sockaddr)*from);
        sip_session->set_sockaddr_len(fromlen);
        sip_session->set_peer_ip(peer_ip);
        sip_session->set_peer_port(peer_port);
    }else if (req->is_message()) {
        SrsGb28181SipSession* sip_session = fetch(session_id);
        if (!sip_session || sip_session->register_status() == SrsGb28181SipSessionUnkonw){
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return err;
        }
       
        //reponse status 
        send_status(req, from, fromlen);
        //sip_session->set_register_status(SrsGb28181SipSessionRegisterOk);
        //sip_session->set_register_time(srs_get_system_time());
        sip_session->set_alive_status(SrsGb28181SipSessionAliveOk);
        sip_session->set_alive_time(srs_get_system_time());
        sip_session->set_sockaddr((sockaddr)*from);
        sip_session->set_sockaddr_len(fromlen);
        sip_session->set_peer_port(peer_port);
        sip_session->set_peer_ip(peer_ip);

        //send invite, play client av
        //start ps rtp listen, recv ps stream
        if (config->sip_auto_play && sip_session->register_status() == SrsGb28181SipSessionRegisterOk &&
            sip_session->alive_status() == SrsGb28181SipSessionAliveOk &&
            sip_session->invite_status() == SrsGb28181SipSessionUnkonw)
        {
            srs_trace("gb28181: request client id=%s, peer(%s, %d)", req->sip_auth_id.c_str(), 
                        peer_ip.c_str(), peer_port);
            srs_trace("gb28181: request %s method=%s, uri=%s, version=%s ", req->get_cmdtype_str().c_str(), 
                        req->method.c_str(), req->uri.c_str(), req->version.c_str());

            SrsGb28181StreamChannel ch;
            ch.set_channel_id(session_id);
            ch.set_ip(config->host);
            if (config->sip_invite_port_fixed){
               ch.set_port_mode(RTP_PORT_MODE_FIXED);
            }else {
               ch.set_port_mode(RTP_PORT_MODE_RANDOM);
            }

            int code = _srs_gb28181->create_stream_channel(&ch);
            if (code == ERROR_SUCCESS){
                code = send_invite(req, ch.get_ip(),
                        ch.get_rtp_port(), ch.get_ssrc());
            }

            if (code == ERROR_SUCCESS){
                sip_session->set_invite_status(SrsGb28181SipSessionTrying);
                sip_session->set_invite_time(srs_get_system_time());
            }
          
        }
    }else if (req->is_invite()) {
        SrsGb28181SipSession* sip_session = fetch(session_id);

        srs_trace("gb28181: request client id=%s, peer(%s, %d)", req->sip_auth_id.c_str(), 
                        peer_ip.c_str(), peer_port);
        srs_trace("gb28181: request %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
       
        if (!sip_session){
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return err;
        }
        
        sip_session->set_sockaddr((sockaddr)*from);
        sip_session->set_sockaddr_len(fromlen);

        if (sip_session->register_status() == SrsGb28181SipSessionUnkonw ||
            sip_session->alive_status() == SrsGb28181SipSessionUnkonw) {
            srs_trace("gb28181: %s client not registered or not alive", req->sip_auth_id.c_str());
            return err;
        }
        
        if (req->cmdtype == SrsSipCmdRespone && req->status == "200") {
            srs_trace("gb28181: INVITE response %s client status=%s", req->sip_auth_id.c_str(), req->status.c_str());
            send_ack(req, from, fromlen);
            sip_session->set_invite_status(SrsGb28181SipSessionInviteOk);
            sip_session->set_invite_time(srs_get_system_time());
            //Record tag and branch, which are required by the 'bye' command,
            sip_session->set_request(req);
        }else{
            sip_session->set_invite_status(SrsGb28181SipSessionUnkonw);
            sip_session->set_invite_time(srs_get_system_time());
        }
    }else if (req->is_bye()) {
        srs_trace("gb28181: request client id=%s, peer(%s, %d)", req->sip_auth_id.c_str(), 
                        peer_ip.c_str(), peer_port);
        srs_trace("gb28181: request %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
      
        SrsGb28181SipSession* sip_session = fetch(session_id);
        send_status(req, from, fromlen);

        if (!sip_session){
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return err;
        }

        sip_session->set_sockaddr((sockaddr)*from);
        sip_session->set_sockaddr_len(fromlen);
       
        sip_session->set_invite_status(SrsGb28181SipSessionBye);
        sip_session->set_invite_time(srs_get_system_time());
   
    }else{
        srs_trace("gb28181: ingor request method=%s", req->method.c_str());
    }
  
    return err;
}

int  SrsGb28181SipService::send_message(sockaddr* from, int fromlen, std::stringstream& ss)
{
    std::string str = ss.str();
    srs_info("gb28181: send_message:%s", str.c_str());
    srs_assert(!str.empty());

    int ret = srs_sendto(lfd, (char*)str.c_str(), (int)str.length(), from, fromlen, SRS_UTIME_NO_TIMEOUT);
    if (ret <= 0){
        srs_trace("gb28181: send_message falid (%d)", ret);
    }
    
    return ret;
}


int SrsGb28181SipService::send_ack(SrsSipRequest *req, sockaddr *f, int l)
{
    srs_assert(req);

    std::stringstream ss;
    
    req->host =  config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    sip->req_ack(ss, req);
    return send_message(f, l, ss);
}

int SrsGb28181SipService::send_status(SrsSipRequest *req,  sockaddr *f, int l)
{
    srs_assert(req);

    std::stringstream ss;
    
    req->host =  config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    sip->resp_status(ss, req);
    return send_message(f, l, ss);
}


int  SrsGb28181SipService::send_invite(SrsSipRequest *req,  string ip, int port, uint32_t ssrc)
{
    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return ERROR_GB28181_SESSION_IS_NOTEXIST;
    }
    
    //if you are inviting or succeed in invite, 
    //you cannot invite again. you need to 'bye' and try again
    if (sip_session->invite_status() == SrsGb28181SipSessionTrying ||
        sip_session->invite_status() == SrsGb28181SipSessionInviteOk){
        return ERROR_GB28181_SIP_IS_INVITING;   
    }
   
    req->host =  config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    std::stringstream ss;
    sip->req_invite(ss, req, ip, port, ssrc);

    sockaddr addr = sip_session->sockaddr_from();

    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return ERROR_GB28181_SIP_INVITE_FAILED;
    }

    sip_session->set_invite_status(SrsGb28181SipSessionTrying);

    return ERROR_SUCCESS;

}

int SrsGb28181SipService::send_bye(SrsSipRequest *req)
{
    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return ERROR_GB28181_SESSION_IS_NOTEXIST;
    }

    //prame branch, from_tag, to_tag, call_id, 
    //The parameter of 'bye' must be the same as 'invite'
    SrsSipRequest r = sip_session->request();
    req->copy(&r);

    req->host =  config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;

    //get protocol stack 
    std::stringstream ss;
    sip->req_bye(ss, req);
   
    sockaddr addr = sip_session->sockaddr_from();
    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return ERROR_GB28181_SIP_BYE_FAILED;
    }

    return ERROR_SUCCESS;
}

int SrsGb28181SipService::send_sip_raw_data(SrsSipRequest *req,  std::string data)
{
    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return ERROR_GB28181_SESSION_IS_NOTEXIST;
    }
    
    std::stringstream ss;
    ss << data;

    sockaddr addr = sip_session->sockaddr_from();
    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return ERROR_GB28181_SIP_BYE_FAILED;
    }

    return ERROR_SUCCESS;
}

srs_error_t SrsGb28181SipService::fetch_or_create_sip_session(SrsSipRequest *req,  SrsGb28181SipSession** sip_session)
{
    srs_error_t err = srs_success;

    SrsGb28181SipSession* sess = NULL;
    if ((sess = fetch(req->sip_auth_id)) != NULL) {
        *sip_session = sess;
        return err;
    }
    
    sess = new SrsGb28181SipSession(this, req);;
    if ((err = sess->serve()) != srs_success) {
        return srs_error_wrap(err, "gb28181: sip serssion serve %s", req->sip_auth_id.c_str());
    }
    
    sessions[req->sip_auth_id] = sess;
    *sip_session = sess;
    
    return err;
}

SrsGb28181SipSession* SrsGb28181SipService::fetch(std::string sid)
{
    std::map<std::string, SrsGb28181SipSession*>::iterator it = sessions.find(sid);
    if (it == sessions.end()){
        return NULL;
    }else{
        return it->second;
    }
}

void SrsGb28181SipService::remove_session(std::string sid)
{
    std::map<std::string, SrsGb28181SipSession*>::iterator it = sessions.find(sid);
    if (it != sessions.end()){
        //srs_freep(it->second);
        //thread exit management by gb28181 manger
        _srs_gb28181->remove_sip_session(it->second);
        sessions.erase(it);
    }
}


void SrsGb28181SipService::destroy()
{
    //destory all sip session
    std::map<std::string, SrsGb28181SipSession*>::iterator it;
    for (it = sessions.begin(); it != sessions.end(); ++it) {
        //srs_freep(it->second);
        //thread exit management by gb28181 manger
        _srs_gb28181->remove_sip_session(it->second);
    }
    sessions.clear();
}


