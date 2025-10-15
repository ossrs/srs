//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RECV_THREAD_HPP
#define SRS_APP_RECV_THREAD_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_performance.hpp>
#include <srs_protocol_stream.hpp>

class SrsRtmpServer;
class SrsRtmpCommonMessage;
class SrsRtmpConn;
class SrsLiveSource;
class ISrsRequest;
class SrsLiveConsumer;
class SrsHttpConn;
class SrsHttpxConn;
class ISrsRtmpServer;
class SrsRecvThread;
class ISrsRecvThread;
class ISrsAppConfig;

// The message consumer which consume a message.
class ISrsMessageConsumer
{
public:
    ISrsMessageConsumer();
    virtual ~ISrsMessageConsumer();

public:
    // Consume the received message.
    // @remark user must free this message.
    virtual srs_error_t consume(SrsRtmpCommonMessage *msg) = 0;
};

// The message pumper to pump messages to processer.
class ISrsMessagePumper : public ISrsMessageConsumer
{
public:
    ISrsMessagePumper();
    virtual ~ISrsMessagePumper();

public:
    // Whether the pumper is interrupted.
    // For example, when pumpter is busy, it's interrupted,
    // please wait for a while then try to feed the pumper.
    virtual bool interrupted() = 0;
    // Interrupt the pumper for a error.
    virtual void interrupt(srs_error_t error) = 0;
    // When start the pumper.
    virtual void on_start() = 0;
    // When stop the pumper.
    virtual void on_stop() = 0;
};

// The recv thread interface.
class ISrsRecvThread : public ISrsCoroutineHandler
{
public:
    ISrsRecvThread();
    virtual ~ISrsRecvThread();

public:
    virtual SrsContextId cid() = 0;
    virtual srs_error_t start() = 0;
    virtual void stop() = 0;
};

// The recv thread, use message handler to handle each received message.
class SrsRecvThread : public ISrsRecvThread
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsCoroutine *trd_;
    ISrsMessagePumper *pumper_;
    ISrsRtmpServer *rtmp_;
    SrsContextId parent_cid_;
    // The recv timeout in srs_utime_t.
    srs_utime_t timeout_;

public:
    // Constructor.
    // @param tm The receive timeout in srs_utime_t.
    SrsRecvThread(ISrsMessagePumper *p, ISrsRtmpServer *r, srs_utime_t tm, SrsContextId parent_cid);
    virtual ~SrsRecvThread();

public:
    virtual SrsContextId cid();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void stop_loop();
    // Interface ISrsReusableThread2Handler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
};

// The queue recv thread interface.
class ISrsQueueRecvThread : public ISrsMessagePumper
{
public:
    ISrsQueueRecvThread();
    virtual ~ISrsQueueRecvThread();

public:
};

// The recv thread used to replace the timeout recv,
// which hurt performance for the epoll_ctrl is frequently used.
// @see: SrsRtmpConn::playing
// @see: https://github.com/ossrs/srs/issues/217
class SrsQueueRecvThread : public ISrsQueueRecvThread
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsRtmpCommonMessage *> queue_;
    ISrsRecvThread *trd_;
    ISrsRtmpServer *rtmp_;
    // The recv thread error code.
    srs_error_t recv_error_;
    SrsLiveConsumer *consumer_;

public:
    // TODO: FIXME: Refine timeout in time unit.
    SrsQueueRecvThread(SrsLiveConsumer *consumer, ISrsRtmpServer *rtmp_sdk, srs_utime_t tm, SrsContextId parent_cid);
    virtual ~SrsQueueRecvThread();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual bool empty();
    virtual int size();
    virtual SrsRtmpCommonMessage *pump();
    virtual srs_error_t error_code();
    // Interface ISrsMessagePumper
public:
    virtual srs_error_t consume(SrsRtmpCommonMessage *msg);
    virtual bool interrupted();
    virtual void interrupt(srs_error_t err);
    virtual void on_start();
    virtual void on_stop();
};

// The publish recv thread interface.
class ISrsPublishRecvThread : public ISrsMessagePumper,
#ifdef SRS_PERF_MERGED_READ
                              public IMergeReadHandler
#endif
{
public:
    ISrsPublishRecvThread();
    virtual ~ISrsPublishRecvThread();

public:
};

// The publish recv thread got message and callback the source method to process message.
// @see: https://github.com/ossrs/srs/issues/237
class SrsPublishRecvThread : public ISrsPublishRecvThread
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t nn_msgs_for_yield_;
    ISrsRecvThread *trd_;
    ISrsRtmpServer *rtmp_;
    ISrsRequest *req_;
    // The msgs already got.
    int64_t nb_msgs_;
    // The video frames we got.
    uint64_t video_frames_;
    // For mr(merged read),
    bool mr_;
    int mr_fd_;
    srs_utime_t mr_sleep_;
    // For realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime_;
    // The recv thread error code.
    srs_error_t recv_error_;
    SrsRtmpConn *conn_;
    // The params for conn callback.
    SrsSharedPtr<SrsLiveSource> source_;
    // The error timeout cond
    srs_cond_t error_;
    // The merged context id.
    SrsContextId cid_;
    SrsContextId ncid_;

public:
    SrsPublishRecvThread(ISrsRtmpServer *rtmp_sdk, ISrsRequest *_req,
                         int mr_sock_fd, srs_utime_t tm, SrsRtmpConn *conn, SrsSharedPtr<SrsLiveSource> source, SrsContextId parent_cid);
    void assemble();
    virtual ~SrsPublishRecvThread();

public:
    // Wait for error for some timeout.
    virtual srs_error_t wait(srs_utime_t tm);
    virtual int64_t nb_msgs();
    virtual uint64_t nb_video_frames();
    virtual srs_error_t error_code();
    virtual void set_cid(SrsContextId v);
    virtual SrsContextId get_cid();

public:
    virtual srs_error_t start();
    virtual void stop();
    // Interface ISrsMessagePumper
public:
    virtual srs_error_t consume(SrsRtmpCommonMessage *msg);
    virtual bool interrupted();
    virtual void interrupt(srs_error_t err);
    virtual void on_start();
    virtual void on_stop();
    // Interface IMergeReadHandler
public:
#ifdef SRS_PERF_MERGED_READ
    virtual void on_read(ssize_t nread);
#endif

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void set_socket_buffer(srs_utime_t sleep_v);
};

// The HTTP receive thread interface.
class ISrsHttpRecvThread : public ISrsCoroutineHandler
{
public:
    ISrsHttpRecvThread();
    virtual ~ISrsHttpRecvThread();

public:
};

// The HTTP receive thread, try to read messages util EOF.
// For example, the HTTP FLV serving thread will use the receive thread to break
// when client closed the request, to avoid FD leak.
// @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
class SrsHttpRecvThread : public ISrsHttpRecvThread
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHttpxConn *conn_;
    ISrsCoroutine *trd_;

public:
    SrsHttpRecvThread(SrsHttpxConn *c);
    virtual ~SrsHttpRecvThread();

public:
    virtual srs_error_t start();

public:
    virtual srs_error_t pull();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
};

#endif
