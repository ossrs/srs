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

#ifndef SRS_APP_MPEGTS_UDP_HPP
#define SRS_APP_MPEGTS_UDP_HPP

/*
#include <srs_app_mpegts_udp.hpp>
*/

#include <srs_core.hpp>

class sockaddr_in;
#include <string>

class SrsConfDirective;

#ifdef SRS_AUTO_STREAM_CASTER

/**
* the mpegts over udp stream caster.
*/
class SrsMpegtsOverUdp
{
private:
    std::string output;
public:
    SrsMpegtsOverUdp(SrsConfDirective* c);
    virtual ~SrsMpegtsOverUdp();
public:
    /**
    * when udp listener got a udp packet, notice server to process it.
    * @param type, the client type, used to create concrete connection, 
    *       for instance RTMP connection to serve client.
    * @param from, the udp packet from address.
    * @param buf, the udp packet bytes, user should copy if need to use.
    * @param nb_buf, the size of udp packet bytes.
    * @remark user should never use the buf, for it's a shared memory bytes.
    */
    virtual int on_udp_packet(sockaddr_in* from, char* buf, int nb_buf);
private:
    /**
    * when got a ts packet, in size TS_PACKET_SIZE.
    */
    virtual int on_ts_packet(char* ts_packet);
};

#endif

#endif
