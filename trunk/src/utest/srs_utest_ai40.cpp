//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai40.hpp>

#include <srs_core_platform.hpp>
#include <srs_kernel_aac.hpp>
#include <srs_kernel_buffer.hpp>
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
