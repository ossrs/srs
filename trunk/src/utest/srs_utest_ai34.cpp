//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai34.hpp>

#include <srs_app_http_hooks.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_json.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_manual_config.hpp>
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

MockAppConfigForForwardHub::MockAppConfigForForwardHub()
{
    forward_enabled_ = true;
    queue_length_ = 30 * SRS_UTIME_SECONDS;
}

MockAppConfigForForwardHub::~MockAppConfigForForwardHub()
{
}

bool MockAppConfigForForwardHub::get_forward_enabled(std::string vhost)
{
    return forward_enabled_;
}

srs_utime_t MockAppConfigForForwardHub::get_queue_length(std::string vhost)
{
    return queue_length_;
}

MockHttpHooksForForwardHub::MockHttpHooksForForwardHub()
{
    count_ = 0;
    req_ = NULL;
    error_ = srs_success;
}

MockHttpHooksForForwardHub::~MockHttpHooksForForwardHub()
{
    srs_freep(error_);
}

srs_error_t MockHttpHooksForForwardHub::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    count_++;
    url_ = url;
    req_ = req;
    if (error_ != srs_success) {
        return srs_error_copy(error_);
    }

    rtmp_urls = urls_;
    return srs_success;
}

MockForwarderForFanOut::MockForwarderForFanOut(std::string name, std::vector<std::string> *log)
{
    name_ = name;
    log_ = log;
    meta_error_ = srs_success;
    audio_error_ = srs_success;
    video_error_ = srs_success;
}

MockForwarderForFanOut::~MockForwarderForFanOut()
{
    log_->push_back(name_ + ":freed");
    srs_freep(meta_error_);
    srs_freep(audio_error_);
    srs_freep(video_error_);
}

srs_error_t MockForwarderForFanOut::initialize(ISrsRequest *r, std::string ep)
{
    return srs_success;
}

void MockForwarderForFanOut::set_queue_size(srs_utime_t queue_size)
{
}

srs_error_t MockForwarderForFanOut::on_publish()
{
    return srs_success;
}

void MockForwarderForFanOut::on_unpublish()
{
    log_->push_back(name_ + ":unpublish");
}

srs_error_t MockForwarderForFanOut::on_meta_data(SrsMediaPacket *shared_metadata)
{
    return record(shared_metadata, meta_error_);
}

srs_error_t MockForwarderForFanOut::on_audio(SrsMediaPacket *shared_audio)
{
    return record(shared_audio, audio_error_);
}

srs_error_t MockForwarderForFanOut::on_video(SrsMediaPacket *shared_video)
{
    return record(shared_video, video_error_);
}

srs_error_t MockForwarderForFanOut::record(SrsMediaPacket *pkt, srs_error_t err)
{
    packets_.push_back(pkt);
    log_->push_back(name_ + ":" + std::string(1, utest_forward_marker(pkt)));
    return srs_error_copy(err);
}

// An origin hub with mock HLS, DASH, DVR, encoder, exec and HDS, on the given source, request,
// config and hooks, so only its forward parts do real work.
static SrsOriginHub *utest_forward_hub(MockLiveSourceForOriginHub *source, ISrsRequest *req, ISrsAppConfig *config, ISrsHttpHooks *hooks)
{
    SrsOriginHub *hub = new SrsOriginHub();

    srs_freep(hub->hls_);
    hub->hls_ = new MockHlsForOriginHub();
    srs_freep(hub->dash_);
    hub->dash_ = new MockDashForOriginHub();
    srs_freep(hub->dvr_);
    hub->dvr_ = new MockDvrForOriginHub();
    srs_freep(hub->encoder_);
    hub->encoder_ = new MockMediaEncoderForOriginHub();
    srs_freep(hub->ng_exec_);
    hub->ng_exec_ = new MockNgExecForOriginHub();
#ifdef SRS_HDS
    srs_freep(hub->hds_);
    hub->hds_ = new MockHdsForOriginHub();
#endif

    hub->source_ = source;
    hub->req_ = req;
    hub->config_ = config;
    hub->hooks_ = hooks;
    return hub;
}

static SrsForwarder *utest_forward_at(SrsOriginHub *hub, int i)
{
    return dynamic_cast<SrsForwarder *>(hub->forwarders_.at(i));
}

// Whether the forwarder's coroutine was started by on_publish(), not the dummy one it is built with.
static bool utest_forward_started(SrsForwarder *forwarder)
{
    return dynamic_cast<SrsSTCoroutine *>(forwarder->trd_) != NULL;
}

// With forward disabled, neither the destinations nor the backend create a forwarder, and the
// backend is never asked.
VOID TEST(OriginHubForwardTest, DisabledCreatesNoForwarders)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    config->forward_enabled_ = false;
    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    config->set_forward_destinations(destinations);
    config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    hooks->urls_.push_back("rtmp://127.0.0.1:1937/live/livestream");

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());

    EXPECT_EQ(0, (int)hub->forwarders_.size());
    EXPECT_EQ(0, hooks->count_);
}

// Each destination gets one started forwarder, in config order, with the entry as its endpoint,
// its own copy of the publisher's request, and the vhost's queue length.
VOID TEST(OriginHubForwardTest, DestinationCreatesOneForwarderPerEntry)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "livestream"));
    req->param_ = "?token=abc";

    config->queue_length_ = 7 * SRS_UTIME_SECONDS;
    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    destinations.push_back("127.0.0.1:1937");
    destinations.push_back("10.0.0.1");
    config->set_forward_destinations(destinations);

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());

    EXPECT_EQ(0, hooks->count_);
    ASSERT_EQ(3, (int)hub->forwarders_.size());
    for (int i = 0; i < 3; i++) {
        SrsForwarder *forwarder = utest_forward_at(hub.get(), i);
        ASSERT_TRUE(forwarder != NULL);
        EXPECT_STREQ(destinations[i].c_str(), forwarder->ep_forward_.c_str()) << "#" << i;
        EXPECT_TRUE(forwarder->req_ != req.get()) << "#" << i;
        EXPECT_STREQ("test.vhost", forwarder->req_->vhost_.c_str()) << "#" << i;
        EXPECT_STREQ("live", forwarder->req_->app_.c_str()) << "#" << i;
        EXPECT_STREQ("livestream", forwarder->req_->stream_.c_str()) << "#" << i;
        EXPECT_STREQ("?token=abc", forwarder->req_->param_.c_str()) << "#" << i;
        EXPECT_EQ(7 * SRS_UTIME_SECONDS, forwarder->queue_->max_queue_size_) << "#" << i;
        EXPECT_TRUE(utest_forward_started(forwarder)) << "#" << i;
    }

    hub->destroy_forwarders();
    EXPECT_EQ(0, (int)hub->forwarders_.size());
}

// An rtmps:// destination fails create_forwarders(): the forwarders before it are started, the
// failed one is kept in the list but never started, and the ones after it are not created.
VOID TEST(OriginHubForwardTest, RtmpsDestinationFailsCreate)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    destinations.push_back("rtmps://127.0.0.1:443/live");
    destinations.push_back("127.0.0.1:1938");
    config->set_forward_destinations(destinations);

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    err = hub->create_forwarders();
    EXPECT_EQ(ERROR_NOT_SUPPORTED, srs_error_code(err));
    srs_freep(err);

    ASSERT_EQ(2, (int)hub->forwarders_.size());
    EXPECT_TRUE(utest_forward_started(utest_forward_at(hub.get(), 0)));
    EXPECT_STREQ("rtmps://127.0.0.1:443/live", utest_forward_at(hub.get(), 1)->ep_forward_.c_str());
    EXPECT_FALSE(utest_forward_started(utest_forward_at(hub.get(), 1)));

    hub->destroy_forwarders();
    EXPECT_EQ(0, (int)hub->forwarders_.size());
}

// A backend with urls creates one forwarder per url and ignores the destinations; the backend is
// asked once, with its url and the publisher's request.
VOID TEST(OriginHubForwardTest, BackendIgnoresDestination)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    config->queue_length_ = 9 * SRS_UTIME_SECONDS;
    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    destinations.push_back("127.0.0.1:1937");
    destinations.push_back("127.0.0.1:1938");
    config->set_forward_destinations(destinations);
    config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    hooks->urls_.push_back("rtmp://192.168.1.10:1935/live/stream1");
    hooks->urls_.push_back("rtmp://192.168.1.11:1935/live/stream2");

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());

    EXPECT_EQ(1, hooks->count_);
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/forward", hooks->url_.c_str());
    EXPECT_TRUE(hooks->req_ == req.get());

    ASSERT_EQ(2, (int)hub->forwarders_.size());
    EXPECT_STREQ("192.168.1.10:1935", utest_forward_at(hub.get(), 0)->ep_forward_.c_str());
    EXPECT_STREQ("192.168.1.11:1935", utest_forward_at(hub.get(), 1)->ep_forward_.c_str());
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(9 * SRS_UTIME_SECONDS, utest_forward_at(hub.get(), i)->queue_->max_queue_size_) << "#" << i;
        EXPECT_TRUE(utest_forward_started(utest_forward_at(hub.get(), i))) << "#" << i;
    }

    hub->destroy_forwarders();
}

// A backend that returns no urls means no forward at all: the destinations are still ignored.
VOID TEST(OriginHubForwardTest, BackendWithNoUrlsIgnoresDestination)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    config->set_forward_destinations(destinations);
    config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());

    EXPECT_EQ(1, hooks->count_);
    EXPECT_EQ(0, (int)hub->forwarders_.size());
}

// A backend error fails create_forwarders() with the backend's error, and creates nothing.
VOID TEST(OriginHubForwardTest, BackendErrorFailsCreate)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    config->set_forward_destinations(destinations);
    config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    hooks->urls_.push_back("rtmp://192.168.1.10:1935/live/stream1");
    hooks->error_ = srs_error_new(ERROR_SOCKET_TIMEOUT, "mock backend");

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    err = hub->create_forwarders();
    EXPECT_EQ(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(1, hooks->count_);
    EXPECT_EQ(0, (int)hub->forwarders_.size());
}

// A backend directive with no arg, or an empty arg, is not applied: the destinations are used and
// the backend is never asked.
VOID TEST(OriginHubForwardTest, EmptyBackendArgUsesDestination)
{
    srs_error_t err;

    for (int nb_args = 0; nb_args < 2; nb_args++) {
        SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
        SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
        SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
        SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

        std::vector<std::string> destinations;
        destinations.push_back("127.0.0.1:1936");
        destinations.push_back("127.0.0.1:1937");
        config->set_forward_destinations(destinations);
        config->backend_directive_ = new SrsConfDirective();
        config->backend_directive_->name_ = "backend";
        if (nb_args) {
            config->backend_directive_->args_.push_back("");
        }
        hooks->urls_.push_back("rtmp://192.168.1.10:1935/live/stream1");

        SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
        HELPER_EXPECT_SUCCESS(hub->create_forwarders());

        EXPECT_EQ(0, hooks->count_) << "args=" << nb_args;
        ASSERT_EQ(2, (int)hub->forwarders_.size()) << "args=" << nb_args;
        EXPECT_STREQ("127.0.0.1:1936", utest_forward_at(hub.get(), 0)->ep_forward_.c_str());
        EXPECT_STREQ("127.0.0.1:1937", utest_forward_at(hub.get(), 1)->ep_forward_.c_str());

        hub->destroy_forwarders();
    }
}

// Each backend url decides its forwarder's endpoint and request, not the publisher's request:
// the host and port (1935 when absent), the app and stream, the query, and a vhost in the query.
// The url the forwarder then connects to is built from them; with no vhost in the backend url,
// the vhost is the url's host, so the forwarder adds it as a ?vhost= query.
VOID TEST(OriginHubForwardTest, BackendUrlDecidesEndpointAndRequest)
{
    srs_error_t err;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "livestream"));
    req->param_ = "?pub=1";
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());

    config->set_forward_backend("http://127.0.0.1:8085/api/v1/forward");
    hooks->urls_.push_back("rtmp://192.168.1.10:19350/live/stream1");
    hooks->urls_.push_back("rtmp://192.168.1.11/live/stream2");
    hooks->urls_.push_back("rtmp://192.168.1.12:1935/live/stream3?token=abc");
    hooks->urls_.push_back("rtmp://192.168.1.13:1935/live/stream4?vhost=v.example.com");
    hooks->urls_.push_back("rtmp://192.168.1.14:1935/other/app5");

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));
    HELPER_EXPECT_SUCCESS(hub->create_forwarders());
    ASSERT_EQ(5, (int)hub->forwarders_.size());

    const char *eps[] = {"192.168.1.10:19350", "192.168.1.11:1935", "192.168.1.12:1935", "192.168.1.13:1935", "192.168.1.14:1935"};
    const char *hosts[] = {"192.168.1.10", "192.168.1.11", "192.168.1.12", "192.168.1.13", "192.168.1.14"};
    const char *vhosts[] = {"192.168.1.10", "192.168.1.11", "192.168.1.12", "v.example.com", "192.168.1.14"};
    const char *apps[] = {"live", "live", "live", "live", "other"};
    const char *streams[] = {"stream1", "stream2", "stream3", "stream4", "app5"};
    const char *params[] = {"", "", "?token=abc", "?vhost=v.example.com", ""};
    const char *urls[] = {
        "rtmp://192.168.1.10:19350/live/stream1?vhost=192.168.1.10",
        "rtmp://192.168.1.11:1935/live/stream2?vhost=192.168.1.11",
        "rtmp://192.168.1.12:1935/live/stream3?token=abc&vhost=192.168.1.12",
        "rtmp://192.168.1.13:1935/live/stream4?vhost=v.example.com",
        "rtmp://192.168.1.14:1935/other/app5?vhost=192.168.1.14",
    };

    for (int i = 0; i < 5; i++) {
        SrsForwarder *forwarder = utest_forward_at(hub.get(), i);
        EXPECT_STREQ(eps[i], forwarder->ep_forward_.c_str()) << "#" << i;
        EXPECT_STREQ(hosts[i], forwarder->req_->host_.c_str()) << "#" << i;
        EXPECT_STREQ(vhosts[i], forwarder->req_->vhost_.c_str()) << "#" << i;
        EXPECT_STREQ(apps[i], forwarder->req_->app_.c_str()) << "#" << i;
        EXPECT_STREQ(streams[i], forwarder->req_->stream_.c_str()) << "#" << i;
        EXPECT_STREQ(params[i], forwarder->req_->param_.c_str()) << "#" << i;

        // Stop the started coroutine, then connect once with a client that fails to connect,
        // to get the url.
        forwarder->on_unpublish();
        MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
        sdk->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "mock connect");
        factory->next_ = sdk;
        utest_forward_wire(forwarder, factory.get(), config.get(), 1);

        err = forwarder->do_cycle();
        EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err)) << "#" << i;
        srs_freep(err);

        ASSERT_EQ(i + 1, (int)factory->urls_.size());
        EXPECT_STREQ(urls[i], factory->urls_[i].c_str()) << "#" << i;
    }

    hub->destroy_forwarders();
}

// Metadata, audio and video reach every forwarder, in forwarder order, as the same shared packet.
VOID TEST(OriginHubForwardTest, FanOutReachesEveryForwarderInOrder)
{
    srs_error_t err;

    std::vector<std::string> log;
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));
    SrsUniquePtr<SrsMediaPacket> metadata(utest_forward_metadata(0, 'M'));
    SrsUniquePtr<SrsMediaPacket> audio(utest_forward_audio_raw(10, 'A'));
    SrsUniquePtr<SrsMediaPacket> video(utest_forward_video_keyframe(20, 'K'));
    SrsUniquePtr<SrsOnMetaDataPacket> packet(new SrsOnMetaDataPacket());

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), _srs_config, _srs_hooks));
    MockForwarderForFanOut *f1 = new MockForwarderForFanOut("f1", &log);
    MockForwarderForFanOut *f2 = new MockForwarderForFanOut("f2", &log);
    hub->forwarders_.push_back(f1);
    hub->forwarders_.push_back(f2);

    HELPER_EXPECT_SUCCESS(hub->on_meta_data(metadata.get(), packet.get()));
    HELPER_EXPECT_SUCCESS(hub->on_audio(audio.get()));
    HELPER_EXPECT_SUCCESS(hub->on_video(video.get(), false));

    const char *expected[] = {"f1:M", "f2:M", "f1:A", "f2:A", "f1:K", "f2:K"};
    ASSERT_EQ(6, (int)log.size());
    for (int i = 0; i < 6; i++) {
        EXPECT_STREQ(expected[i], log[i].c_str()) << "#" << i;
    }

    ASSERT_EQ(3, (int)f1->packets_.size());
    ASSERT_EQ(3, (int)f2->packets_.size());
    EXPECT_TRUE(f1->packets_[0] == metadata.get() && f2->packets_[0] == metadata.get());
    EXPECT_TRUE(f1->packets_[1] == audio.get() && f2->packets_[1] == audio.get());
    EXPECT_TRUE(f1->packets_[2] == video.get() && f2->packets_[2] == video.get());
}

// The first forwarder error returns from the fan-out: the forwarders before it and the failing
// one got the packet, the ones after it did not. For metadata, the DVR after the fan-out does not
// get it either; for audio, the DVR before the fan-out already did.
VOID TEST(OriginHubForwardTest, FanOutStopsAtFirstForwarderError)
{
    srs_error_t err;

    std::vector<std::string> log;
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));
    SrsUniquePtr<SrsMediaPacket> metadata(utest_forward_metadata(0, 'M'));
    SrsUniquePtr<SrsMediaPacket> audio(utest_forward_audio_raw(10, 'A'));
    SrsUniquePtr<SrsMediaPacket> video(utest_forward_video_keyframe(20, 'K'));
    SrsUniquePtr<SrsOnMetaDataPacket> packet(new SrsOnMetaDataPacket());

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), _srs_config, _srs_hooks));
    MockDvrForOriginHub *dvr = dynamic_cast<MockDvrForOriginHub *>(hub->dvr_);
    MockForwarderForFanOut *f1 = new MockForwarderForFanOut("f1", &log);
    MockForwarderForFanOut *f2 = new MockForwarderForFanOut("f2", &log);
    MockForwarderForFanOut *f3 = new MockForwarderForFanOut("f3", &log);
    f2->meta_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock meta");
    f2->audio_error_ = srs_error_new(ERROR_SOCKET_READ, "mock audio");
    f2->video_error_ = srs_error_new(ERROR_SOCKET_TIMEOUT, "mock video");
    hub->forwarders_.push_back(f1);
    hub->forwarders_.push_back(f2);
    hub->forwarders_.push_back(f3);

    err = hub->on_meta_data(metadata.get(), packet.get());
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, dvr->on_meta_data_count_);

    err = hub->on_audio(audio.get());
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, dvr->on_audio_count_);

    err = hub->on_video(video.get(), false);
    EXPECT_EQ(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, dvr->on_video_count_);

    const char *expected[] = {"f1:M", "f2:M", "f1:A", "f2:A", "f1:K", "f2:K"};
    ASSERT_EQ(6, (int)log.size());
    for (int i = 0; i < 6; i++) {
        EXPECT_STREQ(expected[i], log[i].c_str()) << "#" << i;
    }
    EXPECT_EQ(0, (int)f3->packets_.size());
}

// on_forwarder_start() enqueues the source's cached metadata, video and audio sequence headers
// into that forwarder, in that order; with nothing cached it enqueues nothing.
VOID TEST(OriginHubForwardTest, OnForwarderStartFeedsCachedMetadataAndHeaders)
{
    srs_error_t err;

    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    if (true) {
        SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
        source->meta()->meta_ = utest_forward_metadata(0, 'M');
        source->meta()->video_ = utest_forward_video_sh(0, 'W');
        source->meta()->audio_ = utest_forward_audio_sh(0, 'B');

        SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), _srs_config, _srs_hooks));
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

        HELPER_EXPECT_SUCCESS(hub->on_forwarder_start(forwarder.get()));
        EXPECT_EQ(3, forwarder->queue_->size());
        EXPECT_EQ('W', utest_forward_marker(forwarder->sh_video_));
        EXPECT_EQ('B', utest_forward_marker(forwarder->sh_audio_));

        // The forwarder's cached headers first, then the queue.
        MockRtmpClientForForwarder *sdk = utest_forward_run_once(forwarder.get());
        const char markers[] = {'W', 'B', 'M', 'W', 'B'};
        ASSERT_EQ(5, (int)sdk->sent_.size());
        for (int i = 0; i < 5; i++) {
            EXPECT_EQ(markers[i], utest_forward_marker(sdk->sent_[i])) << "#" << i;
        }
        ASSERT_EQ(1, (int)sdk->batches_.size());
        EXPECT_EQ(3, sdk->batches_[0]);
    }

    if (true) {
        SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
        SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), _srs_config, _srs_hooks));
        SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get()));

        HELPER_EXPECT_SUCCESS(hub->on_forwarder_start(forwarder.get()));
        EXPECT_EQ(0, forwarder->queue_->size());
        EXPECT_TRUE(forwarder->sh_video_ == NULL);
        EXPECT_TRUE(forwarder->sh_audio_ == NULL);
    }
}

// Unpublish calls on_unpublish() on each forwarder, then frees it, one by one; a republish
// creates the forwarders again from the config.
VOID TEST(OriginHubForwardTest, UnpublishFreesForwardersAndRepublishCreatesThem)
{
    srs_error_t err;

    std::vector<std::string> log;
    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:1936");
    destinations.push_back("127.0.0.1:1937");
    config->set_forward_destinations(destinations);

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));

    if (true) {
        hub->forwarders_.push_back(new MockForwarderForFanOut("f1", &log));
        hub->forwarders_.push_back(new MockForwarderForFanOut("f2", &log));

        hub->on_unpublish();
        EXPECT_FALSE(hub->active());
        EXPECT_EQ(0, (int)hub->forwarders_.size());

        const char *expected[] = {"f1:unpublish", "f1:freed", "f2:unpublish", "f2:freed"};
        ASSERT_EQ(4, (int)log.size());
        for (int i = 0; i < 4; i++) {
            EXPECT_STREQ(expected[i], log[i].c_str()) << "#" << i;
        }
    }

    // Publish, unpublish and publish again: each publish creates and starts both forwarders.
    for (int round = 0; round < 2; round++) {
        HELPER_EXPECT_SUCCESS(hub->on_publish());
        EXPECT_TRUE(hub->active());
        ASSERT_EQ(2, (int)hub->forwarders_.size()) << "round " << round;
        for (int i = 0; i < 2; i++) {
            EXPECT_STREQ(destinations[i].c_str(), utest_forward_at(hub.get(), i)->ep_forward_.c_str()) << "round " << round;
            EXPECT_TRUE(utest_forward_started(utest_forward_at(hub.get(), i))) << "round " << round;
        }

        hub->on_unpublish();
        EXPECT_FALSE(hub->active());
        EXPECT_EQ(0, (int)hub->forwarders_.size()) << "round " << round;
    }
}

MockForwardBackendExchange::MockForwardBackendExchange()
{
    create_count_ = 0;
    post_count_ = 0;
    port_ = 0;
    status_code_ = 200;
    response_ = "{\"code\":0,\"data\":{\"urls\":[]}}";
    post_error_ = srs_success;
}

MockForwardBackendExchange::~MockForwardBackendExchange()
{
    srs_freep(post_error_);
}

MockHttpClientForForwardBackend::MockHttpClientForForwardBackend(MockForwardBackendExchange *exchange)
{
    exchange_ = exchange;
}

MockHttpClientForForwardBackend::~MockHttpClientForForwardBackend()
{
}

srs_error_t MockHttpClientForForwardBackend::initialize(std::string schema, std::string h, int p, srs_utime_t tm)
{
    exchange_->schema_ = schema;
    exchange_->host_ = h;
    exchange_->port_ = p;
    return srs_success;
}

srs_error_t MockHttpClientForForwardBackend::get(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    return srs_error_new(ERROR_NOT_SUPPORTED, "get");
}

srs_error_t MockHttpClientForForwardBackend::post(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    exchange_->post_count_++;
    exchange_->path_ = path;
    exchange_->body_ = req;

    if (exchange_->post_error_ != srs_success) {
        return srs_error_copy(exchange_->post_error_);
    }

    MockHttpMessageForHooks *msg = new MockHttpMessageForHooks();
    msg->status_code_ = exchange_->status_code_;
    msg->body_content_ = exchange_->response_;
    *ppmsg = msg;
    return srs_success;
}

void MockHttpClientForForwardBackend::set_recv_timeout(srs_utime_t tm)
{
}

void MockHttpClientForForwardBackend::kbps_sample(const char *label, srs_utime_t age)
{
}

MockAppFactoryForForwardBackend::MockAppFactoryForForwardBackend()
{
}

MockAppFactoryForForwardBackend::~MockAppFactoryForForwardBackend()
{
}

ISrsHttpClient *MockAppFactoryForForwardBackend::create_http_client()
{
    exchange_.create_count_++;
    return new MockHttpClientForForwardBackend(&exchange_);
}

// Hooks that use the mock factory and statistic, and leave the globals alone when freed.
class UtestForwardBackendHooks
{
public:
    MockAppFactoryForForwardBackend factory_;
    MockStatisticForHooks stat_;
    SrsHttpHooks hooks_;

public:
    UtestForwardBackendHooks()
    {
        hooks_.factory_ = &factory_;
        hooks_.stat_ = &stat_;
        hooks_.config_ = NULL;
    }
    ~UtestForwardBackendHooks()
    {
        hooks_.factory_ = NULL;
        hooks_.stat_ = NULL;
    }

public:
    // Call on_forward_backend() for a publisher of live/livestream, with the backend answering
    // the status and body.
    srs_error_t call(int status, std::string response, std::vector<std::string> &urls)
    {
        factory_.exchange_.status_code_ = status;
        factory_.exchange_.response_ = response;

        MockRequest req("__defaultVhost__", "live", "livestream");
        return hooks_.on_forward_backend("http://127.0.0.1:8085/api/v1/forward", &req, urls);
    }
};

// The POST to the backend url carries exactly action=on_forward, server_id, service_id, client_id,
// ip, vhost, app, tcUrl, stream and param, from the statistic, the context id and the request.
VOID TEST(HttpHooksForwardBackendTest, PostsPublisherRequestFields)
{
    srs_error_t err;

    UtestForwardBackendHooks h;
    h.stat_.server_id_ = "vid-forward";
    h.stat_.service_id_ = "sid-forward";

    MockRequest req("test.vhost", "live", "stream1");
    req.ip_ = "192.168.1.100";
    req.tcUrl_ = "rtmp://test.vhost/live";
    req.param_ = "?token=abc123";

    SrsContextId before = _srs_context->get_id();
    SrsContextId cid;
    _srs_context->set_id(cid.set_value("forward-backend-cid"));

    std::vector<std::string> urls;
    err = h.hooks_.on_forward_backend("http://127.0.0.1:8085/api/v1/forward?app=x", &req, urls);
    _srs_context->set_id(before);
    HELPER_EXPECT_SUCCESS(err);

    MockForwardBackendExchange &ex = h.factory_.exchange_;
    EXPECT_EQ(1, ex.create_count_);
    EXPECT_EQ(1, ex.post_count_);
    EXPECT_STREQ("http", ex.schema_.c_str());
    EXPECT_STREQ("127.0.0.1", ex.host_.c_str());
    EXPECT_EQ(8085, ex.port_);
    EXPECT_STREQ("/api/v1/forward?app=x", ex.path_.c_str());

    SrsUniquePtr<SrsJsonAny> body(SrsJsonAny::loads(ex.body_));
    ASSERT_TRUE(body.get() != NULL) << ex.body_;
    ASSERT_TRUE(body->is_object()) << ex.body_;
    SrsJsonObject *obj = body->to_object();

    const char *fields[][2] = {
        {"action", "on_forward"},
        {"server_id", "vid-forward"},
        {"service_id", "sid-forward"},
        {"client_id", "forward-backend-cid"},
        {"ip", "192.168.1.100"},
        {"vhost", "test.vhost"},
        {"app", "live"},
        {"tcUrl", "rtmp://test.vhost/live"},
        {"stream", "stream1"},
        {"param", "?token=abc123"},
    };
    int nn_fields = (int)(sizeof(fields) / sizeof(fields[0]));
    ASSERT_EQ(nn_fields, obj->count()) << ex.body_;
    for (int i = 0; i < nn_fields; i++) {
        EXPECT_STREQ(fields[i][0], obj->key_at(i).c_str()) << "#" << i;
        SrsJsonAny *prop = obj->get_property(fields[i][0]);
        ASSERT_TRUE(prop != NULL && prop->is_string()) << fields[i][0];
        EXPECT_STREQ(fields[i][1], prop->to_str().c_str()) << fields[i][0];
    }
}

// A post that fails, or an HTTP status other than 200 or 201, fails the hook with no urls.
VOID TEST(HttpHooksForwardBackendTest, PostErrorAndHttpStatusFail)
{
    srs_error_t err;

    if (true) {
        UtestForwardBackendHooks h;
        h.factory_.exchange_.post_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "refused");
        std::vector<std::string> urls;
        HELPER_EXPECT_FAILED(h.call(200, "", urls));
        EXPECT_EQ(1, h.factory_.exchange_.post_count_);
        EXPECT_EQ(0, (int)urls.size());
    }

    int statuses[] = {500, 404, 302, 204};
    for (int i = 0; i < (int)(sizeof(statuses) / sizeof(statuses[0])); i++) {
        UtestForwardBackendHooks h;
        std::vector<std::string> urls;
        err = h.call(statuses[i], "{\"code\":0,\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\"]}}", urls);
        ASSERT_TRUE(err != srs_success) << statuses[i];
        EXPECT_EQ(ERROR_HTTP_STATUS_INVALID, srs_error_code(err)) << statuses[i];
        srs_freep(err);
        EXPECT_EQ(0, (int)urls.size()) << statuses[i];
    }

    // 201 Created is accepted like 200.
    if (true) {
        UtestForwardBackendHooks h;
        std::vector<std::string> urls;
        HELPER_EXPECT_SUCCESS(h.call(201, "{\"code\":0,\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\"]}}", urls));
        ASSERT_EQ(1, (int)urls.size());
        EXPECT_STREQ("rtmp://127.0.0.1/live/a", urls[0].c_str());
    }
}

// The response must be a JSON object with code 0. An empty body, a body that is not JSON, a JSON
// value that is not an object, and an object without code 0 each fail the hook. The bare "0" that
// other hooks accept passes the HTTP check but still fails here, because it has no urls.
VOID TEST(HttpHooksForwardBackendTest, ResponseNotJsonObjectFails)
{
    srs_error_t err;

    struct {
        const char *response;
        int code;
    } cases[] = {
        {"", ERROR_HTTP_DATA_INVALID},
        {"not json", ERROR_HTTP_DATA_INVALID},
        {"[\"rtmp://127.0.0.1/live/a\"]", ERROR_HTTP_DATA_INVALID},
        {"\"rtmp://127.0.0.1/live/a\"", ERROR_HTTP_DATA_INVALID},
        {"1", ERROR_HTTP_DATA_INVALID},
        {"0", ERROR_SYSTEM_FORWARD_LOOP},
        {"{\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\"]}}", ERROR_RESPONSE_CODE},
        {"{\"code\":\"0\",\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\"]}}", ERROR_RESPONSE_CODE},
        {"{\"code\":100,\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\"]}}", ERROR_RESPONSE_CODE},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        UtestForwardBackendHooks h;
        std::vector<std::string> urls;
        err = h.call(200, cases[i].response, urls);
        ASSERT_TRUE(err != srs_success) << cases[i].response;
        EXPECT_EQ(cases[i].code, srs_error_code(err)) << cases[i].response;
        srs_freep(err);
        EXPECT_EQ(0, (int)urls.size()) << cases[i].response;
    }
}

// A code 0 object without a data object, or whose data has no urls array, fails the hook.
VOID TEST(HttpHooksForwardBackendTest, MissingDataOrUrlsFails)
{
    srs_error_t err;

    const char *cases[] = {
        "{\"code\":0}",
        "{\"code\":0,\"urls\":[\"rtmp://127.0.0.1/live/a\"]}",
        "{\"code\":0,\"data\":\"rtmp://127.0.0.1/live/a\"}",
        "{\"code\":0,\"data\":[\"rtmp://127.0.0.1/live/a\"]}",
        "{\"code\":0,\"data\":null}",
        "{\"code\":0,\"data\":{}}",
        "{\"code\":0,\"data\":{\"url\":[\"rtmp://127.0.0.1/live/a\"]}}",
        "{\"code\":0,\"data\":{\"urls\":\"rtmp://127.0.0.1/live/a\"}}",
        "{\"code\":0,\"data\":{\"urls\":{\"0\":\"rtmp://127.0.0.1/live/a\"}}}",
        "{\"code\":0,\"data\":{\"urls\":null}}",
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        UtestForwardBackendHooks h;
        std::vector<std::string> urls;
        err = h.call(200, cases[i], urls);
        ASSERT_TRUE(err != srs_success) << cases[i];
        EXPECT_EQ(ERROR_SYSTEM_FORWARD_LOOP, srs_error_code(err)) << cases[i];
        srs_freep(err);
        EXPECT_EQ(0, (int)urls.size()) << cases[i];
    }
}

// An empty urls array succeeds with no urls, which means no forward at all.
VOID TEST(HttpHooksForwardBackendTest, EmptyUrlsSucceedsWithNoUrls)
{
    srs_error_t err;

    UtestForwardBackendHooks h;
    std::vector<std::string> urls;
    HELPER_EXPECT_SUCCESS(h.call(200, "{\"code\":0,\"data\":{\"urls\":[]}}", urls));
    EXPECT_EQ(0, (int)urls.size());
    EXPECT_EQ(1, h.factory_.exchange_.post_count_);
}

// Every non-empty string is kept, in the backend's order and as is, without checking it is an RTMP
// url; empty strings are skipped; the urls are appended to what the caller's vector holds.
VOID TEST(HttpHooksForwardBackendTest, KeepsUrlsInOrderAndSkipsEmptyStrings)
{
    srs_error_t err;

    UtestForwardBackendHooks h;
    std::vector<std::string> urls;
    urls.push_back("rtmp://kept.before/live/s");

    HELPER_EXPECT_SUCCESS(h.call(200,
                                 "{\"code\":0,\"data\":{\"urls\":["
                                 "\"\","
                                 "\"rtmp://10.0.0.2:19350/app2/stream2?token=t2\","
                                 "\"\","
                                 "\"rtmp://10.0.0.1/live/livestream\","
                                 "\"srt://10.0.0.3:10080\","
                                 "\"rtmp://10.0.0.2:19350/app2/stream2?token=t2\","
                                 "\"\"]}}",
                                 urls));

    const char *expected[] = {
        "rtmp://kept.before/live/s",
        "rtmp://10.0.0.2:19350/app2/stream2?token=t2",
        "rtmp://10.0.0.1/live/livestream",
        "srt://10.0.0.3:10080",
        "rtmp://10.0.0.2:19350/app2/stream2?token=t2",
    };
    int nn_expected = (int)(sizeof(expected) / sizeof(expected[0]));
    ASSERT_EQ(nn_expected, (int)urls.size());
    for (int i = 0; i < nn_expected; i++) {
        EXPECT_STREQ(expected[i], urls[i].c_str()) << "#" << i;
    }
}

// A non-string entry in urls aborts the server: on_forward_backend() calls to_str() on each entry,
// and SrsJsonAny::to_str() asserts the value is a string. Disabled because the assert kills the
// test binary; run it with --gtest_also_run_disabled_tests to see the abort. When fixed, the hook
// should fail or skip the entry, and this test should pin that instead.
VOID TEST(HttpHooksForwardBackendTest, DISABLED_NonStringUrlAbortsServer)
{
    srs_error_t err;

    UtestForwardBackendHooks h;
    std::vector<std::string> urls;
    err = h.call(200, "{\"code\":0,\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\",123]}}", urls);
    HELPER_EXPECT_FAILED(err);
}

// The forward config getters, read from a config file, overwritten by env, and their defaults.
// SRS_VHOST_FORWARD_DESTINATION overwrites get_forwards() too.

// With no vhost, or a vhost with no forward section, forwarding is off and there is no
// destination or backend.
VOID TEST(ConfigForwardTest, DefaultsWithoutForwardSection)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_forward_enabled("__defaultVhost__"));
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
        EXPECT_TRUE(conf.get_forwards("__defaultVhost__") == NULL);
        EXPECT_TRUE(conf.get_forwards("ossrs.net") == NULL);
        EXPECT_TRUE(conf.get_forward_backend("__defaultVhost__") == NULL);
        EXPECT_TRUE(conf.get_forward_backend("ossrs.net") == NULL);
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{hls{enabled on;}}"));
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
        EXPECT_TRUE(conf.get_forwards("ossrs.net") == NULL);
        EXPECT_TRUE(conf.get_forward_backend("ossrs.net") == NULL);

        SrsConfDirective *vhost = conf.get_vhost("ossrs.net");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_FALSE(conf.get_forward_enabled(vhost));
    }

    if (true) {
        // A forward section without enabled, destination or backend.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{}}"));
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
        EXPECT_TRUE(conf.get_forwards("ossrs.net") == NULL);
        EXPECT_TRUE(conf.get_forward_backend("ossrs.net") == NULL);
    }
}

// enabled is true only for "on"; any other value is false.
VOID TEST(ConfigForwardTest, EnabledFromConfigFile)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on;}}"));
        EXPECT_TRUE(conf.get_forward_enabled("ossrs.net"));
        SrsConfDirective *vhost = conf.get_vhost("ossrs.net");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(conf.get_forward_enabled(vhost));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled off;}}"));
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled yes;}}"));
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled;}}"));
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
    }
}

// An unknown vhost falls back to __defaultVhost__ for every forward getter.
VOID TEST(ConfigForwardTest, UnknownVhostUsesDefaultVhost)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{forward{enabled on; destination 127.0.0.1:1936; backend http://127.0.0.1:8085/api/v1/forward;}}"
                                                       "vhost ossrs.net{hls{enabled on;}}"));

    EXPECT_TRUE(conf.get_forward_enabled("unknown.vhost"));
    SrsConfDirective *dests = conf.get_forwards("unknown.vhost");
    ASSERT_TRUE(dests != NULL);
    EXPECT_STREQ("127.0.0.1:1936", dests->arg0().c_str());
    SrsConfDirective *backend = conf.get_forward_backend("unknown.vhost");
    ASSERT_TRUE(backend != NULL);
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/forward", backend->arg0().c_str());

    // A configured vhost does not inherit the default vhost's forward section.
    EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
    EXPECT_TRUE(conf.get_forwards("ossrs.net") == NULL);
    EXPECT_TRUE(conf.get_forward_backend("ossrs.net") == NULL);
}

// destination and backend keep every arg, in order, as written.
VOID TEST(ConfigForwardTest, DestinationAndBackendFromConfigFile)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; destination 127.0.0.1:1936 10.0.0.1:19350 example.com;"
                                                       " backend http://127.0.0.1:8085/api/v1/forward?token=abc;}}"));

    SrsConfDirective *dests = conf.get_forwards("ossrs.net");
    ASSERT_TRUE(dests != NULL);
    EXPECT_STREQ("destination", dests->name_.c_str());
    ASSERT_EQ(3, (int)dests->args_.size());
    EXPECT_STREQ("127.0.0.1:1936", dests->args_.at(0).c_str());
    EXPECT_STREQ("10.0.0.1:19350", dests->args_.at(1).c_str());
    EXPECT_STREQ("example.com", dests->args_.at(2).c_str());

    SrsConfDirective *backend = conf.get_forward_backend("ossrs.net");
    ASSERT_TRUE(backend != NULL);
    EXPECT_STREQ("backend", backend->name_.c_str());
    ASSERT_EQ(1, (int)backend->args_.size());
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/forward?token=abc", backend->arg0().c_str());

    // Destination without backend, and backend without destination.
    if (true) {
        MockSrsConfig c2;
        HELPER_ASSERT_SUCCESS(c2.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; destination 127.0.0.1:1936;}}"));
        EXPECT_TRUE(c2.get_forwards("ossrs.net") != NULL);
        EXPECT_TRUE(c2.get_forward_backend("ossrs.net") == NULL);
    }
    if (true) {
        MockSrsConfig c2;
        HELPER_ASSERT_SUCCESS(c2.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; backend http://127.0.0.1:8085/api/v1/forward;}}"));
        EXPECT_TRUE(c2.get_forwards("ossrs.net") == NULL);
        EXPECT_TRUE(c2.get_forward_backend("ossrs.net") != NULL);
    }
}

// SRS_VHOST_FORWARD_ENABLED overwrites the config file for every vhost, both ways, and also
// applies with no vhost at all. An empty value does not overwrite.
VOID TEST(ConfigForwardTest, EnabledFromEnv)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled off;}}"));
        SrsSetEnvConfig(conf, forward_enabled, "SRS_VHOST_FORWARD_ENABLED", "on");
        EXPECT_TRUE(conf.get_forward_enabled("ossrs.net"));
        EXPECT_TRUE(conf.get_forward_enabled("no.such.vhost"));
        SrsConfDirective *vhost = conf.get_vhost("ossrs.net");
        ASSERT_TRUE(vhost != NULL);
        EXPECT_TRUE(conf.get_forward_enabled(vhost));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on;}}"));
        SrsSetEnvConfig(conf, forward_enabled, "SRS_VHOST_FORWARD_ENABLED", "off");
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
    }

    if (true) {
        // Any value other than "on" is false, like the config file.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on;}}"));
        SrsSetEnvConfig(conf, forward_enabled, "SRS_VHOST_FORWARD_ENABLED", "true");
        EXPECT_FALSE(conf.get_forward_enabled("ossrs.net"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on;}}"));
        SrsSetEnvConfig(conf, forward_enabled, "SRS_VHOST_FORWARD_ENABLED", "");
        EXPECT_TRUE(conf.get_forward_enabled("ossrs.net"));
    }
}

// SRS_VHOST_FORWARD_BACKEND overwrites the config file, split on spaces with empty parts dropped.
// A value of only spaces gives a directive with no args, not NULL; the origin hub then treats the
// backend as not applied and uses the destinations.
VOID TEST(ConfigForwardTest, BackendFromEnv)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; backend http://192.168.1.1:8085/api/v1/forward;}}"));
        SrsSetEnvConfig(conf, forward_backend, "SRS_VHOST_FORWARD_BACKEND", "http://10.0.0.1:8085/api/v1/forward?token=abc");

        SrsConfDirective *backend = conf.get_forward_backend("ossrs.net");
        ASSERT_TRUE(backend != NULL);
        EXPECT_STREQ("srs.vhost.forward.backend", backend->name_.c_str());
        ASSERT_EQ(1, (int)backend->args_.size());
        EXPECT_STREQ("http://10.0.0.1:8085/api/v1/forward?token=abc", backend->arg0().c_str());

        // The same env directive for any vhost, configured or not.
        EXPECT_TRUE(conf.get_forward_backend("no.such.vhost") == backend);
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        SrsSetEnvConfig(conf, forward_backend, "SRS_VHOST_FORWARD_BACKEND", "  http://a/x   http://b/y ");
        SrsConfDirective *backend = conf.get_forward_backend("__defaultVhost__");
        ASSERT_TRUE(backend != NULL);
        ASSERT_EQ(2, (int)backend->args_.size());
        EXPECT_STREQ("http://a/x", backend->args_.at(0).c_str());
        EXPECT_STREQ("http://b/y", backend->args_.at(1).c_str());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; backend http://192.168.1.1:8085/api/v1/forward;}}"));
        SrsSetEnvConfig(conf, forward_backend, "SRS_VHOST_FORWARD_BACKEND", "   ");
        SrsConfDirective *backend = conf.get_forward_backend("ossrs.net");
        ASSERT_TRUE(backend != NULL);
        EXPECT_EQ(0, (int)backend->args_.size());
        EXPECT_TRUE(backend->arg0().empty());
    }

    if (true) {
        // An empty value does not overwrite.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; backend http://192.168.1.1:8085/api/v1/forward;}}"));
        SrsSetEnvConfig(conf, forward_backend, "SRS_VHOST_FORWARD_BACKEND", "");
        SrsConfDirective *backend = conf.get_forward_backend("ossrs.net");
        ASSERT_TRUE(backend != NULL);
        EXPECT_STREQ("http://192.168.1.1:8085/api/v1/forward", backend->arg0().c_str());
    }
}

// SRS_VHOST_FORWARD_DESTINATION overwrites the config file with one env directive for every vhost,
// one arg per destination, split on spaces.
VOID TEST(ConfigForwardTest, DestinationFromEnv)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        SrsSetEnvConfig(conf, forward_destination, "SRS_VHOST_FORWARD_DESTINATION", "127.0.0.1:1936 127.0.0.1:1937");
        SrsConfDirective *dests = conf.get_forwards("__defaultVhost__");
        ASSERT_TRUE(dests != NULL);
        EXPECT_STREQ("srs.vhost.forward.destination", dests->name_.c_str());
        ASSERT_EQ(2, (int)dests->args_.size());
        EXPECT_STREQ("127.0.0.1:1936", dests->args_.at(0).c_str());
        EXPECT_STREQ("127.0.0.1:1937", dests->args_.at(1).c_str());
    }

    if (true) {
        // The env wins over the config file, for a configured vhost and an unknown one.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; destination 127.0.0.1:1936;}}"));
        SrsSetEnvConfig(conf, forward_destination, "SRS_VHOST_FORWARD_DESTINATION", "10.0.0.1:1936 10.0.0.2:1936");
        SrsConfDirective *dests = conf.get_forwards("ossrs.net");
        ASSERT_TRUE(dests != NULL);
        ASSERT_EQ(2, (int)dests->args_.size());
        EXPECT_STREQ("10.0.0.1:1936", dests->args_.at(0).c_str());
        EXPECT_STREQ("10.0.0.2:1936", dests->args_.at(1).c_str());
        EXPECT_TRUE(conf.get_forwards("no.such.vhost") == dests);
    }

    if (true) {
        // An empty value does not overwrite.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; destination 127.0.0.1:1936;}}"));
        SrsSetEnvConfig(conf, forward_destination, "SRS_VHOST_FORWARD_DESTINATION", "");
        SrsConfDirective *dests = conf.get_forwards("ossrs.net");
        ASSERT_TRUE(dests != NULL);
        ASSERT_EQ(1, (int)dests->args_.size());
        EXPECT_STREQ("127.0.0.1:1936", dests->arg0().c_str());
    }

    if (true) {
        // Unset, the config file value is used.
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost ossrs.net{forward{enabled on; destination 127.0.0.1:1936 127.0.0.1:1937;}}"));
        SrsConfDirective *dests = conf.get_forwards("ossrs.net");
        ASSERT_TRUE(dests != NULL);
        EXPECT_STREQ("destination", dests->name_.c_str());
        ASSERT_EQ(2, (int)dests->args_.size());
        EXPECT_STREQ("127.0.0.1:1936", dests->args_.at(0).c_str());
        EXPECT_STREQ("127.0.0.1:1937", dests->args_.at(1).c_str());
        EXPECT_TRUE(conf.get_forwards("__defaultVhost__") == NULL);
    }
}
