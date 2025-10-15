//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_recv_thread.hpp>

#include <srs_app_config.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>

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

ISrsRecvThread::ISrsRecvThread()
{
}

ISrsRecvThread::~ISrsRecvThread()
{
}

SrsRecvThread::SrsRecvThread(ISrsMessagePumper *p, ISrsRtmpServer *r, srs_utime_t tm, SrsContextId parent_cid)
{
    rtmp_ = r;
    pumper_ = p;
    timeout_ = tm;
    parent_cid_ = parent_cid;
    trd_ = new SrsDummyCoroutine();
}

SrsRecvThread::~SrsRecvThread()
{
    srs_freep(trd_);
}

SrsContextId SrsRecvThread::cid()
{
    return trd_->cid();
}

srs_error_t SrsRecvThread::start()
{
    srs_error_t err = srs_success;

    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("recv", this, parent_cid_);

    // change stack size to 256K, fix crash when call some 3rd-part api.
    ((SrsSTCoroutine *)trd_)->set_stack_size(1 << 18);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "recv thread");
    }

    return err;
}

void SrsRecvThread::stop()
{
    trd_->stop();
}

void SrsRecvThread::stop_loop()
{
    trd_->interrupt();
}

srs_error_t SrsRecvThread::cycle()
{
    srs_error_t err = srs_success;

    // the multiple messages writev improve performance large,
    // but the timeout recv will cause 33% sys call performance,
    // to use isolate thread to recv, can improve about 33% performance.
    rtmp_->set_recv_timeout(SRS_UTIME_NO_TIMEOUT);

    pumper_->on_start();

    if ((err = do_cycle()) != srs_success) {
        err = srs_error_wrap(err, "recv thread");
    }

    // reset the timeout to pulse mode.
    rtmp_->set_recv_timeout(timeout_);

    pumper_->on_stop();

    return err;
}

srs_error_t SrsRecvThread::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "recv thread");
        }

        // When the pumper is interrupted, wait then retry.
        if (pumper_->interrupted()) {
            srs_usleep(timeout_);
            continue;
        }

        SrsRtmpCommonMessage *msg = NULL;

        // Process the received message.
        if ((err = rtmp_->recv_message(&msg)) == srs_success) {
            err = pumper_->consume(msg);
        }

        if (err != srs_success) {
            // Interrupt the receive thread for any error.
            trd_->interrupt();

            // Notify the pumper to quit for error.
            pumper_->interrupt(err);

            return srs_error_wrap(err, "recv thread");
        }
    }

    return err;
}

ISrsQueueRecvThread::ISrsQueueRecvThread()
{
}

ISrsQueueRecvThread::~ISrsQueueRecvThread()
{
}

SrsQueueRecvThread::SrsQueueRecvThread(SrsLiveConsumer *consumer, ISrsRtmpServer *rtmp_sdk, srs_utime_t tm, SrsContextId parent_cid)
{
    consumer_ = consumer;
    rtmp_ = rtmp_sdk;
    recv_error_ = srs_success;
    trd_ = new SrsRecvThread(this, rtmp_sdk, tm, parent_cid);
}

SrsQueueRecvThread::~SrsQueueRecvThread()
{
    stop();

    // clear all messages.
    std::vector<SrsRtmpCommonMessage *>::iterator it;
    for (it = queue_.begin(); it != queue_.end(); ++it) {
        SrsRtmpCommonMessage *msg = *it;
        srs_freep(msg);
    }
    queue_.clear();

    srs_freep(recv_error_);
    srs_freep(trd_);
}

srs_error_t SrsQueueRecvThread::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "queue recv thread");
    }

    return err;
}

void SrsQueueRecvThread::stop()
{
    trd_->stop();
}

bool SrsQueueRecvThread::empty()
{
    return queue_.empty();
}

int SrsQueueRecvThread::size()
{
    return (int)queue_.size();
}

SrsRtmpCommonMessage *SrsQueueRecvThread::pump()
{
    srs_assert(!queue_.empty());

    SrsRtmpCommonMessage *msg = *queue_.begin();

    queue_.erase(queue_.begin());

    return msg;
}

srs_error_t SrsQueueRecvThread::error_code()
{
    return srs_error_copy(recv_error_);
}

srs_error_t SrsQueueRecvThread::consume(SrsRtmpCommonMessage *msg)
{
    // put into queue, the send thread will get and process it,
    // @see SrsRtmpConn::process_play_control_msg
    queue_.push_back(msg);
#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (consumer_) {
        consumer_->wakeup();
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
    srs_freep(recv_error_);
    recv_error_ = srs_error_copy(err);

#ifdef SRS_PERF_QUEUE_COND_WAIT
    if (consumer_) {
        consumer_->wakeup();
    }
#endif
}

void SrsQueueRecvThread::on_start()
{
    // disable the protocol auto response,
    // for the isolate recv thread should never send any messages.
    rtmp_->set_auto_response(false);
}

void SrsQueueRecvThread::on_stop()
{
    // enable the protocol auto response,
    // for the isolate recv thread terminated.
    rtmp_->set_auto_response(true);
}

ISrsPublishRecvThread::ISrsPublishRecvThread()
{
}

ISrsPublishRecvThread::~ISrsPublishRecvThread()
{
}

SrsPublishRecvThread::SrsPublishRecvThread(ISrsRtmpServer *rtmp_sdk, ISrsRequest *_req,
                                           int mr_sock_fd, srs_utime_t tm, SrsRtmpConn *conn, SrsSharedPtr<SrsLiveSource> source, SrsContextId parent_cid)
{
    rtmp_ = rtmp_sdk;

    conn_ = conn;
    source_ = source;

    nn_msgs_for_yield_ = 0;
    recv_error_ = srs_success;
    nb_msgs_ = 0;
    video_frames_ = 0;
    error_ = srs_cond_new();

    req_ = _req;
    mr_fd_ = mr_sock_fd;

    trd_ = new SrsRecvThread(this, rtmp_sdk, tm, parent_cid);

    config_ = _srs_config;
}

void SrsPublishRecvThread::assemble()
{
    mr_ = config_->get_mr_enabled(req_->vhost_);
    mr_sleep_ = config_->get_mr_sleep(req_->vhost_);
    realtime_ = config_->get_realtime_enabled(req_->vhost_, false);
}

SrsPublishRecvThread::~SrsPublishRecvThread()
{
    trd_->stop();

    srs_cond_destroy(error_);
    srs_freep(recv_error_);
    srs_freep(trd_);

    config_ = NULL;
}

srs_error_t SrsPublishRecvThread::wait(srs_utime_t tm)
{
    if (recv_error_ != srs_success) {
        return srs_error_copy(recv_error_);
    }

    // ignore any return of cond wait.
    srs_cond_timedwait(error_, tm);

    return srs_success;
}

int64_t SrsPublishRecvThread::nb_msgs()
{
    return nb_msgs_;
}

uint64_t SrsPublishRecvThread::nb_video_frames()
{
    return video_frames_;
}

srs_error_t SrsPublishRecvThread::error_code()
{
    return srs_error_copy(recv_error_);
}

void SrsPublishRecvThread::set_cid(SrsContextId v)
{
    ncid_ = v;
}

SrsContextId SrsPublishRecvThread::get_cid()
{
    return ncid_;
}

srs_error_t SrsPublishRecvThread::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        err = srs_error_wrap(err, "publish recv thread");
    }

    ncid_ = cid_ = trd_->cid();

    return err;
}

void SrsPublishRecvThread::stop()
{
    trd_->stop();
}

srs_error_t SrsPublishRecvThread::consume(SrsRtmpCommonMessage *msg)
{
    srs_error_t err = srs_success;

    // when cid changed, change it.
    if (ncid_.compare(cid_)) {
        _srs_context->set_id(ncid_);
        cid_ = ncid_;
    }

    nb_msgs_++;

    if (msg->header_.is_video()) {
        video_frames_++;
    }

    // log to show the time of recv thread.
    srs_verbose("recv thread now=%" PRId64 "us, got msg time=%" PRId64 "ms, size=%d",
                srs_time_now_realtime(), msg->header_.timestamp, msg->size);

    // the rtmp connection will handle this message
    err = conn_->handle_publish_message(source_, msg);

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
    srs_freep(recv_error_);
    recv_error_ = srs_error_copy(err);

    // when recv thread error, signal the conn thread to process it.
    srs_cond_signal(error_);
}

void SrsPublishRecvThread::on_start()
{
    // we donot set the auto response to false,
    // for the main thread never send message.

#ifdef SRS_PERF_MERGED_READ
    if (mr_) {
        // set underlayer buffer size
        set_socket_buffer(mr_sleep_);

        // disable the merge read
        rtmp_->set_merge_read(true, this);
    }
#endif
}

void SrsPublishRecvThread::on_stop()
{
    // we donot set the auto response to true,
    // for we donot set to false yet.

    // when thread stop, signal the conn thread which wait.
    srs_cond_signal(error_);

#ifdef SRS_PERF_MERGED_READ
    if (mr_) {
        // disable the merge read
        rtmp_->set_merge_read(false, NULL);
    }
#endif
}

#ifdef SRS_PERF_MERGED_READ
void SrsPublishRecvThread::on_read(ssize_t nread)
{
    if (!mr_ || realtime_) {
        return;
    }

    if (nread < 0 || mr_sleep_ <= 0) {
        return;
    }

    /**
     * to improve read performance, merge some packets then read,
     * when it on and read small bytes, we sleep to wait more data.,
     * that is, we merge some data to read together.
     */
    if (nread < SRS_MR_SMALL_BYTES) {
        srs_usleep(mr_sleep_);
    }
}
#endif

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

    int fd = mr_fd_;
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
              srsu2msi(mr_sleep_), srsu2msi(sleep_v), socket_buffer_size, onb_rbuf, nb_rbuf,
              SRS_MR_SMALL_BYTES, realtime_);

    rtmp_->set_recv_buffer(nb_rbuf);
}

ISrsHttpRecvThread::ISrsHttpRecvThread()
{
}

ISrsHttpRecvThread::~ISrsHttpRecvThread()
{
}

SrsHttpRecvThread::SrsHttpRecvThread(SrsHttpxConn *c)
{
    conn_ = c;
    trd_ = new SrsSTCoroutine("http-receive", this, _srs_context->get_id());
}

SrsHttpRecvThread::~SrsHttpRecvThread()
{
    srs_freep(trd_);
}

srs_error_t SrsHttpRecvThread::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "http recv thread");
    }

    return err;
}

srs_error_t SrsHttpRecvThread::pull()
{
    return trd_->pull();
}

srs_error_t SrsHttpRecvThread::cycle()
{
    srs_error_t err = srs_success;

    while ((err = trd_->pull()) == srs_success) {
        // Ignore any received messages.
        if ((err = conn_->pop_message(NULL)) != srs_success) {
            return srs_error_wrap(err, "pop message");
        }
    }

    return err;
}
