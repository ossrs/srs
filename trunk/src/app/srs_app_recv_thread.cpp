//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_recv_thread.hpp>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>

using namespace std;

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

    // Some third-party APIs crash on a smaller stack.
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

    // A recv with a timeout costs about 33% of the performance in system calls.
    rtmp_->set_recv_timeout(SRS_UTIME_NO_TIMEOUT);

    pumper_->on_start();

    if ((err = do_cycle()) != srs_success) {
        err = srs_error_wrap(err, "recv thread");
    }

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

        if (pumper_->interrupted()) {
            srs_usleep(timeout_);
            continue;
        }

        SrsRtmpCommonMessage *msg = NULL;

        if ((err = rtmp_->recv_message(&msg)) == srs_success) {
            err = pumper_->consume(msg);
        }

        if (err != srs_success) {
            trd_->interrupt();

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
    if (!msg) {
        return srs_success;
    }

    // The play coroutine pumps and handles it, see SrsRtmpConn::process_play_control_msg.
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
    // Receive one message at a time: handling it may stop and free this thread, which drops the
    // messages still queued.
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
    // The receive coroutine must never send messages.
    rtmp_->set_auto_response(false);
}

void SrsQueueRecvThread::on_stop()
{
    rtmp_->set_auto_response(true);
}

ISrsPublishRecvThread::ISrsPublishRecvThread()
{
}

ISrsPublishRecvThread::~ISrsPublishRecvThread()
{
}

SrsPublishRecvThread::SrsPublishRecvThread(ISrsRtmpServer *rtmp_sdk, ISrsRequest *req,
                                           int mr_sock_fd, srs_utime_t tm, SrsRtmpConn *conn, SrsSharedPtr<SrsLiveSource> source, SrsContextId parent_cid)
{
    rtmp_ = rtmp_sdk;

    conn_ = conn;
    source_ = source;

    nn_msgs_for_yield_ = 0;
    recv_error_ = srs_success;
    nb_msgs_ = 0;
    video_frames_ = 0;
    audio_frames_ = 0;
    error_ = srs_cond_new();
    reported_video_frames_ = 0;
    reported_audio_frames_ = 0;
    frames_report_time_ = 0;

    req_ = req;
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

uint64_t SrsPublishRecvThread::nb_audio_frames()
{
    return audio_frames_;
}

srs_error_t SrsPublishRecvThread::report_frames()
{
    srs_error_t err = srs_success;

    ISrsStatistic *stat = conn_->stat_;

    // @remark https://github.com/ossrs/srs/issues/851
    if ((err = stat->on_video_frames(req_, (int)(video_frames_ - reported_video_frames_))) != srs_success) {
        return srs_error_wrap(err, "stat video frames");
    }
    reported_video_frames_ = video_frames_;

    if ((err = stat->on_audio_frames(req_, (int)(audio_frames_ - reported_audio_frames_))) != srs_success) {
        return srs_error_wrap(err, "stat audio frames");
    }
    reported_audio_frames_ = audio_frames_;

    return err;
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

    if (ncid_.compare(cid_)) {
        _srs_context->set_id(ncid_);
        cid_ = ncid_;
    }

    nb_msgs_++;

    if (msg->header_.is_video()) {
        video_frames_++;
    } else if (msg->header_.is_audio()) {
        audio_frames_++;
    }

    srs_verbose("recv thread now=%" PRId64 "us, got msg time=%" PRId64 "ms, size=%d",
                srs_time_now_realtime(), msg->header_.timestamp, msg->size);

    err = conn_->handle_publish_message(source_, msg);

    // The source shares the payload it keeps.
    srs_freep(msg);

    if (err != srs_success) {
        return srs_error_wrap(err, "handle publish message");
    }

    // The publishing loop reports only after its wait times out.
    srs_utime_t now = srs_time_now_cached();
    if (now - frames_report_time_ >= SRS_UTIME_SECONDS) {
        frames_report_time_ = now;
        if ((err = report_frames()) != srs_success) {
            return srs_error_wrap(err, "report frames");
        }
    }

    // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777463768
    if (++nn_msgs_for_yield_ >= 15) {
        nn_msgs_for_yield_ = 0;
        srs_thread_yield();
    }

    return err;
}

bool SrsPublishRecvThread::interrupted()
{
    return false;
}

void SrsPublishRecvThread::interrupt(srs_error_t err)
{
    srs_freep(recv_error_);
    recv_error_ = srs_error_copy(err);

    srs_cond_signal(error_);
}

void SrsPublishRecvThread::on_start()
{
    // Auto response stays on, since the main thread never sends messages.

#ifdef SRS_PERF_MERGED_READ
    if (mr_) {
        set_socket_buffer(mr_sleep_);

        rtmp_->set_merge_read(true, this);
    }
#endif
}

void SrsPublishRecvThread::on_stop()
{
    srs_cond_signal(error_);

#ifdef SRS_PERF_MERGED_READ
    if (mr_) {
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

    // Sleep after a small read, so more packets arrive and are read together in fewer reads.
    if (nread < SRS_MR_SMALL_BYTES) {
        srs_usleep(mr_sleep_);
    }
}
#endif

void SrsPublishRecvThread::set_socket_buffer(srs_utime_t sleep_v)
{
    // The buffer holds what a stream sends during one sleep, sleep(ms)*kbps/8 bytes, such as
    // 800*1000/8=100000B for a 1000kbps stream and an 800ms sleep.
    int kbps = 5000;
    int socket_buffer_size = srsu2msi(sleep_v) * kbps / 8;

    int fd = mr_fd_;
    int onb_rbuf = 0;
    socklen_t sock_buf_size = sizeof(int);
    srs_getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &onb_rbuf, &sock_buf_size);

    // The system doubles the SO_RCVBUF value it is given.
    int nb_rbuf = socket_buffer_size / 2;
    if (srs_setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, sock_buf_size) < 0) {
        srs_warn("set sock SO_RCVBUF=%d failed.", nb_rbuf);
    }
    srs_getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &nb_rbuf, &sock_buf_size);

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
    trd_ = NULL;

    app_factory_ = _srs_app_factory;
    context_ = _srs_context;
}

void SrsHttpRecvThread::assemble()
{
    trd_ = app_factory_->create_coroutine("http-receive", this, context_->get_id());
}

SrsHttpRecvThread::~SrsHttpRecvThread()
{
    srs_freep(trd_);

    app_factory_ = NULL;
    context_ = NULL;
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
        if ((err = conn_->pop_message(NULL)) != srs_success) {
            return srs_error_wrap(err, "pop message");
        }
    }

    return err;
}
