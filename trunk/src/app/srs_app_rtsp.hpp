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

#ifndef SRS_APP_RTSP_HPP
#define SRS_APP_RTSP_HPP

/*
#include <srs_app_rtsp.hpp>
*/

#include <srs_core.hpp>

#include <string>
#include <vector>
#include <map>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_listener.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

class SrsStSocket;
class SrsRtspConn;
class SrsRtspStack;
class SrsRtspCaster;
class SrsConfDirective;

/**
* a rtp connection which transport a stream.
*/
class SrsRtpConn: public ISrsUdpHandler
{
private:
    SrsUdpListener* listener;
    SrsRtspConn* rtsp;
    int _port;
public:
    SrsRtpConn(SrsRtspConn* r, int p);
    virtual ~SrsRtpConn();
public:
    virtual int port();
    virtual int listen();
// interface ISrsUdpHandler
public:
    virtual int on_udp_packet(sockaddr_in* from, char* buf, int nb_buf);
};

/**
* the rtsp connection serve the fd.
*/
class SrsRtspConn : public ISrsThreadHandler
{
private:
    std::string output;
private:
    std::string session;
    // video stream.
    int video_id;
    std::string video_codec;
    SrsRtpConn* video_rtp;
    // audio stream.
    int audio_id;
    std::string audio_codec;
    int audio_sample_rate;
    int audio_channel;
    SrsRtpConn* audio_rtp;
    // video sequence header.
    std::string sps;
    std::string pps;
    // audio sequence header.
    std::string asc;
private:
    st_netfd_t stfd;
    SrsStSocket* skt;
    SrsRtspStack* rtsp;
    SrsRtspCaster* caster;
    SrsThread* trd;
public:
    SrsRtspConn(SrsRtspCaster* c, st_netfd_t fd, std::string o);
    virtual ~SrsRtspConn();
public:
    virtual int serve();
private:
    virtual int do_cycle();
// interface ISrsThreadHandler
public:
    virtual int cycle();
    virtual void on_thread_stop();
};

/**
* the caster for rtsp.
*/
class SrsRtspCaster : public ISrsTcpHandler
{
private:
    std::string output;
    int local_port_min;
    int local_port_max;
    // key: port, value: whether used.
    std::map<int, bool> used_ports;
private:
    std::vector<SrsRtspConn*> clients;
public:
    SrsRtspCaster(SrsConfDirective* c);
    virtual ~SrsRtspCaster();
public:
    /**
    * alloc a rtp port from local ports pool.
    * @param pport output the rtp port.
    */
    virtual int alloc_port(int* pport);
    /**
    * free the alloced rtp port.
    */
    virtual void free_port(int lpmin, int lpmax);
// interface ISrsTcpHandler
public:
    virtual int on_tcp_client(st_netfd_t stfd);
// internal methods.
public:
    virtual void remove(SrsRtspConn* conn);
};

#endif

#endif
