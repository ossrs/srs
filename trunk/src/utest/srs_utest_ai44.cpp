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
#include <srs_app_log.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_process.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai30.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai38.hpp>
#include <srs_utest_ai41.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_service.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <string.h>
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

// Log a line the way the SRS log macros do, through a va_list, with errno set as the failed call left it.
static void log_utest_log(SrsFileLog &log, SrsLogLevel level, int error_number, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    errno = error_number;
    log.log(level, NULL, SrsContextId(), fmt, args);
    errno = 0;
    va_end(args);
}

// The process's stdout, sent to a file while the scope lives, so a test reads what the console tank printed.
class LogUtestStdoutScope
{
public:
    int saved_fd_;

public:
    LogUtestStdoutScope()
    {
        fflush(stdout);
        saved_fd_ = ::dup(STDOUT_FILENO);
        int fd = ::open("/tmp/srs-utest-log-console.txt", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        ::dup2(fd, STDOUT_FILENO);
        ::close(fd);
    }
    ~LogUtestStdoutScope()
    {
        restore();
    }

public:
    void restore()
    {
        if (saved_fd_ < 0) {
            return;
        }
        fflush(stdout);
        ::dup2(saved_fd_, STDOUT_FILENO);
        ::close(saved_fd_);
        saved_fd_ = -1;
    }
    string read()
    {
        restore();
        string text;
        FILE *f = ::fopen("/tmp/srs-utest-log-console.txt", "rb");
        if (f) {
            char buf[1024];
            size_t n;
            while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0) {
                text.append(buf, n);
            }
            ::fclose(f);
        }
        ::unlink("/tmp/srs-utest-log-console.txt");
        return text;
    }
};

// With `srs_log_tank console`, the server prints its log to the terminal: a trace line plain, as the operator reads
// most lines, and a warning in yellow, reset at its end so the next line is plain again.
VOID TEST(AppLogTest, ConsoleTankPrintsTracePlainAndWarnInYellow)
{
    SrsFileLog log;
    log.log_to_file_tank_ = false;

    LogUtestStdoutScope console;
    log_utest_log(log, SrsLogLevelTrace, 0, "RTMP client ip=%s:%d", "127.0.0.1", 50320);
    log_utest_log(log, SrsLogLevelWarn, 0, "client disconnect peer. ret=%d", 1007);
    string text = console.read();

    size_t eol = text.find('\n');
    ASSERT_NE(string::npos, eol) << text;
    string trace = text.substr(0, eol + 1);
    string warn = text.substr(eol + 1);

    EXPECT_EQ('[', trace[0]) << trace;
    EXPECT_NE(string::npos, trace.find("[INFO]")) << trace;
    EXPECT_NE(string::npos, trace.find("] RTMP client ip=127.0.0.1:50320\n")) << trace;
    EXPECT_EQ(string::npos, trace.find('\033')) << trace;

    EXPECT_EQ(0, (int)warn.find("\033[33m[")) << warn;
    EXPECT_NE(string::npos, warn.find("[WARN]")) << warn;
    EXPECT_NE(string::npos, warn.find("] client disconnect peer. ret=1007\n\033[0m")) << warn;
    EXPECT_EQ(warn.length() - 4, warn.rfind("\033[0m")) << warn;
}

// A hook server that answers on_connect with a body over the 64KB log buffer, such as an HTML error page with a 200,
// makes the "on_connect ok" trace too long: the line is dropped whole rather than printed cut, and the next line of
// the server is printed as usual.
VOID TEST(AppLogTest, HookResponseOver64KB_TraceLineDroppedNextLinePrinted)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;
    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;

    string res(70 * 1024, 'x');
    log_utest_log(log, SrsLogLevelTrace, 0, "http: on_connect ok, client_id=%s, url=%s, request=%s, response=%s",
                  "7k0v5u21", "http://127.0.0.1:8085/api/v1/clients", "{\"action\":\"on_connect\"}", res.c_str());
    EXPECT_EQ(0, writer.write_console_count_);

    log_utest_log(log, SrsLogLevelTrace, 0, "RTMP client ip=%s:%d", "127.0.0.1", 50320);
    ASSERT_EQ(1, writer.write_console_count_);
    EXPECT_NE(string::npos, writer.console_.find("] RTMP client ip=127.0.0.1:50320\n")) << writer.console_;

    log.writer_ = NULL;
}

// An error line ends with the text of errno, such as "(Broken pipe)" after a client left mid-write. When the error
// description fills the log buffer so that the errno text no longer fits, the line is dropped rather than printed
// without it; a description a few bytes shorter is printed with the errno text.
VOID TEST(AppLogTest, ErrorWithErrnoFillingTheBuffer_LineDropped)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;
    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;

    // The header of an error line has a fixed width for a given errno: measure it on a short line.
    string errno_text = string("(") + strerror(EPIPE) + ")";
    log_utest_log(log, SrsLogLevelError, EPIPE, "serve error %s", "x");
    ASSERT_EQ(1, writer.write_console_count_);
    int header = (int)writer.console_.length() - (int)strlen("serve error x") - (int)errno_text.length() - 1;
    ASSERT_GT(header, 0);
    EXPECT_EQ(errno_text + "\n", writer.console_.substr(writer.console_.length() - errno_text.length() - 1));

    // The description fits the 64KB buffer with 3 bytes left, too few for the errno text.
    int fits = 65536 - header - 3 - (int)strlen("serve error ");
    string desc(fits, 'd');
    log_utest_log(log, SrsLogLevelError, EPIPE, "serve error %s", desc.c_str());
    EXPECT_EQ(1, writer.write_console_count_);

    // With room for the errno text, the line is printed whole.
    desc.resize(fits - (int)errno_text.length());
    log_utest_log(log, SrsLogLevelError, EPIPE, "serve error %s", desc.c_str());
    ASSERT_EQ(2, writer.write_console_count_);
    EXPECT_STREQ("\033[31m", writer.console_color_.c_str());
    EXPECT_EQ(errno_text + "\n", writer.console_.substr(writer.console_.length() - errno_text.length() - 1));

    log.writer_ = NULL;
}

// An encoder sending MPEG-TS over UDP to the caster, as FFmpeg's `-f mpegts udp://host:port` does. The real TS muxer
// writes the PAT and PMT, again before each key frame as FFmpeg resends them, and the PES packets of each frame.
class MpegtsUtestEncoder
{
public:
    MockSrsFileWriter w_;
    SrsTsContext ctx_;
    SrsVideoCodecId vcodec_;
    int taken_;

public:
    MpegtsUtestEncoder(SrsVideoCodecId vcodec)
    {
        vcodec_ = vcodec;
        taken_ = 0;
    }

public:
    // A video access unit of the NALUs at the time in ms.
    srs_error_t video(int64_t ms, const vector<string> &nalus, bool key)
    {
        if (key) {
            ctx_.reset();
        }

        SrsTsMessage msg;
        msg.sid_ = SrsTsPESStreamIdVideoCommon;
        msg.dts_ = msg.pts_ = ms * 90;
        msg.write_pcr_ = key;
        for (int i = 0; i < (int)nalus.size(); i++) {
            msg.payload_->append("\x00\x00\x00\x01", 4);
            msg.payload_->append(nalus[i].data(), (int)nalus[i].size());
        }

        SrsTsContextWriter tscw(&w_, &ctx_, SrsAudioCodecIdAAC, vcodec_);
        return tscw.write_video(&msg);
    }
    // An audio PES of the ADTS frames at the time in ms.
    srs_error_t audio(int64_t ms, const string &adts)
    {
        SrsTsMessage msg;
        msg.sid_ = SrsTsPESStreamIdAudioCommon;
        msg.dts_ = msg.pts_ = ms * 90;
        msg.payload_->append(adts.data(), (int)adts.size());

        SrsTsContextWriter tscw(&w_, &ctx_, SrsAudioCodecIdAAC, vcodec_);
        return tscw.write_audio(&msg);
    }
    // The TS written since the last call, cut into datagrams of the size; FFmpeg sends 1316 bytes, 7 TS packets.
    vector<string> datagrams(int size)
    {
        string ts = w_.str().substr(taken_);
        taken_ += (int)ts.size();

        vector<string> pkts;
        for (int pos = 0; pos < (int)ts.size(); pos += size) {
            pkts.push_back(ts.substr(pos, size));
        }
        return pkts;
    }
};

// An ADTS frame of AAC at 44.1kHz, stereo, with 8 bytes of data. The profile is the ADTS profile, the object type
// minus 1: 1 for AAC LC, 3 for AAC LTP (FFmpeg's `-profile:a aac_ltp`).
static string mpegts_utest_adts(int profile)
{
    int size = 8;
    int len = 7 + size;
    string f;
    f.push_back((char)0xff);
    f.push_back((char)0xf1);
    f.push_back((char)(((profile & 0x03) << 6) | (4 << 2) | (2 >> 2)));
    f.push_back((char)(((2 & 0x03) << 6) | ((len >> 11) & 0x03)));
    f.push_back((char)((len >> 3) & 0xff));
    f.push_back((char)(((len & 0x07) << 5) | 0x1f));
    f.push_back((char)0xfc);
    for (int i = 0; i < size; i++) {
        f.push_back((char)(0x21 + i));
    }
    return f;
}

// The H.264 NALUs of an encoder: its SPS and PPS, an IDR slice and a P slice.
static const string mpegts_utest_sps("\x67\x64\x00\x1f\xac\xd9\x40\x50\x05\xbb\x01\x10", 12);
static const string mpegts_utest_pps("\x68\xeb\xe3\xcb\x22\xc0", 6);
static const string mpegts_utest_idr("\x65\x88\x84\x00\x10", 5);
static const string mpegts_utest_p("\x41\x9a\x21\x6c\x42", 5);

// An H.264 key frame with its SPS and PPS, as an encoder sends each key frame.
static vector<string> mpegts_utest_key_frame()
{
    vector<string> nalus;
    nalus.push_back(mpegts_utest_sps);
    nalus.push_back(mpegts_utest_pps);
    nalus.push_back(mpegts_utest_idr);
    return nalus;
}

static vector<string> mpegts_utest_nalus(const string &nalu)
{
    return vector<string>(1, nalu);
}

// Writes an H.264 and AAC stream of the GOPs, each a key frame then P frames every 40ms, with an audio PES of one
// frame every 40ms, from the time in ms.
static srs_error_t mpegts_utest_write_gops(MpegtsUtestEncoder *enc, int64_t start, int gops, int frames)
{
    srs_error_t err = srs_success;

    for (int g = 0; g < gops; g++) {
        for (int i = 0; i < frames; i++) {
            int64_t ms = start + (g * frames + i) * 40;
            bool key = (i == 0);
            if ((err = enc->video(ms, key ? mpegts_utest_key_frame() : mpegts_utest_nalus(mpegts_utest_p), key)) != srs_success) {
                return err;
            }
            if ((err = enc->audio(ms, mpegts_utest_adts(1))) != srs_success) {
                return err;
            }
        }
    }

    return err;
}

// The RTMP clients the caster creates to publish to its output, each a mock that records what it sends. The caster
// frees a client when it closes it.
class MpegtsUtestFactory : public SrsAppFactory
{
public:
    vector<string> urls_;
    MockRtmpClientForForwarder *client_;

public:
    MpegtsUtestFactory()
    {
        client_ = NULL;
    }
    virtual ~MpegtsUtestFactory()
    {
    }

public:
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
    {
        urls_.push_back(url);
        client_ = new MockRtmpClientForForwarder();
        return client_;
    }
};

// The MPEG-TS caster of a `stream_caster` with `caster mpegts_over_udp`, from config text, fed the datagrams of an
// encoder at 127.0.0.1:5000 as the UDP listener gives them.
class MpegtsUtestCaster
{
public:
    MockSrsConfig conf_;
    MpegtsUtestFactory factory_;
    SrsMpegtsOverUdp *caster_;

public:
    MpegtsUtestCaster()
    {
        caster_ = new SrsMpegtsOverUdp();
        caster_->config_ = &conf_;
        caster_->app_factory_ = &factory_;
    }
    virtual ~MpegtsUtestCaster()
    {
        srs_freep(caster_);
    }

public:
    srs_error_t initialize(string output)
    {
        srs_error_t err = srs_success;

        string text = "stream_caster { enabled on; caster mpegts_over_udp; output " + output + "; listen 8935; }";
        if ((err = conf_.mock_parse(string(_MIN_OK_CONF) + text)) != srs_success) {
            return err;
        }

        caster_->assemble();
        return caster_->initialize(conf_.get_stream_casters().at(0));
    }
    srs_error_t send(const vector<string> &datagrams)
    {
        srs_error_t err = srs_success;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5000);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        for (int i = 0; i < (int)datagrams.size(); i++) {
            string pkt = datagrams[i];
            if ((err = caster_->on_udp_packet((sockaddr *)&addr, sizeof(addr), (char *)pkt.data(), (int)pkt.size())) != srs_success) {
                return err;
            }
        }
        return err;
    }
    // What the caster published to its output so far.
    vector<MockForwarderSentPacket> sent()
    {
        return factory_.client_ ? factory_.client_->sent_ : vector<MockForwarderSentPacket>();
    }
};

// How many of the packets are of the type.
static int mpegts_utest_count(const vector<MockForwarderSentPacket> &sent, SrsFrameType type)
{
    int n = 0;
    for (int i = 0; i < (int)sent.size(); i++) {
        if (sent[i].type_ == type) {
            n++;
        }
    }
    return n;
}

// An IP camera sends HEVC video and AAC audio in MPEG-TS over UDP. The caster publishes H.264 and AAC only: it drops
// each HEVC frame and goes on with the TS, so the audio still reaches the RTMP output, once the queue that waits for
// video holds 300 audio frames. The pithy print lines name the HEVC stream and the audio it sends.
VOID TEST(AppMpegtsUdpTest, CameraSendsHevcWithAac_AudioPublishedHevcDropped)
{
    srs_error_t err;

    ListenerUtestLog log;
    PithyPrintAtOnceScope pithy(SrsPithyPrint::create_caster);

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    // An HEVC key frame of VPS, SPS, PPS and an IDR slice, then a trailing frame, and 3 PES of 101 AAC frames each.
    MpegtsUtestEncoder enc(SrsVideoCodecIdHEVC);
    vector<string> hevc;
    hevc.push_back(string("\x40\x01\x0c\x01", 4));
    hevc.push_back(string("\x42\x01\x01\x01", 4));
    hevc.push_back(string("\x44\x01\xc1\x72", 4));
    hevc.push_back(string("\x26\x01\xaf\x08", 4));
    HELPER_ASSERT_SUCCESS(enc.video(0, hevc, true));
    HELPER_ASSERT_SUCCESS(enc.video(40, mpegts_utest_nalus(string("\x02\x01\xd0\x08", 4)), false));
    string adts;
    for (int i = 0; i < 101; i++) {
        adts.append(mpegts_utest_adts(1));
    }
    for (int i = 0; i < 3; i++) {
        HELPER_ASSERT_SUCCESS(enc.audio(i * 2345, adts));
    }
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));

    // The AAC sequence header and the first frames, and no video.
    vector<MockForwarderSentPacket> sent = c.sent();
    ASSERT_GE((int)sent.size(), 1);
    EXPECT_EQ(0, mpegts_utest_count(sent, SrsFrameTypeVideo));
    EXPECT_EQ((int)sent.size(), mpegts_utest_count(sent, SrsFrameTypeAudio));
    ASSERT_GE((int)sent[0].payload_.size(), 2);
    EXPECT_EQ(0xaf, (uint8_t)sent[0].payload_[0]);
    EXPECT_EQ(0x00, (uint8_t)sent[0].payload_[1]);
    EXPECT_EQ("rtmp://127.0.0.1/live/livestream", c.factory_.urls_.at(0));

    string got = log.last_trace("mpegts: got Video");
    EXPECT_NE(string::npos, got.find("stream=H.265")) << got;
    EXPECT_NE(string::npos, log.last_trace("mpegts: got Audio").find("stream=AAC"));
    EXPECT_NE(string::npos, log.last_trace("mpegts: send msg A").find("size="));
}

// An encoder sends its TS in datagrams smaller than one TS packet, such as FFmpeg with `?pkt_size=100`. The caster
// waits for each whole 188-byte packet and publishes the same stream as from datagrams of 7 packets.
VOID TEST(AppMpegtsUdpTest, EncoderSendsDatagramsSmallerThanTsPacket_StreamReassembled)
{
    srs_error_t err;

    vector<MockForwarderSentPacket> whole;
    if (true) {
        MpegtsUtestCaster c;
        HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));
        MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
        HELPER_ASSERT_SUCCESS(mpegts_utest_write_gops(&enc, 0, 2, 5));
        HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));
        whole = c.sent();
    }

    ListenerUtestLog log;
    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));
    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    HELPER_ASSERT_SUCCESS(mpegts_utest_write_gops(&enc, 0, 2, 5));
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(100)));
    vector<MockForwarderSentPacket> sent = c.sent();

    // The video sequence header, key frames and audio, the same as from whole packets.
    ASSERT_GE((int)whole.size(), 10);
    EXPECT_GE(mpegts_utest_count(whole, SrsFrameTypeVideo), 5);
    EXPECT_GE(mpegts_utest_count(whole, SrsFrameTypeAudio), 5);
    ASSERT_EQ(whole.size(), sent.size());
    for (int i = 0; i < (int)sent.size(); i++) {
        EXPECT_EQ(whole[i].type_, sent[i].type_) << i;
        EXPECT_EQ(whole[i].timestamp_, sent[i].timestamp_) << i;
        EXPECT_TRUE(whole[i].payload_ == sent[i].payload_) << i;
    }
    EXPECT_GE(log.warns("udp: wait 127.0.0.1:5000 packet 100/100 bytes"), 1);
}

// The caster joins a stream midway, such as when SRS restarts while an encoder keeps sending TS in datagrams of 1000
// bytes, not a multiple of 188. Its first datagram starts inside a TS packet: the caster skips to the next sync byte,
// then publishes from the next PAT, PMT and key frame.
VOID TEST(AppMpegtsUdpTest, CasterJoinsUnalignedStreamMidway_PublishesFromNextKeyFrame)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    HELPER_ASSERT_SUCCESS(mpegts_utest_write_gops(&enc, 0, 3, 5));
    vector<string> datagrams = enc.datagrams(1000);
    ASSERT_GE((int)datagrams.size(), 3);
    ASSERT_NE(0x47, (uint8_t)datagrams[1][0]);
    datagrams.erase(datagrams.begin());
    HELPER_ASSERT_SUCCESS(c.send(datagrams));

    // The first video is the sequence header, then the key frame of the second GOP, at 200ms.
    vector<MockForwarderSentPacket> sent = c.sent();
    int first_video = -1;
    for (int i = 0; i < (int)sent.size() && first_video < 0; i++) {
        if (sent[i].type_ == SrsFrameTypeVideo) {
            first_video = i;
        }
    }
    ASSERT_GE(first_video, 0);
    ASSERT_GT((int)sent.size(), first_video + 1);
    EXPECT_EQ(0x17, (uint8_t)sent[first_video].payload_[0]);
    EXPECT_EQ(0x00, (uint8_t)sent[first_video].payload_[1]);
    int key = -1;
    for (int i = first_video + 1; i < (int)sent.size() && key < 0; i++) {
        if (sent[i].type_ == SrsFrameTypeVideo) {
            key = i;
        }
    }
    ASSERT_GE(key, 0);
    EXPECT_EQ(0x17, (uint8_t)sent[key].payload_[0]);
    EXPECT_EQ(0x01, (uint8_t)sent[key].payload_[1]);
    // The queue moves a message 1ms on when another holds its time, here the sequence header.
    EXPECT_EQ(201, sent[key].timestamp_);
}

// An encoder sends AAC LTP, such as FFmpeg with `-profile:a aac_ltp`. The caster makes no AAC sequence header for that
// profile, so it drops the audio and goes on with the TS: the H.264 video is published, once the queue that waits for
// audio holds 100 video frames.
VOID TEST(AppMpegtsUdpTest, EncoderSendsAacLtp_AudioDroppedVideoPublished)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    for (int i = 0; i < 105; i++) {
        int64_t ms = i * 40;
        bool key = (i % 50 == 0);
        HELPER_ASSERT_SUCCESS(enc.video(ms, key ? mpegts_utest_key_frame() : mpegts_utest_nalus(mpegts_utest_p), key));
        HELPER_ASSERT_SUCCESS(enc.audio(ms, mpegts_utest_adts(3)));
    }
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));

    vector<MockForwarderSentPacket> sent = c.sent();
    ASSERT_GE((int)sent.size(), 2);
    EXPECT_EQ(0, mpegts_utest_count(sent, SrsFrameTypeAudio));
    EXPECT_EQ((int)sent.size(), mpegts_utest_count(sent, SrsFrameTypeVideo));
    EXPECT_EQ(0x17, (uint8_t)sent[0].payload_[0]);
    EXPECT_EQ(0x00, (uint8_t)sent[0].payload_[1]);
    EXPECT_EQ(0x17, (uint8_t)sent[1].payload_[0]);
    EXPECT_EQ(0x01, (uint8_t)sent[1].payload_[1]);
    EXPECT_TRUE(c.caster_->aac_specific_config_.empty());
}

// The RTMP origin of the caster's output resets the connection at the publish, as a load balancer in front of a
// restarting origin does. The caster closes its RTMP client and keeps no frame or sequence header of the failed
// publish, so the next frame connects again and its publish starts with the sequence header.
VOID TEST(AppMpegtsUdpTest, OriginResetsAtPublish_CasterClosesAndForgetsSequenceHeaders)
{
    srs_error_t err;

    RtmpConnTestOrigin origin(RtmpConnOriginResetAfterCreateStream);
    HELPER_ASSERT_SUCCESS(origin.start());

    MpegtsUtestCaster c;
    c.caster_->app_factory_ = _srs_app_factory;
    HELPER_ASSERT_SUCCESS(c.initialize(origin.rtmp_url("livestream")));

    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    HELPER_ASSERT_SUCCESS(enc.audio(0, mpegts_utest_adts(1)));
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));

    EXPECT_EQ("rtmp://127.0.0.1:" + srs_strconv_format_int(origin.get_port()) + "/live", origin.tc_url_);
    EXPECT_TRUE(c.caster_->sdk_ == NULL);
    EXPECT_TRUE(c.caster_->aac_specific_config_.empty());
    EXPECT_EQ(0, (int)dynamic_cast<SrsMpegtsQueue *>(c.caster_->queue_)->msgs_.size());
}

// Another program holds the UDP port of the MPEG-TS caster. The caster's listen fails, naming the port, so the server
// does not start.
VOID TEST(AppMpegtsUdpTest, CasterPortHeldByAnotherProgram_ListenFails)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_DGRAM, &port));
    int other = ::socket(AF_INET6, SOCK_DGRAM, 0);
    ASSERT_GE(other, 0);
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;
    ASSERT_EQ(0, ::bind(other, (sockaddr *)&addr, sizeof(addr)));

    MockSrsConfig conf;
    string text = "stream_caster { enabled on; caster mpegts_over_udp; output rtmp://127.0.0.1/live/livestream; listen " + srs_strconv_format_int(port) + "; }";
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + text));

    SrsUniquePtr<SrsUdpCasterListener> listener(new SrsUdpCasterListener());
    listener->config_ = &conf;
    dynamic_cast<SrsMpegtsOverUdp *>(listener->caster_)->config_ = &conf;
    listener->assemble();
    HELPER_ASSERT_SUCCESS(listener->initialize(conf.get_stream_casters().at(0)));

    err = listener->listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    string desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find(":" + srs_strconv_format_int(port))) << desc;
    srs_freep(err);

    listener->close();
    ::close(other);
}

// An encoder, or any host that reaches the caster's UDP port, sends a key frame, then a PES whose SPS is cut to 3 bytes
// and whose PPS is new. The caster should keep the SPS it has and go on. It takes the cut SPS as an empty one, and the
// new PPS then makes it build a sequence header from that empty SPS, whose assert aborts the server.
VOID TEST(AppMpegtsUdpTest, DISABLED_CutSpsThenNewPps_ServerAborts)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    HELPER_ASSERT_SUCCESS(enc.video(0, mpegts_utest_key_frame(), true));
    vector<string> nalus;
    nalus.push_back(mpegts_utest_sps.substr(0, 3));
    nalus.push_back(string("\x68\xee\x3c\x80", 4));
    nalus.push_back(mpegts_utest_idr);
    HELPER_ASSERT_SUCCESS(enc.video(40, nalus, true));
    HELPER_ASSERT_SUCCESS(enc.video(80, mpegts_utest_nalus(mpegts_utest_p), false));
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));

    EXPECT_EQ(mpegts_utest_sps, c.caster_->h264_sps_);
    EXPECT_TRUE(c.caster_->h264_sps_pps_sent_);
}

// An encoder, or any host that reaches the caster's UDP port, sends a PES with two start codes in a row, an empty NALU.
// The caster should skip it and publish the frame. It reads the empty NALU's type and asks whether it is an SPS, whose
// assert of a non-empty NALU aborts the server.
VOID TEST(AppMpegtsUdpTest, DISABLED_TwoStartCodesInARow_ServerAborts)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    vector<string> nalus = mpegts_utest_key_frame();
    nalus.insert(nalus.begin(), string());
    HELPER_ASSERT_SUCCESS(enc.video(0, nalus, true));
    HELPER_ASSERT_SUCCESS(enc.video(40, mpegts_utest_nalus(mpegts_utest_p), false));
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));

    EXPECT_EQ(mpegts_utest_sps, c.caster_->h264_sps_);
    EXPECT_TRUE(c.caster_->h264_sps_pps_sent_);
}

// A video-only encoder whose PES carry no PTS: the TS demuxer gives each frame dts 0. The queue holds video until it
// has audio or 100 frames, and moves a message 1ms on, at most 10 times, while another holds its time. From the 12th
// message on, each one replaces the message at 10ms, which is lost and never freed, while the video count goes on.
VOID TEST(AppMpegtsUdpTest, DISABLED_VideoOnlyWithoutPts_QueuedFramesReplacedAndLeaked)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    SrsTsChannel channel;
    channel.apply_ = SrsTsPidApplyVideo;
    channel.stream_ = SrsTsStreamVideoH264;
    SrsTsPacket packet(NULL);

    // The sequence header and 12 frames.
    for (int i = 0; i < 12; i++) {
        SrsTsMessage msg(&channel, &packet);
        msg.sid_ = SrsTsPESStreamIdVideoCommon;
        vector<string> nalus = (i == 0) ? mpegts_utest_key_frame() : mpegts_utest_nalus(mpegts_utest_p);
        for (int j = 0; j < (int)nalus.size(); j++) {
            msg.payload_->append("\x00\x00\x00\x01", 4);
            msg.payload_->append(nalus[j].data(), (int)nalus[j].size());
        }
        HELPER_ASSERT_SUCCESS(c.caster_->on_ts_message(&msg));
    }

    SrsMpegtsQueue *queue = dynamic_cast<SrsMpegtsQueue *>(c.caster_->queue_);
    EXPECT_EQ(13, queue->nb_videos_);
    EXPECT_EQ(13, (int)queue->msgs_.size());
}

#ifndef _WIN32 // POSIX only: the exec runs /bin/echo and /bin/sleep
// The text of the file, or "" if there is none.
static string ng_exec_utest_read(string path)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        return "";
    }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return string(buf, n);
}

// A vhost with `exec { enabled on; publish ...; }`, as nginx-rtmp's exec_push: when an encoder publishes, each command
// runs with the stream's names in its arguments, the exec logs how many commands run for which stream, and when the
// encoder stops, each command that still runs is killed.
VOID TEST(AppNgExecTest, EncoderPublishes_CommandsRunWithStreamNamesUntilUnpublish)
{
    srs_error_t err;

    ListenerUtestLog log;
    PithyPrintAtOnceScope pithy(SrsPithyPrint::create_exec);

    string out = "/tmp/srs-utest-ng-exec-echo.txt";
    ::unlink(out.c_str());

    MockSrsConfig conf;
    string text = "vhost __defaultVhost__ { exec { enabled on; publish /bin/echo [app]/[stream] 1>" + out + "; publish /bin/sleep 30; } }";
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + text));

    SrsUniquePtr<SrsNgExec> exec(new SrsNgExec());
    exec->config_ = &conf;
    exec->assemble();

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_ASSERT_SUCCESS(exec->on_publish(&req));

    // The exec's coroutine starts both commands, each with a 10ms wait, then logs.
    for (int i = 0; i < 100 && log.last_trace("-> EXE ").empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    string line = log.last_trace("-> EXE ");
    EXPECT_NE(string::npos, line.find("publish=2, input=__defaultVhost__/live/livestream")) << line;

    // The sleep runs; the echo may have quit already, for the next loop to run it again.
    ASSERT_EQ(2, (int)exec->exec_publishs_.size());
    SrsProcess *sleeper = exec->exec_publishs_[1];
    EXPECT_TRUE(sleeper->started());
    pid_t pid = sleeper->get_pid();
    ASSERT_GT(pid, 0);
    EXPECT_EQ(0, srs_kill(pid, 0));

    // The echo wrote the stream's app and name to its stdout file, after the lines the child logs on its start.
    string echoed;
    for (int i = 0; i < 100 && !srs_strings_ends_with(echoed, "\nlive/livestream\n"); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        echoed = ng_exec_utest_read(out);
    }
    EXPECT_NE(string::npos, echoed.find("process actual cli: /bin/echo live/livestream\n")) << echoed;
    EXPECT_TRUE(srs_strings_ends_with(echoed, "\nlive/livestream\n")) << echoed;

    // The encoder stops: the sleep is killed and reaped, and the exec holds no command. Each command is forked with
    // SIGTERM ignored, so the sleep, which sets no handler of its own, stays until the SIGKILL 1s later.
    exec->on_unpublish();
    EXPECT_TRUE(exec->exec_publishs_.empty());
    EXPECT_EQ(-1, srs_kill(pid, 0));
    EXPECT_EQ(ESRCH, errno);
    string killed = log.last_trace("stop process pid=");
    EXPECT_NE(string::npos, killed.find("SIGKILL stop process pid=" + srs_strconv_format_int(pid) + " ok")) << killed;

    ::unlink(out.c_str());
}
#endif
