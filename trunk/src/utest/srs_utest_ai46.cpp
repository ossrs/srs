//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai46.hpp>

#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_manual_config.hpp>

#include <vector>
using namespace std;

// The server's SRT acceptor listening on loopback as `srt_server { listen }` makes it, with the listener and its accept
// coroutine from the real factory, handing each caller to a server that keeps the accepted sockets.
class SrtListenerUtestServer
{
public:
    MockSrsConfig conf_;
    MockSrtClientHandlerForAcceptor server_;
    SrsSrtAcceptor *acceptor_;
    int port_;

public:
    SrtListenerUtestServer()
    {
        acceptor_ = new SrsSrtAcceptor(&server_);
        port_ = 0;
    }
    virtual ~SrtListenerUtestServer()
    {
        // Stop accepting first, then close what the server was handed, as a server that quits does.
        srs_freep(acceptor_);
        for (int i = 0; i < (int)server_.fds_.size(); i++) {
            srs_srt_close(server_.fds_[i]);
        }
    }

public:
    srs_error_t listen(string ip)
    {
        srs_error_t err = srs_success;

        if ((err = srs_utest_free_port(SOCK_DGRAM, &port_)) != srs_success) {
            return srs_error_wrap(err, "port");
        }
        if ((err = conf_.mock_parse(_MIN_OK_CONF "srt_server { enabled on; listen 10080; }")) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        acceptor_->config_ = &conf_;

        return acceptor_->listen(ip, port_);
    }

    // Wait until the server was handed n callers, as long as a caller's handshake may take on loopback.
    void wait_for_callers(int n)
    {
        for (int i = 0; i < 300 && (int)server_.fds_.size() < n; i++) {
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        }
    }

    string streamid_of(int index)
    {
        string streamid;
        srs_error_t err = srs_srt_get_streamid(server_.fds_[index], streamid);
        srs_freep(err);
        return streamid;
    }
};

// An SRT caller such as ffmpeg or OBS, with its stream id, connecting to the server's SRT port.
class SrtListenerUtestCaller
{
public:
    SrsSrtSocket *skt_;

public:
    SrtListenerUtestCaller()
    {
        skt_ = NULL;
    }
    virtual ~SrtListenerUtestCaller()
    {
        srs_freep(skt_);
    }

public:
    srs_error_t connect(int port, string streamid)
    {
        srs_error_t err = srs_success;

        srs_srt_t fd = srs_srt_socket_invalid();
        if ((err = srs_srt_socket_with_default_option(&fd)) != srs_success) {
            return srs_error_wrap(err, "socket");
        }
        skt_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), fd);
        skt_->set_send_timeout(3 * SRS_UTIME_SECONDS);
        skt_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
        if ((err = srs_srt_set_streamid(fd, streamid)) != srs_success) {
            return srs_error_wrap(err, "streamid");
        }
        return skt_->connect("127.0.0.1", port);
    }
};

// State Threads returns NULL when it cannot map the stack of a new coroutine, such as when the process is out of
// memory or over its address space limit.
static void *srt_listener_utest_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// An encoder and then a player connect to the server's SRT port. The listener accepts each one and hands its socket to
// the server, which reads the caller's stream id from it to know the stream and whether it publishes or plays.
VOID TEST(AppSrtListenerTest, EncoderThenPlayerConnect_EachHandedToServer)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1"));

    SrtListenerUtestCaller encoder;
    HELPER_ASSERT_SUCCESS(encoder.connect(s.port_, "#!::r=live/livestream,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/livestream,m=publish", s.streamid_of(0).c_str());

    SrtListenerUtestCaller player;
    HELPER_ASSERT_SUCCESS(player.connect(s.port_, "#!::r=live/livestream,m=request"));
    s.wait_for_callers(2);
    ASSERT_EQ(2, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/livestream,m=request", s.streamid_of(1).c_str());

    // Each caller gets its own socket, apart from the listening one.
    EXPECT_NE(s.server_.fds_[0], s.server_.fds_[1]);
    EXPECT_NE(s.acceptor_->listener_->fd(), s.server_.fds_[0]);
    EXPECT_NE(s.acceptor_->listener_->fd(), s.server_.fds_[1]);
}

// The server refuses a caller, such as one over max_connections, and closes its socket. The listener goes on, so the
// next caller to the port is still accepted and handed to the server.
VOID TEST(AppSrtListenerTest, ServerRefusesCaller_NextCallerStillHanded)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1"));
    s.server_.accept_error_ = srs_error_new(ERROR_EXCEED_CONNECTIONS, "exceed max connections");

    SrtListenerUtestCaller refused;
    HELPER_ASSERT_SUCCESS(refused.connect(s.port_, "#!::r=live/refused,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_close(s.server_.fds_[0]);
    s.server_.fds_.clear();
    srs_freep(s.server_.accept_error_);

    SrtListenerUtestCaller next;
    HELPER_ASSERT_SUCCESS(next.connect(s.port_, "#!::r=live/next,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/next,m=publish", s.streamid_of(0).c_str());
}

// The config listens at an address the host does not have, such as a config moved from another machine
// (192.0.2.1 is a documentation address, never assigned). The bind fails, so the server fails to start with the
// error instead of running with no SRT port.
VOID TEST(AppSrtListenerTest, ListenAtAddressNotOnHost_ServerFailsToStart)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    err = s.listen("192.0.2.1");

    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("srs_srt_listen") != string::npos);
    srs_freep(err);

    // No accept loop is started for the port.
    SrsSrtListener *listener = dynamic_cast<SrsSrtListener *>(s.acceptor_->listener_);
    ASSERT_TRUE(listener != NULL);
    EXPECT_TRUE(NULL == listener->srt_skt_);
}

// No coroutine can be created for the listener's accept loop, as when the process is out of memory at startup. The
// server fails to start with the ST error instead of running with an SRT port nobody accepts on.
VOID TEST(AppSrtListenerTest, NoCoroutineForAcceptLoop_ServerFailsToStart)
{
    srs_error_t err;

    SrtListenerUtestServer s;

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_listener_utest_st_thread_create_out_of_memory;
    err = s.listen("127.0.0.1");
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("start coroutine") != string::npos);
    srs_freep(err);
}

// The server starts its SRT event loop, which polls every SRT socket, before it listens on any port. When no coroutine
// can be created for that loop, as when the process is out of memory at startup, the start fails with the ST error, so
// the server quits instead of running with SRT sockets nobody polls.
VOID TEST(AppSrtServerTest, NoCoroutineForEventLoop_ServerFailsToStart)
{
    srs_error_t err;

    SrsSrtEventLoop loop;
    HELPER_ASSERT_SUCCESS(loop.initialize());
    EXPECT_TRUE(loop.poller() != NULL);

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_listener_utest_st_thread_create_out_of_memory;
    err = loop.start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("start coroutine") != string::npos);
    srs_freep(err);
}
