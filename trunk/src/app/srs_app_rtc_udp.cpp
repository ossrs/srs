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

#include <srs_app_rtc_udp.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_stun_stack.hpp>
#include <srs_rtsp_stack.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_protocol_utility.hpp>

static bool is_stun(const char* data, const int size) {
	return data != NULL && size > 0 && (data[0] == 0 || data[0] == 1);
}

static bool is_rtp_or_rtcp(const char* data, const int size) {
	return data != NULL && size > 0 && (data[0] >= 128 && data[0] <= 191);
}

static bool is_dtls(const char* data, const int size) {
	return data != NULL && size > 0 && (data[0] >= 20 && data[0] <= 64);
}

SrsRtcUserInfo::SrsRtcUserInfo(const std::string& u, const std::string& p)
{
    username = u;
    password = p;
}

SrsRtcUserInfo::~SrsRtcUserInfo()
{
}

SrsRtcOverUdp::SrsRtcOverUdp()
{
}

SrsRtcOverUdp::~SrsRtcOverUdp()
{
}

SrsRtcSession* SrsRtcOverUdp::create_rtc_session(const std::string& peer_ip, const std::string& remote_username, const std::string& remote_password)
{
    SrsRtcSession* rtc_session = new SrsRtcSession();
    // TODO: process exception when session already exist
    user_session_map[peer_ip].insert(make_pair(SrsRtcUserInfo(remote_username, remote_password), rtc_session)).second;

    return rtc_session;
}

SrsRtcSession* SrsRtcOverUdp::find_rtc_session_by_user_info(const std::string& peer_ip, const std::string& remote_username, const std::string& remote_password)
{
    std::map<std::string, std::map<SrsRtcUserInfo, SrsRtcSession*> >::iterator iter = user_session_map.find(peer_ip);
    if (iter == user_session_map.end()) {
        return NULL;
    }

    std::map<SrsRtcUserInfo, SrsRtcSession*>::iterator sub_iter = iter->second.find(SrsRtcUserInfo(remote_username, remote_password));
    if (sub_iter == iter->second.end()) {
        return NULL;
    }

    return sub_iter->second;
}

srs_error_t SrsRtcOverUdp::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
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

    std::string peer_id = peer_ip + ":" + std::string(port_string);

    return on_udp_bytes(peer_ip, peer_port, peer_id, buf, nb_buf);
}

SrsRtcSession* SrsRtcOverUdp::find_rtc_session_by_peer_id(const std::string& peer_id) 
{
    map<string, SrsRtcSession*>::iterator iter = id_session_map.find(peer_id);
    if (iter == id_session_map.end()) {
        return NULL;
    } 

    return iter->second;
}

srs_error_t SrsRtcOverUdp::on_udp_bytes(const string& host, const int& port, const string& peer_id, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;

    srs_trace("recv rtc udp packet from %s:%d, peer_id=%s, nb_buf=%d", host.c_str(), port, peer_id.c_str(), nb_buf);

	if (is_rtp_or_rtcp(buf, nb_buf)) {
        err = on_rtp_or_rtcp(host, port, peer_id, buf, nb_buf);
	} else if (is_stun(buf, nb_buf)) {
        err = on_stun(host, port, peer_id, buf, nb_buf);
	} else if (is_dtls(buf, nb_buf)) {
        err = on_dtls(host, port, peer_id, buf, nb_buf);
	} else {
        return srs_error_wrap(err, "unknown udp packet");
    }

    return err;
}

srs_error_t SrsRtcOverUdp::on_rtp_or_rtcp(const string& host, const int& port, const string& peer_id, const char* buf, int nb_buf) {
    srs_error_t err = srs_success;

    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(peer_id);
    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can't find rtc session in rtp/rtcp host=%s, port=%d", 
                   host.c_str(), port);
    }

    SrsRtpPacket rtp_packet;
    SrsBuffer buffer(const_cast<char*>(buf), nb_buf);
    rtp_packet.decode(&buffer);

    rtc_session->on_rtp_or_rtcp(&rtp_packet);

    return err;
}

srs_error_t SrsRtcOverUdp::on_stun(const string& host, const int& port, const string& peer_id, const char* buf, int nb_buf) {
    srs_error_t err = srs_success;

    SrsStunPacket stun_packet;
    stun_packet.decode(buf, nb_buf);

    SrsRtcSession* rtc_session = find_rtc_session_by_user_info(host, stun_packet.username(), stun_packet.password());
    if (rtc_session == NULL) {
        return err;
        return srs_error_wrap(err, "can't find rtc session in stun host=%s, port=%d, username=%s, password=%s", 
                   host.c_str(), port, stun_packet.username().c_str(), stun_packet.password().c_str());
    }

    // TODO: process when session mismatch
    id_session_map[peer_id] = rtc_session;

    rtc_session->on_stun(&stun_packet);

    return err;
}

srs_error_t SrsRtcOverUdp::on_dtls(const string& host, const int& port, const string& peer_id, const char* buf, int nb_buf) {
    srs_error_t err = srs_success;

    SrsRtcSession* rtc_session = find_rtc_session_by_peer_id(peer_id);
    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can't find rtc session in dtls host=%s, port=%d", 
                   host.c_str(), port);
    }

    rtc_session->on_dtls();

    return err;
}

SrsRtcSession::SrsRtcSession()
{
}

SrsRtcSession::~SrsRtcSession()
{
}

srs_error_t SrsRtcSession::on_rtp_or_rtcp(SrsRtpPacket* rtp_packet)
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t SrsRtcSession::on_stun(SrsStunPacket* stun_packet)
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t SrsRtcSession::on_dtls()
{
    srs_error_t err = srs_success;

    return err;
}

