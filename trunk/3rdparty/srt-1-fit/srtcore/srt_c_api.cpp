/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#include <iterator>
#include <fstream>
#if __APPLE__
   #include "TargetConditionals.h"
#endif
#include "srt.h"
#include "common.h"
#include "core.h"
#include "utilities.h"

using namespace std;


extern "C" {

int srt_startup() { return CUDT::startup(); }
int srt_cleanup() { return CUDT::cleanup(); }

SRTSOCKET srt_socket(int af, int type, int protocol) { return CUDT::socket(af, type, protocol); }
SRTSOCKET srt_create_socket()
{
    // XXX This must include rework around m_iIPVersion. This must be
    // abandoned completely and all "IP VERSION" thing should rely on
    // the exact specification in the 'sockaddr' objects passed to other functions,
    // that is, the "current IP Version" remains undefined until any of
    // srt_bind() or srt_connect() function is done. And when any of these
    // functions are being called, the IP version is contained in the
    // sockaddr object passed there.

    // Until this rework is done, srt_create_socket() will set the
    // default AF_INET family.

    // Note that all arguments except the first one here are ignored.
    return CUDT::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

int srt_bind(SRTSOCKET u, const struct sockaddr * name, int namelen) { return CUDT::bind(u, name, namelen); }
int srt_bind_peerof(SRTSOCKET u, UDPSOCKET udpsock) { return CUDT::bind(u, udpsock); }
int srt_listen(SRTSOCKET u, int backlog) { return CUDT::listen(u, backlog); }
SRTSOCKET srt_accept(SRTSOCKET u, struct sockaddr * addr, int * addrlen) { return CUDT::accept(u, addr, addrlen); }
int srt_connect(SRTSOCKET u, const struct sockaddr * name, int namelen) { return CUDT::connect(u, name, namelen, 0); }
int srt_connect_debug(SRTSOCKET u, const struct sockaddr * name, int namelen, int forced_isn) { return CUDT::connect(u, name, namelen, forced_isn); }

int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen)
{
    bool yes = 1;
    CUDT::setsockopt(u, 0, UDT_RENDEZVOUS, &yes, sizeof yes);

    // Note: PORT is 16-bit and at the same location in both sockaddr_in and sockaddr_in6.
    // Just as a safety precaution, check the structs.
    if ( (local_name->sa_family != AF_INET && local_name->sa_family != AF_INET6)
            || local_name->sa_family != remote_name->sa_family)
        return SRT_EINVPARAM;

    sockaddr_in* local_sin = (sockaddr_in*)local_name;
    sockaddr_in* remote_sin = (sockaddr_in*)remote_name;

    if (local_sin->sin_port != remote_sin->sin_port)
        return SRT_EINVPARAM;

    int st = srt_bind(u, local_name, local_namelen);
    if ( st != 0 )
        return st;

    return srt_connect(u, remote_name, remote_namelen);
}

int srt_close(SRTSOCKET u)
{
    SRT_SOCKSTATUS st = srt_getsockstate(u);

    if ((st == SRTS_NONEXIST) ||
        (st == SRTS_CLOSED)   ||
        (st == SRTS_CLOSING) )
    {
        // It's closed already. Do nothing.
        return 0;
    }

    return CUDT::close(u);
}

int srt_getpeername(SRTSOCKET u, struct sockaddr * name, int * namelen) { return CUDT::getpeername(u, name, namelen); }
int srt_getsockname(SRTSOCKET u, struct sockaddr * name, int * namelen) { return CUDT::getsockname(u, name, namelen); }
int srt_getsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, void * optval, int * optlen)
{ return CUDT::getsockopt(u, level, optname, optval, optlen); }
int srt_setsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, const void * optval, int optlen)
{ return CUDT::setsockopt(u, level, optname, optval, optlen); }

int srt_getsockflag(SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen)
{ return CUDT::getsockopt(u, 0, opt, optval, optlen); }
int srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen)
{ return CUDT::setsockopt(u, 0, opt, optval, optlen); }

int srt_send(SRTSOCKET u, const char * buf, int len) { return CUDT::send(u, buf, len, 0); }
int srt_recv(SRTSOCKET u, char * buf, int len) { return CUDT::recv(u, buf, len, 0); }
int srt_sendmsg(SRTSOCKET u, const char * buf, int len, int ttl, int inorder) { return CUDT::sendmsg(u, buf, len, ttl, 0!=  inorder); }
int srt_recvmsg(SRTSOCKET u, char * buf, int len) { uint64_t ign_srctime; return CUDT::recvmsg(u, buf, len, ign_srctime); }
int64_t srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
    if (!path || !offset )
    {
        return CUDT::setError(CUDTException(MJ_NOTSUP, MN_INVAL, 0));
    }
    fstream ifs(path, ios::binary | ios::in);
    if (!ifs)
    {
        return CUDT::setError(CUDTException(MJ_FILESYSTEM, MN_READFAIL, 0));
    }
    int64_t ret = CUDT::sendfile(u, ifs, *offset, size, block);
    ifs.close();
    return ret;
}

int64_t srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
    if (!path || !offset )
    {
        return CUDT::setError(CUDTException(MJ_NOTSUP, MN_INVAL, 0));
    }
    fstream ofs(path, ios::binary | ios::out);
    if (!ofs)
    {
        return CUDT::setError(CUDTException(MJ_FILESYSTEM, MN_WRAVAIL, 0));
    }
    int64_t ret = CUDT::recvfile(u, ofs, *offset, size, block);
    ofs.close();
    return ret;
}

extern const SRT_MSGCTRL srt_msgctrl_default = { 0, -1, false, 0, 0, 0, 0 };

void srt_msgctrl_init(SRT_MSGCTRL* mctrl)
{
    *mctrl = srt_msgctrl_default;
}

int srt_sendmsg2(SRTSOCKET u, const char * buf, int len, SRT_MSGCTRL *mctrl)
{
    // Allow NULL mctrl in the API, but not internally.
    if (mctrl)
        return CUDT::sendmsg2(u, buf, len, Ref(*mctrl));
    SRT_MSGCTRL mignore = srt_msgctrl_default;
    return CUDT::sendmsg2(u, buf, len, Ref(mignore));
}

int srt_recvmsg2(SRTSOCKET u, char * buf, int len, SRT_MSGCTRL *mctrl)
{
    if (mctrl)
        return CUDT::recvmsg2(u, buf, len, Ref(*mctrl));
    SRT_MSGCTRL mignore = srt_msgctrl_default;
    return CUDT::recvmsg2(u, buf, len, Ref(mignore));
}

const char* srt_getlasterror_str() { return UDT::getlasterror().getErrorMessage(); }

int srt_getlasterror(int* loc_errno)
{
    if ( loc_errno )
        *loc_errno = UDT::getlasterror().getErrno();
    return CUDT::getlasterror().getErrorCode();
}

const char* srt_strerror(int code, int err)
{
    static CUDTException e;
    e = CUDTException(CodeMajor(code/1000), CodeMinor(code%1000), err);
    return(e.getErrorMessage());
}


void srt_clearlasterror()
{
    UDT::getlasterror().clear();
}

int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear) { return CUDT::bstats(u, perf, 0!=  clear); }
int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous) { return CUDT::bstats(u, perf, 0!=  clear, 0!= instantaneous); }

SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u) { return SRT_SOCKSTATUS((int)CUDT::getsockstate(u)); }

// event mechanism
int srt_epoll_create() { return CUDT::epoll_create(); }

// You can use either SRT_EPOLL_* flags or EPOLL* flags from <sys/epoll.h>, both are the same. IN/OUT/ERR only.
// events == NULL accepted, in which case all flags are set.
int srt_epoll_add_usock(int eid, SRTSOCKET u, const int * events) { return CUDT::epoll_add_usock(eid, u, events); }

int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

    if (events) {
        flag = *events;
    } else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }

    // call UDT native function
    return CUDT::epoll_add_ssock(eid, s, &flag);
}

int srt_epoll_remove_usock(int eid, SRTSOCKET u) { return CUDT::epoll_remove_usock(eid, u); }
int srt_epoll_remove_ssock(int eid, SYSSOCKET s) { return CUDT::epoll_remove_ssock(eid, s); }

int srt_epoll_update_usock(int eid, SRTSOCKET u, const int * events)
{
    return CUDT::epoll_update_usock(eid, u, events);
}

int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

    if (events) {
        flag = *events;
    } else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }

    // call UDT native function
    return CUDT::epoll_update_ssock(eid, s, &flag);
}

int srt_epoll_wait(
      int eid,
      SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum,
      int64_t msTimeOut,
      SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum)
  {
    return UDT::epoll_wait2(
        eid,
        readfds, rnum, writefds, wnum,
        msTimeOut,
        lrfds, lrnum, lwfds, lwnum);
}

int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
    return UDT::epoll_uwait(
        eid,
        fdsSet,
        fdsSize,
        msTimeOut);
}

// use this function to set flags. Default flags are always "everything unset".
// Pass 0 here to clear everything, or nonzero to set a desired flag.
// Pass -1 to not change anything (but still get the current flag value).
int32_t srt_epoll_set(int eid, int32_t flags) { return CUDT::epoll_set(eid, flags); }

int srt_epoll_release(int eid) { return CUDT::epoll_release(eid); }

void srt_setloglevel(int ll)
{
    UDT::setloglevel(srt_logging::LogLevel::type(ll));
}

void srt_addlogfa(int fa)
{
    UDT::addlogfa(srt_logging::LogFA(fa));
}

void srt_dellogfa(int fa)
{
    UDT::dellogfa(srt_logging::LogFA(fa));
}

void srt_resetlogfa(const int* fara, size_t fara_size)
{
    UDT::resetlogfa(fara, fara_size);
}

void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler)
{
    UDT::setloghandler(opaque, handler);
}

void srt_setlogflags(int flags)
{
    UDT::setlogflags(flags);
}

int srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes)
{
    return CUDT::getsndbuffer(sock, blocks, bytes);
}

enum SRT_REJECT_REASON srt_getrejectreason(SRTSOCKET sock)
{
    return CUDT::rejectReason(sock);
}

int srt_listen_callback(SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    if (!hook)
        return CUDT::setError(CUDTException(MJ_NOTSUP, MN_INVAL));

    return CUDT::installAcceptHook(lsn, hook, opaq);
}

}
