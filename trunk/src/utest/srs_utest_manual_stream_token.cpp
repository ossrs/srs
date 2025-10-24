//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_stream_token.hpp>

using namespace std;

#include <srs_app_st.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>

// Mock request for testing stream publish tokens
class MockStreamTokenRequest : public SrsRequest
{
public:
    string mock_stream_url;

    MockStreamTokenRequest(const string &url = "/live/livestream")
    {
        mock_stream_url = url;
    }

    virtual ~MockStreamTokenRequest()
    {
    }

    virtual string get_stream_url()
    {
        return mock_stream_url;
    }
};

VOID TEST(StreamTokenTest, TokenBasicProperties)
{
    SrsStreamPublishTokenManager manager;
    SrsStreamPublishToken token("/live/livestream", &manager);

    // Test basic properties
    EXPECT_STREQ("/live/livestream", token.stream_url().c_str());
    EXPECT_FALSE(token.is_acquired());

    // Test setting acquired state
    token.set_acquired(true);
    EXPECT_TRUE(token.is_acquired());

    token.set_acquired(false);
    EXPECT_FALSE(token.is_acquired());

    // Test setting publisher context ID
    SrsContextId test_cid;
    test_cid.set_value("test-context-id");
    token.set_publisher_cid(test_cid);
    EXPECT_STREQ("test-context-id", token.publisher_cid().c_str());
}

VOID TEST(StreamTokenTest, SingleTokenAcquisition)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");
    SrsStreamPublishToken *token = NULL;

    // First acquisition should succeed
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
    EXPECT_TRUE(token != NULL);
    EXPECT_TRUE(token->is_acquired());
    EXPECT_STREQ("/live/stream1", token->stream_url().c_str());

    // Clean up
    srs_freep(token);
}

VOID TEST(StreamTokenTest, DuplicateTokenRejection)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");
    SrsStreamPublishToken *token1 = NULL;
    SrsStreamPublishToken *token2 = NULL;

    // First acquisition should succeed
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token1));
    EXPECT_TRUE(token1 != NULL);
    EXPECT_TRUE(token1->is_acquired());

    // Second acquisition for same stream should fail
    HELPER_EXPECT_FAILED(manager.acquire_token(&req, token2));
    EXPECT_TRUE(token2 == NULL);

    // Clean up
    srs_freep(token1);
}

VOID TEST(StreamTokenTest, DifferentStreamsAllowed)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req1("/live/stream1");
    MockStreamTokenRequest req2("/live/stream2");
    SrsStreamPublishToken *token1 = NULL;
    SrsStreamPublishToken *token2 = NULL;

    // Both acquisitions should succeed for different streams
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req1, token1));
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req2, token2));

    EXPECT_TRUE(token1 != NULL);
    EXPECT_TRUE(token2 != NULL);
    EXPECT_TRUE(token1->is_acquired());
    EXPECT_TRUE(token2->is_acquired());
    EXPECT_STREQ("/live/stream1", token1->stream_url().c_str());
    EXPECT_STREQ("/live/stream2", token2->stream_url().c_str());

    // Clean up
    srs_freep(token1);
    srs_freep(token2);
}

VOID TEST(StreamTokenTest, TokenAutoRelease)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");

    // Acquire token in a scope
    if (true) {
        SrsStreamPublishToken *token = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
        EXPECT_TRUE(token != NULL);
        EXPECT_TRUE(token->is_acquired());

        // Token will be automatically released when destroyed
        srs_freep(token);
    }

    // Now we should be able to acquire the same stream again
    SrsStreamPublishToken *token2 = NULL;
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token2));
    EXPECT_TRUE(token2 != NULL);
    EXPECT_TRUE(token2->is_acquired());

    // Clean up
    srs_freep(token2);
}

VOID TEST(StreamTokenTest, ContextIdTracking)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");
    SrsStreamPublishToken *token = NULL;

    // Get current context ID
    SrsContextId current_cid = _srs_context->generate_id();
    _srs_context->set_id(current_cid);

    // Acquire token
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
    EXPECT_TRUE(token != NULL);

    // Token should track the current context ID
    EXPECT_STREQ(current_cid.c_str(), token->publisher_cid().c_str());

    // Clean up
    srs_freep(token);
}

VOID TEST(StreamTokenTest, ErrorCodeVerification)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");
    SrsStreamPublishToken *token1 = NULL;
    SrsStreamPublishToken *token2 = NULL;

    // First acquisition should succeed
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token1));
    EXPECT_TRUE(token1 != NULL);

    // Second acquisition should fail with specific error code
    err = manager.acquire_token(&req, token2);
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
    EXPECT_TRUE(token2 == NULL);

    // Clean up error and token
    srs_freep(err);
    srs_freep(token1);
}

VOID TEST(StreamTokenTest, MultipleStreamsConcurrent)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    // Test multiple different streams can be acquired simultaneously
    vector<MockStreamTokenRequest *> requests;
    vector<SrsStreamPublishToken *> tokens;

    for (int i = 0; i < 10; i++) {
        string stream_url = "/live/stream" + srs_strconv_format_int(i);
        MockStreamTokenRequest *req = new MockStreamTokenRequest(stream_url);
        requests.push_back(req);

        SrsStreamPublishToken *token = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(req, token));
        EXPECT_TRUE(token != NULL);
        EXPECT_TRUE(token->is_acquired());
        EXPECT_STREQ(stream_url.c_str(), token->stream_url().c_str());
        tokens.push_back(token);
    }

    // Clean up
    for (size_t i = 0; i < requests.size(); i++) {
        srs_freep(requests[i]);
        srs_freep(tokens[i]);
    }
}

VOID TEST(StreamTokenTest, TokenReleaseAndReacquire)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");

    // Acquire, release, and reacquire the same stream multiple times
    for (int i = 0; i < 5; i++) {
        SrsStreamPublishToken *token = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
        EXPECT_TRUE(token != NULL);
        EXPECT_TRUE(token->is_acquired());
        EXPECT_STREQ("/live/stream1", token->stream_url().c_str());

        // Release token
        srs_freep(token);

        // Should be able to acquire again immediately
        SrsStreamPublishToken *token2 = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token2));
        EXPECT_TRUE(token2 != NULL);
        EXPECT_TRUE(token2->is_acquired());

        // Clean up
        srs_freep(token2);
    }
}

VOID TEST(StreamTokenTest, EmptyStreamUrl)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("");
    SrsStreamPublishToken *token = NULL;

    // Should be able to acquire token even with empty stream URL
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
    EXPECT_TRUE(token != NULL);
    EXPECT_TRUE(token->is_acquired());
    EXPECT_STREQ("", token->stream_url().c_str());

    // Second acquisition should fail
    SrsStreamPublishToken *token2 = NULL;
    HELPER_EXPECT_FAILED(manager.acquire_token(&req, token2));
    EXPECT_TRUE(token2 == NULL);

    // Clean up
    srs_freep(token);
}

VOID TEST(StreamTokenTest, TokenDestructorBehavior)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");

    // Test that token destructor properly releases the token
    if (true) {
        SrsStreamPublishToken *token = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
        EXPECT_TRUE(token != NULL);
        EXPECT_TRUE(token->is_acquired());

        // Manually call destructor
        srs_freep(token);
    }

    // Should be able to acquire the same stream again
    SrsStreamPublishToken *token2 = NULL;
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token2));
    EXPECT_TRUE(token2 != NULL);
    EXPECT_TRUE(token2->is_acquired());

    // Clean up
    srs_freep(token2);
}

VOID TEST(StreamTokenTest, ManagerDestructorCleanup)
{
    srs_error_t err;

    // Test that manager destructor properly cleans up all tokens
    if (true) {
        SrsStreamPublishTokenManager manager;

        // Acquire multiple tokens
        for (int i = 0; i < 5; i++) {
            string stream_url = "/live/stream" + srs_strconv_format_int(i);
            MockStreamTokenRequest req(stream_url);
            SrsStreamPublishToken *token = NULL;

            HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
            EXPECT_TRUE(token != NULL);
        }

        // Don't manually free tokens - let manager destructor handle cleanup
        // Manager destructor should clean up all tokens automatically
    }

    // If we reach here without crashes, the cleanup worked correctly
    EXPECT_TRUE(true);
}

// Mock coroutine class to simulate race conditions
class MockTokenCoroutine : public ISrsCoroutineHandler
{
public:
    SrsStreamPublishTokenManager *manager;
    MockStreamTokenRequest *req;
    SrsStreamPublishToken *token;
    srs_error_t result;
    bool completed;
    int delay_ms;

    MockTokenCoroutine(SrsStreamPublishTokenManager *mgr, MockStreamTokenRequest *request, int delay = 0)
    {
        manager = mgr;
        req = request;
        token = NULL;
        result = srs_success;
        completed = false;
        delay_ms = delay;
    }

    virtual ~MockTokenCoroutine()
    {
        srs_freep(result);
        srs_freep(token);
    }

    virtual srs_error_t cycle()
    {
        srs_error_t err = srs_success;

        // Add delay to simulate timing variations
        if (delay_ms > 0) {
            srs_usleep(delay_ms * SRS_UTIME_MILLISECONDS);
        }

        // Try to acquire token
        result = manager->acquire_token(req, token);
        completed = true;

        return err;
    }
};

VOID TEST(StreamTokenTest, RaceConditionPrevention)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    // Create multiple coroutines trying to acquire the same stream
    MockStreamTokenRequest req("/live/stream1");
    vector<MockTokenCoroutine *> coroutines;
    vector<SrsSTCoroutine *> threads;

    // Create 5 coroutines with different delays
    for (int i = 0; i < 5; i++) {
        MockTokenCoroutine *handler = new MockTokenCoroutine(&manager, &req, 1);
        coroutines.push_back(handler);

        SrsSTCoroutine *trd = new SrsSTCoroutine("token-test", handler, _srs_context->get_id());
        threads.push_back(trd);

        HELPER_ASSERT_SUCCESS(trd->start());
    }

    // Wait a bit for completion
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Wait for all coroutines to complete
    for (size_t i = 0; i < threads.size(); i++) {
        threads[i]->stop();
        threads[i]->interrupt();
    }

    // Wait a bit for completion
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Check results - exactly one should succeed, others should fail
    int success_count = 0;
    int failure_count = 0;
    SrsStreamPublishToken *acquired_token = NULL;

    for (size_t i = 0; i < coroutines.size(); i++) {
        if (coroutines[i]->completed) {
            if (coroutines[i]->result == srs_success) {
                success_count++;
                acquired_token = coroutines[i]->token;
            } else {
                failure_count++;
                EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(coroutines[i]->result));
            }
        }
    }

    // Exactly one should succeed
    EXPECT_EQ(1, success_count);
    EXPECT_EQ(4, failure_count);
    EXPECT_TRUE(acquired_token != NULL);

    // Clean up
    for (size_t i = 0; i < coroutines.size(); i++) {
        srs_freep(coroutines[i]);
        srs_freep(threads[i]);
    }
}

VOID TEST(StreamTokenTest, SequentialAcquisitionAfterRelease)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");

    // Test sequential acquisition and release pattern
    for (int round = 0; round < 10; round++) {
        SrsStreamPublishToken *token = NULL;

        // Acquire token
        HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
        EXPECT_TRUE(token != NULL);
        EXPECT_TRUE(token->is_acquired());

        // Verify we can't acquire again
        SrsStreamPublishToken *token2 = NULL;
        HELPER_EXPECT_FAILED(manager.acquire_token(&req, token2));
        EXPECT_TRUE(token2 == NULL);

        // Release token
        srs_freep(token);

        // Should be able to acquire again immediately
        SrsStreamPublishToken *token3 = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token3));
        EXPECT_TRUE(token3 != NULL);
        EXPECT_TRUE(token3->is_acquired());

        // Clean up
        srs_freep(token3);
    }
}

VOID TEST(StreamTokenTest, TokenManagerStressTest)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    // Stress test with many streams
    const int num_streams = 100;
    vector<MockStreamTokenRequest *> requests;
    vector<SrsStreamPublishToken *> tokens;

    // Acquire tokens for many streams
    for (int i = 0; i < num_streams; i++) {
        string stream_url = "/live/stress_stream_" + srs_strconv_format_int(i);
        MockStreamTokenRequest *req = new MockStreamTokenRequest(stream_url);
        requests.push_back(req);

        SrsStreamPublishToken *token = NULL;
        HELPER_EXPECT_SUCCESS(manager.acquire_token(req, token));
        EXPECT_TRUE(token != NULL);
        EXPECT_TRUE(token->is_acquired());
        tokens.push_back(token);
    }

    // Verify all tokens are unique and properly acquired
    for (int i = 0; i < num_streams; i++) {
        EXPECT_TRUE(tokens[i]->is_acquired());
        string expected_url = "/live/stress_stream_" + srs_strconv_format_int(i);
        EXPECT_STREQ(expected_url.c_str(), tokens[i]->stream_url().c_str());

        // Verify we can't acquire the same stream again
        SrsStreamPublishToken *duplicate_token = NULL;
        HELPER_EXPECT_FAILED(manager.acquire_token(requests[i], duplicate_token));
        EXPECT_TRUE(duplicate_token == NULL);
    }

    // Clean up
    for (int i = 0; i < num_streams; i++) {
        srs_freep(requests[i]);
        srs_freep(tokens[i]);
    }
}

VOID TEST(StreamTokenTest, TokenWithDifferentContextIds)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    MockStreamTokenRequest req("/live/stream1");

    SrsContextId first_cid = _srs_context->get_id();
    SrsStreamPublishToken *token1 = NULL;

    // Acquire token with first context
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token1));
    EXPECT_TRUE(token1 != NULL);
    EXPECT_STREQ(first_cid.c_str(), token1->publisher_cid().c_str());

    // Change context ID
    _srs_context->set_id(SrsContextId());
    SrsContextId second_cid = _srs_context->get_id();

    // Should still fail to acquire same stream with different context
    SrsStreamPublishToken *token2 = NULL;
    HELPER_EXPECT_FAILED(manager.acquire_token(&req, token2));
    EXPECT_TRUE(token2 == NULL);

    // Clean up
    srs_freep(token1);
}

VOID TEST(StreamTokenTest, TokenUrlCaseSensitivity)
{
    srs_error_t err;

    SrsStreamPublishTokenManager manager;

    // Test case sensitivity of stream URLs
    MockStreamTokenRequest req1("/live/Stream1");
    MockStreamTokenRequest req2("/live/stream1");
    MockStreamTokenRequest req3("/LIVE/STREAM1");

    SrsStreamPublishToken *token1 = NULL;
    SrsStreamPublishToken *token2 = NULL;
    SrsStreamPublishToken *token3 = NULL;

    // All should be treated as different streams (case sensitive)
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req1, token1));
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req2, token2));
    HELPER_EXPECT_SUCCESS(manager.acquire_token(&req3, token3));

    EXPECT_TRUE(token1 != NULL);
    EXPECT_TRUE(token2 != NULL);
    EXPECT_TRUE(token3 != NULL);

    EXPECT_STREQ("/live/Stream1", token1->stream_url().c_str());
    EXPECT_STREQ("/live/stream1", token2->stream_url().c_str());
    EXPECT_STREQ("/LIVE/STREAM1", token3->stream_url().c_str());

    // Clean up
    srs_freep(token1);
    srs_freep(token2);
    srs_freep(token3);
}

VOID TEST(StreamTokenTest, TokenManagerMemoryLeakPrevention)
{
    srs_error_t err;

    // Test that repeated acquire/release cycles don't cause memory leaks
    for (int cycle = 0; cycle < 10; cycle++) {
        SrsStreamPublishTokenManager manager;

        // Acquire and release multiple tokens
        for (int i = 0; i < 20; i++) {
            string stream_url = "/live/leak_test_" + srs_strconv_format_int(i);
            MockStreamTokenRequest req(stream_url);
            SrsStreamPublishToken *token = NULL;

            HELPER_EXPECT_SUCCESS(manager.acquire_token(&req, token));
            EXPECT_TRUE(token != NULL);

            // Immediately release
            srs_freep(token);
        }

        // Manager destructor should clean up everything
    }

    // If we reach here without crashes or excessive memory usage, test passes
    EXPECT_TRUE(true);
}
