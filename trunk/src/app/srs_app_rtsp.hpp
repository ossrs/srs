//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_HPP
#define SRS_APP_RTSP_HPP

#include <srs_core.hpp>
#include <srs_protocol_rtsp_stack.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtsp_source.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_async_call.hpp>
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_security.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_kernel_io.hpp>

class SrsEphemeralDelta;
class SrsRtspPlayStream;
class SrsRtcSource;
class SrsRtspSource;
class SrsRtspConnection;

class SrsRtspSession
{
private:
    SrsContextId cid_;
    SrsRequest* request_;
    SrsSharedPtr<SrsRtspSource> source_;
    SrsEphemeralDelta* delta_;
    ISrsProtocolReadWriter* skt_;
    // The ip of client.
    std::string ip_;
    int port_;

    SrsSecurity* security_; 

    iovec* cache_iov_;
    SrsBuffer* cache_buffer_;
private:
    // key: ssrc
    std::map<uint32_t, SrsRtcTrackDescription*> tracks_;
    // key: ssrc
    std::map<uint32_t, ISrsStreamWriter*> networks_;
    SrsRtspPlayStream* player_;

public:
    SrsRtspSession(SrsContextId cid, SrsRequest* r, ISrsProtocolReadWriter* skt, std::string ip, int port);
    virtual ~SrsRtspSession();   
public:
    ISrsKbpsDelta* delta();
public:
    virtual srs_error_t do_send_packet(SrsRtpPacket* pkt);

    virtual srs_error_t do_describe(SrsRtspRequest* req, std::string& sdp);
    virtual srs_error_t do_setup(SrsRtspRequest* req, uint32_t* ssrc);
    virtual srs_error_t do_play(SrsRtspRequest* req, SrsRtspConnection* conn);
    virtual srs_error_t do_teardown();
private:
    srs_error_t http_hooks_on_play(SrsRequest* req);
    srs_error_t get_ssrc_by_stream_id(uint32_t stream_id, uint32_t* ssrc);
};

class SrsRtspConnection : public SrsRtspConnection2, public ISrsCoroutineHandler, public ISrsStartable
{
private:
    SrsContextId cid_;
    SrsRequest* request_;
    // The manager object to manage the connection.
    ISrsResourceManager* manager_;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    SrsCoroutine* trd_;
private:
    // The ip and port of client.
    std::string ip_;
    int port_;
    SrsRtspStack* rtsp_;
    SrsRtspSession* session_;
    std::string session_id_;
public:
    SrsRtspConnection(ISrsResourceManager* cm, ISrsProtocolReadWriter* skt, std::string cip, int port);
    virtual ~SrsRtspConnection();
public:
    virtual srs_error_t do_send_packet(SrsRtpPacket* pkt);
public:
    ISrsKbpsDelta* delta();
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
// Interface ISrsExpire.
public:
    virtual void expire();
private:
    srs_error_t do_cycle();
};

class SrsRtspTcpNetwork : public ISrsStreamWriter
{
private:
    ISrsProtocolReadWriter* skt_;
    int channel_;
public:
    SrsRtspTcpNetwork(ISrsProtocolReadWriter* skt, int ch);
    virtual ~SrsRtspTcpNetwork();
// Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite);
};

#endif