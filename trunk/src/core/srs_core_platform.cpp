//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_core_platform.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#ifdef _WIN32
#include <direct.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
// After windows.h, for the IP_ADAPTER_ADDRESSES of GetAdaptersAddresses.
#include <iphlpapi.h>
#include <mstcpip.h>
#include <mswsock.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <map>
#include <string>
#include <vector>
#else
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32

int srs_socket_startup()
{
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? 0 : -1;
}

int srs_close_socket(srs_socket_t fd)
{
    return ::closesocket(fd) == 0 ? 0 : -1;
}

int srs_setsockopt(srs_socket_t fd, int level, int name, const void *value, int size)
{
    return ::setsockopt(fd, level, name, (const char *)value, size) == 0 ? 0 : -1;
}

int srs_getsockopt(srs_socket_t fd, int level, int name, void *value, socklen_t *size)
{
    socklen_t asked = *size;
    if (::getsockopt(fd, level, name, (char *)value, size) != 0) {
        return -1;
    }

    // Winsock gives some BOOL options, such as TCP_NODELAY, as one byte, and SRS passes the size it gets
    // back to setsockopt.
    if (asked == (socklen_t)sizeof(int) && *size == 1) {
        int v = *(unsigned char *)value;
        memcpy(value, &v, sizeof(int));
        *size = sizeof(int);
    }
    return 0;
}

int srs_bind(srs_socket_t fd, const sockaddr *addr, socklen_t size)
{
    if (::bind(fd, addr, size) == 0) {
        return 0;
    }

    int r0 = ::WSAGetLastError();
    if (r0 == WSAEADDRINUSE) {
        errno = EADDRINUSE;
    } else if (r0 == WSAEADDRNOTAVAIL) {
        errno = EADDRNOTAVAIL;
    } else if (r0 == WSAEACCES) {
        errno = EACCES;
    } else if (r0 == WSAEAFNOSUPPORT) {
        errno = EAFNOSUPPORT;
    } else if (r0 == WSAENOTSOCK) {
        errno = ENOTSOCK;
    } else {
        errno = EINVAL;
    }
    return -1;
}

// Windows answers a refused SYN by sending it again, for about 2 s. A SYN to this host is never lost, so
// it is sent only once there; a remote host keeps the retries a lossy network needs.
int srs_socket_refuse_fast(srs_socket_t fd, const sockaddr *addr)
{
    bool loopback = false;
    if (addr->sa_family == AF_INET) {
        loopback = (ntohl(((const sockaddr_in *)addr)->sin_addr.s_addr) >> 24) == 127;
    } else if (addr->sa_family == AF_INET6) {
        loopback = IN6_IS_ADDR_LOOPBACK(&((const sockaddr_in6 *)addr)->sin6_addr) != 0;
    }
    if (!loopback) {
        return 0;
    }

    TCP_INITIAL_RTO_PARAMETERS params;
    params.Rtt = TCP_INITIAL_RTO_UNSPECIFIED_RTT;
    params.MaxSynRetransmissions = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;
    DWORD size = 0;
    if (::WSAIoctl(fd, SIO_TCP_INITIAL_RTO, &params, sizeof(params), NULL, 0, &size, NULL, NULL) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int srs_socket_ignore_unreachable(srs_socket_t fd)
{
    BOOL report = FALSE;
    DWORD size = 0;
    if (::WSAIoctl(fd, SIO_UDP_CONNRESET, &report, sizeof(report), NULL, 0, &size, NULL, NULL) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int srs_socket_cloexec(srs_socket_t fd)
{
    return ::SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0) ? 0 : -1;
}

// Windows lets a listener bind a port with connections in TIME_WAIT anyway, and SO_REUSEADDR there would
// let another socket take a port that is already listening.
int srs_socket_reuseaddr(srs_socket_t /*fd*/)
{
    return 0;
}

// Windows has no SO_REUSEPORT, so a second process cannot listen on the same port.
int srs_socket_reuseport(srs_socket_t /*fd*/)
{
    return 0;
}

// ST's writev on Windows has no iovec limit; this is the Linux IOV_MAX, so writes are split the same way.
int srs_iov_max()
{
    return 1024;
}

// One allocation per address, so srs_freeifaddrs frees each node at once.
struct SrsIfaddrsNode {
    struct ifaddrs ifa;
    struct sockaddr_storage addr;
    char name[256];
};

int srs_getifaddrs(struct ifaddrs **ifap)
{
    *ifap = NULL;

    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *adapters = NULL;
    ULONG r0 = ERROR_BUFFER_OVERFLOW;
    for (int i = 0; i < 3 && r0 == ERROR_BUFFER_OVERFLOW; i++) {
        ::free(adapters);
        if ((adapters = (IP_ADAPTER_ADDRESSES *)::malloc(size)) == NULL) {
            errno = ENOMEM;
            return -1;
        }
        r0 = ::GetAdaptersAddresses(AF_UNSPEC, flags, NULL, adapters, &size);
    }
    if (r0 != ERROR_SUCCESS && r0 != ERROR_NO_DATA) {
        ::free(adapters);
        errno = (r0 == ERROR_NOT_ENOUGH_MEMORY) ? ENOMEM : EIO;
        return -1;
    }

    struct ifaddrs **tail = ifap;
    for (IP_ADAPTER_ADDRESSES *adapter = (r0 == ERROR_SUCCESS) ? adapters : NULL; adapter; adapter = adapter->Next) {
        unsigned int ifa_flags = 0;
        if (adapter->OperStatus == IfOperStatusUp) {
            ifa_flags |= IFF_UP | IFF_RUNNING;
        }
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            ifa_flags |= IFF_LOOPBACK;
        }
        // Tunnels such as Teredo and PPP links are the tun and ppp devices of Linux.
        if (adapter->IfType == IF_TYPE_TUNNEL || adapter->IfType == IF_TYPE_PPP) {
            ifa_flags |= IFF_POINTOPOINT;
        }

        for (IP_ADAPTER_UNICAST_ADDRESS *ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
            const SOCKET_ADDRESS &sa = ua->Address;
            if (!sa.lpSockaddr || sa.iSockaddrLength <= 0 || sa.iSockaddrLength > (int)sizeof(sockaddr_storage)) {
                continue;
            }

            SrsIfaddrsNode *node = (SrsIfaddrsNode *)::calloc(1, sizeof(SrsIfaddrsNode));
            if (!node) {
                srs_freeifaddrs(*ifap);
                *ifap = NULL;
                ::free(adapters);
                errno = ENOMEM;
                return -1;
            }

            memcpy(&node->addr, sa.lpSockaddr, sa.iSockaddrLength);
            // The friendly name, such as Ethernet, in UTF-8; the AdapterName is a GUID.
            if (!::WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, node->name, sizeof(node->name), NULL, NULL)) {
                snprintf(node->name, sizeof(node->name), "if%lu", (unsigned long)adapter->IfIndex);
            }

            node->ifa.ifa_name = node->name;
            node->ifa.ifa_flags = ifa_flags;
            node->ifa.ifa_addr = (struct sockaddr *)&node->addr;
            *tail = &node->ifa;
            tail = &node->ifa.ifa_next;
        }
    }

    ::free(adapters);
    return 0;
}

void srs_freeifaddrs(struct ifaddrs *ifa)
{
    while (ifa) {
        struct ifaddrs *next = ifa->ifa_next;
        // The ifaddrs is the first member of its node.
        ::free(ifa);
        ifa = next;
    }
}

int srs_uname(struct utsname *name)
{
    memset(name, 0, sizeof(*name));
    snprintf(name->sysname, sizeof(name->sysname), "Windows");
    snprintf(name->machine, sizeof(name->machine), "x86_64");

    DWORD size = sizeof(name->nodename);
    if (!::GetComputerNameExA(ComputerNameDnsHostname, name->nodename, &size)) {
        return -1;
    }

    // GetVersionEx reports Windows 8 to a program without a compatibility manifest, so ask ntdll.
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW *);
    HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
    RtlGetVersionFn get_version = ntdll ? (RtlGetVersionFn)(void *)::GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    OSVERSIONINFOW info;
    memset(&info, 0, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    if (!get_version || get_version(&info) != 0) {
        return -1;
    }

    snprintf(name->release, sizeof(name->release), "%lu", (unsigned long)info.dwMajorVersion);
    snprintf(name->version, sizeof(name->version), "%lu.%lu.%lu", (unsigned long)info.dwMajorVersion,
             (unsigned long)info.dwMinorVersion, (unsigned long)info.dwBuildNumber);
    return 0;
}

// A FILETIME duration counts 100ns.
static void srs_filetime_to_timeval(const FILETIME &ft, struct timeval *tv)
{
    uint64_t t = (((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10;
    tv->tv_sec = (long)(t / 1000000);
    tv->tv_usec = (long)(t % 1000000);
}

int srs_getrusage(struct rusage *usage)
{
    memset(usage, 0, sizeof(*usage));

    FILETIME create, exit, kernel, user;
    if (!::GetProcessTimes(::GetCurrentProcess(), &create, &exit, &kernel, &user)) {
        return -1;
    }
    srs_filetime_to_timeval(user, &usage->ru_utime);
    srs_filetime_to_timeval(kernel, &usage->ru_stime);

    PROCESS_MEMORY_COUNTERS mem;
    if (!::K32GetProcessMemoryInfo(::GetCurrentProcess(), &mem, sizeof(mem))) {
        return -1;
    }
    usage->ru_maxrss = (long)(mem.PeakWorkingSetSize / 1024);
    // Windows counts the soft and the hard page faults together, and most are soft, so all are minor here.
    usage->ru_minflt = (long)mem.PageFaultCount;
    return 0;
}

// Windows has no per-process fd limit; this is the cap of ST on Windows, whose st_getfdlimit reads it only
// after st_init, later than the config check.
int srs_fd_limit()
{
    return 1 << 24;
}

int srs_cpu_count(bool online)
{
    if (online) {
        return (int)::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    }
    return (int)::GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
}

// Windows has no /proc CPU times to scale; this is the USER_HZ of Linux.
int srs_clock_ticks()
{
    return 100;
}

// The process handles of the children srs_process_spawn started and srs_waitpid has not reaped yet. Holding
// the handle keeps Windows from giving the pid to another process, as a zombie does on Unix.
static std::map<pid_t, HANDLE> srs_children;

int srs_kill(pid_t pid, int signo)
{
    std::map<pid_t, HANDLE>::iterator it = srs_children.find(pid);
    if (it == srs_children.end()) {
        errno = ESRCH;
        return -1;
    }
    HANDLE child = it->second;

    if (signo != SIGTERM && signo != SIGKILL) {
        errno = ENOSYS;
        return -1;
    }

    // A child that has quit takes the signal and stays to be reaped, as on Unix.
    if (::WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
        return 0;
    }

    // The child leads a process group of its own, so Ctrl+Break reaches only it. It fails when SRS has no
    // console, which the child then has not either, so it is ended at once.
    if (signo == SIGTERM && ::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)pid)) {
        return 0;
    }

    if (!::TerminateProcess(child, 128 + SIGKILL) && ::WaitForSingleObject(child, 0) != WAIT_OBJECT_0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

// The status is the exit code of the child, not a Unix wait status.
pid_t srs_waitpid(pid_t pid, int *status, int options)
{
    std::map<pid_t, HANDLE>::iterator it = srs_children.find(pid);
    if (it == srs_children.end()) {
        errno = ECHILD;
        return -1;
    }
    HANDLE child = it->second;

    DWORD r0 = ::WaitForSingleObject(child, (options & WNOHANG) ? 0 : INFINITE);
    if (r0 == WAIT_TIMEOUT) {
        return 0;
    }
    if (r0 != WAIT_OBJECT_0) {
        errno = EINVAL;
        return -1;
    }

    DWORD code = 0;
    ::GetExitCodeProcess(child, &code);
    if (status) {
        *status = (int)code;
    }

    ::CloseHandle(child);
    srs_children.erase(it);
    return pid;
}

int srs_gettimeofday(struct timeval *tv, struct timezone * /*tz*/)
{
    FILETIME ft;
    ::GetSystemTimePreciseAsFileTime(&ft);

    // FILETIME counts 100ns since 1601-01-01; the Unix epoch is 11644473600s later.
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t = t / 10 - 11644473600000000ULL;

    tv->tv_sec = (long)(t / 1000000);
    tv->tv_usec = (long)(t % 1000000);
    return 0;
}

struct tm *srs_localtime_r(const time_t *t, struct tm *result)
{
    return ::localtime_s(result, t) == 0 ? result : NULL;
}

struct tm *srs_gmtime_r(const time_t *t, struct tm *result)
{
    return ::gmtime_s(result, t) == 0 ? result : NULL;
}

// A 64-bit LCG, whose high 31 bits are as wide as the values of random(). Its state is the first 8
// bytes of the current state buffer, which srs_initstate may give unaligned, so it is copied.
static uint64_t srs_random_default = 1;
static char *srs_random_state = (char *)&srs_random_default;

static uint64_t srs_random_load()
{
    uint64_t v;
    memcpy(&v, srs_random_state, sizeof(v));
    return v;
}

static void srs_random_store(uint64_t v)
{
    memcpy(srs_random_state, &v, sizeof(v));
}

void srs_srandom(unsigned int seed)
{
    srs_random_store(seed);
}

long srs_random()
{
    uint64_t v = srs_random_load() * 6364136223846793005ULL + 1442695040888963407ULL;
    srs_random_store(v);
    return (long)(v >> 33);
}

char *srs_initstate(unsigned int seed, char *state, size_t size)
{
    if (size < sizeof(uint64_t)) {
        errno = EINVAL;
        return NULL;
    }

    char *previous = srs_random_state;
    srs_random_state = state;
    srs_random_store(seed);
    return previous;
}

char *srs_setstate(char *state)
{
    char *previous = srs_random_state;
    srs_random_state = state;
    return previous;
}

int srs_file_open(const char *path, int oflag, ...)
{
    int mode = 0;
    if (oflag & _O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = va_arg(ap, int);
        va_end(ap);
    }

    // The default text mode turns CR LF into LF and stops at Ctrl+Z; MSVC knows only these mode bits.
    return ::_open(path, oflag | _O_BINARY, mode & (_S_IREAD | _S_IWRITE));
}

// The MSVC count is an unsigned int, so a larger one reads or writes part, as POSIX allows.
ssize_t srs_file_read(int fd, void *buf, size_t count)
{
    return ::_read(fd, buf, (unsigned int)(count > INT_MAX ? INT_MAX : count));
}

ssize_t srs_file_write(int fd, const void *buf, size_t count)
{
    return ::_write(fd, buf, (unsigned int)(count > INT_MAX ? INT_MAX : count));
}

srs_off_t srs_file_lseek(int fd, srs_off_t offset, int whence)
{
    return ::_lseeki64(fd, offset, whence);
}

int srs_file_close(int fd)
{
    return ::_close(fd);
}

int srs_file_open_renamable(const char *path, int oflag, int mode)
{
    int accmode = oflag & (_O_RDONLY | _O_WRONLY | _O_RDWR);
    DWORD access = SYNCHRONIZE;
    if (accmode != _O_WRONLY) {
        access |= GENERIC_READ;
    }
    // Append only, as O_APPEND: every write goes to the end, whatever the file position.
    if (accmode != _O_RDONLY) {
        access |= (oflag & _O_APPEND) ? FILE_APPEND_DATA : GENERIC_WRITE;
    }

    DWORD disposition = OPEN_EXISTING;
    if ((oflag & _O_CREAT) && (oflag & _O_EXCL)) {
        disposition = CREATE_NEW;
    } else if ((oflag & _O_CREAT) && (oflag & _O_TRUNC)) {
        disposition = CREATE_ALWAYS;
    } else if (oflag & _O_CREAT) {
        disposition = OPEN_ALWAYS;
    } else if (oflag & _O_TRUNC) {
        disposition = TRUNCATE_EXISTING;
    }
    DWORD attributes = (mode & _S_IWRITE) ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_READONLY;

    HANDLE h = ::CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                             disposition, attributes, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD r0 = ::GetLastError();
        if (r0 == ERROR_FILE_NOT_FOUND || r0 == ERROR_PATH_NOT_FOUND) {
            errno = ENOENT;
        } else if (r0 == ERROR_FILE_EXISTS) {
            errno = EEXIST;
        } else {
            errno = EACCES;
        }
        return -1;
    }

    // Without _O_TEXT the descriptor is in binary mode, as srs_file_open.
    int fd = ::_open_osfhandle((intptr_t)h, accmode == _O_RDONLY ? _O_RDONLY : 0);
    if (fd < 0) {
        ::CloseHandle(h);
    }
    return fd;
}

int srs_fseek(FILE *stream, srs_off_t offset, int whence)
{
    return ::_fseeki64(stream, offset, whence);
}

srs_off_t srs_ftell(FILE *stream)
{
    return ::_ftelli64(stream);
}

int srs_mkdir(const char *path)
{
    return ::_mkdir(path);
}

int srs_rename(const char *from, const char *to)
{
    if (!::MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
        DWORD r0 = ::GetLastError();
        errno = (r0 == ERROR_FILE_NOT_FOUND || r0 == ERROR_PATH_NOT_FOUND) ? ENOENT : EACCES;
        return -1;
    }
    return 0;
}

// The CRT calls the invalid parameter handler, which quits the process, for a negative fd.
static HANDLE srs_file_handle(int fd)
{
    if (fd < 0) {
        return INVALID_HANDLE_VALUE;
    }
    return (HANDLE)::_get_osfhandle(fd);
}

// A Windows lock is mandatory, so a lock over the content would stop other processes from reading the pid.
// Lock one byte far past the end instead, which no reader reaches.
int srs_file_lock(int fd)
{
    HANDLE h = srs_file_handle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));
    ov.OffsetHigh = 0x40000000;
    if (!::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        errno = (::GetLastError() == ERROR_LOCK_VIOLATION) ? EAGAIN : EINVAL;
        return -1;
    }
    return 0;
}

int srs_file_truncate(int fd, srs_off_t size)
{
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }

    errno_t r0 = ::_chsize_s(fd, size);
    if (r0 != 0) {
        errno = r0;
        return -1;
    }
    return 0;
}

int srs_file_cloexec(int fd)
{
    HANDLE h = srs_file_handle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    return ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0) ? 0 : -1;
}

// The parent may have quit since it started this process, and Windows may give its pid to another process.
pid_t srs_getppid()
{
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return 0;
    }

    pid_t ppid = 0;
    DWORD self = ::GetCurrentProcessId();
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    for (BOOL ok = ::Process32First(snap, &entry); ok; ok = ::Process32Next(snap, &entry)) {
        if (entry.th32ProcessID == self) {
            ppid = (pid_t)entry.th32ParentProcessID;
            break;
        }
    }

    ::CloseHandle(snap);
    return ppid;
}

pid_t srs_fork()
{
    errno = ENOSYS;
    return -1;
}

bool srs_daemon_supported()
{
    return false;
}

static std::wstring srs_utf8_to_wide(const char *s)
{
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 1) {
        return std::wstring();
    }
    std::wstring w(n - 1, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}

// Quote an argument so that the child's CRT, or CommandLineToArgvW, splits the command line back into it:
// backslashes are literal unless they come before a quote, which they then escape in pairs.
static void srs_append_arg(std::wstring &cmd, const std::wstring &arg)
{
    if (!cmd.empty()) {
        cmd += L' ';
    }

    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }

    cmd += L'"';
    for (size_t i = 0;; i++) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            slashes++;
            i++;
        }

        if (i == arg.size()) {
            cmd.append(slashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            cmd.append(slashes * 2 + 1, L'\\');
        } else {
            cmd.append(slashes, L'\\');
        }
        cmd += arg[i];
    }
    cmd += L'"';
}

// An inheritable handle for a standard stream of the child: the file opened to append, the null device for
// /dev/null, which configs and SRS name, or SRS's own stream when the file is empty.
static HANDLE srs_child_stdio(const char *file, DWORD std_handle)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!file || !*file) {
        HANDLE self = ::GetStdHandle(std_handle);
        HANDLE dup = NULL;
        if (self != NULL && self != INVALID_HANDLE_VALUE &&
            ::DuplicateHandle(::GetCurrentProcess(), self, ::GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            return dup;
        }
        file = "/dev/null";
    }

    if (strcmp(file, "/dev/null") == 0) {
        return ::CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    // Append only, so the writes of two handles to one file, such as stdout and stderr, never overwrite.
    return ::CreateFileW(srs_utf8_to_wide(file).c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
}

// The job of every child, which ends them all when SRS quits, even when it crashes.
static HANDLE srs_children_job()
{
    static HANDLE job = NULL;
    if (job) {
        return job;
    }

    HANDLE h = ::CreateJobObjectW(NULL, NULL);
    if (!h) {
        return NULL;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit;
    memset(&limit, 0, sizeof(limit));
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(h, JobObjectExtendedLimitInformation, &limit, sizeof(limit))) {
        ::CloseHandle(h);
        return NULL;
    }

    job = h;
    return job;
}

pid_t srs_process_spawn(const char *bin, const char *const argv[], const char *stdout_file,
                        const char *stderr_file, const char *cid, const char *info)
{
    // As execv, the binary is a path, not searched in PATH, but it may leave out the .exe.
    std::wstring app = srs_utf8_to_wide(bin);
    if (::GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) {
        app += L".exe";
    }

    std::wstring cmd;
    for (int i = 0; argv[i]; i++) {
        srs_append_arg(cmd, srs_utf8_to_wide(argv[i]));
    }

    HANDLE job = srs_children_job();
    if (!job) {
        errno = EPERM;
        return -1;
    }

    HANDLE handles[3];
    handles[0] = srs_child_stdio("/dev/null", STD_INPUT_HANDLE);
    handles[1] = srs_child_stdio(stdout_file, STD_OUTPUT_HANDLE);
    handles[2] = srs_child_stdio(stderr_file, STD_ERROR_HANDLE);

    pid_t pid = -1;
    int error = EINVAL;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    for (int i = 0; i < 3; i++) {
        if (handles[i] == INVALID_HANDLE_VALUE) {
            error = ENOENT;
            goto cleanup;
        }
    }

    // Inherit only the three streams, not the sockets and files SRS has open.
    {
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(NULL, 1, 0, &size);
        attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(size);
        if (!attrs || !::InitializeProcThreadAttributeList(attrs, 1, 0, &size)) {
            free(attrs);
            attrs = NULL;
            goto cleanup;
        }
        if (!::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), NULL, NULL)) {
            goto cleanup;
        }
    }

    {
        STARTUPINFOEXW si;
        memset(&si, 0, sizeof(si));
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = handles[0];
        si.StartupInfo.hStdOutput = handles[1];
        si.StartupInfo.hStdError = handles[2];
        si.lpAttributeList = attrs;

        // Its own process group keeps the Ctrl+C of the console from the child, as SIG_IGN does on Unix.
        // Without a console for SRS, the child gets none either, not a new console window.
        DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP;
        if (!::GetConsoleWindow()) {
            flags |= CREATE_NO_WINDOW;
        }

        // CreateProcessW may change the command line, so it gets a copy it can write.
        std::vector<wchar_t> line(cmd.begin(), cmd.end());
        line.push_back(L'\0');
        if (!::CreateProcessW(app.c_str(), &line[0], NULL, NULL, TRUE, flags, NULL, NULL, &si.StartupInfo, &pi)) {
            DWORD r0 = ::GetLastError();
            error = (r0 == ERROR_FILE_NOT_FOUND || r0 == ERROR_PATH_NOT_FOUND) ? ENOENT : EINVAL;
            goto cleanup;
        }
    }

    // In the job before it runs, so nothing it starts escapes the job.
    if (!::AssignProcessToJobObject(job, pi.hProcess)) {
        ::TerminateProcess(pi.hProcess, 128 + SIGKILL);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        error = EPERM;
        goto cleanup;
    }

    // The child writes nothing before it resumes, so the banner comes first, as the Unix child writes it.
    {
        char banner[512];
        int n = snprintf(banner, sizeof(banner), "\nprocess ppid=%d, cid=%s, pid=%d, in=%d, out=%d, err=%d\n",
                         (int)::GetCurrentProcessId(), cid, (int)pi.dwProcessId, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
        std::string text(banner, n > 0 && n < (int)sizeof(banner) ? n : 0);
        text += info;
        DWORD written = 0;
        ::WriteFile(handles[1], text.data(), (DWORD)text.size(), &written, NULL);
    }

    ::ResumeThread(pi.hThread);
    ::CloseHandle(pi.hThread);

    pid = (pid_t)pi.dwProcessId;
    srs_children[pid] = pi.hProcess;

cleanup:
    if (attrs) {
        ::DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
    for (int i = 0; i < 3; i++) {
        if (handles[i] != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handles[i]);
        }
    }

    if (pid < 0) {
        errno = error;
    }
    return pid;
}

int srs_self_pipe_open(int fds[2])
{
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int addrlen = sizeof(addr);

    SOCKET writer = INVALID_SOCKET;
    SOCKET reader = INVALID_SOCKET;
    struct sockaddr_in local, peer;
    int locallen = sizeof(local), peerlen = sizeof(peer);
    if (::bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || ::listen(listener, 1) != 0 ||
        ::getsockname(listener, (struct sockaddr *)&addr, &addrlen) != 0) {
        goto failed;
    }

    writer = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (writer == INVALID_SOCKET || ::connect(writer, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        goto failed;
    }
    reader = ::accept(listener, (struct sockaddr *)&peer, &peerlen);
    if (reader == INVALID_SOCKET || ::getsockname(writer, (struct sockaddr *)&local, &locallen) != 0) {
        goto failed;
    }

    // Any local process may connect to the listener, so the reader must be the peer of our writer.
    if (peer.sin_port != local.sin_port || peer.sin_addr.s_addr != local.sin_addr.s_addr) {
        goto failed;
    }

    ::closesocket(listener);
    fds[0] = (int)reader;
    fds[1] = (int)writer;
    return 0;

failed:
    if (reader != INVALID_SOCKET) {
        ::closesocket(reader);
    }
    if (writer != INVALID_SOCKET) {
        ::closesocket(writer);
    }
    ::closesocket(listener);
    return -1;
}

ssize_t srs_self_pipe_write(int fd, const void *buf, size_t count)
{
    int r = ::send((SOCKET)fd, (const char *)buf, (int)count, 0);
    return r == SOCKET_ERROR ? -1 : r;
}

int srs_self_pipe_close(int fd)
{
    return ::closesocket((SOCKET)fd) == 0 ? 0 : -1;
}

static void (*srs_console_int_handler)(int) = NULL;
static void (*srs_console_term_handler)(int) = NULL;

// Windows runs it on a new thread for each console event.
static BOOL WINAPI srs_console_ctrl(DWORD type)
{
    int signo = SIGTERM;
    void (*handler)(int) = srs_console_term_handler;
    if (type == CTRL_C_EVENT) {
        signo = SIGINT;
        handler = srs_console_int_handler;
    } else if (type != CTRL_BREAK_EVENT && type != CTRL_CLOSE_EVENT && type != CTRL_SHUTDOWN_EVENT) {
        return FALSE;
    }
    if (!handler) {
        return FALSE;
    }

    handler(signo);

    // Windows ends the process once the handler of a close or a shutdown returns, so wait for SRS to quit,
    // until Windows ends it after its timeout.
    if (type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        ::Sleep(INFINITE);
    }
    return TRUE;
}

int srs_signal_install(int signo, void (*handler)(int))
{
    if (signo == SIGINT) {
        srs_console_int_handler = handler;
    } else if (signo == SIGTERM) {
        srs_console_term_handler = handler;
    } else {
        errno = ENOSYS;
        return -1;
    }

    static bool installed = false;
    if (!installed) {
        if (!::SetConsoleCtrlHandler(srs_console_ctrl, TRUE)) {
            return -1;
        }
        installed = true;
    }
    return 0;
}

#else

int srs_socket_startup()
{
    return 0;
}

int srs_close_socket(srs_socket_t fd)
{
    return ::close(fd);
}

int srs_setsockopt(srs_socket_t fd, int level, int name, const void *value, int size)
{
    return ::setsockopt(fd, level, name, value, (socklen_t)size);
}

int srs_getsockopt(srs_socket_t fd, int level, int name, void *value, socklen_t *size)
{
    return ::getsockopt(fd, level, name, value, size);
}

int srs_bind(srs_socket_t fd, const sockaddr *addr, socklen_t size)
{
    return ::bind(fd, addr, size);
}

// Linux and macOS refuse at once.
int srs_socket_refuse_fast(srs_socket_t /*fd*/, const sockaddr * /*addr*/)
{
    return 0;
}

int srs_socket_ignore_unreachable(srs_socket_t /*fd*/)
{
    return 0;
}

int srs_socket_cloexec(srs_socket_t fd)
{
    int flags = ::fcntl(fd, F_GETFD);
    flags |= FD_CLOEXEC;
    return ::fcntl(fd, F_SETFD, flags) == -1 ? -1 : 0;
}

int srs_socket_reuseaddr(srs_socket_t fd)
{
    int v = 1;
    return srs_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(int));
}

int srs_socket_reuseport(srs_socket_t fd)
{
#if defined(SO_REUSEPORT)
    int v = 1;
    return srs_setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(int));
#else
#warning "SO_REUSEPORT is not supported by your OS"
    errno = ENOPROTOOPT;
    return -1;
#endif
}

int srs_iov_max()
{
    return (int)::sysconf(_SC_IOV_MAX);
}

int srs_getifaddrs(struct ifaddrs **ifap)
{
    return ::getifaddrs(ifap);
}

void srs_freeifaddrs(struct ifaddrs *ifa)
{
    ::freeifaddrs(ifa);
}

int srs_uname(struct utsname *name)
{
    return ::uname(name) < 0 ? -1 : 0;
}

int srs_getrusage(struct rusage *usage)
{
    return ::getrusage(RUSAGE_SELF, usage);
}

int srs_fd_limit()
{
    return (int)::sysconf(_SC_OPEN_MAX);
}

int srs_cpu_count(bool online)
{
    return (int)::sysconf(online ? _SC_NPROCESSORS_ONLN : _SC_NPROCESSORS_CONF);
}

int srs_clock_ticks()
{
    return (int)::sysconf(_SC_CLK_TCK);
}

int srs_kill(pid_t pid, int signo)
{
    return ::kill(pid, signo);
}

pid_t srs_waitpid(pid_t pid, int *status, int options)
{
    return ::waitpid(pid, status, options);
}

int srs_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    return ::gettimeofday(tv, tz);
}

struct tm *srs_localtime_r(const time_t *t, struct tm *result)
{
    return ::localtime_r(t, result);
}

struct tm *srs_gmtime_r(const time_t *t, struct tm *result)
{
    return ::gmtime_r(t, result);
}

void srs_srandom(unsigned int seed)
{
    ::srandom(seed);
}

long srs_random()
{
    return ::random();
}

char *srs_initstate(unsigned int seed, char *state, size_t size)
{
    return ::initstate(seed, state, size);
}

char *srs_setstate(char *state)
{
    return ::setstate(state);
}

int srs_file_open(const char *path, int oflag, ...)
{
    int mode = 0;
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return ::open(path, oflag, mode);
}

ssize_t srs_file_read(int fd, void *buf, size_t count)
{
    return ::read(fd, buf, count);
}

ssize_t srs_file_write(int fd, const void *buf, size_t count)
{
    return ::write(fd, buf, count);
}

srs_off_t srs_file_lseek(int fd, srs_off_t offset, int whence)
{
    return ::lseek(fd, offset, whence);
}

int srs_file_close(int fd)
{
    return ::close(fd);
}

int srs_file_open_renamable(const char *path, int oflag, int mode)
{
    return ::open(path, oflag, mode);
}

int srs_fseek(FILE *stream, srs_off_t offset, int whence)
{
    return ::fseeko(stream, offset, whence);
}

srs_off_t srs_ftell(FILE *stream)
{
    return ::ftello(stream);
}

int srs_mkdir(const char *path)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH;
    return ::mkdir(path, mode);
}

int srs_rename(const char *from, const char *to)
{
    return ::rename(from, to);
}

int srs_file_lock(int fd)
{
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;
    return ::fcntl(fd, F_SETLK, &lock) == -1 ? -1 : 0;
}

int srs_file_truncate(int fd, srs_off_t size)
{
    return ::ftruncate(fd, size);
}

int srs_file_cloexec(int fd)
{
    int val = ::fcntl(fd, F_GETFD, 0);
    if (val < 0) {
        return -1;
    }
    return ::fcntl(fd, F_SETFD, val | FD_CLOEXEC) < 0 ? -1 : 0;
}

pid_t srs_getppid()
{
    return ::getppid();
}

pid_t srs_fork()
{
    return ::fork();
}

bool srs_daemon_supported()
{
    return true;
}

static int srs_redirect_output(const char *file, int to_fd)
{
    if (!file || !*file) {
        return 0;
    }

    int fd = ::open(file, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if (fd < 0) {
        return -1;
    }

    int r0 = ::dup2(fd, to_fd);
    ::close(fd);
    return r0 < 0 ? -1 : 0;
}

pid_t srs_process_spawn(const char *bin, const char *const argv[], const char *stdout_file,
                        const char *stderr_file, const char *cid, const char *info)
{
    int ppid = ::getpid();

    pid_t pid = ::fork();
    if (pid != 0) {
        return pid;
    }

    ::signal(SIGINT, SIG_IGN);
    ::signal(SIGTERM, SIG_IGN);

    // No stdin for process, @bug https://github.com/ossrs/srs/issues/1592
    if (srs_redirect_output(stdout_file, STDOUT_FILENO) < 0 || srs_redirect_output(stderr_file, STDERR_FILENO) < 0 ||
        srs_redirect_output("/dev/null", STDIN_FILENO) < 0) {
        fprintf(stdout, "child process error, redirect io failed, errno=%d(%s)\n", errno, strerror(errno));
        ::exit(-1);
    }

    fprintf(stdout, "\n");
    fprintf(stdout, "process ppid=%d, cid=%s, pid=%d, in=%d, out=%d, err=%d\n",
            ppid, cid, ::getpid(), STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);
    fprintf(stdout, "%s", info);

    int r0 = ::execv(bin, (char *const *)argv);
    if (r0 < 0) {
        fprintf(stderr, "fork process failed, errno=%d(%s)", errno, strerror(errno));
    }
    ::exit(r0);
}

int srs_self_pipe_open(int fds[2])
{
    return ::pipe(fds);
}

ssize_t srs_self_pipe_write(int fd, const void *buf, size_t count)
{
    return ::write(fd, buf, count);
}

int srs_self_pipe_close(int fd)
{
    return ::close(fd);
}

int srs_signal_install(int signo, void (*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return ::sigaction(signo, &sa, NULL);
}

#endif
