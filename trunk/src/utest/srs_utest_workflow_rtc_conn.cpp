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

#include <srs_utest_workflow_rtc_conn.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtp.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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

        HELPER_EXPECT_SUCCESS(conn->on_rtp_cipher(data.get(), buf.pos()));
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
