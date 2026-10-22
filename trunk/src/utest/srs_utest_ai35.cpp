//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai35.hpp>

#include <srs_app_process.hpp>
#include <srs_core_platform.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_st.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#include <vector>

using namespace std;

#ifndef _WIN32 // POSIX only: fcntl tells a closed descriptor
VOID TEST(PlatformTest, CloseSocketClosesIt)
{
    srs_socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, fd);

    EXPECT_EQ(0, srs_close_socket(fd));

    errno = 0;
    EXPECT_EQ(-1, ::fcntl(fd, F_GETFD));
    EXPECT_EQ(EBADF, errno);
}
#endif

VOID TEST(PlatformTest, CloseInvalidSocketFails)
{
    EXPECT_EQ(-1, srs_close_socket(SRS_INVALID_SOCKET));
}

VOID TEST(PlatformTest, SetsockoptSetsTheOption)
{
    srs_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, fd);

    int v = 1;
    EXPECT_EQ(0, srs_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)));

    int got = 0;
    socklen_t size = sizeof(got);
    EXPECT_EQ(0, ::getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&got, &size));
    EXPECT_NE(0, got);

    v = 0;
    EXPECT_EQ(0, srs_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)));
    size = sizeof(got);
    EXPECT_EQ(0, ::getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&got, &size));
    EXPECT_EQ(0, got);

    srs_close_socket(fd);
}

VOID TEST(PlatformTest, SetsockoptOnInvalidSocketFails)
{
    int v = 1;
    EXPECT_EQ(-1, srs_setsockopt(SRS_INVALID_SOCKET, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)));
}

VOID TEST(PlatformTest, GettimeofdayGivesTheWallClock)
{
    time_t before = ::time(NULL);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = -1;
    EXPECT_EQ(0, srs_gettimeofday(&tv, NULL));

    time_t after = ::time(NULL);
    EXPECT_GE((time_t)tv.tv_sec, before);
    EXPECT_LE((time_t)tv.tv_sec, after);
    EXPECT_GE(tv.tv_usec, 0);
    EXPECT_LT(tv.tv_usec, 1000000);
}

VOID TEST(PlatformTest, GmtimeGivesUtcFields)
{
    // 2023-11-14 22:13:20 UTC, a Tuesday.
    time_t t = 1700000000;
    struct tm r;
    memset(&r, 0, sizeof(r));

    EXPECT_EQ(&r, srs_gmtime_r(&t, &r));
    EXPECT_EQ(123, r.tm_year);
    EXPECT_EQ(10, r.tm_mon);
    EXPECT_EQ(14, r.tm_mday);
    EXPECT_EQ(22, r.tm_hour);
    EXPECT_EQ(13, r.tm_min);
    EXPECT_EQ(20, r.tm_sec);
    EXPECT_EQ(2, r.tm_wday);
    EXPECT_EQ(317, r.tm_yday);
}

#ifndef _WIN32 // POSIX only: compares with localtime_r
VOID TEST(PlatformTest, LocaltimeMatchesTheSystem)
{
    time_t t = 1700000000;
    struct tm local;
    memset(&local, 0, sizeof(local));
    EXPECT_EQ(&local, srs_localtime_r(&t, &local));

    struct tm want;
    memset(&want, 0, sizeof(want));
    ASSERT_TRUE(::localtime_r(&t, &want) != NULL);

    EXPECT_EQ(want.tm_year, local.tm_year);
    EXPECT_EQ(want.tm_mon, local.tm_mon);
    EXPECT_EQ(want.tm_mday, local.tm_mday);
    EXPECT_EQ(want.tm_hour, local.tm_hour);
    EXPECT_EQ(want.tm_min, local.tm_min);
    EXPECT_EQ(want.tm_sec, local.tm_sec);
    EXPECT_EQ(want.tm_isdst, local.tm_isdst);
}
#endif

VOID TEST(PlatformTest, RandomRepeatsForTheSameSeed)
{
    // srs_srandom reseeds the state of SrsUtestRandomState, which the run's state replaces again after it.
    SrsUtestRandomState state(1);

    srs_srandom(7);
    vector<long> first;
    for (int i = 0; i < 8; i++) {
        first.push_back(srs_random());
    }

    srs_srandom(7);
    vector<long> second;
    for (int i = 0; i < 8; i++) {
        second.push_back(srs_random());
    }
    EXPECT_EQ(first, second);

    srs_srandom(8);
    vector<long> other;
    for (int i = 0; i < 8; i++) {
        other.push_back(srs_random());
    }
    EXPECT_NE(first, other);

    for (int i = 0; i < (int)first.size(); i++) {
        EXPECT_GE(first[i], 0);
        EXPECT_LE(first[i], 0x7fffffffL);
    }
}

VOID TEST(PlatformTest, RandomUsesMoreThan15Bits)
{
    SrsUtestRandomState state(1);

    srs_srandom(7);
    long bits = 0;
    for (int i = 0; i < 64; i++) {
        bits |= srs_random();
    }
    EXPECT_GT(bits, 0x7fffL);
}

VOID TEST(PlatformTest, InitstateRepeatsForTheSameSeed)
{
    char a[128], b[128];

    char *previous = srs_initstate(5, a, sizeof(a));
    ASSERT_TRUE(previous != NULL);
    vector<long> first;
    for (int i = 0; i < 8; i++) {
        first.push_back(srs_random());
    }

    EXPECT_TRUE(srs_initstate(5, b, sizeof(b)) == a);
    vector<long> second;
    for (int i = 0; i < 8; i++) {
        second.push_back(srs_random());
    }
    EXPECT_EQ(first, second);

    EXPECT_TRUE(srs_setstate(previous) == b);
}

VOID TEST(PlatformTest, SetstateGoesOnWithTheReplacedSequence)
{
    char want[128], a[128], b[128];

    char *previous = srs_initstate(1, want, sizeof(want));
    ASSERT_TRUE(previous != NULL);
    long want1 = srs_random();
    long want2 = srs_random();

    srs_initstate(1, a, sizeof(a));
    EXPECT_EQ(want1, srs_random());
    // Another sequence for a while, then back to the first, which goes on where it stopped.
    char *replaced = srs_initstate(2, b, sizeof(b));
    EXPECT_TRUE(replaced == a);
    srs_random();
    EXPECT_TRUE(srs_setstate(replaced) == b);
    EXPECT_EQ(want2, srs_random());

    srs_setstate(previous);
}

#ifndef SRS_OSX
VOID TEST(PlatformTest, TimeHookDefaultsToTheShim)
{
    extern srs_gettimeofday_t _srs_gettimeofday;
    EXPECT_TRUE(_srs_gettimeofday == srs_gettimeofday);
}
#endif

// Past 4 GB, so a 32-bit offset cannot hold it; seeking there writes nothing.
static const srs_off_t kPlatformFarOffset = (srs_off_t)5 * 1024 * 1024 * 1024;

static string platform_tmp_file(const char *name)
{
    return _srs_tmp_file_prefix + "platform-" + name;
}

static void platform_write_file(const string &path, const string &data)
{
    FILE *fp = ::fopen(path.c_str(), "wb");
    ASSERT_TRUE(fp != NULL);
    ASSERT_EQ(data.size(), ::fwrite(data.data(), 1, data.size(), fp));
    ::fclose(fp);
}

static string platform_read_file(const string &path)
{
    string data;
    FILE *fp = ::fopen(path.c_str(), "rb");
    if (!fp) {
        return data;
    }

    char buf[64];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), fp)) > 0) {
        data.append(buf, n);
    }
    ::fclose(fp);
    return data;
}

VOID TEST(PlatformTest, FileWriteThenReadKeepsTheBytes)
{
    string path = platform_tmp_file("file-rw");
    ::unlink(path.c_str());

    // The CR LF must come back as is, not as text-mode line ends.
    const char data[] = "a\r\nb\n\x1a" "c";
    int fd = srs_file_open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    EXPECT_EQ((ssize_t)sizeof(data) - 1, srs_file_write(fd, data, sizeof(data) - 1));
    EXPECT_EQ(0, srs_file_close(fd));

    fd = srs_file_open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    char buf[64];
    EXPECT_EQ((ssize_t)sizeof(data) - 1, srs_file_read(fd, buf, sizeof(buf)));
    EXPECT_EQ(string(data, sizeof(data) - 1), string(buf, sizeof(data) - 1));
    EXPECT_EQ(0, srs_file_read(fd, buf, sizeof(buf)));
    EXPECT_EQ(0, srs_file_close(fd));

    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FileOpenMissingFails)
{
    EXPECT_EQ(-1, srs_file_open(platform_tmp_file("no-such-file").c_str(), O_RDONLY));
}

VOID TEST(PlatformTest, FileLseekUses64BitOffsets)
{
    string path = platform_tmp_file("file-lseek");
    platform_write_file(path, "0123456789");

    int fd = srs_file_open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(10, srs_file_lseek(fd, 0, SEEK_END));
    EXPECT_EQ(kPlatformFarOffset, srs_file_lseek(fd, kPlatformFarOffset, SEEK_SET));
    EXPECT_EQ(kPlatformFarOffset, srs_file_lseek(fd, 0, SEEK_CUR));
    EXPECT_EQ(3, srs_file_lseek(fd, 3, SEEK_SET));

    char c = 0;
    EXPECT_EQ(1, srs_file_read(fd, &c, 1));
    EXPECT_EQ('3', c);
    EXPECT_EQ(0, srs_file_close(fd));

    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FseekUses64BitOffsets)
{
    string path = platform_tmp_file("fseek");
    FILE *fp = ::fopen(path.c_str(), "wb");
    ASSERT_TRUE(fp != NULL);

    EXPECT_EQ(0, srs_fseek(fp, kPlatformFarOffset, SEEK_SET));
    EXPECT_EQ(kPlatformFarOffset, srs_ftell(fp));
    EXPECT_EQ(0, srs_fseek(fp, 0, SEEK_SET));
    EXPECT_EQ(0, srs_ftell(fp));

    ::fclose(fp);
    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FileHooksDefaultToTheShims)
{
    extern srs_open_t _srs_open_fn;
    extern srs_read_t _srs_read_fn;
    extern srs_write_t _srs_write_fn;
    extern srs_lseek_t _srs_lseek_fn;
    extern srs_close_t _srs_close_fn;
    extern srs_fseek_t _srs_fseek_fn;
    extern srs_ftell_t _srs_ftell_fn;

    EXPECT_TRUE(_srs_open_fn == srs_file_open);
    EXPECT_TRUE(_srs_read_fn == srs_file_read);
    EXPECT_TRUE(_srs_write_fn == srs_file_write);
    EXPECT_TRUE(_srs_lseek_fn == srs_file_lseek);
    EXPECT_TRUE(_srs_close_fn == srs_file_close);
    EXPECT_TRUE(_srs_fseek_fn == srs_fseek);
    EXPECT_TRUE(_srs_ftell_fn == srs_ftell);
}

VOID TEST(PlatformTest, FileWriterSeeksPast4GB)
{
    srs_error_t err;
    string path = platform_tmp_file("writer-seek");

    SrsFileWriter w;
    HELPER_ASSERT_SUCCESS(w.open(path));

    w.seek2(kPlatformFarOffset);
    EXPECT_EQ(kPlatformFarOffset, w.tellg());

    srs_off_t seeked = 0;
    HELPER_EXPECT_SUCCESS(w.lseek(kPlatformFarOffset + 1, SEEK_SET, &seeked));
    EXPECT_EQ(kPlatformFarOffset + 1, seeked);

    w.seek2(0);
    w.close();
    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FileReaderSeeksPast4GB)
{
    srs_error_t err;
    string path = platform_tmp_file("reader-seek");
    platform_write_file(path, "0123456789");

    SrsFileReader r;
    HELPER_ASSERT_SUCCESS(r.open(path));
    EXPECT_EQ(10, r.filesize());

    EXPECT_EQ(kPlatformFarOffset, r.seek2(kPlatformFarOffset));
    EXPECT_EQ(kPlatformFarOffset, r.tellg());

    srs_off_t seeked = 0;
    HELPER_EXPECT_SUCCESS(r.lseek(2, SEEK_SET, &seeked));
    EXPECT_EQ(2, seeked);

    char c = 0;
    ssize_t nread = 0;
    HELPER_EXPECT_SUCCESS(r.read(&c, 1, &nread));
    EXPECT_EQ(1, nread);
    EXPECT_EQ('2', c);

    r.close();
    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, MkdirCreatesTheDirOnce)
{
    string path = platform_tmp_file("mkdir");
    ::rmdir(path.c_str());

    EXPECT_EQ(0, srs_mkdir(path.c_str()));
    EXPECT_TRUE(SrsPath().is_dir(path));

    errno = 0;
    EXPECT_EQ(-1, srs_mkdir(path.c_str()));
    EXPECT_EQ(EEXIST, errno);

    ::rmdir(path.c_str());
}

VOID TEST(PlatformTest, RenameReplacesTheTarget)
{
    string from = platform_tmp_file("rename-from");
    string to = platform_tmp_file("rename-to");
    platform_write_file(from, "new");
    platform_write_file(to, "old");

    EXPECT_EQ(0, srs_rename(from.c_str(), to.c_str()));
    EXPECT_EQ("new", platform_read_file(to));
    EXPECT_FALSE(SrsPath().exists(from));

    ::unlink(to.c_str());
}

VOID TEST(PlatformTest, RenameToANewName)
{
    string from = platform_tmp_file("rename-new-from");
    string to = platform_tmp_file("rename-new-to");
    ::unlink(to.c_str());
    platform_write_file(from, "data");

    EXPECT_EQ(0, srs_rename(from.c_str(), to.c_str()));
    EXPECT_EQ("data", platform_read_file(to));
    EXPECT_FALSE(SrsPath().exists(from));

    ::unlink(to.c_str());
}

VOID TEST(PlatformTest, RenameMissingSourceFails)
{
    string from = platform_tmp_file("rename-missing");
    string to = platform_tmp_file("rename-missing-to");
    platform_write_file(to, "old");

    EXPECT_EQ(-1, srs_rename(from.c_str(), to.c_str()));
    EXPECT_EQ("old", platform_read_file(to));

    ::unlink(to.c_str());
}

VOID TEST(PlatformTest, GetsockoptReadsTheOption)
{
    srs_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, fd);

    int v = 1;
    EXPECT_EQ(0, srs_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)));

    int got = 0;
    socklen_t size = sizeof(got);
    EXPECT_EQ(0, srs_getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &got, &size));
    EXPECT_NE(0, got);
    EXPECT_EQ((socklen_t)sizeof(got), size);

    srs_close_socket(fd);
}

VOID TEST(PlatformTest, GetsockoptOnInvalidSocketFails)
{
    int got = 0;
    socklen_t size = sizeof(got);
    EXPECT_EQ(-1, srs_getsockopt(SRS_INVALID_SOCKET, SOL_SOCKET, SO_SNDBUF, &got, &size));
}

static sockaddr_in platform_loopback(int port)
{
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

VOID TEST(PlatformTest, BindTakesAFreePort)
{
    srs_error_t err;

    int types[] = {SOCK_STREAM, SOCK_DGRAM};
    for (int i = 0; i < 2; i++) {
        int port = 0;
        HELPER_ASSERT_SUCCESS(srs_utest_free_port(types[i], &port));
        sockaddr_in addr = platform_loopback(port);

        srs_socket_t fd = ::socket(AF_INET, types[i], 0);
        ASSERT_NE(SRS_INVALID_SOCKET, fd);
        EXPECT_EQ(0, srs_bind(fd, (sockaddr *)&addr, sizeof(addr)));
        EXPECT_FALSE(srs_utest_port_free(types[i], "127.0.0.1", port));
        srs_close_socket(fd);
    }
}

// SRS tells a port in use from other bind failures by errno, which Winsock does not set.
VOID TEST(PlatformTest, BindFailsWithEaddrinuseOnATakenPort)
{
    srs_error_t err;

    int types[] = {SOCK_STREAM, SOCK_DGRAM};
    for (int i = 0; i < 2; i++) {
        int port = 0;
        HELPER_ASSERT_SUCCESS(srs_utest_free_port(types[i], &port));
        sockaddr_in addr = platform_loopback(port);

        srs_socket_t holder = ::socket(AF_INET, types[i], 0);
        ASSERT_NE(SRS_INVALID_SOCKET, holder);
        ASSERT_EQ(0, ::bind(holder, (sockaddr *)&addr, sizeof(addr)));

        srs_socket_t fd = ::socket(AF_INET, types[i], 0);
        ASSERT_NE(SRS_INVALID_SOCKET, fd);
        errno = 0;
        EXPECT_EQ(-1, srs_bind(fd, (sockaddr *)&addr, sizeof(addr)));
        EXPECT_EQ(EADDRINUSE, errno);

        srs_close_socket(fd);
        srs_close_socket(holder);
    }
}

VOID TEST(PlatformTest, SocketCloexecSetsTheFlag)
{
    srs_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, fd);
    EXPECT_FALSE(srs_utest_socket_cloexec(fd));

    EXPECT_EQ(0, srs_socket_cloexec(fd));
    EXPECT_TRUE(srs_utest_socket_cloexec(fd));

    srs_close_socket(fd);
}

VOID TEST(PlatformTest, SocketCloexecOnInvalidSocketFails)
{
    EXPECT_EQ(-1, srs_socket_cloexec(SRS_INVALID_SOCKET));
}

VOID TEST(PlatformTest, SocketReuseaddrSetsTheOption)
{
    srs_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, fd);

    EXPECT_EQ(0, srs_socket_reuseaddr(fd));

    int got = 0;
    socklen_t size = sizeof(got);
    EXPECT_EQ(0, srs_getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &got, &size));
#ifdef _WIN32
    // A no-op on Windows: there SO_REUSEADDR would let a second socket take a port that is listening.
    EXPECT_EQ(0, got);
#else
    EXPECT_NE(0, got);
#endif

    srs_close_socket(fd);
}

// Two listeners that both set the option never share a port, as on Unix.
VOID TEST(PlatformTest, SocketReuseaddrKeepsAListeningPortTaken)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_STREAM, &port));
    sockaddr_in addr = platform_loopback(port);

    srs_socket_t first = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, first);
    EXPECT_EQ(0, srs_socket_reuseaddr(first));
    ASSERT_EQ(0, srs_bind(first, (sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(0, ::listen(first, 1));

    srs_socket_t second = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, second);
    EXPECT_EQ(0, srs_socket_reuseaddr(second));
    errno = 0;
    EXPECT_EQ(-1, srs_bind(second, (sockaddr *)&addr, sizeof(addr)));
    EXPECT_EQ(EADDRINUSE, errno);

    srs_close_socket(second);
    srs_close_socket(first);
}

#ifndef _WIN32 // POSIX only: SO_REUSEPORT
VOID TEST(PlatformTest, SocketReuseportSetsTheOption)
{
    srs_socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(SRS_INVALID_SOCKET, fd);

    EXPECT_EQ(0, srs_socket_reuseport(fd));

    int got = 0;
    socklen_t size = sizeof(got);
    EXPECT_EQ(0, srs_getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &got, &size));
    EXPECT_NE(0, got);

    srs_close_socket(fd);
}
#endif

#ifndef _WIN32 // POSIX only: compares with sysconf
VOID TEST(PlatformTest, IovMaxIsTheSystemLimit)
{
    EXPECT_EQ((int)::sysconf(_SC_IOV_MAX), srs_iov_max());
    EXPECT_GE(srs_iov_max(), 16);
}
#endif

VOID TEST(PlatformTest, GetifaddrsListsTheLoopback)
{
    struct ifaddrs *ifap = NULL;
    ASSERT_EQ(0, srs_getifaddrs(&ifap));
    ASSERT_TRUE(ifap != NULL);

    bool found = false;
    for (struct ifaddrs *p = ifap; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        const sockaddr_in *addr = (const sockaddr_in *)p->ifa_addr;
        if (addr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
            continue;
        }

        found = true;
        EXPECT_TRUE(p->ifa_name != NULL && p->ifa_name[0] != 0);
        EXPECT_NE(0u, p->ifa_flags & IFF_LOOPBACK);
        EXPECT_NE(0u, p->ifa_flags & IFF_UP);
    }
    EXPECT_TRUE(found);

    srs_freeifaddrs(ifap);
}

#ifndef _WIN32 // POSIX only: compares with uname
VOID TEST(PlatformTest, UnameNamesTheSystem)
{
    struct utsname name;
    memset(&name, 0, sizeof(name));
    ASSERT_EQ(0, srs_uname(&name));

    struct utsname expect;
    ASSERT_EQ(0, ::uname(&expect));
    EXPECT_STREQ(expect.sysname, name.sysname);
    EXPECT_STREQ(expect.nodename, name.nodename);
    EXPECT_STREQ(expect.release, name.release);
    EXPECT_STREQ(expect.machine, name.machine);
}
#endif

#ifndef _WIN32 // POSIX only: compares with getrusage
VOID TEST(PlatformTest, GetrusageGivesTheMaxRss)
{
    struct rusage usage;
    memset(&usage, 0, sizeof(usage));
    ASSERT_EQ(0, srs_getrusage(&usage));
    EXPECT_GT(usage.ru_maxrss, 0);

    // The peak never drops, so a later read is at least as large.
    struct rusage expect;
    ASSERT_EQ(0, ::getrusage(RUSAGE_SELF, &expect));
    EXPECT_LE(usage.ru_maxrss, expect.ru_maxrss);
}
#endif

#ifndef _WIN32 // POSIX only: compares with sysconf
VOID TEST(PlatformTest, FdLimitIsTheSystemLimit)
{
    EXPECT_GT(srs_fd_limit(), 0);
    EXPECT_EQ((int)::sysconf(_SC_OPEN_MAX), srs_fd_limit());
}
#endif

#ifndef _WIN32 // POSIX only: compares with sysconf
VOID TEST(PlatformTest, CpuCountIsTheSystemCount)
{
    EXPECT_GT(srs_cpu_count(false), 0);
    EXPECT_GT(srs_cpu_count(true), 0);
    EXPECT_EQ((int)::sysconf(_SC_NPROCESSORS_CONF), srs_cpu_count(false));
    EXPECT_EQ((int)::sysconf(_SC_NPROCESSORS_ONLN), srs_cpu_count(true));
}
#endif

#ifndef _WIN32 // POSIX only: compares with sysconf
VOID TEST(PlatformTest, ClockTicksIsUserHz)
{
    EXPECT_GT(srs_clock_ticks(), 0);
    EXPECT_EQ((int)::sysconf(_SC_CLK_TCK), srs_clock_ticks());
}
#endif

#ifndef _WIN32 // POSIX only: fork
VOID TEST(PlatformTest, KillThenWaitpidReapsTheChild)
{
    pid_t pid = ::fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        ::sleep(10);
        ::_exit(0);
    }

    int status = 0;
    EXPECT_EQ(0, srs_waitpid(pid, &status, WNOHANG));

    EXPECT_EQ(0, srs_kill(pid, SIGKILL));
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(SIGKILL, WTERMSIG(status));
}
#endif

VOID TEST(PlatformTest, WaitpidOnANonChildFails)
{
    int status = 0;
    EXPECT_EQ(-1, srs_waitpid(::getpid(), &status, WNOHANG));
    EXPECT_EQ(ECHILD, errno);
}

#ifndef _WIN32 // POSIX only: fork
// The exit code of a child that tries to lock a file another process holds.
static int platform_child_lock(const string &path)
{
    int fd = srs_file_open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        return 1;
    }
    if (srs_file_lock(fd) == 0) {
        return 2;
    }
    return (errno == EAGAIN || errno == EACCES) ? 0 : 3;
}

VOID TEST(PlatformTest, FileLockFailsInAnotherProcess)
{
    string path = platform_tmp_file("file-lock");
    ::unlink(path.c_str());

    int fd = srs_file_open(path.c_str(), O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(0, srs_file_lock(fd));

    pid_t pid = srs_fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        ::_exit(platform_child_lock(path));
    }

    int status = -1;
    ASSERT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));

    EXPECT_EQ(0, srs_file_close(fd));
    ::unlink(path.c_str());
}
#endif

VOID TEST(PlatformTest, FileLockKeepsTheFileReadable)
{
    string path = platform_tmp_file("file-lock-read");
    ::unlink(path.c_str());

    int fd = srs_file_open(path.c_str(), O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(0, srs_file_lock(fd));
    ASSERT_EQ(5, srs_file_write(fd, "12345", 5));

    EXPECT_EQ("12345", platform_read_file(path));

    EXPECT_EQ(0, srs_file_close(fd));
    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FileLockOnInvalidFdFails)
{
    EXPECT_EQ(-1, srs_file_lock(-1));
}

VOID TEST(PlatformTest, FileTruncateShrinksTheFile)
{
    string path = platform_tmp_file("file-truncate");
    platform_write_file(path, "123456789");

    int fd = srs_file_open(path.c_str(), O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(0, srs_file_truncate(fd, 3));
    EXPECT_EQ("123", platform_read_file(path));
    EXPECT_EQ(0, srs_file_truncate(fd, 0));
    EXPECT_EQ("", platform_read_file(path));

    EXPECT_EQ(0, srs_file_close(fd));
    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FileTruncateOnInvalidFdFails)
{
    EXPECT_EQ(-1, srs_file_truncate(-1, 0));
}

VOID TEST(PlatformTest, FileCloexecSetsTheFlag)
{
    string path = platform_tmp_file("file-cloexec");
    int fd = srs_file_open(path.c_str(), O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0);
    EXPECT_FALSE(srs_utest_fd_cloexec(fd));

    EXPECT_EQ(0, srs_file_cloexec(fd));
    EXPECT_TRUE(srs_utest_fd_cloexec(fd));

    EXPECT_EQ(0, srs_file_close(fd));
    ::unlink(path.c_str());
}

VOID TEST(PlatformTest, FileCloexecOnInvalidFdFails)
{
    EXPECT_EQ(-1, srs_file_cloexec(-1));
}

#ifndef _WIN32 // POSIX only: compares with getppid
VOID TEST(PlatformTest, GetppidIsTheParent)
{
    EXPECT_GT(srs_getppid(), 0);
    EXPECT_EQ(::getppid(), srs_getppid());
}
#endif

#ifndef _WIN32 // POSIX only: fork
VOID TEST(PlatformTest, ForkStartsAChild)
{
    pid_t parent = ::getpid();
    pid_t pid = srs_fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        ::_exit(srs_getppid() == parent ? 7 : 1);
    }

    int status = -1;
    ASSERT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(7, WEXITSTATUS(status));
}
#endif

VOID TEST(PlatformTest, DaemonIsSupported)
{
#ifdef _WIN32
    EXPECT_FALSE(srs_daemon_supported());
#else
    EXPECT_TRUE(srs_daemon_supported());
#endif
}

VOID TEST(PlatformTest, SocketStartupSucceeds)
{
    EXPECT_EQ(0, srs_socket_startup());
    EXPECT_EQ(0, srs_socket_startup());
}

VOID TEST(PlatformTest, SelfPipeWakesTheReader)
{
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, srs_self_pipe_open(fds));
    EXPECT_GE(fds[0], 0);
    EXPECT_GE(fds[1], 0);

    srs_netfd_t reader = srs_netfd_open(fds[0]);
    ASSERT_TRUE(reader != NULL);

    int signo = SIGTERM;
    EXPECT_EQ((ssize_t)sizeof(int), srs_self_pipe_write(fds[1], &signo, sizeof(int)));

    int got = 0;
    EXPECT_EQ((ssize_t)sizeof(int), srs_read(reader, &got, sizeof(int), 100 * SRS_UTIME_MILLISECONDS));
    EXPECT_EQ(SIGTERM, got);

    srs_close_stfd(reader);
    EXPECT_EQ(0, srs_self_pipe_close(fds[1]));
}

VOID TEST(PlatformTest, SelfPipeCloseOnInvalidFdFails)
{
    EXPECT_EQ(-1, srs_self_pipe_close(-1));
}

static int platform_caught_signo = 0;

static void platform_catch_signal(int signo)
{
    platform_caught_signo = signo;
}

#ifndef _WIN32 // POSIX only: signals
VOID TEST(PlatformTest, SignalInstallCallsTheHandler)
{
    platform_caught_signo = 0;
    ASSERT_EQ(0, srs_signal_install(SIGUSR2, platform_catch_signal));

    EXPECT_EQ(0, ::raise(SIGUSR2));
    EXPECT_EQ(SIGUSR2, platform_caught_signo);

    EXPECT_EQ(0, srs_signal_install(SIGUSR2, SIG_DFL));
}
#endif

VOID TEST(PlatformTest, SignalInstallInvalidSignalFails)
{
    EXPECT_EQ(-1, srs_signal_install(-1, platform_catch_signal));
}

#ifndef _WIN32 // POSIX only: children run /bin/sh and /bin/sleep
// Spawn /bin/sh with the script and args, wait for it, and return its exit code, or -1 when it did not start.
static int platform_spawn_sh(const char *script, const vector<string> &args, const string &out, const string &err)
{
    vector<const char *> argv;
    argv.push_back("/bin/sh");
    argv.push_back("-c");
    argv.push_back(script);
    argv.push_back("sh");
    for (int i = 0; i < (int)args.size(); i++) {
        argv.push_back(args[i].c_str());
    }
    argv.push_back(NULL);

    pid_t pid = srs_process_spawn("/bin/sh", &argv[0], out.c_str(), err.c_str(), "cid", "info\n");
    if (pid < 0) {
        return -1;
    }

    int status = -1;
    if (srs_waitpid(pid, &status, 0) != pid) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

VOID TEST(PlatformTest, ProcessSpawnRedirectsTheOutput)
{
    string out = platform_tmp_file("spawn-out");
    string err = platform_tmp_file("spawn-err");
    ::unlink(out.c_str());
    ::unlink(err.c_str());

    EXPECT_EQ(3, platform_spawn_sh("echo out; echo err >&2; exit 3", vector<string>(), out, err));

    string got = platform_read_file(out);
    EXPECT_TRUE(srs_strings_ends_with(got, "out\n")) << got;
    EXPECT_EQ("err\n", platform_read_file(err));

    ::unlink(out.c_str());
    ::unlink(err.c_str());
}

VOID TEST(PlatformTest, ProcessSpawnPassesTheArgsAsIs)
{
    string out = platform_tmp_file("spawn-args");
    ::unlink(out.c_str());

    vector<string> args;
    args.push_back("a b");
    args.push_back("c\"d");
    args.push_back("e\\");
    args.push_back("");
    args.push_back("f\\\\\"g h");
    EXPECT_EQ(0, platform_spawn_sh("printf '[%s]' \"$@\" >&2", args, "", out));

    EXPECT_EQ("[a b][c\"d][e\\][][f\\\\\"g h]", platform_read_file(out));

    ::unlink(out.c_str());
}

VOID TEST(PlatformTest, ProcessSpawnAppendsToTheFile)
{
    string out = platform_tmp_file("spawn-append");
    platform_write_file(out, "old\n");

    EXPECT_EQ(0, platform_spawn_sh("echo new >&2", vector<string>(), "", out));

    EXPECT_EQ("old\nnew\n", platform_read_file(out));

    ::unlink(out.c_str());
}

VOID TEST(PlatformTest, ProcessSpawnSharesOneFileForBothOutputs)
{
    string out = platform_tmp_file("spawn-both");
    ::unlink(out.c_str());

    EXPECT_EQ(0, platform_spawn_sh("echo out; echo err >&2", vector<string>(), out, out));

    string got = platform_read_file(out);
    EXPECT_TRUE(srs_strings_ends_with(got, "out\nerr\n")) << got;

    ::unlink(out.c_str());
}

VOID TEST(PlatformTest, ProcessSpawnHasNoStdin)
{
    string out = platform_tmp_file("spawn-stdin");
    ::unlink(out.c_str());

    // cat ends at once on the null device, where a terminal or a pipe from SRS would block it.
    EXPECT_EQ(0, platform_spawn_sh("cat >&2; echo done >&2", vector<string>(), "", out));

    EXPECT_EQ("done\n", platform_read_file(out));

    ::unlink(out.c_str());
}

VOID TEST(PlatformTest, ProcessSpawnToTheNullDevice)
{
    EXPECT_EQ(0, platform_spawn_sh("echo out; echo err >&2", vector<string>(), "/dev/null", "/dev/null"));
}

VOID TEST(PlatformTest, ProcessSpawnIsReapedOnce)
{
    vector<const char *> argv;
    argv.push_back("/bin/sh");
    argv.push_back("-c");
    argv.push_back("exit 0");
    argv.push_back(NULL);

    pid_t pid = srs_process_spawn("/bin/sh", &argv[0], "", "", "cid", "");
    ASSERT_GT(pid, 0);

    int status = -1;
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_EQ(-1, srs_waitpid(pid, &status, WNOHANG));
    EXPECT_EQ(ECHILD, errno);
}

VOID TEST(PlatformTest, ProcessKillEndsAChildThatIgnoresTerm)
{
    vector<const char *> argv;
    argv.push_back("/bin/sleep");
    argv.push_back("10");
    argv.push_back(NULL);

    pid_t pid = srs_process_spawn("/bin/sleep", &argv[0], "", "", "cid", "");
    ASSERT_GT(pid, 0);

    int status = -1;
    EXPECT_EQ(0, srs_waitpid(pid, &status, WNOHANG));

    EXPECT_EQ(0, srs_kill(pid, SIGKILL));
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));
}

// Wait up to a second for the process to quit, which cycle sees.
static void platform_cycle_until_quit(SrsProcess *process)
{
    for (int i = 0; i < 100 && process->started(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        srs_error_t err = process->cycle();
        srs_freep(err);
    }
}

VOID TEST(SrsProcessTest, StartRedirectsTheOutput)
{
    srs_error_t err;

    string out = platform_tmp_file("process-out");
    string errf = platform_tmp_file("process-err");
    ::unlink(out.c_str());
    ::unlink(errf.c_str());

    vector<string> argv;
    argv.push_back("/bin/sh");
    argv.push_back("-c");
    argv.push_back("echo out; echo err >&2");
    argv.push_back("1>" + out);
    argv.push_back("2>" + errf);

    SrsUniquePtr<SrsProcess> process(new SrsProcess());
    HELPER_ASSERT_SUCCESS(process->initialize("/bin/sh", argv));
    HELPER_ASSERT_SUCCESS(process->start());
    EXPECT_TRUE(process->started());
    EXPECT_GT(process->get_pid(), 0);

    platform_cycle_until_quit(process.get());
    EXPECT_FALSE(process->started());

    string got = platform_read_file(out);
    EXPECT_TRUE(srs_strings_ends_with(got, "out\n")) << got;
    EXPECT_EQ("err\n", platform_read_file(errf));

    ::unlink(out.c_str());
    ::unlink(errf.c_str());
}

VOID TEST(SrsProcessTest, StartTwiceKeepsTheChild)
{
    srs_error_t err;

    vector<string> argv;
    argv.push_back("/bin/sleep");
    argv.push_back("10");

    SrsUniquePtr<SrsProcess> process(new SrsProcess());
    HELPER_ASSERT_SUCCESS(process->initialize("/bin/sleep", argv));
    HELPER_ASSERT_SUCCESS(process->start());
    int pid = process->get_pid();

    HELPER_EXPECT_SUCCESS(process->start());
    EXPECT_EQ(pid, process->get_pid());

    process->stop();
    EXPECT_FALSE(process->started());
}

VOID TEST(SrsProcessTest, StopEndsAChildThatIgnoresTerm)
{
    srs_error_t err;

    vector<string> argv;
    argv.push_back("/bin/sleep");
    argv.push_back("10");

    SrsUniquePtr<SrsProcess> process(new SrsProcess());
    HELPER_ASSERT_SUCCESS(process->initialize("/bin/sleep", argv));
    HELPER_ASSERT_SUCCESS(process->start());
    pid_t pid = process->get_pid();

    HELPER_EXPECT_SUCCESS(process->cycle());
    EXPECT_TRUE(process->started());

    // The child ignores SIGTERM, so stop ends it with SIGKILL after the grace period, and reaps it.
    process->stop();
    EXPECT_FALSE(process->started());

    int status = -1;
    EXPECT_EQ(-1, srs_waitpid(pid, &status, WNOHANG));
    EXPECT_EQ(ECHILD, errno);
}

VOID TEST(SrsProcessTest, CycleSeesTheChildQuit)
{
    srs_error_t err;

    vector<string> argv;
    argv.push_back("/bin/sh");
    argv.push_back("-c");
    argv.push_back("exit 3");

    SrsUniquePtr<SrsProcess> process(new SrsProcess());
    HELPER_ASSERT_SUCCESS(process->initialize("/bin/sh", argv));
    HELPER_ASSERT_SUCCESS(process->start());

    platform_cycle_until_quit(process.get());
    EXPECT_FALSE(process->started());

    // Restart, as the ingesters do once the child quit.
    HELPER_ASSERT_SUCCESS(process->start());
    EXPECT_TRUE(process->started());
    platform_cycle_until_quit(process.get());
    EXPECT_FALSE(process->started());
}
#endif
