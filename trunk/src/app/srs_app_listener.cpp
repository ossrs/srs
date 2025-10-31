//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_listener.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
using namespace std;

#include <srs_app_factory.hpp>
#include <srs_app_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>

SrsPps *_srs_pps_rpkts = NULL;
SrsPps *_srs_pps_addrs = NULL;
SrsPps *_srs_pps_fast_addrs = NULL;

SrsPps *_srs_pps_spkts = NULL;

// set the max packet size.
#define SRS_UDP_MAX_PACKET_SIZE 65535

// sleep in srs_utime_t for udp recv packet.
#define SrsUdpPacketRecvCycleInterval 0

ISrsUdpHandler::ISrsUdpHandler()
{
}

ISrsUdpHandler::~ISrsUdpHandler()
{
}

ISrsUdpMuxHandler::ISrsUdpMuxHandler()
{
}

ISrsUdpMuxHandler::~ISrsUdpMuxHandler()
{
}

ISrsListener::ISrsListener()
{
}

ISrsListener::~ISrsListener()
{
}

ISrsIpListener::ISrsIpListener()
{
}

ISrsIpListener::~ISrsIpListener()
{
}

ISrsTcpHandler::ISrsTcpHandler()
{
}

ISrsTcpHandler::~ISrsTcpHandler()
{
}

SrsUdpListener::SrsUdpListener(ISrsUdpHandler *h)
{
    handler_ = h;
    lfd_ = NULL;
    port_ = 0;
    label_ = "UDP";

    nb_buf_ = SRS_UDP_MAX_PACKET_SIZE;
    buf_ = new char[nb_buf_];

    trd_ = new SrsDummyCoroutine();

    factory_ = _srs_app_factory;
}

SrsUdpListener::~SrsUdpListener()
{
    srs_freep(trd_);
    srs_close_stfd(lfd_);
    srs_freepa(buf_);

    factory_ = NULL;
}

ISrsListener *SrsUdpListener::set_label(const std::string &label)
{
    label_ = label;
    return this;
}

ISrsListener *SrsUdpListener::set_endpoint(const std::string &i, int p)
{
    ip_ = i;
    port_ = p;
    return this;
}

int SrsUdpListener::fd()
{
    return srs_netfd_fileno(lfd_);
}

srs_netfd_t SrsUdpListener::stfd()
{
    return lfd_;
}

void SrsUdpListener::set_socket_buffer()
{
    int default_sndbuf = 0;
    // TODO: FIXME: Config it.
    int expect_sndbuf = 1024 * 1024 * 10; // 10M
    int actual_sndbuf = expect_sndbuf;
    int r0_sndbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_sndbuf);
        // TODO: FIXME: check err
        getsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void *)&default_sndbuf, &opt_len);

        if ((r0_sndbuf = setsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void *)&actual_sndbuf, sizeof(actual_sndbuf))) < 0) {
            srs_warn("set SO_SNDBUF failed, expect=%d, r0=%d", expect_sndbuf, r0_sndbuf);
        }

        opt_len = sizeof(actual_sndbuf);
        // TODO: FIXME: check err
        getsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void *)&actual_sndbuf, &opt_len);
    }

    int default_rcvbuf = 0;
    // TODO: FIXME: Config it.
    int expect_rcvbuf = 1024 * 1024 * 10; // 10M
    int actual_rcvbuf = expect_rcvbuf;
    int r0_rcvbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_rcvbuf);
        // TODO: FIXME: check err
        getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void *)&default_rcvbuf, &opt_len);

        if ((r0_rcvbuf = setsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void *)&actual_rcvbuf, sizeof(actual_rcvbuf))) < 0) {
            srs_warn("set SO_RCVBUF failed, expect=%d, r0=%d", expect_rcvbuf, r0_rcvbuf);
        }

        opt_len = sizeof(actual_rcvbuf);
        // TODO: FIXME: check err
        getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void *)&actual_rcvbuf, &opt_len);
    }

    srs_trace("UDP #%d LISTEN at %s:%d, SO_SNDBUF(default=%d, expect=%d, actual=%d, r0=%d), SO_RCVBUF(default=%d, expect=%d, actual=%d, r0=%d)",
              srs_netfd_fileno(lfd_), ip_.c_str(), port_, default_sndbuf, expect_sndbuf, actual_sndbuf, r0_sndbuf, default_rcvbuf, expect_rcvbuf, actual_rcvbuf, r0_rcvbuf);
}

srs_error_t SrsUdpListener::listen()
{
    srs_error_t err = srs_success;

    // Ignore if not configured.
    if (ip_.empty() || !port_)
        return err;

    srs_close_stfd(lfd_);
    if ((err = srs_udp_listen(ip_, port_, &lfd_)) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip_.c_str(), port_);
    }

    set_socket_buffer();

    srs_freep(trd_);
    trd_ = factory_->create_coroutine("udp", this, _srs_context->get_id());
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }

    return err;
}

void SrsUdpListener::close()
{
    trd_->stop();
}

srs_error_t SrsUdpListener::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "udp listener");
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("%s listener: Ignore error, %s", label_.c_str(), srs_error_desc(err).c_str());
            srs_freep(err);
        }

        if (SrsUdpPacketRecvCycleInterval > 0) {
            srs_usleep(SrsUdpPacketRecvCycleInterval);
        }
    }

    return err;
}

srs_error_t SrsUdpListener::do_cycle()
{
    srs_error_t err = srs_success;

    int nread = 0;
    sockaddr_storage from;
    int nb_from = sizeof(from);
    if ((nread = srs_recvfrom(lfd_, buf_, nb_buf_, (sockaddr *)&from, &nb_from, SRS_UTIME_NO_TIMEOUT)) <= 0) {
        return srs_error_new(ERROR_SOCKET_READ, "udp read, nread=%d", nread);
    }

    // Drop UDP health check packet of Aliyun SLB.
    //      Healthcheck udp check
    // @see https://help.aliyun.com/document_detail/27595.html
    if (nread == 21 && buf_[0] == 0x48 && buf_[1] == 0x65 && buf_[2] == 0x61 && buf_[3] == 0x6c && buf_[19] == 0x63 && buf_[20] == 0x6b) {
        return err;
    }

    if ((err = handler_->on_udp_packet((const sockaddr *)&from, nb_from, buf_, nread)) != srs_success) {
        return srs_error_wrap(err, "handle packet %d bytes", nread);
    }

    return err;
}

SrsTcpListener::SrsTcpListener(ISrsTcpHandler *h)
{
    handler_ = h;
    port_ = 0;
    lfd_ = NULL;
    label_ = "TCP";
    trd_ = new SrsDummyCoroutine();

    factory_ = _srs_app_factory;
}

SrsTcpListener::~SrsTcpListener()
{
    srs_freep(trd_);
    srs_close_stfd(lfd_);

    factory_ = NULL;
}

ISrsListener *SrsTcpListener::set_label(const std::string &label)
{
    label_ = label;
    return this;
}

ISrsListener *SrsTcpListener::set_endpoint(const std::string &i, int p)
{
    ip_ = i;
    port_ = p;
    return this;
}

ISrsListener *SrsTcpListener::set_endpoint(const std::string &endpoint)
{
    std::string ip;
    int port_;
    srs_net_split_for_listener(endpoint, ip, port_);
    return set_endpoint(ip, port_);
}

int SrsTcpListener::port()
{
    return port_;
}

srs_error_t SrsTcpListener::listen()
{
    srs_error_t err = srs_success;

    // Ignore if not configured.
    if (ip_.empty() || !port_)
        return err;

    srs_close_stfd(lfd_);
    if ((err = srs_tcp_listen(ip_, port_, &lfd_)) != srs_success) {
        return srs_error_wrap(err, "listen at %s:%d", ip_.c_str(), port_);
    }

    srs_freep(trd_);
    trd_ = factory_->create_coroutine("tcp", this, _srs_context->get_id());
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    int fd = srs_netfd_fileno(lfd_);
    srs_trace("%s listen at tcp://%s:%d, fd=%d", label_.c_str(), ip_.c_str(), port_, fd);

    return err;
}

void SrsTcpListener::close()
{
    trd_->stop();
    srs_close_stfd(lfd_);
}

srs_error_t SrsTcpListener::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "tcp listener");
        }

        if ((err = do_cycle()) != srs_success) {
            srs_warn("%s listener: Ignore error, %s", label_.c_str(), srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    return err;
}

srs_error_t SrsTcpListener::do_cycle()
{
    srs_error_t err = srs_success;

    srs_netfd_t fd = srs_accept(lfd_, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
    if (fd == NULL) {
        return srs_error_new(ERROR_SOCKET_ACCEPT, "accept at fd=%d", srs_netfd_fileno(lfd_));
    }

    if ((err = srs_fd_closeexec(srs_netfd_fileno(fd))) != srs_success) {
        return srs_error_wrap(err, "set closeexec");
    }

    if ((err = handler_->on_tcp_client(this, fd)) != srs_success) {
        return srs_error_wrap(err, "handle fd=%d", srs_netfd_fileno(fd));
    }

    return err;
}

SrsMultipleTcpListeners::SrsMultipleTcpListeners(ISrsTcpHandler *h)
{
    handler_ = h;

    factory_ = _srs_app_factory;
}

SrsMultipleTcpListeners::~SrsMultipleTcpListeners()
{
    for (vector<ISrsIpListener *>::iterator it = listeners_.begin(); it != listeners_.end(); ++it) {
        ISrsIpListener *l = *it;
        srs_freep(l);
    }

    factory_ = NULL;
}

ISrsListener *SrsMultipleTcpListeners::set_label(const std::string &label)
{
    for (vector<ISrsIpListener *>::iterator it = listeners_.begin(); it != listeners_.end(); ++it) {
        ISrsIpListener *l = *it;
        l->set_label(label);
    }

    return this;
}

ISrsListener *SrsMultipleTcpListeners::set_endpoint(const std::string &i, int p)
{
    for (vector<ISrsIpListener *>::iterator it = listeners_.begin(); it != listeners_.end(); ++it) {
        ISrsIpListener *l = *it;
        l->set_endpoint(i, p);
    }

    return this;
}

ISrsIpListener *SrsMultipleTcpListeners::add(const std::vector<std::string> &endpoints)
{
    for (int i = 0; i < (int)endpoints.size(); i++) {
        string ip;
        int port;
        srs_net_split_for_listener(endpoints[i], ip, port);

        ISrsIpListener *l = factory_->create_tcp_listener(this);
        l->set_endpoint(ip, port);
        listeners_.push_back(l);
    }

    return this;
}

srs_error_t SrsMultipleTcpListeners::listen()
{
    srs_error_t err = srs_success;

    for (vector<ISrsIpListener *>::iterator it = listeners_.begin(); it != listeners_.end(); ++it) {
        ISrsIpListener *l = *it;

        if ((err = l->listen()) != srs_success) {
            return srs_error_wrap(err, "listen");
        }
    }

    return err;
}

void SrsMultipleTcpListeners::close()
{
    for (vector<ISrsIpListener *>::iterator it = listeners_.begin(); it != listeners_.end(); ++it) {
        ISrsIpListener *l = *it;
        srs_freep(l);
    }
    listeners_.clear();
}

srs_error_t SrsMultipleTcpListeners::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    return handler_->on_tcp_client(this, stfd);
}

ISrsUdpMuxSocket::ISrsUdpMuxSocket()
{
}

ISrsUdpMuxSocket::~ISrsUdpMuxSocket()
{
}

SrsUdpMuxSocket::SrsUdpMuxSocket(srs_netfd_t fd)
{
    nn_msgs_for_yield_ = 0;
    nb_buf_ = SRS_UDP_MAX_PACKET_SIZE;
    buf_ = new char[nb_buf_];
    nread_ = 0;

    lfd_ = fd;

    fromlen_ = 0;
    peer_port_ = 0;

    fast_id_ = 0;
    address_changed_ = false;
    cache_buffer_ = new SrsBuffer(buf_, nb_buf_);
}

SrsUdpMuxSocket::~SrsUdpMuxSocket()
{
    srs_freepa(buf_);
    srs_freep(cache_buffer_);
}

int SrsUdpMuxSocket::recvfrom(srs_utime_t timeout)
{
    fromlen_ = sizeof(from_);
    nread_ = srs_recvfrom(lfd_, buf_, nb_buf_, (sockaddr *)&from_, &fromlen_, timeout);
    if (nread_ <= 0) {
        return nread_;
    }

    // Reset the fast cache buffer size.
    cache_buffer_->set_size(nread_);
    cache_buffer_->skip(-1 * cache_buffer_->pos());

    // Drop UDP health check packet of Aliyun SLB.
    //      Healthcheck udp check
    // @see https://help.aliyun.com/document_detail/27595.html
    if (nread_ == 21 && buf_[0] == 0x48 && buf_[1] == 0x65 && buf_[2] == 0x61 && buf_[3] == 0x6c && buf_[19] == 0x63 && buf_[20] == 0x6b) {
        return 0;
    }

    // Parse address from cache.
    if (from_.ss_family == AF_INET) {
        sockaddr_in *addr = (sockaddr_in *)&from_;
        fast_id_ = uint64_t(addr->sin_port) << 48 | uint64_t(addr->sin_addr.s_addr);
    }

    // We will regenerate the peer_ip, peer_port and peer_id.
    address_changed_ = true;

    // Update the stat.
    ++_srs_pps_rpkts->sugar_;

    return nread_;
}

srs_error_t SrsUdpMuxSocket::sendto(void *data, int size, srs_utime_t timeout)
{
    srs_error_t err = srs_success;

    ++_srs_pps_spkts->sugar_;

    int nb_write = srs_sendto(lfd_, data, size, (sockaddr *)&from_, fromlen_, timeout);

    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "sendto timeout %d ms", srsu2msi(timeout));
        }

        return srs_error_new(ERROR_SOCKET_WRITE, "sendto");
    }

    // Yield to another coroutines.
    // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777542162
    if (++nn_msgs_for_yield_ > 20) {
        nn_msgs_for_yield_ = 0;
        srs_thread_yield();
    }

    return err;
}

srs_netfd_t SrsUdpMuxSocket::stfd()
{
    return lfd_;
}

sockaddr_in *SrsUdpMuxSocket::peer_addr()
{
    return (sockaddr_in *)&from_;
}

socklen_t SrsUdpMuxSocket::peer_addrlen()
{
    return (socklen_t)fromlen_;
}

char *SrsUdpMuxSocket::data()
{
    return buf_;
}

int SrsUdpMuxSocket::size()
{
    return nread_;
}

std::string SrsUdpMuxSocket::get_peer_ip() const
{
    return peer_ip_;
}

int SrsUdpMuxSocket::get_peer_port() const
{
    return peer_port_;
}

std::string SrsUdpMuxSocket::peer_id()
{
    if (address_changed_) {
        address_changed_ = false;

        // Parse address from cache.
        bool parsed = false;
        if (from_.ss_family == AF_INET) {
            sockaddr_in *addr = (sockaddr_in *)&from_;

            // Load from fast cache, previous ip.
            std::map<uint32_t, string>::iterator it = cache_.find(addr->sin_addr.s_addr);
            if (it == cache_.end()) {
                peer_ip_ = inet_ntoa(addr->sin_addr);
                cache_[addr->sin_addr.s_addr] = peer_ip_;
            } else {
                peer_ip_ = it->second;
            }

            peer_port_ = ntohs(addr->sin_port);
            parsed = true;
        }

        if (!parsed) {
            // TODO: FIXME: Maybe we should not covert to string for each packet.
            char address_string[64];
            char port_string[16];
            if (getnameinfo((sockaddr *)&from_, fromlen_,
                            (char *)&address_string, sizeof(address_string),
                            (char *)&port_string, sizeof(port_string),
                            NI_NUMERICHOST | NI_NUMERICSERV)) {
                return "";
            }

            peer_ip_ = std::string(address_string);
            peer_port_ = atoi(port_string);
        }

        // Build the peer id, reserve 1 byte for the trailing '\0'.
        static char id_buf[128 + 1];
        int len = snprintf(id_buf, sizeof(id_buf), "%s:%d", peer_ip_.c_str(), peer_port_);
        if (len <= 0 || len >= (int)sizeof(id_buf)) {
            return "";
        }
        peer_id_ = string(id_buf, len);

        // Update the stat.
        ++_srs_pps_addrs->sugar_;
    }

    return peer_id_;
}

uint64_t SrsUdpMuxSocket::fast_id()
{
    ++_srs_pps_fast_addrs->sugar_;
    return fast_id_;
}

SrsBuffer *SrsUdpMuxSocket::buffer()
{
    return cache_buffer_;
}

// LCOV_EXCL_START
ISrsUdpMuxSocket *SrsUdpMuxSocket::copy_sendonly()
{
    SrsUdpMuxSocket *sendonly = new SrsUdpMuxSocket(lfd_);

    // Don't copy buffer
    srs_freepa(sendonly->buf_);
    sendonly->nb_buf_ = 0;
    sendonly->nread_ = 0;
    sendonly->lfd_ = lfd_;
    sendonly->from_ = from_;
    sendonly->fromlen_ = fromlen_;
    sendonly->peer_ip_ = peer_ip_;
    sendonly->peer_port_ = peer_port_;

    // Copy the fast id.
    sendonly->peer_id_ = peer_id_;
    sendonly->fast_id_ = fast_id_;
    sendonly->address_changed_ = address_changed_;

    return sendonly;
}
// LCOV_EXCL_STOP

SrsUdpMuxListener::SrsUdpMuxListener(ISrsUdpMuxHandler *h, std::string i, int p)
{
    handler_ = h;

    ip_ = i;
    port_ = p;
    lfd_ = NULL;

    nb_buf_ = SRS_UDP_MAX_PACKET_SIZE;
    buf_ = new char[nb_buf_];

    trd_ = new SrsDummyCoroutine();
    cid_ = _srs_context->generate_id();

    factory_ = _srs_app_factory;
}

SrsUdpMuxListener::~SrsUdpMuxListener()
{
    srs_freep(trd_);
    srs_close_stfd(lfd_);
    srs_freepa(buf_);

    factory_ = NULL;
}

int SrsUdpMuxListener::fd()
{
    return srs_netfd_fileno(lfd_);
}

srs_netfd_t SrsUdpMuxListener::stfd()
{
    return lfd_;
}

srs_error_t SrsUdpMuxListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = srs_udp_listen(ip_, port_, &lfd_)) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip_.c_str(), port_);
    }

    srs_freep(trd_);
    trd_ = factory_->create_coroutine("udp", this, cid_);

    // change stack size to 256K, fix crash when call some 3rd-part api.
    ((SrsSTCoroutine *)trd_)->set_stack_size(1 << 18);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start thread");
    }

    return err;
}

void SrsUdpMuxListener::set_socket_buffer()
{
    int default_sndbuf = 0;
    // TODO: FIXME: Config it.
    int expect_sndbuf = 1024 * 1024 * 10; // 10M
    int actual_sndbuf = expect_sndbuf;
    int r0_sndbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_sndbuf);
        getsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void *)&default_sndbuf, &opt_len);

        if ((r0_sndbuf = setsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void *)&actual_sndbuf, sizeof(actual_sndbuf))) < 0) {
            srs_warn("set SO_SNDBUF failed, expect=%d, r0=%d", expect_sndbuf, r0_sndbuf);
        }

        opt_len = sizeof(actual_sndbuf);
        getsockopt(fd(), SOL_SOCKET, SO_SNDBUF, (void *)&actual_sndbuf, &opt_len);
    }

    int default_rcvbuf = 0;
    // TODO: FIXME: Config it.
    int expect_rcvbuf = 1024 * 1024 * 10; // 10M
    int actual_rcvbuf = expect_rcvbuf;
    int r0_rcvbuf = 0;
    if (true) {
        socklen_t opt_len = sizeof(default_rcvbuf);
        getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void *)&default_rcvbuf, &opt_len);

        if ((r0_rcvbuf = setsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void *)&actual_rcvbuf, sizeof(actual_rcvbuf))) < 0) {
            srs_warn("set SO_RCVBUF failed, expect=%d, r0=%d", expect_rcvbuf, r0_rcvbuf);
        }

        opt_len = sizeof(actual_rcvbuf);
        getsockopt(fd(), SOL_SOCKET, SO_RCVBUF, (void *)&actual_rcvbuf, &opt_len);
    }

    srs_trace("UDP #%d LISTEN at %s:%d, SO_SNDBUF(default=%d, expect=%d, actual=%d, r0=%d), SO_RCVBUF(default=%d, expect=%d, actual=%d, r0=%d)",
              srs_netfd_fileno(lfd_), ip_.c_str(), port_, default_sndbuf, expect_sndbuf, actual_sndbuf, r0_sndbuf, default_rcvbuf, expect_rcvbuf, actual_rcvbuf, r0_rcvbuf);
}

srs_error_t SrsUdpMuxListener::cycle()
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsPithyPrint> pprint(SrsPithyPrint::create_rtc_recv(srs_netfd_fileno(lfd_)));

    uint64_t nn_msgs = 0;
    uint64_t nn_msgs_stage = 0;
    uint64_t nn_msgs_last = 0;
    uint64_t nn_loop = 0;
    srs_utime_t time_last = srs_time_now_cached();

    SrsUniquePtr<SrsErrorPithyPrint> pp_pkt_handler_err(new SrsErrorPithyPrint());

    set_socket_buffer();

    // Because we have to decrypt the cipher of received packet payload,
    // and the size is not determined, so we think there is at least one copy,
    // and we can reuse the plaintext h264/opus with players when got plaintext.
    SrsUniquePtr<SrsUdpMuxSocket> skt_ptr(new SrsUdpMuxSocket(lfd_));
    ISrsUdpMuxSocket *skt = skt_ptr.get();

    // How many messages to run a yield.
    uint32_t nn_msgs_for_yield = 0;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "udp listener");
        }

        nn_loop++;

        int nread = skt->recvfrom(SRS_UTIME_NO_TIMEOUT);
        if (nread <= 0) {
            if (nread < 0) {
                srs_warn("udp recv error nn=%d", nread);
            }
            // remux udp never return
            continue;
        }

        nn_msgs++;
        nn_msgs_stage++;

        // Handle the UDP packet.
        err = handler_->on_udp_packet(skt);

        // Use pithy print to show more smart information.
        if (err != srs_success) {
            uint32_t nn = 0;
            if (pp_pkt_handler_err->can_print(err, &nn)) {
                // For performance, only restore context when output log.
                _srs_context->set_id(cid_);

                // Append more information.
                err = srs_error_wrap(err, "size=%u, data=[%s]", skt->size(), srs_strings_dumps_hex(skt->data(), skt->size(), 8).c_str());
                srs_warn("handle udp pkt, count=%u/%u, err: %s", pp_pkt_handler_err->nn_count_, nn, srs_error_desc(err).c_str());
            }
            srs_freep(err);
        }

        pprint->elapse();
        if (pprint->can_print()) {
            // LCOV_EXCL_START

            // For performance, only restore context when output log.
            _srs_context->set_id(cid_);

            int pps_average = 0;
            int pps_last = 0;
            if (true) {
                if (srs_time_now_cached() > srs_time_since_startup()) {
                    pps_average = (int)(nn_msgs * SRS_UTIME_SECONDS / (srs_time_now_cached() - srs_time_since_startup()));
                }
                if (srs_time_now_cached() > time_last) {
                    pps_last = (int)((nn_msgs - nn_msgs_last) * SRS_UTIME_SECONDS / (srs_time_now_cached() - time_last));
                }
            }

            string pps_unit = "";
            if (pps_last > 10000 || pps_average > 10000) {
                pps_unit = "(w)";
                pps_last /= 10000;
                pps_average /= 10000;
            } else if (pps_last > 1000 || pps_average > 1000) {
                pps_unit = "(k)";
                pps_last /= 1000;
                pps_average /= 1000;
            }

            srs_trace("<- RTC RECV #%d, udp %" PRId64 ", pps %d/%d%s, schedule %" PRId64,
                      srs_netfd_fileno(lfd_), nn_msgs_stage, pps_average, pps_last, pps_unit.c_str(), nn_loop);
            nn_msgs_last = nn_msgs;
            time_last = srs_time_now_cached();
            nn_loop = 0;
            nn_msgs_stage = 0;

            // LCOV_EXCL_STOP
        }

        if (SrsUdpPacketRecvCycleInterval > 0) {
            srs_usleep(SrsUdpPacketRecvCycleInterval);
        }

        // Yield to another coroutines.
        // @see https://github.com/ossrs/srs/issues/2194#issuecomment-777485531
        if (++nn_msgs_for_yield > 10) {
            nn_msgs_for_yield = 0;
            srs_thread_yield();
        }
    }

    return err;
}
