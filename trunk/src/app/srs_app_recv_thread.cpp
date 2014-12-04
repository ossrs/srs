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

#include <srs_app_recv_thread.hpp>

#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_stack.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>

ISrsMessageHandler::ISrsMessageHandler()
{
}

ISrsMessageHandler::~ISrsMessageHandler()
{
}

SrsRecvThread::SrsRecvThread(ISrsMessageHandler* msg_handler, SrsRtmpServer* rtmp_sdk, int timeout_ms)
{
    timeout = timeout_ms;
    handler = msg_handler;
    rtmp = rtmp_sdk;
    trd = new SrsThread("recv", this, 0, true);
}

SrsRecvThread::~SrsRecvThread()
{
    // stop recv thread.
    stop();

    // destroy the thread.
    srs_freep(trd);
}

int SrsRecvThread::start()
{
    return trd->start();
}

void SrsRecvThread::stop()
{
    trd->stop();
}

int SrsRecvThread::cycle()
{
    int ret = ERROR_SUCCESS;

    while (trd->can_loop()) {
        if (!handler->can_handle()) {
            st_usleep(timeout * 1000);
            continue;
        }
    
        SrsMessage* msg = NULL;
        
        // recv and handle message
        ret = rtmp->recv_message(&msg);
        if (ret == ERROR_SUCCESS) {
            ret = handler->handle(msg);
        }
    
        if (ret != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("thread process message failed. ret=%d", ret);
            }
    
            // we use no timeout to recv, should never got any error.
            trd->stop_loop();
            
            // notice the handler got a recv error.
            handler->on_recv_error(ret);
    
            return ret;
        }
        srs_verbose("thread loop recv message. ret=%d", ret);
    }

    return ret;
}

void SrsRecvThread::stop_loop()
{
    trd->stop_loop();
}

void SrsRecvThread::on_thread_start()
{
    // the multiple messages writev improve performance large,
    // but the timeout recv will cause 33% sys call performance,
    // to use isolate thread to recv, can improve about 33% performance.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/194
    // @see: https://github.com/winlinvip/simple-rtmp-server/issues/217
    rtmp->set_recv_timeout(ST_UTIME_NO_TIMEOUT);
    
    handler->on_thread_start();
}

void SrsRecvThread::on_thread_stop()
{
    // reset the timeout to pulse mode.
    rtmp->set_recv_timeout(timeout * 1000);
    
    handler->on_thread_stop();
}

SrsQueueRecvThread::SrsQueueRecvThread(SrsRtmpServer* rtmp_sdk, int timeout_ms)
    : trd(this, rtmp_sdk, timeout_ms)
{
    rtmp = rtmp_sdk;
    recv_error_code = ERROR_SUCCESS;
}

SrsQueueRecvThread::~SrsQueueRecvThread()
{
    trd.stop();

    // clear all messages.
    std::vector<SrsMessage*>::iterator it;
    for (it = queue.begin(); it != queue.end(); ++it) {
        SrsMessage* msg = *it;
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

SrsMessage* SrsQueueRecvThread::pump()
{
    srs_assert(!queue.empty());
    
    SrsMessage* msg = *queue.begin();
    
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

int SrsQueueRecvThread::handle(SrsMessage* msg)
{
    // put into queue, the send thread will get and process it,
    // @see SrsRtmpConn::process_play_control_msg
    queue.push_back(msg);

    return ERROR_SUCCESS;
}

void SrsQueueRecvThread::on_recv_error(int ret)
{
    recv_error_code = ret;
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
    SrsRtmpServer* rtmp_sdk, int fd, int timeout_ms,
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

    mr_fd = fd;
}

SrsPublishRecvThread::~SrsPublishRecvThread()
{
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

int SrsPublishRecvThread::start()
{
    return trd.start();
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
    // socket recv buffer, system will double it.
    int nb_rbuf = SRS_MR_SOCKET_BUFFER / 2;
    socklen_t sock_buf_size = sizeof(int);
    if (setsockopt(mr_fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, sock_buf_size) < 0) {
        srs_warn("set sock SO_RCVBUF=%d failed.", nb_rbuf);
    }
    getsockopt(mr_fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, &sock_buf_size);

    srs_trace("merged read sockbuf=%d, actual=%d, sleep %d when nread<=%d", 
        SRS_MR_SOCKET_BUFFER, nb_rbuf, SRS_MR_MAX_SLEEP_MS, SRS_MR_SMALL_BYTES);

    // enable the merge read
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/241
    rtmp->set_merge_read(true, this);
#endif
}

void SrsPublishRecvThread::on_thread_stop()
{
    // we donot set the auto response to true,
    // for we donot set to false yet.
    
    // when thread stop, signal the conn thread which wait.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/244
    st_cond_signal(error);

#ifdef SRS_PERF_MERGED_READ
    // disable the merge read
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/241
    rtmp->set_merge_read(false, NULL);
#endif
}

bool SrsPublishRecvThread::can_handle()
{
    // publish thread always can handle message.
    return true;
}

int SrsPublishRecvThread::handle(SrsMessage* msg)
{
    int ret = ERROR_SUCCESS;

    _nb_msgs++;

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
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/244
    st_cond_signal(error);
}

#ifdef SRS_PERF_MERGED_READ
void SrsPublishRecvThread::on_read(ssize_t nread)
{
    if (nread < 0 || SRS_MR_MAX_SLEEP_MS <= 0) {
        return;
    }
    
    /**
    * to improve read performance, merge some packets then read,
    * when it on and read small bytes, we sleep to wait more data.,
    * that is, we merge some data to read together.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/241
    */
    if (nread < SRS_MR_SMALL_BYTES) {
        st_usleep(SRS_MR_MAX_SLEEP_MS * 1000);
    }
}
#endif
