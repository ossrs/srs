#ifndef SRS_WIN_PORTING_H
#define SRS_WIN_PORTING_H

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#if defined(_WIN32)
    #include <windows.h>
#endif

/** 
* for linux like,
* for example, not on windows or it's cygwin. 
* while the _WIN32 includes both 32-bit and 64-bit
*/
#if !defined(_WIN32) || defined(__CYGWIN__)
    #define SOCKET_ETIME		ETIME
    #define SOCKET_ECONNRESET   ECONNRESET

    #define SOCKET int
    #define SOCKET_ERRNO()	errno
    #define SOCKET_RESET(fd) fd = -1; (void)0
    #define SOCKET_CLOSE(fd) \
        if (fd > 0) {\
            ::close(fd); \
            fd = -1; \
        } \
        (void)0
    #define SOCKET_VALID(x) (x > 0)
    #define SOCKET_SETUP()   (void)0
    #define SOCKET_CLEANUP() (void)0
#else /*on windows, but not on cygwin*/
    #include <sys/stat.h>
    #include <time.h>
    #include <winsock2.h>
    #include <stdint.h>

    #ifdef _MSC_VER		//for VS2010
    #include <io.h>
    #include <fcntl.h>
    #define S_IRUSR _S_IREAD
    #define S_IWUSR _S_IWRITE
    #define open _open
    #define close _close
    #define lseek _lseek
    #define write _write
    #define read _read

    typedef int ssize_t;
    typedef int pid_t;
    typedef int mode_t;
    typedef int64_t useconds_t;
    #endif

    #define S_IRGRP 0
    #define S_IWGRP 0
    #define S_IXGRP 0
    #define S_IRWXG 0
    #define S_IROTH 0
    #define S_IWOTH 0
    #define S_IXOTH 0
    #define S_IRWXO 0

    #define PRId64 "lld"

    #define SOCKET_ETIME		WSAETIMEDOUT
    #define SOCKET_ECONNRESET   WSAECONNRESET
    #define SOCKET_ERRNO()    WSAGetLastError()
    #define SOCKET_RESET(x) x=INVALID_SOCKET
    #define SOCKET_CLOSE(x) if(x!=INVALID_SOCKET){::closesocket(x);x=INVALID_SOCKET;}
    #define SOCKET_VALID(x) (x!=INVALID_SOCKET)
    #define SOCKET_BUFF(x)  ((char*)x)
    #define SOCKET_SETUP()	socket_setup()
    #define SOCKET_CLEANUP() socket_cleanup()

    typedef uint32_t u_int32_t;
    typedef uint8_t  u_int8_t;
    typedef int socklen_t;
    struct iovec {
       void* iov_base; /* Starting address */
       size_t iov_len; /* Length in bytes */
    };

    #define snprintf _snprintf
    ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
    const char* inet_ntop(int af, const void *src, char *dst, socklen_t size);
    int gettimeofday(struct timeval* tv, struct timezone* tz);
    pid_t getpid(void);
    int usleep(useconds_t usec);
    int socket_setup();
    int socket_cleanup();
#endif

#endif //SRS_WIN_PORTING_H
