//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai34.hpp>

#include <srs_app_rtmp_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_manual_mock.hpp>

#include <string.h>

using namespace std;

MockRtmpClientForForwarder::MockRtmpClientForForwarder()
{
    connect_count_ = 0;
    publish_count_ = 0;
    close_count_ = 0;
    recv_count_ = 0;
    connect_error_ = srs_success;
    publish_error_ = srs_success;
    send_error_ = srs_success;
    recv_error_ = srs_success;
}

MockRtmpClientForForwarder::~MockRtmpClientForForwarder()
{
    srs_freep(connect_error_);
    srs_freep(publish_error_);
    srs_freep(send_error_);
    srs_freep(recv_error_);
}

srs_error_t MockRtmpClientForForwarder::connect()
{
    connect_count_++;
    return srs_error_copy(connect_error_);
}

void MockRtmpClientForForwarder::close()
{
    close_count_++;
}

srs_error_t MockRtmpClientForForwarder::publish(int chunk_size, bool with_vhost, std::string *pstream)
{
    publish_count_++;
    return srs_error_copy(publish_error_);
}

srs_error_t MockRtmpClientForForwarder::play(int chunk_size, bool with_vhost, std::string *pstream)
{
    return srs_success;
}

void MockRtmpClientForForwarder::kbps_sample(const char *label, srs_utime_t age)
{
}

void MockRtmpClientForForwarder::kbps_sample(const char *label, srs_utime_t age, int msgs)
{
}

int MockRtmpClientForForwarder::sid()
{
    return 1;
}

srs_error_t MockRtmpClientForForwarder::recv_message(SrsRtmpCommonMessage **pmsg)
{
    recv_count_++;
    *pmsg = NULL;
    if (recv_error_ != srs_success) {
        return srs_error_copy(recv_error_);
    }
    // A destination that sends nothing back: the forwarder reads with a short timeout.
    return srs_error_new(ERROR_SOCKET_TIMEOUT, "mock recv timeout");
}

srs_error_t MockRtmpClientForForwarder::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    return srs_success;
}

srs_error_t MockRtmpClientForForwarder::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs)
{
    batches_.push_back(nb_msgs);
    for (int i = 0; i < nb_msgs; i++) {
        record(msgs[i]);
        srs_freep(msgs[i]);
    }
    return srs_error_copy(send_error_);
}

srs_error_t MockRtmpClientForForwarder::send_and_free_message(SrsMediaPacket *msg)
{
    record(msg);
    srs_freep(msg);
    return srs_error_copy(send_error_);
}

void MockRtmpClientForForwarder::set_recv_timeout(srs_utime_t timeout)
{
}

void MockRtmpClientForForwarder::record(SrsMediaPacket *msg)
{
    MockForwarderSentPacket sent;
    sent.type_ = msg->message_type_;
    sent.timestamp_ = msg->timestamp_;
    sent.payload_ = string(msg->payload(), msg->size());
    sent_.push_back(sent);
}

MockCoroutineForForwarder::MockCoroutineForForwarder(int nb_pulls)
{
    nb_pulls_ = nb_pulls;
    pull_count_ = 0;
    stop_count_ = 0;
}

MockCoroutineForForwarder::~MockCoroutineForForwarder()
{
}

srs_error_t MockCoroutineForForwarder::start()
{
    return srs_success;
}

void MockCoroutineForForwarder::stop()
{
    stop_count_++;
}

void MockCoroutineForForwarder::interrupt()
{
}

srs_error_t MockCoroutineForForwarder::pull()
{
    pull_count_++;
    if (pull_count_ > nb_pulls_) {
        return srs_error_new(ERROR_THREAD_DUMMY, "mock pull quit");
    }
    return srs_success;
}

const SrsContextId &MockCoroutineForForwarder::cid()
{
    return cid_;
}

void MockCoroutineForForwarder::set_cid(const SrsContextId &cid)
{
    cid_ = cid;
}

// Build a packet of type t at timestamp ts with the given payload bytes.
static SrsMediaPacket *utest_forward_packet(SrsFrameType t, int64_t ts, const char *bytes, int size)
{
    char *buf = new char[size];
    memcpy(buf, bytes, size);

    SrsMediaPacket *pkt = new SrsMediaPacket();
    pkt->wrap(buf, size);
    pkt->message_type_ = t;
    pkt->timestamp_ = ts;
    return pkt;
}

// FLV tags, the last byte is a marker to tell packets apart.
static SrsMediaPacket *utest_forward_video_sh(int64_t ts, char marker)
{
    char b[] = {0x17, 0x00, 0x00, 0x00, 0x00, marker};
    return utest_forward_packet(SrsFrameTypeVideo, ts, b, sizeof(b));
}

static SrsMediaPacket *utest_forward_video_keyframe(int64_t ts, char marker)
{
    char b[] = {0x17, 0x01, 0x00, 0x00, 0x00, marker};
    return utest_forward_packet(SrsFrameTypeVideo, ts, b, sizeof(b));
}

static SrsMediaPacket *utest_forward_video_inter(int64_t ts, char marker)
{
    char b[] = {0x27, 0x01, 0x00, 0x00, 0x00, marker};
    return utest_forward_packet(SrsFrameTypeVideo, ts, b, sizeof(b));
}

static SrsMediaPacket *utest_forward_audio_sh(int64_t ts, char marker)
{
    char b[] = {(char)0xaf, 0x00, 0x12, marker};
    return utest_forward_packet(SrsFrameTypeAudio, ts, b, sizeof(b));
}

static SrsMediaPacket *utest_forward_audio_raw(int64_t ts, char marker)
{
    char b[] = {(char)0xaf, 0x01, 0x21, marker};
    return utest_forward_packet(SrsFrameTypeAudio, ts, b, sizeof(b));
}

static SrsMediaPacket *utest_forward_metadata(int64_t ts, char marker)
{
    char b[] = {0x02, 0x00, 0x0a, marker};
    return utest_forward_packet(SrsFrameTypeScript, ts, b, sizeof(b));
}

static char utest_forward_marker(SrsMediaPacket *pkt)
{
    return pkt->payload()[pkt->size() - 1];
}

static char utest_forward_marker(const MockForwarderSentPacket &sent)
{
    return sent.payload_.at(sent.payload_.size() - 1);
}

// Feed one packet to the forwarder through its intake of the packet type, then free the
// caller's packet, as the origin hub does.
static srs_error_t utest_forward_feed(SrsForwarder *forwarder, SrsMediaPacket *pkt)
{
    srs_error_t err = srs_success;
    if (pkt->message_type_ == SrsFrameTypeAudio) {
        err = forwarder->on_audio(pkt);
    } else if (pkt->message_type_ == SrsFrameTypeVideo) {
        err = forwarder->on_video(pkt);
    } else {
        err = forwarder->on_meta_data(pkt);
    }
    srs_freep(pkt);
    return err;
}

// Run one iteration of forward() against a recording client: the cached sequence headers, then
// one dump of the queue. The client and coroutine are owned by the forwarder after this.
static MockRtmpClientForForwarder *utest_forward_run_once(SrsForwarder *forwarder)
{
    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    srs_freep(forwarder->sdk_);
    forwarder->sdk_ = sdk;

    srs_freep(forwarder->trd_);
    forwarder->trd_ = new MockCoroutineForForwarder(1);

    srs_error_t err = forwarder->forward();
    // The coroutine quits on the second pull, which ends forward() with an error.
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    return sdk;
}

// Each intake enqueues its own copy that shares the payload; the caller's packet keeps its fields,
// and the queued copy outlives it.
VOID TEST(ForwarderTest, IntakeEnqueuesCopyOfEachPacket)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

    SrsMediaPacket *metadata = utest_forward_metadata(0, 'm');
    SrsMediaPacket *audio = utest_forward_audio_raw(20, 'a');
    SrsMediaPacket *video = utest_forward_video_keyframe(40, 'v');

    HELPER_EXPECT_SUCCESS(forwarder->on_meta_data(metadata));
    HELPER_EXPECT_SUCCESS(forwarder->on_audio(audio));
    HELPER_EXPECT_SUCCESS(forwarder->on_video(video));
    EXPECT_EQ(3, forwarder->queue_->size());

    // The caller's packets are untouched.
    SrsMediaPacket *callers[] = {metadata, audio, video};
    SrsFrameType types[] = {SrsFrameTypeScript, SrsFrameTypeAudio, SrsFrameTypeVideo};
    int64_t timestamps[] = {0, 20, 40};
    char markers[] = {'m', 'a', 'v'};
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(types[i], callers[i]->message_type_);
        EXPECT_EQ(timestamps[i], callers[i]->timestamp_);
        EXPECT_EQ(markers[i], utest_forward_marker(callers[i]));
    }

    SrsMediaPacket *msgs[8];
    int count = 0;
    HELPER_EXPECT_SUCCESS(forwarder->queue_->dump_packets(8, msgs, count));
    ASSERT_EQ(3, count);

    // The queued packets are copies, in the order of intake, sharing the payload.
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(msgs[i] != callers[i]);
        EXPECT_EQ(callers[i]->payload(), msgs[i]->payload());
        EXPECT_EQ(types[i], msgs[i]->message_type_);
        EXPECT_EQ(timestamps[i], msgs[i]->timestamp_);
    }

    // The copies stay valid after the caller frees its packets.
    srs_freep(metadata);
    srs_freep(audio);
    srs_freep(video);
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(markers[i], utest_forward_marker(msgs[i]));
        srs_freep(msgs[i]);
    }

    // Metadata and non-header packets are not cached.
    EXPECT_TRUE(forwarder->sh_audio_ == NULL);
    EXPECT_TRUE(forwarder->sh_video_ == NULL);
}

// An audio sequence header is cached as its own copy; a later one replaces it; a raw audio frame
// leaves it alone, and audio never touches the video cache.
VOID TEST(ForwarderTest, CachesAudioSequenceHeader)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

    SrsUniquePtr<SrsMediaPacket> sh1(utest_forward_audio_sh(0, '1'));
    HELPER_EXPECT_SUCCESS(forwarder->on_audio(sh1.get()));
    ASSERT_TRUE(forwarder->sh_audio_ != NULL);
    EXPECT_TRUE(forwarder->sh_audio_ != sh1.get());
    EXPECT_EQ('1', utest_forward_marker(forwarder->sh_audio_));
    EXPECT_TRUE(forwarder->sh_video_ == NULL);

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(20, 'r')));
    EXPECT_EQ('1', utest_forward_marker(forwarder->sh_audio_));

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(40, '2')));
    EXPECT_EQ('2', utest_forward_marker(forwarder->sh_audio_));
    EXPECT_EQ(40, forwarder->sh_audio_->timestamp_);
    EXPECT_TRUE(forwarder->sh_video_ == NULL);

    // Every packet is also queued, the sequence headers included.
    EXPECT_EQ(3, forwarder->queue_->size());
}

// A video sequence header is cached as its own copy; a later one replaces it; keyframes and inter
// frames leave it alone, and video never touches the audio cache.
VOID TEST(ForwarderTest, CachesVideoSequenceHeader)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

    SrsUniquePtr<SrsMediaPacket> sh1(utest_forward_video_sh(0, '1'));
    HELPER_EXPECT_SUCCESS(forwarder->on_video(sh1.get()));
    ASSERT_TRUE(forwarder->sh_video_ != NULL);
    EXPECT_TRUE(forwarder->sh_video_ != sh1.get());
    EXPECT_EQ('1', utest_forward_marker(forwarder->sh_video_));
    EXPECT_TRUE(forwarder->sh_audio_ == NULL);

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_keyframe(40, 'k')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(80, 'p')));
    EXPECT_EQ('1', utest_forward_marker(forwarder->sh_video_));

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(120, '2')));
    EXPECT_EQ('2', utest_forward_marker(forwarder->sh_video_));
    EXPECT_EQ(120, forwarder->sh_video_->timestamp_);
    EXPECT_TRUE(forwarder->sh_audio_ == NULL);

    EXPECT_EQ(4, forwarder->queue_->size());
}

// The forwarder keeps its own copy of the request, and the endpoint as given.
VOID TEST(ForwarderTest, InitializeCopiesRequestAndKeepsEndpoint)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());

    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        MockRequest *req = new MockRequest("test.vhost", "live", "stream1");
        req->param_ = "?token=abc";

        HELPER_EXPECT_SUCCESS(forwarder->initialize(req, "127.0.0.1:19350"));
        EXPECT_STREQ("127.0.0.1:19350", forwarder->ep_forward_.c_str());
        ASSERT_TRUE(forwarder->req_ != NULL);
        EXPECT_TRUE(forwarder->req_ != req);

        // Changing the original afterwards changes nothing in the forwarder.
        req->vhost_ = "other.vhost";
        req->app_ = "other";
        req->stream_ = "stream2";
        req->param_ = "?token=xyz";
        EXPECT_STREQ("test.vhost", forwarder->req_->vhost_.c_str());
        EXPECT_STREQ("live", forwarder->req_->app_.c_str());
        EXPECT_STREQ("stream1", forwarder->req_->stream_.c_str());
        EXPECT_STREQ("?token=abc", forwarder->req_->param_.c_str());

        // Freeing the original leaves the copy valid.
        srs_freep(req);
        EXPECT_STREQ("stream1", forwarder->req_->stream_.c_str());
    }

    // An endpoint without a port is kept as is; the default port is applied when connecting.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1"));
        EXPECT_STREQ("127.0.0.1", forwarder->ep_forward_.c_str());
        EXPECT_STREQ("livestream", forwarder->req_->stream_.c_str());
    }
}

// With no queue size set, the queue is unbounded: forward() sends the cached video then audio
// sequence header, then every queued packet in order, in one batch.
VOID TEST(ForwarderTest, UnboundedQueueSendsEveryPacket)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));
    for (int i = 1; i <= 12; i++) {
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(i * 100, 'a' + i)));
    }

    MockRtmpClientForForwarder *sdk = utest_forward_run_once(forwarder.get());

    ASSERT_EQ(2 + 14, (int)sdk->sent_.size());
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[1]));
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[2]));
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[3]));
    for (int i = 1; i <= 12; i++) {
        EXPECT_EQ('a' + i, utest_forward_marker(sdk->sent_[3 + i]));
        EXPECT_EQ(i * 100, sdk->sent_[3 + i].timestamp_);
    }
    ASSERT_EQ(1, (int)sdk->batches_.size());
    EXPECT_EQ(14, sdk->batches_[0]);
}

// set_queue_size() bounds the queue by duration: once the queued media spans more than the size,
// the queue drops everything except the sequence headers, which take the timestamp of the newest
// packet. forward() then sends only the cached headers, the kept headers, and what came after.
VOID TEST(ForwarderTest, QueueSizeShrinksToSequenceHeaders)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
    forwarder->set_queue_size(1 * SRS_UTIME_SECONDS);

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_keyframe(100, 'k')));
    for (int i = 2; i <= 10; i++) {
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(i * 100, 'a' + i)));
    }
    EXPECT_EQ(2 + 1 + 9, forwarder->queue_->size());

    // 1100ms spans more than 1s from the first packet at 0ms: the queue shrinks.
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(1100, 'x')));
    EXPECT_EQ(2, forwarder->queue_->size());

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(1200, 'y')));
    EXPECT_EQ(3, forwarder->queue_->size());

    MockRtmpClientForForwarder *sdk = utest_forward_run_once(forwarder.get());

    ASSERT_EQ(5, (int)sdk->sent_.size());
    // The cached sequence headers, as received.
    EXPECT_EQ(SrsFrameTypeVideo, sdk->sent_[0].type_);
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ(0, sdk->sent_[0].timestamp_);
    EXPECT_EQ(SrsFrameTypeAudio, sdk->sent_[1].type_);
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[1]));
    EXPECT_EQ(0, sdk->sent_[1].timestamp_);
    // The sequence headers kept by the shrink, at the time of the packet that overflowed.
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[2]));
    EXPECT_EQ(1100, sdk->sent_[2].timestamp_);
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[3]));
    EXPECT_EQ(1100, sdk->sent_[3].timestamp_);
    // The overflowing inter frame was dropped too; only the one after it is left.
    EXPECT_EQ('y', utest_forward_marker(sdk->sent_[4]));
    EXPECT_EQ(1200, sdk->sent_[4].timestamp_);
}
