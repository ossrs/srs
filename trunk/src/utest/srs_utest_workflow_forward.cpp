/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2026 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_utest_workflow_forward.hpp>

#include <srs_app_rtmp_conn.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_json.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_http.hpp>

// Mock ISrsAppFactory implementation
MockAppFactoryForForwarder::MockAppFactoryForForwarder()
{
    mock_rtmp_client_ = NULL;
}

MockAppFactoryForForwarder::~MockAppFactoryForForwarder()
{
    // Don't free mock_rtmp_client_ - it's managed by the test
}

ISrsBasicRtmpClient *MockAppFactoryForForwarder::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    if (mock_rtmp_client_) {
        mock_rtmp_client_->set_url(url);
    }
    return mock_rtmp_client_;
}

MockHttpHooksForForwardBackendFailure::MockHttpHooksForForwardBackendFailure()
{
    on_forward_backend_count_ = 0;
    on_forward_backend_error_ = srs_success;
}

MockHttpHooksForForwardBackendFailure::~MockHttpHooksForForwardBackendFailure()
{
    srs_freep(on_forward_backend_error_);
}

srs_error_t MockHttpHooksForForwardBackendFailure::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    on_forward_backend_count_++;
    return srs_error_copy(on_forward_backend_error_);
}

void MockHttpHooksForForwardBackendFailure::set_on_forward_backend_error(srs_error_t err)
{
    srs_freep(on_forward_backend_error_);
    on_forward_backend_error_ = srs_error_copy(err);
}

// A dynamic-forward backend is an external dependency. If it fails, SRS should reject the
// current publish attempt and roll back the partially acquired live source so the publisher
// can retry. This regression test defines the required rollback behavior.
VOID TEST(BasicWorkflowForwardTest, ForwardBackendFailureRollsBackPublishState)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<MockHttpHooksForForwardBackendFailure> mock_hooks(new MockHttpHooksForForwardBackendFailure());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "stream1"));

    mock_config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    mock_hooks->set_on_forward_backend_error(srs_error_new(ERROR_HTTP_STATUS_INVALID, "mock HTTP 500"));

    SrsLiveSource *raw_source = new SrsLiveSource();
    raw_source->config_ = mock_config.get();
    raw_source->stat_ = mock_stat.get();
    raw_source->req_ = req->copy();
    raw_source->assemble();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    SrsOriginHub *hub = new SrsOriginHub();
    hub->config_ = mock_config.get();
    hub->stat_ = mock_stat.get();
    hub->hooks_ = mock_hooks.get();
    HELPER_EXPECT_SUCCESS(hub->initialize(source, raw_source->req_));
    raw_source->hub_ = hub;

    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransport(), "127.0.0.1", 1935));
    conn->config_ = mock_config.get();
    conn->stat_ = mock_stat.get();
    conn->hooks_ = mock_hooks.get();
    conn->assemble();

    conn->info_->req_->vhost_ = req->vhost_;
    conn->info_->req_->app_ = req->app_;
    conn->info_->req_->stream_ = req->stream_;
    conn->info_->edge_ = false;

    // Each attempt should reach the backend and fail with its original error. Most importantly,
    // the failed attempt must leave the live source available for the next publisher.
    for (int i = 0; i < 2; i++) {
        err = conn->publishing(source);
        EXPECT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_HTTP_STATUS_INVALID, srs_error_code(err));
        srs_freep(err);

        EXPECT_TRUE(source->can_publish(false));
    }
    EXPECT_EQ(2, mock_hooks->on_forward_backend_count_);
}

// This test is used to verify the basic workflow of the forwarding.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowForwardTest, ManuallyVerifyForwardingHostport)
{
    srs_error_t err;

    // Create mock objects
    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create forwarder
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));

    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();

    // Configure destination with traditional host:port format
    std::string destination = "127.0.0.1:19350";

    // Step 1: Initialize forwarder with destination
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), destination));
    EXPECT_STREQ("127.0.0.1:19350", forwarder->ep_forward_.c_str());

    // Generate a video sequenece header message.
    if (true) {
        // Create a real H.264 video message with proper format.
        // H.264 video format in RTMP/FLV:
        // Byte 0: (FrameType << 4) | CodecID (CodecID=7 for H.264)
        //         FrameType=5 (disposable inter frame), CodecID=7 (H.264) = 0x57
        // Byte 1: AVCPacketType (0=sequence header, 1=NALU, 2=end of sequence)
        // Byte 2-4: CompositionTime (3bytes little-endian int24)
        // Remaining bytes: H.264 data
        int payload_size = 10;
        SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
        msg->header_.initialize_video(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in H.264 video data
        SrsBuffer stream(msg->payload(), payload_size);
        // Frame type & Codec ID: Disposable inter frame (5) + H.264 (7) = 0x57
        stream.write_1bytes(0x57);
        // AVC packet type: 0 = sequence header
        stream.write_1bytes(0x00);
        // Composition time: 0 (3bytes little-endian int24)
        stream.write_3bytes(0x000000);
        // H.264 raw data (5 bytes of dummy video data) - SPS and PPS
        for (int i = 0; i < 5; i++) {
            stream.write_1bytes(0x00);
        }

        // Convert to SrsMediaPacket
        SrsMediaPacket *pkt = new SrsMediaPacket();
        msg->to_msg(pkt);
        forwarder->sh_video_ = pkt;
    }

    // Generate the audio sequence header.
    if (true) {
        // Create a real AAC audio message with proper format.
        // AAC audio format in RTMP/FLV:
        // Byte 0: (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
        //         SoundFormat=10 (AAC), SoundRate=3 (44kHz), SoundSize=1 (16-bit), SoundType=1 (stereo)
        //         = 0xAF
        // Byte 1: AACPacketType (0=sequence header, 1=raw data)
        // Remaining bytes: AAC data
        int payload_size = 10;
        SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
        msg->header_.initialize_audio(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in AAC audio data
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 0 = sequence header
        stream.write_1bytes(0x00);
        // AAC sequence header data (8 bytes of dummy audio data)
        for (int i = 0; i < 8; i++) {
            stream.write_1bytes(0x00);
        }

        // Convert to SrsMediaPacket
        SrsMediaPacket *pkt = new SrsMediaPacket();
        msg->to_msg(pkt);
        forwarder->sh_audio_ = pkt;
    }

    // Step 2: Call on_publish to start forwarding
    HELPER_EXPECT_SUCCESS(forwarder->on_publish());

    // Wait for forwarder to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the forwarder.
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/stream1?vhost=test.vhost", mock_sdk->url_.c_str());
    EXPECT_EQ(2, mock_sdk->send_message_count_);

    // Generate an audio message.
    if (true) {
        // Create a real AAC audio message with proper format.
        // AAC audio format in RTMP/FLV:
        // Byte 0: (SoundFormat << 4) | (SoundRate << 2) | (SoundSize << 1) | SoundType
        //         SoundFormat=10 (AAC), SoundRate=3 (44kHz), SoundSize=1 (16-bit), SoundType=1 (stereo)
        //         = 0xAF
        // Byte 1: AACPacketType (0=sequence header, 1=raw data)
        // Remaining bytes: AAC data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_audio(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in AAC audio data
        SrsBuffer stream(msg->payload(), payload_size);
        // Audio format byte: AAC(10), 44kHz(3), 16-bit(1), stereo(1) = 0xAF
        stream.write_1bytes(0xAF);
        // AAC packet type: 1 = AAC raw data
        stream.write_1bytes(0x01);
        // AAC raw data (8 bytes of dummy audio data)
        for (int i = 0; i < 8; i++) {
            stream.write_1bytes(0x00);
        }

        // Convert to SrsMediaPacket
        SrsUniquePtr<SrsMediaPacket> pkt(new SrsMediaPacket());
        msg->to_msg(pkt.get());
        HELPER_EXPECT_SUCCESS(forwarder->on_video(pkt.get()));

        // Use this message to wakeup the forwarder coroutine.
        mock_sdk->recv_msgs_.push_back(msg);
        mock_sdk->cond_->signal();

        // Wait for forwarder to process the message
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the message is sent to the server.
        EXPECT_EQ(1, mock_sdk->send_and_free_messages_count_);
    }

    // Notify forwarder to quit.
    mock_sdk->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_sdk->cond_->signal();

    // Stop the forwarder coroutine.
    forwarder->on_unpublish();

    // Wait for forwarder to stop
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

// This test is used to verify the forwarding with query parameters (tokens).
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowForwardTest, ManuallyVerifyForwardingWithToken)
{
    srs_error_t err;

    // Create mock objects with query parameters in the request
    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    // Set query parameters that should be forwarded (e.g., authentication tokens)
    req->param_ = "?sdkappid=1007&userid=5fe6e61e&usersig=eJyToken123";

    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());

    // Create forwarder
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));

    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();

    // Configure destination with traditional host:port format
    std::string destination = "127.0.0.1:19350";

    // Step 1: Initialize forwarder with destination
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), destination));
    EXPECT_STREQ("127.0.0.1:19350", forwarder->ep_forward_.c_str());

    // Step 2: Call on_publish to start forwarding
    HELPER_EXPECT_SUCCESS(forwarder->on_publish());

    // Wait for forwarder to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the forwarder URL includes the query parameters from the original request
    // The expected URL should be: rtmp://127.0.0.1:19350/live/stream1?sdkappid=1007&userid=5fe6e61e&usersig=eJyToken123&vhost=test.vhost
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/stream1?sdkappid=1007&userid=5fe6e61e&usersig=eJyToken123&vhost=test.vhost", mock_sdk->url_.c_str());

    // Notify forwarder to quit.
    mock_sdk->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_sdk->cond_->signal();

    // Stop the forwarder coroutine.
    forwarder->on_unpublish();

    // Wait for forwarder to stop
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

// Expect the error code, and free the error.
#define HELPER_EXPECT_FAILED_CODE(x, code)              \
    if (true) {                                         \
        err = x;                                        \
        EXPECT_TRUE(err != srs_success);                \
        EXPECT_EQ((int)code, (int)srs_error_code(err)); \
        srs_freep(err);                                 \
    }

MockForwardDestinationHandler::MockForwardDestinationHandler()
{
    dumps_count_ = 0;
    add_error_ = srs_success;
}

MockForwardDestinationHandler::~MockForwardDestinationHandler()
{
    srs_freep(add_error_);
}

srs_error_t MockForwardDestinationHandler::on_forward_destination_add(SrsForwardDestination *dest)
{
    added_.push_back(dest->id_);
    return srs_error_copy(add_error_);
}

void MockForwardDestinationHandler::on_forward_destination_remove(std::string id)
{
    removed_.push_back(id);
}

void MockForwardDestinationHandler::on_forward_destination_dumps(std::string id, SrsJsonObject *obj)
{
    dumps_count_++;
    obj->set("state", SrsJsonAny::str("forwarding"));
}

MockHttpMessageForForwards::MockHttpMessageForForwards(uint8_t method, std::string url, std::string body) : SrsHttpMessage()
{
    mock_conn_ = new MockHttpConn();
    set_connection(mock_conn_);
    method_ = method;
    body_ = body;
    srs_error_t err = set_url(url, false);
    srs_freep(err);
}

MockHttpMessageForForwards::~MockHttpMessageForForwards()
{
    srs_freep(mock_conn_);
}

srs_error_t MockHttpMessageForForwards::body_read_all(std::string &body)
{
    body = body_;
    return srs_success;
}

uint8_t MockHttpMessageForForwards::method()
{
    return method_;
}

// Create a forward destination for the stream /live/stream1 by default.
static SrsForwardDestination *mock_forward_destination(std::string id, std::string url, std::string stream = "stream1")
{
    SrsForwardDestination *dest = new SrsForwardDestination();
    dest->id_ = id;
    dest->vhost_ = "__defaultVhost__";
    dest->app_ = "live";
    dest->stream_ = stream;
    dest->url_ = url;
    return dest;
}

// Create a H.264 video packet, which is a sequence header, a keyframe or an inter frame.
static SrsMediaPacket *mock_forward_video(bool keyframe, bool sequence_header)
{
    int payload_size = 10;
    SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
    msg->header_.initialize_video(payload_size, 0, 1);
    msg->create_payload(payload_size);

    SrsBuffer stream(msg->payload(), payload_size);
    // FrameType 1 is keyframe, 2 is inter frame, and CodecID 7 is H.264.
    stream.write_1bytes(keyframe ? 0x17 : 0x27);
    // AVCPacketType 0 is sequence header, 1 is NALU.
    stream.write_1bytes(sequence_header ? 0x00 : 0x01);
    stream.write_3bytes(0x000000);
    for (int i = 0; i < 5; i++) {
        stream.write_1bytes(0x00);
    }

    SrsMediaPacket *pkt = new SrsMediaPacket();
    msg->to_msg(pkt);
    return pkt;
}

// The forward destination url carries the stream key of the third-party platform, so it must
// never be exposed by logs or the HTTP API.
VOID TEST(ForwardDestinationsTest, RedactUrl)
{
    EXPECT_STREQ("rtmp://a.rtmp.youtube.com/live2/***", srs_forward_redact_url("rtmp://a.rtmp.youtube.com/live2/xxxx-yyyy").c_str());
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/***", srs_forward_redact_url("rtmp://127.0.0.1:19350/live/key?token=abc").c_str());
    EXPECT_STREQ("rtmp://127.0.0.1/app/***", srs_forward_redact_url("rtmp://127.0.0.1/app?vhost=v&token=x/key").c_str());
    EXPECT_STREQ("rtmp://127.0.0.1/***", srs_forward_redact_url("rtmp://127.0.0.1/key").c_str());
    EXPECT_STREQ("***", srs_forward_redact_url("not-an-url").c_str());
}

VOID TEST(ForwardDestinationsTest, RedactError)
{
    EXPECT_STREQ("publish failed, stream=***, stream_id=1", srs_forward_redact_error("publish failed, stream=key?token=x, stream_id=1").c_str());
    EXPECT_STREQ("send FMLE publish publish failed. stream=***", srs_forward_redact_error("send FMLE publish publish failed. stream=key").c_str());
    EXPECT_STREQ("a stream=*** b stream=***", srs_forward_redact_error("a stream=k1 b stream=k2").c_str());
    // Never change the other parts, even the stream name is short, for example, c.
    EXPECT_STREQ("code=1011(SocketTimeout) : forward : send messages", srs_forward_redact_error("code=1011(SocketTimeout) : forward : send messages").c_str());
}

// A destination is keyed by its id. Adding the same destination again is a no-op, so a controller
// can safely apply its desired state again, for example, after SRS restarts.
VOID TEST(ForwardDestinationsTest, AddFindFetchRemove)
{
    srs_error_t err;

    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());

    SrsUniquePtr<SrsForwardDestination> d1(mock_forward_destination("out1", "rtmp://127.0.0.1:19350/live/s1"));
    EXPECT_STREQ("/live/stream1", d1->stream_url().c_str());

    bool created = false;
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    EXPECT_TRUE(created);

    // The manager copies the destination.
    SrsForwardDestination *found = dests->find("out1");
    ASSERT_TRUE(found != NULL);
    EXPECT_TRUE(found != d1.get());
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/s1", found->url_.c_str());
    EXPECT_TRUE(found->created_at_ > 0);

    // Add the same destination again, it's ignored.
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    EXPECT_FALSE(created);

    // The same id with another url is rejected.
    SrsUniquePtr<SrsForwardDestination> d1b(mock_forward_destination("out1", "rtmp://127.0.0.1:19350/live/other"));
    HELPER_EXPECT_FAILED_CODE(dests->add(d1b.get(), &created), ERROR_FORWARD_DEST_EXISTS);

    // Destinations of other stream.
    SrsUniquePtr<SrsForwardDestination> d2(mock_forward_destination("out2", "rtmp://127.0.0.1:19350/live/s2", "stream2"));
    HELPER_EXPECT_SUCCESS(dests->add(d2.get(), &created));

    std::vector<SrsForwardDestination *> list;
    dests->fetch("/live/stream1", list);
    ASSERT_EQ(1, (int)list.size());
    EXPECT_STREQ("out1", list[0]->id_.c_str());

    list.clear();
    dests->fetch("", list);
    EXPECT_EQ(2, (int)list.size());

    HELPER_EXPECT_SUCCESS(dests->remove("out1"));
    EXPECT_TRUE(dests->find("out1") == NULL);
    EXPECT_TRUE(dests->find("out2") != NULL);

    HELPER_EXPECT_FAILED_CODE(dests->remove("out1"), ERROR_FORWARD_DEST_NOT_FOUND);
}

// Reject the destination which SRS can't forward to, and the id which can't be used in API path.
VOID TEST(ForwardDestinationsTest, RejectInvalid)
{
    srs_error_t err;

    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    bool created = false;

    const char *invalid_urls[] = {
        "",
        "rtmps://live-api-s.facebook.com:443/rtmp/key",
        "srt://127.0.0.1:10080",
        "http://127.0.0.1:8080/live/key",
        "rtmp://",
        "rtmp://127.0.0.1",
        "rtmp://127.0.0.1/",
        "rtmp://127.0.0.1/live",
        "rtmp://127.0.0.1/live/",
    };
    for (int i = 0; i < (int)(sizeof(invalid_urls) / sizeof(invalid_urls[0])); i++) {
        SrsUniquePtr<SrsForwardDestination> d(mock_forward_destination("out1", invalid_urls[i]));
        HELPER_EXPECT_FAILED_CODE(dests->add(d.get(), &created), ERROR_FORWARD_DEST_INVALID);
    }

    const char *invalid_ids[] = {"a/b", "a b", "a?b", "a.b", "0123456789012345678901234567890123456789012345678901234567890123456789"};
    for (int i = 0; i < (int)(sizeof(invalid_ids) / sizeof(invalid_ids[0])); i++) {
        SrsUniquePtr<SrsForwardDestination> d(mock_forward_destination(invalid_ids[i], "rtmp://127.0.0.1/live/key"));
        HELPER_EXPECT_FAILED_CODE(dests->add(d.get(), &created), ERROR_FORWARD_DEST_INVALID);
    }

    // The stream to forward is required.
    SrsUniquePtr<SrsForwardDestination> d(mock_forward_destination("out1", "rtmp://127.0.0.1/live/key", ""));
    HELPER_EXPECT_FAILED_CODE(dests->add(d.get(), &created), ERROR_FORWARD_DEST_INVALID);

    std::vector<SrsForwardDestination *> list;
    dests->fetch("", list);
    EXPECT_EQ(0, (int)list.size());
}

// Generate an id if not specified by the caller.
VOID TEST(ForwardDestinationsTest, GenerateId)
{
    srs_error_t err;

    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    bool created = false;

    SrsUniquePtr<SrsForwardDestination> d(mock_forward_destination("", "rtmp://127.0.0.1/live/key"));
    HELPER_EXPECT_SUCCESS(dests->add(d.get(), &created));
    EXPECT_TRUE(created);

    std::vector<SrsForwardDestination *> list;
    dests->fetch("", list);
    ASSERT_EQ(1, (int)list.size());
    EXPECT_FALSE(list[0]->id_.empty());
    // The caller's object is not changed, the id is returned by the API response.
    EXPECT_TRUE(d->id_.empty());
}

// The number of destinations is limited, to bound the memory and the outbound connections.
VOID TEST(ForwardDestinationsTest, Limit)
{
    srs_error_t err;

    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    bool created = false;

    for (int i = 0; i < SRS_FORWARD_DESTINATIONS_MAX; i++) {
        SrsUniquePtr<SrsForwardDestination> d(mock_forward_destination(srs_strconv_format_int(i), "rtmp://127.0.0.1/live/key"));
        HELPER_EXPECT_SUCCESS(dests->add(d.get(), &created));
    }

    SrsUniquePtr<SrsForwardDestination> d(mock_forward_destination("more", "rtmp://127.0.0.1/live/key"));
    HELPER_EXPECT_FAILED_CODE(dests->add(d.get(), &created), ERROR_FORWARD_DEST_LIMIT);

    // Adding an existing destination is still ok.
    SrsUniquePtr<SrsForwardDestination> d0(mock_forward_destination("0", "rtmp://127.0.0.1/live/key"));
    HELPER_EXPECT_SUCCESS(dests->add(d0.get(), &created));
    EXPECT_FALSE(created);
}

// Only the handler of the publishing stream is notified, so adding or removing a destination
// never touches the other streams.
VOID TEST(ForwardDestinationsTest, NotifyPublishingStream)
{
    srs_error_t err;

    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    MockForwardDestinationHandler h1, h2;
    bool created = false;

    dests->subscribe("/live/stream1", &h1);
    dests->subscribe("/live/stream2", &h2);

    SrsUniquePtr<SrsForwardDestination> d1(mock_forward_destination("out1", "rtmp://127.0.0.1/live/k1"));
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    ASSERT_EQ(1, (int)h1.added_.size());
    EXPECT_STREQ("out1", h1.added_[0].c_str());
    EXPECT_EQ(0, (int)h2.added_.size());

    // Adding the same destination again does not start another forwarder.
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    EXPECT_EQ(1, (int)h1.added_.size());

    // The state is dumped by the handler.
    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        dests->dumps(dests->find("out1"), obj.get());
        EXPECT_EQ(1, h1.dumps_count_);
        std::string json = obj->dumps();
        EXPECT_TRUE(json.find("\"id\":\"out1\"") != std::string::npos);
        EXPECT_TRUE(json.find("\"app\":\"live\"") != std::string::npos);
        EXPECT_TRUE(json.find("\"stream\":\"stream1\"") != std::string::npos);
        EXPECT_TRUE(json.find("\"url\":\"rtmp://127.0.0.1/live/***\"") != std::string::npos);
        EXPECT_TRUE(json.find("\"state\":\"forwarding\"") != std::string::npos);
        EXPECT_TRUE(json.find("k1") == std::string::npos);
    }

    HELPER_EXPECT_SUCCESS(dests->remove("out1"));
    ASSERT_EQ(1, (int)h1.removed_.size());
    EXPECT_STREQ("out1", h1.removed_[0].c_str());
    EXPECT_EQ(0, (int)h2.removed_.size());

    // A handler only unsubscribes itself.
    dests->unsubscribe("/live/stream1", &h2);
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    EXPECT_EQ(2, (int)h1.added_.size());

    // After unsubscribed, the destination is kept but idle.
    dests->unsubscribe("/live/stream1", &h1);
    HELPER_EXPECT_SUCCESS(dests->remove("out1"));
    EXPECT_EQ(1, (int)h1.removed_.size());

    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    EXPECT_EQ(2, (int)h1.added_.size());
    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        dests->dumps(dests->find("out1"), obj.get());
        EXPECT_TRUE(obj->dumps().find("\"state\":\"idle\"") != std::string::npos);
    }
}

// If the publishing stream fails to start forwarding, the destination is not added.
VOID TEST(ForwardDestinationsTest, RollbackWhenHandlerFails)
{
    srs_error_t err;

    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    MockForwardDestinationHandler h1;
    h1.add_error_ = srs_error_new(ERROR_FORWARD_DEST_INVALID, "mock start forwarder failed");
    dests->subscribe("/live/stream1", &h1);

    bool created = false;
    SrsUniquePtr<SrsForwardDestination> d1(mock_forward_destination("out1", "rtmp://127.0.0.1/live/k1"));
    HELPER_EXPECT_FAILED_CODE(dests->add(d1.get(), &created), ERROR_FORWARD_DEST_INVALID);
    EXPECT_TRUE(dests->find("out1") == NULL);
}

// The origin hub starts and stops a forwarder for each destination at runtime, and the other
// forwarders are not touched, the same object keeps running.
VOID TEST(ForwardDestinationsTest, HubAddRemoveKeepsOthers)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    mock_config->forward_api_ = true;
    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "stream1"));

    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());
    hub->config_ = mock_config.get();
    hub->forward_destinations_ = dests.get();
    hub->req_ = req.get();

    // Start forwarding, subscribed to the destinations of stream.
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());
    EXPECT_EQ(0, (int)hub->forwarders_.size());

    // Nobody listens on port 1, so the forwarders keep retrying without connected.
    bool created = false;
    SrsUniquePtr<SrsForwardDestination> d1(mock_forward_destination("out1", "rtmp://127.0.0.1:1/live/k1"));
    SrsUniquePtr<SrsForwardDestination> d2(mock_forward_destination("out2", "rtmp://127.0.0.1:1/live/k2"));
    SrsUniquePtr<SrsForwardDestination> d3(mock_forward_destination("out3", "rtmp://127.0.0.1:1/live/k3"));
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));
    HELPER_EXPECT_SUCCESS(dests->add(d2.get(), &created));
    HELPER_EXPECT_SUCCESS(dests->add(d3.get(), &created));
    ASSERT_EQ(3, (int)hub->forwarders_.size());
    ASSERT_EQ(3, (int)hub->dest_forwarders_.size());

    ISrsForwarder *f1 = hub->dest_forwarders_["out1"];
    ISrsForwarder *f3 = hub->dest_forwarders_["out3"];

    // Remove the one in the middle, the others are the same forwarders.
    HELPER_EXPECT_SUCCESS(dests->remove("out2"));
    ASSERT_EQ(2, (int)hub->forwarders_.size());
    EXPECT_TRUE(hub->forwarders_[0] == f1);
    EXPECT_TRUE(hub->forwarders_[1] == f3);
    EXPECT_TRUE(hub->dest_forwarders_.find("out2") == hub->dest_forwarders_.end());

    // The hub dumps the state of forwarder.
    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        dests->dumps(dests->find("out1"), obj.get());
        std::string json = obj->dumps();
        EXPECT_TRUE(json.find("\"state\"") != std::string::npos);
        EXPECT_TRUE(json.find("\"state\":\"idle\"") == std::string::npos);
    }

    // Unpublish stops all forwarders, but the destinations are kept for next publish.
    hub->destroy_forwarders();
    EXPECT_EQ(0, (int)hub->forwarders_.size());
    EXPECT_EQ(0, (int)hub->dest_forwarders_.size());
    std::vector<SrsForwardDestination *> list;
    dests->fetch("/live/stream1", list);
    EXPECT_EQ(2, (int)list.size());

    // After unpublish, adding a destination does not start a forwarder.
    HELPER_EXPECT_SUCCESS(dests->add(d2.get(), &created));
    EXPECT_EQ(0, (int)hub->forwarders_.size());

    // Publish again, the forwarders of all destinations are started.
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());
    EXPECT_EQ(3, (int)hub->forwarders_.size());
    EXPECT_EQ(3, (int)hub->dest_forwarders_.size());
    hub->destroy_forwarders();
}

// The destinations can't be managed by API unless enabled, so the forwarders are not started.
VOID TEST(ForwardDestinationsTest, HubIgnoresDestinationsIfApiDisabled)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1");
    mock_config->set_forward_destinations(destinations);
    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "stream1"));

    bool created = false;
    SrsUniquePtr<SrsForwardDestination> d1(mock_forward_destination("out1", "rtmp://127.0.0.1:1/live/k1"));
    HELPER_EXPECT_SUCCESS(dests->add(d1.get(), &created));

    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());
    hub->config_ = mock_config.get();
    hub->forward_destinations_ = dests.get();
    hub->req_ = req.get();

    // Only the static destination is forwarded.
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());
    EXPECT_EQ(1, (int)hub->forwarders_.size());
    EXPECT_EQ(0, (int)hub->dest_forwarders_.size());
    hub->destroy_forwarders();
}

// When the forwarder starts, or reconnects, it drops the video frames until a keyframe, so
// the destination is able to decode from the first frame.
VOID TEST(ForwardDestinationsTest, ForwarderWaitsForKeyframe)
{
    srs_error_t err;

    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));
    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));
    EXPECT_STREQ("idle", forwarder->state_.c_str());

    // The stream has video, so the forwarder waits for a keyframe.
    forwarder->sh_video_ = mock_forward_video(true, true);

    HELPER_EXPECT_SUCCESS(forwarder->on_publish());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_STREQ("forwarding", forwarder->state_.c_str());
    EXPECT_EQ(1, forwarder->connects_);
    EXPECT_EQ(1, mock_sdk->send_message_count_);

    // An inter frame, a keyframe and an inter frame.
    if (true) {
        SrsUniquePtr<SrsMediaPacket> p0(mock_forward_video(false, false));
        SrsUniquePtr<SrsMediaPacket> p1(mock_forward_video(true, false));
        SrsUniquePtr<SrsMediaPacket> p2(mock_forward_video(false, false));
        HELPER_EXPECT_SUCCESS(forwarder->on_video(p0.get()));
        HELPER_EXPECT_SUCCESS(forwarder->on_video(p1.get()));
        HELPER_EXPECT_SUCCESS(forwarder->on_video(p2.get()));
    }

    // Wakeup the forwarder coroutine.
    mock_sdk->recv_msgs_.push_back(new SrsRtmpCommonMessage());
    mock_sdk->cond_->signal();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // The first inter frame is dropped.
    EXPECT_EQ(2, mock_sdk->send_and_free_messages_count_);
    EXPECT_EQ(1, forwarder->dropped_frames_);
    EXPECT_EQ(20, forwarder->send_bytes_);

    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        forwarder->dumps(obj.get());
        std::string json = obj->dumps();
        EXPECT_TRUE(json.find("\"state\":\"forwarding\"") != std::string::npos);
        EXPECT_TRUE(json.find("\"connects\":1") != std::string::npos);
        EXPECT_TRUE(json.find("\"failures\":0") != std::string::npos);
        EXPECT_TRUE(json.find("\"send_bytes\":20") != std::string::npos);
        EXPECT_TRUE(json.find("\"dropped_frames\":1") != std::string::npos);
    }

    mock_sdk->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_sdk->cond_->signal();
    forwarder->on_unpublish();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

// The packets queued while the forwarder is disconnected are stale, so they are dropped when
// connected, instead of sending them in a burst.
VOID TEST(ForwardDestinationsTest, ForwarderDropsStaleQueue)
{
    srs_error_t err;

    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));
    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));

    // Queued before connected, for example, while retrying.
    if (true) {
        SrsUniquePtr<SrsMediaPacket> p0(mock_forward_video(true, false));
        SrsUniquePtr<SrsMediaPacket> p1(mock_forward_video(false, false));
        HELPER_EXPECT_SUCCESS(forwarder->on_video(p0.get()));
        HELPER_EXPECT_SUCCESS(forwarder->on_video(p1.get()));
    }

    HELPER_EXPECT_SUCCESS(forwarder->on_publish());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_STREQ("forwarding", forwarder->state_.c_str());

    mock_sdk->recv_msgs_.push_back(new SrsRtmpCommonMessage());
    mock_sdk->cond_->signal();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(0, mock_sdk->send_and_free_messages_count_);

    mock_sdk->recv_err_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_sdk->cond_->signal();
    forwarder->on_unpublish();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

// The forwarder reports the failure and keeps retrying.
VOID TEST(ForwardDestinationsTest, ForwarderReportsFailure)
{
    srs_error_t err;

    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    mock_sdk->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect refused");
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));
    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));

    HELPER_EXPECT_SUCCESS(forwarder->on_publish());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_STREQ("retrying", forwarder->state_.c_str());
    EXPECT_EQ(1, forwarder->failures_);
    EXPECT_EQ(0, forwarder->connects_);
    EXPECT_TRUE(forwarder->last_error_.find("mock connect refused") != std::string::npos);
    EXPECT_TRUE(forwarder->last_error_at_ > 0);

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    forwarder->dumps(obj.get());
    std::string json = obj->dumps();
    EXPECT_TRUE(json.find("\"state\":\"retrying\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"failures\":1") != std::string::npos);
    EXPECT_TRUE(json.find("mock connect refused") != std::string::npos);

    // Stop while waiting to retry, the mock client must not be reused.
    forwarder->on_unpublish();
}

// The error of publishing to the destination carries the stream key, which must be hidden.
VOID TEST(ForwardDestinationsTest, ForwarderRedactsStreamKeyInError)
{
    srs_error_t err;

    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live2", "secretkey"));
    req->param_ = "?token=secrettoken";
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    mock_sdk->publish_error_ = srs_error_new(ERROR_RTMP_ACCESS_DENIED, "publish failed, stream=secretkey?token=secrettoken");
    SrsUniquePtr<MockAppFactoryForForwarder> mock_factory(new MockAppFactoryForForwarder());
    mock_factory->mock_rtmp_client_ = mock_sdk;
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(mock_hub.get()));
    forwarder->app_factory_ = mock_factory.get();
    forwarder->config_ = mock_config.get();
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));

    HELPER_EXPECT_SUCCESS(forwarder->on_publish());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_STREQ("retrying", forwarder->state_.c_str());
    EXPECT_TRUE(forwarder->last_error_.find("publish failed") != std::string::npos);
    EXPECT_TRUE(forwarder->last_error_.find("secretkey") == std::string::npos) << forwarder->last_error_;
    EXPECT_TRUE(forwarder->last_error_.find("secrettoken") == std::string::npos) << forwarder->last_error_;

    forwarder->on_unpublish();
}

VOID TEST(ForwardDestinationsTest, ConfigForwardApi)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost v1{forward{enabled on;api on;}} vhost v2{forward{enabled on;destination 127.0.0.1:1936;}}"));
    EXPECT_TRUE(conf.get_forward_api("v1"));
    EXPECT_FALSE(conf.get_forward_api("v2"));
    EXPECT_FALSE(conf.get_forward_api("v3"));

    if (true) {
        SrsSetEnvConfig(conf, forward_api, "SRS_VHOST_FORWARD_API", "on");
        EXPECT_TRUE(conf.get_forward_api("v2"));
    }
}

// The HTTP API to add, list, get and remove forward destinations.
VOID TEST(ForwardDestinationsTest, HttpApi)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    mock_config->forward_api_ = true;
    mock_config->default_vhost_ = new SrsConfDirective();
    mock_config->default_vhost_->name_ = "vhost";
    mock_config->default_vhost_->args_.push_back("__defaultVhost__");
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<SrsForwardDestinations> dests(new SrsForwardDestinations());

    SrsUniquePtr<SrsGoApiForwards> api(new SrsGoApiForwards());
    api->stat_ = mock_stat.get();
    api->config_ = mock_config.get();
    api->forward_destinations_ = dests.get();
    api->entry_ = new SrsHttpMuxEntry();
    api->entry_->pattern = "/api/v1/forwards/";

    std::string body = "{\"id\":\"out1\",\"app\":\"live\",\"stream\":\"stream1\",\"url\":\"rtmp://127.0.0.1:19350/live2/secretkey?token=abc\"}";

    // Add a destination.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_POST, "http://127.0.0.1/api/v1/forwards/", body);
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        std::string res = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(res.find("\"code\":0") != std::string::npos);
        EXPECT_TRUE(res.find("\"id\":\"out1\"") != std::string::npos);
        EXPECT_TRUE(res.find("\"vhost\":\"__defaultVhost__\"") != std::string::npos);
        EXPECT_TRUE(res.find("\"url\":\"rtmp://127.0.0.1:19350/live2/***\"") != std::string::npos);
        EXPECT_TRUE(res.find("\"state\":\"idle\"") != std::string::npos);
        EXPECT_TRUE(res.find("secretkey") == std::string::npos);
        EXPECT_TRUE(dests->find("out1") != NULL);
    }

    // Add it again, it's idempotent.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_POST, "http://127.0.0.1/api/v1/forwards/", body);
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":0") != std::string::npos);
    }

    // The same id with another url.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_POST, "http://127.0.0.1/api/v1/forwards/",
                                     "{\"id\":\"out1\",\"app\":\"live\",\"stream\":\"stream1\",\"url\":\"rtmp://127.0.0.1/live/other\"}");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":3105") != std::string::npos);
    }

    // List all destinations.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_GET, "http://127.0.0.1/api/v1/forwards/", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        std::string res = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(res.find("\"code\":0") != std::string::npos);
        EXPECT_TRUE(res.find("\"server\":") != std::string::npos);
        EXPECT_TRUE(res.find("\"total\":1") != std::string::npos);
        EXPECT_TRUE(res.find("\"forwards\":[") != std::string::npos);
        EXPECT_TRUE(res.find("\"id\":\"out1\"") != std::string::npos);
        EXPECT_TRUE(res.find("secretkey") == std::string::npos);
    }

    // List the destinations of another stream.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_GET, "http://127.0.0.1/api/v1/forwards/?app=live&stream=other", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"total\":0") != std::string::npos);
    }

    // Get the destination.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_GET, "http://127.0.0.1/api/v1/forwards/out1", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        std::string res = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(res.find("\"forward\":{") != std::string::npos);
        EXPECT_TRUE(res.find("\"id\":\"out1\"") != std::string::npos);
    }

    // Remove the destination.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_DELETE, "http://127.0.0.1/api/v1/forwards/out1", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":0") != std::string::npos);
        EXPECT_TRUE(dests->find("out1") == NULL);
    }

    // Not found.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_DELETE, "http://127.0.0.1/api/v1/forwards/out1", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":3106") != std::string::npos);
    }
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_GET, "http://127.0.0.1/api/v1/forwards/out1", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":3106") != std::string::npos);
    }

    // Invalid body or destination.
    const char *invalid_bodies[] = {
        "not json",
        "[]",
        "{\"app\":\"live\",\"stream\":\"stream1\"}",
        "{\"app\":\"live\",\"stream\":\"stream1\",\"url\":\"rtmps://127.0.0.1/live/key\"}",
        "{\"app\":\"live\",\"url\":\"rtmp://127.0.0.1/live/key\"}",
        "{\"app\":\"live\",\"stream\":1,\"url\":\"rtmp://127.0.0.1/live/key\"}",
    };
    for (int i = 0; i < (int)(sizeof(invalid_bodies) / sizeof(invalid_bodies[0])); i++) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_POST, "http://127.0.0.1/api/v1/forwards/", invalid_bodies[i]);
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":3104") != std::string::npos) << invalid_bodies[i];
    }

    // Not allowed method.
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_PUT, "http://127.0.0.1/api/v1/forwards/out1", "");
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("405") != std::string::npos);
    }

    // The forward API is disabled for the vhost.
    mock_config->forward_api_ = false;
    if (true) {
        MockResponseWriter w;
        MockHttpMessageForForwards r(SRS_CONSTS_HTTP_POST, "http://127.0.0.1/api/v1/forwards/", body);
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, &r));
        EXPECT_TRUE(HELPER_BUFFER2STR(&w.io.out_buffer).find("\"code\":3103") != std::string::npos);
        EXPECT_TRUE(dests->find("out1") == NULL);
    }

    srs_freep(api->entry_);
}
