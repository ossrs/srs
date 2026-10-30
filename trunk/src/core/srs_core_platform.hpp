//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_PLATFORM_HPP
#define SRS_CORE_PLATFORM_HPP

// The platform layer: the system headers, types and calls SRS uses, so the rest of SRS builds on Linux,
// macOS and Windows without an #ifdef. The file has three parts:
//   1. Common: the headers and macros of every platform.
//   2. Platform: the system headers of each platform, and on Windows the POSIX types and macros it lacks.
//   3. Calls: the srs_ functions, the same on every platform. On Linux and macOS each is the POSIX call;
//      on Windows it is built from Win32 and the CRT. The comments say where Windows differs.

///////////////////////////////////////////////////////////
// 1. Common
///////////////////////////////////////////////////////////

// For 32bit os, 2G big file limit for unistd io,
// ie. read/write/lseek to use 64bits size for huge file.
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

// For int64_t print using PRId64 format.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

// For RTC/FFMPEG build.
#if !defined(__STDC_CONSTANT_MACROS)
#define __STDC_CONSTANT_MACROS
#endif

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#include <inttypes.h>

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>

// For CentOS 6 or C++98, @see https://github.com/ossrs/srs/issues/2815
#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif

///////////////////////////////////////////////////////////
// 2. Platform: Linux and macOS
///////////////////////////////////////////////////////////
#ifndef _WIN32

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

typedef int srs_socket_t;
#define SRS_INVALID_SOCKET (-1)

// A 64-bit file offset, with _FILE_OFFSET_BITS.
typedef off_t srs_off_t;

#endif

///////////////////////////////////////////////////////////
// 2. Platform: Windows
///////////////////////////////////////////////////////////
#ifdef _WIN32

// Before windows.h, which includes the old winsock.h that conflicts with it, as ST's public.h requires.
#include <winsock2.h>
// ST's public.h defines the POSIX types Windows lacks, such as ssize_t and struct iovec.
#include <st.h>
// The MSVC headers for the POSIX names of unistd.h, such as getpid, unlink and getcwd.
#include <direct.h>
#include <io.h>
#include <process.h>
#include <string.h>
#include <sys/stat.h>

// SRS has its own error code by this name, and uses no Win32 one.
#undef ERROR_NOT_SUPPORTED

// Sockets: a Winsock SOCKET is 64-bit and not a file descriptor.
typedef SOCKET srs_socket_t;
#define SRS_INVALID_SOCKET INVALID_SOCKET

// The shutdown directions by their POSIX names.
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

// Files: a 64-bit file offset, since off_t is a 32-bit long on Windows.
typedef int64_t srs_off_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// The mode of access that checks only that the file exists.
#define F_OK 0

// Strings: the case-insensitive compare by its POSIX name.
#define strcasecmp _stricmp

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

// MSVC knows only read and write for the owner; srs_file_open drops the rest.
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IXUSR 0
#define S_IRGRP 0
#define S_IWGRP 0
#define S_IXGRP 0
#define S_IROTH 0
#define S_IWOTH 0
#define S_IXOTH 0

// Processes: the status of srs_waitpid is the exit code of the child on Windows.
typedef int pid_t;
#define WNOHANG 1
#define WIFEXITED(status) 1
#define WEXITSTATUS(status) (status)

// Signals: the ones SRS names that MSVC does not define, with their Linux numbers. Windows cannot send
// them, but the HTTP raw API passes them to the signal handler.
#define SIGHUP 1
#define SIGQUIT 3
#define SIGKILL 9
#define SIGUSR1 10
#define SIGUSR2 12

// Time: MSVC has no struct timezone; srs_gettimeofday ignores it, as Linux does.
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

// The interface list of srs_getifaddrs, with the fields SRS reads. ws2ipdef.h already defines IFF_UP and
// IFF_LOOPBACK, with other values than Linux.
struct ifaddrs {
    struct ifaddrs *ifa_next;
    char *ifa_name;
    unsigned int ifa_flags;
    struct sockaddr *ifa_addr;
};
#ifndef IFF_POINTOPOINT
#define IFF_POINTOPOINT IFF_POINTTOPOINT
#endif
#ifndef IFF_RUNNING
#define IFF_RUNNING 0x40
#endif
#ifndef IFF_PROMISC
#define IFF_PROMISC 0x100
#endif

// The system names of srs_uname.
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

// The fields of the Linux rusage, with ru_maxrss in KB; srs_getrusage leaves the counters Windows lacks at 0.
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

#endif

///////////////////////////////////////////////////////////
// 3. Calls: sockets
///////////////////////////////////////////////////////////

// Start the socket library before the first socket call. Winsock needs WSAStartup, which ST calls
// only in st_init, after the config check has resolved the local IPs.
extern int srs_socket_startup();
// Close a socket that ST does not own yet; a file or pipe is not a socket on Windows.
extern int srs_close_socket(srs_socket_t fd);
// The option value is a const char* on Windows.
extern int srs_setsockopt(srs_socket_t fd, int level, int name, const void *value, int size);
// The option value is a char* on Windows, and a one-byte value is widened to the int POSIX gives.
extern int srs_getsockopt(srs_socket_t fd, int level, int name, void *value, socklen_t *size);
// Winsock does not set errno, so a failed bind sets it here, such as EADDRINUSE.
extern int srs_bind(srs_socket_t fd, const sockaddr *addr, socklen_t size);
// Make a connect to a closed port on this host fail at once. Windows retries a refused connect for
// about 2 s, so a down hook or origin on the same host would hold the client that long.
extern int srs_socket_refuse_fast(srs_socket_t fd, const sockaddr *addr);
// Keep a UDP socket receiving after an ICMP port unreachable from a peer that went away. Windows fails the
// next receive with it even on an unconnected socket, which serves many peers; Unix only on a connected one.
extern int srs_socket_ignore_unreachable(srs_socket_t fd);
// Keep a child process from inheriting the socket.
extern int srs_socket_cloexec(srs_socket_t fd);
// Let a listener bind its port again while old connections are in TIME_WAIT.
extern int srs_socket_reuseaddr(srs_socket_t fd);
// Let several processes listen on one port.
extern int srs_socket_reuseport(srs_socket_t fd);
// The most iovecs one writev takes.
extern int srs_iov_max();

///////////////////////////////////////////////////////////
// 3. Calls: system and resources
///////////////////////////////////////////////////////////

// The local interface addresses; free them with srs_freeifaddrs.
extern int srs_getifaddrs(struct ifaddrs **ifap);
extern void srs_freeifaddrs(struct ifaddrs *ifa);
extern int srs_uname(struct utsname *name);

// The resource usage of this process.
extern int srs_getrusage(struct rusage *usage);
// The most files and sockets this process may open.
extern int srs_fd_limit();
// The number of CPUs, configured or online.
extern int srs_cpu_count(bool online);
// The clock ticks per second of the /proc CPU times.
extern int srs_clock_ticks();

///////////////////////////////////////////////////////////
// 3. Calls: processes
///////////////////////////////////////////////////////////

// Send a signal to a child process, and reap it.
extern int srs_kill(pid_t pid, int signo);
extern pid_t srs_waitpid(pid_t pid, int *status, int options);
// The parent of this process.
extern pid_t srs_getppid();
// Start a child process; Windows has no fork.
extern pid_t srs_fork();
// Whether SRS can run as a daemon, which forks.
extern bool srs_daemon_supported();

// Start bin as a child process with argv, ended by a NULL, which waits for it with srs_waitpid.
// - Its stdout and stderr append to the files, or go to SRS's own when a file is empty; its stdin is the
//   null device.
// - Before the child runs, its stdout gets a banner with the cid, the pids and the info text.
// - It ignores the Ctrl+C and SIGTERM sent to SRS.
// - Windows has no fork: there the child is a process group of its own, which srs_kill sends SIGTERM to
//   as Ctrl+Break, in a job that ends it when SRS quits.
extern pid_t srs_process_spawn(const char *bin, const char *const argv[], const char *stdout_file,
                               const char *stderr_file, const char *cid, const char *info);
#ifndef _WIN32
// Open the file to append, creating it, and make to_fd a copy of it; the spawned child uses it for its stdout,
// stderr and stdin. Does nothing for a NULL or empty file. Returns -1 when the open or the dup2 fails.
extern int srs_redirect_output(const char *file, int to_fd);
#endif

///////////////////////////////////////////////////////////
// 3. Calls: signals
///////////////////////////////////////////////////////////

// A self-pipe that wakes a coroutine from a signal handler: fds[0] is the end ST reads, fds[1] the end to
// write. ST polls only sockets on Windows, so there it is a pair of connected loopback TCP sockets.
extern int srs_self_pipe_open(int fds[2]);
// Write to the self-pipe, from a signal handler on Unix, or from the console control thread on Windows.
extern ssize_t srs_self_pipe_write(int fd, const void *buf, size_t count);
extern int srs_self_pipe_close(int fd);

// Install the handler of a signal. Windows has console control events instead:
// - Ctrl+C is SIGINT.
// - Ctrl+Break, closing the console and the system shutdown are SIGTERM.
// - Any other signal fails with ENOSYS.
extern int srs_signal_install(int signo, void (*handler)(int));

///////////////////////////////////////////////////////////
// 3. Calls: time and random
///////////////////////////////////////////////////////////

extern int srs_gettimeofday(struct timeval *tv, struct timezone *tz);
extern struct tm *srs_localtime_r(const time_t *t, struct tm *result);
extern struct tm *srs_gmtime_r(const time_t *t, struct tm *result);

// Windows has no srandom and random, and its rand gives only 15 bits.
extern void srs_srandom(unsigned int seed);
extern long srs_random();
// Switch srs_random to a sequence of its own, kept in state, as initstate and setstate do; both return
// the state they replace. Windows has neither.
extern char *srs_initstate(unsigned int seed, char *state, size_t size);
extern char *srs_setstate(char *state);

///////////////////////////////////////////////////////////
// 3. Calls: files
///////////////////////////////////////////////////////////

// File IO by descriptor, in binary mode on Windows.
extern int srs_file_open(const char *path, int oflag, ...);
extern ssize_t srs_file_read(int fd, void *buf, size_t count);
extern ssize_t srs_file_write(int fd, const void *buf, size_t count);
extern srs_off_t srs_file_lseek(int fd, srs_off_t offset, int whence);
extern int srs_file_close(int fd);
// Open a file that another process may rename or remove while it is open, as on Unix, such as a log that a
// rotation tool moves away; Windows refuses that for a file the CRT opens.
extern int srs_file_open_renamable(const char *path, int oflag, int mode);

// The fseek and ftell of a FILE, with 64-bit offsets.
extern int srs_fseek(FILE *stream, srs_off_t offset, int whence);
extern srs_off_t srs_ftell(FILE *stream);

// Create a directory with mode 0775; Windows has no mode.
extern int srs_mkdir(const char *path);
// Rename a file, replacing the target, which Windows rename does not do.
extern int srs_rename(const char *from, const char *to);

// Lock a whole file for writing without waiting; it fails with EAGAIN or EACCES while another process holds
// the lock, which is released when the file is closed.
extern int srs_file_lock(int fd);
extern int srs_file_truncate(int fd, srs_off_t size);
// Keep a child process from inheriting the file.
extern int srs_file_cloexec(int fd);

#endif
