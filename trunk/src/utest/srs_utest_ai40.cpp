//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai40.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_core_platform.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_format.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#if defined(SRS_BACKTRACE) && defined(__linux)
#include <dlfcn.h>
#endif

#include <map>
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

// An HTTP callback server, such as the origin cluster discovery, answers with an error status and a body larger
// than the 4 MB log buffer. The wrapped error keeps the start of the message, cut to the buffer, so the log still
// says which callback failed.
VOID TEST(KernelErrorTest, WrapKeepsTheStartOfAHugeHookResponse)
{
    string res(5 * 1024 * 1024, 'x');
    srs_error_t err = srs_error_new(ERROR_HTTP_STATUS_INVALID, "status=500");
    err = srs_error_wrap(err, "http: post %s, status=%d, res=%s", "http://127.0.0.1:8085/api/v1/clusters", 500, res.c_str());

    EXPECT_EQ(ERROR_HTTP_STATUS_INVALID, srs_error_code(err));
    EXPECT_EQ((size_t)(4 * 1024 * 1024 - 1), err->msg_.size());
    EXPECT_TRUE(srs_strings_starts_with(err->msg_, "http: post http://127.0.0.1:8085/api/v1/clusters, status=500, res=xxx"));
    EXPECT_EQ(string::npos, err->msg_.find('\0'));
    EXPECT_TRUE(srs_strings_contains(srs_error_summary(err), " : http: post http://127.0.0.1:8085/api/v1/clusters"));
    EXPECT_TRUE(srs_strings_ends_with(srs_error_summary(err), "xxx : status=500"));

    srs_freep(err);
}

// The same huge callback body in a new error, such as the cluster discovery's "load json from %s", should keep the
// start of the message as the wrap does; it loses the whole message instead.
VOID TEST(KernelErrorTest, DISABLED_NewErrorWithHugeHookResponse_MessageIsEmpty)
{
    string res(5 * 1024 * 1024, 'x');
    srs_error_t err = srs_error_new(ERROR_OCLUSTER_DISCOVER, "load json from %s", res.c_str());

    EXPECT_EQ(ERROR_OCLUSTER_DISCOVER, srs_error_code(err));
    EXPECT_TRUE(srs_strings_starts_with(err->msg_, "load json from xxx"));

    srs_freep(err);
}

#if defined(SRS_BACKTRACE) && defined(__linux)
extern void *parse_symbol_offset(char *frame);
extern char *addr2line_format(void *addr, char *symbol, char *buffer, int nn_buffer);
extern int srs_parse_asan_backtrace_symbols(char *symbol, char *out_buf);
extern void srs_backtrace_log();
extern const char *_srs_binary;

// A function of the SRS binary, exported by -rdynamic like every server function, so a backtrace names it.
extern "C" int srs_utest_ai40_backtrace_marker(int v)
{
    int r = v * 3;
    if (r > 100) {
        r -= 7;
    }
    return r;
}

// The offset of an address inside srs_utest_ai40_backtrace_marker in the binary file, as ASan prints it.
static void *kernel_error_marker_file_offset()
{
    Dl_info info;
    if (dladdr((void *)srs_utest_ai40_backtrace_marker, &info) == 0) {
        return NULL;
    }
    return (void *)(uintptr_t)((char *)(void *)srs_utest_ai40_backtrace_marker - (char *)info.dli_fbase + 8);
}

// A failed srs_assert logs the stack before it aborts: the first line names the binary, then one line per frame
// with the function and source line that addr2line finds, so the crash can be read from the log alone.
VOID TEST(KernelErrorTest, AssertBacktraceNamesCallers)
{
    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    srs_backtrace_log();
    _srs_log = original;

    ASSERT_GE(log.errors_.size(), (size_t)3);
    int nn_frames = 0;
    ASSERT_EQ(1, sscanf(log.errors_[0].c_str(), "backtrace %d frames of", &nn_frames));
    EXPECT_EQ((size_t)nn_frames + 1, log.errors_.size());
    EXPECT_TRUE(srs_strings_contains(log.errors_[0], string(" of ") + _srs_binary + " "));

    // The frames of the logger and of its caller are resolved by addr2line (parsed is 1), callee first. A build
    // with ASan may add its backtrace interceptor as the first frame.
    int logger = -1, caller = -1;
    for (int i = 1; i < (int)log.errors_.size(); i++) {
        const string &line = log.errors_[i];
        EXPECT_TRUE(srs_strings_starts_with(line, "#" + srs_strconv_format_int(i - 1) + " ")) << line;
        if (logger < 0 && srs_strings_contains(line, " 1 0x") && srs_strings_contains(line, ": srs_backtrace_log() at srs_kernel_error.cpp:")) {
            logger = i;
        }
        if (caller < 0 && srs_strings_contains(line, " 1 0x") && srs_strings_contains(line, "::TestBody() at srs_utest_ai40.cpp:")) {
            caller = i;
        }
    }
    EXPECT_GT(logger, 0);
    EXPECT_EQ(logger + 1, caller);
}

// A backtrace frame of an exported function, as backtrace_symbols prints it, resolves to the function's offset in
// the binary plus the frame's offset, which addr2line turns into the function name and source line.
VOID TEST(KernelErrorTest, BacktraceFrameOfExportedFunctionResolves)
{
    EXPECT_EQ(9, srs_utest_ai40_backtrace_marker(3));

    char frame[] = "./objs/srs_utest(srs_utest_ai40_backtrace_marker+0x8) [0x55d0c1a2b3c4]";
    void *offset = parse_symbol_offset(frame);
    EXPECT_EQ(kernel_error_marker_file_offset(), offset);

    char buffer[128];
    char *fmt = addr2line_format(offset, frame, buffer, sizeof(buffer));
    ASSERT_EQ(buffer, fmt);
    EXPECT_TRUE(srs_strings_contains(fmt, "srs_utest_ai40_backtrace_marker at srs_utest_ai40.cpp:"));
}

// A frame whose address is not in the SRS binary, such as one in libc resolved against it, gets "??" from addr2line;
// the frame is logged as backtrace_symbols printed it.
VOID TEST(KernelErrorTest, Addr2lineKeepsFrameOutsideTheBinary)
{
    char frame[] = "/lib/libc.so.6(+0x1) [0x7f0000000001]";
    char buffer[128];
    char *fmt = addr2line_format((void *)2, frame, buffer, sizeof(buffer));
    EXPECT_EQ(frame, fmt);
}

// The SRS binary was deleted or replaced after SRS started, as an upgrade does, so addr2line cannot open it; the frame
// is logged as backtrace_symbols printed it.
VOID TEST(KernelErrorTest, Addr2lineKeepsFrameWhenTheBinaryIsGone)
{
    char frame[] = "./objs/srs(srs_utest_ai40_backtrace_marker+0x8) [0x55d0c1a2b3c4]";
    char buffer[128];

    const char *original = _srs_binary;
    _srs_binary = "/tmp/srs-utest-ai40-binary-is-gone";
    char *fmt = addr2line_format(kernel_error_marker_file_offset(), frame, buffer, sizeof(buffer));
    _srs_binary = original;

    EXPECT_EQ(frame, fmt);
}

// SRS started from a path too long for the addr2line command keeps the frame as backtrace_symbols printed it.
VOID TEST(KernelErrorTest, Addr2lineKeepsFrameWhenTheBinaryPathIsTooLong)
{
    char frame[] = "./objs/srs(srs_utest_ai40_backtrace_marker+0x8) [0x55d0c1a2b3c4]";
    char buffer[128];
    string path = "/opt/" + string(600, 'd') + "/objs/srs";

    const char *original = _srs_binary;
    _srs_binary = path.c_str();
    char *fmt = addr2line_format(kernel_error_marker_file_offset(), frame, buffer, sizeof(buffer));
    _srs_binary = original;

    EXPECT_EQ(frame, fmt);
}

// ASan reports a frame it cannot symbolize as the binary plus an offset; when addr2line cannot open the binary either,
// the frame is reported as not resolved.
VOID TEST(KernelErrorTest, AsanFrameNotResolvedWhenTheBinaryIsGone)
{
    char symbol[128];
    snprintf(symbol, sizeof(symbol), "    #0 0x55d0c1a2b3c4  (/srs/objs/srs+%p)", kernel_error_marker_file_offset());
    char out_buf[256] = {0};

    const char *original = _srs_binary;
    _srs_binary = "/tmp/srs-utest-ai40-binary-is-gone";
    int r0 = srs_parse_asan_backtrace_symbols(symbol, out_buf);
    _srs_binary = original;

    EXPECT_EQ(ERROR_BACKTRACE_ADDR2LINE, r0);
}

#ifdef SRS_SANITIZER_LOG
extern void asan_report_callback(const char *str);

// An ASan report goes to the SRS log line by line: plain lines as they are, symbolized frames with the reason they
// were not resolved again, and unsymbolized frames (the binary plus an offset) with what addr2line found.
VOID TEST(KernelErrorTest, AsanReportLogsEachLine)
{
    char frame0[128];
    snprintf(frame0, sizeof(frame0), "    #0 0x55d0c1a2b3c4  (%s+%p)", _srs_binary, kernel_error_marker_file_offset());
    string report = string("==1==ERROR: AddressSanitizer: heap-use-after-free\n") + frame0 + "\n" +
                    "    #1 0x55d0c1a2b3d4 in SrsLiveSource::on_audio(SrsRtmpCommonMessage*) /srs/src/app/srs_app_rtmp_source.cpp:1011";

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    asan_report_callback(report.c_str());
    _srs_log = original;

    ASSERT_EQ((size_t)3, log.errors_.size());
    EXPECT_STREQ("==1==ERROR: AddressSanitizer: heap-use-after-free", log.errors_[0].c_str());
    EXPECT_TRUE(srs_strings_starts_with(log.errors_[1], string(frame0) + ", 0x"));
    EXPECT_FALSE(srs_strings_contains(log.errors_[1], "r0="));
    EXPECT_TRUE(srs_strings_ends_with(log.errors_[2], ":1011, r0=1093"));
}

// The unsymbolized ASan frame should be logged with the function and source line from addr2line, as a failed assert
// logs its frames; only the first 7 characters of the address are kept.
VOID TEST(KernelErrorTest, DISABLED_AsanUnsymbolizedFrame_ResolvedNameCutTo7Chars)
{
    char frame0[128];
    snprintf(frame0, sizeof(frame0), "    #0 0x55d0c1a2b3c4  (%s+%p)", _srs_binary, kernel_error_marker_file_offset());

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    asan_report_callback(frame0);
    _srs_log = original;

    ASSERT_EQ((size_t)1, log.errors_.size());
    EXPECT_TRUE(srs_strings_contains(log.errors_[0], "srs_utest_ai40_backtrace_marker at srs_utest_ai40.cpp:")) << log.errors_[0];
}
#endif
#endif

// An HTTP static file server, such as http_server with dir ./objs/nginx/html, serves a page from disk
// through its default file reader factory: the player gets the bytes of the file on disk.
VOID TEST(KernelFileTest, StaticFileServedFromDiskByDefaultReader)
{
    srs_error_t err;

    string name = "srs-utest-ai40-static-page.html";
    string path = srs_utest_tmp_dir() + "/" + name;
    if (true) {
        SrsFileWriter fw;
        HELPER_ASSERT_SUCCESS(fw.open(path));
        HELPER_ASSERT_SUCCESS(fw.write((void *)"<h1>SRS</h1>", 12, NULL));
    }

    SrsHttpMuxEntry e;
    e.pattern = "/";

    SrsHttpFileServer h(srs_utest_tmp_dir());
    h.entry_ = &e;

    MockResponseWriter w;
    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/" + name, false));

    HELPER_EXPECT_SUCCESS(h.serve_http(&w, &r));
    __MOCK_HTTP_EXPECT_STREQ(200, "<h1>SRS</h1>", w);

    ::unlink(path.c_str());
}

// A publisher to an edge sends a message on chunk stream 100, a 2-byte basic header, which the received
// message keeps as its preferred chunk stream. The edge proxies it to the origin as it is, so the chunk
// stream is chosen by the message type: returns the bytes the edge writes to the origin.
static string flv_edge_proxy_high_cid_message(uint8_t type, const uint8_t *payload, int size)
{
    srs_error_t err = srs_success;

    MockBufferIO publisher;
    uint8_t chunk[] = {
        0x00, 100 - 64,                                  // fmt=0, cid=100 in a 2-byte basic header
        0x00, 0x00, 0x00,                                // timestamp
        0x00, 0x00, (uint8_t)size,                       // payload length
        type,                                            // message type
        0x01, 0x00, 0x00, 0x00,                          // stream id, little endian
    };
    publisher.in_buffer.append((char *)chunk, sizeof(chunk));
    publisher.in_buffer.append((char *)payload, size);

    SrsProtocol edge_in(&publisher);
    SrsRtmpCommonMessage *msg_raw = NULL;
    if ((err = edge_in.recv_message(&msg_raw)) != srs_success) {
        srs_freep(err);
        return "";
    }
    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
    if (msg->header_.prefer_cid_ != 100) {
        return "";
    }

    MockBufferIO origin;
    SrsProtocol edge_out(&origin);
    SrsMediaPacket copy;
    msg->to_msg(&copy);
    if ((err = edge_out.send_and_free_message(copy.copy(), 1)) != srs_success) {
        srs_freep(err);
        return "";
    }

    return string(origin.out_buffer.bytes(), origin.out_buffer.length());
}

// The edge forwards a publisher's control messages, such as a SetBufferLength user control or a set peer
// bandwidth, on the protocol control chunk stream 2, not the publisher's chunk stream 100.
VOID TEST(KernelFlvTest, EdgeProxiesPublisherControlFromHighChunkStream)
{
    // User control, SetBufferLength of stream 1 to 3000ms.
    uint8_t set_buffer_length[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0b, 0xb8};
    string out = flv_edge_proxy_high_cid_message(RTMP_MSG_UserControlMessage, set_buffer_length, sizeof(set_buffer_length));
    ASSERT_EQ(12 + sizeof(set_buffer_length), out.size());
    EXPECT_EQ(0x02, (uint8_t)out[0]); // fmt=0, cid=2
    EXPECT_EQ(RTMP_MSG_UserControlMessage, (uint8_t)out[7]);
    EXPECT_EQ(0, memcmp(out.data() + 12, set_buffer_length, sizeof(set_buffer_length)));

    // Set peer bandwidth, window 2500000, dynamic limit.
    uint8_t set_peer_bandwidth[] = {0x00, 0x26, 0x25, 0xa0, 0x02};
    out = flv_edge_proxy_high_cid_message(RTMP_MSG_SetPeerBandwidth, set_peer_bandwidth, sizeof(set_peer_bandwidth));
    ASSERT_EQ(12 + sizeof(set_peer_bandwidth), out.size());
    EXPECT_EQ(0x02, (uint8_t)out[0]);
    EXPECT_EQ(RTMP_MSG_SetPeerBandwidth, (uint8_t)out[7]);
}

// The edge forwards a publisher's AMF3 data message, such as onMetaData from a Flash encoder, on the
// stream data chunk stream 5, as it does an AMF0 data message.
VOID TEST(KernelFlvTest, EdgeProxiesPublisherAmf3MetadataFromHighChunkStream)
{
    // AMF3 data: a 0x00 format byte, then the AMF0 string "onMetaData" and a null.
    uint8_t amf3_metadata[] = {0x00, 0x02, 0x00, 0x0a, 'o', 'n', 'M', 'e', 't', 'a', 'D', 'a', 't', 'a', 0x05};
    string out = flv_edge_proxy_high_cid_message(RTMP_MSG_AMF3DataMessage, amf3_metadata, sizeof(amf3_metadata));
    ASSERT_EQ(12 + sizeof(amf3_metadata), out.size());
    EXPECT_EQ(0x05, (uint8_t)out[0]); // fmt=0, cid=5
    EXPECT_EQ(RTMP_MSG_AMF3DataMessage, (uint8_t)out[7]);
    EXPECT_EQ(0, memcmp(out.data() + 12, amf3_metadata, sizeof(amf3_metadata)));
}

// A media packet of the given type and size, filled with the FLV tag body of an AVC or AAC frame.
static SrsMediaPacket *flv_media_packet(SrsFrameType type, int64_t timestamp, bool sequence_header, int size)
{
    SrsMediaPacket *msg = new SrsMediaPacket();
    msg->timestamp_ = timestamp;
    msg->message_type_ = type;
    char *data = new char[size];
    memset(data, 0x55, size);
    if (type == SrsFrameTypeVideo) {
        data[0] = 0x17; // keyframe, AVC
        data[1] = sequence_header ? 0x00 : 0x01;
    } else if (type == SrsFrameTypeAudio) {
        data[0] = (char)0xaf; // AAC
        data[1] = sequence_header ? 0x00 : 0x01;
    }
    msg->wrap(data, size);
    return msg;
}

// An HTTP-FLV player joins a stream whose first packets are video frames and only an AAC sequence header,
// so the guessed FLV header has video only (#939). With drop_if_not_match on, the default, the audio
// tags are dropped, in the first batch and later ones, so the player never gets a tag its header lacks.
VOID TEST(KernelFlvTest, HttpFlvGuessedVideoOnlyDropsAudioTags)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    HELPER_ASSERT_SUCCESS(w.open(""));
    SrsFlvStreamEncoder enc;
    HELPER_ASSERT_SUCCESS(enc.initialize(&w, NULL));
    enc.set_guess_has_av(true);
    enc.set_drop_if_not_match(true);

    SrsMediaPacket *msgs[] = {
        flv_media_packet(SrsFrameTypeVideo, 0, true, 10),
        flv_media_packet(SrsFrameTypeAudio, 0, true, 4),
        flv_media_packet(SrsFrameTypeVideo, 40, false, 20),
    };
    HELPER_EXPECT_SUCCESS(enc.write_tags(msgs, 3));

    // FLV header with the video flag only, then the two video tags.
    ASSERT_EQ(13 + (11 + 10 + 4) + (11 + 20 + 4), w.filesize());
    EXPECT_EQ(0x01, (uint8_t)w.data()[4]);
    EXPECT_EQ(SrsFrameTypeVideo, (uint8_t)w.data()[13]);
    EXPECT_EQ(SrsFrameTypeVideo, (uint8_t)w.data()[13 + 25]);

    // The audio frames that come later are dropped too.
    SrsMediaPacket *later[] = {
        flv_media_packet(SrsFrameTypeAudio, 23, false, 100),
        flv_media_packet(SrsFrameTypeVideo, 80, false, 30),
    };
    HELPER_EXPECT_SUCCESS(enc.write_tags(later, 2));
    ASSERT_EQ(13 + 25 + 35 + (11 + 30 + 4), w.filesize());
    EXPECT_EQ(SrsFrameTypeVideo, (uint8_t)w.data()[13 + 25 + 35]);

    for (int i = 0; i < 3; i++) {
        srs_freep(msgs[i]);
    }
    for (int i = 0; i < 2; i++) {
        srs_freep(later[i]);
    }
}

// An HTTP-FLV mount for an audio-only stream, with has_video off and guess_has_av off, gets an FLV header
// with audio only. With drop_if_not_match on, a video frame the publisher sends anyway is dropped,
// whether it comes alone or in a batch.
VOID TEST(KernelFlvTest, HttpFlvHasVideoOffDropsVideoTags)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    HELPER_ASSERT_SUCCESS(w.open(""));
    SrsFlvStreamEncoder enc;
    HELPER_ASSERT_SUCCESS(enc.initialize(&w, NULL));
    enc.set_guess_has_av(false);
    enc.set_has_audio(true);
    enc.set_has_video(false);
    enc.set_drop_if_not_match(true);

    char avc[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x65};
    HELPER_EXPECT_SUCCESS(enc.write_video(0, avc, sizeof(avc)));
    ASSERT_EQ(13, w.filesize());
    EXPECT_EQ(0x04, (uint8_t)w.data()[4]);

    SrsMediaPacket *msgs[] = {
        flv_media_packet(SrsFrameTypeVideo, 40, false, 20),
        flv_media_packet(SrsFrameTypeAudio, 23, false, 8),
    };
    HELPER_EXPECT_SUCCESS(enc.write_tags(msgs, 2));
    ASSERT_EQ(13 + (11 + 8 + 4), w.filesize());
    EXPECT_EQ(SrsFrameTypeAudio, (uint8_t)w.data()[13]);

    for (int i = 0; i < 2; i++) {
        srs_freep(msgs[i]);
    }
}

// An HTTP-FLV mount for a video-only stream, with has_audio off and guess_has_av off, gets an FLV header
// with video only. With drop_if_not_match on, an audio frame is dropped and the video frame is written.
VOID TEST(KernelFlvTest, HttpFlvHasAudioOffDropsAudioTags)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    HELPER_ASSERT_SUCCESS(w.open(""));
    SrsFlvStreamEncoder enc;
    HELPER_ASSERT_SUCCESS(enc.initialize(&w, NULL));
    enc.set_guess_has_av(false);
    enc.set_has_audio(false);
    enc.set_has_video(true);
    enc.set_drop_if_not_match(true);

    char aac[] = {(char)0xaf, 0x01, 0x21, 0x10, 0x04, 0x60};
    HELPER_EXPECT_SUCCESS(enc.write_audio(0, aac, sizeof(aac)));
    ASSERT_EQ(13, w.filesize());
    EXPECT_EQ(0x01, (uint8_t)w.data()[4]);

    char avc[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x65};
    HELPER_EXPECT_SUCCESS(enc.write_video(0, avc, sizeof(avc)));
    ASSERT_EQ(13 + (11 + 6 + 4), w.filesize());
    EXPECT_EQ(SrsFrameTypeVideo, (uint8_t)w.data()[13]);
}

// State Threads returns NULL when it cannot map the stack of a new coroutine, such as when the
// process is out of memory or over its address space limit.
static void *hourglass_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// The server and every source manager start a 1s hourglass for their periodic jobs, such as the
// stats and heartbeat ticks. When no coroutine can be created for it, start fails with the ST error,
// so the server stops starting up instead of running without its timer, and no tick is ever fired.
VOID TEST(KernelHourglassTest, ServerTimerFailsToStartWhenNoCoroutineCanBeCreated)
{
    srs_error_t err = srs_success;

    MockSrsHourGlass handler;
    SrsHourGlass hourglass("srs", &handler, 1 * SRS_UTIME_SECONDS);
    hourglass.assemble();
    HELPER_EXPECT_SUCCESS(hourglass.tick(2, 3 * SRS_UTIME_SECONDS));

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)hourglass_st_thread_create_out_of_memory;
    err = hourglass.start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("start timer") != string::npos);
    srs_freep(err);

    // A started hourglass fires every due tick in its first round, as soon as this test yields.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(handler.events_.empty());
}

// The shared 20ms, 100ms, 1s and 5s timers are fast timers built by the kernel factory at startup.
// When no coroutine can be created for one, start fails with the ST error and no subscriber, such as
// the clock monitor or an RTC connection, is ever called.
VOID TEST(KernelHourglassTest, SharedFastTimerFailsToStartWhenNoCoroutineCanBeCreated)
{
    srs_error_t err = srs_success;

    ISrsFastTimer *timer = _srs_kernel_factory->create_fast_timer("shared", 20 * SRS_UTIME_MILLISECONDS);
    SrsUniquePtr<ISrsFastTimer> guard(timer);

    MockSrsFastTimer handler;
    timer->subscribe(&handler);

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)hourglass_st_thread_create_out_of_memory;
    err = timer->start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("start timer") != string::npos);
    srs_freep(err);

    // A started timer notifies its subscribers in its first round, as soon as this test yields.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(handler.timer_calls_.empty());
}

// At startup srs_global_initialize() starts the four shared timers in order. When one cannot start,
// the error names that timer for the operator, the timers after it are not started, and the clock
// monitor is not subscribed, so the server exits instead of running with a timer missing.
VOID TEST(KernelHourglassTest, SharedTimerStartupNamesTheTimerThatFailed)
{
    const char *names[] = {"start timer20ms", "start timer100ms", "start timer1s", "start timer5s"};

    for (int failed = 0; failed < 4; failed++) {
        MockFastTimerForSharedTimer timers[4];
        timers[failed].start_error_ = srs_error_new(ERROR_ST_CREATE_CYCLE_THREAD, "create failed");

        MockKernelFactoryForFastTimer factory;
        for (int i = 0; i < 4; i++) {
            factory.fast_timers_.push_back(&timers[i]);
        }

        SrsSharedTimer shared;
        shared.factory_ = &factory;

        srs_error_t err = shared.initialize();
        EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err)) << "failed=" << failed;
        EXPECT_TRUE(srs_error_desc(err).find(names[failed]) != string::npos) << "failed=" << failed;
        srs_freep(err);

        for (int i = 0; i < 4; i++) {
            EXPECT_EQ(i <= failed ? 1 : 0, timers[i].start_count_) << "failed=" << failed << ", timer=" << i;
        }
        EXPECT_TRUE(timers[0].subscribed_.empty()) << "failed=" << failed;

        // The timers are borrowed, so the destructor must not free them.
        shared.timer20ms_ = NULL;
        shared.timer100ms_ = NULL;
        shared.timer1s_ = NULL;
        shared.timer5s_ = NULL;
        shared.factory_ = NULL;
    }
}

static vector<SrsPps *> kbps_server_line_pps()
{
    SrsPps *all[] = {_srs_pps_cids_get, _srs_pps_cids_set, _srs_pps_timer, _srs_pps_pub, _srs_pps_conn,
                     _srs_pps_dispose, _srs_pps_clock_15ms, _srs_pps_clock_20ms, _srs_pps_clock_25ms,
                     _srs_pps_clock_30ms, _srs_pps_clock_35ms, _srs_pps_clock_40ms, _srs_pps_clock_80ms,
                     _srs_pps_clock_160ms, _srs_pps_timer_s, _srs_pps_objs_rtps, _srs_pps_objs_rraw,
                     _srs_pps_objs_rfua, _srs_pps_objs_rbuf, _srs_pps_objs_msgs, _srs_pps_objs_rothers};
    return vector<SrsPps *>(all, all + sizeof(all) / sizeof(all[0]));
}

vector<SrsPps *> kbps_rtc_line_pps()
{
    SrsPps *all[] = {_srs_pps_rpkts, _srs_pps_rrtps, _srs_pps_rstuns, _srs_pps_rrtcps, _srs_pps_spkts,
                     _srs_pps_srtps, _srs_pps_sstuns, _srs_pps_srtcps, _srs_pps_pli, _srs_pps_twcc, _srs_pps_rr,
                     _srs_pps_snack, _srs_pps_snack2, _srs_pps_sanack, _srs_pps_svnack, _srs_pps_rnack,
                     _srs_pps_rnack2, _srs_pps_rhnack, _srs_pps_rmnack, _srs_pps_srtx, _srs_pps_rrtx,
                     _srs_pps_rrtx_unwrap, _srs_pps_rrtx_padding, _srs_pps_ids, _srs_pps_fids,
                     _srs_pps_fids_level0, _srs_pps_addrs, _srs_pps_fast_addrs};
    return vector<SrsPps *>(all, all + sizeof(all) / sizeof(all[0]));
}

// Every 10s the server logs "SRS: cpu=..,..MB" followed by a group for each kind of work done in the
// last 10s: coroutine id lookups, timer ticks, disposed connections, clock jitter and RTC objects.
// An idle server prints none of them; a busy one prints each rate; when the work stops, the groups
// go away again 10s later.
VOID TEST(KernelKbpsTest, ServerStatsLineShowsTheRatesOfABusyServer)
{
    KbpsGlobalPpsScope scope(kbps_server_line_pps());

    // The first stats line after the server starts: nothing has happened yet.
    if (true) {
        scope.clock_.set_clock(0);
        SrsKbpsStats stats;
        srs_global_kbps_update(&stats);
        EXPECT_STREQ("", stats.cid_desc_.c_str());
        EXPECT_STREQ("", stats.timer_desc_.c_str());
        EXPECT_STREQ("", stats.free_desc_.c_str());
        EXPECT_STREQ("", stats.clock_desc_.c_str());
        EXPECT_STREQ("", stats.objs_desc_.c_str());
    }

    // 10s of a busy server: log contexts read and set by coroutines, the 20ms timer ticking with RTC
    // publishers and players on it, a few clients leaving, the 20ms clock late now and then, and RTP
    // packets parsed for the publishers.
    _srs_pps_cids_get->sugar_ += 1200;
    _srs_pps_cids_set->sugar_ += 400;
    _srs_pps_timer->sugar_ += 500;
    _srs_pps_pub->sugar_ += 100;
    _srs_pps_conn->sugar_ += 300;
    _srs_pps_dispose->sugar_ += 30;
    _srs_pps_clock_20ms->sugar_ += 480;
    _srs_pps_timer_s->sugar_ += 10;
    _srs_pps_objs_rtps->sugar_ += 2000;
    _srs_pps_objs_rraw->sugar_ += 1000;
    _srs_pps_objs_rfua->sugar_ += 500;
    _srs_pps_objs_msgs->sugar_ += 3000;
    _srs_pps_objs_rothers->sugar_ += 10;
    _srs_pps_objs_rbuf->sugar_ += 1500;

    if (true) {
        scope.clock_.set_clock(10 * SRS_UTIME_SECONDS);
        SrsKbpsStats stats;
        srs_global_kbps_update(&stats);
        EXPECT_STREQ(", cid=120,40", stats.cid_desc_.c_str());
        EXPECT_STREQ(", timer=50,10,30", stats.timer_desc_.c_str());
        EXPECT_STREQ(", free=3", stats.free_desc_.c_str());
        EXPECT_STREQ(", clock=0,48,0,0,0,0,0,0,1", stats.clock_desc_.c_str());
        EXPECT_STREQ(", objs=(pkt:200,raw:100,fua:50,msg:300,oth:1,buf:150)", stats.objs_desc_.c_str());
    }

    // The clients leave and the server is idle for the next 10s: the line is short again.
    if (true) {
        scope.clock_.set_clock(20 * SRS_UTIME_SECONDS);
        SrsKbpsStats stats;
        srs_global_kbps_update(&stats);
        EXPECT_STREQ("", stats.cid_desc_.c_str());
        EXPECT_STREQ("", stats.timer_desc_.c_str());
        EXPECT_STREQ("", stats.free_desc_.c_str());
        EXPECT_STREQ("", stats.clock_desc_.c_str());
        EXPECT_STREQ("", stats.objs_desc_.c_str());
    }
}

// Every 10s the RTC server logs "RTC: Server conns=N" followed by the packet, RTCP, NACK and session
// lookup rates of the last 10s. Here WebRTC clients send and receive over UDP: every packet looks up
// its session by the fast id of its address, most of them hit the level-0 cache.
VOID TEST(KernelKbpsTest, RtcStatsLineShowsTheRatesOfUdpClients)
{
    KbpsGlobalPpsScope scope(kbps_rtc_line_pps());

    if (true) {
        scope.clock_.set_clock(0);
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);
        EXPECT_STREQ("", stats.rpkts_desc_.c_str());
        EXPECT_STREQ("", stats.spkts_desc_.c_str());
        EXPECT_STREQ("", stats.rtcp_desc_.c_str());
        EXPECT_STREQ("", stats.snk_desc_.c_str());
        EXPECT_STREQ("", stats.rnk_desc_.c_str());
        EXPECT_STREQ("", stats.fid_desc_.c_str());
    }

    // 10s of traffic: received RTP, STUN and RTCP; sent RTP to players; PLI, TWCC and RR feedback;
    // NACKs sent to the publisher for lost audio and video; NACKs from players, some for packets no
    // longer in the queue.
    _srs_pps_rpkts->sugar_ += 5000;
    _srs_pps_rrtps->sugar_ += 4500;
    _srs_pps_rstuns->sugar_ += 100;
    _srs_pps_rrtcps->sugar_ += 400;
    _srs_pps_spkts->sugar_ += 8000;
    _srs_pps_srtps->sugar_ += 7600;
    _srs_pps_sstuns->sugar_ += 100;
    _srs_pps_srtcps->sugar_ += 300;
    _srs_pps_pli->sugar_ += 20;
    _srs_pps_twcc->sugar_ += 100;
    _srs_pps_rr->sugar_ += 50;
    _srs_pps_snack->sugar_ += 30;
    _srs_pps_sanack->sugar_ += 10;
    _srs_pps_svnack->sugar_ += 20;
    _srs_pps_snack2->sugar_ += 50;
    _srs_pps_rnack->sugar_ += 40;
    _srs_pps_rnack2->sugar_ += 80;
    _srs_pps_rhnack->sugar_ += 20;
    _srs_pps_rmnack->sugar_ += 10;
    _srs_pps_ids->sugar_ += 100;
    _srs_pps_fids->sugar_ += 150;
    _srs_pps_fids_level0->sugar_ += 4850;
    _srs_pps_addrs->sugar_ += 100;
    _srs_pps_fast_addrs->sugar_ += 5000;

    if (true) {
        scope.clock_.set_clock(10 * SRS_UTIME_SECONDS);
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);
        EXPECT_STREQ(", rpkts=(500,rtp:450,stun:10,rtcp:40)", stats.rpkts_desc_.c_str());
        EXPECT_STREQ(", spkts=(800,rtp:760,stun:10,rtcp:30)", stats.spkts_desc_.c_str());
        EXPECT_STREQ(", rtcp=(pli:2,twcc:10,rr:5)", stats.rtcp_desc_.c_str());
        EXPECT_STREQ(", snk=(3,a:1,v:2,h:5)", stats.snk_desc_.c_str());
        EXPECT_STREQ(", rnk=(4,8,h:2,m:1)", stats.rnk_desc_.c_str());
        EXPECT_STREQ(", fid=(id:10,fid:15,ffid:485,addr:10,faddr:500)", stats.fid_desc_.c_str());
    }
}

// With WebRTC over TCP only, no UDP packet arrives, so no address gets a fast id; each TCP client still
// finds its session by the STUN username, so the RTC stats line should show the id lookups. The check
// joins the five rates with the comma operator instead of ||, so only the last one, the fast id
// count, decides, and the fid group is missing.
VOID TEST(KernelKbpsTest, DISABLED_RtcStatsLineWithTcpClientsOnly_FidGroupMissing)
{
    KbpsGlobalPpsScope scope(kbps_rtc_line_pps());

    scope.clock_.set_clock(0);
    if (true) {
        SrsKbsRtcStats stats;
        srs_global_rtc_update(&stats);
    }

    // 50 WebRTC clients connect over TCP in 10s, each looking up its session by name once.
    _srs_pps_ids->sugar_ += 50;

    scope.clock_.set_clock(10 * SRS_UTIME_SECONDS);
    SrsKbsRtcStats stats;
    srs_global_rtc_update(&stats);
    EXPECT_STREQ(", fid=(id:5,fid:0,ffid:0,addr:0,faddr:0)", stats.fid_desc_.c_str());
}

// An origin stream with one HTTP player: the publisher's messages go into the live source as
// SrsRtmpConn::process_publish_message passes them, and the player's consumer is drained into its encoder
// as SrsLiveStream does.
class AudioPlayerStream
{
public:
    MockAppConfig config_;
    SrsUniquePtr<SrsLiveSource> source_;
    SrsLiveConsumer *consumer_;
    MockSrsRequest req_;

public:
    AudioPlayerStream() : source_(new SrsLiveSource()), consumer_(NULL), req_("test.vhost", "live", "livestream")
    {
        source_->config_ = &config_;
        source_->req_ = new MockSrsRequest("test.vhost", "live", "livestream");
        source_->assemble();
    }
    virtual ~AudioPlayerStream()
    {
        srs_freep(consumer_);
    }

public:
    // An RTMP audio message.
    srs_error_t publish(int64_t timestamp, const char *data, int size)
    {
        SrsRtmpCommonMessage msg;
        msg.header_.initialize_audio(size, (uint32_t)timestamp, 1);
        msg.create_payload(size);
        memcpy(msg.payload(), data, size);
        return source_->on_audio(&msg);
    }
    // An RTMP aggregate message, as an FMS or Wowza edge sends, holding one FLV audio tag whose DataSize
    // is 0. SrsProtocol drops an empty RTMP message, but not an empty tag inside an aggregate.
    srs_error_t publish_empty_audio_in_aggregate(int64_t timestamp)
    {
        // TagType 8, DataSize 0, Timestamp and TimestampExtended, StreamID 0, PreviousTagSize 11.
        char tag[] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b};
        SrsBuffer b(tag, sizeof(tag));
        b.skip(4);
        b.write_3bytes((int32_t)timestamp);

        SrsRtmpCommonMessage msg;
        msg.header_.message_type_ = RTMP_MSG_AggregateMessage;
        msg.header_.payload_length_ = sizeof(tag);
        msg.header_.timestamp_ = timestamp;
        msg.header_.stream_id_ = 1;
        msg.create_payload(sizeof(tag));
        memcpy(msg.payload(), tag, sizeof(tag));
        return source_->on_aggregate(&msg);
    }
    // Sends every queued message to the player's encoder; nb_msgs is how many the consumer had.
    srs_error_t play(ISrsBufferEncoder *enc, int &nb_msgs)
    {
        srs_error_t err = srs_success;

        SrsMessageArray msgs(16);
        nb_msgs = 0;
        if ((err = consumer_->dump_packets(&msgs, nb_msgs)) != srs_success) {
            return srs_error_wrap(err, "dump");
        }

        SrsLiveStream stream(&req_, NULL);
        err = stream.streaming_send_messages(enc, msgs.msgs_, nb_msgs);
        msgs.free(nb_msgs);
        return err;
    }
};

// An RTMP publisher of MP3 audio sends an aggregate message holding an audio tag with no data. The live
// source splits it and, as it knows the stream is MP3, queues the empty audio message for every player. An
// HTTP .mp3 player must skip it and keep playing. SrsMp3Transmuxer::write_audio asserts that the payload is
// not NULL, so the server aborts.
VOID TEST(KernelMp3Test, DISABLED_HttpMp3PlayerGetsEmptyAudioMessage_ServerAborts)
{
    srs_error_t err;

    AudioPlayerStream s;
    HELPER_EXPECT_SUCCESS(s.source_->create_consumer(s.consumer_));

    // The FLV audio tag byte (MP3, 44 kHz, 16-bit, stereo), then an MP3 frame header.
    const char frame[] = {0x2f, (char)0xff, (char)0xfb, (char)0x90, 0x00};
    HELPER_EXPECT_SUCCESS(s.publish(0, frame, sizeof(frame)));
    HELPER_EXPECT_SUCCESS(s.publish_empty_audio_in_aggregate(26));

    MockSrsFileWriter writer;
    HELPER_EXPECT_SUCCESS(writer.open("livestream.mp3"));
    SrsMp3StreamEncoder enc;
    HELPER_EXPECT_SUCCESS(enc.initialize(&writer, NULL));
    int64_t id3_size = writer.filesize();

    int nb_msgs = 0;
    HELPER_EXPECT_SUCCESS(s.play(&enc, nb_msgs));
    EXPECT_EQ(2, nb_msgs);

    // Only the MP3 frame, without the tag byte, follows the ID3 header.
    EXPECT_EQ(id3_size + 4, writer.filesize());
}

// The same for an HTTP .aac player of an RTMP AAC stream: SrsAacTransmuxer::write_audio asserts the
// payload too, so the empty audio tag aborts the server.
VOID TEST(KernelMp3Test, DISABLED_HttpAacPlayerGetsEmptyAudioMessage_ServerAborts)
{
    srs_error_t err;

    AudioPlayerStream s;
    HELPER_EXPECT_SUCCESS(s.source_->create_consumer(s.consumer_));

    // AAC sequence header (AAC LC, 44.1 kHz, stereo), then a raw frame of 2 bytes.
    const char sh[] = {(char)0xaf, 0x00, 0x12, 0x10};
    const char frame[] = {(char)0xaf, 0x01, 0x21, 0x00};
    HELPER_EXPECT_SUCCESS(s.publish(0, sh, sizeof(sh)));
    HELPER_EXPECT_SUCCESS(s.publish(0, frame, sizeof(frame)));
    HELPER_EXPECT_SUCCESS(s.publish_empty_audio_in_aggregate(23));

    MockSrsFileWriter writer;
    HELPER_EXPECT_SUCCESS(writer.open("livestream.aac"));
    SrsAacStreamEncoder enc;
    HELPER_EXPECT_SUCCESS(enc.initialize(&writer, NULL));

    int nb_msgs = 0;
    HELPER_EXPECT_SUCCESS(s.play(&enc, nb_msgs));
    EXPECT_EQ(3, nb_msgs);

    // One ADTS frame: the 7-byte header and the 2 raw bytes.
    EXPECT_EQ(9, writer.filesize());
}

// H.264 sequence header of a 768x320 stream, as an FLV video tag, and the same stream after its encoder restarted
// with another PPS.
static const uint8_t mp4_avc_sh[] = {
    0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac,
    0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18,
    0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
static const uint8_t mp4_avc_sh_new_pps[] = {
    0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac,
    0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18,
    0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x3c};
// An H.264 key frame and inter frame, each one NALU after its 4-byte length.
static const uint8_t mp4_avc_idr[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
static const uint8_t mp4_avc_inter[] = {0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21, 0x6c, 0x42};
// The same frames as MP4 samples, without the FLV tag header.
static const uint8_t mp4_avc_idr_sample[] = {0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
static const uint8_t mp4_avc_inter_sample[] = {0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21, 0x6c, 0x42};
// HEVC key frame (IDR_W_RADL) and inter frame (TRAIL_R), as FLV video tags with the codec id 12.
static const uint8_t mp4_hevc_idr[] = {0x1c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x26, 0x01, 0xaf, 0x00, 0x10};
static const uint8_t mp4_hevc_inter[] = {0x2c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x02, 0x01, 0xd0, 0x00, 0x10};
// AAC LC sequence headers: 44.1 kHz stereo; 48 kHz stereo; and 44.1 kHz mono, flagged mono as the SRT and RTSP
// ingest does from the ADTS channel configuration.
static const uint8_t mp4_aac_sh[] = {0xaf, 0x00, 0x12, 0x10};
static const uint8_t mp4_aac_sh_48k[] = {0xaf, 0x00, 0x11, 0x90};
static const uint8_t mp4_aac_mono_sh[] = {0xae, 0x00, 0x12, 0x08};
static const uint8_t mp4_aac_frame[] = {0xaf, 0x01, 0x21, 0x11, 0x45, 0x00};
static const uint8_t mp4_aac_frame_sample[] = {0x21, 0x11, 0x45, 0x00};

// The HEVC sequence header of a 720p stream as an FLV video tag with the codec id 12: the HEVCDecoderConfigurationRecord
// of one VPS, SPS and PPS, or of the PPS twice, as the encoder sends after a restart with other settings.
// pps_flags is the PPS's first byte: its id, its SPS id, dependent_slice_segments_enabled_flag, output_flag_present_flag,
// num_extra_slice_header_bits and sign_data_hiding_enabled_flag.
srs_error_t mp4_hevc_sh(int nb_pps, string &tag, uint8_t pps_flags)
{
    srs_error_t err = srs_success;

    const uint8_t vps[] = {0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03,
                           0x00, 0x00, 0x03, 0x00, 0x3d, 0x95, 0x98, 0x09};
    const uint8_t sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
                           0x00, 0x3d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16, 0x59, 0x59, 0xa4, 0x93, 0x2b, 0xc0, 0x40, 0x40,
                           0x00, 0x00, 0xfa, 0x40, 0x00, 0x17, 0x70, 0x02};
    const uint8_t pps[] = {0x44, 0x01, pps_flags, 0x72, 0xb4, 0x62, 0x40};

    vector<string> ppss(nb_pps, string((const char *)pps, sizeof(pps)));
    string record;
    SrsRawHEVCStream hevc;
    if ((err = hevc.mux_sequence_header(string((const char *)vps, sizeof(vps)), string((const char *)sps, sizeof(sps)), ppss, record)) != srs_success) {
        return srs_error_wrap(err, "mux hevc sequence header");
    }

    tag = string("\x1c\x00\x00\x00\x00", 5) + record;
    return err;
}

// Feeds one FLV video tag to the MP4 encoder the way SrsDvrMp4Segmenter::encode_video does.
static srs_error_t mp4_dvr_video(SrsMp4Encoder *enc, SrsFormat *fmt, uint32_t dts, const uint8_t *tag, int size)
{
    srs_error_t err = srs_success;

    if ((err = fmt->on_video(dts, (char *)tag, size)) != srs_success) {
        return srs_error_wrap(err, "demux video");
    }

    SrsVideoAvcFrameTrait ct = fmt->video_->avc_packet_type_;
    if (ct == SrsVideoAvcFrameTraitSequenceHeader) {
        enc->vcodec_ = fmt->vcodec_->id_;
    }

    uint32_t pts = dts + (uint32_t)fmt->video_->cts_;
    return enc->write_sample(fmt, SrsMp4HandlerTypeVIDE, fmt->video_->frame_type_, ct, dts, pts, (uint8_t *)fmt->raw_, (uint32_t)fmt->nb_raw_);
}

// Feeds one FLV audio tag to the MP4 encoder the way SrsDvrMp4Segmenter::encode_audio does.
static srs_error_t mp4_dvr_audio(SrsMp4Encoder *enc, SrsFormat *fmt, uint32_t dts, const uint8_t *tag, int size)
{
    srs_error_t err = srs_success;

    if ((err = fmt->on_audio(dts, (char *)tag, size)) != srs_success) {
        return srs_error_wrap(err, "demux audio");
    }

    SrsAudioAacFrameTrait ct = fmt->audio_->aac_packet_type_;
    if (ct == SrsAudioAacFrameTraitSequenceHeader) {
        enc->set_audio_codec(fmt->acodec_->id_, fmt->acodec_->sound_rate_, fmt->acodec_->sound_size_, fmt->acodec_->sound_type_);
    }

    return enc->write_sample(fmt, SrsMp4HandlerTypeSOUN, 0x00, ct, dts, dts, (uint8_t *)fmt->raw_, (uint32_t)fmt->nb_raw_);
}

// The top-level boxes of an MP4 file or segment, decoded as a player reads them.
class Mp4TopBoxes
{
public:
    vector<SrsMp4Box *> boxes_;

public:
    ~Mp4TopBoxes()
    {
        for (int i = 0; i < (int)boxes_.size(); i++) {
            SrsMp4Box *box = boxes_[i];
            srs_freep(box);
        }
    }

    srs_error_t decode(const string &data)
    {
        srs_error_t err = srs_success;

        SrsBuffer buf((char *)data.data(), (int)data.size());
        while (!buf.empty()) {
            int start = buf.pos();

            SrsMp4Box *box = NULL;
            if ((err = SrsMp4Box::discovery(&buf, &box)) != srs_success) {
                return srs_error_wrap(err, "discovery at %d", start);
            }
            boxes_.push_back(box);

            if ((err = box->decode(&buf)) != srs_success) {
                return srs_error_wrap(err, "decode at %d", start);
            }

            // The mdat decodes only its header; skip its payload to the next box.
            buf.skip(start + (int)box->sz() - buf.pos());
        }

        return err;
    }

    SrsMp4Box *find(SrsMp4BoxType type)
    {
        for (int i = 0; i < (int)boxes_.size(); i++) {
            if (boxes_[i]->type_ == type) {
                return boxes_[i];
            }
        }
        return NULL;
    }

    SrsMp4MovieBox *moov()
    {
        return dynamic_cast<SrsMp4MovieBox *>(find(SrsMp4BoxTypeMOOV));
    }
};

// A publisher sends HEVC over RTMP and the vhost records it to .mp4. The moov must describe an hev1 track whose
// hvcC holds the publisher's sequence header, so a player can decode it. An encoder resends the same sequence
// header, such as on each key frame, and the recording goes on with the one config.
VOID TEST(KernelMp4Test, DvrHevcStreamRecordsHev1TrackWithItsConfig)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));

    MockSrsFileWriter fw;
    SrsMp4Encoder enc;
    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, (const uint8_t *)sh.data(), (int)sh.size()));
    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_hevc_idr, sizeof(mp4_hevc_idr)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 40, (const uint8_t *)sh.data(), (int)sh.size()));
    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 40, mp4_hevc_inter, sizeof(mp4_hevc_inter)));
    HELPER_ASSERT_SUCCESS(enc.flush());

    Mp4TopBoxes file;
    HELPER_ASSERT_SUCCESS(file.decode(fw.str()));
    ASSERT_TRUE(file.moov() != NULL);
    SrsMp4TrackBox *trak = file.moov()->video();
    ASSERT_TRUE(trak != NULL);
    ASSERT_EQ(1u, trak->stsd()->entry_count());

    SrsMp4VisualSampleEntry *hev1 = dynamic_cast<SrsMp4VisualSampleEntry *>(trak->stsd()->entrie_at(0));
    ASSERT_TRUE(hev1 != NULL);
    EXPECT_EQ(SrsMp4BoxTypeHEV1, hev1->type_);
    EXPECT_GT(fmt.vcodec_->width_, 0);
    EXPECT_EQ(fmt.vcodec_->width_, (int)hev1->width_);
    EXPECT_EQ(fmt.vcodec_->height_, (int)hev1->height_);

    SrsMp4HvcCBox *hvcC = hev1->hvcC();
    ASSERT_TRUE(hvcC != NULL);
    EXPECT_EQ(sh.substr(5), string(hvcC->hevc_config_.begin(), hvcC->hevc_config_.end()));

    // The two frames are the samples; the resent sequence header is not.
    EXPECT_EQ(2u, trak->stsz()->sample_count_);
}

// An RTMP publisher resends its H.264 and AAC sequence headers in the middle of a DVR recording, as an encoder does
// when its output reconnects. The .mp4 keeps the one config of each track and only the frames become samples.
VOID TEST(KernelMp4Test, DvrResentAvcAndAacSequenceHeadersAreIgnored)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    SrsMp4Encoder enc;
    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_avc_sh, sizeof(mp4_avc_sh)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_sh, sizeof(mp4_aac_sh)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_avc_idr, sizeof(mp4_avc_idr)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_frame, sizeof(mp4_aac_frame)));

    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 40, mp4_avc_sh, sizeof(mp4_avc_sh)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 40, mp4_aac_sh, sizeof(mp4_aac_sh)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 40, mp4_avc_inter, sizeof(mp4_avc_inter)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 46, mp4_aac_frame, sizeof(mp4_aac_frame)));
    HELPER_ASSERT_SUCCESS(enc.flush());

    Mp4TopBoxes file;
    HELPER_ASSERT_SUCCESS(file.decode(fw.str()));
    ASSERT_TRUE(file.moov() != NULL);

    SrsMp4TrackBox *vide = file.moov()->video();
    ASSERT_TRUE(vide != NULL && vide->avc1() != NULL && vide->avc1()->avcC() != NULL);
    vector<char> &avcc = vide->avc1()->avcC()->avc_config_;
    EXPECT_EQ(string((const char *)mp4_avc_sh + 5, sizeof(mp4_avc_sh) - 5), string(avcc.begin(), avcc.end()));
    EXPECT_EQ(2u, vide->stsz()->sample_count_);

    SrsMp4TrackBox *soun = file.moov()->audio();
    ASSERT_TRUE(soun != NULL && soun->mp4a() != NULL);
    SrsMp4DecoderSpecificInfo *asc = soun->mp4a()->esds()->es->decConfigDescr_.decSpecificInfo_;
    ASSERT_TRUE(asc != NULL);
    EXPECT_EQ(string("\x12\x10", 2), string(asc->asc_.begin(), asc->asc_.end()));
    EXPECT_EQ(2u, soun->stsz()->sample_count_);
}

// A publisher restarts its encoder with other settings during a DVR recording, and the new sequence header differs.
// One .mp4 has one sample description per track, so the recording fails with an error, instead of writing frames
// the moov describes wrongly. The same holds for H.264, HEVC and AAC.
VOID TEST(KernelMp4Test, DvrCodecConfigChangeMidRecordingFails)
{
    srs_error_t err;

    if (true) {
        MockSrsFileWriter fw;
        SrsMp4Encoder enc;
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

        HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_avc_sh, sizeof(mp4_avc_sh)));
        HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_avc_idr, sizeof(mp4_avc_idr)));

        err = mp4_dvr_video(&enc, &fmt, 40, mp4_avc_sh_new_pps, sizeof(mp4_avc_sh_new_pps));
        EXPECT_EQ(ERROR_MP4_AVCC_CHANGE, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        string sh, sh_new;
        HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));
        HELPER_ASSERT_SUCCESS(mp4_hevc_sh(2, sh_new));

        MockSrsFileWriter fw;
        SrsMp4Encoder enc;
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

        HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, (const uint8_t *)sh.data(), (int)sh.size()));
        HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_hevc_idr, sizeof(mp4_hevc_idr)));

        err = mp4_dvr_video(&enc, &fmt, 40, (const uint8_t *)sh_new.data(), (int)sh_new.size());
        EXPECT_EQ(ERROR_MP4_HVCC_CHANGE, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        MockSrsFileWriter fw;
        SrsMp4Encoder enc;
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw));

        HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_sh, sizeof(mp4_aac_sh)));
        HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_frame, sizeof(mp4_aac_frame)));

        err = mp4_dvr_audio(&enc, &fmt, 23, mp4_aac_sh_48k, sizeof(mp4_aac_sh_48k));
        EXPECT_EQ(ERROR_MP4_ASC_CHANGE, srs_error_code(err));
        srs_freep(err);
    }
}

// The file of a DVR recording that grew past 4 GB. The writer keeps the boxes the encoder writes, and only counts
// the bytes of a frame's payload, so a test can pass a frame size larger than its buffer.
class Mp4LargeFileWriter : public ISrsWriteSeeker
{
public:
    // The writes of up to 64 KB, by file offset.
    map<int64_t, string> chunks_;
    int64_t pos_;
    int64_t size_;

public:
    Mp4LargeFileWriter()
    {
        pos_ = size_ = 0;
    }

    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        if (size <= 64 * 1024) {
            chunks_[pos_] = string((char *)buf, size);
        }
        pos_ += size;
        size_ = srs_max(size_, pos_);
        if (nwrite) {
            *nwrite = size;
        }
        return srs_success;
    }

    virtual srs_error_t writev(const iovec * /*iov*/, int /*iov_size*/, ssize_t * /*nwrite*/)
    {
        return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "not supported");
    }

    virtual srs_error_t lseek(srs_off_t offset, int whence, srs_off_t *seeked)
    {
        if (whence == SEEK_SET) {
            pos_ = offset;
        } else if (whence == SEEK_CUR) {
            pos_ += offset;
        } else {
            pos_ = size_ + offset;
        }
        if (seeked) {
            *seeked = pos_;
        }
        return srs_success;
    }
};

// A long DVR recording of a high bitrate stream passes 4 GB, beyond a 32-bit box size or chunk offset. The mdat
// must use the 64-bit large size, taking the 8 bytes of the free box the encoder reserved before it, and both
// tracks must use co64 for the offsets of their samples. Otherwise a player seeks into the wrong bytes.
VOID TEST(KernelMp4Test, DvrRecordingOver4GBUsesLargeSizeAndChunkOffsets)
{
    srs_error_t err;

    Mp4LargeFileWriter w;
    SrsMp4Encoder enc;
    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(enc.initialize(&w));

    HELPER_EXPECT_SUCCESS(mp4_dvr_video(&enc, &fmt, 0, mp4_avc_sh, sizeof(mp4_avc_sh)));
    HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_sh, sizeof(mp4_aac_sh)));

    // Each key frame stands for over half an hour of the stream.
    const uint32_t nb_frame = 2200000000u;
    for (int i = 0; i < 3; i++) {
        uint32_t dts = i * 1000;
        HELPER_ASSERT_SUCCESS(fmt.on_video(dts, (char *)mp4_avc_idr, sizeof(mp4_avc_idr)));
        HELPER_EXPECT_SUCCESS(enc.write_sample(&fmt, SrsMp4HandlerTypeVIDE, fmt.video_->frame_type_, fmt.video_->avc_packet_type_, dts, dts, (uint8_t *)fmt.raw_, nb_frame));
    }
    HELPER_EXPECT_SUCCESS(mp4_dvr_audio(&enc, &fmt, 3000, mp4_aac_frame, sizeof(mp4_aac_frame)));
    HELPER_ASSERT_SUCCESS(enc.flush());

    // The ftyp, then the free box and the mdat header, 8 bytes each, then the samples.
    ASSERT_TRUE(w.chunks_.count(0));
    int64_t ftyp_size = (int64_t)w.chunks_[0].size();
    int64_t first_sample = ftyp_size + 16;
    uint64_t mdat_bytes = 3 * (uint64_t)nb_frame + sizeof(mp4_aac_frame_sample);

    // The mdat header now starts where the free box was, with the large size.
    ASSERT_TRUE(w.chunks_.count(ftyp_size));
    string mdat_header = w.chunks_[ftyp_size];
    ASSERT_EQ(16u, mdat_header.size());
    SrsBuffer mdat_buf((char *)mdat_header.data(), (int)mdat_header.size());
    SrsMp4Box *box = NULL;
    HELPER_ASSERT_SUCCESS(SrsMp4Box::discovery(&mdat_buf, &box));
    SrsUniquePtr<SrsMp4Box> mdat(box);
    EXPECT_TRUE(dynamic_cast<SrsMp4MediaDataBox *>(mdat.get()) != NULL);
    // The size 1 says the 64-bit size follows the type.
    EXPECT_EQ(1u, (uint32_t)mdat_buf.read_4bytes());
    EXPECT_EQ(SrsMp4BoxTypeMDAT, (SrsMp4BoxType)mdat_buf.read_4bytes());
    EXPECT_EQ(16 + mdat_bytes, (uint64_t)mdat_buf.read_8bytes());

    // The moov follows the samples.
    int64_t moov_offset = first_sample + (int64_t)mdat_bytes;
    ASSERT_TRUE(w.chunks_.count(moov_offset));
    Mp4TopBoxes moov;
    HELPER_ASSERT_SUCCESS(moov.decode(w.chunks_[moov_offset]));
    ASSERT_TRUE(moov.moov() != NULL);

    SrsMp4SampleTableBox *vide = moov.moov()->video()->stbl();
    EXPECT_TRUE(vide->stco() == NULL);
    ASSERT_TRUE(vide->co64() != NULL);
    ASSERT_EQ(3u, vide->co64()->entry_count_);
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ((uint64_t)first_sample + i * (uint64_t)nb_frame, vide->co64()->entries_[i]);
        EXPECT_EQ(nb_frame, vide->stsz()->entry_sizes_[i]);
    }

    SrsMp4SampleTableBox *soun = moov.moov()->audio()->stbl();
    EXPECT_TRUE(soun->stco() == NULL);
    ASSERT_TRUE(soun->co64() != NULL);
    ASSERT_EQ(1u, soun->co64()->entry_count_);
    EXPECT_EQ((uint64_t)first_sample + 3 * (uint64_t)nb_frame, soun->co64()->entries_[0]);
}

typedef srs_error_t (*Mp4WriteSession)(MockSrsFileWriter *fw);

// The disk fills at each byte up to where a clean run starts its last write, so a write is always refused: the
// session must report the error, not leave a truncated file as if it were whole.
static void mp4_expect_disk_full_fails(Mp4WriteSession session, int64_t last_write_start)
{
    for (int64_t full = 0; full <= last_write_start; full++) {
        MockSrsFileWriter fw;
        fw.error_offset = (int)full;
        srs_error_t err = session(&fw);
        EXPECT_TRUE(err != srs_success) << "disk full at " << full;
        srs_freep(err);
    }
}

static srs_error_t mp4_dvr_session(MockSrsFileWriter *fw)
{
    srs_error_t err = srs_success;

    SrsMp4Encoder enc;
    SrsFormat fmt;
    if ((err = fmt.initialize()) != srs_success) {
        return srs_error_wrap(err, "format");
    }
    if ((err = enc.initialize(fw)) != srs_success) {
        return srs_error_wrap(err, "initialize");
    }
    if ((err = mp4_dvr_video(&enc, &fmt, 0, mp4_avc_sh, sizeof(mp4_avc_sh))) != srs_success) {
        return srs_error_wrap(err, "avc sh");
    }
    if ((err = mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_sh, sizeof(mp4_aac_sh))) != srs_success) {
        return srs_error_wrap(err, "aac sh");
    }
    if ((err = mp4_dvr_video(&enc, &fmt, 0, mp4_avc_idr, sizeof(mp4_avc_idr))) != srs_success) {
        return srs_error_wrap(err, "idr");
    }
    if ((err = mp4_dvr_audio(&enc, &fmt, 0, mp4_aac_frame, sizeof(mp4_aac_frame))) != srs_success) {
        return srs_error_wrap(err, "aac");
    }
    if ((err = mp4_dvr_video(&enc, &fmt, 40, mp4_avc_inter, sizeof(mp4_avc_inter))) != srs_success) {
        return srs_error_wrap(err, "inter");
    }
    return enc.flush();
}

// The disk fills while SRS records a stream to .mp4: writing the ftyp, the free box, the mdat header, a frame, or
// the moov at the end fails, and the recording reports it.
VOID TEST(KernelMp4Test, DvrDiskFullFailsTheRecording)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    HELPER_ASSERT_SUCCESS(mp4_dvr_session(&fw));
    string file = fw.str();
    size_t moov = file.find("moov");
    ASSERT_NE(string::npos, moov);

    // The moov is the last box written at the end; the mdat header is then rewritten in place.
    mp4_expect_disk_full_fails(mp4_dvr_session, (int64_t)moov - 4);
}

static srs_error_t mp4_dash_video_init_session(MockSrsFileWriter *fw)
{
    srs_error_t err = srs_success;

    SrsFormat fmt;
    if ((err = fmt.initialize()) != srs_success) {
        return srs_error_wrap(err, "format");
    }
    if ((err = fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh))) != srs_success) {
        return srs_error_wrap(err, "avc sh");
    }

    SrsMp4M2tsInitEncoder init;
    if ((err = init.initialize(fw)) != srs_success) {
        return srs_error_wrap(err, "initialize");
    }
    return init.write(&fmt, true, 1);
}

static srs_error_t mp4_dash_segment_session(MockSrsFileWriter *fw)
{
    srs_error_t err = srs_success;

    SrsMp4M2tsSegmentEncoder enc;
    if ((err = enc.initialize(fw, 1, 0, 1)) != srs_success) {
        return srs_error_wrap(err, "initialize");
    }
    if ((err = enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 0, 0, (uint8_t *)mp4_avc_idr_sample, sizeof(mp4_avc_idr_sample))) != srs_success) {
        return srs_error_wrap(err, "idr");
    }
    if ((err = enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeInterFrame, 40, 40, (uint8_t *)mp4_avc_inter_sample, sizeof(mp4_avc_inter_sample))) != srs_success) {
        return srs_error_wrap(err, "inter");
    }
    uint64_t dts = 80;
    return enc.flush(dts);
}

// The disk fills while SRS writes DASH: the init.mp4 (ftyp, moov) or a segment (styp, sidx, moof, mdat header, a
// frame) fails to write, and the error is reported.
VOID TEST(KernelMp4Test, DashDiskFullFailsInitAndSegment)
{
    srs_error_t err;

    MockSrsFileWriter init;
    HELPER_ASSERT_SUCCESS(mp4_dash_video_init_session(&init));
    size_t moov = init.str().find("moov");
    ASSERT_NE(string::npos, moov);
    mp4_expect_disk_full_fails(mp4_dash_video_init_session, (int64_t)moov - 4);

    MockSrsFileWriter segment;
    HELPER_ASSERT_SUCCESS(mp4_dash_segment_session(&segment));
    mp4_expect_disk_full_fails(mp4_dash_segment_session, segment.filesize() - (int64_t)sizeof(mp4_avc_inter_sample));
}

static srs_error_t mp4_hls_init_session(MockSrsFileWriter *fw)
{
    srs_error_t err = srs_success;

    SrsFormat fmt;
    if ((err = fmt.initialize()) != srs_success) {
        return srs_error_wrap(err, "format");
    }
    if ((err = fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh))) != srs_success) {
        return srs_error_wrap(err, "avc sh");
    }
    if ((err = fmt.on_audio(0, (char *)mp4_aac_sh, sizeof(mp4_aac_sh))) != srs_success) {
        return srs_error_wrap(err, "aac sh");
    }

    SrsMp4M2tsInitEncoder init;
    if ((err = init.initialize(fw)) != srs_success) {
        return srs_error_wrap(err, "initialize");
    }
    return init.write(&fmt, 1, 2);
}

static srs_error_t mp4_hls_segment_session(MockSrsFileWriter *fw)
{
    srs_error_t err = srs_success;

    SrsFmp4SegmentEncoder enc;
    if ((err = enc.initialize(fw, 1, 0, 1, 2)) != srs_success) {
        return srs_error_wrap(err, "initialize");
    }
    if ((err = enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 0, 0, (uint8_t *)mp4_avc_idr_sample, sizeof(mp4_avc_idr_sample))) != srs_success) {
        return srs_error_wrap(err, "idr");
    }
    if ((err = enc.write_sample(SrsMp4HandlerTypeSOUN, 0, 0, 0, (uint8_t *)mp4_aac_frame_sample, sizeof(mp4_aac_frame_sample))) != srs_success) {
        return srs_error_wrap(err, "aac");
    }
    return enc.flush(40);
}

// The disk fills while SRS writes HLS fMP4: the init.mp4 of both tracks (ftyp, moov) or a segment (moof, mdat
// header, a video or audio frame) fails to write, and the error is reported.
VOID TEST(KernelMp4Test, HlsFmp4DiskFullFailsInitAndSegment)
{
    srs_error_t err;

    MockSrsFileWriter init;
    HELPER_ASSERT_SUCCESS(mp4_hls_init_session(&init));
    size_t moov = init.str().find("moov");
    ASSERT_NE(string::npos, moov);
    mp4_expect_disk_full_fails(mp4_hls_init_session, (int64_t)moov - 4);

    // The audio frames are written after the video frames.
    MockSrsFileWriter segment;
    HELPER_ASSERT_SUCCESS(mp4_hls_segment_session(&segment));
    mp4_expect_disk_full_fails(mp4_hls_segment_session, segment.filesize() - (int64_t)sizeof(mp4_aac_frame_sample));
}

// DASH of an HEVC stream: the video init.mp4 must describe an hev1 track whose hvcC holds the stream's sequence
// header.
VOID TEST(KernelMp4Test, DashHevcVideoInitSegmentHasHev1Track)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));
    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)sh.data(), (int)sh.size()));

    MockSrsFileWriter fw;
    SrsMp4M2tsInitEncoder init;
    HELPER_ASSERT_SUCCESS(init.initialize(&fw));
    HELPER_ASSERT_SUCCESS(init.write(&fmt, true, 1));

    Mp4TopBoxes file;
    HELPER_ASSERT_SUCCESS(file.decode(fw.str()));
    ASSERT_TRUE(file.moov() != NULL && file.moov()->video() != NULL);
    SrsMp4VisualSampleEntry *hev1 = dynamic_cast<SrsMp4VisualSampleEntry *>(file.moov()->video()->stsd()->entrie_at(0));
    ASSERT_TRUE(hev1 != NULL);
    EXPECT_EQ(SrsMp4BoxTypeHEV1, hev1->type_);
    EXPECT_EQ(fmt.vcodec_->width_, (int)hev1->width_);
    ASSERT_TRUE(hev1->hvcC() != NULL);
    EXPECT_EQ(sh.substr(5), string(hev1->hvcC()->hevc_config_.begin(), hev1->hvcC()->hevc_config_.end()));
}

// DASH of a mono AAC stream, such as one an SRT encoder publishes from a single microphone: the audio init.mp4
// must say one channel, or a player upmixes or fails to configure its decoder.
VOID TEST(KernelMp4Test, DashMonoAacInitSegmentHasOneChannel)
{
    srs_error_t err;

    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_audio(0, (char *)mp4_aac_mono_sh, sizeof(mp4_aac_mono_sh)));

    MockSrsFileWriter fw;
    SrsMp4M2tsInitEncoder init;
    HELPER_ASSERT_SUCCESS(init.initialize(&fw));
    HELPER_ASSERT_SUCCESS(init.write(&fmt, false, 2));

    Mp4TopBoxes file;
    HELPER_ASSERT_SUCCESS(file.decode(fw.str()));
    ASSERT_TRUE(file.moov() != NULL && file.moov()->audio() != NULL);
    SrsMp4AudioSampleEntry *mp4a = file.moov()->audio()->mp4a();
    ASSERT_TRUE(mp4a != NULL);
    EXPECT_EQ(1, mp4a->channelcount_);
    EXPECT_EQ(16, mp4a->samplesize_);
    EXPECT_EQ(44100u << 16, mp4a->samplerate_);
}

// HLS fMP4 of an H.264 stream with mono AAC: the init.mp4 of both tracks must say one audio channel.
VOID TEST(KernelMp4Test, HlsFmp4MonoAacInitSegmentHasOneChannel)
{
    srs_error_t err;

    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh)));
    HELPER_ASSERT_SUCCESS(fmt.on_audio(0, (char *)mp4_aac_mono_sh, sizeof(mp4_aac_mono_sh)));

    MockSrsFileWriter fw;
    SrsMp4M2tsInitEncoder init;
    HELPER_ASSERT_SUCCESS(init.initialize(&fw));
    HELPER_ASSERT_SUCCESS(init.write(&fmt, 1, 2));

    Mp4TopBoxes file;
    HELPER_ASSERT_SUCCESS(file.decode(fw.str()));
    ASSERT_TRUE(file.moov() != NULL && file.moov()->audio() != NULL && file.moov()->video() != NULL);
    SrsMp4AudioSampleEntry *mp4a = file.moov()->audio()->mp4a();
    ASSERT_TRUE(mp4a != NULL);
    EXPECT_EQ(1, mp4a->channelcount_);
    EXPECT_EQ(16, mp4a->samplesize_);
}

// HLS fMP4 with SAMPLE-AES (cbcs) of an HEVC stream with AAC: in the init.mp4 the hev1 entry becomes encv and the
// mp4a entry enca, each with a sinf that keeps the original format in frma, so a player knows what it decodes once
// it decrypts, and the cbcs scheme in schm.
VOID TEST(KernelMp4Test, HlsFmp4EncryptedHevcInitSegmentProtectsBothTracks)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));
    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)sh.data(), (int)sh.size()));
    HELPER_ASSERT_SUCCESS(fmt.on_audio(0, (char *)mp4_aac_sh, sizeof(mp4_aac_sh)));

    MockSrsFileWriter fw;
    SrsMp4M2tsInitEncoder init;
    HELPER_ASSERT_SUCCESS(init.initialize(&fw));
    unsigned char kid[16], iv[16];
    memset(kid, 0x11, sizeof(kid));
    memset(iv, 0x22, sizeof(iv));
    init.config_encryption(1, 9, kid, iv, 16);
    HELPER_ASSERT_SUCCESS(init.write(&fmt, 1, 2));

    string data = fw.str();
    size_t encv = data.find("encv");
    ASSERT_NE(string::npos, encv);
    size_t frma = data.find("frma", encv);
    ASSERT_NE(string::npos, frma);
    EXPECT_EQ("hev1", data.substr(frma + 4, 4));
    // No sample entry is left as a clear hev1.
    EXPECT_EQ(frma + 4, data.find("hev1"));
    EXPECT_NE(string::npos, data.find(sh.substr(5), encv));

    size_t enca = data.find("enca");
    ASSERT_NE(string::npos, enca);
    frma = data.find("frma", enca);
    ASSERT_NE(string::npos, frma);
    EXPECT_EQ("mp4a", data.substr(frma + 4, 4));

    size_t schm = data.find("schm", encv);
    ASSERT_NE(string::npos, schm);
    EXPECT_EQ("cbcs", data.substr(schm + 8, 4));
}

// A DASH segment carries each frame's composition offset in the trun. B-frames give positive offsets, and a trun of
// version 0 holds them unsigned. Some encoders shift the presentation time before the decoding time, which gives a
// negative offset, such as -40 ms for the first frame of a segment; the trun must then be version 1, which holds
// them signed, or a player shows the frame about 2^32 ticks late. The sidx in front gives the start and the
// duration of the segment for the player to keep audio and video in sync.
VOID TEST(KernelMp4Test, DashSegmentTrunVersionFollowsTheSignOfCompositionOffsets)
{
    srs_error_t err;

    // B-frames: the key frame shows 80 ms after it decodes.
    if (true) {
        MockSrsFileWriter fw;
        SrsMp4M2tsSegmentEncoder enc;
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw, 1, 0, 1));
        HELPER_ASSERT_SUCCESS(enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 0, 80, (uint8_t *)mp4_avc_idr_sample, sizeof(mp4_avc_idr_sample)));
        HELPER_ASSERT_SUCCESS(enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeInterFrame, 40, 40, (uint8_t *)mp4_avc_inter_sample, sizeof(mp4_avc_inter_sample)));
        uint64_t dts = 80;
        HELPER_ASSERT_SUCCESS(enc.flush(dts));

        Mp4TopBoxes segment;
        HELPER_ASSERT_SUCCESS(segment.decode(fw.str()));
        SrsMp4MovieFragmentBox *moof = dynamic_cast<SrsMp4MovieFragmentBox *>(segment.find(SrsMp4BoxTypeMOOF));
        ASSERT_TRUE(moof != NULL);
        SrsMp4TrackFragmentBox *traf = dynamic_cast<SrsMp4TrackFragmentBox *>(moof->get(SrsMp4BoxTypeTRAF));
        ASSERT_TRUE(traf != NULL && traf->trun() != NULL);
        SrsMp4TrackFragmentRunBox *trun = traf->trun();
        EXPECT_EQ(0, trun->version_);
        ASSERT_EQ(2u, trun->entries_.size());
        EXPECT_EQ(80, trun->entries_[0]->sample_composition_time_offset_);
        EXPECT_EQ(0, trun->entries_[1]->sample_composition_time_offset_);
    }

    // The third segment starts at 2s; its key frame decodes at 2040 and shows at 2000.
    if (true) {
        MockSrsFileWriter fw;
        SrsMp4M2tsSegmentEncoder enc;
        HELPER_ASSERT_SUCCESS(enc.initialize(&fw, 3, 2 * SRS_UTIME_SECONDS, 1));
        HELPER_ASSERT_SUCCESS(enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 2040, 2000, (uint8_t *)mp4_avc_idr_sample, sizeof(mp4_avc_idr_sample)));
        HELPER_ASSERT_SUCCESS(enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeInterFrame, 2080, 2120, (uint8_t *)mp4_avc_inter_sample, sizeof(mp4_avc_inter_sample)));
        uint64_t dts = 2120;
        HELPER_ASSERT_SUCCESS(enc.flush(dts));

        Mp4TopBoxes segment;
        HELPER_ASSERT_SUCCESS(segment.decode(fw.str()));
        ASSERT_EQ(4u, segment.boxes_.size());
        EXPECT_EQ(SrsMp4BoxTypeSTYP, segment.boxes_[0]->type_);
        EXPECT_EQ(SrsMp4BoxTypeSIDX, segment.boxes_[1]->type_);
        EXPECT_EQ(SrsMp4BoxTypeMOOF, segment.boxes_[2]->type_);
        EXPECT_EQ(SrsMp4BoxTypeMDAT, segment.boxes_[3]->type_);

        SrsMp4MovieFragmentBox *moof = dynamic_cast<SrsMp4MovieFragmentBox *>(segment.boxes_[2]);
        ASSERT_TRUE(moof != NULL);
        SrsMp4TrackFragmentBox *traf = dynamic_cast<SrsMp4TrackFragmentBox *>(moof->get(SrsMp4BoxTypeTRAF));
        ASSERT_TRUE(traf != NULL && traf->trun() != NULL);
        SrsMp4TrackFragmentRunBox *trun = traf->trun();
        EXPECT_EQ(1, trun->version_);
        ASSERT_EQ(2u, trun->entries_.size());
        EXPECT_EQ(-40, trun->entries_[0]->sample_composition_time_offset_);
        EXPECT_EQ(40, trun->entries_[1]->sample_composition_time_offset_);
        EXPECT_EQ(40u, trun->entries_[0]->sample_duration_);
        EXPECT_EQ(40u, trun->entries_[1]->sample_duration_);
        EXPECT_EQ(sizeof(mp4_avc_idr_sample), trun->entries_[0]->sample_size_);
        // The samples start right after the mdat header that follows the moof.
        EXPECT_EQ((int32_t)moof->sz() + 8, trun->data_offset_);

        SrsMp4SegmentIndexBox *sidx = dynamic_cast<SrsMp4SegmentIndexBox *>(segment.boxes_[1]);
        ASSERT_TRUE(sidx != NULL);
        EXPECT_EQ(1, sidx->version_);
        EXPECT_EQ(1000u, sidx->timescale_);
        EXPECT_EQ(2000u, sidx->earliest_presentation_time_);
        ASSERT_EQ(1u, sidx->entries_.size());
        EXPECT_EQ(80u, sidx->entries_[0].subsegment_duration_);
        EXPECT_EQ(moof->sz() + segment.boxes_[3]->sz(), sidx->entries_[0].referenced_size_);
        EXPECT_EQ(1, sidx->entries_[0].starts_with_SAP_);
    }
}

// HLS fMP4 with SAMPLE-AES (cbcs): a key frame carries an SEI over 64 KB before its slice, such as one with large
// user data. The senc gives the clear bytes before each protected range in a 16-bit field, so a clear run over
// 65535 bytes is split into subsamples with no protected bytes. The player then leaves the whole SEI and the slice
// header clear, and decrypts only the first block of each 10 in the rest of the slice.
VOID TEST(KernelMp4Test, HlsFmp4EncryptedFrameWithLargeSeiSplitsTheClearRun)
{
    srs_error_t err;

    // An SEI NALU of 70000 bytes, then an IDR slice of 100 bytes, each after its 4-byte length.
    string sample;
    sample.append("\x00\x01\x11\x70", 4);
    sample.append(1, '\x06');
    sample.append(69999, '\x5a');
    sample.append("\x00\x00\x00\x64", 4);
    sample.append(1, '\x65');
    for (int i = 0; i < 99; i++) {
        sample.append(1, (char)i);
    }

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder enc;
    HELPER_ASSERT_SUCCESS(enc.initialize(&fw, 1, 0, 1, 2));
    unsigned char key[16], iv[16];
    memset(key, 0x33, sizeof(key));
    memset(iv, 0x44, sizeof(iv));
    HELPER_ASSERT_SUCCESS(enc.config_cipher(key, iv));
    enc.config_video_cipher(SrsVideoCodecIdAVC, 4);
    HELPER_ASSERT_SUCCESS(enc.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 0, 0, (uint8_t *)sample.data(), (uint32_t)sample.size()));
    HELPER_ASSERT_SUCCESS(enc.flush(40));

    string data = fw.str();
    size_t senc_type = data.find("senc");
    ASSERT_NE(string::npos, senc_type);
    SrsBuffer buf((char *)data.data() + senc_type - 4, (int)(data.size() - senc_type + 4));
    SrsMp4SampleEncryptionBox senc(0);
    HELPER_ASSERT_SUCCESS(senc.decode(&buf));
    ASSERT_EQ(1u, senc.entries_.size());

    // The SEI with its length, and the slice's length and first 32 bytes, are 70040 clear bytes before the 68
    // protected bytes of the slice.
    vector<SrsMp4SubSampleEncryptionInfo> &infos = senc.entries_[0]->subsample_infos_;
    ASSERT_EQ(2u, infos.size());
    EXPECT_EQ(65535, infos[0].bytes_of_clear_data_);
    EXPECT_EQ(0u, infos[0].bytes_of_protected_data_);
    EXPECT_EQ(70040 - 65535, infos[1].bytes_of_clear_data_);
    EXPECT_EQ(68u, infos[1].bytes_of_protected_data_);

    // In the mdat, only the first 16 protected bytes are encrypted.
    size_t mdat_type = data.find("mdat");
    ASSERT_NE(string::npos, mdat_type);
    string out = data.substr(mdat_type + 4);
    ASSERT_EQ(sample.size(), out.size());
    EXPECT_EQ(sample.substr(0, 70040), out.substr(0, 70040));
    EXPECT_NE(sample.substr(70040, 16), out.substr(70040, 16));
    EXPECT_EQ(sample.substr(70056), out.substr(70056));
}

// Expects err to fail with the error code, and frees it.
static void packet_expect_error_code(int code, srs_error_t err)
{
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(code, srs_error_code(err));
    srs_freep(err);
}

// An H.264 inter frame as an FLV video tag, holding the NALUs each after its 4-byte length.
static string packet_avc_ibmf_tag(const vector<string> &nalus)
{
    string tag("\x27\x01\x00\x00\x00", 5);
    for (size_t i = 0; i < nalus.size(); i++) {
        uint32_t size = (uint32_t)nalus[i].size();
        tag.push_back((char)(size >> 24));
        tag.push_back((char)(size >> 16));
        tag.push_back((char)(size >> 8));
        tag.push_back((char)size);
        tag += nalus[i];
    }
    return tag;
}

// An H.264 inter frame as an FLV video tag in AnnexB, as some encoders send it: each NALU after a 3-byte start code.
static string packet_avc_annexb_tag(const vector<string> &nalus)
{
    string tag("\x27\x01\x00\x00\x00", 5);
    for (size_t i = 0; i < nalus.size(); i++) {
        tag += string("\x00\x00\x01", 3) + nalus[i];
    }
    return tag;
}

// An HEVC inter frame of one TRAIL_R slice as an FLV video tag: the 2-byte NALU header, then the slice header bits given
// as '0' and '1' characters, padded with zero bits to whole bytes.
static string packet_hevc_trail_r_tag(const string &bits)
{
    string nalu("\x02\x01", 2);
    uint8_t v = 0;
    int n = 0;
    for (size_t i = 0; i < bits.size(); i++) {
        v = (uint8_t)((v << 1) | (bits[i] == '1' ? 1 : 0));
        if (++n == 8) {
            nalu.push_back((char)v);
            v = 0;
            n = 0;
        }
    }
    if (n) {
        nalu.push_back((char)(v << (8 - n)));
    }

    vector<string> nalus(1, nalu);
    string tag = packet_avc_ibmf_tag(nalus);
    tag[0] = (char)0x2c; // Inter frame, codec id 12.
    return tag;
}

// The RTMP to WebRTC bridge of a stream with keep_bframe off, the default: WebRTC cannot play B frames, so the bridge
// reads the slice header of each NALU and drops the B slices.
class PacketRtcBridge
{
public:
    MockStreamBridgeRequest req_;
    MockRtpTarget target_;
    SrsSharedPtr<SrsRtcSource> source_;
    SrsRtcRtpBuilder builder_;

public:
    PacketRtcBridge() : source_(new SrsRtcSource()), builder_(_srs_app_factory, &target_, source_)
    {
    }

    srs_error_t initialize()
    {
        srs_error_t err = srs_success;
        if ((err = source_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "rtc source");
        }
        builder_.assemble();
        if ((err = builder_.initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "rtc builder");
        }
        builder_.keep_bframe_ = false;
        return err;
    }

    srs_error_t sequence_header(const string &tag)
    {
        return builder_.format_->on_video(0, (char *)tag.data(), (int)tag.size());
    }

    // Demuxes one FLV video frame as SrsRtcRtpBuilder::on_video does, and returns the NALUs the bridge keeps.
    srs_error_t video(const string &tag, vector<SrsNaluSample *> &kept)
    {
        srs_error_t err = srs_success;
        kept.clear();
        if ((err = builder_.format_->on_video(0, (char *)tag.data(), (int)tag.size())) != srs_success) {
            return srs_error_wrap(err, "demux");
        }
        bool has_idr = false;
        SrsMediaPacket msg;
        return builder_.filter(&msg, builder_.format_, has_idr, kept);
    }
};

// Some encoders publish H.264 over RTMP in AnnexB, an access unit delimiter, an SEI and the IDR slice each after a
// start code. The frame must be split into its three NALUs, so the IDR is seen and each NALU is sent on alone.
VOID TEST(KernelPacketTest, AnnexbFrameIsSplitIntoItsNalus)
{
    srs_error_t err;

    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh)));

    vector<string> nalus;
    nalus.push_back(string("\x09\xf0", 2));
    nalus.push_back(string("\x06\x05\x01\x80", 4));
    nalus.push_back(string("\x65\x88\x84\x00\x10", 5));
    string tag = packet_avc_annexb_tag(nalus);
    tag[0] = (char)0x17;
    HELPER_ASSERT_SUCCESS(fmt.on_video(40, (char *)tag.data(), (int)tag.size()));

    EXPECT_EQ(SrsAvcPayloadFormatAnnexb, fmt.vcodec_->payload_format_);
    ASSERT_EQ(3, fmt.video_->nb_samples_);
    for (int i = 0; i < 3; i++) {
        SrsNaluSample *sample = &fmt.video_->samples_[i];
        EXPECT_EQ(nalus[i], string(sample->bytes_, sample->size_));
    }
    EXPECT_TRUE(fmt.video_->has_aud_);
    EXPECT_TRUE(fmt.video_->has_idr_);
}

// An AnnexB frame with a start code written twice, as a muxer that adds a start code to NALUs that already have one
// gives: the empty NALU between the two start codes is skipped, not sent on as a NALU of no bytes.
VOID TEST(KernelPacketTest, AnnexbFrameWithRepeatedStartCodeSkipsTheEmptyNalu)
{
    srs_error_t err;

    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh)));

    vector<string> nalus;
    nalus.push_back("");
    nalus.push_back(string("\x41\x9a\x21\x6c\x42", 5));
    string tag = packet_avc_annexb_tag(nalus);
    HELPER_ASSERT_SUCCESS(fmt.on_video(40, (char *)tag.data(), (int)tag.size()));

    ASSERT_EQ(1, fmt.video_->nb_samples_);
    EXPECT_EQ(nalus[1], string(fmt.video_->samples_[0].bytes_, fmt.video_->samples_[0].size_));
}

// An encoder that limits the slice size, for RTP-sized slices, cuts a large frame into many slices. SRS keeps at most
// 256 NALUs of a frame; a frame with more is refused, in either payload format, rather than overflow the sample table.
VOID TEST(KernelPacketTest, FrameWithMoreNalusThanTheLimitIsRefused)
{
    srs_error_t err;

    vector<string> one(1, string("\x41\x9a", 2));
    vector<string> limit(256, string("\x41\x9a", 2));
    vector<string> over(257, string("\x41\x9a", 2));

    if (true) {
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());
        HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh)));

        string tag = packet_avc_ibmf_tag(limit);
        HELPER_EXPECT_SUCCESS(fmt.on_video(40, (char *)tag.data(), (int)tag.size()));
        EXPECT_EQ(SrsAvcPayloadFormatIbmf, fmt.vcodec_->payload_format_);
        EXPECT_EQ(256, fmt.video_->nb_samples_);

        tag = packet_avc_ibmf_tag(over);
        packet_expect_error_code(ERROR_HLS_DECODE_ERROR, fmt.on_video(80, (char *)tag.data(), (int)tag.size()));
    }
    if (true) {
        SrsFormat fmt;
        HELPER_ASSERT_SUCCESS(fmt.initialize());
        HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh)));

        string tag = packet_avc_annexb_tag(one);
        HELPER_EXPECT_SUCCESS(fmt.on_video(40, (char *)tag.data(), (int)tag.size()));
        EXPECT_EQ(SrsAvcPayloadFormatAnnexb, fmt.vcodec_->payload_format_);

        tag = packet_avc_annexb_tag(over);
        packet_expect_error_code(ERROR_HLS_DECODE_ERROR, fmt.on_video(80, (char *)tag.data(), (int)tag.size()));
    }
}

// An encoder whose sequence header sets lengthSizeMinusOne to 1 writes each NALU after a 2-byte length.
VOID TEST(KernelPacketTest, IbmfFrameWithTwoByteNaluLengths)
{
    srs_error_t err;

    // lengthSizeMinusOne is the low 2 bits of byte 9.
    string sh((const char *)mp4_avc_sh, sizeof(mp4_avc_sh));
    sh[9] = (char)0xfd;

    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)sh.data(), (int)sh.size()));
    EXPECT_EQ(1, fmt.vcodec_->NAL_unit_length_);

    const uint8_t frame[] = {0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21, 0x6c, 0x42, 0x00, 0x02, 0x06, 0x05};
    HELPER_ASSERT_SUCCESS(fmt.on_video(40, (char *)frame, sizeof(frame)));

    ASSERT_EQ(2, fmt.video_->nb_samples_);
    EXPECT_EQ(string("\x41\x9a\x21\x6c\x42", 5), string(fmt.video_->samples_[0].bytes_, fmt.video_->samples_[0].size_));
    EXPECT_EQ(string("\x06\x05", 2), string(fmt.video_->samples_[1].bytes_, fmt.video_->samples_[1].size_));
}

// A frame cut short, whose last NALU length says more bytes than the frame has left, is refused rather than read past
// the end of the message. Either payload format is tried first, so the frame is refused as AnnexB too.
VOID TEST(KernelPacketTest, IbmfFrameCutInsideItsLastNaluIsRefused)
{
    srs_error_t err;

    SrsFormat fmt;
    HELPER_ASSERT_SUCCESS(fmt.initialize());
    HELPER_ASSERT_SUCCESS(fmt.on_video(0, (char *)mp4_avc_sh, sizeof(mp4_avc_sh)));

    // A NALU of 5 bytes, then a NALU whose length says 5 bytes but only 3 follow.
    const uint8_t frame[] = {0x27, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21, 0x6c, 0x42,
                             0x00, 0x00, 0x00, 0x05, 0x41, 0x9a, 0x21};
    packet_expect_error_code(ERROR_HLS_DECODE_ERROR, fmt.on_video(40, (char *)frame, sizeof(frame)));

    // The same frame without the cut NALU passes.
    HELPER_EXPECT_SUCCESS(fmt.on_video(40, (char *)frame, sizeof(frame) - 7));
    EXPECT_EQ(1, fmt.video_->nb_samples_);
}

// The RTMP to WebRTC bridge reads first_mb_in_slice and slice_type of each H.264 slice to drop B slices. A slice cut
// before its slice_type is refused.
VOID TEST(KernelPacketTest, RtcAvcSliceHeaderCutShortIsRefused)
{
    srs_error_t err;

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(string((const char *)mp4_avc_sh, sizeof(mp4_avc_sh))));

    // The non-IDR slice: first_mb_in_slice ue(v) "1" is 0, then the byte ends before slice_type.
    vector<SrsNaluSample *> kept;
    packet_expect_error_code(ERROR_AVC_NALU_UEV, bridge.video(packet_avc_ibmf_tag(vector<string>(1, string("\x41\x80", 2))), kept));

    // The header of a P slice: first_mb_in_slice "1" is 0, slice_type "1" is 0, P.
    HELPER_EXPECT_SUCCESS(bridge.video(packet_avc_ibmf_tag(vector<string>(1, string("\x41\xc0", 2))), kept));
    EXPECT_EQ(1, (int)kept.size());
}

// An HEVC slice cut inside its header, before slice_pic_parameter_set_id or slice_type ends, is refused by the bridge.
VOID TEST(KernelPacketTest, RtcHevcSliceHeaderCutShortIsRefused)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(sh));

    vector<SrsNaluSample *> kept;
    // first_slice_segment_in_pic_flag, then 7 zero bits of slice_pic_parameter_set_id and no more.
    packet_expect_error_code(ERROR_HEVC_NALU_UEV, bridge.video(packet_hevc_trail_r_tag("1"), kept));
    // first_slice_segment_in_pic_flag, slice_pic_parameter_set_id 0, then 6 zero bits of slice_type and no more.
    packet_expect_error_code(ERROR_HEVC_NALU_UEV, bridge.video(packet_hevc_trail_r_tag("11"), kept));

    // The whole header of a P slice: slice_type ue(v) "010" is 1, then the rbsp stop bit.
    HELPER_EXPECT_SUCCESS(bridge.video(packet_hevc_trail_r_tag("1" "1" "010" "1"), kept));
    EXPECT_EQ(1, (int)kept.size());
}

// HEVC allows 64 PPS ids, 0 to 63. A slice naming PPS 64, as a corrupt slice header gives, is refused, not looked up
// past the end of the PPS table.
VOID TEST(KernelPacketTest, RtcHevcSliceWithPpsIdOutOfRangeIsRefused)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(sh));

    // first_slice_segment_in_pic_flag, then slice_pic_parameter_set_id ue(v) "0000001" "000001" is 63+1.
    vector<SrsNaluSample *> kept;
    packet_expect_error_code(ERROR_HEVC_DECODE_ERROR, bridge.video(packet_hevc_trail_r_tag("1" "0000001000001" "1"), kept));
}

// An encoder that enables dependent slice segments in its PPS can send a slice segment that takes its slice_type from
// the slice before it. The bridge does not support it and refuses the frame; the picture's first slice still passes.
VOID TEST(KernelPacketTest, RtcHevcDependentSliceSegmentIsNotSupported)
{
    srs_error_t err;

    // The PPS's first byte with dependent_slice_segments_enabled_flag set: id "1", SPS id "1", then the flag "1".
    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh, 0xe1));

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(sh));
    EXPECT_EQ(1, bridge.builder_.format_->vcodec_->hevc_dec_conf_record_.pps_table_[0].dependent_slice_segments_enabled_flag_);

    // The first slice of a picture has no dependent_slice_segment_flag: a P slice passes.
    vector<SrsNaluSample *> kept;
    HELPER_EXPECT_SUCCESS(bridge.video(packet_hevc_trail_r_tag("1" "1" "010" "1"), kept));
    EXPECT_EQ(1, (int)kept.size());

    // A later slice segment: first_slice_segment_in_pic_flag 0, PPS 0, dependent_slice_segment_flag 1.
    packet_expect_error_code(ERROR_HEVC_DECODE_ERROR, bridge.video(packet_hevc_trail_r_tag("0" "1" "1" "1"), kept));
}

// A PPS with num_extra_slice_header_bits 2 puts two reserved bits in each slice header before slice_type. The bridge
// must skip them to read the right slice_type: drop the B slice and keep the P slice.
VOID TEST(KernelPacketTest, RtcHevcExtraSliceHeaderBitsAreSkipped)
{
    srs_error_t err;

    // The PPS's first byte with num_extra_slice_header_bits "010": id "1", SPS id "1", "0", "0", "010", "1".
    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh, 0xc5));

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(sh));
    EXPECT_EQ(2, bridge.builder_.format_->vcodec_->hevc_dec_conf_record_.pps_table_[0].num_extra_slice_header_bits_);

    // first_slice_segment_in_pic_flag, PPS 0, the reserved bits "11", slice_type ue(v) "010" is P.
    vector<SrsNaluSample *> kept;
    HELPER_EXPECT_SUCCESS(bridge.video(packet_hevc_trail_r_tag("1" "1" "11" "010" "1"), kept));
    EXPECT_EQ(1, (int)kept.size());

    // The reserved bits "00", slice_type ue(v) "1" is 0, B.
    HELPER_EXPECT_SUCCESS(bridge.video(packet_hevc_trail_r_tag("1" "1" "00" "1" "1"), kept));
    EXPECT_EQ(0, (int)kept.size());
}

// Bug: an HEVC slice of only its 2-byte NALU header passes the RTMP demuxer, then the bridge reads a bit of its slice
// header from no bytes and srs_assert aborts the server. A publisher can crash SRS with one such frame when the vhost
// converts RTMP to WebRTC. The frame must be refused with an error, as a slice cut inside its header is.
VOID TEST(KernelPacketTest, DISABLED_RtcHevcSliceWithOnlyNaluHeader_ServerAborts)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(sh));

    vector<SrsNaluSample *> kept;
    HELPER_EXPECT_FAILED(bridge.video(packet_hevc_trail_r_tag(""), kept));
}

// Bug: a slice that is not the first of its picture has slice_segment_address before slice_type, Ceil(Log2(
// PicSizeInCtbsY)) bits (H.265 7.3.6.1). The bridge does not skip it and reads slice_type from the address bits, so a
// P slice whose address has its top bit set reads as B and is dropped: the WebRTC player loses that part of the
// picture. Here the third slice of a 720p P frame cut into three slices.
VOID TEST(KernelPacketTest, DISABLED_RtcHevcLaterSliceOfPFrame_DroppedAsBframe)
{
    srs_error_t err;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));

    PacketRtcBridge bridge;
    HELPER_ASSERT_SUCCESS(bridge.initialize());
    HELPER_ASSERT_SUCCESS(bridge.sequence_header(sh));

    // The picture size in CTBs, and the bits of slice_segment_address, from the stream's SPS.
    SrsHevcRbspSps *sps = &bridge.builder_.format_->vcodec_->hevc_dec_conf_record_.sps_table_[0];
    int ctb = 1 << (sps->log2_min_luma_coding_block_size_minus3_ + 3 + sps->log2_diff_max_min_luma_coding_block_size_);
    int ctbs = (int)((sps->pic_width_in_luma_samples_ + ctb - 1) / ctb * ((sps->pic_height_in_luma_samples_ + ctb - 1) / ctb));
    int nb_bits = 0;
    while ((1 << nb_bits) < ctbs) {
        nb_bits++;
    }
    EXPECT_EQ(1280u, sps->pic_width_in_luma_samples_);
    EXPECT_EQ(720u, sps->pic_height_in_luma_samples_);

    int address = ctbs * 2 / 3;
    string address_bits;
    for (int i = nb_bits - 1; i >= 0; i--) {
        address_bits.push_back((address >> i) & 1 ? '1' : '0');
    }
    ASSERT_EQ('1', address_bits[0]);

    // first_slice_segment_in_pic_flag 0, PPS 0, slice_segment_address, slice_type ue(v) "010" is P.
    vector<SrsNaluSample *> kept;
    HELPER_EXPECT_SUCCESS(bridge.video(packet_hevc_trail_r_tag("0" "1" + address_bits + "010" "1"), kept));
    EXPECT_EQ(1, (int)kept.size());
}

// The pack header of a GB28181 camera: 14 bytes and 6 stuffing bytes.
string ps_pack_header()
{
    return string("\x00\x00\x01\xba\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05", 20);
}

// The system header fields of the same camera after header_length: rate bound, audio and video bounds, flags.
static string ps_system_fields()
{
    return string("\x80\x98\x09\x04\xe1\x7f", 6);
}

// The system header of the same camera, listing streams 0xe0, 0xc0, 0xbd and 0xbf.
string ps_system_header()
{
    return string("\x00\x00\x01\xbb\x00\x12", 6) + ps_system_fields() +
           string("\xe0\xe0\x80\xc0\xc0\x08\xbd\xe0\x80\xbf\xe0\x80", 12);
}

// A system header whose header_length covers the fields and the bytes after them.
static string ps_system_header_with(const string &after_fields)
{
    int length = 6 + (int)after_fields.size();
    string h("\x00\x00\x01\xbb", 4);
    h.push_back((char)((length >> 8) & 0xff));
    h.push_back((char)(length & 0xff));
    return h + ps_system_fields() + after_fields;
}

// A PES packet of the stream id, with PES_packet_length as the size of the body.
string ps_pes(uint8_t sid, const string &body)
{
    string pes("\x00\x00\x01", 3);
    pes.push_back((char)sid);
    pes.push_back((char)((body.size() >> 8) & 0xff));
    pes.push_back((char)(body.size() & 0xff));
    return pes + body;
}

// A video or audio PES with a PTS, holding the payload.
static string ps_media_pes(uint8_t sid, const string &payload)
{
    return ps_pes(sid, string("\x80\x80\x05\x21\x00\x01\x00\x01", 8) + payload);
}

// An H.264 IDR slice in AnnexB, as a camera puts it in a video PES.
static string ps_h264_idr()
{
    return string("\x00\x00\x00\x01\x65\x88\x84\x00\x33\xff", 10);
}

// A GB28181 camera with G.711 A-law audio lists it in its PSM as stream type 0x90, which the PS demuxer does not know.
// The PSM sets the video type and leaves the audio type unset; the audio PES still reaches the muxer, which decides.
VOID TEST(KernelPSTest, CameraWithG711AudioLeavesPsmAudioTypeUnset)
{
    srs_error_t err;

    // PSM: current, version 0, marker; no program info; H.264 on 0xe0 and G.711 A-law on 0xc0, no descriptors; CRC.
    string psm = ps_pes(0xbc, string("\x80\x01\x00\x00\x00\x08\x1b\xe0\x00\x00\x90\xc0\x00\x00\x00\x00\x00\x00", 18));
    string pack = ps_pack_header() + ps_system_header() + psm + ps_media_pes(0xe0, ps_h264_idr()) +
                  ps_media_pes(0xc0, string(160, '\xd5'));

    MockPsHandler handler;
    SrsPsContext context;
    SrsBuffer b((char *)pack.data(), (int)pack.size());
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));

    EXPECT_EQ(SrsTsStreamVideoH264, context.video_stream_type_);
    EXPECT_EQ(SrsTsStreamReserved, context.audio_stream_type_);
    ASSERT_EQ(2, (int)handler.msgs_.size());
    EXPECT_TRUE(handler.msgs_[0]->is_video());
    EXPECT_TRUE(handler.msgs_[1]->is_audio());
    EXPECT_EQ(160, handler.msgs_[1]->payload_->length());
}

// A PSM from a non-compliant or corrupted device is refused: the PS pack fails, so the GB28181 connection goes to
// recover mode, and the stream types the PSM would set stay as they were.
VOID TEST(KernelPSTest, CorruptPsmIsRefused)
{
    struct {
        const char *name;
        string payload;
    } cases[] = {
        {"shorter than its fixed fields", string("\x80\x01\x00", 3)},
        {"next PSM, not yet applicable", string("\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00", 10)},
        {"marker bit cleared", string("\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00", 10)},
        {"program info past the end", string("\x80\x01\x00\x10\x00\x00", 6)},
        {"no ES map length", string("\x80\x01\x00\x00", 4)},
        {"ES map past the end", string("\x80\x01\x00\x00\x00\x10\x1b\xe0\x00\x00", 10)},
        {"ES entry cut short", string("\x80\x01\x00\x00\x00\x02\x1b\xe0\x00\x00\x00\x00", 12)},
        {"ES info past the map", string("\x80\x01\x00\x00\x00\x04\x1b\xe0\x00\x08\x00\x00\x00\x00", 14)},
        {"no CRC", string("\x80\x01\x00\x00\x00\x04\x1b\xe0\x00\x00", 10)},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        SCOPED_TRACE(cases[i].name);

        string pack = ps_pack_header() + ps_pes(0xbc, cases[i].payload);

        MockPsHandler handler;
        SrsPsContext context;
        SrsBuffer b((char *)pack.data(), (int)pack.size());
        packet_expect_error_code(ERROR_GB_PS_HEADER, context.decode(&b, &handler));

        EXPECT_EQ(SrsTsStreamReserved, context.video_stream_type_);
        EXPECT_EQ(SrsTsStreamReserved, context.audio_stream_type_);
    }
}

// The muxer cannot take a message, such as while it connects its RTMP output: decoding fails with the muxer's error, so
// the connection recovers, and the next pack decodes from a fresh message.
VOID TEST(KernelPSTest, MuxerRefusingMessageFailsDecode)
{
    srs_error_t err;

    MockPsHandler handler;
    handler.error_code_ = ERROR_GB_RTMP_CONNECTING;
    SrsPsContext context;

    string pack = ps_pack_header() + ps_media_pes(0xe0, ps_h264_idr());
    if (true) {
        SrsBuffer b((char *)pack.data(), (int)pack.size());
        packet_expect_error_code(ERROR_GB_RTMP_CONNECTING, context.decode(&b, &handler));
        EXPECT_TRUE(handler.msgs_.empty());
    }

    handler.error_code_ = 0;
    if (true) {
        SrsBuffer b((char *)pack.data(), (int)pack.size());
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ(1, (int)handler.msgs_.size());
        SrsTsMessage *msg = handler.msgs_[0];
        EXPECT_TRUE(msg->is_video());
        EXPECT_EQ(ps_h264_idr(), string(msg->payload_->bytes(), msg->payload_->length()));
    }
}

// The GB28181 TCP connection keeps a PS header cut at the end of an RTP packet only when it is 32 bytes or less. A pack
// whose RTP packet ends inside its headers after more bytes is refused whole, and the stream goes back to the pack
// start, for the recover mode to skip from there.
VOID TEST(KernelPSTest, PackCutInsideItsHeadersIsRefusedAndRewound)
{
    struct {
        const char *name;
        string bytes;
    } cases[] = {
        {"inside the system header", ps_pack_header() + ps_system_header().substr(0, 18)},
        {"in the PES start code after the system header", ps_pack_header() + ps_system_header() + string("\x00\x00", 2)},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        SCOPED_TRACE(cases[i].name);
        EXPECT_LT(32, (int)cases[i].bytes.size());

        MockPsHandler handler;
        SrsPsContext context;
        context.set_detect_ps_integrity(true);
        SrsBuffer b((char *)cases[i].bytes.data(), (int)cases[i].bytes.size());
        packet_expect_error_code(ERROR_GB_PS_HEADER, context.decode(&b, &handler));

        EXPECT_EQ(0, b.pos());
        EXPECT_TRUE(handler.msgs_.empty());
    }
}

// A system header whose header_length ends inside a stream entry, from a corrupted device, is refused.
VOID TEST(KernelPSTest, SystemHeaderWithCutStreamEntryIsRefused)
{
    string pack = ps_pack_header() + ps_system_header_with(string("\xe0\xe0", 2)) + ps_media_pes(0xe0, ps_h264_idr());

    MockPsHandler handler;
    SrsPsContext context;
    SrsBuffer b((char *)pack.data(), (int)pack.size());
    packet_expect_error_code(ERROR_GB_PS_HEADER, context.decode(&b, &handler));
    EXPECT_TRUE(handler.msgs_.empty());
}

// A device pads its system header after the stream list, inside header_length: the list ends at the first byte that is
// not a stream entry, the padding is skipped, and the video after it still decodes.
VOID TEST(KernelPSTest, SystemHeaderPaddingAfterStreamListIsSkipped)
{
    srs_error_t err;

    string pack = ps_pack_header() + ps_system_header_with(string("\xe0\xe0\x80\x7f\xff", 5)) +
                  ps_media_pes(0xe0, ps_h264_idr());

    MockPsHandler handler;
    SrsPsContext context;
    SrsBuffer b((char *)pack.data(), (int)pack.size());
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));

    SrsPsPacket *ps = context.helper()->ps_;
    EXPECT_TRUE(ps->has_system_header_);
    EXPECT_EQ(0xe0, (int)ps->video_stream_id_);
    EXPECT_EQ(0, (int)ps->audio_stream_id_);
    ASSERT_EQ(1, (int)handler.msgs_.size());
    EXPECT_TRUE(handler.msgs_[0]->is_video());
}

// Bug: the GB28181 TCP connection keeps the bytes of a PS header cut at the end of an RTP packet, up to 32 bytes, to
// parse with the next packet, but the PS decoder holds them only once 4 bytes or more came. When the RTP packet ends 1
// to 3 bytes into the next PES start code, decoding fails, the connection goes to recover mode and drops the pack.
VOID TEST(KernelPSTest, DISABLED_PesStartCodeCutAfterTwoBytes_PackDropped)
{
    srs_error_t err;

    MockPsHandler handler;
    SrsPsContext context;
    context.set_detect_ps_integrity(true);

    // A slice larger than the 32 bytes the decoder holds for the next packet.
    string video = ps_media_pes(0xe0, ps_h264_idr() + string(200, '\x5a'));

    // The first RTP payload ends with 2 bytes of the video PES start code, which the connection keeps.
    string first = ps_pack_header() + ps_media_pes(0xc0, string(160, '\xd5')) + video.substr(0, 2);
    SrsBuffer b((char *)first.data(), (int)first.size());
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
    ASSERT_EQ(2, b.left());

    // The next RTP payload follows the kept bytes.
    SrsBuffer b2((char *)video.data(), (int)video.size());
    HELPER_ASSERT_SUCCESS(context.decode(&b2, &handler));
    ASSERT_EQ(2, (int)handler.msgs_.size());
    EXPECT_TRUE(handler.msgs_[0]->is_audio());
    EXPECT_TRUE(handler.msgs_[1]->is_video());
}

// At startup the server starts the RTC and GB connection managers, each with a coroutine that frees
// the removed connections. When State Threads cannot create it (out of memory), start fails with the
// ST error, so the server exits instead of running with connections that are never freed.
VOID TEST(KernelResourceTest, ConnManagerFailsToStartWhenNoCoroutineCanBeCreated)
{
    srs_error_t err = srs_success;

    SrsResourceManager manager("RTC", true);
    manager.assemble();

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)hourglass_st_thread_create_out_of_memory;
    err = manager.start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("conn manager") != string::npos);
    srs_freep(err);

    // Nothing frees a removed connection, even after this test yields.
    MockSrsResource *conn = new MockSrsResource();
    manager.add(conn);
    manager.remove(conn);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, (int)manager.size());
    EXPECT_EQ(1, (int)manager.zombies_.size());

    // The manager frees its zombies when it is destroyed, in the context of its own id.
    manager.cid_ = _srs_context->get_id();
}

// A WebRTC session subscribes to the connection manager in assemble() and unsubscribes in its
// destructor. The manager frees a disposed session after its clear has dropped the unsubscribed
// handlers, so the freed session's address stays in the unsubscribed list until the next clear. When
// malloc gives that address to the next session, which subscribes before then, the new session is a
// live handler and must get the dispose events of the other sessions.
VOID TEST(KernelResourceTest, NewSessionAtFreedSessionAddressGetsDisposeEvents)
{
    SrsResourceManager manager("RTC", true);
    manager.assemble();
    manager.cid_ = _srs_context->get_id();

    // One handler object stands for the old session and the new session at the same address.
    MockSrsDisposingHandler session;
    manager.subscribe(&session);

    // The old session is freed by the manager's clear, and its destructor unsubscribes.
    MockSrsResource *old_session = new MockSrsResource();
    manager.add_with_name("old", old_session);
    manager.remove(old_session);
    manager.clear();
    manager.unsubscribe(&session);
    ASSERT_EQ(1, (int)session.disposing_calls_.size());

    // The new session at the same address subscribes, before the manager clears again.
    manager.subscribe(&session);

    // Another session leaves: the new session hears of it, before and while it is disposed.
    MockSrsResource *other = new MockSrsResource();
    manager.add_with_name("other", other);
    manager.remove(other);
    ASSERT_EQ(2, (int)session.before_dispose_calls_.size());
    EXPECT_TRUE(other == session.before_dispose_calls_.at(1));

    manager.clear();
    ASSERT_EQ(2, (int)session.disposing_calls_.size());
    EXPECT_TRUE(other == session.disposing_calls_.at(1));
    EXPECT_TRUE(manager.empty());

    manager.unsubscribe(&session);
}

#ifdef SRS_GB28181
static int resource_trace_count(const vector<string> &traces, const string &text)
{
    int count = 0;
    for (int i = 0; i < (int)traces.size(); i++) {
        if (traces.at(i).find(text) != string::npos) {
            count++;
        }
    }
    return count;
}

// A GB28181 publish API call creates a session, held by a shared resource in the verbose GB manager
// and run by an executor coroutine. When the session ends, the executor and then the shared resource
// are disposed, and the manager logs each by its description, which both take from the session the
// shared resource holds. The device id is then free for the device to publish again.
VOID TEST(KernelResourceTest, GbSessionEndsAndManagerLogsItBySessionDesc)
{
    srs_error_t err = srs_success;

    SrsResourceManager manager("GB", true);
    manager.assemble();
    manager.cid_ = _srs_context->get_id();

    MockAppFactoryForGbPublish factory;
    SrsConfDirective conf;
    SrsGoApiGbPublish api(&conf);
    api.gb_manager_ = &manager;
    api.app_factory_ = &factory;

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;

    MockGbSessionForApiPublish *session = factory.mock_gb_session_;
    err = api.bind_session("34020000001320000001", 1234567890);
    if (err != srs_success) {
        _srs_log = original;
    }
    HELPER_ASSERT_SUCCESS(err);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(manager.find_by_id("34020000001320000001") != NULL);

    // The session ends, so its executor removes itself from the manager.
    session->owner_coroutine_->interrupt();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Disposing the executor removes the shared resource, and the next clear disposes it.
    manager.clear();
    manager.clear();
    _srs_log = original;

    // The executor and the shared resource each log the session's description, before and while disposed.
    EXPECT_EQ(2, resource_trace_count(log.traces_, "GB: before dispose resource(MockGbSessionForApiPublish)"));
    EXPECT_EQ(2, resource_trace_count(log.traces_, "GB: disposing #0 resource(MockGbSessionForApiPublish)"));
    EXPECT_TRUE(manager.empty());
    EXPECT_TRUE(manager.find_by_id("34020000001320000001") == NULL);
    EXPECT_TRUE(manager.find_by_fast_id(1234567890) == NULL);
}
#endif

// A WebRTC publisher's video track as SrsRtcPublishStream builds it: a 1000-packet ring buffer and a NACK list of
// up to 666 lost packets, with the circuit breaker below its high water level so NACK is on.
class RtcQueueVideoTrack
{
public:
    MockRtcPacketReceiver receiver_;
    MockCircuitBreaker breaker_;
    SrsRtcVideoRecvTrack *track_;

public:
    RtcQueueVideoTrack()
    {
        SrsRtcTrackDescription desc;
        desc.type_ = "video";
        desc.id_ = "video-track";
        desc.ssrc_ = 0x2345;
        desc.is_active_ = true;
        track_ = new SrsRtcVideoRecvTrack(&receiver_, &desc, false);
        breaker_.hybrid_high_water_level_ = false;
        track_->circuit_breaker_ = &breaker_;
    }
    ~RtcQueueVideoTrack()
    {
        srs_freep(track_);
    }

public:
    // The publisher's RTP packet arrives; the track keeps a copy in its ring buffer.
    srs_error_t receive(uint16_t seq)
    {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(0x2345);
        pkt.header_.set_sequence(seq);
        SrsRtpPacket *p = &pkt;
        return track_->on_nack(&p);
    }
    // The 20ms NACK timer asks for the lost packets that are due.
    vector<uint16_t> due_nacks(uint32_t &timeout_nacks)
    {
        SrsRtcpNack nack(0x2345);
        track_->nack_receiver_->get_nack_seqs(nack, timeout_nacks);
        return nack.get_lost_sns();
    }
    // Make every lost packet due at each timer tick, as if each tick came after every NACK interval.
    void nack_at_every_tick()
    {
        SrsNackOption &opts = track_->nack_receiver_->opts_;
        opts.first_nack_interval_ = 0;
        opts.nack_interval_ = 0;
        opts.min_nack_interval_ = 0;
        opts.nack_check_interval_ = 0;
    }
};

// A publisher's RTP sequence numbers wrap from 65535 to 0 every 65536 packets, about every 10 minutes for a 1 Mbps
// video. When packet 65535 is lost at the wrap, the next packet 0 counts the wrap in the high 16 bits of the
// receiver report's extended highest sequence, and 65535 is NACKed.
VOID TEST(KernelRtcQueueTest, PublisherSequenceWrapsWithLastPacketLost)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    SrsRtpRingBuffer *queue = t.track_->rtp_queue_;
    SrsRtpNackForReceiver *nack = t.track_->nack_receiver_;

    HELPER_ASSERT_SUCCESS(t.receive(65533));
    HELPER_ASSERT_SUCCESS(t.receive(65534));
    EXPECT_EQ(65534u, queue->get_extended_highest_sequence());

    HELPER_ASSERT_SUCCESS(t.receive(0));
    EXPECT_EQ(65536u, queue->get_extended_highest_sequence());
    EXPECT_TRUE(nack->find(65535) != NULL);
    EXPECT_EQ(1u, nack->queue_.size());

    HELPER_ASSERT_SUCCESS(t.receive(1));
    EXPECT_EQ(65537u, queue->get_extended_highest_sequence());
    EXPECT_EQ(65533, queue->begin_);
    EXPECT_EQ(5, queue->size());

    // The lost packet arrives late; it does not count the wrap again.
    HELPER_ASSERT_SUCCESS(t.receive(65535));
    EXPECT_TRUE(nack->queue_.empty());
    EXPECT_EQ(65537u, queue->get_extended_highest_sequence());
}

// B10: when packet 65535 arrives in order, the end of the ring buffer wraps to 0 before packet 0 arrives, so packet
// 0 is not seen as a wrap. The receiver report's extended highest sequence is then 0xffffffff after 65535 and 0
// after packet 0, instead of 65535 and 65536: every in-order wrap is lost, and the number jumps back.
VOID TEST(KernelRtcQueueTest, DISABLED_PublisherSequenceWrapsInOrder_WrapNotCounted)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    SrsRtpRingBuffer *queue = t.track_->rtp_queue_;

    HELPER_ASSERT_SUCCESS(t.receive(65534));
    HELPER_ASSERT_SUCCESS(t.receive(65535));
    EXPECT_EQ(65535u, queue->get_extended_highest_sequence());

    HELPER_ASSERT_SUCCESS(t.receive(0));
    EXPECT_EQ(65536u, queue->get_extended_highest_sequence());
    EXPECT_TRUE(t.track_->nack_receiver_->queue_.empty());
}

// The first packets of a publisher can arrive out of order, so a packet older than the first one received arrives
// next. The packets between it and the first one are lost or still on the way, so they go in the NACK list, and
// the ring buffer starts from the older packet. A NACKed packet that arrives is taken off the list.
VOID TEST(KernelRtcQueueTest, PublisherStartupPacketsOutOfOrderNackTheGap)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    SrsRtpRingBuffer *queue = t.track_->rtp_queue_;
    SrsRtpNackForReceiver *nack = t.track_->nack_receiver_;

    HELPER_ASSERT_SUCCESS(t.receive(100));
    HELPER_ASSERT_SUCCESS(t.receive(97));
    EXPECT_EQ(97, queue->begin_);
    EXPECT_EQ(101, queue->end_);
    EXPECT_TRUE(nack->find(98) != NULL);
    EXPECT_TRUE(nack->find(99) != NULL);
    EXPECT_TRUE(nack->find(100) == NULL);
    EXPECT_TRUE(queue->at(97) != NULL);

    // 98 arrives late, 99 stays lost.
    HELPER_ASSERT_SUCCESS(t.receive(98));
    EXPECT_TRUE(nack->find(98) == NULL);
    EXPECT_TRUE(nack->find(99) != NULL);
    EXPECT_TRUE(queue->at(98) != NULL);

    // The highest sequence for the receiver report is still the first packet's.
    EXPECT_EQ(100u, queue->get_extended_highest_sequence());
}

// B9: a packet older than the ring buffer's start, such as a startup packet out of order, puts itself in the NACK
// list with the gap: the range is [seq, begin), while a packet after the end gives [end, seq). The server then
// NACKs a packet it already has, so the publisher resends it, until the NACK times out after 15 requests or 1s.
VOID TEST(KernelRtcQueueTest, DISABLED_PublisherStartupPacketOutOfOrder_ArrivedPacketNacked)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    t.nack_at_every_tick();

    HELPER_ASSERT_SUCCESS(t.receive(100));
    HELPER_ASSERT_SUCCESS(t.receive(99));
    EXPECT_TRUE(t.track_->nack_receiver_->find(99) == NULL);

    uint32_t timeout_nacks = 0;
    vector<uint16_t> seqs = t.due_nacks(timeout_nacks);
    EXPECT_TRUE(seqs.empty());
}

// A publisher that jumps its sequence forward, such as an encoder restart that keeps the SSRC, or a loss burst of
// more packets than the NACK list holds, fills the list. The track then gives up: the NACK list is cleared, the
// buffered packets are freed, and the ring buffer starts again from the next packet, which NACKs nothing.
VOID TEST(KernelRtcQueueTest, PublisherSequenceJumpFillsNackListAndResetsQueue)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    SrsRtpRingBuffer *queue = t.track_->rtp_queue_;
    SrsRtpNackForReceiver *nack = t.track_->nack_receiver_;

    for (uint16_t seq = 1; seq <= 10; seq++) {
        HELPER_ASSERT_SUCCESS(t.receive(seq));
    }
    EXPECT_TRUE(queue->at(1) != NULL);

    // 11 to 899 lost: 889 packets, over the 666 the list holds.
    HELPER_ASSERT_SUCCESS(t.receive(900));
    EXPECT_TRUE(nack->queue_.empty());
    for (uint16_t seq = 1; seq <= 10; seq++) {
        EXPECT_TRUE(queue->at(seq) == NULL);
    }
    EXPECT_TRUE(queue->at(900) != NULL);

    // The next packet starts the ring buffer again.
    HELPER_ASSERT_SUCCESS(t.receive(901));
    EXPECT_EQ(901, queue->begin_);
    EXPECT_EQ(902, queue->end_);
    EXPECT_TRUE(nack->queue_.empty());

    // A loss after the restart is NACKed as usual.
    HELPER_ASSERT_SUCCESS(t.receive(903));
    EXPECT_TRUE(nack->find(902) != NULL);
}

// A lost packet the publisher never resends, such as one its own buffer no longer holds, is NACKed at each timer
// tick up to 15 times more. Then the track gives it up: it is counted as a timed-out NACK, no longer requested, and
// the ring buffer moves past it.
VOID TEST(KernelRtcQueueTest, LostPacketNeverResentGivenUpAfterMaxNacks)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    t.nack_at_every_tick();
    SrsRtpRingBuffer *queue = t.track_->rtp_queue_;

    HELPER_ASSERT_SUCCESS(t.receive(1));
    HELPER_ASSERT_SUCCESS(t.receive(3));
    EXPECT_EQ(1, queue->begin_);

    uint32_t timeout_nacks = 0;
    for (int i = 0; i < 16; i++) {
        vector<uint16_t> seqs = t.due_nacks(timeout_nacks);
        ASSERT_EQ(1, (int)seqs.size()) << "tick " << i;
        EXPECT_EQ(2, seqs.at(0));
    }
    EXPECT_EQ(0u, timeout_nacks);

    vector<uint16_t> seqs = t.due_nacks(timeout_nacks);
    EXPECT_TRUE(seqs.empty());
    EXPECT_EQ(1u, timeout_nacks);
    EXPECT_TRUE(t.track_->nack_receiver_->find(2) == NULL);
    EXPECT_EQ(3, queue->begin_);
    EXPECT_TRUE(queue->at(3) != NULL);

    // Nothing more to request or time out.
    seqs = t.due_nacks(timeout_nacks);
    EXPECT_TRUE(seqs.empty());
    EXPECT_EQ(1u, timeout_nacks);
}

// The shared 20ms timer can tick again less than 20ms after the last NACK check, when one tick ran late and the next
// is on time. That tick sends no NACK, so a lost packet is not requested twice in a row.
VOID TEST(KernelRtcQueueTest, NackTimerTicksEarlyRequestsNothing)
{
    srs_error_t err = srs_success;
    RtcQueueVideoTrack t;
    t.nack_at_every_tick();
    t.track_->nack_receiver_->opts_.nack_check_interval_ = 20 * SRS_UTIME_MILLISECONDS;

    HELPER_ASSERT_SUCCESS(t.receive(1));
    HELPER_ASSERT_SUCCESS(t.receive(3));

    uint32_t timeout_nacks = 0;
    vector<uint16_t> seqs = t.due_nacks(timeout_nacks);
    ASSERT_EQ(1, (int)seqs.size());
    EXPECT_EQ(2, seqs.at(0));

    // The next tick, at once.
    seqs = t.due_nacks(timeout_nacks);
    EXPECT_TRUE(seqs.empty());
    EXPECT_EQ(1, t.track_->nack_receiver_->find(2)->req_nack_count_);
    EXPECT_EQ(0u, timeout_nacks);
}

static string rtcp_be16(uint16_t v)
{
    char b[2] = {(char)(v >> 8), (char)(v & 0xff)};
    return string(b, 2);
}

static string rtcp_be32(uint32_t v)
{
    return rtcp_be16((uint16_t)(v >> 16)) + rtcp_be16((uint16_t)(v & 0xffff));
}

// One RTCP packet: version 2, the count or feedback format, the packet type, the sender's SSRC, and a body of whole
// 32-bit words.
static string rtcp_packet(uint8_t rc, uint8_t pt, uint32_t sender_ssrc, const string &body)
{
    string p;
    p.push_back((char)(0x80 | rc));
    p.push_back((char)pt);
    p += rtcp_be16((uint16_t)((8 + body.size()) / 4 - 1));
    p += rtcp_be32(sender_ssrc);
    return p + body;
}

// A report block: fraction lost 1/256, 2 packets lost, the highest sequence, jitter 30, no LSR and DLSR yet.
static string rtcp_report_block(uint32_t ssrc, uint32_t highest_sn)
{
    return rtcp_be32(ssrc) + rtcp_be32(0x01000002) + rtcp_be32(highest_sn) + rtcp_be32(30) + rtcp_be32(0) + rtcp_be32(0);
}

// The packets of one compound RTCP datagram from a peer, in wire order, decoded as SrsRtcConnection::on_rtcp does.
class RtcpDatagram
{
public:
    string bytes_;
    vector<SrsRtcpCommon *> packets_;

public:
    RtcpDatagram(const string &bytes) : bytes_(bytes) {}
    ~RtcpDatagram()
    {
        for (size_t i = 0; i < packets_.size(); i++) {
            srs_freep(packets_[i]);
        }
    }
    srs_error_t decode()
    {
        srs_error_t err = srs_success;
        SrsRtcpCompound compound;
        SrsBuffer buffer((char *)bytes_.data(), (int)bytes_.size());
        if ((err = compound.decode(&buffer)) != srs_success) {
            return err;
        }
        SrsRtcpCommon *rtcp = NULL;
        while ((rtcp = compound.get_next_rtcp()) != NULL) {
            packets_.insert(packets_.begin(), rtcp);
        }
        return err;
    }
    // Where packet i starts, for the handlers that parse its raw bytes, such as the publisher's XR handler.
    int offset_of(int i)
    {
        return (int)(packets_.at(i)->data() - bytes_.data());
    }
};

// A Chrome player's receiver feedback in one datagram: its receiver report, a NACK for the packets it lost, and a PLI
// asking for a key frame. SrsRtcConnection::dispatch_rtcp finds the player by each one's media SSRC.
VOID TEST(KernelRtcRtcpTest, PlayerReportWithNackAndPliDecodedPerPacket)
{
    srs_error_t err = srs_success;

    string rr = rtcp_packet(1, SrsRtcpType_rr, 0x1001, rtcp_report_block(0xa001, 5000));
    // PID 100 with BLP 0b101: 100, 101 and 103 lost.
    string nack = rtcp_packet(1, SrsRtcpType_rtpfb, 0x1001, rtcp_be32(0xa001) + rtcp_be16(100) + rtcp_be16(0x0005));
    string pli = rtcp_packet(1, SrsRtcpType_psfb, 0x1001, rtcp_be32(0xa001));
    RtcpDatagram d(rr + nack + pli);
    HELPER_ASSERT_SUCCESS(d.decode());
    ASSERT_EQ(3, (int)d.packets_.size());

    SrsRtcpRR *r = dynamic_cast<SrsRtcpRR *>(d.packets_.at(0));
    ASSERT_TRUE(r != NULL);
    EXPECT_EQ(0xa001u, r->get_rb_ssrc());
    EXPECT_EQ(5000u, r->get_highest_sn());

    SrsRtcpNack *n = dynamic_cast<SrsRtcpNack *>(d.packets_.at(1));
    ASSERT_TRUE(n != NULL);
    EXPECT_EQ(0xa001u, n->get_media_ssrc());
    vector<uint16_t> lost = n->get_lost_sns();
    ASSERT_EQ(3, (int)lost.size());
    EXPECT_EQ(100, lost.at(0));
    EXPECT_EQ(101, lost.at(1));
    EXPECT_EQ(103, lost.at(2));
    EXPECT_EQ((int)rr.size(), d.offset_of(1));

    SrsRtcpPli *p = dynamic_cast<SrsRtcpPli *>(d.packets_.at(2));
    ASSERT_TRUE(p != NULL);
    EXPECT_EQ(0xa001u, p->get_media_ssrc());
    EXPECT_EQ((int)(rr.size() + nack.size()), d.offset_of(2));
}

// A player's bandwidth feedback: a REMB estimate (PSFB format 15) and a transport-wide CC report (RTPFB format 15).
// The connection handles both itself; the TWCC handler gets the packet's raw bytes from data().
VOID TEST(KernelRtcRtcpTest, PlayerBandwidthFeedbackRembAndTwccDecoded)
{
    srs_error_t err = srs_success;

    string rr = rtcp_packet(1, SrsRtcpType_rr, 0x1001, rtcp_report_block(0xa001, 5000));
    // Media SSRC 0, "REMB", 1 SSRC, exponent 0 and mantissa 250000 (250 kbps), the SSRC.
    string remb = rtcp_packet(15, SrsRtcpType_psfb, 0x1001, rtcp_be32(0) + "REMB" + rtcp_be32(0x0103d090) + rtcp_be32(0xa001));
    // Base 200, 2 packets, reference time 10, feedback 3, a run of 2 small deltas, deltas 4 and 4.
    string twcc = rtcp_packet(15, SrsRtcpType_rtpfb, 0x1001,
                              rtcp_be32(0xa001) + rtcp_be16(200) + rtcp_be16(2) + rtcp_be32((10 << 8) | 3) + rtcp_be16(0x2002) + rtcp_be16(0x0404));
    RtcpDatagram d(rr + remb + twcc);
    HELPER_ASSERT_SUCCESS(d.decode());
    ASSERT_EQ(3, (int)d.packets_.size());

    SrsRtcpFbCommon *e = dynamic_cast<SrsRtcpFbCommon *>(d.packets_.at(1));
    ASSERT_TRUE(e != NULL);
    EXPECT_TRUE(dynamic_cast<SrsRtcpPli *>(e) == NULL);
    EXPECT_EQ(SrsRtcpType_psfb, e->type());
    EXPECT_EQ(15, e->get_rc());
    EXPECT_EQ(0u, e->get_media_ssrc());

    SrsRtcpTWCC *t = dynamic_cast<SrsRtcpTWCC *>(d.packets_.at(2));
    ASSERT_TRUE(t != NULL);
    EXPECT_EQ(SrsRtcpType_rtpfb, t->type());
    EXPECT_EQ(15, t->get_rc());
    EXPECT_EQ((int)(rr.size() + remb.size()), d.offset_of(2));
    ASSERT_EQ((int)twcc.size(), t->size());
    EXPECT_EQ(0, memcmp(twcc.data(), t->data(), twcc.size()));
}

// Feedback SRS does not act on: a TMMBR bitrate request (RTPFB format 3, RFC 5104), and the SLI and RPSI of older
// VP8 endpoints (PSFB formats 2 and 3, RFC 4585). Each is skipped whole, so the PLI after them is still read right.
VOID TEST(KernelRtcRtcpTest, PeerTmmbrSliAndRpsiSkippedWhole)
{
    srs_error_t err = srs_success;

    string tmmbr = rtcp_packet(3, SrsRtcpType_rtpfb, 0x1001, rtcp_be32(0) + rtcp_be32(0xa001) + rtcp_be32(0x0c0186a0));
    // First MB 0, 100 MBs, picture ID 5.
    string sli = rtcp_packet(2, SrsRtcpType_psfb, 0x1001, rtcp_be32(0xa001) + rtcp_be32((0 << 19) | (100 << 6) | 5));
    // 16 padding bits, payload type 96, a 2-byte bit string.
    string rpsi = rtcp_packet(3, SrsRtcpType_psfb, 0x1001, rtcp_be32(0xa001) + rtcp_be32(0x10601234) + rtcp_be32(0x56780000));
    string pli = rtcp_packet(1, SrsRtcpType_psfb, 0x1001, rtcp_be32(0xa001));
    RtcpDatagram d(tmmbr + sli + rpsi + pli);
    HELPER_ASSERT_SUCCESS(d.decode());
    ASSERT_EQ(4, (int)d.packets_.size());

    SrsRtcpFbCommon *m = dynamic_cast<SrsRtcpFbCommon *>(d.packets_.at(0));
    ASSERT_TRUE(m != NULL);
    EXPECT_TRUE(dynamic_cast<SrsRtcpNack *>(m) == NULL);
    EXPECT_EQ(SrsRtcpType_rtpfb, m->type());
    EXPECT_EQ(3, m->get_rc());

    SrsRtcpSli *s = dynamic_cast<SrsRtcpSli *>(d.packets_.at(1));
    ASSERT_TRUE(s != NULL);
    EXPECT_EQ(0xa001u, s->get_media_ssrc());

    SrsRtcpRpsi *r = dynamic_cast<SrsRtcpRpsi *>(d.packets_.at(2));
    ASSERT_TRUE(r != NULL);
    EXPECT_EQ(0xa001u, r->get_media_ssrc());

    SrsRtcpPli *p = dynamic_cast<SrsRtcpPli *>(d.packets_.at(3));
    ASSERT_TRUE(p != NULL);
    EXPECT_EQ(0xa001u, p->get_media_ssrc());
    EXPECT_EQ((int)(tmmbr.size() + sli.size() + rpsi.size()), d.offset_of(3));
}

// A publisher that also receives (sendrecv) puts a report block in its sender report, then its SDES, then the XR DLRR
// answering the server's RRTR. The SR keeps its sender info for the A/V sync; the XR handler reads the XR's raw bytes.
VOID TEST(KernelRtcRtcpTest, PublisherSenderReportWithReportBlockThenXrDecoded)
{
    srs_error_t err = srs_success;

    string sender_info = rtcp_be32(0xe5a1b2c3) + rtcp_be32(0x40000000) + rtcp_be32(90000) + rtcp_be32(120) + rtcp_be32(96000);
    string sr = rtcp_packet(1, SrsRtcpType_sr, 0xb001, sender_info + rtcp_report_block(0x1001, 77));
    string sdes_item = string("\x01\x04" "abcd" "\x00\x00", 8);
    string sdes = rtcp_packet(1, SrsRtcpType_sdes, 0xb001, sdes_item);
    // A DLRR block (type 5, 3 words): the server's SSRC, its last RRTR and the delay since.
    string xr = rtcp_packet(0, SrsRtcpType_xr, 0xb001, rtcp_be32(0x05000003) + rtcp_be32(0x2001) + rtcp_be32(0x12345678) + rtcp_be32(0x00010000));
    RtcpDatagram d(sr + sdes + xr);
    HELPER_ASSERT_SUCCESS(d.decode());
    ASSERT_EQ(3, (int)d.packets_.size());

    SrsRtcpSR *s = dynamic_cast<SrsRtcpSR *>(d.packets_.at(0));
    ASSERT_TRUE(s != NULL);
    EXPECT_EQ(0xb001u, s->get_ssrc());
    EXPECT_EQ(0xe5a1b2c340000000ull, s->get_ntp());
    EXPECT_EQ(90000u, s->get_rtp_ts());
    EXPECT_EQ(120u, s->get_rtp_send_packets());
    EXPECT_EQ(96000u, s->get_rtp_send_bytes());

    EXPECT_EQ(SrsRtcpType_sdes, d.packets_.at(1)->type());
    EXPECT_EQ((int)sr.size(), d.offset_of(1));

    SrsRtcpXr *x = dynamic_cast<SrsRtcpXr *>(d.packets_.at(2));
    ASSERT_TRUE(x != NULL);
    EXPECT_EQ((int)(sr.size() + sdes.size()), d.offset_of(2));
    ASSERT_EQ((int)xr.size(), x->size());
    EXPECT_EQ(0, memcmp(xr.data(), x->data(), xr.size()));
}

// A player that receives audio and video reports both SSRCs in one receiver report. SRS keeps the first block for
// dispatch and skips the others, so the packet after the report is still read right.
VOID TEST(KernelRtcRtcpTest, PlayerReportWithTwoReportBlocksDecodedWhole)
{
    srs_error_t err = srs_success;

    string rr = rtcp_packet(2, SrsRtcpType_rr, 0x1001, rtcp_report_block(0xa001, 5000) + rtcp_report_block(0xa002, 900));
    string pli = rtcp_packet(1, SrsRtcpType_psfb, 0x1001, rtcp_be32(0xa002));
    RtcpDatagram d(rr + pli);
    HELPER_ASSERT_SUCCESS(d.decode());
    ASSERT_EQ(2, (int)d.packets_.size());

    SrsRtcpRR *r = dynamic_cast<SrsRtcpRR *>(d.packets_.at(0));
    ASSERT_TRUE(r != NULL);
    EXPECT_EQ(0xa001u, r->get_rb_ssrc());
    EXPECT_EQ(5000u, r->get_highest_sn());

    SrsRtcpPli *p = dynamic_cast<SrsRtcpPli *>(d.packets_.at(1));
    ASSERT_TRUE(p != NULL);
    EXPECT_EQ(0xa002u, p->get_media_ssrc());
    EXPECT_EQ((int)rr.size(), d.offset_of(1));
}

// A feedback packet whose length field claims more than the datagram holds, as a peer that cuts or corrupts it sends.
// The whole datagram is refused, whatever the feedback type, and nothing after the bad packet is read.
VOID TEST(KernelRtcRtcpTest, FeedbackLongerThanDatagramRefused)
{
    srs_error_t err = srs_success;

    struct {
        uint8_t rc;
        uint8_t pt;
    } types[] = {
        {15, SrsRtcpType_rtpfb}, // TWCC
        {1, SrsRtcpType_rtpfb},  // NACK
        {3, SrsRtcpType_rtpfb},  // TMMBR
        {1, SrsRtcpType_psfb},   // PLI
        {2, SrsRtcpType_psfb},   // SLI
        {3, SrsRtcpType_psfb},   // RPSI
        {15, SrsRtcpType_psfb},  // REMB
        {0, SrsRtcpType_xr},
    };

    string rr = rtcp_packet(1, SrsRtcpType_rr, 0x1001, rtcp_report_block(0xa001, 5000));
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        // Length 5 claims 24 bytes, but only the header and the sender's SSRC arrived.
        string cut = rtcp_packet(types[i].rc, types[i].pt, 0x1001, "");
        cut[3] = 5;
        RtcpDatagram d(rr + cut);
        err = d.decode();
        ASSERT_TRUE(err != srs_success) << "rc=" << (int)types[i].rc << ", pt=" << (int)types[i].pt;
        EXPECT_EQ(ERROR_RTC_RTCP, srs_error_code(err));
        srs_freep(err);
        EXPECT_TRUE(d.packets_.empty());
    }
}

// A WebRTC player can crash the server with one RTCP packet. The SDP answer gives the player the SSRCs SRS sends with.
// An RTPFB packet of a format other than NACK and TWCC, such as a TMMBR, is decoded as SrsRtcpFbCommon. If its sender
// SSRC is one of those SSRCs, SrsRtcConnection::dispatch_rtcp finds the player by it, and SrsRtcPlayStream::on_rtcp
// casts it to SrsRtcpNack. The cast gives NULL, and on_rtcp_nack reads through it.
VOID TEST(KernelRtcRtcpTest, DISABLED_PlayerSendsTmmbrWithServerSsrcAsSender_ServerCrashes)
{
    srs_error_t err = srs_success;

    MockRtcAsyncTaskExecutor exec;
    MockRtcPacketSender sender;
    MockExpire expire;
    SrsContextId cid;
    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    SrsUniquePtr<SrsRtcPlayStream> player(new SrsRtcPlayStream(&exec, &expire, &sender, cid));
    // The player plays a track that SRS sends with SSRC 0xa001.
    conn->players_ssrc_map_[0xa001] = player.get();

    string tmmbr = rtcp_packet(3, SrsRtcpType_rtpfb, 0xa001, rtcp_be32(0) + rtcp_be32(0xa001) + rtcp_be32(0x0c0186a0));
    err = conn->on_rtcp((char *)tmmbr.data(), (int)tmmbr.size());
    conn->players_ssrc_map_.clear();

    // An unsupported feedback is ignored or refused, never a crash.
    srs_freep(err);
    SUCCEED();
}

// One TWCC feedback as the publisher's congestion controller reads it (draft-holmer-rmcat-transport-wide-cc-extensions-01
// section 3.1): the base sequence, the status of each packet, and the arrival time of each received packet.
class TwccFeedbackRead
{
public:
    int size_;
    int length_;
    uint16_t base_sn_;
    uint16_t count_;
    uint32_t reference_time_;
    vector<uint16_t> chunks_;
    // 0 not received, 1 a small delta, 2 a large or negative delta.
    vector<int> statuses_;
    // Each received packet's arrival time, rebuilt from the reference time and the deltas.
    map<uint16_t, srs_utime_t> arrivals_;

public:
    TwccFeedbackRead() : size_(0), length_(0), base_sn_(0), count_(0), reference_time_(0) {}
    srs_error_t parse(char *data, int size)
    {
        SrsBuffer b(data, size);
        size_ = size;
        if (!b.require(20)) {
            return srs_error_new(ERROR_RTC_RTCP, "header");
        }
        b.skip(2);
        length_ = b.read_2bytes();
        b.skip(8);
        base_sn_ = b.read_2bytes();
        count_ = b.read_2bytes();
        reference_time_ = b.read_3bytes();
        b.skip(1);

        while ((int)statuses_.size() < count_) {
            if (!b.require(2)) {
                return srs_error_new(ERROR_RTC_RTCP, "chunk");
            }
            uint16_t c = b.read_2bytes();
            chunks_.push_back(c);
            if ((c & 0x8000) == 0) {
                for (int i = 0; i < (c & 0x1fff); i++) {
                    statuses_.push_back((c >> 13) & 0x03);
                }
            } else if ((c & 0x4000) == 0) {
                for (int i = 0; i < 14; i++) {
                    statuses_.push_back((c >> (13 - i)) & 0x01);
                }
            } else {
                for (int i = 0; i < 7; i++) {
                    statuses_.push_back((c >> (2 * (6 - i))) & 0x03);
                }
            }
        }
        statuses_.resize(count_);

        srs_utime_t t = (srs_utime_t)reference_time_ * 64 * SRS_UTIME_MILLISECONDS;
        for (int i = 0; i < count_; i++) {
            if (statuses_[i] == 0) {
                continue;
            }
            int16_t delta = 0;
            if (statuses_[i] == 1) {
                if (!b.require(1)) {
                    return srs_error_new(ERROR_RTC_RTCP, "small delta");
                }
                delta = (uint8_t)b.read_1bytes();
            } else {
                if (!b.require(2)) {
                    return srs_error_new(ERROR_RTC_RTCP, "large delta");
                }
                delta = (int16_t)b.read_2bytes();
            }
            t += delta * 250;
            arrivals_[(uint16_t)(base_sn_ + i)] = t;
        }
        return srs_success;
    }
};

// Sends one feedback as SrsRtcPublishStream::send_periodic_twcc does, into one UDP datagram, and reads it back.
static srs_error_t twcc_send_feedback(SrsRtcpTWCC &twcc, uint8_t fb_count, TwccFeedbackRead &fb)
{
    srs_error_t err = srs_success;
    char pkt[kMaxUDPDataSize];
    SrsBuffer buffer(pkt, sizeof(pkt));
    twcc.set_feedback_count(fb_count);
    if ((err = twcc.encode(&buffer)) != srs_success) {
        return err;
    }
    return fb.parse(pkt, buffer.pos());
}

// Each packet the publisher sent and SRS received is in the feedback with its arrival time, to the 250us unit, and the
// feedback is whole 32-bit words as its length says.
static void twcc_expect_arrivals(const TwccFeedbackRead &fb, const map<uint16_t, srs_utime_t> &received)
{
    EXPECT_EQ(0, fb.size_ % 4);
    EXPECT_EQ((fb.length_ + 1) * 4, fb.size_);
    ASSERT_EQ(received.size(), fb.arrivals_.size());
    for (map<uint16_t, srs_utime_t>::const_iterator it = received.begin(); it != received.end(); ++it) {
        map<uint16_t, srs_utime_t>::const_iterator got = fb.arrivals_.find(it->first);
        ASSERT_TRUE(got != fb.arrivals_.end()) << "sn=" << it->first;
        srs_utime_t diff = got->second - it->second;
        EXPECT_LE(diff < 0 ? -diff : diff, 125) << "sn=" << it->first;
    }
}

// The publisher's packets come in 1ms apart, then one comes 100ms late, a delta over 63.75ms. The steady run goes
// out as one run-length chunk, and the late packet as a 2-byte delta.
VOID TEST(KernelRtcRtcpTest, TwccFeedbackSteadyRunThenLatePacket)
{
    srs_error_t err = srs_success;
    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);

    // 640ms is 10 reference time units of 64ms.
    srs_utime_t base = 640 * SRS_UTIME_MILLISECONDS;
    map<uint16_t, srs_utime_t> received;
    for (uint16_t sn = 0; sn < 20; sn++) {
        received[sn] = base + sn * SRS_UTIME_MILLISECONDS;
    }
    received[20] = base + 119 * SRS_UTIME_MILLISECONDS;
    for (map<uint16_t, srs_utime_t>::iterator it = received.begin(); it != received.end(); ++it) {
        HELPER_ASSERT_SUCCESS(twcc.recv_packet(it->first, it->second));
    }

    TwccFeedbackRead fb;
    HELPER_ASSERT_SUCCESS(twcc_send_feedback(twcc, 0, fb));
    EXPECT_EQ(0, fb.base_sn_);
    EXPECT_EQ(21, fb.count_);
    EXPECT_EQ(10u, fb.reference_time_);
    ASSERT_EQ(2, (int)fb.chunks_.size());
    EXPECT_EQ(0x2014, fb.chunks_.at(0)); // 20 small deltas
    EXPECT_EQ(0x4001, fb.chunks_.at(1)); // 1 large delta
    twcc_expect_arrivals(fb, received);
    EXPECT_FALSE(twcc.need_feedback());
}

// Every other packet of the publisher is lost. 14 statuses that mix lost and small go out as one 1-bit status vector
// chunk.
VOID TEST(KernelRtcRtcpTest, TwccFeedbackEveryOtherPacketLostUsesOneBitChunk)
{
    srs_error_t err = srs_success;
    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);

    srs_utime_t base = 640 * SRS_UTIME_MILLISECONDS;
    map<uint16_t, srs_utime_t> received;
    for (uint16_t sn = 0; sn <= 14; sn += 2) {
        received[sn] = base + sn * SRS_UTIME_MILLISECONDS;
        HELPER_ASSERT_SUCCESS(twcc.recv_packet(sn, received[sn]));
    }

    TwccFeedbackRead fb;
    HELPER_ASSERT_SUCCESS(twcc_send_feedback(twcc, 0, fb));
    EXPECT_EQ(0, fb.base_sn_);
    EXPECT_EQ(15, fb.count_);
    ASSERT_EQ(2, (int)fb.chunks_.size());
    EXPECT_EQ(0xaaaa, fb.chunks_.at(0)); // 1-bit vector: 1,0,1,0,...,1,0
    EXPECT_EQ(0x2001, fb.chunks_.at(1)); // the last packet, received
    for (int i = 0; i < 15; i++) {
        EXPECT_EQ(i % 2 == 0 ? 1 : 0, fb.statuses_.at(i)) << "sn=" << i;
    }
    twcc_expect_arrivals(fb, received);
}

// A packet is lost and the next two arrive swapped, a negative delta. Statuses that mix lost, small and large go out
// as a 2-bit status vector chunk of 7.
VOID TEST(KernelRtcRtcpTest, TwccFeedbackLossAndReorderUsesTwoBitChunk)
{
    srs_error_t err = srs_success;
    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);

    srs_utime_t base = 640 * SRS_UTIME_MILLISECONDS;
    map<uint16_t, srs_utime_t> received;
    received[0] = base;
    // 1 is lost; 3 arrives before 2.
    received[2] = base + 3 * SRS_UTIME_MILLISECONDS;
    received[3] = base + 2 * SRS_UTIME_MILLISECONDS;
    for (uint16_t sn = 4; sn <= 7; sn++) {
        received[sn] = base + sn * SRS_UTIME_MILLISECONDS;
    }
    for (map<uint16_t, srs_utime_t>::iterator it = received.begin(); it != received.end(); ++it) {
        HELPER_ASSERT_SUCCESS(twcc.recv_packet(it->first, it->second));
    }

    TwccFeedbackRead fb;
    HELPER_ASSERT_SUCCESS(twcc_send_feedback(twcc, 0, fb));
    EXPECT_EQ(8, fb.count_);
    ASSERT_EQ(2, (int)fb.chunks_.size());
    EXPECT_EQ(0xd195, fb.chunks_.at(0)); // 2-bit vector: 1,0,1,2,1,1,1
    EXPECT_EQ(0x2001, fb.chunks_.at(1));
    EXPECT_EQ(2, fb.statuses_.at(3));
    twcc_expect_arrivals(fb, received);
}

// A packet is lost, eight arrive in order, then two arrive swapped. The first 7 statuses go out as a 2-bit chunk when
// the negative delta comes, and the 4 statuses after them stay for the next chunk, which ends the feedback.
VOID TEST(KernelRtcRtcpTest, TwccFeedbackLossThenLaterReorderCarriesStatusesOver)
{
    srs_error_t err = srs_success;
    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);

    srs_utime_t base = 640 * SRS_UTIME_MILLISECONDS;
    map<uint16_t, srs_utime_t> received;
    received[0] = base;
    // 1 is lost; 11 arrives before 10.
    for (uint16_t sn = 2; sn <= 12; sn++) {
        received[sn] = base + sn * SRS_UTIME_MILLISECONDS;
    }
    received[10] = base + 11 * SRS_UTIME_MILLISECONDS;
    received[11] = base + 10 * SRS_UTIME_MILLISECONDS;
    for (map<uint16_t, srs_utime_t>::iterator it = received.begin(); it != received.end(); ++it) {
        HELPER_ASSERT_SUCCESS(twcc.recv_packet(it->first, it->second));
    }

    TwccFeedbackRead fb;
    HELPER_ASSERT_SUCCESS(twcc_send_feedback(twcc, 0, fb));
    EXPECT_EQ(13, fb.count_);
    ASSERT_EQ(2, (int)fb.chunks_.size());
    EXPECT_EQ(0xd155, fb.chunks_.at(0)); // 2-bit vector: 1,0,1,1,1,1,1
    EXPECT_EQ(0xd564, fb.chunks_.at(1)); // 2-bit vector: 1,1,1,1,2,1
    EXPECT_EQ(0, fb.statuses_.at(1));
    EXPECT_EQ(2, fb.statuses_.at(11));
    twcc_expect_arrivals(fb, received);
}

// The feedback ends in 10 statuses that mix lost and small, more than a 2-bit chunk holds. They go out as a 1-bit
// chunk.
VOID TEST(KernelRtcRtcpTest, TwccFeedbackEndingInMixedLossUsesOneBitChunk)
{
    srs_error_t err = srs_success;
    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);

    srs_utime_t base = 640 * SRS_UTIME_MILLISECONDS;
    uint16_t sns[] = {0, 2, 4, 6, 8, 9};
    map<uint16_t, srs_utime_t> received;
    for (int i = 0; i < (int)(sizeof(sns) / sizeof(sns[0])); i++) {
        received[sns[i]] = base + sns[i] * SRS_UTIME_MILLISECONDS;
        HELPER_ASSERT_SUCCESS(twcc.recv_packet(sns[i], received[sns[i]]));
    }

    TwccFeedbackRead fb;
    HELPER_ASSERT_SUCCESS(twcc_send_feedback(twcc, 0, fb));
    EXPECT_EQ(10, fb.count_);
    ASSERT_EQ(1, (int)fb.chunks_.size());
    EXPECT_EQ(0xaab0, fb.chunks_.at(0)); // 1-bit vector: 1,0,1,0,1,0,1,0,1,1
    twcc_expect_arrivals(fb, received);
}

static void twcc_receive_burst(SrsRtcpTWCC &twcc, uint16_t first_sn, int count, map<uint16_t, srs_utime_t> &received)
{
    srs_utime_t base = 640 * SRS_UTIME_MILLISECONDS;
    for (int i = 0; i < count; i++) {
        uint16_t sn = (uint16_t)(first_sn + i);
        received[sn] = base + i * 100;
        srs_error_t err = twcc.recv_packet(sn, received[sn]);
        EXPECT_TRUE(err == srs_success);
        srs_freep(err);
    }
}

static srs_error_t twcc_send_all(SrsRtcpTWCC &twcc, vector<TwccFeedbackRead> &fbs)
{
    srs_error_t err = srs_success;
    for (int i = 0; i < 1024 && twcc.need_feedback(); i++) {
        TwccFeedbackRead fb;
        if ((err = twcc_send_feedback(twcc, (uint8_t)i, fb)) != srs_success) {
            return err;
        }
        fbs.push_back(fb);
    }
    return err;
}

// A high bitrate publisher while the 100ms TWCC timer runs late: 1500 packets wait for one feedback, more than one UDP
// datagram holds. The timer should send feedbacks until all are reported, the second continuing where the first
// stopped. But SrsRtcpTWCC::do_encode stops adding packets when their 1-byte deltas fill the datagram, then requires 2
// bytes for each delta, so the encode fails and clears every packet: the publisher gets no feedback for any of them.
// Any feedback of more than about 725 packets with small deltas fails so.
VOID TEST(KernelRtcRtcpTest, DISABLED_TwccFeedbackForLateTimerBurst_EncodeFailsAndDropsAll)
{
    srs_error_t err = srs_success;
    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);

    map<uint16_t, srs_utime_t> received;
    twcc_receive_burst(twcc, 0, 1500, received);

    vector<TwccFeedbackRead> fbs;
    HELPER_ASSERT_SUCCESS(twcc_send_all(twcc, fbs));
    ASSERT_EQ(2, (int)fbs.size());
    EXPECT_EQ(0, fbs.at(0).base_sn_);
    EXPECT_LE(fbs.at(0).size_, kMaxUDPDataSize);
    EXPECT_EQ(fbs.at(0).count_, fbs.at(1).base_sn_);
    EXPECT_EQ(1500, fbs.at(0).count_ + fbs.at(1).count_);

    TwccFeedbackRead all = fbs.at(1);
    all.arrivals_.insert(fbs.at(0).arrivals_.begin(), fbs.at(0).arrivals_.end());
    twcc_expect_arrivals(all, received);
}

// The same late burst, when the first feedback stops right before sequence 0. The rest must go out in the next
// feedback, but SrsRtcpTWCC::encode takes next_base_sn_ 0 for "nothing left" and clears them, so the publisher never
// hears of them; the next feedback reports them as lost. Until the encode failure of the test above is fixed, this
// test fails on that error first, in the probe.
VOID TEST(KernelRtcRtcpTest, DISABLED_TwccFeedbackSplitAtSequenceZero_RestNeverReported)
{
    srs_error_t err = srs_success;

    // Learn how many packets of this burst one feedback holds.
    uint16_t first_sn = 0;
    if (true) {
        SrsRtcpTWCC probe(0x2001);
        map<uint16_t, srs_utime_t> received;
        twcc_receive_burst(probe, 0, 1500, received);
        TwccFeedbackRead fb;
        HELPER_ASSERT_SUCCESS(twcc_send_feedback(probe, 0, fb));
        first_sn = (uint16_t)(0 - fb.count_);
    }

    SrsRtcpTWCC twcc(0x2001);
    twcc.set_media_ssrc(0xb001);
    map<uint16_t, srs_utime_t> received;
    twcc_receive_burst(twcc, first_sn, 1500, received);

    vector<TwccFeedbackRead> fbs;
    HELPER_ASSERT_SUCCESS(twcc_send_all(twcc, fbs));
    ASSERT_EQ(2, (int)fbs.size());
    EXPECT_EQ(0, fbs.at(1).base_sn_);
    EXPECT_EQ(1500, fbs.at(0).count_ + fbs.at(1).count_);
}

// --- P2.18: kernel/srs_kernel_rtc_rtp.cpp ------------------------------------

// A WebRTC publisher's RTP header carries its one-byte extensions, then the SRTP auth tag trails the whole packet. The
// transport-cc sequence is parsed from the wire before SRTP unprotect (SrsRtcPublishStream::on_rtp_cipher), so a packet
// that fails SRTP still advances congestion control. The fast parser walks the 0xBEDE extension block, skipping a
// padding byte and the audio-level extension, to the transport-cc one.
static string rtp_with_extensions(uint8_t cc, uint16_t twcc_sn, bool twcc_present, const string &trailer)
{
    // One-byte extension block: padding, audio-level (id 2, len 1 byte), then transport-cc (id 5, len 2 bytes).
    string ext;
    ext.push_back((char)0x00); // padding
    ext.push_back((char)0x20); // id=2 (audio level), len nibble 0 -> 1 data byte
    ext.push_back((char)0x80); // audio level value
    if (twcc_present) {
        ext.push_back((char)0x51); // id=5 (transport-cc), len nibble 1 -> 2 data bytes
        ext.push_back((char)(twcc_sn >> 8));
        ext.push_back((char)(twcc_sn & 0xff));
    }
    while (ext.size() % 4) {
        ext.push_back((char)0x00); // pad to whole 32-bit words
    }

    string p;
    p.push_back((char)(0x90 | (cc & 0x0f))); // V=2, X=1, CC=cc
    p.push_back((char)0x60);                 // M=0, PT=96
    p += rtcp_be16(0x1234);                  // sequence
    p += rtcp_be32(0x11223344);              // timestamp
    p += rtcp_be32(0x0a0b0c0d);              // SSRC
    for (int i = 0; i < cc; i++) {
        p += rtcp_be32(0xc0000000 + i); // CSRC list
    }
    p += rtcp_be16(0xBEDE);
    p += rtcp_be16((uint16_t)(ext.size() / 4));
    p += ext;
    p += trailer;
    return p;
}

VOID TEST(KernelRtcRtpTest, FastParseTwccAmongExtensions)
{
    srs_error_t err = srs_success;

    // The SRTP auth tag (10 bytes here) trails the RTP, giving the parser the slack it needs past the extensions.
    string trailer(10, '\xaa');
    string p = rtp_with_extensions(0, 0xabcd, true, trailer);

    uint16_t twcc_sn = 0;
    HELPER_ASSERT_SUCCESS(srs_rtp_fast_parse_twcc((char *)p.data(), (int)p.size(), 5, twcc_sn));
    EXPECT_EQ(0xabcd, twcc_sn);
}

// The same packet with a contributing-source list: the parser accounts for the CSRC bytes before the extensions.
VOID TEST(KernelRtcRtpTest, FastParseTwccWithCsrcList)
{
    srs_error_t err = srs_success;

    string trailer(10, '\xaa');
    string p = rtp_with_extensions(2, 0x5678, true, trailer);

    uint16_t twcc_sn = 0;
    HELPER_ASSERT_SUCCESS(srs_rtp_fast_parse_twcc((char *)p.data(), (int)p.size(), 5, twcc_sn));
    EXPECT_EQ(0x5678, twcc_sn);
}

// A packet whose extension block has no transport-cc extension: the parser walks every one-byte extension and finds
// none, so it returns without a sequence, and the publisher skips the transport-cc update for it.
VOID TEST(KernelRtcRtpTest, FastParseTwccAbsentWalksAllExtensions)
{
    srs_error_t err = srs_success;

    string trailer(10, '\xaa');
    string p = rtp_with_extensions(0, 0, false, trailer);

    uint16_t twcc_sn = 0xffff;
    HELPER_ASSERT_SUCCESS(srs_rtp_fast_parse_twcc((char *)p.data(), (int)p.size(), 5, twcc_sn));
    EXPECT_EQ(0xffff, twcc_sn); // unchanged
}

// Malformed packets the parser rejects before SRTP: one large enough but with the extension bit clear, one whose CSRC
// list makes it too short, and one whose profile is not 0xBEDE.
VOID TEST(KernelRtcRtpTest, FastParseTwccRejectsMalformedHeaders)
{
    srs_error_t err = srs_success;
    uint16_t twcc_sn = 0;

    // Extension bit clear, but the packet is long enough to pass the size check.
    string no_ext(40, '\x00');
    no_ext[0] = (char)0x80; // V=2, X=0
    err = srs_rtp_fast_parse_twcc((char *)no_ext.data(), (int)no_ext.size(), 5, twcc_sn);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // Extension bit set with CC=2, but only the fixed size: the CSRC list makes it too short.
    string short_csrc(20, '\x00');
    short_csrc[0] = (char)0x92; // V=2, X=1, CC=2
    err = srs_rtp_fast_parse_twcc((char *)short_csrc.data(), (int)short_csrc.size(), 5, twcc_sn);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // Extension profile is not 0xBEDE (one-byte extensions), which this fast parser does not support.
    string p = rtp_with_extensions(0, 0xabcd, true, string(10, '\xaa'));
    p[12] = (char)0x10;
    p[13] = (char)0x00; // profile 0x1000
    err = srs_rtp_fast_parse_twcc((char *)p.data(), (int)p.size(), 5, twcc_sn);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// The extension-type map is negotiated from the SDP's a=extmap lines. An id out of the 1..255 range is rejected, and a
// known URI maps to its type, which the full header decoder uses to tell transport-cc and audio-level apart.
VOID TEST(KernelRtcRtpTest, ExtensionTypesRegisterByUri)
{
    SrsRtpExtensionTypes types;

    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));
    EXPECT_TRUE(types.register_by_uri(2, kAudioLevelUri));
    EXPECT_EQ(kRtpExtensionTransportSequenceNumber, types.get_type(5));
    EXPECT_EQ(kRtpExtensionAudioLevel, types.get_type(2));
    EXPECT_EQ(kRtpExtensionNone, types.get_type(9)); // unregistered id

    // An unknown URI, and an id out of range, are both refused.
    EXPECT_FALSE(types.register_by_uri(3, "urn:unknown"));
    EXPECT_FALSE(types.register_by_uri(0, kTWCCExt));
    EXPECT_FALSE(types.register_by_uri(256, kTWCCExt));
}

// A publisher's audio packet carries both a transport-cc sequence and an ssrc-audio-level in its 0xBEDE block. With the
// extension map set and transport-cc decoding enabled, the full header decoder reads both, and the header re-encodes to
// the same extension block.
VOID TEST(KernelRtcRtpTest, HeaderExtensionsTwccAndAudioLevelRoundTrip)
{
    srs_error_t err = srs_success;

    SrsRtpExtensionTypes types;
    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));
    EXPECT_TRUE(types.register_by_uri(2, kAudioLevelUri));

    // Build the extensions the publisher sends.
    SrsRtpExtensions src;
    src.set_types_(&types);
    HELPER_ASSERT_SUCCESS(src.set_twcc_sequence_number(5, 0x4142));
    HELPER_ASSERT_SUCCESS(src.set_audio_level(2, 0x30));

    char b[256];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();
    EXPECT_EQ((int)src.nb_bytes(), nb);

    // The publisher stream decodes transport-cc in the full header (enable_twcc_decode), plus the audio level.
    SrsRtpExtensions dst;
    dst.set_types_(&types);
    dst.enable_twcc_decode();
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));

    uint16_t twcc_sn = 0;
    HELPER_ASSERT_SUCCESS(dst.get_twcc_sequence_number(twcc_sn));
    EXPECT_EQ(0x4142, twcc_sn);
    uint8_t level = 0;
    HELPER_ASSERT_SUCCESS(dst.get_audio_level(level));
    EXPECT_EQ(0x30, level);
}

// By default transport-cc is parsed by the fast path, so the full header decoder skips the transport-cc extension
// (decode_twcc_extension_ false) while still reading the audio level. The skipped extension leaves no sequence.
VOID TEST(KernelRtcRtpTest, HeaderExtensionsSkipTwccWhenNotDecodingIt)
{
    srs_error_t err = srs_success;

    SrsRtpExtensionTypes types;
    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));
    EXPECT_TRUE(types.register_by_uri(2, kAudioLevelUri));

    SrsRtpExtensions src;
    src.set_types_(&types);
    HELPER_ASSERT_SUCCESS(src.set_twcc_sequence_number(5, 0x4142));
    HELPER_ASSERT_SUCCESS(src.set_audio_level(2, 0x30));

    char b[256];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();

    SrsRtpExtensions dst;
    dst.set_types_(&types);
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));

    uint16_t twcc_sn = 0;
    EXPECT_TRUE(dst.get_twcc_sequence_number(twcc_sn) != srs_success); // skipped, not stored
    uint8_t level = 0;
    HELPER_ASSERT_SUCCESS(dst.get_audio_level(level));
    EXPECT_EQ(0x30, level);
}

// The extension block of a 0x1000 (two-byte header) profile is not parsed for values, but it is skipped so the payload
// that follows is reached.
VOID TEST(KernelRtcRtpTest, HeaderExtensionsTwoByteProfileSkipped)
{
    srs_error_t err = srs_success;

    // profile 0x1000, length 1 word (4 bytes), then 4 bytes of extension data.
    string p = rtcp_be16(0x1000) + rtcp_be16(1) + string(4, '\x11');
    SrsRtpExtensions ext;
    SrsBuffer buf((char *)p.data(), (int)p.size());
    HELPER_ASSERT_SUCCESS(ext.decode(&buf));
    EXPECT_TRUE(buf.empty());
}

// Asking a header with no extensions for its transport-cc sequence, or an extension block with no audio level for the
// level, returns an error rather than a value.
VOID TEST(KernelRtcRtpTest, HeaderExtensionsMissingValuesReturnError)
{
    srs_error_t err = srs_success;

    SrsRtpHeader h;
    uint16_t twcc_sn = 0;
    EXPECT_TRUE(h.get_twcc_sequence_number(twcc_sn) != srs_success);

    SrsRtpExtensions ext;
    EXPECT_TRUE(ext.get_twcc_sequence_number(twcc_sn) != srs_success);
    uint8_t level = 0;
    EXPECT_TRUE(ext.get_audio_level(level) != srs_success);
}

// Truncated extension blocks are refused: one too short for the 4-byte profile/length, one that promises more
// extension bytes than it carries, and a transport-cc or audio-level extension cut inside its value.
VOID TEST(KernelRtcRtpTest, HeaderExtensionsTruncatedAreRefused)
{
    srs_error_t err = srs_success;

    // Too short for the profile and length fields.
    if (true) {
        string p(3, '\x00');
        SrsRtpExtensions ext;
        SrsBuffer buf((char *)p.data(), (int)p.size());
        err = ext.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }

    // A transport-cc extension cut after its one-byte header.
    if (true) {
        SrsRtpExtensionTwcc twcc;
        string p;
        p.push_back((char)0x51); // id 5, len nibble 1
        SrsBuffer buf((char *)p.data(), (int)p.size());
        err = twcc.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }

    // A transport-cc extension whose len nibble is not 1 is invalid.
    if (true) {
        SrsRtpExtensionTwcc twcc;
        string p;
        p.push_back((char)0x53); // id 5, len nibble 3
        p += rtcp_be16(0);
        SrsBuffer buf((char *)p.data(), (int)p.size());
        err = twcc.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }

    // An audio-level extension cut after its header, and one whose len nibble is not 0.
    if (true) {
        SrsRtpExtensionOneByte al;
        string p;
        p.push_back((char)0x20); // id 2, len nibble 0, no value byte
        SrsBuffer buf((char *)p.data(), (int)p.size());
        err = al.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
    if (true) {
        SrsRtpExtensionOneByte al;
        string p;
        p.push_back((char)0x23); // id 2, len nibble 3
        p += string(3, '\x00');
        SrsBuffer buf((char *)p.data(), (int)p.size());
        err = al.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
}

// Encoding into a buffer too small for the extensions, or for a single extension, fails rather than overruns.
VOID TEST(KernelRtcRtpTest, HeaderExtensionsEncodeIntoShortBufferFails)
{
    srs_error_t err = srs_success;

    SrsRtpExtensionTypes types;
    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));

    SrsRtpExtensions ext;
    ext.set_types_(&types);
    HELPER_ASSERT_SUCCESS(ext.set_twcc_sequence_number(5, 0x1111));

    // Only the 0xBEDE and length words fit; the extension bytes do not.
    char b[4];
    SrsBuffer buf(b, sizeof(b));
    err = ext.encode(&buf);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // A single transport-cc extension needs 3 bytes.
    SrsRtpExtensionTwcc twcc;
    twcc.set_id(5);
    twcc.set_sn(0x1111);
    char b2[2];
    SrsBuffer buf2(b2, sizeof(b2));
    err = twcc.encode(&buf2);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // A single audio-level extension needs 2 bytes.
    SrsRtpExtensionOneByte al;
    al.set_id(2);
    al.set_value(0x30);
    char b3[1];
    SrsBuffer buf3(b3, sizeof(b3));
    err = al.encode(&buf3);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// A mixer or relay forwards a packet with a contributing-source list, over a transport-cc extension. The header
// round-trips: the CSRC list and the extension block are both preserved.
VOID TEST(KernelRtcRtpTest, HeaderWithCsrcRoundTrip)
{
    srs_error_t err = srs_success;

    SrsRtpExtensionTypes types;
    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));

    SrsRtpHeader src;
    src.set_extensions(&types);
    src.set_payload_type(96);
    src.set_sequence(0x2233);
    src.set_ssrc(0x0a0b0c0d);
    src.set_twcc_sequence_number(5, 0x7788);

    // Two contributing sources: nb_bytes and encode account for them, and decode reads them back.
    src.cc_ = 2;
    src.csrc_[0] = 0xc0c0c0c0;
    src.csrc_[1] = 0xd0d0d0d0;

    char b[256];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();

    SrsRtpHeader dst;
    dst.set_extensions(&types);
    dst.enable_twcc_decode();
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));
    EXPECT_EQ(0x2233, dst.get_sequence());
    EXPECT_EQ(0x0a0b0c0d, dst.get_ssrc());
    EXPECT_EQ(2, dst.cc_);
    EXPECT_EQ((uint32_t)0xc0c0c0c0, dst.csrc_[0]);
    uint16_t twcc_sn = 0;
    HELPER_ASSERT_SUCCESS(dst.get_twcc_sequence_number(twcc_sn));
    EXPECT_EQ(0x7788, twcc_sn);
}

// A header with the extension bit set but too few bytes for even the 4-byte profile and length words is refused
// gracefully.
VOID TEST(KernelRtcRtpTest, HeaderTruncatedBeforeExtensionsRefused)
{
    srs_error_t err = srs_success;

    SrsRtpExtensionTypes types;
    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));

    // Fixed 12-byte header with X=1, then only 2 of the 4 extension-header bytes.
    string w;
    w.push_back((char)0x90); // V=2, X=1, CC=0
    w.push_back((char)0x60); // PT=96
    w += rtcp_be16(0x2233);
    w += rtcp_be32(0);
    w += rtcp_be32(0x0a0b0c0d);
    w += rtcp_be16(0xBEDE); // only the profile word, length word missing

    SrsRtpHeader dst;
    dst.set_extensions(&types);
    SrsBuffer dbuf((char *)w.data(), (int)w.size());
    err = dst.decode(&dbuf);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// B14: a WebRTC peer's RTP packet declares an extension length longer than the packet carries. SrsRtpExtensions::decode
// reads the 4-byte profile and length words with a require, but then skips extension_length*4 bytes with no check, so
// SrsBuffer::skip asserts and the server aborts. The packet is parsed in do_on_rtp_plaintext after SRTP unprotect, so
// any peer that can publish over WebRTC can crash the server with one packet. Fix: require extension_length*4 bytes
// before building the sub-buffer and skipping. High severity.
VOID TEST(KernelRtcRtpTest, DISABLED_RtpExtensionLengthBeyondPacket_ServerAborts)
{
    srs_error_t err = srs_success;

    SrsRtpExtensionTypes types;
    EXPECT_TRUE(types.register_by_uri(5, kTWCCExt));

    // Fixed header with X=1, profile 0xBEDE, extension_length 0x00ff words (1020 bytes), but no extension bytes follow.
    string w;
    w.push_back((char)0x90); // V=2, X=1, CC=0
    w.push_back((char)0x60); // PT=96
    w += rtcp_be16(0x2233);
    w += rtcp_be32(0);
    w += rtcp_be32(0x0a0b0c0d);
    w += rtcp_be16(0xBEDE);
    w += rtcp_be16(0x00ff); // 255 words of extension that are not present

    SrsRtpHeader dst;
    dst.set_extensions(&types);
    SrsBuffer dbuf((char *)w.data(), (int)w.size());
    err = dst.decode(&dbuf); // aborts today: SrsBuffer::skip assert
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// The RTMP-to-WebRTC packetizer builds a STAP-A of SPS and PPS before an IDR, and the publisher parser reads a STAP-A.
// The payload round-trips, and get_sps, get_pps and get_idr find each NALU; a zero-length NALU entry is skipped.
VOID TEST(KernelRtcRtpTest, StapAPayloadRoundTripAndGetters)
{
    srs_error_t err = srs_success;

    string sps("\x67\x42\x00\x1f", 4);
    string pps("\x68\xce\x3c\x80", 4);
    string idr("\x65\x88\x84\x00\x10", 5);

    SrsRtpSTAPPayload src;
    SrsNaluSample s_sps, s_pps, s_idr;
    s_sps.bytes_ = (char *)sps.data(); s_sps.size_ = (int)sps.size();
    s_pps.bytes_ = (char *)pps.data(); s_pps.size_ = (int)pps.size();
    s_idr.bytes_ = (char *)idr.data(); s_idr.size_ = (int)idr.size();
    src.nalus_.push_back(&s_sps);
    src.nalus_.push_back(&s_pps);
    src.nalus_.push_back(&s_idr);

    char b[256];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();
    EXPECT_EQ((int)src.nb_bytes(), nb);
    src.nalus_.clear(); // the samples are stack objects; do not let the destructor free them

    // The parser decodes the STAP-A, with a zero-length entry prepended that must be skipped by the getters.
    string wire(b, nb);
    wire.insert(1, string("\x00\x00", 2)); // a zero-length NALU entry right after the STAP header
    SrsRtpSTAPPayload dst;
    SrsBuffer dbuf((char *)wire.data(), (int)wire.size());
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));

    ASSERT_TRUE(dst.get_sps() != NULL);
    EXPECT_EQ(sps, string(dst.get_sps()->bytes_, dst.get_sps()->size_));
    ASSERT_TRUE(dst.get_pps() != NULL);
    EXPECT_EQ(pps, string(dst.get_pps()->bytes_, dst.get_pps()->size_));
    ASSERT_TRUE(dst.get_idr() != NULL);
    EXPECT_EQ(idr, string(dst.get_idr()->bytes_, dst.get_idr()->size_));

    // A copy holds the same NALUs.
    SrsUniquePtr<ISrsRtpPayloader> cp(dst.copy());
    SrsRtpSTAPPayload *cpp = dynamic_cast<SrsRtpSTAPPayload *>(cp.get());
    ASSERT_TRUE(cpp != NULL);
    ASSERT_TRUE(cpp->get_sps() != NULL);
    EXPECT_EQ(sps, string(cpp->get_sps()->bytes_, cpp->get_sps()->size_));
}

// A large NALU is packetized into FU-A fragments (SrsRtpFUAPayload), and the publisher parser reads a fragment. The
// payload round-trips with its start/end flags, and a copy preserves it.
VOID TEST(KernelRtcRtpTest, FuaPayloadRoundTripAndCopy)
{
    srs_error_t err = srs_success;

    string frag("\x9a\x21\x6c\x42\x00\x11\x22", 7);
    SrsRtpFUAPayload src;
    src.nri_ = (SrsAvcNaluType)0x60;
    src.nalu_type_ = SrsAvcNaluTypeIDR;
    src.start_ = true;
    src.end_ = false;
    SrsNaluSample s;
    s.bytes_ = (char *)frag.data(); s.size_ = (int)frag.size();
    src.nalus_.push_back(&s);

    char b[64];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();
    EXPECT_EQ((int)src.nb_bytes(), nb);
    src.nalus_.clear();

    SrsRtpFUAPayload dst;
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));
    EXPECT_TRUE(dst.start_);
    EXPECT_FALSE(dst.end_);
    EXPECT_EQ(SrsAvcNaluTypeIDR, dst.nalu_type_);

    SrsUniquePtr<ISrsRtpPayloader> cp(dst.copy());
    SrsRtpFUAPayload *cpp = dynamic_cast<SrsRtpFUAPayload *>(cp.get());
    ASSERT_TRUE(cpp != NULL);
    EXPECT_EQ(SrsAvcNaluTypeIDR, cpp->nalu_type_);
    EXPECT_EQ((int)dst.nb_bytes(), (int)cpp->nb_bytes());
}

// The merge-NALUs packetizer collects a frame's NALUs into one SrsRtpRawNALUs, inserting a start code between them, and
// a short enough set goes out in a single RTP packet. An empty sample is ignored.
VOID TEST(KernelRtcRtpTest, RawNalusCollectEncodeAndRead)
{
    srs_error_t err = srs_success;

    string n1("\x67\x42\x00\x1f", 4);
    string n2("\x68\xce\x3c\x80", 4);

    SrsRtpRawNALUs raw;
    SrsNaluSample *s1 = new SrsNaluSample();
    s1->bytes_ = (char *)n1.data(); s1->size_ = (int)n1.size();
    SrsNaluSample *s2 = new SrsNaluSample();
    s2->bytes_ = (char *)n2.data(); s2->size_ = (int)n2.size();
    SrsNaluSample *empty = new SrsNaluSample();
    empty->bytes_ = NULL; empty->size_ = 0;
    raw.push_back(empty); // ignored: size 0
    raw.push_back(s1);
    raw.push_back(s2);

    // nb_bytes is the two NALUs plus the 3-byte start code inserted between them.
    EXPECT_EQ((int)(n1.size() + 3 + n2.size()), (int)raw.nb_bytes());

    char b[64];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(raw.encode(&ebuf));
    EXPECT_EQ((int)raw.nb_bytes(), ebuf.pos());

    // A copy carries the same bytes.
    SrsUniquePtr<ISrsRtpPayloader> cp(raw.copy());
    EXPECT_EQ((int)raw.nb_bytes(), (int)cp->nb_bytes());
}

// The NACK responder answers with an RFC 4588 RTX packet: the two-byte original sequence number, then the original
// payload. The publisher's RTX packet is parsed the same way, recovering the original sequence and payload.
VOID TEST(KernelRtcRtpTest, RtxPayloadRoundTrip)
{
    srs_error_t err = srs_success;

    string media("\x00\x11\x22\x33\x44", 5);
    SrsRtpRawPayload *inner = new SrsRtpRawPayload();
    inner->payload_ = (char *)media.data();
    inner->nn_payload_ = (int)media.size();

    SrsRtpRtxPayload src;
    src.osn_ = 0x1234;
    src.payload_ = inner;

    char b[64];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();
    EXPECT_EQ((int)src.nb_bytes(), nb);

    SrsRtpRtxPayload dst;
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));
    EXPECT_EQ(0x1234, dst.osn_);
    ASSERT_TRUE(dst.payload_ != NULL);
    EXPECT_EQ((int)media.size(), (int)dst.payload_->nb_bytes());

    SrsUniquePtr<ISrsRtpPayloader> cp(dst.copy());
    EXPECT_EQ((int)dst.nb_bytes(), (int)cp->nb_bytes());

    // A packet too short for the two-byte OSN is refused when encoding.
    char tiny[1];
    SrsBuffer tbuf(tiny, sizeof(tiny));
    SrsRtpRtxPayload small;
    small.osn_ = 1;
    err = small.encode(&tbuf);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// A WebRTC HEVC publisher sends a STAP (aggregation) packet of VPS, SPS and PPS, and FU fragments of a slice. The HEVC
// payloads round-trip, and the STAP getters find each parameter set.
VOID TEST(KernelRtcRtpTest, HevcStapAndFuaPayloadsRoundTrip)
{
    srs_error_t err = srs_success;

    string vps("\x40\x01\x0c\x01", 4); // NALU type 32 (VPS)
    string sps("\x42\x01\x01\x01", 4); // NALU type 33 (SPS)
    string pps("\x44\x01\xc1\x72", 4); // NALU type 34 (PPS)

    SrsRtpSTAPPayloadHevc stap;
    SrsNaluSample sv, ss, sp;
    sv.bytes_ = (char *)vps.data(); sv.size_ = (int)vps.size();
    ss.bytes_ = (char *)sps.data(); ss.size_ = (int)sps.size();
    sp.bytes_ = (char *)pps.data(); sp.size_ = (int)pps.size();
    stap.nalus_.push_back(&sv);
    stap.nalus_.push_back(&ss);
    stap.nalus_.push_back(&sp);

    char b[256];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(stap.encode(&ebuf));
    int nb = ebuf.pos();
    EXPECT_EQ((int)stap.nb_bytes(), nb);
    stap.nalus_.clear();

    SrsRtpSTAPPayloadHevc dst;
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));
    ASSERT_TRUE(dst.get_vps() != NULL);
    EXPECT_EQ(vps, string(dst.get_vps()->bytes_, dst.get_vps()->size_));
    ASSERT_TRUE(dst.get_sps() != NULL);
    EXPECT_EQ(sps, string(dst.get_sps()->bytes_, dst.get_sps()->size_));
    ASSERT_TRUE(dst.get_pps() != NULL);
    EXPECT_EQ(pps, string(dst.get_pps()->bytes_, dst.get_pps()->size_));
    EXPECT_TRUE(dst.get_idr() == NULL); // no IRAP slice in a parameter-set STAP

    SrsUniquePtr<ISrsRtpPayloader> cp(dst.copy());
    EXPECT_EQ((int)dst.nb_bytes(), (int)cp->nb_bytes());

    // An HEVC FU fragment of an IDR slice.
    string frag("\x01\xaf\x00\x11\x22", 5);
    SrsRtpFUAPayloadHevc fua;
    fua.nalu_type_ = SrsHevcNaluType_CODED_SLICE_IDR;
    fua.start_ = true;
    fua.end_ = true;
    SrsNaluSample fs;
    fs.bytes_ = (char *)frag.data(); fs.size_ = (int)frag.size();
    fua.nalus_.push_back(&fs);

    char fb[64];
    SrsBuffer fbuf(fb, sizeof(fb));
    HELPER_ASSERT_SUCCESS(fua.encode(&fbuf));
    int fnb = fbuf.pos();
    EXPECT_EQ((int)fua.nb_bytes(), fnb);
    fua.nalus_.clear();

    SrsRtpFUAPayloadHevc fdst;
    SrsBuffer fdbuf(fb, fnb);
    HELPER_ASSERT_SUCCESS(fdst.decode(&fdbuf));
    EXPECT_TRUE(fdst.start_);
    EXPECT_TRUE(fdst.end_);
    EXPECT_EQ(SrsHevcNaluType_CODED_SLICE_IDR, fdst.nalu_type_);

    SrsUniquePtr<ISrsRtpPayloader> fcp(fdst.copy());
    EXPECT_EQ((int)fdst.nb_bytes(), (int)fcp->nb_bytes());
}

// Payload decoders refuse a fragment or aggregation unit cut short of its header.
VOID TEST(KernelRtcRtpTest, PayloadsTruncatedAreRefused)
{
    srs_error_t err = srs_success;

    // STAP-A needs 1 byte; an entry size needs 2 bytes and its NALU bytes.
    if (true) {
        SrsRtpSTAPPayload p;
        SrsBuffer buf((char *)"", 0);
        err = p.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
    if (true) {
        string w("\x18\x00\x05", 3); // STAP header, entry size 5, but no NALU bytes
        SrsRtpSTAPPayload p;
        SrsBuffer buf((char *)w.data(), (int)w.size());
        err = p.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
    // FU-A needs 2 bytes of headers and then 1 byte of payload.
    if (true) {
        string w("\x7c", 1);
        SrsRtpFUAPayload p;
        SrsBuffer buf((char *)w.data(), (int)w.size());
        err = p.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
    // HEVC STAP and FU need their 2- and 3-byte headers.
    if (true) {
        string w("\x60", 1);
        SrsRtpSTAPPayloadHevc p;
        SrsBuffer buf((char *)w.data(), (int)w.size());
        err = p.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
    if (true) {
        string w("\x62\x01", 2);
        SrsRtpFUAPayloadHevc p;
        SrsBuffer buf((char *)w.data(), (int)w.size());
        err = p.decode(&buf);
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
    }
}

// A relayed RTP packet carries a raw payload and RTP padding. Encode writes the payload then the padding bytes, and the
// publisher's decode strips the padding before the payload, so a padding probe's payload reads empty.
VOID TEST(KernelRtcRtpTest, PacketPaddingEncodeAndDecode)
{
    srs_error_t err = srs_success;

    string media("\x11\x22\x33\x44", 4);
    SrsRtpPacket src;
    src.header_.set_payload_type(96);
    src.header_.set_sequence(7);
    src.header_.set_ssrc(0x0a0b0c0d);
    src.header_.set_padding(4);
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    raw->payload_ = (char *)media.data();
    raw->nn_payload_ = (int)media.size();
    src.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    char b[128];
    SrsBuffer ebuf(b, sizeof(b));
    HELPER_ASSERT_SUCCESS(src.encode(&ebuf));
    int nb = ebuf.pos();
    EXPECT_EQ(12 + (int)media.size() + 4, nb);

    SrsRtpPacket dst;
    SrsBuffer dbuf(b, nb);
    HELPER_ASSERT_SUCCESS(dst.decode(&dbuf));
    EXPECT_EQ(7, dst.header_.get_sequence());
    EXPECT_EQ(4, dst.header_.get_padding());
    ASSERT_TRUE(dst.payload() != NULL);
    EXPECT_EQ((int)media.size(), (int)dst.payload()->nb_bytes());
}

// Encoding a packet into a buffer that fits the header but not the payload fails rather than overruns.
VOID TEST(KernelRtcRtpTest, PacketEncodeShortOfPayloadFails)
{
    srs_error_t err = srs_success;

    string media("\x11\x22\x33\x44", 4);
    SrsRtpPacket pkt;
    pkt.header_.set_payload_type(96);
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    raw->payload_ = (char *)media.data();
    raw->nn_payload_ = (int)media.size();
    pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    // 14 bytes fit the 12-byte header but not the 4-byte payload.
    char pb[14];
    SrsBuffer pbuf(pb, sizeof(pb));
    err = pkt.encode(&pbuf);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// A packet whose padding length is larger than the bytes left is refused by decode.
VOID TEST(KernelRtcRtpTest, PacketPaddingLongerThanBufferRefused)
{
    srs_error_t err = srs_success;

    // Fixed header with the padding bit set, then two payload bytes, the last of which claims 200 padding bytes.
    string w;
    w.push_back((char)0xa0); // V=2, P=1
    w.push_back((char)0x60); // PT=96
    w += rtcp_be16(1);
    w += rtcp_be32(0);
    w += rtcp_be32(0x0a0b0c0d);
    w.push_back((char)0x11);
    w.push_back((char)200); // padding length far beyond the packet

    SrsRtpPacket pkt;
    SrsBuffer buf((char *)w.data(), (int)w.size());
    err = pkt.decode(&buf);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// One 188-byte TS packet: the 4-byte header, then the payload. A payload shorter than 184 bytes is padded the way a
// muxer pads the last packet of a PES, by an adaptation field of stuffing bytes, which for a payload of 183 bytes is
// the single length byte 0.
static string ts_packet(uint16_t pid, bool pusi, uint8_t cc, const string &payload)
{
    string p;
    p.push_back((char)0x47);
    p.push_back((char)(((pusi ? 1 : 0) << 6) | ((pid >> 8) & 0x1f)));
    p.push_back((char)(pid & 0xff));

    int nb_af = 184 - (int)payload.size();
    if (nb_af <= 0) {
        p.push_back((char)(0x10 | (cc & 0x0f)));
        return p + payload.substr(0, 184);
    }

    p.push_back((char)(0x30 | (cc & 0x0f)));
    p.push_back((char)(nb_af - 1));
    if (nb_af > 1) {
        p.push_back((char)0x00);
        p += string(nb_af - 2, (char)0xff);
    }
    return p + payload;
}

// A TS packet with the adaptation field control and the adaptation field (its length byte first) given as they are,
// for a packet damaged in transit; the rest of the 188 bytes is the payload, then 0xff.
static string ts_packet_af(uint16_t pid, bool pusi, uint8_t afc, const string &af, const string &payload)
{
    string p;
    p.push_back((char)0x47);
    p.push_back((char)(((pusi ? 1 : 0) << 6) | ((pid >> 8) & 0x1f)));
    p.push_back((char)(pid & 0xff));
    p.push_back((char)((afc << 4) & 0x30));
    p += af + payload;
    if (p.size() < 188) {
        p += string(188 - p.size(), (char)0xff);
    }
    return p.substr(0, 188);
}

// An adaptation field of nb bytes after its length byte: the flags, then stuffing.
static string ts_af_stuffing(int nb)
{
    return string(1, (char)nb) + string(1, (char)0x00) + string(nb - 1, (char)0xff);
}

// A PSI section after its pointer field: the table id, the section length, the body and the CRC.
static string ts_psi(uint8_t table_id, const string &body)
{
    int section_length = (int)body.size() + 4;

    string s;
    s.push_back((char)table_id);
    s.push_back((char)(0xb0 | ((section_length >> 8) & 0x0f)));
    s.push_back((char)(section_length & 0xff));
    s += body;
    s += rtcp_be32(srs_crc32_mpegts(s.data(), (int)s.size()));

    return string(1, (char)0x00) + s;
}

// A PAT of program 1 whose PMT is on pmt_pid.
static string ts_pat(uint16_t pmt_pid)
{
    return ts_psi(0x00, rtcp_be16(1) + string("\xc1\x00\x00", 3) + rtcp_be16(1) + rtcp_be16(0xe000 | pmt_pid));
}

// One elementary stream of a PMT, with its descriptors.
static string ts_pmt_es(uint8_t stream_type, uint16_t pid, const string &es_info)
{
    return string(1, (char)stream_type) + rtcp_be16(0xe000 | pid) + rtcp_be16((uint16_t)(0xf000 | es_info.size())) + es_info;
}

// A PMT of program 1 with its program descriptors and elementary streams.
static string ts_pmt(uint16_t pcr_pid, const string &program_info, const string &streams)
{
    string body = rtcp_be16(1) + string("\xc1\x00\x00", 3) + rtcp_be16(0xe000 | pcr_pid);
    body += rtcp_be16((uint16_t)(0xf000 | program_info.size())) + program_info + streams;
    return ts_psi(0x02, body);
}

// A packet of one PSI section, the rest of the payload filled as the muxer does, with 0xff.
static string ts_psi_packet(uint16_t pid, const string &section, char fill = (char)0xff)
{
    return ts_packet(pid, true, 0, section + string(184 - section.size(), fill));
}

// The PAT and PMT an encoder sends first: H.264 on PID 0x100 and AAC on PID 0x101, the PMT on 0x1001.
static string ts_av_program()
{
    string streams = ts_pmt_es(0x1b, 0x100, "") + ts_pmt_es(0x0f, 0x101, "");
    return ts_psi_packet(0, ts_pat(0x1001)) + ts_psi_packet(0x1001, ts_pmt(0x100, "", streams));
}

// A 33-bit PTS or DTS in its 5 bytes with the marker bits.
static string ts_timestamp(uint8_t prefix, int64_t v)
{
    string s;
    s.push_back((char)((prefix << 4) | (((v >> 30) & 0x07) << 1) | 0x01));
    s += rtcp_be16((uint16_t)((((v >> 15) & 0x7fff) << 1) | 0x01));
    s += rtcp_be16((uint16_t)(((v & 0x7fff) << 1) | 0x01));
    return s;
}

// A PES header: the start code, the stream id, the PES_packet_length for nb_data bytes of data (0 without
// with_length, as muxers write video), and the PTS, with the DTS when it differs.
static string ts_pes_header(uint8_t sid, int64_t pts, int64_t dts, int nb_data, bool with_length)
{
    bool has_dts = (pts != dts);
    int nb_header = has_dts ? 10 : 5;

    string s("\x00\x00\x01", 3);
    s.push_back((char)sid);
    s += rtcp_be16(with_length ? (uint16_t)(3 + nb_header + nb_data) : 0);
    s.push_back((char)0x80);
    s.push_back((char)(has_dts ? 0xc0 : 0x80));
    s.push_back((char)nb_header);
    s += ts_timestamp(has_dts ? 0x03 : 0x02, pts);
    if (has_dts) {
        s += ts_timestamp(0x01, dts);
    }
    return s;
}

// Frame data of nb bytes that differ from each other, so a lost or repeated part shows.
static string ts_frame_data(int nb, uint8_t seed)
{
    string s;
    for (int i = 0; i < nb; i++) {
        s.push_back((char)(seed + i * 7));
    }
    return s;
}

// A PES split into the TS packets of one PID, the first one starting the unit, with continuity counters from cc.
static vector<string> ts_pes_packets(uint16_t pid, uint8_t cc, const string &pes)
{
    vector<string> pkts;
    for (size_t pos = 0; pos < pes.size(); pos += 184) {
        pkts.push_back(ts_packet(pid, pos == 0, cc++, pes.substr(pos, 184)));
    }
    return pkts;
}

// The messages the demuxer gives the SRT or UDP ingest, copied out, as the ingest uses each before the next.
class TsMessages : public ISrsTsHandler
{
public:
    struct Message {
        int pid_;
        SrsTsStream stream_;
        SrsTsPESStreamId sid_;
        int64_t dts_;
        int64_t pts_;
        string payload_;
    };
    vector<Message> msgs_;
    // Returned for each message, as an ingest that refuses it does.
    srs_error_t refuse_;

public:
    TsMessages()
    {
        refuse_ = srs_success;
    }
    virtual ~TsMessages()
    {
        srs_freep(refuse_);
    }

public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg)
    {
        Message m;
        m.pid_ = msg->channel_->pid_;
        m.stream_ = msg->channel_->stream_;
        m.sid_ = msg->sid_;
        m.dts_ = msg->dts_;
        m.pts_ = msg->pts_;
        if (msg->payload_->length() > 0) {
            m.payload_ = string(msg->payload_->bytes(), msg->payload_->length());
        }
        msgs_.push_back(m);
        return (refuse_ != srs_success) ? srs_error_copy(refuse_) : srs_success;
    }
};

// Feeds a publisher's TS to the demuxer one 188-byte packet at a time, as the SRT and UDP ingest do.
static srs_error_t ts_decode(SrsTsContext *ctx, const string &ts, ISrsTsHandler *h)
{
    srs_error_t err = srs_success;

    for (size_t pos = 0; pos + SRS_TS_PACKET_SIZE <= ts.size(); pos += SRS_TS_PACKET_SIZE) {
        SrsBuffer b((char *)ts.data() + pos, SRS_TS_PACKET_SIZE);
        if ((err = ctx->decode(&b, h)) != srs_success) {
            return srs_error_wrap(err, "ts packet at %d", (int)pos);
        }
    }

    return err;
}

// Decodes a new publisher's program and then ts, the packets of one test case.
static srs_error_t ts_decode_after_program(const string &ts, ISrsTsHandler *h)
{
    SrsTsContext ctx;
    return ts_decode(&ctx, ts_av_program() + ts, h);
}

// Reads the last PMT (PID 0x1001) of a muxer's output: its PCR PID and each stream's type and PID.
static bool ts_read_pmt(const string &ts, int &pcr_pid, vector<pair<int, int> > &streams)
{
    bool found = false;

    for (size_t pos = 0; pos + SRS_TS_PACKET_SIZE <= ts.size(); pos += SRS_TS_PACKET_SIZE) {
        int pid = (((uint8_t)ts[pos + 1] & 0x1f) << 8) | (uint8_t)ts[pos + 2];
        if (pid != 0x1001) {
            continue;
        }

        SrsTsContext ctx;
        ctx.set(0x1001, SrsTsPidApplyPMT);
        SrsTsPacket pkt(&ctx);
        SrsBuffer b((char *)ts.data() + pos, SRS_TS_PACKET_SIZE);
        SrsTsMessage *msg = NULL;
        srs_error_t err = pkt.decode(&b, &msg);
        if (err != srs_success) {
            srs_freep(err);
            return false;
        }

        SrsTsPayloadPMT *pmt = dynamic_cast<SrsTsPayloadPMT *>(pkt.payload_);
        if (!pmt) {
            return false;
        }

        pcr_pid = pmt->PCR_PID;
        streams.clear();
        for (int i = 0; i < (int)pmt->infos.size(); i++) {
            SrsTsPayloadPMTESInfo *info = pmt->infos.at(i);
            streams.push_back(make_pair((int)info->stream_type, (int)info->elementary_PID));
        }
        found = true;
    }

    return found;
}

// Whether the TS packet at index carries a PCR in its adaptation field.
static bool ts_has_pcr(const string &ts, int index)
{
    string p = ts.substr(index * SRS_TS_PACKET_SIZE, SRS_TS_PACKET_SIZE);
    int afc = ((uint8_t)p[3] >> 4) & 0x03;
    if (afc != 2 && afc != 3) {
        return false;
    }
    return (uint8_t)p[4] > 0 && ((uint8_t)p[5] & 0x10) != 0;
}

// The NALUs of an AnnexB payload, after either start code.
static vector<string> ts_annexb_nalus(const string &p)
{
    vector<string> nalus;
    size_t start = string::npos;

    for (size_t i = 0; i + 3 <= p.size();) {
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
            if (start != string::npos) {
                string nalu = p.substr(start, i - start);
                while (!nalu.empty() && nalu[nalu.size() - 1] == 0) {
                    nalu.erase(nalu.size() - 1);
                }
                nalus.push_back(nalu);
            }
            i += 3;
            start = i;
            continue;
        }
        i++;
    }
    if (start != string::npos) {
        nalus.push_back(p.substr(start));
    }

    return nalus;
}

// Writes one FLV tag of a publisher to an HLS segment as SrsHlsController does: the format parses it, the muxer
// caches the frame as a TS message, skipping AAC and video sequence headers, and the segment's writer writes it.
static srs_error_t ts_hls_write(SrsFormat *fmt, SrsTsMessageCache *cache, SrsTsContextWriter *tscw, bool video, int64_t timestamp, const string &tag)
{
    srs_error_t err = srs_success;

    string data = tag;
    if (video) {
        if ((err = fmt->on_video(timestamp, (char *)data.data(), (int)data.size())) != srs_success) {
            return srs_error_wrap(err, "format video");
        }
        if (fmt->video()->avc_packet_type_ == SrsVideoAvcFrameTraitSequenceHeader) {
            return err;
        }
        if ((err = cache->cache_video(fmt->video(), timestamp * 90)) != srs_success) {
            return srs_error_wrap(err, "cache video");
        }
        err = tscw->write_video(cache->video());
        SrsTsMessage *msg = cache->video();
        srs_freep(msg);
        cache->set_video(NULL);
        return err;
    }

    if ((err = fmt->on_audio(timestamp, (char *)data.data(), (int)data.size())) != srs_success) {
        return srs_error_wrap(err, "format audio");
    }
    if (fmt->is_aac_sequence_header()) {
        return err;
    }
    if ((err = cache->cache_audio(fmt->audio(), timestamp * 90)) != srs_success) {
        return srs_error_wrap(err, "cache audio");
    }
    err = tscw->write_audio(cache->audio());
    SrsTsMessage *msg = cache->audio();
    srs_freep(msg);
    cache->set_audio(NULL);
    return err;
}

// Writes one FLV tag to an HTTP-TS player's encoder, as SrsLiveStream::streaming_send_messages does.
static srs_error_t ts_http_write(SrsTsStreamEncoder *enc, bool video, int64_t timestamp, const uint8_t *tag, int size)
{
    string data((const char *)tag, size);
    if (video) {
        return enc->write_video(timestamp, (char *)data.data(), (int)data.size());
    }
    return enc->write_audio(timestamp, (char *)data.data(), (int)data.size());
}

// FFmpeg pads a PES packet one byte short of a full packet with an adaptation field of only its length byte, 0. The
// SRT ingest skips that byte and gets the whole frame.
VOID TEST(KernelTsTest, SrtPublisherOneByteStuffingIsSkipped)
{
    srs_error_t err = srs_success;

    // The 14-byte PES header and 169 bytes of data leave one byte of the 184 for the adaptation field.
    string data = ts_frame_data(169, 1);
    string pes = ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data;
    vector<string> pkts = ts_pes_packets(0x101, 0, pes);
    ASSERT_EQ(1, (int)pkts.size());
    EXPECT_EQ(0x30, (uint8_t)pkts[0][3] & 0x30);
    EXPECT_EQ(0, (uint8_t)pkts[0][4]);

    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode_after_program(pkts[0], &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(0x101, h.msgs_[0].pid_);
    EXPECT_EQ(SrsTsStreamAudioAAC, h.msgs_[0].stream_);
    EXPECT_EQ(90000, h.msgs_[0].pts_);
    EXPECT_TRUE(data == h.msgs_[0].payload_);
}

// A packet whose adaptation field length is out of range for its adaptation field control is refused: more than 182
// bytes with a payload, or other than 183 without one.
VOID TEST(KernelTsTest, SrtPublisherCorruptAdaptationFieldFailsThePacket)
{
    if (true) {
        TsMessages h;
        string pkt = ts_packet_af(0x100, false, 3, string(1, (char)183), "");
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_AF, ts_decode_after_program(pkt, &h));
        EXPECT_EQ(0, (int)h.msgs_.size());
    }

    if (true) {
        TsMessages h;
        string pkt = ts_packet_af(0x100, false, 2, string(1, (char)100), "");
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_AF, ts_decode_after_program(pkt, &h));
        EXPECT_EQ(0, (int)h.msgs_.size());
    }
}

// An encoder with SCTE-35 ad markers sends a registration descriptor in its PMT, an ISO 639 language descriptor on
// its audio, and a SCTE-35 stream. The ingest keeps the H.264 and AAC streams, drops the SCTE-35 stream, and skips its
// packets.
VOID TEST(KernelTsTest, SrtPublisherPmtWithDescriptorsAndScte35Track)
{
    srs_error_t err = srs_success;

    string registration("\x05\x04" "CUEI", 6);
    string language("\x0a\x04" "eng\x00", 6);
    string streams = ts_pmt_es(0x1b, 0x100, "") + ts_pmt_es(0x0f, 0x101, language) + ts_pmt_es(0x86, 0x1f0, "");
    string ts = ts_psi_packet(0, ts_pat(0x1001)) + ts_psi_packet(0x1001, ts_pmt(0x100, registration, streams));

    SrsTsContext ctx;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&ctx, ts, &h));

    ASSERT_TRUE(ctx.get(0x100) != NULL);
    EXPECT_EQ(SrsTsPidApplyVideo, ctx.get(0x100)->apply_);
    EXPECT_EQ(SrsTsStreamVideoH264, ctx.get(0x100)->stream_);
    ASSERT_TRUE(ctx.get(0x101) != NULL);
    EXPECT_EQ(SrsTsPidApplyAudio, ctx.get(0x101)->apply_);
    EXPECT_EQ(SrsTsStreamAudioAAC, ctx.get(0x101)->stream_);
    EXPECT_TRUE(ctx.get(0x1f0) == NULL);

    // A splice_info_section on the SCTE-35 PID is skipped, and the next audio frame comes through.
    string scte35 = ts_packet(0x1f0, true, 0, ts_psi(0xfc, string(16, (char)0x00)));
    string data = ts_frame_data(32, 3);
    string audio = ts_packet(0x101, true, 0, ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data);
    HELPER_EXPECT_SUCCESS(ts_decode(&ctx, scte35 + audio, &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(0x101, h.msgs_[0].pid_);
    EXPECT_TRUE(data == h.msgs_[0].payload_);
}

// A PAT or PMT damaged in transit fails its packet: a bad CRC, a section longer than the packet, a section cut by the
// adaptation field, or a descriptor loop longer than the section. An empty section is ignored.
VOID TEST(KernelTsTest, SrtPublisherDamagedPsiFailsThePacket)
{
    srs_error_t err = srs_success;

    if (true) {
        string pat = ts_pat(0x1001);
        pat[pat.size() - 1] ^= 0xff;
        SrsTsContext ctx;
        TsMessages h;
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_PSI, ts_decode(&ctx, ts_psi_packet(0, pat), &h));
    }

    if (true) {
        string pat = ts_pat(0x1001);
        pat[2] = (char)0xb3;
        pat[3] = (char)0xff;
        SrsTsContext ctx;
        TsMessages h;
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_PSI, ts_decode(&ctx, ts_psi_packet(0, pat), &h));
    }

    if (true) {
        // The adaptation field leaves the pointer field and the table id, but not the section length.
        string pkt = ts_packet_af(0, true, 3, ts_af_stuffing(181), string("\x00\x00", 2));
        SrsTsContext ctx;
        TsMessages h;
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_PSI, ts_decode(&ctx, pkt, &h));
    }

    if (true) {
        // An empty PAT teaches nothing, and the program comes from the next PAT. The ingest only logs a failed
        // packet, so whether this one fails does not matter.
        SrsTsContext ctx;
        TsMessages h;
        srs_error_t r0 = ts_decode(&ctx, ts_psi_packet(0, string("\x00\x00\xb0\x00", 4)), &h);
        srs_freep(r0);
        EXPECT_TRUE(ctx.get(0x1001) == NULL);
        HELPER_EXPECT_SUCCESS(ts_decode(&ctx, ts_psi_packet(0, ts_pat(0x1001)), &h));
        ASSERT_TRUE(ctx.get(0x1001) != NULL);
        EXPECT_EQ(SrsTsPidApplyPMT, ctx.get(0x1001)->apply_);
    }

    if (true) {
        // The program_info_length says 1023 bytes.
        string pmt = ts_pmt(0x100, "", ts_pmt_es(0x1b, 0x100, ""));
        pmt[11] = (char)0xf3;
        pmt[12] = (char)0xff;
        SrsTsContext ctx;
        TsMessages h;
        string ts = ts_psi_packet(0, ts_pat(0x1001)) + ts_psi_packet(0x1001, pmt);
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_PMT, ts_decode(&ctx, ts, &h));
    }

    if (true) {
        // The audio stream's ES_info_length says 1023 bytes.
        string es = ts_pmt_es(0x0f, 0x101, "");
        es[3] = (char)0xf3;
        es[4] = (char)0xff;
        SrsTsContext ctx;
        TsMessages h;
        string ts = ts_psi_packet(0, ts_pat(0x1001)) + ts_psi_packet(0x1001, ts_pmt(0x101, "", es));
        packet_expect_error_code(ERROR_STREAM_CASTER_TS_PMT, ts_decode(&ctx, ts, &h));
    }
}

// An encoder that pads its PAT and PMT packets with zeros instead of 0xff still has its program read.
VOID TEST(KernelTsTest, SrtPublisherPsiPaddedWithZerosStillParses)
{
    srs_error_t err = srs_success;

    string streams = ts_pmt_es(0x1b, 0x100, "") + ts_pmt_es(0x0f, 0x101, "");
    string ts = ts_psi_packet(0, ts_pat(0x1001), (char)0x00) + ts_psi_packet(0x1001, ts_pmt(0x100, "", streams), (char)0x00);

    SrsTsContext ctx;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&ctx, ts, &h));
    ASSERT_TRUE(ctx.get(0x100) != NULL);
    EXPECT_EQ(SrsTsPidApplyVideo, ctx.get(0x100)->apply_);
    ASSERT_TRUE(ctx.get(0x101) != NULL);
    EXPECT_EQ(SrsTsPidApplyAudio, ctx.get(0x101)->apply_);
}

// A publisher whose stream the ingest joins in the middle of a frame: the rest of that frame is dropped, and the next
// frame comes through.
VOID TEST(KernelTsTest, SrtPublisherJoinedMidFrameDropsTheRest)
{
    srs_error_t err = srs_success;

    string data = ts_frame_data(404, 5);
    vector<string> pkts = ts_pes_packets(0x101, 0, ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data);
    ASSERT_EQ(3, (int)pkts.size());

    string next = ts_frame_data(100, 9);
    vector<string> next_pkts = ts_pes_packets(0x101, 3, ts_pes_header(0xc0, 91920, 91920, (int)next.size(), true) + next);

    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode_after_program(pkts[1] + pkts[2] + next_pkts[0], &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(91920, h.msgs_[0].pts_);
    EXPECT_TRUE(next == h.msgs_[0].payload_);
}

// The last packet of a frame is lost: the next frame's start shows it, the cut frame is dropped, and the next frame
// comes through.
VOID TEST(KernelTsTest, SrtPublisherLostLastPacketDropsTheFrame)
{
    srs_error_t err = srs_success;

    string data = ts_frame_data(404, 5);
    vector<string> pkts = ts_pes_packets(0x101, 0, ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data);
    ASSERT_EQ(3, (int)pkts.size());

    string next = ts_frame_data(100, 9);
    vector<string> next_pkts = ts_pes_packets(0x101, 3, ts_pes_header(0xc0, 91920, 91920, (int)next.size(), true) + next);

    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode_after_program(pkts[0] + pkts[1] + next_pkts[0], &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(91920, h.msgs_[0].pts_);
    EXPECT_TRUE(next == h.msgs_[0].payload_);
}

// A packet that arrives twice, as UDP may deliver it, is used once.
VOID TEST(KernelTsTest, SrtPublisherDuplicatedPacketIsUsedOnce)
{
    srs_error_t err = srs_success;

    string data = ts_frame_data(404, 5);
    vector<string> pkts = ts_pes_packets(0x101, 0, ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data);
    ASSERT_EQ(3, (int)pkts.size());

    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode_after_program(pkts[0] + pkts[1] + pkts[1] + pkts[2], &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_TRUE(data == h.msgs_[0].payload_);
}

// A packet in the middle of a frame is lost: the gap in the continuity counter drops the frame, and the next frame
// comes through.
VOID TEST(KernelTsTest, SrtPublisherLostMiddlePacketDropsTheFrame)
{
    srs_error_t err = srs_success;

    string data = ts_frame_data(404, 5);
    vector<string> pkts = ts_pes_packets(0x101, 0, ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data);
    ASSERT_EQ(3, (int)pkts.size());

    string next = ts_frame_data(100, 9);
    vector<string> next_pkts = ts_pes_packets(0x101, 3, ts_pes_header(0xc0, 91920, 91920, (int)next.size(), true) + next);

    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode_after_program(pkts[0] + pkts[2] + next_pkts[0], &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(91920, h.msgs_[0].pts_);
    EXPECT_TRUE(next == h.msgs_[0].payload_);
}

// A PES header damaged in transit fails its packet: a bad start code, a header cut by the adaptation field, optional
// fields past the packet, a PES_packet_length shorter than the header, or a PTS or DTS with a bad marker or prefix.
VOID TEST(KernelTsTest, SrtPublisherDamagedPesHeaderFailsThePacket)
{
    string data = ts_frame_data(8, 1);
    string pts_only = ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data;
    string pts_dts = ts_pes_header(0xe0, 93600, 90000, (int)data.size(), true) + data;

    vector<string> damaged;
    // The start code 00 00 02.
    damaged.push_back(pts_only);
    damaged.back()[2] = 0x02;
    // The PES_packet_length 3 holds less than the flags and the PTS.
    damaged.push_back(pts_only);
    damaged.back()[4] = 0x00;
    damaged.back()[5] = 0x03;
    // The PTS: the marker after bits 32..30, the prefix, the marker after bits 29..15, the marker after bits 14..0.
    damaged.push_back(pts_only);
    damaged.back()[9] &= ~0x01;
    damaged.push_back(pts_only);
    damaged.back()[9] &= 0x0f;
    damaged.push_back(pts_only);
    damaged.back()[11] &= ~0x01;
    damaged.push_back(pts_only);
    damaged.back()[13] &= ~0x01;
    // The PTS, then the DTS, of a frame with both.
    damaged.push_back(pts_dts);
    damaged.back()[9] &= ~0x01;
    damaged.push_back(pts_dts);
    damaged.back()[14] &= ~0x01;

    vector<string> pkts;
    for (int i = 0; i < (int)damaged.size(); i++) {
        pkts.push_back(ts_packet(0x101, true, 0, damaged[i]));
    }
    // A PES header of 4 bytes, cut by the adaptation field.
    pkts.push_back(ts_packet_af(0x101, true, 3, ts_af_stuffing(179), string("\x00\x00\x01\xc0", 4)));
    // The flags ask for a PTS and a DTS, but the packet ends after them.
    pkts.push_back(ts_packet_af(0x101, true, 3, ts_af_stuffing(174), string("\x00\x00\x01\xc0\x00\x00\x80\xc0\x0a", 9)));

    for (int i = 0; i < (int)pkts.size(); i++) {
        TsMessages h;
        srs_error_t err = ts_decode_after_program(pkts[i], &h);
        EXPECT_EQ(ERROR_STREAM_CASTER_TS_PSE, srs_error_code(err)) << "case " << i;
        srs_freep(err);
        EXPECT_EQ(0, (int)h.msgs_.size()) << "case " << i;
    }
}

// The ingest refuses a frame, for example the SRT bridge on a codec it does not support: the demuxer stops and
// returns its error.
VOID TEST(KernelTsTest, SrtIngestRefusingFrameFailsTheDecode)
{
    string data = ts_frame_data(32, 3);
    string audio = ts_packet(0x101, true, 0, ts_pes_header(0xc0, 90000, 90000, (int)data.size(), true) + data);

    TsMessages h;
    h.refuse_ = srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "unsupported codec");
    packet_expect_error_code(ERROR_STREAM_CASTER_TS_CODEC, ts_decode_after_program(audio, &h));
    EXPECT_EQ(1, (int)h.msgs_.size());
}

// An H.264 encoder such as OBS sends the SPS and PPS only in the sequence header. HLS puts them before each key frame
// of the segment, after an access unit delimiter, and only the delimiter before an inter frame.
VOID TEST(KernelTsTest, HlsKeyFrameGetsSpsPpsOfSequenceHeader)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    SrsTsContext ctx;
    SrsTsContextWriter tscw(&w, &ctx, SrsAudioCodecIdDisabled, SrsVideoCodecIdAVC);
    SrsFormat fmt;
    HELPER_EXPECT_SUCCESS(fmt.initialize());
    SrsTsMessageCache cache;

    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 0, string((const char *)mp4_avc_sh, sizeof(mp4_avc_sh))));
    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 40, string((const char *)mp4_avc_idr, sizeof(mp4_avc_idr))));
    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 80, string((const char *)mp4_avc_inter, sizeof(mp4_avc_inter))));

    SrsTsContext demux;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&demux, w.str(), &h));
    ASSERT_EQ(2, (int)h.msgs_.size());

    vector<string> key = ts_annexb_nalus(h.msgs_[0].payload_);
    ASSERT_EQ(4, (int)key.size());
    EXPECT_TRUE(string("\x09\xf0", 2) == key[0]);
    EXPECT_TRUE(string((const char *)mp4_avc_sh + 13, 25) == key[1]);
    EXPECT_TRUE(string((const char *)mp4_avc_sh + 41, 5) == key[2]);
    EXPECT_TRUE(string((const char *)mp4_avc_idr + 9, 5) == key[3]);

    vector<string> inter = ts_annexb_nalus(h.msgs_[1].payload_);
    ASSERT_EQ(2, (int)inter.size());
    EXPECT_TRUE(string("\x09\xf0", 2) == inter[0]);
    EXPECT_TRUE(string((const char *)mp4_avc_inter + 9, 5) == inter[1]);
}

// An HEVC encoder sends the VPS, SPS and PPS only in the sequence header. HLS puts them before each IRAP frame, and
// nothing before an inter frame.
VOID TEST(KernelTsTest, HlsHevcKeyFrameGetsParameterSetsOfSequenceHeader)
{
    srs_error_t err = srs_success;

    string sh;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, sh));

    MockSrsFileWriter w;
    SrsTsContext ctx;
    SrsTsContextWriter tscw(&w, &ctx, SrsAudioCodecIdDisabled, SrsVideoCodecIdHEVC);
    SrsFormat fmt;
    HELPER_EXPECT_SUCCESS(fmt.initialize());
    SrsTsMessageCache cache;

    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 0, sh));
    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 40, string((const char *)mp4_hevc_idr, sizeof(mp4_hevc_idr))));
    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 80, string((const char *)mp4_hevc_inter, sizeof(mp4_hevc_inter))));

    SrsTsContext demux;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&demux, w.str(), &h));
    ASSERT_EQ(2, (int)h.msgs_.size());
    EXPECT_EQ(SrsTsStreamVideoHEVC, h.msgs_[0].stream_);

    vector<string> key = ts_annexb_nalus(h.msgs_[0].payload_);
    ASSERT_EQ(4, (int)key.size());
    EXPECT_EQ(SrsHevcNaluType_VPS, SrsHevcNaluTypeParse(key[0][0]));
    EXPECT_EQ(SrsHevcNaluType_SPS, SrsHevcNaluTypeParse(key[1][0]));
    EXPECT_EQ(SrsHevcNaluType_PPS, SrsHevcNaluTypeParse(key[2][0]));
    EXPECT_TRUE(string((const char *)mp4_hevc_idr + 9, 5) == key[3]);

    vector<string> inter = ts_annexb_nalus(h.msgs_[1].payload_);
    ASSERT_EQ(1, (int)inter.size());
    EXPECT_TRUE(string((const char *)mp4_hevc_inter + 9, 5) == inter[0]);
}

// A frame whose composition time is over 1s, as an encoder with a deep B-frame delay sends, keeps its PTS through the
// HLS segment and back through the demuxer.
VOID TEST(KernelTsTest, HlsFrameWithCompositionTimeOver1sKeepsItsPts)
{
    srs_error_t err = srs_success;

    string idr((const char *)mp4_avc_idr, sizeof(mp4_avc_idr));
    idr[2] = 0x00;
    idr[3] = 0x05;
    idr[4] = (char)0xdc; // 1500ms

    MockSrsFileWriter w;
    SrsTsContext ctx;
    SrsTsContextWriter tscw(&w, &ctx, SrsAudioCodecIdDisabled, SrsVideoCodecIdAVC);
    SrsFormat fmt;
    HELPER_EXPECT_SUCCESS(fmt.initialize());
    SrsTsMessageCache cache;

    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 0, string((const char *)mp4_avc_sh, sizeof(mp4_avc_sh))));
    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, true, 1000, idr));

    SrsTsContext demux;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&demux, w.str(), &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(90000, h.msgs_[0].dts_);
    EXPECT_EQ(90000 + 1500 * 90, h.msgs_[0].pts_);
}

// An MP3 radio stream to HLS: the PMT lists MP3 on its PID, carrying the PCR, and each frame goes out as it came.
VOID TEST(KernelTsTest, HlsMp3AudioOnlyStream)
{
    srs_error_t err = srs_success;

    // MP3, 44.1 kHz, 16-bit, stereo; an MPEG-1 Layer III frame header and data.
    string frame = string("\xff\xfb\x90\x64", 4) + ts_frame_data(28, 2);
    string tag = string("\x2f", 1) + frame;

    MockSrsFileWriter w;
    SrsTsContext ctx;
    SrsTsContextWriter tscw(&w, &ctx, SrsAudioCodecIdMP3, SrsVideoCodecIdDisabled);
    SrsFormat fmt;
    HELPER_EXPECT_SUCCESS(fmt.initialize());
    SrsTsMessageCache cache;

    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, false, 0, tag));
    HELPER_EXPECT_SUCCESS(ts_hls_write(&fmt, &cache, &tscw, false, 26, tag));

    int pcr_pid = 0;
    vector<pair<int, int> > streams;
    ASSERT_TRUE(ts_read_pmt(w.str(), pcr_pid, streams));
    ASSERT_EQ(1, (int)streams.size());
    EXPECT_EQ(SrsTsStreamAudioMp3, streams[0].first);
    EXPECT_EQ(0x102, streams[0].second);
    EXPECT_EQ(0x102, pcr_pid);
    // The PAT, the PMT, then the first frame's packet with the PCR.
    EXPECT_TRUE(ts_has_pcr(w.str(), 2));

    SrsTsContext demux;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&demux, w.str(), &h));
    ASSERT_EQ(2, (int)h.msgs_.size());
    EXPECT_EQ(SrsTsStreamAudioMp3, h.msgs_[0].stream_);
    EXPECT_TRUE(frame == h.msgs_[0].payload_);
    EXPECT_EQ(26 * 90, h.msgs_[1].dts_);
    EXPECT_TRUE(frame == h.msgs_[1].payload_);
}

// An HTTP-TS player leaves: the write of the PMT after the PAT, or of a frame after both, fails, and the encoder
// returns the error.
VOID TEST(KernelTsTest, HttpTsPlayerLeavesMidStream)
{
    srs_error_t err = srs_success;

    if (true) {
        MockSrsFileWriter w;
        w.error_offset = SRS_TS_PACKET_SIZE;
        SrsTsStreamEncoder enc;
        HELPER_EXPECT_SUCCESS(enc.initialize(&w, NULL));
        HELPER_EXPECT_SUCCESS(ts_http_write(&enc, false, 0, mp4_aac_sh, sizeof(mp4_aac_sh)));
        err = ts_http_write(&enc, false, 23, mp4_aac_frame, sizeof(mp4_aac_frame));
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
        EXPECT_EQ(SRS_TS_PACKET_SIZE, w.filesize());
    }

    if (true) {
        MockSrsFileWriter w;
        w.error_offset = 2 * SRS_TS_PACKET_SIZE;
        SrsTsStreamEncoder enc;
        HELPER_EXPECT_SUCCESS(enc.initialize(&w, NULL));
        HELPER_EXPECT_SUCCESS(ts_http_write(&enc, true, 0, mp4_avc_sh, sizeof(mp4_avc_sh)));
        err = ts_http_write(&enc, true, 40, mp4_avc_idr, sizeof(mp4_avc_idr));
        EXPECT_TRUE(err != srs_success);
        srs_freep(err);
        EXPECT_EQ(2 * SRS_TS_PACKET_SIZE, w.filesize());
    }
}

// A publisher sends a video info frame (#288): the HTTP-TS player gets nothing for it, and the next key frame.
VOID TEST(KernelTsTest, HttpTsVideoInfoFrameIsSkipped)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    SrsTsStreamEncoder enc;
    HELPER_EXPECT_SUCCESS(enc.initialize(&w, NULL));
    HELPER_EXPECT_SUCCESS(ts_http_write(&enc, true, 0, mp4_avc_sh, sizeof(mp4_avc_sh)));

    // Frame type 5 (video info), H.264, packet type 1.
    const uint8_t info[] = {0x57, 0x01};
    HELPER_EXPECT_SUCCESS(ts_http_write(&enc, true, 20, info, sizeof(info)));
    EXPECT_EQ(0, w.filesize());

    HELPER_EXPECT_SUCCESS(ts_http_write(&enc, true, 40, mp4_avc_idr, sizeof(mp4_avc_idr)));
    SrsTsContext demux;
    TsMessages h;
    HELPER_EXPECT_SUCCESS(ts_decode(&demux, w.str(), &h));
    ASSERT_EQ(1, (int)h.msgs_.size());
    EXPECT_EQ(40 * 90, h.msgs_[0].dts_);
}

// A broken encoder sends an AAC frame of 8192 bytes, more than the 13-bit ADTS frame length can carry: the HTTP-TS
// encoder refuses it.
VOID TEST(KernelTsTest, HttpTsAacFrameTooLongForAdtsIsRefused)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    SrsTsStreamEncoder enc;
    HELPER_EXPECT_SUCCESS(enc.initialize(&w, NULL));
    HELPER_EXPECT_SUCCESS(ts_http_write(&enc, false, 0, mp4_aac_sh, sizeof(mp4_aac_sh)));

    string tag = string("\xaf\x01", 2) + string(8192, (char)0x21);
    packet_expect_error_code(ERROR_HLS_AAC_FRAME_LENGTH, ts_http_write(&enc, false, 23, (const uint8_t *)tag.data(), (int)tag.size()));
    EXPECT_EQ(0, w.filesize());
}

extern srs_fwrite_t _srs_fwrite_fn;

static size_t ts_fwrite_disk_full(const void * /*ptr*/, size_t /*size*/, size_t /*nitems*/, FILE * /*stream*/)
{
    return 0;
}

// Makes every fwrite fail as on a full disk while it lives.
class TsDiskFullScope
{
private:
    srs_fwrite_t fwrite_;

public:
    TsDiskFullScope()
    {
        fwrite_ = _srs_fwrite_fn;
        _srs_fwrite_fn = ts_fwrite_disk_full;
    }
    ~TsDiskFullScope()
    {
        _srs_fwrite_fn = fwrite_;
    }
};

// An encrypted HLS segment on a full disk: the write that fills an AES block fails, and closing the segment, whose
// padding block fails to write too, still closes the file. Nothing reaches the disk.
VOID TEST(KernelTsTest, HlsEncryptedSegmentOnFullDisk)
{
    srs_error_t err = srs_success;

    unsigned char key[16];
    unsigned char iv[16];
    for (int i = 0; i < 16; i++) {
        key[i] = (unsigned char)i;
        iv[i] = (unsigned char)(0xf0 + i);
    }

    string path = srs_utest_tmp_dir() + "/srs_utest_ai40_hls_enc_full_" + srs_strconv_format_int(getpid()) + ".ts";

    char pkt[SRS_TS_PACKET_SIZE];
    memset(pkt, 0xff, sizeof(pkt));
    pkt[0] = 0x47;

    SrsEncFileWriter w;
    HELPER_EXPECT_SUCCESS(w.config_cipher(key, iv));
    HELPER_ASSERT_SUCCESS(w.open(path));

    if (true) {
        TsDiskFullScope full;

        // Three packets wait in the AES block; the fourth fills it, which fails to go to disk.
        for (int i = 0; i < 3; i++) {
            HELPER_EXPECT_SUCCESS(w.write(pkt, SRS_TS_PACKET_SIZE, NULL));
        }
        packet_expect_error_code(ERROR_SYSTEM_FILE_WRITE, w.write(pkt, SRS_TS_PACKET_SIZE, NULL));

        // HLS closes the segment when it gives up on it, with one more packet waiting.
        HELPER_EXPECT_SUCCESS(w.write(pkt, SRS_TS_PACKET_SIZE, NULL));
        w.close();
    }
    EXPECT_FALSE(w.is_open());

    FILE *fp = fopen(path.c_str(), "rb");
    ASSERT_TRUE(fp != NULL);
    fseek(fp, 0, SEEK_END);
    EXPECT_EQ(0, ftell(fp));
    fclose(fp);
    unlink(path.c_str());
}

// B15: an empty audio tag of an RTMP aggregate message (the B5 path) makes the HTTP-TS encoder write the previous
// audio frame again. SrsFormat::on_audio returns early for an empty tag and keeps the previous frame, whose samples
// point into the previous message's payload, and SrsTsTransmuxer caches and writes that frame.
VOID TEST(KernelTsTest, DISABLED_HttpTsEmptyAudioAfterFrame_PreviousFrameWrittenAgain)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter w;
    SrsTsStreamEncoder enc;
    HELPER_EXPECT_SUCCESS(enc.initialize(&w, NULL));

    HELPER_EXPECT_SUCCESS(ts_http_write(&enc, false, 0, mp4_aac_sh, sizeof(mp4_aac_sh)));
    // The frame stays alive here, as when the GOP cache still holds its message; with the payload freed, the
    // second write reads freed memory.
    string frame((const char *)mp4_aac_frame, sizeof(mp4_aac_frame));
    HELPER_EXPECT_SUCCESS(enc.write_audio(23, (char *)frame.data(), (int)frame.size()));
    int64_t size = w.filesize();

    HELPER_EXPECT_SUCCESS(enc.write_audio(46, NULL, 0));
    EXPECT_EQ(size, w.filesize());
}

// B16: a vhost with parse_sps off (#474) takes an H.264 stream whose SPS the parser refuses, here one with the
// reserved_zero_2bits set, and plays it over HTTP-FLV and HLS, but its HTTP-TS players are refused at the sequence
// header: SrsTsTransmuxer parses the stream again with its own SrsFormat, which always parses the SPS.
VOID TEST(KernelTsTest, DISABLED_HttpTsWithParseSpsOff_SequenceHeaderRefused)
{
    srs_error_t err = srs_success;

    // The SPS's constraint flags byte, 0x00, with the reserved_zero_2bits set.
    string sh((const char *)mp4_avc_sh, sizeof(mp4_avc_sh));
    sh[15] = 0x03;

    // The source, with parse_sps off, takes the sequence header.
    SrsFormat source;
    HELPER_EXPECT_SUCCESS(source.initialize());
    source.avc_parse_sps_ = false;
    HELPER_EXPECT_SUCCESS(source.on_video(0, (char *)sh.data(), (int)sh.size()));

    MockSrsFileWriter w;
    SrsTsStreamEncoder enc;
    HELPER_EXPECT_SUCCESS(enc.initialize(&w, NULL));
    HELPER_EXPECT_SUCCESS(enc.write_video(0, (char *)sh.data(), (int)sh.size()));
    HELPER_EXPECT_SUCCESS(ts_http_write(&enc, true, 40, mp4_avc_idr, sizeof(mp4_avc_idr)));
    EXPECT_LT(0, w.filesize());
}
