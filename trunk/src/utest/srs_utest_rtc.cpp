/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srs_utest_rtc.hpp>

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_conn.hpp>

VOID TEST(KernelRTCTest, SequenceCompare)
{
    if (true) {
        EXPECT_EQ(0, srs_rtp_seq_distance(0, 0));
        EXPECT_EQ(0, srs_rtp_seq_distance(1, 1));
        EXPECT_EQ(0, srs_rtp_seq_distance(3, 3));

        EXPECT_EQ(1, srs_rtp_seq_distance(0, 1));
        EXPECT_EQ(-1, srs_rtp_seq_distance(1, 0));
        EXPECT_EQ(1, srs_rtp_seq_distance(65535, 0));
    }

    if (true) {
        EXPECT_FALSE(srs_rtp_seq_distance(1, 1) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65534, 65535) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(0, 1) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(255, 256) > 0);

        EXPECT_TRUE(srs_rtp_seq_distance(65535, 0) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65280, 0) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65535, 255) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65280, 255) > 0);

        EXPECT_FALSE(srs_rtp_seq_distance(0, 65535) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(0, 65280) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(255, 65535) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(255, 65280) > 0);

        // Note that srs_rtp_seq_distance(0, 32768)>0 is TRUE by https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
        //      but for WebRTC jitter buffer it's FALSE and we follow it.
        EXPECT_FALSE(srs_rtp_seq_distance(0, 32768) > 0);
        // It's FALSE definitely.
        EXPECT_FALSE(srs_rtp_seq_distance(32768, 0) > 0);
    }

    if (true) {
        EXPECT_FALSE(srs_seq_is_newer(1, 1));
        EXPECT_TRUE(srs_seq_is_newer(65535, 65534));
        EXPECT_TRUE(srs_seq_is_newer(1, 0));
        EXPECT_TRUE(srs_seq_is_newer(256, 255));

        EXPECT_TRUE(srs_seq_is_newer(0, 65535));
        EXPECT_TRUE(srs_seq_is_newer(0, 65280));
        EXPECT_TRUE(srs_seq_is_newer(255, 65535));
        EXPECT_TRUE(srs_seq_is_newer(255, 65280));

        EXPECT_FALSE(srs_seq_is_newer(65535, 0));
        EXPECT_FALSE(srs_seq_is_newer(65280, 0));
        EXPECT_FALSE(srs_seq_is_newer(65535, 255));
        EXPECT_FALSE(srs_seq_is_newer(65280, 255));

        EXPECT_FALSE(srs_seq_is_newer(32768, 0));
        EXPECT_FALSE(srs_seq_is_newer(0, 32768));
    }

    if (true) {
        EXPECT_FALSE(srs_seq_distance(1, 1) > 0);
        EXPECT_TRUE(srs_seq_distance(65535, 65534) > 0);
        EXPECT_TRUE(srs_seq_distance(1, 0) > 0);
        EXPECT_TRUE(srs_seq_distance(256, 255) > 0);

        EXPECT_TRUE(srs_seq_distance(0, 65535) > 0);
        EXPECT_TRUE(srs_seq_distance(0, 65280) > 0);
        EXPECT_TRUE(srs_seq_distance(255, 65535) > 0);
        EXPECT_TRUE(srs_seq_distance(255, 65280) > 0);

        EXPECT_FALSE(srs_seq_distance(65535, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(65280, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(65535, 255) > 0);
        EXPECT_FALSE(srs_seq_distance(65280, 255) > 0);

        EXPECT_FALSE(srs_seq_distance(32768, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(0, 32768) > 0);
    }

    if (true) {
        EXPECT_FALSE(srs_seq_is_rollback(1, 1));
        EXPECT_FALSE(srs_seq_is_rollback(65535, 65534));
        EXPECT_FALSE(srs_seq_is_rollback(1, 0));
        EXPECT_FALSE(srs_seq_is_rollback(256, 255));

        EXPECT_TRUE(srs_seq_is_rollback(0, 65535));
        EXPECT_TRUE(srs_seq_is_rollback(0, 65280));
        EXPECT_TRUE(srs_seq_is_rollback(255, 65535));
        EXPECT_TRUE(srs_seq_is_rollback(255, 65280));

        EXPECT_FALSE(srs_seq_is_rollback(65535, 0));
        EXPECT_FALSE(srs_seq_is_rollback(65280, 0));
        EXPECT_FALSE(srs_seq_is_rollback(65535, 255));
        EXPECT_FALSE(srs_seq_is_rollback(65280, 255));

        EXPECT_FALSE(srs_seq_is_rollback(32768, 0));
        EXPECT_FALSE(srs_seq_is_rollback(0, 32768));
    }
}

VOID TEST(KernelRTCTest, DefaultTrackStatus)
{
    // By default, track is disabled.
    if (true) {
        SrsRtcTrackDescription td;

        // The track must default to disable, that is, the active is false.
        EXPECT_FALSE(td.is_active_);
    }

    // Enable it by player.
    if (true) {
        SrsRtcConnection s(NULL, SrsContextId()); SrsRtcPlayStream play(&s, SrsContextId());
        SrsRtcAudioSendTrack* audio; SrsRtcVideoSendTrack *video;

        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "audio"; ds.id_ = "NSNWOn19NDn12o8nNeji2"; ds.ssrc_ = 100;
            play.audio_tracks_[ds.ssrc_] = audio = new SrsRtcAudioSendTrack(&s, &ds);
        }
        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "video"; ds.id_ = "VMo22nfLDn122nfnDNL2"; ds.ssrc_ = 200;
            play.video_tracks_[ds.ssrc_] = video = new SrsRtcVideoSendTrack(&s, &ds);
        }
        EXPECT_FALSE(audio->get_track_status());
        EXPECT_FALSE(video->get_track_status());

        play.set_all_tracks_status(true);
        EXPECT_TRUE(audio->get_track_status());
        EXPECT_TRUE(video->get_track_status());
    }

    // Enable it by publisher.
    if (true) {
        SrsRtcConnection s(NULL, SrsContextId()); SrsRtcPublishStream publish(&s);
        SrsRtcAudioRecvTrack* audio; SrsRtcVideoRecvTrack *video;

        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "audio"; ds.id_ = "NSNWOn19NDn12o8nNeji2"; ds.ssrc_ = 100;
            audio = new SrsRtcAudioRecvTrack(&s, &ds); publish.audio_tracks_.push_back(audio);
        }
        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "video"; ds.id_ = "VMo22nfLDn122nfnDNL2"; ds.ssrc_ = 200;
            video = new SrsRtcVideoRecvTrack(&s, &ds); publish.video_tracks_.push_back(video);
        }
        EXPECT_FALSE(audio->get_track_status());
        EXPECT_FALSE(video->get_track_status());

        publish.set_all_tracks_status(true);
        EXPECT_TRUE(audio->get_track_status());
        EXPECT_TRUE(video->get_track_status());
    }
}

