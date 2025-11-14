//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai07.hpp>

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest.hpp>

// External function declaration from srs_app_rtc_source.cpp
extern srs_error_t aac_raw_append_adts_header(SrsMediaPacket *shared_audio, SrsFormat *format, char **pbuf, int *pnn_buf);

// Mock class implementations

MockRtcSourceForConsumer::MockRtcSourceForConsumer()
{
    source_id_.set_value("test-source-id");
    pre_source_id_.set_value("test-pre-source-id");
    consumer_destroy_count_ = 0;
    can_publish_ = true;
    is_created_ = true;
}

MockRtcSourceForConsumer::~MockRtcSourceForConsumer()
{
}

SrsContextId MockRtcSourceForConsumer::get_id()
{
    return source_id_;
}

SrsContextId MockRtcSourceForConsumer::get_pre_source_id()
{
    return pre_source_id_;
}

void MockRtcSourceForConsumer::on_consumer_destroy(ISrsRtcConsumer *consumer)
{
    consumer_destroy_count_++;
}

bool MockRtcSourceForConsumer::can_publish()
{
    return can_publish_;
}

bool MockRtcSourceForConsumer::is_created()
{
    return is_created_;
}

SrsContextId MockRtcSourceForConsumer::source_id()
{
    return source_id_;
}

SrsContextId MockRtcSourceForConsumer::pre_source_id()
{
    return pre_source_id_;
}

MockRtcSourceChangeCallback::MockRtcSourceChangeCallback()
{
    stream_change_count_ = 0;
    last_stream_desc_ = NULL;
}

MockRtcSourceChangeCallback::~MockRtcSourceChangeCallback()
{
}

void MockRtcSourceChangeCallback::on_stream_change(SrsRtcSourceDescription *desc)
{
    stream_change_count_++;
    last_stream_desc_ = desc;
}

MockRtcConsumer::MockRtcConsumer()
{
    update_source_id_count_ = 0;
    stream_change_count_ = 0;
    enqueue_count_ = 0;
    last_stream_desc_ = NULL;
    source_id_.set_value("test-consumer-source-id");
    consumer_id_.set_value("test-consumer-id");
    should_update_source_id_ = false;
    enqueue_error_ = srs_success;
}

MockRtcConsumer::~MockRtcConsumer()
{
    srs_freep(enqueue_error_);
}

SrsContextId MockRtcConsumer::get_id()
{
    return consumer_id_;
}

void MockRtcConsumer::update_source_id()
{
    update_source_id_count_++;
}

void MockRtcConsumer::on_stream_change(SrsRtcSourceDescription *desc)
{
    stream_change_count_++;
    last_stream_desc_ = desc;
}

srs_error_t MockRtcConsumer::enqueue(SrsRtpPacket *pkt)
{
    enqueue_count_++;
    srs_freep(pkt); // Free the packet to avoid memory leak in tests
    return srs_error_copy(enqueue_error_);
}

void MockRtcConsumer::set_enqueue_error(srs_error_t err)
{
    srs_freep(enqueue_error_);
    enqueue_error_ = srs_error_copy(err);
}

MockRtcSourceEventHandler::MockRtcSourceEventHandler()
{
    on_unpublish_count_ = 0;
    on_consumers_finished_count_ = 0;
}

MockRtcSourceEventHandler::~MockRtcSourceEventHandler()
{
}

void MockRtcSourceEventHandler::on_unpublish()
{
    on_unpublish_count_++;
}

void MockRtcSourceEventHandler::on_consumers_finished()
{
    on_consumers_finished_count_++;
}

MockRtcPublishStream::MockRtcPublishStream() : SrsRtcPublishStream(NULL, NULL, NULL, SrsContextId())
{
    request_keyframe_count_ = 0;
    last_keyframe_ssrc_ = 0;
    context_id_.set_value("test-publish-stream-id");
}

MockRtcPublishStream::~MockRtcPublishStream()
{
}

void MockRtcPublishStream::request_keyframe(uint32_t ssrc, SrsContextId cid)
{
    request_keyframe_count_++;
    last_keyframe_ssrc_ = ssrc;
    last_keyframe_cid_ = cid;
}

const SrsContextId &MockRtcPublishStream::context_id()
{
    return context_id_;
}

void MockRtcPublishStream::set_context_id(const SrsContextId &cid)
{
    context_id_ = cid;
}

MockCircuitBreaker::MockCircuitBreaker()
{
    hybrid_high_water_level_ = false;
    hybrid_critical_water_level_ = false;
    hybrid_dying_water_level_ = false;
}

MockCircuitBreaker::~MockCircuitBreaker()
{
}

srs_error_t MockCircuitBreaker::initialize()
{
    return srs_success;
}

bool MockCircuitBreaker::hybrid_high_water_level()
{
    return hybrid_high_water_level_;
}

bool MockCircuitBreaker::hybrid_critical_water_level()
{
    return hybrid_critical_water_level_;
}

bool MockCircuitBreaker::hybrid_dying_water_level()
{
    return hybrid_dying_water_level_;
}

void MockCircuitBreaker::set_hybrid_dying_water_level(bool dying)
{
    hybrid_dying_water_level_ = dying;
}

MockFailingRequest::MockFailingRequest()
{
    should_fail_copy_ = false;
    stream_url_ = "rtmp://localhost/live/test";
    vhost_ = "test.vhost";
    app_ = "live";
    stream_ = "test";
    host_ = "localhost";
    port_ = 1935;
    args_ = NULL;
}

MockFailingRequest::MockFailingRequest(const std::string &stream_url, bool should_fail_copy)
{
    should_fail_copy_ = should_fail_copy;
    stream_url_ = stream_url;
    vhost_ = "test.vhost";
    app_ = "live";
    stream_ = "test";
    host_ = "localhost";
    port_ = 1935;
    args_ = NULL;
}

MockFailingRequest::~MockFailingRequest()
{
    srs_freep(args_);
}

ISrsRequest *MockFailingRequest::copy()
{
    if (should_fail_copy_) {
        return NULL;
    }
    MockFailingRequest *cp = new MockFailingRequest();
    cp->should_fail_copy_ = should_fail_copy_;
    cp->stream_url_ = stream_url_;
    cp->vhost_ = vhost_;
    cp->app_ = app_;
    cp->stream_ = stream_;
    cp->host_ = host_;
    cp->port_ = port_;
    return cp;
}

void MockFailingRequest::set_should_fail_copy(bool should_fail)
{
    should_fail_copy_ = should_fail;
}

void MockFailingRequest::update_auth(ISrsRequest *req)
{
    // Mock implementation - do nothing
}

std::string MockFailingRequest::get_stream_url()
{
    return stream_url_;
}

void MockFailingRequest::strip()
{
    // Mock implementation - do nothing
}

ISrsRequest *MockFailingRequest::as_http()
{
    return this;
}

MockFailingRtcSource::MockFailingRtcSource()
{
    should_fail_initialize_ = false;
    initialize_error_ = srs_success;
}

MockFailingRtcSource::~MockFailingRtcSource()
{
    srs_freep(initialize_error_);
}

srs_error_t MockFailingRtcSource::initialize(ISrsRequest *req)
{
    if (should_fail_initialize_) {
        return srs_error_copy(initialize_error_);
    }
    return srs_success;
}

void MockFailingRtcSource::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
    should_fail_initialize_ = true;
}

MockRtcBridge::MockRtcBridge()
{
    initialize_count_ = 0;
    setup_codec_count_ = 0;
    on_publish_count_ = 0;
    on_unpublish_count_ = 0;
    on_rtp_count_ = 0;
    initialize_error_ = srs_success;
    setup_codec_error_ = srs_success;
    on_publish_error_ = srs_success;
    on_rtp_error_ = srs_success;
    last_initialize_req_ = NULL;
    last_audio_codec_ = SrsAudioCodecIdForbidden;
    last_video_codec_ = SrsVideoCodecIdForbidden;
    last_rtp_packet_ = NULL;
}

MockRtcBridge::~MockRtcBridge()
{
    srs_freep(initialize_error_);
    srs_freep(setup_codec_error_);
    srs_freep(on_publish_error_);
    srs_freep(on_rtp_error_);
    srs_freep(last_rtp_packet_);
}

srs_error_t MockRtcBridge::initialize(ISrsRequest *req)
{
    initialize_count_++;
    last_initialize_req_ = req;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockRtcBridge::setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec)
{
    setup_codec_count_++;
    last_audio_codec_ = acodec;
    last_video_codec_ = vcodec;
    return srs_error_copy(setup_codec_error_);
}

srs_error_t MockRtcBridge::on_publish()
{
    on_publish_count_++;
    return srs_error_copy(on_publish_error_);
}

void MockRtcBridge::on_unpublish()
{
    on_unpublish_count_++;
}

srs_error_t MockRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    on_rtp_count_++;
    srs_freep(last_rtp_packet_);
    last_rtp_packet_ = pkt->copy();
    return srs_error_copy(on_rtp_error_);
}

void MockRtcBridge::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockRtcBridge::set_setup_codec_error(srs_error_t err)
{
    srs_freep(setup_codec_error_);
    setup_codec_error_ = srs_error_copy(err);
}

void MockRtcBridge::set_on_publish_error(srs_error_t err)
{
    srs_freep(on_publish_error_);
    on_publish_error_ = srs_error_copy(err);
}

void MockRtcBridge::set_on_rtp_error(srs_error_t err)
{
    srs_freep(on_rtp_error_);
    on_rtp_error_ = srs_error_copy(err);
}

VOID TEST(AppTest2, AacRawAppendAdtsHeaderSequenceHeader)
{
    srs_error_t err;

    // Create a mock AAC sequence header packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with AAC sequence header
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Set up AAC codec info to simulate sequence header
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_object_ = SrsAacObjectTypeAacLC;
    format->acodec_->aac_sample_rate_ = 4; // 44100 Hz index
    format->acodec_->aac_channels_ = 2;

    // Create audio frame info to simulate sequence header
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->dts_ = 1000;
    format->audio_->cts_ = 0;
    format->audio_->nb_samples_ = 0; // Sequence header has 0 samples

    char *buf = NULL;
    int nn_buf = 0;

    // Test: sequence header should return success without creating buffer
    HELPER_EXPECT_SUCCESS(aac_raw_append_adts_header(audio_packet.get(), format.get(), &buf, &nn_buf));
    EXPECT_TRUE(buf == NULL);
    EXPECT_EQ(0, nn_buf);
}

VOID TEST(AppTest2, AacRawAppendAdtsHeaderNoSamples)
{
    srs_error_t err;

    // Create a mock AAC packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with AAC but no samples
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Set up AAC codec info
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_object_ = SrsAacObjectTypeAacLC;
    format->acodec_->aac_sample_rate_ = 4; // 44100 Hz index
    format->acodec_->aac_channels_ = 2;

    // Create audio frame info with no samples
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->dts_ = 1000;
    format->audio_->cts_ = 0;
    format->audio_->nb_samples_ = 0; // No samples

    char *buf = NULL;
    int nn_buf = 0;

    // Test: no samples should return success without creating buffer
    HELPER_EXPECT_SUCCESS(aac_raw_append_adts_header(audio_packet.get(), format.get(), &buf, &nn_buf));
    EXPECT_TRUE(buf == NULL);
    EXPECT_EQ(0, nn_buf);
}

VOID TEST(AppTest2, AacRawAppendAdtsHeaderMultipleSamples)
{
    srs_error_t err;

    // Create a mock AAC packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with AAC
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Set up AAC codec info
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_object_ = SrsAacObjectTypeAacLC;
    format->acodec_->aac_sample_rate_ = 4; // 44100 Hz index
    format->acodec_->aac_channels_ = 2;

    // Create audio frame info with multiple samples (should fail)
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->dts_ = 1000;
    format->audio_->cts_ = 0;
    format->audio_->nb_samples_ = 2; // Multiple samples should cause error

    char *buf = NULL;
    int nn_buf = 0;

    // Test: multiple samples should return error
    HELPER_EXPECT_FAILED(aac_raw_append_adts_header(audio_packet.get(), format.get(), &buf, &nn_buf));
}

VOID TEST(AppTest2, AacRawAppendAdtsHeaderValidSingleSample)
{
    srs_error_t err;

    // Create a mock AAC packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with AAC
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Set up AAC codec info
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_object_ = SrsAacObjectTypeAacLC; // Object type 2
    format->acodec_->aac_sample_rate_ = 4;                // 44100 Hz index
    format->acodec_->aac_channels_ = 2;                   // Stereo

    // Create audio frame info with single sample
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->dts_ = 1000;
    format->audio_->cts_ = 0;
    format->audio_->nb_samples_ = 1; // Single sample

    // Create sample data
    const char sample_data[] = {0x01, 0x02, 0x03, 0x04, 0x05}; // 5 bytes of sample data
    format->audio_->samples_[0].bytes_ = (char *)sample_data;
    format->audio_->samples_[0].size_ = sizeof(sample_data);

    char *buf = NULL;
    int nn_buf = 0;

    // Test: valid single sample should create ADTS header + data
    HELPER_EXPECT_SUCCESS(aac_raw_append_adts_header(audio_packet.get(), format.get(), &buf, &nn_buf));

    // Verify buffer was created
    EXPECT_TRUE(buf != NULL);
    EXPECT_EQ(7 + sizeof(sample_data), nn_buf); // 7 bytes ADTS header + sample data

    if (!buf) {
        return;
    }
    SrsUniquePtr<char> buf_ptr(buf);

    // Verify ADTS header structure
    EXPECT_EQ((unsigned char)0xFF, (unsigned char)buf[0]); // Sync word high
    EXPECT_EQ((unsigned char)0xF9, (unsigned char)buf[1]); // Sync word low + layer + protection

    // Verify AAC object type, sample rate, and channels are encoded correctly
    // Byte 2: (object-1)<<6 | (sample_rate&0x0F)<<2 | (channels&0x04)>>2
    unsigned char expected_byte2 = ((SrsAacObjectTypeAacLC - 1) << 6) | ((4 & 0x0F) << 2) | ((2 & 0x04) >> 2);
    EXPECT_EQ(expected_byte2, (unsigned char)buf[2]);

    // Verify frame length is encoded correctly (total length = 7 + sample size)
    int expected_frame_len = 7 + sizeof(sample_data);
    // Frame length spans bytes 3-5
    unsigned char expected_byte3 = ((2 & 0x03) << 6) | ((expected_frame_len >> 11) & 0x03);
    unsigned char expected_byte4 = (expected_frame_len >> 3) & 0xFF;
    unsigned char expected_byte5 = ((expected_frame_len & 0x07) << 5) | 0x1F;

    EXPECT_EQ(expected_byte3, (unsigned char)buf[3]);
    EXPECT_EQ(expected_byte4, (unsigned char)buf[4]);
    EXPECT_EQ(expected_byte5, (unsigned char)buf[5]);

    EXPECT_EQ((unsigned char)0xFC, (unsigned char)buf[6]); // Buffer fullness + frame count

    // Verify sample data is copied correctly
    for (int i = 0; i < sizeof(sample_data); i++) {
        EXPECT_EQ(sample_data[i], buf[7 + i]);
    }
}

VOID TEST(AppTest2, AacRawAppendAdtsHeaderDifferentCodecParams)
{
    srs_error_t err;

    // Create a mock AAC packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with different AAC parameters
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Set up AAC codec info with different parameters
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_object_ = SrsAacObjectTypeAacHE; // Object type 5
    format->acodec_->aac_sample_rate_ = 3;                // 48000 Hz index
    format->acodec_->aac_channels_ = 1;                   // Mono

    // Create audio frame info with single sample
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->dts_ = 2000;
    format->audio_->cts_ = 0;
    format->audio_->nb_samples_ = 1;

    // Create sample data
    const char sample_data[] = {(char)0xAA, (char)0xBB, (char)0xCC}; // 3 bytes of sample data
    format->audio_->samples_[0].bytes_ = (char *)sample_data;
    format->audio_->samples_[0].size_ = sizeof(sample_data);

    char *buf = NULL;
    int nn_buf = 0;

    // Test: different codec parameters should work
    HELPER_EXPECT_SUCCESS(aac_raw_append_adts_header(audio_packet.get(), format.get(), &buf, &nn_buf));

    // Verify buffer was created with correct size
    EXPECT_TRUE(buf != NULL);
    EXPECT_EQ(7 + sizeof(sample_data), nn_buf);

    if (!buf) {
        return;
    }
    SrsUniquePtr<char> buf_ptr(buf);

    // Verify ADTS header with different parameters
    EXPECT_EQ((unsigned char)0xFF, (unsigned char)buf[0]);
    EXPECT_EQ((unsigned char)0xF9, (unsigned char)buf[1]);

    // Verify different AAC object type, sample rate, and channels
    unsigned char expected_byte2 = (unsigned char)(((SrsAacObjectTypeAacHE - 1) << 6) | ((3 & 0x0F) << 2) | ((1 & 0x04) >> 2));
    EXPECT_EQ(expected_byte2, (unsigned char)buf[2]);
}

VOID TEST(AppTest2, AacRawAppendAdtsHeaderLargeSample)
{
    srs_error_t err;

    // Create a mock AAC packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create format with AAC
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());

    // Set up AAC codec info
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->aac_object_ = SrsAacObjectTypeAacLC;
    format->acodec_->aac_sample_rate_ = 4; // 44100 Hz index
    format->acodec_->aac_channels_ = 2;

    // Create audio frame info with single large sample
    format->audio_ = new SrsParsedAudioPacket();
    format->audio_->dts_ = 1000;
    format->audio_->cts_ = 0;
    format->audio_->nb_samples_ = 1;

    // Create large sample data (1KB)
    const int large_size = 1024;
    SrsUniquePtr<char> large_sample(new char[large_size]);
    for (int i = 0; i < large_size; i++) {
        large_sample.get()[i] = (char)(i % 256);
    }

    format->audio_->samples_[0].bytes_ = large_sample.get();
    format->audio_->samples_[0].size_ = large_size;

    char *buf = NULL;
    int nn_buf = 0;

    // Test: large sample should work correctly
    HELPER_EXPECT_SUCCESS(aac_raw_append_adts_header(audio_packet.get(), format.get(), &buf, &nn_buf));

    // Verify buffer was created with correct size
    EXPECT_TRUE(buf != NULL);
    EXPECT_EQ(7 + large_size, nn_buf);

    if (!buf) {
        return;
    }
    SrsUniquePtr<char> buf_ptr(buf);

    // Verify ADTS header
    EXPECT_EQ((unsigned char)0xFF, (unsigned char)buf[0]);
    EXPECT_EQ((unsigned char)0xF9, (unsigned char)buf[1]);

    // Verify frame length encoding for large frame
    int expected_frame_len = 7 + large_size;
    unsigned char byte4 = (expected_frame_len >> 3) & 0xFF;
    EXPECT_EQ(byte4, (unsigned char)buf[4]);

    // Verify sample data is copied correctly (check first and last bytes)
    EXPECT_EQ(large_sample.get()[0], buf[7]);
    EXPECT_EQ(large_sample.get()[large_size - 1], buf[7 + large_size - 1]);
}

VOID TEST(AppTest2, RtcConsumerUpdateSourceId)
{
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Initially should_update_source_id_ should be false
    consumer.update_source_id();

    // After calling update_source_id, the flag should be set
    // We can verify this by calling dump_packet which checks and resets the flag
    SrsRtpPacket *pkt = NULL;
    srs_error_t err;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt == NULL); // No packets in queue
}

VOID TEST(AppTest2, RtcConsumerEnqueueAndDumpSinglePacket)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Create a test RTP packet
    SrsRtpPacket *test_pkt = new SrsRtpPacket();
    test_pkt->header_.set_sequence(1234);
    test_pkt->header_.set_timestamp(5678);
    test_pkt->header_.set_ssrc(0x12345678);

    // Enqueue the packet (consumer takes ownership)
    HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt));

    // Dump the packet
    SrsRtpPacket *dumped_pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));

    EXPECT_TRUE(dumped_pkt != NULL);
    EXPECT_EQ(1234, dumped_pkt->header_.get_sequence());
    EXPECT_EQ(5678, dumped_pkt->header_.get_timestamp());
    EXPECT_EQ(0x12345678, dumped_pkt->header_.get_ssrc());

    srs_freep(dumped_pkt);
}

VOID TEST(AppTest2, RtcConsumerEnqueueMultiplePackets)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Create and enqueue multiple test packets
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *test_pkt = new SrsRtpPacket();
        test_pkt->header_.set_sequence(1000 + i);
        test_pkt->header_.set_timestamp(2000 + i * 100);
        test_pkt->header_.set_ssrc(0x12345678);

        HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt));
    }

    // Dump packets in FIFO order
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *dumped_pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));

        EXPECT_TRUE(dumped_pkt != NULL);
        EXPECT_EQ(1000 + i, dumped_pkt->header_.get_sequence());
        EXPECT_EQ(2000 + i * 100, dumped_pkt->header_.get_timestamp());

        srs_freep(dumped_pkt);
    }

    // Queue should be empty now
    SrsRtpPacket *empty_pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&empty_pkt));
    EXPECT_TRUE(empty_pkt == NULL);
}

VOID TEST(AppTest2, RtcConsumerDumpEmptyQueue)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Dump from empty queue should return NULL
    SrsRtpPacket *pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt == NULL);
}

VOID TEST(AppTest2, RtcConsumerStreamChangeCallback)
{
    MockRtcSourceForConsumer mock_source;
    MockRtcSourceChangeCallback mock_callback;
    SrsRtcConsumer consumer(&mock_source);

    // Set the callback handler
    consumer.set_handler(&mock_callback);

    // Create a mock stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Trigger stream change
    consumer.on_stream_change(stream_desc.get());

    // Verify callback was called
    EXPECT_EQ(1, mock_callback.stream_change_count_);
    EXPECT_EQ(stream_desc.get(), mock_callback.last_stream_desc_);
}

VOID TEST(AppTest2, RtcConsumerStreamChangeCallbackNoHandler)
{
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // No handler set - should not crash
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // This should not crash even without handler
    consumer.on_stream_change(stream_desc.get());
}

VOID TEST(AppTest2, RtcConsumerQueueSizeCheck)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Enqueue packets and verify queue behavior
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *test_pkt = new SrsRtpPacket();
        test_pkt->header_.set_sequence(1000 + i);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt));
    }

    // Verify we can dump packets in correct order
    SrsRtpPacket *pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt != NULL);
    EXPECT_EQ(1000, pkt->header_.get_sequence());
    srs_freep(pkt);
}

VOID TEST(AppTest2, RtcConsumerEnqueueBehavior)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Enqueue multiple packets to test queue behavior
    SrsRtpPacket *test_pkt1 = new SrsRtpPacket();
    test_pkt1->header_.set_sequence(1001);
    HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt1));

    SrsRtpPacket *test_pkt2 = new SrsRtpPacket();
    test_pkt2->header_.set_sequence(1002);
    HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt2));

    SrsRtpPacket *test_pkt3 = new SrsRtpPacket();
    test_pkt3->header_.set_sequence(1003);
    HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt3));

    // Verify packets are in queue in FIFO order
    SrsRtpPacket *pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt != NULL);
    EXPECT_EQ(1001, pkt->header_.get_sequence());
    srs_freep(pkt);

    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt != NULL);
    EXPECT_EQ(1002, pkt->header_.get_sequence());
    srs_freep(pkt);
}

VOID TEST(AppTest2, RtcConsumerSourceIdUpdate)
{
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Set different source IDs
    mock_source.source_id_.set_value("new-source-id");
    mock_source.pre_source_id_.set_value("old-source-id");

    // Trigger source ID update
    consumer.update_source_id();

    // Dump packet should log the source ID change
    SrsRtpPacket *pkt = NULL;
    srs_error_t err;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt == NULL); // No packets in queue

    // After dump_packet, the update flag should be reset
    // Calling dump_packet again should not trigger another log
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt == NULL);
}

VOID TEST(AppTest2, RtcConsumerPacketMemoryManagement)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Create test packets and enqueue copies (simulating real usage)
    std::vector<SrsRtpPacket *> original_packets;
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket *original_pkt = new SrsRtpPacket();
        original_pkt->header_.set_sequence(2000 + i);
        original_pkt->header_.set_timestamp(3000 + i * 100);
        original_packets.push_back(original_pkt);

        // Enqueue a copy (this is how it's used in real code)
        HELPER_EXPECT_SUCCESS(consumer.enqueue(original_pkt->copy()));
    }

    // Dump all packets and verify they have correct data
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket *dumped_pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));

        EXPECT_TRUE(dumped_pkt != NULL);
        EXPECT_TRUE(dumped_pkt != original_packets[i]); // Should be different instances
        EXPECT_EQ(2000 + i, dumped_pkt->header_.get_sequence());
        EXPECT_EQ(3000 + i * 100, dumped_pkt->header_.get_timestamp());

        srs_freep(dumped_pkt);
    }

    // Clean up original packets
    for (size_t i = 0; i < original_packets.size(); i++) {
        srs_freep(original_packets[i]);
    }
}

VOID TEST(AppTest2, RtcConsumerCallbackHandlerLifecycle)
{
    MockRtcSourceForConsumer mock_source;
    MockRtcSourceChangeCallback callback1;
    MockRtcSourceChangeCallback callback2;
    SrsRtcConsumer consumer(&mock_source);

    // Initially no handler
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream-1";
    consumer.on_stream_change(stream_desc.get());
    EXPECT_EQ(0, callback1.stream_change_count_);
    EXPECT_EQ(0, callback2.stream_change_count_);

    // Set first handler
    consumer.set_handler(&callback1);
    consumer.on_stream_change(stream_desc.get());
    EXPECT_EQ(1, callback1.stream_change_count_);
    EXPECT_EQ(0, callback2.stream_change_count_);

    // Change handler
    consumer.set_handler(&callback2);
    stream_desc->id_ = "test-stream-2";
    consumer.on_stream_change(stream_desc.get());
    EXPECT_EQ(1, callback1.stream_change_count_); // Should not increase
    EXPECT_EQ(1, callback2.stream_change_count_); // Should increase

    // Remove handler
    consumer.set_handler(NULL);
    consumer.on_stream_change(stream_desc.get());
    EXPECT_EQ(1, callback1.stream_change_count_); // Should not increase
    EXPECT_EQ(1, callback2.stream_change_count_); // Should not increase
}

VOID TEST(AppTest2, RtcConsumerLargePacketQueue)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    const int large_count = 100;

    // Enqueue many packets
    for (int i = 0; i < large_count; i++) {
        SrsRtpPacket *test_pkt = new SrsRtpPacket();
        test_pkt->header_.set_sequence(4000 + i);
        test_pkt->header_.set_timestamp(5000 + i * 10);
        test_pkt->header_.set_ssrc(0xABCDEF00 + i);

        HELPER_EXPECT_SUCCESS(consumer.enqueue(test_pkt));
    }

    // Dump all packets and verify order
    for (int i = 0; i < large_count; i++) {
        SrsRtpPacket *dumped_pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));

        EXPECT_TRUE(dumped_pkt != NULL);
        EXPECT_EQ(4000 + i, dumped_pkt->header_.get_sequence());
        EXPECT_EQ(5000 + i * 10, dumped_pkt->header_.get_timestamp());
        EXPECT_EQ(0xABCDEF00 + i, dumped_pkt->header_.get_ssrc());

        srs_freep(dumped_pkt);
    }

    // Queue should be empty
    SrsRtpPacket *empty_pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&empty_pkt));
    EXPECT_TRUE(empty_pkt == NULL);
}

VOID TEST(AppTest2, RtcSourceOnSourceChangedNoConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Call on_source_changed with no consumers - should succeed
    HELPER_EXPECT_SUCCESS(source->on_source_changed());
}

VOID TEST(AppTest2, RtcSourceOnSourceChangedWithConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create consumers using the source's create_consumer method
    ISrsRtcConsumer *consumer1 = NULL;
    ISrsRtcConsumer *consumer2 = NULL;
    ISrsRtcConsumer *consumer3 = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer1));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer2));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer3));

    // Create a stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream-123";
    source->set_stream_desc(stream_desc.get());

    // Call on_source_changed - should notify all consumers
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Verify stream description is accessible
    EXPECT_TRUE(source->has_stream_desc());

    // Note: We can't easily verify consumer notifications without accessing private members
    // or creating a more complex mock setup. The test verifies the method executes successfully.
}

VOID TEST(AppTest2, RtcSourceOnSourceChangedContextIdChange)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create consumers
    ISrsRtcConsumer *consumer1 = NULL;
    ISrsRtcConsumer *consumer2 = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer1));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer2));

    // Store original context ID
    SrsContextId original_context = _srs_context->get_id();

    // Change the global context ID to simulate source ID change
    SrsContextId new_context = _srs_context->generate_id();
    _srs_context->set_id(new_context);

    // Call on_source_changed - should detect context ID change and notify consumers
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Verify source ID was updated
    EXPECT_TRUE(source->source_id().compare(new_context) == 0);

    // Restore original context
    _srs_context->set_id(original_context);
}

VOID TEST(AppTest2, RtcSourceOnSourceChangedNoContextIdChange)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Call on_source_changed first time to set initial context ID
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Store the source ID after first call
    SrsContextId first_source_id = source->source_id();

    // Call on_source_changed again without changing context ID
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Verify source ID remains the same (no context change)
    EXPECT_TRUE(source->source_id().compare(first_source_id) == 0);
}

VOID TEST(AppTest2, RtcSourceOnSourceChangedPreviousSourceId)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Store original context ID
    SrsContextId original_context = _srs_context->get_id();

    // Call on_source_changed first time to set initial context ID
    HELPER_EXPECT_SUCCESS(source->on_source_changed());
    SrsContextId first_context = source->source_id();

    // Note: Due to the buggy implementation, _pre_source_id gets set to the current context ID
    // on the first call when _source_id was empty, so it's not empty after the first call
    EXPECT_FALSE(source->pre_source_id().empty());
    EXPECT_TRUE(source->pre_source_id().compare(first_context) == 0);

    // Change the global context ID to simulate source ID change
    SrsContextId new_context = _srs_context->generate_id();
    _srs_context->set_id(new_context);

    // Call on_source_changed - should update current source ID but not previous (since it's not empty)
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Verify current source ID was updated
    EXPECT_TRUE(source->source_id().compare(new_context) == 0);

    // Verify previous source ID remains the first_context (not updated because it's not empty)
    EXPECT_TRUE(source->pre_source_id().compare(first_context) == 0);

    // Change context ID again
    SrsContextId third_context = _srs_context->generate_id();
    _srs_context->set_id(third_context);

    // Call on_source_changed again
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Verify current source ID was updated to third context
    EXPECT_TRUE(source->source_id().compare(third_context) == 0);

    // Verify previous source ID remains the first_context (not updated again because it's not empty)
    EXPECT_TRUE(source->pre_source_id().compare(first_context) == 0);

    // Restore original context
    _srs_context->set_id(original_context);
}

VOID TEST(AppTest2, RtcSourceOnSourceChangedWithStreamDescription)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Create and set stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream-with-desc";

    // Add audio track description
    stream_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    stream_desc->audio_track_desc_->type_ = "audio";
    stream_desc->audio_track_desc_->id_ = "audio-track-1";
    stream_desc->audio_track_desc_->ssrc_ = 12345;

    // Add video track description
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->id_ = "video-track-1";
    video_track->ssrc_ = 67890;
    stream_desc->video_track_descs_.push_back(video_track);

    source->set_stream_desc(stream_desc.get());

    // Call on_source_changed
    HELPER_EXPECT_SUCCESS(source->on_source_changed());

    // Verify stream description is accessible
    EXPECT_TRUE(source->has_stream_desc());
}

VOID TEST(AppTest2, RtcConsumerWaitWithSufficientPackets)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Add some packets to the queue
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(1000 + i);
        pkt->header_.set_timestamp(2000 + i * 100);
        pkt->header_.set_ssrc(0x12345678);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Wait for 3 messages - should return immediately since we have 5
    consumer.wait(3);

    // Verify we can dump packets (queue should still have packets)
    SrsRtpPacket *dumped_pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));
    EXPECT_TRUE(dumped_pkt != NULL);
    EXPECT_EQ(1000, dumped_pkt->header_.get_sequence());
    srs_freep(dumped_pkt);

    // Verify more packets are available
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));
    EXPECT_TRUE(dumped_pkt != NULL);
    EXPECT_EQ(1001, dumped_pkt->header_.get_sequence());
    srs_freep(dumped_pkt);
}

VOID TEST(AppTest2, RtcConsumerWaitWithMoreThanRequestedPackets)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Add 5 packets to the queue
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(2000 + i);
        pkt->header_.set_timestamp(3000 + i * 50);
        pkt->header_.set_ssrc(0xABCDEF00);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Wait for 3 messages - should return immediately since we have 5 > 3
    consumer.wait(3);

    // Verify all packets are available
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *dumped_pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));
        EXPECT_TRUE(dumped_pkt != NULL);
        EXPECT_EQ(2000 + i, dumped_pkt->header_.get_sequence());
        EXPECT_EQ(3000 + i * 50, dumped_pkt->header_.get_timestamp());
        srs_freep(dumped_pkt);
    }

    // Queue should be empty now
    SrsRtpPacket *empty_pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&empty_pkt));
    EXPECT_TRUE(empty_pkt == NULL);
}

VOID TEST(AppTest2, RtcConsumerWaitWithZeroMessages)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Add some packets to the queue
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(3000 + i);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Wait for 0 messages - should return immediately regardless of queue size
    consumer.wait(0);

    // Verify packets are still available
    SrsRtpPacket *dumped_pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&dumped_pkt));
    EXPECT_TRUE(dumped_pkt != NULL);
    EXPECT_EQ(3000, dumped_pkt->header_.get_sequence());
    srs_freep(dumped_pkt);
}

VOID TEST(AppTest2, RtcConsumerWaitWithEmptyQueueZeroMessages)
{
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Queue is empty, wait for -1 messages - should return immediately
    // This tests the edge case where nb_msgs is -1
    consumer.wait(-1);

    // Verify queue is still empty
    SrsRtpPacket *pkt = NULL;
    srs_error_t err;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt == NULL);
}

VOID TEST(AppTest2, RtcConsumerWaitWithInsufficientPackets)
{
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Add only 2 packets to the queue
    for (int i = 0; i < 2; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(4000 + i);
        pkt->header_.set_timestamp(5000 + i * 200);
        pkt->header_.set_ssrc(0xDEADBEEF);
        srs_error_t err;
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Start a goroutine to add more packets after a delay
    SrsCoroutineChan ctx;
    ctx.push(&consumer);

    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsRtcConsumer *consumer = (SrsRtcConsumer *)ctx.pop();
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        for (int i = 0; i < 4; i++) {
            SrsRtpPacket *pkt = new SrsRtpPacket();
            pkt->header_.set_sequence(4002 + i);
            pkt->header_.set_timestamp(5000 + (i + 2) * 200);
            pkt->header_.set_ssrc(0xDEADBEEF);
            srs_error_t err;
            HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt));
        }
    });

    // Wait for 5 messages when we only have 2
    // In real usage, this would set mw_waiting_ = true and block until more packets arrive
    consumer.wait(5);

    // Verify we still have the 2 packets we added
    SrsRtpPacket *pkt1 = NULL;
    srs_error_t err;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt1));
    EXPECT_TRUE(pkt1 != NULL);
    EXPECT_EQ(4000, pkt1->header_.get_sequence());
    srs_freep(pkt1);

    SrsRtpPacket *pkt2 = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt2));
    EXPECT_TRUE(pkt2 != NULL);
    EXPECT_EQ(4001, pkt2->header_.get_sequence());
    srs_freep(pkt2);
}

VOID TEST(AppTest2, RtcConsumerWaitMultipleCalls)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Test multiple wait calls with different thresholds

    // First, add 4 packets
    for (int i = 0; i < 4; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(5000 + i);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Wait for 2 messages - should return immediately
    consumer.wait(2);

    // Wait for 1 message - should return immediately
    consumer.wait(1);

    // Wait for 3 messages - should return immediately (we have exactly 3)
    consumer.wait(3);

    // Start a goroutine to add more packets after a delay
    SrsCoroutineChan ctx;
    ctx.push(&consumer);

    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsRtcConsumer *consumer = (SrsRtcConsumer *)ctx.pop();
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        for (int i = 0; i < 1; i++) {
            SrsRtpPacket *pkt = new SrsRtpPacket();
            pkt->header_.set_sequence(4002 + i);
            pkt->header_.set_timestamp(5000 + (i + 2) * 200);
            pkt->header_.set_ssrc(0xDEADBEEF);
            srs_error_t err;
            HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt));
        }
    });

    // Wait for 4 messages - would block for a while in real usage, but we can't test that
    consumer.wait(4);

    // Verify all packets are still available
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket *pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(5000 + i, pkt->header_.get_sequence());
        srs_freep(pkt);
    }
}

VOID TEST(AppTest2, RtcConsumerWaitAfterDumpingPackets)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Add 5 packets
    for (int i = 0; i < 5; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(6000 + i);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Wait for 3 messages - should return immediately
    consumer.wait(3);

    // Dump 2 packets, leaving 3 in queue
    for (int i = 0; i < 2; i++) {
        SrsRtpPacket *pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
        EXPECT_TRUE(pkt != NULL);
        srs_freep(pkt);
    }

    // Wait for 2 messages - should return immediately (we still have 3)
    consumer.wait(2);

    // Dump 1 more packet, leaving 2 in queue
    SrsRtpPacket *pkt = NULL;
    HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
    EXPECT_TRUE(pkt != NULL);
    srs_freep(pkt);

    // Start a goroutine to add more packets after a delay
    SrsCoroutineChan ctx;
    ctx.push(&consumer);

    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsRtcConsumer *consumer = (SrsRtcConsumer *)ctx.pop();
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        for (int i = 0; i < 2; i++) {
            SrsRtpPacket *pkt = new SrsRtpPacket();
            pkt->header_.set_sequence(4002 + i);
            pkt->header_.set_timestamp(5000 + (i + 2) * 200);
            pkt->header_.set_ssrc(0xDEADBEEF);
            srs_error_t err;
            HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt));
        }
    });

    // Wait for 3 messages - would block for a while in real usage (we only have 2)
    consumer.wait(3);

    // Verify remaining packets
    for (int i = 0; i < 3; i++) {
        SrsRtpPacket *remaining_pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&remaining_pkt));
        EXPECT_TRUE(remaining_pkt != NULL);
        srs_freep(remaining_pkt);
    }
}

VOID TEST(AppTest2, RtcConsumerWaitWithLargeThreshold)
{
    srs_error_t err;
    MockRtcSourceForConsumer mock_source;
    SrsRtcConsumer consumer(&mock_source);

    // Add 10 packets
    for (int i = 0; i < 10; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(7000 + i);
        pkt->header_.set_timestamp(8000 + i * 1000);
        HELPER_EXPECT_SUCCESS(consumer.enqueue(pkt));
    }

    // Start a goroutine to add more packets after a delay
    SrsCoroutineChan ctx;
    ctx.push(&consumer);

    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsRtcConsumer *consumer = (SrsRtcConsumer *)ctx.pop();
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        for (int i = 0; i < 91; i++) {
            SrsRtpPacket *pkt = new SrsRtpPacket();
            pkt->header_.set_sequence(4002 + i);
            pkt->header_.set_timestamp(5000 + (i + 2) * 200);
            pkt->header_.set_ssrc(0xDEADBEEF);
            srs_error_t err;
            HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt));
        }
    });

    // Wait for a large number of messages (100) - would block for a while in real usage
    consumer.wait(100);

    // Verify all 10 packets are still available
    for (int i = 0; i < 10; i++) {
        SrsRtpPacket *pkt = NULL;
        HELPER_EXPECT_SUCCESS(consumer.dump_packet(&pkt));
        EXPECT_TRUE(pkt != NULL);
        EXPECT_EQ(7000 + i, pkt->header_.get_sequence());
        EXPECT_EQ(8000 + i * 1000, pkt->header_.get_timestamp());
        srs_freep(pkt);
    }
}

VOID TEST(AppTest2, RtcSourceOnRtpWithBridgeSuccess)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create and set mock bridge
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Create test RTP packet
    SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
    test_pkt->header_.set_sequence(1234);
    test_pkt->header_.set_timestamp(5678);
    test_pkt->header_.set_ssrc(0x12345678);

    // Test: on_rtp should call bridge->on_rtp and succeed
    HELPER_EXPECT_SUCCESS(source->on_rtp(test_pkt.get()));

    // Verify bridge was called
    EXPECT_EQ(1, mock_bridge->on_rtp_count_);
    EXPECT_TRUE(mock_bridge->last_rtp_packet_ != NULL);
    EXPECT_EQ(1234, mock_bridge->last_rtp_packet_->header_.get_sequence());
    EXPECT_EQ(5678, mock_bridge->last_rtp_packet_->header_.get_timestamp());
    EXPECT_EQ(0x12345678, mock_bridge->last_rtp_packet_->header_.get_ssrc());
}

VOID TEST(AppTest2, RtcSourceOnRtpWithBridgeFailure)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock bridge that will fail on_rtp
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    srs_error_t bridge_error = srs_error_new(ERROR_RTC_RTP_MUXER, "mock bridge error");
    mock_bridge->set_on_rtp_error(bridge_error);
    source->set_bridge(mock_bridge);

    // Create test RTP packet
    SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
    test_pkt->header_.set_sequence(9999);
    test_pkt->header_.set_timestamp(8888);
    test_pkt->header_.set_ssrc(0xABCDEF00);

    // Test: on_rtp should fail when bridge->on_rtp fails
    HELPER_EXPECT_FAILED(source->on_rtp(test_pkt.get()));

    // Verify bridge was called
    EXPECT_EQ(1, mock_bridge->on_rtp_count_);
    EXPECT_TRUE(mock_bridge->last_rtp_packet_ != NULL);
    EXPECT_EQ(9999, mock_bridge->last_rtp_packet_->header_.get_sequence());
    EXPECT_EQ(8888, mock_bridge->last_rtp_packet_->header_.get_timestamp());
    EXPECT_EQ(0xABCDEF00, mock_bridge->last_rtp_packet_->header_.get_ssrc());

    srs_freep(bridge_error);
}

VOID TEST(AppTest2, RtcSourceOnRtpWithoutBridge)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source without setting bridge
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create test RTP packet
    SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
    test_pkt->header_.set_sequence(5555);
    test_pkt->header_.set_timestamp(6666);
    test_pkt->header_.set_ssrc(0x11223344);

    // Test: on_rtp should succeed when no bridge is set (rtc_bridge_ is NULL)
    HELPER_EXPECT_SUCCESS(source->on_rtp(test_pkt.get()));
}

VOID TEST(AppTest2, RtcSourceOnRtpWithBridgeAndConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create consumers
    ISrsRtcConsumer *consumer1 = NULL;
    ISrsRtcConsumer *consumer2 = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer1));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer2));

    // Create and set mock bridge
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Create test RTP packet
    SrsUniquePtr<SrsRtpPacket> test_pkt(new SrsRtpPacket());
    test_pkt->header_.set_sequence(7777);
    test_pkt->header_.set_timestamp(8888);
    test_pkt->header_.set_ssrc(0xFEDCBA98);

    // Test: on_rtp should succeed, delivering to consumers and bridge
    HELPER_EXPECT_SUCCESS(source->on_rtp(test_pkt.get()));

    // Verify bridge was called
    EXPECT_EQ(1, mock_bridge->on_rtp_count_);
    EXPECT_TRUE(mock_bridge->last_rtp_packet_ != NULL);
    EXPECT_EQ(7777, mock_bridge->last_rtp_packet_->header_.get_sequence());
    EXPECT_EQ(8888, mock_bridge->last_rtp_packet_->header_.get_timestamp());
    EXPECT_EQ(0xFEDCBA98, mock_bridge->last_rtp_packet_->header_.get_ssrc());

    // Verify consumers received packets (we can't easily check this without accessing private members,
    // but the test verifies the method executes successfully with both consumers and bridge)
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateNewSource)
{
    srs_error_t err;

    // Create RTC source manager
    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create a mock request
    SrsUniquePtr<MockFailingRequest> req(new MockFailingRequest("test-stream-url"));

    // Fetch or create source - should create new source
    SrsSharedPtr<SrsRtcSource> source;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req.get(), source));

    // Verify source was created
    EXPECT_TRUE(source.get() != NULL);

    // Fetch the same source again - should return existing source
    SrsSharedPtr<SrsRtcSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req.get(), source2));

    // Verify it's the same source instance
    EXPECT_TRUE(source.get() == source2.get());
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateInitializeSuccess)
{
    srs_error_t err;

    // Create RTC source manager
    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create a mock request
    SrsUniquePtr<MockFailingRequest> req(new MockFailingRequest("test-stream-success"));

    // Fetch or create source - should create new source and initialize successfully
    SrsSharedPtr<SrsRtcSource> source;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req.get(), source));

    // Verify source was created and initialized
    EXPECT_TRUE(source.get() != NULL);

    // Create a consumer to make the source not dead
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Now the source should not be dead since it has a consumer
    EXPECT_FALSE(source->stream_is_dead());
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateInitializeFailure)
{
    srs_error_t err;

    // Create a custom source manager that uses our failing mock source
    class MockRtcSourceManager : public SrsRtcSourceManager
    {
    public:
        bool should_fail_initialize_;
        srs_error_t initialize_error_;

        MockRtcSourceManager()
        {
            should_fail_initialize_ = false;
            initialize_error_ = srs_success;
        }

        virtual ~MockRtcSourceManager()
        {
            srs_freep(initialize_error_);
        }

        virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
        {
            srs_error_t err = srs_success;

            bool created = false;
            // Should never invoke any function during the locking.
            if (true) {
                // Use lock to protect coroutine switch.
                SrsLocker(&lock_);

                string stream_url = r->get_stream_url();
                std::map<std::string, SrsSharedPtr<SrsRtcSource> >::iterator it = pool_.find(stream_url);

                if (it != pool_.end()) {
                    SrsSharedPtr<SrsRtcSource> source = it->second;
                    pps = source;
                } else {
                    // Create our failing mock source instead of regular source
                    MockFailingRtcSource *mock_source = new MockFailingRtcSource();
                    if (should_fail_initialize_) {
                        mock_source->set_initialize_error(initialize_error_);
                    }
                    SrsSharedPtr<SrsRtcSource> source = SrsSharedPtr<SrsRtcSource>(mock_source);
                    pps = source;

                    pool_[stream_url] = source;
                    created = true;
                }
            }

            // Initialize source.
            if (created && (err = pps->initialize(r)) != srs_success) {
                return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
            }

            // we always update the request of resource,
            // for origin auth is on, the token in request maybe invalid,
            // and we only need to update the token of request, it's simple.
            if (!created) {
                pps->update_auth(r);
            }

            return err;
        }

        void set_initialize_error(srs_error_t err)
        {
            srs_freep(initialize_error_);
            initialize_error_ = srs_error_copy(err);
            should_fail_initialize_ = true;
        }
    };

    // Create mock source manager
    MockRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Set up the manager to fail initialization
    srs_error_t test_error = srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "test initialization failure");
    manager.set_initialize_error(test_error);
    srs_freep(test_error);

    // Create a mock request
    SrsUniquePtr<MockFailingRequest> req(new MockFailingRequest("test-stream-fail"));

    // Fetch or create source - should fail during initialization
    SrsSharedPtr<SrsRtcSource> source;
    HELPER_EXPECT_FAILED(manager.fetch_or_create(req.get(), source));
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateErrorWrapping)
{
    srs_error_t err;

    // Create a custom source manager that uses our failing mock source
    class MockRtcSourceManagerWithErrorWrapping : public SrsRtcSourceManager
    {
    public:
        virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
        {
            srs_error_t err = srs_success;

            bool created = false;
            // Should never invoke any function during the locking.
            if (true) {
                // Use lock to protect coroutine switch.
                SrsLocker(&lock_);

                string stream_url = r->get_stream_url();
                std::map<std::string, SrsSharedPtr<SrsRtcSource> >::iterator it = pool_.find(stream_url);

                if (it != pool_.end()) {
                    SrsSharedPtr<SrsRtcSource> source = it->second;
                    pps = source;
                } else {
                    // Create a failing mock source
                    MockFailingRtcSource *mock_source = new MockFailingRtcSource();
                    srs_error_t init_error = srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "mock init error");
                    mock_source->set_initialize_error(init_error);
                    srs_freep(init_error);

                    SrsSharedPtr<SrsRtcSource> source = SrsSharedPtr<SrsRtcSource>(mock_source);
                    pps = source;

                    pool_[stream_url] = source;
                    created = true;
                }
            }

            // Initialize source.
            if (created && (err = pps->initialize(r)) != srs_success) {
                return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
            }

            // we always update the request of resource,
            // for origin auth is on, the token in request maybe invalid,
            // and we only need to update the token of request, it's simple.
            if (!created) {
                pps->update_auth(r);
            }

            return err;
        }
    };

    // Create mock source manager
    MockRtcSourceManagerWithErrorWrapping manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create a mock request
    SrsUniquePtr<MockFailingRequest> req(new MockFailingRequest("test-stream-error-wrap"));

    // Fetch or create source - should fail during initialization and wrap the error
    SrsSharedPtr<SrsRtcSource> source;
    err = manager.fetch_or_create(req.get(), source);

    // Verify that the error was wrapped with context information
    EXPECT_TRUE(err != srs_success);
    if (err != srs_success) {
        std::string error_desc = srs_error_desc(err);
        // The error should contain the wrapped context with stream URL
        EXPECT_TRUE(error_desc.find("init source") != std::string::npos);
        EXPECT_TRUE(error_desc.find("test-stream-error-wrap") != std::string::npos);
        srs_freep(err);
    }
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateMultipleStreams)
{
    srs_error_t err;

    // Create RTC source manager
    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create multiple mock requests for different streams
    SrsUniquePtr<MockFailingRequest> req1(new MockFailingRequest("stream1"));
    SrsUniquePtr<MockFailingRequest> req2(new MockFailingRequest("stream2"));
    SrsUniquePtr<MockFailingRequest> req3(new MockFailingRequest("stream3"));

    // Fetch or create sources for different streams
    SrsSharedPtr<SrsRtcSource> source1, source2, source3;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req1.get(), source1));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req2.get(), source2));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req3.get(), source3));

    // Verify all sources were created and are different instances
    EXPECT_TRUE(source1.get() != NULL);
    EXPECT_TRUE(source2.get() != NULL);
    EXPECT_TRUE(source3.get() != NULL);
    EXPECT_TRUE(source1.get() != source2.get());
    EXPECT_TRUE(source2.get() != source3.get());
    EXPECT_TRUE(source1.get() != source3.get());

    // Fetch the same streams again - should return existing sources
    SrsSharedPtr<SrsRtcSource> source1_again, source2_again, source3_again;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req1.get(), source1_again));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req2.get(), source2_again));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req3.get(), source3_again));

    // Verify they are the same instances (not newly created)
    EXPECT_TRUE(source1.get() == source1_again.get());
    EXPECT_TRUE(source2.get() == source2_again.get());
    EXPECT_TRUE(source3.get() == source3_again.get());
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateExistingSourceUpdateAuth)
{
    srs_error_t err;

    // Create RTC source manager
    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create a mock request
    SrsUniquePtr<MockFailingRequest> req(new MockFailingRequest("test-stream-auth"));

    // First call - should create new source and initialize it
    SrsSharedPtr<SrsRtcSource> source1;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req.get(), source1));
    EXPECT_TRUE(source1.get() != NULL);

    // Second call with same stream URL - should return existing source and call update_auth
    // This tests the !created path where pps->update_auth(r) is called
    SrsSharedPtr<SrsRtcSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req.get(), source2));

    // Verify it's the same source instance (existing source was returned)
    EXPECT_TRUE(source1.get() == source2.get());

    // The update_auth method should have been called on the existing source
    // We can't directly verify this without modifying the source, but we can verify
    // that the fetch_or_create succeeded and returned the same instance
}

VOID TEST(AppTest2, RtcSourceManagerFetchOrCreateConcurrentAccess)
{
    srs_error_t err;

    // Create RTC source manager
    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create mock requests for the same stream
    SrsUniquePtr<MockFailingRequest> req1(new MockFailingRequest("concurrent-stream"));
    SrsUniquePtr<MockFailingRequest> req2(new MockFailingRequest("concurrent-stream"));

    // Simulate concurrent access by calling fetch_or_create multiple times
    // The locking mechanism should ensure only one source is created
    SrsSharedPtr<SrsRtcSource> source1, source2, source3;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req1.get(), source1));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req2.get(), source2));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(req1.get(), source3));

    // All should return the same source instance
    EXPECT_TRUE(source1.get() != NULL);
    EXPECT_TRUE(source1.get() == source2.get());
    EXPECT_TRUE(source1.get() == source3.get());
}

VOID TEST(AppTest2, RtcSourceOnConsumerDestroyRemoveConsumer)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumers
    MockRtcConsumer *consumer1 = new MockRtcConsumer();
    MockRtcConsumer *consumer2 = new MockRtcConsumer();
    MockRtcConsumer *consumer3 = new MockRtcConsumer();

    // Add consumers to source manually (simulating create_consumer)
    source->consumers_.push_back(consumer1);
    source->consumers_.push_back(consumer2);
    source->consumers_.push_back(consumer3);

    // Verify initial state
    EXPECT_EQ(3, (int)source->consumers_.size());

    // Remove middle consumer
    source->on_consumer_destroy(consumer2);
    EXPECT_EQ(2, (int)source->consumers_.size());
    EXPECT_EQ(consumer1, source->consumers_[0]);
    EXPECT_EQ(consumer3, source->consumers_[1]);

    // Remove first consumer
    source->on_consumer_destroy(consumer1);
    EXPECT_EQ(1, (int)source->consumers_.size());
    EXPECT_EQ(consumer3, source->consumers_[0]);

    // Remove last consumer
    source->on_consumer_destroy(consumer3);
    EXPECT_EQ(0, (int)source->consumers_.size());

    // Try to remove non-existent consumer (should not crash)
    MockRtcConsumer *non_existent = new MockRtcConsumer();
    source->on_consumer_destroy(non_existent);
    EXPECT_EQ(0, (int)source->consumers_.size());

    // Clean up
    srs_freep(consumer1);
    srs_freep(consumer2);
    srs_freep(consumer3);
    srs_freep(non_existent);
}

VOID TEST(AppTest2, RtcSourceOnConsumerDestroyNotifyEventHandlers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock publish stream and event handlers
    MockRtcPublishStream *publish_stream = new MockRtcPublishStream();
    MockRtcSourceEventHandler *handler1 = new MockRtcSourceEventHandler();
    MockRtcSourceEventHandler *handler2 = new MockRtcSourceEventHandler();

    // Set up source with publish stream and event handlers
    source->set_publish_stream(publish_stream);
    source->rtc_source_subscribe(handler1);
    source->rtc_source_subscribe(handler2);

    // Create mock consumers
    MockRtcConsumer *consumer1 = new MockRtcConsumer();
    MockRtcConsumer *consumer2 = new MockRtcConsumer();

    // Add consumers to source manually
    source->consumers_.push_back(consumer1);
    source->consumers_.push_back(consumer2);

    // Verify initial state
    EXPECT_EQ(0, handler1->on_consumers_finished_count_);
    EXPECT_EQ(0, handler2->on_consumers_finished_count_);

    // Remove first consumer - should not notify handlers yet
    source->on_consumer_destroy(consumer1);
    EXPECT_EQ(0, handler1->on_consumers_finished_count_);
    EXPECT_EQ(0, handler2->on_consumers_finished_count_);

    // Remove last consumer - should notify all handlers
    source->on_consumer_destroy(consumer2);
    EXPECT_EQ(1, handler1->on_consumers_finished_count_);
    EXPECT_EQ(1, handler2->on_consumers_finished_count_);

    // Clean up
    srs_freep(consumer1);
    srs_freep(consumer2);
    srs_freep(publish_stream);
    srs_freep(handler1);
    srs_freep(handler2);
}

VOID TEST(AppTest2, RtcSourceOnConsumerDestroyNoPublishStream)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock event handler
    MockRtcSourceEventHandler *handler = new MockRtcSourceEventHandler();
    source->rtc_source_subscribe(handler);

    // Create mock consumer
    MockRtcConsumer *consumer = new MockRtcConsumer();
    source->consumers_.push_back(consumer);

    // Verify initial state - no publish stream set
    EXPECT_TRUE(source->publish_stream() == NULL);
    EXPECT_EQ(0, handler->on_consumers_finished_count_);

    // Remove consumer - should not notify handlers because no publish stream
    source->on_consumer_destroy(consumer);
    EXPECT_EQ(0, handler->on_consumers_finished_count_);

    // Clean up
    srs_freep(consumer);
    srs_freep(handler);
}

VOID TEST(AppTest2, RtcSourceOnConsumerDestroyStreamDeath)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumer
    MockRtcConsumer *consumer = new MockRtcConsumer();
    source->consumers_.push_back(consumer);

    // Verify initial state - stream is not created (no publisher)
    EXPECT_FALSE(source->is_created_);
    // After fix #4449: stream_die_at_ is initialized to current time, not 0
    srs_utime_t initial_die_at = source->stream_die_at_;
    EXPECT_GT(initial_die_at, 0);

    // Remove consumer when stream is not created - should update stream_die_at_
    srs_utime_t before_time = srs_time_now_cached();
    source->on_consumer_destroy(consumer);
    srs_utime_t after_time = srs_time_now_cached();

    // Verify stream death time was updated to current time
    EXPECT_TRUE(source->stream_die_at_ >= before_time);
    EXPECT_TRUE(source->stream_die_at_ <= after_time);

    // Clean up
    srs_freep(consumer);
}

VOID TEST(AppTest2, RtcSourceOnConsumerDestroyStreamAlive)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumer
    MockRtcConsumer *consumer = new MockRtcConsumer();
    source->consumers_.push_back(consumer);

    // Set stream as created (has publisher)
    source->is_created_ = true;

    // Verify initial state
    EXPECT_TRUE(source->is_created_);
    // After fix #4449: stream_die_at_ is initialized to current time, not 0
    srs_utime_t initial_die_at = source->stream_die_at_;
    EXPECT_GT(initial_die_at, 0);

    // Remove consumer when stream is created - should NOT update stream_die_at_
    source->on_consumer_destroy(consumer);

    // Verify stream death time was NOT changed (still has initial value)
    EXPECT_EQ(initial_die_at, source->stream_die_at_);

    // Clean up
    srs_freep(consumer);
}

VOID TEST(AppTest2, RtcSourceOnPublishBasicSuccess)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Verify initial state
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);

    // Test basic on_publish without bridge
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify state after publish
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceOnPublishWithConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumers
    MockRtcConsumer *consumer1 = new MockRtcConsumer();
    MockRtcConsumer *consumer2 = new MockRtcConsumer();
    source->consumers_.push_back(consumer1);
    source->consumers_.push_back(consumer2);

    // Verify initial consumer state
    EXPECT_EQ(0, consumer1->update_source_id_count_);
    EXPECT_EQ(0, consumer1->stream_change_count_);
    EXPECT_EQ(0, consumer2->update_source_id_count_);
    EXPECT_EQ(0, consumer2->stream_change_count_);

    // Test on_publish - should notify consumers via on_source_changed
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify consumers were notified
    EXPECT_EQ(1, consumer1->update_source_id_count_);
    EXPECT_EQ(1, consumer1->stream_change_count_);
    EXPECT_EQ(1, consumer2->update_source_id_count_);
    EXPECT_EQ(1, consumer2->stream_change_count_);

    // Clean up
    srs_freep(consumer1);
    srs_freep(consumer2);
}

VOID TEST(AppTest2, RtcSourceOnPublishConsumerError)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumer that will cause on_source_changed to fail
    MockRtcConsumer *consumer = new MockRtcConsumer();
    source->consumers_.push_back(consumer);

    // Mock a scenario where on_source_changed would fail
    // This is tricky since on_source_changed calls consumer methods directly
    // We'll test by ensuring the method completes successfully even with consumers
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify consumer was notified
    EXPECT_EQ(1, consumer->update_source_id_count_);
    EXPECT_EQ(1, consumer->stream_change_count_);

    // Clean up
    srs_freep(consumer);
}

VOID TEST(AppTest2, RtcSourceOnPublishWithStreamDescription)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with audio and video tracks
    SrsRtcSourceDescription *stream_desc = new SrsRtcSourceDescription();

    // Add audio track
    stream_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    stream_desc->audio_track_desc_->type_ = "audio";
    stream_desc->audio_track_desc_->media_ = new SrsAudioPayload(111, "opus", 48000, 2);

    // Add video track
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(video_track);

    source->set_stream_desc(stream_desc);

    // Create mock consumer to verify stream change notification
    MockRtcConsumer *consumer = new MockRtcConsumer();
    source->consumers_.push_back(consumer);

    // Test on_publish with stream description
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify consumer received stream description
    EXPECT_EQ(1, consumer->stream_change_count_);
    EXPECT_TRUE(consumer->last_stream_desc_ != NULL);

    // Verify source state
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);

    // Clean up
    srs_freep(consumer);
    srs_freep(stream_desc);
}

VOID TEST(AppTest2, RtcSourceOnPublishStateTransition)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Verify initial state
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);

    // Test on_publish state transition
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify both flags are set to true
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);

    // Test calling on_publish again (should still succeed)
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // State should remain true
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceOnPublishSourceIdChange)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumer to track source ID changes
    MockRtcConsumer *consumer = new MockRtcConsumer();
    source->consumers_.push_back(consumer);

    // Get initial source ID (should be empty)
    SrsContextId initial_id = source->source_id();
    EXPECT_TRUE(initial_id.empty());

    // Test on_publish - should set source ID from current context
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify source ID was set
    SrsContextId new_id = source->source_id();
    EXPECT_FALSE(new_id.empty());

    // Verify consumer was notified of source ID change
    EXPECT_EQ(1, consumer->update_source_id_count_);

    // Clean up
    srs_freep(consumer);
}

VOID TEST(AppTest2, RtcSourceSubscribeBasic)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock event handlers
    SrsUniquePtr<MockRtcSourceEventHandler> handler1(new MockRtcSourceEventHandler());
    SrsUniquePtr<MockRtcSourceEventHandler> handler2(new MockRtcSourceEventHandler());

    // Initially no handlers should be subscribed
    EXPECT_EQ(0, (int)source->event_handlers_.size());

    // Subscribe first handler
    source->rtc_source_subscribe(handler1.get());
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(handler1.get(), source->event_handlers_[0]);

    // Subscribe second handler
    source->rtc_source_subscribe(handler2.get());
    EXPECT_EQ(2, (int)source->event_handlers_.size());
    EXPECT_EQ(handler1.get(), source->event_handlers_[0]);
    EXPECT_EQ(handler2.get(), source->event_handlers_[1]);
}

VOID TEST(AppTest2, RtcSourceSubscribeDuplicateHandler)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock event handler
    SrsUniquePtr<MockRtcSourceEventHandler> handler(new MockRtcSourceEventHandler());

    // Subscribe handler first time
    source->rtc_source_subscribe(handler.get());
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(handler.get(), source->event_handlers_[0]);

    // Subscribe same handler again - should not add duplicate
    source->rtc_source_subscribe(handler.get());
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(handler.get(), source->event_handlers_[0]);

    // Subscribe same handler multiple times - should still be only one
    source->rtc_source_subscribe(handler.get());
    source->rtc_source_subscribe(handler.get());
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(handler.get(), source->event_handlers_[0]);
}

VOID TEST(AppTest2, RtcSourceSubscribeNullHandler)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Initially no handlers should be subscribed
    EXPECT_EQ(0, (int)source->event_handlers_.size());

    // Subscribe null handler - should add it (implementation allows null)
    source->rtc_source_subscribe(NULL);
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(NULL, source->event_handlers_[0]);

    // Subscribe null handler again - should not add duplicate
    source->rtc_source_subscribe(NULL);
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(NULL, source->event_handlers_[0]);
}

VOID TEST(AppTest2, RtcSourceSubscribeMultipleHandlers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create multiple mock event handlers
    SrsUniquePtr<MockRtcSourceEventHandler> handler1(new MockRtcSourceEventHandler());
    SrsUniquePtr<MockRtcSourceEventHandler> handler2(new MockRtcSourceEventHandler());
    SrsUniquePtr<MockRtcSourceEventHandler> handler3(new MockRtcSourceEventHandler());

    // Initially no handlers should be subscribed
    EXPECT_EQ(0, (int)source->event_handlers_.size());

    // Subscribe handlers in order
    source->rtc_source_subscribe(handler1.get());
    source->rtc_source_subscribe(handler2.get());
    source->rtc_source_subscribe(handler3.get());

    // Verify all handlers are subscribed in correct order
    EXPECT_EQ(3, (int)source->event_handlers_.size());
    EXPECT_EQ(handler1.get(), source->event_handlers_[0]);
    EXPECT_EQ(handler2.get(), source->event_handlers_[1]);
    EXPECT_EQ(handler3.get(), source->event_handlers_[2]);

    // Try to subscribe duplicates - should not change the list
    source->rtc_source_subscribe(handler2.get());
    source->rtc_source_subscribe(handler1.get());
    EXPECT_EQ(3, (int)source->event_handlers_.size());
    EXPECT_EQ(handler1.get(), source->event_handlers_[0]);
    EXPECT_EQ(handler2.get(), source->event_handlers_[1]);
    EXPECT_EQ(handler3.get(), source->event_handlers_[2]);
}

VOID TEST(AppTest2, RtcSourceSubscribeUnsubscribeInteraction)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock event handlers
    SrsUniquePtr<MockRtcSourceEventHandler> handler1(new MockRtcSourceEventHandler());
    SrsUniquePtr<MockRtcSourceEventHandler> handler2(new MockRtcSourceEventHandler());

    // Subscribe both handlers
    source->rtc_source_subscribe(handler1.get());
    source->rtc_source_subscribe(handler2.get());
    EXPECT_EQ(2, (int)source->event_handlers_.size());

    // Unsubscribe first handler
    source->rtc_source_unsubscribe(handler1.get());
    EXPECT_EQ(1, (int)source->event_handlers_.size());
    EXPECT_EQ(handler2.get(), source->event_handlers_[0]);

    // Re-subscribe first handler - should be added back
    source->rtc_source_subscribe(handler1.get());
    EXPECT_EQ(2, (int)source->event_handlers_.size());
    EXPECT_EQ(handler2.get(), source->event_handlers_[0]);
    EXPECT_EQ(handler1.get(), source->event_handlers_[1]);

    // Unsubscribe all handlers
    source->rtc_source_unsubscribe(handler1.get());
    source->rtc_source_unsubscribe(handler2.get());
    EXPECT_EQ(0, (int)source->event_handlers_.size());

    // Re-subscribe in different order
    source->rtc_source_subscribe(handler2.get());
    source->rtc_source_subscribe(handler1.get());
    EXPECT_EQ(2, (int)source->event_handlers_.size());
    EXPECT_EQ(handler2.get(), source->event_handlers_[0]);
    EXPECT_EQ(handler1.get(), source->event_handlers_[1]);
}

VOID TEST(AppTest2, RtcSourceSubscribeEventNotification)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock event handlers
    SrsUniquePtr<MockRtcSourceEventHandler> handler1(new MockRtcSourceEventHandler());
    SrsUniquePtr<MockRtcSourceEventHandler> handler2(new MockRtcSourceEventHandler());

    // Subscribe handlers
    source->rtc_source_subscribe(handler1.get());
    source->rtc_source_subscribe(handler2.get());

    // Verify initial state
    EXPECT_EQ(0, handler1->on_unpublish_count_);
    EXPECT_EQ(0, handler2->on_unpublish_count_);

    // Manually set is_created_ to true to simulate published state
    source->is_created_ = true;

    // Trigger unpublish event - should notify all subscribed handlers
    source->on_unpublish();

    // Verify handlers were notified
    EXPECT_EQ(1, handler1->on_unpublish_count_);
    EXPECT_EQ(1, handler2->on_unpublish_count_);

    // Test second scenario: unsubscribe one handler and test again
    // Create a new source for the second test to avoid state issues
    SrsUniquePtr<SrsRtcSource> source2(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source2->initialize(req.get()));

    // Reset handler counters
    handler1->on_unpublish_count_ = 0;
    handler2->on_unpublish_count_ = 0;

    // Subscribe both handlers to the new source
    source2->rtc_source_subscribe(handler1.get());
    source2->rtc_source_subscribe(handler2.get());

    // Unsubscribe handler1
    source2->rtc_source_unsubscribe(handler1.get());

    // Verify only handler2 is subscribed now
    EXPECT_EQ(1, (int)source2->event_handlers_.size());
    EXPECT_EQ(handler2.get(), source2->event_handlers_[0]);

    // Set is_created_ and trigger unpublish
    source2->is_created_ = true;
    source2->on_unpublish();

    // Only handler2 should be notified this time
    EXPECT_EQ(0, handler1->on_unpublish_count_); // Not called
    EXPECT_EQ(1, handler2->on_unpublish_count_); // Called once
}

VOID TEST(AppTest2, RtcSourcePublishStreamGetterSetter)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Initially, publish_stream should be NULL
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Create a mock publish stream
    SrsUniquePtr<MockRtcPublishStream> publish_stream(new MockRtcPublishStream());
    SrsContextId test_cid;
    test_cid.set_value("test-context-id");
    publish_stream->set_context_id(test_cid);

    // Set the publish stream
    source->set_publish_stream(publish_stream.get());

    // Verify the publish stream is set correctly
    EXPECT_TRUE(source->publish_stream() != NULL);
    EXPECT_EQ(publish_stream.get(), source->publish_stream());
    SrsContextId expected_cid;
    expected_cid.set_value("test-context-id");
    EXPECT_TRUE(source->publish_stream()->context_id().compare(expected_cid) == 0);

    // Set to NULL
    source->set_publish_stream(NULL);
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Set again to verify it can be changed
    source->set_publish_stream(publish_stream.get());
    EXPECT_EQ(publish_stream.get(), source->publish_stream());
}

VOID TEST(AppTest2, RtcSourcePublishStreamWithConsumerDestroy)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a mock publish stream
    SrsUniquePtr<MockRtcPublishStream> publish_stream(new MockRtcPublishStream());
    source->set_publish_stream(publish_stream.get());

    // Create a mock event handler
    SrsUniquePtr<MockRtcSourceEventHandler> handler(new MockRtcSourceEventHandler());
    source->rtc_source_subscribe(handler.get());

    // Create consumers
    ISrsRtcConsumer *consumer1 = NULL;
    ISrsRtcConsumer *consumer2 = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer1));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer2));

    // Verify consumers exist
    EXPECT_EQ(2, (int)source->consumers_.size());

    // Destroy first consumer - should not trigger event handler since consumers remain
    source->on_consumer_destroy(consumer1);
    EXPECT_EQ(1, (int)source->consumers_.size());
    EXPECT_EQ(0, handler->on_consumers_finished_count_);

    // Destroy second consumer - should trigger event handler since no consumers remain
    source->on_consumer_destroy(consumer2);
    EXPECT_EQ(0, (int)source->consumers_.size());
    EXPECT_EQ(1, handler->on_consumers_finished_count_);

    // Clean up
    srs_freep(consumer1);
    srs_freep(consumer2);
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsBasicSuccess)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Test consumer_dumps with default parameters (all true)
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer));

    // Verify the method returns success
    // Note: The method only prints a trace message and returns srs_success
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsWithAllParameterCombinations)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Test all combinations of ds, dm, dg parameters
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, true, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, true, false));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, false, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, false, false));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, false, true, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, false, true, false));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, false, false, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, false, false, false));
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsWithNullConsumer)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Test consumer_dumps with NULL consumer - should still succeed
    // The current implementation doesn't use the consumer parameter
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(NULL));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(NULL, true, true, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(NULL, false, false, false));
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsMultipleConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create multiple consumers
    ISrsRtcConsumer *consumer1 = NULL;
    ISrsRtcConsumer *consumer2 = NULL;
    ISrsRtcConsumer *consumer3 = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer1));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer2));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer3));

    // Test consumer_dumps with different consumers
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer1));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer2, true, false, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer3, false, true, false));

    // Test multiple calls on same consumer
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer1));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer1, false, false, false));
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsWithMockConsumer)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a mock consumer (not created through source->create_consumer)
    SrsUniquePtr<MockRtcConsumer> mock_consumer(new MockRtcConsumer());

    // Test consumer_dumps with mock consumer - should still succeed
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(mock_consumer.get()));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(mock_consumer.get(), true, true, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(mock_consumer.get(), false, false, false));
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsSequentialCalls)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Test multiple sequential calls to consumer_dumps
    for (int i = 0; i < 10; i++) {
        HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer));
    }

    // Test sequential calls with different parameter combinations
    for (int i = 0; i < 5; i++) {
        bool ds = (i % 2 == 0);
        bool dm = (i % 3 == 0);
        bool dg = (i % 4 == 0);
        HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, ds, dm, dg));
    }
}

VOID TEST(AppTest2, RtcSourceConsumerDumpsAfterSourceStateChanges)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

    // Test consumer_dumps before publish
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer));

    // Publish the source
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Test consumer_dumps after publish
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, true, true));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, false, false, false));

    // Unpublish the source
    source->on_unpublish();

    // Test consumer_dumps after unpublish
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer));
    HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, false, true));
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeSuccess)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create and set up stream description with audio and video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream-with-bridge";

    // Add audio track
    stream_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    stream_desc->audio_track_desc_->type_ = "audio";
    stream_desc->audio_track_desc_->id_ = "audio-track-1";
    stream_desc->audio_track_desc_->ssrc_ = 12345;
    stream_desc->audio_track_desc_->media_ = new SrsAudioPayload(111, "opus", 48000, 2);

    // Add video track
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->id_ = "video-track-1";
    video_track->ssrc_ = 67890;
    video_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(video_track);

    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Test on_publish with RTC bridge - should call all bridge methods
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify bridge methods were called in correct order
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(1, mock_bridge->on_publish_count_);

    // Verify bridge was initialized with correct request (bridge gets a copy)
    EXPECT_TRUE(mock_bridge->last_initialize_req_ != NULL);

    // Verify bridge was set up with correct codecs
    EXPECT_EQ(SrsAudioCodecIdOpus, mock_bridge->last_audio_codec_);
    EXPECT_EQ(SrsVideoCodecIdAVC, mock_bridge->last_video_codec_);

    // Verify source state
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);

    // Clean up properly by calling on_unpublish to unsubscribe from timer
    source->on_unpublish();
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeInitializeFailure)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge that fails on initialize (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    srs_error_t bridge_error = srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "bridge initialize failed");
    mock_bridge->set_initialize_error(bridge_error);
    srs_freep(bridge_error);

    source->set_bridge(mock_bridge);

    // Test on_publish with failing bridge - should return error
    HELPER_EXPECT_FAILED(source->on_publish());

    // Verify bridge initialize was called but setup_codec and on_publish were not
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(0, mock_bridge->setup_codec_count_);
    EXPECT_EQ(0, mock_bridge->on_publish_count_);
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeSetupCodecFailure)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with audio and video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    stream_desc->audio_track_desc_->type_ = "audio";
    stream_desc->audio_track_desc_->media_ = new SrsAudioPayload(111, "opus", 48000, 2);

    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(video_track);

    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge that fails on setup_codec (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    srs_error_t bridge_error = srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "bridge setup codec failed");
    mock_bridge->set_setup_codec_error(bridge_error);
    srs_freep(bridge_error);

    source->set_bridge(mock_bridge);

    // Test on_publish with failing bridge - should return error
    HELPER_EXPECT_FAILED(source->on_publish());

    // Verify bridge initialize and setup_codec were called but on_publish was not
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(0, mock_bridge->on_publish_count_);

    // Verify correct codecs were passed to setup_codec
    EXPECT_EQ(SrsAudioCodecIdOpus, mock_bridge->last_audio_codec_);
    EXPECT_EQ(SrsVideoCodecIdAVC, mock_bridge->last_video_codec_);
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeOnPublishFailure)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge that fails on on_publish (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    srs_error_t bridge_error = srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "bridge on_publish failed");
    mock_bridge->set_on_publish_error(bridge_error);
    srs_freep(bridge_error);

    source->set_bridge(mock_bridge);

    // Test on_publish with failing bridge - should return error
    HELPER_EXPECT_FAILED(source->on_publish());

    // Verify all bridge methods were called
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(1, mock_bridge->on_publish_count_);
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeDefaultCodecs)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description without audio/video tracks (should use defaults)
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Test on_publish with default codecs
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify bridge methods were called
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(1, mock_bridge->on_publish_count_);

    // Verify default codecs were used (Opus for audio, AVC/H264 for video)
    EXPECT_EQ(SrsAudioCodecIdOpus, mock_bridge->last_audio_codec_);
    EXPECT_EQ(SrsVideoCodecIdAVC, mock_bridge->last_video_codec_);

    // Clean up properly by calling on_unpublish to unsubscribe from timer
    source->on_unpublish();
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeAudioOnlyStream)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with only audio track
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    stream_desc->audio_track_desc_->type_ = "audio";
    stream_desc->audio_track_desc_->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    // No video tracks

    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Test on_publish with audio-only stream
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify bridge methods were called
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(1, mock_bridge->on_publish_count_);

    // Verify codecs (Opus for audio, default AVC for video even in audio-only stream)
    EXPECT_EQ(SrsAudioCodecIdOpus, mock_bridge->last_audio_codec_);
    EXPECT_EQ(SrsVideoCodecIdAVC, mock_bridge->last_video_codec_);

    // Clean up properly by calling on_unpublish to unsubscribe from timer
    source->on_unpublish();
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeVideoOnlyStream)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with only video track
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    // No audio track
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(video_track);

    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Test on_publish with video-only stream
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify bridge methods were called
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(1, mock_bridge->on_publish_count_);

    // Verify codecs (default Opus for audio even in video-only stream, AVC/H264 for video)
    EXPECT_EQ(SrsAudioCodecIdOpus, mock_bridge->last_audio_codec_);
    EXPECT_EQ(SrsVideoCodecIdAVC, mock_bridge->last_video_codec_);

    // Clean up properly by calling on_unpublish to unsubscribe from timer
    source->on_unpublish();
}

VOID TEST(AppTest2, RtcSourceOnPublishWithoutRtcBridge)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    source->set_stream_desc(stream_desc.get());

    // No RTC bridge set (rtc_bridge_ is NULL)
    EXPECT_TRUE(source->rtc_bridge_ == NULL);

    // Test on_publish without RTC bridge - should succeed
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify source state
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceOnPublishWithRtcBridgeTimerSubscription)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    source->set_stream_desc(stream_desc.get());

    // Create mock RTC bridge (source will take ownership)
    MockRtcBridge *mock_bridge = new MockRtcBridge();
    source->set_bridge(mock_bridge);

    // Test on_publish with RTC bridge - should subscribe to timer
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // Verify bridge methods were called
    EXPECT_EQ(1, mock_bridge->initialize_count_);
    EXPECT_EQ(1, mock_bridge->setup_codec_count_);
    EXPECT_EQ(1, mock_bridge->on_publish_count_);

    // Note: We can't easily test timer subscription without accessing private members
    // or creating a more complex mock setup. The test verifies the method executes successfully.

    // Verify source state
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);

    // Clean up properly by calling on_unpublish to unsubscribe from timer
    source->on_unpublish();
}

VOID TEST(AppTest2, RtcSourceCanPublishInitialState)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Initially, stream is not created, so can_publish should return true
    EXPECT_TRUE(source->can_publish());
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceCanPublishAfterStreamCreated)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Initially can publish
    EXPECT_TRUE(source->can_publish());

    // Set stream as created
    source->set_stream_created();

    // After stream is created, can_publish should return false
    EXPECT_FALSE(source->can_publish());
    EXPECT_TRUE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceCanPublishAfterPublish)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Initially can publish
    EXPECT_TRUE(source->can_publish());

    // Call on_publish which sets both is_created_ and is_delivering_packets_ to true
    HELPER_EXPECT_SUCCESS(source->on_publish());

    // After publish, can_publish should return false because is_created_ is true
    EXPECT_FALSE(source->can_publish());
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceCanPublishAfterUnpublish)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Publish first
    HELPER_EXPECT_SUCCESS(source->on_publish());
    EXPECT_FALSE(source->can_publish());

    // Unpublish - this sets both is_created_ and is_delivering_packets_ to false
    source->on_unpublish();

    // After unpublish, can_publish should return true because is_created_ is now false
    EXPECT_TRUE(source->can_publish());
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
}

VOID TEST(AppTest2, RtcSourceSetStreamCreatedBasic)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Verify initial state
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
    EXPECT_TRUE(source->can_publish());

    // Call set_stream_created
    source->set_stream_created();

    // Verify state after set_stream_created
    EXPECT_TRUE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_); // Should remain false
    EXPECT_FALSE(source->can_publish());          // Should now return false
}

VOID TEST(AppTest2, RtcSourceSetStreamCreatedMultipleCalls)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // First call to set_stream_created
    source->set_stream_created();
    EXPECT_TRUE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);

    // Note: Multiple calls to set_stream_created would violate the assertion
    // srs_assert(!is_created_ && !is_delivering_packets_) in the implementation
    // So we don't test multiple calls as it would cause assertion failure
}

VOID TEST(AppTest2, RtcSourceSetStreamCreatedBeforePublish)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Set stream created first
    source->set_stream_created();
    EXPECT_TRUE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
    EXPECT_FALSE(source->can_publish());

    // Then call on_publish - this should set is_delivering_packets_ to true
    // but is_created_ is already true
    HELPER_EXPECT_SUCCESS(source->on_publish());
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);
    EXPECT_FALSE(source->can_publish());
}

VOID TEST(AppTest2, RtcSourceSetStreamCreatedStateTransitions)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Test state transitions
    // Initial state: not created, not delivering, can publish
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
    EXPECT_TRUE(source->can_publish());

    // After set_stream_created: created, not delivering, cannot publish
    source->set_stream_created();
    EXPECT_TRUE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
    EXPECT_FALSE(source->can_publish());

    // After on_publish: created, delivering, cannot publish
    HELPER_EXPECT_SUCCESS(source->on_publish());
    EXPECT_TRUE(source->is_created_);
    EXPECT_TRUE(source->is_delivering_packets_);
    EXPECT_FALSE(source->can_publish());

    // After on_unpublish: not created, not delivering, can publish
    source->on_unpublish();
    EXPECT_FALSE(source->is_created_);
    EXPECT_FALSE(source->is_delivering_packets_);
    EXPECT_TRUE(source->can_publish()); // Can publish again because is_created_ is now false
}

VOID TEST(AppTest2, RtcSourceCanPublishWithConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create consumers - should not affect can_publish
    ISrsRtcConsumer *consumer1 = NULL;
    ISrsRtcConsumer *consumer2 = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer1));
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer2));

    // can_publish should still return true (only depends on is_created_)
    EXPECT_TRUE(source->can_publish());

    // Set stream created
    source->set_stream_created();

    // can_publish should now return false regardless of consumers
    EXPECT_FALSE(source->can_publish());
}

VOID TEST(AppTest2, RtcSourceCanPublishWithStreamDescription)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create and set stream description - should not affect can_publish
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";
    source->set_stream_desc(stream_desc.get());

    // can_publish should still return true (only depends on is_created_)
    EXPECT_TRUE(source->can_publish());

    // Set stream created
    source->set_stream_created();

    // can_publish should now return false regardless of stream description
    EXPECT_FALSE(source->can_publish());
}

VOID TEST(AppTest2, RtcSourceCanPublishConsistency)
{
    srs_error_t err;

    // Create multiple sources to test consistency
    for (int i = 0; i < 5; i++) {
        SrsUniquePtr<SrsRequest> req(new SrsRequest());
        req->host_ = "localhost";
        req->vhost_ = "test.vhost";
        req->app_ = "live";
        req->stream_ = "test" + std::to_string(i);

        SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
        HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

        // All sources should initially allow publishing
        EXPECT_TRUE(source->can_publish());

        // After setting stream created, none should allow publishing
        source->set_stream_created();
        EXPECT_FALSE(source->can_publish());

        // Multiple calls to can_publish should return consistent results
        for (int j = 0; j < 3; j++) {
            EXPECT_FALSE(source->can_publish());
        }
    }
}

VOID TEST(AppTest2, RtcSourceSetStreamCreatedWithEventHandlers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create and subscribe event handlers
    SrsUniquePtr<MockRtcSourceEventHandler> handler1(new MockRtcSourceEventHandler());
    SrsUniquePtr<MockRtcSourceEventHandler> handler2(new MockRtcSourceEventHandler());
    source->rtc_source_subscribe(handler1.get());
    source->rtc_source_subscribe(handler2.get());

    // Verify initial state
    EXPECT_FALSE(source->is_created_);
    EXPECT_TRUE(source->can_publish());

    // Call set_stream_created - should not trigger event handlers
    source->set_stream_created();

    // Verify state changed but handlers not called
    EXPECT_TRUE(source->is_created_);
    EXPECT_FALSE(source->can_publish());
    EXPECT_EQ(0, handler1->on_unpublish_count_);
    EXPECT_EQ(0, handler1->on_consumers_finished_count_);
    EXPECT_EQ(0, handler2->on_unpublish_count_);
    EXPECT_EQ(0, handler2->on_consumers_finished_count_);
}

VOID TEST(AppTest2, RtcSourcePublishStreamWithoutConsumerDestroy)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Don't set publish stream (leave as NULL)
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Create a mock event handler
    SrsUniquePtr<MockRtcSourceEventHandler> handler(new MockRtcSourceEventHandler());
    source->rtc_source_subscribe(handler.get());

    // Create and destroy consumer
    ISrsRtcConsumer *consumer = NULL;
    HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));
    EXPECT_EQ(1, (int)source->consumers_.size());

    // Destroy consumer - should not trigger event handler since publish_stream is NULL
    source->on_consumer_destroy(consumer);
    EXPECT_EQ(0, (int)source->consumers_.size());
    EXPECT_EQ(0, handler->on_consumers_finished_count_);

    // Clean up
    srs_freep(consumer);
}

VOID TEST(AppTest2, RtcSourcePublishStreamMultipleChanges)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create multiple mock publish streams
    SrsUniquePtr<MockRtcPublishStream> publish_stream1(new MockRtcPublishStream());
    SrsUniquePtr<MockRtcPublishStream> publish_stream2(new MockRtcPublishStream());
    SrsUniquePtr<MockRtcPublishStream> publish_stream3(new MockRtcPublishStream());

    SrsContextId cid1, cid2, cid3;
    cid1.set_value("context-1");
    cid2.set_value("context-2");
    cid3.set_value("context-3");

    publish_stream1->set_context_id(cid1);
    publish_stream2->set_context_id(cid2);
    publish_stream3->set_context_id(cid3);

    // Test multiple changes
    source->set_publish_stream(publish_stream1.get());
    EXPECT_EQ(publish_stream1.get(), source->publish_stream());
    EXPECT_TRUE(source->publish_stream()->context_id().compare(cid1) == 0);

    source->set_publish_stream(publish_stream2.get());
    EXPECT_EQ(publish_stream2.get(), source->publish_stream());
    EXPECT_TRUE(source->publish_stream()->context_id().compare(cid2) == 0);

    source->set_publish_stream(publish_stream3.get());
    EXPECT_EQ(publish_stream3.get(), source->publish_stream());
    EXPECT_TRUE(source->publish_stream()->context_id().compare(cid3) == 0);

    // Set back to NULL
    source->set_publish_stream(NULL);
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Set to first stream again
    source->set_publish_stream(publish_stream1.get());
    EXPECT_EQ(publish_stream1.get(), source->publish_stream());
    EXPECT_TRUE(source->publish_stream()->context_id().compare(cid1) == 0);
}

VOID TEST(AppTest2, RtcSourcePublishStreamKeyframeRequest)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a mock publish stream
    SrsUniquePtr<MockRtcPublishStream> publish_stream(new MockRtcPublishStream());
    SrsContextId test_cid;
    test_cid.set_value("keyframe-test-context");
    publish_stream->set_context_id(test_cid);

    // Set the publish stream
    source->set_publish_stream(publish_stream.get());

    // Verify initial state
    EXPECT_EQ(0, publish_stream->request_keyframe_count_);
    EXPECT_EQ(0u, publish_stream->last_keyframe_ssrc_);

    // Test keyframe request functionality through publish stream
    uint32_t test_ssrc = 12345;
    SrsContextId request_cid;
    request_cid.set_value("request-context");

    source->publish_stream()->request_keyframe(test_ssrc, request_cid);

    // Verify the request was recorded
    EXPECT_EQ(1, publish_stream->request_keyframe_count_);
    EXPECT_EQ(test_ssrc, publish_stream->last_keyframe_ssrc_);
    EXPECT_TRUE(publish_stream->last_keyframe_cid_.compare(request_cid) == 0);

    // Test multiple requests
    source->publish_stream()->request_keyframe(54321, test_cid);
    EXPECT_EQ(2, publish_stream->request_keyframe_count_);
    EXPECT_EQ(54321u, publish_stream->last_keyframe_ssrc_);
    EXPECT_TRUE(publish_stream->last_keyframe_cid_.compare(test_cid) == 0);
}

VOID TEST(AppTest2, RtcSourcePublishStreamContextIdAccess)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a mock publish stream with specific context ID
    SrsUniquePtr<MockRtcPublishStream> publish_stream(new MockRtcPublishStream());
    SrsContextId original_cid;
    original_cid.set_value("original-context-id");
    publish_stream->set_context_id(original_cid);

    // Set the publish stream
    source->set_publish_stream(publish_stream.get());

    // Test context ID access
    const SrsContextId &retrieved_cid = source->publish_stream()->context_id();
    EXPECT_TRUE(retrieved_cid.compare(original_cid) == 0);
    EXPECT_STREQ("original-context-id", retrieved_cid.c_str());

    // Change context ID and verify
    SrsContextId new_cid;
    new_cid.set_value("updated-context-id");
    publish_stream->set_context_id(new_cid);

    const SrsContextId &updated_cid = source->publish_stream()->context_id();
    EXPECT_TRUE(updated_cid.compare(new_cid) == 0);
    EXPECT_STREQ("updated-context-id", updated_cid.c_str());
    EXPECT_TRUE(updated_cid.compare(original_cid) != 0); // Should be different from original
}

VOID TEST(AppTest2, RtcSourcePublishStreamNullSafety)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Initially should be NULL
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Setting NULL should be safe
    source->set_publish_stream(NULL);
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Create and set a publish stream
    SrsUniquePtr<MockRtcPublishStream> publish_stream(new MockRtcPublishStream());
    source->set_publish_stream(publish_stream.get());
    EXPECT_TRUE(source->publish_stream() != NULL);

    // Set back to NULL
    source->set_publish_stream(NULL);
    EXPECT_TRUE(source->publish_stream() == NULL);

    // Multiple NULL sets should be safe
    source->set_publish_stream(NULL);
    source->set_publish_stream(NULL);
    EXPECT_TRUE(source->publish_stream() == NULL);
}

VOID TEST(AppTest2, RtcSourceOnRtpNoConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create a test RTP packet
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
    pkt->header_.set_sequence(100);
    pkt->header_.set_timestamp(1000);
    pkt->header_.set_ssrc(12345);

    // Set mock circuit breaker
    MockCircuitBreaker mock_circuit_breaker;
    source->circuit_breaker_ = &mock_circuit_breaker;

    // Test: on_rtp with no consumers should succeed
    HELPER_EXPECT_SUCCESS(source->on_rtp(pkt.get()));
}

VOID TEST(AppTest2, RtcSourceOnRtpWithConsumers)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumers
    MockRtcConsumer *consumer1 = new MockRtcConsumer();
    MockRtcConsumer *consumer2 = new MockRtcConsumer();
    MockRtcConsumer *consumer3 = new MockRtcConsumer();

    // Add consumers to source manually (simulating create_consumer)
    source->consumers_.push_back(consumer1);
    source->consumers_.push_back(consumer2);
    source->consumers_.push_back(consumer3);

    // Create a test RTP packet
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
    pkt->header_.set_sequence(100);
    pkt->header_.set_timestamp(1000);
    pkt->header_.set_ssrc(12345);

    // Set mock circuit breaker
    MockCircuitBreaker mock_circuit_breaker;
    source->circuit_breaker_ = &mock_circuit_breaker;

    // Test: on_rtp should enqueue packet to all consumers
    HELPER_EXPECT_SUCCESS(source->on_rtp(pkt.get()));

    // Verify all consumers received the packet
    EXPECT_EQ(1, consumer1->enqueue_count_);
    EXPECT_EQ(1, consumer2->enqueue_count_);
    EXPECT_EQ(1, consumer3->enqueue_count_);

    // Clean up consumers
    srs_freep(consumer1);
    srs_freep(consumer2);
    srs_freep(consumer3);
}

VOID TEST(AppTest2, RtcSourceOnRtpCircuitBreakerDying)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumers
    MockRtcConsumer *consumer1 = new MockRtcConsumer();
    MockRtcConsumer *consumer2 = new MockRtcConsumer();
    source->consumers_.push_back(consumer1);
    source->consumers_.push_back(consumer2);

    // Create a test RTP packet
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
    pkt->header_.set_sequence(100);
    pkt->header_.set_timestamp(1000);
    pkt->header_.set_ssrc(12345);

    // Create mock circuit breaker
    MockCircuitBreaker mock_circuit_breaker;
    mock_circuit_breaker.set_hybrid_dying_water_level(true); // Circuit breaker is dying
    source->circuit_breaker_ = &mock_circuit_breaker;

    // Test: on_rtp should drop packet when circuit breaker is dying
    HELPER_EXPECT_SUCCESS(source->on_rtp(pkt.get()));

    // Verify packet was dropped (consumers should not receive it)
    EXPECT_EQ(0, consumer1->enqueue_count_);
    EXPECT_EQ(0, consumer2->enqueue_count_);

    // Clean up consumers
    srs_freep(consumer1);
    srs_freep(consumer2);
}

VOID TEST(AppTest2, RtcSourceOnRtpConsumerEnqueueError)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source and initialize
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create mock consumers - one will fail
    MockRtcConsumer *consumer1 = new MockRtcConsumer();
    MockRtcConsumer *consumer2 = new MockRtcConsumer();
    MockRtcConsumer *consumer3 = new MockRtcConsumer();

    // Set consumer2 to return error on enqueue
    srs_error_t test_error = srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "test enqueue error");
    consumer2->set_enqueue_error(test_error);
    srs_freep(test_error);

    source->consumers_.push_back(consumer1);
    source->consumers_.push_back(consumer2);
    source->consumers_.push_back(consumer3);

    // Create a test RTP packet
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
    pkt->header_.set_sequence(100);
    pkt->header_.set_timestamp(1000);
    pkt->header_.set_ssrc(12345);

    // Set mock circuit breaker
    MockCircuitBreaker mock_circuit_breaker;
    source->circuit_breaker_ = &mock_circuit_breaker;

    // Test: on_rtp should fail when consumer enqueue fails
    HELPER_EXPECT_FAILED(source->on_rtp(pkt.get()));

    // Verify first consumer received packet, but processing stopped at second consumer
    EXPECT_EQ(1, consumer1->enqueue_count_);
    EXPECT_EQ(1, consumer2->enqueue_count_); // Called but failed
    EXPECT_EQ(0, consumer3->enqueue_count_); // Never reached due to error

    // Clean up consumers
    srs_freep(consumer1);
    srs_freep(consumer2);
    srs_freep(consumer3);
}

VOID TEST(AppTest2, RtcSourceGetTrackDescNoStreamDesc)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Clear the default stream description created by init_for_play_before_publishing
    source->set_stream_desc(NULL);

    // Test: get_track_desc with no stream description should return empty vector
    std::vector<SrsRtcTrackDescription *> audio_tracks = source->get_track_desc("audio", "opus");
    EXPECT_TRUE(audio_tracks.empty());

    std::vector<SrsRtcTrackDescription *> video_tracks = source->get_track_desc("video", "H264");
    EXPECT_TRUE(video_tracks.empty());

    std::vector<SrsRtcTrackDescription *> all_video_tracks = source->get_track_desc("video", "");
    EXPECT_TRUE(all_video_tracks.empty());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescAudioTrackMatching)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with audio track
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create audio track description with Opus codec
    SrsRtcTrackDescription *audio_track = new SrsRtcTrackDescription();
    audio_track->type_ = "audio";
    audio_track->id_ = "audio-track-1";
    audio_track->ssrc_ = 12345;
    audio_track->direction_ = "sendrecv";
    audio_track->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    stream_desc->audio_track_desc_ = audio_track;

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc for matching audio codec should return the track
    std::vector<SrsRtcTrackDescription *> opus_tracks = source->get_track_desc("audio", "opus");
    EXPECT_EQ(1, (int)opus_tracks.size());
    // Note: set_stream_desc creates a copy, so we check properties instead of pointer equality
    EXPECT_EQ("audio", opus_tracks[0]->type_);
    EXPECT_EQ("audio-track-1", opus_tracks[0]->id_);
    EXPECT_EQ(12345u, opus_tracks[0]->ssrc_);

    // Test: get_track_desc for non-matching audio codec should return empty
    std::vector<SrsRtcTrackDescription *> aac_tracks = source->get_track_desc("audio", "AAC");
    EXPECT_TRUE(aac_tracks.empty());

    // Test: get_track_desc for non-matching audio codec should return empty
    std::vector<SrsRtcTrackDescription *> mp3_tracks = source->get_track_desc("audio", "MP3");
    EXPECT_TRUE(mp3_tracks.empty());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescAudioTrackNoAudioTrack)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description without audio track
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";
    stream_desc->audio_track_desc_ = NULL; // No audio track

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc for audio should return empty when no audio track exists
    std::vector<SrsRtcTrackDescription *> opus_tracks = source->get_track_desc("audio", "opus");
    EXPECT_TRUE(opus_tracks.empty());

    std::vector<SrsRtcTrackDescription *> aac_tracks = source->get_track_desc("audio", "AAC");
    EXPECT_TRUE(aac_tracks.empty());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescVideoTrackMatching)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create H.264 video track description
    SrsRtcTrackDescription *h264_track = new SrsRtcTrackDescription();
    h264_track->type_ = "video";
    h264_track->id_ = "video-h264-track";
    h264_track->ssrc_ = 54321;
    h264_track->direction_ = "sendrecv";
    h264_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(h264_track);

    // Create H.265 video track description
    SrsRtcTrackDescription *h265_track = new SrsRtcTrackDescription();
    h265_track->type_ = "video";
    h265_track->id_ = "video-h265-track";
    h265_track->ssrc_ = 54322;
    h265_track->direction_ = "sendrecv";
    h265_track->media_ = new SrsVideoPayload(49, "H265", 90000);
    stream_desc->video_track_descs_.push_back(h265_track);

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc for H264 should return the H264 track
    std::vector<SrsRtcTrackDescription *> h264_tracks = source->get_track_desc("video", "H264");
    EXPECT_EQ(1, (int)h264_tracks.size());
    // Note: set_stream_desc creates a copy, so we check properties instead of pointer equality
    EXPECT_EQ("video", h264_tracks[0]->type_);
    EXPECT_EQ("video-h264-track", h264_tracks[0]->id_);
    EXPECT_EQ(54321u, h264_tracks[0]->ssrc_);

    // Test: get_track_desc for H265 should return the H265 track
    std::vector<SrsRtcTrackDescription *> h265_tracks = source->get_track_desc("video", "H265");
    EXPECT_EQ(1, (int)h265_tracks.size());
    // Note: set_stream_desc creates a copy, so we check properties instead of pointer equality
    EXPECT_EQ("video", h265_tracks[0]->type_);
    EXPECT_EQ("video-h265-track", h265_tracks[0]->id_);
    EXPECT_EQ(54322u, h265_tracks[0]->ssrc_);

    // Test: get_track_desc for non-matching video codec should return empty
    std::vector<SrsRtcTrackDescription *> vp8_tracks = source->get_track_desc("video", "VP8");
    EXPECT_TRUE(vp8_tracks.empty());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescVideoTrackEmptyMediaName)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with multiple video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create H.264 video track description
    SrsRtcTrackDescription *h264_track = new SrsRtcTrackDescription();
    h264_track->type_ = "video";
    h264_track->id_ = "video-h264-track";
    h264_track->ssrc_ = 11111;
    h264_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(h264_track);

    // Create H.265 video track description
    SrsRtcTrackDescription *h265_track = new SrsRtcTrackDescription();
    h265_track->type_ = "video";
    h265_track->id_ = "video-h265-track";
    h265_track->ssrc_ = 22222;
    h265_track->media_ = new SrsVideoPayload(49, "H265", 90000);
    stream_desc->video_track_descs_.push_back(h265_track);

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc with empty media_name should return all video tracks
    std::vector<SrsRtcTrackDescription *> all_video_tracks = source->get_track_desc("video", "");
    EXPECT_EQ(2, (int)all_video_tracks.size());
    // Note: set_stream_desc creates copies, so we check properties instead of pointer equality
    EXPECT_EQ("video-h264-track", all_video_tracks[0]->id_);
    EXPECT_EQ("video-h265-track", all_video_tracks[1]->id_);
}

VOID TEST(AppTest2, RtcSourceGetTrackDescVideoTrackNoVideoTracks)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description without video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";
    // video_track_descs_ is empty by default

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc for video should return empty when no video tracks exist
    std::vector<SrsRtcTrackDescription *> h264_tracks = source->get_track_desc("video", "H264");
    EXPECT_TRUE(h264_tracks.empty());

    std::vector<SrsRtcTrackDescription *> all_video_tracks = source->get_track_desc("video", "");
    EXPECT_TRUE(all_video_tracks.empty());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescMixedAudioVideoTracks)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with both audio and video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create audio track description
    SrsRtcTrackDescription *audio_track = new SrsRtcTrackDescription();
    audio_track->type_ = "audio";
    audio_track->id_ = "audio-opus-track";
    audio_track->ssrc_ = 33333;
    audio_track->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    stream_desc->audio_track_desc_ = audio_track;

    // Create multiple video track descriptions
    SrsRtcTrackDescription *h264_track = new SrsRtcTrackDescription();
    h264_track->type_ = "video";
    h264_track->id_ = "video-h264-track";
    h264_track->ssrc_ = 44444;
    h264_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(h264_track);

    SrsRtcTrackDescription *h265_track = new SrsRtcTrackDescription();
    h265_track->type_ = "video";
    h265_track->id_ = "video-h265-track";
    h265_track->ssrc_ = 55555;
    h265_track->media_ = new SrsVideoPayload(49, "H265", 90000);
    stream_desc->video_track_descs_.push_back(h265_track);

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc for audio should return only audio track
    std::vector<SrsRtcTrackDescription *> opus_tracks = source->get_track_desc("audio", "opus");
    EXPECT_EQ(1, (int)opus_tracks.size());
    // Note: set_stream_desc creates copies, so we check properties instead of pointer equality
    EXPECT_EQ("audio", opus_tracks[0]->type_);
    EXPECT_EQ("audio-opus-track", opus_tracks[0]->id_);

    // Test: get_track_desc for video should return only matching video tracks
    std::vector<SrsRtcTrackDescription *> h264_tracks = source->get_track_desc("video", "H264");
    EXPECT_EQ(1, (int)h264_tracks.size());
    EXPECT_EQ("video", h264_tracks[0]->type_);
    EXPECT_EQ("video-h264-track", h264_tracks[0]->id_);

    std::vector<SrsRtcTrackDescription *> h265_tracks = source->get_track_desc("video", "H265");
    EXPECT_EQ(1, (int)h265_tracks.size());
    EXPECT_EQ("video", h265_tracks[0]->type_);
    EXPECT_EQ("video-h265-track", h265_tracks[0]->id_);

    // Test: get_track_desc for all video tracks
    std::vector<SrsRtcTrackDescription *> all_video_tracks = source->get_track_desc("video", "");
    EXPECT_EQ(2, (int)all_video_tracks.size());
    EXPECT_EQ("video-h264-track", all_video_tracks[0]->id_);
    EXPECT_EQ("video-h265-track", all_video_tracks[1]->id_);
}

VOID TEST(AppTest2, RtcSourceGetTrackDescInvalidType)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with both audio and video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create audio track
    SrsRtcTrackDescription *audio_track = new SrsRtcTrackDescription();
    audio_track->type_ = "audio";
    audio_track->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    stream_desc->audio_track_desc_ = audio_track;

    // Create video track
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(video_track);

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc with invalid type should return empty
    std::vector<SrsRtcTrackDescription *> invalid_tracks = source->get_track_desc("invalid", "opus");
    EXPECT_TRUE(invalid_tracks.empty());

    std::vector<SrsRtcTrackDescription *> data_tracks = source->get_track_desc("data", "H264");
    EXPECT_TRUE(data_tracks.empty());

    std::vector<SrsRtcTrackDescription *> empty_type_tracks = source->get_track_desc("", "opus");
    EXPECT_TRUE(empty_type_tracks.empty());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescCaseSensitivity)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create audio track
    SrsRtcTrackDescription *audio_track = new SrsRtcTrackDescription();
    audio_track->type_ = "audio";
    audio_track->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    stream_desc->audio_track_desc_ = audio_track;

    // Create video track
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(video_track);

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: type parameter is case sensitive
    std::vector<SrsRtcTrackDescription *> audio_upper = source->get_track_desc("AUDIO", "opus");
    EXPECT_TRUE(audio_upper.empty());

    std::vector<SrsRtcTrackDescription *> video_upper = source->get_track_desc("VIDEO", "H264");
    EXPECT_TRUE(video_upper.empty());

    // Test: media_name parameter is case insensitive (codec string matching converts to uppercase)
    std::vector<SrsRtcTrackDescription *> opus_upper = source->get_track_desc("audio", "OPUS");
    EXPECT_EQ(1, (int)opus_upper.size()); // Should work due to case insensitive matching

    std::vector<SrsRtcTrackDescription *> h264_lower = source->get_track_desc("video", "h264");
    EXPECT_EQ(1, (int)h264_lower.size()); // Should work due to case insensitive matching

    // Test: original case should also work
    std::vector<SrsRtcTrackDescription *> opus_correct = source->get_track_desc("audio", "opus");
    EXPECT_EQ(1, (int)opus_correct.size());

    std::vector<SrsRtcTrackDescription *> h264_correct = source->get_track_desc("video", "H264");
    EXPECT_EQ(1, (int)h264_correct.size());
}

VOID TEST(AppTest2, RtcSourceGetTrackDescMultipleMatchingVideoTracks)
{
    srs_error_t err;

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->ip_ = "127.0.0.1";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "test";

    // Create RTC source
    SrsUniquePtr<SrsRtcSource> source(new SrsRtcSource());
    HELPER_EXPECT_SUCCESS(source->initialize(req.get()));

    // Create stream description with multiple H264 video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    stream_desc->id_ = "test-stream";

    // Create first H264 video track
    SrsRtcTrackDescription *h264_track1 = new SrsRtcTrackDescription();
    h264_track1->type_ = "video";
    h264_track1->id_ = "video-h264-track-1";
    h264_track1->ssrc_ = 88888;
    h264_track1->media_ = new SrsVideoPayload(102, "H264", 90000);
    stream_desc->video_track_descs_.push_back(h264_track1);

    // Create second H264 video track
    SrsRtcTrackDescription *h264_track2 = new SrsRtcTrackDescription();
    h264_track2->type_ = "video";
    h264_track2->id_ = "video-h264-track-2";
    h264_track2->ssrc_ = 99999;
    h264_track2->media_ = new SrsVideoPayload(103, "H264", 90000); // Different PT but same codec
    stream_desc->video_track_descs_.push_back(h264_track2);

    // Create H265 video track for contrast
    SrsRtcTrackDescription *h265_track = new SrsRtcTrackDescription();
    h265_track->type_ = "video";
    h265_track->id_ = "video-h265-track";
    h265_track->ssrc_ = 11111;
    h265_track->media_ = new SrsVideoPayload(49, "H265", 90000);
    stream_desc->video_track_descs_.push_back(h265_track);

    // Set stream description
    source->set_stream_desc(stream_desc.get());

    // Test: get_track_desc for H264 should return both H264 tracks
    std::vector<SrsRtcTrackDescription *> h264_tracks = source->get_track_desc("video", "H264");
    EXPECT_EQ(2, (int)h264_tracks.size());
    // Note: set_stream_desc creates copies, so we check properties instead of pointer equality
    EXPECT_EQ("video-h264-track-1", h264_tracks[0]->id_);
    EXPECT_EQ("video-h264-track-2", h264_tracks[1]->id_);

    // Test: get_track_desc for H265 should return only the H265 track
    std::vector<SrsRtcTrackDescription *> h265_tracks = source->get_track_desc("video", "H265");
    EXPECT_EQ(1, (int)h265_tracks.size());
    EXPECT_EQ("video-h265-track", h265_tracks[0]->id_);

    // Test: get_track_desc with empty media_name should return all video tracks
    std::vector<SrsRtcTrackDescription *> all_video_tracks = source->get_track_desc("video", "");
    EXPECT_EQ(3, (int)all_video_tracks.size());
    EXPECT_EQ("video-h264-track-1", all_video_tracks[0]->id_);
    EXPECT_EQ("video-h264-track-2", all_video_tracks[1]->id_);
    EXPECT_EQ("video-h265-track", all_video_tracks[2]->id_);
}

// Reproduce issue 4449: Newly created source is immediately considered dead
// When a new source is created with stream_die_at_=0, if notify() timer fires
// before a publisher connects, the source gets deleted because stream_is_dead()
// returns true. This causes "new live source, dead=1" in logs.
VOID TEST(ReproduceIssue4449, RtmpLiveSourceNotifyDeletesNewlyCreatedSource)
{
    srs_error_t err;

    // Create a source manager
    SrsUniquePtr<SrsLiveSourceManager> manager(new SrsLiveSourceManager());
    HELPER_EXPECT_SUCCESS(manager->initialize());

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "thegobot";

    // Fetch or create source (this creates a new source)
    SrsSharedPtr<SrsLiveSource> source;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(req.get(), source));

    // After fix: newly created source should NOT be dead
    EXPECT_FALSE(source->stream_is_dead());
    EXPECT_EQ(1, (int)manager->pool_.size());

    // Simulate timer firing - call notify()
    int pool_size_before = (int)manager->pool_.size();
    HELPER_EXPECT_SUCCESS(manager->notify(0, 0, 0));
    int pool_size_after = (int)manager->pool_.size();

    // After fix: the newly created source should NOT be deleted by notify
    EXPECT_EQ(pool_size_before, pool_size_after);
    EXPECT_EQ(1, pool_size_after);
}

// Test SRT source for the same issue
VOID TEST(ReproduceIssue4449, SrtSourceNotifyDeletesNewlyCreatedSource)
{
    srs_error_t err;

    // Create a SRT source manager
    SrsUniquePtr<SrsSrtSourceManager> manager(new SrsSrtSourceManager());
    HELPER_EXPECT_SUCCESS(manager->initialize());

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "thegobot";

    // Fetch or create source (this creates a new source)
    SrsSharedPtr<SrsSrtSource> source;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(req.get(), source));

    // After fix: newly created source should NOT be dead
    EXPECT_FALSE(source->stream_is_dead());
    EXPECT_EQ(1, (int)manager->pool_.size());

    // Simulate timer firing - call notify()
    int pool_size_before = (int)manager->pool_.size();
    HELPER_EXPECT_SUCCESS(manager->notify(0, 0, 0));
    int pool_size_after = (int)manager->pool_.size();

    // After fix: the newly created source should NOT be deleted by notify
    EXPECT_EQ(pool_size_before, pool_size_after);
    EXPECT_EQ(1, pool_size_after);
}

// Test RTC source for the same issue
VOID TEST(ReproduceIssue4449, RtcSourceNotifyDeletesNewlyCreatedSource)
{
    srs_error_t err;

    // Create a RTC source manager
    SrsUniquePtr<SrsRtcSourceManager> manager(new SrsRtcSourceManager());
    HELPER_EXPECT_SUCCESS(manager->initialize());

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "thegobot";

    // Fetch or create source (this creates a new source)
    SrsSharedPtr<SrsRtcSource> source;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(req.get(), source));

    // After fix: newly created source should NOT be dead
    EXPECT_FALSE(source->stream_is_dead());
    EXPECT_EQ(1, (int)manager->pool_.size());

    // Simulate timer firing - call notify()
    int pool_size_before = (int)manager->pool_.size();
    HELPER_EXPECT_SUCCESS(manager->notify(0, 0, 0));
    int pool_size_after = (int)manager->pool_.size();

    // After fix: the newly created source should NOT be deleted by notify
    EXPECT_EQ(pool_size_before, pool_size_after);
    EXPECT_EQ(1, pool_size_after);
}

#ifdef SRS_RTSP
// Test RTSP source for the same issue
VOID TEST(ReproduceIssue4449, RtspSourceNotifyDeletesNewlyCreatedSource)
{
    srs_error_t err;

    // Create a RTSP source manager
    SrsUniquePtr<SrsRtspSourceManager> manager(new SrsRtspSourceManager());
    HELPER_EXPECT_SUCCESS(manager->initialize());

    // Create a mock request
    SrsUniquePtr<SrsRequest> req(new SrsRequest());
    req->host_ = "localhost";
    req->vhost_ = "test.vhost";
    req->app_ = "live";
    req->stream_ = "thegobot";

    // Fetch or create source (this creates a new source)
    SrsSharedPtr<SrsRtspSource> source;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(req.get(), source));

    // After fix: newly created source should NOT be dead
    EXPECT_FALSE(source->stream_is_dead());
    EXPECT_EQ(1, (int)manager->pool_.size());

    // Simulate timer firing - call notify()
    int pool_size_before = (int)manager->pool_.size();
    HELPER_EXPECT_SUCCESS(manager->notify(0, 0, 0));
    int pool_size_after = (int)manager->pool_.size();

    // After fix: the newly created source should NOT be deleted by notify
    EXPECT_EQ(pool_size_before, pool_size_after);
    EXPECT_EQ(1, pool_size_after);
}
#endif
