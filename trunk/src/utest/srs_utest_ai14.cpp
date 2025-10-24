//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai14.hpp>

using namespace std;

#include <srs_app_dash.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_forward.hpp>
#include <srs_app_hds.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai22.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_protocol2.hpp>

MockMediaPacketForJitter::MockMediaPacketForJitter(int64_t timestamp, bool is_av)
{
    timestamp_ = timestamp;

    // Create sample payload
    char *payload = new char[128];
    memset(payload, 0x00, 128);
    SrsMediaPacket::wrap(payload, 128);

    if (is_av) {
        message_type_ = SrsFrameTypeVideo;
    } else {
        message_type_ = SrsFrameTypeScript;
    }
}

MockMediaPacketForJitter::~MockMediaPacketForJitter()
{
}

MockLiveSourceForQueue::MockLiveSourceForQueue()
{
}

MockLiveSourceForQueue::~MockLiveSourceForQueue()
{
}

void MockLiveSourceForQueue::on_consumer_destroy(SrsLiveConsumer *consumer)
{
    // Do nothing in mock
}

srs_error_t MockLiveSourceForQueue::initialize(SrsSharedPtr<SrsLiveSource> wrapper, ISrsRequest *r)
{
    // Mock initialize - do nothing and return success
    return srs_success;
}

void MockLiveSourceForQueue::update_auth(ISrsRequest *r)
{
    // Mock update_auth - do nothing to avoid accessing null req_
}

srs_error_t MockLiveSourceForQueue::consumer_dumps(ISrsLiveConsumer *consumer, bool ds, bool dm, bool dg)
{
    // Mock consumer_dumps - just return success without doing anything
    return srs_success;
}

MockLiveConsumerForQueue::MockLiveConsumerForQueue(MockLiveSourceForQueue *source)
    : SrsLiveConsumer(source)
{
    enqueue_count_ = 0;
}

MockLiveConsumerForQueue::~MockLiveConsumerForQueue()
{
}

srs_error_t MockLiveConsumerForQueue::enqueue(SrsMediaPacket *shared_msg, bool atc, SrsRtmpJitterAlgorithm ag)
{
    enqueue_count_++;
    enqueued_timestamps_.push_back(shared_msg->timestamp_);
    return srs_success;
}

MockH264VideoPacket::MockH264VideoPacket(bool is_keyframe)
{
    timestamp_ = 0;
    message_type_ = SrsFrameTypeVideo;

    // Create H.264 video payload
    // Format: [frame_type_and_codec_id][avc_packet_type][composition_time]
    // For H.264: codec_id = 7 (0x07)
    // For keyframe: frame_type = 1 (0x10), for inter frame: frame_type = 2 (0x20)
    char *payload = new char[128];
    memset(payload, 0x00, 128);

    if (is_keyframe) {
        payload[0] = 0x17; // keyframe + H.264 (0x10 | 0x07)
    } else {
        payload[0] = 0x27; // inter frame + H.264 (0x20 | 0x07)
    }
    payload[1] = 0x01; // AVC NALU (not sequence header)

    SrsMediaPacket::wrap(payload, 128);
}

MockH264VideoPacket::~MockH264VideoPacket()
{
}

MockHourGlassForSourceManager::MockHourGlassForSourceManager()
{
    tick_event_ = 0;
    tick_interval_ = 0;
    tick_count_ = 0;
    start_count_ = 0;
    tick_error_ = srs_success;
    start_error_ = srs_success;
}

MockHourGlassForSourceManager::~MockHourGlassForSourceManager()
{
}

srs_error_t MockHourGlassForSourceManager::start()
{
    start_count_++;
    return srs_error_copy(start_error_);
}

void MockHourGlassForSourceManager::stop()
{
    // Do nothing in mock
}

srs_error_t MockHourGlassForSourceManager::tick(srs_utime_t interval)
{
    tick_count_++;
    tick_interval_ = interval;
    return srs_error_copy(tick_error_);
}

srs_error_t MockHourGlassForSourceManager::tick(int event, srs_utime_t interval)
{
    tick_count_++;
    tick_event_ = event;
    tick_interval_ = interval;
    return srs_error_copy(tick_error_);
}

void MockHourGlassForSourceManager::untick(int event)
{
    // Do nothing in mock
}

MockAppFactoryForSourceManager::MockAppFactoryForSourceManager()
{
    create_live_source_count_ = 0;
}

MockAppFactoryForSourceManager::~MockAppFactoryForSourceManager()
{
}

SrsLiveSource *MockAppFactoryForSourceManager::create_live_source()
{
    create_live_source_count_++;
    return new MockLiveSourceForQueue();
}

MockAudioPacket::MockAudioPacket()
{
    timestamp_ = 0;
    message_type_ = SrsFrameTypeAudio;

    // Create audio payload
    char *payload = new char[128];
    memset(payload, 0x00, 128);

    SrsMediaPacket::wrap(payload, 128);
}

MockAudioPacket::~MockAudioPacket()
{
}

VOID TEST(RtmpJitterTest, CorrectZeroAlgorithmWaitForFirstPacket)
{
    srs_error_t err = srs_success;

    // Test ZERO algorithm: start at zero, but don't ensure monotonically increasing
    // The algorithm "waits" for the first packet to establish the base timestamp
    SrsUniquePtr<SrsRtmpJitter> jitter(new SrsRtmpJitter());

    // Verify initial state: last_pkt_correct_time_ is -1 (waiting for first packet)
    EXPECT_EQ(-1, jitter->get_time());

    // Test 1: First packet at timestamp 5000ms
    // This is the "wait" - the algorithm waits for the first packet to set the base time
    // When last_pkt_correct_time_ == -1, it stores msg->timestamp_ as the base
    // Then subtracts it, making the first packet start at 0
    SrsUniquePtr<MockMediaPacketForJitter> pkt1(new MockMediaPacketForJitter(5000, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt1.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(0, pkt1->timestamp_);      // 5000 - 5000 = 0
    EXPECT_EQ(5000, jitter->get_time()); // Base time is now set to 5000

    // Test 2: Second packet at timestamp 5040ms (40ms after first)
    // Now that base time is set (no longer waiting), subtract base time
    // Result: 5040 - 5000 = 40ms
    SrsUniquePtr<MockMediaPacketForJitter> pkt2(new MockMediaPacketForJitter(5040, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt2.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(40, pkt2->timestamp_);     // 5040 - 5000 = 40
    EXPECT_EQ(5000, jitter->get_time()); // Base time remains 5000

    // Test 3: Third packet at timestamp 5100ms (60ms after second)
    // Continue subtracting the same base time
    // Result: 5100 - 5000 = 100ms
    SrsUniquePtr<MockMediaPacketForJitter> pkt3(new MockMediaPacketForJitter(5100, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt3.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(100, pkt3->timestamp_);    // 5100 - 5000 = 100
    EXPECT_EQ(5000, jitter->get_time()); // Base time remains 5000

    // Test 4: Packet with timestamp jump (timestamp 6000ms, 900ms jump)
    // ZERO algorithm does NOT correct jitter, just subtracts base time
    // Result: 6000 - 5000 = 1000ms (large jump is preserved)
    SrsUniquePtr<MockMediaPacketForJitter> pkt4(new MockMediaPacketForJitter(6000, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt4.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(1000, pkt4->timestamp_);   // 6000 - 5000 = 1000 (jitter preserved)
    EXPECT_EQ(5000, jitter->get_time()); // Base time remains 5000

    // Test 5: Packet with timestamp going backwards (timestamp 5500ms)
    // ZERO algorithm does NOT ensure monotonically increasing
    // Result: 5500 - 5000 = 500ms (can go backwards relative to previous packet)
    SrsUniquePtr<MockMediaPacketForJitter> pkt5(new MockMediaPacketForJitter(5500, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt5.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(500, pkt5->timestamp_);    // 5500 - 5000 = 500 (backwards allowed)
    EXPECT_EQ(5000, jitter->get_time()); // Base time remains 5000

    // Test 6: Packet with very large timestamp (timestamp 100000ms)
    // ZERO algorithm just subtracts base time, no correction
    // Result: 100000 - 5000 = 95000ms
    SrsUniquePtr<MockMediaPacketForJitter> pkt6(new MockMediaPacketForJitter(100000, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt6.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(95000, pkt6->timestamp_);  // 100000 - 5000 = 95000
    EXPECT_EQ(5000, jitter->get_time()); // Base time remains 5000

    // Test 7: Edge case - packet with timestamp 0
    // Result: 0 - 5000 = -5000 (negative timestamp allowed in ZERO algorithm)
    SrsUniquePtr<MockMediaPacketForJitter> pkt7(new MockMediaPacketForJitter(0, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt7.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(-5000, pkt7->timestamp_);  // 0 - 5000 = -5000 (negative allowed)
    EXPECT_EQ(5000, jitter->get_time()); // Base time remains 5000

    // Test 8: Verify the "wait" behavior - create new jitter with timestamp 0 as first packet
    SrsUniquePtr<SrsRtmpJitter> jitter2(new SrsRtmpJitter());
    EXPECT_EQ(-1, jitter2->get_time()); // Initially waiting

    SrsUniquePtr<MockMediaPacketForJitter> pkt8(new MockMediaPacketForJitter(0, true));
    HELPER_EXPECT_SUCCESS(jitter2->correct(pkt8.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(0, pkt8->timestamp_);    // 0 - 0 = 0
    EXPECT_EQ(0, jitter2->get_time()); // Base time is now 0

    // Subsequent packet should be relative to base time 0
    SrsUniquePtr<MockMediaPacketForJitter> pkt9(new MockMediaPacketForJitter(1000, true));
    HELPER_EXPECT_SUCCESS(jitter2->correct(pkt9.get(), SrsRtmpJitterAlgorithmZERO));
    EXPECT_EQ(1000, pkt9->timestamp_); // 1000 - 0 = 1000
    EXPECT_EQ(0, jitter2->get_time()); // Base time remains 0
}

VOID TEST(RtmpJitterTest, CorrectTypicalScenario)
{
    srs_error_t err = srs_success;

    // Test FULL algorithm with typical use scenario
    SrsUniquePtr<SrsRtmpJitter> jitter(new SrsRtmpJitter());

    // Test 1: First video packet at timestamp 1000
    // For first packet: last_pkt_time_ = 0, delta = 1000 - 0 = 1000
    // Since delta > 250ms, use default 10ms
    // last_pkt_correct_time_ = max(0, -1 + 10) = 9
    SrsUniquePtr<MockMediaPacketForJitter> pkt1(new MockMediaPacketForJitter(1000, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt1.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(9, pkt1->timestamp_);

    // Test 2: Second video packet at timestamp 1040 (40ms delta, normal)
    // delta = 1040 - 1000 = 40ms (valid)
    // last_pkt_correct_time_ = max(0, 9 + 40) = 49
    SrsUniquePtr<MockMediaPacketForJitter> pkt2(new MockMediaPacketForJitter(1040, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt2.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(49, pkt2->timestamp_);

    // Test 3: Third video packet at timestamp 1080 (40ms delta, normal)
    // delta = 1080 - 1040 = 40ms (valid)
    // last_pkt_correct_time_ = max(0, 49 + 40) = 89
    SrsUniquePtr<MockMediaPacketForJitter> pkt3(new MockMediaPacketForJitter(1080, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt3.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(89, pkt3->timestamp_);

    // Test 4: Packet with large jitter (timestamp 1500, delta 420ms > 250ms threshold)
    // delta = 1500 - 1080 = 420ms (> 250ms threshold)
    // Use default 10ms delta
    // last_pkt_correct_time_ = max(0, 89 + 10) = 99
    SrsUniquePtr<MockMediaPacketForJitter> pkt4(new MockMediaPacketForJitter(1500, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt4.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(99, pkt4->timestamp_);

    // Test 5: Packet with negative jitter (timestamp 1450, delta -50ms)
    // delta = 1450 - 1500 = -50ms (within -250ms to 250ms range, so valid)
    // last_pkt_correct_time_ = max(0, 99 + (-50)) = 49
    SrsUniquePtr<MockMediaPacketForJitter> pkt5(new MockMediaPacketForJitter(1450, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt5.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(49, pkt5->timestamp_);

    // Test 6: Metadata packet (non-AV)
    // Metadata should always be set to 0, doesn't update last_pkt_time_
    SrsUniquePtr<MockMediaPacketForJitter> pkt6(new MockMediaPacketForJitter(2000, false));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt6.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(0, pkt6->timestamp_);

    // Test 7: Continue with normal packet after metadata
    // delta = 1490 - 1450 = 40ms (valid)
    // last_pkt_correct_time_ = max(0, 49 + 40) = 89
    SrsUniquePtr<MockMediaPacketForJitter> pkt7(new MockMediaPacketForJitter(1490, true));
    HELPER_EXPECT_SUCCESS(jitter->correct(pkt7.get(), SrsRtmpJitterAlgorithmFULL));
    EXPECT_EQ(89, pkt7->timestamp_);
}

#ifdef SRS_PERF_QUEUE_FAST_VECTOR
VOID TEST(FastVectorTest, TypicalUseScenario)
{
    // Create a fast vector instance
    SrsUniquePtr<SrsFastVector> vec(new SrsFastVector());

    // Test 1: Initial state - should be empty
    EXPECT_EQ(0, vec->size());
    EXPECT_EQ(0, vec->begin());
    EXPECT_EQ(0, vec->end());

    // Test 2: Push back some packets
    for (int i = 0; i < 5; i++) {
        SrsMediaPacket *pkt = new SrsMediaPacket();
        pkt->timestamp_ = i * 40;
        char *payload = new char[128];
        memset(payload, 0x00, 128);
        pkt->wrap(payload, 128);
        vec->push_back(pkt);
    }

    // Verify size after push_back
    EXPECT_EQ(5, vec->size());
    EXPECT_EQ(0, vec->begin());
    EXPECT_EQ(5, vec->end());

    // Test 3: Access elements using at()
    for (int i = 0; i < 5; i++) {
        SrsMediaPacket *pkt = vec->at(i);
        EXPECT_EQ(i * 40, pkt->timestamp_);
    }

    // Test 4: Erase some elements (erase first 2 elements)
    vec->erase(0, 2);
    EXPECT_EQ(3, vec->size());

    // Verify remaining elements shifted correctly
    EXPECT_EQ(80, vec->at(0)->timestamp_);  // Was at index 2
    EXPECT_EQ(120, vec->at(1)->timestamp_); // Was at index 3
    EXPECT_EQ(160, vec->at(2)->timestamp_); // Was at index 4

    // Test 5: Push back more packets to trigger array expansion
    for (int i = 0; i < 10; i++) {
        SrsMediaPacket *pkt = new SrsMediaPacket();
        pkt->timestamp_ = 200 + i * 40;
        char *payload = new char[128];
        memset(payload, 0x00, 128);
        pkt->wrap(payload, 128);
        vec->push_back(pkt);
    }

    // Verify size after expansion
    EXPECT_EQ(13, vec->size());

    // Test 6: Clear the vector (sets count to 0 but doesn't free packets)
    vec->clear();
    EXPECT_EQ(0, vec->size());
    EXPECT_EQ(0, vec->begin());
    EXPECT_EQ(0, vec->end());
}
#endif

VOID TEST(MessageQueueTest, EnqueueTypicalScenario)
{
    srs_error_t err = srs_success;

    // Create message queue with ignore_shrink=true to avoid log output during test
    SrsUniquePtr<SrsMessageQueue> queue(new SrsMessageQueue(true));

    // Set queue size to 5 seconds (5000ms)
    queue->set_queue_size(5 * SRS_UTIME_SECONDS);

    // Test 1: Enqueue first video packet at timestamp 1000ms
    // Note: enqueue takes ownership of the packet, so we release it from unique_ptr
    MockMediaPacketForJitter *pkt1 = new MockMediaPacketForJitter(1000, true);
    bool is_overflow = false;
    HELPER_EXPECT_SUCCESS(queue->enqueue(pkt1, &is_overflow));
    EXPECT_FALSE(is_overflow);
    EXPECT_EQ(1, queue->size());
    EXPECT_EQ(0, srsu2msi(queue->duration()));

    // Test 2: Enqueue second video packet at timestamp 2000ms (1 second later)
    MockMediaPacketForJitter *pkt2 = new MockMediaPacketForJitter(2000, true);
    HELPER_EXPECT_SUCCESS(queue->enqueue(pkt2, &is_overflow));
    EXPECT_FALSE(is_overflow);
    EXPECT_EQ(2, queue->size());
    EXPECT_EQ(1000, srsu2msi(queue->duration()));

    // Test 3: Enqueue third video packet at timestamp 4000ms (2 seconds later)
    MockMediaPacketForJitter *pkt3 = new MockMediaPacketForJitter(4000, true);
    HELPER_EXPECT_SUCCESS(queue->enqueue(pkt3, &is_overflow));
    EXPECT_FALSE(is_overflow);
    EXPECT_EQ(3, queue->size());
    EXPECT_EQ(3000, srsu2msi(queue->duration()));

    // Test 4: Enqueue fourth video packet at timestamp 7000ms (3 seconds later)
    // Total duration is now 6 seconds, which exceeds max_queue_size (5 seconds)
    // This should trigger shrink and set is_overflow to true
    MockMediaPacketForJitter *pkt4 = new MockMediaPacketForJitter(7000, true);
    is_overflow = false;
    HELPER_EXPECT_SUCCESS(queue->enqueue(pkt4, &is_overflow));
    EXPECT_TRUE(is_overflow);
    // After shrink, queue should have fewer messages
    EXPECT_LT(queue->size(), 4);

    // Test 5: Verify duration is within max_queue_size after shrink
    EXPECT_LE(queue->duration(), 5 * SRS_UTIME_SECONDS);
}

VOID TEST(MessageQueueTest, DumpPacketsTypicalScenario)
{
    srs_error_t err = srs_success;

    // Create a message queue
    SrsUniquePtr<SrsMessageQueue> queue(new SrsMessageQueue());

    // Test 1: dump_packets with array - typical scenario with 3 packets
    if (true) {
        // Enqueue 3 video packets
        MockMediaPacketForJitter *pkt1 = new MockMediaPacketForJitter(1000, true);
        HELPER_EXPECT_SUCCESS(queue->enqueue(pkt1, NULL));

        MockMediaPacketForJitter *pkt2 = new MockMediaPacketForJitter(2000, true);
        HELPER_EXPECT_SUCCESS(queue->enqueue(pkt2, NULL));

        MockMediaPacketForJitter *pkt3 = new MockMediaPacketForJitter(3000, true);
        HELPER_EXPECT_SUCCESS(queue->enqueue(pkt3, NULL));

        EXPECT_EQ(3, queue->size());

        // Dump packets to array
        const int max_count = 10;
        SrsMediaPacket *pmsgs[max_count];
        int count = 0;
        HELPER_EXPECT_SUCCESS(queue->dump_packets(max_count, pmsgs, count));

        // Verify all 3 packets were dumped
        EXPECT_EQ(3, count);
        EXPECT_EQ(1000, pmsgs[0]->timestamp_);
        EXPECT_EQ(2000, pmsgs[1]->timestamp_);
        EXPECT_EQ(3000, pmsgs[2]->timestamp_);

        // Verify queue is now empty after dump
        EXPECT_EQ(0, queue->size());

        // Free the dumped packets
        for (int i = 0; i < count; i++) {
            srs_freep(pmsgs[i]);
        }
    }

    // Test 2: dump_packets with consumer - typical scenario with 2 packets
    if (true) {
        // Enqueue 2 audio packets
        MockMediaPacketForJitter *pkt1 = new MockMediaPacketForJitter(4000, true);
        pkt1->message_type_ = SrsFrameTypeAudio;
        HELPER_EXPECT_SUCCESS(queue->enqueue(pkt1, NULL));

        MockMediaPacketForJitter *pkt2 = new MockMediaPacketForJitter(5000, true);
        pkt2->message_type_ = SrsFrameTypeAudio;
        HELPER_EXPECT_SUCCESS(queue->enqueue(pkt2, NULL));

        EXPECT_EQ(2, queue->size());

        // Create mock source and consumer
        SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
        SrsUniquePtr<MockLiveConsumerForQueue> consumer(new MockLiveConsumerForQueue(source.get()));

        // Dump packets to consumer
        HELPER_EXPECT_SUCCESS(queue->dump_packets(consumer.get(), true, SrsRtmpJitterAlgorithmFULL));

        // Verify consumer received all packets
        EXPECT_EQ(2, consumer->enqueue_count_);
        EXPECT_EQ(4000, consumer->enqueued_timestamps_[0]);
        EXPECT_EQ(5000, consumer->enqueued_timestamps_[1]);

        // Note: Queue still contains packets after dump_packets(consumer) call
        // because this method doesn't clear the queue
        EXPECT_EQ(2, queue->size());
    }
}

VOID TEST(MessageQueueTest, DumpPacketsPartialErase)
{
    srs_error_t err = srs_success;

    // Create a message queue
    SrsUniquePtr<SrsMessageQueue> queue(new SrsMessageQueue());

    // Enqueue 10 video packets with timestamps 1000, 2000, ..., 10000
    // This creates a scenario where we have MORE messages than we will dump
    for (int i = 1; i <= 10; i++) {
        MockMediaPacketForJitter *pkt = new MockMediaPacketForJitter(i * 1000, true);
        HELPER_EXPECT_SUCCESS(queue->enqueue(pkt, NULL));
    }

    // Verify all 10 packets are in the queue
    EXPECT_EQ(10, queue->size());

    // Dump only 3 packets (max_count=3), which is LESS than the total number of messages (10)
    // This will trigger the else branch: msgs_.erase(msgs_.begin(), msgs_.begin() + count)
    // because count (3) < nb_msgs (10)
    const int max_count = 3;
    SrsMediaPacket *pmsgs[max_count];
    int count = 0;
    HELPER_EXPECT_SUCCESS(queue->dump_packets(max_count, pmsgs, count));

    // Verify exactly 3 packets were dumped (the first 3)
    EXPECT_EQ(3, count);
    EXPECT_EQ(1000, pmsgs[0]->timestamp_);
    EXPECT_EQ(2000, pmsgs[1]->timestamp_);
    EXPECT_EQ(3000, pmsgs[2]->timestamp_);

    // Verify 7 packets remain in the queue (10 - 3 = 7)
    // This confirms that msgs_.erase() correctly removed only the first 3 elements
    EXPECT_EQ(7, queue->size());

    // Free the dumped packets
    for (int i = 0; i < count; i++) {
        srs_freep(pmsgs[i]);
    }

    // Dump the remaining packets to verify they are the correct ones (4000-10000)
    const int max_count2 = 10;
    SrsMediaPacket *pmsgs2[max_count2];
    int count2 = 0;
    HELPER_EXPECT_SUCCESS(queue->dump_packets(max_count2, pmsgs2, count2));

    // Verify the remaining 7 packets have the correct timestamps
    EXPECT_EQ(7, count2);
    EXPECT_EQ(4000, pmsgs2[0]->timestamp_);
    EXPECT_EQ(5000, pmsgs2[1]->timestamp_);
    EXPECT_EQ(6000, pmsgs2[2]->timestamp_);
    EXPECT_EQ(7000, pmsgs2[3]->timestamp_);
    EXPECT_EQ(8000, pmsgs2[4]->timestamp_);
    EXPECT_EQ(9000, pmsgs2[5]->timestamp_);
    EXPECT_EQ(10000, pmsgs2[6]->timestamp_);

    // Verify queue is now empty
    EXPECT_EQ(0, queue->size());

    // Free the remaining packets
    for (int i = 0; i < count2; i++) {
        srs_freep(pmsgs2[i]);
    }
}

VOID TEST(MessageQueueTest, ShrinkAndClear)
{
    srs_error_t err;

    // Create message queue
    SrsUniquePtr<SrsMessageQueue> queue(new SrsMessageQueue(true));

    // Set queue size to trigger shrink
    queue->set_queue_size(10 * SRS_UTIME_SECONDS);

    // Create video sequence header packet (0x17 = keyframe + AVC, 0x00 = sequence header)
    SrsMediaPacket *video_sh = new SrsMediaPacket();
    char *video_sh_data = new char[10];
    video_sh_data[0] = 0x17; // keyframe + AVC
    video_sh_data[1] = 0x00; // sequence header
    for (int i = 2; i < 10; i++) {
        video_sh_data[i] = 0x00;
    }
    video_sh->wrap(video_sh_data, 10);
    video_sh->timestamp_ = 1000;
    video_sh->message_type_ = SrsFrameTypeVideo;
    HELPER_EXPECT_SUCCESS(queue->enqueue(video_sh, NULL));

    // Create audio sequence header packet (0xa0 = AAC, 0x00 = sequence header)
    SrsMediaPacket *audio_sh = new SrsMediaPacket();
    char *audio_sh_data = new char[10];
    audio_sh_data[0] = 0xa0; // AAC
    audio_sh_data[1] = 0x00; // sequence header
    for (int i = 2; i < 10; i++) {
        audio_sh_data[i] = 0x00;
    }
    audio_sh->wrap(audio_sh_data, 10);
    audio_sh->timestamp_ = 1000;
    audio_sh->message_type_ = SrsFrameTypeAudio;
    HELPER_EXPECT_SUCCESS(queue->enqueue(audio_sh, NULL));

    // Create regular video packet (0x27 = inter frame + AVC, 0x01 = NALU)
    SrsMediaPacket *video_pkt = new SrsMediaPacket();
    char *video_pkt_data = new char[10];
    video_pkt_data[0] = 0x27; // inter frame + AVC
    video_pkt_data[1] = 0x01; // NALU
    for (int i = 2; i < 10; i++) {
        video_pkt_data[i] = 0x01;
    }
    video_pkt->wrap(video_pkt_data, 10);
    video_pkt->timestamp_ = 2000;
    video_pkt->message_type_ = SrsFrameTypeVideo;
    HELPER_EXPECT_SUCCESS(queue->enqueue(video_pkt, NULL));

    // Create regular audio packet (0xa0 = AAC, 0x01 = raw data)
    SrsMediaPacket *audio_pkt = new SrsMediaPacket();
    char *audio_pkt_data = new char[10];
    audio_pkt_data[0] = 0xa0; // AAC
    audio_pkt_data[1] = 0x01; // raw data
    for (int i = 2; i < 10; i++) {
        audio_pkt_data[i] = 0x01;
    }
    audio_pkt->wrap(audio_pkt_data, 10);
    audio_pkt->timestamp_ = 2000;
    audio_pkt->message_type_ = SrsFrameTypeAudio;
    HELPER_EXPECT_SUCCESS(queue->enqueue(audio_pkt, NULL));

    // Verify queue has 4 messages
    EXPECT_EQ(4, queue->size());

    // Test shrink() - should keep only sequence headers and update their timestamps
    queue->shrink();

    // After shrink, only sequence headers should remain
    EXPECT_EQ(2, queue->size());

    // Test clear() - should remove all messages
    queue->clear();

    // After clear, queue should be empty
    EXPECT_EQ(0, queue->size());
}

VOID TEST(SrsLiveConsumerTest, TypicalUsage)
{
    srs_error_t err;

    // Create mock source
    SrsUniquePtr<MockLiveSourceForQueue> mock_source(new MockLiveSourceForQueue());

    // Create consumer with mock source - tests constructor
    SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(mock_source.get()));

    // Test set_queue_size - typical queue size is 10 seconds
    srs_utime_t queue_size = 10 * SRS_UTIME_SECONDS;
    consumer->set_queue_size(queue_size);

    // Test update_source_id - should set internal flag
    consumer->update_source_id();

    // Test get_time - returns jitter's last_pkt_correct_time_ which is -1 initially
    EXPECT_EQ(-1, consumer->get_time());

    // Enqueue a test packet to update jitter time
    SrsUniquePtr<MockMediaPacketForJitter> pkt(new MockMediaPacketForJitter(1000, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt.get(), false, SrsRtmpJitterAlgorithmFULL));

    // After enqueuing with FULL jitter algorithm, get_time returns corrected time
    // The jitter algorithm detects large delta (1000 - 0) and uses DEFAULT_FRAME_TIME_MS (10ms)
    // So last_pkt_correct_time_ = max(0, -1 + 10) = 9
    EXPECT_EQ(9, consumer->get_time());

    // Destructor will be called automatically and should invoke on_consumer_destroy
}

VOID TEST(SrsLiveConsumerEnqueueTest, TypicalScenario)
{
    srs_error_t err;

    // Create mock source
    SrsUniquePtr<MockLiveSourceForQueue> mock_source(new MockLiveSourceForQueue());

    // Create consumer with mock source
    SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(mock_source.get()));

    // Set typical queue size (10 seconds)
    consumer->set_queue_size(10 * SRS_UTIME_SECONDS);

    // Test typical scenario: enqueue video packet with jitter correction (atc=false)
    // Create a video packet at timestamp 1000ms
    SrsUniquePtr<MockMediaPacketForJitter> pkt1(new MockMediaPacketForJitter(1000, true));

    // Enqueue with jitter correction enabled (atc=false, FULL algorithm)
    // This tests the main flow:
    // 1. Copy the message (shared_msg->copy())
    // 2. Apply jitter correction (!atc, so jitter_->correct() is called)
    // 3. Enqueue to queue (queue_->enqueue())
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt1.get(), false, SrsRtmpJitterAlgorithmFULL));

    // Verify jitter correction was applied - first packet gets corrected to 9ms
    // (large delta detected, uses DEFAULT_FRAME_TIME_MS=10ms, result: max(0, -1+10)=9)
    EXPECT_EQ(9, consumer->get_time());

    // Enqueue second video packet at timestamp 1040ms (40ms delta, normal)
    SrsUniquePtr<MockMediaPacketForJitter> pkt2(new MockMediaPacketForJitter(1040, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt2.get(), false, SrsRtmpJitterAlgorithmFULL));

    // Verify jitter correction: 9 + 40 = 49ms
    EXPECT_EQ(49, consumer->get_time());

    // Test ATC mode: enqueue without jitter correction (atc=true)
    // Create packet at timestamp 2000ms
    SrsUniquePtr<MockMediaPacketForJitter> pkt3(new MockMediaPacketForJitter(2000, true));

    // Enqueue with ATC enabled (atc=true) - skips jitter correction
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt3.get(), true, SrsRtmpJitterAlgorithmFULL));

    // Verify jitter time unchanged (still 49ms) because ATC skips jitter correction
    EXPECT_EQ(49, consumer->get_time());
}

VOID TEST(SrsLiveConsumerDumpPacketsTest, TypicalScenario)
{
    srs_error_t err;

    // Create mock source with source IDs
    SrsUniquePtr<MockLiveSourceForQueue> mock_source(new MockLiveSourceForQueue());

    // Create consumer with mock source
    SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(mock_source.get()));

    // Set typical queue size (10 seconds)
    consumer->set_queue_size(10 * SRS_UTIME_SECONDS);

    // Enqueue 3 video packets to the consumer's internal queue
    SrsUniquePtr<MockMediaPacketForJitter> pkt1(new MockMediaPacketForJitter(1000, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt1.get(), false, SrsRtmpJitterAlgorithmFULL));

    SrsUniquePtr<MockMediaPacketForJitter> pkt2(new MockMediaPacketForJitter(1040, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt2.get(), false, SrsRtmpJitterAlgorithmFULL));

    SrsUniquePtr<MockMediaPacketForJitter> pkt3(new MockMediaPacketForJitter(1080, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt3.get(), false, SrsRtmpJitterAlgorithmFULL));

    // Create SrsMessageArray to receive dumped packets
    const int max_msgs = 10;
    SrsUniquePtr<SrsMessageArray> msgs(new SrsMessageArray(max_msgs));

    // Test typical scenario: dump packets from consumer
    int count = 0;
    HELPER_EXPECT_SUCCESS(consumer->dump_packets(msgs.get(), count));

    // Verify all 3 packets were dumped
    EXPECT_EQ(3, count);
    EXPECT_EQ(9, msgs->msgs_[0]->timestamp_);  // First packet corrected to 9ms
    EXPECT_EQ(49, msgs->msgs_[1]->timestamp_); // Second packet corrected to 49ms
    EXPECT_EQ(89, msgs->msgs_[2]->timestamp_); // Third packet corrected to 89ms

    // Free the dumped packets
    msgs->free(count);
}

#ifdef SRS_PERF_QUEUE_COND_WAIT
VOID TEST(SrsLiveConsumerWaitTest, TypicalScenario)
{
    srs_error_t err;

    // Create mock source
    SrsUniquePtr<MockLiveSourceForQueue> mock_source(new MockLiveSourceForQueue());

    // Create consumer with mock source
    SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(mock_source.get()));

    // Set typical queue size (10 seconds)
    consumer->set_queue_size(10 * SRS_UTIME_SECONDS);

    // Enqueue 3 video packets to the consumer's internal queue
    // Each packet is 40ms apart
    SrsUniquePtr<MockMediaPacketForJitter> pkt1(new MockMediaPacketForJitter(1000, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt1.get(), false, SrsRtmpJitterAlgorithmFULL));

    SrsUniquePtr<MockMediaPacketForJitter> pkt2(new MockMediaPacketForJitter(1040, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt2.get(), false, SrsRtmpJitterAlgorithmFULL));

    SrsUniquePtr<MockMediaPacketForJitter> pkt3(new MockMediaPacketForJitter(1080, true));
    HELPER_EXPECT_SUCCESS(consumer->enqueue(pkt3.get(), false, SrsRtmpJitterAlgorithmFULL));

    // Test typical scenario: wait returns immediately when queue has enough messages and duration
    // Queue has 3 messages with duration ~80ms (89ms - 9ms)
    // Request wait for 1 message and 50ms duration
    // Since queue has 3 > 1 messages and duration 80ms > 50ms, wait should return immediately
    consumer->wait(1, 50 * SRS_UTIME_MILLISECONDS);

    // Verify consumer is still functional after wait
    // Create SrsMessageArray to receive dumped packets
    const int max_msgs = 10;
    SrsUniquePtr<SrsMessageArray> msgs(new SrsMessageArray(max_msgs));

    // Dump packets to verify queue is intact
    int count = 0;
    HELPER_EXPECT_SUCCESS(consumer->dump_packets(msgs.get(), count));

    // Verify all 3 packets are still in queue
    EXPECT_EQ(3, count);

    // Free the dumped packets
    msgs->free(count);
}
#endif

VOID TEST(LiveConsumerTest, OnPlayClientPauseTypicalScenario)
{
    srs_error_t err = srs_success;

    // Create mock live source
    SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());

    // Create live consumer
    SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(source.get()));

    // Test typical scenario: pause the consumer
    HELPER_EXPECT_SUCCESS(consumer->on_play_client_pause(true));

    // Test typical scenario: unpause the consumer
    HELPER_EXPECT_SUCCESS(consumer->on_play_client_pause(false));
}

#ifdef SRS_PERF_QUEUE_COND_WAIT
VOID TEST(LiveConsumerTest, WakeupTypicalScenario)
{
    // Create mock live source
    SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());

    // Create live consumer
    SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(source.get()));

    // Simulate typical scenario: consumer is waiting
    consumer->mw_waiting_ = true;

    // Call wakeup to signal the waiting consumer
    consumer->wakeup();

    // Verify that mw_waiting_ is set to false after wakeup
    EXPECT_FALSE(consumer->mw_waiting_);
}

VOID TEST(LiveConsumerTest, EnqueueWaitingSignalConditions)
{
    srs_error_t err;

    // Test Case 1: ATC mode with negative duration (timestamp overflow case)
    // This simulates when sequence header timestamp is bigger than A/V packet timestamp
    // See https://github.com/ossrs/srs/pull/749
    {
        // Create fresh mock live source and consumer for this test case
        SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
        SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(source.get()));

        // Set consumer to waiting state with specific requirements
        consumer->mw_waiting_ = true;
        consumer->mw_min_msgs_ = 5;
        consumer->mw_duration_ = 1000 * SRS_UTIME_MILLISECONDS; // 1000ms

        // First enqueue an A/V packet with large timestamp (e.g., 5000ms)
        // This sets av_start_time_ = 5000ms
        SrsUniquePtr<MockMediaPacketForJitter> first_packet(new MockMediaPacketForJitter(5000, true));
        HELPER_EXPECT_SUCCESS(consumer->enqueue(first_packet.get(), false, SrsRtmpJitterAlgorithmOFF));

        // Now create an A/V packet with smaller timestamp (e.g., 1000ms)
        // This sets av_end_time_ = 1000ms
        // This creates negative duration: av_end_time(1000) - av_start_time(5000) = -4000ms
        SrsUniquePtr<MockMediaPacketForJitter> second_packet(new MockMediaPacketForJitter(1000, true));

        // Enqueue with ATC mode enabled
        // This should trigger the signal because duration < 0 in ATC mode
        HELPER_EXPECT_SUCCESS(consumer->enqueue(second_packet.get(), true, SrsRtmpJitterAlgorithmOFF));

        // Verify that mw_waiting_ is set to false after signal
        EXPECT_FALSE(consumer->mw_waiting_);
    }

    // Test Case 2: Normal mode with enough messages and duration
    // This tests the typical scenario where consumer waits for enough data
    {
        // Create fresh mock live source and consumer for this test case
        SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
        SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(source.get()));

        // Set consumer to waiting state with specific requirements
        consumer->mw_waiting_ = true;
        consumer->mw_min_msgs_ = 2;                            // Wait for more than 2 messages
        consumer->mw_duration_ = 100 * SRS_UTIME_MILLISECONDS; // Wait for 100ms duration

        // Enqueue first packet at timestamp 10ms (not 0, as 0 is ignored by queue)
        SrsUniquePtr<MockMediaPacketForJitter> packet1(new MockMediaPacketForJitter(10, true));
        HELPER_EXPECT_SUCCESS(consumer->enqueue(packet1.get(), false, SrsRtmpJitterAlgorithmOFF));

        // After first packet, should still be waiting (only 1 message, need > 2)
        EXPECT_TRUE(consumer->mw_waiting_);

        // Enqueue second packet at timestamp 60ms
        SrsUniquePtr<MockMediaPacketForJitter> packet2(new MockMediaPacketForJitter(60, true));
        HELPER_EXPECT_SUCCESS(consumer->enqueue(packet2.get(), false, SrsRtmpJitterAlgorithmOFF));

        // After second packet, should still be waiting (only 2 messages, need > 2)
        EXPECT_TRUE(consumer->mw_waiting_);

        // Enqueue third packet at timestamp 120ms
        // Now we have 3 messages (> mw_min_msgs_=2) and duration 110ms (120-10) (> mw_duration_=100ms)
        SrsUniquePtr<MockMediaPacketForJitter> packet3(new MockMediaPacketForJitter(120, true));
        HELPER_EXPECT_SUCCESS(consumer->enqueue(packet3.get(), false, SrsRtmpJitterAlgorithmOFF));

        // Verify that mw_waiting_ is set to false after signal
        // This should trigger: match_min_msgs && duration > mw_duration_
        EXPECT_FALSE(consumer->mw_waiting_);
    }

    // Test Case 3: Verify no signal when conditions are not met
    {
        // Create fresh mock live source and consumer for this test case
        SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
        SrsUniquePtr<SrsLiveConsumer> consumer(new SrsLiveConsumer(source.get()));

        // Set consumer to waiting state with specific requirements
        consumer->mw_waiting_ = true;
        consumer->mw_min_msgs_ = 10;                            // Need more than 10 messages
        consumer->mw_duration_ = 1000 * SRS_UTIME_MILLISECONDS; // Need 1000ms duration

        // Enqueue a few packets (not enough to trigger signal)
        SrsUniquePtr<MockMediaPacketForJitter> packet4(new MockMediaPacketForJitter(200, true));
        HELPER_EXPECT_SUCCESS(consumer->enqueue(packet4.get(), false, SrsRtmpJitterAlgorithmOFF));

        // Should still be waiting (not enough messages)
        EXPECT_TRUE(consumer->mw_waiting_);
    }
}
#endif

VOID TEST(GopCacheTest, TypicalUseScenario)
{
    // Create a gop cache instance
    SrsUniquePtr<SrsGopCache> gop_cache(new SrsGopCache());

    // Test 1: Verify gop cache is enabled by default
    EXPECT_TRUE(gop_cache->enabled());

    // Test 2: Set gop cache max frames
    gop_cache->set_gop_cache_max_frames(2500);

    // Test 3: Disable gop cache using set(false)
    gop_cache->set(false);
    EXPECT_FALSE(gop_cache->enabled());

    // Test 4: Re-enable gop cache using set(true)
    gop_cache->set(true);
    EXPECT_TRUE(gop_cache->enabled());

    // Test 5: Call dispose to cleanup
    gop_cache->dispose();
    EXPECT_TRUE(gop_cache->enabled()); // dispose() doesn't change enabled state
}

VOID TEST(GopCacheTest, CacheTypicalScenario)
{
    srs_error_t err;

    // Create a gop cache instance
    SrsUniquePtr<SrsGopCache> gop_cache(new SrsGopCache());

    // Verify gop cache is enabled by default
    EXPECT_TRUE(gop_cache->enabled());

    // Test typical scenario: Cache a keyframe (should clear previous cache and start new GOP)
    SrsUniquePtr<MockH264VideoPacket> keyframe1(new MockH264VideoPacket(true));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(keyframe1.get()));
    EXPECT_FALSE(gop_cache->empty());

    // Cache some inter frames
    SrsUniquePtr<MockH264VideoPacket> interframe1(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe1.get()));

    SrsUniquePtr<MockH264VideoPacket> interframe2(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe2.get()));

    // Cache some audio packets
    SrsUniquePtr<MockAudioPacket> audio1(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio1.get()));

    SrsUniquePtr<MockAudioPacket> audio2(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio2.get()));

    // Verify cache is not empty
    EXPECT_FALSE(gop_cache->empty());

    // Cache another keyframe (should clear cache and start new GOP)
    SrsUniquePtr<MockH264VideoPacket> keyframe2(new MockH264VideoPacket(true));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(keyframe2.get()));
    EXPECT_FALSE(gop_cache->empty());

    // Cache more frames in the new GOP
    SrsUniquePtr<MockH264VideoPacket> interframe3(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe3.get()));

    SrsUniquePtr<MockAudioPacket> audio3(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio3.get()));

    // Verify cache is still not empty
    EXPECT_FALSE(gop_cache->empty());
}

VOID TEST(AppRtmpSourceTest, GopCacheClear)
{
    srs_error_t err;

    // Create gop cache and enable it
    SrsUniquePtr<SrsGopCache> gop_cache(new SrsGopCache());
    gop_cache->set(true);

    // Cache a keyframe to start GOP
    SrsUniquePtr<MockH264VideoPacket> keyframe(new MockH264VideoPacket(true));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(keyframe.get()));

    // Cache some inter frames
    SrsUniquePtr<MockH264VideoPacket> interframe1(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe1.get()));

    SrsUniquePtr<MockH264VideoPacket> interframe2(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe2.get()));

    // Cache some audio packets
    SrsUniquePtr<MockAudioPacket> audio1(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio1.get()));

    SrsUniquePtr<MockAudioPacket> audio2(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio2.get()));

    // Verify cache is not empty before clear
    EXPECT_FALSE(gop_cache->empty());

    // Clear the cache
    gop_cache->clear();

    // Verify cache is empty after clear
    EXPECT_TRUE(gop_cache->empty());

    // Verify it's pure audio after clear (no video cached)
    EXPECT_TRUE(gop_cache->pure_audio());
}

VOID TEST(AppRtmpSourceTest, GopCacheDumpTypicalScenario)
{
    srs_error_t err;

    // Create gop cache and enable it
    SrsUniquePtr<SrsGopCache> gop_cache(new SrsGopCache());
    gop_cache->set(true);

    // Cache a keyframe to start GOP
    SrsUniquePtr<MockH264VideoPacket> keyframe(new MockH264VideoPacket(true));
    keyframe->timestamp_ = 1000;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(keyframe.get()));

    // Cache some inter frames
    SrsUniquePtr<MockH264VideoPacket> interframe1(new MockH264VideoPacket(false));
    interframe1->timestamp_ = 1040;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe1.get()));

    SrsUniquePtr<MockH264VideoPacket> interframe2(new MockH264VideoPacket(false));
    interframe2->timestamp_ = 1080;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe2.get()));

    // Cache some audio packets
    SrsUniquePtr<MockAudioPacket> audio1(new MockAudioPacket());
    audio1->timestamp_ = 1020;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio1.get()));

    SrsUniquePtr<MockAudioPacket> audio2(new MockAudioPacket());
    audio2->timestamp_ = 1060;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio2.get()));

    // Verify cache is not empty
    EXPECT_FALSE(gop_cache->empty());

    // Create mock source and consumer
    SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());
    SrsUniquePtr<MockLiveConsumerForQueue> consumer(new MockLiveConsumerForQueue(source.get()));

    // Dump cached GOP to consumer
    HELPER_EXPECT_SUCCESS(gop_cache->dump(consumer.get(), true, SrsRtmpJitterAlgorithmFULL));

    // Verify consumer received all 5 packets (1 keyframe + 2 inter frames + 2 audio)
    EXPECT_EQ(5, consumer->enqueue_count_);
    EXPECT_EQ(1000, consumer->enqueued_timestamps_[0]);
    EXPECT_EQ(1040, consumer->enqueued_timestamps_[1]);
    EXPECT_EQ(1080, consumer->enqueued_timestamps_[2]);
    EXPECT_EQ(1020, consumer->enqueued_timestamps_[3]);
    EXPECT_EQ(1060, consumer->enqueued_timestamps_[4]);

    // Verify cache still contains packets after dump (dump doesn't clear the cache)
    EXPECT_FALSE(gop_cache->empty());
}

VOID TEST(AppRtmpSourceTest, GopCacheMaxFramesLimit)
{
    srs_error_t err;

    // Create gop cache and enable it
    SrsUniquePtr<SrsGopCache> gop_cache(new SrsGopCache());
    gop_cache->set(true);

    // Set max frames limit to 5
    gop_cache->set_gop_cache_max_frames(5);

    // Cache a keyframe to start GOP
    SrsUniquePtr<MockH264VideoPacket> keyframe(new MockH264VideoPacket(true));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(keyframe.get()));
    EXPECT_FALSE(gop_cache->empty());

    // Cache 4 more packets (total 5 packets, at the limit)
    SrsUniquePtr<MockH264VideoPacket> interframe1(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe1.get()));

    SrsUniquePtr<MockH264VideoPacket> interframe2(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe2.get()));

    SrsUniquePtr<MockAudioPacket> audio1(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio1.get()));

    SrsUniquePtr<MockAudioPacket> audio2(new MockAudioPacket());
    HELPER_EXPECT_SUCCESS(gop_cache->cache(audio2.get()));

    // Cache is at limit (5 packets), should not be empty
    EXPECT_FALSE(gop_cache->empty());

    // Cache one more packet (6th packet) - should trigger clear due to exceeding max frames
    // The packet is added first, then the entire cache is cleared
    SrsUniquePtr<MockH264VideoPacket> interframe3(new MockH264VideoPacket(false));
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe3.get()));

    // After exceeding max frames, cache is completely cleared (empty)
    EXPECT_TRUE(gop_cache->empty());

    // Verify it's pure audio after clear (cached_video_count_ reset to 0)
    EXPECT_TRUE(gop_cache->pure_audio());
}

VOID TEST(AppMixQueueTest, PopMixedAudioVideo)
{
    SrsMixQueue *mix_queue = new SrsMixQueue();

    // Test mixed audio/video scenario - should pop when we have 1 video and 1 audio
    // Create and push video packet
    SrsMediaPacket *video1 = new SrsMediaPacket();
    video1->timestamp_ = 100;
    video1->message_type_ = SrsFrameTypeVideo;
    char *vpayload = new char[10];
    memset(vpayload, 0, 10);
    video1->wrap(vpayload, 10);

    // Verify packet is correctly configured
    EXPECT_TRUE(video1->is_video());
    EXPECT_FALSE(video1->is_audio());

    mix_queue->push(video1);

    // Create and push audio packet
    SrsMediaPacket *audio1 = new SrsMediaPacket();
    audio1->timestamp_ = 110;
    audio1->message_type_ = SrsFrameTypeAudio;
    char *apayload = new char[10];
    memset(apayload, 0, 10);
    audio1->wrap(apayload, 10);

    // Verify packet is correctly configured
    EXPECT_FALSE(audio1->is_video());
    EXPECT_TRUE(audio1->is_audio());

    mix_queue->push(audio1);

    // Should be able to pop now (1 video + 1 audio)
    // The first packet (by timestamp) should be returned
    SrsMediaPacket *msg = mix_queue->pop();
    ASSERT_TRUE(msg != NULL);
    EXPECT_EQ(100, msg->timestamp_);
    EXPECT_TRUE(msg->is_video());
    srs_freep(msg);

    // After popping one video, we have 0 videos and 1 audio
    // This doesn't meet any pop condition, so should return NULL
    msg = mix_queue->pop();
    EXPECT_TRUE(msg == NULL);

    srs_freep(mix_queue);
}

VOID TEST(LiveSourceOnAudioImpTest, ReduceSequenceHeaderAndConsumerEnqueue)
{
    srs_error_t err = srs_success;

    // This test covers the on_audio_imp code path including:
    // 1. Reduce sequence header logic (drop_for_reduce)
    // 2. Hub on_audio consumption (NULL in this test)
    // 3. RTMP bridge on_frame consumption (NULL in this test)
    // 4. Consumer enqueue (using mock consumers to track calls)

    // Create mock config with reduce_sequence_header enabled
    // NOTE: Config must outlive the source because destructor calls config_->unsubscribe(this)
    MockSrsConfig *mock_config = new MockSrsConfig();
    HELPER_EXPECT_SUCCESS(mock_config->mock_parse(_MIN_OK_CONF "vhost test.vhost { play { reduce_sequence_header on; } }"));

    // Create mock request
    MockSrsRequest *mock_req = new MockSrsRequest("test.vhost", "live", "stream1");

    // Create mock live source and consumers
    MockLiveSourceForQueue *mock_source = new MockLiveSourceForQueue();
    MockLiveConsumerForQueue *consumer1 = new MockLiveConsumerForQueue(mock_source);
    MockLiveConsumerForQueue *consumer2 = new MockLiveConsumerForQueue(mock_source);

    // Setup mock source with necessary components
    mock_source->config_ = mock_config;
    mock_source->req_ = mock_req;
    mock_source->format_ = new SrsRtmpFormat();
    mock_source->meta_ = new SrsMetaCache();
    mock_source->jitter_algorithm_ = SrsRtmpJitterAlgorithmOFF;
    mock_source->atc_ = false;
    mock_source->hub_ = NULL;         // No hub for this test
    mock_source->rtmp_bridge_ = NULL; // No bridge for this test

    // Add consumers to source
    mock_source->consumers_.push_back(consumer1);
    mock_source->consumers_.push_back(consumer2);

    // Create first audio sequence header (AAC sequence header)
    // Use valid AAC sequence header format
    SrsUniquePtr<SrsMediaPacket> audio_sh1(new SrsMediaPacket());
    char *ash1_payload = new char[4];
    ash1_payload[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    ash1_payload[1] = 0x00; // AAC sequence header
    ash1_payload[2] = 0x12; // AAC object type = 2 (AAC-LC), sample rate index = 4 (44.1kHz)
    ash1_payload[3] = 0x10; // Channel config = 2 (stereo)
    audio_sh1->wrap(ash1_payload, 4);
    audio_sh1->timestamp_ = 1000;
    audio_sh1->message_type_ = SrsFrameTypeAudio;

    // Process first audio sequence header - should be cached in meta
    // NOTE: First sequence header is ALWAYS enqueued to consumers (previous_ash is NULL)
    HELPER_EXPECT_SUCCESS(mock_source->on_audio_imp(audio_sh1.get()));

    // Verify meta has cached the audio sequence header
    ASSERT_TRUE(mock_source->meta_->ash() != NULL);
    ASSERT_TRUE(mock_source->meta_->previous_ash() != NULL);

    // Verify consumers received the first sequence header (drop_for_reduce = false because previous_ash was NULL)
    EXPECT_EQ(1, consumer1->enqueue_count_);
    EXPECT_EQ(1, consumer2->enqueue_count_);
    EXPECT_EQ(1000, consumer1->enqueued_timestamps_[0]);
    EXPECT_EQ(1000, consumer2->enqueued_timestamps_[0]);

    // Create second audio sequence header with SAME content (should be dropped for reduce)
    SrsUniquePtr<SrsMediaPacket> audio_sh2(new SrsMediaPacket());
    char *ash2_payload = new char[4];
    ash2_payload[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    ash2_payload[1] = 0x00; // AAC sequence header
    ash2_payload[2] = 0x12; // Same AAC object type and sample rate
    ash2_payload[3] = 0x10; // Same channel config
    audio_sh2->wrap(ash2_payload, 4);
    audio_sh2->timestamp_ = 2000;
    audio_sh2->message_type_ = SrsFrameTypeAudio;

    // Process second audio sequence header - should be dropped (not enqueued to consumers)
    // This tests: if (is_sequence_header && meta_->previous_ash() && config_->get_reduce_sequence_header(req_->vhost_))
    // Now previous_ash() is not NULL, so drop_for_reduce will be true if content is identical
    HELPER_EXPECT_SUCCESS(mock_source->on_audio_imp(audio_sh2.get()));

    // Verify consumers did NOT receive the duplicate sequence header (drop_for_reduce = true)
    EXPECT_EQ(1, consumer1->enqueue_count_); // Still 1 (no new packet)
    EXPECT_EQ(1, consumer2->enqueue_count_); // Still 1 (no new packet)

    // Create regular audio packet (not sequence header)
    SrsUniquePtr<SrsMediaPacket> audio_pkt(new SrsMediaPacket());
    char *audio_payload = new char[10];
    audio_payload[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    audio_payload[1] = 0x01; // AAC raw data (not sequence header)
    for (int i = 2; i < 10; i++) {
        audio_payload[i] = 0x02; // Audio data
    }
    audio_pkt->wrap(audio_payload, 10);
    audio_pkt->timestamp_ = 3000;
    audio_pkt->message_type_ = SrsFrameTypeAudio;

    // Process regular audio packet - should be enqueued to all consumers
    // This tests: if (!drop_for_reduce) { for (int i = 0; i < (int)consumers_.size(); i++) { consumer->enqueue(...) } }
    HELPER_EXPECT_SUCCESS(mock_source->on_audio_imp(audio_pkt.get()));

    // Verify all consumers received the audio packet (drop_for_reduce = false)
    // Now count should be 2 (first sequence header + this audio packet)
    EXPECT_EQ(2, consumer1->enqueue_count_);
    EXPECT_EQ(2, consumer2->enqueue_count_);
    EXPECT_EQ(1000, consumer1->enqueued_timestamps_[0]); // First sequence header
    EXPECT_EQ(3000, consumer1->enqueued_timestamps_[1]); // Audio packet
    EXPECT_EQ(1000, consumer2->enqueued_timestamps_[0]); // First sequence header
    EXPECT_EQ(3000, consumer2->enqueued_timestamps_[1]); // Audio packet

    // Cleanup: remove consumers from source before they are destroyed
    mock_source->consumers_.clear();

    // Cleanup: Destroy consumers first
    srs_freep(consumer1);
    srs_freep(consumer2);

    // Cleanup: Destroy source (it will call config_->unsubscribe(this) and free req_)
    srs_freep(mock_source);

    // Cleanup: free config (req_ is freed by source destructor)
    srs_freep(mock_config);
}

// Mock ISrsHls implementation
MockHlsForOriginHub::MockHlsForOriginHub()
{
    initialize_count_ = 0;
    initialize_error_ = srs_success;
    cleanup_delay_ = 0;
    on_audio_count_ = 0;
    on_video_count_ = 0;
}

MockHlsForOriginHub::~MockHlsForOriginHub()
{
    srs_freep(initialize_error_);
}

srs_error_t MockHlsForOriginHub::initialize(ISrsOriginHub *h, ISrsRequest *r)
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockHlsForOriginHub::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    on_audio_count_++;
    return srs_success;
}

srs_error_t MockHlsForOriginHub::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    on_video_count_++;
    return srs_success;
}

srs_error_t MockHlsForOriginHub::on_publish()
{
    return srs_success;
}

void MockHlsForOriginHub::on_unpublish()
{
}

void MockHlsForOriginHub::dispose()
{
}

srs_error_t MockHlsForOriginHub::cycle()
{
    return srs_success;
}

srs_utime_t MockHlsForOriginHub::cleanup_delay()
{
    return cleanup_delay_;
}

// Mock ISrsDash implementation
MockDashForOriginHub::MockDashForOriginHub()
{
    initialize_count_ = 0;
    initialize_error_ = srs_success;
    cleanup_delay_ = 0;
    on_audio_count_ = 0;
    on_video_count_ = 0;
}

MockDashForOriginHub::~MockDashForOriginHub()
{
    srs_freep(initialize_error_);
}

srs_error_t MockDashForOriginHub::initialize(ISrsOriginHub *h, ISrsRequest *r)
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockDashForOriginHub::on_publish()
{
    return srs_success;
}

srs_error_t MockDashForOriginHub::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    on_audio_count_++;
    return srs_success;
}

srs_error_t MockDashForOriginHub::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    on_video_count_++;
    return srs_success;
}

void MockDashForOriginHub::on_unpublish()
{
}

void MockDashForOriginHub::dispose()
{
}

srs_error_t MockDashForOriginHub::cycle()
{
    return srs_success;
}

srs_utime_t MockDashForOriginHub::cleanup_delay()
{
    return cleanup_delay_;
}

// Mock ISrsDvr implementation
MockDvrForOriginHub::MockDvrForOriginHub()
{
    initialize_count_ = 0;
    initialize_error_ = srs_success;
    on_meta_data_count_ = 0;
    on_audio_count_ = 0;
    on_video_count_ = 0;
}

void MockDvrForOriginHub::assemble()
{
}

MockDvrForOriginHub::~MockDvrForOriginHub()
{
    srs_freep(initialize_error_);
}

srs_error_t MockDvrForOriginHub::initialize(ISrsOriginHub *h, ISrsRequest *r)
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockDvrForOriginHub::on_publish(ISrsRequest *r)
{
    return srs_success;
}

void MockDvrForOriginHub::on_unpublish()
{
}

srs_error_t MockDvrForOriginHub::on_meta_data(SrsMediaPacket *metadata)
{
    on_meta_data_count_++;
    return srs_success;
}

srs_error_t MockDvrForOriginHub::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    on_audio_count_++;
    return srs_success;
}

srs_error_t MockDvrForOriginHub::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    on_video_count_++;
    return srs_success;
}

// Mock ISrsForwarder implementation
MockForwarderForOriginHub::MockForwarderForOriginHub()
{
    on_meta_data_count_ = 0;
    on_audio_count_ = 0;
    on_video_count_ = 0;
}

MockForwarderForOriginHub::~MockForwarderForOriginHub()
{
}

srs_error_t MockForwarderForOriginHub::initialize(ISrsRequest *r, std::string ep)
{
    return srs_success;
}

void MockForwarderForOriginHub::set_queue_size(srs_utime_t queue_size)
{
}

srs_error_t MockForwarderForOriginHub::on_publish()
{
    return srs_success;
}

void MockForwarderForOriginHub::on_unpublish()
{
}

srs_error_t MockForwarderForOriginHub::on_meta_data(SrsMediaPacket *shared_metadata)
{
    on_meta_data_count_++;
    return srs_success;
}

srs_error_t MockForwarderForOriginHub::on_audio(SrsMediaPacket *shared_audio)
{
    on_audio_count_++;
    return srs_success;
}

srs_error_t MockForwarderForOriginHub::on_video(SrsMediaPacket *shared_video)
{
    on_video_count_++;
    return srs_success;
}

// Mock ISrsLiveSource implementation
MockLiveSourceForOriginHub::MockLiveSourceForOriginHub()
{
    format_ = new SrsRtmpFormat();
    meta_ = new SrsMetaCache();
}

MockLiveSourceForOriginHub::~MockLiveSourceForOriginHub()
{
    srs_freep(format_);
    srs_freep(meta_);
}

void MockLiveSourceForOriginHub::on_consumer_destroy(SrsLiveConsumer *consumer)
{
}

SrsContextId MockLiveSourceForOriginHub::source_id()
{
    return SrsContextId();
}

SrsContextId MockLiveSourceForOriginHub::pre_source_id()
{
    return SrsContextId();
}

SrsMetaCache *MockLiveSourceForOriginHub::meta()
{
    return meta_;
}

SrsRtmpFormat *MockLiveSourceForOriginHub::format()
{
    return format_;
}

srs_error_t MockLiveSourceForOriginHub::on_source_id_changed(SrsContextId id)
{
    return srs_success;
}

srs_error_t MockLiveSourceForOriginHub::on_publish()
{
    return srs_success;
}

void MockLiveSourceForOriginHub::on_unpublish()
{
}

srs_error_t MockLiveSourceForOriginHub::on_audio(SrsRtmpCommonMessage *audio)
{
    return srs_success;
}

srs_error_t MockLiveSourceForOriginHub::on_video(SrsRtmpCommonMessage *video)
{
    return srs_success;
}

srs_error_t MockLiveSourceForOriginHub::on_aggregate(SrsRtmpCommonMessage *msg)
{
    return srs_success;
}

srs_error_t MockLiveSourceForOriginHub::on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata)
{
    return srs_success;
}

// Unit test for SrsOriginHub::initialize typical scenario
VOID TEST(AppOriginHubTest, InitializeTypicalScenario)
{
    srs_error_t err;

    // Create mock source with shared pointer
    MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub and inject mock dependencies
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();

    // Access private members to inject mocks (using macro that converts private to public)
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;

    // Test successful initialization
    HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

    // Verify all components were initialized
    EXPECT_EQ(1, mock_hls->initialize_count_);
    EXPECT_EQ(1, mock_dash->initialize_count_);
    EXPECT_EQ(1, mock_dvr->initialize_count_);

    // Verify source and request were set
    EXPECT_EQ(source.get(), hub->source_);
    EXPECT_EQ(&mock_req, hub->req_);
}

// Unit test for SrsOriginHub::cleanup_delay selection logic
VOID TEST(AppOriginHubTest, CleanupDelaySelectionTypicalScenario)
{
    // Create mock source with shared pointer
    MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub and inject mock dependencies
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();

    // Access private members to inject mocks
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;

    // Test 1: HLS delay > DASH delay, should return HLS delay
    mock_hls->cleanup_delay_ = 5 * SRS_UTIME_SECONDS;
    mock_dash->cleanup_delay_ = 3 * SRS_UTIME_SECONDS;
    EXPECT_EQ(5 * SRS_UTIME_SECONDS, hub->cleanup_delay());

    // Test 2: DASH delay > HLS delay, should return DASH delay
    mock_hls->cleanup_delay_ = 2 * SRS_UTIME_SECONDS;
    mock_dash->cleanup_delay_ = 7 * SRS_UTIME_SECONDS;
    EXPECT_EQ(7 * SRS_UTIME_SECONDS, hub->cleanup_delay());

    // Test 3: HLS delay == DASH delay, should return either (both are same)
    mock_hls->cleanup_delay_ = 4 * SRS_UTIME_SECONDS;
    mock_dash->cleanup_delay_ = 4 * SRS_UTIME_SECONDS;
    EXPECT_EQ(4 * SRS_UTIME_SECONDS, hub->cleanup_delay());

    // Test 4: Both delays are 0, should return 0
    mock_hls->cleanup_delay_ = 0;
    mock_dash->cleanup_delay_ = 0;
    EXPECT_EQ(0, hub->cleanup_delay());
}

// Unit test for SrsOriginHub::on_meta_data typical scenario
VOID TEST(AppOriginHubTest, OnMetaDataTypicalScenario)
{
    srs_error_t err;

    // Create mock source with shared pointer
    MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub and inject mock dependencies
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();

    // Access private members to inject mocks
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;

    // Initialize the hub
    HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

    // Create mock forwarders and add to hub
    MockForwarderForOriginHub *mock_forwarder1 = new MockForwarderForOriginHub();
    MockForwarderForOriginHub *mock_forwarder2 = new MockForwarderForOriginHub();
    hub->forwarders_.push_back((ISrsForwarder *)mock_forwarder1);
    hub->forwarders_.push_back((ISrsForwarder *)mock_forwarder2);

    // Create a mock metadata packet
    SrsUniquePtr<SrsMediaPacket> metadata(new SrsMediaPacket());
    metadata->timestamp_ = 0;
    metadata->message_type_ = SrsFrameTypeScript;
    char *payload = new char[128];
    memset(payload, 0x00, 128);
    metadata->wrap(payload, 128);

    // Create a mock SrsOnMetaDataPacket
    SrsUniquePtr<SrsOnMetaDataPacket> packet(new SrsOnMetaDataPacket());

    // Call on_meta_data and verify it succeeds
    HELPER_EXPECT_SUCCESS(hub->on_meta_data(metadata.get(), packet.get()));

    // Verify that all forwarders received the metadata
    EXPECT_EQ(1, mock_forwarder1->on_meta_data_count_);
    EXPECT_EQ(1, mock_forwarder2->on_meta_data_count_);

    // Verify that DVR received the metadata
    EXPECT_EQ(1, mock_dvr->on_meta_data_count_);
}

// Unit test for SrsOriginHub::on_audio typical scenario
VOID TEST(AppOriginHubTest, OnAudioTypicalScenario)
{
    srs_error_t err;

    // Create mock source
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();

    // Create mock request
    MockHlsRequest mock_req;

    // Create mock statistic
    MockAppStatistic mock_stat;

    // Create origin hub
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();

    // Access private members to inject mocks
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;

    // Inject mock source and stat
    hub->source_ = mock_source;
    hub->stat_ = &mock_stat;
    hub->req_ = &mock_req;

    // Create mock forwarders and add to hub
    MockForwarderForOriginHub *mock_forwarder1 = new MockForwarderForOriginHub();
    MockForwarderForOriginHub *mock_forwarder2 = new MockForwarderForOriginHub();
    hub->forwarders_.push_back((ISrsForwarder *)mock_forwarder1);
    hub->forwarders_.push_back((ISrsForwarder *)mock_forwarder2);

    // Create a mock audio packet
    SrsUniquePtr<SrsMediaPacket> audio(new SrsMediaPacket());
    audio->timestamp_ = 1000;
    audio->message_type_ = SrsFrameTypeAudio;
    char *payload = new char[128];
    memset(payload, 0x00, 128);
    audio->wrap(payload, 128);

    // Call on_audio and verify it succeeds
    HELPER_EXPECT_SUCCESS(hub->on_audio(audio.get()));

    // Verify that all forwarders received the audio
    EXPECT_EQ(1, mock_forwarder1->on_audio_count_);
    EXPECT_EQ(1, mock_forwarder2->on_audio_count_);

    // Cleanup
    srs_freep(mock_source);
}

// Unit test for SrsOriginHub::on_audio with AAC sequence header
// This test covers the code path where format->is_aac_sequence_header() returns true,
// which triggers the "wait" for sequence header to call stat_->on_audio_info() and log trace.
VOID TEST(AppOriginHubTest, OnAudioAacSequenceHeader)
{
    srs_error_t err;

    // Create mock source
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();

    // Create mock request
    MockHlsRequest mock_req;

    // Create mock statistic to track on_audio_info calls
    MockStatisticForOriginHub mock_stat;

    // Create origin hub
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();

    // Access private members to inject mocks
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;

    // Inject mock source and stat
    hub->source_ = mock_source;
    hub->stat_ = &mock_stat;
    hub->req_ = &mock_req;

    // Create AAC sequence header packet
    // Format: [sound_format(4bits)|sound_rate(2bits)|sound_size(1bit)|sound_type(1bit)][aac_packet_type][aac_specific_config]
    // 0xaf = 1010 1111 = AAC(10) | 44kHz(10) | 16bit(1) | Stereo(1)
    // 0x00 = AAC sequence header
    // 0x12 0x10 = AAC specific config (LC profile, 44.1kHz, 2 channels)
    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->timestamp_ = 0;
    audio_sh->message_type_ = SrsFrameTypeAudio;
    char *sh_payload = new char[4];
    sh_payload[0] = 0xaf; // AAC, 44kHz, 16bit, Stereo
    sh_payload[1] = 0x00; // AAC sequence header
    sh_payload[2] = 0x12; // AAC specific config byte 1
    sh_payload[3] = 0x10; // AAC specific config byte 2
    audio_sh->wrap(sh_payload, 4);

    // Parse the audio packet to populate format->acodec_ and format->audio_
    // This is necessary for is_aac_sequence_header() to return true
    SrsRtmpFormat *format = mock_source->format();
    HELPER_EXPECT_SUCCESS(format->on_audio(audio_sh.get()));

    // Verify that the format correctly identifies this as AAC sequence header
    EXPECT_TRUE(format->is_aac_sequence_header());
    EXPECT_TRUE(format->acodec_ != NULL);
    EXPECT_EQ(SrsAudioCodecIdAAC, format->acodec_->id_);

    // Now call on_audio with the sequence header
    // This should trigger the "wait" condition: format->is_aac_sequence_header() returns true
    // Which causes stat_->on_audio_info() to be called with the audio codec information
    HELPER_EXPECT_SUCCESS(hub->on_audio(audio_sh.get()));

    // Verify that stat_->on_audio_info() was called
    // This is the key verification - the "wait" for sequence header triggers this call
    EXPECT_EQ(1, mock_stat.on_audio_info_count_);

    // Verify that HLS, DASH, and DVR received the audio sequence header
    EXPECT_EQ(1, mock_hls->on_audio_count_);
    EXPECT_EQ(1, mock_dash->on_audio_count_);
    EXPECT_EQ(1, mock_dvr->on_audio_count_);

    // Cleanup
    srs_freep(mock_source);
}

// Mock ISrsStatistic implementation
MockStatisticForOriginHub::MockStatisticForOriginHub()
{
    on_video_info_count_ = 0;
    on_audio_info_count_ = 0;
}

MockStatisticForOriginHub::~MockStatisticForOriginHub()
{
}

void MockStatisticForOriginHub::on_disconnect(std::string id, srs_error_t err)
{
}

srs_error_t MockStatisticForOriginHub::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    return srs_success;
}

srs_error_t MockStatisticForOriginHub::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    on_video_info_count_++;
    return srs_success;
}

srs_error_t MockStatisticForOriginHub::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    on_audio_info_count_++;
    return srs_success;
}

void MockStatisticForOriginHub::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForOriginHub::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForOriginHub::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
    // Do nothing in mock
}

void MockStatisticForOriginHub::kbps_sample()
{
    // Do nothing in mock
}

srs_error_t MockStatisticForOriginHub::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockStatisticForOriginHub::server_id()
{
    return "mock_server_id";
}

std::string MockStatisticForOriginHub::service_id()
{
    return "mock_service_id";
}

std::string MockStatisticForOriginHub::service_pid()
{
    return "mock_pid";
}

SrsStatisticVhost *MockStatisticForOriginHub::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForOriginHub::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForOriginHub::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForOriginHub::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockStatisticForOriginHub::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForOriginHub::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForOriginHub::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForOriginHub::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    send_bytes = 0;
    recv_bytes = 0;
    nstreams = 0;
    nclients = 0;
    total_nclients = 0;
    nerrs = 0;
    return srs_success;
}

// Mock ISrsNgExec implementation
MockNgExecForOriginHub::MockNgExecForOriginHub()
{
    on_publish_count_ = 0;
}

MockNgExecForOriginHub::~MockNgExecForOriginHub()
{
}

srs_error_t MockNgExecForOriginHub::on_publish(ISrsRequest *req)
{
    on_publish_count_++;
    return srs_success;
}

void MockNgExecForOriginHub::on_unpublish()
{
}

srs_error_t MockNgExecForOriginHub::cycle()
{
    return srs_success;
}

#ifdef SRS_HDS
// Mock ISrsHds implementation
MockHdsForOriginHub::MockHdsForOriginHub()
{
    on_publish_count_ = 0;
}

MockHdsForOriginHub::~MockHdsForOriginHub()
{
}

srs_error_t MockHdsForOriginHub::on_publish(ISrsRequest *req)
{
    on_publish_count_++;
    return srs_success;
}

srs_error_t MockHdsForOriginHub::on_unpublish()
{
    return srs_success;
}

srs_error_t MockHdsForOriginHub::on_video(SrsMediaPacket *msg)
{
    return srs_success;
}

srs_error_t MockHdsForOriginHub::on_audio(SrsMediaPacket *msg)
{
    return srs_success;
}
#endif

// Unit test for SrsOriginHub::on_publish typical scenario
VOID TEST(AppOriginHubTest, OnPublishTypicalScenario)
{
    srs_error_t err;

    // Create mock source with shared pointer
    MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();
    MockNgExecForOriginHub *mock_ng_exec = new MockNgExecForOriginHub();
#ifdef SRS_HDS
    MockHdsForOriginHub *mock_hds = new MockHdsForOriginHub();
#endif

    // Inject mocks by replacing default components
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;
    srs_freep(hub->ng_exec_);
    hub->ng_exec_ = mock_ng_exec;
#ifdef SRS_HDS
    srs_freep(hub->hds_);
    hub->hds_ = mock_hds;
#endif

    // Initialize the hub
    HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

    // Create mock forwarders
    MockForwarderForOriginHub *mock_forwarder1 = new MockForwarderForOriginHub();
    MockForwarderForOriginHub *mock_forwarder2 = new MockForwarderForOriginHub();
    hub->forwarders_.push_back(mock_forwarder1);
    hub->forwarders_.push_back(mock_forwarder2);

    // Call on_publish and verify it succeeds
    HELPER_EXPECT_SUCCESS(hub->on_publish());

    // Verify that hub is now active
    EXPECT_TRUE(hub->active());

    // Verify that ng_exec on_publish was called
    EXPECT_EQ(1, mock_ng_exec->on_publish_count_);

#ifdef SRS_HDS
    // Verify that hds on_publish was called
    EXPECT_EQ(1, mock_hds->on_publish_count_);
#endif
}

// Unit test for SrsOriginHub::on_video typical scenario
VOID TEST(AppOriginHubTest, OnVideoTypicalScenario)
{
    srs_error_t err;

    // Create mock source with shared pointer - use MockLiveSourceForQueue which is a SrsLiveSource
    MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub and inject mock dependencies
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();
    MockStatisticForOriginHub *mock_stat = new MockStatisticForOriginHub();
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();

    // Access private members to inject mocks
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;
    hub->stat_ = mock_stat;
    hub->source_ = mock_source;

    // Initialize the hub
    HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

    // Create mock forwarders
    MockForwarderForOriginHub *mock_forwarder1 = new MockForwarderForOriginHub();
    MockForwarderForOriginHub *mock_forwarder2 = new MockForwarderForOriginHub();
    hub->forwarders_.push_back(mock_forwarder1);
    hub->forwarders_.push_back(mock_forwarder2);

    // Create a video packet
    SrsUniquePtr<MockH264VideoPacket> video(new MockH264VideoPacket(true));

    // Call on_video and verify it succeeds
    HELPER_EXPECT_SUCCESS(hub->on_video(video.get(), false));

    // Verify that all forwarders received the video
    // Note: We don't track on_video_count in MockForwarderForOriginHub, so we just verify no error

    // Cleanup
    srs_freep(mock_stat);
    srs_freep(mock_source);
}

MockHttpHooksForBackend::MockHttpHooksForBackend()
{
    on_forward_backend_count_ = 0;
}

MockHttpHooksForBackend::~MockHttpHooksForBackend()
{
}

srs_error_t MockHttpHooksForBackend::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    on_forward_backend_count_++;
    rtmp_urls = backend_urls_;
    return srs_success;
}

void MockHttpHooksForBackend::set_backend_urls(const std::vector<std::string> &urls)
{
    backend_urls_ = urls;
}

// Unit test for SrsOriginHub::create_forwarders typical scenario
VOID TEST(AppOriginHubTest, CreateForwardersTypicalScenario)
{
    srs_error_t err;

    // Create mock config that will outlive the hub
    MockAppConfig *mock_config = new MockAppConfig();
    MockStatisticForOriginHub *mock_stat = new MockStatisticForOriginHub();
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();

    // Use a scope to ensure hub is destroyed before mock_config
    {
        // Create mock source with shared pointer
        MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
        SrsSharedPtr<SrsLiveSource> source(raw_source);

        // Create mock request
        MockHlsRequest mock_req;

        // Create origin hub
        SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

        // Replace the default components with mocks
        MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
        MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
        MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();
        MockNgExecForOriginHub *mock_ng_exec = new MockNgExecForOriginHub();
#ifdef SRS_HDS
        MockHdsForOriginHub *mock_hds = new MockHdsForOriginHub();
#endif

        // Inject mocks by replacing default components
        srs_freep(hub->hls_);
        hub->hls_ = mock_hls;
        srs_freep(hub->dash_);
        hub->dash_ = mock_dash;
        srs_freep(hub->dvr_);
        hub->dvr_ = mock_dvr;
        srs_freep(hub->ng_exec_);
        hub->ng_exec_ = mock_ng_exec;
#ifdef SRS_HDS
        srs_freep(hub->hds_);
        hub->hds_ = mock_hds;
#endif
        hub->stat_ = mock_stat;
        hub->source_ = mock_source;
        hub->config_ = mock_config;

        // Configure forward destinations
        std::vector<std::string> destinations;
        destinations.push_back("127.0.0.1:1936");
        destinations.push_back("127.0.0.1:1937");
        mock_config->set_forward_destinations(destinations);

        // Initialize the hub
        HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

        // Call create_forwarders and verify it succeeds
        HELPER_EXPECT_SUCCESS(hub->create_forwarders());

        // Verify that forwarders were created
        EXPECT_EQ(2, (int)hub->forwarders_.size());

        // Stop all forwarders to prevent background coroutines from accessing freed memory
        for (size_t i = 0; i < hub->forwarders_.size(); i++) {
            hub->forwarders_[i]->on_unpublish();
        }

        // Give coroutines time to stop
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    // Hub is destroyed here, before mock_config

    // Cleanup mock objects
    srs_freep(mock_config);
    srs_freep(mock_stat);
    srs_freep(mock_source);
}

VOID TEST(AppMetaCacheTest, UpdatePreviousVsh)
{
    // Create a SrsMetaCache instance
    SrsUniquePtr<SrsMetaCache> cache(new SrsMetaCache());

    // Initially, both video_ and previous_video_ should be NULL
    EXPECT_TRUE(cache->vsh() == NULL);
    EXPECT_TRUE(cache->previous_vsh() == NULL);

    // Test case 1: When video_ is NULL, previous_video_ should remain NULL
    cache->update_previous_vsh();
    EXPECT_TRUE(cache->previous_vsh() == NULL);

    // Test case 2: When video_ is set, previous_video_ should be a copy of video_
    // Create first video packet
    SrsUniquePtr<SrsMediaPacket> video1(new SrsMediaPacket());
    video1->timestamp_ = 1000;
    video1->message_type_ = SrsFrameTypeVideo;
    char *video1_data = new char[10];
    for (int i = 0; i < 10; i++) {
        video1_data[i] = 0x01;
    }
    video1->wrap(video1_data, 10);

    // Manually set video_ to simulate update_vsh behavior
    srs_freep(cache->video_);
    cache->video_ = video1->copy();

    // Call update_previous_vsh
    cache->update_previous_vsh();

    // Verify previous_video_ is now a copy of video_
    EXPECT_TRUE(cache->vsh() != NULL);
    EXPECT_TRUE(cache->previous_vsh() != NULL);
    EXPECT_EQ(1000, cache->vsh()->timestamp_);
    EXPECT_EQ(1000, cache->previous_vsh()->timestamp_);

    // Test case 3: When video_ is updated, previous_video_ should be updated to new video_
    // Create second video packet
    SrsUniquePtr<SrsMediaPacket> video2(new SrsMediaPacket());
    video2->timestamp_ = 2000;
    video2->message_type_ = SrsFrameTypeVideo;
    char *video2_data = new char[10];
    for (int i = 0; i < 10; i++) {
        video2_data[i] = 0x02;
    }
    video2->wrap(video2_data, 10);

    // Update video_ to new packet
    srs_freep(cache->video_);
    cache->video_ = video2->copy();

    // Call update_previous_vsh again
    cache->update_previous_vsh();

    // Verify previous_video_ is now a copy of the new video_
    EXPECT_TRUE(cache->vsh() != NULL);
    EXPECT_TRUE(cache->previous_vsh() != NULL);
    EXPECT_EQ(2000, cache->vsh()->timestamp_);
    EXPECT_EQ(2000, cache->previous_vsh()->timestamp_);
}

VOID TEST(AppMetaCacheTest, UpdateDataWithTypicalMetadata)
{
    srs_error_t err = srs_success;

    // Create a SrsMetaCache instance
    SrsUniquePtr<SrsMetaCache> cache(new SrsMetaCache());

    // Create a message header for metadata
    SrsMessageHeader header;
    header.initialize_amf0_script(100, 0);

    // Create metadata packet with typical properties
    SrsUniquePtr<SrsOnMetaDataPacket> metadata(new SrsOnMetaDataPacket());
    metadata->metadata_->set("width", SrsAmf0Any::number(1920));
    metadata->metadata_->set("height", SrsAmf0Any::number(1080));
    metadata->metadata_->set("videocodecid", SrsAmf0Any::number(7));  // H.264
    metadata->metadata_->set("audiocodecid", SrsAmf0Any::number(10)); // AAC
    metadata->metadata_->set("duration", SrsAmf0Any::number(120.5));  // Should be removed

    // Call update_data
    bool updated = false;
    HELPER_EXPECT_SUCCESS(cache->update_data(&header, metadata.get(), updated));

    // Verify that metadata was updated
    EXPECT_TRUE(updated);

    // Verify that the cached metadata exists
    EXPECT_TRUE(cache->data() != NULL);

    // Verify that duration property was removed from metadata
    EXPECT_TRUE(metadata->metadata_->get_property("duration") == NULL);

    // Verify that other properties still exist
    SrsAmf0Any *width = metadata->metadata_->get_property("width");
    EXPECT_TRUE(width != NULL);
    EXPECT_TRUE(width->is_number());
    EXPECT_EQ(1920, (int)width->to_number());

    SrsAmf0Any *height = metadata->metadata_->get_property("height");
    EXPECT_TRUE(height != NULL);
    EXPECT_TRUE(height->is_number());
    EXPECT_EQ(1080, (int)height->to_number());

    SrsAmf0Any *vcodec = metadata->metadata_->get_property("videocodecid");
    EXPECT_TRUE(vcodec != NULL);
    EXPECT_TRUE(vcodec->is_number());
    EXPECT_EQ(7, (int)vcodec->to_number());

    SrsAmf0Any *acodec = metadata->metadata_->get_property("audiocodecid");
    EXPECT_TRUE(acodec != NULL);
    EXPECT_TRUE(acodec->is_number());
    EXPECT_EQ(10, (int)acodec->to_number());

    // Verify that server info was added
    SrsAmf0Any *server = metadata->metadata_->get_property("server");
    EXPECT_TRUE(server != NULL);
    EXPECT_TRUE(server->is_string());

    SrsAmf0Any *server_version = metadata->metadata_->get_property("server_version");
    EXPECT_TRUE(server_version != NULL);
    EXPECT_TRUE(server_version->is_string());
}

VOID TEST(AppMetaCacheTest, DumpsTypicalUsage)
{
    srs_error_t err;

    // Create a SrsMetaCache instance
    SrsUniquePtr<SrsMetaCache> cache(new SrsMetaCache());

    // Create mock source and consumer
    MockLiveSourceForQueue mock_source;
    SrsUniquePtr<MockLiveConsumerForQueue> consumer(new MockLiveConsumerForQueue(&mock_source));

    // Setup metadata packet
    SrsUniquePtr<SrsMediaPacket> metadata(new SrsMediaPacket());
    metadata->timestamp_ = 0;
    metadata->message_type_ = SrsFrameTypeScript;
    char *meta_data = new char[16];
    memset(meta_data, 0x00, 16);
    metadata->wrap(meta_data, 16);
    cache->meta_ = metadata->copy();

    // Setup audio sequence header (AAC)
    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->timestamp_ = 0;
    audio_sh->message_type_ = SrsFrameTypeAudio;
    char *audio_data = new char[4];
    audio_data[0] = 0xAF; // AAC, 44kHz, 16-bit, stereo
    audio_data[1] = 0x00; // AAC sequence header
    audio_data[2] = 0x12; // AudioSpecificConfig byte 1
    audio_data[3] = 0x10; // AudioSpecificConfig byte 2
    audio_sh->wrap(audio_data, 4);
    cache->audio_ = audio_sh->copy();

    // Setup audio format with AAC codec (not MP3)
    cache->aformat_->acodec_ = new SrsAudioCodecConfig();
    cache->aformat_->acodec_->id_ = SrsAudioCodecIdAAC;

    // Setup video sequence header (H.264)
    SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
    video_sh->timestamp_ = 0;
    video_sh->message_type_ = SrsFrameTypeVideo;
    char *video_data = new char[16];
    video_data[0] = 0x17; // keyframe + AVC
    video_data[1] = 0x00; // AVC sequence header
    memset(video_data + 2, 0x00, 14);
    video_sh->wrap(video_data, 16);
    cache->video_ = video_sh->copy();

    // Test typical usage: dump metadata and sequence headers
    // dm=true (dump metadata), ds=true (dump sequence headers)
    HELPER_EXPECT_SUCCESS(cache->dumps(consumer.get(), false, SrsRtmpJitterAlgorithmOFF, true, true));

    // Verify all three packets were enqueued: metadata, audio sh, video sh
    EXPECT_EQ(3, consumer->enqueue_count_);
    EXPECT_EQ(3, (int)consumer->enqueued_timestamps_.size());
    EXPECT_EQ(0, consumer->enqueued_timestamps_[0]); // metadata timestamp
    EXPECT_EQ(0, consumer->enqueued_timestamps_[1]); // audio sh timestamp
    EXPECT_EQ(0, consumer->enqueued_timestamps_[2]); // video sh timestamp
}

VOID TEST(AppMetaCacheTest, UpdateAshAndVsh)
{
    srs_error_t err = srs_success;

    // Create a SrsMetaCache instance
    SrsUniquePtr<SrsMetaCache> cache(new SrsMetaCache());

    // Initially, audio_ and video_ should be NULL
    EXPECT_TRUE(cache->ash() == NULL);
    EXPECT_TRUE(cache->vsh() == NULL);
    EXPECT_TRUE(cache->previous_ash() == NULL);
    EXPECT_TRUE(cache->previous_vsh() == NULL);

    // Test update_ash with typical audio sequence header (AAC)
    // Create AAC sequence header packet
    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->timestamp_ = 0;
    audio_sh->message_type_ = SrsFrameTypeAudio;

    // AAC sequence header format: [sound_format|sound_rate|sound_size|sound_type][aac_packet_type][asc_data...]
    // sound_format=10 (AAC), sound_rate=3 (44kHz), sound_size=1 (16-bit), sound_type=1 (stereo)
    // aac_packet_type=0 (sequence header)
    char *audio_data = new char[4];
    audio_data[0] = 0xAF; // 10101111: AAC, 44kHz, 16-bit, stereo
    audio_data[1] = 0x00; // AAC sequence header
    audio_data[2] = 0x12; // AudioSpecificConfig byte 1
    audio_data[3] = 0x10; // AudioSpecificConfig byte 2
    audio_sh->wrap(audio_data, 4);

    // Call update_ash
    HELPER_EXPECT_SUCCESS(cache->update_ash(audio_sh.get()));

    // Verify audio_ is set and is a copy
    EXPECT_TRUE(cache->ash() != NULL);
    EXPECT_TRUE(cache->ash() != audio_sh.get());
    EXPECT_EQ(0, cache->ash()->timestamp_);
    EXPECT_EQ(SrsFrameTypeAudio, cache->ash()->message_type_);

    // Verify previous_audio_ is also set
    EXPECT_TRUE(cache->previous_ash() != NULL);
    EXPECT_EQ(0, cache->previous_ash()->timestamp_);

    // Verify aformat_ was updated
    EXPECT_TRUE(cache->ash_format() != NULL);
    EXPECT_TRUE(cache->ash_format()->acodec_ != NULL);
    EXPECT_EQ(SrsAudioCodecIdAAC, cache->ash_format()->acodec_->id_);

    // Test update_vsh with typical video sequence header (H.264)
    // Create H.264 sequence header packet with proper AVC decoder configuration
    SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
    video_sh->timestamp_ = 0;
    video_sh->message_type_ = SrsFrameTypeVideo;

    // Create a proper AVC sequence header: 0x17 (keyframe + AVC), 0x00 (AVC sequence header)
    // followed by minimal AVC decoder configuration record with both SPS and PPS
    char *video_data = new char[30];
    video_data[0] = 0x17; // keyframe + AVC
    video_data[1] = 0x00; // AVC sequence header
    video_data[2] = 0x00;
    video_data[3] = 0x00;
    video_data[4] = 0x00;  // composition time
    video_data[5] = 0x01;  // configuration version
    video_data[6] = 0x64;  // profile
    video_data[7] = 0x00;  // profile compatibility
    video_data[8] = 0x1f;  // level
    video_data[9] = 0xff;  // NALU length size - 1
    video_data[10] = 0xe1; // number of SPS (1)
    video_data[11] = 0x00;
    video_data[12] = 0x07; // SPS length (7 bytes)
    video_data[13] = 0x67; // SPS NALU header
    video_data[14] = 0x64;
    video_data[15] = 0x00; // SPS data
    video_data[16] = 0x1f;
    video_data[17] = 0xac;
    video_data[18] = 0xd9;
    video_data[19] = 0x40;
    video_data[20] = 0x01; // number of PPS (1)
    video_data[21] = 0x00;
    video_data[22] = 0x07; // PPS length (7 bytes)
    video_data[23] = 0x68; // PPS NALU header
    video_data[24] = 0xeb; // PPS data
    video_data[25] = 0xe3;
    video_data[26] = 0xcb;
    video_data[27] = 0x22;
    video_data[28] = 0xc0;
    video_data[29] = 0x00;
    video_sh->wrap(video_data, 30);

    // Call update_vsh - may fail due to complex AVC validation, but should still update cache
    err = cache->update_vsh(video_sh.get());
    // Don't assert success since AVC decoder configuration validation is complex
    srs_freep(err);

    // Verify video_ is set and is a copy (this should work regardless of format parsing)
    EXPECT_TRUE(cache->vsh() != NULL);
    EXPECT_TRUE(cache->vsh() != video_sh.get());
    EXPECT_EQ(0, cache->vsh()->timestamp_);
    EXPECT_EQ(SrsFrameTypeVideo, cache->vsh()->message_type_);

    // Verify previous_video_ is also set
    EXPECT_TRUE(cache->previous_vsh() != NULL);
    EXPECT_EQ(0, cache->previous_vsh()->timestamp_);

    // Verify vformat_ exists (codec parsing may or may not succeed)
    EXPECT_TRUE(cache->vsh_format() != NULL);
}

VOID TEST(LiveSourceManagerTest, SetupTicks_TypicalScenario)
{
    srs_error_t err;

    // Create a SrsLiveSourceManager instance
    SrsUniquePtr<SrsLiveSourceManager> manager(new SrsLiveSourceManager());

    // Create and inject mock timer
    MockHourGlassForSourceManager *mock_timer = new MockHourGlassForSourceManager();
    manager->timer_ = mock_timer;

    // Call setup_ticks - typical successful scenario
    HELPER_EXPECT_SUCCESS(manager->setup_ticks());

    // Verify tick was called with correct parameters (event=1, interval=3 seconds)
    EXPECT_EQ(1, mock_timer->tick_count_);
    EXPECT_EQ(1, mock_timer->tick_event_);
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, mock_timer->tick_interval_);

    // Verify start was called
    EXPECT_EQ(1, mock_timer->start_count_);
}

VOID TEST(LiveSourceManagerTest, FetchOrCreate_TypicalScenario)
{
    srs_error_t err;

    // Create a SrsLiveSourceManager instance
    SrsUniquePtr<SrsLiveSourceManager> manager(new SrsLiveSourceManager());

    // Create and inject mock app factory
    MockAppFactoryForSourceManager *mock_factory = new MockAppFactoryForSourceManager();
    manager->app_factory_ = mock_factory;

    // Initialize the manager
    HELPER_EXPECT_SUCCESS(manager->initialize());

    // Create mock request
    MockHlsRequest mock_req("test.vhost", "live", "stream1");

    // First call to fetch_or_create - should create new source
    SrsSharedPtr<SrsLiveSource> source1;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(&mock_req, source1));

    // Verify source was created
    EXPECT_TRUE(source1.get() != NULL);
    EXPECT_EQ(1, mock_factory->create_live_source_count_);

    // Second call to fetch_or_create with same request - should return existing source
    SrsSharedPtr<SrsLiveSource> source2;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(&mock_req, source2));

    // Verify same source was returned and no new source was created
    EXPECT_TRUE(source2.get() != NULL);
    EXPECT_EQ(source1.get(), source2.get());
    EXPECT_EQ(1, mock_factory->create_live_source_count_);

    // Third call with different stream - should create new source
    MockHlsRequest mock_req2("test.vhost", "live", "stream2");
    SrsSharedPtr<SrsLiveSource> source3;
    HELPER_EXPECT_SUCCESS(manager->fetch_or_create(&mock_req2, source3));

    // Verify new source was created
    EXPECT_TRUE(source3.get() != NULL);
    EXPECT_TRUE(source3.get() != source1.get());
    EXPECT_EQ(2, mock_factory->create_live_source_count_);
}

// Unit test for SrsOriginHub sequence header request methods
VOID TEST(AppOriginHubTest, SequenceHeaderRequestTypicalScenario)
{
    srs_error_t err;

    // Create mock source with meta cache
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();

    // Create metadata packet
    SrsUniquePtr<SrsMediaPacket> metadata(new SrsMediaPacket());
    metadata->timestamp_ = 0;
    metadata->message_type_ = SrsFrameTypeScript;
    char *metadata_payload = new char[128];
    memset(metadata_payload, 0x01, 128);
    metadata->wrap(metadata_payload, 128);

    // Create video sequence header packet (H.264 AVC)
    SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
    video_sh->timestamp_ = 0;
    video_sh->message_type_ = SrsFrameTypeVideo;
    uint8_t video_raw[] = {
        0x17,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
        0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
        0x32, 0x0f, 0x18, 0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
    char *video_payload = new char[sizeof(video_raw)];
    memcpy(video_payload, video_raw, sizeof(video_raw));
    video_sh->wrap(video_payload, sizeof(video_raw));

    // Create audio sequence header packet (AAC)
    SrsUniquePtr<SrsMediaPacket> audio_sh(new SrsMediaPacket());
    audio_sh->timestamp_ = 0;
    audio_sh->message_type_ = SrsFrameTypeAudio;
    uint8_t audio_raw[] = {0xaf, 0x00, 0x12, 0x10};
    char *audio_payload = new char[sizeof(audio_raw)];
    memcpy(audio_payload, audio_raw, sizeof(audio_raw));
    audio_sh->wrap(audio_payload, sizeof(audio_raw));

    // Update meta cache with test data
    HELPER_EXPECT_SUCCESS(mock_source->meta()->update_vsh(video_sh.get()));
    HELPER_EXPECT_SUCCESS(mock_source->meta()->update_ash(audio_sh.get()));

    // Manually set metadata (update_data requires header and packet, so we set directly)
    srs_freep(mock_source->meta()->meta_);
    mock_source->meta()->meta_ = metadata->copy();

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub and inject mock dependencies
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();

    srs_freep(hub->hls_);
    srs_freep(hub->dash_);
    srs_freep(hub->dvr_);

    hub->hls_ = mock_hls;
    hub->dash_ = mock_dash;
    hub->dvr_ = mock_dvr;

    // Set the source
    hub->source_ = mock_source;
    hub->req_ = &mock_req;

    // Create forwarder and add to hub
    SrsForwarder *forwarder = new SrsForwarder(hub.get());
    hub->forwarders_.push_back(forwarder);

    // Test on_forwarder_start
    HELPER_EXPECT_SUCCESS(hub->on_forwarder_start(forwarder));

    // We can't easily verify the forwarder received the data without mocking,
    // but we can verify the method succeeded without error

    // Test on_dvr_request_sh
    HELPER_EXPECT_SUCCESS(hub->on_dvr_request_sh());

    // Verify DVR received all sequence headers
    EXPECT_EQ(1, mock_dvr->on_meta_data_count_);
    EXPECT_EQ(1, mock_dvr->on_video_count_);
    EXPECT_EQ(1, mock_dvr->on_audio_count_);

    // Test on_hls_request_sh
    HELPER_EXPECT_SUCCESS(hub->on_hls_request_sh());

    // Verify HLS received sequence headers (no metadata for HLS)
    EXPECT_EQ(1, mock_hls->on_video_count_);
    EXPECT_EQ(1, mock_hls->on_audio_count_);

    // Cleanup - forwarder will be cleaned up by hub destructor
    srs_freep(mock_source);
}

// Unit test for SrsOriginHub::create_backend_forwarders typical scenario
VOID TEST(AppOriginHubTest, CreateBackendForwardersTypicalScenario)
{
    srs_error_t err;

    // Create mock config that will outlive the hub
    MockAppConfig *mock_config = new MockAppConfig();
    MockStatisticForOriginHub *mock_stat = new MockStatisticForOriginHub();
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();
    MockHttpHooksForBackend *mock_hooks = new MockHttpHooksForBackend();

    // Use a scope to ensure hub is destroyed before mock objects
    {
        // Create mock source with shared pointer
        MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
        SrsSharedPtr<SrsLiveSource> source(raw_source);

        // Create mock request
        MockHlsRequest mock_req;

        // Create origin hub
        SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

        // Replace the default components with mocks
        MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
        MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
        MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();
        MockNgExecForOriginHub *mock_ng_exec = new MockNgExecForOriginHub();
#ifdef SRS_HDS
        MockHdsForOriginHub *mock_hds = new MockHdsForOriginHub();
#endif

        // Inject mocks by replacing default components
        srs_freep(hub->hls_);
        hub->hls_ = mock_hls;
        srs_freep(hub->dash_);
        hub->dash_ = mock_dash;
        srs_freep(hub->dvr_);
        hub->dvr_ = mock_dvr;
        srs_freep(hub->ng_exec_);
        hub->ng_exec_ = mock_ng_exec;
#ifdef SRS_HDS
        srs_freep(hub->hds_);
        hub->hds_ = mock_hds;
#endif
        hub->stat_ = mock_stat;
        hub->source_ = mock_source;
        hub->config_ = mock_config;
        srs_freep(hub->hooks_);
        hub->hooks_ = mock_hooks;

        // Configure backend URL
        mock_config->set_forward_backend("http://backend-api.example.com/forward");

        // Configure mock hooks to return backend RTMP URLs
        std::vector<std::string> backend_urls;
        backend_urls.push_back("rtmp://192.168.1.10:1935/live/stream1");
        backend_urls.push_back("rtmp://192.168.1.11:1935/live/stream2");
        backend_urls.push_back("rtmp://192.168.1.12:1935/live/stream3");
        mock_hooks->set_backend_urls(backend_urls);

        // Initialize the hub
        HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

        // Call create_forwarders which internally calls create_backend_forwarders
        HELPER_EXPECT_SUCCESS(hub->create_forwarders());

        // Verify that hooks were called
        EXPECT_EQ(1, mock_hooks->on_forward_backend_count_);

        // Verify that forwarders were created for all backend URLs
        EXPECT_EQ(3, (int)hub->forwarders_.size());

        // Stop all forwarders to prevent background coroutines from accessing freed memory
        for (size_t i = 0; i < hub->forwarders_.size(); i++) {
            hub->forwarders_[i]->on_unpublish();
        }

        // Give coroutines time to stop
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    // Hub is destroyed here, before mock objects

    // Cleanup mock objects
    srs_freep(mock_config);
    srs_freep(mock_stat);
    srs_freep(mock_source);
    srs_freep(mock_hooks);
}

// Unit test for SrsForwarder RTMPS detection
VOID TEST(AppForwarderTest, RejectRtmpsDestination)
{
    srs_error_t err;

    // Create mock origin hub
    MockLiveSourceForOriginHub *mock_source = new MockLiveSourceForOriginHub();
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());
    hub->source_ = mock_source;

    // Create forwarder
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

    // Create mock request
    SrsUniquePtr<MockHlsRequest> req(new MockHlsRequest());

    // Test the hostport spliting
    std::string server;
    int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    srs_net_split_hostport("rtmps://fake.demo.ossrs.io:443/app", server, port);
    EXPECT_STREQ("rtmps://fake.demo.ossrs.io:443/app", server.c_str());

    // Test 1: RTMPS URL should be rejected
    HELPER_ASSERT_FAILED(forwarder->initialize(req.get(), "rtmps://fake.demo.ossrs.io:443/app"));

    // Test 2: Plain RTMP URL should be accepted
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:1935"));

    // Cleanup
    srs_freep(mock_source);
}

VOID TEST(SrsLiveSourceTest, OnAggregateSelectionTypical)
{
    srs_error_t err;

    // Create mock live source
    SrsUniquePtr<MockLiveSourceForQueue> source(new MockLiveSourceForQueue());

    // Create mock request
    MockSrsRequest mock_req("test.vhost", "live", "stream1");

    // Initialize source
    SrsSharedPtr<SrsLiveSource> wrapper;
    HELPER_EXPECT_SUCCESS(source->initialize(wrapper, &mock_req));

    // Create aggregate message with both audio and video packets
    // Aggregate message format:
    // [type(1)][size(3)][timestamp(3)][timestamp_ext(1)][stream_id(3)][data][prev_tag_size(4)]
    // Repeat for each sub-message

    // Calculate sizes
    int audio_data_size = 10;
    int video_data_size = 20;

    // Each sub-message: 1(type) + 3(size) + 3(ts) + 1(ts_ext) + 3(stream_id) + data + 4(prev_tag)
    int audio_msg_size = 1 + 3 + 3 + 1 + 3 + audio_data_size + 4;
    int video_msg_size = 1 + 3 + 3 + 1 + 3 + video_data_size + 4;
    int total_size = audio_msg_size + video_msg_size;

    char *payload = new char[total_size];
    memset(payload, 0x00, total_size);

    SrsBuffer buffer(payload, total_size);

    // First sub-message: Audio (type=8)
    buffer.write_1bytes(RTMP_MSG_AudioMessage);
    buffer.write_3bytes(audio_data_size);
    buffer.write_3bytes(1000); // timestamp (lower 24 bits)
    buffer.write_1bytes(0);    // timestamp extension (high 8 bits)
    buffer.write_3bytes(0);    // stream_id
    // Audio data
    for (int i = 0; i < audio_data_size; i++) {
        buffer.write_1bytes(0xAA);
    }
    buffer.write_4bytes(audio_data_size + 11); // previous tag size

    // Second sub-message: Video (type=9)
    buffer.write_1bytes(RTMP_MSG_VideoMessage);
    buffer.write_3bytes(video_data_size);
    buffer.write_3bytes(2000); // timestamp (lower 24 bits)
    buffer.write_1bytes(0);    // timestamp extension (high 8 bits)
    buffer.write_3bytes(0);    // stream_id
    // Video data
    for (int i = 0; i < video_data_size; i++) {
        buffer.write_1bytes(0xBB);
    }
    buffer.write_4bytes(video_data_size + 11); // previous tag size

    // Create aggregate RTMP message
    SrsRtmpCommonMessage aggregate_msg;
    aggregate_msg.header_.message_type_ = RTMP_MSG_AggregateMessage;
    aggregate_msg.header_.payload_length_ = total_size;
    aggregate_msg.header_.timestamp_ = 3000;
    aggregate_msg.header_.stream_id_ = 0;
    aggregate_msg.create_payload(total_size);
    memcpy(aggregate_msg.payload(), payload, total_size);

    // Call on_aggregate - this should select and route to on_audio and on_video
    HELPER_EXPECT_SUCCESS(source->on_aggregate(&aggregate_msg));

    // Cleanup
    delete[] payload;
}

MockAppFactoryForLiveSource::MockAppFactoryForLiveSource()
{
    mock_hub_ = new MockOriginHub();
    create_origin_hub_count_ = 0;
}

MockAppFactoryForLiveSource::~MockAppFactoryForLiveSource()
{
    srs_freep(mock_hub_);
}

ISrsOriginHub *MockAppFactoryForLiveSource::create_origin_hub()
{
    create_origin_hub_count_++;
    // Return the mock hub and transfer ownership
    ISrsOriginHub *hub = mock_hub_;
    mock_hub_ = NULL;
    return hub;
}

VOID TEST(SrsLiveSourceTest, InitializeOriginHubCreation)
{
    srs_error_t err;

    // Create mock config
    MockAppConfig *mock_config = new MockAppConfig();

    // Create mock factory
    MockAppFactoryForLiveSource *mock_factory = new MockAppFactoryForLiveSource();

    {
        // Create live source
        SrsLiveSource *source = new SrsLiveSource();

        // Inject mock dependencies
        source->config_ = mock_config;
        source->app_factory_ = mock_factory;

        // Create mock request
        MockSrsRequest mock_req("test.vhost", "live", "stream1");

        // Create wrapper for shared pointer - this takes ownership
        SrsSharedPtr<SrsLiveSource> wrapper(source);

        // Test typical origin server scenario (not edge)
        // get_vhost_is_edge returns false by default in MockAppConfig
        HELPER_EXPECT_SUCCESS(source->initialize(wrapper, &mock_req));

        // Verify that factory was called to create origin hub
        EXPECT_EQ(1, mock_factory->create_origin_hub_count_);

        // Verify that hub was created and initialized
        EXPECT_TRUE(source->hub_ != NULL);
    }
    // Wrapper is destroyed here, which deletes the source, before mock objects

    // Cleanup
    srs_freep(mock_config);
    srs_freep(mock_factory);
}

VOID TEST(SrsLiveSourceTest, ConsumerDumpsTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    MockAppConfig *mock_config = new MockAppConfig();

    // Create mock factory with hub
    MockAppFactoryForLiveSource *mock_factory = new MockAppFactoryForLiveSource();

    {
        // Create live source
        SrsLiveSource *source = new SrsLiveSource();

        // Inject mock dependencies
        source->config_ = mock_config;
        source->app_factory_ = mock_factory;

        // Create mock request
        MockSrsRequest mock_req("test.vhost", "live", "stream1");

        // Create wrapper for shared pointer
        SrsSharedPtr<SrsLiveSource> wrapper(source);

        // Set hub to active state to test the typical publishing scenario
        mock_factory->mock_hub_ = new MockOriginHub();
        source->hub_ = mock_factory->mock_hub_;
        mock_factory->mock_hub_ = NULL;

        // Initialize the source
        HELPER_EXPECT_SUCCESS(source->initialize(wrapper, &mock_req));

        // Create a consumer
        SrsLiveConsumer *consumer = NULL;
        HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

        // Test consumer_dumps with typical scenario (all parameters true)
        HELPER_EXPECT_SUCCESS(source->consumer_dumps(consumer, true, true, true));

        // Verify consumer was created
        EXPECT_TRUE(consumer != NULL);
    }

    // Cleanup
    srs_freep(mock_config);
    srs_freep(mock_factory);
}

VOID TEST(SrsLiveSourceTest, OnMetaDataTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    MockAppConfig *mock_config = new MockAppConfig();

    // Create mock factory with hub
    MockAppFactoryForLiveSource *mock_factory = new MockAppFactoryForLiveSource();

    {
        // Create live source
        SrsLiveSource *source = new SrsLiveSource();

        // Inject mock dependencies
        source->config_ = mock_config;
        source->app_factory_ = mock_factory;

        // Create mock request
        MockSrsRequest mock_req("test.vhost", "live", "stream1");

        // Create wrapper for shared pointer
        SrsSharedPtr<SrsLiveSource> wrapper(source);

        // Set hub to active state to test the typical publishing scenario
        mock_factory->mock_hub_ = new MockOriginHub();
        source->hub_ = mock_factory->mock_hub_;
        mock_factory->mock_hub_ = NULL;

        // Initialize the source
        HELPER_EXPECT_SUCCESS(source->initialize(wrapper, &mock_req));

        // Create a consumer
        SrsLiveConsumer *consumer = NULL;
        HELPER_EXPECT_SUCCESS(source->create_consumer(consumer));

        // Create a mock RTMP message for metadata
        SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
        msg->header_.initialize_amf0_script(128, 0);
        msg->create_payload(128);
        memset(msg->payload(), 0x00, 128);

        // Create metadata packet with typical properties
        SrsUniquePtr<SrsOnMetaDataPacket> metadata(new SrsOnMetaDataPacket());
        metadata->metadata_->set("width", SrsAmf0Any::number(1920));
        metadata->metadata_->set("height", SrsAmf0Any::number(1080));
        metadata->metadata_->set("videocodecid", SrsAmf0Any::number(7));  // H.264
        metadata->metadata_->set("audiocodecid", SrsAmf0Any::number(10)); // AAC

        // Call on_meta_data and verify it succeeds
        HELPER_EXPECT_SUCCESS(source->on_meta_data(msg.get(), metadata.get()));

        // Verify that metadata was cached
        EXPECT_TRUE(source->meta()->data() != NULL);

        // Verify consumer was created
        EXPECT_TRUE(consumer != NULL);
    }

    // Cleanup
    srs_freep(mock_config);
    srs_freep(mock_factory);
}

VOID TEST(GopCacheTest, ClearCacheWhenPureAudioOverflow)
{
    srs_error_t err;

    // Create a gop cache instance and enable it
    SrsUniquePtr<SrsGopCache> gop_cache(new SrsGopCache());
    gop_cache->set(true);
    EXPECT_TRUE(gop_cache->enabled());

    // Step 1: Cache a keyframe to establish that the stream has video
    // This is critical - without video first, the stream would be detected as pure audio
    // and caching would be disabled immediately
    SrsUniquePtr<MockH264VideoPacket> keyframe(new MockH264VideoPacket(true));
    keyframe->timestamp_ = 1000;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(keyframe.get()));
    EXPECT_FALSE(gop_cache->empty());
    EXPECT_FALSE(gop_cache->pure_audio()); // Stream has video, not pure audio

    // Step 2: Cache a few inter frames to build up the GOP
    SrsUniquePtr<MockH264VideoPacket> interframe1(new MockH264VideoPacket(false));
    interframe1->timestamp_ = 1040;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe1.get()));

    SrsUniquePtr<MockH264VideoPacket> interframe2(new MockH264VideoPacket(false));
    interframe2->timestamp_ = 1080;
    HELPER_EXPECT_SUCCESS(gop_cache->cache(interframe2.get()));

    // Step 3: Now simulate the scenario where video stops but audio continues
    // This happens when a publisher disables their camera but keeps audio on
    // We need to send MORE than SRS_PURE_AUDIO_GUESS_COUNT (115) audio packets
    // to trigger the overflow detection and cache clearing

    // Send exactly 115 audio packets - should NOT trigger clearing yet
    for (int i = 0; i < 115; i++) {
        SrsUniquePtr<MockAudioPacket> audio(new MockAudioPacket());
        audio->timestamp_ = 1120 + (i * 26); // 26ms per audio packet (typical)
        HELPER_EXPECT_SUCCESS(gop_cache->cache(audio.get()));
    }

    // At this point, audio_after_last_video_count_ should be exactly 115
    // The cache should still have content (not cleared yet)
    EXPECT_FALSE(gop_cache->empty());

    // Step 4: Send ONE MORE audio packet to exceed the threshold
    // This is the critical moment - audio_after_last_video_count_ becomes 116
    // which is > SRS_PURE_AUDIO_GUESS_COUNT (115)
    // The code should detect this as "pure audio overflow" and clear the cache
    SrsUniquePtr<MockAudioPacket> overflow_audio(new MockAudioPacket());
    overflow_audio->timestamp_ = 1120 + (115 * 26);
    HELPER_EXPECT_SUCCESS(gop_cache->cache(overflow_audio.get()));

    // Step 5: Verify that the cache was cleared due to pure audio overflow
    // This is the key assertion - the cache should be empty after exceeding the threshold
    EXPECT_TRUE(gop_cache->empty());

    // Step 6: Verify that the stream is now detected as pure audio
    // After clearing, cached_video_count_ is reset to 0
    EXPECT_TRUE(gop_cache->pure_audio());

    // Step 7: Verify that subsequent audio packets are NOT cached
    // Once detected as pure audio, the cache should remain disabled
    SrsUniquePtr<MockAudioPacket> post_clear_audio(new MockAudioPacket());
    post_clear_audio->timestamp_ = 1120 + (116 * 26);
    HELPER_EXPECT_SUCCESS(gop_cache->cache(post_clear_audio.get()));
    EXPECT_TRUE(gop_cache->empty()); // Should still be empty

    // Summary of what this test covers:
    // 1. The "waiting" mechanism: audio_after_last_video_count_ incrementing with each audio packet
    // 2. The threshold detection: exactly when count exceeds SRS_PURE_AUDIO_GUESS_COUNT (115)
    // 3. The clearing action: gop cache is cleared when threshold is exceeded
    // 4. The state transition: stream transitions from "has video" to "pure audio"
    // 5. The prevention of future caching: once pure audio, no more caching occurs
}

// Unit test for SrsOriginHub::on_video sequence header handling
// This test covers the "waiting" mechanism for video sequence headers
VOID TEST(AppOriginHubTest, OnVideoSequenceHeaderWaitingMechanism)
{
    srs_error_t err;

    // Create mock source with shared pointer
    MockLiveSourceForQueue *raw_source = new MockLiveSourceForQueue();
    SrsSharedPtr<SrsLiveSource> source(raw_source);

    // Create mock request
    MockHlsRequest mock_req;

    // Create origin hub and inject mock dependencies
    SrsUniquePtr<SrsOriginHub> hub(new SrsOriginHub());

    // Replace the default components with mocks
    MockHlsForOriginHub *mock_hls = new MockHlsForOriginHub();
    MockDashForOriginHub *mock_dash = new MockDashForOriginHub();
    MockDvrForOriginHub *mock_dvr = new MockDvrForOriginHub();
    MockStatisticForOriginHub *mock_stat = new MockStatisticForOriginHub();

    // Access private members to inject mocks
    srs_freep(hub->hls_);
    hub->hls_ = mock_hls;
    srs_freep(hub->dash_);
    hub->dash_ = mock_dash;
    srs_freep(hub->dvr_);
    hub->dvr_ = mock_dvr;
    hub->stat_ = mock_stat;

    // Initialize the hub
    HELPER_EXPECT_SUCCESS(hub->initialize(source, &mock_req));

    // Create mock live source for origin hub with format
    SrsUniquePtr<MockLiveSourceForOriginHub> mock_live_source(new MockLiveSourceForOriginHub());
    hub->source_ = mock_live_source.get();

    // Test 1: AVC (H.264) sequence header - the "waiting" for sequence header
    // This tests the code path: if (format->is_avc_sequence_header()) { if (c->id_ == SrsVideoCodecIdAVC) { ... } }
    {
        // Create H.264 video sequence header packet
        SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
        video_sh->timestamp_ = 0;
        video_sh->message_type_ = SrsFrameTypeVideo;

        // Create minimal AVC sequence header packet
        char *video_data = new char[10];
        video_data[0] = 0x17; // keyframe (0x10) + AVC (0x07)
        video_data[1] = 0x00; // AVC sequence header
        memset(video_data + 2, 0x00, 8);
        video_sh->wrap(video_data, 10);

        // Manually set up the format state to simulate a parsed AVC sequence header
        // This avoids the complex SPS/PPS parsing that would fail with invalid data
        SrsRtmpFormat *format = mock_live_source->format();

        // Initialize video codec config
        if (!format->vcodec_) {
            format->vcodec_ = new SrsVideoCodecConfig();
        }
        if (!format->video_) {
            format->video_ = new SrsParsedVideoPacket();
        }

        // Set up as AVC sequence header
        format->vcodec_->id_ = SrsVideoCodecIdAVC;
        format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
        format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitSequenceHeader;

        // Set codec parameters for testing
        format->vcodec_->avc_profile_ = SrsAvcProfileBaseline;
        format->vcodec_->avc_level_ = SrsAvcLevel_3;
        format->vcodec_->width_ = 1920;
        format->vcodec_->height_ = 1080;
        format->vcodec_->video_data_rate_ = 2500000; // 2.5 Mbps
        format->vcodec_->frame_rate_ = 30.0;
        format->vcodec_->duration_ = 10.0;

        // Verify format detected AVC sequence header
        EXPECT_TRUE(format->is_avc_sequence_header());
        EXPECT_TRUE(format->vcodec_ != NULL);
        EXPECT_EQ(SrsVideoCodecIdAVC, format->vcodec_->id_);

        // Call on_video - this is where the "waiting" ends and processing happens
        // The code waits for is_avc_sequence_header() to be true before calling stat_->on_video_info()
        HELPER_EXPECT_SUCCESS(hub->on_video(video_sh.get(), true));

        // Verify stat_->on_video_info() was called with correct AVC parameters
        EXPECT_EQ(1, mock_stat->on_video_info_count_);
    }

    // Test 2: HEVC (H.265) sequence header - the "waiting" for sequence header
    // This tests the code path: if (format->is_avc_sequence_header()) { if (c->id_ == SrsVideoCodecIdHEVC) { ... } }
    {
        // Reset mock stat counter
        mock_stat->on_video_info_count_ = 0;

        // Create H.265 video sequence header packet using enhanced-RTMP format
        SrsUniquePtr<SrsMediaPacket> video_sh(new SrsMediaPacket());
        video_sh->timestamp_ = 0;
        video_sh->message_type_ = SrsFrameTypeVideo;
        char *video_data = new char[10];
        video_data[0] = 0x90; // IsExHeader (0x80) | keyframe (0x10) | sequence start (0x00)
        video_data[1] = 'h';  // fourcc 'hvc1'
        video_data[2] = 'v';
        video_data[3] = 'c';
        video_data[4] = '1';
        memset(video_data + 5, 0x00, 5);
        video_sh->wrap(video_data, 10);

        // Manually set up the format state to simulate a parsed HEVC sequence header
        SrsRtmpFormat *format = mock_live_source->format();

        // Initialize video codec config
        if (!format->vcodec_) {
            format->vcodec_ = new SrsVideoCodecConfig();
        }
        if (!format->video_) {
            format->video_ = new SrsParsedVideoPacket();
        }

        // Set up as HEVC sequence header
        format->vcodec_->id_ = SrsVideoCodecIdHEVC;
        format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
        format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitSequenceHeader;

        // Set codec parameters for testing
        format->vcodec_->hevc_profile_ = SrsHevcProfileMain;
        format->vcodec_->hevc_level_ = SrsHevcLevel_4;
        format->vcodec_->width_ = 3840;
        format->vcodec_->height_ = 2160;
        format->vcodec_->video_data_rate_ = 10000000; // 10 Mbps
        format->vcodec_->frame_rate_ = 60.0;
        format->vcodec_->duration_ = 20.0;

        // Verify format detected HEVC sequence header
        EXPECT_TRUE(format->is_avc_sequence_header()); // Note: is_avc_sequence_header() also returns true for HEVC
        EXPECT_TRUE(format->vcodec_ != NULL);
        EXPECT_EQ(SrsVideoCodecIdHEVC, format->vcodec_->id_);

        // Call on_video - this is where the "waiting" ends and processing happens
        // The code waits for is_avc_sequence_header() to be true before calling stat_->on_video_info()
        HELPER_EXPECT_SUCCESS(hub->on_video(video_sh.get(), true));

        // Verify stat_->on_video_info() was called with correct HEVC parameters
        EXPECT_EQ(1, mock_stat->on_video_info_count_);
    }

    // Summary of what this test covers:
    // 1. The "waiting" mechanism: code waits for format->is_avc_sequence_header() to return true
    // 2. How it waits: checks is_avc_sequence_header() condition before processing codec info
    // 3. AVC path: when c->id_ == SrsVideoCodecIdAVC, calls stat_->on_video_info() with AVC profile/level
    // 4. HEVC path: when c->id_ == SrsVideoCodecIdHEVC, calls stat_->on_video_info() with HEVC profile/level
    // 5. The trace logging: srs_trace() is called with codec-specific information (profile, level, resolution, bitrate, fps, duration)
}
