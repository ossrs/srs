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
    send_count_ = 0;
    fail_send_at_ = 0;
    recv_timeout_ = 0;
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
    send_count_++;
    batches_.push_back(nb_msgs);
    for (int i = 0; i < nb_msgs; i++) {
        record(msgs[i]);
        srs_freep(msgs[i]);
    }
    return send_result();
}

srs_error_t MockRtmpClientForForwarder::send_and_free_message(SrsMediaPacket *msg)
{
    send_count_++;
    record(msg);
    srs_freep(msg);
    return send_result();
}

void MockRtmpClientForForwarder::set_recv_timeout(srs_utime_t timeout)
{
    recv_timeout_ = timeout;
}

srs_error_t MockRtmpClientForForwarder::send_result()
{
    if (fail_send_at_ > 0 && send_count_ != fail_send_at_) {
        return srs_success;
    }
    return srs_error_copy(send_error_);
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

MockAppFactoryForForwarderCycle::MockAppFactoryForForwarderCycle()
{
    next_ = NULL;
    cto_ = 0;
    sto_ = 0;
}

MockAppFactoryForForwarderCycle::~MockAppFactoryForForwarderCycle()
{
    // A client handed out is owned by the forwarder; only an unused one is ours.
    srs_freep(next_);
}

ISrsBasicRtmpClient *MockAppFactoryForForwarderCycle::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    MockRtmpClientForForwarder *client = next_ ? next_ : new MockRtmpClientForForwarder();
    next_ = NULL;

    client->url_ = url;
    clients_.push_back(client);
    urls_.push_back(url);
    cto_ = cto;
    sto_ = sto;
    return client;
}

MockOriginHubForForwarderStart::MockOriginHubForForwarderStart()
{
    metadata_ = NULL;
    vsh_ = NULL;
    ash_ = NULL;
}

MockOriginHubForForwarderStart::~MockOriginHubForForwarderStart()
{
    srs_freep(metadata_);
    srs_freep(vsh_);
    srs_freep(ash_);
}

srs_error_t MockOriginHubForForwarderStart::on_forwarder_start(SrsForwarder *forwarder)
{
    srs_error_t err = srs_success;

    on_forwarder_start_count_++;
    if (on_forwarder_start_error_ != srs_success) {
        return srs_error_copy(on_forwarder_start_error_);
    }

    if (metadata_ && (err = forwarder->on_meta_data(metadata_)) != srs_success) {
        return srs_error_wrap(err, "forward metadata");
    }
    if (vsh_ && (err = forwarder->on_video(vsh_)) != srs_success) {
        return srs_error_wrap(err, "forward video sh");
    }
    if (ash_ && (err = forwarder->on_audio(ash_)) != srs_success) {
        return srs_error_wrap(err, "forward audio sh");
    }

    return err;
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

// Wire a forwarder to the mock factory and config, and give it a coroutine that allows nb_pulls
// pulls. The forwarder owns the coroutine.
static MockCoroutineForForwarder *utest_forward_wire(SrsForwarder *forwarder, MockAppFactoryForForwarderCycle *factory,
                                                     MockAppConfig *config, int nb_pulls)
{
    forwarder->app_factory_ = factory;
    forwarder->config_ = config;

    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(nb_pulls);
    srs_freep(forwarder->trd_);
    forwarder->trd_ = trd;
    return trd;
}

// do_cycle() builds the destination url from the endpoint host:port and the request's app,
// stream, query and vhost, and connects with a 3s connect and 30s stream timeout.
VOID TEST(ForwarderTest, DoCycleUrlFromEndpointAndRequest)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());

    // A host:port endpoint, a vhost that is not the default one, and a query.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
        req->param_ = "?token=abc";
        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "10.0.0.1:19350"));
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 1);

        err = forwarder->do_cycle();
        // The coroutine quits on the second pull inside forward().
        HELPER_EXPECT_FAILED(err);

        ASSERT_EQ(1, (int)factory->urls_.size());
        EXPECT_STREQ("rtmp://10.0.0.1:19350/live/stream1?token=abc&vhost=test.vhost", factory->urls_[0].c_str());
        EXPECT_EQ(3 * SRS_UTIME_SECONDS, factory->cto_);
        EXPECT_EQ(SRS_CONSTS_RTMP_TIMEOUT, factory->sto_);

        MockRtmpClientForForwarder *sdk = factory->clients_[0];
        EXPECT_EQ(1, sdk->connect_count_);
        EXPECT_EQ(1, sdk->publish_count_);
        EXPECT_EQ(SRS_CONSTS_RTMP_PULSE, sdk->recv_timeout_);
        EXPECT_EQ(1, hub->on_forwarder_start_count_);
    }

    // An endpoint without a port uses 1935; the default vhost with an IP host adds no vhost.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));
        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "10.0.0.2"));
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 1);

        err = forwarder->do_cycle();
        HELPER_EXPECT_FAILED(err);

        ASSERT_EQ(2, (int)factory->urls_.size());
        EXPECT_STREQ("rtmp://10.0.0.2:1935/live/livestream", factory->urls_[1].c_str());
    }

    // A vhost in the query is kept and no other vhost is added.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "stream1"));
        req->param_ = "?vhost=other.vhost&token=abc";
        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "10.0.0.3:1936"));
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 1);

        err = forwarder->do_cycle();
        HELPER_EXPECT_FAILED(err);

        ASSERT_EQ(3, (int)factory->urls_.size());
        EXPECT_STREQ("rtmp://10.0.0.3:1936/live/stream1?vhost=other.vhost&token=abc", factory->urls_[2].c_str());
    }
}

// A connect failure returns an error before publishing; a publish failure returns an error
// before the hub is told; a hub failure returns an error before anything is sent.
VOID TEST(ForwarderTest, DoCycleStopsAtFirstFailure)
{
    srs_error_t err;

    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    // Connect fails.
    if (true) {
        SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 10);

        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
        factory->next_ = sdk;

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(1, sdk->connect_count_);
        EXPECT_EQ(0, sdk->publish_count_);
        EXPECT_EQ(0, hub->on_forwarder_start_count_);
        EXPECT_EQ(0, (int)sdk->sent_.size());
        // The queue is left for the next attempt.
        EXPECT_EQ(1, forwarder->queue_->size());
    }

    // Publish fails.
    if (true) {
        SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 10);

        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->publish_error_ = srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "mock publish");
        factory->next_ = sdk;

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_RTMP_MESSAGE_DECODE, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(1, sdk->connect_count_);
        EXPECT_EQ(1, sdk->publish_count_);
        EXPECT_EQ(0, hub->on_forwarder_start_count_);
        EXPECT_EQ(0, (int)sdk->sent_.size());
        EXPECT_EQ(1, forwarder->queue_->size());
    }

    // The hub fails.
    if (true) {
        SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
        hub->on_forwarder_start_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock hub");
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 10);

        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        factory->next_ = sdk;

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(1, sdk->publish_count_);
        EXPECT_EQ(1, hub->on_forwarder_start_count_);
        EXPECT_EQ(0, (int)sdk->sent_.size());
        EXPECT_EQ(0, sdk->recv_count_);
        EXPECT_EQ(1, forwarder->queue_->size());
    }
}

// forward() sends the cached video then audio sequence header one by one, then the queue in
// order, at most 128 packets per send.
VOID TEST(ForwarderTest, ForwardSendsInBatchesOf128)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));
    for (int i = 1; i <= 300; i++) {
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(i, 'a')));
    }

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    forwarder->sdk_ = sdk;
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(4);
    srs_freep(forwarder->trd_);
    forwarder->trd_ = trd;

    err = forwarder->forward();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    // Two single sends for the cached headers, then three batches; the fourth pass finds the
    // queue empty and sends nothing.
    EXPECT_EQ(2 + 3, sdk->send_count_);
    ASSERT_EQ(3, (int)sdk->batches_.size());
    EXPECT_EQ(128, sdk->batches_[0]);
    EXPECT_EQ(128, sdk->batches_[1]);
    EXPECT_EQ(302 - 256, sdk->batches_[2]);
    EXPECT_EQ(4, sdk->recv_count_);
    EXPECT_EQ(5, trd->pull_count_);
    EXPECT_EQ(0, forwarder->queue_->size());

    ASSERT_EQ(2 + 302, (int)sdk->sent_.size());
    EXPECT_EQ(SrsFrameTypeVideo, sdk->sent_[0].type_);
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ(SrsFrameTypeAudio, sdk->sent_[1].type_);
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[1]));
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[2]));
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[3]));
    for (int i = 1; i <= 300; i++) {
        EXPECT_EQ(i, sdk->sent_[3 + i].timestamp_);
    }
}

// A send failure ends forward() with that error, whether it is a cached header or a batch.
VOID TEST(ForwarderTest, ForwardEndsOnSendFailure)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());

    // The cached video sequence header fails: the audio one is never sent.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));

        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
        sdk->fail_send_at_ = 1;
        forwarder->sdk_ = sdk;
        srs_freep(forwarder->trd_);
        forwarder->trd_ = new MockCoroutineForForwarder(10);

        err = forwarder->forward();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(1, sdk->send_count_);
        ASSERT_EQ(1, (int)sdk->sent_.size());
        EXPECT_EQ('V', utest_forward_marker(sdk->sent_[0]));
        EXPECT_EQ(0, sdk->recv_count_);
        EXPECT_EQ(2, forwarder->queue_->size());
    }

    // The cached audio sequence header fails: the queue is never read.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));

        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
        sdk->fail_send_at_ = 2;
        forwarder->sdk_ = sdk;
        srs_freep(forwarder->trd_);
        forwarder->trd_ = new MockCoroutineForForwarder(10);

        err = forwarder->forward();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(2, sdk->send_count_);
        EXPECT_EQ(0, sdk->recv_count_);
        EXPECT_EQ(2, forwarder->queue_->size());
    }

    // No cached headers, the first batch fails: the batch is consumed from the queue and lost.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(10, 'a')));
        HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(20, 'b')));

        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
        forwarder->sdk_ = sdk;
        srs_freep(forwarder->trd_);
        forwarder->trd_ = new MockCoroutineForForwarder(10);

        err = forwarder->forward();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(1, sdk->send_count_);
        ASSERT_EQ(1, (int)sdk->batches_.size());
        EXPECT_EQ(2, sdk->batches_[0]);
        EXPECT_EQ(1, sdk->recv_count_);
        EXPECT_EQ(0, forwarder->queue_->size());
    }
}

// A receive error other than a timeout ends forward() before the queue is sent; a timeout is the
// normal case and is ignored.
VOID TEST(ForwarderTest, ForwardEndsOnReceiveError)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(10, 'a')));

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->recv_error_ = srs_error_new(ERROR_SOCKET_READ, "mock recv");
    forwarder->sdk_ = sdk;
    srs_freep(forwarder->trd_);
    forwarder->trd_ = new MockCoroutineForForwarder(10);

    err = forwarder->forward();
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(1, sdk->recv_count_);
    // Only the cached sequence header went out; the queue is untouched.
    ASSERT_EQ(1, (int)sdk->sent_.size());
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ(0, (int)sdk->batches_.size());
    EXPECT_EQ(2, forwarder->queue_->size());
}

// A stopped coroutine ends forward() after the cached headers, before any read or queue send.
VOID TEST(ForwarderTest, ForwardEndsWhenCoroutineStopped)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_raw(10, 'a')));

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    forwarder->sdk_ = sdk;
    MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(0);
    srs_freep(forwarder->trd_);
    forwarder->trd_ = trd;

    err = forwarder->forward();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(1, trd->pull_count_);
    EXPECT_EQ(0, sdk->recv_count_);
    ASSERT_EQ(2, (int)sdk->sent_.size());
    EXPECT_EQ('V', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ('A', utest_forward_marker(sdk->sent_[1]));
    EXPECT_EQ(0, (int)sdk->batches_.size());
    EXPECT_EQ(3, forwarder->queue_->size());
}

// Today's reconnect: what was queued while disconnected is sent as a stale burst, and every
// connect sends the sequence headers several times and the metadata after the media. The hub's
// cached metadata and sequence headers are enqueued behind the queue by on_forwarder_start(),
// while forward() first sends the forwarder's own cached sequence headers.
VOID TEST(ForwarderTest, ReconnectSendsStaleQueueThenHeadersAgain)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHubForForwarderStart> hub(new MockOriginHubForForwarderStart());
    hub->metadata_ = utest_forward_metadata(0, 'M');
    hub->vsh_ = utest_forward_video_sh(0, 'W');
    hub->ash_ = utest_forward_audio_sh(0, 'B');

    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));

    // The stream as the publisher sends it, before the forwarder connects.
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_metadata(0, 'm')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_sh(0, 'V')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_audio_sh(0, 'A')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_keyframe(100, 'k')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(200, 'p')));

    // The first connect: the first queue send fails.
    std::vector<MockForwarderSentPacket> first;
    if (true) {
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 10);
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock send");
        sdk->fail_send_at_ = 3;
        factory->next_ = sdk;

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
        srs_freep(err);

        EXPECT_EQ(1, hub->on_forwarder_start_count_);
        ASSERT_EQ(1, (int)sdk->batches_.size());
        EXPECT_EQ(8, sdk->batches_[0]);
        first = sdk->sent_;
    }

    // The cached headers, now the hub's; the publisher's stream; the hub's metadata and headers.
    const char first_markers[] = {'W', 'B', 'm', 'V', 'A', 'k', 'p', 'M', 'W', 'B'};
    const int64_t first_timestamps[] = {0, 0, 0, 0, 0, 100, 200, 0, 0, 0};
    ASSERT_EQ(10, (int)first.size());
    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(first_markers[i], utest_forward_marker(first[i])) << "first #" << i;
        EXPECT_EQ(first_timestamps[i], first[i].timestamp_) << "first #" << i;
    }
    EXPECT_EQ('W', utest_forward_marker(forwarder->sh_video_));
    EXPECT_EQ('B', utest_forward_marker(forwarder->sh_audio_));

    // Media that arrives while the forwarder waits to retry.
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(300, 'q')));
    HELPER_EXPECT_SUCCESS(utest_forward_feed(forwarder.get(), utest_forward_video_inter(400, 'r')));

    // The reconnect, with a new client.
    if (true) {
        utest_forward_wire(forwarder.get(), factory.get(), config.get(), 1);

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
        srs_freep(err);
    }

    EXPECT_EQ(2, hub->on_forwarder_start_count_);
    ASSERT_EQ(2, (int)factory->clients_.size());
    MockRtmpClientForForwarder *sdk = factory->clients_[1];
    EXPECT_EQ(1, sdk->connect_count_);
    EXPECT_EQ(1, sdk->publish_count_);

    // The cached headers, then the stale inter frames from the middle of a GOP, then the hub's
    // metadata and sequence headers again, after the media.
    const char second_markers[] = {'W', 'B', 'q', 'r', 'M', 'W', 'B'};
    const int64_t second_timestamps[] = {0, 0, 300, 400, 0, 0, 0};
    ASSERT_EQ(7, (int)sdk->sent_.size());
    for (int i = 0; i < 7; i++) {
        EXPECT_EQ(second_markers[i], utest_forward_marker(sdk->sent_[i])) << "second #" << i;
        EXPECT_EQ(second_timestamps[i], sdk->sent_[i].timestamp_) << "second #" << i;
    }
    ASSERT_EQ(1, (int)sdk->batches_.size());
    EXPECT_EQ(5, sdk->batches_[0]);
}

// on_unpublish() stops the coroutine and closes the client; with no client yet it only stops.
VOID TEST(ForwarderTest, UnpublishStopsCoroutineAndClosesClient)
{
    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());

    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        forwarder->sdk_ = sdk;
        MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(10);
        srs_freep(forwarder->trd_);
        forwarder->trd_ = trd;

        forwarder->on_unpublish();
        EXPECT_EQ(1, trd->stop_count_);
        EXPECT_EQ(1, sdk->close_count_);
        // The client is closed, not freed: the forwarder still owns it.
        EXPECT_TRUE(forwarder->sdk_ == sdk);
    }

    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        MockCoroutineForForwarder *trd = new MockCoroutineForForwarder(10);
        srs_freep(forwarder->trd_);
        forwarder->trd_ = trd;

        forwarder->on_unpublish();
        EXPECT_EQ(1, trd->stop_count_);
        EXPECT_TRUE(forwarder->sdk_ == NULL);
    }

    // A forwarder that never published has the dummy coroutine: unpublish is safe.
    if (true) {
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));
        forwarder->on_unpublish();
        EXPECT_TRUE(forwarder->sdk_ == NULL);
    }
}
