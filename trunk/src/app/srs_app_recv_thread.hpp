/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifndef SRS_APP_RECV_THREAD_HPP
#define SRS_APP_RECV_THREAD_HPP

/*
#include <srs_app_recv_thread.hpp>
*/

#include <srs_core.hpp>

#include <vector>

#include <srs_app_thread.hpp>

class SrsRtmpServer;
class SrsMessage;

/**
* the recv thread used to replace the timeout recv,
* which hurt performance for the epoll_ctrl is frequently used.
* @see: SrsRtmpConn::playing
* @see: https://github.com/winlinvip/simple-rtmp-server/issues/217
*/
class SrsRecvThread : public ISrsThreadHandler
{
private:
    SrsThread* trd;
    SrsRtmpServer* rtmp;
    std::vector<SrsMessage*> queue;
public:
    SrsRecvThread(SrsRtmpServer* rtmp_sdk);
    virtual ~SrsRecvThread();
public:
    virtual bool empty();
    virtual int size();
    virtual SrsMessage* pump();
public:
    virtual int start();
    virtual void stop();
    virtual int cycle();
public:
    virtual void on_thread_start();
    virtual void on_thread_stop();
};

#endif

