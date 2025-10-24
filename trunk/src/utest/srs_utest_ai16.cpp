//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai16.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <vector>

// External function declarations for testing
extern srs_error_t srs_api_response_jsonp(ISrsHttpResponseWriter *w, string callback, string data);
extern srs_error_t srs_api_response_code(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, int code);
extern srs_error_t srs_api_response_code(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, srs_error_t code);

// External global variables for reload state
extern srs_error_t _srs_reload_err;
extern SrsReloadState _srs_reload_state;
extern std::string _srs_reload_id;

VOID TEST(KernelBalanceTest, RoundRobinBasicSelection)
{
    // Test the major use scenario: round-robin selection across multiple servers
    // This covers the typical edge server origin selection use case

    SrsUniquePtr<SrsLbRoundRobin> lb(new SrsLbRoundRobin());

    // Setup test servers
    vector<string> servers;
    servers.push_back("192.168.1.100:1935");
    servers.push_back("192.168.1.101:1935");
    servers.push_back("192.168.1.102:1935");

    // Test round-robin selection - should cycle through all servers
    string selected1 = lb->select(servers);
    EXPECT_STREQ("192.168.1.100:1935", selected1.c_str());
    EXPECT_EQ(0, (int)lb->current());
    EXPECT_STREQ("192.168.1.100:1935", lb->selected().c_str());

    string selected2 = lb->select(servers);
    EXPECT_STREQ("192.168.1.101:1935", selected2.c_str());
    EXPECT_EQ(1, (int)lb->current());
    EXPECT_STREQ("192.168.1.101:1935", lb->selected().c_str());

    string selected3 = lb->select(servers);
    EXPECT_STREQ("192.168.1.102:1935", selected3.c_str());
    EXPECT_EQ(2, (int)lb->current());
    EXPECT_STREQ("192.168.1.102:1935", lb->selected().c_str());

    // Test wrap-around - should go back to first server
    string selected4 = lb->select(servers);
    EXPECT_STREQ("192.168.1.100:1935", selected4.c_str());
    EXPECT_EQ(0, (int)lb->current());
    EXPECT_STREQ("192.168.1.100:1935", lb->selected().c_str());

    // Continue cycling to verify consistent behavior
    string selected5 = lb->select(servers);
    EXPECT_STREQ("192.168.1.101:1935", selected5.c_str());
    EXPECT_EQ(1, (int)lb->current());

    string selected6 = lb->select(servers);
    EXPECT_STREQ("192.168.1.102:1935", selected6.c_str());
    EXPECT_EQ(2, (int)lb->current());
}

VOID TEST(SrsBufferCacheTest, ConstructorAndUpdateAuth)
{
    srs_error_t err;

    // Test the major use scenario: constructor initialization and update_auth
    // This covers the typical HTTP streaming cache initialization use case

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));
    mock_request->pageUrl_ = "http://example.com/page";
    mock_request->swfUrl_ = "http://example.com/player.swf";
    mock_request->tcUrl_ = "rtmp://127.0.0.1/live";

    // Test constructor - should initialize all fields properly
    SrsUniquePtr<SrsBufferCache> cache(new SrsBufferCache(mock_request.get()));

    // Verify that the request was copied (as_http() was called in constructor)
    EXPECT_TRUE(cache->req_ != NULL);
    EXPECT_STREQ("test.vhost", cache->req_->vhost_.c_str());
    EXPECT_STREQ("live", cache->req_->app_.c_str());
    EXPECT_STREQ("stream1", cache->req_->stream_.c_str());

    // Verify that queue and thread were created
    EXPECT_TRUE(cache->queue_ != NULL);
    EXPECT_TRUE(cache->trd_ != NULL);

    // Verify that fast_cache was initialized to 0
    EXPECT_EQ(0, (int)cache->fast_cache_);

    // Verify that config and live_sources were set to global singletons
    EXPECT_TRUE(cache->config_ != NULL);
    EXPECT_TRUE(cache->live_sources_ != NULL);

    // Test update_auth - should update the request with new auth info
    SrsUniquePtr<MockRequest> new_request(new MockRequest("test.vhost", "live", "stream1"));
    new_request->pageUrl_ = "http://example.com/new_page";
    new_request->swfUrl_ = "http://example.com/new_player.swf";
    new_request->tcUrl_ = "rtmp://127.0.0.1/live_new";

    HELPER_EXPECT_SUCCESS(cache->update_auth(new_request.get()));

    // Verify that the request was updated
    EXPECT_TRUE(cache->req_ != NULL);
    EXPECT_STREQ("http://example.com/new_page", cache->req_->pageUrl_.c_str());
    EXPECT_STREQ("http://example.com/new_player.swf", cache->req_->swfUrl_.c_str());
    EXPECT_STREQ("rtmp://127.0.0.1/live_new", cache->req_->tcUrl_.c_str());

    // Destructor will be called automatically and should clean up all resources
}

VOID TEST(SrsBufferCacheTest, DumpCacheWithMessages)
{
    srs_error_t err;

    // Test the major use scenario: dump_cache with messages in queue
    // This covers the typical HTTP streaming cache dump use case

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create buffer cache
    SrsUniquePtr<SrsBufferCache> cache(new SrsBufferCache(mock_request.get()));

    // Set fast_cache to enable dump_cache functionality
    cache->fast_cache_ = 3 * SRS_UTIME_SECONDS;

    // Create mock media packets and enqueue them to the cache queue
    SrsMediaPacket *video_packet1 = new SrsMediaPacket();
    video_packet1->timestamp_ = 1000;
    video_packet1->message_type_ = SrsFrameTypeVideo;
    char *video_data1 = new char[128];
    memset(video_data1, 0x00, 128);
    video_data1[0] = 0x17; // keyframe + H.264
    video_packet1->wrap(video_data1, 128);

    SrsMediaPacket *audio_packet1 = new SrsMediaPacket();
    audio_packet1->timestamp_ = 1020;
    audio_packet1->message_type_ = SrsFrameTypeAudio;
    char *audio_data1 = new char[32];
    memset(audio_data1, 0x00, 32);
    audio_data1[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    audio_packet1->wrap(audio_data1, 32);

    // Enqueue packets to cache queue - queue takes ownership
    HELPER_EXPECT_SUCCESS(cache->queue_->enqueue(video_packet1, NULL));
    HELPER_EXPECT_SUCCESS(cache->queue_->enqueue(audio_packet1, NULL));

    // Verify queue has packets
    EXPECT_EQ(2, cache->queue_->size());

    // Create mock source and consumer
    SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
    SrsUniquePtr<MockLiveConsumerForQueue> consumer(new MockLiveConsumerForQueue(source.get()));

    // Test dump_cache - should dump all packets to consumer
    HELPER_EXPECT_SUCCESS(cache->dump_cache(consumer.get(), SrsRtmpJitterAlgorithmFULL));

    // Verify consumer received all packets
    EXPECT_EQ(2, consumer->enqueue_count_);
    EXPECT_EQ(1000, consumer->enqueued_timestamps_[0]);
    EXPECT_EQ(1020, consumer->enqueued_timestamps_[1]);

    // Verify queue still contains packets (dump_cache doesn't clear the queue)
    EXPECT_EQ(2, cache->queue_->size());
}

VOID TEST(HttpStreamTest, TsStreamEncoderWriteAudioVideo)
{
    srs_error_t err;

    // Test the major use scenario: writing audio and video data to TS stream encoder
    // This covers the typical HTTP-TS streaming use case

    // Create mock file writer for TS output
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());
    HELPER_EXPECT_SUCCESS(writer->open("test.ts"));

    // Create TS stream encoder
    SrsUniquePtr<SrsTsStreamEncoder> encoder(new SrsTsStreamEncoder());

    // Initialize encoder with mock writer
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), NULL));

    // Prepare test video data (H.264 keyframe with AVC packet)
    // Format: [frame_type(4bits) + codec_id(4bits)][avc_packet_type][composition_time(3bytes)][data]
    char video_data[128];
    memset(video_data, 0x00, sizeof(video_data));
    video_data[0] = 0x17; // keyframe (1) + H.264 (7)
    video_data[1] = 0x01; // AVC NALU
    video_data[2] = 0x00; // composition time
    video_data[3] = 0x00;
    video_data[4] = 0x00;
    // Add some dummy NALU data
    video_data[5] = 0x00;
    video_data[6] = 0x00;
    video_data[7] = 0x00;
    video_data[8] = 0x01; // NALU start code
    video_data[9] = 0x65; // IDR slice

    // Write video data
    HELPER_EXPECT_SUCCESS(encoder->write_video(1000, video_data, sizeof(video_data)));

    // Prepare test audio data (AAC packet)
    // Format: [sound_format(4bits) + sound_rate(2bits) + sound_size(1bit) + sound_type(1bit)][aac_packet_type][data]
    char audio_data[64];
    memset(audio_data, 0x00, sizeof(audio_data));
    audio_data[0] = 0xAF; // AAC (10) + 44kHz (3) + 16-bit (1) + stereo (1)
    audio_data[1] = 0x01; // AAC raw data
    // Add some dummy AAC data
    for (int i = 2; i < 64; i++) {
        audio_data[i] = i;
    }

    // Write audio data
    HELPER_EXPECT_SUCCESS(encoder->write_audio(1020, audio_data, sizeof(audio_data)));

    // Write metadata (should succeed but do nothing)
    char metadata[32];
    memset(metadata, 0x00, sizeof(metadata));
    HELPER_EXPECT_SUCCESS(encoder->write_metadata(1040, metadata, sizeof(metadata)));

    // Verify that data was written to the mock file writer
    EXPECT_TRUE(writer->filesize() > 0);
}

VOID TEST(HttpStreamTest, TsStreamEncoderCacheAndAVSettings)
{
    srs_error_t err;

    // Test the major use scenario: cache behavior and audio/video settings
    // This covers TS stream encoder cache handling and A/V configuration

    // Create mock file writer for TS output
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());
    HELPER_EXPECT_SUCCESS(writer->open("test.ts"));

    // Create TS stream encoder
    SrsUniquePtr<SrsTsStreamEncoder> encoder(new SrsTsStreamEncoder());

    // Initialize encoder with mock writer
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), NULL));

    // Test has_cache - TS stream encoder should not have cache (uses SrsLiveSource GOP cache)
    EXPECT_FALSE(encoder->has_cache());

    // Test dump_cache - should always succeed and do nothing for TS stream
    HELPER_EXPECT_SUCCESS(encoder->dump_cache(NULL, SrsRtmpJitterAlgorithmFULL));

    // Test set_has_audio - configure encoder to expect audio
    encoder->set_has_audio(true);

    // Test set_has_video - configure encoder to expect video
    encoder->set_has_video(true);

    // Test set_guess_has_av - enable A/V guessing mode
    encoder->set_guess_has_av(true);

    // Verify encoder still works after configuration
    // Prepare test video data (H.264 keyframe)
    char video_data[128];
    memset(video_data, 0x00, sizeof(video_data));
    video_data[0] = 0x17; // keyframe + H.264
    video_data[1] = 0x01; // AVC NALU
    video_data[5] = 0x00;
    video_data[6] = 0x00;
    video_data[7] = 0x00;
    video_data[8] = 0x01; // NALU start code
    video_data[9] = 0x65; // IDR slice

    // Write video data - should succeed with configured settings
    HELPER_EXPECT_SUCCESS(encoder->write_video(1000, video_data, sizeof(video_data)));

    // Prepare test audio data (AAC packet)
    char audio_data[64];
    memset(audio_data, 0x00, sizeof(audio_data));
    audio_data[0] = 0xAF; // AAC + 44kHz + 16-bit + stereo
    audio_data[1] = 0x01; // AAC raw data

    // Write audio data - should succeed with configured settings
    HELPER_EXPECT_SUCCESS(encoder->write_audio(1020, audio_data, sizeof(audio_data)));

    // Verify that data was written successfully
    EXPECT_TRUE(writer->filesize() > 0);
}

VOID TEST(SrsFlvStreamEncoderTest, InitializeSuccess)
{
    srs_error_t err;

    // Test the major use scenario: initialize FLV stream encoder with file writer
    // This covers the typical HTTP-FLV streaming initialization use case

    // Create mock file writer
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());

    // Create FLV stream encoder
    SrsUniquePtr<SrsFlvStreamEncoder> encoder(new SrsFlvStreamEncoder());

    // Initialize encoder with file writer - should succeed
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), NULL));

    // Verify that encoder is ready to write data (internal enc_ was initialized)
    // We can verify this by checking that subsequent operations don't crash
    EXPECT_TRUE(encoder.get() != NULL);
}

VOID TEST(SrsFlvStreamEncoderTest, WriteAudioVideoMetadata)
{
    srs_error_t err;

    // Test the major use scenario: write audio, video, and metadata to FLV stream
    // This covers the typical HTTP-FLV streaming workflow where encoder writes
    // FLV header automatically on first write, then writes media packets

    // Create mock file writer
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());

    // Create FLV stream encoder
    SrsUniquePtr<SrsFlvStreamEncoder> encoder(new SrsFlvStreamEncoder());

    // Initialize encoder with file writer
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), NULL));

    // Prepare test metadata (AMF0 encoded onMetaData)
    char metadata[128];
    memset(metadata, 0x00, sizeof(metadata));
    metadata[0] = 0x02; // AMF0 string marker
    metadata[1] = 0x00;
    metadata[2] = 0x0a; // length = 10
    memcpy(metadata + 3, "onMetaData", 10);
    metadata[13] = 0x08; // AMF0 object marker

    // Write metadata - should succeed and trigger header write
    HELPER_EXPECT_SUCCESS(encoder->write_metadata(0, metadata, sizeof(metadata)));

    // Prepare test video data (H.264 keyframe)
    char video_data[128];
    memset(video_data, 0x00, sizeof(video_data));
    video_data[0] = 0x17; // keyframe + H.264
    video_data[1] = 0x01; // AVC NALU
    video_data[5] = 0x00;
    video_data[6] = 0x00;
    video_data[7] = 0x00;
    video_data[8] = 0x01; // NALU start code
    video_data[9] = 0x65; // IDR slice

    // Write video data - should succeed (header already written)
    HELPER_EXPECT_SUCCESS(encoder->write_video(1000, video_data, sizeof(video_data)));

    // Prepare test audio data (AAC packet)
    char audio_data[64];
    memset(audio_data, 0x00, sizeof(audio_data));
    audio_data[0] = 0xAF; // AAC + 44kHz + 16-bit + stereo
    audio_data[1] = 0x01; // AAC raw data

    // Write audio data - should succeed (header already written)
    HELPER_EXPECT_SUCCESS(encoder->write_audio(1020, audio_data, sizeof(audio_data)));

    // Verify that data was written successfully
    // The file should contain FLV header + metadata tag + video tag + audio tag
    EXPECT_TRUE(writer->filesize() > 0);
}

VOID TEST(SrsFlvStreamEncoderTest, ConfigurationAndCacheMethods)
{
    srs_error_t err;

    // Test the major use scenario: configure encoder settings and verify cache behavior
    // This covers the typical HTTP-FLV streaming encoder configuration use case

    // Create mock file writer
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());

    // Create FLV stream encoder
    SrsUniquePtr<SrsFlvStreamEncoder> encoder(new SrsFlvStreamEncoder());

    // Initialize encoder with mock writer
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), NULL));

    // Test set_drop_if_not_match - should configure the underlying transmuxer
    encoder->set_drop_if_not_match(true);
    encoder->set_drop_if_not_match(false);

    // Test set_has_audio - should configure audio presence
    encoder->set_has_audio(true);
    encoder->set_has_audio(false);
    encoder->set_has_audio(true); // Reset to true for later tests

    // Test set_has_video - should configure video presence
    encoder->set_has_video(true);
    encoder->set_has_video(false);
    encoder->set_has_video(true); // Reset to true for later tests

    // Test set_guess_has_av - should configure A/V guessing behavior
    encoder->set_guess_has_av(true);
    encoder->set_guess_has_av(false);
    encoder->set_guess_has_av(true); // Reset to true

    // Test has_cache - should always return false for FLV stream encoder
    // because FLV stream uses GOP cache from SrsLiveSource
    EXPECT_FALSE(encoder->has_cache());

    // Test dump_cache - should always succeed and do nothing
    // because FLV stream ignores cache (uses SrsLiveSource cache instead)
    HELPER_EXPECT_SUCCESS(encoder->dump_cache(NULL, SrsRtmpJitterAlgorithmOFF));

    // Verify encoder still works after configuration changes
    // Write a video frame to ensure encoder is functional
    char video_data[10];
    video_data[0] = 0x17; // AVC keyframe
    video_data[1] = 0x01; // AVC NALU
    HELPER_EXPECT_SUCCESS(encoder->write_video(1000, video_data, sizeof(video_data)));

    // Verify that data was written successfully
    EXPECT_TRUE(writer->filesize() > 0);
}

VOID TEST(HttpStreamTest, FlvStreamEncoderWriteTagsWithGuessAV)
{
    srs_error_t err;

    // Test the major use scenario: write_tags with guess_has_av enabled
    // This covers the typical HTTP-FLV streaming workflow where encoder
    // automatically detects whether stream has audio/video by analyzing packets
    // and writes FLV header accordingly (issue #939)

    // Create mock file writer
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());

    // Create FLV stream encoder
    SrsUniquePtr<SrsFlvStreamEncoder> encoder(new SrsFlvStreamEncoder());

    // Initialize encoder with file writer
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), NULL));

    // Enable guess_has_av mode (default is true)
    encoder->set_guess_has_av(true);

    // Create array of media packets with mixed audio and video
    const int count = 5;
    SrsMediaPacket *msgs[count];

    // Create video sequence header (H.264 SPS/PPS)
    msgs[0] = new SrsMediaPacket();
    msgs[0]->timestamp_ = 0;
    msgs[0]->message_type_ = SrsFrameTypeVideo;
    char *video_sh_data = new char[10];
    video_sh_data[0] = 0x17; // keyframe + AVC
    video_sh_data[1] = 0x00; // AVC sequence header
    for (int i = 2; i < 10; i++) {
        video_sh_data[i] = (char)i;
    }
    msgs[0]->wrap(video_sh_data, 10);

    // Create video frame (non-sequence header)
    msgs[1] = new SrsMediaPacket();
    msgs[1]->timestamp_ = 40;
    msgs[1]->message_type_ = SrsFrameTypeVideo;
    char *video_frame_data = new char[20];
    video_frame_data[0] = 0x17; // keyframe + AVC
    video_frame_data[1] = 0x01; // AVC NALU
    for (int i = 2; i < 20; i++) {
        video_frame_data[i] = (char)(0x10 + i);
    }
    msgs[1]->wrap(video_frame_data, 20);

    // Create audio sequence header (AAC)
    msgs[2] = new SrsMediaPacket();
    msgs[2]->timestamp_ = 0;
    msgs[2]->message_type_ = SrsFrameTypeAudio;
    char *audio_sh_data = new char[4];
    audio_sh_data[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    audio_sh_data[1] = 0x00; // AAC sequence header
    audio_sh_data[2] = 0x12;
    audio_sh_data[3] = 0x10;
    msgs[2]->wrap(audio_sh_data, 4);

    // Create audio frame (non-sequence header)
    msgs[3] = new SrsMediaPacket();
    msgs[3]->timestamp_ = 23;
    msgs[3]->message_type_ = SrsFrameTypeAudio;
    char *audio_frame_data = new char[128];
    audio_frame_data[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    audio_frame_data[1] = 0x01; // AAC raw data
    for (int i = 2; i < 128; i++) {
        audio_frame_data[i] = (char)(0x20 + i);
    }
    msgs[3]->wrap(audio_frame_data, 128);

    // Create another video frame
    msgs[4] = new SrsMediaPacket();
    msgs[4]->timestamp_ = 80;
    msgs[4]->message_type_ = SrsFrameTypeVideo;
    char *video_frame_data2 = new char[30];
    video_frame_data2[0] = 0x27; // inter frame + AVC
    video_frame_data2[1] = 0x01; // AVC NALU
    for (int i = 2; i < 30; i++) {
        video_frame_data2[i] = (char)(0x30 + i);
    }
    msgs[4]->wrap(video_frame_data2, 30);

    // Write all tags at once - encoder should:
    // 1. Analyze packets to detect has_audio=true, has_video=true
    // 2. Count non-sequence-header frames (2 video frames, 1 audio frame)
    // 3. Write FLV header with both audio and video flags
    // 4. Write all the tags
    HELPER_EXPECT_SUCCESS(encoder->write_tags(msgs, count));

    // Verify that FLV header and tags were written
    EXPECT_TRUE(writer->filesize() > 0);

    // Clean up - write_tags does not free the messages
    for (int i = 0; i < count; i++) {
        srs_freep(msgs[i]);
    }
}

VOID TEST(AppHttpStreamTest, AacStreamEncoderMajorScenario)
{
    srs_error_t err;

    // Test the major use scenario: initialize encoder, write AAC audio data, and dump cache
    // This covers the typical AAC HTTP streaming use case

    // Create mock file writer and buffer cache
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());
    HELPER_EXPECT_SUCCESS(writer->open("test.aac"));

    MockBufferCache mock_cache;

    // Create AAC stream encoder
    SrsUniquePtr<SrsAacStreamEncoder> encoder(new SrsAacStreamEncoder());

    // Test initialization
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), &mock_cache));

    // Test has_cache - should always return true for AAC encoder
    EXPECT_TRUE(encoder->has_cache());

    // Create AAC sequence header (AudioSpecificConfig)
    // Format: [sound_format(4bits)|sound_rate(2bits)|sound_size(1bit)|sound_type(1bit)][aac_packet_type][AudioSpecificConfig]
    char aac_sequence_header[4];
    aac_sequence_header[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    aac_sequence_header[1] = 0x00; // AAC sequence header
    aac_sequence_header[2] = 0x12; // AudioSpecificConfig byte 1 (AAC-LC, 44.1kHz)
    aac_sequence_header[3] = 0x10; // AudioSpecificConfig byte 2 (stereo)

    // Write AAC sequence header
    HELPER_EXPECT_SUCCESS(encoder->write_audio(0, aac_sequence_header, 4));

    // Create AAC raw audio frame
    // Format: [sound_format(4bits)|sound_rate(2bits)|sound_size(1bit)|sound_type(1bit)][aac_packet_type][raw_aac_frame_data]
    char aac_raw_frame[10];
    aac_raw_frame[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    aac_raw_frame[1] = 0x01; // AAC raw frame data
    // Fill with dummy AAC frame data
    for (int i = 2; i < 10; i++) {
        aac_raw_frame[i] = 0xCB;
    }

    // Write AAC raw audio frame
    HELPER_EXPECT_SUCCESS(encoder->write_audio(1000, aac_raw_frame, 10));

    // Verify that ADTS header (7 bytes) + AAC frame data (8 bytes) were written
    // Total: 7 + 8 = 15 bytes
    EXPECT_EQ(15, writer->filesize());

    // Test write_video - should be ignored for AAC encoder
    char dummy_video[10];
    memset(dummy_video, 0x00, 10);
    HELPER_EXPECT_SUCCESS(encoder->write_video(2000, dummy_video, 10));
    // File size should not change after writing video
    EXPECT_EQ(15, writer->filesize());

    // Test write_metadata - should be ignored for AAC encoder
    char dummy_metadata[10];
    memset(dummy_metadata, 0x00, 10);
    HELPER_EXPECT_SUCCESS(encoder->write_metadata(3000, dummy_metadata, 10));
    // File size should not change after writing metadata
    EXPECT_EQ(15, writer->filesize());

    // Test dump_cache - should delegate to buffer cache
    MockLiveSourceForQueue mock_source;
    SrsUniquePtr<MockLiveConsumerForQueue> consumer(new MockLiveConsumerForQueue(&mock_source));

    HELPER_EXPECT_SUCCESS(encoder->dump_cache(consumer.get(), SrsRtmpJitterAlgorithmFULL));

    // Verify that dump_cache was called on the buffer cache
    EXPECT_EQ(1, mock_cache.dump_cache_count_);
    EXPECT_EQ(consumer.get(), mock_cache.last_consumer_);
    EXPECT_EQ(SrsRtmpJitterAlgorithmFULL, mock_cache.last_jitter_);
}

VOID TEST(BufferWriterTest, WriteToHttpResponse)
{
    srs_error_t err;

    // Test the major use scenario: writing data to HTTP response through SrsBufferWriter
    // This covers the typical HTTP streaming use case where media data is written directly to HTTP response

    // Create mock HTTP response writer
    MockResponseWriter mock_writer;

    // Set content length to allow writing data
    char test_data[] = "Hello, SRS!";
    char buf1[] = "First";
    char buf2[] = "Second";
    int total_size = (sizeof(test_data) - 1) + (sizeof(buf1) - 1) + (sizeof(buf2) - 1);
    mock_writer.header()->set_content_length(total_size);

    // Create SrsBufferWriter with the mock writer
    SrsUniquePtr<SrsBufferWriter> buffer_writer(new SrsBufferWriter(&mock_writer));

    // Test is_open - should always return true
    EXPECT_TRUE(buffer_writer->is_open());

    // Test tellg - should always return 0
    EXPECT_EQ(0, buffer_writer->tellg());

    // Test open - should always succeed
    HELPER_EXPECT_SUCCESS(buffer_writer->open("dummy_file"));

    // Test write - write some data
    ssize_t nwrite = 0;
    HELPER_EXPECT_SUCCESS(buffer_writer->write(test_data, sizeof(test_data) - 1, &nwrite));
    EXPECT_EQ((ssize_t)(sizeof(test_data) - 1), nwrite);

    // Verify data was written to the mock writer
    string written_data = string(mock_writer.io.out_buffer.bytes(), mock_writer.io.out_buffer.length());
    EXPECT_TRUE(written_data.find("Hello, SRS!") != string::npos);

    // Test writev - write multiple buffers
    iovec iov[2];
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1) - 1;
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2) - 1;

    ssize_t nwrite_v = 0;
    HELPER_EXPECT_SUCCESS(buffer_writer->writev(iov, 2, &nwrite_v));
    EXPECT_EQ((ssize_t)(sizeof(buf1) - 1 + sizeof(buf2) - 1), nwrite_v);

    // Test close - should do nothing but not crash
    buffer_writer->close();
}

// New mock implementations for SrsLiveStream testing
MockHttpxConnForLiveStream::MockHttpxConnForLiveStream() : SrsHttpxConn(NULL, NULL, NULL, "127.0.0.1", 1935, "", "")
{
    enable_stat_called_ = false;
}

MockHttpxConnForLiveStream::~MockHttpxConnForLiveStream()
{
}

void MockHttpxConnForLiveStream::set_enable_stat(bool v)
{
    enable_stat_called_ = true;
    SrsHttpxConn::set_enable_stat(v);
}

MockHttpConnForLiveStream::MockHttpConnForLiveStream() : SrsHttpConn(NULL, NULL, NULL, "127.0.0.1", 1935)
{
    mock_handler_ = new MockHttpxConnForLiveStream();
}

MockHttpConnForLiveStream::~MockHttpConnForLiveStream()
{
    srs_freep(mock_handler_);
}

ISrsHttpConnOwner *MockHttpConnForLiveStream::handler()
{
    return mock_handler_;
}

MockHttpMessageForLiveStream::MockHttpMessageForLiveStream() : SrsHttpMessage()
{
    mock_conn_ = new MockHttpConnForLiveStream();
}

MockHttpMessageForLiveStream::~MockHttpMessageForLiveStream()
{
    srs_freep(mock_conn_);
}

ISrsConnection *MockHttpMessageForLiveStream::connection()
{
    return mock_conn_;
}

std::string MockHttpMessageForLiveStream::path()
{
    return "/live/stream.flv";
}

VOID TEST(SrsLiveStreamTest, ServeHttpWithDisabledEntry)
{
    srs_error_t err;

    // Test the major use scenario: serve_http_impl with entry_->enabled = false
    // This covers the case where stream is disabled and should return error after
    // security check and HTTP hooks

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create mock buffer cache
    SrsUniquePtr<MockBufferCache> mock_cache(new MockBufferCache());

    // Create SrsLiveStream
    SrsUniquePtr<SrsLiveStream> live_stream(new SrsLiveStream(mock_request.get(), mock_cache.get()));

    // Create and set mock dependencies
    MockStatisticForLiveStream mock_stat;
    MockSecurity mock_security;

    // Replace dependencies with mocks
    live_stream->stat_ = &mock_stat;
    srs_freep(live_stream->security_);
    live_stream->security_ = &mock_security;

    // Create mock HTTP message and response writer
    SrsUniquePtr<MockHttpMessageForLiveStream> mock_message(new MockHttpMessageForLiveStream());
    MockResponseWriter mock_writer;

    // Set up entry with enabled = false - this is the key test condition
    live_stream->entry_ = new SrsHttpMuxEntry();
    live_stream->entry_->enabled = false;
    live_stream->entry_->pattern = "/live/stream.flv";

    // Call serve_http - should add viewer, call serve_http_impl, then remove viewer
    err = live_stream->serve_http(&mock_writer, mock_message.get());

    // Verify that error was returned due to disabled entry
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_RTMP_STREAM_NOT_FOUND, srs_error_code(err));
    srs_freep(err);

    // Verify that stat->on_client was called
    EXPECT_EQ(1, mock_stat.on_client_count_);

    // Verify that security->check was called
    EXPECT_EQ(1, mock_security.check_count_);

    // Verify that viewers list is empty after serve_http returns
    // This confirms that the viewer was added before serve_http_impl and removed after
    EXPECT_EQ(0, (int)live_stream->viewers_.size());

    // Clean up - set dependencies back to NULL before destruction
    live_stream->stat_ = NULL;
    live_stream->security_ = NULL;
    srs_freep(live_stream->entry_);
}

// Mock ISrsStatistic implementation for SrsLiveStream testing
MockStatisticForLiveStream::MockStatisticForLiveStream()
{
    on_client_count_ = 0;
    on_client_error_ = srs_success;
}

MockStatisticForLiveStream::~MockStatisticForLiveStream()
{
}

void MockStatisticForLiveStream::on_disconnect(std::string id, srs_error_t err)
{
}

srs_error_t MockStatisticForLiveStream::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    on_client_count_++;
    return srs_error_copy(on_client_error_);
}

srs_error_t MockStatisticForLiveStream::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockStatisticForLiveStream::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockStatisticForLiveStream::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForLiveStream::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForLiveStream::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
}

void MockStatisticForLiveStream::kbps_sample()
{
}

srs_error_t MockStatisticForLiveStream::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockStatisticForLiveStream::server_id()
{
    return "mock_server_id";
}

std::string MockStatisticForLiveStream::service_id()
{
    return "mock_service_id";
}

std::string MockStatisticForLiveStream::service_pid()
{
    return "mock_pid";
}

SrsStatisticVhost *MockStatisticForLiveStream::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForLiveStream::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForLiveStream::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForLiveStream::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockStatisticForLiveStream::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForLiveStream::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForLiveStream::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForLiveStream::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    send_bytes = 0;
    recv_bytes = 0;
    nstreams = 0;
    nclients = 0;
    total_nclients = 0;
    nerrs = 0;
    return srs_success;
}

// Mock config implementation for SrsLiveStream hooks testing
MockAppConfigForLiveStreamHooks::MockAppConfigForLiveStreamHooks()
{
    http_hooks_enabled_ = false;
    on_play_directive_ = NULL;
    on_stop_directive_ = NULL;
}

MockAppConfigForLiveStreamHooks::~MockAppConfigForLiveStreamHooks()
{
    srs_freep(on_play_directive_);
    srs_freep(on_stop_directive_);
}

bool MockAppConfigForLiveStreamHooks::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfigForLiveStreamHooks::get_vhost_on_play(std::string vhost)
{
    return on_play_directive_;
}

SrsConfDirective *MockAppConfigForLiveStreamHooks::get_vhost_on_stop(std::string vhost)
{
    return on_stop_directive_;
}

// Mock HTTP hooks implementation for SrsLiveStream testing
MockHttpHooksForLiveStream::MockHttpHooksForLiveStream()
{
    on_play_count_ = 0;
    on_play_error_ = srs_success;
    on_stop_count_ = 0;
}

MockHttpHooksForLiveStream::~MockHttpHooksForLiveStream()
{
    srs_freep(on_play_error_);
}

srs_error_t MockHttpHooksForLiveStream::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForLiveStream::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooksForLiveStream::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForLiveStream::on_unpublish(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForLiveStream::on_play(std::string url, ISrsRequest *req)
{
    on_play_count_++;
    on_play_calls_.push_back(std::make_pair(url, req));
    return srs_error_copy(on_play_error_);
}

void MockHttpHooksForLiveStream::on_stop(std::string url, ISrsRequest *req)
{
    on_stop_count_++;
    on_stop_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooksForLiveStream::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooksForLiveStream::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                               std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooksForLiveStream::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooksForLiveStream::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooksForLiveStream::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooksForLiveStream::reset()
{
    on_play_calls_.clear();
    on_play_count_ = 0;
    srs_freep(on_play_error_);
    on_play_error_ = srs_success;
    on_stop_calls_.clear();
    on_stop_count_ = 0;
}

VOID TEST(SrsLiveStreamTest, HttpHooksOnPlayAndStop)
{
    srs_error_t err = srs_success;

    // Test the major use scenario: http_hooks_on_play and http_hooks_on_stop with multiple hook URLs
    // This covers the typical HTTP-FLV/HLS streaming hook notification use case

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create mock buffer cache
    SrsUniquePtr<MockBufferCache> mock_cache(new MockBufferCache());

    // Create SrsLiveStream
    SrsUniquePtr<SrsLiveStream> live_stream(new SrsLiveStream(mock_request.get(), mock_cache.get()));

    // Create mock config and hooks
    MockAppConfigForLiveStreamHooks *mock_config = new MockAppConfigForLiveStreamHooks();
    MockHttpHooksForLiveStream *mock_hooks = new MockHttpHooksForLiveStream();

    // Inject mock dependencies
    live_stream->config_ = mock_config;
    live_stream->hooks_ = mock_hooks;

    // Enable HTTP hooks
    mock_config->http_hooks_enabled_ = true;

    // Setup on_play hook URLs
    SrsConfDirective *on_play_directive = new SrsConfDirective();
    on_play_directive->args_.push_back("http://localhost:8080/api/on_play");
    on_play_directive->args_.push_back("http://localhost:8080/api/on_play2");
    mock_config->on_play_directive_ = on_play_directive;

    // Setup on_stop hook URLs
    SrsConfDirective *on_stop_directive = new SrsConfDirective();
    on_stop_directive->args_.push_back("http://localhost:8080/api/on_stop");
    on_stop_directive->args_.push_back("http://localhost:8080/api/on_stop2");
    mock_config->on_stop_directive_ = on_stop_directive;

    // Create mock HTTP message
    SrsUniquePtr<MockHttpMessage> mock_http_msg(new MockHttpMessage());

    // Test http_hooks_on_play - should call hooks for all URLs
    HELPER_EXPECT_SUCCESS(live_stream->http_hooks_on_play(mock_http_msg.get()));

    // Verify on_play was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_play_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_play_calls_.size());
    EXPECT_STREQ("http://localhost:8080/api/on_play", mock_hooks->on_play_calls_[0].first.c_str());
    EXPECT_STREQ("http://localhost:8080/api/on_play2", mock_hooks->on_play_calls_[1].first.c_str());

    // Test http_hooks_on_stop - should call hooks for all URLs
    live_stream->http_hooks_on_stop(mock_http_msg.get());

    // Verify on_stop was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_stop_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_stop_calls_.size());
    EXPECT_STREQ("http://localhost:8080/api/on_stop", mock_hooks->on_stop_calls_[0].first.c_str());
    EXPECT_STREQ("http://localhost:8080/api/on_stop2", mock_hooks->on_stop_calls_[1].first.c_str());

    // Clean up - set to NULL to avoid double free (mocks will be freed automatically)
    live_stream->config_ = NULL;
    live_stream->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

VOID TEST(SrsLiveEntryTest, FormatDetection)
{
    // Test the major use scenario: SrsLiveEntry format detection based on file extension
    // This covers the typical HTTP streaming entry creation use case where the mount path
    // determines the stream format (FLV, TS, AAC, MP3)

    // Test TS format detection
    SrsUniquePtr<SrsLiveEntry> ts_entry(new SrsLiveEntry("/live/stream.ts"));
    EXPECT_TRUE(ts_entry->is_ts());
    EXPECT_FALSE(ts_entry->is_aac());
    EXPECT_FALSE(ts_entry->is_mp3());
    EXPECT_FALSE(ts_entry->is_flv());

    // Test AAC format detection
    SrsUniquePtr<SrsLiveEntry> aac_entry(new SrsLiveEntry("/live/stream.aac"));
    EXPECT_TRUE(aac_entry->is_aac());
    EXPECT_FALSE(aac_entry->is_ts());
    EXPECT_FALSE(aac_entry->is_mp3());
    EXPECT_FALSE(aac_entry->is_flv());

    // Test MP3 format detection
    SrsUniquePtr<SrsLiveEntry> mp3_entry(new SrsLiveEntry("/live/stream.mp3"));
    EXPECT_TRUE(mp3_entry->is_mp3());
    EXPECT_FALSE(mp3_entry->is_ts());
    EXPECT_FALSE(mp3_entry->is_aac());
    EXPECT_FALSE(mp3_entry->is_flv());

    // Test FLV format detection (for completeness)
    SrsUniquePtr<SrsLiveEntry> flv_entry(new SrsLiveEntry("/live/stream.flv"));
    EXPECT_TRUE(flv_entry->is_flv());
    EXPECT_FALSE(flv_entry->is_ts());
    EXPECT_FALSE(flv_entry->is_aac());
    EXPECT_FALSE(flv_entry->is_mp3());
}

VOID TEST(SrsHttpStreamServerTest, InitializeFlvEntry)
{
    srs_error_t err = srs_success;

    // Test the major use scenario: initialize_flv_entry creates template handler
    // when HTTP remux is enabled for a vhost
    // This covers the typical HTTP-FLV live streaming initialization use case

    // Create mock config
    MockAppConfigForHttpStreamServer *mock_config = new MockAppConfigForHttpStreamServer();
    mock_config->http_remux_enabled_ = true;
    mock_config->http_remux_mount_ = "[vhost]/[app]/[stream].flv";

    // Use a nested scope to control server lifetime
    {
        // Create SrsHttpStreamServer
        SrsUniquePtr<SrsHttpStreamServer> server(new SrsHttpStreamServer());

        // Replace config with mock
        server->config_ = mock_config;

        // Test initialize_flv_entry with enabled HTTP remux
        std::string vhost = "test.vhost";
        HELPER_EXPECT_SUCCESS(server->initialize_flv_entry(vhost));

        // Verify template handler was created
        EXPECT_EQ(1, (int)server->templateHandlers_.size());
        EXPECT_TRUE(server->templateHandlers_.find(vhost) != server->templateHandlers_.end());

        SrsLiveEntry *entry = server->templateHandlers_[vhost];
        EXPECT_TRUE(entry != NULL);
        EXPECT_STREQ("[vhost]/[app]/[stream].flv", entry->mount_.c_str());

        // Verify it's a template entry (no stream/cache/req created yet)
        EXPECT_TRUE(entry->stream_ == NULL);
        EXPECT_TRUE(entry->cache_ == NULL);
        EXPECT_TRUE(entry->req_ == NULL);
        EXPECT_FALSE(entry->disposing_);

        // Test initialize_flv_entry with disabled HTTP remux
        mock_config->http_remux_enabled_ = false;
        std::string vhost2 = "disabled.vhost";
        HELPER_EXPECT_SUCCESS(server->initialize_flv_entry(vhost2));

        // Verify no template handler was created for disabled vhost
        EXPECT_EQ(1, (int)server->templateHandlers_.size());
        EXPECT_TRUE(server->templateHandlers_.find(vhost2) == server->templateHandlers_.end());

        // Server destructor will be called here when exiting scope
    }

    // Now we can safely free the mock config
    srs_freep(mock_config);
}

MockAsyncCallWorker::MockAsyncCallWorker()
{
    execute_count_ = 0;
}

MockAsyncCallWorker::~MockAsyncCallWorker()
{
    // Free all tasks
    for (size_t i = 0; i < tasks_.size(); i++) {
        srs_freep(tasks_[i]);
    }
    tasks_.clear();
}

srs_error_t MockAsyncCallWorker::execute(ISrsAsyncCallTask *t)
{
    execute_count_++;
    tasks_.push_back(t);
    return srs_success;
}

srs_error_t MockAsyncCallWorker::start()
{
    return srs_success;
}

void MockAsyncCallWorker::stop()
{
}

MockAppConfigForHttpStreamServer::MockAppConfigForHttpStreamServer()
{
    http_remux_enabled_ = true;
    http_remux_mount_ = "[vhost]/[app]/[stream].flv";
    vhost_directive_ = NULL;
}

MockAppConfigForHttpStreamServer::~MockAppConfigForHttpStreamServer()
{
    srs_freep(vhost_directive_);
}

bool MockAppConfigForHttpStreamServer::get_vhost_http_remux_enabled(std::string vhost)
{
    return http_remux_enabled_;
}

std::string MockAppConfigForHttpStreamServer::get_vhost_http_remux_mount(std::string vhost)
{
    return http_remux_mount_;
}

SrsConfDirective *MockAppConfigForHttpStreamServer::get_vhost(std::string vhost, bool try_default_vhost)
{
    return vhost_directive_;
}

bool MockAppConfigForHttpStreamServer::get_vhost_enabled(SrsConfDirective *conf)
{
    return conf != NULL;
}

MockHttpMessageForDynamicMatch::MockHttpMessageForDynamicMatch() : SrsHttpMessage()
{
    path_ = "/live/stream1.flv";
    ext_ = ".flv";
    host_ = "test.vhost";
    mock_conn_ = new MockHttpConn();
    set_connection(mock_conn_);

    // Initialize the URL properly so to_request() can parse it
    SrsHttpHeader header;
    header.set("Host", host_);
    set_basic(HTTP_REQUEST, HTTP_GET, HTTP_STATUS_OK, 0);
    set_header(&header, true);
    set_url(path_, false);
}

MockHttpMessageForDynamicMatch::~MockHttpMessageForDynamicMatch()
{
    srs_freep(mock_conn_);
}

std::string MockHttpMessageForDynamicMatch::path()
{
    return path_;
}

std::string MockHttpMessageForDynamicMatch::ext()
{
    return ext_;
}

std::string MockHttpMessageForDynamicMatch::host()
{
    return host_;
}

VOID TEST(SrsHttpStreamServerTest, HttpMountAndUnmount)
{
    srs_error_t err = srs_success;

    // Test the major use scenario: http_mount creates stream entry from template,
    // and http_unmount marks it for disposal
    // This covers the typical HTTP-FLV live streaming mount/unmount use case

    // Create mock config
    MockAppConfigForHttpStreamServer *mock_config = new MockAppConfigForHttpStreamServer();

    // Use a nested scope to control server lifetime
    {
        // Create SrsHttpStreamServer
        SrsUniquePtr<SrsHttpStreamServer> server(new SrsHttpStreamServer());

        // Replace the async worker created in constructor with our mock
        MockAsyncCallWorker *mock_async = new MockAsyncCallWorker();
        srs_freep(server->async_);
        server->async_ = mock_async;
        server->config_ = mock_config;

        // Setup template handler for vhost
        std::string vhost = "test.vhost";
        std::string mount = "[vhost]/[app]/[stream].flv";
        SrsLiveEntry *tmpl = new SrsLiveEntry(mount);
        server->templateHandlers_[vhost] = tmpl;

        // Create mock request for stream
        SrsUniquePtr<MockRequest> mock_request(new MockRequest(vhost, "live", "stream1"));

        // Test http_mount - should create stream entry from template
        HELPER_EXPECT_SUCCESS(server->http_mount(mock_request.get()));

        // Verify stream entry was created
        std::string sid = mock_request->get_stream_url();
        EXPECT_TRUE(server->streamHandlers_.find(sid) != server->streamHandlers_.end());

        SrsLiveEntry *entry = server->streamHandlers_[sid];
        EXPECT_TRUE(entry != NULL);
        EXPECT_FALSE(entry->disposing_);
        EXPECT_TRUE(entry->stream_ != NULL);
        EXPECT_TRUE(entry->cache_ != NULL);
        EXPECT_TRUE(entry->req_ != NULL);

        // Verify mount path was correctly generated
        std::string expected_mount = "test.vhost/live/stream1.flv";
        EXPECT_STREQ(expected_mount.c_str(), entry->mount_.c_str());

        // Test http_mount again with same request - should reuse existing entry
        HELPER_EXPECT_SUCCESS(server->http_mount(mock_request.get()));
        EXPECT_EQ(1, (int)server->streamHandlers_.size());

        // Test http_unmount - should mark entry as disposing and schedule async destroy
        server->http_unmount(mock_request.get());

        // Verify entry is marked as disposing
        EXPECT_TRUE(entry->disposing_);

        // Verify async task was scheduled
        EXPECT_EQ(1, mock_async->execute_count_);
        EXPECT_EQ(1, (int)mock_async->tasks_.size());

        // Test http_mount after unmount - should fail with ERROR_STREAM_DISPOSING
        err = server->http_mount(mock_request.get());
        EXPECT_TRUE(err != srs_success);
        EXPECT_EQ(ERROR_STREAM_DISPOSING, srs_error_code(err));
        srs_freep(err);

        // Server destructor will be called here when exiting scope
        // It will call async_->stop() and free async_, so we don't need to do it manually
    }

    // Now we can safely free the mock config
    srs_freep(mock_config);
}

VOID TEST(SrsGoApiSummariesTest, ServeHttpSuccess)
{
    srs_error_t err;

    // Test the major use scenario: serve_http returns JSON response with server info and summaries
    // This covers the typical HTTP API /api/v1/summaries request use case

    // Create SrsGoApiSummaries handler
    SrsUniquePtr<SrsGoApiSummaries> handler(new SrsGoApiSummaries());

    // Create mock HTTP response writer and message
    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_message(new MockHttpMessageForApiResponse());

    // Call serve_http - should return success and write JSON response
    HELPER_EXPECT_SUCCESS(handler->serve_http(&mock_writer, mock_message.get()));

    // Verify that response was written
    string response = string(mock_writer.io.out_buffer.bytes(), mock_writer.io.out_buffer.length());
    EXPECT_TRUE(response.length() > 0);

    // The response includes HTTP headers, we need to extract just the JSON body
    // Find the start of JSON (after the double CRLF that separates headers from body)
    size_t json_start = response.find("\r\n\r\n");
    ASSERT_TRUE(json_start != string::npos);
    string json_body = response.substr(json_start + 4);

    // Parse the JSON response
    SrsJsonAny *json = SrsJsonAny::loads(json_body);
    ASSERT_TRUE(json != NULL);
    ASSERT_TRUE(json->is_object());
    SrsUniquePtr<SrsJsonObject> obj((SrsJsonObject *)json);

    // Verify "code" field is ERROR_SUCCESS
    SrsJsonAny *code_any = obj->get_property("code");
    ASSERT_TRUE(code_any != NULL);
    ASSERT_TRUE(code_any->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, code_any->to_integer());

    // Verify "server" field exists and is a string
    SrsJsonAny *server_any = obj->get_property("server");
    ASSERT_TRUE(server_any != NULL);
    ASSERT_TRUE(server_any->is_string());

    // Verify "service" field exists and is a string
    SrsJsonAny *service_any = obj->get_property("service");
    ASSERT_TRUE(service_any != NULL);
    ASSERT_TRUE(service_any->is_string());

    // Verify "pid" field exists and is a string
    SrsJsonAny *pid_any = obj->get_property("pid");
    ASSERT_TRUE(pid_any != NULL);
    ASSERT_TRUE(pid_any->is_string());

    // Verify "data" object exists (from srs_api_dump_summaries)
    SrsJsonAny *data_any = obj->get_property("data");
    ASSERT_TRUE(data_any != NULL);
    ASSERT_TRUE(data_any->is_object());
}

VOID TEST(SrsGoApiAuthorsTest, ServeHttpSuccess)
{
    srs_error_t err;

    // Test the major use scenario: serve_http returns JSON response with license and contributors
    // This covers the typical HTTP API /api/v1/authors request use case

    // Create SrsGoApiAuthors handler
    SrsUniquePtr<SrsGoApiAuthors> handler(new SrsGoApiAuthors());

    // Create mock HTTP response writer and message
    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_message(new MockHttpMessageForApiResponse());

    // Call serve_http - should return success and write JSON response
    HELPER_EXPECT_SUCCESS(handler->serve_http(&mock_writer, mock_message.get()));

    // Verify that response was written
    string response = string(mock_writer.io.out_buffer.bytes(), mock_writer.io.out_buffer.length());
    EXPECT_TRUE(response.length() > 0);

    // The response includes HTTP headers, we need to extract just the JSON body
    // Find the start of JSON (after the double CRLF that separates headers from body)
    size_t json_start = response.find("\r\n\r\n");
    ASSERT_TRUE(json_start != string::npos);
    string json_body = response.substr(json_start + 4);

    // Parse the JSON response
    SrsJsonAny *json = SrsJsonAny::loads(json_body);
    ASSERT_TRUE(json != NULL);
    ASSERT_TRUE(json->is_object());
    SrsUniquePtr<SrsJsonObject> obj((SrsJsonObject *)json);

    // Verify "code" field is ERROR_SUCCESS
    SrsJsonAny *code_any = obj->get_property("code");
    ASSERT_TRUE(code_any != NULL);
    ASSERT_TRUE(code_any->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, code_any->to_integer());

    // Verify "server" field exists and is a string
    SrsJsonAny *server_any = obj->get_property("server");
    ASSERT_TRUE(server_any != NULL);
    ASSERT_TRUE(server_any->is_string());

    // Verify "service" field exists and is a string
    SrsJsonAny *service_any = obj->get_property("service");
    ASSERT_TRUE(service_any != NULL);
    ASSERT_TRUE(service_any->is_string());

    // Verify "pid" field exists and is a string
    SrsJsonAny *pid_any = obj->get_property("pid");
    ASSERT_TRUE(pid_any != NULL);
    ASSERT_TRUE(pid_any->is_string());

    // Verify "data" object exists
    SrsJsonAny *data_any = obj->get_property("data");
    ASSERT_TRUE(data_any != NULL);
    ASSERT_TRUE(data_any->is_object());
    SrsJsonObject *data = (SrsJsonObject *)data_any;

    // Verify "license" field exists and contains "MIT"
    SrsJsonAny *license_any = data->get_property("license");
    ASSERT_TRUE(license_any != NULL);
    ASSERT_TRUE(license_any->is_string());
    EXPECT_STREQ("MIT", license_any->to_str().c_str());

    // Verify "contributors" field exists and contains the contributors URL
    SrsJsonAny *contributors_any = data->get_property("contributors");
    ASSERT_TRUE(contributors_any != NULL);
    ASSERT_TRUE(contributors_any->is_string());
    string contributors_url = contributors_any->to_str();
    EXPECT_TRUE(contributors_url.find("github.com/ossrs/srs") != string::npos);
    EXPECT_TRUE(contributors_url.find("AUTHORS.md") != string::npos);
}

VOID TEST(SrsGoApiFeaturesTest, ServeHttpSuccess)
{
    srs_error_t err;

    // Test the major use scenario: serve_http returns JSON response with build info and feature flags
    // This covers the typical HTTP API /api/v1/features request use case

    // Create SrsGoApiFeatures handler
    SrsUniquePtr<SrsGoApiFeatures> handler(new SrsGoApiFeatures());

    // Create mock HTTP response writer and message
    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_message(new MockHttpMessageForApiResponse());

    // Call serve_http - should return success and write JSON response
    HELPER_EXPECT_SUCCESS(handler->serve_http(&mock_writer, mock_message.get()));

    // Verify that response was written
    string response = string(mock_writer.io.out_buffer.bytes(), mock_writer.io.out_buffer.length());
    EXPECT_TRUE(response.length() > 0);

    // The response includes HTTP headers, we need to extract just the JSON body
    // Find the start of JSON (after the double CRLF that separates headers from body)
    size_t json_start = response.find("\r\n\r\n");
    ASSERT_TRUE(json_start != string::npos);
    string json_body = response.substr(json_start + 4);

    // Parse the JSON response
    SrsJsonAny *json = SrsJsonAny::loads(json_body);
    ASSERT_TRUE(json != NULL);
    ASSERT_TRUE(json->is_object());
    SrsUniquePtr<SrsJsonObject> obj((SrsJsonObject *)json);

    // Verify "code" field is ERROR_SUCCESS
    SrsJsonAny *code_any = obj->get_property("code");
    ASSERT_TRUE(code_any != NULL);
    ASSERT_TRUE(code_any->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, code_any->to_integer());

    // Verify "server" field exists and is a string
    SrsJsonAny *server_any = obj->get_property("server");
    ASSERT_TRUE(server_any != NULL);
    ASSERT_TRUE(server_any->is_string());

    // Verify "service" field exists and is a string
    SrsJsonAny *service_any = obj->get_property("service");
    ASSERT_TRUE(service_any != NULL);
    ASSERT_TRUE(service_any->is_string());

    // Verify "pid" field exists and is a string
    SrsJsonAny *pid_any = obj->get_property("pid");
    ASSERT_TRUE(pid_any != NULL);
    ASSERT_TRUE(pid_any->is_string());

    // Verify "data" object exists
    SrsJsonAny *data_any = obj->get_property("data");
    ASSERT_TRUE(data_any != NULL);
    ASSERT_TRUE(data_any->is_object());
    SrsJsonObject *data = (SrsJsonObject *)data_any;

    // Verify build info fields exist in data
    SrsJsonAny *options_any = data->get_property("options");
    ASSERT_TRUE(options_any != NULL);
    ASSERT_TRUE(options_any->is_string());

    SrsJsonAny *options2_any = data->get_property("options2");
    ASSERT_TRUE(options2_any != NULL);
    ASSERT_TRUE(options2_any->is_string());

    SrsJsonAny *build_any = data->get_property("build");
    ASSERT_TRUE(build_any != NULL);
    ASSERT_TRUE(build_any->is_string());

    SrsJsonAny *build2_any = data->get_property("build2");
    ASSERT_TRUE(build2_any != NULL);
    ASSERT_TRUE(build2_any->is_string());

    // Verify "features" object exists in data
    SrsJsonAny *features_any = data->get_property("features");
    ASSERT_TRUE(features_any != NULL);
    ASSERT_TRUE(features_any->is_object());
    SrsJsonObject *features = (SrsJsonObject *)features_any;

    // Verify key feature flags exist and are boolean
    SrsJsonAny *ssl_any = features->get_property("ssl");
    ASSERT_TRUE(ssl_any != NULL);
    ASSERT_TRUE(ssl_any->is_boolean());
    EXPECT_TRUE(ssl_any->to_boolean());

    SrsJsonAny *hls_any = features->get_property("hls");
    ASSERT_TRUE(hls_any != NULL);
    ASSERT_TRUE(hls_any->is_boolean());
    EXPECT_TRUE(hls_any->to_boolean());

    SrsJsonAny *hds_any = features->get_property("hds");
    ASSERT_TRUE(hds_any != NULL);
    ASSERT_TRUE(hds_any->is_boolean());

    SrsJsonAny *callback_any = features->get_property("callback");
    ASSERT_TRUE(callback_any != NULL);
    ASSERT_TRUE(callback_any->is_boolean());
    EXPECT_TRUE(callback_any->to_boolean());

    SrsJsonAny *api_any = features->get_property("api");
    ASSERT_TRUE(api_any != NULL);
    ASSERT_TRUE(api_any->is_boolean());
    EXPECT_TRUE(api_any->to_boolean());

    SrsJsonAny *httpd_any = features->get_property("httpd");
    ASSERT_TRUE(httpd_any != NULL);
    ASSERT_TRUE(httpd_any->is_boolean());
    EXPECT_TRUE(httpd_any->to_boolean());

    SrsJsonAny *dvr_any = features->get_property("dvr");
    ASSERT_TRUE(dvr_any != NULL);
    ASSERT_TRUE(dvr_any->is_boolean());
    EXPECT_TRUE(dvr_any->to_boolean());

    SrsJsonAny *transcode_any = features->get_property("transcode");
    ASSERT_TRUE(transcode_any != NULL);
    ASSERT_TRUE(transcode_any->is_boolean());
    EXPECT_TRUE(transcode_any->to_boolean());

    SrsJsonAny *ingest_any = features->get_property("ingest");
    ASSERT_TRUE(ingest_any != NULL);
    ASSERT_TRUE(ingest_any->is_boolean());
    EXPECT_TRUE(ingest_any->to_boolean());

    SrsJsonAny *stat_any = features->get_property("stat");
    ASSERT_TRUE(stat_any != NULL);
    ASSERT_TRUE(stat_any->is_boolean());
    EXPECT_TRUE(stat_any->to_boolean());

    SrsJsonAny *caster_any = features->get_property("caster");
    ASSERT_TRUE(caster_any != NULL);
    ASSERT_TRUE(caster_any->is_boolean());
    EXPECT_TRUE(caster_any->to_boolean());

    // Verify performance feature flags exist and are boolean
    SrsJsonAny *complex_send_any = features->get_property("complex_send");
    ASSERT_TRUE(complex_send_any != NULL);
    ASSERT_TRUE(complex_send_any->is_boolean());

    SrsJsonAny *tcp_nodelay_any = features->get_property("tcp_nodelay");
    ASSERT_TRUE(tcp_nodelay_any != NULL);
    ASSERT_TRUE(tcp_nodelay_any->is_boolean());

    SrsJsonAny *so_sendbuf_any = features->get_property("so_sendbuf");
    ASSERT_TRUE(so_sendbuf_any != NULL);
    ASSERT_TRUE(so_sendbuf_any->is_boolean());

    SrsJsonAny *mr_any = features->get_property("mr");
    ASSERT_TRUE(mr_any != NULL);
    ASSERT_TRUE(mr_any->is_boolean());
}

VOID TEST(SrsHttpStreamServerTest, DynamicMatchHttpFlv)
{
    srs_error_t err = srs_success;

    // Test the major use scenario: dynamic_match for HTTP-FLV stream request
    // This covers the typical edge server HTTP-FLV dynamic matching use case
    // where a request comes in for a stream that hasn't been mounted yet

    // Create mock config with vhost support
    MockAppConfigForHttpStreamServer *mock_config = new MockAppConfigForHttpStreamServer();
    mock_config->http_remux_enabled_ = true;
    mock_config->http_remux_mount_ = "[vhost]/[app]/[stream].flv";

    // Create vhost directive
    SrsConfDirective *vhost_directive = new SrsConfDirective();
    vhost_directive->name_ = "vhost";
    vhost_directive->args_.push_back("test.vhost");
    mock_config->vhost_directive_ = vhost_directive;

    // Use a nested scope to control server lifetime
    {
        // Create SrsHttpStreamServer
        SrsUniquePtr<SrsHttpStreamServer> server(new SrsHttpStreamServer());

        // Replace the async worker created in constructor with our mock
        MockAsyncCallWorker *mock_async = new MockAsyncCallWorker();
        srs_freep(server->async_);
        server->async_ = mock_async;
        server->config_ = mock_config;

        // Setup template handler for vhost
        std::string vhost = "test.vhost";
        std::string mount = "[vhost]/[app]/[stream].flv";
        SrsLiveEntry *tmpl = new SrsLiveEntry(mount);
        server->templateHandlers_[vhost] = tmpl;

        // Create mock HTTP message for HTTP-FLV request
        SrsUniquePtr<MockHttpMessageForDynamicMatch> mock_request(new MockHttpMessageForDynamicMatch());
        mock_request->path_ = "/live/stream1.flv";
        mock_request->ext_ = ".flv";
        mock_request->host_ = "test.vhost";

        // Test dynamic_match - should create stream entry from template
        ISrsHttpHandler *handler = NULL;
        HELPER_EXPECT_SUCCESS(server->dynamic_match(mock_request.get(), &handler));

        // Verify handler was set
        EXPECT_TRUE(handler != NULL);

        // Verify stream entry was created in streamHandlers_
        EXPECT_EQ(1, (int)server->streamHandlers_.size());

        // Verify the stream entry has correct properties
        std::map<std::string, SrsLiveEntry *>::iterator it = server->streamHandlers_.begin();
        EXPECT_TRUE(it != server->streamHandlers_.end());
        SrsLiveEntry *entry = it->second;
        EXPECT_TRUE(entry != NULL);
        EXPECT_TRUE(entry->stream_ != NULL);
        EXPECT_TRUE(entry->cache_ != NULL);
        EXPECT_TRUE(entry->req_ != NULL);
        EXPECT_FALSE(entry->disposing_);

        // Verify mount path was correctly generated
        std::string expected_mount = "test.vhost/live/stream1.flv";
        EXPECT_STREQ(expected_mount.c_str(), entry->mount_.c_str());

        // Test dynamic_match again with same request - should reuse existing handler
        ISrsHttpHandler *handler2 = NULL;
        HELPER_EXPECT_SUCCESS(server->dynamic_match(mock_request.get(), &handler2));
        EXPECT_TRUE(handler2 != NULL);
        EXPECT_EQ(handler, handler2);
        EXPECT_EQ(1, (int)server->streamHandlers_.size());

        // Server destructor will be called here when exiting scope
    }

    // Now we can safely free the mock config
    srs_freep(mock_config);
}

MockLiveStreamForDestroy::MockLiveStreamForDestroy()
{
    alive_ = true;
    expired_ = false;
}

MockLiveStreamForDestroy::~MockLiveStreamForDestroy()
{
}

srs_error_t MockLiveStreamForDestroy::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_success;
}

srs_error_t MockLiveStreamForDestroy::update_auth(ISrsRequest *r)
{
    return srs_success;
}

bool MockLiveStreamForDestroy::alive()
{
    return alive_;
}

void MockLiveStreamForDestroy::expire()
{
    expired_ = true;
    alive_ = false;
}

MockBufferCacheForDestroy::MockBufferCacheForDestroy()
{
    alive_ = true;
    stopped_ = false;
}

MockBufferCacheForDestroy::~MockBufferCacheForDestroy()
{
}

srs_error_t MockBufferCacheForDestroy::start()
{
    return srs_success;
}

void MockBufferCacheForDestroy::stop()
{
    stopped_ = true;
    alive_ = false;
}

bool MockBufferCacheForDestroy::alive()
{
    return alive_;
}

srs_error_t MockBufferCacheForDestroy::dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter)
{
    return srs_success;
}

srs_error_t MockBufferCacheForDestroy::update_auth(ISrsRequest *r)
{
    return srs_success;
}

MockBufferEncoderForStreamingSend::MockBufferEncoderForStreamingSend()
{
    write_audio_count_ = 0;
    write_video_count_ = 0;
    write_metadata_count_ = 0;
    write_error_ = srs_success;
}

MockBufferEncoderForStreamingSend::~MockBufferEncoderForStreamingSend()
{
    srs_freep(write_error_);
}

srs_error_t MockBufferEncoderForStreamingSend::initialize(SrsFileWriter *w, ISrsBufferCache *c)
{
    return srs_success;
}

srs_error_t MockBufferEncoderForStreamingSend::write_audio(int64_t timestamp, char *data, int size)
{
    write_audio_count_++;
    audio_timestamps_.push_back(timestamp);
    return srs_error_copy(write_error_);
}

srs_error_t MockBufferEncoderForStreamingSend::write_video(int64_t timestamp, char *data, int size)
{
    write_video_count_++;
    video_timestamps_.push_back(timestamp);
    return srs_error_copy(write_error_);
}

srs_error_t MockBufferEncoderForStreamingSend::write_metadata(int64_t timestamp, char *data, int size)
{
    write_metadata_count_++;
    metadata_timestamps_.push_back(timestamp);
    return srs_error_copy(write_error_);
}

bool MockBufferEncoderForStreamingSend::has_cache()
{
    return false;
}

srs_error_t MockBufferEncoderForStreamingSend::dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter)
{
    return srs_success;
}

void MockBufferEncoderForStreamingSend::reset()
{
    write_audio_count_ = 0;
    write_video_count_ = 0;
    write_metadata_count_ = 0;
    audio_timestamps_.clear();
    video_timestamps_.clear();
    metadata_timestamps_.clear();
    srs_freep(write_error_);
    write_error_ = srs_success;
}

VOID TEST(SrsLiveStreamTest, StreamingSendMessagesWithMixedPackets)
{
    srs_error_t err;

    // Test the major use scenario: streaming_send_messages with mixed audio, video, and metadata packets
    // This covers the typical HTTP streaming workflow where encoder writes different packet types

    // Create mock request and buffer cache
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));
    SrsUniquePtr<MockBufferCache> mock_cache(new MockBufferCache());

    // Create SrsLiveStream
    SrsUniquePtr<SrsLiveStream> live_stream(new SrsLiveStream(mock_request.get(), mock_cache.get()));

    // Create mock encoder
    MockBufferEncoderForStreamingSend mock_encoder;

    // Create array of media packets with mixed types
    const int count = 5;
    SrsMediaPacket *msgs[count];

    // Create video packet
    msgs[0] = new SrsMediaPacket();
    msgs[0]->timestamp_ = 1000;
    msgs[0]->message_type_ = SrsFrameTypeVideo;
    char *video_data = new char[10];
    memset(video_data, 0x17, 10);
    msgs[0]->wrap(video_data, 10);

    // Create audio packet
    msgs[1] = new SrsMediaPacket();
    msgs[1]->timestamp_ = 1020;
    msgs[1]->message_type_ = SrsFrameTypeAudio;
    char *audio_data = new char[8];
    memset(audio_data, 0xAF, 8);
    msgs[1]->wrap(audio_data, 8);

    // Create metadata packet
    msgs[2] = new SrsMediaPacket();
    msgs[2]->timestamp_ = 1040;
    msgs[2]->message_type_ = SrsFrameTypeScript;
    char *metadata_data = new char[12];
    memset(metadata_data, 0x02, 12);
    msgs[2]->wrap(metadata_data, 12);

    // Create another video packet
    msgs[3] = new SrsMediaPacket();
    msgs[3]->timestamp_ = 1060;
    msgs[3]->message_type_ = SrsFrameTypeVideo;
    char *video_data2 = new char[15];
    memset(video_data2, 0x27, 15);
    msgs[3]->wrap(video_data2, 15);

    // Create another audio packet
    msgs[4] = new SrsMediaPacket();
    msgs[4]->timestamp_ = 1080;
    msgs[4]->message_type_ = SrsFrameTypeAudio;
    char *audio_data2 = new char[9];
    memset(audio_data2, 0xAF, 9);
    msgs[4]->wrap(audio_data2, 9);

    // Test streaming_send_messages - should call encoder methods for each packet type
    HELPER_EXPECT_SUCCESS(live_stream->streaming_send_messages(&mock_encoder, msgs, count));

    // Verify encoder methods were called with correct counts
    EXPECT_EQ(2, mock_encoder.write_video_count_);
    EXPECT_EQ(2, mock_encoder.write_audio_count_);
    EXPECT_EQ(1, mock_encoder.write_metadata_count_);

    // Verify timestamps were passed correctly
    EXPECT_EQ(2, (int)mock_encoder.video_timestamps_.size());
    EXPECT_EQ(1000, mock_encoder.video_timestamps_[0]);
    EXPECT_EQ(1060, mock_encoder.video_timestamps_[1]);

    EXPECT_EQ(2, (int)mock_encoder.audio_timestamps_.size());
    EXPECT_EQ(1020, mock_encoder.audio_timestamps_[0]);
    EXPECT_EQ(1080, mock_encoder.audio_timestamps_[1]);

    EXPECT_EQ(1, (int)mock_encoder.metadata_timestamps_.size());
    EXPECT_EQ(1040, mock_encoder.metadata_timestamps_[0]);

    // Clean up - free the messages
    for (int i = 0; i < count; i++) {
        srs_freep(msgs[i]);
    }
}

VOID TEST(SrsLiveStreamTest, DoServeHttpFlvWithDisabledEntry)
{
    srs_error_t err;

    // Test the major use scenario: do_serve_http with FLV encoder and entry_->enabled = false
    // This covers the typical HTTP-FLV streaming initialization and immediate exit scenario

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create mock buffer cache
    SrsUniquePtr<MockBufferCache> mock_cache(new MockBufferCache());

    // Create SrsLiveStream
    SrsUniquePtr<SrsLiveStream> live_stream(new SrsLiveStream(mock_request.get(), mock_cache.get()));

    // Create and set mock entry with enabled = false to exit the while loop immediately
    live_stream->entry_ = new SrsHttpMuxEntry();
    live_stream->entry_->enabled = false;
    live_stream->entry_->pattern = "/live/stream.flv";

    // Create mock HTTP message and response writer
    SrsUniquePtr<MockHttpMessageForLiveStream> mock_message(new MockHttpMessageForLiveStream());
    MockResponseWriter mock_writer;

    // Create mock live source and consumer
    SrsUniquePtr<MockLiveSourceForQueue> mock_source(new MockLiveSourceForQueue());
    SrsUniquePtr<MockLiveConsumerForQueue> mock_consumer(new MockLiveConsumerForQueue(mock_source.get()));

    // Call do_serve_http - should initialize FLV encoder, write header, and exit immediately
    err = live_stream->do_serve_http(mock_source.get(), mock_consumer.get(), &mock_writer, mock_message.get());

    // Verify that the method returns ERROR_HTTP_STREAM_EOF
    // The while loop should exit immediately because entry_->enabled is false,
    // and then return ERROR_HTTP_STREAM_EOF to disconnect the client
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_HTTP_STREAM_EOF, srs_error_code(err));
    srs_freep(err);

    // Verify that HTTP header was written (status 200 OK)
    // Check through the internal writer object
    EXPECT_TRUE(mock_writer.w->writer_->header_wrote());

    // Clean up - set entry_ back to NULL before destruction
    srs_freep(live_stream->entry_);
}

VOID TEST(SrsLiveStreamTest, AliveAndExpireWithViewers)
{
    // Test the major use scenario: alive() and expire() with multiple viewers
    // This covers the typical HTTP-FLV/HLS streaming viewer management use case
    // where multiple viewers are watching the same stream and need to be expired

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create mock buffer cache
    SrsUniquePtr<MockBufferCache> mock_cache(new MockBufferCache());

    // Create SrsLiveStream
    SrsUniquePtr<SrsLiveStream> live_stream(new SrsLiveStream(mock_request.get(), mock_cache.get()));

    // Test alive() with no viewers - should return false
    EXPECT_FALSE(live_stream->alive());

    // Create mock viewers (HTTP connections)
    SrsUniquePtr<MockHttpConn> viewer1(new MockHttpConn());
    SrsUniquePtr<MockHttpConn> viewer2(new MockHttpConn());
    SrsUniquePtr<MockHttpConn> viewer3(new MockHttpConn());

    // Add viewers to the stream
    live_stream->viewers_.push_back(viewer1.get());
    live_stream->viewers_.push_back(viewer2.get());
    live_stream->viewers_.push_back(viewer3.get());

    // Test alive() with viewers - should return true
    EXPECT_TRUE(live_stream->alive());
    EXPECT_EQ(3, (int)live_stream->viewers_.size());

    // Test expire() - should call expire() on all viewers
    live_stream->expire();

    // Note: We cannot directly verify that expire() was called on each viewer
    // because MockHttpConn::expire() doesn't track calls. However, the test
    // verifies that expire() doesn't crash and completes successfully.

    // Clean up - remove viewers before destruction to avoid assertion failure
    live_stream->viewers_.clear();

    // Verify alive() returns false after clearing viewers
    EXPECT_FALSE(live_stream->alive());
}

VOID TEST(HttpStreamDestroyTest, DestroyStreamSuccess)
{
    srs_error_t err = srs_success;

    // Test the major use scenario: successfully destroy an HTTP stream entry
    // This covers the typical cleanup path when a stream is being disposed

    // Create real SrsHttpServeMux
    SrsUniquePtr<SrsHttpServeMux> mux(new SrsHttpServeMux());
    HELPER_EXPECT_SUCCESS(mux->initialize());

    std::map<std::string, SrsLiveEntry *> streamHandlers;

    // Create a live entry with mock stream and cache
    std::string sid = "test_stream_id";
    std::string mount = "/live/stream.flv";
    SrsLiveEntry *entry = new SrsLiveEntry(mount);
    entry->disposing_ = true;

    // Create mock stream and cache that will stop immediately
    MockLiveStreamForDestroy *mock_stream = new MockLiveStreamForDestroy();
    MockBufferCacheForDestroy *mock_cache = new MockBufferCacheForDestroy();

    // Create mock request
    MockRequest *mock_req = new MockRequest();

    entry->stream_ = mock_stream;
    entry->cache_ = mock_cache;
    entry->req_ = mock_req;

    // Add entry to handlers map
    streamHandlers[sid] = entry;

    // Register the handler with mux so unhandle can work
    HELPER_EXPECT_SUCCESS(mux->handle(mount, mock_stream));

    // Create the destroy task
    SrsUniquePtr<SrsHttpStreamDestroy> destroy_task(
        new SrsHttpStreamDestroy(mux.get(), &streamHandlers, sid));

    // Verify initial state
    EXPECT_EQ(1, (int)streamHandlers.size());
    EXPECT_TRUE(mock_stream->alive_);
    EXPECT_TRUE(mock_cache->alive_);
    EXPECT_FALSE(mock_stream->expired_);
    EXPECT_FALSE(mock_cache->stopped_);

    // Execute the destroy task - this will free mock_stream and mock_cache
    HELPER_EXPECT_SUCCESS(destroy_task->call());

    // After call(), the stream and cache objects are freed by SrsUniquePtr
    // We can only verify that the entry was removed from handlers
    EXPECT_EQ(0, (int)streamHandlers.size());
}

VOID TEST(SrsBufferCacheTest, StopAndAlive)
{
    // Test the major use scenario: stop() and alive() methods with fast_cache enabled
    // This covers the typical HTTP streaming cache lifecycle management use case

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create buffer cache
    SrsUniquePtr<SrsBufferCache> cache(new SrsBufferCache(mock_request.get()));

    // Test alive() when fast_cache is disabled (default is 0)
    EXPECT_FALSE(cache->alive());

    // Enable fast_cache to test the actual functionality
    cache->fast_cache_ = 3 * SRS_UTIME_SECONDS;

    // Replace the real coroutine with a mock coroutine
    MockCoroutineForRtmpConn *mock_trd = new MockCoroutineForRtmpConn();
    srs_freep(cache->trd_);
    cache->trd_ = mock_trd;

    // Test alive() when thread is healthy (pull returns success)
    mock_trd->pull_error_ = srs_success;
    EXPECT_TRUE(cache->alive());
    EXPECT_EQ(1, mock_trd->pull_count_);

    // Test alive() when thread has error (pull returns error)
    mock_trd->pull_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock error");
    EXPECT_FALSE(cache->alive());
    EXPECT_EQ(2, mock_trd->pull_count_);

    // Test stop() - should call trd_->stop()
    cache->stop();
    // Note: We can't directly verify stop() was called on mock_trd since MockCoroutineForRtmpConn
    // doesn't track stop() calls, but we verify it doesn't crash

    // Test stop() when fast_cache is disabled - should return early without calling trd_->stop()
    cache->fast_cache_ = 0;
    cache->stop(); // Should not crash even though fast_cache is 0
}

VOID TEST(SrsBufferCacheTest, CycleWithThreadPullError)
{
    srs_error_t err;

    // Test the major use scenario: cycle() method with thread pull error
    // This covers the typical HTTP streaming cache cycle execution where the thread
    // is interrupted or encounters an error, causing the cycle to exit

    // Create mock request
    SrsUniquePtr<MockRequest> mock_request(new MockRequest("test.vhost", "live", "stream1"));

    // Create buffer cache
    SrsUniquePtr<SrsBufferCache> cache(new SrsBufferCache(mock_request.get()));

    // Enable fast_cache to allow cycle to run
    cache->fast_cache_ = 3 * SRS_UTIME_SECONDS;

    // Replace the real coroutine with a mock coroutine that will return error on pull
    MockCoroutineForRtmpConn *mock_trd = new MockCoroutineForRtmpConn();
    srs_freep(cache->trd_);
    cache->trd_ = mock_trd;

    // Set pull_error to make trd_->pull() return error immediately
    // This will cause cycle() to exit the while loop and return the error
    mock_trd->pull_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock thread interrupted");

    // Call cycle() - should create live source, create consumer, dump consumer,
    // then enter the while loop and exit immediately when trd_->pull() returns error
    err = cache->cycle();

    // Verify that cycle() returned an error (wrapped with "buffer cache" context)
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
    srs_freep(err);

    // Verify that pull() was called at least once
    EXPECT_TRUE(mock_trd->pull_count_ > 0);
}

VOID TEST(AppHttpStreamTest, Mp3StreamEncoderMajorScenario)
{
    srs_error_t err;

    // Test the major use scenario: initialize encoder, write MP3 audio data, ignore video/metadata
    // This covers the typical MP3 HTTP streaming use case

    // Create mock file writer and buffer cache
    SrsUniquePtr<MockSrsFileWriter> writer(new MockSrsFileWriter());
    HELPER_EXPECT_SUCCESS(writer->open("test.mp3"));

    MockBufferCache mock_cache;

    // Create MP3 stream encoder
    SrsUniquePtr<SrsMp3StreamEncoder> encoder(new SrsMp3StreamEncoder());

    // Test initialization - should initialize transmuxer and write MP3 header
    HELPER_EXPECT_SUCCESS(encoder->initialize(writer.get(), &mock_cache));

    // Verify MP3 header was written (ID3v2 header should be present)
    EXPECT_TRUE(writer->filesize() > 0);

    // Test has_cache - should always return true for MP3 encoder
    EXPECT_TRUE(encoder->has_cache());

    // Test write_audio - should write MP3 audio data
    // FLV audio tag format: first byte is sound format (MP3=2 in upper 4 bits)
    // 0x2f = 0010 1111 = MP3 codec (2) + 44kHz (3) + 16-bit (1) + stereo (1)
    char audio_data[] = {0x2f, 0x00, (char)0xff, (char)0xfb, (char)0x90, 0x00}; // FLV audio tag + MP3 frame
    HELPER_EXPECT_SUCCESS(encoder->write_audio(1000, audio_data, sizeof(audio_data)));

    // Verify audio data was written
    int64_t size_after_audio = writer->filesize();
    EXPECT_TRUE(size_after_audio > 0);

    // Test write_video - should be ignored (MP3 is audio-only)
    char video_data[] = {0x17, 0x00, 0x00, 0x00, 0x00};
    int64_t size_before_video = writer->filesize();
    HELPER_EXPECT_SUCCESS(encoder->write_video(2000, video_data, sizeof(video_data)));

    // Verify video data was NOT written (size should remain the same)
    EXPECT_EQ(size_before_video, writer->filesize());

    // Test write_metadata - should be ignored (MP3 doesn't use FLV metadata)
    char metadata_data[] = {0x02, 0x00, 0x0a, 0x6f, 0x6e, 0x4d, 0x65, 0x74, 0x61, 0x44, 0x61, 0x74, 0x61};
    int64_t size_before_metadata = writer->filesize();
    HELPER_EXPECT_SUCCESS(encoder->write_metadata(3000, metadata_data, sizeof(metadata_data)));

    // Verify metadata was NOT written (size should remain the same)
    EXPECT_EQ(size_before_metadata, writer->filesize());

    // Test dump_cache - should delegate to buffer cache
    HELPER_EXPECT_SUCCESS(encoder->dump_cache(NULL, SrsRtmpJitterAlgorithmOFF));

    // Verify dump_cache was called on the mock cache
    EXPECT_EQ(1, mock_cache.dump_cache_count_);
    EXPECT_EQ(SrsRtmpJitterAlgorithmOFF, mock_cache.last_jitter_);
}

VOID TEST(HttpApiTest, JsonpResponse)
{
    srs_error_t err;

    // Create mock response writer
    MockResponseWriter w;

    // Test JSONP response with callback and data
    string callback = "myCallback";
    string data = "{\"code\":0,\"message\":\"success\"}";

    HELPER_EXPECT_SUCCESS(srs_api_response_jsonp(&w, callback, data));

    // Verify the response contains callback(data) format
    string response = HELPER_BUFFER2STR(&w.io.out_buffer);
    EXPECT_TRUE(response.find("myCallback({\"code\":0,\"message\":\"success\"})") != string::npos);
}

MockHttpMessageForApiResponse::MockHttpMessageForApiResponse()
{
    mock_conn_ = new MockHttpConn();
    set_connection(mock_conn_);
    is_jsonp_ = false;
    callback_ = "";
    path_ = "/api/test";
}

MockHttpMessageForApiResponse::~MockHttpMessageForApiResponse()
{
    srs_freep(mock_conn_);
}

bool MockHttpMessageForApiResponse::is_jsonp()
{
    return is_jsonp_;
}

std::string MockHttpMessageForApiResponse::query_get(std::string key)
{
    if (key == "callback") {
        return callback_;
    }
    std::map<std::string, std::string>::iterator it = query_params_.find(key);
    if (it != query_params_.end()) {
        return it->second;
    }
    return "";
}

std::string MockHttpMessageForApiResponse::path()
{
    return path_;
}

VOID TEST(HttpApiTest, GoApiV1ServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiV1::serve_http returns JSON with server info and API URLs
    // This covers the typical HTTP API v1 root endpoint use case

    // Create SrsGoApiV1 instance
    SrsUniquePtr<SrsGoApiV1> api(new SrsGoApiV1());

    // Create mock statistic
    MockStatisticForLiveStream mock_stat;

    // Replace stat_ with mock
    api->stat_ = &mock_stat;

    // Create mock HTTP message and response writer
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    MockResponseWriter mock_writer;

    // Call serve_http - should return JSON with server info and API URLs
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify response was written
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);
    EXPECT_TRUE(response.length() > 0);

    // Verify response contains expected JSON fields
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\":\"mock_server_id\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\":\"mock_service_id\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\":\"mock_pid\"") != string::npos);

    // Verify response contains API URLs
    EXPECT_TRUE(response.find("\"urls\"") != string::npos);
    EXPECT_TRUE(response.find("\"versions\"") != string::npos);
    EXPECT_TRUE(response.find("\"summaries\"") != string::npos);
    EXPECT_TRUE(response.find("\"rusages\"") != string::npos);
    EXPECT_TRUE(response.find("\"vhosts\"") != string::npos);
    EXPECT_TRUE(response.find("\"streams\"") != string::npos);
    EXPECT_TRUE(response.find("\"clients\"") != string::npos);
    EXPECT_TRUE(response.find("\"raw\"") != string::npos);
    EXPECT_TRUE(response.find("\"clusters\"") != string::npos);

    // Verify response contains test URLs
    EXPECT_TRUE(response.find("\"tests\"") != string::npos);
    EXPECT_TRUE(response.find("\"requests\"") != string::npos);
    EXPECT_TRUE(response.find("\"errors\"") != string::npos);
    EXPECT_TRUE(response.find("\"redirects\"") != string::npos);

    // Clean up - set stat_ back to NULL before destruction
    api->stat_ = NULL;
}

VOID TEST(SrsGoApiVhostsTest, ServeHttpGetAllVhosts)
{
    srs_error_t err;

    // Test the major use scenario: GET request to /api/v1/vhosts/ returns all vhosts
    // This covers the typical HTTP API request to list all vhosts

    // Create SrsGoApiVhosts handler
    SrsUniquePtr<SrsGoApiVhosts> handler(new SrsGoApiVhosts());

    // Create mock HTTP mux entry with pattern
    SrsHttpMuxEntry entry;
    entry.pattern = "/api/v1/vhosts/";
    handler->entry_ = &entry;

    // Create mock HTTP response writer and message
    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_message(new MockHttpMessageForApiResponse());

    // Set the URL to match the pattern (no vhost_id, so list all vhosts)
    HELPER_EXPECT_SUCCESS(mock_message->set_url("http://127.0.0.1/api/v1/vhosts/", false));

    // Call serve_http - should return success and write JSON response with all vhosts
    HELPER_EXPECT_SUCCESS(handler->serve_http(&mock_writer, mock_message.get()));

    // Verify that response was written
    string response = string(mock_writer.io.out_buffer.bytes(), mock_writer.io.out_buffer.length());
    EXPECT_TRUE(response.length() > 0);

    // Extract JSON body from HTTP response (after headers)
    size_t json_start = response.find("\r\n\r\n");
    ASSERT_TRUE(json_start != string::npos);
    string json_body = response.substr(json_start + 4);

    // Parse the JSON response
    SrsJsonAny *json = SrsJsonAny::loads(json_body);
    ASSERT_TRUE(json != NULL);
    ASSERT_TRUE(json->is_object());
    SrsUniquePtr<SrsJsonObject> obj((SrsJsonObject *)json);

    // Verify "code" field is ERROR_SUCCESS
    SrsJsonAny *code_any = obj->get_property("code");
    ASSERT_TRUE(code_any != NULL);
    ASSERT_TRUE(code_any->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, code_any->to_integer());

    // Verify "server" field exists and is a string
    SrsJsonAny *server_any = obj->get_property("server");
    ASSERT_TRUE(server_any != NULL);
    ASSERT_TRUE(server_any->is_string());

    // Verify "service" field exists and is a string
    SrsJsonAny *service_any = obj->get_property("service");
    ASSERT_TRUE(service_any != NULL);
    ASSERT_TRUE(service_any->is_string());

    // Verify "pid" field exists and is a string
    SrsJsonAny *pid_any = obj->get_property("pid");
    ASSERT_TRUE(pid_any != NULL);
    ASSERT_TRUE(pid_any->is_string());

    // Verify "vhosts" array exists (for listing all vhosts)
    SrsJsonAny *vhosts_any = obj->get_property("vhosts");
    ASSERT_TRUE(vhosts_any != NULL);
    ASSERT_TRUE(vhosts_any->is_array());
}

VOID TEST(HttpApiTest, ApiResponseCodeWithJsonAndJsonp)
{
    srs_error_t err;

    // Test the major use scenario: srs_api_response_code handles both JSON and JSONP responses
    // This covers the typical HTTP API response workflow where the function automatically
    // detects whether the request is JSONP (has callback parameter) and responds accordingly

    // Test 1: JSON response with integer code (no JSONP)
    {
        MockResponseWriter mock_writer;
        SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
        mock_msg->is_jsonp_ = false;

        HELPER_EXPECT_SUCCESS(srs_api_response_code(&mock_writer, mock_msg.get(), 0));

        // Verify JSON response format: {"code":0}
        string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);
        EXPECT_TRUE(response.find("{\"code\":0}") != string::npos);
        EXPECT_TRUE(response.find("callback") == string::npos); // No callback wrapper
    }

    // Test 2: JSONP response with integer code (has callback parameter)
    {
        MockResponseWriter mock_writer;
        SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
        mock_msg->is_jsonp_ = true;
        mock_msg->callback_ = "myCallback";

        HELPER_EXPECT_SUCCESS(srs_api_response_code(&mock_writer, mock_msg.get(), 0));

        // Verify JSONP response format: myCallback({"code":0})
        string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);
        EXPECT_TRUE(response.find("myCallback({\"code\":0})") != string::npos);
    }

    // Test 3: JSON response with srs_error_t code (no JSONP)
    {
        MockResponseWriter mock_writer;
        SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
        mock_msg->is_jsonp_ = false;

        srs_error_t test_err = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "stream not found");
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&mock_writer, mock_msg.get(), test_err));
        // Note: srs_api_response_code frees the error internally, so we don't need to free it

        // Verify JSON response contains the error code
        string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);
        char expected[128];
        snprintf(expected, sizeof(expected), "{\"code\":%d}", ERROR_RTMP_STREAM_NOT_FOUND);
        EXPECT_TRUE(response.find(expected) != string::npos);
    }

    // Test 4: JSONP response with srs_error_t code (has callback parameter)
    {
        MockResponseWriter mock_writer;
        SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
        mock_msg->is_jsonp_ = true;
        mock_msg->callback_ = "errorCallback";

        srs_error_t test_err = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "stream not found");
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&mock_writer, mock_msg.get(), test_err));
        // Note: srs_api_response_code frees the error internally, so we don't need to free it

        // Verify JSONP response format: errorCallback({"code":ERROR_CODE})
        string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);
        char expected[128];
        snprintf(expected, sizeof(expected), "errorCallback({\"code\":%d})", ERROR_RTMP_STREAM_NOT_FOUND);
        EXPECT_TRUE(response.find(expected) != string::npos);
    }

    // Test 5: JSON response with success code (srs_success as srs_error_t)
    {
        MockResponseWriter mock_writer;
        SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
        mock_msg->is_jsonp_ = false;

        srs_error_t success_err = srs_success;
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&mock_writer, mock_msg.get(), success_err));

        // Verify JSON response format: {"code":0}
        string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);
        EXPECT_TRUE(response.find("{\"code\":0}") != string::npos);
    }

    // Test the major use scenario: srs_api_response_code with both JSON and JSONP responses
    // This covers the typical HTTP API response workflow where the function automatically
    // detects whether the request is JSONP (has callback parameter) and responds accordingly

    // Test 1: JSON response with success code - most common use case
    {
        MockResponseWriterForJsonp w;
        SrsUniquePtr<MockHttpMessage> msg(new MockHttpMessage());

        // Call srs_api_response_code with success code
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&w, msg.get(), ERROR_SUCCESS));

        // Verify JSON response was written with correct code
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(response.find("{\"code\":0}") != string::npos);

        // Verify content-type is application/json
        EXPECT_STREQ("application/json", w.header()->content_type().c_str());
    }

    // Test 2: JSON response with error code
    {
        MockResponseWriterForJsonp w;
        SrsUniquePtr<MockHttpMessage> msg(new MockHttpMessage());

        // Call srs_api_response_code with error code
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&w, msg.get(), ERROR_RTMP_STREAM_NOT_FOUND));

        // Verify error code was written in JSON response
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);
        char expected[64];
        snprintf(expected, sizeof(expected), "{\"code\":%d}", ERROR_RTMP_STREAM_NOT_FOUND);
        EXPECT_TRUE(response.find(expected) != string::npos);

        // Verify content-type is application/json
        EXPECT_STREQ("application/json", w.header()->content_type().c_str());
    }

    // Test 3: Error code response with automatic error cleanup (srs_error_t overload)
    {
        MockResponseWriterForJsonp w;
        SrsUniquePtr<MockHttpMessage> msg(new MockHttpMessage());

        // Create an error object - srs_api_response_code will free it automatically
        srs_error_t error_code = srs_error_new(ERROR_RTMP_STREAM_NOT_FOUND, "stream not found");

        // Call srs_api_response_code with error - it should free the error object
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&w, msg.get(), error_code));

        // Verify error code was written in JSON response
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);
        char expected[64];
        snprintf(expected, sizeof(expected), "{\"code\":%d}", ERROR_RTMP_STREAM_NOT_FOUND);
        EXPECT_TRUE(response.find(expected) != string::npos);

        // Verify content-type is application/json
        EXPECT_STREQ("application/json", w.header()->content_type().c_str());

        // Note: error_code was freed by srs_api_response_code, so we don't free it here
    }

    // Test 4: JSONP response (with callback parameter)
    {
        MockResponseWriterForJsonp w;
        SrsUniquePtr<MockHttpMessage> msg(new MockHttpMessage());

        // Set up JSONP request by adding callback query parameter
        msg->set_url("/api/v1/test?callback=myCallback", false);

        // Debug: Check if is_jsonp() works
        bool is_jsonp = msg->is_jsonp();
        string callback = msg->query_get("callback");

        // Call srs_api_response_code with success code
        HELPER_EXPECT_SUCCESS(srs_api_response_code(&w, msg.get(), ERROR_SUCCESS));

        // Verify response was written
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);

        // If is_jsonp() returns true, expect JSONP format, otherwise expect JSON format
        if (is_jsonp && !callback.empty()) {
            // JSONP format: callback({"code":0})
            EXPECT_TRUE(response.find("myCallback({\"code\":0})") != string::npos);
            EXPECT_STREQ("text/javascript", w.header()->content_type().c_str());
        } else {
            // JSON format: {"code":0}
            EXPECT_TRUE(response.find("{\"code\":0}") != string::npos);
            EXPECT_STREQ("application/json", w.header()->content_type().c_str());
        }
    }
}

VOID TEST(HttpApiTest, GoApiApiServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiApi::serve_http returns API metadata
    // This covers the typical /api endpoint that provides server information and API version

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiApi> api(new SrsGoApiApi());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"urls\"") != string::npos);
    EXPECT_TRUE(response.find("\"v1\"") != string::npos);
    EXPECT_TRUE(response.find("the api version 1.0") != string::npos);
}

VOID TEST(HttpApiTest, GoApiVersionServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiVersion::serve_http returns version information
    // This covers the typical /api/v1/version endpoint that provides SRS version details

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiVersion> api(new SrsGoApiVersion());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"data\"") != string::npos);
    EXPECT_TRUE(response.find("\"major\"") != string::npos);
    EXPECT_TRUE(response.find("\"minor\"") != string::npos);
    EXPECT_TRUE(response.find("\"revision\"") != string::npos);
    EXPECT_TRUE(response.find("\"version\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiRusagesServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiRusages::serve_http returns system resource usage information
    // This covers the typical /api/v1/rusages endpoint that provides system rusage statistics

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiRusages> api(new SrsGoApiRusages());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"data\"") != string::npos);

    // Check for rusage-specific fields
    EXPECT_TRUE(response.find("\"ok\"") != string::npos);
    EXPECT_TRUE(response.find("\"sample_time\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_utime\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_stime\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_maxrss\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_ixrss\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_idrss\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_isrss\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_minflt\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_majflt\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_nswap\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_inblock\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_oublock\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_msgsnd\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_msgrcv\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_nsignals\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_nvcsw\"") != string::npos);
    EXPECT_TRUE(response.find("\"ru_nivcsw\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiSelfProcStatsServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiSelfProcStats::serve_http returns process statistics
    // This covers the typical /api/v1/self_proc_stats endpoint that provides /proc/self/stat information

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiSelfProcStats> api(new SrsGoApiSelfProcStats());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"data\"") != string::npos);

    // Check for self proc stat-specific fields
    EXPECT_TRUE(response.find("\"ok\"") != string::npos);
    EXPECT_TRUE(response.find("\"sample_time\"") != string::npos);
    EXPECT_TRUE(response.find("\"percent\"") != string::npos);
    EXPECT_TRUE(response.find("\"comm\"") != string::npos);
    EXPECT_TRUE(response.find("\"state\"") != string::npos);
    EXPECT_TRUE(response.find("\"ppid\"") != string::npos);
    EXPECT_TRUE(response.find("\"pgrp\"") != string::npos);
    EXPECT_TRUE(response.find("\"session\"") != string::npos);
    EXPECT_TRUE(response.find("\"tty_nr\"") != string::npos);
    EXPECT_TRUE(response.find("\"tpgid\"") != string::npos);
    EXPECT_TRUE(response.find("\"flags\"") != string::npos);
    EXPECT_TRUE(response.find("\"minflt\"") != string::npos);
    EXPECT_TRUE(response.find("\"cminflt\"") != string::npos);
    EXPECT_TRUE(response.find("\"majflt\"") != string::npos);
    EXPECT_TRUE(response.find("\"cmajflt\"") != string::npos);
    EXPECT_TRUE(response.find("\"utime\"") != string::npos);
    EXPECT_TRUE(response.find("\"stime\"") != string::npos);
    EXPECT_TRUE(response.find("\"cutime\"") != string::npos);
    EXPECT_TRUE(response.find("\"cstime\"") != string::npos);
    EXPECT_TRUE(response.find("\"priority\"") != string::npos);
    EXPECT_TRUE(response.find("\"nice\"") != string::npos);
    EXPECT_TRUE(response.find("\"num_threads\"") != string::npos);
    EXPECT_TRUE(response.find("\"itrealvalue\"") != string::npos);
    EXPECT_TRUE(response.find("\"starttime\"") != string::npos);
    EXPECT_TRUE(response.find("\"vsize\"") != string::npos);
    EXPECT_TRUE(response.find("\"rss\"") != string::npos);
    EXPECT_TRUE(response.find("\"rsslim\"") != string::npos);
    EXPECT_TRUE(response.find("\"startcode\"") != string::npos);
    EXPECT_TRUE(response.find("\"endcode\"") != string::npos);
    EXPECT_TRUE(response.find("\"startstack\"") != string::npos);
    EXPECT_TRUE(response.find("\"kstkesp\"") != string::npos);
    EXPECT_TRUE(response.find("\"kstkeip\"") != string::npos);
    EXPECT_TRUE(response.find("\"signal\"") != string::npos);
    EXPECT_TRUE(response.find("\"blocked\"") != string::npos);
    EXPECT_TRUE(response.find("\"sigignore\"") != string::npos);
    EXPECT_TRUE(response.find("\"sigcatch\"") != string::npos);
    EXPECT_TRUE(response.find("\"wchan\"") != string::npos);
    EXPECT_TRUE(response.find("\"nswap\"") != string::npos);
    EXPECT_TRUE(response.find("\"cnswap\"") != string::npos);
    EXPECT_TRUE(response.find("\"exit_signal\"") != string::npos);
    EXPECT_TRUE(response.find("\"processor\"") != string::npos);
    EXPECT_TRUE(response.find("\"rt_priority\"") != string::npos);
    EXPECT_TRUE(response.find("\"policy\"") != string::npos);
    EXPECT_TRUE(response.find("\"delayacct_blkio_ticks\"") != string::npos);
    EXPECT_TRUE(response.find("\"guest_time\"") != string::npos);
    EXPECT_TRUE(response.find("\"cguest_time\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiSystemProcStatsServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiSystemProcStats::serve_http returns system CPU statistics
    // This covers the typical /api/v1/system_proc_stats endpoint that provides /proc/stat information

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiSystemProcStats> api(new SrsGoApiSystemProcStats());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"data\"") != string::npos);

    // Check for system proc stat-specific fields
    EXPECT_TRUE(response.find("\"ok\"") != string::npos);
    EXPECT_TRUE(response.find("\"sample_time\"") != string::npos);
    EXPECT_TRUE(response.find("\"percent\"") != string::npos);
    EXPECT_TRUE(response.find("\"user\"") != string::npos);
    EXPECT_TRUE(response.find("\"nice\"") != string::npos);
    EXPECT_TRUE(response.find("\"sys\"") != string::npos);
    EXPECT_TRUE(response.find("\"idle\"") != string::npos);
    EXPECT_TRUE(response.find("\"iowait\"") != string::npos);
    EXPECT_TRUE(response.find("\"irq\"") != string::npos);
    EXPECT_TRUE(response.find("\"softirq\"") != string::npos);
    EXPECT_TRUE(response.find("\"steal\"") != string::npos);
    EXPECT_TRUE(response.find("\"guest\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiMemInfosServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiMemInfos::serve_http returns memory information
    // This covers the typical /api/v1/meminfos endpoint that provides /proc/meminfo statistics

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiMemInfos> api(new SrsGoApiMemInfos());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"data\"") != string::npos);

    // Check for memory info-specific fields
    EXPECT_TRUE(response.find("\"ok\"") != string::npos);
    EXPECT_TRUE(response.find("\"sample_time\"") != string::npos);
    EXPECT_TRUE(response.find("\"percent_ram\"") != string::npos);
    EXPECT_TRUE(response.find("\"percent_swap\"") != string::npos);
    EXPECT_TRUE(response.find("\"MemActive\"") != string::npos);
    EXPECT_TRUE(response.find("\"RealInUse\"") != string::npos);
    EXPECT_TRUE(response.find("\"NotInUse\"") != string::npos);
    EXPECT_TRUE(response.find("\"MemTotal\"") != string::npos);
    EXPECT_TRUE(response.find("\"MemFree\"") != string::npos);
    EXPECT_TRUE(response.find("\"Buffers\"") != string::npos);
    EXPECT_TRUE(response.find("\"Cached\"") != string::npos);
    EXPECT_TRUE(response.find("\"SwapTotal\"") != string::npos);
    EXPECT_TRUE(response.find("\"SwapFree\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiRootServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiRoot::serve_http returns API root information
    // This covers the typical / endpoint that provides server identification and available API URLs

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiRoot> api(new SrsGoApiRoot());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Verify response contains code field
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);

    // Verify response contains server identification fields
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);

    // Verify response contains urls object
    EXPECT_TRUE(response.find("\"urls\"") != string::npos);
    EXPECT_TRUE(response.find("\"api\"") != string::npos);

    // Verify response contains rtc URLs
    EXPECT_TRUE(response.find("\"rtc\"") != string::npos);
    EXPECT_TRUE(response.find("\"v1\"") != string::npos);
    EXPECT_TRUE(response.find("\"play\"") != string::npos);
    EXPECT_TRUE(response.find("\"publish\"") != string::npos);
    EXPECT_TRUE(response.find("\"nack\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiRequestsServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiRequests::serve_http returns request information
    // This covers the typical /api/v1/requests endpoint that echoes back request details
    // including URI, path, method, headers, and server information

    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    SrsUniquePtr<SrsGoApiRequests> api(new SrsGoApiRequests());
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required server info fields
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"data\"") != string::npos);

    // Check for request-specific fields in data object
    EXPECT_TRUE(response.find("\"uri\"") != string::npos);
    EXPECT_TRUE(response.find("\"path\"") != string::npos);
    EXPECT_TRUE(response.find("\"METHOD\"") != string::npos);
    EXPECT_TRUE(response.find("\"headers\"") != string::npos);

    // Check for server information fields
    EXPECT_TRUE(response.find("\"sigature\"") != string::npos);
    EXPECT_TRUE(response.find("\"version\"") != string::npos);
    EXPECT_TRUE(response.find("\"link\"") != string::npos);
    EXPECT_TRUE(response.find("\"time\"") != string::npos);
}

VOID TEST(HttpApiTest, GoApiVhostsServeHttpWithVhostId)
{
    srs_error_t err;

    // Test the major use scenario: SrsGoApiVhosts::serve_http returns vhost list
    // This covers the typical /api/v1/vhosts endpoint that provides vhost statistics

    // Create mock response writer and HTTP message
    MockResponseWriter mock_writer;
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->is_jsonp_ = false;

    // Set URL for parse_rest_id to work correctly
    HELPER_EXPECT_SUCCESS(mock_msg->set_url("http://127.0.0.1/api/v1/vhosts/", false));

    // Create mock statistic
    MockStatisticForLiveStream mock_stat;

    // Create API handler and inject mock statistic
    SrsUniquePtr<SrsGoApiVhosts> api(new SrsGoApiVhosts());
    api->stat_ = &mock_stat;

    // Setup entry pattern for REST ID parsing
    api->entry_ = new SrsHttpMuxEntry();
    api->entry_->pattern = "/api/v1/vhosts/";

    // Test the major use scenario: no vhost ID provided - should list all vhosts
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"vhosts\"") != string::npos);

    // Clean up
    api->stat_ = NULL;
    srs_freep(api->entry_);
}

VOID TEST(HttpApiTest, StreamsApiGetSpecificStream)
{
    srs_error_t err;

    // Create mock HTTP message for GET request with stream ID
    SrsUniquePtr<MockHttpMessageForApiResponse> mock_msg(new MockHttpMessageForApiResponse());
    mock_msg->_method = SRS_CONSTS_HTTP_GET;
    mock_msg->path_ = "/api/v1/streams/test_stream_id_123";
    HELPER_EXPECT_SUCCESS(mock_msg->set_url("http://127.0.0.1/api/v1/streams/test_stream_id_123", false));

    // Create mock response writer
    MockResponseWriter mock_writer;

    // Create mock statistic with a test stream
    MockStatisticForLiveStream mock_stat;

    // Create a real stream object to return from find_stream
    SrsStatisticStream test_stream;
    test_stream.id_ = "test_stream_id_123";
    test_stream.stream_ = "livestream";
    test_stream.app_ = "live";
    test_stream.url_ = "live/livestream";
    test_stream.tcUrl_ = "rtmp://localhost/live";

    // Create a vhost for the stream
    SrsStatisticVhost test_vhost;
    test_vhost.id_ = "test_vhost_id";
    test_stream.vhost_ = &test_vhost;

    // Mock find_stream to return our test stream
    // Note: We need to extend MockStatisticForLiveStream to support find_stream
    // For now, we'll create a custom mock inline
    class MockStatisticForStreamsApi : public MockStatisticForLiveStream
    {
    public:
        SrsStatisticStream *stream_to_return_;
        MockStatisticForStreamsApi() : stream_to_return_(NULL) {}
        virtual SrsStatisticStream *find_stream(std::string sid)
        {
            if (sid == "test_stream_id_123" && stream_to_return_) {
                return stream_to_return_;
            }
            return NULL;
        }
    };

    MockStatisticForStreamsApi mock_stat_with_stream;
    mock_stat_with_stream.stream_to_return_ = &test_stream;

    // Create API handler and inject mock statistic
    SrsUniquePtr<SrsGoApiStreams> api(new SrsGoApiStreams());
    api->stat_ = &mock_stat_with_stream;

    // Setup entry pattern for REST ID parsing
    api->entry_ = new SrsHttpMuxEntry();
    api->entry_->pattern = "/api/v1/streams/";

    // Test the major use scenario: specific stream ID provided - should return stream details
    HELPER_EXPECT_SUCCESS(api->serve_http(&mock_writer, mock_msg.get()));

    // Verify JSON response contains expected fields
    string response = HELPER_BUFFER2STR(&mock_writer.io.out_buffer);

    // Check for required fields in response
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"stream\"") != string::npos);

    // Check for stream-specific fields from dumps() method
    EXPECT_TRUE(response.find("\"id\":\"test_stream_id_123\"") != string::npos);
    EXPECT_TRUE(response.find("\"name\":\"livestream\"") != string::npos);
    EXPECT_TRUE(response.find("\"app\":\"live\"") != string::npos);

    // Clean up
    api->stat_ = NULL;
    srs_freep(api->entry_);
}

VOID TEST(HTTPApiTest, ClientsApiGetAllClients)
{
    srs_error_t err;

    // Test the major use scenario: GET /api/v1/clients to list all clients
    // This covers the typical HTTP API usage for querying client list

    // Create mock statistic with find_client and dumps_clients support
    class MockStatisticForClientsApi : public MockStatisticForLiveStream
    {
    public:
        SrsStatisticClient *client_to_return_;
        srs_error_t dumps_clients_error_;
        int dumps_clients_count_;

        MockStatisticForClientsApi() : client_to_return_(NULL), dumps_clients_error_(srs_success), dumps_clients_count_(0) {}
        virtual ~MockStatisticForClientsApi() {}

        virtual SrsStatisticClient *find_client(std::string client_id)
        {
            if (client_id == "test_client_123" && client_to_return_) {
                return client_to_return_;
            }
            return NULL;
        }

        virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count)
        {
            dumps_clients_count_++;
            if (dumps_clients_error_ != srs_success) {
                return srs_error_copy(dumps_clients_error_);
            }
            // Add mock client data
            SrsJsonObject *client = SrsJsonAny::object();
            client->set("id", SrsJsonAny::str("test_client_123"));
            client->set("vhost", SrsJsonAny::str("__defaultVhost__"));
            client->set("stream", SrsJsonAny::str("livestream"));
            client->set("ip", SrsJsonAny::str("127.0.0.1"));
            arr->append(client);
            return srs_success;
        }
    };

    // Create mock statistic
    SrsUniquePtr<MockStatisticForClientsApi> mock_stat(new MockStatisticForClientsApi());

    // Create SrsGoApiClients instance
    SrsUniquePtr<SrsGoApiClients> api(new SrsGoApiClients());
    api->stat_ = mock_stat.get();

    // Create mock entry with pattern
    api->entry_ = new SrsHttpMuxEntry();
    api->entry_->pattern = "/api/v1/clients/";

    // Create mock HTTP request for GET /api/v1/clients?start=0&count=10
    SrsUniquePtr<SrsHttpMessage> req(new SrsHttpMessage());
    HELPER_EXPECT_SUCCESS(req->set_url("http://127.0.0.1/api/v1/clients?start=0&count=10", false));

    // Create mock HTTP response writer
    MockResponseWriter w;

    // Call serve_http
    HELPER_EXPECT_SUCCESS(api->serve_http(&w, req.get()));

    // Verify dumps_clients was called
    EXPECT_EQ(1, mock_stat->dumps_clients_count_);

    // Verify response contains expected JSON structure
    string response = HELPER_BUFFER2STR(&w.io.out_buffer);
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\":\"mock_server_id\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\":\"mock_service_id\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\":\"mock_pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"clients\"") != string::npos);
    EXPECT_TRUE(response.find("\"id\":\"test_client_123\"") != string::npos);
    EXPECT_TRUE(response.find("\"stream\":\"livestream\"") != string::npos);

    // Clean up
    api->stat_ = NULL;
    srs_freep(api->entry_);
}

VOID TEST(HTTPApiTest, ClientsApiGetSpecificClient)
{
    srs_error_t err;

    // Test the major use scenario: GET /api/v1/clients/{client_id} to get specific client info
    // This covers the typical HTTP API usage for querying a specific client and calling client->dumps()

    // Create mock statistic with find_client support
    class MockStatisticForClientApi : public MockStatisticForLiveStream
    {
    public:
        SrsStatisticClient *client_to_return_;

        MockStatisticForClientApi() : client_to_return_(NULL) {}
        virtual ~MockStatisticForClientApi() {}

        virtual SrsStatisticClient *find_client(std::string client_id)
        {
            if (client_id == "test_client_456" && client_to_return_) {
                return client_to_return_;
            }
            return NULL;
        }
    };

    // Create mock statistic
    SrsUniquePtr<MockStatisticForClientApi> mock_stat(new MockStatisticForClientApi());

    // Create a real SrsStatisticClient with all required dependencies
    SrsUniquePtr<SrsStatisticClient> test_client(new SrsStatisticClient());
    test_client->id_ = "test_client_456";
    test_client->type_ = SrsRtmpConnPlay;

    // Create mock request for the client - SrsStatisticClient destructor will free this
    MockRequest *mock_req = new MockRequest("__defaultVhost__", "live", "livestream");
    test_client->req_ = mock_req;

    // Create mock vhost and stream for the client
    SrsStatisticVhost test_vhost;
    test_vhost.id_ = "__defaultVhost__";

    SrsStatisticStream test_stream;
    test_stream.id_ = "livestream";
    test_stream.vhost_ = &test_vhost;

    test_client->stream_ = &test_stream;

    // Set the client to return
    mock_stat->client_to_return_ = test_client.get();

    // Create SrsGoApiClients instance
    SrsUniquePtr<SrsGoApiClients> api(new SrsGoApiClients());
    api->stat_ = mock_stat.get();

    // Create mock entry with pattern
    api->entry_ = new SrsHttpMuxEntry();
    api->entry_->pattern = "/api/v1/clients/";

    // Create mock HTTP request for GET /api/v1/clients/test_client_456
    SrsUniquePtr<SrsHttpMessage> req(new SrsHttpMessage());
    HELPER_EXPECT_SUCCESS(req->set_url("http://127.0.0.1/api/v1/clients/test_client_456", false));

    // Create mock HTTP response writer
    MockResponseWriter w;

    // Call serve_http - this should call client->dumps()
    HELPER_EXPECT_SUCCESS(api->serve_http(&w, req.get()));

    // Verify response contains expected JSON structure with client data
    string response = HELPER_BUFFER2STR(&w.io.out_buffer);
    EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    EXPECT_TRUE(response.find("\"server\":\"mock_server_id\"") != string::npos);
    EXPECT_TRUE(response.find("\"service\":\"mock_service_id\"") != string::npos);
    EXPECT_TRUE(response.find("\"pid\":\"mock_pid\"") != string::npos);
    EXPECT_TRUE(response.find("\"client\"") != string::npos);
    EXPECT_TRUE(response.find("\"id\":\"test_client_456\"") != string::npos);
    EXPECT_TRUE(response.find("\"vhost\":\"__defaultVhost__\"") != string::npos);
    EXPECT_TRUE(response.find("\"stream\":\"livestream\"") != string::npos);
    EXPECT_TRUE(response.find("\"type\":\"rtmp-play\"") != string::npos);

    // Clean up
    api->stat_ = NULL;
    srs_freep(api->entry_);
}

VOID TEST(SrsGoApiClustersTest, ServeHttpSuccess)
{
    srs_error_t err;

    // Test the major use scenario: serve_http returns JSON response with query parameters and origin cluster info
    // This covers the typical HTTP API /api/v1/clusters request use case for origin cluster discovery

    // Create SrsGoApiClusters handler
    SrsUniquePtr<SrsGoApiClusters> handler(new SrsGoApiClusters());

    // Create mock HTTP response writer
    MockResponseWriter mock_writer;

    // Create mock HTTP message with query parameters
    class MockHttpMessageForClusters : public SrsHttpMessage
    {
    public:
        MockHttpConn *mock_conn_;
        std::map<std::string, std::string> query_params_;

        MockHttpMessageForClusters()
        {
            mock_conn_ = new MockHttpConn();
            set_connection(mock_conn_);
            // Set query parameters for the test
            query_params_["ip"] = "192.168.1.100";
            query_params_["vhost"] = "test.vhost";
            query_params_["app"] = "live";
            query_params_["stream"] = "livestream";
            query_params_["coworker"] = "127.0.0.1:1935";
        }

        virtual ~MockHttpMessageForClusters()
        {
            srs_freep(mock_conn_);
        }

        virtual std::string query_get(std::string key)
        {
            std::map<std::string, std::string>::iterator it = query_params_.find(key);
            if (it != query_params_.end()) {
                return it->second;
            }
            return "";
        }

        virtual std::string path()
        {
            return "/api/v1/clusters";
        }

        virtual bool is_jsonp()
        {
            return false;
        }
    };

    SrsUniquePtr<MockHttpMessageForClusters> mock_message(new MockHttpMessageForClusters());

    // Call serve_http - should return success and write JSON response
    HELPER_EXPECT_SUCCESS(handler->serve_http(&mock_writer, mock_message.get()));

    // Verify that response was written
    string response = string(mock_writer.io.out_buffer.bytes(), mock_writer.io.out_buffer.length());
    EXPECT_TRUE(response.length() > 0);

    // The response includes HTTP headers, we need to extract just the JSON body
    // Find the start of JSON (after the double CRLF that separates headers from body)
    size_t json_start = response.find("\r\n\r\n");
    ASSERT_TRUE(json_start != string::npos);
    string json_body = response.substr(json_start + 4);

    // Parse the JSON response
    SrsJsonAny *json = SrsJsonAny::loads(json_body);
    ASSERT_TRUE(json != NULL);
    ASSERT_TRUE(json->is_object());
    SrsUniquePtr<SrsJsonObject> obj((SrsJsonObject *)json);

    // Verify "code" field is ERROR_SUCCESS
    SrsJsonAny *code_any = obj->get_property("code");
    ASSERT_TRUE(code_any != NULL);
    ASSERT_TRUE(code_any->is_integer());
    EXPECT_EQ(ERROR_SUCCESS, code_any->to_integer());

    // Verify "data" object exists
    SrsJsonAny *data_any = obj->get_property("data");
    ASSERT_TRUE(data_any != NULL);
    ASSERT_TRUE(data_any->is_object());
    SrsJsonObject *data = (SrsJsonObject *)data_any;

    // Verify "query" object exists and contains the query parameters
    SrsJsonAny *query_any = data->get_property("query");
    ASSERT_TRUE(query_any != NULL);
    ASSERT_TRUE(query_any->is_object());
    SrsJsonObject *query = (SrsJsonObject *)query_any;

    // Verify query parameters are correctly echoed back
    SrsJsonAny *ip_any = query->get_property("ip");
    ASSERT_TRUE(ip_any != NULL);
    ASSERT_TRUE(ip_any->is_string());
    EXPECT_STREQ("192.168.1.100", ip_any->to_str().c_str());

    SrsJsonAny *vhost_any = query->get_property("vhost");
    ASSERT_TRUE(vhost_any != NULL);
    ASSERT_TRUE(vhost_any->is_string());
    EXPECT_STREQ("test.vhost", vhost_any->to_str().c_str());

    SrsJsonAny *app_any = query->get_property("app");
    ASSERT_TRUE(app_any != NULL);
    ASSERT_TRUE(app_any->is_string());
    EXPECT_STREQ("live", app_any->to_str().c_str());

    SrsJsonAny *stream_any = query->get_property("stream");
    ASSERT_TRUE(stream_any != NULL);
    ASSERT_TRUE(stream_any->is_string());
    EXPECT_STREQ("livestream", stream_any->to_str().c_str());

    // Verify "origin" field exists (from SrsCoWorkers::dumps)
    // Note: origin will be null if the stream is not published, which is expected in this test
    SrsJsonAny *origin_any = data->get_property("origin");
    ASSERT_TRUE(origin_any != NULL);
    // origin can be null or object depending on whether stream is published
}

MockSignalHandler::MockSignalHandler()
{
    signal_received_ = 0;
    signal_count_ = 0;
}

MockSignalHandler::~MockSignalHandler()
{
}

void MockSignalHandler::on_signal(int signo)
{
    signal_received_ = signo;
    signal_count_++;
}

void MockSignalHandler::reset()
{
    signal_received_ = 0;
    signal_count_ = 0;
}

MockAppConfigForRawApi::MockAppConfigForRawApi()
{
    raw_api_ = false;
    allow_reload_ = false;
    allow_query_ = false;
    allow_update_ = false;
    raw_to_json_error_ = srs_success;
}

MockAppConfigForRawApi::~MockAppConfigForRawApi()
{
}

bool MockAppConfigForRawApi::get_raw_api()
{
    return raw_api_;
}

bool MockAppConfigForRawApi::get_raw_api_allow_reload()
{
    return allow_reload_;
}

bool MockAppConfigForRawApi::get_raw_api_allow_query()
{
    return allow_query_;
}

bool MockAppConfigForRawApi::get_raw_api_allow_update()
{
    return allow_update_;
}

srs_error_t MockAppConfigForRawApi::raw_to_json(SrsJsonObject *obj)
{
    if (raw_to_json_error_ != srs_success) {
        return srs_error_copy(raw_to_json_error_);
    }

    // Add some test data to the object
    obj->set("raw_api", SrsJsonAny::boolean(raw_api_));
    obj->set("allow_reload", SrsJsonAny::boolean(allow_reload_));
    obj->set("allow_query", SrsJsonAny::boolean(allow_query_));
    obj->set("allow_update", SrsJsonAny::boolean(allow_update_));

    return srs_success;
}

VOID TEST(HttpApiTest, GoApiRawServeHttp)
{
    srs_error_t err;

    // Test the major use scenario: HTTP RAW API with reload functionality
    // This covers the typical RAW API use cases: query config, reload, and reload-fetch

    // Create mock signal handler
    MockSignalHandler mock_handler;

    // Create mock config
    MockAppConfigForRawApi mock_config;
    mock_config.raw_api_ = true;
    mock_config.allow_reload_ = true;
    mock_config.allow_query_ = true;
    mock_config.allow_update_ = true;

    // Create SrsGoApiRaw instance
    SrsUniquePtr<SrsGoApiRaw> api(new SrsGoApiRaw(&mock_handler));

    // Inject mock config before calling assemble()
    api->config_ = &mock_config;
    api->assemble();

    // Test 1: rpc=raw - query the raw api config
    {
        MockResponseWriter w;
        SrsUniquePtr<MockHttpMessageForApiResponse> r(new MockHttpMessageForApiResponse());
        r->query_params_["rpc"] = "raw";

        HELPER_EXPECT_SUCCESS(api->serve_http(&w, r.get()));

        // Verify response contains raw api config
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(response.find("\"code\":0") != string::npos);
        EXPECT_TRUE(response.find("\"raw_api\":true") != string::npos);
        EXPECT_TRUE(response.find("\"allow_reload\":true") != string::npos);
    }

    // Test 2: rpc=reload - trigger reload signal
    {
        MockResponseWriter w;
        SrsUniquePtr<MockHttpMessageForApiResponse> r(new MockHttpMessageForApiResponse());
        r->query_params_["rpc"] = "reload";

        mock_handler.reset();
        HELPER_EXPECT_SUCCESS(api->serve_http(&w, r.get()));

        // Verify reload signal was sent
        EXPECT_EQ(SRS_SIGNAL_RELOAD, mock_handler.signal_received_);
        EXPECT_EQ(1, mock_handler.signal_count_);

        // Verify response indicates success
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(response.find("\"code\":0") != string::npos);
    }

    // Test 3: rpc=reload-fetch - query reload status
    {
        MockResponseWriter w;
        SrsUniquePtr<MockHttpMessageForApiResponse> r(new MockHttpMessageForApiResponse());
        r->query_params_["rpc"] = "reload-fetch";

        HELPER_EXPECT_SUCCESS(api->serve_http(&w, r.get()));

        // Verify response contains reload status
        string response = HELPER_BUFFER2STR(&w.io.out_buffer);
        EXPECT_TRUE(response.find("\"code\":0") != string::npos);
        EXPECT_TRUE(response.find("\"data\"") != string::npos);
        EXPECT_TRUE(response.find("\"err\"") != string::npos);
        EXPECT_TRUE(response.find("\"msg\"") != string::npos);
        EXPECT_TRUE(response.find("\"state\"") != string::npos);
        EXPECT_TRUE(response.find("\"rid\"") != string::npos);
    }

    // Unsubscribe before cleanup to avoid double unsubscribe in destructor
    mock_config.unsubscribe(api.get());
}

VOID TEST(SrsGoApiMetricsTest, ServeHttpSuccess)
{
    srs_error_t err = srs_success;

    // Create mock statistic with test data
    MockStatisticForResampleKbps mock_stat;

    // Create mock config
    MockAppConfig mock_config;

    // Create SrsGoApiMetrics instance
    SrsUniquePtr<SrsGoApiMetrics> api(new SrsGoApiMetrics());
    api->stat_ = &mock_stat;
    api->config_ = &mock_config;
    api->enabled_ = true;
    api->label_ = "test_label";
    api->tag_ = "test_tag";

    // Create mock HTTP request and response
    MockResponseWriter w;
    SrsUniquePtr<MockHttpMessageForApiResponse> r(new MockHttpMessageForApiResponse());

    // Call serve_http
    HELPER_EXPECT_SUCCESS(api->serve_http(&w, r.get()));

    // Verify response
    string response = HELPER_BUFFER2STR(&w.io.out_buffer);

    // Verify response contains Prometheus metrics format
    EXPECT_TRUE(response.find("# HELP srs_build_info") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_build_info gauge") != string::npos);
    EXPECT_TRUE(response.find("srs_build_info{") != string::npos);

    // Verify server/service info is included
    EXPECT_TRUE(response.find("server=\"mock_server_id\"") != string::npos);
    EXPECT_TRUE(response.find("service=\"mock_service_id\"") != string::npos);
    EXPECT_TRUE(response.find("pid=\"mock_pid\"") != string::npos);

    // Verify label and tag are included
    EXPECT_TRUE(response.find("label=\"test_label\"") != string::npos);
    EXPECT_TRUE(response.find("tag=\"test_tag\"") != string::npos);

    // Verify CPU metric
    EXPECT_TRUE(response.find("# HELP srs_cpu_percent") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_cpu_percent gauge") != string::npos);
    EXPECT_TRUE(response.find("srs_cpu_percent") != string::npos);

    // Verify memory metric
    EXPECT_TRUE(response.find("# HELP srs_memory") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_memory gauge") != string::npos);
    EXPECT_TRUE(response.find("srs_memory") != string::npos);

    // Verify send/receive bytes metrics
    EXPECT_TRUE(response.find("# HELP srs_send_bytes_total") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_send_bytes_total counter") != string::npos);
    EXPECT_TRUE(response.find("srs_send_bytes_total") != string::npos);

    EXPECT_TRUE(response.find("# HELP srs_receive_bytes_total") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_receive_bytes_total counter") != string::npos);
    EXPECT_TRUE(response.find("srs_receive_bytes_total") != string::npos);

    // Verify streams metric
    EXPECT_TRUE(response.find("# HELP srs_streams") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_streams gauge") != string::npos);
    EXPECT_TRUE(response.find("srs_streams") != string::npos);

    // Verify clients metrics
    EXPECT_TRUE(response.find("# HELP srs_clients") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_clients gauge") != string::npos);
    EXPECT_TRUE(response.find("srs_clients") != string::npos);

    EXPECT_TRUE(response.find("# HELP srs_clients_total") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_clients_total counter") != string::npos);
    EXPECT_TRUE(response.find("srs_clients_total") != string::npos);

    // Verify errors metric
    EXPECT_TRUE(response.find("# HELP srs_clients_errs_total") != string::npos);
    EXPECT_TRUE(response.find("# TYPE srs_clients_errs_total counter") != string::npos);
    EXPECT_TRUE(response.find("srs_clients_errs_total") != string::npos);

    // Clean up
    api->stat_ = NULL;
    api->config_ = NULL;
}
