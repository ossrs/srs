/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2025 Winlin
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

#include <srs_utest_workflow_http_conn.hpp>

#include <srs_app_http_conn.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai19.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

// This test is used to verify the basic workflow of the HTTP connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowHttpConnTest, ManuallyVerifyForHttpRequest)
{
    srs_error_t err;

    // Create mock objects for dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockAppFactory> mock_app_factory(new MockAppFactory());
    SrsUniquePtr<MockHttpxConn> mock_handler(new MockHttpxConn());
    MockProtocolReadWriter *mock_io = new MockProtocolReadWriter();
    MockHttpParser *mock_parser = new MockHttpParser();
    SrsUniquePtr<MockHttpServeMux> mock_http_mux(new MockHttpServeMux());

    // Create SrsHttpConn - it takes ownership of mock_io
    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(mock_handler.get(), mock_io, mock_http_mux.get(), "192.168.1.100", 8080));

    // Inject mock dependencies into private fields
    conn->config_ = mock_config.get();
    conn->app_factory_ = mock_app_factory.get();
    srs_freep(conn->parser_);
    conn->parser_ = mock_parser;

    // Start the HTTP connection
    HELPER_EXPECT_SUCCESS(conn->start());

    // Wait for coroutine to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify that http_mux->handle() was called
    EXPECT_TRUE(mock_parser->initialize_called_);
    EXPECT_TRUE(mock_handler->on_start_called_);
    EXPECT_TRUE(mock_parser->parse_message_called_);

    // Create a mock HTTP message for the parser to return
    // Note: The parser will take ownership of this message
    MockHttpMessage *mock_message = new MockHttpMessage();
    mock_message->set_url("/api/v1/versions", false);
    mock_parser->messages_.push_back(mock_message);
    mock_parser->cond_->signal();

    // Wait for message processing
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify that http_mux->handle() and serve_http() were called
    EXPECT_EQ(1, mock_http_mux->serve_http_count_);
}

// This test is used to verify the basic workflow of the HTTPx connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowHttpConnTest, ManuallyVerifyForHttpxRequest)
{
    srs_error_t err;

    // Create mock objects for dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockAppFactory> mock_app_factory(new MockAppFactory());
    MockProtocolReadWriter *mock_io = new MockProtocolReadWriter();
    MockHttpParser *mock_parser = new MockHttpParser();
    SrsUniquePtr<MockHttpServeMux> mock_http_mux(new MockHttpServeMux());
    MockSslConnection *mock_ssl = new MockSslConnection();
    SrsUniquePtr<MockConnectionManager> mock_manager(new MockConnectionManager());

    // Create SrsHttpxConn - it takes ownership of mock_io
    // Set key and cert to empty to disable SSL
    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(mock_manager.get(), mock_io, mock_http_mux.get(), "192.168.1.100", 8080, "", ""));

    // Inject mock dependencies into private fields
    connx->config_ = mock_config.get();
    // Inject mock SSL connection
    srs_freep(connx->ssl_);
    connx->ssl_ = mock_ssl;

    // Access the internal SrsHttpConn through conn_ field (cast from ISrsHttpConn* to SrsHttpConn*)
    SrsHttpConn *conn = new SrsHttpConn(connx.get(), mock_ssl, mock_http_mux.get(), "192.168.1.100", 8080);
    conn->config_ = mock_config.get();
    conn->app_factory_ = mock_app_factory.get();
    srs_freep(conn->parser_);
    conn->parser_ = mock_parser;

    srs_freep(connx->conn_);
    connx->conn_ = conn;

    // Start the HTTPx connection
    HELPER_EXPECT_SUCCESS(connx->start());

    // Wait for coroutine to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify that parser->initialize() was called
    EXPECT_TRUE(mock_parser->initialize_called_);
    EXPECT_TRUE(mock_parser->parse_message_called_);

    // Verify HTTPS handshake was called
    EXPECT_TRUE(mock_ssl->handshake_called_);

    // Create a mock HTTP message for the parser to return
    // Note: The parser will take ownership of this message
    MockHttpMessage *mock_message = new MockHttpMessage();
    mock_message->set_url("/api/v1/versions", false);
    mock_parser->messages_.push_back(mock_message);
    mock_parser->cond_->signal();

    // Wait for message processing
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify that http_mux->serve_http() was called
    EXPECT_EQ(1, mock_http_mux->serve_http_count_);
}

// This test is used to verify the basic workflow of the HTTP FLV streaming.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowHttpConnTest, ManuallyVerifyForHttpStream)
{
    srs_error_t err;

    // Create SrsLiveStream object under test
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "livestream"));
    SrsUniquePtr<MockBufferCache> mock_cache(new MockBufferCache());
    SrsUniquePtr<SrsLiveStream> live_stream(new SrsLiveStream(mock_request.get(), mock_cache.get()));

    // Create mock dependencies
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockLiveSourceManager> mock_live_sources(new MockLiveSourceManager());
    SrsUniquePtr<MockAppStatistic> mock_stat(new MockAppStatistic());
    SrsUniquePtr<MockHttpHooks> mock_hooks(new MockHttpHooks());
    SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
    SrsUniquePtr<MockHttpMessage> mock_message(new MockHttpMessage());
    SrsUniquePtr<SrsHttpMuxEntry> mock_entry(new SrsHttpMuxEntry());
    MockProtocolReadWriter *mock_io = new MockProtocolReadWriter();
    SrsUniquePtr<MockHttpServeMux> mock_http_mux(new MockHttpServeMux());
    SrsUniquePtr<MockConnectionManager> mock_manager(new MockConnectionManager());
    SrsUniquePtr<MockAppFactory> mock_app_factory(new MockAppFactory());
    MockHttpParser *mock_parser = new MockHttpParser();
    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(mock_manager.get(), mock_io, mock_http_mux.get(), "192.168.1.100", 8080, "", ""));
    SrsHttpConn *conn = new SrsHttpConn(connx.get(), mock_io, mock_http_mux.get(), "192.168.1.100", 8080);

    // Inject mock dependencies into SrsLiveStream private fields
    live_stream->config_ = mock_config.get();
    live_stream->live_sources_ = mock_live_sources.get();
    live_stream->stat_ = mock_stat.get();
    live_stream->hooks_ = mock_hooks.get();

    // Do not wait for utest, consume messages immediately. Remove this when HTTP stream use cond signal.
    mock_config->mw_sleep_ = 100; // 0.1ms

    mock_entry->enabled = true;
    mock_entry->pattern = "/live/livestream.flv";
    live_stream->entry_ = mock_entry.get();

    connx->config_ = mock_config.get();
    conn->config_ = mock_config.get();
    conn->app_factory_ = mock_app_factory.get();
    srs_freep(conn->parser_);
    conn->parser_ = mock_parser;
    srs_freep(connx->conn_);
    connx->conn_ = conn;
    mock_message->set_connection(conn);

    // Start a coroutine to run the streaming, because it's a blocking operation
    SrsCoroutineChan ctx;
    ctx.push(live_stream.get());
    ctx.push(mock_writer.get());
    ctx.push(mock_message.get());
    srs_error_t r0 = srs_success;
    ctx.push(&r0);
    SrsUniquePtr<SrsCond> cond(new SrsCond());
    ctx.push(cond.get());
    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsLiveStream *live_stream = (SrsLiveStream *)ctx.pop();
        ISrsHttpResponseWriter *mock_writer = (ISrsHttpResponseWriter *)ctx.pop();
        ISrsHttpMessage *mock_message = (ISrsHttpMessage *)ctx.pop();
        srs_error_t *r0 = (srs_error_t *)ctx.pop();
        SrsCond *cond = (SrsCond *)ctx.pop();
        *r0 = live_stream->serve_http(mock_writer, mock_message);
        cond->signal();
    });

    // Wait for coroutine to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify that live source has a consumer
    MockLiveSource *mock_source = dynamic_cast<MockLiveSource *>(mock_live_sources->mock_source_.get());
    EXPECT_EQ(1, (int)mock_source->consumers_.size());
    EXPECT_EQ(1, mock_source->on_dump_packets_count_);

    // Feed SSL a message to cover the http recv thread.
    if (true) {
        mock_io->recv_msgs_.push_back("test data");
        mock_io->cond_->signal();

        // Wait for http thread to consume the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify that the recv thread consumed the message.
        EXPECT_EQ(1, mock_io->read_count_);
    }

    // Create an RTMP audio message to feed consumer.
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

        // Feed audio to source.
        SrsLiveSource *source = mock_source;
        HELPER_EXPECT_SUCCESS(source->on_audio(msg));

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the mock response writer received the audio data
        EXPECT_EQ(101, mock_writer->io.out_buffer.length());
    }

    // Create an RTMP video message to feed consumer.
    if (true) {
        // Create a real H.264 video message with proper format.
        // H.264 video format in RTMP/FLV:
        // Byte 0: (FrameType << 4) | CodecID (CodecID=7 for H.264)
        //         FrameType=1 (key frame), CodecID=7 (H.264) = 0x17
        // Byte 1: AVCPacketType (0=sequence header, 1=NALU, 2=end of sequence)
        // Byte 2-4: CompositionTime (3bytes little-endian int24)
        // Remaining bytes: H.264 data
        int payload_size = 10;
        SrsRtmpCommonMessage *msg = new SrsRtmpCommonMessage();
        msg->header_.initialize_video(payload_size, 0, 1);
        msg->create_payload(payload_size);

        // Fill in H.264 video data
        SrsBuffer stream(msg->payload(), payload_size);
        // Frame type & Codec ID: Key frame (1) + H.264 (7) = 0x17
        stream.write_1bytes(0x17);
        // AVC packet type: 1 = NALU
        stream.write_1bytes(0x01);
        // Composition time: 0 (3bytes little-endian int24)
        stream.write_3bytes(0x000000);
        // H.264 raw data (5 bytes of dummy video data)
        for (int i = 0; i < 5; i++) {
            stream.write_1bytes(0x00);
        }

        // Feed video to source.
        SrsLiveSource *source = mock_source;
        HELPER_EXPECT_SUCCESS(source->on_video(msg));

        // Wait for consumer to process the message.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the mock response writer received the video data
        EXPECT_EQ(132, mock_writer->io.out_buffer.length());
    }

    // Simulate client quit event, the receive thread will get this error.
    // Note that we should not sleep here, because we need to use cond to get the signal.
    // If we sleep, the signal will be lost.
    if (true) {
        mock_io->read_error_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
        mock_io->cond_->signal();
    }

    // Wait fr or coroutine to stop
    cond->wait();
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(r0));
    srs_freep(r0);
}
