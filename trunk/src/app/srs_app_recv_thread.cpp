/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_app_recv_thread.hpp>

#include <srs_rtmp_stack.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>

using namespace std;

// the max small bytes to group
#define SRS_MR_SMALL_BYTES 4096

ISrsMessageHandler::ISrsMessageHandler()
{
}

ISrsMessageHandler::~ISrsMessageHandler()
{
}

SrsRecvThread::SrsRecvThread(ISrsMessageHandler* msg_handler, SrsRtmpServer* rtmp_sdk, int tm)
{
    timeout = tm;
    handler = msg_handler;
    rtmp = rtmp_sdk;
    trd = new SrsReusableThread2("recv", this);
}

SrsRecvThread::~SrsRecvThread()
{
    // stop recv thread.
    stop();

    // destroy the thread.
    srs_freep(trd);
}

int SrsRecvThread::cid()
{
    return trd->cid();
}

int SrsRecvThread::start()
{
    return trd->start();
}

void SrsRecvThread::stop()
{
    trd->stop();
}

void SrsRecvThread::stop_loop()
{
    trd->interrupt();
}

int SrsRecvThread::cycle()
{
    int ret = ERROR_SUCCESS;

    while (!trd->interrupted()) {
        if (!handler->can_handle()) {
            st_usleep(timeout * 1000);
            continue;
        }
    
        SrsCommonMessage* msg = NULL;
        
        // recv and handle message
        ret = rtmp->recv_message(&msg);
        if (ret == ERROR_SUCCESS) {
            ret = handler->handle(msg);
        }
    
        if (ret != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret) && !srs_is_system_control_error(ret)) {
                srs_error("thread process message failed. ret=%d", ret);
            }
    
            // we use no timeout to recv, should never got any error.
            trd->interrupt();
            
            // notice the handler got a recv error.
            handler->on_recv_error(ret);
    
            return ret;
        }
        srs_verbose("thread loop recv message. ret=%d", ret);
    }

    return ret;
}

void SrsRecvThread::on_thread_start()
{
    // the multiple messages writev improve performance large,
    // but the timeout recv will cause 33% sys call performance,
    // to use isolate thread to recv, can improve about 33% performance.
    // @see https://github.com/ossrs/srs/issues/194
    // @see: https://github.com/ossrs/srs/issues/217
    rtmp->set_recv_timeout(SRS_CONSTS_NO_TMMS);
    
    handler->on_thread_start();
}

void SrsRecvThread::on_thread_stop()
{
    // reset the timeout to pulse mode.
    rtmp->set_recv_timeout(timeout * 1000);
    
    handler->on_thread_stop();
}

SrsQueueRecvThread::SrsQueueRecvThread(SrsConsumer* consumer, SrsRtmpServer* rtmp_sdk, int timeout_ms)
    : trd(this, rtmp_sdk, timeout_ms)
{
    _consumer = consumer;
    rtmp = rtmp_sdk;
    recv_error_code = ERROR_SUCCESS;
}

SrsQueueRecvThread::~SrsQueueRecvThread()
{
    stop();

    // clear all messages.
    std::vector<SrsCommonMessage*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsCommonMessage* msg = *it;
        srs_freep(msg);
    }
    queue.clear();
}

int SrsQueueRecvThread::start()
{
    return trd.start();
}

void SrsQueueRecvThread::stop()
{
    trd.stop();
}

bool SrsQueueRecvThread::empty()
{
    return queue.empty();
}

int SrsQueueRecvThread::size()
{
    return (int)queue.size();
}

SrsCommonMessage* SrsQueueRecvThread::pump()
{
    srs_assert(!queue.empty());
    
    SrsCommonMessage* msg = *queue.begin();
    
    queue.erase(queue.begin());
    
    return msg;
}

int SrsQueueRecvThread::error_code()
{
    return recv_error_code;
}

bool SrsQueueRecvThread::can_handle()
{
    // we only recv one message and then process it,
    // for the message may cause the thread to stop,
    // when stop, the thread is freed, so the messages
    // are dropped.
    return empty();
}

int SrsQueueRecvThread::handle(SrsCommonMessage* msg)
{
    // put into queue, the send thread will get and process it,
    // @see SrsRtmpConn::process_play_control_msg
    queue.push_back(msg);
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (_consumer) {
        _consumer->wakeup();
    }
#endif
    return ERROR_SUCCESS;
}

void SrsQueueRecvThread::on_recv_error(int ret)
{
    recv_error_code = ret;
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (_consumer) {
        _consumer->wakeup();
    }
#endif
}

void SrsQueueRecvThread::on_thread_start()
{
    // disable the protocol auto response,
    // for the isolate recv thread should never send any messages.
    rtmp->set_auto_response(false);
}

void SrsQueueRecvThread::on_thread_stop()
{
    // enable the protocol auto response,
    // for the isolate recv thread terminated.
    rtmp->set_auto_response(true);
}

SrsPublishRecvThread::SrsPublishRecvThread(
    SrsRtmpServer* rtmp_sdk, 
    SrsRequest* _req, int mr_sock_fd, int timeout_ms, 
    SrsRtmpConn* conn, SrsSource* source, bool is_fmle, bool is_edge
): trd(this, rtmp_sdk, timeout_ms)
{
    rtmp = rtmp_sdk;

    _conn = conn;
    _source = source;
    _is_fmle = is_fmle;
    _is_edge = is_edge;

    recv_error_code = ERROR_SUCCESS;
    _nb_msgs = 0;
    error = st_cond_new();
    ncid = cid = 0;
    
    req = _req;
    mr_fd = mr_sock_fd;

    // the mr settings, 
    // @see https://github.com/ossrs/srs/issues/241
    mr = _srs_config->get_mr_enabled(req->vhost);
    mr_sleep = _srs_config->get_mr_sleep_ms(req->vhost);
    
    realtime = _srs_config->get_realtime_enabled(req->vhost);
    
    _srs_config->subscribe(this);
}

SrsPublishRecvThread::~SrsPublishRecvThread()
{
    _srs_config->unsubscribe(this);
    
    trd.stop();
    st_cond_destroy(error);
}

int SrsPublishRecvThread::wait(int timeout_ms)
{
    if (recv_error_code != ERROR_SUCCESS) {
        return recv_error_code;
    }
    
    // ignore any return of cond wait.
    st_cond_timedwait(error, timeout_ms * 1000);
    
    return ERROR_SUCCESS;
}

int64_t SrsPublishRecvThread::nb_msgs()
{
    return _nb_msgs;
}

int SrsPublishRecvThread::error_code()
{
    return recv_error_code;
}

void SrsPublishRecvThread::set_cid(int v)
{
    ncid = v;
}

int SrsPublishRecvThread::get_cid()
{
    return ncid;
}

int SrsPublishRecvThread::start()
{
    int ret = trd.start();
    ncid = cid = trd.cid();
    return ret;
}

void SrsPublishRecvThread::stop()
{
    trd.stop();
}

void SrsPublishRecvThread::on_thread_start()
{
    // we donot set the auto response to false,
    // for the main thread never send message.

#ifdef SRS_PERF_MERGED_READ
    if (mr) {
        // set underlayer buffer size
        set_socket_buffer(mr_sleep);

        // disable the merge read
        // @see https://github.com/ossrs/srs/issues/241
        rtmp->set_merge_read(true, this);
    }
#endif
}

void SrsPublishRecvThread::on_thread_stop()
{
    // we donot set the auto response to true,
    // for we donot set to false yet.
    
    // when thread stop, signal the conn thread which wait.
    // @see https://github.com/ossrs/srs/issues/244
    st_cond_signal(error);

#ifdef SRS_PERF_MERGED_READ
    if (mr) {
        // disable the merge read
        // @see https://github.com/ossrs/srs/issues/241
        rtmp->set_merge_read(false, NULL);
    }
#endif
}

bool SrsPublishRecvThread::can_handle()
{
    // publish thread always can handle message.
    return true;
}

int SrsPublishRecvThread::handle(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    // when cid changed, change it.
    if (ncid != cid) {
        _srs_context->set_id(ncid);
        cid = ncid;
    }

    _nb_msgs++;
    
    // log to show the time of recv thread.
    srs_verbose("recv thread now=%"PRId64"us, got msg time=%"PRId64"ms, size=%d",
        srs_update_system_time_ms(), msg->header.timestamp, msg->size);

    // the rtmp connection will handle this message
    ret = _conn->handle_publish_message(_source, msg, _is_fmle, _is_edge);

    // must always free it,
    // the source will copy it if need to use.
    srs_freep(msg);
    
    return ret;
}

void SrsPublishRecvThread::on_recv_error(int ret)
{
    recv_error_code = ret;

    // when recv thread error, signal the conn thread to process it.
    // @see https://github.com/ossrs/srs/issues/244
    st_cond_signal(error);
}

#ifdef SRS_PERF_MERGED_READ
void SrsPublishRecvThread::on_read(ssize_t nread)
{
    if (!mr || realtime) {
        return;
    }
    
    if (nread < 0 || mr_sleep <= 0) {
        return;
    }
    
    /**
    * to improve read performance, merge some packets then read,
    * when it on and read small bytes, we sleep to wait more data.,
    * that is, we merge some data to read together.
    * @see https://github.com/ossrs/srs/issues/241
    */
    if (nread < SRS_MR_SMALL_BYTES) {
        st_usleep(mr_sleep * 1000);
    }
}
#endif

int SrsPublishRecvThread::on_reload_vhost_publish(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }

    // the mr settings, 
    // @see https://github.com/ossrs/srs/issues/241
    bool mr_enabled = _srs_config->get_mr_enabled(req->vhost);
    int sleep_ms = _srs_config->get_mr_sleep_ms(req->vhost);

    // update buffer when sleep ms changed.
    if (mr_sleep != sleep_ms) {
        set_socket_buffer(sleep_ms);
    }

#ifdef SRS_PERF_MERGED_READ
    // mr enabled=>disabled
    if (mr && !mr_enabled) {
        // disable the merge read
        // @see https://github.com/ossrs/srs/issues/241
        rtmp->set_merge_read(false, NULL);
    }
    // mr disabled=>enabled
    if (!mr && mr_enabled) {
        // enable the merge read
        // @see https://github.com/ossrs/srs/issues/241
        rtmp->set_merge_read(true, this);
    }
#endif

    // update to new state
    mr = mr_enabled;
    mr_sleep = sleep_ms;
    
    return ret;
}

int SrsPublishRecvThread::on_reload_vhost_realtime(string vhost)
{
    int ret = ERROR_SUCCESS;
    
    if (req->vhost != vhost) {
        return ret;
    }
    
    bool realtime_enabled = _srs_config->get_realtime_enabled(req->vhost);
    srs_trace("realtime changed %d=>%d", realtime, realtime_enabled);
    realtime = realtime_enabled;
    
    return ret;
}

void SrsPublishRecvThread::set_socket_buffer(int sleep_ms)
{
    // the bytes:
    //      4KB=4096, 8KB=8192, 16KB=16384, 32KB=32768, 64KB=65536,
    //      128KB=131072, 256KB=262144, 512KB=524288
    // the buffer should set to sleep*kbps/8,
    // for example, your system delivery stream in 1000kbps,
    // sleep 800ms for small bytes, the buffer should set to:
    //      800*1000/8=100000B(about 128KB).
    // other examples:
    //      2000*3000/8=750000B(about 732KB).
    //      2000*5000/8=1250000B(about 1220KB).
    int kbps = 5000;
    int socket_buffer_size = sleep_ms * kbps / 8;
    
    int fd = mr_fd;
    int onb_rbuf = 0;
    socklen_t sock_buf_size = sizeof(int);
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &onb_rbuf, &sock_buf_size);

    // socket recv buffer, system will double it.
    int nb_rbuf = socket_buffer_size / 2;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, sock_buf_size) < 0) {
        srs_warn("set sock SO_RCVBUF=%d failed.", nb_rbuf);
    }
    getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, &sock_buf_size);

    srs_trace("mr change sleep %d=>%d, erbuf=%d, rbuf %d=>%d, sbytes=%d, realtime=%d",
        mr_sleep, sleep_ms, socket_buffer_size, onb_rbuf, nb_rbuf, 
        SRS_MR_SMALL_BYTES, realtime);

    rtmp->set_recv_buffer(nb_rbuf);
}

