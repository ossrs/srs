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
#include <srs_app_http_stream.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(SRS_BACKTRACE) && defined(__linux)
#include <dlfcn.h>
#endif

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
