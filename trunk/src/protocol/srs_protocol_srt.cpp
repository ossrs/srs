//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_srt.hpp>

#include <sstream>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_core_autofree.hpp>

#include <srt/srt.h>

// TODO: FIXME: protocol could no include app's header file, so define TAG_SRT in this file.
#define TAG_SRT "SRT"

#define SET_SRT_OPT_STR(srtfd, optname, buf, size)                                  \
    if (srt_setsockflag(srtfd, optname, buf, size) == SRT_ERROR) {                  \
        std::stringstream ss;                                                       \
        ss << "srtfd=" << srtfd << ",set " << #optname                              \
           << " failed,err=" << srt_getlasterror_str();                             \
        return srs_error_new(ERROR_SRT_SOCKOPT, "%s", ss.str().c_str());            \
    } 

#define SET_SRT_OPT(srtfd, optname, val)                                            \
    if (srt_setsockflag(srtfd, optname, &val, sizeof(val)) == SRT_ERROR) {          \
        std::stringstream ss;                                                       \
        ss << "srtfd=" << srtfd << ",set " << #optname << "=" << val                \
           << " failed,err=" << srt_getlasterror_str();                             \
        return srs_error_new(ERROR_SRT_SOCKOPT, "%s", ss.str().c_str());            \
    } 

#define GET_SRT_OPT(srtfd, optname, val)                                        \
    do {                                                                        \
        int size = sizeof(val);                                                 \
        if (srt_getsockflag(srtfd, optname, &val, &size) == SRT_ERROR) {        \
            std::stringstream ss;                                               \
            ss << "srtfd=" << srtfd << ",get " << #optname                      \
               << " failed,err=" << srt_getlasterror_str();                     \
            return srs_error_new(ERROR_SRT_SOCKOPT, "%s", ss.str().c_str());    \
        }                                                                       \
    } while (0)


static srs_error_t do_srs_srt_listen(srs_srt_t srt_fd, addrinfo* r)
{
    srs_error_t err = srs_success;

    if ((err = srs_srt_nonblock(srt_fd)) != srs_success) {
        return srs_error_wrap(err, "nonblock");
    }

    if (srt_bind(srt_fd, r->ai_addr, r->ai_addrlen) == -1) {
        return srs_error_new(ERROR_SOCKET_BIND, "bind");
    }

    if (srt_listen(srt_fd, 100) == -1) {
        return srs_error_new(ERROR_SOCKET_LISTEN, "listen");
    }

    return err;
}

static srs_error_t do_srs_srt_get_streamid(srs_srt_t srt_fd, string& streamid)
{
    // SRT max streamid length is 512.
    char sid[512];
    GET_SRT_OPT(srt_fd, SRTO_STREAMID, sid);

    streamid.assign(sid);
    return srs_success;
}

static void srs_srt_log_handler(void* opaque, int level, const char* file, int line, const char* area, const char* message)
{
    switch (level) {
        case srt_logging::LogLevel::debug:
            srs_info2(TAG_SRT, "%s:%d(%s) # %s", file, line, area, message);
            break;
        case srt_logging::LogLevel::note:
            srs_trace2(TAG_SRT, "%s:%d(%s) # %s", file, line, area, message);
            break;
        case srt_logging::LogLevel::warning:
            srs_warn2(TAG_SRT, "%s:%d(%s) # %s", file, line, area, message);
            break;
        case srt_logging::LogLevel::error:
        case srt_logging::LogLevel::fatal:
            srs_error2(TAG_SRT, "%s:%d(%s) # %s", file, line, area, message);
            break;
        default:
            srs_trace2(TAG_SRT, "%s:%d(%s) # %s", file, line, area, message);
            break;
    }
}

static string srt_sockstatus_to_str(const SRT_SOCKSTATUS& status)
{
    switch (status) {
        case SRTS_INIT: return "SRTS_INIT";
        case SRTS_OPENED: return "SRTS_OPENED";
        case SRTS_LISTENING: return "SRTS_LISTENING";
        case SRTS_CONNECTING: return "SRT_CONNECTING";
        case SRTS_CONNECTED: return "SRTS_CONNECTED";
        case SRTS_BROKEN: return "SRTS_BROKEN";
        case SRTS_CLOSING: return "SRTS_CLOSING";
        case SRTS_CLOSED: return "SRTS_CLOSED";
        case SRTS_NONEXIST: return "SRTS_NONEXIST";
        default: return "unknown";
    }

    return "unknown";
}

srs_error_t srs_srt_log_initialize()
{
    srs_error_t err = srs_success;

    srt_setlogflags(0 | SRT_LOGF_DISABLE_TIME | SRT_LOGF_DISABLE_SEVERITY |
        SRT_LOGF_DISABLE_THREADNAME | SRT_LOGF_DISABLE_EOL);
    srt_setloghandler(NULL, srs_srt_log_handler);

    return err;
}

srs_srt_t srs_srt_socket_invalid()
{
    return SRT_INVALID_SOCK;
}

srs_error_t srs_srt_socket(srs_srt_t* pfd)
{
    srs_error_t err = srs_success;

    srs_srt_t srt_fd = 0;
    if ((srt_fd = srt_create_socket()) < 0) {
        return srs_error_new(ERROR_SOCKET_CREATE, "create srt socket");
    }

    *pfd = srt_fd;

    return err;
}

srs_error_t srs_srt_close(srs_srt_t fd)
{
    // TODO: FIXME: Handle error.
    srt_close(fd);
    return srs_success;
}

srs_error_t srs_srt_socket_with_default_option(srs_srt_t* pfd)
{
    srs_error_t err = srs_success;

    srs_srt_t srt_fd = 0;
    if ((srt_fd = srt_create_socket()) < 0) {
        return srs_error_new(ERROR_SOCKET_CREATE, "create srt socket");
    }

    if ((err = srs_srt_nonblock(srt_fd)) != srs_success) {
        return srs_error_wrap(err, "nonblock");
    }

    if ((err = srs_srt_set_tsbpdmode(srt_fd, false)) != srs_success) {
        return srs_error_wrap(err, "set tsbpdmode");
    }

    if ((err = srs_srt_set_tlpktdrop(srt_fd, false)) != srs_success) {
        return srs_error_wrap(err, "set tlpktdrop");
    }

    if ((err = srs_srt_set_latency(srt_fd, false)) != srs_success) {
        return srs_error_wrap(err, "set latency");
    }

    *pfd = srt_fd;

    return err;
}

srs_error_t srs_srt_listen(srs_srt_t srt_fd, std::string ip, int port)
{
    srs_error_t err = srs_success;

    char sport[8];
    int r0 = snprintf(sport, sizeof(sport), "%d", port);
    srs_assert(r0 > 0 && r0 < (int)sizeof(sport));

    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICHOST;

    addrinfo* r = NULL;
    SrsAutoFreeH(addrinfo, r, freeaddrinfo);
    if(getaddrinfo(ip.c_str(), sport, (const addrinfo*)&hints, &r)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "getaddrinfo hints=(%d,%d,%d)",
            hints.ai_family, hints.ai_socktype, hints.ai_flags);
    }

    if ((err = do_srs_srt_listen(srt_fd, r)) != srs_success) {
        srt_close(srt_fd);
        return srs_error_wrap(err, "srt_fd=%d", srt_fd);
    }

    return err;
}

srs_error_t srs_srt_nonblock(srs_srt_t srt_fd)
{
    int sync = 0;
    SET_SRT_OPT(srt_fd, SRTO_SNDSYN, sync);
    SET_SRT_OPT(srt_fd, SRTO_RCVSYN, sync);

    return srs_success;
}

srs_error_t srs_srt_set_maxbw(srs_srt_t srt_fd, int64_t maxbw)
{
    SET_SRT_OPT(srt_fd, SRTO_MAXBW, maxbw);
    return srs_success;
}

srs_error_t srs_srt_set_mss(srs_srt_t srt_fd, int mss)
{
    SET_SRT_OPT(srt_fd, SRTO_MSS, mss);
    return srs_success;
}

srs_error_t srs_srt_set_payload_size(srs_srt_t srt_fd, int payload_size)
{
    SET_SRT_OPT(srt_fd, SRTO_PAYLOADSIZE, payload_size);
    return srs_success;
}

srs_error_t srs_srt_set_connect_timeout(srs_srt_t srt_fd, int timeout)
{
    SET_SRT_OPT(srt_fd, SRTO_CONNTIMEO, timeout);
    return srs_success;
}

srs_error_t srs_srt_set_peer_idle_timeout(srs_srt_t srt_fd, int timeout)
{
    SET_SRT_OPT(srt_fd, SRTO_PEERIDLETIMEO, timeout);
    return srs_success;
}

srs_error_t srs_srt_set_tsbpdmode(srs_srt_t srt_fd, bool tsbpdmode)
{
    SET_SRT_OPT(srt_fd, SRTO_TSBPDMODE, tsbpdmode);
    return srs_success;
}

srs_error_t srs_srt_set_sndbuf(srs_srt_t srt_fd, int sndbuf)
{
    SET_SRT_OPT(srt_fd, SRTO_SNDBUF, sndbuf);
    return srs_success;
}

srs_error_t srs_srt_set_rcvbuf(srs_srt_t srt_fd, int rcvbuf)
{
    SET_SRT_OPT(srt_fd, SRTO_RCVBUF, rcvbuf);
    return srs_success;
}

srs_error_t srs_srt_set_tlpktdrop(srs_srt_t srt_fd, bool tlpktdrop)
{
    SET_SRT_OPT(srt_fd, SRTO_TLPKTDROP, tlpktdrop);
    return srs_success;
}

srs_error_t srs_srt_set_latency(srs_srt_t srt_fd, int latency)
{
    SET_SRT_OPT(srt_fd, SRTO_LATENCY, latency);
    return srs_success;
}

srs_error_t srs_srt_set_rcv_latency(srs_srt_t srt_fd, int rcv_latency)
{
    SET_SRT_OPT(srt_fd, SRTO_RCVLATENCY, rcv_latency);
    return srs_success;
}

srs_error_t srs_srt_set_peer_latency(srs_srt_t srt_fd, int peer_latency)
{
    SET_SRT_OPT(srt_fd, SRTO_PEERLATENCY, peer_latency);
    return srs_success;
}

srs_error_t srs_srt_set_streamid(srs_srt_t srt_fd, const std::string& streamid)
{
    SET_SRT_OPT_STR(srt_fd, SRTO_STREAMID, streamid.data(), streamid.size());
    return srs_success;
}

srs_error_t srs_srt_set_passphrase(srs_srt_t srt_fd, const std::string& passphrase)
{
    SET_SRT_OPT_STR(srt_fd, SRTO_PASSPHRASE, passphrase.data(), passphrase.size());
    return srs_success;
}

srs_error_t srs_srt_set_pbkeylen(srs_srt_t srt_fd, int pbkeylen)
{
    SET_SRT_OPT(srt_fd, SRTO_PBKEYLEN, pbkeylen);
    return srs_success;
}

srs_error_t srs_srt_get_maxbw(srs_srt_t srt_fd, int64_t& maxbw)
{
    GET_SRT_OPT(srt_fd, SRTO_MAXBW, maxbw);
    return srs_success;
}

srs_error_t srs_srt_get_mss(srs_srt_t srt_fd, int& mss)
{
    GET_SRT_OPT(srt_fd, SRTO_MSS, mss);
    return srs_success;
}

srs_error_t srs_srt_get_payload_size(srs_srt_t srt_fd, int& payload_size)
{
    GET_SRT_OPT(srt_fd, SRTO_PAYLOADSIZE, payload_size);
    return srs_success;
}

srs_error_t srs_srt_get_connect_timeout(srs_srt_t srt_fd, int& timeout)
{
    GET_SRT_OPT(srt_fd, SRTO_CONNTIMEO, timeout);
    return srs_success;
}

srs_error_t srs_srt_get_peer_idle_timeout(srs_srt_t srt_fd, int& timeout)
{
    GET_SRT_OPT(srt_fd, SRTO_PEERIDLETIMEO, timeout);
    return srs_success;
}

srs_error_t srs_srt_get_tsbpdmode(srs_srt_t srt_fd, bool& tsbpdmode)
{
    GET_SRT_OPT(srt_fd, SRTO_TSBPDMODE, tsbpdmode);
    return srs_success;
}

srs_error_t srs_srt_get_sndbuf(srs_srt_t srt_fd, int& sndbuf)
{
    GET_SRT_OPT(srt_fd, SRTO_SNDBUF, sndbuf);
    return srs_success;
}

srs_error_t srs_srt_get_rcvbuf(srs_srt_t srt_fd, int& rcvbuf)
{
    GET_SRT_OPT(srt_fd, SRTO_RCVBUF, rcvbuf);
    return srs_success;
}

srs_error_t srs_srt_get_tlpktdrop(srs_srt_t srt_fd, bool& tlpktdrop)
{
    GET_SRT_OPT(srt_fd, SRTO_TLPKTDROP, tlpktdrop);
    return srs_success;
}

srs_error_t srs_srt_get_latency(srs_srt_t srt_fd, int& latency)
{
    GET_SRT_OPT(srt_fd, SRTO_LATENCY, latency);
    return srs_success;
}

srs_error_t srs_srt_get_rcv_latency(srs_srt_t srt_fd, int& rcv_latency)
{
    GET_SRT_OPT(srt_fd, SRTO_RCVLATENCY, rcv_latency);
    return srs_success;
}

srs_error_t srs_srt_get_peer_latency(srs_srt_t srt_fd, int& peer_latency)
{
    GET_SRT_OPT(srt_fd, SRTO_PEERLATENCY, peer_latency);
    return srs_success;
}

srs_error_t srs_srt_get_streamid(srs_srt_t srt_fd, std::string& streamid)
{
    srs_error_t err = srs_success;

    if ((err = do_srs_srt_get_streamid(srt_fd, streamid)) != srs_success) {
        return srs_error_wrap(err, "srt get streamid");
    }

    return err;
}

srs_error_t srs_srt_get_local_ip_port(srs_srt_t srt_fd, std::string& ip, int& port)
{
    srs_error_t err = srs_success;

    // discovery client information
    sockaddr_storage addr;
    int addrlen = sizeof(addr);
    if (srt_getsockname(srt_fd, (sockaddr*)&addr, &addrlen) == -1) {
        return srs_error_new(ERROR_SRT_SOCKOPT, "srt_getsockname");
    }

    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo((const sockaddr*)&addr, addrlen, h, nbh,NULL, 0, NI_NUMERICHOST);
    if (r0) {
        return srs_error_new(ERROR_SRT_SOCKOPT, "getnameinfo");
    }

    switch(addr.ss_family) {
        case AF_INET:
            port = ntohs(((sockaddr_in*)&addr)->sin_port);
         break;
        case AF_INET6:
            port = ntohs(((sockaddr_in6*)&addr)->sin6_port);
         break;
    }

    ip.assign(saddr);
    return err;
}

srs_error_t srs_srt_get_remote_ip_port(srs_srt_t srt_fd, std::string& ip, int& port)
{
    srs_error_t err = srs_success;

    // discovery client information
    sockaddr_storage addr;
    int addrlen = sizeof(addr);
    if (srt_getpeername(srt_fd, (sockaddr*)&addr, &addrlen) == -1) {
        return srs_error_new(ERROR_SRT_SOCKOPT, "srt_getpeername");
    }

    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo((const sockaddr*)&addr, addrlen, h, nbh,NULL, 0, NI_NUMERICHOST);
    if (r0) {
        return srs_error_new(ERROR_SRT_SOCKOPT, "getnameinfo");
    }

    switch(addr.ss_family) {
        case AF_INET:
            port = ntohs(((sockaddr_in*)&addr)->sin_port);
         break;
        case AF_INET6:
            port = ntohs(((sockaddr_in6*)&addr)->sin6_port);
         break;
    }

    ip.assign(saddr);
    return err;
}

SrsSrtStat::SrsSrtStat()
{
    stat_ = new SRT_TRACEBSTATS();
}

SrsSrtStat::~SrsSrtStat()
{
    SRT_TRACEBSTATS* p = (SRT_TRACEBSTATS*)stat_;
    srs_freep(p);
}

int64_t SrsSrtStat::pktRecv()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktRecv;
}

int SrsSrtStat::pktRcvLoss()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktRcvLoss;
}

int SrsSrtStat::pktRcvRetrans()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktRcvRetrans;
}

int SrsSrtStat::pktRcvDrop()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktRcvDrop;
}

int64_t SrsSrtStat::pktSent()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktSent;
}

int SrsSrtStat::pktSndLoss()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktSndLoss;
}

int SrsSrtStat::pktRetrans()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktRetrans;
}

int SrsSrtStat::pktSndDrop()
{
    return ((SRT_TRACEBSTATS*)stat_)->pktSndDrop;
}

srs_error_t SrsSrtStat::fetch(srs_srt_t srt_fd, bool clear)
{
    srs_error_t err = srs_success;

    int r0 = srt_bstats(srt_fd, (SRT_TRACEBSTATS*)stat_, clear);
    if (r0) {
        return srs_error_new(ERROR_SRT_STATS, "srt_bstats r0=%d", r0);
    }

    return err;
}

class SrsSrtPoller : public ISrsSrtPoller
{
public:
    SrsSrtPoller();
    virtual ~SrsSrtPoller();
public:
    srs_error_t initialize();
    srs_error_t add_socket(SrsSrtSocket* srt_skt);
    srs_error_t mod_socket(SrsSrtSocket* srt_skt);
    srs_error_t del_socket(SrsSrtSocket* srt_skt);
    srs_error_t wait(int timeout_ms, int* pn_fds);
public:
    virtual int size();
private:
    // Find SrsSrtSocket* context by srs_srt_t.
    std::map<srs_srt_t, SrsSrtSocket*> fd_sockets_;
    int srt_epoller_fd_;
    std::vector<SRT_EPOLL_EVENT> events_;
};

SrsSrtPoller::SrsSrtPoller()
{
    srt_epoller_fd_ = -1;
}

SrsSrtPoller::~SrsSrtPoller()
{
    if (srt_epoller_fd_ > 0) {
        srt_epoll_release(srt_epoller_fd_);
    }
}

srs_error_t SrsSrtPoller::initialize()
{
    srs_error_t err = srs_success;

    srt_epoller_fd_ = srt_epoll_create();
    events_.resize(1024);

    // Enable srt empty poller, avoid warning.
    srt_epoll_set(srt_epoller_fd_, SRT_EPOLL_ENABLE_EMPTY);

    return err;
}

srs_error_t SrsSrtPoller::add_socket(SrsSrtSocket* srt_skt)
{
    srs_error_t err = srs_success;

    int events = srt_skt->events();
    srs_srt_t srtfd = srt_skt->fd();

    int ret = srt_epoll_add_usock(srt_epoller_fd_, srtfd, &events);

    srs_info("srt poller %d add srt socket %d, events=%d", srt_epoller_fd_, srtfd, events);
    if (ret == SRT_ERROR) {
        return srs_error_new(ERROR_SRT_EPOLL, "srt epoll add socket=%lu failed, err=%s", srtfd, srt_getlasterror_str());
    }

    // record srtfd to SrsSrtSocket*
    fd_sockets_[srtfd] = srt_skt;

    return err;
}

srs_error_t SrsSrtPoller::del_socket(SrsSrtSocket* srt_skt)
{
    srs_error_t err = srs_success;

    srs_srt_t srtfd = srt_skt->fd();

    int ret = srt_epoll_remove_usock(srt_epoller_fd_, srtfd);
    srs_info("srt poller %d remove srt socket %d", srt_epoller_fd_, srtfd);
    if (ret == SRT_ERROR) {
        return srs_error_new(ERROR_SRT_EPOLL, "srt epoll remove socket=%lu failed, err=%s", srtfd, srt_getlasterror_str());
    }

    fd_sockets_.erase(srtfd);

    return err;
}

srs_error_t SrsSrtPoller::wait(int timeout_ms, int* pn_fds)
{
    srs_error_t err = srs_success;

    // wait srt event fired, will timeout after `timeout_ms` milliseconds.
    int ret = srt_epoll_uwait(srt_epoller_fd_, events_.data(), events_.size(), timeout_ms);
    *pn_fds = ret;

    if (ret < 0) {
        return srs_error_new(ERROR_SRT_EPOLL, "srt_epoll_uwait, ret=%d, err=%s", ret, srt_getlasterror_str());
    }

    for (int i = 0; i < ret; ++i) {
        SRT_EPOLL_EVENT event = events_[i];
        map<srs_srt_t, SrsSrtSocket*>::iterator iter = fd_sockets_.find(event.fd);
        if (iter == fd_sockets_.end()) {
            srs_assert(false);
        }

        SrsSrtSocket* srt_skt = iter->second;
        srs_assert(srt_skt != NULL);

        // notify error, don't notify read/write event.
        if (event.events & SRT_EPOLL_ERR) {
            srt_skt->notify_error();
        } else {
            if (event.events & SRT_EPOLL_IN) {
                srt_skt->notify_readable();
            }
            if (event.events & SRT_EPOLL_OUT) {
                srt_skt->notify_writeable();
            }
        }
    }

    return err;
}

int SrsSrtPoller::size()
{
    return (int)fd_sockets_.size();
}

srs_error_t SrsSrtPoller::mod_socket(SrsSrtSocket* srt_skt)
{
    srs_error_t err = srs_success;

    int events = srt_skt->events();
    srs_srt_t srtfd = srt_skt->fd();

    int ret = srt_epoll_update_usock(srt_epoller_fd_, srtfd, &events);
    srs_info("srt poller %d update srt socket %d, events=%d", srt_epoller_fd_, srtfd, events);

    if (ret == SRT_ERROR) {
        return srs_error_new(ERROR_SRT_EPOLL, "srt epoll update socket=%lu failed, err=%s", srtfd, srt_getlasterror_str());
    }

    return err;
}

ISrsSrtPoller::ISrsSrtPoller()
{
}

ISrsSrtPoller::~ISrsSrtPoller()
{
}

ISrsSrtPoller* srs_srt_poller_new()
{
    return new SrsSrtPoller();
}

SrsSrtSocket::SrsSrtSocket(ISrsSrtPoller* srt_poller, srs_srt_t srt_fd)
{
    srt_poller_ = srt_poller;
    srt_fd_ = srt_fd;
    has_error_ = 0;
    read_cond_ = srs_cond_new();
    write_cond_ = srs_cond_new();

    recv_timeout_ = 5 * SRS_UTIME_SECONDS;
    send_timeout_ = 5 * SRS_UTIME_SECONDS;

    recv_bytes_ = 0;
    send_bytes_ = 0;

    events_ = 0;
}

SrsSrtSocket::~SrsSrtSocket()
{
    srs_error_t err = srt_poller_->del_socket(this);
    if (err != srs_success) {
        srs_error("srt poller remove socket failed, err=%s", srs_error_desc(err).c_str());
        srs_error_reset(err);
    }

    srs_cond_destroy(read_cond_);
    srs_cond_destroy(write_cond_);

    srs_trace("close srt_fd=%d", srt_fd_);
    srt_close(srt_fd_);
}

srs_error_t SrsSrtSocket::connect(const string& ip, int port)
{
    srs_error_t err = srs_success;

    sockaddr_in inaddr;
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(port);
    // TODO: FIXME: inet_addr is deprecated
    inaddr.sin_addr.s_addr = inet_addr(ip.c_str());

    // @see https://github.com/Haivision/srt/blob/master/docs/API/API-functions.md#srt_connect
    int ret = srt_connect(srt_fd_, (const sockaddr*)&inaddr, sizeof(inaddr));
    if (ret != 0) {
        return srs_error_new(ERROR_SRT_IO, "srt_connect, err=%s", srt_getlasterror_str());
    }

    // Connect succeed, in async mode, means SRT API succeed and return directly,
    // and the connection is in progress, like tcp socket API connect errno EINPROGRESS,
    // and the SRT IO threads will do the real handshake step to finish srt connect.
    SRT_SOCKSTATUS srt_status = srt_getsockstate(srt_fd_);
    if (srt_status == SRTS_CONNECTED) {
        return err;
    }

    // Connect is in progress, wait until it finish or error.
    if ((err = wait_writeable()) != srs_success) {
        return srs_error_wrap(err, "wait writeable");
    }

    // Double check if connect is established.
    srt_status = srt_getsockstate(srt_fd_);
    if (srt_status != SRTS_CONNECTED) {
        return srs_error_new(ERROR_SRT_IO, "srt_connect, err=%s", srt_getlasterror_str());
    }

    return err;
}

srs_error_t SrsSrtSocket::accept(srs_srt_t* client_srt_fd)
{
    srs_error_t err = srs_success;

    while (true) {
        sockaddr_in inaddr;
        int addrlen = sizeof(inaddr);
        // @see https://github.com/Haivision/srt/blob/master/docs/API/API-functions.md#srt_accept
        srs_srt_t srt_fd = srt_accept(srt_fd_, (sockaddr*)&inaddr, &addrlen);

        // Accept ok, return with the SRT client fd.
        if (srt_fd != srs_srt_socket_invalid()) {
            *client_srt_fd = srt_fd;
            return err;
        }

        // Got something error, return immediately.
        if (srt_getlasterror(NULL) != SRT_EASYNCRCV) {
            return srs_error_new(ERROR_SRT_IO, "srt_accept, err=%s", srt_getlasterror_str());
        }

        // Accept would block, wait until new client connect or error.
        if ((err = wait_readable()) != srs_success) {
            return srs_error_wrap(err, "wait readable");
        }
    }

    return err;
}

srs_error_t SrsSrtSocket::recvmsg(void* buf, size_t size, ssize_t* nread)
{
    srs_error_t err = srs_success;

    while (true) {
        // @see https://github.com/Haivision/srt/blob/master/docs/API/API-functions.md#srt_recvmsg
        int ret = srt_recvmsg(srt_fd_, (char*)buf, size);

        // Receive message ok.
        if (ret >= 0) {
            recv_bytes_ += ret;
            *nread = ret;
            return err;
        }

        // Got something error, return immediately.
        if (srt_getlasterror(NULL) != SRT_EASYNCRCV) {
            return srs_error_new(ERROR_SRT_IO, "srt_recvmsg, err=%s", srt_getlasterror_str());
        }

        // Wait for the fd ready or error, switch to other coroutines.
        if ((err = wait_readable()) != srs_success) {
            return srs_error_wrap(err, "wait readable");
        }
    }

    return err;
}

srs_error_t SrsSrtSocket::sendmsg(void* buf, size_t size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    while (true) {
        // @see https://github.com/Haivision/srt/blob/master/docs/API/API-functions.md#srt_sendmsg
        int ret = srt_sendmsg(srt_fd_, (const char*)buf, size, -1, 1);

        // Send message ok.
        if (ret >= 0) {
            send_bytes_ += ret;
            *nwrite = ret;
            return err;
        }

        // Got something error, return immediately.
        if (srt_getlasterror(NULL) != SRT_EASYNCSND) {
            return srs_error_new(ERROR_SRT_IO, "srt_sendmsg, err=%s", srt_getlasterror_str());
        }

        // Wait for the fd ready or error, switch to other coroutines.
        if ((err = wait_writeable()) != srs_success) {
            return srs_error_wrap(err, "wait writeable");
        }
    }

    return err;
}

srs_error_t SrsSrtSocket::wait_readable()
{
    srs_error_t err = srs_success;

    // Check if error occured.
    if ((err = check_error()) != srs_success) {
        return srs_error_wrap(err, "has error");
    }

    // Subscribe in and error event
    if ((err = enable_read()) != srs_success) {
        return srs_error_wrap(err, "enable read");
    }

    // Wait event fired or timeout.
    int ret = srs_cond_timedwait(read_cond_, recv_timeout_);
    // TODO: FIXME: need to disable it?
    if ((err = disable_read()) != srs_success) {
        srs_freep(err);
    }

    if (ret != 0) {
        // Timeout and events no fired.
        if (errno == ETIME) {
            return srs_error_new(ERROR_SRT_TIMEOUT, "srt socket %d timeout", srt_fd_);
        }
        // Interrupted, maybe coroutine terminated.
        if (errno == EINTR) {
            return srs_error_new(ERROR_SRT_INTERRUPT, "srt socket %d interrupted", srt_fd_);
        }
        return srs_error_new(ERROR_SRT_IO, "srt socket %d wait read", srt_fd_);
    }

    // Check if we are notify with error event.
    if ((err = check_error()) != srs_success) {
        return srs_error_wrap(err, "has error");
    }

    return err;
}

srs_error_t SrsSrtSocket::wait_writeable()
{
    srs_error_t err = srs_success;

    if ((err = check_error()) != srs_success) {
        return srs_error_wrap(err, "has error");
    }

    if ((err = enable_write()) != srs_success) {
        return srs_error_wrap(err, "enable write");
    }

    int ret = srs_cond_timedwait(write_cond_, send_timeout_);
    if ((err = disable_write()) != srs_success) {
        srs_freep(err);
    }

    if (ret != 0) {
        if (errno == ETIME) {
            return srs_error_new(ERROR_SRT_TIMEOUT, "srt socket %d timeout", srt_fd_);
        }
        if (errno == EINTR) {
            return srs_error_new(ERROR_SRT_INTERRUPT, "srt socket %d interrupted", srt_fd_);
        }
        return srs_error_new(ERROR_SRT_IO, "srt socket %d wait write", srt_fd_);
    }

    if ((err = check_error()) != srs_success) {
        return srs_error_wrap(err, "has error");
    }

    return err;
}

void SrsSrtSocket::notify_readable()
{
    srs_cond_signal(read_cond_);
}

void SrsSrtSocket::notify_writeable()
{
    srs_cond_signal(write_cond_);
}

void SrsSrtSocket::notify_error()
{
    // mark error, and check when read/write
    has_error_ = true;
    srs_cond_signal(read_cond_);
    srs_cond_signal(write_cond_);
}

srs_error_t SrsSrtSocket::enable_read()
{
    return enable_event(SRT_EPOLL_IN | SRT_EPOLL_ERR);
}

srs_error_t SrsSrtSocket::disable_read()
{
    return disable_event(SRT_EPOLL_IN);
}

srs_error_t SrsSrtSocket::enable_write()
{
    return enable_event(SRT_EPOLL_OUT | SRT_EPOLL_ERR);
}

srs_error_t SrsSrtSocket::disable_write()
{
    return disable_event(SRT_EPOLL_OUT);
}

srs_error_t SrsSrtSocket::enable_event(int event)
{
    srs_error_t err = srs_success;

    // Event has been subscribed.
    if ((events_ & event) == event) {
        return err;
    }

    int old_events = events_;
    events_ |= event;

    if (old_events == 0) {
        err = srt_poller_->add_socket(this);
    } else {
        err = srt_poller_->mod_socket(this);
    }

    return err;
}

srs_error_t SrsSrtSocket::disable_event(int event)
{
    srs_error_t err = srs_success;

    // Event has been unsubscribed.
    if ((events_ & event) == 0) {
        return err;
    }

    events_ &= (~event);

    if (events_ == 0) {
        err = srt_poller_->del_socket(this);
    } else {
        err = srt_poller_->mod_socket(this);
    }

    return err;
}

srs_error_t SrsSrtSocket::check_error()
{
    srs_error_t err = srs_success;

    if (has_error_) {
        SRT_SOCKSTATUS status = srt_getsockstate(srt_fd_);
        return srs_error_new(ERROR_SRT_IO, "error occured, socket status=%s", srt_sockstatus_to_str(status).c_str());
    }

    return err;
}

