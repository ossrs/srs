//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_kernel2.hpp>

#include <srs_kernel_ps.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_app_utility.hpp>

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
            "\x80\x60\x00\x00\x00\x00\x00\x00\x0b\xeb\xdf\xa1\x00\x00\x01\xba" \
            "\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05" \
            "\x00\x00\x01\xbb\x00\x12\x80\x98\x09\x04\xe1\x7f\xe0\xe0\x80\xc0" \
            "\xc0\x08\xbd\xe0\x80\xbf\xe0\x80\x00\x00\x01\xbc\x00\x5e\xfc\xff" \
            "\x00\x24\x40\x0e\x48\x4b\x01\x00\x16\x9b\xa5\x22\x2e\xf7\x00\xff" \
            "\xff\xff\x41\x12\x48\x4b\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09" \
            "\x0a\x0b\x0c\x0d\x0e\x0f\x00\x30\x1b\xe0\x00\x1c\x42\x0e\x07\x10" \
            "\x10\xea\x02\x80\x01\xe0\x11\x30\x00\x00\x1c\x20\x2a\x0a\x7f\xff" \
            "\x00\x00\x07\x08\x1f\xfe\x50\x3c\x0f\xc0\x00\x0c\x43\x0a\x00\x90" \
            "\xfe\x02\xb1\x13\x01\xf4\x03\xff\xcb\x85\x54\xb9\x00\x00\x01\xe0" \
            "\x00\x26\x8c\x80\x07\x21\x1a\x1b\x93\x25\xff\xfc\x00\x00\x00\x01" \
            "\x67\x4d\x00\x1e\x9d\xa8\x28\x0f\x69\xb8\x08\x08\x0a\x00\x00\x03" \
            "\x00\x02\x00\x00\x03\x00\x65\x08\x00\x00\x01\xe0\x00\x0e\x8c\x00" \
            "\x03\xff\xff\xfc\x00\x00\x00\x01\x68\xee\x3c\x80\x00\x00\x01\xe0" \
            "\x00\x0e\x8c\x00\x02\xff\xfc\x00\x00\x00\x01\x06\xe5\x01\xba\x80" \
            "\x00\x00\x01\xe0\x35\x62\x8c\x00\x02\xff\xf8\x00\x00\x00\x01\x65", 256) + string(1156, 'x');
        if (true) {
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

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
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

        // Bytes continuity for the large video frame, got nothing message yet.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(0, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=9, Time=0
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x09\x00\x00\x00\x00\x0b\xeb\xdf\xa1", 12) + string(1300, 'x')
            + string("\x00\x00\x01\xbd\x00\x6a\x8c\x80\x07\x21\x1a\x1b\x93\x25\xff\xf8" \
                "\x00\x02\x00\x17\x00\x01\x80\x00\x00\xff\xa0\x05\xe0\xf1\xf0\x50" \
                "\x18\x52\xd6\x5c\xa2\x78\x90\x23\xf9\xf6\x64\xba\xc7\x90\x5e\xd3" \
                "\x80\x2f\x29\xad\x06\xee\x14\x62\xec\x6f\x77\xaa\x71\x80\xb3\x50" \
                "\xb8\xd1\x85\x7f\x44\x30\x4f\x44\xfd\xcd\x21\xe6\x55\x36\x08\x6c" \
                "\xb8\xd1\x85\x7f\x44\x30\x4f\x44\xfd\xcd\x21\xe6\x55\x36\x08\x6c" \
                "\xc9\xf6\x5c\x74", 100);
        if (true) {
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

        // There should be one large video message, might be an I frame.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=10, Time=0
    if (true) {
        SrsRtpPacket rtp;
        string raw("\x80\x60\x00\x0a\x00\x00\x00\x00\x0b\xeb\xdf\xa1" \
            "\x57\xb3\xa3\xbc\x16\x2c\x3c\x9e\x69\x89\x48\xa4", 24);
        if (true) {
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

        // There should be a message of private stream, we ignore it, so we won't get it in callback.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(0, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=11, Time=3600
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x0b\x00\x00\x0e\x10\x0b\xeb\xdf\xa1", 12)
            + string("\x00\x00\x01\xc0" \
                "\x00\x82\x8c\x80\x09\x21\x1a\x1b\xa3\x51\xff\xff\xff\xf8\xff\xf9" \
                "\x50\x40\x0e\xdf\xfc\x01\x2c\x2e\x84\x28\x23\x0a\x85\x82\xa2\x40" \
                "\x90\x50\x2c\x14\x0b\x05\x42\x41\x30\x90\x44\x28\x16\x08\x84\x82", 52)
            + string(84, 'x');
        if (true) {
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

        // There should be one audio message.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=12, Time=3600
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x0c\x00\x00\x0e\x10\x0b\xeb\xdf\xa1", 12)
            + string("\x00\x00\x01\xc0" \
                "\x00\x8a\x8c\x80\x09\x21\x1a\x1b\xb3\x7d\xff\xff\xff\xf8\xff\xf9" \
                "\x50\x40\x0f\xdf\xfc\x01\x2c\x2e\x88\x2a\x13\x0a\x09\x82\x41\x10" \
                "\x90\x58\x26\x14\x13\x05\x02\xc2\x10\xa0\x58\x4a\x14\x0a\x85\x02", 52)
            + string(92, 'x');
        if (true) {
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

        // There should be another audio message.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBDFA1, Seq=13, Time=3600
    if (true) {
        SrsRtpPacket rtp;
        string raw = string("\x80\x60\x00\x0d\x00\x00\x0e\x10\x0b\xeb\xdf\xa1", 12)
            + string("\x00\x00\x01\xba" \
                "\x44\x68\x6e\xbd\x14\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x06" \
                "\x00\x00\x01\xe0\x03\x4a\x8c\x80\x08\x21\x1a\x1b\xaf\x45\xff\xff" \
                "\xf8\x00\x00\x00\x01\x61\xe0\x08\xbf\x3c\xb6\x63\x68\x4b\x7f\xea", 52)
            + string(816, 'x');
        if (true) {
            SrsBuffer b((char*)raw.data(), raw.length());
            HELPER_ASSERT_SUCCESS(rtp.decode(&b));
        }

        SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
        SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

        // There should be another audio message.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ(1, handler.msgs_.size());
    }
}

VOID TEST(KernelPSTest, PsPacketHeaderClockDecode)
{
    srs_error_t err = srs_success;

    SrsPsContext context;

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x44\x68\x6e\x4c\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x00686c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x64\x68\x6e\x4c\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x10686c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\x68\x6e\x4c\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18686c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe8\x6e\x4c\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e86c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\x6e\x4c\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e96c992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\x4c\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9ec992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xcc\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9ed992, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdc\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edb92, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x94\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb2, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x9c\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x9e\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x100, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x9f\x01"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x180, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x9f\x11"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x188, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x9f\x19"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x18c, pkt.system_clock_reference_extension_);
    }

    if(true) {
        SrsPsPacket pkt(&context);
        SrsBuffer b((char*)"\x00\x00\x01\xba"/*start*/ "\x74\xe9\xee\xdd\x9f\x1b"/*clock*/ "\x01\x30\x13\xf8"/*mux*/, 14);
        HELPER_ASSERT_SUCCESS(pkt.decode(&b));
        EXPECT_EQ(0x18e9edbb3, pkt.system_clock_reference_base_);
        EXPECT_EQ(0x18d, pkt.system_clock_reference_extension_);
    }
}

VOID TEST(KernelLogTest, LogLevelString)
{
#ifdef SRS_LOG_LEVEL_V2
    EXPECT_STREQ("FORB",    srs_log_level_strings[SrsLogLevelForbidden]);
    EXPECT_STREQ("TRACE",   srs_log_level_strings[SrsLogLevelVerbose]);
    EXPECT_STREQ("DEBUG",   srs_log_level_strings[SrsLogLevelInfo]);
    EXPECT_STREQ("INFO",    srs_log_level_strings[SrsLogLevelTrace]);
    EXPECT_STREQ("WARN",    srs_log_level_strings[SrsLogLevelWarn]);
    EXPECT_STREQ("ERROR",   srs_log_level_strings[SrsLogLevelError]);
    EXPECT_STREQ("OFF",     srs_log_level_strings[SrsLogLevelDisabled]);
#else
    EXPECT_STREQ("Forb",    srs_log_level_strings[SrsLogLevelForbidden]);
    EXPECT_STREQ("Verb",    srs_log_level_strings[SrsLogLevelVerbose]);
    EXPECT_STREQ("Debug",   srs_log_level_strings[SrsLogLevelInfo]);
    EXPECT_STREQ("Trace",   srs_log_level_strings[SrsLogLevelTrace]);
    EXPECT_STREQ("Warn",    srs_log_level_strings[SrsLogLevelWarn]);
    EXPECT_STREQ("Error",   srs_log_level_strings[SrsLogLevelError]);
    EXPECT_STREQ("Off",     srs_log_level_strings[SrsLogLevelDisabled]);
#endif
}

VOID TEST(KernelLogTest, LogLevelStringV2)
{
    EXPECT_EQ(srs_get_log_level("verbose"),      SrsLogLevelVerbose);
    EXPECT_EQ(srs_get_log_level("info"),      SrsLogLevelInfo);
    EXPECT_EQ(srs_get_log_level("trace"),       SrsLogLevelTrace);
    EXPECT_EQ(srs_get_log_level("warn"),       SrsLogLevelWarn);
    EXPECT_EQ(srs_get_log_level("error"),      SrsLogLevelError);
    EXPECT_EQ(srs_get_log_level("off"),        SrsLogLevelDisabled);

    EXPECT_EQ(srs_get_log_level_v2("trace"),   SrsLogLevelVerbose);
    EXPECT_EQ(srs_get_log_level_v2("debug"),   SrsLogLevelInfo);
    EXPECT_EQ(srs_get_log_level_v2("info"),    SrsLogLevelTrace);
    EXPECT_EQ(srs_get_log_level_v2("warn"),    SrsLogLevelWarn);
    EXPECT_EQ(srs_get_log_level_v2("error"),   SrsLogLevelError);
    EXPECT_EQ(srs_get_log_level_v2("off"),     SrsLogLevelDisabled);
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

        HELPER_EXPECT_SUCCESS(f.write((void*) "HelloWorld", 10, NULL));
        EXPECT_EQ(10, f.tellg());

        f.seek2(5);
        EXPECT_EQ(5, f.tellg());

        HELPER_EXPECT_SUCCESS(f.write((void*) "HelloWorld", 10, NULL));
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

        HELPER_EXPECT_SUCCESS(f.write((void*) "HelloWorld", 10, NULL));
        EXPECT_EQ(30, f.tellg());

        HELPER_EXPECT_SUCCESS(f.lseek(0, SEEK_SET, &v));
        EXPECT_EQ(0, v);

        HELPER_EXPECT_SUCCESS(f.write((void*) "HelloWorld", 10, NULL));
        EXPECT_EQ(10, f.tellg());
    }

    SrsFileReader fr;
    HELPER_ASSERT_SUCCESS(fr.open(filename));

    // "HelloWorldWorld\0\0\0\0\0HelloWorld"
    string str;
    HELPER_ASSERT_SUCCESS(srs_ioutil_read_all(&fr, str));
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
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*) "\x17\x01\x00\x00\x12", 5));

        // Verify the frame type, codec id, avc packet type and composition time.
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, f.video->frame_type);
        EXPECT_EQ(SrsVideoCodecIdAVC, f.vcodec->id);
        EXPECT_EQ(SrsVideoAvcFrameTraitNALU, f.video->avc_packet_type);
        EXPECT_EQ(0x12, f.video->cts);
    }

    // For new RTMP enhanced specification, with ext tag header.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*) "\x91hvc1\x00\x00\x12", 8));

        // Verify the frame type, codec id, avc packet type and composition time.
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, f.video->frame_type);
        EXPECT_EQ(SrsVideoCodecIdHEVC, f.vcodec->id);
        EXPECT_EQ(SrsVideoHEVCFrameTraitPacketTypeCodedFrames, f.video->avc_packet_type);
        EXPECT_EQ(0x12, f.video->cts);
    }

    // If packet type is 3, which is coded frame X, the composition time is 0.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_SUCCESS(f.on_video(0, (char*) "\x93hvc1", 5));

        // Verify the frame type, codec id, avc packet type and composition time.
        EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, f.video->frame_type);
        EXPECT_EQ(SrsVideoCodecIdHEVC, f.vcodec->id);
        EXPECT_EQ(SrsVideoHEVCFrameTraitPacketTypeCodedFramesX, f.video->avc_packet_type);
        EXPECT_EQ(0, f.video->cts);
    }

    // Should fail if only 1 byte for ext tag header, should be more bytes for fourcc.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char*) "\x91", 1));
    }

    // Should fail if only 5 bytes for ext tag header, should be more bytes for fourcc.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char*) "\x91hvc1", 5));
    }

    // Should fail if codec id is hvc2 for ext tag header, should be hvc1.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char*) "\x93hvc2", 5));
    }

    // Should fail if codec id is mvc1 for ext tag header, should be hvc1.
    if (true) {
        SrsFormat f;
        HELPER_ASSERT_SUCCESS(f.initialize());
        HELPER_EXPECT_FAILED(f.on_video(0, (char*) "\x93mvc1", 5));
    }
}

VOID TEST(KernelCodecTest, VideoFormatSepcialMProtect_DJI_M30)
{
    srs_error_t err;

    SrsFormat f;
    HELPER_EXPECT_SUCCESS(f.initialize());

    // Frame 80442, the sequence header, wireshark filter:
    //      rtmpt && rtmpt.video.type==1 && rtmpt.video.format==7
    HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)""
        "\x17\x00\x00\x00\x00\x01\x64\x00\x28\xff\xe1\x00\x12\x67\x64\x00" \
        "\x28\xac\xb4\x03\xc0\x11\x34\xa4\x14\x18\x18\x1b\x42\x84\xd4\x01" \
        "\x00\x05\x68\xee\x06\xf2\xc0", 39));

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
    HELPER_EXPECT_SUCCESS(f.on_video(0, (char*)""
        "\x17\x00\x00\x00\x00\x01\x64\x00\x28\xff\xe1\x00\x12\x67\x64\x00" \
        "\x28\xac\xb4\x03\xc0\x11\x34\xa4\x14\x18\x18\x1b\x42\x84\xd4\x01" \
        "\x00\x05\x68\xee\x06\xf2\xc0", 39));

    // Frame 82749
    char data[9];
    memcpy(data, "\x27\x01\x00\x00\x00\x00\x00\x00\x00", sizeof(data));
    HELPER_EXPECT_SUCCESS(f.on_video(0, data, sizeof(data)));
}
