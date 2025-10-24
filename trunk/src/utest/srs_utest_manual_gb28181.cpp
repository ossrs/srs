//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_gb28181.hpp>

#include <sstream>
using namespace std;

#include <srs_app_gb28181.hpp>
#include <srs_app_http_static.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_protocol.hpp>

VOID TEST(KernelPSTest, PsPacketDecodePartialPesHeader)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A PES packet with complete header, but without enough data.
    string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsPacketDecodePartialPesHeader2)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // Ignore if PS header is not integrity.
    context.ctx_->set_detect_ps_integrity(true);

    // A PES packet with complete header, but without enough data.
    string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    // Ignored for not enough bytes.
    EXPECT_EQ(0, b.pos());
}

VOID TEST(KernelPSTest, PsPacketDecodeInvalidPesHeader)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid PES packet.
    string raw = string("\x00\x02\x00\x17\x00\x01\x80\x01", 8);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsPacketDecodeInvalidRtp)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid RTP packet.
    string raw = string("x80\x01", 2);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsPacketDecodeRecover)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    if (true) {
        // A PES packet with complete header, but without enough data.
        string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(1, context.recover_);
    }

    if (true) {
        // Continous data, but should be dropped for recover mode.
        string raw(136 - 8, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(2, context.recover_);
    }

    if (true) {
        // New PES packet with header, but should be dropped for recover mode.
        string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8) + string(136 - 8, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(3, context.recover_);
    }

    if (true) {
        // New pack header, should be ok and quit recover mode.
        string raw = string("\x00\x00\x01\xba\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05", 20) + string("\x00\x00\x01\xc0\x00\x82\x8c\x80\x09\x21\x1a\x1b\xa3\x51\xff\xff\xff\xf8", 18) + string(118, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)1, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);
    }
}

VOID TEST(KernelPSTest, PsRecoverLimit)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid RTP packet.
    for (int i = 0; i < 16; i++) {
        string raw = string("x80\x01", 2);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(i + 1, context.recover_);
    }

    // The last time, should fail.
    string raw = string("x80\x01", 2);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be fail, because exceed max recover limit.
    HELPER_ASSERT_FAILED(context.decode_rtp(&b, 0, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(17, context.recover_);
}

VOID TEST(KernelPSTest, PsRecoverLimit2)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid PES packet.
    for (int i = 0; i < 16; i++) {
        string raw = string("\x00\x02\x00\x17\x00\x01\x80\x01", 8);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(i + 1, context.recover_);
    }

    // The last time, should fail.
    string raw = string("\x00\x02\x00\x17\x00\x01\x80\x01", 8);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be fail, because exceed max recover limit.
    HELPER_ASSERT_FAILED(context.decode_rtp(&b, 0, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(17, context.recover_);
}

VOID TEST(KernelPSTest, PsNoRecoverLargeLength)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with large RTP packet.
    string raw = string(1501, 'x');
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_FAILED(context.decode_rtp(&b, 0, &handler));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsSkipUtilPack)
{
    if (true) {
        string raws[] = {
            string("\x00\x00\x01\xba", 4),
            string("\xaa\x00\x00\x01\xba", 5),
            string("\x00\x00\x00\x01\xba", 5),
            string("\x01\x00\x00\x01\xba", 5),
            string("\xaa\xaa\x00\x00\x01\xba", 6),
            string("\x00\x00\x00\x00\x01\xba", 6),
            string("\x00\x01\x00\x00\x01\xba", 6),
            string("\x01\x00\x00\x00\x01\xba", 6),
            string("\xaa\xaa\xaa\x00\x00\x01\xba", 7),
            string("\x00\x00\x00\x00\x00\x01\xba", 7),
            string("\x00\x00\x01\x00\x00\x01\xba", 7),
            string("\x00\x01\x00\x00\x00\x01\xba", 7),
            string("\x01\x00\x00\x00\x00\x01\xba", 7),
            string("\xaa\xaa\xaa\xaa\x00\x00\x01\xba", 8),
            string("\x00\x00\x00\x00\x00\x00\x01\xba", 8),
            string("\x00\x00\x00\x01\x00\x00\x01\xba", 8),
            string("\x00\x00\x01\x00\x00\x00\x01\xba", 8),
            string("\x00\x01\x00\x00\x00\x00\x01\xba", 8),
            string("\x01\x00\x00\x00\x00\x00\x01\xba", 8),
        };
        for (int i = 0; i < (int)(sizeof(raws) / sizeof(string)); i++) {
            string raw = raws[i];
            SrsBuffer b((char *)raw.data(), raw.length());
            EXPECT_TRUE(srs_skip_util_pack(&b));
        }
    }

    if (true) {
        string raws[] = {
            string(8, '\x00') + string(4, '\xaa'),
            string(7, '\x00') + string(4, '\xaa'),
            string(6, '\x00') + string(4, '\xaa'),
            string(5, '\x00') + string(4, '\xaa'),
            string(4, '\x00') + string(4, '\xaa'),
            string(3, '\x00') + string(4, '\xaa'),
            string(2, '\x00') + string(4, '\xaa'),
            string(1, '\x00') + string(4, '\xaa'),
            string(1, '\x00') + string(3, '\xaa'),
            string(1, '\x00') + string(2, '\xaa'),
            string(1, '\x00') + string(1, '\xaa'),
            string(1, '\x00'),
            string(8, '\x00'),
            string(8, '\x01'),
            string(8, '\xaa'),
            string(7, '\x00'),
            string(7, '\x01'),
            string(7, '\xaa'),
            string(6, '\x00'),
            string(6, '\x01'),
            string(6, '\xaa'),
            string(5, '\x00'),
            string(5, '\x01'),
            string(5, '\xaa'),
            string(4, '\x00'),
            string(4, '\x01'),
            string(4, '\xaa'),
            string(3, '\x00'),
            string(3, '\x01'),
            string(3, '\xaa'),
            string(2, '\x00'),
            string(2, '\x01'),
            string(2, '\xaa'),
            string(1, '\x00'),
            string(1, '\x01'),
            string(1, '\xaa'),
        };
        for (int i = 0; i < (int)(sizeof(raws) / sizeof(string)); i++) {
            string raw = raws[i];
            SrsBuffer b((char *)raw.data(), raw.length());
            EXPECT_FALSE(srs_skip_util_pack(&b));
        }
    }

    if (true) {
        SrsBuffer b(NULL, 0);
        EXPECT_FALSE(srs_skip_util_pack(&b));
    }
}

VOID TEST(KernelPSTest, PsPacketDecodeRegularMessage)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    SrsRtpPacket rtp;
    string raw = string(
        "\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35\x00\x00\x01\xc0"
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40"
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50"
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10"
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2"
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f"
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b"
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c",
        128);
    SrsBuffer b2((char *)raw.data(), raw.length());
    HELPER_ASSERT_SUCCESS(rtp.decode(&b2));

    SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
    SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    SrsTsMessage *m = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, m->sid_);
    EXPECT_EQ(100, m->PES_packet_length_);
}

VOID TEST(KernelPSTest, PsPacketDecodeRegularMessage2)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    SrsRtpPacket rtp;
    string raw = string(
        "\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35\x00\x00\x01\xc0"
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40"
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50"
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10"
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2"
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f"
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b"
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c",
        128);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, &handler));
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    SrsTsMessage *m = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, m->sid_);
    EXPECT_EQ(100, m->PES_packet_length_);
}

VOID TEST(KernelPSTest, PsPacketDecodeRegularMessage3)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    SrsRtpPacket rtp;
    string raw = string(
        "\x00\x00\x01\xc0\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35"
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40"
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50"
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10"
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2"
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f"
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b"
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c",
        128);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 4, &handler));
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    SrsTsMessage *m = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, m->sid_);
    EXPECT_EQ(100, m->PES_packet_length_);
}

VOID TEST(KernelPSTest, PsPacketDecodeInvalidStartCode)
{
    srs_error_t err = srs_success;

    // PS: Enter recover=1, seq=31914, ts=95648400, pt=96, pack=31813, pack-msgs=6, sopm=31913,
    // bytes=[00 02 00 17 00 01 80 01], pos=0, left=96 for err
    // code=4048(GbPsHeader)(Invalid PS header for GB28181) : decode pack : decode : Invalid PS stream 0 0x2 0
    //
    // thread [2378][ha430859]: decode() [src/app/srs_app_gb28181.cpp:1631][errno=35]
    // thread [2378][ha430859]: decode() [src/kernel/srs_kernel_ps.cpp:76][errno=35]
    // thread [2378][ha430859]: do_decode() [src/kernel/srs_kernel_ps.cpp:145][errno=35]

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31813, Time=95648400
    if (true) {
        string raw = string(
                         "\x00\x00\x01\xba"
                         "\x56\x29\xb6\x04\xd4\x01\x09\xc3\x47\xfe\xff\xff\x01\x78\x46\xc7"
                         "\x00\x00\x01\xbb\x00\x12\x84\xe1\xa3\x04\xe1\x7f\xe0\xe0\x80\xc0"
                         "\xc0\x08\xbd\xe0\x80\xbf\xe0\x80\x00\x00\x01\xbc\x00\x5e\xe0\xff"
                         "\x00\x24\x40\x0e\x48\x4b\x01\x00\x16\x9f\x21\xb6\x6b\x77\x00\xff"
                         "\xff\xff\x41\x12\x48\x4b\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09"
                         "\x0a\x0b\x0c\x0d\x0e\x0f\x00\x30\x1b\xe0\x00\x1c\x42\x0e\x07\x10"
                         "\x10\xea\x0a\x00\x05\xa0\x11\x30\x00\x00\x1c\x21\x2a\x0a\x7f\xff"
                         "\x00\x00\x07\x08\x1f\xfe\x40\xb4\x0f\xc0\x00\x0c\x43\x0a\x00\x90"
                         "\xfe\x02\xb1\x13\x01\xf4\x03\xff\x51\x2b\xe5\x9b\x00\x00\x01\xe0"
                         "\x00\x2a\x8c\x80\x0a\x25\x8a\x6d\x81\x35\xff\xff\xff\xff\xfc\x00"
                         "\x00\x00\x01\x67\x4d\x00\x32\x9d\xa8\x0a\x00\x2d\x69\xb8\x08\x08"
                         "\x0a\x00\x00\x03\x00\x02\x00\x00\x03\x00\x65\x08\x00\x00\x01\xe0"
                         "\x00\x0e\x8c\x00\x03\xff\xff\xfc\x00\x00\x00\x01\x68\xee\x3c\x80"
                         "\x00\x00\x01\xe0\x00\x0e\x8c\x00\x02\xff\xfc\x00\x00\x00\x01\x06"
                         "\xe5\x01\xfb\x80\x00\x00\x01\xe0\xff\xc6\x8c\x00\x03\xff\xff\xfd"
                         "\x00\x00\x00\x01\x65\xb8\x00\x00\x03\x02\x98\x24\x13\xff\xf2\x82",
                         1400 - 1140) +
                     string(1140, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)3, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(65472, last->PES_packet_length_);
        ASSERT_EQ(1156, last->payload_->length());
    }

    // Seq 31814 to 31858, 45*1400=63000, left is 65472-1156-63000=1316 bytes in next packet(seq=31859).
    for (int i = 0; i <= 31858 - 31814; i++) {
        string raw(1400, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)3, handler.msgs_.size()); // We don't clear handler, so there must be 3 messages.
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(65472, last->PES_packet_length_);
        ASSERT_EQ(1156 + 1400 * (i + 1), last->payload_->length());
    }
    if (true) {
        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(65472, last->PES_packet_length_);
        ASSERT_EQ(64156, last->payload_->length());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31859, Time=95648400 [TCP segment of a reassembled PDU]
    if (true) {
        string raw = string(1312, 'x') + string(
                                             "\x1a\x67\xe4\x00" /* last 1312+4=1316 bytes for previous video frame */
                                             "\x00\x00\x01\xe0\xff\xc6\x88\x00\x03\xff\xff\xff"
                                             "\x7a\xc3\x59\x8e\x08\x09\x39\x7d\x56\xa5\x97\x2e\xf5\xc6\x7e\x2c"
                                             "\xb8\xd3\x7f\x4b\x57\x6a\xba\x7a\x75\xd0\xb9\x95\x19\x61\x13\xd5"
                                             "\x21\x8c\x88\x62\x62\x4c\xa8\x3c\x0e\x2e\xe6\x2b\x3d\xf0\x9a\x8e"
                                             "\xb3\xbc\xe1\xe7\x52\x79\x4b\x14\xa9\x8e\xf0\x78\x38\xf4\xb6\x27"
                                             "\x62\x4f\x97\x89\x87\xc8\x8f\x6c",
                                             88);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)4, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(65472, last->PES_packet_length_);
        ASSERT_EQ(72, last->payload_->length());
    }

    // Seq 31860 to 31905, 46*1400=64400, left is 65472-72-64400=1000 bytes in next packet(seq=31906).
    for (int i = 0; i <= 31905 - 31860; i++) {
        string raw(1400, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)4, handler.msgs_.size()); // We don't clear handler, so there must be 4 messages.
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(65472, last->PES_packet_length_);
        ASSERT_EQ(72 + 1400 * (i + 1), last->payload_->length());
    }
    if (true) {
        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(65472, last->PES_packet_length_);
        ASSERT_EQ(64472, last->payload_->length());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31906, Time=95648400 [TCP segment of a reassembled PDU]
    if (true) {
        string raw = string(992, 'x') + string("\x28\xa9\x68\x46\x6f\xaf\x11\x9e" /* last 992+8=1000 bytes for previous video frame */
                                               "\x00\x00\x01\xe0\x27\xc2\x88\x00"
                                               "\x03\xff\xff\xfa\x05\xcb\xbc\x6f\x7b\x70\x13\xbc\xc1\xc8\x9a\x7d"
                                               "\x13\x09\x6d\x17\x78\xb7\xaf\x95\x23\xa6\x25\x40\xc0\xdf\x8b\x7e",
                                               48) +
                     string(360, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)5, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(10172, last->PES_packet_length_);
        ASSERT_EQ(388, last->payload_->length());
    }

    // Seq 31907 to 31912, 6*1400=8400, left is 10172-388-8400=1384 bytes in next packet(seq=31913).
    for (int i = 0; i <= 31912 - 31907; i++) {
        string raw(1400, 'x');
        SrsBuffer b((char *)raw.data(), raw.length());

        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)5, handler.msgs_.size()); // We don't clear handler, so there must be 5 messages.
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(10172, last->PES_packet_length_);
        ASSERT_EQ(388 + 1400 * (i + 1), last->payload_->length());
    }
    if (true) {
        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(10172, last->PES_packet_length_);
        ASSERT_EQ(8788, last->payload_->length());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31913, Time=95648400
    if (true) {
        string raw = string(1376, 'x') + string(
                                             "\x02\xf0\x42\x42\x74\xe3\x1c\x20" /* last 1376+8=1384 bytes for previous video frame */
                                             "\x00\x00\x01\xbd\x00\x6a\x8c\x80"
                                             "\x07\x25\x8a\x6d\x81\x35\xff\xf8",
                                             24);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)6, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage *last = context.ctx_->last();
        ASSERT_EQ(96, last->PES_packet_length_);
        ASSERT_EQ(0, last->payload_->length());
    }

    // Seq 31914, 96 bytes
    if (true) {
        string raw = string(
            "\x00\x02\x00\x17\x00\x01\x80\x01\x78\xff\x46\xc7\xe0\xf1\xf0\x50"
            "\x49\x6c\x65\xc2\x19\x2b\xae\x38\xd1\xa7\x08\x00\x82\x60\x16\x39"
            "\xa6\x6b\xa7\x03\x8e\x8d\xff\x3c\xe2\xa9\x80\xac\x09\x06\x60\xc9"
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a"
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a"
            "\xa2\x15\x7a\x9e\x83\x7a\xee\xb1\xd6\x64\xdf\x7e\x11\x9c\xb9\xe9",
            96);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)6, handler.msgs_.size()); // Private Stream is dropped, so there should be still 6 messages.
        EXPECT_EQ(0, context.recover_);
    }
}

VOID TEST(KernelPSTest, PsPacketDecodePartialPayload)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    if (true) {
        string raw = string(
            "\x00\x00\x01\xbd\x00\x6a" /* PES header */
            "\x8c\x80\x07\x25\x8a\x6d\x81\x35\xff\xf8" /* PES header data */,
            16);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)0, handler.msgs_.size()); // Drop Private Stream message.
        EXPECT_EQ(0, context.recover_);
    }

    if (true) {
        string raw = string(
            /* Bellow is PES packet payload, 96 bytes */
            "\x00\x02\x00\x17\x00\x01\x80\x01\x78\xff\x46\xc7\xe0\xf1\xf0\x50"
            "\x49\x6c\x65\xc2\x19\x2b\xae\x38\xd1\xa7\x08\x00\x82\x60\x16\x39"
            "\xa6\x6b\xa7\x03\x8e\x8d\xff\x3c\xe2\xa9\x80\xac\x09\x06\x60\xc9"
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a"
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a"
            "\xa2\x15\x7a\x9e\x83\x7a\xee\xb1\xd6\x64\xdf\x7e\x11\x9c\xb9\xe9",
            96);
        SrsBuffer b((char *)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)0, handler.msgs_.size()); // Drop Private Stream message.
        EXPECT_EQ(0, context.recover_);
    }
}

VOID TEST(KernelPSTest, PsPacketDecodePrivateStream)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    string raw = string(
        "\x00\x00\x01\xbd\x00\x6a"                                       /* PES header */
        "\x8c\x80\x07\x25\x8a\x6d\x81\x35\xff\xf8" /* PES header data */ /* Bellow is PES packet payload, 96 bytes */
        "\x00\x02\x00\x17\x00\x01\x80\x01\x78\xff\x46\xc7\xe0\xf1\xf0\x50"
        "\x49\x6c\x65\xc2\x19\x2b\xae\x38\xd1\xa7\x08\x00\x82\x60\x16\x39"
        "\xa6\x6b\xa7\x03\x8e\x8d\xff\x3c\xe2\xa9\x80\xac\x09\x06\x60\xc9"
        "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a"
        "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a"
        "\xa2\x15\x7a\x9e\x83\x7a\xee\xb1\xd6\x64\xdf\x7e\x11\x9c\xb9\xe9",
        16 + 96);
    SrsBuffer b((char *)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
    ASSERT_EQ((size_t)0, handler.msgs_.size()); // Drop Private Stream message.
    EXPECT_EQ(0, context.recover_);
}

VOID TEST(KernelPSTest, MpegpsQueueFieldNaming)
{
    srs_error_t err = srs_success;

    SrsMpegpsQueue queue;

    // Test that the queue starts with zero counts
    SrsMediaPacket *msg = queue.dequeue();
    EXPECT_TRUE(msg == NULL);

    // Create a mock audio packet
    SrsMediaPacket *audio_msg = new SrsMediaPacket();
    audio_msg->timestamp_ = 1000;
    audio_msg->message_type_ = SrsFrameTypeAudio;

    // Push audio packet
    HELPER_EXPECT_SUCCESS(queue.push(audio_msg));

    // Create a mock video packet
    SrsMediaPacket *video_msg = new SrsMediaPacket();
    video_msg->timestamp_ = 2000;
    video_msg->message_type_ = SrsFrameTypeVideo;

    // Push video packet
    HELPER_EXPECT_SUCCESS(queue.push(video_msg));

    // Should not dequeue yet (need at least 2 of each type)
    msg = queue.dequeue();
    EXPECT_TRUE(msg == NULL);

    // Add more packets to meet the dequeue threshold
    for (int i = 0; i < 3; i++) {
        SrsMediaPacket *audio = new SrsMediaPacket();
        audio->timestamp_ = 3000 + i;
        audio->message_type_ = SrsFrameTypeAudio;
        HELPER_EXPECT_SUCCESS(queue.push(audio));

        SrsMediaPacket *video = new SrsMediaPacket();
        video->timestamp_ = 4000 + i;
        video->message_type_ = SrsFrameTypeVideo;
        HELPER_EXPECT_SUCCESS(queue.push(video));
    }

    // Now should be able to dequeue
    msg = queue.dequeue();
    EXPECT_TRUE(msg != NULL);
    if (msg) {
        EXPECT_EQ(1000, msg->timestamp_);
        srs_freep(msg);
    }
}
