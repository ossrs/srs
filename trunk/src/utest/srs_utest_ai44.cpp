//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai44.hpp>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai38.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_service.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

using namespace std;

class IngestUtestFactory;

// The clock of the ingest loop. It does not sleep: it records each wait, and calls the test's hook at each 3s wait
// before a retry.
class IngestUtestTime : public ISrsTime
{
public:
    IngestUtestFactory *factory_;

public:
    IngestUtestTime(IngestUtestFactory *factory);
    virtual ~IngestUtestTime();

public:
    virtual void usleep(srs_utime_t duration);
};

// The server's factory for the ingester: each FFmpeg is the real one, reading the test config, on a process that
// forks nothing and keeps its arguments, and the clock does not sleep.
class IngestUtestFactory : public SrsAppFactory
{
public:
    ISrsAppConfig *config_;
    // The process of each FFmpeg made, in order; each is freed with its FFmpeg.
    vector<MockProcessForFFMPEG *> processes_;
    // Each wait of the ingest loop or the dispose, in order.
    vector<srs_utime_t> sleeps_;
    // Called at each 3s wait before a retry, with the retry's number from 1.
    void (*on_retry_)(IngestUtestFactory *factory, int retry);
    int retries_;
    SrsIngester *ingester_;

public:
    IngestUtestFactory()
    {
        config_ = NULL;
        on_retry_ = NULL;
        retries_ = 0;
        ingester_ = NULL;
    }
    virtual ~IngestUtestFactory()
    {
    }

public:
    virtual ISrsFFMPEG *create_ffmpeg(std::string ffmpeg_bin)
    {
        SrsFFMPEG *ffmpeg = new SrsFFMPEG(ffmpeg_bin);
        ffmpeg->config_ = config_;

        MockProcessForFFMPEG *process = new MockProcessForFFMPEG();
        srs_freep(ffmpeg->process_);
        ffmpeg->process_ = process;
        processes_.push_back(process);

        return ffmpeg;
    }
    virtual ISrsTime *create_time()
    {
        return new IngestUtestTime(this);
    }
};

IngestUtestTime::IngestUtestTime(IngestUtestFactory *factory)
{
    factory_ = factory;
}

IngestUtestTime::~IngestUtestTime()
{
}

void IngestUtestTime::usleep(srs_utime_t duration)
{
    factory_->sleeps_.push_back(duration);
    if (duration == 3 * SRS_UTIME_SECONDS && factory_->on_retry_) {
        factory_->on_retry_(factory_, ++factory_->retries_);
    }
}

// The server's ingester, set up from config text as the server sets it up, with its warnings kept.
class IngestUtestServer
{
public:
    MockSrsConfig conf_;
    IngestUtestFactory factory_;
    SrsIngester *ingester_;
    MockLogForSignal log_;
    ISrsLog *saved_log_;

public:
    IngestUtestServer()
    {
        ingester_ = new SrsIngester();
        ingester_->assemble();
        ingester_->config_ = &conf_;
        ingester_->app_factory_ = &factory_;
        factory_.config_ = &conf_;
        factory_.ingester_ = ingester_;

        saved_log_ = _srs_log;
        _srs_log = &log_;
    }
    ~IngestUtestServer()
    {
        srs_freep(ingester_);
        _srs_log = saved_log_;
    }

public:
    srs_error_t load(string text)
    {
        return conf_.mock_parse(string(_MIN_OK_CONF) + text);
    }
    // The warnings that hold this text.
    int warns(string text)
    {
        int n = 0;
        for (int i = 0; i < (int)log_.warns_.size(); i++) {
            if (log_.warns_[i].find(text) != string::npos) {
                n++;
            }
        }
        return n;
    }
};

// The engine of an ingest that copies the input, publishing to the stream of the ingest's vhost on this server.
#define INGEST_UTEST_COPY "engine { enabled off; output rtmp://127.0.0.1:[port]/live/livestream?vhost=[vhost]; }"
#define INGEST_UTEST_FFMPEG "ffmpeg ./objs/ffmpeg/bin/ffmpeg;"

// The command line FFmpeg would run, as one string.
static string ingest_utest_cli(MockProcessForFFMPEG *process)
{
    return srs_strings_join(process->argv_, " ");
}

// The operator forgot the ffmpeg line of the second ingest. The server refuses to start, naming it, and frees the
// FFmpeg it already made for the first ingest: no FFmpeg is forked, and no ingest loop runs.
VOID TEST(AppIngestTest, IngestWithoutFfmpegBinary_ServerStartRefused)
{
    srs_error_t err;

    IngestUtestServer s;
    HELPER_ASSERT_SUCCESS(s.load("vhost v {"
                                 "    ingest a { enabled on; input { type file; url ./doc/source.flv; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " }"
                                 "    ingest b { enabled on; input { type stream; url udp://0.0.0.0:2222; } " INGEST_UTEST_COPY " }"
                                 "}"));

    err = s.ingester_->start();
    EXPECT_EQ(ERROR_ENCODER_PARSE, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);

    EXPECT_EQ(1, (int)s.factory_.processes_.size());
    EXPECT_TRUE(s.ingester_->ingesters_.empty());
    EXPECT_TRUE(dynamic_cast<SrsDummyCoroutine *>(s.ingester_->trd_) != NULL);
}

// A vhost turned off keeps its ingests off, and an ingest turned off is skipped: only the enabled ingest of the
// enabled vhost runs FFmpeg, which copies its input to the vhost's stream, as its engine is off.
VOID TEST(AppIngestTest, DisabledVhostOrIngest_OnlyEnabledIngestRuns)
{
    srs_error_t err;

    IngestUtestServer s;
    HELPER_ASSERT_SUCCESS(s.load("vhost off { enabled off;"
                                 "    ingest a { enabled on; input { type file; url ./doc/source.flv; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " }"
                                 "}"
                                 "vhost v {"
                                 "    ingest b { enabled off; input { type file; url ./doc/source.flv; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " }"
                                 "    ingest c { enabled on; input { type stream; url udp://0.0.0.0:2222; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " }"
                                 "}"));

    HELPER_ASSERT_SUCCESS(s.ingester_->parse());
    ASSERT_EQ(1, (int)s.ingester_->ingesters_.size());
    EXPECT_EQ("v/c", s.ingester_->ingesters_.at(0)->uri());

    ASSERT_EQ(1, (int)s.factory_.processes_.size());
    HELPER_ASSERT_SUCCESS(s.ingester_->ingesters_.at(0)->start());
    MockProcessForFFMPEG *process = s.factory_.processes_.at(0);
    EXPECT_EQ("./objs/ffmpeg/bin/ffmpeg", process->binary_);
    string cli = ingest_utest_cli(process);
    EXPECT_NE(string::npos, cli.find("-re -i udp://0.0.0.0:2222 -vcodec copy -acodec copy -f flv -y rtmp://127.0.0.1:1935/live/livestream?vhost=v"))
        << cli;
}

// An ingest of a file names no url, or asks for a device, which the server does not support: the server refuses to
// start, naming the ingest, and runs no FFmpeg.
VOID TEST(AppIngestTest, IngestFileWithoutUrlOrDevice_ServerStartRefused)
{
    srs_error_t err;

    struct {
        const char *input;
        int code;
    } cases[] = {
        {"type file;", ERROR_ENCODER_NO_INPUT},
        {"type device; url /dev/video0;", ERROR_ENCODER_INPUT_TYPE},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        IngestUtestServer s;
        HELPER_ASSERT_SUCCESS(s.load(string("vhost v { ingest cam { enabled on; input { ") + cases[i].input + " } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " } }"));

        err = s.ingester_->start();
        EXPECT_EQ(cases[i].code, srs_error_code(err)) << cases[i].input << ", " << srs_error_desc(err);
        EXPECT_NE(string::npos, srs_error_desc(err).find("ingest=cam")) << srs_error_desc(err);
        srs_freep(err);

        EXPECT_TRUE(s.ingester_->ingesters_.empty()) << cases[i].input;
        ASSERT_EQ(1, (int)s.factory_.processes_.size());
    }
}

// An ingest engine re-encodes video with libx264 but names no vprofile: the engine is refused when the server
// starts, as an encoder's would be at publish, and no FFmpeg is kept.
VOID TEST(AppIngestTest, IngestEngineWithoutVprofile_ServerStartRefused)
{
    srs_error_t err;

    IngestUtestServer s;
    HELPER_ASSERT_SUCCESS(s.load("vhost v { ingest i { enabled on; input { type file; url ./doc/source.flv; } " INGEST_UTEST_FFMPEG
                                 "    engine hd { enabled on; vcodec libx264; vbitrate 800; vpreset medium; acodec libfdk_aac; abitrate 64;"
                                 "        output rtmp://127.0.0.1:[port]/live/livestream_hd; }"
                                 "} }"));

    err = s.ingester_->start();
    EXPECT_EQ(ERROR_ENCODER_VPROFILE, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);

    EXPECT_TRUE(s.ingester_->ingesters_.empty());
    EXPECT_TRUE(dynamic_cast<SrsDummyCoroutine *>(s.ingester_->trd_) != NULL);
}

// State Threads cannot create a coroutine: the process is out of memory or over its address space limit.
static void *ingest_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// The server starts its ingests when no coroutine can be created for the ingest loop: the start fails with the ST
// error, so the server does not run on with ingests that never start, and no FFmpeg was forked.
VOID TEST(AppIngestTest, IngestLoopWithNoCoroutine_ServerStartRefused)
{
    srs_error_t err;

    IngestUtestServer s;
    HELPER_ASSERT_SUCCESS(s.load("vhost v { ingest i { enabled on; input { type file; url ./doc/source.flv; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " } }"));

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)ingest_st_thread_create_out_of_memory;
    err = s.ingester_->start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err)) << srs_error_desc(err);
    EXPECT_NE(string::npos, srs_error_desc(err).find("start coroutine")) << srs_error_desc(err);
    srs_freep(err);

    ASSERT_EQ(1, (int)s.factory_.processes_.size());
    EXPECT_EQ(0, s.factory_.processes_.at(0)->start_count_);
}

// At the first retry the fork works again; at the second the server is told to quit.
static void ingest_utest_fork_back_then_quit(IngestUtestFactory *factory, int retry)
{
    if (retry == 1) {
        srs_freep(factory->processes_.at(1)->start_error_);
    } else if (retry == 2) {
        // The server's dispose, which runs in the server's coroutine while the ingest loop waits.
        factory->ingester_->dispose();
    }
}

// The server runs two ingests, and the fork of the second FFmpeg fails once (the process limit is reached). The loop
// warns and tries again 3s later, when the first FFmpeg keeps running and the second is forked. When the server is
// told to quit, the dispose stops both FFmpegs, and the loop ends at its next wake-up instead of retrying.
VOID TEST(AppIngestTest, FfmpegForkFailsOnce_RetriedAfter3sUntilServerQuits)
{
    srs_error_t err;

    IngestUtestServer s;
    HELPER_ASSERT_SUCCESS(s.load("vhost v {"
                                 "    ingest a { enabled on; input { type file; url ./doc/source.flv; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " }"
                                 "    ingest b { enabled on; input { type stream; url udp://0.0.0.0:2222; } " INGEST_UTEST_FFMPEG INGEST_UTEST_COPY " }"
                                 "}"));
    s.factory_.on_retry_ = ingest_utest_fork_back_then_quit;

    HELPER_ASSERT_SUCCESS(s.ingester_->start());
    ASSERT_EQ(2, (int)s.factory_.processes_.size());
    MockProcessForFFMPEG *a = s.factory_.processes_.at(0);
    MockProcessForFFMPEG *b = s.factory_.processes_.at(1);
    b->start_error_ = srs_error_new(ERROR_ENCODER_FORK, "vfork process failed");

    // Let the ingest loop run; its clock does not sleep, so it runs until the dispose ends it.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_EQ(1, s.warns("Ingester: Ignore error"));
    EXPECT_EQ(1, s.warns("vfork process failed"));

    EXPECT_TRUE(a->started_);
    EXPECT_EQ(1, a->start_count_);
    EXPECT_EQ(2, a->cycle_count_);
    EXPECT_TRUE(b->started_);
    EXPECT_EQ(2, b->start_count_);
    EXPECT_EQ(1, b->cycle_count_);

    EXPECT_EQ(1, a->fast_stop_count_);
    EXPECT_EQ(1, a->fast_kill_count_);
    EXPECT_EQ(1, b->fast_stop_count_);
    EXPECT_EQ(1, b->fast_kill_count_);

    ASSERT_EQ(3, (int)s.factory_.sleeps_.size());
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, s.factory_.sleeps_.at(0));
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, s.factory_.sleeps_.at(1));
    EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, s.factory_.sleeps_.at(2));

    // The loop ended on its own, with no error.
    SrsSTCoroutine *trd = dynamic_cast<SrsSTCoroutine *>(s.ingester_->trd_);
    ASSERT_TRUE(trd != NULL);
    EXPECT_TRUE(trd->impl_->cycle_done_);
    HELPER_EXPECT_SUCCESS(trd->pull());
}

extern srs_utime_t _srs_system_time_us_cache;
extern srs_utime_t _srs_system_time_startup_time;

// The health check a load balancer such as Aliyun SLB sends to a UDP port, 21 bytes.
static const string listener_utest_slb_check = "Healthcheck udp check";

// Holds the port of 127.0.0.1 as another program does, with a socket that sets no reuse option, so a listen of SRS,
// which sets SO_REUSEPORT, cannot share it. Returns the socket, or -1.
static int listener_utest_hold_port(int type, int port)
{
    int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::bind(fd, (sockaddr *)&addr, sizeof(addr)) != 0 || (type == SOCK_STREAM && ::listen(fd, 1) != 0)) {
        ::close(fd);
        return -1;
    }

    return fd;
}

// Two free ports of the socket type that differ, for two listens of one test.
static srs_error_t listener_utest_two_ports(int type, int *p1, int *p2)
{
    srs_error_t err = srs_success;

    if ((err = srs_utest_free_port(type, p1)) != srs_success) {
        return err;
    }
    for (int i = 0; i < 100; i++) {
        if ((err = srs_utest_free_port(type, p2)) != srs_success || *p2 != *p1) {
            return err;
        }
    }

    return srs_error_new(ERROR_SOCKET_BIND, "no second free port");
}

// Sends the datagram from the client socket to the port of the ip, an IPv4 or IPv6 address. Returns what sendto does.
static int listener_utest_send(srs_netfd_t client, string ip, int port, string data)
{
    sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));
    int len = 0;
    if (ip.find(':') != string::npos) {
        sockaddr_in6 *a6 = (sockaddr_in6 *)&addr;
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons(port);
        inet_pton(AF_INET6, ip.c_str(), &a6->sin6_addr);
        len = sizeof(sockaddr_in6);
    } else {
        sockaddr_in *a4 = (sockaddr_in *)&addr;
        a4->sin_family = AF_INET;
        a4->sin_port = htons(port);
        a4->sin_addr.s_addr = inet_addr(ip.c_str());
        len = sizeof(sockaddr_in);
    }
    return srs_sendto(client, (void *)data.data(), (int)data.size(), (sockaddr *)&addr, len, SRS_UTIME_NO_TIMEOUT);
}

// The server's log while it lives: each warning and trace is kept.
class ListenerUtestLog
{
public:
    MockLogForSignal log_;
    ISrsLog *saved_;

public:
    ListenerUtestLog()
    {
        saved_ = _srs_log;
        _srs_log = &log_;
    }
    ~ListenerUtestLog()
    {
        _srs_log = saved_;
    }

public:
    // The last trace that holds the text, or "".
    string last_trace(string text)
    {
        string found;
        for (int i = 0; i < (int)log_.traces_.size(); i++) {
            if (log_.traces_[i].find(text) != string::npos) {
                found = log_.traces_[i];
            }
        }
        return found;
    }
    // How many warnings hold the text.
    int warns(string text)
    {
        int n = 0;
        for (int i = 0; i < (int)log_.warns_.size(); i++) {
            if (log_.warns_[i].find(text) != string::npos) {
                n++;
            }
        }
        return n;
    }
};

// The server's cached clock while it lives: the server started at base_, and the test moves the clock on. The
// clock is restored when it goes.
class ListenerUtestClock
{
public:
    srs_utime_t base_;
    srs_utime_t saved_now_;
    srs_utime_t saved_startup_;

public:
    ListenerUtestClock()
    {
        saved_now_ = _srs_system_time_us_cache;
        saved_startup_ = _srs_system_time_startup_time;
        base_ = srs_time_now_realtime();
        _srs_system_time_startup_time = base_;
        _srs_system_time_us_cache = base_;
    }
    ~ListenerUtestClock()
    {
        _srs_system_time_us_cache = saved_now_;
        _srs_system_time_startup_time = saved_startup_;
    }
};

// The WebRTC server on its UDP port, as SrsRtcServer takes each packet: it reads the fast id of the address, then
// its peer id, which finds the session. Time can pass between packets: with a step, packet n comes at
// clock_base_ + n * clock_step_ of the server's cached clock. Another coroutine, such as a session's timer, can
// become ready at the first packet.
class ListenerUtestRtcServer : public ISrsUdpMuxHandler
{
public:
    int count_;
    vector<string> packets_;
    vector<string> peer_ids_;
    vector<uint64_t> fast_ids_;
    string peer_ip_;
    int peer_port_;
    srs_utime_t clock_base_;
    srs_utime_t clock_step_;
    ISrsCoroutine *ready_at_first_;

public:
    ListenerUtestRtcServer()
    {
        count_ = 0;
        peer_port_ = 0;
        clock_base_ = 0;
        clock_step_ = 0;
        ready_at_first_ = NULL;
    }
    virtual ~ListenerUtestRtcServer()
    {
    }

public:
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt)
    {
        count_++;
        packets_.push_back(string(skt->data(), skt->size()));
        fast_ids_.push_back(skt->fast_id());
        peer_ids_.push_back(skt->peer_id());
        peer_ip_ = skt->get_peer_ip();
        peer_port_ = skt->get_peer_port();

        if (clock_step_) {
            _srs_system_time_us_cache = clock_base_ + count_ * clock_step_;
        }

        if (count_ == 1 && ready_at_first_) {
            srs_error_t err = ready_at_first_->start();
            srs_freep(err);
        }

        return srs_success;
    }

public:
    // Waits for the port's coroutine to take the packets, at most about 1s.
    void wait(int count)
    {
        for (int i = 0; i < 1000 && count_ < count; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
};

// A coroutine that runs once, when it gets the CPU, and records how many packets the WebRTC server had by then.
class ListenerUtestReadyCoroutine : public ISrsCoroutineHandler
{
public:
    ListenerUtestRtcServer *server_;
    int ran_at_;
    SrsSTCoroutine trd_;

public:
    ListenerUtestReadyCoroutine(ListenerUtestRtcServer *server) : trd_("utest-ready", this)
    {
        server_ = server;
        ran_at_ = -1;
    }
    virtual ~ListenerUtestReadyCoroutine()
    {
    }

public:
    virtual srs_error_t cycle()
    {
        ran_at_ = server_->count_;
        return srs_success;
    }
};

// Waits for the MPEG-TS caster to take the packets, at most about 1s.
static void listener_utest_wait_caster(MockUdpHandler *caster, int count)
{
    for (int i = 0; i < 1000 && caster->packet_count_ < count; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
}

// The config listens RTMP on two ports, like `listen 1935 1936;`. A client connecting to the second port reaches the
// server as a client of the RTMP listener, not of the one port's listener, so the server makes an RTMP connection for
// it, managed until the client leaves.
VOID TEST(AppListenerTest, RtmpClientOnSecondListenPort_ServerMakesRtmpConnection)
{
    srs_error_t err;

    int p1 = 0, p2 = 0;
    HELPER_ASSERT_SUCCESS(listener_utest_two_ports(SOCK_STREAM, &p1, &p2));

    SrsResourceManager manager("utest-ai44-rtmp");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    vector<string> endpoints;
    endpoints.push_back("127.0.0.1:" + srs_strconv_format_int(p1));
    endpoints.push_back("127.0.0.1:" + srs_strconv_format_int(p2));
    server->rtmp_listener_->add(endpoints)->set_label("RTMP");
    HELPER_ASSERT_SUCCESS(server->rtmp_listener_->listen());

    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", p2, 1 * SRS_UTIME_SECONDS, &stfd));
    SrsTcpConnection *client = new SrsTcpConnection(stfd);
    for (int i = 0; i < 100 && manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }

    ASSERT_EQ(1, (int)manager.size());
    SrsRtmpConn *rtmp = dynamic_cast<SrsRtmpConn *>(manager.at(0));
    ASSERT_TRUE(rtmp != NULL);
    // The connection removes itself from the server's manager when it ends, which is the global one in SRS.
    rtmp->manager_ = &manager;

    srs_freep(client);
    for (int i = 0; i < 300 && !manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(manager.empty());

    server->rtmp_listener_->close();
    server->conn_manager_ = NULL;
}

// The config listens RTMP on two ports, and another program holds the second one. The server does not start, and the
// error names the port.
VOID TEST(AppListenerTest, RtmpSecondListenPortHeldByAnotherProgram_ListenFails)
{
    srs_error_t err;

    int p1 = 0, p2 = 0;
    HELPER_ASSERT_SUCCESS(listener_utest_two_ports(SOCK_STREAM, &p1, &p2));
    int other = listener_utest_hold_port(SOCK_STREAM, p2);
    ASSERT_GE(other, 0);

    MockTcpHandler handler;
    SrsMultipleTcpListeners rtmp(&handler);
    vector<string> endpoints;
    endpoints.push_back("127.0.0.1:" + srs_strconv_format_int(p1));
    endpoints.push_back("127.0.0.1:" + srs_strconv_format_int(p2));
    rtmp.add(endpoints)->set_label("RTMP");

    err = rtmp.listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    string desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find("listen at 127.0.0.1:" + srs_strconv_format_int(p2))) << desc;
    srs_freep(err);

    rtmp.close();
    ::close(other);
}

// Another program holds the UDP port of the MPEG-TS caster, or of WebRTC. The listen fails, naming the port, so the
// server does not start.
VOID TEST(AppListenerTest, UdpPortHeldByAnotherProgram_ListenFails)
{
    srs_error_t err;

    int p1 = 0, p2 = 0;
    HELPER_ASSERT_SUCCESS(listener_utest_two_ports(SOCK_DGRAM, &p1, &p2));
    int other1 = listener_utest_hold_port(SOCK_DGRAM, p1);
    ASSERT_GE(other1, 0);
    int other2 = listener_utest_hold_port(SOCK_DGRAM, p2);
    ASSERT_GE(other2, 0);

    MockUdpHandler caster;
    SrsUniquePtr<SrsUdpListener> mpegts(new SrsUdpListener(&caster));
    mpegts->set_endpoint("127.0.0.1", p1);
    mpegts->set_label("MPEGTS");
    err = mpegts->listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    string desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find("listen 127.0.0.1:" + srs_strconv_format_int(p1))) << desc;
    srs_freep(err);

    MockUdpMuxHandler rtc;
    SrsUniquePtr<SrsUdpMuxListener> webrtc(new SrsUdpMuxListener(&rtc, "127.0.0.1", p2));
    err = webrtc->listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find("listen 127.0.0.1:" + srs_strconv_format_int(p2))) << desc;
    srs_freep(err);

    ::close(other1);
    ::close(other2);
}

// State Threads cannot create a coroutine: the process is out of memory or over its address space limit.
static void *listener_utest_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// The server starts when no coroutine can be created. The MPEG-TS caster's UDP port, a TCP port such as the GB28181
// media port, and the WebRTC UDP port each fail to listen with the ST error, so the server does not start with a port
// that nothing reads.
VOID TEST(AppListenerTest, ListenWhenNoCoroutineCanBeCreated_EachListenerFails)
{
    srs_error_t err;

    int udp_port = 0, rtc_port = 0, tcp_port = 0;
    HELPER_ASSERT_SUCCESS(listener_utest_two_ports(SOCK_DGRAM, &udp_port, &rtc_port));
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_STREAM, &tcp_port));

    MockUdpHandler caster;
    SrsUniquePtr<SrsUdpListener> mpegts(new SrsUdpListener(&caster));
    mpegts->set_endpoint("127.0.0.1", udp_port);
    mpegts->set_label("MPEGTS");

    MockTcpHandler gb;
    SrsUniquePtr<SrsTcpListener> media(new SrsTcpListener(&gb));
    media->set_endpoint("127.0.0.1", tcp_port);
    media->set_label("GB-TCP");

    MockUdpMuxHandler rtc;
    SrsUniquePtr<SrsUdpMuxListener> webrtc(new SrsUdpMuxListener(&rtc, "127.0.0.1", rtc_port));

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)listener_utest_st_thread_create_out_of_memory;
    srs_error_t e_mpegts = mpegts->listen();
    srs_error_t e_media = media->listen();
    srs_error_t e_webrtc = webrtc->listen();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(e_mpegts));
    EXPECT_NE(string::npos, srs_error_desc(e_mpegts).find("start thread"));
    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(e_media));
    EXPECT_NE(string::npos, srs_error_desc(e_media).find("start coroutine"));
    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(e_webrtc));
    EXPECT_NE(string::npos, srs_error_desc(e_webrtc).find("start thread"));
    srs_freep(e_mpegts);
    srs_freep(e_media);
    srs_freep(e_webrtc);
}

#ifdef SRS_GB28181
// A gb28181 stream caster with no `listen`. The server starts with no GB28181 media port, as the TCP listener skips an
// endpoint with no port, and still mounts the SIP server's publish API.
VOID TEST(AppListenerTest, GbCasterWithoutListen_NoMediaPortPublishApiMounted)
{
    srs_error_t err;

    MockAppConfigForGbListener config;
    config.stream_caster_listen_port_ = 0;
    SrsHttpServeMux mux;
    MockApiServerOwnerForGbListener owner;
    owner.mux_ = &mux;

    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(&owner));
    listener->config_ = &config;

    SrsConfDirective conf;
    conf.name_ = "stream_caster";
    conf.args_.push_back("gb28181");
    HELPER_EXPECT_SUCCESS(listener->initialize(&conf));
    HELPER_EXPECT_SUCCESS(listener->listen());

    SrsTcpListener *media = dynamic_cast<SrsTcpListener *>(listener->media_listener_);
    ASSERT_TRUE(media != NULL);
    EXPECT_TRUE(media->lfd_ == NULL);
    EXPECT_TRUE(mux.static_matchers_.find("/gb/v1/publish/") != mux.static_matchers_.end());
}
#endif

// A load balancer checks the MPEG-TS caster's UDP port with its health check packet. The listener drops it, so the
// caster never parses it as TS, and the encoder's next packet reaches the caster.
VOID TEST(AppListenerTest, LoadBalancerHealthCheckToMpegtsCaster_Dropped)
{
    srs_error_t err;

    MockUdpHandler caster;
    SrsUniquePtr<SrsUdpListener> mpegts(new SrsUdpListener(&caster));
    mpegts->set_label("MPEGTS");
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_listener_listen(mpegts.get(), "127.0.0.1", &port));

    srs_netfd_t client = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &client));
    SrsUniquePtr<srs_netfd_t> client_ptr(&client, srs_close_stfd_ptr);

    string ts(188, 0x47);
    EXPECT_EQ(21, listener_utest_send(client, "127.0.0.1", port, listener_utest_slb_check));
    EXPECT_EQ(188, listener_utest_send(client, "127.0.0.1", port, ts));
    listener_utest_wait_caster(&caster, 1);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_EQ(1, caster.packet_count_);
    EXPECT_EQ(ts, caster.last_packet_data_);
}

// A load balancer checks the WebRTC UDP port with its health check packet. The listener drops it, so the WebRTC server
// never looks up a session for it, and a client's next packet reaches the server.
VOID TEST(AppListenerTest, LoadBalancerHealthCheckToWebrtcPort_Dropped)
{
    srs_error_t err;

    ListenerUtestRtcServer rtc;
    SrsUniquePtr<SrsUdpMuxListener> webrtc(new SrsUdpMuxListener(&rtc, "127.0.0.1", 0));
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_mux_listener_listen(webrtc.get(), &port));

    srs_netfd_t client = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &client));
    SrsUniquePtr<srs_netfd_t> client_ptr(&client, srs_close_stfd_ptr);

    // A STUN binding request: type, length 0, magic cookie and transaction id.
    string stun = string("\x00\x01\x00\x00\x21\x12\xa4\x42", 8) + "utest-tid-12";
    EXPECT_EQ(21, listener_utest_send(client, "127.0.0.1", port, listener_utest_slb_check));
    EXPECT_EQ((int)stun.size(), listener_utest_send(client, "127.0.0.1", port, stun));
    rtc.wait(1);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    ASSERT_EQ(1, rtc.count_);
    EXPECT_EQ(stun, rtc.packets_.at(0));
}

// The MPEG-TS caster refuses a packet, such as one that is not TS. The listener logs the error with its label and goes
// on, so the encoder's next packet still reaches the caster.
VOID TEST(AppListenerTest, MpegtsCasterRefusesPacket_WarnsAndKeepsReceiving)
{
    srs_error_t err;

    ListenerUtestLog log;
    MockUdpHandler caster;
    caster.on_udp_packet_error_ = srs_error_new(ERROR_STREAM_CASTER_TS_SYNC_BYTE, "ts: sync byte");
    SrsUniquePtr<SrsUdpListener> mpegts(new SrsUdpListener(&caster));
    mpegts->set_label("MPEGTS");
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_listener_listen(mpegts.get(), "127.0.0.1", &port));

    srs_netfd_t client = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &client));
    SrsUniquePtr<srs_netfd_t> client_ptr(&client, srs_close_stfd_ptr);

    EXPECT_EQ(5, listener_utest_send(client, "127.0.0.1", port, "hello"));
    EXPECT_EQ(6, listener_utest_send(client, "127.0.0.1", port, "world!"));
    listener_utest_wait_caster(&caster, 2);

    EXPECT_EQ(2, caster.packet_count_);
    EXPECT_EQ("world!", caster.last_packet_data_);
    EXPECT_EQ(2, log.warns("MPEGTS listener: Ignore error"));
    EXPECT_EQ(1, log.warns("handle packet 5 bytes"));
    EXPECT_EQ(1, log.warns("handle packet 6 bytes"));

    mpegts->close();
}

// A WebRTC client on IPv6 sends to the server's UDP port on [::1]. Its address has no fast id, so the server finds the
// session by the peer id, which the socket makes from the numeric IPv6 address and port, the same for each packet.
VOID TEST(AppListenerTest, WebrtcClientOverIpv6_PeerIdFromAddress)
{
    srs_error_t err;

    ListenerUtestRtcServer rtc;
    SrsUniquePtr<SrsUdpMuxListener> webrtc(new SrsUdpMuxListener(&rtc, "::1", 0));
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_mux_listener_listen(webrtc.get(), &port));

    srs_netfd_t client = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("::1", 0, &client));
    SrsUniquePtr<srs_netfd_t> client_ptr(&client, srs_close_stfd_ptr);

    sockaddr_in6 local;
    socklen_t nb_local = sizeof(local);
    ASSERT_EQ(0, getsockname(srs_netfd_fileno(client), (sockaddr *)&local, &nb_local));
    int client_port = ntohs(local.sin6_port);

    EXPECT_EQ(4, listener_utest_send(client, "::1", port, "stun"));
    EXPECT_EQ(4, listener_utest_send(client, "::1", port, "dtls"));
    rtc.wait(2);

    ASSERT_EQ(2, rtc.count_);
    string id = "::1:" + srs_strconv_format_int(client_port);
    EXPECT_EQ(id, rtc.peer_ids_.at(0));
    EXPECT_EQ(id, rtc.peer_ids_.at(1));
    EXPECT_EQ(0, (int)rtc.fast_ids_.at(0));
    EXPECT_EQ(0, (int)rtc.fast_ids_.at(1));
    EXPECT_EQ("::1", rtc.peer_ip_);
    EXPECT_EQ(client_port, rtc.peer_port_);
}

// Many WebRTC packets wait on the server's UDP port at once, such as a burst of video. The port's coroutine yields
// after every 10 packets, so another coroutine that became ready meanwhile, such as a session's timer, runs after the
// 11th packet, not only when the burst is drained.
VOID TEST(AppListenerTest, WebrtcPortBurst_OtherCoroutinesRunEveryTenPackets)
{
    srs_error_t err;

    ListenerUtestRtcServer rtc;
    ListenerUtestReadyCoroutine timer(&rtc);
    rtc.ready_at_first_ = &timer.trd_;

    SrsUniquePtr<SrsUdpMuxListener> webrtc(new SrsUdpMuxListener(&rtc, "127.0.0.1", 0));
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_mux_listener_listen(webrtc.get(), &port));

    srs_netfd_t client = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &client));
    SrsUniquePtr<srs_netfd_t> client_ptr(&client, srs_close_stfd_ptr);

    // The whole burst is queued on the port before its coroutine reads the first packet.
    for (int i = 0; i < 25; i++) {
        EXPECT_EQ(3, listener_utest_send(client, "127.0.0.1", port, "rtp"));
    }
    rtc.wait(25);

    EXPECT_EQ(25, rtc.count_);
    EXPECT_EQ(11, timer.ran_at_);
}

// The "<- RTC RECV" line of a WebRTC UDP port, logged at the second packet with the pithy print due at each packet,
// when one packet comes per step since the server started.
static string listener_utest_rtc_recv_line(srs_utime_t step, srs_error_t *perr)
{
    srs_error_t &err = *perr;

    PithyPrintAtOnceScope pithy;
    ListenerUtestClock clock;
    ListenerUtestLog log;

    ListenerUtestRtcServer rtc;
    rtc.clock_base_ = clock.base_;
    rtc.clock_step_ = step;

    SrsUniquePtr<SrsUdpMuxListener> webrtc(new SrsUdpMuxListener(&rtc, "127.0.0.1", 0));
    int port = 0;
    if ((err = srs_utest_udp_mux_listener_listen(webrtc.get(), &port)) != srs_success) {
        return "";
    }
    pithy.at_once(SrsPithyPrint::create_rtc_recv(webrtc->fd()));

    srs_netfd_t client = NULL;
    if ((err = srs_udp_listen("127.0.0.1", 0, &client)) != srs_success) {
        return "";
    }
    SrsUniquePtr<srs_netfd_t> client_ptr(&client, srs_close_stfd_ptr);

    listener_utest_send(client, "127.0.0.1", port, "rtp");
    rtc.wait(1);
    listener_utest_send(client, "127.0.0.1", port, "rtp");
    rtc.wait(2);

    string prefix = "<- RTC RECV #" + srs_strconv_format_int(webrtc->fd()) + ", ";
    return log.last_trace(prefix);
}

// The WebRTC UDP port logs its receive rate at each pithy print: packets since the last line, the average and the
// last rate in packets per second, and its loops. Under load the rates are shown in thousands (k) or ten thousands (w).
VOID TEST(AppListenerTest, WebrtcRecvRateLine_RatesInUnitsByLoad)
{
    srs_error_t err = srs_success;

    // 500 packets per second.
    string line = listener_utest_rtc_recv_line(2 * SRS_UTIME_MILLISECONDS, &err);
    HELPER_ASSERT_SUCCESS(err);
    EXPECT_NE(string::npos, line.find(", udp 1, pps 500/500, schedule 1")) << line;

    // 2,000 packets per second.
    line = listener_utest_rtc_recv_line(500, &err);
    HELPER_ASSERT_SUCCESS(err);
    EXPECT_NE(string::npos, line.find(", udp 1, pps 2/2(k), schedule 1")) << line;

    // 20,000 packets per second.
    line = listener_utest_rtc_recv_line(50, &err);
    HELPER_ASSERT_SUCCESS(err);
    EXPECT_NE(string::npos, line.find(", udp 1, pps 2/2(w), schedule 1")) << line;
}
