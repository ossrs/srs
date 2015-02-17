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

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

class SrsConfDirective;
class SrsStSocket;
class SrsRtspStack;
class SrsRtspCaster;

/**
* the handler for rtsp handler.
*/
class ISrsRtspHandler
{
public:
    ISrsRtspHandler();
    virtual ~ISrsRtspHandler();
public:
    /**
    * serve the rtsp connection.
    */
    virtual int serve_client(st_netfd_t stfd) = 0;
};

/**
* the rtsp connection serve the fd.
*/
class SrsRtspConn : public ISrsThreadHandler
{
private:
    std::string output;
    int local_port_min;
    int local_port_max;
private:
    std::string session;
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
    SrsRtspConn(SrsRtspCaster* c, st_netfd_t fd, std::string o, int lpmin, int lpmax);
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
class SrsRtspCaster : public ISrsRtspHandler
{
private:
    std::string output;
    int local_port_min;
    int local_port_max;
private:
    std::vector<SrsRtspConn*> clients;
public:
    SrsRtspCaster(SrsConfDirective* c);
    virtual ~SrsRtspCaster();
public:
    virtual int serve_client(st_netfd_t stfd);
// internal methods.
public:
    virtual void remove(SrsRtspConn* conn);
};

#endif

#endif
