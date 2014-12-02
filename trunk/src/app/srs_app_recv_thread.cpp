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
    trd = new SrsThread(this, 0, true);
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
    SrsRtmpServer* rtmp_sdk, int timeout_ms,
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
}

SrsPublishRecvThread::~SrsPublishRecvThread()
{
    trd.stop();
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
}

void SrsPublishRecvThread::on_thread_start()
{
    // we donot set the auto response to false,
    // for the main thread never send message.

    // notice the protocol stack to merge chunks to big buffer.
    // for example, the buffer is 64KB=512kb, it's 1s buffer for 500kbps video stream.
    // so we can use read_fullly(64KB) to merge all chunks in 1s.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/241
    rtmp->set_merge_chunks(true);
}

void SrsPublishRecvThread::on_thread_stop()
{
    // revert state
    rtmp->set_merge_chunks(false);
}
