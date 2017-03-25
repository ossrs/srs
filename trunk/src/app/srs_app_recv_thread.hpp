/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#ifndef SRS_APP_RECV_THREAD_HPP
#define SRS_APP_RECV_THREAD_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_thread.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_reload.hpp>

class SrsRtmpServer;
class SrsCommonMessage;
class SrsRtmpConn;
class SrsSource;
class SrsRequest;
class SrsConsumer;

/**
 * The message consumer which consume a message.
 */
class ISrsMessageConsumer
{
public:
    ISrsMessageConsumer();
    virtual ~ISrsMessageConsumer();
public:
    /**
     * Consume the received message.
     * @remark user must free this message.
     */
    virtual int consume(SrsCommonMessage* msg) = 0;
};

/**
 * The message pumper to pump messages to processer.
 */
class ISrsMessagePumper : public ISrsMessageConsumer
{
public:
    ISrsMessagePumper();
    virtual ~ISrsMessagePumper();
public:
    /**
     * Whether the pumper is interrupted.
     * For example, when pumpter is busy, it's interrupted,
     * please wait for a while then try to feed the pumper.
     */
    virtual bool interrupted() = 0;
    /**
     * Interrupt the pumper for a error.
     */
    virtual void interrupt(int error) = 0;
    /**
     * When start the pumper.
     */
    virtual void on_start() = 0;
    /**
     * When stop the pumper.
     */
    virtual void on_stop() = 0;
};

/**
 * the recv thread, use message handler to handle each received message.
 */
class SrsRecvThread : public ISrsReusableThread2Handler
{
protected:
    SrsReusableThread2* trd;
    ISrsMessagePumper* pumper;
    SrsRtmpServer* rtmp;
    // The recv timeout in ms.
    int timeout;
public:
    // Constructor.
    // @param tm The receive timeout in ms.
    SrsRecvThread(ISrsMessagePumper* p, SrsRtmpServer* r, int tm);
    virtual ~SrsRecvThread();
public:
    virtual int cid();
public:
    virtual int start();
    virtual void stop();
    virtual void stop_loop();
// interface ISrsReusableThread2Handler
public:
    virtual int cycle();
    virtual void on_thread_start();
    virtual void on_thread_stop();
};

/**
 * the recv thread used to replace the timeout recv,
 * which hurt performance for the epoll_ctrl is frequently used.
 * @see: SrsRtmpConn::playing
 * @see: https://github.com/ossrs/srs/issues/217
 */
class SrsQueueRecvThread : public ISrsMessagePumper
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
// interface ISrsMessagePumper
public:
    virtual int consume(SrsCommonMessage* msg);
    virtual bool interrupted();
    virtual void interrupt(int ret);
    virtual void on_start();
    virtual void on_stop();
};

/**
 * the publish recv thread got message and callback the source method to process message.
 * @see: https://github.com/ossrs/srs/issues/237
 */
class SrsPublishRecvThread : virtual public ISrsMessagePumper, virtual public ISrsReloadHandler
#ifdef SRS_PERF_MERGED_READ
, virtual public IMergeReadHandler
#endif
{
private:
    SrsRecvThread trd;
    SrsRtmpServer* rtmp;
    SrsRequest* req;
    // the msgs already got.
    int64_t _nb_msgs;
    // for mr(merged read),
    // @see https://github.com/ossrs/srs/issues/241
    bool mr;
    int mr_fd;
    int mr_sleep;
    // for realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime;
    // the recv thread error code.
    int recv_error_code;
    SrsRtmpConn* _conn;
    // the params for conn callback.
    SrsSource* _source;
    // the error timeout cond
    // @see https://github.com/ossrs/srs/issues/244
    st_cond_t error;
    // merged context id.
    int cid;
    int ncid;
public:
    SrsPublishRecvThread(SrsRtmpServer* rtmp_sdk, SrsRequest* _req, int mr_sock_fd, int timeout_ms, SrsRtmpConn* conn, SrsSource* source);
    virtual ~SrsPublishRecvThread();
public:
    /**
     * wait for error for some timeout.
     */
    virtual int wait(uint64_t timeout_ms);
    virtual int64_t nb_msgs();
    virtual int error_code();
    virtual void set_cid(int v);
    virtual int get_cid();
public:
    virtual int start();
    virtual void stop();
// interface ISrsMessagePumper
public:
    virtual int consume(SrsCommonMessage* msg);
    virtual bool interrupted();
    virtual void interrupt(int ret);
    virtual void on_start();
    virtual void on_stop();
// interface IMergeReadHandler
public:
#ifdef SRS_PERF_MERGED_READ
    virtual void on_read(ssize_t nread);
#endif
// interface ISrsReloadHandler
public:
    virtual int on_reload_vhost_publish(std::string vhost);
    virtual int on_reload_vhost_realtime(std::string vhost);
private:
    virtual void set_socket_buffer(int sleep_ms);
};

#endif

