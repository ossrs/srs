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

        // Note that it's TRUE at https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
        EXPECT_FALSE(srs_rtp_seq_distance(0, 32768) > 0);
        // It's FALSE definitely.
        EXPECT_FALSE(srs_rtp_seq_distance(32768, 0) > 0);
    }

    if (true) {
        EXPECT_FALSE(SrsSeqIsNewer(1, 1));
        EXPECT_TRUE(SrsSeqIsNewer(65535, 65534));
        EXPECT_TRUE(SrsSeqIsNewer(1, 0));
        EXPECT_TRUE(SrsSeqIsNewer(256, 255));

        EXPECT_TRUE(SrsSeqIsNewer(0, 65535));
        EXPECT_TRUE(SrsSeqIsNewer(0, 65280));
        EXPECT_TRUE(SrsSeqIsNewer(255, 65535));
        EXPECT_TRUE(SrsSeqIsNewer(255, 65280));

        EXPECT_FALSE(SrsSeqIsNewer(65535, 0));
        EXPECT_FALSE(SrsSeqIsNewer(65280, 0));
        EXPECT_FALSE(SrsSeqIsNewer(65535, 255));
        EXPECT_FALSE(SrsSeqIsNewer(65280, 255));

        EXPECT_TRUE(SrsSeqIsNewer(32768, 0));
        EXPECT_FALSE(SrsSeqIsNewer(0, 32768));
    }

    if (true) {
        EXPECT_FALSE(SrsSeqDistance(1, 1) > 0);
        EXPECT_TRUE(SrsSeqDistance(65535, 65534) > 0);
        EXPECT_TRUE(SrsSeqDistance(1, 0) > 0);
        EXPECT_TRUE(SrsSeqDistance(256, 255) > 0);

        EXPECT_TRUE(SrsSeqDistance(0, 65535) > 0);
        EXPECT_TRUE(SrsSeqDistance(0, 65280) > 0);
        EXPECT_TRUE(SrsSeqDistance(255, 65535) > 0);
        EXPECT_TRUE(SrsSeqDistance(255, 65280) > 0);

        EXPECT_FALSE(SrsSeqDistance(65535, 0) > 0);
        EXPECT_FALSE(SrsSeqDistance(65280, 0) > 0);
        EXPECT_FALSE(SrsSeqDistance(65535, 255) > 0);
        EXPECT_FALSE(SrsSeqDistance(65280, 255) > 0);

        EXPECT_TRUE(SrsSeqDistance(32768, 0) > 0);
        EXPECT_FALSE(SrsSeqDistance(0, 32768) > 0);
    }
}

