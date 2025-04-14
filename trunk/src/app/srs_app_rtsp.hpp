//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_HPP
#define SRS_APP_RTSP_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_protocol_rtsp_stack.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_async_call.hpp>
#include <srs_app_rtc_conn.hpp>

class SrsServer;
class SrsTcpConnection;
class SrsRtspConnection;
class SrsNetworkDelta;
class ISrsKbpsDelta;
class SrsRtcPlayStream;
class SrsRtcServer;
class SrsRtcSource;

class SrsRtspConn : public SrsRtcConnection, public ISrsCoroutineHandler, public ISrsStartable
{
private:
    SrsRequest* request_;
    SrsSharedPtr<SrsRtcSource> source_;
    bool disposing_;
private:
    std::map<uint32_t, SrsRtcTrackDescription*> sub_relations_;
    // key: stream id
    std::map<std::string, SrsRtcPlayStream*> players_;
    std::string session_;
private:
    // The manager object to manage the connection.
    ISrsResourceManager* manager_;
    // The ip and port of client.
    std::string ip_;
    int port_;
    // The delta for statistic.
    SrsNetworkDelta* delta_;
    ISrsProtocolReadWriter* skt_;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    SrsCoroutine* trd_;
    // Packet cache.
    char* pkt_;
    SrsRtspStack* rtsp_;
private:
    SrsContextId cid_;
public:
    SrsRtspConn(ISrsResourceManager* cm, ISrsProtocolReadWriter* skt, std::string cip, int port);
    virtual ~SrsRtspConn();
// interface ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource* c);
    virtual void on_disposing(ISrsResource* c);
public:
    ISrsKbpsDelta* delta();
    // Interrupt transport by session.
    void interrupt();
// Interface ISrsResource.
public:
    virtual std::string desc();
    virtual const SrsContextId& get_id();
// Interface ISrsConnection.
public:
    virtual std::string remote_ip();
// Interface ISrsStartable
public:
    // Start the client green thread.
    // when server get a client from listener,
    // 1. server will create an concrete connection(for instance, RTMP connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the on_thread_stop(), which will use server
    // To remove the client by server->remove(this).
    virtual srs_error_t start();
// Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
private:
    virtual srs_error_t do_cycle();
};


// class SrsRtspPlayStream : public ISrsCoroutineHandler
// {
// private:
//     SrsContextId cid_;
//     SrsFastCoroutine* trd_;
//     SrsRtspConn* conn_;
//     bool is_started_;
// public:
//     SrsRtspPlayStream(SrsRtspConn* conn, const SrsContextId& cid);
//     virtual ~SrsRtspPlayStream();
// public:
//     virtual srs_error_t start();
//     virtual void stop();
//     virtual srs_error_t cycle();
//     virtual srs_error_t initialize(SrsRequest* request);
// };

#endif