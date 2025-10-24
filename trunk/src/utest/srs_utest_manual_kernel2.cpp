//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_manual_kernel2.hpp>

#include <srs_app_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_rtc_rtcp.hpp>

VOID TEST(KernelPSTest, PsPacketDecodeNormal)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsPsContext context;

    // Payload of GB28181 camera PS stream, the first packet:
    //      PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=0, Time=0
    if (true) {
        SrsRtpPacket rtp;
        string raw = string(
                         "\x80\x60\x00\x00\x00\x00\x00\x00\x0b\xeb\xdf\xa1\x00\x00\x01\xba"
                         "\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05"
                         "\x00\x00\x01\xbb\x00\x12\x80\x98\x09\x04\xe1\x7f\xe0\xe0\x80\xc0"
                         "\xc0\x08\xbd\xe0\x80\xbf\xe0\x80\x00\x00\x01\xbc\x00\x5e\xfc\xff"
                         "\x00\x24\x40\x0e\x48\x4b\x01\x00\x16\x9b\xa5\x22\x2e\xf7\x00\xff"
                         "\xff\xff\x41\x12\x48\x4b\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09"
                         "\x0a\x0b\x0c\x0d\x0e\x0f\x00\x30\x1b\xe0\x00\x1c\x42\x0e\x07\x10"
                         "\x10\xea\x02\x80\x01\xe0\x11\x30\x00\x00\x1c\x20\x2a\x0a\x7f\xff"
                         "\x00\x00\x07\x08\x1f\xfe\x50\x3c\x0f\xc0\x00\x0c\x43\x0a\x00\x90"
                         "\xfe\x02\xb1\x13\x01\xf4\x03\xff\xcb\x85\x54\xb9\x00\x00\x01\xe0"
                         "\x00\x26\x8c\x80\x07\x21\x1a\x1b\x93\x25\xff\xfc\x00\x00\x00\x01"
                         "\x67\x4d\x00\x1e\x9d\xa8\x28\x0f\x69\xb8\x08\x08\x0a\x00\x00\x03"
                         "\x00\x02\x00\x00\x03\x00\x65\x08\x00\x00\x01\xe0\x00\x0e\x8c\x00"
                         "\x03\xff\xff\xfc\x00\x00\x00\x01\x68\xee\x3c\x80\x00\x00\x01\xe0"
                         "\x00\x0e\x8c\x00\x02\xff\xfc\x00\x00\x00\x01\x06\xe5\x01\xba\x80"
                         "\x00\x00\x01\xe0\x35\x62\x8c\x00\x02\xff\xf8\x00\x00\x00\x01\x65",
                         256) +
                     string(1156, 'x');
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // There should be three video messages.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        EXPECT_EQ(3, handler.msgs_.size());
    }

    // We use the first packet bytes to mock the RTP packet seq 1 to 8.
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=1, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=2, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=3, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=4, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=5, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=6, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=7, Time=0
    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=8, Time=0
    for (int i = 0; i < 8; i++) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x01\x00\x00\x00\x00\x0b\xeb\xdf\xa1", 12) + string(1400, 'x');
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // Bytes continuity for the large video frame, got nothing message yet.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(0, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=9, Time=0
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x09\x00\x00\x00\x00\x0b\xeb\xdf\xa1", 12) + string(1300, 'x') + string("\x00\x00\x01\xbd\x00\x6a\x8c\x80\x07\x21\x1a\x1b\x93\x25\xff\xf8"
                                                                                                                 "\x00\x02\x00\x17\x00\x01\x80\x00\x00\xff\xa0\x05\xe0\xf1\xf0\x50"
                                                                                                                 "\x18\x52\xd6\x5c\xa2\x78\x90\x23\xf9\xf6\x64\xba\xc7\x90\x5e\xd3"
                                                                                                                 "\x80\x2f\x29\xad\x06\xee\x14\x62\xec\x6f\x77\xaa\x71\x80\xb3\x50"
                                                                                                                 "\xb8\xd1\x85\x7f\x44\x30\x4f\x44\xfd\xcd\x21\xe6\x55\x36\x08\x6c"
                                                                                                                 "\xb8\xd1\x85\x7f\x44\x30\x4f\x44\xfd\xcd\x21\xe6\x55\x36\x08\x6c"
                                                                                                                 "\xc9\xf6\x5c\x74",
                                                                                                                 100);
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // There should be one large video message, might be an I frame.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=10, Time=0
    if (true) {
        SrsRtpPacket rtp;
        string raw("\x80\x60\x00\x0a\x00\x00\x00\x00\x0b\xeb\xdf\xa1"
                   "\x57\xb3\xa3\xbc\x16\x2c\x3c\x9e\x69\x89\x48\xa4",
                   24);
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // There should be a message of private stream, we ignore it, so we won't get it in callback.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(0, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=11, Time=3600
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x0b\x00\x00\x0e\x10\x0b\xeb\xdf\xa1", 12) + string("\x00\x00\x01\xc0"
                                                                                             "\x00\x82\x8c\x80\x09\x21\x1a\x1b\xa3\x51\xff\xff\xff\xf8\xff\xf9"
                                                                                             "\x50\x40\x0e\xdf\xfc\x01\x2c\x2e\x84\x28\x23\x0a\x85\x82\xa2\x40"
                                                                                             "\x90\x50\x2c\x14\x0b\x05\x42\x41\x30\x90\x44\x28\x16\x08\x84\x82",
                                                                                             52) +
                     string(84, 'x');
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // There should be one audio message.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=12, Time=3600
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x0c\x00\x00\x0e\x10\x0b\xeb\xdf\xa1", 12) + string("\x00\x00\x01\xc0"
                                                                                             "\x00\x8a\x8c\x80\x09\x21\x1a\x1b\xb3\x7d\xff\xff\xff\xf8\xff\xf9"
                                                                                             "\x50\x40\x0f\xdf\xfc\x01\x2c\x2e\x88\x2a\x13\x0a\x09\x82\x41\x10"
                                                                                             "\x90\x58\x26\x14\x13\x05\x02\xc2\x10\xa0\x58\x4a\x14\x0a\x85\x02",
                                                                                             52) +
                     string(92, 'x');
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // There should be another audio message.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=13, Time=3600
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x0d\x00\x00\x0e\x10\x0b\xeb\xdf\xa1", 12) + string("\x00\x00\x01\xba"
                                                                                             "\x44\x68\x6e\xbd\x14\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x06"
                                                                                             "\x00\x00\x01\xe0\x03\x4a\x8c\x80\x08\x21\x1a\x1b\xaf\x45\xff\xff"
                                                                                             "\xf8\x00\x00\x00\x01\x61\xe0\x08\xbf\x3c\xb6\x63\x68\x4b\x7f\xea",
                                                                                             52) +
                     string(816, 'x');
        if (true) {
            SrsBuffer b((char *)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
        SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);

        // There should be another audio message.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }
}

VOID TEST(KernelPSTest, PsPacketHeaderClockDecode)
{
    srs_error_t err = srs_success;

    SrsPsContext context;

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x44\x68\x6e\x4c\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x00686c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x64\x68\x6e\x4c\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x10686c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\x68\x6e\x4c\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18686c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe8\x6e\x4c\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e86c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\x6e\x4c\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e96c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\x4c\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9ec992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xcc\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9ed992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdc\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edb92, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x94\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb2, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x9c\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x9e\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x100, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x9f\x01" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x180, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x9f\x11" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x188, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x9f\x19" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x18c, pkt.system_clock_reference_extension_);
    }

    if (true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char *)"\x00\x00\x01\xba" /*start*/ "\x74\xe9\xee\xdd\x9f\x1b" /*clock*/ "\x01\x30\x13\xf8" /*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x18d, pkt.system_clock_reference_extension_);
    }
}

VOID TEST(KernelLogTest, LogLevelString)
{
#ifdef SRS_LOG_LEVEL_V2
    EXPECT_STREQ("FORB", srs_log_level_strings[SrsLogLevelForbidden]);
    EXPECT_STREQ("TRACE", srs_log_level_strings[SrsLogLevelVerbose]);
    EXPECT_STREQ("DEBUG", srs_log_level_strings[SrsLogLevelInfo]);
    EXPECT_STREQ("INFO", srs_log_level_strings[SrsLogLevelTrace]);
    EXPECT_STREQ("WARN", srs_log_level_strings[SrsLogLevelWarn]);
    EXPECT_STREQ("ERROR", srs_log_level_strings[SrsLogLevelError]);
    EXPECT_STREQ("OFF", srs_log_level_strings[SrsLogLevelDisabled]);
#else
    EXPECT_STREQ("Forb", srs_log_level_strings[SrsLogLevelForbidden]);
    EXPECT_STREQ("Verb", srs_log_level_strings[SrsLogLevelVerbose]);
    EXPECT_STREQ("Debug", srs_log_level_strings[SrsLogLevelInfo]);
    EXPECT_STREQ("Trace", srs_log_level_strings[SrsLogLevelTrace]);
    EXPECT_STREQ("Warn", srs_log_level_strings[SrsLogLevelWarn]);
    EXPECT_STREQ("Error", srs_log_level_strings[SrsLogLevelError]);
    EXPECT_STREQ("Off", srs_log_level_strings[SrsLogLevelDisabled]);
#endif
}

VOID TEST(KernelLogTest, LogLevelStringV2)
{
    EXPECT_EQ(srs_get_log_level("verbose"), SrsLogLevelVerbose);
    EXPECT_EQ(srs_get_log_level("info"), SrsLogLevelInfo);
    EXPECT_EQ(srs_get_log_level("trace"), SrsLogLevelTrace);
    EXPECT_EQ(srs_get_log_level("warn"), SrsLogLevelWarn);
    EXPECT_EQ(srs_get_log_level("error"), SrsLogLevelError);
    EXPECT_EQ(srs_get_log_level("off"), SrsLogLevelDisabled);

    EXPECT_EQ(srs_get_log_level_v2("trace"), SrsLogLevelVerbose);
    EXPECT_EQ(srs_get_log_level_v2("debug"), SrsLogLevelInfo);
    EXPECT_EQ(srs_get_log_level_v2("info"), SrsLogLevelTrace);
    EXPECT_EQ(srs_get_log_level_v2("warn"), SrsLogLevelWarn);
    EXPECT_EQ(srs_get_log_level_v2("error"), SrsLogLevelError);
    EXPECT_EQ(srs_get_log_level_v2("off"), SrsLogLevelDisabled);
}

VOID TEST(KernelFileWriterTest, RealfileTest)
{
    srs_error_t err;

    string filename = _srs_tmp_file_prefix + "test-realfile.log";
    MockFileRemover disposer(filename);

    if (true) {
        SrsFileWriter f;
        HELPER_EXPECT_SUCCESS(f.open(filename));
        EXPECT_TRUE(f.is_open());
        EXPECT_EQ(0, f.tellg());

        HELPER_EXPECT_SUCCESS(f.write((void *)"HelloWorld", 10, NULL));
        EXPECT_EQ(10, f.tellg());

        f.seek2(5);
        EXPECT_EQ(5, f.tellg());

        HELPER_EXPECT_SUCCESS(f.write((void *)"HelloWorld", 10, NULL));
        EXPECT_EQ(15, f.tellg());

        off_t v = 0;
        HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_CUR, &v));
        EXPECT_EQ(15, v);

        HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_SET, &v));
        EXPECT_EQ(0, v);

        HELPER_EXPECT_SUCCESS(f.lseek(10, SEEK_SET, &v));
        EXPECT_EQ(10, v);

        HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_END, &v));
        EXPECT_EQ(15, v);

        // There are 5 bytes empty lagging in file.
        HELPER_EXPECT_SUCCESS(f.lseek(5, SEEK_END, &v));
        EXPECT_EQ(20, v);

        HELPER_EXPECT_SUCCESS(f.write((void *)"HelloWorld", 10, NULL));
        EXPECT_EQ(30, f.tellg());

        HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_SET, &v));
        EXPECT_EQ(0, v);

        HELPER_EXPECT_SUCCESS(f.write((void *)"HelloWorld", 10, NULL));
        EXPECT_EQ(10, f.tellg());
    }

    SrsFileReader fr;
    HELPER_ASSERT_SUCCESS(fr.open(filename));

    // "HelloWorldWorld\0\0\0\0\0HelloWorld"
    string str;
    HELPER_ASSERT_SUCCESS(srs_io_readall(&fr, str));
    EXPECT_STREQ("HelloWorldWorld", str.c_str());
    EXPECT_STREQ("HelloWorld", str.substr(20).c_str());
}

VOID TEST(KernelRTMPExtTest, ExtRTMPTest)
{
    srs_error_t err;

    // For legacy RTMP specification, without ext tag header.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char *)"\x17\x01\x00\x00\x12", 5));

        // Verify the frame type, codec id, avc packet type and composition time.
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, f.video_->frame_type_);
        EXPECT_EQ(SrsVideoCodecIdAVC, f.vcodec_->id_);
        EXPECT_EQ(SrsVideoAvcFrameTraitNALU, f.video_->avc_packet_type_);
        EXPECT_EQ(0x12, f.video_->cts_);
    }

    // For new RTMP enhanced specification, with ext tag header.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char *)"\x91hvc1\x00\x00\x12", 8));

        // Verify the frame type, codec id, avc packet type and composition time.
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, f.video_->frame_type_);
        EXPECT_EQ(SrsVideoCodecIdHEVC, f.vcodec_->id_);
        EXPECT_EQ(SrsVideoHEVCFrameTraitPacketTypeCodedFrames, f.video_->avc_packet_type_);
        EXPECT_EQ(0x12, f.video_->cts_);
    }

    // If packet type is 3, which is coded frame X, the composition time is 0.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char *)"\x93hvc1", 5));

        // Verify the frame type, codec id, avc packet type and composition time.
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, f.video_->frame_type_);
        EXPECT_EQ(SrsVideoCodecIdHEVC, f.vcodec_->id_);
        EXPECT_EQ(SrsVideoHEVCFrameTraitPacketTypeCodedFramesX, f.video_->avc_packet_type_);
        EXPECT_EQ(0, f.video_->cts_);
    }

    // Should fail if only 1 byte for ext tag header, should be more bytes for fourcc.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char *)"\x91", 1));
    }

    // Should fail if only 5 bytes for ext tag header, should be more bytes for fourcc.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char *)"\x91hvc1", 5));
    }

    // Should fail if codec id is hvc2 for ext tag header, should be hvc1.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char *)"\x93hvc2", 5));
    }

    // Should fail if codec id is mvc1 for ext tag header, should be hvc1.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char *)"\x93mvc1", 5));
    }
}

VOID TEST(KernelCodecTest, VideoFormatSepcialMProtect_DJI_M30)
{
    srs_error_t err;

    SrsFormat f;
    HELPER_EXPECT_SUCCESS(f.initialize());

    // Frame 80442, the sequence header, wireshark filter:
    //      rtmpt && rtmpt.video.type==1 && rtmpt.video.format==7
    HELPER_EXPECT_SUCCESS(f.on_video(0, (char *)""
                                                "\x17\x00\x00\x00\x00\x01\x64\x00\x28\xff\xe1\x00\x12\x67\x64\x00"
                                                "\x28\xac\xb4\x03\xc0\x11\x34\xa4\x14\x18\x18\x1b\x42\x84\xd4\x01"
                                                "\x00\x05\x68\xee\x06\xf2\xc0",
                                     39));

    MockProtectedBuffer buffer;
    if (buffer.alloc(9)) {
        EXPECT_TRUE(false) << "mmap failed, errno=" << errno;
        return;
    }

    // Frame 82749
    memcpy(buffer.data_, "\x27\x01\x00\x00\x00\x00\x00\x00\x00", buffer.size_);
    HELPER_EXPECT_SUCCESS(f.on_video(0, buffer.data_, buffer.size_));
}

VOID TEST(KernelCodecTest, VideoFormatSepcialAsan_DJI_M30)
{
    srs_error_t err;

    SrsFormat f;
    HELPER_EXPECT_SUCCESS(f.initialize());

    // Frame 80442, the sequence header, wireshark filter:
    //      rtmpt && rtmpt.video.type==1 && rtmpt.video.format==7
    HELPER_EXPECT_SUCCESS(f.on_video(0, (char *)""
                                                "\x17\x00\x00\x00\x00\x01\x64\x00\x28\xff\xe1\x00\x12\x67\x64\x00"
                                                "\x28\xac\xb4\x03\xc0\x11\x34\xa4\x14\x18\x18\x1b\x42\x84\xd4\x01"
                                                "\x00\x05\x68\xee\x06\xf2\xc0",
                                     39));

    // Frame 82749
    char data[9];
    memcpy(data, "\x27\x01\x00\x00\x00\x00\x00\x00\x00", sizeof(data));
    HELPER_EXPECT_SUCCESS(f.on_video(0, data, sizeof(data)));
}

VOID TEST(KernelCodecTest, VideoFormatRbspSimple)
{
    // |---------------------|----------------------------|
    // |      rbsp           |  nalu with emulation bytes |
    // |---------------------|----------------------------|
    // | 0x00 0x00 0x00      |     0x00 0x00 0x03 0x00    |
    // | 0x00 0x00 0x01      |     0x00 0x00 0x03 0x01    |
    // | 0x00 0x00 0x02      |     0x00 0x00 0x03 0x02    |
    // | 0x00 0x00 0x03      |     0x00 0x00 0x03 0x03    |
    // | 0x00 0x00 0x03 0x04 |     0x00 0x00 0x03 0x04    |
    // |---------------------|----------------------------|
    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03, 0x00};
        vector<uint8_t> expect = {0x00, 0x00, 0x00};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03, 0x01};
        vector<uint8_t> expect = {0x00, 0x00, 0x01};

        SrsBuffer b((char *)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03, 0x02};
        vector<uint8_t> expect = {0x00, 0x00, 0x02};

        SrsBuffer b((char *)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03, 0x03};
        vector<uint8_t> expect = {0x00, 0x00, 0x03};

        SrsBuffer b((char *)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03, 0x04};
        vector<uint8_t> expect = {0x00, 0x00, 0x03, 0x04};

        SrsBuffer b((char *)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03, 0xff};
        vector<uint8_t> expect = {0x00, 0x00, 0x03, 0xff};

        SrsBuffer b((char *)nalu.data(), nalu.size());
        vector<uint8_t> rbsp(nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }
}

VOID TEST(KernelCodecTest, VideoFormatRbspEdge)
{
    if (true) {
        vector<uint8_t> nalu = {0x00, 0x00, 0x03};
        vector<uint8_t> expect = {0x00, 0x00, 0x03};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    if (true) {
        vector<uint8_t> nalu = {0xff, 0x00, 0x00, 0x03};
        vector<uint8_t> expect = {0xff, 0x00, 0x00, 0x03};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x01; v <= 0xff; v++) {
        vector<uint8_t> nalu = {(uint8_t)v, 0x00, 0x00, 0x03};
        vector<uint8_t> expect = {(uint8_t)v, 0x00, 0x00, 0x03};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }
}

VOID TEST(KernelCodecTest, VideoFormatRbspNormal)
{
    for (uint16_t v = 0x01; v <= 0xff; v++) {
        vector<uint8_t> nalu = {0x00, (uint8_t)v, 0x03, 0x00};
        vector<uint8_t> expect = {0x00, (uint8_t)v, 0x03, 0x00};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x01; v <= 0xff; v++) {
        vector<uint8_t> nalu = {(uint8_t)v, 0x00, 0x03, 0x00};
        vector<uint8_t> expect = {(uint8_t)v, 0x00, 0x03, 0x00};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x00; v <= 0xff; v++) {
        vector<uint8_t> nalu = {0x00, 0x00, (uint8_t)v};
        vector<uint8_t> expect = {0x00, 0x00, (uint8_t)v};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x00; v <= 0xff; v++) {
        vector<uint8_t> nalu = {0x00, (uint8_t)v};
        vector<uint8_t> expect = {0x00, (uint8_t)v};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x00; v <= 0xff; v++) {
        vector<uint8_t> nalu = {(uint8_t)v};
        vector<uint8_t> expect = {(uint8_t)v};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x00; v <= 0xff; v++) {
        vector<uint8_t> nalu = {(uint8_t)v, (uint8_t)v};
        vector<uint8_t> expect = {(uint8_t)v, (uint8_t)v};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }

    for (uint16_t v = 0x00; v <= 0xff; v++) {
        vector<uint8_t> nalu = {(uint8_t)v, (uint8_t)v, (uint8_t)v};
        vector<uint8_t> expect = {(uint8_t)v, (uint8_t)v, (uint8_t)v};

        vector<uint8_t> rbsp(nalu.size());
        SrsBuffer b((char *)nalu.data(), nalu.size());
        int nb_rbsp = srs_rbsp_remove_emulation_bytes(&b, rbsp);

        ASSERT_EQ(nb_rbsp, (int)expect.size());
        EXPECT_TRUE(srs_bytes_equal(rbsp.data(), expect.data(), nb_rbsp));
    }
}

VOID TEST(KernelCodecTest, HEVCDuplicatedCode)
{
    EXPECT_NE(ERROR_HEVC_NALU_UEV, ERROR_STREAM_CASTER_HEVC_VPS);
    EXPECT_NE(ERROR_HEVC_NALU_SEV, ERROR_STREAM_CASTER_HEVC_SPS);
}

VOID TEST(KernelMemoryBlockTest, MemoryBlockBasic)
{

    // Test basic construction and destruction
    if (true) {
        SrsMemoryBlock block;
        EXPECT_EQ(0, block.size());
        EXPECT_EQ(NULL, block.payload());
    }

    // Test create with size
    if (true) {
        SrsMemoryBlock block;
        block.create(1024);
        EXPECT_EQ(1024, block.size());
        EXPECT_NE((char *)NULL, block.payload());
    }

    // Test create with data
    if (true) {
        SrsMemoryBlock block;
        char test_data[] = "Hello, World!";
        int test_size = strlen(test_data);

        block.create(test_data, test_size);
        EXPECT_EQ(test_size, block.size());
        EXPECT_NE((char *)NULL, block.payload());
        EXPECT_EQ(0, memcmp(block.payload(), test_data, test_size));
    }

    // Test attach
    if (true) {
        SrsMemoryBlock block;
        char *test_data = new char[100];
        memset(test_data, 0x42, 100);

        block.attach(test_data, 100);
        EXPECT_EQ(100, block.size());
        EXPECT_EQ(test_data, block.payload());

        // Memory will be freed by block destructor
    }
}

VOID TEST(KernelMemoryBlockTest, SharedMemoryBlock)
{

    // Test basic shared memory block usage
    if (true) {
        SrsSharedPtr<SrsMemoryBlock> shared_block(new SrsMemoryBlock());
        shared_block->create(1024);

        EXPECT_EQ(1024, shared_block->size());
        EXPECT_NE((char *)NULL, shared_block->payload());

        // Test sharing
        SrsSharedPtr<SrsMemoryBlock> shared_copy = shared_block;
        EXPECT_EQ(shared_block->payload(), shared_copy->payload());
        EXPECT_EQ(shared_block->size(), shared_copy->size());
    }

    // Test multiple references
    if (true) {
        SrsSharedPtr<SrsMemoryBlock> original(new SrsMemoryBlock());
        char test_data[] = "Shared memory test data";
        original->create(test_data, strlen(test_data));

        // Create multiple references
        SrsSharedPtr<SrsMemoryBlock> copy1 = original;
        SrsSharedPtr<SrsMemoryBlock> copy2 = original;
        SrsSharedPtr<SrsMemoryBlock> copy3 = copy1;

        // All should point to the same memory
        EXPECT_EQ(original->payload(), copy1->payload());
        EXPECT_EQ(original->payload(), copy2->payload());
        EXPECT_EQ(original->payload(), copy3->payload());

        // All should have the same size
        EXPECT_EQ(original->size(), copy1->size());
        EXPECT_EQ(original->size(), copy2->size());
        EXPECT_EQ(original->size(), copy3->size());

        // Verify data integrity
        EXPECT_EQ(0, memcmp(original->payload(), test_data, strlen(test_data)));
        EXPECT_EQ(0, memcmp(copy1->payload(), test_data, strlen(test_data)));
        EXPECT_EQ(0, memcmp(copy2->payload(), test_data, strlen(test_data)));
        EXPECT_EQ(0, memcmp(copy3->payload(), test_data, strlen(test_data)));
    }
}

VOID TEST(KernelBufferTest, SrsBufferConstructorAndBasics)
{
    // Test constructor with valid data
    char data[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    SrsBuffer buf(data, 10);

    // Test basic properties
    EXPECT_EQ(10, buf.size());
    EXPECT_EQ(0, buf.pos());
    EXPECT_EQ(10, buf.left());
    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(data, buf.data());
    EXPECT_EQ(data, buf.head());

    // Test constructor with NULL data
    SrsBuffer null_buf(NULL, 0);
    EXPECT_EQ(0, null_buf.size());
    EXPECT_EQ(0, null_buf.pos());
    EXPECT_EQ(0, null_buf.left());
    EXPECT_TRUE(null_buf.empty());
    EXPECT_EQ(NULL, null_buf.data());
    EXPECT_EQ(NULL, null_buf.head());
}

VOID TEST(KernelBufferTest, SrsBufferCopy)
{
    char data[5] = {0x10, 0x20, 0x30, 0x40, 0x50};
    SrsBuffer buf(data, 5);

    // Move position forward
    buf.read_1bytes();
    buf.read_1bytes();
    EXPECT_EQ(2, buf.pos());

    // Test copy preserves position
    SrsBuffer *copy = buf.copy();
    EXPECT_EQ(5, copy->size());
    EXPECT_EQ(2, copy->pos());
    EXPECT_EQ(3, copy->left());
    EXPECT_EQ(data, copy->data());
    EXPECT_EQ(data + 2, copy->head());

    // Verify copy is independent
    copy->read_1bytes();
    EXPECT_EQ(3, copy->pos());
    EXPECT_EQ(2, buf.pos()); // Original unchanged

    srs_freep(copy);
}

VOID TEST(KernelBufferTest, SrsBufferSizeAndSetSize)
{
    char data[10];
    SrsBuffer buf(data, 10);

    EXPECT_EQ(10, buf.size());

    // Test set_size
    buf.set_size(5);
    EXPECT_EQ(5, buf.size());
    EXPECT_EQ(5, buf.left());

    // Test set_size with larger value
    buf.set_size(15);
    EXPECT_EQ(15, buf.size());
    EXPECT_EQ(15, buf.left());

    // Test set_size with zero
    buf.set_size(0);
    EXPECT_EQ(0, buf.size());
    EXPECT_EQ(0, buf.left());
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferPositionAndHead)
{
    char data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, (char)0x88};
    SrsBuffer buf(data, 8);

    // Initial state
    EXPECT_EQ(0, buf.pos());
    EXPECT_EQ(data, buf.head());

    // After reading some bytes
    buf.read_1bytes();
    EXPECT_EQ(1, buf.pos());
    EXPECT_EQ(data + 1, buf.head());

    buf.read_2bytes();
    EXPECT_EQ(3, buf.pos());
    EXPECT_EQ(data + 3, buf.head());

    // Skip to end
    buf.skip(5);
    EXPECT_EQ(8, buf.pos());
    EXPECT_EQ(data + 8, buf.head());
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferLeftAndEmpty)
{
    char data[6];
    SrsBuffer buf(data, 6);

    // Initial state
    EXPECT_EQ(6, buf.left());
    EXPECT_FALSE(buf.empty());

    // After reading
    buf.read_1bytes();
    EXPECT_EQ(5, buf.left());
    EXPECT_FALSE(buf.empty());

    buf.read_4bytes();
    EXPECT_EQ(1, buf.left());
    EXPECT_FALSE(buf.empty());

    buf.read_1bytes();
    EXPECT_EQ(0, buf.left());
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferRequire)
{
    char data[10];
    SrsBuffer buf(data, 10);

    // Test require with available bytes
    EXPECT_TRUE(buf.require(1));
    EXPECT_TRUE(buf.require(10));
    EXPECT_FALSE(buf.require(11));

    // After consuming some bytes
    buf.read_4bytes();
    EXPECT_TRUE(buf.require(1));
    EXPECT_TRUE(buf.require(6));
    EXPECT_FALSE(buf.require(7));

    // At the end
    buf.read_4bytes();
    buf.read_2bytes();
    EXPECT_FALSE(buf.require(1));
    EXPECT_TRUE(buf.require(0));

    // Test require with zero and negative values
    buf.skip(-10); // Reset to beginning
    EXPECT_TRUE(buf.require(0));
    // Note: According to header comment, require should assert for negative values
    // but we won't test that as it would crash the test
}

VOID TEST(KernelBufferTest, SrsBufferSkipOperations)
{
    char data[10] = {0};
    SrsBuffer buf(data, 10);

    // Test basic properties first
    EXPECT_EQ(0, buf.pos());
    EXPECT_EQ(10, buf.size());
    EXPECT_EQ(data, buf.data());

    // Test simple forward skip
    buf.skip(1);
    EXPECT_EQ(1, buf.pos());
    EXPECT_EQ(9, buf.left());
}

VOID TEST(KernelBufferTest, SrsBufferRead1Bytes)
{
    char data[4] = {0x12, 0x34, 0x56, 0x78};
    SrsBuffer buf(data, 4);

    // Test reading 1-byte values
    EXPECT_EQ(0x12, buf.read_1bytes());
    EXPECT_EQ(1, buf.pos());

    EXPECT_EQ(0x34, buf.read_1bytes());
    EXPECT_EQ(2, buf.pos());

    EXPECT_EQ(0x56, buf.read_1bytes());
    EXPECT_EQ(3, buf.pos());

    EXPECT_EQ(0x78, buf.read_1bytes());
    EXPECT_EQ(4, buf.pos());
    EXPECT_TRUE(buf.empty());

    // Test with signed values
    char signed_data[2] = {(char)0xFF, (char)0x80};
    SrsBuffer signed_buf(signed_data, 2);

    EXPECT_EQ(-1, signed_buf.read_1bytes());
    EXPECT_EQ(-128, signed_buf.read_1bytes());
}

VOID TEST(KernelBufferTest, SrsBufferRead2Bytes)
{
    char data[8] = {0x12, 0x34, 0x56, 0x78, (char)0x9A, (char)0xBC, (char)0xDE, (char)0xF0};
    SrsBuffer buf(data, 8);

    // Test big-endian 2-byte reads
    EXPECT_EQ(0x1234, buf.read_2bytes());
    EXPECT_EQ(2, buf.pos());

    EXPECT_EQ(0x5678, buf.read_2bytes());
    EXPECT_EQ(4, buf.pos());

    // Test little-endian 2-byte reads
    buf.skip(-4); // Reset to beginning
    EXPECT_EQ(0x3412, buf.read_le2bytes());
    EXPECT_EQ(2, buf.pos());

    EXPECT_EQ(0x7856, buf.read_le2bytes());
    EXPECT_EQ(4, buf.pos());

    // Continue with remaining bytes
    EXPECT_EQ((int16_t)0x9ABC, buf.read_2bytes());
    EXPECT_EQ((int16_t)0xDEF0, buf.read_2bytes());
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferRead3Bytes)
{
    char data[9] = {0x12, 0x34, 0x56, 0x78, (char)0x9A, (char)0xBC, (char)0xDE, (char)0xF0, 0x11};
    SrsBuffer buf(data, 9);

    // Test big-endian 3-byte reads
    EXPECT_EQ(0x123456, buf.read_3bytes());
    EXPECT_EQ(3, buf.pos());

    EXPECT_EQ(0x789ABC, buf.read_3bytes());
    EXPECT_EQ(6, buf.pos());

    // Test little-endian 3-byte reads
    buf.skip(-6); // Reset to beginning
    EXPECT_EQ(0x563412, buf.read_le3bytes());
    EXPECT_EQ(3, buf.pos());

    EXPECT_EQ(0xBC9A78, buf.read_le3bytes());
    EXPECT_EQ(6, buf.pos());

    // Read remaining bytes
    EXPECT_EQ(0xDEF011, buf.read_3bytes());
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferRead4Bytes)
{
    char data[12] = {0x12, 0x34, 0x56, 0x78, (char)0x9A, (char)0xBC, (char)0xDE, (char)0xF0,
                     0x11, 0x22, 0x33, 0x44};
    SrsBuffer buf(data, 12);

    // Test big-endian 4-byte reads
    EXPECT_EQ(0x12345678, buf.read_4bytes());
    EXPECT_EQ(4, buf.pos());

    EXPECT_EQ((int32_t)0x9ABCDEF0, buf.read_4bytes());
    EXPECT_EQ(8, buf.pos());

    // Test little-endian 4-byte reads
    buf.skip(-8); // Reset to beginning
    EXPECT_EQ(0x78563412, buf.read_le4bytes());
    EXPECT_EQ(4, buf.pos());

    EXPECT_EQ((int32_t)0xF0DEBC9A, buf.read_le4bytes());
    EXPECT_EQ(8, buf.pos());

    // Read remaining bytes
    EXPECT_EQ(0x11223344, buf.read_4bytes());
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferRead8Bytes)
{
    char data[16] = {0x12, 0x34, 0x56, 0x78, (char)0x9A, (char)0xBC, (char)0xDE, (char)0xF0,
                     0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, (char)0x88};
    SrsBuffer buf(data, 16);

    // Test big-endian 8-byte reads
    EXPECT_EQ(0x123456789ABCDEF0LL, buf.read_8bytes());
    EXPECT_EQ(8, buf.pos());

    // Test little-endian 8-byte reads
    buf.skip(-8); // Reset to beginning
    EXPECT_EQ(0xF0DEBC9A78563412LL, buf.read_le8bytes());
    EXPECT_EQ(8, buf.pos());

    // Read remaining bytes
    EXPECT_EQ(0x1122334455667788LL, buf.read_8bytes());
    EXPECT_TRUE(buf.empty());

    // Test with maximum values
    char max_data[8] = {(char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF,
                        (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF};
    SrsBuffer max_buf(max_data, 8);
    EXPECT_EQ(-1LL, max_buf.read_8bytes());
}

VOID TEST(KernelBufferTest, SrsBufferReadString)
{
    char data[] = "Hello, World! This is a test string.";
    SrsBuffer buf(data, strlen(data));

    // Test reading strings of various lengths
    std::string str1 = buf.read_string(5);
    EXPECT_STREQ("Hello", str1.c_str());
    EXPECT_EQ(5, buf.pos());

    std::string str2 = buf.read_string(2);
    EXPECT_STREQ(", ", str2.c_str());
    EXPECT_EQ(7, buf.pos());

    std::string str3 = buf.read_string(6);
    EXPECT_STREQ("World!", str3.c_str());
    EXPECT_EQ(13, buf.pos());

    // Test reading zero-length string
    std::string empty_str = buf.read_string(0);
    EXPECT_STREQ("", empty_str.c_str());
    EXPECT_EQ(13, buf.pos()); // Position unchanged

    // Test reading remaining string
    int remaining = buf.left();
    std::string remaining_str = buf.read_string(remaining);
    EXPECT_STREQ(" This is a test string.", remaining_str.c_str());
    EXPECT_TRUE(buf.empty());

    // Test with binary data containing null bytes
    char binary_data[6] = {0x41, 0x00, 0x42, 0x00, 0x43, 0x44};
    SrsBuffer binary_buf(binary_data, 6);
    std::string binary_str = binary_buf.read_string(6);
    EXPECT_EQ(6, binary_str.length());
    EXPECT_EQ(0x41, binary_str[0]);
    EXPECT_EQ(0x00, binary_str[1]);
    EXPECT_EQ(0x42, binary_str[2]);
    EXPECT_EQ(0x00, binary_str[3]);
    EXPECT_EQ(0x43, binary_str[4]);
    EXPECT_EQ(0x44, binary_str[5]);
}

VOID TEST(KernelBufferTest, SrsBufferReadBytes)
{
    char data[10] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, (char)0x80, (char)0x90, (char)0xA0};
    SrsBuffer buf(data, 10);

    // Test reading bytes into buffer
    char output[4];
    buf.read_bytes(output, 4);
    EXPECT_EQ(0x10, output[0]);
    EXPECT_EQ(0x20, output[1]);
    EXPECT_EQ(0x30, output[2]);
    EXPECT_EQ(0x40, output[3]);
    EXPECT_EQ(4, buf.pos());

    // Test reading more bytes
    char output2[3];
    buf.read_bytes(output2, 3);
    EXPECT_EQ(0x50, output2[0]);
    EXPECT_EQ(0x60, output2[1]);
    EXPECT_EQ(0x70, output2[2]);
    EXPECT_EQ(7, buf.pos());

    // Test reading zero bytes
    char output3[1] = {(char)0xFF};
    buf.read_bytes(output3, 0);
    EXPECT_EQ((char)0xFF, output3[0]); // Should be unchanged
    EXPECT_EQ(7, buf.pos());           // Position unchanged

    // Test reading remaining bytes
    char output4[3];
    buf.read_bytes(output4, 3);
    EXPECT_EQ((char)0x80, output4[0]);
    EXPECT_EQ((char)0x90, output4[1]);
    EXPECT_EQ((char)0xA0, output4[2]);
    EXPECT_TRUE(buf.empty());
}

VOID TEST(KernelBufferTest, SrsBufferWrite1Bytes)
{
    char data[8];
    SrsBuffer buf(data, 8);

    // Test writing 1-byte values
    buf.write_1bytes(0x12);
    EXPECT_EQ(1, buf.pos());
    EXPECT_EQ(0x12, data[0]);

    buf.write_1bytes(0x34);
    EXPECT_EQ(2, buf.pos());
    EXPECT_EQ(0x34, data[1]);

    // Test writing signed values
    buf.write_1bytes(-1);
    EXPECT_EQ(3, buf.pos());
    EXPECT_EQ((char)0xFF, data[2]);

    buf.write_1bytes(-128);
    EXPECT_EQ(4, buf.pos());
    EXPECT_EQ((char)0x80, data[3]);

    // Test writing maximum positive value
    buf.write_1bytes(127);
    EXPECT_EQ(5, buf.pos());
    EXPECT_EQ(0x7F, data[4]);

    // Verify we can read back what we wrote
    buf.skip(-5); // Reset to beginning
    EXPECT_EQ(0x12, buf.read_1bytes());
    EXPECT_EQ(0x34, buf.read_1bytes());
    EXPECT_EQ(-1, buf.read_1bytes());
    EXPECT_EQ(-128, buf.read_1bytes());
    EXPECT_EQ(127, buf.read_1bytes());
}

VOID TEST(KernelBufferTest, SrsBufferWrite2Bytes)
{
    char data[16];
    SrsBuffer buf(data, 16);

    // Test big-endian 2-byte writes
    buf.write_2bytes(0x1234);
    EXPECT_EQ(2, buf.pos());
    EXPECT_EQ(0x12, data[0]);
    EXPECT_EQ(0x34, data[1]);

    buf.write_2bytes(0x5678);
    EXPECT_EQ(4, buf.pos());
    EXPECT_EQ(0x56, data[2]);
    EXPECT_EQ(0x78, data[3]);

    // Test little-endian 2-byte writes
    buf.write_le2bytes(0x9ABC);
    EXPECT_EQ(6, buf.pos());
    EXPECT_EQ((char)0xBC, data[4]);
    EXPECT_EQ((char)0x9A, data[5]);

    buf.write_le2bytes(0xDEF0);
    EXPECT_EQ(8, buf.pos());
    EXPECT_EQ((char)0xF0, data[6]);
    EXPECT_EQ((char)0xDE, data[7]);

    // Test with negative values
    buf.write_2bytes(-1);
    EXPECT_EQ(10, buf.pos());
    EXPECT_EQ((char)0xFF, data[8]);
    EXPECT_EQ((char)0xFF, data[9]);

    // Verify we can read back what we wrote
    buf.skip(-10); // Reset to beginning
    EXPECT_EQ(0x1234, buf.read_2bytes());
    EXPECT_EQ(0x5678, buf.read_2bytes());
    EXPECT_EQ((int16_t)0x9ABC, buf.read_le2bytes());
    EXPECT_EQ((int16_t)0xDEF0, buf.read_le2bytes());
    EXPECT_EQ(-1, buf.read_2bytes());
}

VOID TEST(KernelBufferTest, SrsBufferWrite3Bytes)
{
    char data[18];
    SrsBuffer buf(data, 18);

    // Test big-endian 3-byte writes
    buf.write_3bytes(0x123456);
    EXPECT_EQ(3, buf.pos());
    EXPECT_EQ(0x12, data[0]);
    EXPECT_EQ(0x34, data[1]);
    EXPECT_EQ(0x56, data[2]);

    buf.write_3bytes(0x789ABC);
    EXPECT_EQ(6, buf.pos());
    EXPECT_EQ(0x78, data[3]);
    EXPECT_EQ((char)0x9A, data[4]);
    EXPECT_EQ((char)0xBC, data[5]);

    // Test little-endian 3-byte writes
    buf.write_le3bytes(0xDEF012);
    EXPECT_EQ(9, buf.pos());
    EXPECT_EQ(0x12, data[6]);
    EXPECT_EQ((char)0xF0, data[7]);
    EXPECT_EQ((char)0xDE, data[8]);

    buf.write_le3bytes(0x345678);
    EXPECT_EQ(12, buf.pos());
    EXPECT_EQ(0x78, data[9]);
    EXPECT_EQ(0x56, data[10]);
    EXPECT_EQ(0x34, data[11]);

    // Test with maximum 3-byte value
    buf.write_3bytes(0xFFFFFF);
    EXPECT_EQ(15, buf.pos());
    EXPECT_EQ((char)0xFF, data[12]);
    EXPECT_EQ((char)0xFF, data[13]);
    EXPECT_EQ((char)0xFF, data[14]);

    // Verify we can read back what we wrote
    buf.skip(-15); // Reset to beginning
    EXPECT_EQ(0x123456, buf.read_3bytes());
    EXPECT_EQ(0x789ABC, buf.read_3bytes());
    EXPECT_EQ(0xDEF012, buf.read_le3bytes());
    EXPECT_EQ(0x345678, buf.read_le3bytes());
    EXPECT_EQ(0xFFFFFF, buf.read_3bytes());
}

VOID TEST(KernelBufferTest, SrsBufferWrite4Bytes)
{
    char data[24];
    SrsBuffer buf(data, 24);

    // Test big-endian 4-byte writes
    buf.write_4bytes(0x12345678);
    EXPECT_EQ(4, buf.pos());
    EXPECT_EQ(0x12, data[0]);
    EXPECT_EQ(0x34, data[1]);
    EXPECT_EQ(0x56, data[2]);
    EXPECT_EQ(0x78, data[3]);

    buf.write_4bytes((int32_t)0x9ABCDEF0);
    EXPECT_EQ(8, buf.pos());
    EXPECT_EQ((char)0x9A, data[4]);
    EXPECT_EQ((char)0xBC, data[5]);
    EXPECT_EQ((char)0xDE, data[6]);
    EXPECT_EQ((char)0xF0, data[7]);

    // Test little-endian 4-byte writes
    buf.write_le4bytes(0x11223344);
    EXPECT_EQ(12, buf.pos());
    EXPECT_EQ(0x44, data[8]);
    EXPECT_EQ(0x33, data[9]);
    EXPECT_EQ(0x22, data[10]);
    EXPECT_EQ(0x11, data[11]);

    buf.write_le4bytes(0x55667788);
    EXPECT_EQ(16, buf.pos());
    EXPECT_EQ((char)0x88, data[12]);
    EXPECT_EQ(0x77, data[13]);
    EXPECT_EQ(0x66, data[14]);
    EXPECT_EQ(0x55, data[15]);

    // Test with negative values
    buf.write_4bytes(-1);
    EXPECT_EQ(20, buf.pos());
    EXPECT_EQ((char)0xFF, data[16]);
    EXPECT_EQ((char)0xFF, data[17]);
    EXPECT_EQ((char)0xFF, data[18]);
    EXPECT_EQ((char)0xFF, data[19]);

    // Verify we can read back what we wrote
    buf.skip(-20); // Reset to beginning
    EXPECT_EQ(0x12345678, buf.read_4bytes());
    EXPECT_EQ((int32_t)0x9ABCDEF0, buf.read_4bytes());
    EXPECT_EQ(0x11223344, buf.read_le4bytes());
    EXPECT_EQ(0x55667788, buf.read_le4bytes());
    EXPECT_EQ(-1, buf.read_4bytes());
}

VOID TEST(KernelBufferTest, SrsBufferWrite8Bytes)
{
    char data[32];
    SrsBuffer buf(data, 32);

    // Test big-endian 8-byte writes
    buf.write_8bytes(0x123456789ABCDEF0LL);
    EXPECT_EQ(8, buf.pos());
    EXPECT_EQ(0x12, data[0]);
    EXPECT_EQ(0x34, data[1]);
    EXPECT_EQ(0x56, data[2]);
    EXPECT_EQ(0x78, data[3]);
    EXPECT_EQ((char)0x9A, data[4]);
    EXPECT_EQ((char)0xBC, data[5]);
    EXPECT_EQ((char)0xDE, data[6]);
    EXPECT_EQ((char)0xF0, data[7]);

    buf.write_8bytes(0x1122334455667788LL);
    EXPECT_EQ(16, buf.pos());
    EXPECT_EQ(0x11, data[8]);
    EXPECT_EQ(0x22, data[9]);
    EXPECT_EQ(0x33, data[10]);
    EXPECT_EQ(0x44, data[11]);
    EXPECT_EQ(0x55, data[12]);
    EXPECT_EQ(0x66, data[13]);
    EXPECT_EQ(0x77, data[14]);
    EXPECT_EQ((char)0x88, data[15]);

    // Test little-endian 8-byte writes
    buf.write_le8bytes(0xAABBCCDDEEFF0011LL);
    EXPECT_EQ(24, buf.pos());
    EXPECT_EQ(0x11, data[16]);
    EXPECT_EQ(0x00, data[17]);
    EXPECT_EQ((char)0xFF, data[18]);
    EXPECT_EQ((char)0xEE, data[19]);
    EXPECT_EQ((char)0xDD, data[20]);
    EXPECT_EQ((char)0xCC, data[21]);
    EXPECT_EQ((char)0xBB, data[22]);
    EXPECT_EQ((char)0xAA, data[23]);

    // Test with negative values
    buf.write_8bytes(-1LL);
    EXPECT_EQ(32, buf.pos());
    for (int i = 24; i < 32; i++) {
        EXPECT_EQ((char)0xFF, data[i]);
    }

    // Verify we can read back what we wrote
    buf.skip(-32); // Reset to beginning
    EXPECT_EQ(0x123456789ABCDEF0LL, buf.read_8bytes());
    EXPECT_EQ(0x1122334455667788LL, buf.read_8bytes());
    EXPECT_EQ((int64_t)0xAABBCCDDEEFF0011LL, buf.read_le8bytes());
    EXPECT_EQ(-1LL, buf.read_8bytes());
}

VOID TEST(KernelBufferTest, SrsBufferWriteString)
{
    char data[64];
    SrsBuffer buf(data, 64);

    // Test writing simple string
    std::string str1 = "Hello";
    buf.write_string(str1);
    EXPECT_EQ(5, buf.pos());
    EXPECT_EQ('H', data[0]);
    EXPECT_EQ('e', data[1]);
    EXPECT_EQ('l', data[2]);
    EXPECT_EQ('l', data[3]);
    EXPECT_EQ('o', data[4]);

    // Test writing string with special characters
    std::string str2 = ", World!";
    buf.write_string(str2);
    EXPECT_EQ(13, buf.pos());
    EXPECT_EQ(',', data[5]);
    EXPECT_EQ(' ', data[6]);
    EXPECT_EQ('W', data[7]);
    EXPECT_EQ('!', data[12]);

    // Test writing empty string
    std::string empty_str = "";
    buf.write_string(empty_str);
    EXPECT_EQ(13, buf.pos()); // Position unchanged

    // Test writing string with null bytes
    std::string binary_str;
    binary_str.push_back(0x41);
    binary_str.push_back(0x00);
    binary_str.push_back(0x42);
    binary_str.push_back(0x00);
    binary_str.push_back(0x43);
    buf.write_string(binary_str);
    EXPECT_EQ(18, buf.pos());
    EXPECT_EQ(0x41, data[13]);
    EXPECT_EQ(0x00, data[14]);
    EXPECT_EQ(0x42, data[15]);
    EXPECT_EQ(0x00, data[16]);
    EXPECT_EQ(0x43, data[17]);

    // Verify we can read back what we wrote
    buf.skip(-18); // Reset to beginning
    std::string read_str1 = buf.read_string(5);
    EXPECT_STREQ("Hello", read_str1.c_str());
    std::string read_str2 = buf.read_string(8);
    EXPECT_STREQ(", World!", read_str2.c_str());
    std::string read_binary = buf.read_string(5);
    EXPECT_EQ(5, read_binary.length());
    EXPECT_EQ(0x41, read_binary[0]);
    EXPECT_EQ(0x00, read_binary[1]);
    EXPECT_EQ(0x42, read_binary[2]);
    EXPECT_EQ(0x00, read_binary[3]);
    EXPECT_EQ(0x43, read_binary[4]);
}

VOID TEST(KernelBufferTest, SrsBufferWriteBytes)
{
    char data[32];
    SrsBuffer buf(data, 32);

    // Test writing byte array
    char input1[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    buf.write_bytes(input1, 6);
    EXPECT_EQ(6, buf.pos());
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(input1[i], data[i]);
    }

    // Test writing another byte array
    char input2[4] = {0x70, (char)0x80, (char)0x90, (char)0xA0};
    buf.write_bytes(input2, 4);
    EXPECT_EQ(10, buf.pos());
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(input2[i], data[6 + i]);
    }

    // Test writing zero bytes
    char input3[1] = {(char)0xFF};
    buf.write_bytes(input3, 0);
    EXPECT_EQ(10, buf.pos()); // Position unchanged

    // Test writing bytes with null values
    char input4[5] = {(char)0xB0, 0x00, (char)0xC0, 0x00, (char)0xD0};
    buf.write_bytes(input4, 5);
    EXPECT_EQ(15, buf.pos());
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(input4[i], data[10 + i]);
    }

    // Verify we can read back what we wrote
    buf.skip(-15); // Reset to beginning
    char output1[6];
    buf.read_bytes(output1, 6);
    for (int i = 0; i < 6; i++) {
        EXPECT_EQ(input1[i], output1[i]);
    }

    char output2[4];
    buf.read_bytes(output2, 4);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(input2[i], output2[i]);
    }

    char output4[5];
    buf.read_bytes(output4, 5);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(input4[i], output4[i]);
    }
}

VOID TEST(KernelBufferTest, SrsBufferEdgeCases)
{
    // Test with very small buffer
    char small_data[1];
    SrsBuffer small_buf(small_data, 1);

    small_buf.write_1bytes(0x42);
    EXPECT_EQ(1, small_buf.pos());
    EXPECT_TRUE(small_buf.empty());

    small_buf.skip(-1);
    EXPECT_EQ(0x42, small_buf.read_1bytes());
    EXPECT_TRUE(small_buf.empty());

    // Test position tracking with mixed operations
    char mixed_data[16];
    SrsBuffer mixed_buf(mixed_data, 16);

    mixed_buf.write_4bytes(0x12345678);
    EXPECT_EQ(4, mixed_buf.pos());

    mixed_buf.skip(-2);
    EXPECT_EQ(2, mixed_buf.pos());
    EXPECT_EQ(0x5678, mixed_buf.read_2bytes());
    EXPECT_EQ(4, mixed_buf.pos());

    mixed_buf.write_2bytes((int16_t)0x9ABC);
    EXPECT_EQ(6, mixed_buf.pos());

    // Verify data integrity
    mixed_buf.skip(-6);
    EXPECT_EQ(0x12345678, mixed_buf.read_4bytes());
    EXPECT_EQ((int16_t)0x9ABC, mixed_buf.read_2bytes());

    // Test boundary conditions
    char boundary_data[8];
    SrsBuffer boundary_buf(boundary_data, 8);

    // Fill buffer completely
    boundary_buf.write_8bytes(0x123456789ABCDEF0LL);
    EXPECT_EQ(8, boundary_buf.pos());
    EXPECT_TRUE(boundary_buf.empty());
    EXPECT_EQ(0, boundary_buf.left());

    // Reset and verify
    boundary_buf.skip(-8);
    EXPECT_EQ(0, boundary_buf.pos());
    EXPECT_FALSE(boundary_buf.empty());
    EXPECT_EQ(8, boundary_buf.left());
    EXPECT_EQ(0x123456789ABCDEF0LL, boundary_buf.read_8bytes());
}

VOID TEST(KernelBitBufferTest, Constructor)
{
    char data[4] = {0x12, 0x34, 0x56, 0x78};
    SrsBuffer buffer(data, 4);
    SrsBitBuffer bb(&buffer);

    // Test initial state
    EXPECT_FALSE(bb.empty());
    EXPECT_EQ(32, bb.left_bits());
}

VOID TEST(KernelBitBufferTest, EmptyBuffer)
{
    SrsBuffer buffer(NULL, 0);
    SrsBitBuffer bb(&buffer);

    EXPECT_TRUE(bb.empty());
    EXPECT_EQ(0, bb.left_bits());
}

VOID TEST(KernelBitBufferTest, ReadBit)
{
    // Test reading individual bits from 0x80 (10000000)
    char data[1] = {(char)0x80};
    SrsBuffer buffer(data, 1);
    SrsBitBuffer bb(&buffer);

    EXPECT_EQ(1, bb.read_bit()); // First bit is 1
    EXPECT_EQ(0, bb.read_bit()); // Second bit is 0
    EXPECT_EQ(0, bb.read_bit()); // Third bit is 0
    EXPECT_EQ(0, bb.read_bit()); // Fourth bit is 0
    EXPECT_EQ(0, bb.read_bit()); // Fifth bit is 0
    EXPECT_EQ(0, bb.read_bit()); // Sixth bit is 0
    EXPECT_EQ(0, bb.read_bit()); // Seventh bit is 0
    EXPECT_EQ(0, bb.read_bit()); // Eighth bit is 0

    EXPECT_TRUE(bb.empty());
    EXPECT_EQ(0, bb.left_bits());
}

VOID TEST(KernelBitBufferTest, ReadBitMultipleBytes)
{
    // Test reading bits across byte boundaries: 0xFF00 (11111111 00000000)
    char data[2] = {(char)0xFF, 0x00};
    SrsBuffer buffer(data, 2);
    SrsBitBuffer bb(&buffer);

    // Read first 8 bits (all 1s)
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(1, bb.read_bit());
    }

    // Read next 8 bits (all 0s)
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(0, bb.read_bit());
    }

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, RequireBits)
{
    char data[4] = {0x12, 0x34, 0x56, 0x78};
    SrsBuffer buffer(data, 4);
    SrsBitBuffer bb(&buffer);

    EXPECT_TRUE(bb.require_bits(1));
    EXPECT_TRUE(bb.require_bits(32));
    EXPECT_FALSE(bb.require_bits(33));
    EXPECT_FALSE(bb.require_bits(-1));

    // After reading some bits
    bb.read_bits(16);
    EXPECT_TRUE(bb.require_bits(16));
    EXPECT_FALSE(bb.require_bits(17));
}

VOID TEST(KernelBitBufferTest, SkipBits)
{
    char data[2] = {0x12, 0x34};
    SrsBuffer buffer(data, 2);
    SrsBitBuffer bb(&buffer);

    EXPECT_EQ(16, bb.left_bits());
    bb.skip_bits(4);
    EXPECT_EQ(12, bb.left_bits());

    bb.skip_bits(8);
    EXPECT_EQ(4, bb.left_bits());

    bb.skip_bits(4);
    EXPECT_EQ(0, bb.left_bits());
    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, ReadBits)
{
    // Test reading 0x12 (00010010) bit by bit and in groups
    char data[1] = {0x12};
    SrsBuffer buffer(data, 1);
    SrsBitBuffer bb(&buffer);

    // Read first 4 bits: 0001 = 1
    int32_t value = bb.read_bits(4);
    EXPECT_EQ(1, value);

    // Read next 4 bits: 0010 = 2
    value = bb.read_bits(4);
    EXPECT_EQ(2, value);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, ReadBitsLargeValue)
{
    // Test reading 0x12345678
    char data[4] = {0x12, 0x34, 0x56, 0x78};
    SrsBuffer buffer(data, 4);
    SrsBitBuffer bb(&buffer);

    int32_t value = bb.read_bits(32);
    EXPECT_EQ(0x12345678, value);
    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, ReadBitsAcrossByteBoundary)
{
    // Test reading bits that span across byte boundaries
    // 0x12 = 00010010, 0x34 = 00110100
    char data[2] = {0x12, 0x34};
    SrsBuffer buffer(data, 2);
    SrsBitBuffer bb(&buffer);

    // Read 12 bits: 000100100011 = 0x123
    int32_t value = bb.read_bits(12);
    EXPECT_EQ(0x123, value);

    // Read remaining 4 bits: 0100 = 4
    value = bb.read_bits(4);
    EXPECT_EQ(4, value);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, Read8Bits)
{
    char data[2] = {0x12, 0x34};
    SrsBuffer buffer(data, 2);
    SrsBitBuffer bb(&buffer);

    // Test fast path (byte-aligned)
    int8_t value = bb.read_8bits();
    EXPECT_EQ(0x12, value);

    value = bb.read_8bits();
    EXPECT_EQ(0x34, value);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, Read8BitsNonAligned)
{
    char data[2] = {0x12, 0x34};
    SrsBuffer buffer(data, 2);
    SrsBitBuffer bb(&buffer);

    // Read 4 bits first to make it non-aligned
    bb.read_bits(4);

    // Now read 8 bits (should use slow path)
    int8_t value = bb.read_8bits();
    EXPECT_EQ(0x23, value); // 0010 0011 from remaining bits

    // Read remaining 4 bits
    int32_t remaining = bb.read_bits(4);
    EXPECT_EQ(4, remaining);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, Read16Bits)
{
    char data[4] = {0x12, 0x34, 0x56, 0x78};
    SrsBuffer buffer(data, 4);
    SrsBitBuffer bb(&buffer);

    // Test fast path (byte-aligned)
    int16_t value = bb.read_16bits();
    EXPECT_EQ(0x1234, value);

    value = bb.read_16bits();
    EXPECT_EQ(0x5678, value);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, Read16BitsNonAligned)
{
    char data[3] = {0x12, 0x34, 0x56};
    SrsBuffer buffer(data, 3);
    SrsBitBuffer bb(&buffer);

    // Read 4 bits first to make it non-aligned
    bb.read_bits(4);

    // Now read 16 bits (should use slow path)
    int16_t value = bb.read_16bits();
    EXPECT_EQ(0x2345, value); // From remaining bits

    // Read remaining 4 bits
    int32_t remaining = bb.read_bits(4);
    EXPECT_EQ(6, remaining);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, Read32Bits)
{
    char data[8] = {0x12, 0x34, 0x56, 0x78, (char)0x9A, (char)0xBC, (char)0xDE, (char)0xF0};
    SrsBuffer buffer(data, 8);
    SrsBitBuffer bb(&buffer);

    // Test fast path (byte-aligned)
    int32_t value = bb.read_32bits();
    EXPECT_EQ(0x12345678, value);

    value = bb.read_32bits();
    EXPECT_EQ((int32_t)0x9ABCDEF0, value);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, Read32BitsNonAligned)
{
    char data[5] = {0x12, 0x34, 0x56, 0x78, (char)0x9A};
    SrsBuffer buffer(data, 5);
    SrsBitBuffer bb(&buffer);

    // Read 4 bits first to make it non-aligned
    bb.read_bits(4);

    // Now read 32 bits (should use slow path)
    int32_t value = bb.read_32bits();
    EXPECT_EQ(0x23456789, value); // From remaining bits

    // Read remaining 4 bits
    int32_t remaining = bb.read_bits(4);
    EXPECT_EQ(0xA, remaining);

    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_BasicValues)
{
    srs_error_t err;

    // Test ue(v) = 0: encoded as "1" (binary)
    if (true) {
        char data[1] = {(char)0x80}; // 10000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(0, (int)value);
    }

    // Test ue(v) = 1: encoded as "010" (binary)
    if (true) {
        char data[1] = {0x40}; // 01000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(1, (int)value);
    }

    // Test ue(v) = 2: encoded as "011" (binary)
    if (true) {
        char data[1] = {0x60}; // 01100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(2, (int)value);
    }

    // Test ue(v) = 3: encoded as "00100" (binary)
    if (true) {
        char data[1] = {0x20}; // 00100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(3, (int)value);
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_LargerValues)
{
    srs_error_t err;

    // Test ue(v) = 6: encoded as "00111" (binary) - 0x38 = 00111000
    if (true) {
        char data[1] = {0x38}; // 00111000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(6, (int)value);
    }

    // Test ue(v) = 7: encoded as "00100" (binary) - 0x10 = 00010000
    if (true) {
        char data[1] = {0x10}; // 00010000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(7, (int)value);
    }

    // Test larger value that spans multiple bytes
    if (true) {
        char data[2] = {0x01, 0x12}; // 00000001 00010010
        SrsBuffer buffer(data, 2);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        // From existing tests: 0x01, 0x12 should give 128 - 1 + 9 = 136
        EXPECT_EQ(136, (int)value);
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_ErrorCases)
{
    srs_error_t err;

    // Test empty buffer
    if (true) {
        SrsBuffer buffer(NULL, 0);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_ue(value));
    }

    // Test overflow case (too many leading zeros)
    if (true) {
        char data[4] = {0x00, 0x00, 0x00, 0x01}; // 31+ leading zeros
        SrsBuffer buffer(data, 4);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_ue(value));
    }

    // Test insufficient data for leadingZeroBits
    if (true) {
        char data[1] = {0x01}; // 00000001 - needs more data
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_ue(value));
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsSE_BasicValues)
{
    srs_error_t err;

    // Test se(v) = 0: same as ue(v) = 0, encoded as "1"
    if (true) {
        char data[1] = {(char)0x80}; // 10000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(0, value);
    }

    // Test se(v) = 1: ue(v) = 1, encoded as "010"
    if (true) {
        char data[1] = {0x40}; // 01000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(1, value);
    }

    // Test se(v) = -1: ue(v) = 2, encoded as "011"
    if (true) {
        char data[1] = {0x60}; // 01100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-1, value);
    }

    // Test se(v) = 2: ue(v) = 3, encoded as "00100"
    if (true) {
        char data[1] = {0x20}; // 00100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(2, value);
    }

    // Test se(v) = -2: ue(v) = 4, encoded as "00101"
    if (true) {
        char data[1] = {0x28}; // 00101000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-2, value);
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsSE_ErrorCases)
{
    srs_error_t err;

    // Test empty buffer
    if (true) {
        SrsBuffer buffer(NULL, 0);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_se(value));
    }

    // Test error propagation from read_bits_ue
    if (true) {
        char data[4] = {0x00, 0x00, 0x00, 0x01}; // Will cause overflow in ue
        SrsBuffer buffer(data, 4);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_se(value));
    }
}

VOID TEST(KernelBitBufferTest, EdgeCases)
{
    // Test reading from single bit buffer
    if (true) {
        char data[1] = {(char)0x80}; // 10000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        EXPECT_EQ(1, bb.read_bit());
        EXPECT_EQ(7, bb.left_bits());
        EXPECT_FALSE(bb.empty());

        // Read remaining bits
        for (int i = 0; i < 7; i++) {
            EXPECT_EQ(0, bb.read_bit());
        }

        EXPECT_TRUE(bb.empty());
        EXPECT_EQ(0, bb.left_bits());
    }

    // Test mixed operations
    if (true) {
        char data[4] = {0x12, 0x34, 0x56, 0x78};
        SrsBuffer buffer(data, 4);
        SrsBitBuffer bb(&buffer);

        // Read some bits
        int32_t bits = bb.read_bits(4);
        EXPECT_EQ(1, bits); // First 4 bits of 0x12

        // Skip some bits
        bb.skip_bits(4);

        // Read a byte (should use slow path since not aligned)
        int8_t byte_val = bb.read_8bits();
        EXPECT_EQ(0x34, byte_val);

        // Read remaining bits
        int32_t remaining = bb.read_bits(16);
        EXPECT_EQ(0x5678, remaining);

        EXPECT_TRUE(bb.empty());
    }
}

VOID TEST(KernelBitBufferTest, LeftBitsCalculation)
{
    char data[3] = {0x12, 0x34, 0x56};
    SrsBuffer buffer(data, 3);
    SrsBitBuffer bb(&buffer);

    EXPECT_EQ(24, bb.left_bits());

    // Read some bits and verify left_bits calculation
    bb.read_bits(5);
    EXPECT_EQ(19, bb.left_bits());

    bb.read_bits(8);
    EXPECT_EQ(11, bb.left_bits());

    bb.read_bits(11);
    EXPECT_EQ(0, bb.left_bits());
    EXPECT_TRUE(bb.empty());
}

VOID TEST(KernelBitBufferTest, RequireBitsEdgeCases)
{
    char data[2] = {0x12, 0x34};
    SrsBuffer buffer(data, 2);
    SrsBitBuffer bb(&buffer);

    // Test boundary conditions
    EXPECT_TRUE(bb.require_bits(0));
    EXPECT_TRUE(bb.require_bits(16));
    EXPECT_FALSE(bb.require_bits(17));

    // After reading partial byte
    bb.read_bits(3);
    EXPECT_TRUE(bb.require_bits(13));
    EXPECT_FALSE(bb.require_bits(14));

    // Test negative input
    EXPECT_FALSE(bb.require_bits(-1));
    EXPECT_FALSE(bb.require_bits(-100));
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_AlgorithmDefinition)
{
    srs_error_t err;

    // Test based on ITU-T H.265 algorithm definition:
    // codeNum = (2^leadingZeroBits) - 1 + read_bits(leadingZeroBits)

    // Test ue(v) = 0: leadingZeroBits = 0, codeNum = (2^0) - 1 + 0 = 0
    // Binary: "1"
    if (true) {
        char data[1] = {(char)0x80}; // 10000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(0, (int)value);
    }

    // Test ue(v) = 1: leadingZeroBits = 1, codeNum = (2^1) - 1 + 0 = 1
    // Binary: "010"
    if (true) {
        char data[1] = {0x40}; // 01000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(1, (int)value);
    }

    // Test ue(v) = 2: leadingZeroBits = 1, codeNum = (2^1) - 1 + 1 = 2
    // Binary: "011"
    if (true) {
        char data[1] = {0x60}; // 01100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(2, (int)value);
    }

    // Test ue(v) = 3: leadingZeroBits = 2, codeNum = (2^2) - 1 + 0 = 3
    // Binary: "00100"
    if (true) {
        char data[1] = {0x20}; // 00100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(3, (int)value);
    }

    // Test ue(v) = 4: leadingZeroBits = 2, codeNum = (2^2) - 1 + 1 = 4
    // Binary: "00101"
    if (true) {
        char data[1] = {0x28}; // 00101000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(4, (int)value);
    }

    // Test ue(v) = 5: leadingZeroBits = 2, codeNum = (2^2) - 1 + 2 = 5
    // Binary: "00110"
    if (true) {
        char data[1] = {0x30}; // 00110000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(5, (int)value);
    }

    // Test ue(v) = 6: leadingZeroBits = 2, codeNum = (2^2) - 1 + 3 = 6
    // Binary: "00111"
    if (true) {
        char data[1] = {0x38}; // 00111000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(6, (int)value);
    }

    // Test ue(v) = 7: Use known working pattern from existing tests
    // Binary: "00010000" = 0x10
    if (true) {
        char data[1] = {0x10}; // 00010000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(7, (int)value);
    }

    // Test ue(v) = 8: Use known working pattern from existing tests
    // Binary: "00010010" = 0x12
    if (true) {
        char data[1] = {0x12}; // 00010010
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(8, (int)value);
    }

    // Test ue(v) = 9: Use known working pattern from existing tests
    // Binary: "00010100" = 0x14
    if (true) {
        char data[1] = {0x14}; // 00010100
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(9, (int)value);
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_LargeValues)
{
    srs_error_t err;

    // Test larger values using known working patterns from existing tests
    // These tests verify the algorithm works for larger values without
    // manually calculating the bit patterns

    // Test a known working large value from existing tests
    if (true) {
        char data[2] = {0x01, 0x12}; // From existing tests: gives 136
        SrsBuffer buffer(data, 2);
        SrsBitBuffer bb(&buffer);

        uint32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(value));
        EXPECT_EQ(136, (int)value); // Known working value
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsSE_AlgorithmDefinition)
{
    srs_error_t err;

    // Test based on ITU-T H.265 algorithm definition:
    // se(v) mapping: if (ue_val & 1) se_val = (ue_val + 1) / 2; else se_val = -(ue_val / 2);

    // Test se(v) = 0: ue(v) = 0, se_val = -(0/2) = 0
    // Binary: "1"
    if (true) {
        char data[1] = {(char)0x80}; // 10000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(0, value);
    }

    // Test se(v) = 1: ue(v) = 1, se_val = (1+1)/2 = 1
    // Binary: "010"
    if (true) {
        char data[1] = {0x40}; // 01000000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(1, value);
    }

    // Test se(v) = -1: ue(v) = 2, se_val = -(2/2) = -1
    // Binary: "011"
    if (true) {
        char data[1] = {0x60}; // 01100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-1, value);
    }

    // Test se(v) = 2: ue(v) = 3, se_val = (3+1)/2 = 2
    // Binary: "00100"
    if (true) {
        char data[1] = {0x20}; // 00100000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(2, value);
    }

    // Test se(v) = -2: ue(v) = 4, se_val = -(4/2) = -2
    // Binary: "00101"
    if (true) {
        char data[1] = {0x28}; // 00101000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-2, value);
    }

    // Test se(v) = 3: ue(v) = 5, se_val = (5+1)/2 = 3
    // Binary: "00110"
    if (true) {
        char data[1] = {0x30}; // 00110000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(3, value);
    }

    // Test se(v) = -3: ue(v) = 6, se_val = -(6/2) = -3
    // Binary: "00111"
    if (true) {
        char data[1] = {0x38}; // 00111000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-3, value);
    }

    // Test se(v) = 4: ue(v) = 7, se_val = (7+1)/2 = 4
    // Use known working pattern: 0x10 gives ue(7)
    if (true) {
        char data[1] = {0x10}; // 00010000
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(4, value);
    }

    // Test se(v) = -4: ue(v) = 8, se_val = -(8/2) = -4
    // Use known working pattern: 0x12 gives ue(8)
    if (true) {
        char data[1] = {0x12}; // 00010010
        SrsBuffer buffer(data, 1);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-4, value);
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsSE_LargeValues)
{
    srs_error_t err;

    // Test larger signed values using known working patterns
    // These tests verify the se() algorithm works for larger values

    // Test a known working large positive value
    // Using ue(136) from existing tests, se_val = (135+1)/2 = 68
    if (true) {
        char data[2] = {0x01, 0x11}; // Pattern that gives ue(135)
        SrsBuffer buffer(data, 2);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(68, value); // (135+1)/2 = 68
    }

    // Test a known working large negative value
    // Using ue(136) from existing tests, se_val = -(136/2) = -68
    if (true) {
        char data[2] = {0x01, 0x12}; // Pattern that gives ue(136)
        SrsBuffer buffer(data, 2);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(value));
        EXPECT_EQ(-68, value); // -(136/2) = -68
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_SE_ErrorPropagation)
{
    srs_error_t err;

    // Test that se() properly propagates errors from ue()
    if (true) {
        SrsBuffer buffer(NULL, 0);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_se(value));
    }

    // Test overflow propagation from ue() to se()
    if (true) {
        char data[4] = {0x00, 0x00, 0x00, 0x01}; // Will cause overflow in ue
        SrsBuffer buffer(data, 4);
        SrsBitBuffer bb(&buffer);

        int32_t value = 999;
        HELPER_EXPECT_FAILED(bb.read_bits_se(value));
    }
}

VOID TEST(KernelBitBufferTest, ReadBitsUE_SE_SequentialReading)
{
    srs_error_t err;

    // Test reading multiple ue/se values from the same buffer
    // Data contains: ue(0)=1, ue(1)=010, ue(2)=011, se(1)=010, se(-1)=011
    // Binary: "1" + "010" + "011" + "010" + "011" = "1010011010011"
    // Padded: "1010011010011000" = 0xA6, 0x98
    if (true) {
        char data[2] = {(char)0xA6, (char)0x98}; // 10100110 10011000
        SrsBuffer buffer(data, 2);
        SrsBitBuffer bb(&buffer);

        uint32_t ue_val = 999;
        int32_t se_val = 999;

        // Read ue(0)
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(ue_val));
        EXPECT_EQ(0, (int)ue_val);

        // Read ue(1)
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(ue_val));
        EXPECT_EQ(1, (int)ue_val);

        // Read ue(2)
        HELPER_EXPECT_SUCCESS(bb.read_bits_ue(ue_val));
        EXPECT_EQ(2, (int)ue_val);

        // Read se(1)
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(se_val));
        EXPECT_EQ(1, se_val);

        // Read se(-1)
        HELPER_EXPECT_SUCCESS(bb.read_bits_se(se_val));
        EXPECT_EQ(-1, se_val);
    }
}
