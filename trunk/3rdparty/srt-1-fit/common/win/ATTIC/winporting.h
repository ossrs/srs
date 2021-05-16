#ifndef _WINPORTING_H_
#define _WINPORTING_H_

// NOTE: This file has been borrowed from LCM project
// http://lcm-proj.github.io/

#if !defined(__MINGW32__)
#define strtoll   _strtoi64
#define	strdup		_strdup
#define	mode_t		int
#define snprintf	_snprintf
//#define	PATH_MAX	MAX_PATH
#define	fseeko		_fseeki64
#define ftello		_ftelli64
//#define socklen_t	int
#define in_addr_t	in_addr
#define	SHUT_RDWR	SD_BOTH
#define	HUGE		HUGE_VAL
#define O_NONBLOCK	0x4000
#define F_GETFL		3
#define	F_SETFL		4
#endif

#include <direct.h>
#include <winsock2.h>

#ifdef __cplusplus
extern "C" {
#endif

// Microsoft implementation of these structures has the 
// pointer and length in reversed positions.
typedef struct iovec
{
    ULONG       iov_len;
    char        *iov_base;
} iovec;

typedef struct msghdr
{
    struct sockaddr    *msg_name;
    int         msg_namelen;
    struct iovec       *msg_iov;
    ULONG       msg_iovlen;
    int         msg_controllen;
    char        *msg_control;
    ULONG       msg_flags;
} msghdr;

//typedef long int ssize_t;

//int inet_aton(const char *cp, struct in_addr *inp);

int fcntl (int fd, int flag1, ...);

size_t recvmsg ( SOCKET s, struct msghdr *msg, int flags );
size_t sendmsg ( SOCKET s, const struct msghdr *msg, int flags );

#ifdef __cplusplus
}
#endif

#endif // _WINPORTING_H_
