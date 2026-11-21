//
// Copyright (c) 2013-2026 The SRS Authors
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
class ISrsAppFactory;
class ISrsContext;

class ISrsMessageConsumer
{
public:
    ISrsMessageConsumer();
    virtual ~ISrsMessageConsumer();

public:
    // The consumer owns the message and must free it.
    virtual srs_error_t consume(SrsRtmpCommonMessage *msg) = 0;
};

class ISrsMessagePumper : public ISrsMessageConsumer
{
public:
    ISrsMessagePumper();
    virtual ~ISrsMessagePumper();

public:
    virtual bool interrupted() = 0;
    virtual void interrupt(srs_error_t error) = 0;
    virtual void on_start() = 0;
    virtual void on_stop() = 0;
};

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

class SrsRecvThread : public ISrsRecvThread
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsCoroutine *trd_;
    ISrsMessagePumper *pumper_;
    ISrsRtmpServer *rtmp_;
    SrsContextId parent_cid_;
    srs_utime_t timeout_;

public:
    SrsRecvThread(ISrsMessagePumper *p, ISrsRtmpServer *r, srs_utime_t tm, SrsContextId parent_cid);
    virtual ~SrsRecvThread();

public:
    virtual SrsContextId cid();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void stop_loop();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
};

class ISrsQueueRecvThread : public ISrsMessagePumper
{
public:
    ISrsQueueRecvThread();
    virtual ~ISrsQueueRecvThread();

public:
};

// Replaces a recv with a timeout while playing, which calls epoll_ctl often and hurts performance.
// @see: https://github.com/ossrs/srs/issues/217
class SrsQueueRecvThread : public ISrsQueueRecvThread
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsRtmpCommonMessage *> queue_;
    ISrsRecvThread *trd_;
    ISrsRtmpServer *rtmp_;
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

public:
    virtual srs_error_t consume(SrsRtmpCommonMessage *msg);
    virtual bool interrupted();
    virtual void interrupt(srs_error_t err);
    virtual void on_start();
    virtual void on_stop();
};

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
    int64_t nb_msgs_;
    uint64_t video_frames_;
    uint64_t audio_frames_;
    bool mr_;
    int mr_fd_;
    srs_utime_t mr_sleep_;
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime_;
    srs_error_t recv_error_;
    SrsRtmpConn *conn_;
    SrsSharedPtr<SrsLiveSource> source_;
    srs_cond_t error_;
    uint64_t reported_video_frames_;
    uint64_t reported_audio_frames_;
    srs_utime_t frames_report_time_;
    SrsContextId cid_;
    SrsContextId ncid_;

public:
    SrsPublishRecvThread(ISrsRtmpServer *rtmp_sdk, ISrsRequest *req,
                         int mr_sock_fd, srs_utime_t tm, SrsRtmpConn *conn, SrsSharedPtr<SrsLiveSource> source, SrsContextId parent_cid);
    void assemble();
    virtual ~SrsPublishRecvThread();

public:
    virtual srs_error_t wait(srs_utime_t tm);
    virtual int64_t nb_msgs();
    virtual uint64_t nb_video_frames();
    virtual uint64_t nb_audio_frames();
    virtual srs_error_t report_frames();
    virtual srs_error_t error_code();
    virtual void set_cid(SrsContextId v);
    virtual SrsContextId get_cid();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual srs_error_t consume(SrsRtmpCommonMessage *msg);
    virtual bool interrupted();
    virtual void interrupt(srs_error_t err);
    virtual void on_start();
    virtual void on_stop();

public:
#ifdef SRS_PERF_MERGED_READ
    virtual void on_read(ssize_t nread);
#endif

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void set_socket_buffer(srs_utime_t sleep_v);
};

class ISrsHttpRecvThread : public ISrsCoroutineHandler
{
public:
    ISrsHttpRecvThread();
    virtual ~ISrsHttpRecvThread();

public:
};

// Reads until EOF, so a streaming response such as HTTP-FLV stops when the client closes, instead
// of leaking the FD.
// @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
class SrsHttpRecvThread : public ISrsHttpRecvThread
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHttpxConn *conn_;
    ISrsCoroutine *trd_;

public:
    SrsHttpRecvThread(SrsHttpxConn *c);
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsHttpRecvThread();

public:
    virtual srs_error_t start();

public:
    virtual srs_error_t pull();

public:
    virtual srs_error_t cycle();
};

#endif
