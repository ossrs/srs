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

#ifndef SRS_APP_RTC_UDP_HPP
#define SRS_APP_RTC_UDP_HPP

#include <srs_core.hpp>

struct sockaddr;
#include <string>
#include <map>

#include <srs_app_st.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_app_listener.hpp>

class SrsRtcSession;

class SrsRtcUserInfo {
private:
    std::string username;
    std::string password;
public:
    SrsRtcUserInfo(const std::string& u, const std::string& p);
    ~SrsRtcUserInfo();

    bool operator<(const SrsRtcUserInfo& rhs) const
    {
        return username < rhs.username && password < rhs.password;
    }
};

// The rtc over udp stream receiver
class SrsRtcOverUdp : virtual public ISrsUdpHandler
{
private:
    std::map<std::string, SrsRtcSession*> id_session_map; // ip:port => session
    std::map<std::string, std::map<SrsRtcUserInfo, SrsRtcSession*> > user_session_map;
public:
    SrsRtcOverUdp();
    virtual ~SrsRtcOverUdp();

    SrsRtcSession* create_rtc_session(const std::string& peer_ip, const std::string& remote_username, const std::string& remote_password);
    SrsRtcSession* find_rtc_session_by_user_info(const std::string& peer_ip, const std::string& remote_username, const std::string& remote_password);
    SrsRtcSession* find_rtc_session_by_peer_id(const std::string& peer_id);
// Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
private:
    virtual srs_error_t on_udp_bytes(const std::string& host, const int& port, const std::string& peer_id, char* buf, int nb_buf);
    srs_error_t on_rtp_or_rtcp(const std::string& host, const int& port, const std::string& peer_id, const char* buf, int nb_buf);
    srs_error_t on_stun(const std::string& host, const int& port, const std::string& peer_id, const char* buf, int nb_buf);
    srs_error_t on_dtls(const std::string& host, const int& port, const std::string& peer_id, const char* buf, int nb_buf);
};

class SrsRtpPacket;
class SrsStunPacket;

class SrsRtcSession
{
private:
    std::string local_username; 
    std::string local_password;
    std::string remote_username;
    std::string remote_password;
public:
    SrsRtcSession();
    virtual ~SrsRtcSession();

    srs_error_t on_rtp_or_rtcp(SrsRtpPacket* rtp_packet);
    srs_error_t on_stun(SrsStunPacket* stun_packet);
    srs_error_t on_dtls();
};

#endif

