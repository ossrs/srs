//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai26.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_security.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>

// Goal tests for one stream URL rule shared by every protocol, see #4739.
//
// A stream URL is resolved like a file path: every segment but the last is the app, the folder,
// and the last segment is the stream, the file. So "tenant-1/live/0/0/AZR-001" is always app
// "tenant-1/live/0/0" and stream "AZR-001", whichever protocol carries it, and a stream name never
// contains a slash. When a client splits the path differently, for example WHIP app=tenant-1 and
// stream=live/0/0/AZR-001, the server must normalize it to the same app and stream, exactly as the
// RTMP tcUrl parser already does.
//
// These tests state the INTENDED behavior. The ones covering protocols that still split the path
// another way are expected to FAIL until those protocols follow the rule. They are the
// specification for the change, not a record of today's behavior.
//
// The RTSP goal test lives in srs_utest_ai22.cpp because RTSP only builds with --rtsp=on.

// The multi-level stream from #4739: the app is four folders deep and the stream is a single name.
#define MULTI_LEVEL_APP "tenant-1/live/0/0"
#define MULTI_LEVEL_STREAM "AZR-001"
#define MULTI_LEVEL_PATH MULTI_LEVEL_APP "/" MULTI_LEVEL_STREAM
#define MULTI_LEVEL_URL "/" MULTI_LEVEL_PATH

// A request follows the rule when its app is the folder, its stream is the file, and its stream
// URL, the key every source and mount is looked up by, ends with the same path.
#define EXPECT_FOLDER_AND_FILE(app, stream, url)                           \
    EXPECT_STREQ(MULTI_LEVEL_APP, (app).c_str());                          \
    EXPECT_STREQ(MULTI_LEVEL_STREAM, (stream).c_str());                    \
    EXPECT_TRUE(srs_strings_ends_with((url), MULTI_LEVEL_URL)) << (url)

#define EXPECT_REQUEST_FOLDER_AND_FILE(req) \
    EXPECT_FOLDER_AND_FILE((req)->app_, (req)->stream_, (req)->get_stream_url())

// A minimal SDP offer, enough for the WebRTC APIs to parse before they negotiate.
#define MULTI_LEVEL_SDP "v=0\r\no=- 123456 2 IN IP4 192.168.1.100\r\ns=WebRTC\r\nt=0 0\r\n" \
                        "a=group:BUNDLE 0\r\n"                                                 \
                        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 H264/90000\r\n"         \
                        "a=mid:0\r\n"

// Copies the resolved app and stream out of a request that does not outlive the call under test.
class StreamUrlCapture
{
public:
    int count_;
    string app_;
    string stream_;
    string url_;

public:
    StreamUrlCapture()
    {
        count_ = 0;
    }

public:
    void capture(ISrsRequest *req)
    {
        count_++;
        app_ = req->app_;
        stream_ = req->stream_;
        url_ = req->get_stream_url();
    }
};

// Records the request an HTTP-FLV viewer is resolved to, then refuses it so the test stops before
// any source is needed. The security check runs on the same request the live source is fetched by.
class MockSecurityForStreamUrlRule : public ISrsSecurity
{
public:
    StreamUrlCapture captured_;

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req)
    {
        captured_.capture(req);
        return srs_error_new(ERROR_SYSTEM_SECURITY_DENY, "stop after resolving the request");
    }
};

// An HTTP-FLV request for any path; the base mock always answers "/live/stream.flv".
class MockHttpMessageForStreamUrlRule : public MockHttpMessageForLiveStream
{
public:
    string path_;

public:
    virtual std::string path()
    {
        return path_;
    }
};

// The deprecated JSON play API, stopped right after it resolves the stream URL.
class MockRtcPlayForStreamUrlRule : public SrsGoApiRtcPlay
{
public:
    StreamUrlCapture captured_;

public:
    MockRtcPlayForStreamUrlRule(ISrsRtcApiServer *server) : SrsGoApiRtcPlay(server)
    {
    }

public:
    using SrsGoApiRtcPlay::serve_http;
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
    {
        captured_.capture(ruc->req_);
        return srs_success;
    }
};

// The deprecated JSON publish API, stopped right after it resolves the stream URL.
class MockRtcPublishForStreamUrlRule : public SrsGoApiRtcPublish
{
public:
    StreamUrlCapture captured_;

public:
    MockRtcPublishForStreamUrlRule(ISrsRtcApiServer *server) : SrsGoApiRtcPublish(server)
    {
    }

public:
    using SrsGoApiRtcPublish::serve_http;
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
    {
        captured_.capture(ruc->req_);
        return srs_success;
    }
};

// WHIP and WHEP, with the publish and play handlers replaced so the request stops once resolved.
class MockRtcWhipForStreamUrlRule : public SrsGoApiRtcWhip
{
public:
    MockRtcPublishForStreamUrlRule *mock_publish_;
    MockRtcPlayForStreamUrlRule *mock_play_;

public:
    MockRtcWhipForStreamUrlRule(ISrsRtcApiServer *server) : SrsGoApiRtcWhip(server)
    {
        srs_freep(publish_);
        srs_freep(play_);
        publish_ = mock_publish_ = new MockRtcPublishForStreamUrlRule(NULL);
        play_ = mock_play_ = new MockRtcPlayForStreamUrlRule(NULL);
    }
};

// Resolve a WHIP or WHEP request carrying the given app and stream query parameters.
static void resolve_whip(string api_path, string app, string stream, StreamUrlCapture &publish, StreamUrlCapture &play)
{
    srs_error_t err = srs_success;

    MockAppConfig config;
    MockRtcApiServerForPlay server;
    MockResponseWriter writer;

    MockRtcWhipForStreamUrlRule whip(&server);
    whip.config_ = &config;

    MockHttpMessageForRtcApi message;
    HELPER_EXPECT_SUCCESS(message.set_url("http://127.0.0.1:1985" + api_path, false));
    message.set_method(SRS_CONSTS_HTTP_POST);
    message.body_content_ = MULTI_LEVEL_SDP;
    message.query_params_["app"] = app;
    message.query_params_["stream"] = stream;
    message.query_params_["ice-ufrag"] = "testufrag123";
    message.query_params_["ice-pwd"] = "testpassword1234567890";
    message.mock_conn_->remote_ip_ = "192.168.1.100";

    SrsRtcUserConfig ruc;
    HELPER_EXPECT_SUCCESS(whip.do_serve_http_with(&writer, &message, &ruc));

    publish = whip.mock_publish_->captured_;
    play = whip.mock_play_->captured_;

    whip.config_ = NULL;
}

// Build the JSON body of the deprecated WebRTC play and publish APIs for a stream URL.
static string rtc_api_body(string streamurl)
{
    SrsUniquePtr<SrsJsonObject> body(SrsJsonAny::object());
    body->set("sdp", SrsJsonAny::str(MULTI_LEVEL_SDP));
    body->set("streamurl", SrsJsonAny::str(streamurl.c_str()));
    body->set("clientip", SrsJsonAny::str("192.168.1.100"));
    return body->dumps();
}

// Resolve an HTTP-FLV viewer of the given path against the stream a publisher mounted.
static void resolve_http_flv_viewer(ISrsRequest *publisher, string path, StreamUrlCapture &viewer)
{
    MockHttpMessageForStreamUrlRule message;
    message.path_ = path;
    MockResponseWriter writer;
    MockBufferCache cache;
    MockStatisticForLiveStream stat;
    MockSecurityForStreamUrlRule security;
    MockAppConfigForLiveStreamHooks config;
    MockHttpHooksForLiveStream hooks;

    SrsLiveStream stream(publisher, &cache);
    stream.stat_ = &stat;
    srs_freep(stream.security_);
    stream.security_ = &security;
    stream.config_ = &config;
    stream.hooks_ = &hooks;
    stream.entry_ = new SrsHttpMuxEntry();
    stream.entry_->enabled = true;

    srs_error_t err = stream.serve_http(&writer, &message);
    srs_freep(err);

    viewer = security.captured_;

    stream.stat_ = NULL;
    stream.security_ = NULL;
    stream.config_ = NULL;
    stream.hooks_ = NULL;
    srs_freep(stream.entry_);
}

// RTMP publisher or player: the multi-level app arrives in the connect tcUrl and the stream in
// publish or play. This is the reference behavior every other protocol must match.
VOID TEST(StreamUrlRuleTest, RtmpServerMultiLevelApp)
{
    SrsRequest req;
    req.tcUrl_ = "rtmp://127.0.0.1:1935/" MULTI_LEVEL_APP;
    req.stream_ = MULTI_LEVEL_STREAM "?token=abc";

    srs_net_url_parse_tcurl(req.tcUrl_, req.schema_, req.host_, req.vhost_, req.app_, req.stream_, req.port_, req.param_);

    EXPECT_REQUEST_FOLDER_AND_FILE(&req);
    EXPECT_STREQ("?token=abc", req.param_.c_str());
}

// RTMP publisher or player whose client splits the path at the first slash, so the stream carries
// folders. The tcUrl parser already normalizes it; this is the normalization the rule requires.
VOID TEST(StreamUrlRuleTest, RtmpServerStreamWithFoldersIsNormalized)
{
    SrsRequest req;
    req.tcUrl_ = "rtmp://127.0.0.1:1935/tenant-1";
    req.stream_ = "live/0/0/" MULTI_LEVEL_STREAM;

    srs_net_url_parse_tcurl(req.tcUrl_, req.schema_, req.host_, req.vhost_, req.app_, req.stream_, req.port_, req.param_);

    EXPECT_REQUEST_FOLDER_AND_FILE(&req);
}

// RTMP publisher or player that sends an empty stream name, so SRS guesses the stream from the
// app. The guess must take the last segment as the stream, not everything after the first slash.
VOID TEST(StreamUrlRuleTest, RtmpServerGuessedStreamIsTheLastSegment)
{
    string app = MULTI_LEVEL_PATH;
    string param;
    string stream;

    srs_net_url_guess_stream(app, param, stream);

    EXPECT_STREQ(MULTI_LEVEL_APP, app.c_str());
    EXPECT_STREQ(MULTI_LEVEL_STREAM, stream.c_str());
    EXPECT_STREQ("", param.c_str());
}

// The same guess when the app also carries the query: the query is moved to the param.
VOID TEST(StreamUrlRuleTest, RtmpServerGuessedStreamWithQuery)
{
    string app = MULTI_LEVEL_PATH "?token=abc";
    string param;
    string stream;

    srs_net_url_guess_stream(app, param, stream);

    EXPECT_STREQ(MULTI_LEVEL_APP, app.c_str());
    EXPECT_STREQ(MULTI_LEVEL_STREAM, stream.c_str());
    EXPECT_STREQ("?token=abc", param.c_str());
}

// A slash only inside the query is part of a param value, not a folder, so there is no stream to
// guess from the app. The last slash must be searched before the query only.
VOID TEST(StreamUrlRuleTest, RtmpServerGuessIgnoresSlashInQuery)
{
    string app = "live?secret=a/b";
    string param;
    string stream;

    srs_net_url_guess_stream(app, param, stream);

    EXPECT_STREQ("live?secret=a/b", app.c_str());
    EXPECT_STREQ("", stream.c_str());
    EXPECT_STREQ("", param.c_str());
}

// RTMP client, used by GB28181 output, ingest, edge pull, and forward: the whole URL is resolved in
// the SrsBasicRtmpClient constructor.
VOID TEST(StreamUrlRuleTest, RtmpClientMultiLevelUrl)
{
    SrsBasicRtmpClient client("rtmp://127.0.0.1:1935/" MULTI_LEVEL_PATH "?token=abc", 0, 0);

    EXPECT_REQUEST_FOLDER_AND_FILE(client.req_);
    EXPECT_STREQ("?token=abc", client.req_->param_.c_str());
}

// SRT publisher, with the standard resource key. Today SRT splits at the first slash and produces
// app "tenant-1" and stream "live/0/0/AZR-001", which is why HTTP-FLV and HTTP-TS cannot play it.
VOID TEST(StreamUrlRuleTest, SrtPublisherMultiLevelStreamId)
{
    MockAppConfig config;
    SrtMode mode;
    SrsRequest req;

    EXPECT_TRUE(srs_srt_streamid_to_request(&config, "#!::r=" MULTI_LEVEL_PATH ",m=publish", mode, &req));

    EXPECT_EQ(SrtModePush, mode);
    EXPECT_REQUEST_FOLDER_AND_FILE(&req);
}

// SRT player, with query parameters on the resource key.
VOID TEST(StreamUrlRuleTest, SrtPlayerMultiLevelStreamIdWithParams)
{
    MockAppConfig config;
    SrtMode mode;
    SrsRequest req;

    EXPECT_TRUE(srs_srt_streamid_to_request(&config, "#!::r=" MULTI_LEVEL_PATH "?token=abc,m=request", mode, &req));

    EXPECT_EQ(SrtModePull, mode);
    EXPECT_REQUEST_FOLDER_AND_FILE(&req);
    EXPECT_STREQ("token=abc", req.param_.c_str());
}

// SRT with the host key carrying the vhost and the path, the form some encoders send.
VOID TEST(StreamUrlRuleTest, SrtHostKeyMultiLevelStreamId)
{
    MockAppConfig config;
    SrtMode mode;
    SrsRequest req;

    EXPECT_TRUE(srs_srt_streamid_to_request(&config, "#!::h=srs.srt.com.cn/" MULTI_LEVEL_PATH ",m=publish", mode, &req));

    EXPECT_STREQ("srs.srt.com.cn", req.vhost_.c_str());
    EXPECT_REQUEST_FOLDER_AND_FILE(&req);
}

// HTTP-FLV, HTTP-TS, and HLS viewers: the request path is resolved into a stream by to_request(),
// which also feeds the HTTP hooks and the HLS statistics.
VOID TEST(StreamUrlRuleTest, HttpPathMultiLevelForEveryExtension)
{
    const char *extensions[] = {".flv", ".ts", ".m3u8"};
    for (int i = 0; i < (int)(sizeof(extensions) / sizeof(extensions[0])); i++) {
        string ext = extensions[i];

        SrsHttpMessage message;
        srs_error_t err = message.set_url("http://127.0.0.1:8080" MULTI_LEVEL_URL + ext + "?token=abc", false);
        HELPER_EXPECT_SUCCESS(err);

        SrsUniquePtr<ISrsRequest> req(message.to_request("ossrs.net"));

        EXPECT_REQUEST_FOLDER_AND_FILE(req.get()) << "ext=" << ext;
    }
}

// HTTP-FLV viewer of a stream published over RTMP: the viewer is resolved to the publisher's stream.
VOID TEST(StreamUrlRuleTest, HttpFlvViewerOfRtmpPublisher)
{
    SrsRequest publisher;
    publisher.tcUrl_ = "rtmp://127.0.0.1:1935/" MULTI_LEVEL_APP;
    publisher.stream_ = MULTI_LEVEL_STREAM;
    srs_net_url_parse_tcurl(publisher.tcUrl_, publisher.schema_, publisher.host_, publisher.vhost_, publisher.app_,
                            publisher.stream_, publisher.port_, publisher.param_);

    StreamUrlCapture viewer;
    resolve_http_flv_viewer(&publisher, MULTI_LEVEL_URL ".flv", viewer);

    EXPECT_EQ(1, viewer.count_);
    EXPECT_FOLDER_AND_FILE(viewer.app_, viewer.stream_, viewer.url_);
    EXPECT_STREQ(publisher.get_stream_url().c_str(), viewer.url_.c_str());
}

// HTTP-FLV viewer of a stream published over SRT, the #4739 report: the viewer must be resolved to
// the publisher's stream. Today it resolves to "/tenant-1/AZR-001", a source with no publisher, and
// the viewer waits forever for media.
VOID TEST(StreamUrlRuleTest, HttpFlvViewerOfSrtPublisher)
{
    MockAppConfig config;
    SrtMode mode;
    SrsRequest publisher;
    EXPECT_TRUE(srs_srt_streamid_to_request(&config, "#!::r=" MULTI_LEVEL_PATH ",m=publish", mode, &publisher));

    StreamUrlCapture viewer;
    resolve_http_flv_viewer(&publisher, MULTI_LEVEL_URL ".flv", viewer);

    EXPECT_EQ(1, viewer.count_);
    EXPECT_FOLDER_AND_FILE(viewer.app_, viewer.stream_, viewer.url_);
    EXPECT_STREQ(publisher.get_stream_url().c_str(), viewer.url_.c_str());
}

// HTTP-FLV viewer that arrives before the stream is published, or on an edge. The template
// "[vhost]/[app]/[stream].flv" must accept a multi-level app and mount the stream, as it does for a
// single-level app. Today the slash count differs from the template, so the viewer gets 404.
VOID TEST(StreamUrlRuleTest, HttpFlvViewerBeforePublish)
{
    srs_error_t err = srs_success;

    // Declared before the server so the server, which references it, is destroyed first.
    SrsUniquePtr<MockAppConfigForHttpStreamServer> config(new MockAppConfigForHttpStreamServer());
    config->http_remux_enabled_ = true;
    config->http_remux_mount_ = "[vhost]/[app]/[stream].flv";
    config->vhost_directive_ = new SrsConfDirective();
    config->vhost_directive_->name_ = "vhost";
    config->vhost_directive_->args_.push_back("test.vhost");

    SrsUniquePtr<SrsHttpStreamServer> server(new SrsHttpStreamServer());
    srs_freep(server->async_);
    server->async_ = new MockAsyncCallWorker();
    server->config_ = config.get();
    server->templateHandlers_["test.vhost"] = new SrsLiveEntry("[vhost]/[app]/[stream].flv");

    SrsUniquePtr<MockHttpMessageForDynamicMatch> message(new MockHttpMessageForDynamicMatch());
    message->path_ = MULTI_LEVEL_URL ".flv";
    message->ext_ = ".flv";
    HELPER_EXPECT_SUCCESS(message->set_url(message->path_, false));

    ISrsHttpHandler *handler = NULL;
    HELPER_EXPECT_SUCCESS(server->dynamic_match(message.get(), &handler));

    EXPECT_TRUE(handler != NULL);
    ASSERT_EQ(1, (int)server->streamHandlers_.size());

    SrsLiveEntry *entry = server->streamHandlers_.begin()->second;
    EXPECT_STREQ("test.vhost" MULTI_LEVEL_URL ".flv", entry->mount_.c_str());
    EXPECT_REQUEST_FOLDER_AND_FILE(entry->req_);
}

// WebRTC deprecated JSON play API: the stream URL is resolved from the streamurl field.
VOID TEST(StreamUrlRuleTest, RtcPlayApiMultiLevelStreamUrl)
{
    srs_error_t err = srs_success;

    MockAppConfig config;
    MockRtcApiServerForPlay server;
    MockResponseWriter writer;
    MockHttpMessageForRtcApi message;
    message.body_content_ = rtc_api_body("webrtc://127.0.0.1/" MULTI_LEVEL_PATH "?token=abc");

    MockRtcPlayForStreamUrlRule api(&server);
    api.config_ = &config;

    SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(api.do_serve_http(&writer, &message, res.get()));

    EXPECT_EQ(1, api.captured_.count_);
    EXPECT_FOLDER_AND_FILE(api.captured_.app_, api.captured_.stream_, api.captured_.url_);

    api.config_ = NULL;
}

// WebRTC deprecated JSON publish API: the stream URL is resolved from the streamurl field.
VOID TEST(StreamUrlRuleTest, RtcPublishApiMultiLevelStreamUrl)
{
    srs_error_t err = srs_success;

    MockAppConfig config;
    MockRtcApiServerForPlay server;
    MockResponseWriter writer;
    MockHttpMessageForRtcApi message;
    message.body_content_ = rtc_api_body("webrtc://127.0.0.1/" MULTI_LEVEL_PATH "?token=abc");

    MockRtcPublishForStreamUrlRule api(&server);
    api.config_ = &config;

    SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(api.do_serve_http(&writer, &message, res.get()));

    EXPECT_EQ(1, api.captured_.count_);
    EXPECT_FOLDER_AND_FILE(api.captured_.app_, api.captured_.stream_, api.captured_.url_);

    api.config_ = NULL;
}

// WHIP publisher that sends the multi-level app and a single stream name.
VOID TEST(StreamUrlRuleTest, WhipMultiLevelApp)
{
    StreamUrlCapture publish, play;
    resolve_whip("/rtc/v1/whip/", MULTI_LEVEL_APP, MULTI_LEVEL_STREAM, publish, play);

    EXPECT_EQ(1, publish.count_);
    EXPECT_EQ(0, play.count_);
    EXPECT_FOLDER_AND_FILE(publish.app_, publish.stream_, publish.url_);
}

// WHIP publisher that splits the path at the first slash, so the stream carries folders. It must
// be normalized like RTMP. Today the stream keeps its slashes and HTTP-FLV cannot play it.
VOID TEST(StreamUrlRuleTest, WhipStreamWithFoldersIsNormalized)
{
    StreamUrlCapture publish, play;
    resolve_whip("/rtc/v1/whip/", "tenant-1", "live/0/0/" MULTI_LEVEL_STREAM, publish, play);

    EXPECT_EQ(1, publish.count_);
    EXPECT_FOLDER_AND_FILE(publish.app_, publish.stream_, publish.url_);
}

// WHEP player that sends the multi-level app and a single stream name.
VOID TEST(StreamUrlRuleTest, WhepMultiLevelApp)
{
    StreamUrlCapture publish, play;
    resolve_whip("/rtc/v1/whep/", MULTI_LEVEL_APP, MULTI_LEVEL_STREAM, publish, play);

    EXPECT_EQ(0, publish.count_);
    EXPECT_EQ(1, play.count_);
    EXPECT_FOLDER_AND_FILE(play.app_, play.stream_, play.url_);
}

// WHEP player that splits the path at the first slash, so the stream carries folders.
VOID TEST(StreamUrlRuleTest, WhepStreamWithFoldersIsNormalized)
{
    StreamUrlCapture publish, play;
    resolve_whip("/rtc/v1/whep/", "tenant-1", "live/0/0/" MULTI_LEVEL_STREAM, publish, play);

    EXPECT_EQ(1, play.count_);
    EXPECT_FOLDER_AND_FILE(play.app_, play.stream_, play.url_);
}
