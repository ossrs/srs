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
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

MockProtocolUtilityForRtcConn::MockProtocolUtilityForRtcConn(std::string ip)
{
    mock_ip_ = ip;
}

MockProtocolUtilityForRtcConn::~MockProtocolUtilityForRtcConn()
{
}

std::vector<SrsIPAddress *> &MockProtocolUtilityForRtcConn::local_ips()
{
    if (!ips_.empty()) {
        return ips_;
    }

    SrsIPAddress *addr = new SrsIPAddress();
    addr->ip_ = mock_ip_;
    addr->is_ipv4_ = true;
    addr->is_loopback_ = false; // Not loopback
    addr->is_internet_ = true;  // Public IP
    addr->ifname_ = "eth0";     // Interface name
    ips_.push_back(addr);

    return ips_;
}

MockAppFactoryForRtcConn::MockAppFactoryForRtcConn()
{
    mock_protocol_utility_ = NULL;
}

MockAppFactoryForRtcConn::~MockAppFactoryForRtcConn()
{
}

ISrsProtocolUtility *MockAppFactoryForRtcConn::create_protocol_utility()
{
    return mock_protocol_utility_;
}

ISrsRtcPublishStream *MockAppFactoryForRtcConn::create_rtc_publish_stream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketReceiver *receiver, const SrsContextId &cid)
{
    SrsRtcPublishStream *publisher = new SrsRtcPublishStream(exec, expire, receiver, cid);
    publisher->rtc_sources_ = rtc_sources_;
    return publisher;
}

MockRtcSourceForRtcConn::MockRtcSourceForRtcConn()
{
    rtp_audio_count_ = 0;
    rtp_video_count_ = 0;
}

MockRtcSourceForRtcConn::~MockRtcSourceForRtcConn()
{
}

srs_error_t MockRtcSourceForRtcConn::on_rtp(SrsRtpPacket *pkt)
{
    if (pkt->frame_type_ == SrsFrameTypeAudio) {
        rtp_audio_count_++;
    } else if (pkt->frame_type_ == SrsFrameTypeVideo) {
        rtp_video_count_++;
    }
    return srs_success;
}

// This test is used to verify the basic workflow of the RTC connection.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcConnTest, ManuallyVerifyForPlayer)
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
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtilityForRtcConn("192.168.1.100");
    MockRtcSourceForRtcConn *mock_rtc_source = new MockRtcSourceForRtcConn();
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

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_player_offer();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
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
VOID TEST(BasicWorkflowRtcConnTest, ManuallyVerifyForPublisher)
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
    mock_app_factory->mock_protocol_utility_ = new MockProtocolUtilityForRtcConn("192.168.1.100");
    MockRtcSourceForRtcConn *mock_rtc_source = new MockRtcSourceForRtcConn();
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

        ruc->remote_sdp_str_ = mock_sdp_factory->create_chrome_publisher_offer();
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
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
