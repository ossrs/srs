/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_mpegts_udp.hpp>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>

// Transport Stream packets are 188 bytes in length.
#define TS_PACKET_SIZE          188

#ifdef SRS_AUTO_STREAM_CASTER

SrsMpegtsOverUdp::SrsMpegtsOverUdp(SrsConfDirective* c)
{
    output = _srs_config->get_stream_caster_output(c);
}

SrsMpegtsOverUdp::~SrsMpegtsOverUdp()
{
}

int SrsMpegtsOverUdp::on_udp_packet(sockaddr_in* from, char* buf, int nb_buf)
{
    int ret = ERROR_SUCCESS;

    std::string peer_ip = inet_ntoa(from->sin_addr);
    int peer_port = ntohs(from->sin_port);

    // drop ts packet when size not modulus by 188
    if (nb_buf < TS_PACKET_SIZE || (nb_buf % TS_PACKET_SIZE) != 0) {
        srs_warn("udp: drop %s:%d packet %d bytes", peer_ip.c_str(), peer_port, nb_buf);
        return ret;
    }
    srs_info("udp: got %s:%d packet %d bytes", peer_ip.c_str(), peer_port, nb_buf);

    // process each ts packet
    for (int i = 0; i < nb_buf; i += TS_PACKET_SIZE) {
        char* ts_packet = buf + i;
        if ((ret = on_ts_packet(ts_packet)) != ERROR_SUCCESS) {
            srs_warn("mpegts: ignore ts packet error. ret=%d", ret);
            continue;
        }
    }

    return ret;
}

int SrsMpegtsOverUdp::on_ts_packet(char* ts_packet)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

#endif
