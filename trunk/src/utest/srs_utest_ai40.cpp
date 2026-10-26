//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai40.hpp>

#include <srs_core_platform.hpp>

#include <fcntl.h>
#include <stdio.h>
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
