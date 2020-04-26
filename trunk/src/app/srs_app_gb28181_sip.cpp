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

SrsGb28181Device::SrsGb28181Device()
{
    device_id = ""; 
    invite_status = SrsGb28181SipSessionUnkonw;
    invite_time = 0;
    device_status = "";
    
}

SrsGb28181Device::~SrsGb28181Device()
{}

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
    _query_catalog_time = 0;
  
    _peer_ip = "";
    _peer_port = 0;

    _fromlen = 0;
    _sip_cseq = 100;
}

SrsGb28181SipSession::~SrsGb28181SipSession()
{
    destroy();

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

void SrsGb28181SipSession::destroy()
{
    //destory all device
    std::map<std::string, SrsGb28181Device*>::iterator it;
    for (it = _device_list.begin(); it != _device_list.end(); ++it) {
        srs_freep(it->second);
    }

    _device_list.clear();
}

srs_error_t SrsGb28181SipSession::do_cycle()
{
    srs_error_t err = srs_success;
    _register_time = srs_get_system_time();
    _alive_time = srs_get_system_time();
    _invite_time = srs_get_system_time();
    //call it immediately after alive ok;
    _query_catalog_time = 0;
  
    while (true) {

        pprint->elapse();

        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "gb28181 sip session cycle");
        }
        
        SrsGb28181Config *config = servcie->get_config();
        srs_utime_t now = srs_get_system_time();
        srs_utime_t reg_duration = now - _register_time;
        srs_utime_t alive_duration = now - _alive_time;
        srs_utime_t query_duration = now - _query_catalog_time;

        //send invite, play client av
        //start ps rtp listen, recv ps stream
        if (_register_status == SrsGb28181SipSessionRegisterOk &&
            _alive_status == SrsGb28181SipSessionAliveOk)
        {
            std::map<std::string, SrsGb28181Device*>::iterator it;
            for (it = _device_list.begin(); it != _device_list.end(); it++) {
                SrsGb28181Device *device = it->second;
                std::string chid = it->first;

                //update device invite time
                srs_utime_t invite_duration = 0;
                if (device->invite_time != 0){
                    invite_duration = srs_get_system_time() - device->invite_time;
                }

                //It is possible that the camera head keeps pushing and opening, 
                //and the duration will be very large. It will take 1 day to update
                if (invite_duration > 24 * SRS_UTIME_HOURS){
                    device->invite_time = srs_get_system_time();
                }

                if (device->invite_status == SrsGb28181SipSessionTrying &&
                    invite_duration > config->sip_ack_timeout){
                    device->invite_status = SrsGb28181SipSessionUnkonw;
                }

                if (!config->sip_auto_play) continue;
                
                //offline or already invite device does not need to send invite
                if (device->device_status != "ON" || 
                    device->invite_status != SrsGb28181SipSessionUnkonw) continue;

                SrsGb28181StreamChannel ch;
               
                ch.set_channel_id(_session_id + "@" + chid);
                ch.set_ip(config->host);

                if (config->sip_invite_port_fixed){
                    ch.set_port_mode(RTP_PORT_MODE_FIXED);
                }else {
                    ch.set_port_mode(RTP_PORT_MODE_RANDOM);
                }

                //create stream channel, ready for recv device av stream
                srs_error_t err = _srs_gb28181->create_stream_channel(&ch);

                if ((err = _srs_gb28181->create_stream_channel(&ch)) == srs_success){
                    SrsSipRequest req;
                    req.sip_auth_id = _session_id;
                   
                    //send invite to device, req push av stream
                    err = servcie->send_invite(&req, ch.get_ip(),
                                ch.get_rtp_port(), ch.get_ssrc(), chid);
                }

                int code = srs_error_code(err);
                if (err != srs_success){
                    srs_error_reset(err);
                }
                                       
                //the same device can't be sent too fast. the device can't handle it
                srs_usleep(1*SRS_UTIME_SECONDS);
               
                srs_trace("gb28181: %s clients device=%s send invite code=%d", 
                    _session_id.c_str(), chid.c_str(), code);
            }//end for (it)
        }//end if (config)

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

        //query device channel
        if (_alive_status == SrsGb28181SipSessionAliveOk &&
            query_duration >= config->sip_query_catalog_interval) {
            SrsSipRequest req;
            req.sip_auth_id = _session_id;
            _query_catalog_time = srs_get_system_time();

            srs_error_t err = servcie->send_query_catalog(&req);
            if (err != srs_success){
                srs_trace("gb28181: sip query catalog error %s",srs_error_desc(err).c_str());
                srs_error_reset(err);
            }

            //print device status
            srs_trace("gb28181: sip session=%s peer(%s, %d) status(%s,%s) duration(%u,%u)",
                _session_id.c_str(), _peer_ip.c_str(), _peer_port, 
                srs_get_sip_session_status_str(_register_status).c_str(),
                srs_get_sip_session_status_str(_alive_status).c_str(),
                (reg_duration / SRS_UTIME_SECONDS), 
                (alive_duration / SRS_UTIME_SECONDS));
     
            std::map<std::string, SrsGb28181Device*>::iterator it;
            for (it = _device_list.begin(); it != _device_list.end(); it++) {
                SrsGb28181Device *device = it->second;
                std::string chid = it->first;
            
                srs_utime_t invite_duration = srs_get_system_time() - device->invite_time;

                if (device->invite_status != SrsGb28181SipSessionTrying &&
                    device->invite_status != SrsGb28181SipSessionInviteOk){
                        invite_duration = 0;
                }

                srs_trace("gb28181: sip session=%s device=%s status(%s, %s), duration(%u)",
                    _session_id.c_str(), chid.c_str(), device->device_status.c_str(), 
                    srs_get_sip_session_status_str(device->invite_status).c_str(),
                    (invite_duration / SRS_UTIME_SECONDS));
            }
        }

        srs_usleep(1 * SRS_UTIME_SECONDS);
    }//end while
    
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

void SrsGb28181SipSession::update_device_list(std::map<std::string, std::string> lst)
{
    std::map<std::string, std::string>::iterator it;
    for (it = lst.begin(); it != lst.end(); ++it) {
        std::string id = it->first;
        std::string status = it->second;

        if  (_device_list.find(id) == _device_list.end()){
            SrsGb28181Device *device = new SrsGb28181Device();
            device->device_id = id;
            device->device_status = status;
            device->invite_status = SrsGb28181SipSessionUnkonw;
            device->invite_time = 0;
            _device_list[id] = device;

        }else {
            SrsGb28181Device *device = _device_list[id];
            device->device_status = status;
        }

        // srs_trace("gb28181: sip session %s, deviceid=%s status=(%s,%s)", 
        //     _session_id.c_str(), id.c_str(), status.c_str(),  
        //     srs_get_sip_session_status_str(device.invite_status).c_str());
    }
}

SrsGb28181Device* SrsGb28181SipSession::get_device_info(std::string chid)
{
    if (_device_list.find(chid) != _device_list.end()){
        return _device_list[chid];
    }
    return NULL;
}

void SrsGb28181SipSession::dumps(SrsJsonObject* obj)
{
    obj->set("id", SrsJsonAny::str(_session_id.c_str()));
    obj->set("device_sumnum", SrsJsonAny::integer(_device_list.size()));
    
    SrsJsonArray* arr = SrsJsonAny::array();
    obj->set("devices", arr);
    std::map<std::string, SrsGb28181Device*>::iterator it;
    for (it = _device_list.begin(); it != _device_list.end(); ++it) {
        SrsGb28181Device *device = it->second;
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        obj->set("device_id", SrsJsonAny::str(device->device_id.c_str()));
        obj->set("device_status", SrsJsonAny::str(device->device_status.c_str()));
        obj->set("invite_status", SrsJsonAny::str(srs_get_sip_session_status_str(device->invite_status).c_str()));
        obj->set("invite_time", SrsJsonAny::integer(device->invite_time/SRS_UTIME_SECONDS));
    }

    //obj->set("rtmp_port", SrsJsonAny::integer(rtmp_port));
    // obj->set("app", SrsJsonAny::str(app.c_str()));
    // obj->set("stream", SrsJsonAny::str(stream.c_str()));
    // obj->set("rtmp_url", SrsJsonAny::str(rtmp_url.c_str()));
   
    // obj->set("ssrc", SrsJsonAny::integer(ssrc));
    // obj->set("rtp_port", SrsJsonAny::integer(rtp_port));
    // obj->set("port_mode", SrsJsonAny::str(port_mode.c_str()));
    // obj->set("rtp_peer_port", SrsJsonAny::integer(rtp_peer_port));
    // obj->set("rtp_peer_ip", SrsJsonAny::str(rtp_peer_ip.c_str()));
    // obj->set("recv_time", SrsJsonAny::integer(recv_time/SRS_UTIME_SECONDS));
    // obj->set("recv_time_str", SrsJsonAny::str(recv_time_str.c_str()));
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

    std::string recv_msg(buf, nb_buf);
    srs_error_t err = on_udp_sip(peer_ip, peer_port, recv_msg, (sockaddr*)from, fromlen);
    if (err != srs_success) {
        return srs_error_wrap(err, "process udp");
    }
    return err;
}

srs_error_t SrsGb28181SipService::on_udp_sip(string peer_ip, int peer_port, 
        std::string recv_msg, sockaddr* from, const int fromlen)
{
    srs_error_t err = srs_success;

    int recv_len = recv_msg.size();
    char* recv_data = (char*)recv_msg.c_str();

    srs_info("gb28181: request peer(%s, %d) nbbuf=%d", peer_ip.c_str(), peer_port, recv_len);
    srs_info("gb28181: request recv message=%s", recv_data);
    
    if (recv_len < 10) {
        return err;
    }

    SrsSipRequest* req = NULL;

    if ((err = sip->parse_request(&req, recv_data, recv_len)) != srs_success) {
        return srs_error_wrap(err, "parse sip request");
    }
 
    req->peer_ip = peer_ip;
    req->peer_port = peer_port;
    SrsAutoFree(SrsSipRequest, req);

    std::string session_id = req->sip_auth_id;

    if (req->is_register()) {
        std::vector<std::string> serial =  srs_string_split(srs_string_replace(req->uri,"sip:", ""), "@");
        if (serial.empty()){
            return srs_error_new(ERROR_GB28181_SIP_PRASE_FAILED, "register string split");
        }

        if (serial.at(0) != config->sip_serial){
            srs_warn("gb28181: client:%s request serial and server serial inconformity(%s:%s)",
             req->sip_auth_id.c_str(), serial.at(0).c_str(), config->sip_serial.c_str());
            return  err;
        }

        srs_trace("gb28181: request client id=%s peer(%s, %d)", req->sip_auth_id.c_str(), peer_ip.c_str(), peer_port);
        srs_trace("gb28181: %s method=%s, uri=%s, version=%s expires=%d", 
            req->get_cmdtype_str().c_str(), req->method.c_str(),
            req->uri.c_str(), req->version.c_str(), req->expires);

        SrsGb28181SipSession* sip_session = NULL;
        if ((err = fetch_or_create_sip_session(req, &sip_session)) != srs_success) {
            srs_error_wrap(err, "create sip session error!");
            return err;
        }
        srs_assert(sip_session);
        sip_session->set_request(req);

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
         
        if (!sip_session){
            sip_session = fetch_session_by_callid(req->call_id);
        }
     
        if (!sip_session || sip_session->register_status() == SrsGb28181SipSessionUnkonw){
            srs_trace("gb28181: %s client not registered", req->sip_auth_id.c_str());
            return err;
        }
       
        //reponse status 
        if (req->cmdtype == SrsSipCmdRequest){
            send_status(req, from, fromlen);
            sip_session->set_alive_status(SrsGb28181SipSessionAliveOk);
            sip_session->set_alive_time(srs_get_system_time());
            sip_session->set_sockaddr((sockaddr)*from);
            sip_session->set_sockaddr_len(fromlen);
            sip_session->set_peer_port(peer_port);
            sip_session->set_peer_ip(peer_ip);
            
            //update device list
            if (req->device_list_map.size() > 0){
                sip_session->update_device_list(req->device_list_map);
            }
        }
       
    }else if (req->is_invite()) {
        SrsGb28181SipSession* sip_session = fetch_session_by_callid(req->call_id);

        srs_trace("gb28181: request client id=%s, peer(%s, %d)", req->sip_auth_id.c_str(), 
                        peer_ip.c_str(), peer_port);
        srs_trace("gb28181: %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
       
        if (!sip_session){
            srs_trace("gb28181: call_id %s not map %s client ", req->call_id.c_str(), req->sip_auth_id.c_str());
            return err;
        }
        
        // sip_session->set_sockaddr((sockaddr)*from);
        // sip_session->set_sockaddr_len(fromlen);

        if (sip_session->register_status() == SrsGb28181SipSessionUnkonw ||
            sip_session->alive_status() == SrsGb28181SipSessionUnkonw) {
            srs_trace("gb28181: %s client not registered or not alive", req->sip_auth_id.c_str());
            return err;
        }
        
        if (req->cmdtype == SrsSipCmdRespone){
            srs_trace("gb28181: INVITE response %s client status=%s", req->sip_auth_id.c_str(), req->status.c_str());

            if (req->status == "200") {
                send_ack(req, from, fromlen);
                SrsGb28181Device *device = sip_session->get_device_info(req->sip_auth_id);
                if (device){
                    device->invite_status = SrsGb28181SipSessionInviteOk;
                    device->req_inivate.copy(req);
                    device->invite_time = srs_get_system_time();
                }
            }else if (req->status == "100") {
                //send_ack(req, from, fromlen);
                SrsGb28181Device *device = sip_session->get_device_info(req->sip_auth_id);
                if (device){
                    device->req_inivate.copy(req);
                    device->invite_status = SrsGb28181SipSessionTrying;
                    device->invite_time = srs_get_system_time();
                }
            }else{
                SrsGb28181Device *device = sip_session->get_device_info(req->sip_auth_id);
                if (device){
                    device->req_inivate.copy(req);
                    device->invite_status = SrsGb28181SipSessionUnkonw;
                    device->invite_time = srs_get_system_time();
                }
            }
        }
       
    }else if (req->is_bye()) {
        srs_trace("gb28181: request client id=%s, peer(%s, %d)", req->sip_auth_id.c_str(), 
                        peer_ip.c_str(), peer_port);
        srs_trace("gb28181: %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
      
        send_status(req, from, fromlen);

        SrsGb28181SipSession* sip_session = fetch_session_by_callid(req->call_id);
        srs_trace("gb28181: request client id=%s, peer(%s, %d)", req->sip_auth_id.c_str(), 
                        peer_ip.c_str(), peer_port);
        srs_trace("gb28181: %s method=%s, uri=%s, version=%s ", 
            req->get_cmdtype_str().c_str(), req->method.c_str(), req->uri.c_str(), req->version.c_str());
       
        if (!sip_session){
            srs_trace("gb28181: call_id %s not map %s client ", req->call_id.c_str(), req->sip_auth_id.c_str());
            return err;
        }

        if (req->cmdtype == SrsSipCmdRespone){
            srs_trace("gb28181: BYE  %s client status=%s", req->sip_auth_id.c_str(), req->status.c_str());

            if (req->status == "200") {
                SrsGb28181Device *device = sip_session->get_device_info(req->sip_auth_id);
                if (device){
                    device->invite_status = SrsGb28181SipSessionBye;
                    device->invite_time = srs_get_system_time();
                }
            }else {
                //TODO:fixme
                SrsGb28181Device *device = sip_session->get_device_info(req->sip_auth_id);
                if (device){
                    device->invite_status = SrsGb28181SipSessionBye;
                    device->invite_time = srs_get_system_time();
                }
            }
        }
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
    req->chid = req->sip_auth_id;

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


srs_error_t  SrsGb28181SipService::send_invite(SrsSipRequest *req,  string ip, int port, uint32_t ssrc, std::string chid)
{
    srs_error_t err = srs_success;

    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "sip session not exist");
    }
    
    //if you are inviting or succeed in invite, 
    //you cannot invite again. you need to 'bye' and try again
    SrsGb28181Device *device = sip_session->get_device_info(chid);
    if (!device || device->device_status != "ON"){
        return srs_error_new(ERROR_GB28181_SIP_CH_OFFLINE, "sip device channel offline");
    }

    if (device->invite_status  == SrsGb28181SipSessionTrying ||
        device->invite_status  == SrsGb28181SipSessionInviteOk){
        return srs_error_new(ERROR_GB28181_SIP_IS_INVITING, "sip device channel inviting");   
    }

    req->host =  config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;
    req->chid = chid;
    req->seq = sip_session->sip_cseq();

    SrsSipRequest register_req = sip_session->request();
    req->to_realm = register_req.to_realm;
    req->from_realm = config->sip_realm;
   
    std::stringstream ss;
    sip->req_invite(ss, req, ip, port, ssrc);
   
    sockaddr addr = sip_session->sockaddr_from();

    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return srs_error_new(ERROR_GB28181_SIP_INVITE_FAILED, "sip device invite failed");
    }

    //prame branch, from_tag, to_tag, call_id, 
    //The parameter of 'bye' must be the same as 'invite'
    device->req_inivate.copy(req);
    device->invite_time = srs_get_system_time();
    device->invite_status = SrsGb28181SipSessionTrying;

    //call_id map sip_session
    sip_session_map_by_callid(sip_session, req->call_id);

    return err;
}

srs_error_t SrsGb28181SipService::send_bye(SrsSipRequest *req, std::string chid)
{
    srs_error_t err = srs_success;

    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "sip session not exist");
    }

    SrsGb28181Device *device = sip_session->get_device_info(chid);
    if (!device){
        return srs_error_new(ERROR_GB28181_SIP_CH_NOTEXIST, "sip device channel not exist");
    }
   
    //prame branch, from_tag, to_tag, call_id, 
    //The parameter of 'bye' must be the same as 'invite'
    //SrsSipRequest r = sip_session->request();

    req->copy(&device->req_inivate);

    req->host = config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;
    req->chid = chid;
    req->seq = sip_session->sip_cseq();
    
    SrsSipRequest register_req = sip_session->request();
    req->to_realm = register_req.to_realm;
    req->from_realm = config->sip_realm;
   
    //get protocol stack 
    std::stringstream ss;
    sip->req_bye(ss, req);
   
    sockaddr addr = sip_session->sockaddr_from();
    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return srs_error_new(ERROR_GB28181_SIP_BYE_FAILED, "sip bye failed");
    }

    return err;
}

srs_error_t SrsGb28181SipService::send_sip_raw_data(SrsSipRequest *req,  std::string data)
{
    srs_error_t err = srs_success;

    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "sip session no exist");
    }
    
    std::stringstream ss;
    ss << data;

    sockaddr addr = sip_session->sockaddr_from();
    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return srs_error_new(ERROR_GB28181_SIP_RAW_DATA_FAILED, "sip raw data failed");
    }

    return err;
}

srs_error_t SrsGb28181SipService::send_query_catalog(SrsSipRequest *req)
{
    srs_error_t err = srs_success;

    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "sip session not exist");
    }

    req->host = config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;
    req->chid = req->sip_auth_id;
    req->seq = sip_session->sip_cseq();

    //get protocol stack 
    std::stringstream ss;
    sip->req_query_catalog(ss, req);

    return send_sip_raw_data(req, ss.str());
}

srs_error_t SrsGb28181SipService::send_ptz(SrsSipRequest *req, std::string chid, std::string cmd, 
            uint8_t speed, int priority)
{
    srs_error_t err = srs_success;

    srs_assert(req);

    SrsGb28181SipSession *sip_session = fetch(req->sip_auth_id);

    if (!sip_session){
        return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "sip session not exist");
    }

    SrsGb28181Device *device = sip_session->get_device_info(chid);
    if (!device){
        return srs_error_new(ERROR_GB28181_SIP_CH_NOTEXIST, "sip device channel not exist");
    }

    if (device->invite_status  != SrsGb28181SipSessionInviteOk){
        return srs_error_new(ERROR_GB28181_SIP_NOT_INVITE, "sip device channel not inviting");   
    }
   
    //prame branch, from_tag, to_tag, call_id, 
    //The parameter of 'bye' must be the same as 'invite'
    //SrsSipRequest r = sip_session->request();
    req->copy(&device->req_inivate);

    req->host = config->host;
    req->host_port = config->sip_port;
    req->realm = config->sip_realm;
    req->serial = config->sip_serial;
    req->chid = chid;
    req->seq = sip_session->sip_cseq();
    
    SrsSipPtzCmdType ptzcmd = SrsSipPtzCmdRight;
    const char *ss_cmd = cmd.c_str();
    if (!strcasecmp(ss_cmd, "stop")){
        ptzcmd = SrsSipPtzCmdStop;
    }else if (!strcasecmp(ss_cmd, "right")){
        ptzcmd = SrsSipPtzCmdRight;
    }else if (!strcasecmp(ss_cmd, "left")){
        ptzcmd = SrsSipPtzCmdLeft;
    }else if (!strcasecmp(ss_cmd, "down")){
        ptzcmd = SrsSipPtzCmdDown;
    }else if (!strcasecmp(ss_cmd, "up")){
        ptzcmd = SrsSipPtzCmdUp;
    }else if (!strcasecmp(ss_cmd, "zoomout")){
        ptzcmd = SrsSipPtzCmdZoomOut;
    }else if (!strcasecmp(ss_cmd, "zoomin")){    
        ptzcmd = SrsSipPtzCmdZoomIn;
    }else{
        return srs_error_new(ERROR_GB28181_SIP_PTZ_CMD_INVALID, "sip ptz cmd no support");  
    }

    if (speed < 0 || speed > 0xFF){
        return srs_error_new(ERROR_GB28181_SIP_PTZ_CMD_INVALID, "sip ptz cmd speed out of range");  
    }

    if (priority <= 0 ){
        priority = 5;
    }

    //get protocol stack 
    std::stringstream ss;
    sip->req_ptz(ss, req, ptzcmd, speed, priority);
   
    sockaddr addr = sip_session->sockaddr_from();
    if (send_message(&addr, sip_session->sockaddr_fromlen(), ss) <= 0)
    {
        return srs_error_new(ERROR_GB28181_SIP_PTZ_FAILED, "sip ptz failed");
    }

    //call_id map sip_session
    sip_session_map_by_callid(sip_session, req->call_id);

    return err;

}

srs_error_t SrsGb28181SipService::query_sip_session(std::string sid,  SrsJsonArray* arr)
{
    srs_error_t err = srs_success;

    if (!sid.empty()){
        SrsGb28181SipSession* sess = fetch(sid);
        if (!sess){
            return srs_error_new(ERROR_GB28181_SESSION_IS_NOTEXIST, "sip session not exist");
        }
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        sess->dumps(obj);
    }else {
        std::map<std::string, SrsGb28181SipSession*>::iterator it;
        for (it = sessions.begin(); it != sessions.end(); ++it) {
            SrsGb28181SipSession* sess = it->second;
            SrsJsonObject* obj = SrsJsonAny::object();
            arr->append(obj);
            sess->dumps(obj);
        }
    }

    return err;
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

void SrsGb28181SipService::sip_session_map_by_callid(SrsGb28181SipSession *sess, std::string call_id)
{
    if (sessions_by_callid.find(call_id) == sessions_by_callid.end()) {
        sessions_by_callid[call_id] = sess;
    }
}

void SrsGb28181SipService::sip_session_unmap_by_callid(std::string call_id)
{
    std::map<std::string, SrsGb28181SipSession*>::iterator it = sessions_by_callid.find(call_id);
    if (it != sessions_by_callid.end()) {
        sessions_by_callid.erase(it);
    }
}

SrsGb28181SipSession* SrsGb28181SipService::fetch_session_by_callid(std::string call_id)
{
    SrsGb28181SipSession* session = NULL;
    if (sessions_by_callid.find(call_id) == sessions_by_callid.end()) {
        return NULL;
    }
    
    session = sessions_by_callid[call_id];
    return session;
}


