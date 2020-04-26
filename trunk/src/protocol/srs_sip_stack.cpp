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

#include <srs_sip_stack.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

#include <stdio.h>
#include <stdlib.h>
#include <iostream>  
#include <map>

using namespace std;

#include <srs_protocol_io.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_rtsp_stack.hpp>

unsigned int srs_sip_random(int min,int max)  
{  
    //it is possible to duplicate data with time(0)
    srand(unsigned(srs_update_system_time()));
    return  rand() % (max - min + 1) + min;
} 

std::string srs_sip_generate_branch()
{
   int rand = srs_sip_random(10000000, 99999999);
   std::stringstream branch; 
   branch << "SrsGbB" << rand;
   return branch.str();
}

std::string srs_sip_generate_to_tag()
{
   uint32_t rand = srs_sip_random(10000000, 99999999);
   std::stringstream branch; 
   branch << "SrsGbT" << rand;
   return branch.str();
}

std::string srs_sip_generate_from_tag()
{
   uint32_t rand = srs_sip_random(10000000, 99999999);
   std::stringstream branch; 
   branch << "SrsGbF" << rand;
   return branch.str();
}

std::string srs_sip_generate_call_id()
{
   uint32_t rand = srs_sip_random(10000000, 99999999);
   std::stringstream branch; 
   branch << "2020" << rand;
   return branch.str();
}

std::string srs_sip_generate_sn()
{
   uint32_t rand = srs_sip_random(10000000, 99999999);
   std::stringstream sn; 
   sn << rand;
   return sn.str();
}

std::string  srs_sip_get_form_to_uri(std::string  msg)
{
    //<sip:34020000002000000001@3402000000>;tag=536961166
    //sip:34020000002000000001@3402000000 

    size_t pos = msg.find("<");
    if (pos == string::npos) {
        return msg;
    }

    msg = msg.substr(pos+1);

    size_t pos2 = msg.find(">");
    if (pos2 == string::npos) {
        return msg;
    }

    msg = msg.substr(0, pos2);
    return msg;
}

std::string srs_sip_get_utc_date()
{
    // clock time
    timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return "";
    }
    
    // to calendar time
    struct tm* tm;
    if ((tm = gmtime(&tv.tv_sec)) == NULL) {
        return "";
    }
    
    //Date: 2020-03-21T14:20:57.638
    std::string utc_date = "";
    char buffer[25] = {0};
    snprintf(buffer, 25,
                "%d-%02d-%02dT%02d:%02d:%02d.%03d",
                1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(tv.tv_usec / 1000));
    utc_date = buffer;
    return utc_date;
}


std::string srs_sip_get_param(std::string msg, std::string param)
{
    std::vector<std::string>  vec_params = srs_string_split(msg, ";");

    for (vector<string>::iterator it = vec_params.begin(); it != vec_params.end(); ++it) {
        string  value = *it;
        
        size_t pos = value.find(param);
        if (pos == string::npos) {
            continue;
        }

        std::vector<std::string>  v_pram = srs_string_split(value, "=");
        
        if (v_pram.size() > 1) {
            return v_pram.at(1);
        }
    }
    return "";
}

SrsSipRequest::SrsSipRequest()
{
    seq = 0;
    content_length = 0;
    sdp = NULL;
    transport = NULL;

    method = "";
    uri = "";;
    version = "";;
    seq = 0;
    content_type = "";
    content_length = 0;
    call_id = "";
    from = "";
    to = "";
    via = "";
    from_tag = "";
    to_tag = "";
    contact = "";
    user_agent = "";
    branch = "";
    status = "";
    expires = 3600;
    max_forwards = 70;
    www_authenticate = "";
    authorization = "";
    cmdtype = SrsSipCmdRequest;

    host = "127.0.0.1";;
    host_port = 5060;

    serial = "";;
    realm = "";;

    sip_auth_id = "";
    sip_auth_pwd = "";
    sip_username = "";
    peer_ip = "";
    peer_port = 0;

    chid = "";

    from_realm = "";
    to_realm = "";
}

SrsSipRequest::~SrsSipRequest()
{
    srs_freep(sdp);
    srs_freep(transport);
}

bool SrsSipRequest::is_register()
{
    return method == SRS_SIP_METHOD_REGISTER;
}

bool SrsSipRequest::is_invite()
{
    return method == SRS_SIP_METHOD_INVITE;
}

bool SrsSipRequest::is_ack()
{
    return method == SRS_SIP_METHOD_ACK;
}

bool SrsSipRequest::is_message()
{
    return method == SRS_SIP_METHOD_MESSAGE;
}

bool SrsSipRequest::is_bye()
{
    return method == SRS_SIP_METHOD_BYE;
}

std::string SrsSipRequest::get_cmdtype_str()
{
    switch(cmdtype) {
        case SrsSipCmdRequest:
            return "request";
        case SrsSipCmdRespone:
            return "respone";
    }

    return "";
}

void SrsSipRequest::copy(SrsSipRequest* src)
{
    if (!src){
        return;
    }
    
    method = src->method;
    uri = src->uri;
    version = src->version;
    seq = src->seq;
    content_type = src->content_type;
    content_length = src->content_length;
    call_id = src->call_id;
    from = src->from;
    to = src->to;
    via = src->via;
    from_tag = src->from_tag;
    to_tag = src->to_tag;
    contact = src->contact;
    user_agent = src->user_agent;
    branch = src->branch;
    status = src->status;
    expires = src->expires;
    max_forwards = src->max_forwards;
    www_authenticate = src->www_authenticate;
    authorization = src->authorization;
    cmdtype = src->cmdtype;

    host = src->host;
    host_port = src->host_port;

    serial = src->serial;
    realm = src->realm;
    
    sip_auth_id = src->sip_auth_id;
    sip_auth_pwd = src->sip_auth_pwd;
    sip_username = src->sip_username;
    peer_ip = src->peer_ip;
    peer_port = src->peer_port;

    chid = src->chid;

    xml_body_map = src->xml_body_map;
    device_list_map = src->device_list_map;

    from_realm = src->from_realm;
    to_realm  = src->to_realm;
}

SrsSipStack::SrsSipStack()
{
    buf = new SrsSimpleStream();
}

SrsSipStack::~SrsSipStack()
{
    srs_freep(buf);
}

srs_error_t SrsSipStack::parse_request(SrsSipRequest** preq, const char* recv_msg, int nb_len)
{
    srs_error_t err = srs_success;
    
    SrsSipRequest* req = new SrsSipRequest();
    if ((err = do_parse_request(req, recv_msg)) != srs_success) {
        srs_freep(req);
        return srs_error_wrap(err, "recv message");
    }
    
    *preq = req;
    
    return err;
}

srs_error_t SrsSipStack::parse_xml(std::string xml_msg, std::map<std::string, std::string> &json_map)
{
    /*
    <?xml version="1.0" encoding="gb2312"?>
    <Notify>
    <CmdType>Keepalive</CmdType>
    <SN>2034</SN>
    <DeviceID>34020000001110000001</DeviceID>
    <Status>OK</Status>
    <Info>
    <DeviceID>34020000001320000002</DeviceID>
    <DeviceID>34020000001320000003</DeviceID>
    <DeviceID>34020000001320000005</DeviceID>
    <DeviceID>34020000001320000006</DeviceID>
    <DeviceID>34020000001320000007</DeviceID>
    <DeviceID>34020000001320000008</DeviceID>
    </Info>
    </Notify>
    */
   
    const char* start = xml_msg.c_str();
    const char* end = start + xml_msg.size();
    char* p = (char*)start;
    
    char* value_start = NULL;

    std::string xml_header;
    int xml_layer = 0;

    //std::map<string, string> json_map;
    std::map<int, string> json_key;
    while (p < end) {
        if (p[0] == '\n'){
            p +=1;
            value_start = NULL;
        } else if (p[0] == '\r' && p[1] == '\n') {
            p +=2;
            value_start = NULL;
        } else if (p[0] == '<' && p[1] == '/') { //</Notify> xml item end flag
            std::string value = "";
            if (value_start) {
                value = std::string(value_start, p-value_start);
            }
            
            //skip </
            p += 2;
            
            //</Notify> get Notify
            char *s = p;
            while (p[0] != '>') {p++;}
            std::string key(s, p-s);

            //<DeviceList Num="2"> get DeviceList
            std::vector<string> vec = srs_string_split(key, " ");
            if (vec.empty()){
                return srs_error_new(ERROR_GB28181_SIP_PRASE_FAILED, "prase xml"); 
            }

            key = vec.at(0);

            /*xml element to map
                <Notify>
                    <info>
                        <DeviceID>34020000001320000001</DeviceID>
                        <DeviceID>34020000001320000002</DeviceID>
                    </info>
                </Notify>
            to map is: Notify@Info@DeviceID:34020000001320000001,34020000001320000002
            */
           
            //get map key
            std::string mkey = "";
            for (int i = 0; i < xml_layer ; i++){
                if (mkey.empty()) {
                    mkey = json_key[i];
                }else{
                    mkey =  mkey + "@" + json_key[i];     
                }
            }
 
            //set map value
            if (!mkey.empty()){
                if (json_map.find(mkey) == json_map.end()){
                    json_map[mkey] = value;         
                }else{
                    json_map[mkey] = json_map[mkey] + ","+ value;
                }    
            }
          
            value_start = NULL;
            xml_layer--;

        } else if (p[0] == '<') { //<Notify>  xml item begin flag
            //skip <
            p +=1;

            //<Notify> get Notify
            char *s = p;
            while (p[0] != '>') {p++;}
            std::string key(s, p-s);

            if (srs_string_contains(key, "?xml")){
                //xml header
                xml_header = key;
                json_map["XmlHeader"] = xml_header;
            }else {
                //<DeviceList Num="2"> get DeviceList
                std::vector<string> vec = srs_string_split(key, " ");
                if (vec.empty()){
                    return srs_error_new(ERROR_GB28181_SIP_PRASE_FAILED, "prase xml"); 
                }

                key = vec.at(0);

                //key to map by xml_layer
                //<Notify>
                //  <info>
                //  </info>
                //</Notify>
                //json_key[0] = "Notify"
                //json_key[1] = "info"
                json_key[xml_layer] = key; 
                xml_layer++;  
            }

            p +=1;
            value_start = p;
        } else {
          p++;
        }
    }

    // std::map<string, string>::iterator it2;
    // for (it2 = json_map.begin(); it2 != json_map.end(); ++it2) {
    //     srs_trace("========%s:%s", it2->first.c_str(), it2->second.c_str());
    // }

    return srs_success;
}

srs_error_t SrsSipStack::do_parse_request(SrsSipRequest* req, const char* recv_msg)
{
    srs_error_t err = srs_success;

    std::vector<std::string> header_body = srs_string_split(recv_msg, SRS_RTSP_CRLFCRLF);
    if (header_body.empty()){
        return srs_error_new(ERROR_GB28181_SIP_PRASE_FAILED, "parse reques message"); 
    }

    std::string header = header_body.at(0);
    //Must be added SRS_RTSP_CRLFCRLF in order to handle the last line header
    header += SRS_RTSP_CRLFCRLF; 
    std::string body = "";

    if (header_body.size() > 1){
       body =  header_body.at(1);
    }

    srs_info("sip: header=%s\n", header.c_str());
    srs_info("sip: body=%s\n", body.c_str());

    // parse one by one.
    char* start = (char*)header.c_str();
    char* end = start + header.size();
    char* p = start;
    char* newline_start = start;
    std::string firstline = "";
    while (p < end) {
        if (p[0] == '\r' && p[1] == '\n'){
            p +=2;
            int linelen = (int)(p - newline_start);
            std::string oneline(newline_start, linelen);
            newline_start = p;

            if (firstline == ""){
                firstline = srs_string_replace(oneline, "\r\n", "");
                srs_info("sip: first line=%s", firstline.c_str());
            }else{
                size_t pos = oneline.find(":");
                if (pos != string::npos){
                    if (pos != 0) {
                        //ex: CSeq: 100 MESSAGE  header is 'CSeq:',content is '100 MESSAGE'
                        std::string head = oneline.substr(0, pos+1);
                        std::string content = oneline.substr(pos+1, oneline.length()-pos-1);
                        content = srs_string_replace(content, "\r\n", "");
                        content = srs_string_trim_start(content, " ");
                        char *phead = (char*)head.c_str();
                        
                        if (!strcasecmp(phead, "call-id:")) {
                            std::vector<std::string> vec_callid = srs_string_split(content, " ");
                            req->call_id = vec_callid.empty() ? "" : vec_callid.at(0);
                        } 
                        else if (!strcasecmp(phead, "contact:")) {
                            req->contact = content;
                        } 
                        else if (!strcasecmp(phead, "content-encoding:")) {
                            srs_trace("sip: message head %s content=%s", phead, content.c_str());
                        } 
                        else if (!strcasecmp(phead, "content-length:")) {
                            req->content_length = strtoul(content.c_str(), NULL, 10);
                        } 
                        else if (!strcasecmp(phead, "content-type:")) {
                            req->content_type = content;
                        } 
                        else if (!strcasecmp(phead, "cseq:")) {
                            std::vector<std::string> vec_seq = srs_string_split(content, " ");
                            std::string seq = vec_seq.empty() ? "" : vec_seq.at(0);
                            req->seq =  strtoul(seq.c_str(), NULL, 10);
                            req->method = vec_seq.size() > 0 ? vec_seq.at(1) : "";
                        } 
                        else if (!strcasecmp(phead, "from:")) {
                            content = srs_string_replace(content, "sip:", "");
                            req->from = srs_sip_get_form_to_uri(content.c_str());
                            if (srs_string_contains(content, "tag")) {
                                req->from_tag = srs_sip_get_param(content.c_str(), "tag");
                            }

                            std::vector<std::string> vec = srs_string_split(req->from, "@");
                            if (vec.size() > 1){
                                req->from_realm = vec.at(1);
                            }
                        } 
                        else if (!strcasecmp(phead, "to:")) {
                            content = srs_string_replace(content, "sip:", "");
                            req->to = srs_sip_get_form_to_uri(content.c_str());
                            if (srs_string_contains(content, "tag")) {
                                req->to_tag = srs_sip_get_param(content.c_str(), "tag");
                            }

                            std::vector<std::string> vec = srs_string_split(req->to, "@");
                            if (vec.size() > 1){
                                req->to_realm = vec.at(1);
                            }
                        } 
                        else if (!strcasecmp(phead, "via:")) {
                            req->via = content;
                            req->branch = srs_sip_get_param(content.c_str(), "branch");
                        } 
                        else if (!strcasecmp(phead, "expires:")){
                            req->expires = strtoul(content.c_str(), NULL, 10);
                        }
                        else if (!strcasecmp(phead, "user-agent:")){
                            req->user_agent = content;
                        } 
                        else if (!strcasecmp(phead, "max-forwards:")){
                            req->max_forwards = strtoul(content.c_str(), NULL, 10);
                        }
                        else if (!strcasecmp(phead, "www-authenticate:")){
                            req->www_authenticate = content;
                        } 
                        else if (!strcasecmp(phead, "authorization:")){
                            req->authorization = content;
                        } 
                        else {
                            //TODO: fixme
                            srs_trace("sip: unkonw message head %s content=%s", phead, content.c_str());
                        }
                   }
                }
            }
        }else{
            p++;
        }
    }
   
    std::vector<std::string>  method_uri_ver = srs_string_split(firstline, " ");

    if (method_uri_ver.empty()) {
        return srs_error_new(ERROR_GB28181_SIP_PRASE_FAILED, "parse request firstline is empty"); 
    }

    //respone first line text:SIP/2.0 200 OK
    if (!strcasecmp(method_uri_ver.at(0).c_str(), "sip/2.0")) {
        req->cmdtype = SrsSipCmdRespone;
        //req->method= vec_seq.at(1);
        req->status = method_uri_ver.size() > 0 ? method_uri_ver.at(1) : "";
        req->version = method_uri_ver.at(0);
        req->uri = req->from;

        vector<string> str = srs_string_split(req->to, "@");
        std::string ss = str.empty() ? "" : str.at(0);
        req->sip_auth_id = srs_string_replace(ss, "sip:", "");
  
    }else {//request first line text :MESSAGE sip:34020000002000000001@3402000000 SIP/2.0
        req->cmdtype = SrsSipCmdRequest;
        req->method= method_uri_ver.at(0);
        req->uri = method_uri_ver.size() > 0 ? method_uri_ver.at(1) : "";
        req->version = method_uri_ver.size() > 1 ? method_uri_ver.at(2) : "";

        vector<string> str = srs_string_split(req->from, "@");
        std::string ss = str.empty() ? "" : str.at(0);
        req->sip_auth_id = srs_string_replace(ss, "sip:", "");
    }

    req->sip_username =  req->sip_auth_id;

    //Content-Type: Application/MANSCDP+xml
    if (!strcasecmp(req->content_type.c_str(),"application/manscdp+xml")){
        std::map<std::string, std::string> body_map;
        //xml to map
        if ((err = parse_xml(body, body_map)) != srs_success) {
            return srs_error_wrap(err, "sip parse xml");
        };
        
        //Response Cmd
        if (body_map.find("Response") != body_map.end()){
            std::string cmdtype = body_map["Response@CmdType"];
            if (cmdtype == "Catalog"){
                //Response@DeviceList@Item@DeviceID:3000001,3000002
                std::vector<std::string> vec_device_id = srs_string_split(body_map["Response@DeviceList@Item@DeviceID"], ",");
                //Response@DeviceList@Item@Status:ON,OFF
                std::vector<std::string> vec_device_status = srs_string_split(body_map["Response@DeviceList@Item@Status"], ",");
                 
                //map key:devicd_id value:status 
                for(int i=0 ; i<vec_device_id.size(); i++){
                    std::string status = "";
                    if (vec_device_id.size() > i) {
                        status = vec_device_status.at(i);
                    }
              
                    req->device_list_map[vec_device_id.at(i)] = status;
                }
            }else{
                //TODO: fixme
                srs_trace("sip: Response cmdtype=%s not processed", cmdtype.c_str());
            }
        } //Notify Cmd
        else if (body_map.find("Notify") !=  body_map.end()){
            std::string cmdtype = body_map["Notify@CmdType"];
            if (cmdtype == "Keepalive"){
                //TODO: ????
                std::vector<std::string> vec_device_id = srs_string_split(body_map["Notify@Info@DeviceID"], ",");
                for(int i=0; i<vec_device_id.size(); i++){
                    //req->device_list_map[vec_device_id.at(i)] = "OFF";
                }
            }else{
               //TODO: fixme
               srs_trace("sip: Notify cmdtype=%s not processed", cmdtype.c_str());
            }
        }// end if(body_map)
    }//end if (!strcasecmp)
   
    srs_info("sip: method=%s uri=%s version=%s cmdtype=%s", 
           req->method.c_str(), req->uri.c_str(), req->version.c_str(), req->get_cmdtype_str().c_str());
    srs_info("via=%s", req->via.c_str());
    srs_info("via_branch=%s", req->branch.c_str());
    srs_info("cseq=%d", req->seq);
    srs_info("contact=%s", req->contact.c_str());
    srs_info("from=%s",  req->from.c_str());
    srs_info("to=%s",  req->to.c_str());
    srs_info("callid=%s", req->call_id.c_str());
    srs_info("status=%s", req->status.c_str());
    srs_info("from_tag=%s", req->from_tag.c_str());
    srs_info("to_tag=%s", req->to_tag.c_str());
    srs_info("sip_auth_id=%s", req->sip_auth_id.c_str());

    return err;
}

std::string SrsSipStack::get_sip_from(SrsSipRequest const *req)
{
    std::string from_tag;
    if (req->from_tag.empty()){
        from_tag = "";
    }else {
        from_tag = ";tag=" + req->from_tag;
    }

    return  "<sip:" + req->from + ">" + from_tag;
}

std::string SrsSipStack::get_sip_to(SrsSipRequest const *req)
{
    std::string to_tag;
    if (req->to_tag.empty()){
        to_tag = "";
    }else {
        to_tag = ";tag=" + req->to_tag;
    }

    return  "<sip:" + req->to + ">" + to_tag;
}

std::string SrsSipStack::get_sip_via(SrsSipRequest const *req)
{
    std::string via = srs_string_replace(req->via, SRS_SIP_VERSION"/UDP ", "");
    std::vector<std::string> vec_via = srs_string_split(via, ";");

    std::string ip_port = vec_via.empty() ? "" : vec_via.at(0);
    std::vector<std::string> vec_ip_port = srs_string_split(ip_port, ":");

    std::string ip = vec_ip_port.empty() ? "" : vec_ip_port.at(0);
    std::string port = vec_ip_port.size() > 1 ? vec_ip_port.at(1) : "";
    
    std::string branch, rport, received;
    if (req->branch.empty()){
        branch = "";
    }else {
        branch = ";branch=" + req->branch;
    }

    if (!req->peer_ip.empty()){
        ip = req->peer_ip;

        std::stringstream ss;
        ss << req->peer_port;
        port = ss.str();
    }

    received = ";received=" + ip;
    rport = ";rport=" + port;

    return SRS_SIP_VERSION"/UDP " + ip_port + rport + received + branch;
}

void SrsSipStack::resp_keepalive(std::stringstream& ss, SrsSipRequest *req)
{
    ss << SRS_SIP_VERSION <<" 200 OK" << SRS_RTSP_CRLF
    << "Via: " << SRS_SIP_VERSION << "/UDP " << req->host << ":" << req->host_port << ";branch=" << req->branch << SRS_RTSP_CRLF
    << "From: <sip:" << req->from.c_str() << ">;tag=" << req->from_tag << SRS_RTSP_CRLF
    << "To: <sip:"<< req->to.c_str() << ">\r\n"
    << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
    << "CSeq: " << req->seq << " " << req->method << SRS_RTSP_CRLF
    << "Contact: "<< req->contact << SRS_RTSP_CRLF
    << "Max-Forwards: 70" << SRS_RTSP_CRLF
    << "User-Agent: "<< SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
    << "Content-Length: 0" << SRS_RTSP_CRLFCRLF;
}

void SrsSipStack::resp_status(stringstream& ss, SrsSipRequest *req)
{
    if (req->method == "REGISTER"){
        /* 
        //request:  sip-agent-----REGISTER------->sip-server
        REGISTER sip:34020000002000000001@3402000000 SIP/2.0
        Via: SIP/2.0/UDP 192.168.137.11:5060;rport;branch=z9hG4bK1371463273
        From: <sip:34020000001320000003@3402000000>;tag=2043466181
        To: <sip:34020000001320000003@3402000000>
        Call-ID: 1011047669
        CSeq: 1 REGISTER
        Contact: <sip:34020000001320000003@192.168.137.11:5060>
        Max-Forwards: 70
        User-Agent: IP Camera
        Expires: 3600
        Content-Length: 0
        
        //response:  sip-agent<-----200 OK--------sip-server
        SIP/2.0 200 OK
        Via: SIP/2.0/UDP 192.168.137.11:5060;rport;branch=z9hG4bK1371463273
        From: <sip:34020000001320000003@3402000000>
        To: <sip:34020000001320000003@3402000000>
        CSeq: 1 REGISTER
        Call-ID: 1011047669
        Contact: <sip:34020000001320000003@192.168.137.11:5060>
        User-Agent: SRS/4.0.4(Leo)
        Expires: 3600
        Content-Length: 0

        */
        if (req->authorization.empty()){
            //TODO: fixme supoort 401
            //return req_401_unauthorized(ss, req);
        }

        ss << SRS_SIP_VERSION <<" 200 OK" << SRS_RTSP_CRLF
        << "Via: " << get_sip_via(req) << SRS_RTSP_CRLF
        << "From: "<< get_sip_from(req) << SRS_RTSP_CRLF
        << "To: "<<  get_sip_to(req) << SRS_RTSP_CRLF
        << "CSeq: "<< req->seq << " " << req->method <<  SRS_RTSP_CRLF
        << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
        << "Contact: " << req->contact << SRS_RTSP_CRLF
        << "User-Agent: " << SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
        << "Expires: " << req->expires << SRS_RTSP_CRLF
        << "Content-Length: 0" << SRS_RTSP_CRLFCRLF;
    }else{
        /*
        //request: sip-agnet-------MESSAGE------->sip-server
        MESSAGE sip:34020000002000000001@3402000000 SIP/2.0
        Via: SIP/2.0/UDP 192.168.137.11:5060;rport;branch=z9hG4bK1066375804
        From: <sip:34020000001320000003@3402000000>;tag=1925919231
        To: <sip:34020000002000000001@3402000000>
        Call-ID: 1185236415
        CSeq: 20 MESSAGE
        Content-Type: Application/MANSCDP+xml
        Max-Forwards: 70
        User-Agent: IP Camera
        Content-Length:   175

        <?xml version="1.0" encoding="UTF-8"?>
        <Notify>
        <CmdType>Keepalive</CmdType>
        <SN>1</SN>
        <DeviceID>34020000001320000003</DeviceID>
        <Status>OK</Status>
        <Info>
        </Info>
        </Notify>
        //response: sip-agent------200 OK --------> sip-server
        SIP/2.0 200 OK
        Via: SIP/2.0/UDP 192.168.137.11:5060;rport;branch=z9hG4bK1066375804
        From: <sip:34020000001320000003@3402000000>
        To: <sip:34020000002000000001@3402000000>
        CSeq: 20 MESSAGE
        Call-ID: 1185236415
        User-Agent: SRS/4.0.4(Leo)
        Content-Length: 0
        
        */

        ss << SRS_SIP_VERSION <<" 200 OK" << SRS_RTSP_CRLF
        << "Via: " << get_sip_via(req) << SRS_RTSP_CRLF
        << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
        << "To: "<< get_sip_to(req) << SRS_RTSP_CRLF
        << "CSeq: "<< req->seq << " " << req->method <<  SRS_RTSP_CRLF
        << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
        << "User-Agent: " << SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
        << "Content-Length: 0" << SRS_RTSP_CRLFCRLF;
    }
   
}

void SrsSipStack::req_invite(stringstream& ss, SrsSipRequest *req, string ip, int port, uint32_t ssrc)
{
    /* 
    //request: sip-agent <-------INVITE------ sip-server
    INVITE sip:34020000001320000003@3402000000 SIP/2.0
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@39.100.155.146:15063>;tag=512358805
    To: <sip:34020000001320000003@3402000000>
    Call-ID: 200008805
    CSeq: 20 INVITE
    Content-Type: Application/SDP
    Contact: <sip:34020000001320000003@3402000000>
    Max-Forwards: 70 
    User-Agent: SRS/4.0.4(Leo)
    Subject: 34020000001320000003:630886,34020000002000000001:0
    Content-Length: 164

    v=0
    o=34020000001320000003 0 0 IN IP4 39.100.155.146
    s=Play
    c=IN IP4 39.100.155.146
    t=0 0
    m=video 9000 RTP/AVP 96
    a=recvonly
    a=rtpmap:96 PS/90000
    y=630886
    //response: sip-agent --------100 Trying--------> sip-server
    SIP/2.0 100 Trying
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport=15063;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@39.100.155.146:15063>;tag=512358805
    To: <sip:34020000001320000003@3402000000>
    Call-ID: 200008805
    CSeq: 20 INVITE
    User-Agent: IP Camera
    Content-Length: 0

    //response: sip-agent -------200 OK--------> sip-server 
    SIP/2.0 200 OK
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport=15063;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@39.100.155.146:15063>;tag=512358805
    To: <sip:34020000001320000003@3402000000>;tag=1083111311
    Call-ID: 200008805
    CSeq: 20 INVITE
    Contact: <sip:34020000001320000003@192.168.137.11:5060>
    Content-Type: application/sdp
    User-Agent: IP Camera
    Content-Length:   263

    v=0
    o=34020000001320000003 1073 1073 IN IP4 192.168.137.11
    s=Play
    c=IN IP4 192.168.137.11
    t=0 0
    m=video 15060 RTP/AVP 96
    a=setup:active
    a=sendonly
    a=rtpmap:96 PS/90000
    a=username:34020000001320000003
    a=password:12345678
    a=filesize:0
    y=0000630886
    f=
    //request: sip-agent <------ ACK ------- sip-server
    ACK sip:34020000001320000003@3402000000 SIP/2.0
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@39.100.155.146:15063>;tag=512358805
    To: <sip:34020000001320000003@3402000000>
    Call-ID: 200008805
    CSeq: 20 ACK
    Max-Forwards: 70
    User-Agent: SRS/4.0.4(Leo)
    Content-Length: 0
    */
    char _ssrc[11];
    sprintf(_ssrc, "%010d", ssrc);
  
    std::stringstream sdp;
    sdp << "v=0" << SRS_RTSP_CRLF
    << "o=" << req->serial << " 0 0 IN IP4 " << ip << SRS_RTSP_CRLF
    << "s=Play" << SRS_RTSP_CRLF
    << "c=IN IP4 " << ip << SRS_RTSP_CRLF
    << "t=0 0" << SRS_RTSP_CRLF
    //TODO 97 98 99 current no support
    //<< "m=video " << port <<" RTP/AVP 96 97 98 99" << SRS_RTSP_CRLF
    << "m=video " << port <<" RTP/AVP 96" << SRS_RTSP_CRLF
    << "a=recvonly" << SRS_RTSP_CRLF
    << "a=rtpmap:96 PS/90000" << SRS_RTSP_CRLF
    //TODO: current no support
    //<< "a=rtpmap:97 MPEG4/90000" << SRS_RTSP_CRLF
    //<< "a=rtpmap:98 H264/90000" << SRS_RTSP_CRLF
    //<< "a=rtpmap:99 H265/90000" << SRS_RTSP_CRLF
    //<< "a=streamMode:MAIN\r\n"
    //<< "a=filesize:0\r\n"
    << "y=" << _ssrc << SRS_RTSP_CRLF;

    
    std::stringstream from, to, uri;
    //"INVITE sip:34020000001320000001@3402000000 SIP/2.0\r\n
    uri << "sip:" <<  req->chid << "@" << req->realm;
    //From: <sip:34020000002000000001@%s:%s>;tag=500485%d\r\n
    from << req->serial << "@" << req->realm;
    to <<  req->chid <<  "@" << req->realm;
   
    req->from = from.str();
    req->to = to.str();

    if (!req->to_realm.empty()){
        req->to  =  req->chid + "@" + req->to_realm;
    }

    if (!req->from_realm.empty()){
        req->from  =  req->serial + "@" + req->from_realm;
    }

    req->uri  = uri.str();

    req->call_id = srs_sip_generate_call_id();
    req->branch = srs_sip_generate_branch();
    req->from_tag = srs_sip_generate_from_tag();

    ss << "INVITE " << req->uri << " " << SRS_SIP_VERSION << SRS_RTSP_CRLF
    << "Via: " << SRS_SIP_VERSION << "/UDP "<< req->host << ":" << req->host_port << ";rport;branch=" << req->branch << SRS_RTSP_CRLF
    << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
    << "To: " << get_sip_to(req) << SRS_RTSP_CRLF
    << "Call-ID: " << req->call_id <<SRS_RTSP_CRLF
    << "CSeq: " << req->seq << " INVITE" << SRS_RTSP_CRLF
    << "Content-Type: Application/SDP" << SRS_RTSP_CRLF
    << "Contact: <sip:" << req->to << ">" << SRS_RTSP_CRLF
    << "Max-Forwards: 70" << SRS_RTSP_CRLF
    << "User-Agent: " << SRS_SIP_USER_AGENT <<SRS_RTSP_CRLF
    << "Subject: "<< req->chid << ":" << _ssrc << "," << req->serial << ":0" << SRS_RTSP_CRLF
    << "Content-Length: " << sdp.str().length() << SRS_RTSP_CRLFCRLF
    << sdp.str();
}


void SrsSipStack::req_401_unauthorized(std::stringstream& ss, SrsSipRequest *req)
{
    /* sip-agent <-----401 Unauthorized ------ sip-server
    SIP/2.0 401 Unauthorized
    Via: SIP/2.0/UDP 192.168.137.92:5061;rport=61378;received=192.168.1.13;branch=z9hG4bK802519080
    From: <sip:34020000001320000004@192.168.137.92:5061>;tag=611442989
    To: <sip:34020000001320000004@192.168.137.92:5061>;tag=102092689
    CSeq: 1 REGISTER
    Call-ID: 1650345118
    User-Agent: SRS/4.0.4(Leo)
    Contact: <sip:34020000002000000001@192.168.1.23:15060>
    Content-Length: 0
    WWW-Authenticate: Digest realm="3402000000",qop="auth",nonce="f1da98bd160f3e2efe954c6eedf5f75a"
    */

    ss << SRS_SIP_VERSION <<" 401 Unauthorized" << SRS_RTSP_CRLF
    //<< "Via: " << req->via << SRS_RTSP_CRLF
    << "Via: " << get_sip_via(req) << SRS_RTSP_CRLF
    << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
    << "To: " << get_sip_to(req) << SRS_RTSP_CRLF
    << "CSeq: "<< req->seq << " " << req->method <<  SRS_RTSP_CRLF
    << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
    << "Contact: " << req->contact << SRS_RTSP_CRLF
    << "User-Agent: " << SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
    << "Content-Length: 0" << SRS_RTSP_CRLF
    << "WWW-Authenticate: Digest realm=\"3402000000\",qop=\"auth\",nonce=\"f1da98bd160f3e2efe954c6eedf5f75a\"" << SRS_RTSP_CRLFCRLF;
    return;
}

void SrsSipStack::req_ack(std::stringstream& ss, SrsSipRequest *req){
    /*
    //request: sip-agent <------ ACK ------- sip-server
    ACK sip:34020000001320000003@3402000000 SIP/2.0
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@39.100.155.146:15063>;tag=512358805
    To: <sip:34020000001320000003@3402000000>
    Call-ID: 200008805
    CSeq: 20 ACK
    Max-Forwards: 70
    User-Agent: SRS/4.0.4(Leo)
    Content-Length: 0
    */
  
    ss << "ACK " << "sip:" <<  req->chid << "@" << req->realm << " "<< SRS_SIP_VERSION << SRS_RTSP_CRLF
    << "Via: " << SRS_SIP_VERSION << "/UDP " << req->host << ":" << req->host_port << ";rport;branch=" << req->branch << SRS_RTSP_CRLF
    << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
    << "To: "<< get_sip_to(req) << SRS_RTSP_CRLF
    << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
    << "CSeq: " << req->seq << " ACK"<< SRS_RTSP_CRLF
    << "Max-Forwards: 70" << SRS_RTSP_CRLF
    << "User-Agent: "<< SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
    << "Content-Length: 0" << SRS_RTSP_CRLFCRLF;
}

void SrsSipStack::req_bye(std::stringstream& ss, SrsSipRequest *req)
{
    /*
    //request: sip-agent <------BYE------ sip-server
    BYE sip:34020000001320000003@3402000000 SIP/2.0
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@3402000000>;tag=512358805
    To: <sip:34020000001320000003@3402000000>;tag=1083111311
    Call-ID: 200008805
    CSeq: 79 BYE
    Max-Forwards: 70
    User-Agent: SRS/4.0.4(Leo)
    Content-Length: 0

    //response: sip-agent ------200 OK ------> sip-server
    SIP/2.0 200 OK
    Via: SIP/2.0/UDP 39.100.155.146:15063;rport=15063;branch=z9hG4bK34208805
    From: <sip:34020000002000000001@3402000000>;tag=512358805
    To: <sip:34020000001320000003@3402000000>;tag=1083111311
    Call-ID: 200008805
    CSeq: 79 BYE
    User-Agent: IP Camera
    Content-Length: 0

    */

    std::stringstream from, to, uri;
    uri << "sip:" <<  req->chid << "@" << req->realm;
    from << req->serial << "@"  << req->realm;
    to << req->chid <<  "@" <<  req->realm;

    req->from = from.str();
    req->to = to.str();

    if (!req->to_realm.empty()){
        req->to  =  req->chid + "@" + req->to_realm;
    }

    if (!req->from_realm.empty()){
        req->from  =  req->serial + "@" + req->from_realm;
    }

    req->uri  = uri.str();

    ss << "BYE " << req->uri << " "<< SRS_SIP_VERSION << SRS_RTSP_CRLF
    //<< "Via: "<< SRS_SIP_VERSION << "/UDP "<< req->host << ":" << req->host_port << ";rport" << branch << SRS_RTSP_CRLF
    << "Via: " << SRS_SIP_VERSION << "/UDP " << req->host << ":" << req->host_port << ";rport;branch=" << req->branch << SRS_RTSP_CRLF
    << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
    << "To: " << get_sip_to(req) << SRS_RTSP_CRLF
    //bye callid is inivte callid
    << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
    << "CSeq: "<< req->seq <<" BYE" << SRS_RTSP_CRLF
    << "Max-Forwards: 70" << SRS_RTSP_CRLF
    << "User-Agent: " << SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
    << "Content-Length: 0" << SRS_RTSP_CRLFCRLF;
   
}

void SrsSipStack::req_query_catalog(std::stringstream& ss, SrsSipRequest *req)
{
    /*
    //request: sip-agent <----MESSAGE Query Catalog--- sip-server
    MESSAGE sip:34020000001110000001@192.168.1.21:5060 SIP/2.0
    Via: SIP/2.0/UDP 192.168.1.17:5060;rport;branch=z9hG4bK563315752
    From: <sip:34020000001110000001@3402000000>;tag=387315752
    To: <sip:34020000001110000001@192.168.1.21:5060>
    Call-ID: 728315752
    CSeq: 32 MESSAGE
    Content-Type: Application/MANSCDP+xml
    Max-Forwards: 70
    User-Agent: SRS/4.0.20(Leo)
    Content-Length: 162

    <?xml version="1.0" encoding="UTF-8"?>
    <Query>
        <CmdType>Catalog</CmdType>
        <SN>419315752</SN>
        <DeviceID>34020000001110000001</DeviceID>
    </Query>
    SIP/2.0 200 OK
    Via: SIP/2.0/UDP 192.168.1.17:5060;rport=5060;branch=z9hG4bK563315752
    From: <sip:34020000001110000001@3402000000>;tag=387315752
    To: <sip:34020000001110000001@192.168.1.21:5060>;tag=1420696981
    Call-ID: 728315752
    CSeq: 32 MESSAGE
    User-Agent: Embedded Net DVR/NVR/DVS
    Content-Length: 0

    //response: sip-agent ----MESSAGE Query Catalog---> sip-server
    SIP/2.0 200 OK
    Via: SIP/2.0/UDP 192.168.1.17:5060;rport=5060;received=192.168.1.17;branch=z9hG4bK563315752
    From: <sip:34020000001110000001@3402000000>;tag=387315752
    To: <sip:34020000001110000001@192.168.1.21:5060>;tag=1420696981
    CSeq: 32 MESSAGE
    Call-ID: 728315752
    User-Agent: SRS/4.0.20(Leo)
    Content-Length: 0

    //request: sip-agent ----MESSAGE Response Catalog---> sip-server
    MESSAGE sip:34020000001110000001@3402000000.spvmn.cn SIP/2.0
    Via: SIP/2.0/UDP 192.168.1.21:5060;rport;branch=z9hG4bK1681502633
    From: <sip:34020000001110000001@3402000000.spvmn.cn>;tag=1194168247
    To: <sip:34020000001110000001@3402000000.spvmn.cn>
    Call-ID: 685380150
    CSeq: 20 MESSAGE
    Content-Type: Application/MANSCDP+xml
    Max-Forwards: 70
    User-Agent: Embedded Net DVR/NVR/DVS
    Content-Length:   909

    <?xml version="1.0" encoding="gb2312"?>
    <Response>
    <CmdType>Catalog</CmdType>
    <SN>419315752</SN>
    <DeviceID>34020000001110000001</DeviceID>
    <SumNum>8</SumNum>
    <DeviceList Num="2">
    <Item>
    <DeviceID>34020000001320000001</DeviceID>
    <Name>Camera 01</Name>
    <Manufacturer>Manufacturer</Manufacturer>
    <Model>Camera</Model>
    <Owner>Owner</Owner>
    <CivilCode>CivilCode</CivilCode>
    <Address>192.168.254.18</Address>
    <Parental>0</Parental>
    <SafetyWay>0</SafetyWay>
    <RegisterWay>1</RegisterWay>
    <Secrecy>0</Secrecy>
    <Status>ON</Status>
    </Item>
    <Item>
    <DeviceID>34020000001320000002</DeviceID>
    <Name>IPCamera 02</Name>
    <Manufacturer>Manufacturer</Manufacturer>
    <Model>Camera</Model>
    <Owner>Owner</Owner>
    <CivilCode>CivilCode</CivilCode>
    <Address>192.168.254.14</Address>
    <Parental>0</Parental>
    <SafetyWay>0</SafetyWay>
    <RegisterWay>1</RegisterWay>
    <Secrecy>0</Secrecy>
    <Status>OFF</Status>
    </Item>
    </DeviceList>
    </Response>

    */

    std::stringstream xml;
    std::string xmlbody;

    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << SRS_RTSP_CRLF
    << "<Query>" << SRS_RTSP_CRLF
    << "<CmdType>Catalog</CmdType>" << SRS_RTSP_CRLF
    << "<SN>" << srs_sip_generate_sn() << "</SN>" << SRS_RTSP_CRLF
    << "<DeviceID>" << req->sip_auth_id << "</DeviceID>" << SRS_RTSP_CRLF
    << "</Query>" << SRS_RTSP_CRLF;
    xmlbody = xml.str();

    std::stringstream from, to, uri;
    //"INVITE sip:34020000001320000001@3402000000 SIP/2.0\r\n
    uri << "sip:" <<  req->sip_auth_id << "@" << req->realm;
    //From: <sip:34020000002000000001@%s:%s>;tag=500485%d\r\n
    from << req->serial << "@" << req->host << ":"  << req->host_port;
    to << req->sip_auth_id <<  "@" << req->realm;
 
    req->from = from.str();
    req->to   = to.str();
    req->uri  = uri.str();
   
    req->call_id = srs_sip_generate_call_id();
    req->branch = srs_sip_generate_branch();
    req->from_tag = srs_sip_generate_from_tag();

    ss << "MESSAGE " << req->uri << " " << SRS_SIP_VERSION << SRS_RTSP_CRLF
    << "Via: " << SRS_SIP_VERSION << "/UDP "<< req->host << ":" << req->host_port << ";rport;branch=" << req->branch << SRS_RTSP_CRLF
    << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
    << "To: " << get_sip_to(req) << SRS_RTSP_CRLF
    << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
    << "CSeq: " << req->seq << " MESSAGE" << SRS_RTSP_CRLF
    << "Content-Type: Application/MANSCDP+xml" << SRS_RTSP_CRLF
    << "Max-Forwards: 70" << SRS_RTSP_CRLF
    << "User-Agent: " << SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
    << "Content-Length: " << xmlbody.length() << SRS_RTSP_CRLFCRLF
    << xmlbody;

}

void SrsSipStack::req_ptz(std::stringstream& ss, SrsSipRequest *req, uint8_t cmd, uint8_t speed, int priority)
{
   
    /*
    <?xml version="1.0"?>  
    <Control>  
    <CmdType>DeviceControl</CmdType>  
    <SN>11</SN>  
    <DeviceID>34020000001310000053</DeviceID>  
    <PTZCmd>A50F01021F0000D6</PTZCmd>  
    </Control> 
    */

    uint8_t ptz_cmd[8] = {0};
    ptz_cmd[0] = SRS_SIP_PTZ_START;
    ptz_cmd[1] = 0x0F;
    ptz_cmd[2] = 0x01;
    ptz_cmd[3] = cmd;
    switch(cmd){
        case SrsSipPtzCmdStop: // = 0x00
            ptz_cmd[4] = 0;
            ptz_cmd[5] = 0;
            ptz_cmd[6] = 0;
            break;
        case SrsSipPtzCmdRight: // = 0x01,
        case SrsSipPtzCmdLeft: //  = 0x02,
            ptz_cmd[4] = speed;
            break;
        case SrsSipPtzCmdDown: //  = 0x04,
        case SrsSipPtzCmdUp: //    = 0x08,
            ptz_cmd[5] = speed;
            break;
        case SrsSipPtzCmdZoomOut: //  = 0x10,
        case SrsSipPtzCmdZoomIn: //   = 0x20
            ptz_cmd[6] = (speed & 0x0F) << 4;
            break;
        default:
            return;
    }

    uint32_t check = 0;
    for (int i = 0; i < 7; i++){
        check += ptz_cmd[i];
    }

    ptz_cmd[7] = (uint8_t)(check % 256);

    std::stringstream ss_ptzcmd;
    for (int i = 0; i < 8; i++){
        char hex_cmd[3] = {0};
        sprintf(hex_cmd, "%02X", ptz_cmd[i]);
        ss_ptzcmd << hex_cmd;
    }

    std::stringstream xml;
    std::string xmlbody;

    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << SRS_RTSP_CRLF
    << "<Control>" << SRS_RTSP_CRLF
    << "<CmdType>DeviceControl</CmdType>" << SRS_RTSP_CRLF
    << "<SN>" << srs_sip_generate_sn() << "</SN>" << SRS_RTSP_CRLF
    << "<DeviceID>" << req->sip_auth_id << "</DeviceID>" << SRS_RTSP_CRLF
    << "<PTZCmd>" << ss_ptzcmd.str() << "</PTZCmd>" << SRS_RTSP_CRLF
    << "<Info>" << SRS_RTSP_CRLF
    << "<ControlPriority>" << priority << "</ControlPriority>" << SRS_RTSP_CRLF
    << "</Info>" << SRS_RTSP_CRLF
    << "</Control>" << SRS_RTSP_CRLF;
    xmlbody = xml.str();

    std::stringstream from, to, uri, call_id;
    //"INVITE sip:34020000001320000001@3402000000 SIP/2.0\r\n
    uri << "sip:" <<  req->sip_auth_id << "@" << req->realm;
    //From: <sip:34020000002000000001@%s:%s>;tag=500485%d\r\n
    from << req->serial << "@" << req->host << ":"  << req->host_port;
    to << req->sip_auth_id <<  "@" << req->realm;
   
    req->from = from.str();
    req->to   = to.str();
    req->uri  = uri.str();

    req->call_id = srs_sip_generate_call_id();
    req->branch = srs_sip_generate_branch();
    req->from_tag = srs_sip_generate_from_tag();

    ss << "MESSAGE " << req->uri << " "<< SRS_SIP_VERSION << SRS_RTSP_CRLF
    //<< "Via: "<< SRS_SIP_VERSION << "/UDP "<< req->host << ":" << req->host_port << ";rport" << branch << SRS_RTSP_CRLF
    << "Via: " << SRS_SIP_VERSION << "/UDP " << req->host << ":" << req->host_port << ";rport;branch=" << req->branch << SRS_RTSP_CRLF
    << "From: " << get_sip_from(req) << SRS_RTSP_CRLF
    << "To: " << get_sip_to(req) << SRS_RTSP_CRLF
    << "Call-ID: " << req->call_id << SRS_RTSP_CRLF
    << "CSeq: "<< req->seq <<" MESSAGE" << SRS_RTSP_CRLF
    << "Content-Type: Application/MANSCDP+xml" << SRS_RTSP_CRLF
    << "Max-Forwards: 70" << SRS_RTSP_CRLF
    << "User-Agent: " << SRS_SIP_USER_AGENT << SRS_RTSP_CRLF
    << "Content-Length: " << xmlbody.length() << SRS_RTSP_CRLFCRLF
    << xmlbody;

}

#endif

