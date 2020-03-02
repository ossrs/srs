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

#include <srs_app_rtc_conn.hpp>

using namespace std;

#include <sstream>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_stun_stack.hpp>

static bool is_stun(const char* data, const int size) {
    return data != NULL && size > 0 && (data[0] == 0 || data[0] == 1); 
}

static bool is_rtp_or_rtcp(const char* data, const int size) {
    return data != NULL && size > 0 && (data[0] >= 128 && data[0] <= 191);
}

static bool is_dtls(const char* data, const int size) {
    return data != NULL && size > 0 && (data[0] >= 20 && data[0] <= 64);
}

SrsSdpMediaInfo::SrsSdpMediaInfo()
{
}

SrsSdpMediaInfo::~SrsSdpMediaInfo()
{
}

SrsSdp::SrsSdp()
{
}

SrsSdp::~SrsSdp()
{
}

srs_error_t SrsSdp::parse(const string& sdp)
{
    srs_error_t err = srs_success;

    if (sdp.size() < 2 || sdp[0] != 'v' || sdp[1] != '=') {
        return srs_error_wrap(err, "invalid sdp");
    }

    string line;
    istringstream is(sdp);
    while (getline(is, line)) {
        srs_trace("line=%s", line.c_str());

        if (line.size() < 2 || line[1] != '=') {
            return srs_error_wrap(err, "invalid sdp line=%s", line.c_str());
        }

        switch (line[1]) {
            case 'v' :{
                break;
            }
            case 'o' :{
                break;
            }
            case 's' :{
                break;
            }
            case 't' :{
                break;
            }
            case 'c' :{
                break;
            }
            case 'a' :{
                if (parse_attr(line) != srs_success) {
                    return srs_error_wrap(err, "parse sdp line=%s failed", line.c_str());
                }
                break;
            }
            case 'm' :{
                break;
            }
        }
    }

    return err;
}

srs_error_t SrsSdp::parse_attr(const string& line)
{
    srs_error_t err = srs_success;

    return err;
}

SrsRtcSession::SrsRtcSession()
{
    session_state = INIT;
}

SrsRtcSession::~SrsRtcSession()
{
}

srs_error_t SrsRtcSession::on_stun(const SrsStunPacket& stun_packet)
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t SrsRtcSession::send_packet()
{
}

SrsRtcServer::SrsRtcServer(SrsServer* svr)
{
    server = svr;
}

SrsRtcServer::~SrsRtcServer()
{
}

srs_error_t SrsRtcServer::initialize()
{
    srs_error_t err = srs_success;

    return err;
}

srs_error_t SrsRtcServer::on_udp_packet(const string& peer_ip, const int peer_port, const char* data, const int size)
{
    srs_error_t err = srs_success;

    if (is_stun(data, size)) {
        return on_stun(peer_ip, peer_port, data, size);
    } else if (is_dtls(data, size)) { 
        return on_dtls(peer_ip, peer_port, data, size);
    } else if (is_rtp_or_rtcp(data, size)) {
        return on_rtp_or_rtcp(peer_ip, peer_port, data, size);
    } 

    return srs_error_wrap(err, "unknown packet type");
}

srs_error_t SrsRtcServer::on_stun(const string& peer_ip, const int peer_port, const char* data, const int size)
{
    srs_error_t err = srs_success;

    srs_trace("peer %s:%d stun", peer_ip.c_str(), peer_port);

    SrsStunPacket stun_packet;
    if (stun_packet.decode(data, size) != srs_success) {
        return srs_error_wrap(err, "decode stun failed");
    }

    std::string peer_ufrag = stun_packet.ufrag();
    SrsRtcSession* rtc_session = find_rtc_session(peer_ufrag);
    if (rtc_session == NULL) {
        return srs_error_wrap(err, "can not find rtc_session, ufrag=%s", peer_ufrag.c_str());
    }

    return rtc_session->on_stun(stun_packet);
}

srs_error_t SrsRtcServer::on_dtls(const string& peer_ip, const int peer_port, const char* data, const int size)
{
    srs_error_t err = srs_success;
    return err;
}

srs_error_t SrsRtcServer::on_rtp_or_rtcp(const string& peer_ip, const int peer_port, const char* data, const int size)
{
    srs_error_t err = srs_success;
    return err;
}

SrsRtcSession* SrsRtcServer::find_rtc_session(const std::string& ufrag)
{
    map<string, SrsRtcSession*>::iterator iter = map_sessions.find(ufrag);
    if (iter == map_sessions.end()) {
        return NULL; 
    }

    return iter->second;
}
