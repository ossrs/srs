/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <srs_rtmp_buffer.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_reload.hpp>

class SrsRtmpServer;
class SrsCommonMessage;
class SrsRtmpConn;
class SrsSource;
class SrsRequest;
class SrsConsumer;

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
    virtual int handle(SrsCommonMessage* msg) = 0;
    /**
    * when recv message error.
    */
    virtual void on_recv_error(int ret) = 0;
    /**
    * when thread start or stop, 
    * for example, the message handler can set whether auto response.
    */
    virtual void on_thread_start() = 0;
    virtual void on_thread_stop() = 0;
};

/**
 * the recv thread, use message handler to handle each received message.
 */
class SrsRecvThread : public ISrsReusableThreadHandler
{
protected:
    SrsReusableThread* trd;
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
* @see: https://github.com/simple-rtmp-server/srs/issues/217
*/
class SrsQueueRecvThread : public ISrsMessageHandler
{
private:
    std::vector<SrsCommonMessage*> queue;
    SrsRecvThread trd;
    SrsRtmpServer* rtmp;
    // the recv thread error code.
    int recv_error_code;
    SrsConsumer* _consumer;
public:
    SrsQueueRecvThread(SrsConsumer* consumer, SrsRtmpServer* rtmp_sdk, int timeout_ms);
    virtual ~SrsQueueRecvThread();
public:
    virtual int start();
    virtual void stop();
public:
    virtual bool empty();
    virtual int size();
    virtual SrsCommonMessage* pump();
    virtual int error_code();
public:
    virtual bool can_handle();
    virtual int handle(SrsCommonMessage* msg);
    virtual void on_recv_error(int ret);
public:
    virtual void on_thread_start();
    virtual void on_thread_stop();
};

/**
* the publish recv thread got message and callback the source method to process message.
* @see: https://github.com/simple-rtmp-server/srs/issues/237
*/
class SrsPublishRecvThread : virtual public ISrsMessageHandler
#ifdef SRS_PERF_MERGED_READ
    , virtual public IMergeReadHandler
#endif
    , virtual public ISrsReloadHandler
{
private:
    SrsRecvThread trd;
    SrsRtmpServer* rtmp;
    SrsRequest* req;
    // the msgs already got.
    int64_t _nb_msgs;
    // for mr(merged read),
    // @see https://github.com/simple-rtmp-server/srs/issues/241
    bool mr;
    int mr_fd;
    int mr_sleep;
    // for realtime
    // @see https://github.com/simple-rtmp-server/srs/issues/257
    bool realtime;
    // the recv thread error code.
    int recv_error_code;
    SrsRtmpConn* _conn;
    // the params for conn callback.
    SrsSource* _source;
    bool _is_fmle;
    bool _is_edge;
    // the error timeout cond
    // @see https://github.com/simple-rtmp-server/srs/issues/244
    st_cond_t error;
public:
    SrsPublishRecvThread(SrsRtmpServer* rtmp_sdk, 
        SrsRequest* _req, int mr_sock_fd, int timeout_ms, 
        SrsRtmpConn* conn, SrsSource* source, bool is_fmle, bool is_edge);
    virtual ~SrsPublishRecvThread();
public:
    /**
    * wait for error for some timeout.
    */
    virtual int wait(int timeout_ms);
    virtual int64_t nb_msgs();
    virtual int error_code();
public:
    virtual int start();
    virtual void stop();
    virtual void on_thread_start();
    virtual void on_thread_stop();
// interface ISrsMessageHandler    
public:
    virtual bool can_handle();
    virtual int handle(SrsCommonMessage* msg);
    virtual void on_recv_error(int ret);
// interface IMergeReadHandler
public:
#ifdef SRS_PERF_MERGED_READ
    virtual void on_read(ssize_t nread);
#endif
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_mr(std::string vhost);
    virtual int on_reload_vhost_realtime(std::string vhost);
private:
    virtual void set_socket_buffer(int sleep_ms);
};

#endif

