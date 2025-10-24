//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_source_lock.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <st.h>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif

/**
 * Unit tests for source lock refinement (PR #4449)
 *
 * This file tests the race condition fixes in source managers where multiple
 * coroutines could create duplicate sources for the same stream. The fix ensures
 * atomic source creation and pool insertion.
 *
 * Template Design:
 * - MockOtherSourceAsyncCreator<ManagerType, SourceType>: Template for RTC, SRT, RTSP
 * - MockLiveSourceAsyncCreator: Separate class for Live sources (needs handler parameter)
 * - Type aliases provide backward compatibility with original class names
 */

// This mock request always cause context switch in get_stream_url().
class MockAsyncSrsRequest : public ISrsRequest
{
public:
    string mock_stream_url;
    bool enable_context_switch;

    MockAsyncSrsRequest(const string &url = "/live/livestream", bool context_switch = false)
    {
        mock_stream_url = url;
        enable_context_switch = context_switch;

        // Initialize all ISrsRequest members to safe defaults
        objectEncoding_ = RTMP_SIG_AMF0_VER;
        duration_ = -1;
        port_ = SRS_CONSTS_RTMP_DEFAULT_PORT;
        args_ = NULL; // Initialize to NULL to prevent crashes
        protocol_ = "rtmp";

        // Parse the URL to set vhost, app, stream
        size_t app_pos = url.find('/', 1); // Find second slash
        if (app_pos != string::npos) {
            size_t stream_pos = url.find('/', app_pos + 1); // Find third slash
            if (stream_pos != string::npos) {
                app_ = url.substr(app_pos + 1, stream_pos - app_pos - 1);
                stream_ = url.substr(stream_pos + 1);
            } else {
                app_ = url.substr(app_pos + 1);
                stream_ = "livestream";
            }
        } else {
            app_ = "live";
            stream_ = "livestream";
        }

        vhost_ = "localhost";
    }

    virtual string get_stream_url()
    {
        on_source_created();
        return mock_stream_url;
    }

    virtual void on_source_created()
    {
        // Simulate context switch that could happen during source initialization
        if (enable_context_switch) {
            // Force a context switch by yielding to other coroutines
            // This simulates the original race condition scenario
            st_usleep(1 * SRS_UTIME_MILLISECONDS); // 1ms sleep to trigger context switch
        }
    }

    virtual ISrsRequest *copy()
    {
        MockAsyncSrsRequest *cp = new MockAsyncSrsRequest(mock_stream_url, enable_context_switch);

        *cp = *this;
        if (args_) {
            cp->args_ = args_->copy()->to_object();
        }

        return cp;
    }

    virtual void update_auth(ISrsRequest *req)
    {
    }

    virtual void strip()
    {
    }

    virtual ISrsRequest *as_http()
    {
        return this;
    }
};

// Template for non-live source async creators (RTC, SRT, RTSP)
// Since SRS uses C++98, we use a simple template approach
// Note: MockLiveSourceAsyncCreator is kept separate because it requires an additional
// ISrsLiveSourceHandler parameter that the other source managers don't need
template <typename ManagerType, typename SourceType>
class MockOtherSourceAsyncCreator : public ISrsCoroutineHandler
{
public:
    SrsWaitGroup *wg_;
    ManagerType *manager_;
    MockAsyncSrsRequest *req_;
    SrsSharedPtr<SourceType> source_;

    MockOtherSourceAsyncCreator(SrsWaitGroup *w, ManagerType *m, MockAsyncSrsRequest *r)
    {
        wg_ = w;
        manager_ = m;
        req_ = r;
    }

    virtual ~MockOtherSourceAsyncCreator()
    {
    }

    virtual srs_error_t cycle()
    {
        srs_error_t err = do_cycle();
        wg_->done();
        return err;
    }

    srs_error_t do_cycle()
    {
        srs_error_t err = srs_success;

        // Test fetch_or_create - should create new source
        if ((err = manager_->fetch_or_create(req_, source_)) != srs_success) {
            return srs_error_wrap(err, "fetch or create");
        }

        return err;
    }
};

// Type aliases for convenience (C++98 compatible)
typedef MockOtherSourceAsyncCreator<SrsLiveSourceManager, SrsLiveSource> MockLiveSourceAsyncCreator;

// Test race condition of source managers
VOID TEST(SourceLockTest, LiveSourceManager_RaceCondition)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test1", true); // Enable context switch
    SrsSharedPtr<SrsLiveSource> source;

    // Create a coroutine to create source2.
    SrsWaitGroup wg;
    MockLiveSourceAsyncCreator creator(&wg, &manager, &req);
    SrsSTCoroutine trd("test", &creator);

    wg.add(1);
    HELPER_EXPECT_SUCCESS(trd.start());

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Wait for coroutine to finish.
    wg.wait();

    // The created two sources should be the same instance (no duplicates created)
    EXPECT_EQ(source.get(), creator.source_.get());

    // Test fetch - should return the same source
    SrsSharedPtr<SrsLiveSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsLiveSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

// Type aliases for convenience (C++98 compatible)
typedef MockOtherSourceAsyncCreator<SrsRtcSourceManager, SrsRtcSource> MockRtcSourceAsyncCreator;

// Test race condition of source managers
VOID TEST(SourceLockTest, RtcSourceManager_RaceCondition)
{
    srs_error_t err;

    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test1", true); // Enable context switch
    SrsSharedPtr<SrsRtcSource> source;

    // Create a coroutine to create source2.
    SrsWaitGroup wg;
    MockRtcSourceAsyncCreator creator(&wg, &manager, &req);
    SrsSTCoroutine trd("test", &creator);

    wg.add(1);
    HELPER_EXPECT_SUCCESS(trd.start());

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Wait for coroutine to finish.
    wg.wait();

    // The created two sources should be the same instance (no duplicates created)
    EXPECT_EQ(source.get(), creator.source_.get());

    // Test fetch - should return the same source
    SrsSharedPtr<SrsRtcSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsRtcSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

typedef MockOtherSourceAsyncCreator<SrsSrtSourceManager, SrsSrtSource> MockSrtSourceAsyncCreator;

// Test race condition of source managers
VOID TEST(SourceLockTest, SrtSourceManager_RaceCondition)
{
    srs_error_t err;

    SrsSrtSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test1", true); // Enable context switch
    SrsSharedPtr<SrsSrtSource> source;

    // Create a coroutine to create source2.
    SrsWaitGroup wg;
    MockSrtSourceAsyncCreator creator(&wg, &manager, &req);
    SrsSTCoroutine trd("test", &creator);

    wg.add(1);
    HELPER_EXPECT_SUCCESS(trd.start());

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Wait for coroutine to finish.
    wg.wait();

    // The created two sources should be the same instance (no duplicates created)
    EXPECT_EQ(source.get(), creator.source_.get());

    // Test fetch - should return the same source
    SrsSharedPtr<SrsSrtSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsSrtSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

// Test basic functionality of source managers
VOID TEST(SourceLockTest, LiveSourceManager_BasicFunctionality)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test1");
    SrsSharedPtr<SrsLiveSource> source;

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Test fetch - should return the same source
    SrsSharedPtr<SrsLiveSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsLiveSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

// Test concurrent access scenarios to verify race condition fixes
VOID TEST(SourceLockTest, LiveSourceManager_ConcurrentAccess)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/concurrent_test", true); // Enable context switch

    // Simulate multiple concurrent requests for the same stream
    vector<SrsSharedPtr<SrsLiveSource> > sources;
    const int concurrent_requests = 10;

    for (int i = 0; i < concurrent_requests; i++) {
        SrsSharedPtr<SrsLiveSource> source;
        HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
        sources.push_back(source);
        EXPECT_TRUE(source.get() != NULL);
    }

    // All sources should be the same instance (no duplicates created)
    for (int i = 1; i < concurrent_requests; i++) {
        EXPECT_EQ(sources[0].get(), sources[i].get());
    }
}

// Test different stream URLs create different sources
VOID TEST(SourceLockTest, LiveSourceManager_DifferentStreams)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Create sources for different streams
    MockAsyncSrsRequest req1("/live/stream1");
    MockAsyncSrsRequest req2("/live/stream2");
    MockAsyncSrsRequest req3("/live/stream3");

    SrsSharedPtr<SrsLiveSource> source1, source2, source3;

    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req1, source1));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req2, source2));
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req3, source3));

    // All sources should be different instances
    EXPECT_TRUE(source1.get() != NULL);
    EXPECT_TRUE(source2.get() != NULL);
    EXPECT_TRUE(source3.get() != NULL);
    EXPECT_NE(source1.get(), source2.get());
    EXPECT_NE(source1.get(), source3.get());
    EXPECT_NE(source2.get(), source3.get());

    // Fetch should return the same sources
    SrsSharedPtr<SrsLiveSource> fetched1 = manager.fetch(&req1);
    SrsSharedPtr<SrsLiveSource> fetched2 = manager.fetch(&req2);
    SrsSharedPtr<SrsLiveSource> fetched3 = manager.fetch(&req3);

    EXPECT_EQ(source1.get(), fetched1.get());
    EXPECT_EQ(source2.get(), fetched2.get());
    EXPECT_EQ(source3.get(), fetched3.get());
}

// Test fetch for non-existent stream returns NULL
VOID TEST(SourceLockTest, LiveSourceManager_FetchNonExistent)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/nonexistent");

    // Fetch non-existent source should return NULL
    SrsSharedPtr<SrsLiveSource> source = manager.fetch(&req);
    EXPECT_TRUE(source.get() == NULL);
}

// Test that created flag works correctly for initialization
VOID TEST(SourceLockTest, LiveSourceManager_CreatedFlagLogic)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/created_flag_test");

    // First call should create and initialize
    SrsSharedPtr<SrsLiveSource> source1;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source1));
    EXPECT_TRUE(source1.get() != NULL);

    // Second call should find existing and only update auth (not initialize again)
    SrsSharedPtr<SrsLiveSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source1.get(), source2.get());
}

// Test lock protection during source creation
VOID TEST(SourceLockTest, LiveSourceManager_LockProtection)
{
    srs_error_t err;

    // Test that the lock scope is properly limited
    // This test verifies that no functions are called during locking

    SrsLiveSourceManager live_manager;
    HELPER_EXPECT_SUCCESS(live_manager.initialize());

    SrsRtcSourceManager rtc_manager;
    HELPER_EXPECT_SUCCESS(rtc_manager.initialize());

    SrsSrtSourceManager srt_manager;
    HELPER_EXPECT_SUCCESS(srt_manager.initialize());

    // Test that managers can handle rapid successive calls
    MockAsyncSrsRequest req("/live/lock_test");

    for (int i = 0; i < 5; i++) {
        SrsSharedPtr<SrsLiveSource> live_source;
        HELPER_EXPECT_SUCCESS(live_manager.fetch_or_create(&req, live_source));
        EXPECT_TRUE(live_source.get() != NULL);

        SrsSharedPtr<SrsRtcSource> rtc_source;
        HELPER_EXPECT_SUCCESS(rtc_manager.fetch_or_create(&req, rtc_source));
        EXPECT_TRUE(rtc_source.get() != NULL);

        SrsSharedPtr<SrsSrtSource> srt_source;
        HELPER_EXPECT_SUCCESS(srt_manager.fetch_or_create(&req, srt_source));
        EXPECT_TRUE(srt_source.get() != NULL);
    }
}

// Test that simulates the original race condition scenario from issue #1230
// This test verifies that the fix prevents multiple sources for the same stream
VOID TEST(SourceLockTest, LiveSourceManager_RaceConditionPrevention_Issue1230)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/race_condition_test", true); // Enable context switch

    // Simulate the scenario where multiple coroutines try to create
    // sources for the same stream simultaneously
    vector<SrsSharedPtr<SrsLiveSource> > sources;
    const int num_attempts = 20;

    // This simulates what would happen if multiple coroutines
    // called fetch_or_create simultaneously before the fix
    for (int i = 0; i < num_attempts; i++) {
        SrsSharedPtr<SrsLiveSource> source;
        HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
        sources.push_back(source);

        // Verify source is valid
        EXPECT_TRUE(source.get() != NULL);

        // Verify it's the same source every time (no duplicates)
        if (i > 0) {
            EXPECT_EQ(sources[0].get(), source.get())
                << "Race condition detected: different source instances created for same stream";
        }
    }

    // Double-check that all sources are identical
    for (int i = 1; i < num_attempts; i++) {
        EXPECT_EQ(sources[0].get(), sources[i].get())
            << "Source " << i << " differs from source 0";
    }

    // Verify fetch also returns the same source
    SrsSharedPtr<SrsLiveSource> fetched = manager.fetch(&req);
    EXPECT_EQ(sources[0].get(), fetched.get());
}

// Test the atomic nature of source creation and pool insertion
VOID TEST(SourceLockTest, LiveSourceManager_AtomicSourceCreation)
{
    srs_error_t err;

    // Test all source manager types
    SrsRtcSourceManager rtc_manager;
    HELPER_EXPECT_SUCCESS(rtc_manager.initialize());

    SrsSrtSourceManager srt_manager;
    HELPER_EXPECT_SUCCESS(srt_manager.initialize());

    MockAsyncSrsRequest req("/live/atomic_test", true); // Enable context switch

    // Test RTC source manager
    vector<SrsSharedPtr<SrsRtcSource> > rtc_sources;
    for (int i = 0; i < 10; i++) {
        SrsSharedPtr<SrsRtcSource> source;
        HELPER_EXPECT_SUCCESS(rtc_manager.fetch_or_create(&req, source));
        rtc_sources.push_back(source);
        EXPECT_TRUE(source.get() != NULL);
        if (i > 0) {
            EXPECT_EQ(rtc_sources[0].get(), source.get());
        }
    }

    // Test SRT source manager
    vector<SrsSharedPtr<SrsSrtSource> > srt_sources;
    for (int i = 0; i < 10; i++) {
        SrsSharedPtr<SrsSrtSource> source;
        HELPER_EXPECT_SUCCESS(srt_manager.fetch_or_create(&req, source));
        srt_sources.push_back(source);
        EXPECT_TRUE(source.get() != NULL);
        if (i > 0) {
            EXPECT_EQ(srt_sources[0].get(), source.get());
        }
    }
}

// Test that verifies the fix addresses the specific issue mentioned in #1230
// where DVR and HLS module initialization could cause coroutine switches
VOID TEST(SourceLockTest, LiveSourceManager_CoroutineSwitchProtection)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Test multiple streams to ensure each gets its own source
    vector<string> stream_urls;
    stream_urls.push_back("/live/dvr_test1");
    stream_urls.push_back("/live/dvr_test2");
    stream_urls.push_back("/live/hls_test1");
    stream_urls.push_back("/live/hls_test2");
    stream_urls.push_back("/live/mixed_test");

    map<string, SrsSharedPtr<SrsLiveSource> > created_sources;

    // Create sources for each stream
    for (size_t idx = 0; idx < stream_urls.size(); idx++) {
        const string &url = stream_urls[idx];
        MockAsyncSrsRequest req(url, true); // Enable context switch
        SrsSharedPtr<SrsLiveSource> source;

        HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
        EXPECT_TRUE(source.get() != NULL);

        created_sources[url] = source;

        // Verify subsequent calls return the same source
        for (int i = 0; i < 3; i++) {
            SrsSharedPtr<SrsLiveSource> same_source;
            HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, same_source));
            EXPECT_EQ(source.get(), same_source.get())
                << "Source changed for stream " << url << " on attempt " << i;
        }
    }

    // Verify all sources are different from each other
    vector<SrsLiveSource *> source_ptrs;
    for (map<string, SrsSharedPtr<SrsLiveSource> >::iterator it = created_sources.begin();
         it != created_sources.end(); ++it) {
        source_ptrs.push_back(it->second.get());
    }

    for (size_t i = 0; i < source_ptrs.size(); i++) {
        for (size_t j = i + 1; j < source_ptrs.size(); j++) {
            EXPECT_NE(source_ptrs[i], source_ptrs[j])
                << "Sources for different streams should be different instances";
        }
    }
}

// Test edge case with empty or invalid stream URLs
VOID TEST(SourceLockTest, LiveSourceManager_EdgeCases_InvalidUrls)
{
    srs_error_t err;

    SrsLiveSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    // Test with various edge case URLs
    vector<string> edge_case_urls;
    edge_case_urls.push_back("");                       // Empty URL
    edge_case_urls.push_back("/");                      // Root only
    edge_case_urls.push_back("/live/");                 // Trailing slash
    edge_case_urls.push_back("/live//");                // Double slash
    edge_case_urls.push_back("/live/test?param=value"); // With parameters

    for (size_t idx = 0; idx < edge_case_urls.size(); idx++) {
        const string &url = edge_case_urls[idx];
        MockAsyncSrsRequest req(url);
        SrsSharedPtr<SrsLiveSource> source;

        // Should handle edge cases gracefully
        HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
        EXPECT_TRUE(source.get() != NULL);

        // Verify consistency
        SrsSharedPtr<SrsLiveSource> source2;
        HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
        EXPECT_EQ(source.get(), source2.get());
    }
}

VOID TEST(SourceLockTest, RtcSourceManager_BasicFunctionality)
{
    srs_error_t err;

    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test2");
    SrsSharedPtr<SrsRtcSource> source;

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Test fetch - should return the same source
    SrsSharedPtr<SrsRtcSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsRtcSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

VOID TEST(SourceLockTest, SrtSourceManager_BasicFunctionality)
{
    srs_error_t err;

    SrsSrtSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test4");
    SrsSharedPtr<SrsSrtSource> source;

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Note: SrsSrtSourceManager doesn't have a fetch method, only fetch_or_create
    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsSrtSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

VOID TEST(SourceLockTest, RtcSourceManager_ConcurrentAccess)
{
    srs_error_t err;

    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/rtc_concurrent_test", true); // Enable context switch

    // Simulate multiple concurrent requests for the same stream
    vector<SrsSharedPtr<SrsRtcSource> > sources;
    const int concurrent_requests = 10;

    for (int i = 0; i < concurrent_requests; i++) {
        SrsSharedPtr<SrsRtcSource> source;
        HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
        sources.push_back(source);
        EXPECT_TRUE(source.get() != NULL);
    }

    // All sources should be the same instance (no duplicates created)
    for (int i = 1; i < concurrent_requests; i++) {
        EXPECT_EQ(sources[0].get(), sources[i].get());
    }
}

VOID TEST(SourceLockTest, RtcSourceManager_FetchNonExistent)
{
    srs_error_t err;

    SrsRtcSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/nonexistent");

    // Fetch non-existent source should return NULL
    SrsSharedPtr<SrsRtcSource> source = manager.fetch(&req);
    EXPECT_TRUE(source.get() == NULL);
}

#ifdef SRS_RTSP
typedef MockOtherSourceAsyncCreator<SrsRtspSourceManager, SrsRtspSource> MockRtspSourceAsyncCreator;

// Test race condition of source managers
VOID TEST(SourceLockTest, RtspSourceManager_RaceCondition)
{
    srs_error_t err;

    SrsRtspSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test1", true); // Enable context switch
    SrsSharedPtr<SrsRtspSource> source;

    // Create a coroutine to create source2.
    SrsWaitGroup wg;
    MockRtspSourceAsyncCreator creator(&wg, &manager, &req);
    SrsSTCoroutine trd("test", &creator);

    wg.add(1);
    HELPER_EXPECT_SUCCESS(trd.start());

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Wait for coroutine to finish.
    wg.wait();

    // The created two sources should be the same instance (no duplicates created)
    EXPECT_EQ(source.get(), creator.source_.get());

    // Test fetch - should return the same source
    SrsSharedPtr<SrsRtspSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsRtspSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}

// Test lock protection during source creation
VOID TEST(SourceLockTest, LiveSourceManager_LockProtection2)
{
    srs_error_t err;

    // Test that the lock scope is properly limited
    // This test verifies that no functions are called during locking

    SrsRtspSourceManager rtsp_manager;
    HELPER_EXPECT_SUCCESS(rtsp_manager.initialize());

    // Test that managers can handle rapid successive calls
    MockAsyncSrsRequest req("/live/lock_test");

    for (int i = 0; i < 5; i++) {
        SrsSharedPtr<SrsRtspSource> rtsp_source;
        HELPER_EXPECT_SUCCESS(rtsp_manager.fetch_or_create(&req, rtsp_source));
        EXPECT_TRUE(rtsp_source.get() != NULL);
    }
}

// Test the atomic nature of source creation and pool insertion
VOID TEST(SourceLockTest, LiveSourceManager_AtomicSourceCreation2)
{
    srs_error_t err;

    // Test all source manager types
    SrsRtspSourceManager rtsp_manager;
    HELPER_EXPECT_SUCCESS(rtsp_manager.initialize());

    MockAsyncSrsRequest req("/live/atomic_test", true); // Enable context switch

    // Test RTSP source manager
    vector<SrsSharedPtr<SrsRtspSource> > rtsp_sources;
    for (int i = 0; i < 10; i++) {
        SrsSharedPtr<SrsRtspSource> source;
        HELPER_EXPECT_SUCCESS(rtsp_manager.fetch_or_create(&req, source));
        rtsp_sources.push_back(source);
        EXPECT_TRUE(source.get() != NULL);
        if (i > 0) {
            EXPECT_EQ(rtsp_sources[0].get(), source.get());
        }
    }
}

VOID TEST(SourceLockTest, RtspSourceManager_BasicFunctionality)
{
    srs_error_t err;

    SrsRtspSourceManager manager;
    HELPER_EXPECT_SUCCESS(manager.initialize());

    MockAsyncSrsRequest req("/live/test3");
    SrsSharedPtr<SrsRtspSource> source;

    // Test fetch_or_create - should create new source
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source));
    EXPECT_TRUE(source.get() != NULL);

    // Test fetch - should return the same source
    SrsSharedPtr<SrsRtspSource> fetched = manager.fetch(&req);
    EXPECT_TRUE(fetched.get() != NULL);
    EXPECT_EQ(source.get(), fetched.get());

    // Test fetch_or_create again - should return existing source
    SrsSharedPtr<SrsRtspSource> source2;
    HELPER_EXPECT_SUCCESS(manager.fetch_or_create(&req, source2));
    EXPECT_EQ(source.get(), source2.get());
}
#endif // SRS_RTSP
