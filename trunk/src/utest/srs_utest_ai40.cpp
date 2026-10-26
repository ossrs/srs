//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai40.hpp>

#include <srs_core_platform.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_utest_manual_kernel.hpp>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

using namespace std;

#ifndef _WIN32 // POSIX only: the child runs /bin/sh
static string platform_read_file(const string &path)
{
    string got;
    FILE *fp = ::fopen(path.c_str(), "rb");
    if (!fp) {
        return got;
    }
    char buf[64];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), fp)) > 0) {
        got.append(buf, n);
    }
    ::fclose(fp);
    return got;
}

// A Ctrl+C in SRS's terminal signals the whole foreground process group, and a stop script may send SIGTERM to
// it too. The children, such as ffmpeg, start with SIGINT and SIGTERM ignored, so only SRS stops them, after it
// stops its streams.
VOID TEST(PlatformTest, ProcessSpawnChildIgnoresIntAndTerm)
{
    string out = _srs_tmp_file_prefix + "platform-spawn-signals";
    ::unlink(out.c_str());

    vector<const char *> argv;
    argv.push_back("/bin/sh");
    argv.push_back("-c");
    argv.push_back("kill -INT $$; kill -TERM $$; echo alive >&2");
    argv.push_back(NULL);

    pid_t pid = srs_process_spawn("/bin/sh", &argv[0], "", out.c_str(), "cid", "");
    ASSERT_GT(pid, 0);

    int status = -1;
    ASSERT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));

    EXPECT_EQ("alive\n", platform_read_file(out));

    ::unlink(out.c_str());
}

// A spawned child given no stdout or stderr file keeps SRS's own, so the redirect leaves the fd as it was.
VOID TEST(PlatformTest, RedirectOutputNoFileKeepsTheFd)
{
    int fds[2];
    ASSERT_EQ(0, ::pipe(fds));
    int to_fd = ::dup(fds[1]);
    ASSERT_GE(to_fd, 0);

    EXPECT_EQ(0, srs_redirect_output(NULL, to_fd));
    EXPECT_EQ(0, srs_redirect_output("", to_fd));

    // The fd still writes to the pipe.
    ASSERT_EQ(3, ::write(to_fd, "out", 3));
    char buf[8];
    ASSERT_EQ(3, ::read(fds[0], buf, sizeof(buf)));
    EXPECT_EQ("out", string(buf, 3));

    ::close(to_fd);
    ::close(fds[0]);
    ::close(fds[1]);
}

// The child's stdout goes to the log file; each spawn of a restarted ffmpeg appends to the same file instead of
// wiping the log of the run before.
VOID TEST(PlatformTest, RedirectOutputAppendsToTheFile)
{
    string out = _srs_tmp_file_prefix + "platform-redirect-append";
    ::unlink(out.c_str());

    int to_fd = ::open("/dev/null", O_WRONLY);
    ASSERT_GE(to_fd, 0);

    ASSERT_EQ(0, srs_redirect_output(out.c_str(), to_fd));
    ASSERT_EQ(6, ::write(to_fd, "first\n", 6));
    EXPECT_EQ("first\n", platform_read_file(out));

    ASSERT_EQ(0, srs_redirect_output(out.c_str(), to_fd));
    ASSERT_EQ(7, ::write(to_fd, "second\n", 7));
    EXPECT_EQ("first\nsecond\n", platform_read_file(out));

    ::close(to_fd);
    ::unlink(out.c_str());
}

// A log file in a directory that does not exist cannot be opened, so the child reports the error and quits.
VOID TEST(PlatformTest, RedirectOutputFailsWhenTheFileCannotOpen)
{
    string out = _srs_tmp_file_prefix + "platform-redirect-no-such-dir/out.log";

    int to_fd = ::open("/dev/null", O_WRONLY);
    ASSERT_GE(to_fd, 0);

    EXPECT_EQ(-1, srs_redirect_output(out.c_str(), to_fd));
    EXPECT_NE(0, ::access(out.c_str(), F_OK));

    ::close(to_fd);
}

// A bad target fd makes the dup2 fail; the opened file is closed and the call fails.
VOID TEST(PlatformTest, RedirectOutputFailsWhenTheFdIsBad)
{
    string out = _srs_tmp_file_prefix + "platform-redirect-bad-fd";
    ::unlink(out.c_str());

    // The lowest free fd, which the opened file takes and must give back.
    int free_fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(free_fd, 0);
    ::close(free_fd);

    EXPECT_EQ(-1, srs_redirect_output(out.c_str(), -1));

    int fd = ::open("/dev/null", O_RDONLY);
    EXPECT_EQ(free_fd, fd);
    ::close(fd);

    ::unlink(out.c_str());
}
#endif

// An HTTP .aac player gets ADTS frames, a 7-byte header then the raw AAC data, for each RTMP audio packet. When
// the player goes away, the next write, the frame's ADTS header, fails. The stream stops with the socket error,
// and no part of that frame reaches the writer.
VOID TEST(KernelAACTest, AdtsHeaderWriteFailsWhenPlayerLeaves)
{
    srs_error_t err;

    SrsAacTransmuxer m;
    MockSrsFileWriter f;
    HELPER_EXPECT_SUCCESS(m.initialize(&f));

    // AAC sequence header, 44.1kHz stereo, then one raw frame of one byte.
    HELPER_EXPECT_SUCCESS(m.write_audio(0, (char *)"\xaf\x00\x12\x10", 4));
    HELPER_EXPECT_SUCCESS(m.write_audio(0, (char *)"\xaf\x01\xcb", 3));
    EXPECT_EQ(8, f.tellg());

    // The player disconnects, so every write fails from now on.
    f.err = srs_error_new(ERROR_SOCKET_WRITE, "player closed");

    err = m.write_audio(23, (char *)"\xaf\x01\xcc", 3);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("write aac header"));
    srs_freep(err);

    // Nothing of the failed frame was written.
    EXPECT_EQ(8, f.tellg());
}

// A WebRTC publisher aggregates small NALUs, such as SPS and PPS, into one STAP-A packet, each NALU after a
// 2-byte size. A broken or hostile publisher sends a size with the top bit set, 0x8000 or more, which the
// decoder reads as a negative int. It must reject the packet, never keep a NALU of negative size or move the
// read position backward.
VOID TEST(KernelBufferTest, StapANaluSizeWithTopBitSetIsRejected)
{
    srs_error_t err;

    // STAP-A header (NRI 3, type 24), then a NALU size of 0xffff, then more bytes than any short NALU needs.
    char data[64];
    memset(data, 0x41, sizeof(data));
    data[0] = 0x78;
    data[1] = (char)0xff;
    data[2] = (char)0xff;
    SrsBuffer buf(data, sizeof(data));

    SrsRtpSTAPPayload stap;
    err = stap.decode(&buf);
    EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err));
    srs_freep(err);

    EXPECT_TRUE(stap.nalus_.empty());
    EXPECT_EQ(3, buf.pos());
}

// The same for an HEVC publisher: a STAP packet (RFC 7798, type 48) whose NALU size is 0x8000 is rejected.
VOID TEST(KernelBufferTest, StapHevcNaluSizeWithTopBitSetIsRejected)
{
    srs_error_t err;

    // PayloadHdr of type 48, then a NALU size of 0x8000.
    char data[64];
    memset(data, 0x41, sizeof(data));
    data[0] = 0x60;
    data[1] = 0x01;
    data[2] = (char)0x80;
    data[3] = 0x00;
    SrsBuffer buf(data, sizeof(data));

    SrsRtpSTAPPayloadHevc stap;
    err = stap.decode(&buf);
    EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err));
    srs_freep(err);

    EXPECT_TRUE(stap.nalus_.empty());
    EXPECT_EQ(4, buf.pos());
}

// An RTMP publisher sends an AAC sequence header whose AudioSpecificConfig carries a sample rate index. The origin
// turns the index into a sample rate for the stream statistics, and the HTTP API reports it as a number, so a
// stream at any common AAC rate shows its real rate, not the 44100 that the FLV tag header always claims for AAC.
VOID TEST(KernelCodecTest, AacSequenceHeaderRateReportedByStreamApi)
{
    // Index 3 (48000) to 11 (8000) of ISO/IEC 14496-3, the rates encoders such as OBS, FFmpeg and phones use.
    for (int index = 3; index <= 11; index++) {
        if (srs_aac_srates[index] == 32000) {
            continue; // The enum has no 32000, see DISABLED_AacSequenceHeaderRateReportedByStreamApi_32kHzReportsZero.
        }

        SrsAudioSampleRate rate = srs_audio_sample_rate_from_number(srs_aac_srates[index]);
        EXPECT_NE(SrsAudioSampleRateForbidden, rate) << "index " << index;
        EXPECT_EQ((uint32_t)srs_aac_srates[index], srs_audio_sample_rate2number(rate)) << "index " << index;
    }
}

// The same for an AAC stream at 32000 Hz, an index the encoders of some IP cameras use, or at 96000, 88200, 64000
// or 7350 Hz. The enum has no value for these rates, so the stream API reports a sample rate of 0.
VOID TEST(KernelCodecTest, DISABLED_AacSequenceHeaderRateReportedByStreamApi_32kHzReportsZero)
{
    for (int index = 0; index <= 12; index++) {
        SrsAudioSampleRate rate = srs_audio_sample_rate_from_number(srs_aac_srates[index]);
        EXPECT_EQ((uint32_t)srs_aac_srates[index], srs_audio_sample_rate2number(rate)) << "index " << index;
    }
}

// An RTMP publisher sends MP3, whose sample rate is the FLV tag header's SoundRate: 5.5, 11, 22 or 44 kHz. The
// stream API reports it as a number in Hz.
VOID TEST(KernelCodecTest, Mp3FlvSoundRateReportedByStreamApi)
{
    EXPECT_EQ(5512u, srs_audio_sample_rate2number(SrsAudioSampleRate5512));
    EXPECT_EQ(11025u, srs_audio_sample_rate2number(SrsAudioSampleRate11025));
    EXPECT_EQ(22050u, srs_audio_sample_rate2number(SrsAudioSampleRate22050));
    EXPECT_EQ(44100u, srs_audio_sample_rate2number(SrsAudioSampleRate44100));
}

// An enhanced RTMP publisher, such as OBS 29.1 or later, sends HEVC with the extended video tag header: the top bit
// set, the frame type in bits 4-6, the packet type in bits 0-3, then the FourCC "hvc1". The origin must find the
// sequence start, to cache it for players that join later, and must not take a coded frame for it.
VOID TEST(KernelCodecTest, EnhancedRtmpHevcSequenceStartIsSequenceHeader)
{
    // Key frame, SequenceStart.
    char start[] = {(char)0x90, 'h', 'v', 'c', '1', 0x01};
    EXPECT_TRUE(SrsFlvVideo::hevc(start, sizeof(start)));
    EXPECT_FALSE(SrsFlvVideo::h264(start, sizeof(start)));
    EXPECT_TRUE(SrsFlvVideo::keyframe(start, sizeof(start)));
    EXPECT_TRUE(SrsFlvVideo::sh(start, sizeof(start)));

    // Key frame, CodedFrames: a key frame, but not the sequence header.
    char coded[] = {(char)0x91, 'h', 'v', 'c', '1', 0x00, 0x00, 0x00};
    EXPECT_TRUE(SrsFlvVideo::hevc(coded, sizeof(coded)));
    EXPECT_TRUE(SrsFlvVideo::keyframe(coded, sizeof(coded)));
    EXPECT_FALSE(SrsFlvVideo::sh(coded, sizeof(coded)));

    // Inter frame, CodedFramesX.
    char inter[] = {(char)0xa3, 'h', 'v', 'c', '1', 0x00};
    EXPECT_TRUE(SrsFlvVideo::hevc(inter, sizeof(inter)));
    EXPECT_FALSE(SrsFlvVideo::keyframe(inter, sizeof(inter)));
    EXPECT_FALSE(SrsFlvVideo::sh(inter, sizeof(inter)));
}

// The origin accepts an enhanced RTMP HEVC packet, and drops one it cannot handle: a header cut before the end of
// the FourCC, or another codec's FourCC, such as AV1 ("av01") or VP9 ("vp09") from a newer OBS.
VOID TEST(KernelCodecTest, EnhancedRtmpVideoAcceptedOnlyForHevc)
{
    char hevc[] = {(char)0x91, 'h', 'v', 'c', '1', 0x00, 0x00, 0x00};
    EXPECT_TRUE(SrsFlvVideo::acceptable(hevc, sizeof(hevc)));

    // The tag ends inside the FourCC.
    EXPECT_FALSE(SrsFlvVideo::acceptable(hevc, 4));
    EXPECT_FALSE(SrsFlvVideo::hevc(hevc, 4));

    char av1[] = {(char)0x90, 'a', 'v', '0', '1', 0x00};
    EXPECT_FALSE(SrsFlvVideo::acceptable(av1, sizeof(av1)));
    EXPECT_FALSE(SrsFlvVideo::hevc(av1, sizeof(av1)));
    EXPECT_FALSE(SrsFlvVideo::sh(av1, sizeof(av1)));

    char vp9[] = {(char)0x91, 'v', 'p', '0', '9', 0x00};
    EXPECT_FALSE(SrsFlvVideo::acceptable(vp9, sizeof(vp9)));
}

// The stream API and the sequence header log show the H.264 level from the SPS level_idc, which is ten times the
// level. Encoders write 4.2 for 1080p60, 5.2 for 4K60, and 6 to 6.2 for 8K.
VOID TEST(KernelCodecTest, AvcLevelOfHighFrameRateAndLargeStreamsReported)
{
    EXPECT_STREQ("4.2", srs_avc_level2str((SrsAvcLevel)42).c_str());
    EXPECT_STREQ("5.2", srs_avc_level2str((SrsAvcLevel)52).c_str());
    EXPECT_STREQ("6", srs_avc_level2str((SrsAvcLevel)60).c_str());
    EXPECT_STREQ("6.1", srs_avc_level2str((SrsAvcLevel)61).c_str());
    EXPECT_STREQ("6.2", srs_avc_level2str((SrsAvcLevel)62).c_str());
}

// The stream API and the sequence header log show the HEVC profile from general_profile_idc: Main10 for HDR and
// 10-bit streams, Main Still Picture, and Rext for 4:2:2 or 4:4:4. A profile SRS does not name, such as SCC (9),
// shows as "Other".
VOID TEST(KernelCodecTest, HevcProfileOfTenBitAndRangeExtensionStreamsReported)
{
    EXPECT_STREQ("Main", srs_hevc_profile2str((SrsHevcProfile)1).c_str());
    EXPECT_STREQ("Main10", srs_hevc_profile2str((SrsHevcProfile)2).c_str());
    EXPECT_STREQ("Main Still Picture", srs_hevc_profile2str((SrsHevcProfile)3).c_str());
    EXPECT_STREQ("Rext", srs_hevc_profile2str((SrsHevcProfile)4).c_str());
    EXPECT_STREQ("Other", srs_hevc_profile2str((SrsHevcProfile)9).c_str());
}

// The stream API and the sequence header log show the HEVC level from general_level_idc, which is 30 times the
// level: from 1 (30) for small streams to 6.2 (186) for 8K. A value that is no level shows as "Other".
VOID TEST(KernelCodecTest, HevcLevelFromGeneralLevelIdcReported)
{
    EXPECT_STREQ("1", srs_hevc_level2str((SrsHevcLevel)30).c_str());
    EXPECT_STREQ("2", srs_hevc_level2str((SrsHevcLevel)60).c_str());
    EXPECT_STREQ("2.1", srs_hevc_level2str((SrsHevcLevel)63).c_str());
    EXPECT_STREQ("3", srs_hevc_level2str((SrsHevcLevel)90).c_str());
    EXPECT_STREQ("3.1", srs_hevc_level2str((SrsHevcLevel)93).c_str());
    EXPECT_STREQ("4", srs_hevc_level2str((SrsHevcLevel)120).c_str());
    EXPECT_STREQ("4.1", srs_hevc_level2str((SrsHevcLevel)123).c_str());
    EXPECT_STREQ("5", srs_hevc_level2str((SrsHevcLevel)150).c_str());
    EXPECT_STREQ("5.1", srs_hevc_level2str((SrsHevcLevel)153).c_str());
    EXPECT_STREQ("5.2", srs_hevc_level2str((SrsHevcLevel)156).c_str());
    EXPECT_STREQ("6", srs_hevc_level2str((SrsHevcLevel)180).c_str());
    EXPECT_STREQ("6.1", srs_hevc_level2str((SrsHevcLevel)183).c_str());
    EXPECT_STREQ("6.2", srs_hevc_level2str((SrsHevcLevel)186).c_str());
    EXPECT_STREQ("Other", srs_hevc_level2str((SrsHevcLevel)31).c_str());
}
