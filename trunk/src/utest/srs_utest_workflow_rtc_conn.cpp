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

#include <srs_utest_workflow_rtc_conn.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtp.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

#include <sstream>

// This test is used to verify the basic workflow of the RTC connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcManuallyVerifyForPlayer)
{
    srs_error_t err;

    // Create mock dependencies FIRST (they must outlive the connection)
    SrsUniquePtr<MockCircuitBreaker> mock_circuit_breaker(new MockCircuitBreaker());
    SrsUniquePtr<MockConnectionManager> mock_conn_manager(new MockConnectionManager());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockDtlsCertificate> mock_dtls_certificate(new MockDtlsCertificate());
    SrsUniquePtr<MockSdpFactory> mock_sdp_factory(new MockSdpFactory());
    SrsUniquePtr<MockAppFactoryForRtcConn> mock_app_factory(new MockAppFactoryForRtcConn());

    mock_config->rtc_dtls_role_ = "passive";
    mock_dtls_certificate->fingerprint_ = "test-fingerprint";
    mock_app_factory->rtc_sources_ = mock_rtc_sources.get();
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
    MockRtcSource *mock_rtc_source = new MockRtcSource();
    mock_rtc_sources->mock_source_ = SrsSharedPtr<SrsRtcSource>(mock_rtc_source);

    // Create a real ISrsRtcConnection using _srs_app_factory_
    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    cid.set_value("test-rtc-conn-player-workflow");

    SrsUniquePtr<ISrsRtcConnection> conn_ptr(_srs_app_factory->create_rtc_connection(&mock_exec, cid));
    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection *>(conn_ptr.get());
    EXPECT_TRUE(conn != NULL);

    // Mock the RTC conn, also mock the config in publisher_negotiator_ and player_negotiator_
    conn->circuit_breaker_ = mock_circuit_breaker.get();
    conn->conn_manager_ = mock_conn_manager.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->config_ = mock_config.get();
    conn->dtls_certificate_ = mock_dtls_certificate.get();
    conn->app_factory_ = mock_app_factory.get();

    SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn->publisher_negotiator_);
    pub_neg->config_ = mock_config.get();
    SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn->player_negotiator_);
    play_neg->config_ = mock_config.get();
    play_neg->rtc_sources_ = mock_rtc_sources.get();

    // Create RTC user config for add_player
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    if (true) {
        srs_freep(ruc->req_);
        ruc->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc->publish_ = false;
        ruc->dtls_ = true;
        ruc->srtp_ = true;
        ruc->audio_before_video_ = false;

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_player_offer_with_h264();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
        EXPECT_TRUE(ruc->remote_sdp_.media_descs_.size() == 2);
    }

    // Add player, which negotiate the SDP and generate local SDP
    SrsSdp local_sdp;
    local_sdp.session_config_.dtls_role_ = mock_config->get_rtc_dtls_role(ruc->req_->vhost_);

    if (true) {
        HELPER_EXPECT_SUCCESS(conn->add_player(ruc.get(), local_sdp));

        // Verify publishers and SSRC mappings
        EXPECT_TRUE(conn->players_.size() == 1);
        EXPECT_TRUE(conn->players_ssrc_map_.size() == 2);

        // Verify the local SDP was generated with media information
        EXPECT_TRUE(local_sdp.version_ == "0");
        EXPECT_TRUE(local_sdp.group_policy_ == "BUNDLE");
        EXPECT_TRUE(local_sdp.msids_.size() == 1);
        EXPECT_TRUE(local_sdp.msids_[0] == "live/stream1");
        EXPECT_TRUE(local_sdp.media_descs_.size() == 2);

        // First should be audio media desc
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(audio_desc->type_ == "audio");
        EXPECT_FALSE(audio_desc->recvonly_);
        EXPECT_TRUE(audio_desc->payload_types_.size() == 1);
        EXPECT_TRUE(audio_desc->payload_types_[0].payload_type_ == mock_sdp_factory->audio_pt_);
        EXPECT_TRUE(audio_desc->payload_types_[0].encoding_name_ == "opus");
        EXPECT_TRUE(audio_desc->payload_types_[0].clock_rate_ == 48000);

        // Second should be video media desc
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(video_desc->type_ == "video");
        EXPECT_FALSE(video_desc->recvonly_);
        EXPECT_TRUE(video_desc->payload_types_.size() == 1);
        EXPECT_TRUE(video_desc->payload_types_[0].payload_type_ == mock_sdp_factory->video_pt_);
        EXPECT_TRUE(video_desc->payload_types_[0].encoding_name_ == "H264");
        EXPECT_TRUE(video_desc->payload_types_[0].clock_rate_ == 90000);
    }

    // Generate local SDP and setup SDP.
    std::string username;
    if (true) {
        bool status = true;
        conn->set_all_tracks_status(ruc->req_->get_stream_url(), ruc->publish_, status);

        HELPER_EXPECT_SUCCESS(conn->generate_local_sdp(ruc.get(), local_sdp, username));
        conn->set_remote_sdp(ruc->remote_sdp_);
        conn->set_local_sdp(local_sdp);
        conn->set_state_as_waiting_stun();

        // Verify the local SDP was generated ice pwd
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(!audio_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!audio_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(audio_desc->candidates_.size() == 1);
        EXPECT_TRUE(audio_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(audio_desc->session_info_.setup_ == "passive");

        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(!video_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!video_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(video_desc->candidates_.size() == 1);
        EXPECT_TRUE(video_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(video_desc->session_info_.setup_ == "passive");

        EXPECT_TRUE(local_sdp.session_negotiate_.dtls_role_ == "passive");
    }

    // Initialize the connection
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->initialize(ruc->req_, ruc->dtls_, ruc->srtp_, username));
        EXPECT_TRUE(conn->nack_enabled_);
    }

    // DTLS done, start player consumer
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->on_dtls_handshake_done());

        // Wait for coroutine to start. Normally it should be ready and stopped at wait for
        // RTP packets from consumer.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the consumer is created and started
        EXPECT_TRUE(conn->players_.size() == 1);
        SrsRtcPlayStream *player = dynamic_cast<SrsRtcPlayStream *>(conn->players_.begin()->second);
        EXPECT_TRUE(player->is_started_);

        // Stop the player
        player->stop();
    }
}

// This test is used to verify the basic workflow of the RTC connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcManuallyVerifyForPublisher)
{
    srs_error_t err;

    // Create mock dependencies FIRST (they must outlive the connection)
    SrsUniquePtr<MockCircuitBreaker> mock_circuit_breaker(new MockCircuitBreaker());
    SrsUniquePtr<MockConnectionManager> mock_conn_manager(new MockConnectionManager());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockDtlsCertificate> mock_dtls_certificate(new MockDtlsCertificate());
    SrsUniquePtr<MockSdpFactory> mock_sdp_factory(new MockSdpFactory());
    SrsUniquePtr<MockAppFactoryForRtcConn> mock_app_factory(new MockAppFactoryForRtcConn());
    SrsStreamPublishTokenManager token_manager;

    mock_config->rtc_dtls_role_ = "passive";
    mock_dtls_certificate->fingerprint_ = "test-fingerprint";
    mock_app_factory->rtc_sources_ = mock_rtc_sources.get();
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
    MockRtcSource *mock_rtc_source = new MockRtcSource();
    mock_rtc_sources->mock_source_ = SrsSharedPtr<SrsRtcSource>(mock_rtc_source);

    // Create a real ISrsRtcConnection using _srs_app_factory_
    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    cid.set_value("test-rtc-conn-publisher-workflow");

    SrsUniquePtr<ISrsRtcConnection> conn_ptr(_srs_app_factory->create_rtc_connection(&mock_exec, cid));
    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection *>(conn_ptr.get());
    EXPECT_TRUE(conn != NULL);

    // Mock the RTC conn, also mock the config in publisher_negotiator_ and player_negotiator_
    conn->circuit_breaker_ = mock_circuit_breaker.get();
    conn->conn_manager_ = mock_conn_manager.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->config_ = mock_config.get();
    conn->dtls_certificate_ = mock_dtls_certificate.get();
    conn->app_factory_ = mock_app_factory.get();

    SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn->publisher_negotiator_);
    pub_neg->config_ = mock_config.get();
    SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn->player_negotiator_);
    play_neg->config_ = mock_config.get();
    play_neg->rtc_sources_ = mock_rtc_sources.get();

    // Create RTC user config for add_publisher
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    if (true) {
        srs_freep(ruc->req_);
        ruc->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc->publish_ = true;
        ruc->dtls_ = true;
        ruc->srtp_ = true;
        ruc->audio_before_video_ = false;

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_publisher_offer_with_h264();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
        EXPECT_TRUE(ruc->remote_sdp_.media_descs_.size() == 2);
    }

    // Add publisher, which negotiate the SDP and generate local SDP
    SrsSdp local_sdp;
    local_sdp.session_config_.dtls_role_ = mock_config->get_rtc_dtls_role(ruc->req_->vhost_);

    if (true) {
        HELPER_EXPECT_SUCCESS(conn->add_publisher(ruc.get(), local_sdp));

        // Verify publishers and SSRC mappings
        EXPECT_TRUE(conn->publishers_.size() == 1);
        EXPECT_TRUE(conn->publishers_ssrc_map_.size() == 2);
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->audio_ssrc_) != conn->publishers_ssrc_map_.end());
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->video_ssrc_) != conn->publishers_ssrc_map_.end());

        // Verify the source stream desription, should have two tracks.
        SrsRtcSourceDescription *stream_desc = mock_rtc_sources->mock_source_->stream_desc_;
        EXPECT_TRUE(stream_desc->audio_track_desc_ != NULL);
        EXPECT_TRUE(stream_desc->video_track_descs_.size() == 1);

        // Verify the audio track ssrc and payload type.
        EXPECT_TRUE(stream_desc->audio_track_desc_->ssrc_ == mock_sdp_factory->audio_ssrc_);
        EXPECT_TRUE(stream_desc->audio_track_desc_->media_->pt_ == mock_sdp_factory->audio_pt_);

        // Verify the video track ssrc and payload type.
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->ssrc_ == mock_sdp_factory->video_ssrc_);
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->pt_ == mock_sdp_factory->video_pt_);

        // Verify the local SDP was generated with media information
        EXPECT_TRUE(local_sdp.version_ == "0");
        EXPECT_TRUE(local_sdp.group_policy_ == "BUNDLE");
        EXPECT_TRUE(local_sdp.msids_.size() == 1);
        EXPECT_TRUE(local_sdp.msids_[0] == "live/stream1");
        EXPECT_TRUE(local_sdp.media_descs_.size() == 2);

        // First should be audio media desc
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(audio_desc->type_ == "audio");
        EXPECT_TRUE(audio_desc->recvonly_);
        EXPECT_TRUE(audio_desc->payload_types_.size() == 1);
        EXPECT_TRUE(audio_desc->payload_types_[0].payload_type_ == mock_sdp_factory->audio_pt_);
        EXPECT_TRUE(audio_desc->payload_types_[0].encoding_name_ == "opus");
        EXPECT_TRUE(audio_desc->payload_types_[0].clock_rate_ == 48000);

        // Second should be video media desc
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(video_desc->type_ == "video");
        EXPECT_TRUE(video_desc->recvonly_);
        EXPECT_TRUE(video_desc->payload_types_.size() == 1);
        EXPECT_TRUE(video_desc->payload_types_[0].payload_type_ == mock_sdp_factory->video_pt_);
        EXPECT_TRUE(video_desc->payload_types_[0].encoding_name_ == "H264");
        EXPECT_TRUE(video_desc->payload_types_[0].clock_rate_ == 90000);
    }

    // Generate local SDP and setup SDP.
    std::string username;
    if (true) {
        bool status = true;
        conn->set_all_tracks_status(ruc->req_->get_stream_url(), ruc->publish_, status);

        HELPER_EXPECT_SUCCESS(conn->generate_local_sdp(ruc.get(), local_sdp, username));
        conn->set_remote_sdp(ruc->remote_sdp_);
        conn->set_local_sdp(local_sdp);
        conn->set_state_as_waiting_stun();

        // Verify the local SDP was generated ice pwd
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(!audio_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!audio_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(audio_desc->candidates_.size() == 1);
        EXPECT_TRUE(audio_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(audio_desc->session_info_.setup_ == "passive");

        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(!video_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!video_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(video_desc->candidates_.size() == 1);
        EXPECT_TRUE(video_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(video_desc->session_info_.setup_ == "passive");

        EXPECT_TRUE(local_sdp.session_negotiate_.dtls_role_ == "passive");
    }

    // Initialize the connection
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->initialize(ruc->req_, ruc->dtls_, ruc->srtp_, username));
        EXPECT_TRUE(conn->nack_enabled_);

        // Create and set publish token
        SrsStreamPublishToken *publish_token_raw = NULL;
        HELPER_EXPECT_SUCCESS(token_manager.acquire_token(ruc->req_, publish_token_raw));
        SrsSharedPtr<ISrsStreamPublishToken> publish_token(publish_token_raw);

        conn->set_publish_token(publish_token);
        EXPECT_TRUE(conn->publish_token_->is_acquired());
    }

    // DTLS done, start publisher
    SrsRtcPublishStream *publisher = NULL;
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->on_dtls_handshake_done());

        // Wait for coroutine to start. Normally it should be ready wait for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the publisher is created and started
        EXPECT_TRUE(conn->publishers_.size() == 1);
        publisher = dynamic_cast<SrsRtcPublishStream *>(conn->publishers_.begin()->second);
        EXPECT_TRUE(publisher->is_sender_started_);
    }

    // Got a RTP audio packet.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->audio_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(mock_sdp_factory->audio_pt_);

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_audio_count_, i + 1);
    }

    // Got a RTP video packet.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->video_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(mock_sdp_factory->video_pt_);

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_video_count_, i + 1);
    }

    // Stop the publisher
    publisher->stop();
}

// This test is used to verify the libdatachannel SDP offer from issue 4570.
// The issue reports that SRS returns an incomplete SDP answer when receiving
// an offer from libdatachannel library.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcManuallyVerifyForLibdatachannel)
{
    srs_error_t err;

    // Create mock dependencies FIRST (they must outlive the connection)
    SrsUniquePtr<MockCircuitBreaker> mock_circuit_breaker(new MockCircuitBreaker());
    SrsUniquePtr<MockConnectionManager> mock_conn_manager(new MockConnectionManager());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockDtlsCertificate> mock_dtls_certificate(new MockDtlsCertificate());
    SrsUniquePtr<MockSdpFactory> mock_sdp_factory(new MockSdpFactory());
    SrsUniquePtr<MockAppFactoryForRtcConn> mock_app_factory(new MockAppFactoryForRtcConn());
    SrsStreamPublishTokenManager token_manager;

    mock_config->rtc_dtls_role_ = "passive";
    mock_dtls_certificate->fingerprint_ = "test-fingerprint";
    mock_app_factory->rtc_sources_ = mock_rtc_sources.get();
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
    MockRtcSource *mock_rtc_source = new MockRtcSource();
    mock_rtc_sources->mock_source_ = SrsSharedPtr<SrsRtcSource>(mock_rtc_source);

    // Create a real ISrsRtcConnection using _srs_app_factory_
    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    cid.set_value("test-rtc-conn-libdatachannel-workflow");

    SrsUniquePtr<ISrsRtcConnection> conn_ptr(_srs_app_factory->create_rtc_connection(&mock_exec, cid));
    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection *>(conn_ptr.get());
    EXPECT_TRUE(conn != NULL);

    // Mock the RTC conn, also mock the config in publisher_negotiator_ and player_negotiator_
    conn->circuit_breaker_ = mock_circuit_breaker.get();
    conn->conn_manager_ = mock_conn_manager.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->config_ = mock_config.get();
    conn->dtls_certificate_ = mock_dtls_certificate.get();
    conn->app_factory_ = mock_app_factory.get();

    SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn->publisher_negotiator_);
    pub_neg->config_ = mock_config.get();
    SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn->player_negotiator_);
    play_neg->config_ = mock_config.get();
    play_neg->rtc_sources_ = mock_rtc_sources.get();

    // Create RTC user config for add_publisher with libdatachannel SDP offer from issue #4570
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    if (true) {
        srs_freep(ruc->req_);
        ruc->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc->publish_ = true;
        ruc->dtls_ = true;
        ruc->srtp_ = true;
        ruc->audio_before_video_ = false;

        ruc->remote_sdp_str_ = mock_sdp_factory->create_libdatachannel_publisher_offer_with_h264();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
        EXPECT_TRUE(ruc->remote_sdp_.media_descs_.size() == 2);
    }

    // Add publisher, which negotiate the SDP and generate local SDP
    SrsSdp local_sdp;
    local_sdp.session_config_.dtls_role_ = mock_config->get_rtc_dtls_role(ruc->req_->vhost_);

    if (true) {
        HELPER_EXPECT_SUCCESS(conn->add_publisher(ruc.get(), local_sdp));

        // Verify publishers and SSRC mappings
        EXPECT_TRUE(conn->publishers_.size() == 1);
        EXPECT_TRUE(conn->publishers_ssrc_map_.size() == 2);
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->video_ssrc_) != conn->publishers_ssrc_map_.end());
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->audio_ssrc_) != conn->publishers_ssrc_map_.end());

        // Verify the source stream desription, should have two tracks.
        SrsRtcSourceDescription *stream_desc = mock_rtc_sources->mock_source_->stream_desc_;
        EXPECT_TRUE(stream_desc->audio_track_desc_ != NULL);
        EXPECT_TRUE(stream_desc->video_track_descs_.size() == 1);

        // Verify the audio track ssrc and payload type.
        EXPECT_TRUE(stream_desc->audio_track_desc_->ssrc_ == mock_sdp_factory->audio_ssrc_);
        EXPECT_TRUE(stream_desc->audio_track_desc_->media_->pt_ == mock_sdp_factory->audio_pt_);

        // Verify the video track ssrc and payload type.
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->ssrc_ == mock_sdp_factory->video_ssrc_);
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->pt_ == mock_sdp_factory->video_pt_);

        // Verify the local SDP was generated with media information
        EXPECT_TRUE(local_sdp.version_ == "0");
        EXPECT_TRUE(local_sdp.group_policy_ == "BUNDLE");
        EXPECT_TRUE(local_sdp.msids_.size() == 1);
        EXPECT_TRUE(local_sdp.msids_[0] == "live/stream1");
        EXPECT_TRUE(local_sdp.media_descs_.size() == 2);

        // First should be video media desc (libdatachannel puts video first)
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(video_desc->type_ == "video");
        EXPECT_TRUE(video_desc->recvonly_);
        EXPECT_TRUE(video_desc->payload_types_.size() >= 1);
        EXPECT_TRUE(video_desc->payload_types_[0].payload_type_ == mock_sdp_factory->video_pt_);
        EXPECT_TRUE(video_desc->payload_types_[0].encoding_name_ == "H264");
        EXPECT_TRUE(video_desc->payload_types_[0].clock_rate_ == 90000);

        // Second should be audio media desc
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(audio_desc->type_ == "audio");
        EXPECT_TRUE(audio_desc->recvonly_);
        EXPECT_TRUE(audio_desc->payload_types_.size() == 1);
        EXPECT_TRUE(audio_desc->payload_types_[0].payload_type_ == mock_sdp_factory->audio_pt_);
        EXPECT_TRUE(audio_desc->payload_types_[0].encoding_name_ == "opus");
        EXPECT_TRUE(audio_desc->payload_types_[0].clock_rate_ == 48000);
    }

    // Generate local SDP and setup SDP.
    std::string username;
    if (true) {
        bool status = true;
        conn->set_all_tracks_status(ruc->req_->get_stream_url(), ruc->publish_, status);

        HELPER_EXPECT_SUCCESS(conn->generate_local_sdp(ruc.get(), local_sdp, username));
        conn->set_remote_sdp(ruc->remote_sdp_);
        conn->set_local_sdp(local_sdp);
        conn->set_state_as_waiting_stun();

        // Verify the local SDP was generated ice pwd
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(!video_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!video_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(video_desc->candidates_.size() == 1);
        EXPECT_TRUE(video_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(video_desc->session_info_.setup_ == "passive");

        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(!audio_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!audio_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(audio_desc->candidates_.size() == 1);
        EXPECT_TRUE(audio_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(audio_desc->session_info_.setup_ == "passive");

        EXPECT_TRUE(local_sdp.session_negotiate_.dtls_role_ == "passive");
    }

    // Initialize the connection
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->initialize(ruc->req_, ruc->dtls_, ruc->srtp_, username));
        EXPECT_TRUE(conn->nack_enabled_);

        // Create and set publish token
        SrsStreamPublishToken *publish_token_raw = NULL;
        HELPER_EXPECT_SUCCESS(token_manager.acquire_token(ruc->req_, publish_token_raw));
        SrsSharedPtr<ISrsStreamPublishToken> publish_token(publish_token_raw);

        conn->set_publish_token(publish_token);
        EXPECT_TRUE(conn->publish_token_->is_acquired());
    }

    // DTLS done, start publisher
    SrsRtcPublishStream *publisher = NULL;
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->on_dtls_handshake_done());

        // Wait for coroutine to start. Normally it should be ready wait for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the publisher is created and started
        EXPECT_TRUE(conn->publishers_.size() == 1);
        publisher = dynamic_cast<SrsRtcPublishStream *>(conn->publishers_.begin()->second);
        EXPECT_TRUE(publisher->is_sender_started_);
    }

    // Stop the publisher
    publisher->stop();
}

// This test is used to verify the basic workflow of the RTC connection with AV1 codec.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcManuallyVerifyForPublisherWithAV1)
{
    srs_error_t err;

    // Create mock dependencies FIRST (they must outlive the connection)
    SrsUniquePtr<MockCircuitBreaker> mock_circuit_breaker(new MockCircuitBreaker());
    SrsUniquePtr<MockConnectionManager> mock_conn_manager(new MockConnectionManager());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockDtlsCertificate> mock_dtls_certificate(new MockDtlsCertificate());
    SrsUniquePtr<MockSdpFactory> mock_sdp_factory(new MockSdpFactory());
    SrsUniquePtr<MockAppFactoryForRtcConn> mock_app_factory(new MockAppFactoryForRtcConn());
    SrsStreamPublishTokenManager token_manager;

    mock_config->rtc_dtls_role_ = "passive";
    mock_dtls_certificate->fingerprint_ = "test-fingerprint";
    mock_app_factory->rtc_sources_ = mock_rtc_sources.get();
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
    MockRtcSource *mock_rtc_source = new MockRtcSource();
    mock_rtc_sources->mock_source_ = SrsSharedPtr<SrsRtcSource>(mock_rtc_source);

    // Create a real ISrsRtcConnection using _srs_app_factory_
    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    cid.set_value("test-rtc-conn-publisher-av1-workflow");

    SrsUniquePtr<ISrsRtcConnection> conn_ptr(_srs_app_factory->create_rtc_connection(&mock_exec, cid));
    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection *>(conn_ptr.get());
    EXPECT_TRUE(conn != NULL);

    // Mock the RTC conn, also mock the config in publisher_negotiator_ and player_negotiator_
    conn->circuit_breaker_ = mock_circuit_breaker.get();
    conn->conn_manager_ = mock_conn_manager.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->config_ = mock_config.get();
    conn->dtls_certificate_ = mock_dtls_certificate.get();
    conn->app_factory_ = mock_app_factory.get();

    SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn->publisher_negotiator_);
    pub_neg->config_ = mock_config.get();
    SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn->player_negotiator_);
    play_neg->config_ = mock_config.get();
    play_neg->rtc_sources_ = mock_rtc_sources.get();

    // Create RTC user config for add_publisher with AV1 codec
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    if (true) {
        srs_freep(ruc->req_);
        ruc->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc->publish_ = true;
        ruc->dtls_ = true;
        ruc->srtp_ = true;
        ruc->audio_before_video_ = false;
        ruc->vcodec_ = "av1"; // Specify AV1 codec

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_publisher_offer_with_av1();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
        EXPECT_TRUE(ruc->remote_sdp_.media_descs_.size() == 2);
    }

    // Add publisher, which negotiate the SDP and generate local SDP
    SrsSdp local_sdp;
    local_sdp.session_config_.dtls_role_ = mock_config->get_rtc_dtls_role(ruc->req_->vhost_);

    if (true) {
        HELPER_EXPECT_SUCCESS(conn->add_publisher(ruc.get(), local_sdp));

        // Verify publishers and SSRC mappings
        EXPECT_TRUE(conn->publishers_.size() == 1);
        EXPECT_TRUE(conn->publishers_ssrc_map_.size() == 2);
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->audio_ssrc_) != conn->publishers_ssrc_map_.end());
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->video_ssrc_) != conn->publishers_ssrc_map_.end());

        // Verify the source stream desription, should have two tracks.
        SrsRtcSourceDescription *stream_desc = mock_rtc_sources->mock_source_->stream_desc_;
        EXPECT_TRUE(stream_desc->audio_track_desc_ != NULL);
        EXPECT_TRUE(stream_desc->video_track_descs_.size() == 1);

        // Verify the audio track ssrc and payload type.
        EXPECT_TRUE(stream_desc->audio_track_desc_->ssrc_ == mock_sdp_factory->audio_ssrc_);
        EXPECT_TRUE(stream_desc->audio_track_desc_->media_->pt_ == mock_sdp_factory->audio_pt_);

        // Verify the video track ssrc and payload type.
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->ssrc_ == mock_sdp_factory->video_ssrc_);
        // AV1 uses payload type 45
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->pt_ == 45);

        // Verify the codec is AV1
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->name_ == "AV1");

        // Verify the local SDP was generated with media information
        EXPECT_TRUE(local_sdp.version_ == "0");
        EXPECT_TRUE(local_sdp.group_policy_ == "BUNDLE");
        EXPECT_TRUE(local_sdp.msids_.size() == 1);
        EXPECT_TRUE(local_sdp.msids_[0] == "live/stream1");
        EXPECT_TRUE(local_sdp.media_descs_.size() == 2);

        // First should be audio media desc
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(audio_desc->type_ == "audio");
        EXPECT_TRUE(audio_desc->recvonly_);
        EXPECT_TRUE(audio_desc->payload_types_.size() == 1);
        EXPECT_TRUE(audio_desc->payload_types_[0].payload_type_ == mock_sdp_factory->audio_pt_);
        EXPECT_TRUE(audio_desc->payload_types_[0].encoding_name_ == "opus");
        EXPECT_TRUE(audio_desc->payload_types_[0].clock_rate_ == 48000);

        // Second should be video media desc with AV1
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(video_desc->type_ == "video");
        EXPECT_TRUE(video_desc->recvonly_);
        EXPECT_TRUE(video_desc->payload_types_.size() == 1);
        EXPECT_TRUE(video_desc->payload_types_[0].payload_type_ == 45);
        EXPECT_TRUE(video_desc->payload_types_[0].encoding_name_ == "AV1");
        EXPECT_TRUE(video_desc->payload_types_[0].clock_rate_ == 90000);
    }

    // Generate local SDP and setup SDP.
    std::string username;
    if (true) {
        bool status = true;
        conn->set_all_tracks_status(ruc->req_->get_stream_url(), ruc->publish_, status);

        HELPER_EXPECT_SUCCESS(conn->generate_local_sdp(ruc.get(), local_sdp, username));
        conn->set_remote_sdp(ruc->remote_sdp_);
        conn->set_local_sdp(local_sdp);
        conn->set_state_as_waiting_stun();

        // Verify the local SDP was generated ice pwd
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(!audio_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!audio_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(audio_desc->candidates_.size() == 1);
        EXPECT_TRUE(audio_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(audio_desc->session_info_.setup_ == "passive");

        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(!video_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!video_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(video_desc->candidates_.size() == 1);
        EXPECT_TRUE(video_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(video_desc->session_info_.setup_ == "passive");

        EXPECT_TRUE(local_sdp.session_negotiate_.dtls_role_ == "passive");
    }

    // Initialize the connection
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->initialize(ruc->req_, ruc->dtls_, ruc->srtp_, username));
        EXPECT_TRUE(conn->nack_enabled_);

        // Create and set publish token
        SrsStreamPublishToken *publish_token_raw = NULL;
        HELPER_EXPECT_SUCCESS(token_manager.acquire_token(ruc->req_, publish_token_raw));
        SrsSharedPtr<ISrsStreamPublishToken> publish_token(publish_token_raw);

        conn->set_publish_token(publish_token);
        EXPECT_TRUE(conn->publish_token_->is_acquired());
    }

    // DTLS done, start publisher
    SrsRtcPublishStream *publisher = NULL;
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->on_dtls_handshake_done());

        // Wait for coroutine to start. Normally it should be ready wait for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the publisher is created and started
        EXPECT_TRUE(conn->publishers_.size() == 1);
        publisher = dynamic_cast<SrsRtcPublishStream *>(conn->publishers_.begin()->second);
        EXPECT_TRUE(publisher->is_sender_started_);
    }

    // Got a RTP audio packet.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->audio_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(mock_sdp_factory->audio_pt_);

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_audio_count_, i + 1);
    }

    // Got a RTP video packet with AV1 payload type.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->video_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(45); // AV1 payload type

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_video_count_, i + 1);
    }

    // Stop the publisher
    publisher->stop();
}

// This test is used to verify the basic workflow of the RTC connection with VP9 codec.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcManuallyVerifyForPublisherWithVP9)
{
    srs_error_t err;

    // Create mock dependencies FIRST (they must outlive the connection)
    SrsUniquePtr<MockCircuitBreaker> mock_circuit_breaker(new MockCircuitBreaker());
    SrsUniquePtr<MockConnectionManager> mock_conn_manager(new MockConnectionManager());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockDtlsCertificate> mock_dtls_certificate(new MockDtlsCertificate());
    SrsUniquePtr<MockSdpFactory> mock_sdp_factory(new MockSdpFactory());
    SrsUniquePtr<MockAppFactoryForRtcConn> mock_app_factory(new MockAppFactoryForRtcConn());
    SrsStreamPublishTokenManager token_manager;

    mock_config->rtc_dtls_role_ = "passive";
    mock_dtls_certificate->fingerprint_ = "test-fingerprint";
    mock_app_factory->rtc_sources_ = mock_rtc_sources.get();
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
    MockRtcSource *mock_rtc_source = new MockRtcSource();
    mock_rtc_sources->mock_source_ = SrsSharedPtr<SrsRtcSource>(mock_rtc_source);

    // Create a real ISrsRtcConnection using _srs_app_factory_
    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    cid.set_value("test-rtc-conn-publisher-vp9-workflow");

    SrsUniquePtr<ISrsRtcConnection> conn_ptr(_srs_app_factory->create_rtc_connection(&mock_exec, cid));
    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection *>(conn_ptr.get());
    EXPECT_TRUE(conn != NULL);

    // Mock the RTC conn, also mock the config in publisher_negotiator_ and player_negotiator_
    conn->circuit_breaker_ = mock_circuit_breaker.get();
    conn->conn_manager_ = mock_conn_manager.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->config_ = mock_config.get();
    conn->dtls_certificate_ = mock_dtls_certificate.get();
    conn->app_factory_ = mock_app_factory.get();

    SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn->publisher_negotiator_);
    pub_neg->config_ = mock_config.get();
    SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn->player_negotiator_);
    play_neg->config_ = mock_config.get();
    play_neg->rtc_sources_ = mock_rtc_sources.get();

    // Create RTC user config for add_publisher with VP9 codec
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    if (true) {
        srs_freep(ruc->req_);
        ruc->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc->publish_ = true;
        ruc->dtls_ = true;
        ruc->srtp_ = true;
        ruc->audio_before_video_ = false;
        ruc->vcodec_ = "vp9"; // Specify VP9 codec

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_publisher_offer_with_vp9();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
        EXPECT_TRUE(ruc->remote_sdp_.media_descs_.size() == 2);
    }

    // Add publisher, which negotiate the SDP and generate local SDP
    SrsSdp local_sdp;
    local_sdp.session_config_.dtls_role_ = mock_config->get_rtc_dtls_role(ruc->req_->vhost_);

    if (true) {
        HELPER_EXPECT_SUCCESS(conn->add_publisher(ruc.get(), local_sdp));

        // Verify publishers and SSRC mappings
        EXPECT_TRUE(conn->publishers_.size() == 1);
        EXPECT_TRUE(conn->publishers_ssrc_map_.size() == 2);
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->audio_ssrc_) != conn->publishers_ssrc_map_.end());
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->video_ssrc_) != conn->publishers_ssrc_map_.end());

        // Verify the source stream desription, should have two tracks.
        SrsRtcSourceDescription *stream_desc = mock_rtc_sources->mock_source_->stream_desc_;
        EXPECT_TRUE(stream_desc->audio_track_desc_ != NULL);
        EXPECT_TRUE(stream_desc->video_track_descs_.size() == 1);

        // Verify the audio track ssrc and payload type.
        EXPECT_TRUE(stream_desc->audio_track_desc_->ssrc_ == mock_sdp_factory->audio_ssrc_);
        EXPECT_TRUE(stream_desc->audio_track_desc_->media_->pt_ == mock_sdp_factory->audio_pt_);

        // Verify the video track ssrc and payload type.
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->ssrc_ == mock_sdp_factory->video_ssrc_);
        // VP9 uses payload type 98
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->pt_ == 98);

        // Verify the codec is VP9
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->name_ == "VP9");

        // Verify the local SDP was generated with media information
        EXPECT_TRUE(local_sdp.version_ == "0");
        EXPECT_TRUE(local_sdp.group_policy_ == "BUNDLE");
        EXPECT_TRUE(local_sdp.msids_.size() == 1);
        EXPECT_TRUE(local_sdp.msids_[0] == "live/stream1");
        EXPECT_TRUE(local_sdp.media_descs_.size() == 2);

        // First should be audio media desc
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(audio_desc->type_ == "audio");
        EXPECT_TRUE(audio_desc->recvonly_);
        EXPECT_TRUE(audio_desc->payload_types_.size() == 1);
        EXPECT_TRUE(audio_desc->payload_types_[0].payload_type_ == mock_sdp_factory->audio_pt_);
        EXPECT_TRUE(audio_desc->payload_types_[0].encoding_name_ == "opus");
        EXPECT_TRUE(audio_desc->payload_types_[0].clock_rate_ == 48000);

        // Second should be video media desc with VP9
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(video_desc->type_ == "video");
        EXPECT_TRUE(video_desc->recvonly_);
        EXPECT_TRUE(video_desc->payload_types_.size() == 1);
        EXPECT_TRUE(video_desc->payload_types_[0].payload_type_ == 98);
        EXPECT_TRUE(video_desc->payload_types_[0].encoding_name_ == "VP9");
        EXPECT_TRUE(video_desc->payload_types_[0].clock_rate_ == 90000);
    }

    // Generate local SDP and setup SDP.
    std::string username;
    if (true) {
        bool status = true;
        conn->set_all_tracks_status(ruc->req_->get_stream_url(), ruc->publish_, status);

        HELPER_EXPECT_SUCCESS(conn->generate_local_sdp(ruc.get(), local_sdp, username));
        conn->set_remote_sdp(ruc->remote_sdp_);
        conn->set_local_sdp(local_sdp);
        conn->set_state_as_waiting_stun();

        // Verify the local SDP was generated ice pwd
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(!audio_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!audio_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(audio_desc->candidates_.size() == 1);
        EXPECT_TRUE(audio_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(audio_desc->session_info_.setup_ == "passive");

        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(!video_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!video_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(video_desc->candidates_.size() == 1);
        EXPECT_TRUE(video_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(video_desc->session_info_.setup_ == "passive");

        EXPECT_TRUE(local_sdp.session_negotiate_.dtls_role_ == "passive");
    }

    // Initialize the connection
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->initialize(ruc->req_, ruc->dtls_, ruc->srtp_, username));
        EXPECT_TRUE(conn->nack_enabled_);

        // Create and set publish token
        SrsStreamPublishToken *publish_token_raw = NULL;
        HELPER_EXPECT_SUCCESS(token_manager.acquire_token(ruc->req_, publish_token_raw));
        SrsSharedPtr<ISrsStreamPublishToken> publish_token(publish_token_raw);

        conn->set_publish_token(publish_token);
        EXPECT_TRUE(conn->publish_token_->is_acquired());
    }

    // DTLS done, start publisher
    SrsRtcPublishStream *publisher = NULL;
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->on_dtls_handshake_done());

        // Wait for coroutine to start. Normally it should be ready wait for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the publisher is created and started
        EXPECT_TRUE(conn->publishers_.size() == 1);
        publisher = dynamic_cast<SrsRtcPublishStream *>(conn->publishers_.begin()->second);
        EXPECT_TRUE(publisher->is_sender_started_);
    }

    // Got a RTP audio packet.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->audio_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(mock_sdp_factory->audio_pt_);

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_audio_count_, i + 1);
    }

    // Got a RTP video packet with VP9 payload type.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->video_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(98); // VP9 payload type

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_video_count_, i + 1);
    }

    // Stop the publisher
    publisher->stop();
}

// This test is used to verify the basic workflow of the RTC connection with G.711 PCMU codec.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcManuallyVerifyForPublisherWithG711Pcmu)
{
    srs_error_t err;

    // Create mock dependencies FIRST (they must outlive the connection)
    SrsUniquePtr<MockCircuitBreaker> mock_circuit_breaker(new MockCircuitBreaker());
    SrsUniquePtr<MockConnectionManager> mock_conn_manager(new MockConnectionManager());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockDtlsCertificate> mock_dtls_certificate(new MockDtlsCertificate());
    SrsUniquePtr<MockSdpFactory> mock_sdp_factory(new MockSdpFactory());
    SrsUniquePtr<MockAppFactoryForRtcConn> mock_app_factory(new MockAppFactoryForRtcConn());
    SrsStreamPublishTokenManager token_manager;

    mock_config->rtc_dtls_role_ = "passive";
    mock_dtls_certificate->fingerprint_ = "test-fingerprint";
    mock_app_factory->rtc_sources_ = mock_rtc_sources.get();
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
    MockRtcSource *mock_rtc_source = new MockRtcSource();
    mock_rtc_sources->mock_source_ = SrsSharedPtr<SrsRtcSource>(mock_rtc_source);

    // Create a real ISrsRtcConnection using _srs_app_factory_
    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    cid.set_value("test-rtc-conn-publisher-g711-pcmu-workflow");

    SrsUniquePtr<ISrsRtcConnection> conn_ptr(_srs_app_factory->create_rtc_connection(&mock_exec, cid));
    SrsRtcConnection *conn = dynamic_cast<SrsRtcConnection *>(conn_ptr.get());
    EXPECT_TRUE(conn != NULL);

    // Mock the RTC conn, also mock the config in publisher_negotiator_ and player_negotiator_
    conn->circuit_breaker_ = mock_circuit_breaker.get();
    conn->conn_manager_ = mock_conn_manager.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->config_ = mock_config.get();
    conn->dtls_certificate_ = mock_dtls_certificate.get();
    conn->app_factory_ = mock_app_factory.get();

    SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn->publisher_negotiator_);
    pub_neg->config_ = mock_config.get();
    SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn->player_negotiator_);
    play_neg->config_ = mock_config.get();
    play_neg->rtc_sources_ = mock_rtc_sources.get();

    // Create RTC user config for add_publisher with G.711 PCMU codec
    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    if (true) {
        srs_freep(ruc->req_);
        ruc->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc->publish_ = true;
        ruc->dtls_ = true;
        ruc->srtp_ = true;
        ruc->audio_before_video_ = false;
        ruc->acodec_ = "pcmu"; // Specify PCMU codec

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_publisher_offer_with_g711_pcmu();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
        EXPECT_TRUE(ruc->remote_sdp_.media_descs_.size() == 2);
    }

    // Add publisher, which negotiate the SDP and generate local SDP
    SrsSdp local_sdp;
    local_sdp.session_config_.dtls_role_ = mock_config->get_rtc_dtls_role(ruc->req_->vhost_);

    if (true) {
        HELPER_EXPECT_SUCCESS(conn->add_publisher(ruc.get(), local_sdp));

        // Verify publishers and SSRC mappings
        EXPECT_TRUE(conn->publishers_.size() == 1);
        EXPECT_TRUE(conn->publishers_ssrc_map_.size() == 2);
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->audio_ssrc_) != conn->publishers_ssrc_map_.end());
        EXPECT_TRUE(conn->publishers_ssrc_map_.find(mock_sdp_factory->video_ssrc_) != conn->publishers_ssrc_map_.end());

        // Verify the source stream desription, should have two tracks.
        SrsRtcSourceDescription *stream_desc = mock_rtc_sources->mock_source_->stream_desc_;
        EXPECT_TRUE(stream_desc->audio_track_desc_ != NULL);
        EXPECT_TRUE(stream_desc->video_track_descs_.size() == 1);

        // Verify the audio track ssrc and payload type.
        EXPECT_TRUE(stream_desc->audio_track_desc_->ssrc_ == mock_sdp_factory->audio_ssrc_);
        // PCMU uses payload type 0
        EXPECT_TRUE(stream_desc->audio_track_desc_->media_->pt_ == 0);

        // Verify the codec is PCMU
        EXPECT_TRUE(stream_desc->audio_track_desc_->media_->name_ == "PCMU");

        // Verify the video track ssrc and payload type.
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->ssrc_ == mock_sdp_factory->video_ssrc_);
        EXPECT_TRUE(stream_desc->video_track_descs_[0]->media_->pt_ == mock_sdp_factory->video_pt_);

        // Verify the local SDP was generated with media information
        EXPECT_TRUE(local_sdp.version_ == "0");
        EXPECT_TRUE(local_sdp.group_policy_ == "BUNDLE");
        EXPECT_TRUE(local_sdp.msids_.size() == 1);
        EXPECT_TRUE(local_sdp.msids_[0] == "live/stream1");
        EXPECT_TRUE(local_sdp.media_descs_.size() == 2);

        // First should be audio media desc with PCMU
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(audio_desc->type_ == "audio");
        EXPECT_TRUE(audio_desc->recvonly_);
        EXPECT_TRUE(audio_desc->payload_types_.size() == 1);
        EXPECT_TRUE(audio_desc->payload_types_[0].payload_type_ == 0);
        EXPECT_TRUE(audio_desc->payload_types_[0].encoding_name_ == "PCMU");
        EXPECT_TRUE(audio_desc->payload_types_[0].clock_rate_ == 8000);

        // Second should be video media desc
        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(video_desc->type_ == "video");
        EXPECT_TRUE(video_desc->recvonly_);
        EXPECT_TRUE(video_desc->payload_types_.size() == 1);
        EXPECT_TRUE(video_desc->payload_types_[0].payload_type_ == mock_sdp_factory->video_pt_);
        EXPECT_TRUE(video_desc->payload_types_[0].encoding_name_ == "H264");
        EXPECT_TRUE(video_desc->payload_types_[0].clock_rate_ == 90000);
    }

    // Generate local SDP and setup SDP.
    std::string username;
    if (true) {
        bool status = true;
        conn->set_all_tracks_status(ruc->req_->get_stream_url(), ruc->publish_, status);

        HELPER_EXPECT_SUCCESS(conn->generate_local_sdp(ruc.get(), local_sdp, username));
        conn->set_remote_sdp(ruc->remote_sdp_);
        conn->set_local_sdp(local_sdp);
        conn->set_state_as_waiting_stun();

        // Verify the local SDP was generated ice pwd
        SrsMediaDesc *audio_desc = &local_sdp.media_descs_[0];
        EXPECT_TRUE(!audio_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!audio_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(audio_desc->candidates_.size() == 1);
        EXPECT_TRUE(audio_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(audio_desc->session_info_.setup_ == "passive");

        SrsMediaDesc *video_desc = &local_sdp.media_descs_[1];
        EXPECT_TRUE(!video_desc->session_info_.ice_pwd_.empty());
        EXPECT_TRUE(!video_desc->session_info_.fingerprint_.empty());
        EXPECT_TRUE(video_desc->candidates_.size() == 1);
        EXPECT_TRUE(video_desc->candidates_[0].ip_ == "192.168.1.100");
        EXPECT_TRUE(video_desc->session_info_.setup_ == "passive");

        EXPECT_TRUE(local_sdp.session_negotiate_.dtls_role_ == "passive");
    }

    // Initialize the connection
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->initialize(ruc->req_, ruc->dtls_, ruc->srtp_, username));
        EXPECT_TRUE(conn->nack_enabled_);

        // Create and set publish token
        SrsStreamPublishToken *publish_token_raw = NULL;
        HELPER_EXPECT_SUCCESS(token_manager.acquire_token(ruc->req_, publish_token_raw));
        SrsSharedPtr<ISrsStreamPublishToken> publish_token(publish_token_raw);

        conn->set_publish_token(publish_token);
        EXPECT_TRUE(conn->publish_token_->is_acquired());
    }

    // DTLS done, start publisher
    SrsRtcPublishStream *publisher = NULL;
    if (true) {
        HELPER_EXPECT_SUCCESS(conn->on_dtls_handshake_done());

        // Wait for coroutine to start. Normally it should be ready wait for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the publisher is created and started
        EXPECT_TRUE(conn->publishers_.size() == 1);
        publisher = dynamic_cast<SrsRtcPublishStream *>(conn->publishers_.begin()->second);
        EXPECT_TRUE(publisher->is_sender_started_);
    }

    // Got a RTP audio packet with PCMU payload type.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->audio_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(0); // PCMU payload type

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_audio_count_, i + 1);
    }

    // Got a RTP video packet.
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(mock_sdp_factory->video_ssrc_);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(1000);
        pkt.header_.set_payload_type(mock_sdp_factory->video_pt_);

        SrsUniquePtr<char[]> data(new char[1500]);
        SrsBuffer buf(data.get(), 1500);
        HELPER_EXPECT_SUCCESS(pkt.encode(&buf));

        bool dropped = false;
        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos(), &dropped));
        HELPER_EXPECT_SUCCESS(conn->on_rtp_plaintext(data.get(), buf.pos()));

        EXPECT_EQ(mock_rtc_source->rtp_video_count_, i + 1);
    }

    // Stop the publisher
    publisher->stop();
}

#ifdef SRS_FFMPEG_FIT
// The scenario for the play negotiation tests below, see issue #4738.
//
// SRS treats H.264, H.265 and AV1 equally, and the rule is the same for all of them:
//
//   1. Before publishing, the codec of the stream is unknown, so the client decides. Any codec it
//      asks for is allowed, and asking for none answers the first placeholder track. See
//      SrsRtcSource::init_for_play_before_publishing for the placeholder tracks.
//   2. While publishing, the codec of the stream is known, so the stream decides. Asking for none
//      answers the codec of the stream, asking for that codec is allowed, and asking for any other
//      codec is refused, because SRS never transcodes for WebRTC and the client could only ever
//      get a black picture.
//
// A stream published by a non-WebRTC publisher, such as RTMP or SRT, keeps the placeholder tracks,
// and the bridge selects the track of the codec it detects from the video sequence header, see
// SrsRtcRtpBuilder::on_video. So the answer must match the track the bridge selected, both the
// codec and the SSRC, otherwise SrsRtcPlayStream::send_packet drops every packet as "Drop for ssrc
// %u not found" and the client shows a black picture.
//
// A stream published by a WebRTC publisher replaces the placeholder tracks with the tracks it
// negotiated, see SrsRtcConnection::add_publisher, so the source holds exactly one video track.
// @see https://github.com/ossrs/srs/issues/4738
class MockRtcPlayScenario
{
public:
    MockRtcSourceManager rtc_sources_;
    MockAppConfig config_;
    MockSdpFactory sdp_;
    MockRtcAsyncCallRequest req_;
    MockRtpTarget rtp_target_;
    SrsSharedPtr<SrsRtcSource> source_;
    SrsRtcRtpBuilder *builder_;
    SrsRtcPlayerNegotiator negotiator_;
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations_;

public:
    MockRtcPlayScenario() : req_("test.vhost", "live", "stream1"), source_(new SrsRtcSource())
    {
        builder_ = NULL;
        negotiator_.config_ = &config_;
        negotiator_.rtc_sources_ = &rtc_sources_;
    }
    virtual ~MockRtcPlayScenario()
    {
        srs_freep(builder_);

        std::map<uint32_t, SrsRtcTrackDescription *>::iterator it;
        for (it = sub_relations_.begin(); it != sub_relations_.end(); ++it) {
            srs_freep(it->second);
        }
    }

public:
    // Create the source with the placeholder tracks, without any publisher yet.
    srs_error_t initialize()
    {
        srs_error_t err = srs_success;

        if ((err = source_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "initialize source");
        }
        rtc_sources_.mock_source_ = source_;

        return err;
    }

    // Publish a stream of the codec by RTMP or SRT, which is bridged to the RTC source. Only H.264
    // and H.265 are supported by the bridge, see SrsRtcRtpBuilder::on_video.
    srs_error_t publish_by_bridge(SrsVideoCodecId codec)
    {
        srs_error_t err = srs_success;

        if ((err = initialize()) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }

        builder_ = new SrsRtcRtpBuilder(_srs_app_factory, &rtp_target_, source_);
        if ((err = builder_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "initialize builder");
        }

        // The bridge selects the track of the codec it detected from the sequence header.
        if ((err = builder_->initialize_video_track(codec)) != srs_success) {
            return srs_error_wrap(err, "initialize video track");
        }

        return err;
    }

    // Publish a stream of the codec by WebRTC, which replaces the placeholder tracks with the
    // negotiated ones, so the source holds exactly one video track.
    srs_error_t publish_by_webrtc(std::string codec_name, uint8_t video_pt)
    {
        srs_error_t err = srs_success;

        if ((err = initialize()) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }

        SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());

        SrsRtcTrackDescription *audio_track = new SrsRtcTrackDescription();
        stream_desc->audio_track_desc_ = audio_track;
        audio_track->type_ = "audio";
        audio_track->id_ = "audio-publisher";
        audio_track->ssrc_ = 3001;
        audio_track->direction_ = "recvonly";
        audio_track->media_ = new SrsAudioPayload(kAudioPayloadType, "opus", 48000, 2);

        SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(video_track);
        video_track->type_ = "video";
        video_track->id_ = "video-publisher";
        video_track->ssrc_ = 3002;
        video_track->direction_ = "recvonly";
        video_track->media_ = new SrsVideoPayload(video_pt, codec_name, 90000);

        source_->set_stream_desc(stream_desc.get());

        return err;
    }

    // Play the stream, with vcodec asked by the client, or empty if the client asks for none.
    srs_error_t play(std::string vcodec, std::string offer)
    {
        srs_error_t err = srs_success;

        SrsRtcUserConfig ruc;
        srs_freep(ruc.req_);
        ruc.req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc.publish_ = false;
        ruc.dtls_ = true;
        ruc.srtp_ = true;
        ruc.vcodec_ = vcodec;

        if ((err = ruc.remote_sdp_.parse(offer)) != srs_success) {
            return srs_error_wrap(err, "parse offer");
        }

        return negotiator_.negotiate_play_capability(&ruc, sub_relations_);
    }

    // The video track the player subscribes to, NULL if the player gets no video at all.
    SrsRtcTrackDescription *video_track()
    {
        std::map<uint32_t, SrsRtcTrackDescription *>::iterator it;
        for (it = sub_relations_.begin(); it != sub_relations_.end(); ++it) {
            if (it->second->type_ == "video") {
                return it->second;
            }
        }
        return NULL;
    }

    // The SSRC of the publisher the player subscribes to, for the video track.
    uint32_t video_publish_ssrc()
    {
        std::map<uint32_t, SrsRtcTrackDescription *>::iterator it;
        for (it = sub_relations_.begin(); it != sub_relations_.end(); ++it) {
            if (it->second->type_ == "video") {
                return it->first;
            }
        }
        return 0;
    }

    // The SSRC the bridge builds the RTP packets with.
    uint32_t bridge_ssrc()
    {
        return builder_->video_builder_->video_ssrc_;
    }
};

// One row of the matrix below: the codec the client asks for, and the codec it should be answered
// with, or NULL if the request should be refused.
struct MockRtcPlayCase
{
    const char *vcodec_;
    const char *answer_;
};

#define SRS_ARRAY_LENGTH(a) (int)(sizeof(a) / sizeof(a[0]))

// Play before publishing. The codec of the stream is unknown, so the client decides: every codec
// it asks for is allowed, and asking for none answers the first placeholder track, H.264.
// @see https://github.com/ossrs/srs/issues/4738
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayBeforePublishWithAnyCodec)
{
    srs_error_t err;

    MockRtcPlayCase cases[] = {
        {"", "H264"},
        {"h264", "H264"},
        {"h265", "H265"},
        {"av1", "AV1"},
    };

    for (int i = 0; i < SRS_ARRAY_LENGTH(cases); i++) {
        MockRtcPlayScenario s;
        HELPER_EXPECT_SUCCESS(s.initialize());

        err = s.play(cases[i].vcodec_, s.sdp_.create_player_offer_with_all_codecs());
        EXPECT_TRUE(srs_success == err) << "vcodec=" << cases[i].vcodec_ << ", err=" << srs_error_desc(err);
        srs_freep(err);

        ASSERT_TRUE(s.video_track() != NULL) << "vcodec=" << cases[i].vcodec_;
        EXPECT_STREQ(cases[i].answer_, s.video_track()->media_->name_.c_str()) << "vcodec=" << cases[i].vcodec_;
    }
}

// Publish H.264 by RTMP or SRT. The codec of the stream is known, so the stream decides: asking for
// none or for H.264 answers H.264, on the SSRC the bridge sends with, and asking for any other
// codec is refused.
// @see https://github.com/ossrs/srs/issues/4738
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayAvcStreamWithAnyCodec)
{
    srs_error_t err;

    MockRtcPlayCase cases[] = {
        {"", "H264"},
        {"h264", "H264"},
        {"h265", NULL},
        {"av1", NULL},
    };

    for (int i = 0; i < SRS_ARRAY_LENGTH(cases); i++) {
        MockRtcPlayScenario s;
        HELPER_EXPECT_SUCCESS(s.publish_by_bridge(SrsVideoCodecIdAVC));

        err = s.play(cases[i].vcodec_, s.sdp_.create_player_offer_with_all_codecs());
        if (!cases[i].answer_) {
            EXPECT_TRUE(srs_success != err) << "vcodec=" << cases[i].vcodec_;
            srs_freep(err);
            continue;
        }

        EXPECT_TRUE(srs_success == err) << "vcodec=" << cases[i].vcodec_ << ", err=" << srs_error_desc(err);
        srs_freep(err);

        ASSERT_TRUE(s.video_track() != NULL) << "vcodec=" << cases[i].vcodec_;
        EXPECT_STREQ(cases[i].answer_, s.video_track()->media_->name_.c_str()) << "vcodec=" << cases[i].vcodec_;
        EXPECT_EQ(s.bridge_ssrc(), s.video_publish_ssrc()) << "vcodec=" << cases[i].vcodec_;
    }
}

// Publish H.265 by RTMP or SRT, which is the issue reported in #4738. The same rule as H.264.
// @see https://github.com/ossrs/srs/issues/4738
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayHevcStreamWithAnyCodec)
{
    srs_error_t err;

    MockRtcPlayCase cases[] = {
        {"", "H265"},
        {"h264", NULL},
        {"h265", "H265"},
        {"av1", NULL},
    };

    for (int i = 0; i < SRS_ARRAY_LENGTH(cases); i++) {
        MockRtcPlayScenario s;
        HELPER_EXPECT_SUCCESS(s.publish_by_bridge(SrsVideoCodecIdHEVC));

        err = s.play(cases[i].vcodec_, s.sdp_.create_player_offer_with_all_codecs());
        if (!cases[i].answer_) {
            EXPECT_TRUE(srs_success != err) << "vcodec=" << cases[i].vcodec_;
            srs_freep(err);
            continue;
        }

        EXPECT_TRUE(srs_success == err) << "vcodec=" << cases[i].vcodec_ << ", err=" << srs_error_desc(err);
        srs_freep(err);

        ASSERT_TRUE(s.video_track() != NULL) << "vcodec=" << cases[i].vcodec_;
        EXPECT_STREQ(cases[i].answer_, s.video_track()->media_->name_.c_str()) << "vcodec=" << cases[i].vcodec_;
        EXPECT_EQ(s.bridge_ssrc(), s.video_publish_ssrc()) << "vcodec=" << cases[i].vcodec_;
    }
}

// Publish AV1 by WebRTC, which is the only way to publish AV1, because the RTMP to RTC bridge
// supports H.264 and H.265 only. The same rule as H.264 and H.265.
// @see https://github.com/ossrs/srs/issues/4738
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayAv1StreamWithAnyCodec)
{
    srs_error_t err;

    MockRtcPlayCase cases[] = {
        {"", "AV1"},
        {"h264", NULL},
        {"h265", NULL},
        {"av1", "AV1"},
    };

    for (int i = 0; i < SRS_ARRAY_LENGTH(cases); i++) {
        MockRtcPlayScenario s;
        HELPER_EXPECT_SUCCESS(s.publish_by_webrtc("AV1", 45));

        err = s.play(cases[i].vcodec_, s.sdp_.create_player_offer_with_all_codecs());
        if (!cases[i].answer_) {
            EXPECT_TRUE(srs_success != err) << "vcodec=" << cases[i].vcodec_;
            srs_freep(err);
            continue;
        }

        EXPECT_TRUE(srs_success == err) << "vcodec=" << cases[i].vcodec_ << ", err=" << srs_error_desc(err);
        srs_freep(err);

        ASSERT_TRUE(s.video_track() != NULL) << "vcodec=" << cases[i].vcodec_;
        EXPECT_STREQ(cases[i].answer_, s.video_track()->media_->name_.c_str()) << "vcodec=" << cases[i].vcodec_;
    }
}

// Play an H.265 stream by a client which does not support H.265, such as Chrome without HEVC. SRS
// does not transcode for WebRTC, so there is no way to serve this client. Refuse it with an
// explicit error, rather than answering an H.264 SDP that the server never sends, which leaves the
// client with a black picture and no clue why.
// @see https://github.com/ossrs/srs/issues/4738
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayHevcStreamByAvcOnlyPlayer)
{
    srs_error_t err;

    MockRtcPlayScenario s;
    HELPER_EXPECT_SUCCESS(s.publish_by_bridge(SrsVideoCodecIdHEVC));
    HELPER_EXPECT_FAILED(s.play("", s.sdp_.create_chrome_player_offer_with_h264()));
}
#endif

// A Chrome-like recvonly player offer with Opus, H.264 on video_pt, and two RFC 4588 rtx payloads: payload 98 whose
// apt names a payload that is not offered, listed first, then rtx_pt with the given apt. The answer must select
// the rtx payload by apt, never by position.
static std::string mock_player_offer_with_rtx(int video_pt, int rtx_pt, int apt)
{
    std::stringstream ss;
    ss << "v=0\r\n"
       << "o=- 4611731400430051339 2 IN IP4 127.0.0.1\r\n"
       << "s=-\r\n"
       << "t=0 0\r\n"
       << "a=group:BUNDLE 0 1\r\n"
       << "a=msid-semantic: WMS\r\n"
       << "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:0\r\n"
       << "a=recvonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtpmap:111 opus/48000/2\r\n"
       << "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
       << "m=video 9 UDP/TLS/RTP/SAVPF " << video_pt << " 98 " << rtx_pt << "\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:1\r\n"
       << "a=recvonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtcp-rsize\r\n"
       << "a=rtpmap:" << video_pt << " H264/90000\r\n"
       << "a=rtcp-fb:" << video_pt << " nack\r\n"
       << "a=rtcp-fb:" << video_pt << " nack pli\r\n"
       << "a=rtcp-fb:" << video_pt << " transport-cc\r\n"
       << "a=fmtp:" << video_pt << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
       << "a=rtpmap:98 rtx/90000\r\n"
       << "a=fmtp:98 apt=97\r\n"
       << "a=rtpmap:" << rtx_pt << " rtx/90000\r\n"
       << "a=fmtp:" << rtx_pt << " apt=" << apt << "\r\n";
    return ss.str();
}

// A player negotiation through the public add_player, with the mocks the manually verified player workflow uses. The
// mocks are declared before the connection so they outlive it.
class MockRtcPlayerRtxScenario
{
public:
    MockCircuitBreaker circuit_breaker_;
    MockConnectionManager conn_manager_;
    MockRtcSourceManager rtc_sources_;
    MockAppConfig config_;
    MockDtlsCertificate dtls_certificate_;
    MockSdpFactory sdp_;
    MockAppFactoryForRtcConn app_factory_;
    MockRtcAsyncTaskExecutor exec_;
    SrsContextId cid_;
    SrsUniquePtr<ISrsRtcConnection> conn_ptr_;
    SrsRtcConnection *conn_;
    SrsUniquePtr<SrsRtcUserConfig> ruc_;
    SrsSdp local_sdp_;

public:
    MockRtcPlayerRtxScenario() : conn_ptr_(_srs_app_factory->create_rtc_connection(&exec_, cid_)), ruc_(new SrsRtcUserConfig())
    {
        config_.rtc_dtls_role_ = "passive";
        dtls_certificate_.fingerprint_ = "test-fingerprint";
        app_factory_.rtc_sources_ = &rtc_sources_;
        app_factory_.mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
        rtc_sources_.mock_source_ = SrsSharedPtr<SrsRtcSource>(new MockRtcSource());

        conn_ = dynamic_cast<SrsRtcConnection *>(conn_ptr_.get());
        conn_->circuit_breaker_ = &circuit_breaker_;
        conn_->conn_manager_ = &conn_manager_;
        conn_->rtc_sources_ = &rtc_sources_;
        conn_->config_ = &config_;
        conn_->dtls_certificate_ = &dtls_certificate_;
        conn_->app_factory_ = &app_factory_;

        SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn_->publisher_negotiator_);
        pub_neg->config_ = &config_;
        SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn_->player_negotiator_);
        play_neg->config_ = &config_;
        play_neg->rtc_sources_ = &rtc_sources_;

        srs_freep(ruc_->req_);
        ruc_->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc_->publish_ = false;
        ruc_->dtls_ = true;
        ruc_->srtp_ = true;
        ruc_->audio_before_video_ = false;
    }
    virtual ~MockRtcPlayerRtxScenario()
    {
    }

public:
    // Negotiate the offer with the server preference, and return the result of add_player.
    srs_error_t add_player(const std::string &offer, bool prefer_rtx)
    {
        srs_error_t err = srs_success;

        config_.set_rtc_nack_prefer_rtx(prefer_rtx);

        ruc_->remote_sdp_str_ = offer;
        if ((err = ruc_->remote_sdp_.parse(offer)) != srs_success) {
            return srs_error_wrap(err, "parse offer");
        }

        local_sdp_.session_config_.dtls_role_ = config_.get_rtc_dtls_role(ruc_->req_->vhost_);
        if ((err = conn_->add_player(ruc_.get(), local_sdp_)) != srs_success) {
            return srs_error_wrap(err, "add player");
        }

        return err;
    }
    SrsMediaDesc *audio_desc() { return &local_sdp_.media_descs_[0]; }
    SrsMediaDesc *video_desc() { return &local_sdp_.media_descs_[1]; }
};

// Assert today's plain answer: one video payload, one SSRC, no FID group, and two SSRCs registered on the connection.
static void expect_plain_player_answer(MockRtcPlayerRtxScenario &s)
{
    EXPECT_EQ(2, (int)s.conn_->players_ssrc_map_.size());
    ASSERT_EQ(2, (int)s.local_sdp_.media_descs_.size());

    SrsMediaDesc *video = s.video_desc();
    ASSERT_EQ(1, (int)video->payload_types_.size());
    EXPECT_STREQ("H264", video->payload_types_[0].encoding_name_.c_str());
    EXPECT_TRUE(video->ssrc_groups_.empty());
    ASSERT_EQ(1, (int)video->ssrc_infos_.size());
    EXPECT_NE(0u, video->ssrc_infos_[0].ssrc_);
}

// With nack_prefer_rtx on and an offer carrying rtx for the negotiated codec, the play answer advertises RTX: the rtx
// payload at the media clock rate with apt, and a FID group binding the media SSRC to a second, generated SSRC that
// has its own a=ssrc line and the same cname. The rtx payload whose apt does not match is not selected.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayerRtxOfferNegotiatesRtx)
{
    srs_error_t err;

    MockRtcPlayerRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.add_player(mock_player_offer_with_rtx(96, 107, 96), true));

    // The media SSRC, the RTX SSRC, and the audio SSRC all route to the player.
    EXPECT_EQ(3, (int)s.conn_->players_ssrc_map_.size());
    ASSERT_EQ(2, (int)s.local_sdp_.media_descs_.size());

    SrsMediaDesc *video = s.video_desc();
    ASSERT_EQ(2, (int)video->payload_types_.size());
    EXPECT_EQ(96, video->payload_types_[0].payload_type_);
    EXPECT_STREQ("H264", video->payload_types_[0].encoding_name_.c_str());

    SrsMediaPayloadType &rtx = video->payload_types_[1];
    EXPECT_EQ(107, rtx.payload_type_);
    EXPECT_STREQ("rtx", rtx.encoding_name_.c_str());
    EXPECT_EQ(90000, rtx.clock_rate_);
    EXPECT_STREQ("apt=96", rtx.format_specific_param_.c_str());
    EXPECT_TRUE(rtx.rtcp_fb_.empty());

    ASSERT_EQ(1, (int)video->ssrc_groups_.size());
    EXPECT_STREQ("FID", video->ssrc_groups_[0].semantic_.c_str());
    ASSERT_EQ(2, (int)video->ssrc_groups_[0].ssrcs_.size());
    ASSERT_EQ(2, (int)video->ssrc_infos_.size());
    EXPECT_EQ(video->ssrc_infos_[0].ssrc_, video->ssrc_groups_[0].ssrcs_[0]);
    EXPECT_EQ(video->ssrc_infos_[1].ssrc_, video->ssrc_groups_[0].ssrcs_[1]);
    EXPECT_NE(0u, video->ssrc_groups_[0].ssrcs_[0]);
    EXPECT_NE(0u, video->ssrc_groups_[0].ssrcs_[1]);
    EXPECT_NE(video->ssrc_groups_[0].ssrcs_[0], video->ssrc_groups_[0].ssrcs_[1]);
    EXPECT_STREQ(video->ssrc_infos_[0].cname_.c_str(), video->ssrc_infos_[1].cname_.c_str());

    // On the wire the group binds the two SSRCs, and each SSRC declares its cname, RFC 5576 section 4.2. The srs-bench
    // layer caught the encoder skipping a=ssrc-group, so the encoded answer is asserted here too.
    if (true) {
        std::ostringstream os;
        HELPER_ASSERT_SUCCESS(s.local_sdp_.encode(os));
        std::string answer = os.str();

        std::stringstream fid;
        fid << "a=ssrc-group:FID " << video->ssrc_groups_[0].ssrcs_[0] << " " << video->ssrc_groups_[0].ssrcs_[1] << "\r\n";
        EXPECT_TRUE(answer.find(fid.str()) != std::string::npos) << answer;

        std::stringstream rtx_cname;
        rtx_cname << "a=ssrc:" << video->ssrc_groups_[0].ssrcs_[1] << " cname:" << video->ssrc_infos_[1].cname_ << "\r\n";
        EXPECT_TRUE(answer.find(rtx_cname.str()) != std::string::npos) << answer;
    }

    // Audio is untouched.
    SrsMediaDesc *audio = s.audio_desc();
    EXPECT_EQ(1, (int)audio->payload_types_.size());
    EXPECT_TRUE(audio->ssrc_groups_.empty());
    EXPECT_EQ(1, (int)audio->ssrc_infos_.size());
}

// A player that offers no rtx gets today's answer although the server prefers RTX. This passes from the start and
// locks in the accepted fallback: the preference never becomes a requirement.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayerWithoutRtxOfferKeepsPlainAnswer)
{
    srs_error_t err;

    MockRtcPlayerRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.add_player(s.sdp_.create_chrome_player_offer_with_h264(), true));
    expect_plain_player_answer(s);
}

// An RTX-capable offer against nack_prefer_rtx off gets today's answer, with no rtx payload and no FID group. This
// passes from the start and is the case that proves the preference is only a preference.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayerRtxOfferWithPreferOffKeepsPlainAnswer)
{
    srs_error_t err;

    MockRtcPlayerRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.add_player(mock_player_offer_with_rtx(96, 107, 96), false));
    expect_plain_player_answer(s);
}

// Drive SrsRtcPlayerNegotiator::negotiate_play_capability directly, with a mock config, a mock source holding the
// placeholder tracks, and a mock SSRC generator returning consecutive known values from 500001.
class MockRtcPlayerRtxNegotiation
{
public:
    MockRtcSourceManager rtc_sources_;
    MockAppConfig config_;
    MockRtcSSRCGenerator ssrc_generator_;
    MockSdpFactory sdp_;
    SrsRtcPlayerNegotiator negotiator_;
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations_;

public:
    MockRtcPlayerRtxNegotiation()
    {
        negotiator_.config_ = &config_;
        negotiator_.rtc_sources_ = &rtc_sources_;
        negotiator_.ssrc_generator_ = &ssrc_generator_;
        rtc_sources_.mock_source_ = SrsSharedPtr<SrsRtcSource>(new MockRtcSource());
    }
    virtual ~MockRtcPlayerRtxNegotiation()
    {
        std::map<uint32_t, SrsRtcTrackDescription *>::iterator it;
        for (it = sub_relations_.begin(); it != sub_relations_.end(); ++it) {
            srs_freep(it->second);
        }
    }

public:
    srs_error_t negotiate(const std::string &offer, bool prefer_rtx)
    {
        srs_error_t err = srs_success;

        config_.set_rtc_nack_prefer_rtx(prefer_rtx);

        SrsRtcUserConfig ruc;
        srs_freep(ruc.req_);
        ruc.req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc.publish_ = false;
        ruc.dtls_ = true;
        ruc.srtp_ = true;

        if ((err = ruc.remote_sdp_.parse(offer)) != srs_success) {
            return srs_error_wrap(err, "parse offer");
        }

        return negotiator_.negotiate_play_capability(&ruc, sub_relations_);
    }
    SrsRtcTrackDescription *video_track()
    {
        std::map<uint32_t, SrsRtcTrackDescription *>::iterator it;
        for (it = sub_relations_.begin(); it != sub_relations_.end(); ++it) {
            if (it->second->type_ == "video") {
                return it->second;
            }
        }
        return NULL;
    }
};

// The negotiator selects the rtx payload whose apt is the negotiated video payload type, plan D3, carries the offered
// clock rate, and allocates the RTX SSRC through the generator after the media SSRC, plan D7. The offer lists audio
// first, so the generator hands out 500001 to audio, 500002 to video, and 500003 to the RTX stream.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayNegotiatorSelectsRtxByApt)
{
    srs_error_t err;

    MockRtcPlayerRtxNegotiation s;
    HELPER_ASSERT_SUCCESS(s.negotiate(mock_player_offer_with_rtx(96, 107, 96), true));

    SrsRtcTrackDescription *track = s.video_track();
    ASSERT_TRUE(track != NULL);
    EXPECT_EQ(500002u, track->ssrc_);
    EXPECT_EQ(500003u, track->rtx_ssrc_);
    EXPECT_EQ(3, s.ssrc_generator_.count_);

    SrsRtxPayloadDes *rtx = dynamic_cast<SrsRtxPayloadDes *>(track->rtx_);
    ASSERT_TRUE(rtx != NULL);
    EXPECT_EQ(107, (int)rtx->pt_);
    EXPECT_EQ(96, (int)rtx->apt_);
    EXPECT_EQ(90000, rtx->sample_);
    EXPECT_STREQ("rtx", rtx->name_.c_str());
}

// Without a usable rtx offer the track carries no RTX at all, so the generator is asked for the two media SSRCs only.
// These cases pass from the start: today's answer is the accepted final answer for each of them, plan D1.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPlayNegotiatorKeepsPlainWithoutUsableRtx)
{
    srs_error_t err;

    // The offer has no rtx payload, although the server prefers RTX.
    if (true) {
        MockRtcPlayerRtxNegotiation s;
        HELPER_ASSERT_SUCCESS(s.negotiate(s.sdp_.create_chrome_player_offer_with_h264(), true));

        SrsRtcTrackDescription *track = s.video_track();
        ASSERT_TRUE(track != NULL);
        EXPECT_TRUE(track->rtx_ == NULL);
        EXPECT_EQ(0u, track->rtx_ssrc_);
        EXPECT_EQ(2, s.ssrc_generator_.count_);
    }

    // The offer has rtx for the negotiated codec, but the server does not prefer RTX.
    if (true) {
        MockRtcPlayerRtxNegotiation s;
        HELPER_ASSERT_SUCCESS(s.negotiate(mock_player_offer_with_rtx(96, 107, 96), false));

        SrsRtcTrackDescription *track = s.video_track();
        ASSERT_TRUE(track != NULL);
        EXPECT_TRUE(track->rtx_ == NULL);
        EXPECT_EQ(0u, track->rtx_ssrc_);
        EXPECT_EQ(2, s.ssrc_generator_.count_);
    }

    // Every rtx payload in the offer names another apt than the negotiated video payload type.
    if (true) {
        MockRtcPlayerRtxNegotiation s;
        HELPER_ASSERT_SUCCESS(s.negotiate(mock_player_offer_with_rtx(96, 107, 97), true));

        SrsRtcTrackDescription *track = s.video_track();
        ASSERT_TRUE(track != NULL);
        EXPECT_TRUE(track->rtx_ == NULL);
        EXPECT_EQ(0u, track->rtx_ssrc_);
        EXPECT_EQ(2, s.ssrc_generator_.count_);
    }
}

// A Chrome-like sendonly publisher offer with Opus on ssrc 300001 and H.264 on video_pt with ssrc 300011, plus two RFC
// 4588 rtx payloads: payload 98 whose apt names a payload that is not offered, then rtx_pt with the given apt. With
// with_fid, the offer binds the video SSRC to the RTX SSRC 300012 by a FID group, as every browser does.
static std::string mock_publisher_offer_with_rtx(int video_pt, int rtx_pt, int apt, bool with_fid)
{
    std::stringstream ss;
    ss << "v=0\r\n"
       << "o=- 4611731400430051340 2 IN IP4 127.0.0.1\r\n"
       << "s=-\r\n"
       << "t=0 0\r\n"
       << "a=group:BUNDLE 0 1\r\n"
       << "a=msid-semantic: WMS stream\r\n"
       << "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:0\r\n"
       << "a=sendonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtpmap:111 opus/48000/2\r\n"
       << "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
       << "a=ssrc:300001 cname:test-audio-cname\r\n"
       << "a=ssrc:300001 msid:stream audio\r\n"
       << "m=video 9 UDP/TLS/RTP/SAVPF " << video_pt << " 98 " << rtx_pt << "\r\n"
       << "c=IN IP4 0.0.0.0\r\n"
       << "a=rtcp:9 IN IP4 0.0.0.0\r\n"
       << "a=ice-ufrag:test1234\r\n"
       << "a=ice-pwd:testpassword1234567890\r\n"
       << "a=ice-options:trickle\r\n"
       << "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
       << "a=setup:actpass\r\n"
       << "a=mid:1\r\n"
       << "a=sendonly\r\n"
       << "a=rtcp-mux\r\n"
       << "a=rtcp-rsize\r\n"
       << "a=rtpmap:" << video_pt << " H264/90000\r\n"
       << "a=rtcp-fb:" << video_pt << " nack\r\n"
       << "a=rtcp-fb:" << video_pt << " nack pli\r\n"
       << "a=rtcp-fb:" << video_pt << " transport-cc\r\n"
       << "a=fmtp:" << video_pt << " level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
       << "a=rtpmap:98 rtx/90000\r\n"
       << "a=fmtp:98 apt=97\r\n"
       << "a=rtpmap:" << rtx_pt << " rtx/90000\r\n"
       << "a=fmtp:" << rtx_pt << " apt=" << apt << "\r\n";
    if (with_fid) {
        ss << "a=ssrc-group:FID 300011 300012\r\n";
    }
    ss << "a=ssrc:300011 cname:test-video-cname\r\n"
       << "a=ssrc:300011 msid:stream video\r\n";
    if (with_fid) {
        ss << "a=ssrc:300012 cname:test-video-cname\r\n"
           << "a=ssrc:300012 msid:stream video\r\n";
    }
    return ss.str();
}

static const uint32_t kPubVideoSsrc = 300011;
static const uint32_t kPubRtxSsrc = 300012;
static const uint8_t kPubVideoPt = 96;
static const uint8_t kPubRtxPt = 107;

// A publisher negotiated through the public add_publisher and brought to the state where RTP arrives, with the mocks
// the manually verified publisher workflow uses. Packets are fed as bytes through on_rtp_plaintext, and what reaches
// the source is read from the mock source.
class MockRtcPublisherRtxScenario
{
public:
    MockCircuitBreaker circuit_breaker_;
    MockConnectionManager conn_manager_;
    MockRtcSourceManager rtc_sources_;
    MockAppConfig config_;
    MockDtlsCertificate dtls_certificate_;
    MockSdpFactory sdp_;
    MockAppFactoryForRtcConn app_factory_;
    MockRtcAsyncTaskExecutor exec_;
    SrsStreamPublishTokenManager token_manager_;
    SrsContextId cid_;
    SrsUniquePtr<ISrsRtcConnection> conn_ptr_;
    SrsRtcConnection *conn_;
    MockRtcSource *source_;
    SrsRtcPublishStream *publisher_;
    SrsUniquePtr<SrsRtcUserConfig> ruc_;
    SrsSdp local_sdp_;
    uint8_t payload_[4];

public:
    MockRtcPublisherRtxScenario() : conn_ptr_(_srs_app_factory->create_rtc_connection(&exec_, cid_)), ruc_(new SrsRtcUserConfig())
    {
        publisher_ = NULL;
        config_.rtc_dtls_role_ = "passive";
        dtls_certificate_.fingerprint_ = "test-fingerprint";
        app_factory_.rtc_sources_ = &rtc_sources_;
        app_factory_.mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");
        source_ = new MockRtcSource();
        rtc_sources_.mock_source_ = SrsSharedPtr<SrsRtcSource>(source_);

        conn_ = dynamic_cast<SrsRtcConnection *>(conn_ptr_.get());
        conn_->circuit_breaker_ = &circuit_breaker_;
        conn_->conn_manager_ = &conn_manager_;
        conn_->rtc_sources_ = &rtc_sources_;
        conn_->config_ = &config_;
        conn_->dtls_certificate_ = &dtls_certificate_;
        conn_->app_factory_ = &app_factory_;

        SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn_->publisher_negotiator_);
        pub_neg->config_ = &config_;
        SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn_->player_negotiator_);
        play_neg->config_ = &config_;
        play_neg->rtc_sources_ = &rtc_sources_;

        srs_freep(ruc_->req_);
        ruc_->req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc_->publish_ = true;
        ruc_->dtls_ = true;
        ruc_->srtp_ = true;

        payload_[0] = 0x65;
        payload_[1] = 0x88;
        payload_[2] = 0x84;
        payload_[3] = 0x00;
    }
    virtual ~MockRtcPublisherRtxScenario()
    {
        if (publisher_) {
            publisher_->stop();
        }
    }

public:
    // Negotiate the offer with the server preference and bring the publisher up to receiving RTP.
    srs_error_t publish(const std::string &offer, bool prefer_rtx)
    {
        srs_error_t err = srs_success;

        config_.set_rtc_nack_prefer_rtx(prefer_rtx);

        ruc_->remote_sdp_str_ = offer;
        if ((err = ruc_->remote_sdp_.parse(offer)) != srs_success) {
            return srs_error_wrap(err, "parse offer");
        }

        local_sdp_.session_config_.dtls_role_ = config_.get_rtc_dtls_role(ruc_->req_->vhost_);
        if ((err = conn_->add_publisher(ruc_.get(), local_sdp_)) != srs_success) {
            return srs_error_wrap(err, "add publisher");
        }

        std::string username;
        conn_->set_all_tracks_status(ruc_->req_->get_stream_url(), ruc_->publish_, true);
        if ((err = conn_->generate_local_sdp(ruc_.get(), local_sdp_, username)) != srs_success) {
            return srs_error_wrap(err, "local sdp");
        }
        conn_->set_remote_sdp(ruc_->remote_sdp_);
        conn_->set_local_sdp(local_sdp_);
        conn_->set_state_as_waiting_stun();

        if ((err = conn_->initialize(ruc_->req_, ruc_->dtls_, ruc_->srtp_, username)) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }

        SrsStreamPublishToken *token = NULL;
        if ((err = token_manager_.acquire_token(ruc_->req_, token)) != srs_success) {
            return srs_error_wrap(err, "token");
        }
        conn_->set_publish_token(SrsSharedPtr<ISrsStreamPublishToken>(token));

        if ((err = conn_->on_dtls_handshake_done()) != srs_success) {
            return srs_error_wrap(err, "dtls done");
        }
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        if (conn_->publishers_.size() != 1) {
            return srs_error_new(ERROR_RTC_NO_PUBLISHER, "publishers=%d", (int)conn_->publishers_.size());
        }
        publisher_ = dynamic_cast<SrsRtcPublishStream *>(conn_->publishers_.begin()->second);

        return err;
    }
    SrsMediaDesc *video_desc() { return &local_sdp_.media_descs_[1]; }
    SrsRtcTrackDescription *video_track_desc() { return source_->stream_desc_->video_track_descs_[0]; }
    // Encode a marked video packet on the media SSRC with the payload.
    std::string video_packet(uint16_t seq)
    {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(kPubVideoSsrc);
        pkt.header_.set_payload_type(kPubVideoPt);
        pkt.header_.set_sequence(seq);
        pkt.header_.set_timestamp(90000);
        pkt.header_.set_marker(true);

        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        raw->payload_ = (char *)payload_;
        raw->nn_payload_ = sizeof(payload_);
        pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

        char buf[kRtpPacketSize];
        SrsBuffer stream(buf, sizeof(buf));
        srs_error_t err = pkt.encode(&stream);
        srs_freep(err);
        return std::string(buf, stream.pos());
    }
    // Wrap the video packet of seq as the publisher's RTX packet with the RTX sequence rtx_seq, built the way SRS sends
    // one: the header on the RTX SSRC, payload type and sequence, and the OSN in front of the payload.
    std::string rtx_packet(uint16_t seq, uint16_t rtx_seq)
    {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(kPubRtxSsrc);
        pkt.header_.set_payload_type(kPubRtxPt);
        pkt.header_.set_sequence(rtx_seq);
        pkt.header_.set_timestamp(90000);
        pkt.header_.set_marker(true);

        // The RTX payload owns the raw payload, which references the media bytes.
        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        raw->payload_ = (char *)payload_;
        raw->nn_payload_ = sizeof(payload_);
        SrsRtpRtxPayload *rtx = new SrsRtpRtxPayload();
        rtx->osn_ = seq;
        rtx->payload_ = raw;
        pkt.set_payload(rtx, SrsRtpPacketPayloadTypeUnknown);

        char buf[kRtpPacketSize];
        SrsBuffer stream(buf, sizeof(buf));
        srs_error_t err = pkt.encode(&stream);
        srs_freep(err);
        return std::string(buf, stream.pos());
    }
    // A padding-only RTX probe: the RTX header with the padding bit, then three padding bytes and no OSN.
    std::string rtx_padding_packet(uint16_t rtx_seq)
    {
        uint8_t pkt[] = {0xA0, kPubRtxPt, (uint8_t)(rtx_seq >> 8), (uint8_t)rtx_seq, 0x00, 0x01, 0x5F, 0x90,
                         (uint8_t)(kPubRtxSsrc >> 24), (uint8_t)(kPubRtxSsrc >> 16), (uint8_t)(kPubRtxSsrc >> 8), (uint8_t)kPubRtxSsrc,
                         0x00, 0x00, 0x03};
        return std::string((char *)pkt, sizeof(pkt));
    }
    // The RTX packet of seq with every header shape a real sender may use: one CSRC, a one-byte extension SRS does not
    // know, the marker bit, and three padding bytes after the payload.
    std::string rtx_packet_with_csrc_extension_padding(uint16_t seq, uint16_t rtx_seq)
    {
        uint8_t pkt[] = {0xB1, (uint8_t)(0x80 | kPubRtxPt), (uint8_t)(rtx_seq >> 8), (uint8_t)rtx_seq, 0x00, 0x01, 0x5F, 0x90,
                         (uint8_t)(kPubRtxSsrc >> 24), (uint8_t)(kPubRtxSsrc >> 16), (uint8_t)(kPubRtxSsrc >> 8), (uint8_t)kPubRtxSsrc,
                         0x11, 0x22, 0x33, 0x44, // The CSRC.
                         0xBE, 0xDE, 0x00, 0x01, // One-byte extensions, one word: id 5 with two bytes, then a pad byte.
                         0x51, 0xAB, 0xCD, 0x00,
                         (uint8_t)(seq >> 8), (uint8_t)seq, payload_[0], payload_[1], payload_[2], payload_[3],
                         0x00, 0x00, 0x03};
        return std::string((char *)pkt, sizeof(pkt));
    }
    srs_error_t feed(const std::string &bytes)
    {
        srs_error_t err = srs_success;
        std::string copy = bytes;
        bool dropped = false;
        if ((err = conn_->on_rtp_cipher((char *)copy.data(), (int)copy.size(), &dropped)) != srs_success) {
            return srs_error_wrap(err, "cipher");
        }
        if (dropped) {
            return err;
        }
        return conn_->on_rtp_plaintext((char *)copy.data(), (int)copy.size());
    }
    // Feed the packet through the UDP network with the plaintext transport, the path a real publisher's packet takes.
    srs_error_t feed_udp(const std::string &bytes)
    {
        std::string copy = bytes;
        return conn_->udp()->on_rtp((char *)copy.data(), (int)copy.size());
    }
};

// Assert today's plain publish answer: one video payload, two SSRCs registered, and a plain retransmission of a media
// packet reaches the source as the media packet it is.
static void expect_plain_publisher(MockRtcPublisherRtxScenario &s)
{
    srs_error_t err;

    EXPECT_EQ(2, (int)s.conn_->publishers_ssrc_map_.size());
    ASSERT_EQ(2, (int)s.local_sdp_.media_descs_.size());
    SrsMediaDesc *video = s.video_desc();
    ASSERT_EQ(1, (int)video->payload_types_.size());
    EXPECT_STREQ("H264", video->payload_types_[0].encoding_name_.c_str());
    EXPECT_TRUE(s.video_track_desc()->rtx_ == NULL);
    EXPECT_EQ(0u, s.video_track_desc()->rtx_ssrc_);

    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    EXPECT_EQ(2, s.source_->rtp_video_count_);
    EXPECT_EQ(kPubVideoSsrc, s.source_->last_rtp_ssrc_);
    EXPECT_EQ(100, s.source_->last_rtp_seq_);
    EXPECT_EQ((int)kPubVideoPt, (int)s.source_->last_rtp_pt_);
}

// With nack_prefer_rtx on and a publisher offer carrying rtx for the negotiated codec and a FID group, the publish
// answer advertises the rtx payload with apt and no SSRC lines, the RTX SSRC routes to the publisher, an RTX packet
// is unwrapped into the original before the media path, and a padding-only RTX probe is dropped before it.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherRtxOfferNegotiatesAndUnwraps)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));

    // Audio, video and the video RTX SSRC all route to the publisher.
    EXPECT_EQ(3, (int)s.conn_->publishers_ssrc_map_.size());
    EXPECT_TRUE(s.conn_->publishers_ssrc_map_.find(kPubRtxSsrc) != s.conn_->publishers_ssrc_map_.end());
    EXPECT_EQ(kPubRtxSsrc, s.video_track_desc()->rtx_ssrc_);

    SrsMediaDesc *video = s.video_desc();
    ASSERT_EQ(2, (int)video->payload_types_.size());
    EXPECT_EQ((int)kPubVideoPt, video->payload_types_[0].payload_type_);
    EXPECT_EQ((int)kPubRtxPt, video->payload_types_[1].payload_type_);
    EXPECT_STREQ("rtx", video->payload_types_[1].encoding_name_.c_str());
    EXPECT_EQ(90000, video->payload_types_[1].clock_rate_);
    EXPECT_STREQ("apt=96", video->payload_types_[1].format_specific_param_.c_str());
    EXPECT_TRUE(video->recvonly_);
    EXPECT_TRUE(video->ssrc_groups_.empty());
    EXPECT_TRUE(video->ssrc_infos_.empty());

    // A media packet, then a gap, then the RTX packet that carries the missing one: the source sees the original.
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(102)));
    EXPECT_EQ(2, s.source_->rtp_video_count_);
    ASSERT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) != NULL);
    int frames = s.publisher_->nn_video_frames_;

    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_packet(101, 7)));
    EXPECT_EQ(3, s.source_->rtp_video_count_);
    EXPECT_EQ(kPubVideoSsrc, s.source_->last_rtp_ssrc_);
    EXPECT_EQ(101, s.source_->last_rtp_seq_);
    EXPECT_EQ((int)kPubVideoPt, (int)s.source_->last_rtp_pt_);
    // The recovered packet fills the gap in the NACK requester and counts as media exactly once.
    EXPECT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) == NULL);
    EXPECT_EQ(frames + 1, s.publisher_->nn_video_frames_);

    // A padding-only RTX probe never reaches the source and is counted as padding.
    int64_t padding = _srs_pps_rrtx_padding->sugar_;
    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_padding_packet(8)));
    EXPECT_EQ(3, s.source_->rtp_video_count_);
    EXPECT_EQ(padding + 1, _srs_pps_rrtx_padding->sugar_);

    // Anything on the RTX SSRC without an OSN followed by media is a probe as well, counted as padding and dropped
    // without an error: a bare header, which the existing frame statistics test feeds as auxiliary traffic, and an OSN
    // with nothing behind it.
    if (true) {
        std::string header_only = s.rtx_padding_packet(9).substr(0, 12);
        header_only[0] = (char)0x80;
        HELPER_ASSERT_SUCCESS(s.feed(header_only));
        EXPECT_EQ(3, s.source_->rtp_video_count_);
        EXPECT_EQ(padding + 2, _srs_pps_rrtx_padding->sugar_);

        HELPER_ASSERT_SUCCESS(s.feed(s.rtx_packet(103, 10).substr(0, 14)));
        EXPECT_EQ(3, s.source_->rtp_video_count_);
        EXPECT_EQ(padding + 3, _srs_pps_rrtx_padding->sugar_);
    }

    // A sender report for the RTX SSRC is ignored: the media track's sender report time is untouched, plan D9.
    SrsRtcpSR sr;
    sr.set_ssrc(kPubRtxSsrc);
    sr.set_ntp(0x0102030405060708ULL);
    sr.set_rtp_ts(4321);
    HELPER_ASSERT_SUCCESS(s.publisher_->on_rtcp(&sr));
    EXPECT_EQ(0, (int)s.publisher_->video_tracks_[0]->last_sender_report_rtp_time_);
}

// The unwrap does not depend on the header shape: an RTX packet with a CSRC, a one-byte extension SRS does not know,
// the marker bit and padding restores the original with all of them, because the RTP decoder parses the header once and
// only the SSRC, payload type and sequence are rewritten. This locks in accepted behavior across the move of the unwrap
// from the kernel byte helpers into the decode hook, so it passes before and after that change.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherRtxUnwrapsAnyHeaderShape)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));

    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(102)));
    ASSERT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) != NULL);

    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_packet_with_csrc_extension_padding(101, 7)));
    EXPECT_EQ(3, s.source_->rtp_video_count_);
    EXPECT_EQ(kPubVideoSsrc, s.source_->last_rtp_ssrc_);
    EXPECT_EQ(101, s.source_->last_rtp_seq_);
    EXPECT_EQ((int)kPubVideoPt, (int)s.source_->last_rtp_pt_);
    EXPECT_EQ(90000u, s.source_->last_rtp_timestamp_);
    EXPECT_TRUE(s.source_->last_rtp_marker_);
    EXPECT_EQ(3, (int)s.source_->last_rtp_padding_);
    EXPECT_EQ(std::string((char *)s.payload_, sizeof(s.payload_)), s.source_->last_rtp_payload_);
    EXPECT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) == NULL);
}

// An RTX packet resolves to its video track through a fast cache of its own, kept apart from the media cache: has_ssrc
// matches the RTX SSRC too, so the scan finds the video track, and the RTX SSRC then holds an RTX slot pointing at
// that track while the media slots stay for media SSRCs, which are many times more frequent. Telling RTX from media
// costs no comparison of the caller's own: the lookup reports it. A padding-only probe resolves through the same RTX
// slot and is still counted and dropped.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherRtxResolvesThroughRtxCache)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));
    SrsRtcRecvTrack *video_track = s.publisher_->video_tracks_[0];

    // The first media packet takes the first media slot.
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(102)));
    EXPECT_EQ(kPubVideoSsrc, s.publisher_->cache_ssrc0_);
    EXPECT_FALSE(s.publisher_->cache_is_audio0_);
    EXPECT_TRUE(s.publisher_->cache_track0_ == video_track);
    EXPECT_EQ(0u, s.publisher_->cache_rtx_ssrc0_);

    // The RTX packet takes the first RTX slot as the video track, leaves the media slots alone, and the source still
    // sees the original.
    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_packet(101, 7)));
    EXPECT_EQ(kPubRtxSsrc, s.publisher_->cache_rtx_ssrc0_);
    EXPECT_TRUE(s.publisher_->cache_rtx_track0_ == video_track);
    EXPECT_EQ(0u, s.publisher_->cache_ssrc1_);
    EXPECT_EQ(3, s.source_->rtp_video_count_);
    EXPECT_EQ(101, s.source_->last_rtp_seq_);

    // A probe on the RTX SSRC hits that slot: nothing new is cached, it is counted as padding and reaches nothing.
    int64_t padding = _srs_pps_rrtx_padding->sugar_;
    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_padding_packet(8)));
    EXPECT_EQ(kPubRtxSsrc, s.publisher_->cache_rtx_ssrc0_);
    EXPECT_EQ(0u, s.publisher_->cache_rtx_ssrc1_);
    EXPECT_EQ(0u, s.publisher_->cache_ssrc1_);
    EXPECT_EQ(padding + 1, _srs_pps_rrtx_padding->sugar_);
    EXPECT_EQ(3, s.source_->rtp_video_count_);
}

// A publisher that offers no rtx gets today's answer although the server prefers RTX. Passes from the start and locks
// in the accepted fallback.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherWithoutRtxOfferKeepsPlainAnswer)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    s.sdp_.video_ssrc_ = kPubVideoSsrc;
    s.sdp_.video_pt_ = kPubVideoPt;
    HELPER_ASSERT_SUCCESS(s.publish(s.sdp_.create_chrome_publisher_offer_with_h264(), true));
    expect_plain_publisher(s);
}

// An rtx offer without a FID group cannot tell SRS the RTX SSRC, so the answer stays plain, plan D2. The answer is
// already plain today, but the track keeps a stray rtx payload from the inert plumbing, so this is red until Green.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherRtxOfferWithoutFidKeepsPlainAnswer)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, false), true));
    expect_plain_publisher(s);
}

// An RTX-capable offer against nack_prefer_rtx off gets today's answer; the preference is only a preference, plan D1.
// The answer is already plain today, but the track keeps a stray rtx payload, so this is red until Green.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherRtxOfferWithPreferOffKeepsPlainAnswer)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), false));
    expect_plain_publisher(s);
}

// The RTP receive and RTCP send paths consult the blackhole, which only the server creates; lend one while the test
// feeds the network or sends a NACK.
class MockBlackholeForNack
{
public:
    SrsRtcBlackhole *previous_;
    SrsRtcBlackhole blackhole_;

public:
    MockBlackholeForNack()
    {
        previous_ = _srs_blackhole;
        if (!previous_) {
            _srs_blackhole = &blackhole_;
        }
    }
    virtual ~MockBlackholeForNack()
    {
        _srs_blackhole = previous_;
    }
};

// The NACK simulator drops a received packet before it is decrypted, so the retransmission SRTP later sees is not a
// replay of it; the network layer must then neither decrypt nor deliver the dropped packet. Feeds the packets through
// the UDP network, as a real publisher's packets arrive, so the drop is observed where it has to take effect.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherNackSimulatorDropsBeforeDecrypt)
{
    srs_error_t err;

    MockBlackholeForNack blackhole;
    MockRtcPublisherRtxScenario s;
    s.ruc_->dtls_ = false;
    s.ruc_->srtp_ = false;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));

    HELPER_ASSERT_SUCCESS(s.feed_udp(s.video_packet(100)));
    EXPECT_EQ(1, s.source_->rtp_video_count_);

    // The dropped packet never reaches the source, so the next one opens the gap that SRS then requests.
    s.conn_->simulate_nack_drop(1);
    HELPER_ASSERT_SUCCESS(s.feed_udp(s.video_packet(101)));
    EXPECT_EQ(1, s.source_->rtp_video_count_);
    EXPECT_EQ(100, s.source_->last_rtp_seq_);

    HELPER_ASSERT_SUCCESS(s.feed_udp(s.video_packet(102)));
    EXPECT_EQ(2, s.source_->rtp_video_count_);
    EXPECT_EQ(102, s.source_->last_rtp_seq_);
    EXPECT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) != NULL);

    // Only the packets asked for are dropped.
    HELPER_ASSERT_SUCCESS(s.feed_udp(s.video_packet(103)));
    EXPECT_EQ(3, s.source_->rtp_video_count_);
}

// The cipher hook reports the packet it drops, for the NACK simulator and for a payload type configured to drop, so
// the network layer can stop before decrypting it; the connection forwards the report from the publisher it finds.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherCipherHookReportsDrop)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));

    std::string pkt = s.video_packet(100);
    bool dropped = true;
    HELPER_ASSERT_SUCCESS(s.publisher_->on_rtp_cipher((char *)pkt.data(), (int)pkt.size(), &dropped));
    EXPECT_FALSE(dropped);

    // The simulator drops exactly the packets asked for, counted through the connection that owns the publisher.
    s.conn_->simulate_nack_drop(2);
    HELPER_ASSERT_SUCCESS(s.conn_->on_rtp_cipher((char *)pkt.data(), (int)pkt.size(), &dropped));
    EXPECT_TRUE(dropped);
    HELPER_ASSERT_SUCCESS(s.publisher_->on_rtp_cipher((char *)pkt.data(), (int)pkt.size(), &dropped));
    EXPECT_TRUE(dropped);
    HELPER_ASSERT_SUCCESS(s.publisher_->on_rtp_cipher((char *)pkt.data(), (int)pkt.size(), &dropped));
    EXPECT_FALSE(dropped);

    // A payload type configured to drop is dropped, any other payload type passes.
    s.publisher_->pt_to_drop_ = kPubVideoPt;
    HELPER_ASSERT_SUCCESS(s.publisher_->on_rtp_cipher((char *)pkt.data(), (int)pkt.size(), &dropped));
    EXPECT_TRUE(dropped);
    s.publisher_->pt_to_drop_ = kPubVideoPt + 1;
    HELPER_ASSERT_SUCCESS(s.publisher_->on_rtp_cipher((char *)pkt.data(), (int)pkt.size(), &dropped));
    EXPECT_FALSE(dropped);
}

#ifdef SRS_NACK_DEBUG_LOG_ENABLED
// The simulator build logs the publish side packet by packet, so RTX is verified from the log alone: the NACK SRS
// requests, with the media SSRC and the sequences; the RTX packet that answers it, with the RTX SSRC, payload type
// and sequence, the OSN and the media it restores; the recovered sequence on the media SSRC; and a padding-only RTX
// probe as dropped.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherRtxLogsDetail)
{
    srs_error_t err;

    MockBlackholeForNack blackhole;
    MockRtcPublisherRtxScenario s;
    HELPER_ASSERT_SUCCESS(s.publish(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));

    MockLogForNack log;
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(102)));
    ASSERT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) != NULL);

    // The gap is requested once the first NACK interval has passed; the request reads the cached clock.
    srs_usleep(30 * SRS_UTIME_MILLISECONDS);
    srs_time_now_realtime();
    HELPER_ASSERT_SUCCESS(s.publisher_->check_send_nacks());
    char expected[256];
    snprintf(expected, sizeof(expected), "NACK: request ssrc=%u, seqs=[101]", kPubVideoSsrc);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK:");

    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_packet(101, 7)));
    snprintf(expected, sizeof(expected), "NACK: RTX recv ssrc=%u, pt=%u, seq=7, osn=101, media ssrc=%u, pt=%u",
             kPubRtxSsrc, (uint32_t)kPubRtxPt, kPubVideoSsrc, (uint32_t)kPubVideoPt);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: RTX");
    snprintf(expected, sizeof(expected), "NACK: recovered seq=101, ssrc=%u, pt=%u", kPubVideoSsrc, (uint32_t)kPubVideoPt);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: recovered");

    HELPER_ASSERT_SUCCESS(s.feed(s.rtx_padding_packet(8)));
    snprintf(expected, sizeof(expected), "NACK: RTX padding ssrc=%u, pt=%u, seq=8", kPubRtxSsrc, (uint32_t)kPubRtxPt);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK: RTX padding");
    EXPECT_EQ(1, log.count("NACK: recovered"));
}

// A plain retransmission from a publisher without rtx is logged only as the recovered sequence on the media SSRC,
// with no RTX line before it; the two logs together tell plain from RTX.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublisherPlainLogsDetail)
{
    srs_error_t err;

    MockRtcPublisherRtxScenario s;
    s.sdp_.video_ssrc_ = kPubVideoSsrc;
    s.sdp_.video_pt_ = kPubVideoPt;
    HELPER_ASSERT_SUCCESS(s.publish(s.sdp_.create_chrome_publisher_offer_with_h264(), true));

    MockLogForNack log;
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(100)));
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(102)));
    ASSERT_TRUE(s.publisher_->video_tracks_[0]->nack_receiver_->find(101) != NULL);
    HELPER_ASSERT_SUCCESS(s.feed(s.video_packet(101)));
    EXPECT_EQ(3, s.source_->rtp_video_count_);

    char expected[256];
    snprintf(expected, sizeof(expected), "NACK: recovered seq=101, ssrc=%u, pt=%u", kPubVideoSsrc, (uint32_t)kPubVideoPt);
    EXPECT_EQ(1, log.count(expected)) << log.find("NACK:");
    EXPECT_EQ(0, log.count("NACK: RTX"));
}
#endif

// Drive SrsRtcPublisherNegotiator::negotiate_publish_capability directly with a mock config. The track descriptions
// it produces are inactive, so the FID association must not depend on an active track, plan D13.
class MockRtcPublisherRtxNegotiation
{
public:
    MockAppConfig config_;
    MockSdpFactory sdp_;
    SrsRtcPublisherNegotiator negotiator_;
    SrsRtcSourceDescription stream_desc_;

public:
    MockRtcPublisherRtxNegotiation()
    {
        negotiator_.config_ = &config_;
    }
    virtual ~MockRtcPublisherRtxNegotiation()
    {
    }

public:
    srs_error_t negotiate(const std::string &offer, bool prefer_rtx)
    {
        srs_error_t err = srs_success;

        config_.set_rtc_nack_prefer_rtx(prefer_rtx);

        SrsRtcUserConfig ruc;
        srs_freep(ruc.req_);
        ruc.req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
        ruc.publish_ = true;
        ruc.dtls_ = true;
        ruc.srtp_ = true;

        if ((err = ruc.remote_sdp_.parse(offer)) != srs_success) {
            return srs_error_wrap(err, "parse offer");
        }

        return negotiator_.negotiate_publish_capability(&ruc, &stream_desc_);
    }
    SrsRtcTrackDescription *video_track()
    {
        return stream_desc_.video_track_descs_.empty() ? NULL : stream_desc_.video_track_descs_[0];
    }
};

// The negotiator selects the rtx payload by apt, carries the offered clock rate, and stores the FID group's second
// SSRC as the RTX SSRC while the track is still inactive.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublishNegotiatorSelectsRtxAndFid)
{
    srs_error_t err;

    MockRtcPublisherRtxNegotiation s;
    HELPER_ASSERT_SUCCESS(s.negotiate(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), true));

    SrsRtcTrackDescription *track = s.video_track();
    ASSERT_TRUE(track != NULL);
    EXPECT_FALSE(track->is_active_);
    EXPECT_EQ(kPubVideoSsrc, track->ssrc_);
    EXPECT_EQ(kPubRtxSsrc, track->rtx_ssrc_);

    SrsRtxPayloadDes *rtx = dynamic_cast<SrsRtxPayloadDes *>(track->rtx_);
    ASSERT_TRUE(rtx != NULL);
    EXPECT_EQ((int)kPubRtxPt, (int)rtx->pt_);
    EXPECT_EQ((int)kPubVideoPt, (int)rtx->apt_);
    EXPECT_EQ(90000, rtx->sample_);
}

// Without a usable rtx offer the video track carries no RTX: no FID group, the preference off, or every apt naming
// another payload. Today's answer is already plain for each, plan D1 and D2, but the inert plumbing stores the first
// rtx payload on the track regardless, so these are red until Green.
VOID TEST(BasicWorkflowRtcConnTest, WorkflowRtcPublishNegotiatorKeepsPlainWithoutUsableRtx)
{
    srs_error_t err;

    if (true) {
        MockRtcPublisherRtxNegotiation s;
        HELPER_ASSERT_SUCCESS(s.negotiate(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, false), true));
        SrsRtcTrackDescription *track = s.video_track();
        ASSERT_TRUE(track != NULL);
        EXPECT_TRUE(track->rtx_ == NULL);
        EXPECT_EQ(0u, track->rtx_ssrc_);
    }

    if (true) {
        MockRtcPublisherRtxNegotiation s;
        HELPER_ASSERT_SUCCESS(s.negotiate(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, kPubVideoPt, true), false));
        SrsRtcTrackDescription *track = s.video_track();
        ASSERT_TRUE(track != NULL);
        EXPECT_TRUE(track->rtx_ == NULL);
        EXPECT_EQ(0u, track->rtx_ssrc_);
    }

    if (true) {
        MockRtcPublisherRtxNegotiation s;
        HELPER_ASSERT_SUCCESS(s.negotiate(mock_publisher_offer_with_rtx(kPubVideoPt, kPubRtxPt, 97, true), true));
        SrsRtcTrackDescription *track = s.video_track();
        ASSERT_TRUE(track != NULL);
        EXPECT_TRUE(track->rtx_ == NULL);
        EXPECT_EQ(0u, track->rtx_ssrc_);
    }
}
