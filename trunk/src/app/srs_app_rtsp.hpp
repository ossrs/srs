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

class SrsServer;
class SrsTcpConnection;
class SrsRtspConnection;
class SrsNetworkDelta;
class ISrsKbpsDelta;

class SrsRtspConn : public ISrsConnection, public ISrsCoroutineHandler, public ISrsExecutorHandler, public ISrsStartable
{
private:
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
    // The shared resource which own this object, we should never free it because it's managed by shared ptr.
    SrsSharedResource<SrsRtspConn>* wrapper_;
    // The owner coroutine, allow user to interrupt the loop.
    ISrsInterruptable* owner_coroutine_;
    ISrsContextIdSetter* owner_cid_;
    SrsContextId cid_;
public:
    SrsRtspConn();
    SrsRtspConn(ISrsResourceManager* cm, ISrsProtocolReadWriter* skt, std::string cip, int port);
    virtual ~SrsRtspConn();
public:
    // Setup the owner, the wrapper is the shared ptr, the interruptable object is the coroutine, and the cid is the context id.
    void setup_owner(SrsSharedResource<SrsRtspConn>* wrapper, ISrsInterruptable* owner_coroutine, ISrsContextIdSetter* owner_cid);
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
// Interface ISrsExecutorHandler
public:
    virtual void on_executor_done(ISrsInterruptable* executor);
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
    srs_error_t read_packet(char* pkt, int* nb_pkt);
    srs_error_t on_tcp_pkt(char* pkt, int nb_pkt);
};

#endif