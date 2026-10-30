//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI37_HPP
#define SRS_UTEST_AI37_HPP

/*
#include <srs_utest_ai37.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_async_call.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_ai18.hpp>

#include <string>
#include <utility>
#include <vector>

// Mock ISrsDtlsCallback that keeps every DTLS record the DTLS object sends, so a test can hand it to the peer.
class MockDtlsCallbackForHandshake : public ISrsDtlsCallback
{
public:
    std::vector<std::string> packets_;
    std::vector<std::string> app_data_;
    std::vector<std::pair<std::string, std::string> > alerts_;
    int handshake_done_count_;
    srs_error_t handshake_done_error_;
    srs_error_t app_data_error_;
    srs_error_t write_error_;
    srs_error_t alert_error_;

public:
    MockDtlsCallbackForHandshake();
    virtual ~MockDtlsCallbackForHandshake();

public:
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t write_dtls_data(void *data, int size);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
};

// Hand the records each side sent to the other side, until neither side has anything more to send.
extern srs_error_t dtls_exchange(SrsDtls *client, MockDtlsCallbackForHandshake *ccb, SrsDtls *server, MockDtlsCallbackForHandshake *scb);

// Protect an RTP and an RTCP packet by one side and unprotect them by the other.
extern void expect_srtp_round_trip(ISrsSRTP *sender, ISrsSRTP *receiver, uint16_t seq);

// Mock ISrsRtcConnection that logs each call SrsRtcSessionManager makes while it creates a session.
class MockRtcConnectionForCreateSession : public MockRtcConnectionForUpdateSessions
{
public:
    std::vector<std::string> calls_;
    srs_error_t add_publisher_error_;
    srs_error_t add_player_error_;
    srs_error_t generate_local_sdp_error_;
    srs_error_t initialize_error_;
    std::string generated_username_;
    bool last_tracks_publish_;
    bool last_tracks_status_;
    std::string last_tracks_stream_;
    bool last_init_dtls_;
    bool last_init_srtp_;
    std::string last_init_username_;
    SrsSharedPtr<ISrsStreamPublishToken> publish_token_;
    bool *destroyed_;

public:
    MockRtcConnectionForCreateSession();
    virtual ~MockRtcConnectionForCreateSession();

public:
    virtual srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    virtual srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    virtual void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status);
    virtual void set_remote_sdp(const SrsSdp &sdp);
    virtual void set_local_sdp(const SrsSdp &sdp);
    virtual void set_state_as_waiting_stun();
    virtual srs_error_t initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username);
    virtual void set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> publish_token);
    virtual srs_error_t generate_local_sdp(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, std::string &username);
};

// Mock app factory that hands out the connection the test prepared.
class MockAppFactoryForCreateSession : public SrsAppFactory
{
public:
    ISrsRtcConnection *connection_;
    int create_rtc_connection_count_;

public:
    MockAppFactoryForCreateSession();
    virtual ~MockAppFactoryForCreateSession();

public:
    virtual ISrsRtcConnection *create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid);
};

// Mock async task that counts its calls.
class MockAsyncCallTaskForSessionManager : public ISrsAsyncCallTask
{
public:
    int *calls_;

public:
    MockAsyncCallTaskForSessionManager(int *calls);
    virtual ~MockAsyncCallTaskForSessionManager();

public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

// Mock config that turns on the STUN strict check, as ice-lite servers do.
class MockAppConfigForStunStrictCheck : public MockAppConfig
{
public:
    virtual bool get_rtc_stun_strict_check(std::string vhost) { return true; }
};

// Mock ISrsContextIdSetter that keeps the context id it is given.
class MockContextIdSetterForTcpConnCycle : public ISrsContextIdSetter
{
public:
    SrsContextId cid_;
    int set_cid_count_;

public:
    MockContextIdSetterForTcpConnCycle();
    virtual ~MockContextIdSetterForTcpConnCycle();

public:
    virtual void set_cid(const SrsContextId &cid);
};

// Mock ISrsRtcConnection whose TCP network is injected, and which counts its expiry.
class MockRtcConnectionForTcpConnCycle : public MockRtcConnectionForUpdateSessions
{
public:
    ISrsRtcNetwork *tcp_network_;
    int expire_count_;

public:
    MockRtcConnectionForTcpConnCycle();
    virtual ~MockRtcConnectionForTcpConnCycle();

public:
    virtual ISrsRtcNetwork *tcp();
    virtual void expire();
};

// Mock ISrsSrtClientHandler that records the SRT clients the acceptor hands over.
class MockSrtClientHandlerForAcceptor : public ISrsSrtClientHandler
{
public:
    std::vector<srs_srt_t> fds_;
    srs_error_t accept_error_;

public:
    MockSrtClientHandlerForAcceptor();
    virtual ~MockSrtClientHandlerForAcceptor();

public:
    virtual srs_error_t accept_srt_client(srs_srt_t srt_fd);
};

// A coroutine that waits on an SRT socket, readable or writeable, and keeps the result.
class MockSrtSocketWaiter : public ISrsCoroutineHandler
{
public:
    SrsSrtSocket *skt_;
    bool readable_;
    bool done_;
    srs_error_t err_;

public:
    MockSrtSocketWaiter(SrsSrtSocket *skt, bool readable);
    virtual ~MockSrtSocketWaiter();

public:
    virtual srs_error_t cycle();
};

// The deprecated JSON play API, with the session part replaced so a test sees what the request resolved to.
class MockRtcPlayForJsonApi : public SrsGoApiRtcPlay
{
public:
    int serve_count_;
    srs_error_t serve_error_;
    std::string ip_;
    std::string vhost_;
    std::string app_;
    std::string stream_;
    std::string api_;
    std::string eip_;
    std::string vcodec_;
    std::string acodec_;
    bool publish_;
    bool dtls_;
    bool srtp_;

public:
    MockRtcPlayForJsonApi(ISrsRtcApiServer *server);
    virtual ~MockRtcPlayForJsonApi();

public:
    using SrsGoApiRtcPlay::serve_http;
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc);
};

// The deprecated JSON publish API, with the session part replaced so a test sees what the request resolved to.
class MockRtcPublishForJsonApi : public SrsGoApiRtcPublish
{
public:
    int serve_count_;
    srs_error_t serve_error_;
    std::string ip_;
    std::string vhost_;
    std::string param_;
    std::string api_;
    std::string eip_;
    std::string vcodec_;
    std::string acodec_;
    bool publish_;
    bool dtls_;
    bool srtp_;

public:
    MockRtcPublishForJsonApi(ISrsRtcApiServer *server);
    virtual ~MockRtcPublishForJsonApi();

public:
    using SrsGoApiRtcPublish::serve_http;
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc);
};

// A coroutine that serves one HTTP request, so the handler runs on a coroutine stack as in the server.
class MockHttpServeCoroutine : public ISrsCoroutineHandler
{
public:
    ISrsHttpHandler *handler_;
    ISrsHttpResponseWriter *w_;
    ISrsHttpMessage *r_;
    bool done_;
    srs_error_t err_;

public:
    MockHttpServeCoroutine(ISrsHttpHandler *handler, ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual ~MockHttpServeCoroutine();

public:
    virtual srs_error_t cycle();
};

#endif
