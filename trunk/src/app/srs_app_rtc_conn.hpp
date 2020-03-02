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

#ifndef SRS_APP_RTC_CONN_HPP
#define SRS_APP_RTC_CONN_HPP

#include <srs_core.hpp>

#include <string>
#include <map>
#include <vector>

class SrsServer;
class SrsStunPacket;

class SrsSdpMediaInfo
{
private:
public:
    SrsSdpMediaInfo();
    virtual ~SrsSdpMediaInfo();
};

class SrsSdp
{
private:
    std::string sdp;
    int version;
    std::string ice_ufrag;
    std::string ice_pwd;
    std::string fingerprint;
    std::string setup;
    std::vector<SrsSdpMediaInfo> media_infos;
public:
    SrsSdp();
    virtual ~SrsSdp();

    srs_error_t parse(const std::string& sdp);
private:
    srs_error_t parse_attr(const std::string& line);
};

enum SrsRtcSessionStateType
{
    INIT = -1,
    WAITING_STUN = 1,
    DOING_DTLS_HANDSHAKE = 2,
    ESTABLISHED = 3,
    CLOSED = 4,
};

class SrsRtcSession
{
public:
private:
    SrsSdp  peer_sdp;
    SrsSdp  offer_sdp;
    SrsRtcSessionStateType session_state;
public:
    SrsRtcSession();
    virtual ~SrsRtcSession();

    srs_error_t on_udp_packet(const std::string& peer_ip, const int peer_port, const char* data, const int size);
    srs_error_t on_stun(const SrsStunPacket& stun_packet);
    srs_error_t send_packet();
};

class SrsRtcServer
{
private:
    SrsServer* server;
    std::map<std::string, SrsRtcSession*> map_sessions;
public:
    SrsRtcServer(SrsServer* svr);
    virtual ~SrsRtcServer();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t on_udp_packet(const std::string& peer_ip, const int peer_port, const char* data, const int size);
private:
    srs_error_t on_stun(const std::string& peer_ip, const int peer_port, const char* data, const int size);
    srs_error_t on_dtls(const std::string& peer_ip, const int peer_port, const char* data, const int size);
    srs_error_t on_rtp_or_rtcp(const std::string& peer_ip, const int peer_port, const char* data, const int size);
private:
    SrsRtcSession* find_rtc_session(const std::string& ufrag);
};

#endif

