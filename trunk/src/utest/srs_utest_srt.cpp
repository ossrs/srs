//
// Copyright (c) 2013-2023 Winlin
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_srt.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_srt_utility.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_core_autofree.hpp>

#include <sstream>
#include <vector>
using namespace std;

#include <srt/srt.h>

extern SrsSrtEventLoop* _srt_eventloop;

// TODO: FIXME: set srt log handler.

// Test srt st service
VOID TEST(ServiceSrtPoller, SrtPollOperateSocket) 
{
    srs_error_t err = srs_success;

    ISrsSrtPoller* srt_poller = srs_srt_poller_new();
    HELPER_EXPECT_SUCCESS(srt_poller->initialize());

    srs_srt_t srt_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket(&srt_fd));
    EXPECT_TRUE(srt_fd > 0);

    SrsSrtSocket* srt_socket = new SrsSrtSocket(srt_poller, srt_fd);
    EXPECT_EQ(srt_socket->events(), 0);

    // Enable read, will subscribe SRT_EPOLL_IN and  SRT_EPOLL_ERR event in srt poller.
    HELPER_EXPECT_SUCCESS(srt_socket->enable_read());
    EXPECT_TRUE(srt_socket->events() & SRT_EPOLL_IN);
    EXPECT_TRUE(srt_socket->events() & SRT_EPOLL_ERR);

    // Enable read, will subscribe SRT_EPOLL_OUT and  SRT_EPOLL_ERR event in srt poller.
    HELPER_EXPECT_SUCCESS(srt_socket->enable_write());
    EXPECT_TRUE(srt_socket->events() & SRT_EPOLL_OUT);
    EXPECT_TRUE(srt_socket->events() & SRT_EPOLL_ERR);

    // Disable read, will unsubscribe SRT_EPOLL_IN event in srt poller.
    HELPER_EXPECT_SUCCESS(srt_socket->disable_read());
    EXPECT_FALSE(srt_socket->events() & SRT_EPOLL_IN);
    EXPECT_TRUE(srt_socket->events() & SRT_EPOLL_ERR);

    // Disable write, will unsubscribe SRT_EPOLL_OUT event in srt poller.
    HELPER_EXPECT_SUCCESS(srt_socket->disable_write());
    EXPECT_FALSE(srt_socket->events() & SRT_EPOLL_OUT);
    EXPECT_TRUE(srt_socket->events() & SRT_EPOLL_ERR);

    EXPECT_EQ(srt_poller->size(), 1);
    // Delete socket, will remove in srt poller.
    srs_freep(srt_socket);
    EXPECT_EQ(srt_poller->size(), 0);

    srs_freep(srt_poller);
}

VOID TEST(ServiceSrtPoller, SrtSetGetSocketOpt) 
{
    srs_error_t err = srs_success;

    srs_srt_t srt_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket(&srt_fd));
    HELPER_EXPECT_SUCCESS(srs_srt_nonblock(srt_fd));

    int64_t maxbw = 20000;
    int mss = 1400;
    int payload_size = 1316;
    int connect_timeout = 5000;
    int peer_idle_timeout = 10000;
    bool tsbpdmode = false;
    int sndbuf = 2 * 1024 * 1024;
    int rcvbuf = 10 * 1024 * 1024;
    bool tlpktdrop = false;
    int latency = 0;
    int rcv_latency = 120;
    int peer_latency = 120;
    std::string streamid = "SRS_SRT";

    HELPER_EXPECT_SUCCESS(srs_srt_set_maxbw(srt_fd, maxbw));
    HELPER_EXPECT_SUCCESS(srs_srt_set_mss(srt_fd, mss));
    HELPER_EXPECT_SUCCESS(srs_srt_set_payload_size(srt_fd, payload_size));
    HELPER_EXPECT_SUCCESS(srs_srt_set_connect_timeout(srt_fd, connect_timeout));
    HELPER_EXPECT_SUCCESS(srs_srt_set_peer_idle_timeout(srt_fd, peer_idle_timeout));
    HELPER_EXPECT_SUCCESS(srs_srt_set_tsbpdmode(srt_fd, tsbpdmode));
    HELPER_EXPECT_SUCCESS(srs_srt_set_sndbuf(srt_fd, sndbuf));
    HELPER_EXPECT_SUCCESS(srs_srt_set_rcvbuf(srt_fd, rcvbuf));
    HELPER_EXPECT_SUCCESS(srs_srt_set_tlpktdrop(srt_fd, tlpktdrop));
    HELPER_EXPECT_SUCCESS(srs_srt_set_latency(srt_fd, latency));
    HELPER_EXPECT_SUCCESS(srs_srt_set_rcv_latency(srt_fd, rcv_latency));
    HELPER_EXPECT_SUCCESS(srs_srt_set_peer_latency(srt_fd, peer_latency));
    HELPER_EXPECT_SUCCESS(srs_srt_set_streamid(srt_fd, streamid));

    bool b;
    int i = 0;
    int64_t i64 = 0;
    std::string s;

    HELPER_EXPECT_SUCCESS(srs_srt_get_maxbw(srt_fd, i64));
    EXPECT_EQ(i64, maxbw);
    HELPER_EXPECT_SUCCESS(srs_srt_get_mss(srt_fd, i));
    EXPECT_EQ(i, mss);
    HELPER_EXPECT_SUCCESS(srs_srt_get_payload_size(srt_fd, i));
    EXPECT_EQ(i, payload_size);
    HELPER_EXPECT_SUCCESS(srs_srt_get_connect_timeout(srt_fd, i));
    EXPECT_EQ(i, connect_timeout);
    HELPER_EXPECT_SUCCESS(srs_srt_get_peer_idle_timeout(srt_fd, i));
    EXPECT_EQ(i, peer_idle_timeout);

    // Don't check b equal to option blow, because some opt will deterimated after srt handshake done or change when set.
    HELPER_EXPECT_SUCCESS(srs_srt_get_tsbpdmode(srt_fd, b));
    HELPER_EXPECT_SUCCESS(srs_srt_get_sndbuf(srt_fd, i));
    HELPER_EXPECT_SUCCESS(srs_srt_get_rcvbuf(srt_fd, i));
    HELPER_EXPECT_SUCCESS(srs_srt_get_tlpktdrop(srt_fd, b));
    HELPER_EXPECT_SUCCESS(srs_srt_get_latency(srt_fd, i));
    HELPER_EXPECT_SUCCESS(srs_srt_get_rcv_latency(srt_fd, i));
    HELPER_EXPECT_SUCCESS(srs_srt_get_peer_latency(srt_fd, i));

    HELPER_EXPECT_SUCCESS(srs_srt_get_streamid(srt_fd, s));
    EXPECT_EQ(s, streamid);
}

class MockSrtServer
{
public:
    SrsSrtSocket* srt_socket_;
    srs_srt_t srt_server_fd_;

    MockSrtServer() {
        srt_server_fd_ = srs_srt_socket_invalid();
        srt_socket_ = NULL;
    }

    srs_error_t create_socket() {
        srs_error_t err = srs_success;
        if ((err = srs_srt_socket_with_default_option(&srt_server_fd_)) != srs_success) {
            return srs_error_wrap(err, "create srt socket");
        }
        return err;
    }

    srs_srt_t fd() {
        return srt_server_fd_;
    }

    srs_error_t listen(std::string ip, int port) {
        srs_error_t err = srs_success;

        if ((err = srs_srt_listen(srt_server_fd_, ip, port)) != srs_success) {
            return srs_error_wrap(err, "srt listen");
        }

        srt_socket_ = new SrsSrtSocket(_srt_eventloop->poller(), srt_server_fd_);

        return err;
    }

    virtual ~MockSrtServer() {
        srs_freep(srt_socket_);
    }

    virtual srs_error_t accept(srs_srt_t* client_fd) {
        srs_error_t err = srs_success;

        if ((err = srt_socket_->accept(client_fd)) != srs_success) {
            return srs_error_wrap(err, "srt accept");
        }

        return err;
    }
};

VOID TEST(ServiceStSRTTest, ListenConnectAccept) 
{
    srs_error_t err = srs_success;

    std::string server_ip = "127.0.0.1";
    int server_port = 19000;

    MockSrtServer srt_server;
    HELPER_EXPECT_SUCCESS(srt_server.create_socket());
    HELPER_EXPECT_SUCCESS(srt_server.listen(server_ip, server_port));

    srs_srt_t srt_client_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket(&srt_client_fd));

    SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);
    SrsAutoFree(SrsSrtSocket, srt_client_socket);

    // No client connected, accept will timeout.
    srs_srt_t srt_fd = srs_srt_socket_invalid();
    // Make utest fast timeout.
    srt_server.srt_socket_->set_recv_timeout(50 * SRS_UTIME_MILLISECONDS);
    err = srt_server.accept(&srt_fd);
    EXPECT_EQ(srs_error_code(err), ERROR_SRT_TIMEOUT);
    EXPECT_EQ(srt_fd, srs_srt_socket_invalid());
    srs_freep(err);

    // Client connect to server
    HELPER_EXPECT_SUCCESS(srt_client_socket->connect(server_ip, server_port));

    // Server will accept one client.
    HELPER_EXPECT_SUCCESS(srt_server.accept(&srt_fd));
    EXPECT_NE(srt_fd, srs_srt_socket_invalid());
}

VOID TEST(ServiceStSRTTest, ConnectTimeout) 
{
    srs_error_t err = srs_success;

    srs_srt_t srt_client_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
    SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);

    srt_client_socket->set_send_timeout(50 * SRS_UTIME_MILLISECONDS);
    // Client connect to server which is no listening.
    HELPER_EXPECT_FAILED(srt_client_socket->connect("127.0.0.1", 9099));
}

VOID TEST(ServiceStSRTTest, ConnectWithStreamid) 
{
    srs_error_t err = srs_success;

    std::string server_ip = "127.0.0.1";
    int server_port = 19000;

    MockSrtServer srt_server;
    HELPER_EXPECT_SUCCESS(srt_server.create_socket());
    HELPER_EXPECT_SUCCESS(srt_server.listen(server_ip, server_port));

    std::string streamid = "SRS_SRT_Streamid";
    srs_srt_t srt_client_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
    HELPER_EXPECT_SUCCESS(srs_srt_set_streamid(srt_client_fd, streamid));
    SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);

    HELPER_EXPECT_SUCCESS(srt_client_socket->connect(server_ip, server_port));

    srs_srt_t srt_server_accepted_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srt_server.accept(&srt_server_accepted_fd));
    EXPECT_NE(srt_server_accepted_fd, srs_srt_socket_invalid());
    std::string s;
    HELPER_EXPECT_SUCCESS(srs_srt_get_streamid(srt_server_accepted_fd, s));
    EXPECT_EQ(s, streamid);
}

VOID TEST(ServiceStSRTTest, ReadWrite) 
{
    srs_error_t err = srs_success;

    std::string server_ip = "127.0.0.1";
    int server_port = 19000;

    MockSrtServer srt_server;
    HELPER_EXPECT_SUCCESS(srt_server.create_socket());
    HELPER_EXPECT_SUCCESS(srt_server.listen(server_ip, server_port));

    srs_srt_t srt_client_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
    SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);

    // Client connect to server
    HELPER_EXPECT_SUCCESS(srt_client_socket->connect(server_ip, server_port));

    // Server will accept one client.
    srs_srt_t srt_server_accepted_fd = srs_srt_socket_invalid();
    HELPER_EXPECT_SUCCESS(srt_server.accept(&srt_server_accepted_fd));
    EXPECT_NE(srt_server_accepted_fd, srs_srt_socket_invalid());
    SrsSrtSocket* srt_server_accepted_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_server_accepted_fd);

    if (true) {
        std::string content = "Hello, SRS SRT!";

        // Client send msg to server.
        ssize_t nb_write = 0;
        HELPER_EXPECT_SUCCESS(srt_client_socket->sendmsg((char*)content.data(), content.size(), &nb_write));
        EXPECT_EQ((size_t)nb_write, content.size());

        // Server recv msg from client
        char buf[1500];
        ssize_t nb_read = 0;
        HELPER_EXPECT_SUCCESS(srt_server_accepted_socket->recvmsg(buf, sizeof(buf), &nb_read));
        EXPECT_EQ((size_t)nb_read, content.size());
        EXPECT_EQ(std::string(buf, nb_read), content);

        // Server echo msg back to client.
        HELPER_EXPECT_SUCCESS(srt_server_accepted_socket->sendmsg(buf, nb_read, &nb_write));
        EXPECT_EQ((size_t)nb_write, content.size());

        // Client recv echo msg from server.
        HELPER_EXPECT_SUCCESS(srt_client_socket->recvmsg(buf, sizeof(buf), &nb_read));
        EXPECT_EQ((size_t)nb_read, content.size());
        EXPECT_EQ(std::string(buf, nb_read), content);
    }

    if (true) {
        char buf[1500];
        ssize_t nb_read = 0;
        // Make socket fast timeout in ustet.
        srt_server_accepted_socket->set_recv_timeout(50 * SRS_UTIME_MILLISECONDS);
        // Recv msg from client, but client no send any msg, so will be timeout.
        err = srt_server_accepted_socket->recvmsg(buf, sizeof(buf), &nb_read);
        EXPECT_EQ(srs_error_code(err), ERROR_SRT_TIMEOUT);
        srs_freep(err);
    }
}

// Test srt server 
class MockSrtHandler : public ISrsSrtHandler
{
private:
    srs_srt_t srt_fd;
public:
	MockSrtHandler() {
        srt_fd = srs_srt_socket_invalid();
	}
	virtual ~MockSrtHandler() {
	}
public:
    virtual srs_error_t on_srt_client(srs_srt_t fd) {
        srt_fd = fd;
        return srs_success;
	}
};

VOID TEST(SrtServerTest, SrtListener) 
{
    srs_error_t err = srs_success;

    if (true) {
        MockSrtHandler h;
        SrsSrtListener srt_listener(&h, "127.0.0.1", 9000);
	    HELPER_EXPECT_SUCCESS(srt_listener.create_socket());
        HELPER_EXPECT_SUCCESS(srt_listener.listen());
		EXPECT_TRUE(srt_listener.fd() > 0);
    }
}

// Test srt app
VOID TEST(ProtocolSrtTest, SrtGetStreamInfoNormal) 
{
    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::r=live/livestream,key1=value1,key2=value2", mode, vhost, subpath));
        EXPECT_EQ(SrtModePull, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream?key1=value1&key2=value2", subpath.c_str());
    }

    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::h=host.com,r=live/livestream,key1=value1,key2=value2", mode, vhost, subpath));
        EXPECT_EQ(SrtModePull, mode);
        EXPECT_STREQ("host.com", vhost.c_str());
        EXPECT_STREQ("live/livestream?vhost=host.com&key1=value1&key2=value2", subpath.c_str());
    }
}

VOID TEST(ProtocolSrtTest, SrtGetStreamInfoMethod) 
{
    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::r=live/livestream,m=request", mode, vhost, subpath));
        EXPECT_EQ(SrtModePull, mode);
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }

    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::r=live/livestream,m=publish", mode, vhost, subpath));
        EXPECT_EQ(SrtModePush, mode);
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }
}

VOID TEST(ProtocolSrtTest, SrtGetStreamInfoCompatible) 
{
    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::h=live/livestream,m=request", mode, vhost, subpath));
        EXPECT_EQ(SrtModePull, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }

    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::h=live/livestream,m=publish", mode, vhost, subpath));
        EXPECT_EQ(SrtModePush, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream", subpath.c_str());
    }

    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::h=srs.srt.com.cn/live/livestream,m=request", mode, vhost, subpath));
        EXPECT_EQ(SrtModePull, mode);
        EXPECT_STREQ("srs.srt.com.cn", vhost.c_str());
        EXPECT_STREQ("live/livestream?vhost=srs.srt.com.cn", subpath.c_str());
    }

    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::h=srs.srt.com.cn/live/livestream,m=publish", mode, vhost, subpath));
        EXPECT_EQ(SrtModePush, mode);
        EXPECT_STREQ("srs.srt.com.cn", vhost.c_str());
        EXPECT_STREQ("live/livestream?vhost=srs.srt.com.cn", subpath.c_str());
    }

    if (true) {
        SrtMode mode; string vhost; string subpath;
        EXPECT_TRUE(srs_srt_streamid_info("#!::h=live/livestream?secret=d6d2be37,m=publish", mode, vhost, subpath));
        EXPECT_EQ(SrtModePush, mode);
        EXPECT_STREQ("", vhost.c_str());
        EXPECT_STREQ("live/livestream?secret=d6d2be37", subpath.c_str());
    }
}

VOID TEST(ProtocolSrtTest, SrtStreamIdToRequest)
{
    if (true) {
        SrtMode mode;
        SrsRequest req;
        EXPECT_TRUE(srs_srt_streamid_to_request("#!::r=live/livestream?key1=val1,key2=val2", mode, &req));
        EXPECT_EQ(mode, SrtModePull);
        EXPECT_STREQ(req.vhost.c_str(), srs_get_public_internet_address().c_str());
        EXPECT_STREQ(req.app.c_str(), "live");
        EXPECT_STREQ(req.stream.c_str(), "livestream");
        EXPECT_STREQ(req.param.c_str(), "key1=val1&key2=val2");
    }

    if (true) {
        SrtMode mode;
        SrsRequest req;
        EXPECT_TRUE(srs_srt_streamid_to_request("#!::h=srs.srt.com.cn,r=live/livestream?key1=val1,key2=val2", mode, &req));
        EXPECT_EQ(mode, SrtModePull);
        EXPECT_STREQ(req.vhost.c_str(), "srs.srt.com.cn");
        EXPECT_STREQ(req.app.c_str(), "live");
        EXPECT_STREQ(req.stream.c_str(), "livestream");
        EXPECT_STREQ(req.param.c_str(), "vhost=srs.srt.com.cn&key1=val1&key2=val2");
    }

    if (true) {
        SrtMode mode;
        SrsRequest req;
        EXPECT_TRUE(srs_srt_streamid_to_request("#!::h=live/livestream?key1=val1,key2=val2", mode, &req));
        EXPECT_EQ(mode, SrtModePull);
        EXPECT_STREQ(req.vhost.c_str(), srs_get_public_internet_address().c_str());
        EXPECT_STREQ(req.app.c_str(), "live");
        EXPECT_STREQ(req.stream.c_str(), "livestream");
        EXPECT_STREQ(req.param.c_str(), "key1=val1&key2=val2");
    }

    if (true) {
        SrtMode mode;
        SrsRequest req;
        EXPECT_TRUE(srs_srt_streamid_to_request("#!::h=srs.srt.com.cn/live/livestream?key1=val1,key2=val2", mode, &req));
        EXPECT_EQ(mode, SrtModePull);
        EXPECT_STREQ(req.vhost.c_str(), "srs.srt.com.cn");
        EXPECT_STREQ(req.app.c_str(), "live");
        EXPECT_STREQ(req.stream.c_str(), "livestream");
        EXPECT_STREQ(req.param.c_str(), "vhost=srs.srt.com.cn&key1=val1&key2=val2");
    }
}

VOID TEST(ServiceSRTTest, Encrypt) 
{
    srs_error_t err = srs_success;

    std::string server_ip = "127.0.0.1";
    int server_port = 19000;

    MockSrtServer srt_server;
    HELPER_EXPECT_SUCCESS(srt_server.create_socket());

    string passphrase = "srt_passphrase";
    HELPER_EXPECT_SUCCESS(srs_srt_set_passphrase(srt_server.fd(), passphrase));
    HELPER_EXPECT_SUCCESS(srt_server.listen(server_ip, server_port));

    std::string streamid = "SRS_SRT_Streamid";
    if (true) {
        srs_srt_t srt_client_fd = srs_srt_socket_invalid();
        HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
        HELPER_EXPECT_SUCCESS(srs_srt_set_streamid(srt_client_fd, streamid));
        SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);

        // SRT connect without passphrase, will reject.
        HELPER_EXPECT_FAILED(srt_client_socket->connect(server_ip, server_port));
    }

    if (true) {
        srs_srt_t srt_client_fd = srs_srt_socket_invalid();
        HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
        HELPER_EXPECT_SUCCESS(srs_srt_set_streamid(srt_client_fd, streamid));
        HELPER_EXPECT_SUCCESS(srs_srt_set_passphrase(srt_client_fd, "wrong_passphrase"));
        SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);

        // SRT connect with wrong passphrase, will reject.
        HELPER_EXPECT_FAILED(srt_client_socket->connect(server_ip, server_port));
    }

    if (true) {
        srs_srt_t srt_client_fd = srs_srt_socket_invalid();
        HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
        HELPER_EXPECT_SUCCESS(srs_srt_set_streamid(srt_client_fd, streamid));
        // Set correct passphrase.
        HELPER_EXPECT_SUCCESS(srs_srt_set_passphrase(srt_client_fd, passphrase));
        SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);
        HELPER_EXPECT_SUCCESS(srt_client_socket->connect(server_ip, server_port));

        srs_srt_t srt_server_accepted_fd = srs_srt_socket_invalid();
        HELPER_EXPECT_SUCCESS(srt_server.accept(&srt_server_accepted_fd));
        EXPECT_NE(srt_server_accepted_fd, srs_srt_socket_invalid());
        std::string s;
        HELPER_EXPECT_SUCCESS(srs_srt_get_streamid(srt_server_accepted_fd, s));
        EXPECT_EQ(s, streamid);
    }

    if (true) {
        int pbkeylens[4] = {0, 16, 24, 32};
        for (int i = 0; i < (int)(sizeof(pbkeylens) / sizeof(pbkeylens[0])); ++i) {
            srs_srt_t srt_client_fd = srs_srt_socket_invalid();
            HELPER_EXPECT_SUCCESS(srs_srt_socket_with_default_option(&srt_client_fd));
            HELPER_EXPECT_SUCCESS(srs_srt_set_streamid(srt_client_fd, streamid));
            // Set correct passphrase.
            HELPER_EXPECT_SUCCESS(srs_srt_set_passphrase(srt_client_fd, passphrase));
            // Set different pbkeylen.
            HELPER_EXPECT_SUCCESS(srs_srt_set_pbkeylen(srt_client_fd, pbkeylens[i]));
            SrsSrtSocket* srt_client_socket = new SrsSrtSocket(_srt_eventloop->poller(), srt_client_fd);
            HELPER_EXPECT_SUCCESS(srt_client_socket->connect(server_ip, server_port));

            srs_srt_t srt_server_accepted_fd = srs_srt_socket_invalid();
            HELPER_EXPECT_SUCCESS(srt_server.accept(&srt_server_accepted_fd));
            EXPECT_NE(srt_server_accepted_fd, srs_srt_socket_invalid());
            std::string s;
            HELPER_EXPECT_SUCCESS(srs_srt_get_streamid(srt_server_accepted_fd, s));
            EXPECT_EQ(s, streamid);
        }
    }
}

// TODO: FIXME: add mpegts conn test
// set srt option, recv srt client, get srt client opt and check.

