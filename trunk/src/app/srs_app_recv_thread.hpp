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
class SrsRtmpConn;
class SrsSource;

/**
 * for the recv thread to handle the message.
 */
class ISrsMessageHandler
{
public:
    ISrsMessageHandler();
    virtual ~ISrsMessageHandler();
public:
    /**
    * whether the handler can handle,
    * for example, when queue recv handler got an message,
    * it wait the user to process it, then the recv thread
    * never recv message util the handler is ok.
    */
    virtual bool can_handle() = 0;
    /**
    * process the received message.
    */
    virtual int handle(SrsMessage* msg) = 0;
    /**
    * when recv message error.
    */
    virtual void on_recv_error(int ret) = 0;
};

/**
 * the recv thread, use message handler to handle each received message.
 */
class SrsRecvThread : public ISrsThreadHandler
{
protected:
    SrsThread* trd;
    ISrsMessageHandler* handler;
    SrsRtmpServer* rtmp;
    int timeout;
public:
    SrsRecvThread(ISrsMessageHandler* msg_handler, SrsRtmpServer* rtmp_sdk, int timeout_ms);
    virtual ~SrsRecvThread();
public:
    virtual int start();
    virtual void stop();
    virtual int cycle();
    virtual void stop_loop();
public:
    virtual void on_thread_start();
    virtual void on_thread_stop();
};

/**
* the recv thread used to replace the timeout recv,
* which hurt performance for the epoll_ctrl is frequently used.
* @see: SrsRtmpConn::playing
* @see: https://github.com/winlinvip/simple-rtmp-server/issues/217
*/
class SrsQueueRecvThread : public ISrsMessageHandler
{
private:
    std::vector<SrsMessage*> queue;
    SrsRecvThread trd;
    // the recv thread error code.
    int recv_error_code;
public:
    SrsQueueRecvThread(SrsRtmpServer* rtmp_sdk, int timeout_ms);
    virtual ~SrsQueueRecvThread();
public:
    virtual int start();
    virtual void stop();
public:
    virtual bool empty();
    virtual int size();
    virtual SrsMessage* pump();
    virtual int error_code();
public:
    virtual bool can_handle();
    virtual int handle(SrsMessage* msg);
    virtual void on_recv_error(int ret);
};

/**
 * the publish recv thread got message and callback the source method to process message.
* @see: https://github.com/winlinvip/simple-rtmp-server/issues/237
 */
class SrsPublishRecvThread : public ISrsMessageHandler
{
private:
    SrsRecvThread trd;
    // the msgs already got.
    int64_t _nb_msgs;
    // the recv thread error code.
    int recv_error_code;
    SrsRtmpConn* _conn;
    SrsSource* _source;
    bool _is_fmle;
    bool _is_edge;
public:
    SrsPublishRecvThread(SrsRtmpServer* rtmp_sdk, int timeout_ms,
        SrsRtmpConn* conn, SrsSource* source, bool is_fmle, bool is_edge);
    virtual ~SrsPublishRecvThread();
public:
    virtual int64_t nb_msgs();
    virtual int error_code();
public:
    virtual int start();
    virtual void stop();
public:
    virtual bool can_handle();
    virtual int handle(SrsMessage* msg);
    virtual void on_recv_error(int ret);
};

#endif

