//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai40.hpp>

#include <srs_app_factory.hpp>
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
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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

// The server's stats lines are built every 10s from global counters that every other test shares.
// This scope gives each listed counter a test clock and an empty history, as in a server that just
// started, and puts the counter back as it was when the test ends.
class KbpsGlobalPpsScope
{
public:
    MockWallClock clock_;
    vector<SrsPps *> pps_;
    vector<SrsPps> saved_;

public:
    KbpsGlobalPpsScope(const vector<SrsPps *> &pps)
    {
        pps_ = pps;
        for (int i = 0; i < (int)pps.size(); i++) {
            SrsPps *p = pps[i];
            saved_.push_back(*p);

            SrsPps fresh;
            fresh.clk_ = &clock_;
            *p = fresh;
        }
    }
    ~KbpsGlobalPpsScope()
    {
        for (int i = 0; i < (int)pps_.size(); i++) {
            *pps_[i] = saved_[i];
        }
    }
};

static vector<SrsPps *> kbps_server_line_pps()
{
    SrsPps *all[] = {_srs_pps_cids_get, _srs_pps_cids_set, _srs_pps_timer, _srs_pps_pub, _srs_pps_conn,
                     _srs_pps_dispose, _srs_pps_clock_15ms, _srs_pps_clock_20ms, _srs_pps_clock_25ms,
                     _srs_pps_clock_30ms, _srs_pps_clock_35ms, _srs_pps_clock_40ms, _srs_pps_clock_80ms,
                     _srs_pps_clock_160ms, _srs_pps_timer_s, _srs_pps_objs_rtps, _srs_pps_objs_rraw,
                     _srs_pps_objs_rfua, _srs_pps_objs_rbuf, _srs_pps_objs_msgs, _srs_pps_objs_rothers};
    return vector<SrsPps *>(all, all + sizeof(all) / sizeof(all[0]));
}

static vector<SrsPps *> kbps_rtc_line_pps()
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
static srs_error_t mp4_hevc_sh(int nb_pps, string &tag, uint8_t pps_flags = 0xc1)
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
static string ps_pack_header()
{
    return string("\x00\x00\x01\xba\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05", 20);
}

// The system header fields of the same camera after header_length: rate bound, audio and video bounds, flags.
static string ps_system_fields()
{
    return string("\x80\x98\x09\x04\xe1\x7f", 6);
}

// The system header of the same camera, listing streams 0xe0, 0xc0, 0xbd and 0xbf.
static string ps_system_header()
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
static string ps_pes(uint8_t sid, const string &body)
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
