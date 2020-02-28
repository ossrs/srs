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

#include <srs_app_rtc.hpp>

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
#include <srs_app_rtc_conn.hpp>
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

SrsRtc::SrsRtc(SrsRtcServer* rtc_svr)
{
    rtc_server = rtc_svr;
}

SrsRtc::~SrsRtc()
{
}

srs_error_t SrsRtc::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
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

    return rtc_server->on_udp_packet(peer_ip, peer_port, buf, nb_buf);
}
