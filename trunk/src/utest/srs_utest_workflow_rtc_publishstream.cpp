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

#include <srs_utest_workflow_rtc_publishstream.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_mock.hpp>

// This test is used to verify the basic workflow of the RTC publish stream.
// It's finished with the help of AI, but each step is manually designed
// and verified. So this is not dominated by AI, but by humanbeing.
VOID TEST(BasicWorkflowRtcPublishStreamTest, ManuallyVerify)
{
    srs_error_t err;

    // Create mock objects for dependencies
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockAppStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_exec;
    MockExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    MockRtcTrackDescriptionFactory track_factory;
    SrsContextId cid;
    cid.set_value("test-publish-stream-cid");

    // Create RTC publish stream - use real pli_worker_
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Mock the publish stream object
    if (true) {
        // Inject mock dependencies
        publish_stream->config_ = &mock_config;
        publish_stream->rtc_sources_ = &mock_rtc_sources;
        publish_stream->stat_ = &mock_stat;
    }

    // Create stream description with audio and video tracks
    if (true) {
        SrsUniquePtr<SrsRtcSourceDescription> stream_desc(track_factory.create_stream_description());

        // Initialize the publish stream (it will take ownership of track descriptions)
        HELPER_EXPECT_SUCCESS(publish_stream->initialize(&mock_request, stream_desc.get()));

        // Check the tracks, should be one audio track
        EXPECT_EQ(publish_stream->audio_tracks_.size(), 1);
        // Check the tracks, should be one video track
        EXPECT_EQ(publish_stream->video_tracks_.size(), 1);

        // Test: First call to start() should succeed
        HELPER_EXPECT_SUCCESS(publish_stream->start());

        // Verify is_sender_started_ flag is set
        EXPECT_TRUE(publish_stream->is_sender_started_);

        // Wait for coroutine to start. Normally it should be ready and stopped at wait
        // for PLI requests.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Request a PLI about the video ssrc to the publisher.
    if (true) {
        uint32_t video_ssrc = track_factory.video_ssrc_;
        publish_stream->request_keyframe(video_ssrc, cid);

        // Wait for coroutine to process the request
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        // Verify the PLI is sent out
        EXPECT_EQ(mock_receiver.send_rtcp_fb_pli_count_, 1);
    }

    // Stop the publish stream
    publish_stream->stop();
}
