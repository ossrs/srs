//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RECV_THREAD_HPP
#define SRS_APP_RECV_THREAD_HPP

#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_st.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_core_performance.hpp>
#include <srs_app_reload.hpp>
#include <srs_core_autofree.hpp>

class SrsRtmpServer;
class SrsCommonMessage;
class SrsRtmpConn;
class SrsLiveSource;
class SrsRequest;
class SrsLiveConsumer;
class SrsHttpConn;
class SrsHttpxConn;

// The message consumer which consume a message.
class ISrsMessageConsumer
{
public:
    ISrsMessageConsumer();
    virtual ~ISrsMessageConsumer();
public:
    // Consume the received message.
    // @remark user must free this message.
    virtual srs_error_t consume(SrsCommonMessage* msg) = 0;
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

// The recv thread, use message handler to handle each received message.
class SrsRecvThread : public ISrsCoroutineHandler
{
protected:
    SrsCoroutine* trd;
    ISrsMessagePumper* pumper;
    SrsRtmpServer* rtmp;
    SrsContextId _parent_cid;
    // The recv timeout in srs_utime_t.
    srs_utime_t timeout;
public:
    // Constructor.
    // @param tm The receive timeout in srs_utime_t.
    SrsRecvThread(ISrsMessagePumper* p, SrsRtmpServer* r, srs_utime_t tm, SrsContextId parent_cid);
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
private:
    virtual srs_error_t do_cycle();
};

// The recv thread used to replace the timeout recv,
// which hurt performance for the epoll_ctrl is frequently used.
// @see: SrsRtmpConn::playing
// @see: https://github.com/ossrs/srs/issues/217
class SrsQueueRecvThread : public ISrsMessagePumper
{
private:
    std::vector<SrsCommonMessage*> queue;
    SrsRecvThread trd;
    SrsRtmpServer* rtmp;
    // The recv thread error code.
    srs_error_t recv_error;
    SrsLiveConsumer* _consumer;
public:
	// TODO: FIXME: Refine timeout in time unit.
    SrsQueueRecvThread(SrsLiveConsumer* consumer, SrsRtmpServer* rtmp_sdk, srs_utime_t tm, SrsContextId parent_cid);
    virtual ~SrsQueueRecvThread();
public:
    virtual srs_error_t start();
    virtual void stop();
public:
    virtual bool empty();
    virtual int size();
    virtual SrsCommonMessage* pump();
    virtual srs_error_t error_code();
// Interface ISrsMessagePumper
public:
    virtual srs_error_t consume(SrsCommonMessage* msg);
    virtual bool interrupted();
    virtual void interrupt(srs_error_t err);
    virtual void on_start();
    virtual void on_stop();
};

// The publish recv thread got message and callback the source method to process message.
// @see: https://github.com/ossrs/srs/issues/237
class SrsPublishRecvThread : public ISrsMessagePumper, public ISrsReloadHandler
#ifdef SRS_PERF_MERGED_READ
    , public IMergeReadHandler
#endif
{
private:
    uint32_t nn_msgs_for_yield_;
    SrsRecvThread trd;
    SrsRtmpServer* rtmp;
    SrsRequest* req;
    // The msgs already got.
    int64_t _nb_msgs;
    // The video frames we got.
    uint64_t video_frames;
    // For mr(merged read),
    bool mr;
    int mr_fd;
    srs_utime_t mr_sleep;
    // For realtime
    // @see https://github.com/ossrs/srs/issues/257
    bool realtime;
    // The recv thread error code.
    srs_error_t recv_error;
    SrsRtmpConn* _conn;
    // The params for conn callback.
    SrsSharedPtr<SrsLiveSource> source_;
    // The error timeout cond
    srs_cond_t error;
    // The merged context id.
    SrsContextId cid;
    SrsContextId ncid;
public:
    SrsPublishRecvThread(SrsRtmpServer* rtmp_sdk, SrsRequest* _req,
        int mr_sock_fd, srs_utime_t tm, SrsRtmpConn* conn, SrsSharedPtr<SrsLiveSource> source, SrsContextId parent_cid);
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
    virtual srs_error_t consume(SrsCommonMessage* msg);
    virtual bool interrupted();
    virtual void interrupt(srs_error_t err);
    virtual void on_start();
    virtual void on_stop();
// Interface IMergeReadHandler
public:
#ifdef SRS_PERF_MERGED_READ
    virtual void on_read(ssize_t nread);
#endif
// Interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_publish(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
private:
    virtual void set_socket_buffer(srs_utime_t sleep_v);
};

// The HTTP receive thread, try to read messages util EOF.
// For example, the HTTP FLV serving thread will use the receive thread to break
// when client closed the request, to avoid FD leak.
// @see https://github.com/ossrs/srs/issues/636#issuecomment-298208427
class SrsHttpRecvThread : public ISrsCoroutineHandler
{
private:
    SrsHttpxConn* conn;
    SrsCoroutine* trd;
public:
    SrsHttpRecvThread(SrsHttpxConn* c);
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

