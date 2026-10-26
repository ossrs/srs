//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai43.hpp>

#include <srs_app_forward.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_mock.hpp>

#include <stdio.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace std;

// State Threads cannot create a coroutine: the process is out of memory or over its address space limit.
static void *forward_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// A publisher starts a stream on a vhost that forwards to another server. When no coroutine can be created for the
// forwarder, the publish fails with the ST error, naming the destination, instead of a forward that never runs. The
// forward shows in the stream API until the unpublish after the failure, which stops the forwarder safely and removes
// it.
VOID TEST(AppForwardTest, PublishFailsWhenNoCoroutineCanBeCreatedForForwarder)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:19350");
    config->set_forward_destinations(destinations);

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
    err = hub->on_publish();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("start thread")) << desc;
    EXPECT_NE(string::npos, desc.find("forward-to=127.0.0.1:19350")) << desc;

    ASSERT_EQ(1, (int)hub->forwarders_.size());
    SrsForwarder *forwarder = dynamic_cast<SrsForwarder *>(hub->forwarders_.at(0));
    ASSERT_TRUE(forwarder != NULL);
    string id = forwarder->id_;

    SrsStatisticForward *forward = _srs_stat->find_forward(id);
    ASSERT_TRUE(forward != NULL);
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/livestream", forward->url_.c_str());
    EXPECT_EQ(SrsForwardStateConnecting, forward->state_);

    hub->on_unpublish();
    EXPECT_EQ(0, (int)hub->forwarders_.size());
    EXPECT_TRUE(_srs_stat->find_forward(id) == NULL);
}

// The destination server is down: the forwarder logs the connect error, shows retrying in the stream API, and waits
// 3s before it connects again. An unpublish during that wait stops the forwarder at once, not after the wait, with no
// second connect, and removes it from the stream API.
VOID TEST(AppForwardTest, DestinationDown_RetriedUntilUnpublish)
{
    srs_error_t err;

    SrsStatistic stat;
    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "connection refused");
    factory->next_ = sdk;

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get(), req.get()));
    forwarder->stat_ = &stat;
    forwarder->app_factory_ = factory.get();
    forwarder->config_ = config.get();
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    err = forwarder->on_publish();
    // Let the forwarder connect, fail, and start its wait.
    for (int i = 0; i < 100 && sdk->connect_count_ == 0; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    SrsStatisticForward *forward = stat.find_forward(forwarder->id_);
    SrsForwardState state = forward ? forward->state_ : SrsForwardStateConnecting;

    srs_utime_t starttime = srs_time_now_realtime();
    forwarder->on_unpublish();
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;

    _srs_log = original_log;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(1, sdk->connect_count_);
    EXPECT_EQ(0, sdk->publish_count_);
    EXPECT_EQ(1, (int)factory->clients_.size());
    EXPECT_TRUE(forward != NULL);
    EXPECT_EQ(SrsForwardStateRetrying, state);
    EXPECT_LT(elapsed, 1 * SRS_UTIME_SECONDS);
    EXPECT_TRUE(stat.find_forward(forwarder->id_) == NULL);

    int retries = 0;
    for (int i = 0; i < (int)log.warns_.size(); i++) {
        const string &w = log.warns_[i];
        if (w.find("Forwarder: Ignore error") == 0 && w.find("connection refused") != string::npos) {
            retries++;
        }
    }
    EXPECT_EQ(1, retries);
}

// The destination accepts the publish, then resets the connection at the first packet the forwarder sends, the
// stream's metadata: the forward ends with the write error, and neither the sequence headers nor the queued media are
// sent.
VOID TEST(AppForwardTest, DestinationResetsAtMetadata_NothingElseSent)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get(), req.get()));

    SrsMediaPacket *pkts[] = {utest_forward_metadata(0, 'm'), utest_forward_video_sh(0, 'V'), utest_forward_audio_sh(0, 'A'),
                              utest_forward_video_keyframe(40, 'k')};
    HELPER_EXPECT_SUCCESS(forwarder->on_meta_data(pkts[0]));
    HELPER_EXPECT_SUCCESS(forwarder->on_video(pkts[1]));
    HELPER_EXPECT_SUCCESS(forwarder->on_audio(pkts[2]));
    HELPER_EXPECT_SUCCESS(forwarder->on_video(pkts[3]));
    for (int i = 0; i < 4; i++) {
        srs_freep(pkts[i]);
    }

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
    sdk->fail_send_at_ = 1;
    forwarder->sdk_ = sdk;
    srs_freep(forwarder->trd_);
    forwarder->trd_ = new MockCoroutineForForwarder(10);

    err = forwarder->forward();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("send metadata")) << desc;

    EXPECT_EQ(1, sdk->send_count_);
    ASSERT_EQ(1, (int)sdk->sent_.size());
    EXPECT_EQ(SrsFrameTypeScript, sdk->sent_[0].type_);
    EXPECT_EQ('m', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ(0, sdk->recv_count_);
    EXPECT_EQ(4, forwarder->queue_->size());
}

// A forward is running: each pithy print period the forwarder logs the push's kbps line with the number of messages it
// sent in that loop, including a loop with nothing to send while the publisher is idle.
VOID TEST(AppForwardTest, Forwarding_KbpsLineLoggedWithMessagesSent)
{
    srs_error_t err;

    PithyPrintAtOnceScope pithy(SrsPithyPrint::create_forwarder);

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get(), req.get()));
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));
    utest_forward_wire(forwarder.get(), factory.get(), config.get(), 2);

    // The publisher sends a keyframe and an audio frame in the first loop, then nothing.
    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->live_forwarder_ = forwarder.get();
    sdk->live_.push_back(utest_forward_batch(utest_forward_video_keyframe(100, 'K'), utest_forward_audio_raw(110, 'a')));
    factory->next_ = sdk;

    err = forwarder->do_cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    EXPECT_STREQ("Ka", utest_forward_markers(sdk->sent_).c_str());
    ASSERT_EQ(2, (int)sdk->kbps_labels_.size());
    EXPECT_EQ(SRS_CONSTS_LOG_FOWARDER, sdk->kbps_labels_.at(0));
    EXPECT_EQ(2, sdk->kbps_msgs_.at(0));
    EXPECT_EQ(SRS_CONSTS_LOG_FOWARDER, sdk->kbps_labels_.at(1));
    EXPECT_EQ(0, sdk->kbps_msgs_.at(1));
}

// A segment file of the path, of duration ms long, written to the disk unless it is gone.
static SrsFragment *fragment_utest_segment(std::string path, int64_t duration, bool gone)
{
    if (gone) {
        ::unlink(path.c_str());
    } else {
        FILE *fp = ::fopen(path.c_str(), "wb");
        if (fp) {
            ::fputs("ts", fp);
            ::fclose(fp);
        }
    }

    SrsFragment *fragment = new SrsFragment();
    fragment->set_path(path);
    fragment->append(1000);
    fragment->append(1000 + duration);
    return fragment;
}

static bool fragment_utest_file_exists(std::string path)
{
    return ::access(path.c_str(), F_OK) == 0;
}

// HLS (or DASH) with cleanup on deletes the segments that leave the window. When an expired segment file is already
// gone, such as removed by the operator's cleanup job or a sync to a CDN that moves the files, the delete fails with a
// warning, and the window still deletes the other expired files, frees every expired segment and keeps the live
// segments and their files.
VOID TEST(AppFragmentTest, CleanupExpiredSegmentAlreadyDeleted_OtherExpiredFilesStillDeleted)
{
    string gone = _srs_tmp_file_prefix + "fragment-cleanup-gone.ts";
    string old = _srs_tmp_file_prefix + "fragment-cleanup-old.ts";
    string live1 = _srs_tmp_file_prefix + "fragment-cleanup-live1.ts";
    string live2 = _srs_tmp_file_prefix + "fragment-cleanup-live2.ts";

    SrsUniquePtr<SrsFragmentWindow> window(new SrsFragmentWindow());
    window->append(fragment_utest_segment(gone, 4000, true));
    window->append(fragment_utest_segment(old, 4000, false));
    window->append(fragment_utest_segment(live1, 4000, false));
    window->append(fragment_utest_segment(live2, 4000, false));

    // A 5s window keeps the last two 4s segments; the first two expire.
    window->shrink(5 * SRS_UTIME_SECONDS);
    ASSERT_EQ(2, window->size());
    ASSERT_EQ(2, (int)window->expired_fragments_.size());

    window->clear_expired(true);
    EXPECT_EQ(0, (int)window->expired_fragments_.size());
    EXPECT_FALSE(fragment_utest_file_exists(old));

    EXPECT_EQ(2, window->size());
    EXPECT_EQ(live1, dynamic_cast<SrsFragment *>(window->at(0))->fullpath());
    EXPECT_EQ(live2, dynamic_cast<SrsFragment *>(window->at(1))->fullpath());
    EXPECT_TRUE(fragment_utest_file_exists(live1));
    EXPECT_TRUE(fragment_utest_file_exists(live2));

    ::unlink(live1.c_str());
    ::unlink(live2.c_str());
}
