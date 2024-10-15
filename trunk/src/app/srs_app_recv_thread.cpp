//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_recv_thread.hpp>

#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_config.hpp>
#include <srs_app_source.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_statistic.hpp>

#include <sys/socket.h>
using namespace std;

// the max small bytes to group
#define SRS_MR_SMALL_BYTES 4096

ISrsMessageConsumer::ISrsMessageConsumer()
{
}

ISrsMessageConsumer::~ISrsMessageConsumer()
{
}

ISrsMessagePumper::ISrsMessagePumper()
{
}

ISrsMessagePumper::~ISrsMessagePumper()
{
}

SrsRecvThread::SrsRecvThread(ISrsMessagePumper* p, SrsRtmpServer* r, srs_utime_t tm, SrsContextId parent_cid)
{
    rtmp = r;
    pumper = p;
    timeout = tm;
    _parent_cid = parent_cid;
    trd = new SrsDummyCoroutine();
}

SrsRecvThread::~SrsRecvThread()
{
    srs_freep(trd);
}

SrsContextId SrsRecvThread::cid()
{
    return trd->cid();
}

srs_error_t SrsRecvThread::start()
{
    srs_error_t err = srs_success;
    
    srs_freep(trd);
    trd = new SrsSTCoroutine("recv", this, _parent_cid);

    //change stack size to 256K, fix crash when call some 3rd-part api.
    ((SrsSTCoroutine*)trd)->set_stack_size(1 << 18);
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "recv thread");
    }
    
    return err;
}

void SrsRecvThread::stop()
{
    trd->stop();
}

void SrsRecvThread::stop_loop()
{
    trd->interrupt();
}

srs_error_t SrsRecvThread::cycle()
{
    srs_error_t err = srs_success;
    
    // the multiple messages writev improve performance large,
    // but the timeout recv will cause 33% sys call performance,
    // to use isolate thread to recv, can improve about 33% performance.
    rtmp->set_recv_timeout(SRS_UTIME_NO_TIMEOUT);
    
    pumper->on_start();
    
    if ((err = do_cycle()) != srs_success) {
        err = srs_error_wrap(err, "recv thread");
    }
    
    // reset the timeout to pulse mode.
    rtmp->set_recv_timeout(timeout);
    
    pumper->on_stop();
    
    return err;
}

srs_error_t SrsRecvThread::do_cycle()
{
    srs_error_t err = srs_success;
    
    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "recv thread");
        }
        
        // When the pumper is interrupted, wait then retry.
        if (pumper->interrupted()) {
            srs_usleep(timeout);
            continue;
        }
        
        SrsCommonMessage* msg = NULL;
        
        // Process the received message.
        if ((err = rtmp->recv_message(&msg)) == srs_success) {
            err = pumper->consume(msg);
        }
        
        if (err != srs_success) {
            // Interrupt the receive thread for any error.
            trd->interrupt();
            
            // Notify the pumper to quit for error.
            pumper->interrupt(err);
            
            return srs_error_wrap(err, "recv thread");
        }
    }
    
    return err;
}

SrsQueueRecvThread::SrsQueueRecvThread(SrsLiveConsumer* consumer, SrsRtmpServer* rtmp_sdk, srs_utime_t tm, SrsContextId parent_cid)
	: trd(this, rtmp_sdk, tm, parent_cid)
{
    _consumer = consumer;
    rtmp = rtmp_sdk;
    recv_error = srs_success;
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
    
    srs_freep(recv_error);
}

srs_error_t SrsQueueRecvThread::start()
{
    srs_error_t err = srs_success;
    
    if ((err = trd.start()) != srs_success) {
        return srs_error_wrap(err, "queue recv thread");
    }
    
    return err;
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

srs_error_t SrsQueueRecvThread::error_code()
{
    return srs_error_copy(recv_error);
}

srs_error_t SrsQueueRecvThread::consume(SrsCommonMessage* msg)
{
    // put into queue, the send thread will get and process it,
    // @see SrsRtmpConn::process_play_control_msg
    queue.push_back(msg);
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (_consumer) {
        _consumer->wakeup();
    }
#endif
    return srs_success;
}

bool SrsQueueRecvThread::interrupted()
{
    // we only recv one message and then process it,
    // for the message may cause the thread to stop,
    // when stop, the thread is freed, so the messages
    // are dropped.
    return !empty();
}

void SrsQueueRecvThread::interrupt(srs_error_t err)
{
    srs_freep(recv_error);
    recv_error = srs_error_copy(err);
    
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (_consumer) {
        _consumer->wakeup();
    }
#endif
}

void SrsQueueRecvThread::on_start()
{
    // disable the protocol auto response,
    // for the isolate recv thread should never send any messages.
    rtmp->set_auto_response(false);
}

void SrsQueueRecvThread::on_stop()
{
    // enable the protocol auto response,
    // for the isolate recv thread terminated.
    rtmp->set_auto_response(true);
}

SrsPublishRecvThread::SrsPublishRecvThread(SrsRtmpServer* rtmp_sdk, SrsRequest* _req,
	int mr_sock_fd, srs_utime_t tm, SrsRtmpConn* conn, SrsSharedPtr<SrsLiveSource> source, SrsContextId parent_cid)
    : trd(this, rtmp_sdk, tm, parent_cid)
{
    rtmp = rtmp_sdk;
    
    _conn = conn;
    source_ = source;

    nn_msgs_for_yield_ = 0;
    recv_error = srs_success;
    _nb_msgs = 0;
    video_frames = 0;
    error = srs_cond_new();

    req = _req;
    mr_fd = mr_sock_fd;
    
    // the mr settings,
    mr = _srs_config->get_mr_enabled(req->vhost);
    mr_sleep = _srs_config->get_mr_sleep(req->vhost);
    
    realtime = _srs_config->get_realtime_enabled(req->vhost);
    
    _srs_config->subscribe(this);
}

SrsPublishRecvThread::~SrsPublishRecvThread()
{
    _srs_config->unsubscribe(this);
    
    trd.stop();
    srs_cond_destroy(error);
    srs_freep(recv_error);
}

srs_error_t SrsPublishRecvThread::wait(srs_utime_t tm)
{
    if (recv_error != srs_success) {
        return srs_error_copy(recv_error);
    }
    
    // ignore any return of cond wait.
    srs_cond_timedwait(error, tm);
    
    return srs_success;
}

int64_t SrsPublishRecvThread::nb_msgs()
{
    return _nb_msgs;
}

uint64_t SrsPublishRecvThread::nb_video_frames()
{
    return video_frames;
}

srs_error_t SrsPublishRecvThread::error_code()
{
    return srs_error_copy(recv_error);
}

void SrsPublishRecvThread::set_cid(SrsContextId v)
{
    ncid = v;
}

SrsContextId SrsPublishRecvThread::get_cid()
{
    return ncid;
}

srs_error_t SrsPublishRecvThread::start()
{
    srs_error_t err = srs_success;
    
    if ((err = trd.start()) != srs_success) {
        err = srs_error_wrap(err, "publish recv thread");
    }
    
    ncid = cid = trd.cid();

    return err;
}

void SrsPublishRecvThread::stop()
{
    trd.stop();
}

srs_error_t SrsPublishRecvThread::consume(SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    // when cid changed, change it.
    if (ncid.compare(cid)) {
        _srs_context->set_id(ncid);
        cid = ncid;
    }
    
    _nb_msgs++;
    
    if (msg->header.is_video()) {
        video_frames++;
    }
    
    // log to show the time of recv thread.
    srs_verbose("recv thread now=%" PRId64 "us, got msg time=%" PRId64 "ms, size=%d",
                srs_update_system_time(), msg->header.timestamp, msg->size);
    
    // the rtmp connection will handle this message
    err = _conn->handle_publish_message(source_, msg);
    
    // must always free it,
    // the source will copy it if need to use.
    srs_freep(msg);
    
    if (err != srs_success) {
        return srs_error_wrap(err, "handle publish message");
    }

    // Yield to another coroutines.
    // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777463768
    if (++nn_msgs_for_yield_ >= 15) {
        nn_msgs_for_yield_ = 0;
        srs_thread_yield();
    }
    
    return err;
}

bool SrsPublishRecvThread::interrupted()
{
    // Never interrupted, always can handle message.
    return false;
}

void SrsPublishRecvThread::interrupt(srs_error_t err)
{
    srs_freep(recv_error);
    recv_error = srs_error_copy(err);
    
    // when recv thread error, signal the conn thread to process it.
    srs_cond_signal(error);
}

void SrsPublishRecvThread::on_start()
{
    // we donot set the auto response to false,
    // for the main thread never send message.
    
#ifdef SRS_PERF_MERGED_READ
    if (mr) {
        // set underlayer buffer size
        set_socket_buffer(mr_sleep);
        
        // disable the merge read
        rtmp->set_merge_read(true, this);
    }
#endif
}

void SrsPublishRecvThread::on_stop()
{
    // we donot set the auto response to true,
    // for we donot set to false yet.
    
    // when thread stop, signal the conn thread which wait.
    srs_cond_signal(error);
    
#ifdef SRS_PERF_MERGED_READ
    if (mr) {
        // disable the merge read
        rtmp->set_merge_read(false, NULL);
    }
#endif
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
     */
    if (nread < SRS_MR_SMALL_BYTES) {
        srs_usleep(mr_sleep);
    }
}
#endif

srs_error_t SrsPublishRecvThread::on_reload_vhost_publish(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    // the mr settings,
    bool mr_enabled = _srs_config->get_mr_enabled(req->vhost);
    srs_utime_t sleep_v = _srs_config->get_mr_sleep(req->vhost);
    
    // update buffer when sleep ms changed.
    if (mr_sleep != sleep_v) {
        set_socket_buffer(sleep_v);
    }
    
#ifdef SRS_PERF_MERGED_READ
    // mr enabled=>disabled
    if (mr && !mr_enabled) {
        // disable the merge read
        rtmp->set_merge_read(false, NULL);
    }
    // mr disabled=>enabled
    if (!mr && mr_enabled) {
        // enable the merge read
        rtmp->set_merge_read(true, this);
    }
#endif
    
    // update to new state
    mr = mr_enabled;
    mr_sleep = sleep_v;
    
    return err;
}

srs_error_t SrsPublishRecvThread::on_reload_vhost_realtime(string vhost)
{
    srs_error_t err = srs_success;
    
    if (req->vhost != vhost) {
        return err;
    }
    
    bool realtime_enabled = _srs_config->get_realtime_enabled(req->vhost);
    srs_trace("realtime changed %d=>%d", realtime, realtime_enabled);
    realtime = realtime_enabled;
    
    return err;
}

void SrsPublishRecvThread::set_socket_buffer(srs_utime_t sleep_v)
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
    int socket_buffer_size = srsu2msi(sleep_v) * kbps / 8;
    
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
              srsu2msi(mr_sleep), srsu2msi(sleep_v), socket_buffer_size, onb_rbuf, nb_rbuf,
              SRS_MR_SMALL_BYTES, realtime);
    
    rtmp->set_recv_buffer(nb_rbuf);
}

SrsHttpRecvThread::SrsHttpRecvThread(SrsHttpxConn* c)
{
    conn = c;
    trd = new SrsSTCoroutine("http-receive", this, _srs_context->get_id());
}

SrsHttpRecvThread::~SrsHttpRecvThread()
{
    srs_freep(trd);
}

srs_error_t SrsHttpRecvThread::start()
{
    srs_error_t err = srs_success;
    
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "http recv thread");
    }
    
    return err;
}

srs_error_t SrsHttpRecvThread::pull()
{
    return trd->pull();
}

srs_error_t SrsHttpRecvThread::cycle()
{
    srs_error_t err = srs_success;
    
    while ((err = trd->pull()) == srs_success) {
        // Ignore any received messages.
        if ((err = conn->pop_message(NULL)) != srs_success) {
            return srs_error_wrap(err, "pop message");
        }
    }
    
    return err;
}

