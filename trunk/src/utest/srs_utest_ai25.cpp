//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai25.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_json.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>

// Goal tests for hook rejection across every protocol, for both players and publishers.
//
// An on_play or on_publish hook rejects a client by returning a non-zero application code. Both
// hooks share one implementation - SrsHttpHooks::do_post - so a rejection reaches every protocol
// as the same ERROR_RESPONSE_CODE. Every protocol already blocks the client; what differs is how
// much of the reason reaches them. These tests state the INTENDED behavior, so the ones covering
// paths that still need work are expected to FAIL until that work lands. They are the
// specification for the change, not a record of today's behavior.
//
// WHIP publish is the reference implementation: SrsGoApiRtcPublish::serve_http() converts a hook
// rejection with srs_error_transform(ERROR_SYSTEM_AUTH, ...), which SrsGoApiRtcWhip::serve_http()
// maps to 401. The play path a few hundred lines above uses srs_error_wrap() instead, which is the
// entire reason WHEP answers 500. Keep the WHIP test passing; it guards behavior that already works.
//
// SRT publish is not covered separately: it is the same accepted limitation as SRT play, on the
// same transport with no status channel, so a second test would assert nothing new.
//
// The RTSP goal test lives in srs_utest_ai22.cpp because RTSP sources only build with --rtsp=on.
// RTSP and the HTTP playback protocols have no publish path in SRS, so they are play-only here.

// The application code a hook returns to reject an unauthorized client.
#define MOCK_HOOK_REJECT_STATUS 401

// HTTP-FLV: a rejected viewer must receive the hook's status, not a silently closed connection.
//
// Today SrsLiveStream::serve_http_impl() returns the hook error without writing anything to the
// response writer, so the player observes a dropped TCP connection that is indistinguishable from
// a crash or a network failure.
VOID TEST(HookRejectionTest, HttpFlvRejectedViewerReceivesHookStatus)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockHttpMessageForLiveStream> message(new MockHttpMessageForLiveStream());
    MockResponseWriter writer;
    MockRequest request("test.vhost", "live", "stream1");
    MockBufferCache cache;
    MockStatisticForLiveStream stat;
    MockSecurity security;

    // Enable the on_play hook for this vhost.
    MockAppConfigForLiveStreamHooks config;
    config.http_hooks_enabled_ = true;
    config.on_play_directive_ = new SrsConfDirective();
    config.on_play_directive_->name_ = "on_play";
    config.on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");

    // The hook rejects this viewer as unauthorized.
    MockHttpHooksForLiveStream hooks;
    hooks.on_play_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    SrsLiveStream stream(&request, &cache);
    stream.stat_ = &stat;
    srs_freep(stream.security_);
    stream.security_ = &security;
    stream.config_ = &config;
    stream.hooks_ = &hooks;
    stream.entry_ = new SrsHttpMuxEntry();
    stream.entry_->enabled = false;

    err = stream.serve_http(&writer, message.get());

    // The hook must have been consulted and the playback rejected.
    EXPECT_EQ(1, hooks.on_play_count_);
    EXPECT_TRUE(stream.viewers_.empty());

    // GOAL: the player receives the hook's status instead of a silent disconnect.
    string response(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    EXPECT_FALSE(response.empty());
    EXPECT_EQ(0, (int)response.find("HTTP/1.1 401"));

    // The rejection must not leak the hook URL or any backend detail to the viewer.
    EXPECT_EQ(string::npos, response.find("127.0.0.1"));
    EXPECT_EQ(string::npos, response.find("api/v1/play"));

    srs_freep(err);
    stream.stat_ = NULL;
    stream.security_ = NULL;
    stream.config_ = NULL;
    stream.hooks_ = NULL;
    srs_freep(stream.entry_);
}

// HLS: a rejected viewer must receive the hook's status on the m3u8 request.
//
// SrsHlsStream::serve_new_session() has the same defect as HTTP-FLV: the hook error is returned
// without writing a response, so the player's playlist request dies with no status.
//
// This enters through serve_m3u8_ctx(), the root of HLS streaming, because that is where the
// status is decided; serve_new_session() only raises the error. See srs_http_stream_serve_error.
VOID TEST(HookRejectionTest, HlsRejectedViewerReceivesHookStatus)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockHttpMessageForLiveStream> message(new MockHttpMessageForLiveStream());
    MockResponseWriter writer;
    SrsUniquePtr<MockRequest> request(new MockRequest("test.vhost", "live", "stream1"));
    MockStatisticForLiveStream stat;
    MockSecurity security;

    // Enable the on_play hook for this vhost.
    MockAppConfigForLiveStreamHooks config;
    config.http_hooks_enabled_ = true;
    config.on_play_directive_ = new SrsConfDirective();
    config.on_play_directive_->name_ = "on_play";
    config.on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");

    // The hook rejects this viewer as unauthorized.
    MockHttpHooksForLiveStream hooks;
    hooks.on_play_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    // The constructor only captures dependencies, so the mocks below fully replace them. assemble()
    // is deliberately not called: it only subscribes to the shared timer, which this test does not
    // need, and skipping it keeps the test free of any global side effect.
    SrsUniquePtr<SrsHlsStream> hls(new SrsHlsStream());
    hls->config_ = &config;
    hls->stat_ = &stat;
    hls->hooks_ = &hooks;
    srs_freep(hls->security_);
    hls->security_ = &security;

    // The file reader factory is not used, because a refused viewer never reaches the playlist.
    bool served = false;
    err = hls->serve_m3u8_ctx(&writer, message.get(), NULL, "", request.get(), &served);

    // The hook must have been consulted and the playback rejected.
    EXPECT_TRUE(served);
    EXPECT_EQ(1, hooks.on_play_count_);

    // GOAL: the player receives the hook's status instead of a silent disconnect.
    string response(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    EXPECT_FALSE(response.empty());
    EXPECT_EQ(0, (int)response.find("HTTP/1.1 401"));

    // The rejection must not leak the hook URL or any backend detail to the viewer.
    EXPECT_EQ(string::npos, response.find("127.0.0.1"));
    EXPECT_EQ(string::npos, response.find("api/v1/play"));

    srs_freep(err);
    hls->config_ = NULL;
    hls->stat_ = NULL;
    hls->hooks_ = NULL;
    hls->security_ = NULL;
}

MockFileReaderFactoryForHlsStream::MockFileReaderFactoryForHlsStream(std::string content)
{
    content_ = content;
    create_count_ = 0;
}

MockFileReaderFactoryForHlsStream::~MockFileReaderFactoryForHlsStream()
{
}

SrsFileReader *MockFileReaderFactoryForHlsStream::create_file_reader()
{
    create_count_++;
    return new MockSrsFileReader(content_.data(), (int)content_.length());
}

// Request an HLS playlist with the given URL, and return the raw HTTP response in resp.
static srs_error_t mock_hls_serve_m3u8(SrsHlsStream *hls, ISrsFileReaderFactory *factory, std::string url, std::string &resp)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockHttpMessageForLiveStream> message(new MockHttpMessageForLiveStream());
    if ((err = message->set_url(url, false)) != srs_success) {
        return srs_error_wrap(err, "set url");
    }

    SrsUniquePtr<MockRequest> request(new MockRequest("test.vhost", "live", "stream1"));
    MockResponseWriter writer;

    bool served = false;
    err = hls->serve_m3u8_ctx(&writer, message.get(), factory, "./objs/nginx/html/live/stream1.m3u8", request.get(), &served);

    resp = string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    return err;
}

// HLS: a viewer rejected by the on_play hook must stay rejected when it retries with the same hls_ctx.
//
// The client may choose the hls_ctx of a new session in the query string. SrsHlsStream::serve_m3u8_ctx()
// calls alive() even when serve_new_session() failed, so the rejected ctx is kept as a live session.
// The retry then finds that ctx and takes serve_exists_session(), which serves the playlist without
// consulting the hook, so any client passes on_play by simply asking twice.
VOID TEST(HookRejectionTest, HlsRejectedViewerCannotRetryWithSameCtx)
{
    srs_error_t err = srs_success;

    MockStatisticForLiveStream stat;
    MockSecurity security;
    MockFileReaderFactoryForHlsStream factory("#EXTM3U\n#EXTINF:10.000,\nstream1-0.ts\n");

    MockAppConfigForLiveStreamHooks config;
    config.http_hooks_enabled_ = true;
    config.on_play_directive_ = new SrsConfDirective();
    config.on_play_directive_->name_ = "on_play";
    config.on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");

    // The hook rejects every attempt of this viewer as unauthorized.
    MockHttpHooksForLiveStream hooks;
    hooks.on_play_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    // Only the captured dependencies are replaced. assemble() is not called, so the stream is never
    // subscribed to the real shared timer.
    SrsUniquePtr<SrsHlsStream> hls(new SrsHlsStream());
    hls->config_ = &config;
    hls->stat_ = &stat;
    hls->hooks_ = &hooks;
    srs_freep(hls->security_);
    hls->security_ = &security;
    hls->shared_timer_ = NULL;

    // The first request with a client-chosen hls_ctx is refused by the hook.
    string resp;
    HELPER_EXPECT_FAILED(mock_hls_serve_m3u8(hls.get(), &factory, "/live/stream1.m3u8?hls_ctx=chosenbyclient", resp));
    EXPECT_EQ(1, hooks.on_play_count_);
    EXPECT_EQ(0, (int)resp.find("HTTP/1.1 401"));

    // The refused viewer is removed from statistic at once, because it has no session to expire.
    EXPECT_EQ(1, stat.on_disconnect_count_);

    // GOAL: retrying with the same hls_ctx asks the hook again and is refused again. The playlist of
    // the stream must never be read for a viewer the hook refused.
    HELPER_EXPECT_FAILED(mock_hls_serve_m3u8(hls.get(), &factory, "/live/stream1.m3u8?hls_ctx=chosenbyclient", resp));
    EXPECT_EQ(2, hooks.on_play_count_);
    EXPECT_EQ(0, (int)resp.find("HTTP/1.1 401"));
    EXPECT_EQ(string::npos, resp.find("stream1-0.ts"));
    EXPECT_EQ(0, factory.create_count_);

    hls->config_ = NULL;
    hls->stat_ = NULL;
    hls->hooks_ = NULL;
    hls->security_ = NULL;
}

// HLS: a viewer denied by the security rules must stay denied when it retries with the same hls_ctx.
//
// The security check runs in serve_new_session() before the hook, so it is bypassed by the same retry
// as the on_play hook. A denied viewer has no status mapped yet, so it is refused by closing the
// connection with an empty response.
VOID TEST(HookRejectionTest, HlsDeniedViewerCannotRetryWithSameCtx)
{
    srs_error_t err = srs_success;

    MockStatisticForLiveStream stat;
    MockAppConfigForLiveStreamHooks config;
    MockHttpHooksForLiveStream hooks;
    MockFileReaderFactoryForHlsStream factory("#EXTM3U\n#EXTINF:10.000,\nstream1-0.ts\n");

    // The security rules deny every attempt of this viewer.
    MockSecurity security;
    security.check_error_ = srs_error_new(ERROR_SYSTEM_SECURITY_DENY, "deny by rule");

    SrsUniquePtr<SrsHlsStream> hls(new SrsHlsStream());
    hls->config_ = &config;
    hls->stat_ = &stat;
    hls->hooks_ = &hooks;
    srs_freep(hls->security_);
    hls->security_ = &security;
    hls->shared_timer_ = NULL;

    // The first request with a client-chosen hls_ctx is denied.
    string resp;
    HELPER_EXPECT_FAILED(mock_hls_serve_m3u8(hls.get(), &factory, "/live/stream1.m3u8?hls_ctx=chosenbyclient", resp));
    EXPECT_EQ(1, security.check_count_);
    EXPECT_TRUE(resp.empty());

    // The denied viewer is removed from statistic at once, because it has no session to expire.
    EXPECT_EQ(1, stat.on_disconnect_count_);

    // GOAL: retrying with the same hls_ctx runs the security check again and is denied again.
    HELPER_EXPECT_FAILED(mock_hls_serve_m3u8(hls.get(), &factory, "/live/stream1.m3u8?hls_ctx=chosenbyclient", resp));
    EXPECT_EQ(2, security.check_count_);
    EXPECT_TRUE(resp.empty());
    EXPECT_EQ(0, factory.create_count_);

    hls->config_ = NULL;
    hls->stat_ = NULL;
    hls->hooks_ = NULL;
    hls->security_ = NULL;
}

// RTMP: authorization must be decided before the client is told playback started.
//
// SrsRtmpConn::stream_service_cycle() calls rtmp_->start_play() first, which sends StreamBegin and
// onStatus(NetStream.Play.Start) - a success signal - and only then consults the on_play hook. A
// rejected viewer is therefore told "you are playing" and is then silently disconnected. The order
// must be reversed so that a rejected viewer is never told playback started.
VOID TEST(HookRejectionTest, RtmpRejectedViewerIsNotToldPlaybackStarted)
{
    srs_error_t err = srs_success;

    MockRtmpTransportForDoCycle *transport = new MockRtmpTransportForDoCycle();
    MockRtmpServer *rtmp = new MockRtmpServer();
    MockSecurity *security = new MockSecurity();

    // Enable the on_play hook for this vhost. The default vhost must exist, otherwise
    // check_vhost() refuses the client long before the hook is consulted.
    MockAppConfigForHttpHooksOnPlay *config = new MockAppConfigForHttpHooksOnPlay();
    config->default_vhost_ = new SrsConfDirective();
    config->default_vhost_->name_ = "vhost";
    config->default_vhost_->args_.push_back("__defaultVhost__");
    config->http_hooks_enabled_ = true;
    config->on_play_directive_ = new SrsConfDirective();
    config->on_play_directive_->name_ = "on_play";
    config->on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");

    // The hook rejects this viewer as unauthorized.
    MockHttpHooksForOnPlay *hooks = new MockHttpHooksForOnPlay();
    hooks->on_play_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    SrsRtmpConn *conn = new SrsRtmpConn(transport, "127.0.0.1", 1935);
    conn->config_ = config;
    conn->assemble();

    srs_freep(conn->rtmp_);
    conn->rtmp_ = rtmp;
    srs_freep(conn->security_);
    conn->security_ = security;
    conn->hooks_ = hooks;

    rtmp->type_ = SrsRtmpConnPlay;
    rtmp->stream_ = "livestream";
    conn->info_->type_ = SrsRtmpConnPlay;
    conn->info_->req_->tcUrl_ = "rtmp://127.0.0.1/live";
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    err = conn->stream_service_cycle();

    // The hook must have been consulted and the playback rejected.
    EXPECT_EQ(1, hooks->on_play_count_);
    EXPECT_TRUE(err != srs_success);

    // GOAL: a rejected viewer is never sent onStatus(NetStream.Play.Start), so authorization must
    // be resolved before start_play() runs.
    EXPECT_EQ(0, rtmp->start_play_count_);

    srs_freep(err);
    conn->hooks_ = NULL;
    srs_freep(conn);
    srs_freep(config);
    srs_freep(hooks);
}

// RTMP: the same ordering defect on the publish side.
//
// stream_service_cycle() calls rtmp_->start_fmle_publish(), which answers the publisher with
// onStatus(NetStream.Publish.Start), before publishing() consults the on_publish hook. A rejected
// publisher is therefore told publishing started and is then silently disconnected. Publishers are
// the more common target of hook authorization, because stream keys are usually checked there.
VOID TEST(HookRejectionTest, RtmpRejectedPublisherIsNotToldPublishStarted)
{
    srs_error_t err = srs_success;

    MockRtmpTransportForDoCycle *transport = new MockRtmpTransportForDoCycle();
    MockRtmpServer *rtmp = new MockRtmpServer();
    MockSecurity *security = new MockSecurity();

    // Enable the on_publish hook for this vhost. The default vhost must exist, otherwise
    // check_vhost() refuses the client long before the hook is consulted.
    MockAppConfigForHttpHooksOnPublish *config = new MockAppConfigForHttpHooksOnPublish();
    config->default_vhost_ = new SrsConfDirective();
    config->default_vhost_->name_ = "vhost";
    config->default_vhost_->args_.push_back("__defaultVhost__");
    config->http_hooks_enabled_ = true;
    config->on_publish_directive_ = new SrsConfDirective();
    config->on_publish_directive_->name_ = "on_publish";
    config->on_publish_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/publish");

    // The hook rejects this publisher as unauthorized.
    MockHttpHooksForOnPublish *hooks = new MockHttpHooksForOnPublish();
    hooks->on_publish_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    SrsRtmpConn *conn = new SrsRtmpConn(transport, "127.0.0.1", 1935);
    conn->config_ = config;
    conn->assemble();

    srs_freep(conn->rtmp_);
    conn->rtmp_ = rtmp;
    srs_freep(conn->security_);
    conn->security_ = security;
    conn->hooks_ = hooks;

    rtmp->type_ = SrsRtmpConnFMLEPublish;
    rtmp->stream_ = "livestream";
    conn->info_->type_ = SrsRtmpConnFMLEPublish;
    conn->info_->req_->tcUrl_ = "rtmp://127.0.0.1/live";
    conn->info_->req_->vhost_ = "__defaultVhost__";
    conn->info_->req_->app_ = "live";
    conn->info_->req_->stream_ = "livestream";

    err = conn->stream_service_cycle();

    // The hook must have been consulted and the publish rejected.
    EXPECT_EQ(1, hooks->on_publish_count_);
    EXPECT_TRUE(err != srs_success);

    // GOAL: a rejected publisher is never sent onStatus(NetStream.Publish.Start), so authorization
    // must be resolved before start_fmle_publish() runs.
    EXPECT_EQ(0, rtmp->start_fmle_publish_count_);

    srs_freep(err);
    conn->hooks_ = NULL;
    srs_freep(conn);
    srs_freep(config);
    srs_freep(hooks);
}

// WebRTC: the hook's status must survive to the WHEP/play response.
//
// SrsGoApiRtcWhip::serve_http() already maps error codes onto real HTTP statuses, and it already
// has an authorization status: ERROR_SYSTEM_AUTH answers 401. A rejected on_play hook returns
// ERROR_RESPONSE_CODE, which matches none of the mapped codes and therefore falls through to
// 500 Internal Server Error - a server fault, not an authorization decision.
//
// This exercises the standard WHEP API, not the deprecated JSON play API. The JSON API answers
// HTTP 200 with a fixed {"code":400} for every failure and is not where playback behavior should
// be tested or extended; see the deprecation note on SrsGoApiRtcPlay::serve_http().
VOID TEST(HookRejectionTest, WhepRejectedViewerReceivesHookStatus)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockRtcApiServer> server(new MockRtcApiServer());
    SrsUniquePtr<MockResponseWriter> writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessageForRtcApi> message(new MockHttpMessageForRtcApi());

    // The WHEP endpoint, which dispatches to the play negotiation.
    HELPER_EXPECT_SUCCESS(message->set_url("http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=livestream", false));

    // Enable WebRTC and the on_play hook for this vhost.
    MockAppConfigForRtcPlay *config = new MockAppConfigForRtcPlay();
    config->rtc_server_enabled_ = true;
    config->rtc_enabled_ = true;
    config->rtc_from_rtmp_ = true;
    config->http_hooks_enabled_ = true;
    config->on_play_directive_ = new SrsConfDirective();
    config->on_play_directive_->name_ = "on_play";
    config->on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");

    // The hook rejects this viewer as unauthorized. MockHttpHooksForRtcPlay always succeeds, so use
    // the hook mock that supports error injection.
    MockHttpHooksForOnPlay *hooks = new MockHttpHooksForOnPlay();
    hooks->on_play_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    SrsUniquePtr<SrsGoApiRtcWhip> api(new SrsGoApiRtcWhip(server.get()));
    api->config_ = config;
    api->play_->config_ = config;
    api->play_->hooks_ = hooks;

    // WHEP carries the offer as a raw SDP body, not as JSON.
    message->body_content_ = "v=0\r\n"
                             "o=- 0 0 IN IP4 127.0.0.1\r\n"
                             "s=-\r\n"
                             "t=0 0\r\n"
                             "a=group:BUNDLE 0\r\n"
                             "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                             "c=IN IP4 0.0.0.0\r\n"
                             "a=rtcp:9 IN IP4 0.0.0.0\r\n"
                             "a=ice-ufrag:test\r\n"
                             "a=ice-pwd:testpasswordtestpassword\r\n"
                             "a=fingerprint:sha-256 "
                             "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:"
                             "44:55:66:77:88:99\r\n"
                             "a=setup:actpass\r\n"
                             "a=mid:0\r\n"
                             "a=recvonly\r\n"
                             "a=rtcp-mux\r\n"
                             "a=rtpmap:111 opus/48000/2\r\n";

    HELPER_EXPECT_SUCCESS(api->serve_http(writer.get(), message.get()));

    // The hook must have been consulted and the playback rejected.
    EXPECT_EQ(1, hooks->on_play_count_);

    // GOAL: the rejection is reported as an authorization failure, not as a server fault.
    string response(writer->io.out_buffer.bytes(), writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());
    EXPECT_EQ(0, (int)response.find("HTTP/1.1 401"));

    // The rejection must not leak the hook URL or any backend detail to the viewer.
    EXPECT_EQ(string::npos, response.find("8085"));
    EXPECT_EQ(string::npos, response.find("api/v1/play"));

    api->config_ = NULL;
    api->play_->config_ = NULL;
    api->play_->hooks_ = NULL;
    srs_freep(config);
    srs_freep(hooks);
}

// WHIP: a rejected publisher already receives 401, and must keep receiving it.
//
// WHIP is HTTP just as WHEP is, so a rejected publisher deserves the same authorization status as
// a rejected viewer - and here SRS already does it. SrsGoApiRtcPublish::serve_http() converts the
// hook rejection with srs_error_transform(ERROR_SYSTEM_AUTH, ...) rather than srs_error_wrap(), so
// SrsGoApiRtcWhip::serve_http() maps it onto 401.
//
// Unlike the other tests here this one is expected to PASS today. It is a regression guard, and it
// records the pattern the WHEP play path should adopt: the mechanism already exists in the sibling
// class, so the play fix is to reuse it rather than to invent anything.
VOID TEST(HookRejectionTest, WhipRejectedPublisherReceivesHookStatus)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockRtcApiServer> server(new MockRtcApiServer());
    SrsUniquePtr<MockResponseWriter> writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessageForRtcApi> message(new MockHttpMessageForRtcApi());

    // The WHIP endpoint, which defaults to the publish action.
    HELPER_EXPECT_SUCCESS(message->set_url("http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=livestream", false));

    // Enable WebRTC and the on_publish hook for this vhost.
    MockAppConfigForHttpHooksOnPublish *config = new MockAppConfigForHttpHooksOnPublish();
    config->rtc_server_enabled_ = true;
    config->rtc_enabled_ = true;
    config->http_hooks_enabled_ = true;
    config->on_publish_directive_ = new SrsConfDirective();
    config->on_publish_directive_->name_ = "on_publish";
    config->on_publish_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/publish");

    // The hook rejects this publisher as unauthorized.
    MockHttpHooksForOnPublish *hooks = new MockHttpHooksForOnPublish();
    hooks->on_publish_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    SrsUniquePtr<SrsGoApiRtcWhip> api(new SrsGoApiRtcWhip(server.get()));
    api->config_ = config;
    api->publish_->config_ = config;
    api->publish_->hooks_ = hooks;

    // WHIP carries the offer as a raw SDP body, not as JSON.
    message->body_content_ = "v=0\r\n"
                             "o=- 0 0 IN IP4 127.0.0.1\r\n"
                             "s=-\r\n"
                             "t=0 0\r\n"
                             "a=group:BUNDLE 0\r\n"
                             "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
                             "c=IN IP4 0.0.0.0\r\n"
                             "a=rtcp:9 IN IP4 0.0.0.0\r\n"
                             "a=ice-ufrag:test\r\n"
                             "a=ice-pwd:testpasswordtestpassword\r\n"
                             "a=fingerprint:sha-256 "
                             "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:"
                             "44:55:66:77:88:99\r\n"
                             "a=setup:actpass\r\n"
                             "a=mid:0\r\n"
                             "a=sendonly\r\n"
                             "a=rtcp-mux\r\n"
                             "a=rtpmap:111 opus/48000/2\r\n";

    HELPER_EXPECT_SUCCESS(api->serve_http(writer.get(), message.get()));

    // The hook must have been consulted and the publish rejected.
    EXPECT_EQ(1, hooks->on_publish_count_);

    // The rejection is reported as an authorization failure, not as a server fault.
    string response(writer->io.out_buffer.bytes(), writer->io.out_buffer.length());
    EXPECT_FALSE(response.empty());
    EXPECT_EQ(0, (int)response.find("HTTP/1.1 401"));

    // The rejection must not leak the hook URL or any backend detail to the publisher.
    EXPECT_EQ(string::npos, response.find("8085"));
    EXPECT_EQ(string::npos, response.find("api/v1/publish"));

    api->config_ = NULL;
    api->publish_->config_ = NULL;
    api->publish_->hooks_ = NULL;
    srs_freep(config);
    srs_freep(hooks);
}

// SRT: rejection with no reason is the accepted, final behavior.
//
// MPEG-TS over SRT carries no application-level status or response channel, so there is nothing to
// deliver a rejection reason through. The only guarantee SRS can offer is that the playback is
// refused and the session ends. This test locks that limitation in; unlike the other protocols it
// is expected to PASS today and to keep passing after the enhancement.
VOID TEST(HookRejectionTest, SrtRejectedViewerIsRefusedWithoutStatusChannel)
{
    srs_error_t err = srs_success;

    srs_srt_t dummy_fd = 1;
    SrsUniquePtr<SrsMpegtsSrtConn> conn(new SrsMpegtsSrtConn(NULL, dummy_fd, "192.168.1.100", 9000));
    conn->assemble();

    // Enable the on_play hook for this vhost.
    SrsUniquePtr<MockAppConfigForSrtHooks> config(new MockAppConfigForSrtHooks());
    config->set_http_hooks_enabled(true);
    vector<string> urls;
    urls.push_back("http://127.0.0.1:8085/api/v1/play");
    config->set_on_play_urls(urls);

    // The hook rejects this viewer as unauthorized.
    SrsUniquePtr<MockHttpHooksForSrt> hooks(new MockHttpHooksForSrt());
    hooks->on_play_error_ = srs_error_new(ERROR_RESPONSE_CODE, "http: response object code %d", MOCK_HOOK_REJECT_STATUS);

    SrsUniquePtr<MockRtcAsyncCallRequest> request(new MockRtcAsyncCallRequest("test.vhost", "live", "stream1"));

    conn->config_ = config.get();
    conn->hooks_ = hooks.get();
    conn->req_ = request.get();

    // GOAL: the playback is refused. SRT exposes no status channel, so the rejection reason is
    // deliberately not delivered to the player and the session simply ends.
    HELPER_EXPECT_FAILED(conn->http_hooks_on_play());
    EXPECT_EQ(1, hooks->on_play_count_);

    conn->config_ = NULL;
    conn->hooks_ = NULL;
    conn->req_ = NULL;
    conn->stat_ = NULL;
    conn->stream_publish_tokens_ = NULL;
    conn->srt_sources_ = NULL;
    conn->live_sources_ = NULL;
    conn->rtc_sources_ = NULL;
}
