/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

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
#include <srs_utest_avc.hpp>

#include <srs_raw_avc.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>

VOID TEST(SrsH264Test, ParseAnnexb)
{
    srs_error_t err;

    // Multiple frames.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 1, 0xd, 0xa, 0xf, 0, 0, 1, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(3, nb_frame);
        EXPECT_EQ((char*)(buf+3), frame);

        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(1, nb_frame);
        EXPECT_EQ((char*)(buf+9), frame);
    }

    // N prefix case, should success.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 0, 1, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(4, nb_frame);
        EXPECT_EQ((char*)(buf+4), frame);
    }

    // No prefix, should fail and return an empty frame.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 2, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_FAILED(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(0, nb_frame);
    }

    // No prefix, should fail and return an empty frame.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 1, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_FAILED(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(0, nb_frame);
    }

    // No prefix, should fail and return an empty frame.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_FAILED(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(0, nb_frame);
    }

    // Normal case, should success.
    if (true) {
        SrsRawH264Stream h;

        uint8_t buf[] = {
            0, 0, 1, 0xd, 0xa, 0xf, 0xa,
        };
        SrsBuffer b((char*)buf, sizeof(buf));

        char* frame = NULL; int nb_frame = 0;
        HELPER_ASSERT_SUCCESS(h.annexb_demux(&b, &frame, &nb_frame));
        EXPECT_EQ(4, nb_frame);
        EXPECT_EQ((char*)(buf+3), frame);
    }
}

VOID TEST(SrsH264Test, SequenceHeader)
{
    srs_error_t err;

    // For PPS demux.
    if (true) {
        SrsRawH264Stream h;
        string pps;
        HELPER_ASSERT_FAILED(h.pps_demux(NULL, 0, pps));
        EXPECT_TRUE(pps.empty());
    }
    if (true) {
        SrsRawH264Stream h;
        string frame = "Hello, world!", pps;
        HELPER_ASSERT_SUCCESS(h.pps_demux((char*)frame.data(), frame.length(), pps));
        EXPECT_STREQ("Hello, world!", pps.c_str());
    }

    // For SPS demux.
    if (true) {
        SrsRawH264Stream h;
        string sps;
        HELPER_ASSERT_SUCCESS(h.sps_demux(NULL, 0, sps));
        EXPECT_TRUE(sps.empty());
    }
    if (true) {
        SrsRawH264Stream h;
        string frame = "Hello, world!", sps;
        HELPER_ASSERT_SUCCESS(h.sps_demux((char*)frame.data(), frame.length(), sps));
        EXPECT_STREQ("Hello, world!", sps.c_str());
    }

    // For PPS.
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0x9,
        };
        EXPECT_FALSE(h.is_pps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xf8,
        };
        EXPECT_FALSE(h.is_pps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xe8,
        };
        EXPECT_TRUE(h.is_pps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            8,
        };
        EXPECT_TRUE(h.is_pps((char*)frame, sizeof(frame)));
    }

    // For SPS.
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0x8,
        };
        EXPECT_FALSE(h.is_sps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xf7,
        };
        EXPECT_FALSE(h.is_sps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            0xe7,
        };
        EXPECT_TRUE(h.is_sps((char*)frame, sizeof(frame)));
    }
    if (true) {
        SrsRawH264Stream h;
        uint8_t frame[] = {
            7,
        };
        EXPECT_TRUE(h.is_sps((char*)frame, sizeof(frame)));
    }
}

