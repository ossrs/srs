//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_LISTENER_HPP
#define SRS_APP_LISTENER_HPP

#include <srs_core.hpp>


#include <map>
#include <string>
#include <vector>

#include <srs_app_st.hpp>

struct sockaddr;

class SrsBuffer;
class SrsUdpMuxSocket;
class ISrsListener;
class ISrsAppFactory;
class ISrsUdpMuxSocket;

class ISrsUdpHandler
{
public:
    ISrsUdpHandler();
    virtual ~ISrsUdpHandler();

public:
    // The buf is the listener's own buffer, reused for the next packet, so copy the bytes to keep them.
    virtual srs_error_t on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf) = 0;
};

// TODO: FIXME: Merge with ISrsUdpHandler
class ISrsUdpMuxHandler
{
public:
    ISrsUdpMuxHandler();
    virtual ~ISrsUdpMuxHandler();

public:
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt) = 0;
};

class ISrsListener
{
public:
    ISrsListener();
    virtual ~ISrsListener();

public:
    virtual srs_error_t listen() = 0;
};

class ISrsIpListener : public ISrsListener
{
public:
    ISrsIpListener();
    virtual ~ISrsIpListener();

public:
    virtual ISrsListener *set_endpoint(const std::string &i, int p) = 0;
    virtual ISrsListener *set_label(const std::string &label) = 0;
    virtual void close() = 0;
};

class ISrsTcpHandler
{
public:
    ISrsTcpHandler();
    virtual ~ISrsTcpHandler();

public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd) = 0;
};

class SrsUdpListener : public ISrsCoroutineHandler, public ISrsIpListener
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *factory_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    std::string label_;
    srs_netfd_t lfd_;
    ISrsCoroutine *trd_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    char *buf_;
    int nb_buf_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    ISrsUdpHandler *handler_;
    std::string ip_;
    int port_;

public:
    SrsUdpListener(ISrsUdpHandler *h);
    virtual ~SrsUdpListener();

public:
    ISrsListener *set_label(const std::string &label);
    ISrsListener *set_endpoint(const std::string &i, int p);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual int fd();
    virtual srs_netfd_t stfd();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void set_socket_buffer();

public:
    virtual srs_error_t listen();
    void close();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_cycle();
};

class SrsTcpListener : public ISrsCoroutineHandler, public ISrsIpListener
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string label_;
    srs_netfd_t lfd_;
    ISrsCoroutine *trd_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTcpHandler *handler_;
    std::string ip_;
    int port_;

public:
    SrsTcpListener(ISrsTcpHandler *h);
    virtual ~SrsTcpListener();

public:
    ISrsListener *set_label(const std::string &label);
    ISrsListener *set_endpoint(const std::string &i, int p);
    ISrsListener *set_endpoint(const std::string &endpoint);
    int port();

public:
    virtual srs_error_t listen();
    void close();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_cycle();
};

class ISrsMultipleTcpListeners : public ISrsIpListener
{
public:
    ISrsMultipleTcpListeners();
    virtual ~ISrsMultipleTcpListeners();

public:
    virtual ISrsIpListener *add(const std::vector<std::string> &endpoints) = 0;
};

class SrsMultipleTcpListeners : public ISrsMultipleTcpListeners, public ISrsTcpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTcpHandler *handler_;
    std::vector<ISrsIpListener *> listeners_;

public:
    SrsMultipleTcpListeners(ISrsTcpHandler *h);
    virtual ~SrsMultipleTcpListeners();

public:
    ISrsListener *set_label(const std::string &label);
    ISrsListener *set_endpoint(const std::string &i, int p);
    virtual ISrsIpListener *add(const std::vector<std::string> &endpoints);

public:
    srs_error_t listen();
    void close();

public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);
};

class ISrsUdpMuxSocket
{
public:
    ISrsUdpMuxSocket();
    virtual ~ISrsUdpMuxSocket();

public:
    virtual srs_error_t sendto(void *data, int size, srs_utime_t timeout) = 0;
    virtual std::string get_peer_ip() const = 0;
    virtual int get_peer_port() const = 0;
    virtual std::string peer_id() = 0;
    virtual uint64_t fast_id() = 0;
    virtual ISrsUdpMuxSocket *copy_sendonly() = 0;
    virtual int recvfrom(srs_utime_t timeout) = 0;
    virtual char *data() = 0;
    virtual int size() = 0;
};

// TODO: FIXME: Rename it. Refine it for performance issue.
class SrsUdpMuxSocket : public ISrsUdpMuxSocket
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t nn_msgs_for_yield_;
    std::map<uint32_t, std::string> cache_;
    SrsBuffer *cache_buffer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *buf_;
    int nb_buf_;
    int nread_;
    srs_netfd_t lfd_;
    sockaddr_storage from_;
    int fromlen_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string peer_ip_;
    int peer_port_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string peer_id_;
    bool address_changed_;
    uint64_t fast_id_;

public:
    SrsUdpMuxSocket(srs_netfd_t fd);
    virtual ~SrsUdpMuxSocket();

public:
    int recvfrom(srs_utime_t timeout);
    srs_error_t sendto(void *data, int size, srs_utime_t timeout);
    srs_netfd_t stfd();
    sockaddr_in *peer_addr();
    socklen_t peer_addrlen();
    char *data();
    int size();
    std::string get_peer_ip() const;
    int get_peer_port() const;
    std::string peer_id();
    uint64_t fast_id();
    SrsBuffer *buffer();
    ISrsUdpMuxSocket *copy_sendonly();
};

class ISrsUdpMuxListener : public ISrsListener
{
public:
    ISrsUdpMuxListener();
    virtual ~ISrsUdpMuxListener();

public:
    virtual int fd() = 0;
};

class SrsUdpMuxListener : public ISrsUdpMuxListener, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *factory_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_netfd_t lfd_;
    ISrsCoroutine *trd_;
    SrsContextId cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *buf_;
    int nb_buf_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsUdpMuxHandler *handler_;
    std::string ip_;
    int port_;

public:
    SrsUdpMuxListener(ISrsUdpMuxHandler *h, std::string i, int p);
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsUdpMuxListener();

public:
    virtual int fd();
    virtual srs_netfd_t stfd();

public:
    virtual srs_error_t listen();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void set_socket_buffer();
};

#endif
