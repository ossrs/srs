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
#include <srs_app_recv_thread.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai09.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai24.hpp>
#include <srs_utest_ai30.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai38.hpp>
#include <srs_utest_ai41.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_workflow_rtc_conn.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_sdp.hpp>
#ifdef SRS_FFMPEG_FIT
#include <srs_app_rtc_codec.hpp>
#endif
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif

#include <errno.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <stdarg.h>
#include <string.h>

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
    string path_;

public:
    LogUtestStdoutScope()
    {
        path_ = srs_utest_tmp_dir() + "/srs-utest-log-console.txt";
        fflush(stdout);
        saved_fd_ = ::dup(STDOUT_FILENO);
        // In binary mode, so the file has the bytes the tank printed, without the \r a Windows text mode adds.
        int fd = srs_file_open(path_.c_str(), O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
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
        FILE *f = ::fopen(path_.c_str(), "rb");
        if (f) {
            char buf[1024];
            size_t n;
            while ((n = ::fread(buf, 1, sizeof(buf), f)) > 0) {
                text.append(buf, n);
            }
            ::fclose(f);
        }
        ::unlink(path_.c_str());
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
    // The program holds the port on the IPv4 any address, where the caster listens. A socket on the IPv6 any address
    // holds it too on Linux, but not on Windows.
    int other = (int)::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(other, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
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
    srs_close_socket(other);
}

// An encoder, or any host that reaches the caster's UDP port, sends a key frame, then a PES whose SPS is cut to 3 bytes
// and whose PPS is new. The cut SPS has no profile or level to build a sequence header from, so the caster skips it,
// keeps the SPS it has and goes on.
VOID TEST(AppMpegtsUdpTest, CutSpsThenNewPps_CutSpsSkipped)
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

// An encoder, or any host that reaches the caster's UDP port, sends a PES with two start codes in a row, an empty NALU,
// then a PES that ends with a start code. The caster skips each empty NALU and goes on with the frame.
VOID TEST(AppMpegtsUdpTest, TwoStartCodesInARow_EmptyNaluSkipped)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    MpegtsUtestEncoder enc(SrsVideoCodecIdAVC);
    vector<string> nalus = mpegts_utest_key_frame();
    nalus.insert(nalus.begin(), string());
    HELPER_ASSERT_SUCCESS(enc.video(0, nalus, true));
    nalus = mpegts_utest_nalus(mpegts_utest_p);
    nalus.push_back(string());
    HELPER_ASSERT_SUCCESS(enc.video(40, nalus, false));
    HELPER_ASSERT_SUCCESS(c.send(enc.datagrams(1316)));

    EXPECT_EQ(mpegts_utest_sps, c.caster_->h264_sps_);
    EXPECT_TRUE(c.caster_->h264_sps_pps_sent_);
}

// A video-only encoder whose PES carry no PTS: the TS demuxer gives each frame dts 0. The queue holds video until it
// has audio or 100 frames, and moves a message 1ms on while another holds its time, so each message keeps its own
// time and none is lost. Once the queue holds over 100 frames, it sends the oldest.
VOID TEST(AppMpegtsUdpTest, VideoOnlyWithoutPts_QueuedFramesKept)
{
    srs_error_t err;

    MpegtsUtestCaster c;
    HELPER_ASSERT_SUCCESS(c.initialize("rtmp://127.0.0.1/live/livestream"));

    SrsTsChannel channel;
    channel.apply_ = SrsTsPidApplyVideo;
    channel.stream_ = SrsTsStreamVideoH264;
    SrsTsPacket packet(NULL);

    // The key frame gives the sequence header and a frame, each P frame one more frame.
    SrsMpegtsQueue *queue = dynamic_cast<SrsMpegtsQueue *>(c.caster_->queue_);
    for (int i = 0; i < 111; i++) {
        SrsTsMessage msg(&channel, &packet);
        msg.sid_ = SrsTsPESStreamIdVideoCommon;
        vector<string> nalus = (i == 0) ? mpegts_utest_key_frame() : mpegts_utest_nalus(mpegts_utest_p);
        for (int j = 0; j < (int)nalus.size(); j++) {
            msg.payload_->append("\x00\x00\x00\x01", 4);
            msg.payload_->append(nalus[j].data(), (int)nalus[j].size());
        }
        HELPER_ASSERT_SUCCESS(c.caster_->on_ts_message(&msg));

        // The first 13 messages, before any is sent, each at its own ms.
        if (i == 11) {
            EXPECT_EQ(13, queue->nb_videos_);
            ASSERT_EQ(13, (int)queue->msgs_.size());
            EXPECT_EQ(0, queue->msgs_.begin()->first);
            EXPECT_EQ(12, queue->msgs_.rbegin()->first);
        }
    }

    // 112 messages: the queue keeps 100, and sends each of the other 12, none lost.
    EXPECT_EQ(100, queue->nb_videos_);
    EXPECT_EQ(100, (int)queue->msgs_.size());

    MockRtmpClientForForwarder *client = c.factory_.client_;
    ASSERT_TRUE(client != NULL);
    ASSERT_EQ(12, (int)client->sent_.size());
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(SrsFrameTypeVideo, client->sent_[i].type_);
    }
}

// A GB28181 device turns its audio off while it publishes video whose PES carry no PTS: the queue waits for audio
// that never comes, and moves each video message 1ms on while another holds its time, so each keeps its own time and
// none is lost.
VOID TEST(AppGb28181Test, AudioOffThenVideoWithoutPts_QueuedFramesKept)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegpsQueue> queue(new SrsMpegpsQueue());

    // The audio and video before the device turns its audio off.
    for (int i = 0; i < 2; i++) {
        SrsMediaPacket *audio = new SrsMediaPacket();
        audio->message_type_ = SrsFrameTypeAudio;
        audio->wrap(new char[4](), 4);
        HELPER_ASSERT_SUCCESS(queue->push(audio));
    }
    SrsMediaPacket *first = new SrsMediaPacket();
    first->message_type_ = SrsFrameTypeVideo;
    first->wrap(new char[4](), 4);
    HELPER_ASSERT_SUCCESS(queue->push(first));
    EXPECT_TRUE(queue->dequeue() == NULL);

    // 20 video messages, each at time 0.
    for (int i = 0; i < 20; i++) {
        SrsMediaPacket *video = new SrsMediaPacket();
        video->message_type_ = SrsFrameTypeVideo;
        video->wrap(new char[4](), 4);
        HELPER_ASSERT_SUCCESS(queue->push(video));
    }

    // Each message is held at its own ms: the audio at 0 and 1, the video from 2 on.
    EXPECT_EQ(2, queue->nb_audios_);
    EXPECT_EQ(21, queue->nb_videos_);
    ASSERT_EQ(23, (int)queue->msgs_.size());
    EXPECT_EQ(0, queue->msgs_.begin()->first);
    EXPECT_EQ(22, queue->msgs_.rbegin()->first);
    EXPECT_EQ(first, queue->msgs_[2]);

    // The oldest audio goes out; the video waits for audio until the queue overflows.
    if (true) {
        SrsUniquePtr<SrsMediaPacket> msg(queue->dequeue());
        ASSERT_TRUE(msg.get() != NULL);
        EXPECT_TRUE(msg->is_audio());
        EXPECT_EQ(0, msg->timestamp_);
    }
    EXPECT_TRUE(queue->dequeue() == NULL);
    EXPECT_EQ(21, queue->nb_videos_);
    EXPECT_EQ(22, (int)queue->msgs_.size());
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
    ISrsProcess *sleeper = exec->exec_publishs_[1];
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

// A player's pause command, as it sends one on its play stream.
static string recv_thread_utest_pause(bool is_pause)
{
    RtmpStackAmf0 c;
    c.str("pause").num(0).null().boolean(is_pause).num(1000);
    return rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 1, c.data_);
}

// An AAC frame of a publisher, small enough for one read.
static string recv_thread_utest_audio()
{
    return rtmp_stack_chunks(4, RTMP_MSG_AudioMessage, 1, string("\xaf\x01\x21\x10", 4));
}

// The receive thread of an RTMP player, as SrsRtmpConn::playing makes it: it takes the player's messages from its
// connection over loopback, and queues them for the play loop, which waits on the player's consumer.
class RecvThreadUtestPlayer
{
public:
    StUtestTcpPair pair_;
    MockLiveSourceForQueue source_;
    SrsTcpConnection *skt_;
    SrsRtmpServer *rtmp_;
    SrsLiveConsumer *consumer_;
    SrsQueueRecvThread *rtrd_;
    SrsStSocket client_;

public:
    RecvThreadUtestPlayer()
    {
        skt_ = NULL;
        rtmp_ = NULL;
        consumer_ = NULL;
        rtrd_ = NULL;
    }
    ~RecvThreadUtestPlayer()
    {
        srs_freep(rtrd_);
        srs_freep(consumer_);
        srs_freep(rtmp_);
        srs_freep(skt_);
    }

public:
    srs_error_t open()
    {
        srs_error_t err = srs_success;

        if ((err = pair_.open()) != srs_success) {
            return srs_error_wrap(err, "open");
        }

        skt_ = new SrsTcpConnection(pair_.take_server());
        rtmp_ = new SrsRtmpServer(skt_);
        consumer_ = new SrsLiveConsumer(&source_);
        rtrd_ = new SrsQueueRecvThread(consumer_, rtmp_, SRS_PERF_MW_SLEEP, _srs_context->get_id());
        client_.init(pair_.client_);

        return err;
    }
    srs_error_t send(string data)
    {
        return client_.write((void *)data.data(), data.size(), NULL);
    }
};

// Wakes the player's consumer after 2s, so a play loop that nothing else wakes does not wait for good.
class RecvThreadUtestWaker : public ISrsCoroutineHandler
{
public:
    SrsLiveConsumer *consumer_;
    SrsSTCoroutine trd_;

public:
    RecvThreadUtestWaker(SrsLiveConsumer *consumer) : trd_("utest-waker", this)
    {
        consumer_ = consumer;
    }
    virtual ~RecvThreadUtestWaker()
    {
        trd_.stop();
    }

public:
    virtual srs_error_t cycle()
    {
        srs_usleep(2 * SRS_UTIME_SECONDS);
        srs_error_t err = trd_.pull();
        if (err == srs_success) {
            consumer_->wakeup();
        }
        return err;
    }
};

// A player pauses a stream whose publisher sends nothing yet. The play loop waits on the consumer for media with no
// timeout, so only the receive thread can wake it: the pause wakes it at once, and the loop gets the pause.
VOID TEST(AppRecvThreadTest, PlayerOfStreamWithoutMediaPauses_PlayLoopWokenAtOnce)
{
    srs_error_t err;

    RecvThreadUtestPlayer p;
    HELPER_ASSERT_SUCCESS(p.open());
    HELPER_ASSERT_SUCCESS(p.rtrd_->start());

    RecvThreadUtestWaker waker(p.consumer_);
    HELPER_ASSERT_SUCCESS(waker.trd_.start());

    HELPER_ASSERT_SUCCESS(p.send(recv_thread_utest_pause(true)));

    srs_utime_t starttime = srs_time_now_realtime();
    p.consumer_->wait(SRS_PERF_MW_MIN_MSGS, SRS_PERF_MW_SLEEP);
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;
    EXPECT_LT(elapsed, 1 * SRS_UTIME_SECONDS);

    ASSERT_EQ(1, p.rtrd_->size());
    SrsUniquePtr<SrsRtmpCommonMessage> msg(p.rtrd_->pump());
    SrsRtmpCommand *pkt_raw = NULL;
    HELPER_ASSERT_SUCCESS(p.rtmp_->decode_message(msg.get(), &pkt_raw));
    SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);
    SrsPausePacket *pause = dynamic_cast<SrsPausePacket *>(pkt.get());
    ASSERT_TRUE(pause != NULL);
    EXPECT_TRUE(pause->is_pause_);

    p.rtrd_->stop();
}

// A player sends pause, resume and pause again at once. The receive thread takes one message at a time and holds the
// next until the play loop has handled the queued one, as a message may end the play. When the play ends with a
// message still queued, the thread stops at once, that is no receive error, and the message is dropped with the
// thread; the message never taken stays on the connection.
VOID TEST(AppRecvThreadTest, PlayerSendsMoreBeforeFirstHandled_HeldUntilPumpedThenDroppedAtStop)
{
    srs_error_t err;

    RecvThreadUtestPlayer p;
    HELPER_ASSERT_SUCCESS(p.open());
    HELPER_ASSERT_SUCCESS(p.send(recv_thread_utest_pause(true) + recv_thread_utest_pause(false) + recv_thread_utest_pause(true)));
    HELPER_ASSERT_SUCCESS(p.rtrd_->start());

    srs_usleep(100 * SRS_UTIME_MILLISECONDS);
    ASSERT_EQ(1, p.rtrd_->size());

    // The play loop handles the pause; the thread takes the resume after its wait.
    SrsRtmpCommonMessage *first = p.rtrd_->pump();
    srs_freep(first);
    for (int i = 0; i < 100 && p.rtrd_->empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_EQ(1, p.rtrd_->size());

    // The play ends while the resume is queued.
    srs_utime_t starttime = srs_time_now_realtime();
    p.rtrd_->stop();
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;
    EXPECT_LT(elapsed, 300 * SRS_UTIME_MILLISECONDS);

    err = p.rtrd_->error_code();
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(1, p.rtrd_->size());

    SrsRtmpCommonMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(p.rtmp_->recv_message(&msg_raw));
    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
    SrsRtmpCommand *pkt_raw = NULL;
    HELPER_ASSERT_SUCCESS(p.rtmp_->decode_message(msg.get(), &pkt_raw));
    SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);
    SrsPausePacket *pause = dynamic_cast<SrsPausePacket *>(pkt.get());
    ASSERT_TRUE(pause != NULL);
    EXPECT_TRUE(pause->is_pause_);
}

static void *recv_thread_utest_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// A player and a publisher arrive when no coroutine can be created for their receive threads: each start fails with
// the ST error, which refuses the play or the publish.
VOID TEST(AppRecvThreadTest, ClientsWhenNoCoroutineCanBeCreated_PlayAndPublishRefused)
{
    MockRtmpServer rtmp;
    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    SrsSharedPtr<SrsLiveSource> source;
    SrsQueueRecvThread player(NULL, &rtmp, SRS_PERF_MW_SLEEP, _srs_context->get_id());
    SrsPublishRecvThread publisher(&rtmp, &req, -1, 0, NULL, source, _srs_context->get_id());

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)recv_thread_utest_st_thread_create_out_of_memory;
    srs_error_t play_err = player.start();
    srs_error_t publish_err = publisher.start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(play_err)) << srs_error_desc(play_err);
    EXPECT_NE(string::npos, srs_error_desc(play_err).find("queue recv thread")) << srs_error_desc(play_err);
    srs_freep(play_err);

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(publish_err)) << srs_error_desc(publish_err);
    EXPECT_NE(string::npos, srs_error_desc(publish_err).find("publish recv thread")) << srs_error_desc(publish_err);
    srs_freep(publish_err);
}

// The receive thread of an RTMP publisher, as SrsRtmpConn::publishing makes it: the real connection over loopback
// with the vhost from config text, the stream's source and the stream stats.
class RecvThreadUtestPublisher
{
public:
    MockSrsConfig conf_;
    StUtestTcpPair pair_;
    SrsStatistic stat_;
    MockSrsRequest req_;
    MockLiveSource *source_raw_;
    SrsSharedPtr<SrsLiveSource> source_;
    SrsRtmpConn *conn_;
    SrsPublishRecvThread *rtrd_;
    SrsStSocket client_;
    int fd_;

public:
    RecvThreadUtestPublisher() : req_("__defaultVhost__", "live", "livestream"), source_raw_(new MockLiveSource()), source_(source_raw_)
    {
        conn_ = NULL;
        rtrd_ = NULL;
        fd_ = -1;
    }
    ~RecvThreadUtestPublisher()
    {
        srs_freep(rtrd_);
        srs_freep(conn_);
    }

public:
    srs_error_t open(string text)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(string(_MIN_OK_CONF) + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }

        if ((err = pair_.open()) != srs_success) {
            return srs_error_wrap(err, "open");
        }

        conn_ = new SrsRtmpConn(new SrsRtmpTransport(pair_.take_server()), "127.0.0.1", 1935);
        conn_->config_ = &conf_;
        conn_->assemble();
        conn_->stat_ = &stat_;
        conn_->info_->type_ = SrsRtmpConnFMLEPublish;
        conn_->info_->res_->stream_id_ = 1;
        fd_ = conn_->transport_->osfd();

        rtrd_ = new SrsPublishRecvThread(conn_->rtmp_, &req_, fd_, 0, conn_, source_, _srs_context->get_id());
        rtrd_->config_ = &conf_;
        rtrd_->assemble();

        client_.init(pair_.client_);

        return err;
    }
    srs_error_t send(string data)
    {
        return client_.write((void *)data.data(), data.size(), NULL);
    }
    // Waits for the thread to take n messages, at most about 2s.
    void wait_msgs(int n)
    {
        for (int i = 0; i < 2000 && rtrd_->nb_msgs() < n; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
    }
    int rcvbuf()
    {
        int v = 0;
        socklen_t n = sizeof(int);
        srs_getsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &v, &n);
        return v;
    }
    bool merged_read()
    {
        SrsRtmpServer *rtmp = dynamic_cast<SrsRtmpServer *>(conn_->rtmp_);
        return rtmp->protocol_->in_buffer_->merged_read_;
    }
};

// A publisher on a vhost with merged read, `mr on; mr_latency 350;`. When its thread starts, the socket's receive
// buffer is sized for 350ms of a 5Mbps stream, and after a small read the thread waits the latency for more data
// before it takes the frame. When the publish ends, merged read is off again.
VOID TEST(AppRecvThreadTest, PublisherOnVhostWithMergedRead_BufferSizedAndSmallReadsWaitForMore)
{
    srs_error_t err;

    ListenerUtestLog log;
    RecvThreadUtestPublisher p;
    HELPER_ASSERT_SUCCESS(p.open("vhost __defaultVhost__ { publish { mr on; mr_latency 350; } }"));
    int before = p.rcvbuf();

    HELPER_ASSERT_SUCCESS(p.rtrd_->start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // 350ms at 5000kbps is 218750 bytes; the thread sets half of it, which Linux doubles.
    string line = log.last_trace("mr change sleep");
    string want = "mr change sleep 350=>350, erbuf=218750, rbuf " + srs_strconv_format_int(before) + "=>" + srs_strconv_format_int(p.rcvbuf());
    EXPECT_NE(string::npos, line.find(want)) << line;
    EXPECT_GE(p.rcvbuf(), 109375);
    EXPECT_TRUE(p.merged_read());

    srs_utime_t starttime = srs_time_now_realtime();
    HELPER_ASSERT_SUCCESS(p.send(recv_thread_utest_audio()));
    p.wait_msgs(1);
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;
    EXPECT_EQ(1, p.rtrd_->nb_msgs());
    EXPECT_EQ(1, p.source_raw_->on_audio_count_);
    EXPECT_GE(elapsed, 300 * SRS_UTIME_MILLISECONDS);

    p.rtrd_->stop();
    EXPECT_FALSE(p.merged_read());
}

// Merged read is on, but the vhost wants the lowest latency (`min_latency on`), or sets `mr_latency 0`: a small read
// is taken at once.
VOID TEST(AppRecvThreadTest, PublisherWithMergedReadButMinLatencyOrZeroLatency_SmallReadsDoNotWait)
{
    srs_error_t err;

    const char *vhosts[] = {
        "vhost __defaultVhost__ { min_latency on; publish { mr on; mr_latency 350; } }",
        "vhost __defaultVhost__ { publish { mr on; mr_latency 0; } }",
    };
    for (int i = 0; i < 2; i++) {
        RecvThreadUtestPublisher p;
        HELPER_ASSERT_SUCCESS(p.open(vhosts[i]));
        HELPER_ASSERT_SUCCESS(p.rtrd_->start());
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        EXPECT_TRUE(p.merged_read()) << vhosts[i];

        srs_utime_t starttime = srs_time_now_realtime();
        HELPER_ASSERT_SUCCESS(p.send(recv_thread_utest_audio()));
        p.wait_msgs(1);
        srs_utime_t elapsed = srs_time_now_realtime() - starttime;
        EXPECT_EQ(1, p.rtrd_->nb_msgs()) << vhosts[i];
        EXPECT_LT(elapsed, 200 * SRS_UTIME_MILLISECONDS) << vhosts[i];

        p.rtrd_->stop();
    }
}

// A coroutine that runs once, when it gets the CPU, and records how many messages the publisher's thread had taken.
class RecvThreadUtestProbe : public ISrsCoroutineHandler
{
public:
    SrsPublishRecvThread *rtrd_;
    int64_t ran_at_;
    SrsSTCoroutine trd_;

public:
    RecvThreadUtestProbe(SrsPublishRecvThread *rtrd) : trd_("utest-probe", this)
    {
        rtrd_ = rtrd;
        ran_at_ = -1;
    }
    virtual ~RecvThreadUtestProbe()
    {
    }

public:
    virtual srs_error_t cycle()
    {
        ran_at_ = rtrd_->nb_msgs();
        return srs_success;
    }
};

// A publisher's burst of 20 frames is already in the socket, so its thread never waits for a read. It yields after
// every 15 messages, so another ready coroutine, such as another client's, runs in the middle of the burst.
VOID TEST(AppRecvThreadTest, PublisherSendsBurst_OtherCoroutinesRunEvery15Messages)
{
    srs_error_t err;

    RecvThreadUtestPublisher p;
    HELPER_ASSERT_SUCCESS(p.open(""));

    string burst;
    for (int i = 0; i < 20; i++) {
        burst += recv_thread_utest_audio();
    }
    HELPER_ASSERT_SUCCESS(p.send(burst));

    RecvThreadUtestProbe probe(p.rtrd_);
    HELPER_ASSERT_SUCCESS(p.rtrd_->start());
    HELPER_ASSERT_SUCCESS(probe.trd_.start());
    p.wait_msgs(20);

    EXPECT_EQ(20, p.rtrd_->nb_msgs());
    EXPECT_EQ(20, p.source_raw_->on_audio_count_);
    EXPECT_EQ(15, probe.ran_at_);

    p.rtrd_->stop();
}

// An encoder such as OBS or FFmpeg stops with FCUnpublish. The thread answers that the unpublish succeeded, ends with
// the republish error, and wakes the publish loop, which ends the publish.
VOID TEST(AppRecvThreadTest, EncoderSendsFcUnpublish_ThreadEndsAndWakesPublishLoop)
{
    srs_error_t err;

    RecvThreadUtestPublisher p;
    HELPER_ASSERT_SUCCESS(p.open(""));
    HELPER_ASSERT_SUCCESS(p.rtrd_->start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    RtmpStackAmf0 unpublish;
    unpublish.str("FCUnpublish").num(5).null().str("livestream");
    HELPER_ASSERT_SUCCESS(p.send(recv_thread_utest_audio() + rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 1, unpublish.data_)));

    srs_utime_t starttime = srs_time_now_realtime();
    HELPER_EXPECT_SUCCESS(p.rtrd_->wait(5 * SRS_UTIME_SECONDS));
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;
    EXPECT_LT(elapsed, 1 * SRS_UTIME_SECONDS);

    err = p.rtrd_->error_code();
    EXPECT_EQ(ERROR_CONTROL_REPUBLISH, srs_error_code(err)) << srs_error_desc(err);
    EXPECT_NE(string::npos, srs_error_desc(err).find("handle publish message")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, p.source_raw_->on_audio_count_);

    // The encoder reads NetStream.Unpublish.Success.
    string replies;
    p.client_.set_recv_timeout(1 * SRS_UTIME_SECONDS);
    for (int i = 0; i < 10 && replies.find("NetStream.Unpublish.Success") == string::npos; i++) {
        char buf[4096];
        ssize_t nread = 0;
        if ((err = p.client_.read(buf, sizeof(buf), &nread)) != srs_success) {
            srs_freep(err);
            break;
        }
        replies.append(buf, nread);
    }
    EXPECT_NE(string::npos, replies.find("NetStream.Unpublish.Success"));
}

// A WebRTC session the RTC server made for a WHIP or WHEP client, which only its owner's token may stop.
class RtcApiUtestSession : public MockRtcConnectionForUpdateSessions
{
public:
    std::string token_;
    int expire_count_;
    int nack_drop_;

public:
    RtcApiUtestSession()
    {
        username_ = "srvufrag:cliufrag";
        token_ = "owner-token";
        expire_count_ = 0;
        nack_drop_ = 0;
    }
    virtual ~RtcApiUtestSession()
    {
    }

public:
    virtual void expire()
    {
        expire_count_++;
    }
    virtual std::string token()
    {
        return token_;
    }
    virtual void simulate_nack_drop(int nn)
    {
        nack_drop_ = nn;
    }
};

// The RTC server as the API sees it: it makes the session of an offer, or refuses it, and finds the session by its
// username until it is gone.
class RtcApiUtestServer : public ISrsRtcApiServer
{
public:
    RtcApiUtestSession session_;
    srs_error_t create_error_;
    int create_count_;
    bool created_;
    std::string ip_;
    std::string vhost_;
    std::string app_;
    std::string stream_;
    std::string ice_ufrag_;
    std::string ice_pwd_;
    bool publish_;

public:
    RtcApiUtestServer()
    {
        create_error_ = srs_success;
        create_count_ = 0;
        created_ = false;
        publish_ = false;
    }
    virtual ~RtcApiUtestServer()
    {
        srs_freep(create_error_);
    }

public:
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession)
    {
        create_count_++;
        if (create_error_ != srs_success) {
            return srs_error_copy(create_error_);
        }

        ip_ = ruc->req_->ip_;
        vhost_ = ruc->req_->vhost_;
        app_ = ruc->req_->app_;
        stream_ = ruc->req_->stream_;
        ice_ufrag_ = ruc->req_->ice_ufrag_;
        ice_pwd_ = ruc->req_->ice_pwd_;
        publish_ = ruc->publish_;

        created_ = true;
        *psession = &session_;
        return srs_success;
    }
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag)
    {
        if (!created_ || ufrag != session_.username_) {
            return NULL;
        }
        return &session_;
    }
};

// The hooks of a vhost, counting the publishers and players they are asked about.
class RtcApiUtestHooks : public MockHttpHooksForRtcPlay
{
public:
    int on_publish_count_;

public:
    RtcApiUtestHooks()
    {
        on_publish_count_ = 0;
    }
    virtual ~RtcApiUtestHooks()
    {
    }

public:
    virtual srs_error_t on_publish(std::string url, ISrsRequest *req, SrsRtmpConnType type)
    {
        on_publish_count_++;
        return srs_success;
    }
};

// A response writer that keeps the Location header, which the mock writer leaves out of the response.
class RtcApiUtestWriter : public MockResponseWriter
{
public:
    std::string location_;

public:
    virtual srs_error_t filter(SrsHttpHeader *h)
    {
        location_ = h->get("Location");
        return MockResponseWriter::filter(h);
    }
};

// The WHIP and WHEP API of a server from config text, with the RTC server, sources, stats and hooks it reaches.
class RtcApiUtestWhip
{
public:
    MockSrsConfig conf_;
    SrsStatistic stat_;
    MockRtcSourceManager rtc_sources_;
    MockLiveSourceManager live_sources_;
    RtcApiUtestHooks hooks_;
    RtcApiUtestServer server_;
    SrsGoApiRtcWhip *api_;
    // The Location header of the last response.
    std::string location_;

public:
    RtcApiUtestWhip()
    {
        api_ = NULL;
    }
    ~RtcApiUtestWhip()
    {
        if (api_) {
            api_->config_ = NULL;
            api_->publish_->config_ = NULL;
            api_->publish_->stat_ = NULL;
            api_->publish_->hooks_ = NULL;
            api_->play_->config_ = NULL;
            api_->play_->stat_ = NULL;
            api_->play_->rtc_sources_ = NULL;
            api_->play_->live_sources_ = NULL;
            api_->play_->hooks_ = NULL;
        }
        srs_freep(api_);
    }

public:
    srs_error_t initialize(std::string text)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(string(_MIN_OK_CONF) + "rtc_server { enabled on; listen 8000; } " + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }

        api_ = new SrsGoApiRtcWhip(&server_);
        api_->config_ = &conf_;
        api_->publish_->config_ = &conf_;
        api_->publish_->stat_ = &stat_;
        api_->publish_->hooks_ = &hooks_;
        dynamic_cast<SrsSecurity *>(api_->publish_->security_)->config_ = &conf_;
        api_->play_->config_ = &conf_;
        api_->play_->stat_ = &stat_;
        api_->play_->rtc_sources_ = &rtc_sources_;
        api_->play_->live_sources_ = &live_sources_;
        api_->play_->hooks_ = &hooks_;
        dynamic_cast<SrsSecurity *>(api_->play_->security_)->config_ = &conf_;

        return err;
    }

    // Sends one HTTP request of a WHIP or WHEP client at 10.0.0.1, and returns the response it gets.
    std::string request(uint8_t method, std::string url, std::string body, std::string forwarded_for = "")
    {
        MockHttpMessageForRtcApi message;
        srs_error_t err = message.set_url(url, false);
        srs_freep(err);
        message.query_params_ = message.query_;
        message.set_method(method);
        message.body_content_ = body;
        message.mock_conn_->remote_ip_ = "10.0.0.1";
        if (!forwarded_for.empty()) {
            message.header()->set("X-Forwarded-For", forwarded_for);
        }

        RtcApiUtestWriter writer;
        err = api_->serve_http(&writer, &message);
        location_ = writer.location_;
        srs_freep(err);
        return string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    }

    std::string post(std::string url, std::string body, std::string forwarded_for = "")
    {
        return request(SRS_CONSTS_HTTP_POST, url, body, forwarded_for);
    }
};

#define RTC_API_UTEST_WHIP "http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=livestream"
#define RTC_API_UTEST_WHEP "http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=livestream"
#define RTC_API_UTEST_VHOST "vhost __defaultVhost__ { rtc { enabled on; } } "

// The offer of a WebRTC client with one Opus track, which sends with a=sendonly, plays with a=recvonly, or does both.
static std::string rtc_api_utest_offer(std::string direction)
{
    return "v=0\r\n"
           "o=- 0 0 IN IP4 127.0.0.1\r\n"
           "s=-\r\n"
           "t=0 0\r\n"
           "a=group:BUNDLE 0\r\n"
           "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
           "c=IN IP4 0.0.0.0\r\n"
           "a=ice-ufrag:test\r\n"
           "a=ice-pwd:testpasswordtestpassword\r\n"
           "a=setup:actpass\r\n"
           "a=mid:0\r\n"
           "a=" +
           direction + "\r\n"
                       "a=rtcp-mux\r\n"
                       "a=rtpmap:111 opus/48000/2\r\n";
}

static bool rtc_api_utest_status(const std::string &response, int status)
{
    return response.find("HTTP/1.1 " + srs_strconv_format_int(status) + " ") == 0;
}

static bool rtc_api_utest_code(const std::string &response, int code)
{
    return response.find(srs_strconv_format_int(code) + ": ") != string::npos;
}

// Offers SRS cannot answer, from a client that is not a browser or that asks for what SRS does not do: no BUNDLE
// group, no media, a data channel, no rtcp-mux, a publisher's offer to WHEP or a player's to WHIP. Each is answered 400
// before any session is made, so nothing holds the stream.
VOID TEST(AppRtcApiTest, UnservableOffers_Refused400WithNoSession)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));

    string sendrecv = rtc_api_utest_offer("sendrecv");
    string no_bundle = srs_strings_replace(sendrecv, "a=group:BUNDLE 0\r\n", "");
    string no_media = sendrecv.substr(0, sendrecv.find("m=audio"));
    string data_channel = srs_strings_replace(sendrecv, "a=group:BUNDLE 0\r\n", "a=group:BUNDLE 0 1\r\n") +
                          "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
                          "c=IN IP4 0.0.0.0\r\n"
                          "a=mid:1\r\n"
                          "a=sctp-port:5000\r\n";
    string no_mux = srs_strings_replace(sendrecv, "a=rtcp-mux\r\n", "");

    const char *urls[] = {RTC_API_UTEST_WHIP, RTC_API_UTEST_WHEP};
    for (int i = 0; i < 2; i++) {
        string url = urls[i];
        string offers[] = {no_bundle, no_media, data_channel, no_mux, rtc_api_utest_offer(i == 0 ? "recvonly" : "sendonly")};
        for (int j = 0; j < 5; j++) {
            string response = w.post(url, offers[j]);
            EXPECT_TRUE(rtc_api_utest_status(response, 400)) << url << " offer " << j << ": " << response;
            EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_SDP_EXCHANGE)) << url << " offer " << j << ": " << response;
        }
    }
    EXPECT_EQ(0, w.server_.create_count_);
    EXPECT_EQ(0, (int)w.stat_.clients_.size());

    // The same client with a sendrecv offer is served by both.
    EXPECT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHIP, sendrecv), 201));
    EXPECT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHEP, sendrecv), 201));
    EXPECT_EQ(2, w.server_.create_count_);
}

// An edge vhost pulls its streams from the origin by RTMP and cannot serve WebRTC, even with rtc and rtmp_to_rtc on,
// so both a WHIP publisher and a WHEP player are refused.
VOID TEST(AppRtcApiTest, EdgeVhost_WhipAndWhepRefused)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize("vhost __defaultVhost__ { cluster { mode remote; origin 127.0.0.1:19350; } "
                                       "rtc { enabled on; rtmp_to_rtc on; } } "));

    string response = w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_DISABLED)) << response;

    response = w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_DISABLED)) << response;

    EXPECT_EQ(0, w.server_.create_count_);
    EXPECT_EQ(0, (int)w.stat_.clients_.size());
}

// The RTC server is on, but this vhost has no rtc section: WebRTC clients of the vhost are refused both ways.
VOID TEST(AppRtcApiTest, VhostWithoutRtc_WhipAndWhepRefused)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize("vhost __defaultVhost__ { } "));

    string response = w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_DISABLED)) << response;

    response = w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_DISABLED)) << response;

    EXPECT_EQ(0, w.server_.create_count_);
}

// With rtmp_to_rtc off, a WHEP player of a stream an RTMP encoder publishes is refused, for no WebRTC stream will
// ever come (#2728). A player of the same stream from a WHIP publisher is served.
VOID TEST(AppRtcApiTest, WhepPlayerOfRtmpStreamWithRtmpToRtcOff_Refused)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize("vhost __defaultVhost__ { rtc { enabled on; rtmp_to_rtc off; } } "));

    // An RTMP encoder publishes the stream.
    w.live_sources_.mock_source_->can_publish_ = false;

    string response = w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_DISABLED)) << response;
    EXPECT_EQ(0, w.server_.create_count_);
    EXPECT_EQ(0, (int)w.stat_.clients_.size());

    // A WHIP publisher publishes the stream, so it has a WebRTC source.
    w.rtc_sources_.mock_source_ = SrsSharedPtr<SrsRtcSource>(new SrsRtcSource());
    w.rtc_sources_.mock_source_->is_created_ = true;

    response = w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 201)) << response;
    EXPECT_EQ(1, w.server_.create_count_);
    EXPECT_FALSE(w.server_.publish_);
}

// A WHEP player whose offer the RTC server cannot answer, such as one with no codec the stream has, gets the server's
// error, and leaves the stats it was counted in for its hooks.
VOID TEST(AppRtcApiTest, WhepOfferServerCannotAnswer_PlayerLeavesStats)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));
    w.server_.create_error_ = srs_error_new(ERROR_RTC_SDP_EXCHANGE, "no track found");

    string response = w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 400)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_SDP_EXCHANGE)) << response;
    EXPECT_EQ(1, w.server_.create_count_);
    EXPECT_EQ(0, (int)w.stat_.clients_.size());
}

// A vhost whose hooks check only publishers lets WHEP players in without asking, and one whose hooks check only
// players lets WHIP publishers in without asking.
VOID TEST(AppRtcApiTest, HooksForOneDirectionOnly_OtherDirectionNotAsked)
{
    srs_error_t err = srs_success;

    if (true) {
        RtcApiUtestWhip w;
        HELPER_ASSERT_SUCCESS(w.initialize("vhost __defaultVhost__ { rtc { enabled on; } "
                                           "http_hooks { enabled on; on_publish http://127.0.0.1:8085/api/v1/streams; } } "));

        EXPECT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly")), 201));
        EXPECT_EQ(0, w.hooks_.on_play_count_);
        EXPECT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly")), 201));
        EXPECT_EQ(1, w.hooks_.on_publish_count_);
    }

    if (true) {
        RtcApiUtestWhip w;
        HELPER_ASSERT_SUCCESS(w.initialize("vhost __defaultVhost__ { rtc { enabled on; } "
                                           "http_hooks { enabled on; on_play http://127.0.0.1:8085/api/v1/sessions; } } "));

        EXPECT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly")), 201));
        EXPECT_EQ(0, w.hooks_.on_publish_count_);
        EXPECT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHEP, rtc_api_utest_offer("recvonly")), 201));
        EXPECT_EQ(1, w.hooks_.on_play_count_);
    }
}

// A second WHIP publisher of a stream that already has one is answered 409, as WHIP asks for a busy resource.
VOID TEST(AppRtcApiTest, WhipToStreamWithPublisher_Conflict409)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));
    w.server_.create_error_ = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "stream /live/livestream busy");

    string response = w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 409)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_SYSTEM_STREAM_BUSY)) << response;
    EXPECT_EQ(1, w.server_.create_count_);
}

// A WHIP client stops its session with a DELETE of the Location it was given. A DELETE without the token, or with
// another token, such as from someone who guessed the session's username, is refused and the session goes on.
VOID TEST(AppRtcApiTest, WhipDelete_OnlyTokenOwnerStopsSession)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));

    string response = w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly"));
    ASSERT_TRUE(rtc_api_utest_status(response, 201)) << response;
    string location = "/rtc/v1/whip/?action=delete&token=owner-token&app=live&stream=livestream&session=srvufrag:cliufrag";
    EXPECT_STREQ(location.c_str(), w.location_.c_str());

    string base = "http://127.0.0.1:1985/rtc/v1/whip/?action=delete&app=live&stream=livestream&session=srvufrag:cliufrag";
    response = w.request(SRS_CONSTS_HTTP_DELETE, base, "");
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_INVALID_SESSION)) << response;

    response = w.request(SRS_CONSTS_HTTP_DELETE, base + "&token=guessed-token", "");
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_INVALID_SESSION)) << response;
    EXPECT_EQ(0, w.server_.session_.expire_count_);

    response = w.request(SRS_CONSTS_HTTP_DELETE, "http://127.0.0.1:1985" + location, "");
    EXPECT_TRUE(rtc_api_utest_status(response, 200)) << response;
    EXPECT_EQ(1, w.server_.session_.expire_count_);
}

// A WHIP client that goes away while it sends its offer gets no session.
VOID TEST(AppRtcApiTest, WhipClientGoneWhileSendingOffer_NoSession)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));

    MockHttpMessageForRtcApi message;
    HELPER_ASSERT_SUCCESS(message.set_url(RTC_API_UTEST_WHIP, false));
    message.query_params_ = message.query_;
    message.body_read_all_error_ = srs_error_new(ERROR_SOCKET_READ, "connection reset by peer");

    MockResponseWriter writer;
    HELPER_EXPECT_SUCCESS(w.api_->serve_http(&writer, &message));
    string response(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    EXPECT_TRUE(rtc_api_utest_status(response, 500)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_SOCKET_READ)) << response;
    EXPECT_EQ(0, w.server_.create_count_);
}

// A WHIP publisher behind a reverse proxy, to a host that is a vhost of the config: the session is for the client the
// proxy names, not the proxy, and for that vhost. A host that is no vhost is the default vhost.
VOID TEST(AppRtcApiTest, WhipBehindProxyToNamedVhost_ClientAndVhostResolved)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST "vhost cdn.example.com { rtc { enabled on; } } "));

    string response = w.post("http://cdn.example.com/rtc/v1/whip/?app=live&stream=show", rtc_api_utest_offer("sendonly"), "1.2.3.4, 10.0.0.1");
    EXPECT_TRUE(rtc_api_utest_status(response, 201)) << response;
    EXPECT_STREQ("1.2.3.4", w.server_.ip_.c_str());
    EXPECT_STREQ("cdn.example.com", w.server_.vhost_.c_str());
    EXPECT_STREQ("live", w.server_.app_.c_str());
    EXPECT_STREQ("show", w.server_.stream_.c_str());
    EXPECT_TRUE(w.server_.publish_);

    response = w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 201)) << response;
    EXPECT_STREQ("10.0.0.1", w.server_.ip_.c_str());
    EXPECT_STREQ("__defaultVhost__", w.server_.vhost_.c_str());
}

// A client may set its own ICE ufrag and pwd in the query, as a test tool does to know the session's username.
// Those a browser would fail with, or that are too long, are refused; those at the limits are used.
VOID TEST(AppRtcApiTest, WhipCustomIceCredentials_OutOfRangeRefused)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));

    string pwd22 = "abcdefghijklmnopqrstuv";
    string ufrag32 = "abcdefghijklmnopqrstuvwxyz012345";
    string queries[] = {"ice-ufrag=abc", "ice-ufrag=" + ufrag32 + "6", "ice-pwd=" + pwd22.substr(1), "ice-pwd=" + pwd22 + "abcdefghijk"};
    for (int i = 0; i < 4; i++) {
        string response = w.post(RTC_API_UTEST_WHIP "&" + queries[i], rtc_api_utest_offer("sendonly"));
        EXPECT_TRUE(rtc_api_utest_status(response, 500)) << queries[i] << ": " << response;
        EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_INVALID_ICE)) << queries[i] << ": " << response;
    }
    EXPECT_EQ(0, w.server_.create_count_);

    string response = w.post(RTC_API_UTEST_WHIP "&ice-ufrag=abcd&ice-pwd=" + pwd22, rtc_api_utest_offer("sendonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 201)) << response;
    EXPECT_STREQ("abcd", w.server_.ice_ufrag_.c_str());
    EXPECT_STREQ(pwd22.c_str(), w.server_.ice_pwd_.c_str());

    response = w.post(RTC_API_UTEST_WHEP "&ice-ufrag=" + ufrag32 + "&ice-pwd=" + pwd22 + "abcdefghij", rtc_api_utest_offer("recvonly"));
    EXPECT_TRUE(rtc_api_utest_status(response, 201)) << response;
    EXPECT_STREQ(ufrag32.c_str(), w.server_.ice_ufrag_.c_str());
}

// A WHIP POST with no offer, or a body that is no SDP, is answered 400.
VOID TEST(AppRtcApiTest, WhipEmptyOrGarbageOffer_Refused400)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));

    string response = w.post(RTC_API_UTEST_WHIP, "");
    EXPECT_TRUE(rtc_api_utest_status(response, 400)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_INVALID_SDP)) << response;

    response = w.post(RTC_API_UTEST_WHIP, "{\"sdp\":\"v=0\"}");
    EXPECT_TRUE(rtc_api_utest_status(response, 400)) << response;
    EXPECT_TRUE(rtc_api_utest_code(response, ERROR_RTC_SDP_DECODE)) << response;

    EXPECT_EQ(0, w.server_.create_count_);
}

// The NACK API makes the session of a username drop the next packets, to test NACK recovery of a live client. A drop
// count that is no positive number, or a username with no session, is refused with its error code.
VOID TEST(AppRtcApiTest, NackApi_DropsOnNamedSessionOnly)
{
    srs_error_t err = srs_success;

    RtcApiUtestWhip w;
    HELPER_ASSERT_SUCCESS(w.initialize(RTC_API_UTEST_VHOST));
    ASSERT_TRUE(rtc_api_utest_status(w.post(RTC_API_UTEST_WHIP, rtc_api_utest_offer("sendonly")), 201));

    SrsGoApiRtcNACK api(&w.server_);
    string queries[] = {"username=srvufrag:cliufrag", "username=srvufrag:cliufrag&drop=0", "username=other&drop=3",
                        "username=srvufrag:cliufrag&drop=3"};
    int codes[] = {ERROR_RTC_INVALID_PARAMS, ERROR_RTC_INVALID_PARAMS, ERROR_RTC_NO_SESSION, ERROR_SUCCESS};
    for (int i = 0; i < 4; i++) {
        MockHttpMessageForRtcApi message;
        HELPER_ASSERT_SUCCESS(message.set_url("http://127.0.0.1:1985/rtc/v1/nack/?" + queries[i], false));
        message.query_params_ = message.query_;
        message.set_method(SRS_CONSTS_HTTP_GET);

        MockResponseWriter writer;
        HELPER_EXPECT_SUCCESS(api.serve_http(&writer, &message));
        string response(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
        EXPECT_NE(string::npos, response.find("\"code\":" + srs_strconv_format_int(codes[i]))) << queries[i] << ": " << response;
        EXPECT_EQ(i == 3 ? 3 : 0, w.server_.session_.nack_drop_) << queries[i];
    }
}

#ifdef SRS_FFMPEG_FIT
// The AAC of an RTMP stream transcoded to Opus for WebRTC players, as SrsRtcRtpBuilder does: frames of 1024 samples at
// 44.1kHz with their ADTS header, each with the time aac_dts[i] in ms. Each Opus frame's time is kept with the index of
// the AAC frame that gave it.
struct RtcCodecUtestOpus {
    std::vector<int64_t> dts_;
    std::vector<int> from_;
};

static void rtc_codec_utest_aac_to_opus(const std::vector<int64_t> &aac_dts, RtcCodecUtestOpus &opus)
{
    srs_error_t err = srs_success;

    std::vector<std::string> frames;
    encode_aac_adts_frames((int)aac_dts.size(), frames);
    ASSERT_EQ(aac_dts.size(), frames.size());

    SrsAudioTranscoder transcoder;
    HELPER_ASSERT_SUCCESS(transcoder.initialize(SrsAudioCodecIdAAC, SrsAudioCodecIdOpus, 2, 48000, 48000));

    for (int i = 0; i < (int)frames.size(); i++) {
        SrsParsedAudioPacket aac;
        aac.dts_ = aac_dts[i];
        aac.cts_ = 0;
        HELPER_ASSERT_SUCCESS(aac.add_sample((char *)frames[i].data(), (int)frames[i].size()));

        std::vector<SrsParsedAudioPacket *> outs;
        HELPER_ASSERT_SUCCESS(transcoder.transcode(&aac, outs));
        for (int j = 0; j < (int)outs.size(); j++) {
            opus.dts_.push_back(outs[j]->dts_);
            opus.from_.push_back(i);
        }
        transcoder.free_frames(outs);
    }
}

// The times of n AAC frames from start, 1024 samples at 44.1kHz each.
static void rtc_codec_utest_aac_times(int64_t start, int n, std::vector<int64_t> &aac_dts)
{
    for (int i = 0; i < n; i++) {
        aac_dts.push_back(start + (int64_t)i * 1024 * 1000 / 44100);
    }
}

static int rtc_codec_utest_count(const std::vector<std::string> &lines, const std::string &text)
{
    int n = 0;
    for (int i = 0; i < (int)lines.size(); i++) {
        if (lines[i].find(text) != std::string::npos) {
            n++;
        }
    }
    return n;
}

// The index of each Opus frame that is not 20ms after the one before it.
static std::vector<int> rtc_codec_utest_breaks(const RtcCodecUtestOpus &opus)
{
    std::vector<int> breaks;
    for (int i = 1; i < (int)opus.dts_.size(); i++) {
        int64_t step = opus.dts_[i] - opus.dts_[i - 1];
        if (step < 19 || step > 21) {
            breaks.push_back(i);
        }
    }
    return breaks;
}

// An RTMP encoder whose audio stops for 10s and comes back, such as a camera whose microphone was muted: the Opus for
// WebRTC players jumps to the new time with the AAC, so the audio stays in sync with the video, and goes on in 20ms
// steps on both sides of the jump. The jump is logged once.
VOID TEST(AppRtcCodecTest, RtmpAudioResumesAfter10s_OpusJumpsToNewTime)
{
    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    std::vector<int64_t> aac_dts;
    rtc_codec_utest_aac_times(0, 25, aac_dts);
    rtc_codec_utest_aac_times(10000, 25, aac_dts);

    RtcCodecUtestOpus opus;
    rtc_codec_utest_aac_to_opus(aac_dts, opus);
    _srs_log = original_log;

    std::vector<int> breaks = rtc_codec_utest_breaks(opus);
    ASSERT_EQ(1, (int)breaks.size());
    int jump = breaks[0];
    ASSERT_GT(jump, 20);
    ASSERT_GT((int)opus.dts_.size(), jump + 20);

    // The encoder hands out each Opus frame one AAC frame later, so the new time comes with the first or second AAC
    // frame after the gap.
    EXPECT_GE(opus.from_[jump], 25);
    EXPECT_LE(opus.from_[jump], 26);
    // Before the gap, Opus follows the AAC from 0. The encoder's 6.5ms delay moves a frame a little before the AAC.
    EXPECT_LE(opus.dts_[jump - 1], aac_dts[24] + 40);
    // After the gap, Opus goes on from the AAC's new time.
    EXPECT_GE(opus.dts_[jump], 10000 - 20);
    EXPECT_LE(opus.dts_[jump], 10000 + 40);

    EXPECT_EQ(1, rtc_codec_utest_count(log.traces_, "time diff to large"));
}

// An RTMP encoder whose audio time goes back, such as an encoder that restarts its clock at 0 on the same connection:
// the Opus for WebRTC players starts again at 0 with the AAC, instead of going on from 60s.
VOID TEST(AppRtcCodecTest, RtmpAudioTimeRestartsAtZero_OpusRestartsWithIt)
{
    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    std::vector<int64_t> aac_dts;
    rtc_codec_utest_aac_times(60000, 25, aac_dts);
    rtc_codec_utest_aac_times(0, 25, aac_dts);

    RtcCodecUtestOpus opus;
    rtc_codec_utest_aac_to_opus(aac_dts, opus);
    _srs_log = original_log;

    std::vector<int> breaks = rtc_codec_utest_breaks(opus);
    ASSERT_EQ(1, (int)breaks.size());
    int reset = breaks[0];
    ASSERT_GT(reset, 20);
    ASSERT_GT((int)opus.dts_.size(), reset + 20);

    EXPECT_GE(opus.from_[reset], 25);
    EXPECT_LE(opus.from_[reset], 26);
    EXPECT_GE(opus.dts_[reset - 1], 60000 - 20);
    EXPECT_GE(opus.dts_[reset], -20);
    EXPECT_LE(opus.dts_[reset], 40);

    EXPECT_EQ(1, rtc_codec_utest_count(log.traces_, "time diff to large"));
}

// An RTMP encoder whose audio has a gap under 1s, such as a few frames lost on a busy network: the Opus for WebRTC
// players goes on in 20ms steps over the gap and does not jump, so the players hear no break.
VOID TEST(AppRtcCodecTest, RtmpAudioGapUnder1s_OpusStaysContinuous)
{
    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    std::vector<int64_t> aac_dts;
    rtc_codec_utest_aac_times(0, 25, aac_dts);
    rtc_codec_utest_aac_times(aac_dts.back() + 900, 25, aac_dts);

    RtcCodecUtestOpus opus;
    rtc_codec_utest_aac_to_opus(aac_dts, opus);
    _srs_log = original_log;

    ASSERT_GT((int)opus.dts_.size(), 40);
    for (int i = 1; i < (int)opus.dts_.size(); i++) {
        EXPECT_GE(opus.dts_[i] - opus.dts_[i - 1], 19) << "frame " << i;
        EXPECT_LE(opus.dts_[i] - opus.dts_[i - 1], 21) << "frame " << i;
    }

    EXPECT_EQ(0, rtc_codec_utest_count(log.traces_, "time diff to large"));
}

// An RTMP encoder that sends one corrupt AAC frame, its ADTS header right but its data garbage: the AAC decoder's error
// goes to the SRS error log through the FFmpeg log callback, the frame fails to transcode, and the next good frames
// give Opus again, so the WebRTC players lose only that frame.
VOID TEST(AppRtcCodecTest, RtmpCorruptAacFrame_DecoderErrorLoggedThenRecovers)
{
    srs_error_t err = srs_success;

    std::vector<std::string> frames;
    encode_aac_adts_frames(30, frames);
    ASSERT_EQ(30, (int)frames.size());

    // Frame 10 keeps its 7-byte ADTS header, with garbage after it.
    std::string corrupt = frames[10];
    for (int i = 7; i < (int)corrupt.size(); i++) {
        corrupt[i] = (char)(0xff - i * 7);
    }
    frames[10] = corrupt;

    SrsAudioTranscoder transcoder;
    HELPER_ASSERT_SUCCESS(transcoder.initialize(SrsAudioCodecIdAAC, SrsAudioCodecIdOpus, 2, 48000, 48000));

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;
    bool original_disabled = SrsFFmpegLogHelper::disabled_;
    SrsFFmpegLogHelper::disabled_ = false;

    int errors_before = -1;
    int failed = 0;
    int opus_after = 0;
    for (int i = 0; i < (int)frames.size(); i++) {
        if (i == 10) {
            errors_before = log.error_count_;
        }

        SrsParsedAudioPacket aac;
        aac.dts_ = (int64_t)i * 1024 * 1000 / 44100;
        aac.cts_ = 0;
        HELPER_EXPECT_SUCCESS(aac.add_sample((char *)frames[i].data(), (int)frames[i].size()));

        std::vector<SrsParsedAudioPacket *> outs;
        err = transcoder.transcode(&aac, outs);
        if (err != srs_success) {
            EXPECT_EQ(10, i);
            EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err));
            failed++;
            srs_freep(err);
        } else if (i > 10) {
            opus_after += (int)outs.size();
        }
        transcoder.free_frames(outs);
    }

    SrsFFmpegLogHelper::disabled_ = original_disabled;
    _srs_log = original_log;

    EXPECT_EQ(1, failed);
    EXPECT_EQ(0, errors_before);
    EXPECT_GT(log.error_count_, errors_before);
    EXPECT_GT(opus_after, 15);
}

extern SrsRtpPacket *create_mock_rtp_packet(bool is_audio, int64_t avsync_time, uint32_t ssrc, uint16_t seq, uint32_t ts);

// A WHIP client that asks for G.722 or Speex audio with `?acodec=`, as Chrome offers G.722, publishing to a vhost
// with `rtc_to_rtmp on`: there is no decoder to transcode the audio to AAC, as for G.711, so the stream is converted
// to RTMP without its audio while WebRTC players still get it, and the publish is not refused.
VOID TEST(AppRtcCodecTest, WhipG722WithRtcToRtmp_ConvertedWithoutAudio)
{
    srs_error_t err = srs_success;

    // The SDP's track codec names G722 and SPEEX, which map to codecs with no decoder.
    std::string names[] = {"G722", "SPEEX"};
    for (int i = 0; i < 2; i++) {
        MockRtcFrameTarget target;
        SrsRtcFrameBuilder builder(_srs_app_factory, &target);
        MockRequest req("__defaultVhost__", "live", "livestream");

        SrsAudioCodecId codec = srs_audio_codec_str2id(names[i]);
        HELPER_EXPECT_SUCCESS(builder.initialize(&req, codec, SrsVideoCodecIdAVC));
        EXPECT_TRUE(builder.audio_transcoder_ == NULL);
        HELPER_EXPECT_SUCCESS(builder.on_publish());

        // The audio packets are dropped, with no AAC sequence header and no frame.
        for (int j = 0; j < 5; j++) {
            SrsUniquePtr<SrsRtpPacket> pkt(create_mock_rtp_packet(true, 1000 + j * 20, 11111, 100 + j, 8000 + j * 160));
            HELPER_EXPECT_SUCCESS(builder.on_rtp(pkt.get()));
        }
        EXPECT_EQ(0, target.on_frame_count_);
    }
}
#endif

// The factory of an RTC server whose publish and play streams find the server's RTC sources, and whose candidate
// discovery gets a fresh utility each time, as the connection frees it.
class RtcConnUtestFactory : public MockAppFactoryForRtcConn
{
public:
    virtual ISrsProtocolUtility *create_protocol_utility()
    {
        return new MockProtocolUtility("192.168.1.100");
    }
    virtual ISrsRtcPlayStream *create_rtc_play_stream(ISrsExecRtcAsyncTask *exec, ISrsExpire *expire, ISrsRtcPacketSender *sender, const SrsContextId &cid)
    {
        SrsRtcPlayStream *player = new SrsRtcPlayStream(exec, expire, sender, cid);
        player->rtc_sources_ = rtc_sources_;
        return player;
    }
};

// A client's UDP address, which keeps every packet the server sends to it.
class RtcConnUtestSocket : public MockUdpMuxSocket
{
public:
    std::vector<std::string> sent_;

public:
    virtual srs_error_t sendto(void *data, int size, srs_utime_t timeout)
    {
        sent_.push_back(std::string((char *)data, size));
        return MockUdpMuxSocket::sendto(data, size, timeout);
    }
};

// The RTC server from config text: the stream's source, the sessions' manager, the circuit breaker and the DTLS
// certificate every WebRTC session of a client shares.
class RtcConnUtestServer
{
public:
    MockSrsConfig conf_;
    MockCircuitBreaker circuit_breaker_;
    MockConnectionManager conn_manager_;
    MockRtcSourceManager rtc_sources_;
    MockDtlsCertificate dtls_certificate_;
    RtcConnUtestFactory app_factory_;
    MockRtcAsyncTaskExecutor exec_;
    MockRtcSource *source_;

public:
    RtcConnUtestServer()
    {
        dtls_certificate_.fingerprint_ = "test-fingerprint";
        app_factory_.rtc_sources_ = &rtc_sources_;
        source_ = new MockRtcSource();
        rtc_sources_.mock_source_ = SrsSharedPtr<SrsRtcSource>(source_);
    }

public:
    srs_error_t initialize(std::string text = "")
    {
        if (text.find("rtc_server") == std::string::npos) {
            text = "rtc_server { enabled on; listen 8000; candidate 192.168.1.100; } " + text;
        }
        return conf_.mock_parse(string(_MIN_OK_CONF) + text);
    }
};

// A WebRTC client of the server, a WHIP publisher or a WHEP player, with its own session, as SrsRtcSessionManager
// makes one for each offer.
class RtcConnUtestClient
{
public:
    RtcConnUtestServer *server_;
    SrsContextId cid_;
    SrsUniquePtr<ISrsRtcConnection> conn_ptr_;
    SrsRtcConnection *conn_;
    SrsRtcUserConfig ruc_;
    SrsSdp answer_;
    RtcConnUtestSocket skt_;

public:
    RtcConnUtestClient(RtcConnUtestServer *server) : conn_ptr_(new SrsRtcConnection(&server->exec_, cid_))
    {
        server_ = server;

        conn_ = dynamic_cast<SrsRtcConnection *>(conn_ptr_.get());
        conn_->circuit_breaker_ = &server->circuit_breaker_;
        conn_->conn_manager_ = &server->conn_manager_;
        conn_->rtc_sources_ = &server->rtc_sources_;
        conn_->config_ = &server->conf_;
        conn_->dtls_certificate_ = &server->dtls_certificate_;
        conn_->app_factory_ = &server->app_factory_;
        conn_->assemble();

        SrsRtcPublisherNegotiator *pub_neg = dynamic_cast<SrsRtcPublisherNegotiator *>(conn_->publisher_negotiator_);
        pub_neg->config_ = &server->conf_;
        SrsRtcPlayerNegotiator *play_neg = dynamic_cast<SrsRtcPlayerNegotiator *>(conn_->player_negotiator_);
        play_neg->config_ = &server->conf_;
        play_neg->rtc_sources_ = &server->rtc_sources_;

        srs_freep(ruc_.req_);
        ruc_.req_ = new MockRtcAsyncCallRequest("__defaultVhost__", "live", "livestream");
        skt_.peer_id_ = "10.0.0.1:50000";
    }
    ~RtcConnUtestClient()
    {
        SrsRtcUdpNetwork *udp = dynamic_cast<SrsRtcUdpNetwork *>(conn_->networks_->udp());
        udp->sendonly_skt_ = NULL;
    }

public:
    // Sends the offer to the WHIP API, with the codecs a client asks for by `?vcodec=` and `?acodec=`.
    srs_error_t publish(std::string offer, std::string vcodec = "", std::string acodec = "")
    {
        ruc_.publish_ = true;
        ruc_.vcodec_ = vcodec;
        ruc_.acodec_ = acodec;
        return exchange(offer);
    }

    // Sends the offer to the WHEP API, with the codec a client asks for by `?codec=`.
    srs_error_t play(std::string offer, std::string vcodec = "")
    {
        ruc_.publish_ = false;
        ruc_.vcodec_ = vcodec;
        return exchange(offer);
    }

    // The client's ICE and DTLS are done with encryption off, so the session sends to the client's UDP address.
    srs_error_t connect()
    {
        srs_error_t err = srs_success;

        if ((err = conn_->initialize(ruc_.req_, false, false, "utest")) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }

        SrsRtcUdpNetwork *udp = dynamic_cast<SrsRtcUdpNetwork *>(conn_->networks_->udp());
        udp->sendonly_skt_ = &skt_;
        udp->set_state(SrsRtcNetworkStateEstablished);

        return err;
    }

    // The media sections of the answer of the type, audio or video.
    std::vector<SrsMediaDesc *> answered(std::string type)
    {
        return answer_.find_media_descs(type);
    }

    // The publish stream of the client, after it published.
    SrsRtcPublishStream *publisher()
    {
        return dynamic_cast<SrsRtcPublishStream *>(conn_->publishers_.begin()->second);
    }

    // The play stream of the client, after it played.
    SrsRtcPlayStream *player()
    {
        return dynamic_cast<SrsRtcPlayStream *>(conn_->players_.begin()->second);
    }

    srs_error_t on_rtp(std::string bytes)
    {
        srs_error_t err = srs_success;

        std::string copy = bytes;
        bool dropped = false;
        if ((err = conn_->on_rtp_cipher((char *)copy.data(), (int)copy.size(), &dropped)) != srs_success) {
            return srs_error_wrap(err, "cipher");
        }
        if (dropped) {
            return err;
        }
        return conn_->on_rtp_plaintext((char *)copy.data(), (int)copy.size());
    }

    srs_error_t on_rtcp(std::string bytes)
    {
        std::string copy = bytes;
        return conn_->on_rtcp((char *)copy.data(), (int)copy.size());
    }

private:
    srs_error_t exchange(std::string offer)
    {
        srs_error_t err = srs_success;

        ruc_.remote_sdp_str_ = offer;
        if ((err = ruc_.remote_sdp_.parse(offer)) != srs_success) {
            return srs_error_wrap(err, "parse offer");
        }

        answer_.session_config_.dtls_role_ = server_->conf_.get_rtc_dtls_role(ruc_.req_->vhost_);
        answer_.session_config_.dtls_version_ = server_->conf_.get_rtc_dtls_version(ruc_.req_->vhost_);

        if (ruc_.publish_) {
            err = conn_->add_publisher(&ruc_, answer_);
        } else {
            err = conn_->add_player(&ruc_, answer_);
        }
        if (err != srs_success) {
            return srs_error_wrap(err, "add");
        }

        conn_->set_all_tracks_status(ruc_.req_->get_stream_url(), ruc_.publish_, true);

        std::string username;
        if ((err = conn_->generate_local_sdp(&ruc_, answer_, username)) != srs_success) {
            return srs_error_wrap(err, "local sdp");
        }

        return err;
    }
};

// One m= section of a browser's offer: the transport lines of every section, then the codec and SSRC lines.
static std::string rtc_conn_utest_section(std::string kind, std::string mid, std::string direction, std::string pts, std::string lines)
{
    return "m=" + kind + " 9 UDP/TLS/RTP/SAVPF " + pts + "\r\n" +
           "c=IN IP4 0.0.0.0\r\n"
           "a=rtcp:9 IN IP4 0.0.0.0\r\n"
           "a=ice-ufrag:utst\r\n"
           "a=ice-pwd:utestpassword1234567890ab\r\n"
           "a=ice-options:trickle\r\n"
           "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
           "a=setup:actpass\r\n"
           "a=mid:" +
           mid + "\r\n" +
           "a=" + direction + "\r\n" +
           "a=rtcp-mux\r\n" + lines;
}

static std::string rtc_conn_utest_offer(std::string bundle, std::string sections)
{
    return "v=0\r\n"
           "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
           "s=-\r\n"
           "t=0 0\r\n"
           "a=group:BUNDLE " +
           bundle + "\r\n" +
           "a=msid-semantic: WMS stream\r\n" + sections;
}

#define RTC_CONN_UTEST_TWCC_EXTMAP "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"

// Chrome's Opus section of a publisher, on SSRC 1001.
static std::string rtc_conn_utest_opus_publisher()
{
    return rtc_conn_utest_section("audio", "0", "sendonly", "111",
                                  "a=rtpmap:111 opus/48000/2\r\n"
                                  "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
                                  "a=ssrc:1001 cname:utest\r\n"
                                  "a=ssrc:1001 msid:stream audio\r\n");
}

// Chrome's H.264 section of a publisher, on SSRC 2001.
static std::string rtc_conn_utest_h264_publisher()
{
    return rtc_conn_utest_section("video", "1", "sendonly", "106",
                                  "a=rtpmap:106 H264/90000\r\n"
                                  "a=rtcp-fb:106 nack\r\n"
                                  "a=rtcp-fb:106 nack pli\r\n"
                                  "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
                                  "a=ssrc:2001 cname:utest\r\n"
                                  "a=ssrc:2001 msid:stream video\r\n");
}

// A publisher's video section of one codec, on SSRC 2001.
static std::string rtc_conn_utest_video_publisher(std::string pt, std::string codec)
{
    return rtc_conn_utest_section("video", "1", "sendonly", pt,
                                  RTC_CONN_UTEST_TWCC_EXTMAP
                                  "a=rtpmap:" +
                                      pt + " " + codec + "/90000\r\n" +
                                      "a=rtcp-fb:" + pt + " nack\r\n" +
                                      "a=rtcp-fb:" + pt + " nack pli\r\n" +
                                      "a=rtcp-fb:" + pt + " transport-cc\r\n" +
                                      "a=ssrc:2001 cname:utest\r\n"
                                      "a=ssrc:2001 msid:stream video\r\n");
}

// A player's Opus section.
static std::string rtc_conn_utest_opus_player()
{
    return rtc_conn_utest_section("audio", "0", "recvonly", "111", "a=rtpmap:111 opus/48000/2\r\n");
}

// A player's video section of one codec, with TWCC as Chrome offers it.
static std::string rtc_conn_utest_video_player(std::string pt, std::string codec, std::string fmtp = "")
{
    std::string lines = RTC_CONN_UTEST_TWCC_EXTMAP "a=rtpmap:" + pt + " " + codec + "/90000\r\n" +
                        "a=rtcp-fb:" + pt + " nack\r\n" +
                        "a=rtcp-fb:" + pt + " nack pli\r\n" +
                        "a=rtcp-fb:" + pt + " transport-cc\r\n";
    if (!fmtp.empty()) {
        lines += "a=fmtp:" + pt + " " + fmtp + "\r\n";
    }
    return rtc_conn_utest_section("video", "1", "recvonly", pt, lines);
}

static bool rtc_conn_utest_has_fb(const SrsMediaPayloadType &pt, std::string fb)
{
    return std::find(pt.rtcp_fb_.begin(), pt.rtcp_fb_.end(), fb) != pt.rtcp_fb_.end();
}

// An RTP packet of a publisher, with the TWCC sequence number in extension 3 if it has one.
static std::string rtc_conn_utest_rtp(uint32_t ssrc, uint8_t pt, uint16_t seq, int twcc_sn = -1)
{
    SrsRtpPacket pkt;
    pkt.header_.set_ssrc(ssrc);
    pkt.header_.set_payload_type(pt);
    pkt.header_.set_sequence(seq);
    pkt.header_.set_timestamp(90000 + seq * 3000);
    pkt.header_.set_marker(true);
    if (twcc_sn >= 0) {
        srs_error_t err = pkt.header_.set_twcc_sequence_number(3, (uint16_t)twcc_sn);
        srs_freep(err);
    }

    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    static char payload[] = {0x65, (char)0x88, (char)0x84, 0x00};
    raw->payload_ = payload;
    raw->nn_payload_ = sizeof(payload);
    pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    char buf[kRtpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));
    srs_error_t err = pkt.encode(&stream);
    srs_freep(err);
    return std::string(buf, stream.pos());
}

// Safari publishes HEVC by WHIP with `?codec=hevc`, and a player that asks for HEVC plays it: each answer takes the
// H.265 payload type the client offered, with NACK and TWCC feedback, and the player's answer the TWCC extension too.
VOID TEST(AppRtcConnTest, WhipHevcPublishedThenPlayedByHevcPlayer)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    std::string offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_video_publisher("49", "H265"));
    HELPER_ASSERT_SUCCESS(publisher.publish(offer, "hevc"));

    std::vector<SrsMediaDesc *> videos = publisher.answered("video");
    ASSERT_EQ(1, (int)videos.size());
    ASSERT_EQ(1, (int)videos[0]->payload_types_.size());
    SrsMediaPayloadType &pub = videos[0]->payload_types_[0];
    EXPECT_EQ(49, pub.payload_type_);
    EXPECT_STREQ("H265", pub.encoding_name_.c_str());
    EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "nack"));
    EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "nack pli"));
    EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "transport-cc"));

    RtcConnUtestClient player(&server);
    offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("100", "H265", "level-id=93;profile-id=1;tier-flag=0;tx-mode=SRST"));
    HELPER_ASSERT_SUCCESS(player.play(offer, "hevc"));

    videos = player.answered("video");
    ASSERT_EQ(1, (int)videos.size());
    ASSERT_EQ(1, (int)videos[0]->payload_types_.size());
    SrsMediaPayloadType &play = videos[0]->payload_types_[0];
    EXPECT_EQ(100, play.payload_type_);
    EXPECT_STREQ("H265", play.encoding_name_.c_str());
    EXPECT_TRUE(rtc_conn_utest_has_fb(play, "transport-cc"));
    EXPECT_EQ(kTWCCExt, videos[0]->extmaps_[3]);
    // The player's packets are the publisher's, with the player's payload type.
    EXPECT_EQ(49, player.player()->video_tracks_.begin()->second->track_desc_->media_->pt_of_publisher_);
}

// A WHIP client asks for a codec with `?codec=` that its offer does not have, such as a browser without HEVC or
// AV1, or for the default H.264 with an offer of VP8 only: the publish is refused at the offer.
VOID TEST(AppRtcConnTest, WhipCodecAskedForButNotOfferedRefused)
{
    srs_error_t err;

    struct {
        const char *vcodec_;
        const char *offered_;
    } cases[] = {
        {"hevc", "H264"},
        {"av1", "H264"},
        {"vp9", "H264"},
        {"", "VP8"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());

        RtcConnUtestClient publisher(&server);
        std::string offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_video_publisher("96", cases[i].offered_));
        err = publisher.publish(offer, cases[i].vcodec_);
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err)) << cases[i].vcodec_;
        srs_freep(err);
        EXPECT_TRUE(publisher.conn_->publishers_.empty());
    }
}

// Chrome M96 names AV1 `AV1X`. It publishes AV1 with `?codec=av1`, and an M96 player plays it with `?codec=av1`:
// both answers are AV1X, the encoding name the clients offered.
// @see https://bugs.chromium.org/p/webrtc/issues/detail?id=13166
VOID TEST(AppRtcConnTest, ChromeM96PublishesAndPlaysAv1AsAv1x)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    std::string offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_video_publisher("35", "AV1X"));
    HELPER_ASSERT_SUCCESS(publisher.publish(offer, "av1"));
    ASSERT_EQ(1, (int)publisher.answered("video").size());
    EXPECT_STREQ("AV1X", publisher.answered("video")[0]->payload_types_[0].encoding_name_.c_str());

    RtcConnUtestClient player(&server);
    offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("45", "AV1X"));
    HELPER_ASSERT_SUCCESS(player.play(offer, "av1"));
    ASSERT_EQ(1, (int)player.answered("video").size());
    SrsMediaPayloadType &play = player.answered("video")[0]->payload_types_[0];
    EXPECT_EQ(45, play.payload_type_);
    EXPECT_STREQ("AV1X", play.encoding_name_.c_str());

    // A player that asks for AV1 from a browser without it, such as Safari, is refused.
    RtcConnUtestClient h264_player(&server);
    offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"));
    err = h264_player.play(offer, "av1");
    EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err));
    srs_freep(err);
}

// Chrome publishes VP9 with `?codec=vp9` and TWCC, and a VP9 player plays it, while a player without VP9 is refused,
// as SRS does not transcode for WebRTC.
VOID TEST(AppRtcConnTest, WhipVp9PublishedThenPlayedOnlyByVp9Player)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    std::string offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_video_publisher("98", "VP9"));
    HELPER_ASSERT_SUCCESS(publisher.publish(offer, "vp9"));
    ASSERT_EQ(1, (int)publisher.answered("video").size());
    SrsMediaPayloadType &pub = publisher.answered("video")[0]->payload_types_[0];
    EXPECT_STREQ("VP9", pub.encoding_name_.c_str());
    EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "transport-cc"));
    // The publisher is told the TWCC numbers its packets carry.
    EXPECT_EQ(3, publisher.publisher()->twcc_id_);

    RtcConnUtestClient player(&server);
    offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("101", "VP9"));
    HELPER_ASSERT_SUCCESS(player.play(offer));
    EXPECT_STREQ("VP9", player.answered("video")[0]->payload_types_[0].encoding_name_.c_str());

    RtcConnUtestClient h264_player(&server);
    offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"));
    err = h264_player.play(offer, "vp9");
    EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err));
    srs_freep(err);
}

// H.264 offers without the ideal payload type, profile 42e01f with packetization-mode=1 and
// level-asymmetry-allowed=1, as from embedded and hardware clients: the first fully set payload of another profile is
// taken, else the first one without fmtp, else the first one at all.
VOID TEST(AppRtcConnTest, WhipH264WithoutIdealPayloadTakesBestOffered)
{
    srs_error_t err;

    struct {
        const char *name_;
        const char *lines_;
        const char *pts_;
        int chosen_;
    } cases[] = {
        // A High profile camera, which has no Baseline 42e01f.
        {"high-only",
         "a=rtpmap:102 H264/90000\r\n"
         "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=640c1f\r\n"
         "a=rtpmap:104 H264/90000\r\n"
         "a=fmtp:104 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640c1f\r\n",
         "102 104", 104},
        // A client that offers H.264 with no fmtp at all, after one only in packetization-mode=0.
        {"no-fmtp",
         "a=rtpmap:102 H264/90000\r\n"
         "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f\r\n"
         "a=rtpmap:96 H264/90000\r\n",
         "102 96", 96},
        // Only 42e01f in packetization-mode=0.
        {"mode-0-only",
         "a=rtpmap:102 H264/90000\r\n"
         "a=rtcp-fb:102 nack\r\n"
         "a=rtcp-fb:102 transport-cc\r\n"
         "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f\r\n",
         "102", 102},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());

        RtcConnUtestClient publisher(&server);
        std::string video = rtc_conn_utest_section("video", "1", "sendonly", cases[i].pts_,
                                                   std::string(RTC_CONN_UTEST_TWCC_EXTMAP) + cases[i].lines_ +
                                                       "a=ssrc:2001 cname:utest\r\n"
                                                       "a=ssrc:2001 msid:stream video\r\n");
        HELPER_EXPECT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + video)));

        ASSERT_EQ(1, (int)publisher.answered("video").size()) << cases[i].name_;
        SrsMediaPayloadType &pub = publisher.answered("video")[0]->payload_types_[0];
        EXPECT_EQ(cases[i].chosen_, pub.payload_type_) << cases[i].name_;
        EXPECT_STREQ("H264", pub.encoding_name_.c_str());
    }

    // The fallback keeps the feedback the client offered for it.
    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());
    RtcConnUtestClient publisher(&server);
    std::string video = rtc_conn_utest_section("video", "1", "sendonly", "102", std::string(RTC_CONN_UTEST_TWCC_EXTMAP) + cases[2].lines_ + "a=ssrc:2001 cname:utest\r\n");
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + video)));
    SrsMediaPayloadType &pub = publisher.answered("video")[0]->payload_types_[0];
    EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "nack"));
    EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "transport-cc"));
}

// The audio of a WHIP offer: Opus with NACK and TWCC feedback, as Chrome with audio NACK or Pion offers it, an IP camera's or SIP gateway's G.711 alone (PCMU
// first, as Chrome orders them), and the codecs SRS does not take, G.722 alone or a codec asked for by
// `?acodec=` that the offer lacks, which are refused.
VOID TEST(AppRtcConnTest, WhipAudioCodecOfTheOffer)
{
    srs_error_t err;

    struct {
        const char *lines_;
        const char *pts_;
        const char *acodec_;
        const char *chosen_;
    } cases[] = {
        {"a=rtpmap:111 opus/48000/2\r\n"
         "a=rtcp-fb:111 nack\r\n"
         "a=rtcp-fb:111 transport-cc\r\n",
         "111", "", "opus"},
        {"a=rtpmap:0 PCMU/8000\r\n"
         "a=rtpmap:8 PCMA/8000\r\n",
         "0 8", "", "PCMU"},
        {"a=rtpmap:8 PCMA/8000\r\n", "8", "", "PCMA"},
        {"a=rtpmap:9 G722/8000\r\n", "9", "", NULL},
        {"a=rtpmap:111 opus/48000/2\r\n", "111", "PCMA", NULL},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());

        RtcConnUtestClient publisher(&server);
        std::string audio = rtc_conn_utest_section("audio", "0", "sendonly", cases[i].pts_,
                                                   std::string(RTC_CONN_UTEST_TWCC_EXTMAP) + cases[i].lines_ +
                                                       "a=ssrc:1001 cname:utest\r\n"
                                                       "a=ssrc:1001 msid:stream audio\r\n");
        err = publisher.publish(rtc_conn_utest_offer("0 1", audio + rtc_conn_utest_h264_publisher()), "", cases[i].acodec_);

        if (!cases[i].chosen_) {
            EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err)) << cases[i].pts_;
            srs_freep(err);
            continue;
        }

        HELPER_EXPECT_SUCCESS(err);
        ASSERT_EQ(1, (int)publisher.answered("audio").size());
        SrsMediaPayloadType &pub = publisher.answered("audio")[0]->payload_types_[0];
        EXPECT_STREQ(cases[i].chosen_, pub.encoding_name_.c_str());
        if (i == 0) {
            EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "nack"));
            EXPECT_TRUE(rtc_conn_utest_has_fb(pub, "transport-cc"));
        }
    }
}

// A G.711 stream, from an IP camera or a SIP gateway by WHIP, is played by a player that offers the same codec, and a
// player that offers Opus only is refused, as SRS does not transcode for WebRTC.
VOID TEST(AppRtcConnTest, G711StreamPlayedOnlyByPlayerWithIt)
{
    srs_error_t err;

    const char *codecs[] = {"PCMU", "PCMA"};
    for (int i = 0; i < 2; i++) {
        std::string pt = i == 0 ? "0" : "8";
        std::string rtpmap = "a=rtpmap:" + pt + " " + codecs[i] + "/8000\r\n";

        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());

        RtcConnUtestClient publisher(&server);
        std::string audio = rtc_conn_utest_section("audio", "0", "sendonly", pt, rtpmap + "a=ssrc:1001 cname:utest\r\n");
        HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", audio + rtc_conn_utest_h264_publisher())));

        RtcConnUtestClient player(&server);
        audio = rtc_conn_utest_section("audio", "0", "recvonly", "111 " + pt, "a=rtpmap:111 opus/48000/2\r\n" + rtpmap);
        HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", audio + rtc_conn_utest_video_player("106", "H264"))));
        ASSERT_EQ(1, (int)player.answered("audio").size());
        EXPECT_STREQ(codecs[i], player.answered("audio")[0]->payload_types_[0].encoding_name_.c_str());

        RtcConnUtestClient opus_player(&server);
        err = opus_player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264")));
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err)) << codecs[i];
        srs_freep(err);
    }
}

// A player that offers a kind of media the stream does not have, or only codecs the stream is not in, is refused at
// the offer, rather than answered without that media, which leaves the client waiting for a track that never comes:
// audio for a video-only stream, video for an audio-only stream, VP8 for an H.264 stream, and `?codec=vp9` before
// any publisher, when the stream only has its placeholder tracks.
// @see https://github.com/ossrs/srs/issues/4738
VOID TEST(AppRtcConnTest, PlayerOfferingMediaTheStreamLacksRefused)
{
    srs_error_t err;

    std::string player_offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"));

    if (true) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());
        RtcConnUtestClient publisher(&server);
        HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("1", rtc_conn_utest_h264_publisher())));

        RtcConnUtestClient player(&server);
        err = player.play(player_offer);
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find("no audio track") != std::string::npos);
        srs_freep(err);
    }

    if (true) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());
        RtcConnUtestClient publisher(&server);
        HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0", rtc_conn_utest_opus_publisher())));

        RtcConnUtestClient player(&server);
        err = player.play(player_offer);
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find("no video track") != std::string::npos);
        srs_freep(err);
    }

    if (true) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());
        RtcConnUtestClient publisher(&server);
        HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));

        RtcConnUtestClient player(&server);
        err = player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("96", "VP8")));
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err));
        srs_freep(err);
    }

    if (true) {
        RtcConnUtestServer server;
        HELPER_ASSERT_SUCCESS(server.initialize());
        MockRtcAsyncCallRequest req("__defaultVhost__", "live", "livestream");
        HELPER_ASSERT_SUCCESS(server.source_->initialize(&req));

        RtcConnUtestClient player(&server);
        err = player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("98", "VP9")), "vp9");
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err));
        EXPECT_TRUE(srs_error_desc(err).find("no VP9 track") != std::string::npos);
        srs_freep(err);
    }
}

// Chrome offers RED for Opus and RED with ULPFEC for video. Its publish answer keeps the video RED, and a Chrome
// player gets RED on both tracks, with the publisher's RED payload types mapped to its own. When another publisher
// with other RED payload types takes over the stream, the player maps the new ones.
VOID TEST(AppRtcConnTest, ChromeRedPublishedThenPlayedAndRepublished)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    // A publisher offers RED with its payload types, Opus 111 with RED 63, H.264 106 with RED 121 and ULPFEC 122.
    struct RedPts {
        const char *audio_red_;
        const char *video_red_;
        uint32_t base_ssrc_;
    };
    struct RedOffer {
        static std::string publisher(RedPts p)
        {
            std::string ar = p.audio_red_, vr = p.video_red_;
            char a[16], v[16];
            snprintf(a, sizeof(a), "%u", p.base_ssrc_);
            snprintf(v, sizeof(v), "%u", p.base_ssrc_ + 1);
            std::string audio = rtc_conn_utest_section("audio", "0", "sendonly", "111 " + ar,
                                                       "a=rtpmap:111 opus/48000/2\r\n"
                                                       "a=rtpmap:" +
                                                           ar + " red/48000/2\r\n" +
                                                           "a=fmtp:" + ar + " 111/111\r\n" +
                                                           "a=ssrc:" + a + " cname:utest\r\n" +
                                                           "a=ssrc:" + a + " msid:stream audio\r\n");
            std::string video = rtc_conn_utest_section("video", "1", "sendonly", "106 " + vr + " 122",
                                                       "a=rtpmap:106 H264/90000\r\n"
                                                       "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
                                                       "a=rtpmap:" +
                                                           vr + " red/90000\r\n" +
                                                           "a=rtpmap:122 ulpfec/90000\r\n"
                                                           "a=ssrc:" +
                                                           v + " cname:utest\r\n" +
                                                           "a=ssrc:" + v + " msid:stream video\r\n");
            return rtc_conn_utest_offer("0 1", audio + video);
        }
    };

    RedPts first = {"63", "121", 1001};
    RtcConnUtestClient *publisher = new RtcConnUtestClient(&server);
    HELPER_ASSERT_SUCCESS(publisher->publish(RedOffer::publisher(first)));
    std::vector<SrsMediaDesc *> videos = publisher->answered("video");
    ASSERT_EQ(1, (int)videos.size());
    ASSERT_EQ(2, (int)videos[0]->payload_types_.size());
    EXPECT_STREQ("H264", videos[0]->payload_types_[0].encoding_name_.c_str());
    EXPECT_STREQ("red", videos[0]->payload_types_[1].encoding_name_.c_str());
    EXPECT_EQ(121, videos[0]->payload_types_[1].payload_type_);

    // A Chrome player offers RED as 100 for audio and 120 for video.
    RtcConnUtestClient player(&server);
    std::string audio = rtc_conn_utest_section("audio", "0", "recvonly", "111 100",
                                               "a=rtpmap:111 opus/48000/2\r\n"
                                               "a=rtpmap:100 red/48000/2\r\n");
    std::string video = rtc_conn_utest_section("video", "1", "recvonly", "106 120",
                                               "a=rtpmap:106 H264/90000\r\n"
                                               "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
                                               "a=rtpmap:120 red/90000\r\n");
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", audio + video)));

    std::vector<SrsMediaDesc *> audios = player.answered("audio");
    ASSERT_EQ(1, (int)audios.size());
    ASSERT_EQ(2, (int)audios[0]->payload_types_.size());
    EXPECT_STREQ("red", audios[0]->payload_types_[0].encoding_name_.c_str());
    EXPECT_EQ(100, audios[0]->payload_types_[0].payload_type_);
    EXPECT_STREQ("opus", audios[0]->payload_types_[1].encoding_name_.c_str());
    videos = player.answered("video");
    ASSERT_EQ(1, (int)videos.size());
    ASSERT_EQ(2, (int)videos[0]->payload_types_.size());
    EXPECT_EQ(120, videos[0]->payload_types_[1].payload_type_);

    SrsRtcPlayStream *play = player.player();
    SrsRtcTrackDescription *atrack = play->audio_tracks_.begin()->second->track_desc_;
    SrsRtcTrackDescription *vtrack = play->video_tracks_.begin()->second->track_desc_;
    EXPECT_EQ(63, atrack->red_->pt_of_publisher_);
    EXPECT_EQ(100, atrack->red_->pt_);
    EXPECT_EQ(121, vtrack->red_->pt_of_publisher_);
    EXPECT_EQ(120, vtrack->red_->pt_);

    // The first publisher leaves, and another one publishes with RED 96 and 125, which the source tells the player.
    srs_freep(publisher);
    RtcConnUtestClient second(&server);
    RedPts other = {"96", "125", 3001};
    HELPER_ASSERT_SUCCESS(second.publish(RedOffer::publisher(other)));
    EXPECT_TRUE(play->is_stream_changed(server.source_->stream_desc()));
    play->on_stream_change(server.source_->stream_desc());

    atrack = play->audio_tracks_.begin()->second->track_desc_;
    vtrack = play->video_tracks_.begin()->second->track_desc_;
    EXPECT_EQ(3001u, play->audio_tracks_.begin()->first);
    EXPECT_EQ(96, atrack->red_->pt_of_publisher_);
    EXPECT_EQ(100, atrack->red_->pt_);
    EXPECT_EQ(3002u, play->video_tracks_.begin()->first);
    EXPECT_EQ(125, vtrack->red_->pt_of_publisher_);
    EXPECT_EQ(120, vtrack->red_->pt_);
}

// libdatachannel may offer its tracks with no SSRC lines. A video track with none still gets its section in the
// answer, and a track of the stream named by its mid.
// @see https://github.com/ossrs/srs/issues/4570#issuecomment-3604598513
VOID TEST(AppRtcConnTest, LibdatachannelVideoWithoutSsrcPublished)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    std::string video = rtc_conn_utest_section("video", "video", "sendonly", "96",
                                               "a=rtpmap:96 H264/90000\r\n"
                                               "a=fmtp:96 packetization-mode=1;profile-level-id=42e01f;level-asymmetry-allowed=1\r\n");
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("video", video)));

    std::vector<SrsMediaDesc *> videos = publisher.answered("video");
    ASSERT_EQ(1, (int)videos.size());
    EXPECT_STREQ("video", videos[0]->mid_.c_str());
    EXPECT_EQ(96, videos[0]->payload_types_[0].payload_type_);

    SrsRtcSourceDescription *desc = server.source_->stream_desc();
    ASSERT_EQ(1, (int)desc->video_track_descs_.size());
    EXPECT_STREQ("track-video-video", desc->video_track_descs_[0]->id_.c_str());
    EXPECT_STREQ("live/livestream", desc->video_track_descs_[0]->msid_.c_str());
}

// A publisher that sends ULPFEC on its own SSRC, grouped with the media by `a=ssrc-group:FEC` (RFC 5956): its FEC
// SSRC belongs to the video track and reaches the publisher, and a player's answer groups it with the video too.
VOID TEST(AppRtcConnTest, PublisherWithFecSsrcGroup)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    std::string video = rtc_conn_utest_section("video", "1", "sendonly", "106 122",
                                               "a=rtpmap:106 H264/90000\r\n"
                                               "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
                                               "a=rtpmap:122 ulpfec/90000\r\n"
                                               "a=ssrc-group:FEC 2001 2002\r\n"
                                               "a=ssrc:2001 cname:utest\r\n"
                                               "a=ssrc:2001 msid:stream video\r\n"
                                               "a=ssrc:2002 cname:utest\r\n"
                                               "a=ssrc:2002 msid:stream video\r\n");
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + video)));

    SrsRtcSourceDescription *desc = server.source_->stream_desc();
    ASSERT_EQ(1, (int)desc->video_track_descs_.size());
    EXPECT_EQ(2001u, desc->video_track_descs_[0]->ssrc_);
    EXPECT_EQ(2002u, desc->video_track_descs_[0]->fec_ssrc_);
    EXPECT_TRUE(publisher.conn_->publishers_ssrc_map_[2002] == publisher.publisher());

    RtcConnUtestClient player(&server);
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"))));
    std::vector<SrsMediaDesc *> videos = player.answered("video");
    ASSERT_EQ(1, (int)videos.size());
    ASSERT_EQ(1, (int)videos[0]->ssrc_groups_.size());
    EXPECT_STREQ("FEC", videos[0]->ssrc_groups_[0].semantic_.c_str());
    ASSERT_EQ(2, (int)videos[0]->ssrc_groups_[0].ssrcs_.size());
    EXPECT_EQ(videos[0]->ssrc_infos_[0].ssrc_, videos[0]->ssrc_groups_[0].ssrcs_[0]);
    EXPECT_EQ(2002u, videos[0]->ssrc_groups_[0].ssrcs_[1]);
}

// An offer whose video reuses the audio SSRC, from a broken or hostile client, is refused, as the server could not
// tell the tracks' packets apart. Once that session is gone, the stream can be published by a good offer.
VOID TEST(AppRtcConnTest, OfferWithVideoReusingAudioSsrcRefused)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    if (true) {
        RtcConnUtestClient publisher(&server);
        std::string video = rtc_conn_utest_section("video", "1", "sendonly", "106",
                                                   "a=rtpmap:106 H264/90000\r\n"
                                                   "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
                                                   "a=ssrc:1001 cname:utest\r\n"
                                                   "a=ssrc:1001 msid:stream video\r\n");
        err = publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + video));
        EXPECT_EQ(ERROR_RTC_DUPLICATED_SSRC, srs_error_code(err));
        srs_freep(err);
    }

    RtcConnUtestClient publisher(&server);
    HELPER_EXPECT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
}

// The answer's candidates follow `rtc_server.protocol`, UDP, TCP or both, each on its own port, and its DTLS role is
// the opposite of the client's: passive to an active client, active to a passive one, and the configured role to a
// client that offers both.
VOID TEST(AppRtcConnTest, AnswerCandidatesAndDtlsRole)
{
    srs_error_t err;

    struct {
        const char *protocol_;
        const char *setup_;
        const char *answer_setup_;
    } cases[] = {
        {"udp", "active", "passive"},
        {"tcp", "passive", "active"},
        {"all", "actpass", "passive"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        RtcConnUtestServer server;
        std::string protocol = cases[i].protocol_;
        HELPER_ASSERT_SUCCESS(server.initialize("rtc_server { enabled on; listen 8000; candidate 192.168.1.100; protocol " + protocol +
                                                "; tcp { enabled on; listen 8080; } }"));

        RtcConnUtestClient publisher(&server);
        std::string offer = rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher());
        offer = srs_strings_replace(offer, "a=setup:actpass", std::string("a=setup:") + cases[i].setup_);
        HELPER_ASSERT_SUCCESS(publisher.publish(offer));

        ASSERT_FALSE(publisher.answer_.media_descs_.empty());
        SrsMediaDesc &media = publisher.answer_.media_descs_[0];
        EXPECT_STREQ(cases[i].answer_setup_, media.session_info_.setup_.c_str()) << protocol;

        std::vector<SrsCandidate> &candidates = media.candidates_;
        if (protocol == "udp") {
            ASSERT_EQ(1, (int)candidates.size());
            EXPECT_STREQ("udp", candidates[0].protocol_.c_str());
            EXPECT_EQ(8000, candidates[0].port_);
        } else if (protocol == "tcp") {
            ASSERT_EQ(1, (int)candidates.size());
            EXPECT_STREQ("tcp", candidates[0].protocol_.c_str());
            EXPECT_EQ(8080, candidates[0].port_);
        } else {
            ASSERT_EQ(2, (int)candidates.size());
            EXPECT_STREQ("udp", candidates[0].protocol_.c_str());
            EXPECT_EQ(8000, candidates[0].port_);
            EXPECT_STREQ("tcp", candidates[1].protocol_.c_str());
            EXPECT_EQ(8080, candidates[1].port_);
        }
        EXPECT_STREQ("192.168.1.100", candidates[0].ip_.c_str());
    }
}

// A publisher's packet as the source gives it to each player, by default of the publisher's video SSRC 2001 and payload
// type 106.
static SrsRtpPacket *rtc_conn_utest_source_packet(uint16_t seq, uint32_t ssrc = 2001, uint8_t pt = 106, bool audio = false)
{
    std::string bytes = rtc_conn_utest_rtp(ssrc, pt, seq);
    SrsRtpPacket *pkt = new SrsRtpPacket();
    char *p = pkt->wrap((char *)bytes.data(), (int)bytes.size());
    SrsBuffer buf(p, (int)bytes.size());
    srs_error_t err = pkt->decode(&buf);
    srs_freep(err);
    pkt->frame_type_ = audio ? SrsFrameTypeAudio : SrsFrameTypeVideo;
    return pkt;
}

// A WHEP player whose ICE and DTLS are done gets each video packet of the publisher at its UDP address, with its own
// SSRC and payload type. It asks for a lost one by NACK, which is sent again, and for a keyframe by PLI, which goes
// to the publisher's SSRC. When its address stops taking packets, such as a client gone without a goodbye, the
// session still takes the next packet.
VOID TEST(AppRtcConnTest, WhepPlayerGetsPacketsAtItsAddress)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));

    // The player offers H.264 as 102.
    RtcConnUtestClient player(&server);
    std::string video = rtc_conn_utest_section("video", "1", "recvonly", "102",
                                               "a=rtpmap:102 H264/90000\r\n"
                                               "a=rtcp-fb:102 nack\r\n"
                                               "a=rtcp-fb:102 nack pli\r\n"
                                               "a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n");
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + video)));
    HELPER_ASSERT_SUCCESS(player.connect());
    uint32_t player_ssrc = player.answered("video")[0]->ssrc_infos_[0].ssrc_;

    SrsRtcPlayStream *play = player.player();
    for (uint16_t seq = 100; seq < 102; seq++) {
        SrsRtpPacket *pkt = rtc_conn_utest_source_packet(seq);
        HELPER_EXPECT_SUCCESS(play->send_packet(pkt));
        srs_freep(pkt);
    }

    ASSERT_EQ(2, (int)player.skt_.sent_.size());
    SrsRtpHeader h;
    SrsBuffer b0((char *)player.skt_.sent_[0].data(), (int)player.skt_.sent_[0].size());
    HELPER_ASSERT_SUCCESS(h.decode(&b0));
    EXPECT_EQ(player_ssrc, h.get_ssrc());
    EXPECT_EQ(102, h.get_payload_type());
    uint16_t first_seq = h.get_sequence();

    // The player lost the first packet.
    SrsRtcpNack nack(player_ssrc);
    nack.set_media_ssrc(player_ssrc);
    nack.add_lost_sn(first_seq);
    char buf[kRtcpPacketSize];
    SrsBuffer nb(buf, sizeof(buf));
    HELPER_ASSERT_SUCCESS(nack.encode(&nb));
    HELPER_EXPECT_SUCCESS(player.on_rtcp(std::string(buf, nb.pos())));
    ASSERT_EQ(3, (int)player.skt_.sent_.size());
    EXPECT_EQ(player.skt_.sent_[0], player.skt_.sent_[2]);

    // The player asks for a keyframe.
    SrsRtcpPli pli(player_ssrc);
    pli.set_media_ssrc(player_ssrc);
    SrsBuffer pb(buf, sizeof(buf));
    HELPER_ASSERT_SUCCESS(pli.encode(&pb));
    HELPER_EXPECT_SUCCESS(player.on_rtcp(std::string(buf, pb.pos())));
    SrsRtcPliWorker *worker = dynamic_cast<SrsRtcPliWorker *>(play->pli_worker_);
    EXPECT_TRUE(worker->plis_.find(2001) != worker->plis_.end());

    // The client is gone.
    srs_error_t gone = srs_error_new(ERROR_SOCKET_WRITE, "sendto ECONNREFUSED");
    player.skt_.set_sendto_error(gone);
    srs_freep(gone);
    for (uint16_t seq = 102; seq < 104; seq++) {
        SrsRtpPacket *pkt = rtc_conn_utest_source_packet(seq);
        HELPER_EXPECT_SUCCESS(play->send_packet(pkt));
        srs_freep(pkt);
    }
    EXPECT_EQ(5, (int)player.skt_.sent_.size());
}

// Chrome sends a sender report for each SSRC every second, which the publisher keeps for A/V sync and for the
// receiver reports it sends back.
VOID TEST(AppRtcConnTest, PublisherSenderReportKeptByItsTrack)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));

    SrsRtcpSR sr;
    sr.set_ssrc(2001);
    sr.set_ntp(((uint64_t)3900000000u << 32) | 0x80000000u);
    sr.set_rtp_ts(123456);
    char buf[kRtcpPacketSize];
    SrsBuffer b(buf, sizeof(buf));
    HELPER_ASSERT_SUCCESS(sr.encode(&b));
    HELPER_EXPECT_SUCCESS(publisher.on_rtcp(std::string(buf, b.pos())));

    ASSERT_EQ(1, (int)publisher.publisher()->video_tracks_.size());
    EXPECT_EQ(123456, publisher.publisher()->video_tracks_[0]->last_sender_report_rtp_time_);
}

// A publisher's RTCP XR whose report block says it is longer than the packet, as from a broken client or a forged
// packet, is refused, never read past its end.
VOID TEST(AppRtcConnTest, PublisherXrBlockLongerThanPacketRefused)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));

    // XR of 12 bytes from SSRC 2001, with an RRTR block that says it has 44 bytes.
    uint8_t xr[] = {0x80, 207, 0x00, 0x03, 0x00, 0x00, 0x07, 0xD1, 0x04, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00};
    err = publisher.on_rtcp(std::string((char *)xr, sizeof(xr)));
    EXPECT_EQ(ERROR_RTC_RTCP_CHECK, srs_error_code(err));
    srs_freep(err);
}

// A publisher on an overloaded server: at the circuit breaker's high level, or its critical level too, the packets 101
// to 104 are lost and 105 comes, then the load is back to normal when 106 comes, and 107 to 109 are lost. Every part of the session
// shares the server's circuit breaker, as they share the global one in the server. Gets what the session sent during
// the overload, and the sequence numbers of its NACK after it.
static srs_error_t rtc_conn_utest_nack_through_overload(bool critical, std::vector<std::string> &during, std::vector<uint16_t> &after)
{
    srs_error_t err = srs_success;

    ListenerUtestClock clock;

    RtcConnUtestServer server;
    if ((err = server.initialize()) != srs_success) {
        return srs_error_wrap(err, "server");
    }

    RtcConnUtestClient publisher(&server);
    if ((err = publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher()))) != srs_success) {
        return srs_error_wrap(err, "publish");
    }
    if ((err = publisher.connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    SrsRtcPublishStream *pub = publisher.publisher();
    pub->circuit_breaker_ = &server.circuit_breaker_;
    pub->video_tracks_[0]->circuit_breaker_ = &server.circuit_breaker_;
    pub->audio_tracks_[0]->circuit_breaker_ = &server.circuit_breaker_;

    if ((err = publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 100))) != srs_success) {
        return srs_error_wrap(err, "rtp");
    }

    server.circuit_breaker_.hybrid_high_water_level_ = true;
    server.circuit_breaker_.hybrid_critical_water_level_ = critical;
    if ((err = publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 105))) != srs_success) {
        return srs_error_wrap(err, "rtp");
    }
    _srs_system_time_us_cache = clock.base_ + 100 * SRS_UTIME_MILLISECONDS;
    if ((err = publisher.conn_->do_check_send_nacks()) != srs_success) {
        return srs_error_wrap(err, "nack");
    }
    during = publisher.skt_.sent_;
    publisher.skt_.sent_.clear();

    server.circuit_breaker_.hybrid_high_water_level_ = false;
    server.circuit_breaker_.hybrid_critical_water_level_ = false;
    if ((err = publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 106))) != srs_success) {
        return srs_error_wrap(err, "rtp");
    }
    if ((err = publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 110))) != srs_success) {
        return srs_error_wrap(err, "rtp");
    }
    _srs_system_time_us_cache = clock.base_ + 200 * SRS_UTIME_MILLISECONDS;
    if ((err = publisher.conn_->do_check_send_nacks()) != srs_success) {
        return srs_error_wrap(err, "nack");
    }

    pub->circuit_breaker_ = NULL;
    pub->video_tracks_[0]->circuit_breaker_ = NULL;
    pub->audio_tracks_[0]->circuit_breaker_ = NULL;

    if (publisher.skt_.sent_.size() != 1) {
        return srs_error_new(ERROR_RTC_RTCP, "sent %d", (int)publisher.skt_.sent_.size());
    }

    SrsRtcpCompound compound;
    SrsBuffer b((char *)publisher.skt_.sent_[0].data(), (int)publisher.skt_.sent_[0].size());
    if ((err = compound.decode(&b)) != srs_success) {
        return srs_error_wrap(err, "decode");
    }
    SrsUniquePtr<SrsRtcpCommon> rtcp(compound.get_next_rtcp());
    SrsRtcpNack *nack = dynamic_cast<SrsRtcpNack *>(rtcp.get());
    if (!nack || nack->get_media_ssrc() != 2001) {
        return srs_error_new(ERROR_RTC_RTCP, "not a NACK of 2001");
    }
    after = nack->get_lost_sns();

    return err;
}

static bool rtc_conn_utest_has_sn(const std::vector<uint16_t> &sns, uint16_t sn)
{
    return std::find(sns.begin(), sns.end(), sn) != sns.end();
}

// A publisher on a lossy network: the packets 101 to 104 are lost and 105 comes. The server sends one NACK for the
// four packets, and counts the request and the four sequence numbers it asks for, which the circuit breaker's line
// shows per second.
VOID TEST(AppRtcConnTest, PublisherLosesPackets_NackRequestAndSeqsCounted)
{
    srs_error_t err;

    ListenerUtestClock clock;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
    HELPER_ASSERT_SUCCESS(publisher.connect());

    HELPER_ASSERT_SUCCESS(publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 100)));
    HELPER_ASSERT_SUCCESS(publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 105)));
    publisher.skt_.sent_.clear();

    int64_t requests = _srs_pps_snack2->sugar_;
    int64_t seqs = _srs_pps_snack3->sugar_;
    _srs_system_time_us_cache = clock.base_ + 100 * SRS_UTIME_MILLISECONDS;
    HELPER_ASSERT_SUCCESS(publisher.conn_->do_check_send_nacks());

    ASSERT_EQ(1, (int)publisher.skt_.sent_.size());
    EXPECT_EQ(1, _srs_pps_snack2->sugar_ - requests);
    EXPECT_EQ(4, _srs_pps_snack3->sugar_ - seqs);
}

// The server overloaded: at the circuit breaker's critical level, as at its high level, the publisher's packets still
// go through the receive queue, but no loss is asked for by NACK, to spare the CPU, and the packets lost then are
// never asked for, as only the losses after the load is back to normal are.
VOID TEST(AppRtcConnTest, OverloadedServerSendsNoNack)
{
    srs_error_t err;

    std::vector<std::string> during;
    std::vector<uint16_t> after;
    HELPER_ASSERT_SUCCESS(rtc_conn_utest_nack_through_overload(true, during, after));

    EXPECT_TRUE(during.empty());
    ASSERT_EQ(3, (int)after.size());
    EXPECT_EQ(107, after[0]);
    EXPECT_EQ(108, after[1]);
    EXPECT_EQ(109, after[2]);
}

// After the overload, the server does not ask for a packet it got during it: at the critical level the packet is
// still recorded in the receive queue, so the first packet after it finds no gap before it. Otherwise the publisher
// would resend every such packet, a burst as large as the receive queue after a long overload, just as the server
// recovers.
VOID TEST(AppRtcConnTest, OverloadEnds_PacketReceivedDuringItNotAskedByNack)
{
    srs_error_t err;

    std::vector<std::string> during;
    std::vector<uint16_t> after;
    HELPER_ASSERT_SUCCESS(rtc_conn_utest_nack_through_overload(true, during, after));

    EXPECT_FALSE(rtc_conn_utest_has_sn(after, 105));
}

// The server busy: at the circuit breaker's high level, below critical, the publisher's packets still go through the
// receive queue, but no loss is asked for by NACK, and the packets lost then are never asked for, as only the
// losses after the load is back to normal are.
VOID TEST(AppRtcConnTest, BusyServerSendsNoNack)
{
    srs_error_t err;

    std::vector<std::string> during;
    std::vector<uint16_t> after;
    HELPER_ASSERT_SUCCESS(rtc_conn_utest_nack_through_overload(false, during, after));

    EXPECT_TRUE(during.empty());
    ASSERT_EQ(3, (int)after.size());
    EXPECT_EQ(107, after[0]);
    EXPECT_EQ(108, after[1]);
    EXPECT_EQ(109, after[2]);
}

// Chrome numbers each packet for TWCC. A packet the network duplicates comes with a number already seen, and is
// dropped before SRTP; the feedback of the numbers is sent to the publisher, and a timer that fires late, as on a
// busy server, is logged.
VOID TEST(AppRtcConnTest, PublisherTwccNumbersFedBack)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_video_publisher("106", "H264"))));
    HELPER_ASSERT_SUCCESS(publisher.connect());

    std::string pkt = rtc_conn_utest_rtp(2001, 106, 100, 7);
    HELPER_EXPECT_SUCCESS(publisher.on_rtp(pkt));
    EXPECT_EQ(1, server.source_->rtp_video_count_);

    err = publisher.on_rtp(pkt);
    EXPECT_EQ(ERROR_RTC_RTCP, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, server.source_->rtp_video_count_);

    SrsRtcPublishStream *pub = publisher.publisher();
    pub->last_time_send_twcc_ = srs_time_now_cached() - 200 * SRS_UTIME_MILLISECONDS;
    uint32_t late_before = pub->twcc_epp_->nn_count_;
    HELPER_EXPECT_SUCCESS(pub->send_periodic_twcc());
    EXPECT_EQ(late_before + 1, pub->twcc_epp_->nn_count_);

    ASSERT_EQ(1, (int)publisher.skt_.sent_.size());
    const std::string &fb = publisher.skt_.sent_[0];
    ASSERT_GE((int)fb.size(), 12);
    EXPECT_EQ(205, (uint8_t)fb[1]);
    EXPECT_EQ(15, (uint8_t)fb[0] & 0x1F);
}

// A publisher's RTP packet whose padding count is more than the packet, as from a broken client with encryption off,
// is refused before it reaches the stream.
VOID TEST(AppRtcConnTest, PublisherRtpWithPaddingPastItsEndRefused)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));

    uint8_t rtp[] = {0xA0, 106, 0x00, 0x64, 0x00, 0x01, 0x5F, 0x90, 0x00, 0x00, 0x07, 0xD1, 0x65, 0x88, 0x09};
    err = publisher.on_rtp(std::string((char *)rtp, sizeof(rtp)));
    EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, server.source_->rtp_video_count_);
}

// A Plan-B publisher sends its camera and its screen as two SSRCs of one video section, and a Plan-B player gets both
// in its one video section, each with an SSRC of its own. The player asks for a lost packet of the screen by NACK,
// and it is sent again from the screen's track.
VOID TEST(AppRtcConnTest, PlanBCameraAndScreenPlayed)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    std::string video = rtc_conn_utest_section("video", "1", "sendonly", "106",
                                               "a=rtpmap:106 H264/90000\r\n"
                                               "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
                                               "a=ssrc:2001 cname:utest\r\n"
                                               "a=ssrc:2001 msid:stream camera\r\n"
                                               "a=ssrc:2002 cname:utest\r\n"
                                               "a=ssrc:2002 msid:stream screen\r\n");
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + video)));
    ASSERT_EQ(2, (int)server.source_->stream_desc()->video_track_descs_.size());

    RtcConnUtestClient player(&server);
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"))));
    std::vector<SrsMediaDesc *> videos = player.answered("video");
    ASSERT_EQ(1, (int)videos.size());
    ASSERT_EQ(2, (int)videos[0]->ssrc_infos_.size());
    uint32_t screen_ssrc = videos[0]->ssrc_infos_[1].ssrc_;
    EXPECT_NE(videos[0]->ssrc_infos_[0].ssrc_, screen_ssrc);
    HELPER_ASSERT_SUCCESS(player.connect());

    SrsRtcPlayStream *play = player.player();
    SrsRtpPacket *pkt = rtc_conn_utest_source_packet(500, 2002);
    HELPER_EXPECT_SUCCESS(play->send_packet(pkt));
    srs_freep(pkt);
    ASSERT_EQ(1, (int)player.skt_.sent_.size());

    SrsRtpHeader h;
    SrsBuffer b((char *)player.skt_.sent_[0].data(), (int)player.skt_.sent_[0].size());
    HELPER_ASSERT_SUCCESS(h.decode(&b));
    EXPECT_EQ(screen_ssrc, h.get_ssrc());

    SrsRtcpNack nack(screen_ssrc);
    nack.set_media_ssrc(screen_ssrc);
    nack.add_lost_sn(h.get_sequence());
    char buf[kRtcpPacketSize];
    SrsBuffer nb(buf, sizeof(buf));
    HELPER_ASSERT_SUCCESS(nack.encode(&nb));
    HELPER_EXPECT_SUCCESS(player.on_rtcp(std::string(buf, nb.pos())));
    ASSERT_EQ(2, (int)player.skt_.sent_.size());
    EXPECT_EQ(player.skt_.sent_[0], player.skt_.sent_[1]);
}

// On a server with `protocol all`, a WHEP client may finish DTLS on both UDP and TCP, so its session is told twice
// that it is connected: the player starts once. Each second the player sends a sender report for each track that
// sent media; when the client is gone, each report fails, and the next track's report is still tried.
VOID TEST(AppRtcConnTest, WhepPlayerConnectedTwiceStartsOnce)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));

    RtcConnUtestClient player(&server);
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"))));
    HELPER_ASSERT_SUCCESS(player.connect());

    SrsRtcPlayStream *play = player.player();
    HELPER_EXPECT_SUCCESS(player.conn_->on_dtls_handshake_done());
    ASSERT_TRUE(play->is_started_);
    ISrsCoroutine *sender = play->trd_;
    HELPER_EXPECT_SUCCESS(player.conn_->on_dtls_handshake_done());
    EXPECT_TRUE(sender == play->trd_);

    // The packets have their time on the stream's clock, which the publisher's sender reports gave.
    SrsRtpPacket *pkt = rtc_conn_utest_source_packet(100, 1001, 111, true);
    pkt->set_avsync_time(1000);
    HELPER_EXPECT_SUCCESS(play->send_packet(pkt));
    srs_freep(pkt);
    pkt = rtc_conn_utest_source_packet(100);
    pkt->set_avsync_time(1000);
    HELPER_EXPECT_SUCCESS(play->send_packet(pkt));
    srs_freep(pkt);
    ASSERT_EQ(2, (int)player.skt_.sent_.size());

    srs_error_t gone = srs_error_new(ERROR_SOCKET_WRITE, "sendto ECONNREFUSED");
    player.skt_.set_sendto_error(gone);
    srs_freep(gone);
    HELPER_EXPECT_SUCCESS(play->on_timer(1 * SRS_UTIME_SECONDS));
    ASSERT_EQ(4, (int)player.skt_.sent_.size());
    EXPECT_EQ(200, (uint8_t)player.skt_.sent_[2][1]);
    EXPECT_EQ(200, (uint8_t)player.skt_.sent_[3][1]);
}

// Each second the publisher's session sends a receiver report and an XR RRTR for each track. When the publisher is
// gone without a goodbye, sending fails, and the error names the track, for the timer's warning.
VOID TEST(AppRtcConnTest, PublisherGoneReceiverReportsFail)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
    HELPER_ASSERT_SUCCESS(publisher.connect());
    HELPER_EXPECT_SUCCESS(publisher.on_rtp(rtc_conn_utest_rtp(2001, 106, 100)));

    SrsRtcPublishStream *pub = publisher.publisher();
    HELPER_EXPECT_SUCCESS(pub->send_rtcp_rr());
    HELPER_EXPECT_SUCCESS(pub->send_rtcp_xr_rrtr());
    EXPECT_EQ(4, (int)publisher.skt_.sent_.size());

    srs_error_t gone = srs_error_new(ERROR_SOCKET_WRITE, "sendto ECONNREFUSED");
    publisher.skt_.set_sendto_error(gone);
    srs_freep(gone);

    err = pub->send_rtcp_rr();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("ssrc=2001") != std::string::npos);
    srs_freep(err);

    err = pub->send_rtcp_xr_rrtr();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("ssrc=2001") != std::string::npos);
    srs_freep(err);
}

// A video-only stream, such as a screen share, republished by another publisher with another SSRC: its player sees
// the stream changed by the video alone, and maps the new SSRC to its video track.
VOID TEST(AppRtcConnTest, VideoOnlyStreamRepublishedToItsPlayer)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());

    RtcConnUtestClient *first = new RtcConnUtestClient(&server);
    HELPER_ASSERT_SUCCESS(first->publish(rtc_conn_utest_offer("1", rtc_conn_utest_h264_publisher())));

    RtcConnUtestClient player(&server);
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("1", rtc_conn_utest_video_player("106", "H264"))));
    SrsRtcPlayStream *play = player.player();
    EXPECT_FALSE(play->is_stream_changed(server.source_->stream_desc()));

    srs_freep(first);
    RtcConnUtestClient second(&server);
    std::string video = srs_strings_replace(rtc_conn_utest_h264_publisher(), "a=ssrc:2001", "a=ssrc:4001");
    HELPER_ASSERT_SUCCESS(second.publish(rtc_conn_utest_offer("1", video)));

    EXPECT_TRUE(play->is_stream_changed(server.source_->stream_desc()));
    play->on_stream_change(server.source_->stream_desc());
    EXPECT_EQ(4001u, play->video_tracks_.begin()->first);
    EXPECT_FALSE(play->is_stream_changed(server.source_->stream_desc()));
}

// The DTLS version the API puts in a session's config for the vhost, from a config text.
static string rtc_dtls_utest_vhost_version(MockSrsConfig *conf, string version)
{
    string text = "vhost __defaultVhost__ { rtc { enabled on; dtls_version " + version + "; } }";
    srs_error_t err = conf->mock_parse(string(_MIN_OK_CONF) + text);
    srs_freep(err);
    return conf->get_rtc_dtls_version("__defaultVhost__");
}

// A vhost with dtls_version dtls1.0 answers a client that speaks only DTLS 1.0, such as an old device: SRS, passive,
// agrees on DTLS 1.0, and both sides derive the same SRTP keys, so media flows both ways.
VOID TEST(AppRtcDtlsTest, Dtls10VhostAgreesWithDtls10Client)
{
    srs_error_t err;

    MockSrsConfig conf;
    string version = rtc_dtls_utest_vhost_version(&conf, "dtls1.0");
    ASSERT_STREQ("dtls1.0", version.c_str());

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.0"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", version));
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));

    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_EQ(DTLS1_VERSION, SSL_version(server.impl_->dtls_));
    EXPECT_EQ(DTLS1_VERSION, SSL_version(client.impl_->dtls_));

    string client_recv, client_send, server_recv, server_send;
    HELPER_ASSERT_SUCCESS(client.get_srtp_key(client_recv, client_send));
    HELPER_ASSERT_SUCCESS(server.get_srtp_key(server_recv, server_send));
    EXPECT_TRUE(client_send == server_recv);
    EXPECT_TRUE(client_recv == server_send);

    SrsSRTP client_srtp, server_srtp;
    HELPER_ASSERT_SUCCESS(client_srtp.initialize(client_recv, client_send));
    HELPER_ASSERT_SUCCESS(server_srtp.initialize(server_recv, server_send));
    expect_srtp_round_trip(&client_srtp, &server_srtp, 1);
    expect_srtp_round_trip(&server_srtp, &client_srtp, 1);
}

// A vhost with dtls_version dtls1.0 cannot serve a browser that no longer speaks DTLS 1.0: SRS answers its ClientHello
// by a DTLS 1.0 ServerHello, the browser refuses it by a fatal protocol_version alert, and SRS hands the alert to the
// session, which closes on a fatal alert (SrsRtcConnection::on_dtls_alert), see RFC 6347 section 4.2.1.
VOID TEST(AppRtcDtlsTest, Dtls10VhostRefusedByDtls12OnlyBrowser)
{
    srs_error_t err;

    MockSrsConfig conf;
    string version = rtc_dtls_utest_vhost_version(&conf, "dtls1.0");

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "auto"));
    ASSERT_EQ(1, SSL_set_min_proto_version(client.impl_->dtls_, DTLS1_2_VERSION));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", version));
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    // The browser fails at the ServerHello and sends its alert record(21).
    HELPER_EXPECT_FAILED(dtls_exchange(&client, &ccb, &server, &scb));
    ASSERT_EQ(1, (int)ccb.alerts_.size());
    EXPECT_STREQ("fatal", ccb.alerts_[0].first.c_str());
    EXPECT_STREQ("PV", ccb.alerts_[0].second.c_str());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    EXPECT_EQ(21, (uint8_t)ccb.packets_[0][0]);

    // SRS hands the alert to the session, and its handshake never completes.
    err = server.on_dtls(&ccb.packets_[0][0], (int)ccb.packets_[0].size());
    srs_freep(err);
    ASSERT_EQ(1, (int)scb.alerts_.size());
    EXPECT_STREQ("fatal", scb.alerts_[0].first.c_str());
    EXPECT_STREQ("PV", scb.alerts_[0].second.c_str());
    EXPECT_EQ(0, scb.handshake_done_count_);
    EXPECT_EQ(0, ccb.handshake_done_count_);
    EXPECT_FALSE(server.impl_->handshake_done_for_us_);
}

// A vhost with dtls_version dtls1.2 allows only DTLS 1.2, as dtls1.0 allows only DTLS 1.0, so it refuses a client
// that offers only DTLS 1.0 by a fatal protocol_version alert, see RFC 6347 section 4.2.1.
VOID TEST(AppRtcDtlsTest, Dtls12VhostRefusesDtls10Client)
{
    srs_error_t err;

    MockSrsConfig conf;
    string version = rtc_dtls_utest_vhost_version(&conf, "dtls1.2");
    ASSERT_STREQ("dtls1.2", version.c_str());

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.0"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", version));
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    err = dtls_exchange(&client, &ccb, &server, &scb);
    srs_freep(err);
    EXPECT_EQ(0, scb.handshake_done_count_) << "agreed on version 0x" << std::hex << SSL_version(server.impl_->dtls_);
    ASSERT_FALSE(scb.alerts_.empty());
    EXPECT_STREQ("PV", scb.alerts_[0].second.c_str());
}

// A browser offers DTLS 1.0 and 1.2 in its ClientHello, and a vhost with dtls_version dtls1.2 agrees with it on
// DTLS 1.2, then both derive the same SRTP keys.
VOID TEST(AppRtcDtlsTest, Dtls12VhostAgreesWithBrowserOnDtls12)
{
    srs_error_t err;

    MockSrsConfig conf;
    string version = rtc_dtls_utest_vhost_version(&conf, "dtls1.2");

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "auto"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", version));
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));

    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_EQ(DTLS1_2_VERSION, SSL_version(server.impl_->dtls_));
    EXPECT_EQ(DTLS1_2_VERSION, SSL_version(client.impl_->dtls_));

    string client_recv, client_send, server_recv, server_send;
    HELPER_ASSERT_SUCCESS(client.get_srtp_key(client_recv, client_send));
    HELPER_ASSERT_SUCCESS(server.get_srtp_key(server_recv, server_send));
    EXPECT_TRUE(client_send == server_recv);
    EXPECT_TRUE(client_recv == server_send);
}

// The server's sessions by the username of their ICE checks, and the client addresses each session took, as the RTC
// server's resource manager keeps them to route a datagram to its session.
class RtcNetworkUtestConns : public MockConnectionManager
{
public:
    std::map<std::string, ISrsResource *> names_;
    std::map<std::string, ISrsResource *> ids_;
    std::map<uint64_t, ISrsResource *> fast_ids_;
    int nn_ids_;

public:
    RtcNetworkUtestConns()
    {
        nn_ids_ = 0;
    }

public:
    virtual void add_with_id(const std::string &id, ISrsResource *conn)
    {
        ids_[id] = conn;
        nn_ids_++;
    }
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn)
    {
        fast_ids_[id] = conn;
    }
    virtual ISrsResource *find_by_name(std::string name)
    {
        std::map<std::string, ISrsResource *>::iterator it = names_.find(name);
        return (it == names_.end()) ? NULL : it->second;
    }
};

// Starts the session of a client whose offer was answered, as SrsRtcSessionManager does: the session keeps the offer
// and the answer, waits for the client's STUN on both networks, and is found by the username of the client's checks.
// With dtls and srtp off, the client sends and gets plain RTP, as it asks with `?encrypt=false&dtls=false`.
static srs_error_t rtc_network_utest_start(RtcConnUtestClient &client, RtcNetworkUtestConns &conns, bool dtls, bool srtp)
{
    srs_error_t err = srs_success;

    SrsRtcConnection *conn = client.conn_;
    conn->set_remote_sdp(client.ruc_.remote_sdp_);
    conn->set_local_sdp(client.answer_);
    conn->set_state_as_waiting_stun();

    std::string username = client.answer_.get_ice_ufrag() + ":" + client.ruc_.remote_sdp_.get_ice_ufrag();
    if ((err = conn->initialize(client.ruc_.req_, dtls, srtp, username)) != srs_success) {
        return srs_error_wrap(err, "initialize");
    }
    conns.names_[username] = conn;

    SrsRtcUdpNetwork *udp = dynamic_cast<SrsRtcUdpNetwork *>(conn->networks_->udp());
    udp->conn_manager_ = &conns;

    return err;
}

// The ICE check of the client, signed with the ICE password of the server's answer.
static std::string rtc_network_utest_check(RtcConnUtestClient &client)
{
    return stun_controlling_binding_request(client.conn_->username(), client.answer_.get_ice_pwd(), true);
}

// Whether the datagram is the server's binding response to the client's check: for the check's transaction, and
// signed with the ICE password of the answer, as the browser verifies it.
static srs_error_t rtc_network_utest_response(const std::string &datagram, RtcConnUtestClient &client)
{
    srs_error_t err = srs_success;

    SrsStunPacket response;
    if ((err = response.decode(datagram.data(), (int)datagram.size())) != srs_success) {
        return srs_error_wrap(err, "decode %d bytes", (int)datagram.size());
    }
    if (!response.is_binding_response()) {
        return srs_error_new(ERROR_RTC_STUN, "type=%d", response.get_message_type());
    }
    if (response.get_transcation_id() != "chrome-tid-1") {
        return srs_error_new(ERROR_RTC_STUN, "transaction %s", response.get_transcation_id().c_str());
    }
    if ((err = response.check_message_integrity(client.answer_.get_ice_pwd())) != srs_success) {
        return srs_error_wrap(err, "integrity");
    }

    return err;
}

// The RTC server's UDP port on loopback, and the client sockets that send to it, each from its own address: a
// browser's host candidate, or the client again after its network changed.
class RtcNetworkUtestUdp
{
public:
    srs_netfd_t server_fd_;
    int server_port_;
    SrsUdpMuxSocket *skt_;
    std::vector<srs_netfd_t> clients_;

public:
    RtcNetworkUtestUdp()
    {
        server_fd_ = NULL;
        server_port_ = 0;
        skt_ = NULL;
    }
    ~RtcNetworkUtestUdp()
    {
        srs_freep(skt_);
        for (size_t i = 0; i < clients_.size(); i++) {
            srs_close_stfd(clients_[i]);
        }
        srs_close_stfd(server_fd_);
    }

public:
    srs_error_t listen()
    {
        srs_error_t err = srs_success;

        if ((err = srs_utest_udp_listen("127.0.0.1", &server_port_, &server_fd_)) != srs_success) {
            return srs_error_wrap(err, "listen");
        }
        skt_ = new SrsUdpMuxSocket(server_fd_);

        return err;
    }

    // A new client address, on a port the system assigns.
    srs_error_t client(int *pindex)
    {
        srs_error_t err = srs_success;

        srs_netfd_t fd = NULL;
        if ((err = srs_udp_listen("127.0.0.1", 0, &fd)) != srs_success) {
            return srs_error_wrap(err, "client");
        }
        clients_.push_back(fd);
        *pindex = (int)clients_.size() - 1;

        return err;
    }

    std::string peer_id(int index)
    {
        int port = srs_get_local_port(srs_netfd_fileno(clients_[index]));
        return "127.0.0.1:" + srs_strconv_format_int(port);
    }

    // Sends the datagram from the client, and receives it on the server's port as the UDP listener does.
    srs_error_t deliver(int index, const std::string &datagram)
    {
        sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(server_port_);
        to.sin_addr.s_addr = inet_addr("127.0.0.1");

        int n = srs_sendto(clients_[index], (void *)datagram.data(), (int)datagram.size(), (sockaddr *)&to, sizeof(to), SRS_UTIME_NO_TIMEOUT);
        if (n != (int)datagram.size()) {
            return srs_error_new(ERROR_SOCKET_WRITE, "sendto %d", n);
        }

        n = skt_->recvfrom(SRS_UTIME_SECONDS);
        if (n != (int)datagram.size()) {
            return srs_error_new(ERROR_SOCKET_READ, "recvfrom %d", n);
        }
        return srs_success;
    }

    // The next datagram the client got, or empty if none came in time.
    std::string recv(int index, srs_utime_t timeout = 100 * SRS_UTIME_MILLISECONDS)
    {
        char buf[1500];
        sockaddr_in from;
        int fromlen = sizeof(from);
        int n = srs_recvfrom(clients_[index], buf, sizeof(buf), (sockaddr *)&from, &fromlen, timeout);
        return (n > 0) ? std::string(buf, n) : std::string();
    }

    // The client's ICE check, received on the server's port, handed to the session's UDP network as the session
    // manager does with a binding request.
    srs_error_t check(int index, RtcConnUtestClient &client)
    {
        srs_error_t err = srs_success;

        if ((err = deliver(index, rtc_network_utest_check(client))) != srs_success) {
            return srs_error_wrap(err, "deliver");
        }

        SrsStunPacket ping;
        if ((err = ping.decode(skt_->data(), skt_->size())) != srs_success) {
            return srs_error_wrap(err, "decode");
        }

        SrsRtcUdpNetwork *network = dynamic_cast<SrsRtcUdpNetwork *>(client.conn_->udp());
        return network->on_stun(skt_, &ping, skt_->data(), skt_->size());
    }
};

// A browser's first ICE check reaches the session of its WHIP offer: the server answers it at the browser's address,
// signed with the answer's ICE password, routes the browser's address to the session from now on, and waits for the
// browser's DTLS ClientHello, as the passive DTLS side by default.
VOID TEST(AppRtcNetworkTest, BrowserFirstCheck_AnsweredAtItsAddressThenWaitsForDtls)
{
    srs_error_t err;

    RtcNetworkUtestUdp udp;
    HELPER_ASSERT_SUCCESS(udp.listen());
    int browser = 0;
    HELPER_ASSERT_SUCCESS(udp.client(&browser));

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());
    RtcNetworkUtestConns conns;
    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
    HELPER_ASSERT_SUCCESS(rtc_network_utest_start(publisher, conns, true, true));

    HELPER_ASSERT_SUCCESS(udp.check(browser, publisher));
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(udp.recv(browser), publisher));

    EXPECT_TRUE(conns.ids_[udp.peer_id(browser)] == publisher.conn_);
    EXPECT_EQ(1, (int)conns.fast_ids_.size());

    SrsRtcUdpNetwork *network = dynamic_cast<SrsRtcUdpNetwork *>(publisher.conn_->udp());
    EXPECT_EQ(SrsRtcNetworkStateDtls, network->state_);
    EXPECT_FALSE(network->is_establelished());
    EXPECT_TRUE(udp.recv(browser, 50 * SRS_UTIME_MILLISECONDS).empty());
}

// With `dtls_role active`, the server is the DTLS client of a browser that offers `a=setup:actpass`: right after the
// response to the browser's first check, it sends its ClientHello to the browser's address.
VOID TEST(AppRtcNetworkTest, ActiveDtlsRole_ClientHelloSentAfterFirstCheck)
{
    srs_error_t err;

    RtcNetworkUtestUdp udp;
    HELPER_ASSERT_SUCCESS(udp.listen());
    int browser = 0;
    HELPER_ASSERT_SUCCESS(udp.client(&browser));

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize("vhost __defaultVhost__ { rtc { enabled on; dtls_role active; } }"));
    RtcNetworkUtestConns conns;
    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
    EXPECT_STREQ("active", publisher.answer_.get_dtls_role().c_str());
    HELPER_ASSERT_SUCCESS(rtc_network_utest_start(publisher, conns, true, true));

    HELPER_ASSERT_SUCCESS(udp.check(browser, publisher));
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(udp.recv(browser), publisher));

    // A DTLS handshake record (22) holding a ClientHello (1).
    std::string hello = udp.recv(browser);
    ASSERT_GE((int)hello.size(), 14);
    EXPECT_EQ(22, (uint8_t)hello[0]);
    EXPECT_EQ(1, (uint8_t)hello[13]);

    SrsRtcUdpNetwork *network = dynamic_cast<SrsRtcUdpNetwork *>(publisher.conn_->udp());
    EXPECT_EQ(SrsRtcNetworkStateDtls, network->state_);
}

// A client's network changes during the session, from Wi-Fi to cellular and back, so its ICE checks come from a new
// address, then from the first one again. Each check is answered at the address it came from, and the session sends to
// that address from then on. The first address is taken again from the session's cache, not routed again, and every
// address the session took is released with it.
VOID TEST(AppRtcNetworkTest, ClientAddressChangesAndBack_SessionFollowsIt)
{
    srs_error_t err;

    RtcNetworkUtestUdp udp;
    HELPER_ASSERT_SUCCESS(udp.listen());
    int wifi = 0, cellular = 0;
    HELPER_ASSERT_SUCCESS(udp.client(&wifi));
    HELPER_ASSERT_SUCCESS(udp.client(&cellular));

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());
    RtcNetworkUtestConns conns;
    RtcConnUtestClient player(&server);
    HELPER_ASSERT_SUCCESS(player.play(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_player() + rtc_conn_utest_video_player("106", "H264"))));
    HELPER_ASSERT_SUCCESS(rtc_network_utest_start(player, conns, true, true));
    SrsRtcUdpNetwork *network = dynamic_cast<SrsRtcUdpNetwork *>(player.conn_->udp());

    HELPER_ASSERT_SUCCESS(udp.check(wifi, player));
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(udp.recv(wifi), player));

    HELPER_ASSERT_SUCCESS(udp.check(cellular, player));
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(udp.recv(cellular), player));
    EXPECT_TRUE(udp.recv(wifi, 50 * SRS_UTIME_MILLISECONDS).empty());

    HELPER_ASSERT_SUCCESS(udp.check(wifi, player));
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(udp.recv(wifi), player));
    EXPECT_TRUE(udp.recv(cellular, 50 * SRS_UTIME_MILLISECONDS).empty());

    EXPECT_EQ(2, conns.nn_ids_);
    EXPECT_TRUE(conns.ids_[udp.peer_id(wifi)] == player.conn_);
    EXPECT_TRUE(conns.ids_[udp.peer_id(cellular)] == player.conn_);
    EXPECT_EQ(2, (int)network->peer_addresses_.size());

    // The media the server sends goes to the address of the last check.
    char media[] = "media";
    HELPER_EXPECT_SUCCESS(network->write(media, sizeof(media), NULL));
    EXPECT_STREQ("media", udp.recv(wifi).c_str());
    EXPECT_TRUE(udp.recv(cellular, 50 * SRS_UTIME_MILLISECONDS).empty());
}

// A publisher with encryption off is published at its first ICE check, with no DTLS. The packets the session refuses
// fail at the UDP network with the session's error: RTP of an SSRC the offer did not name, RTP whose padding is longer
// than the packet, and a receiver report longer than its datagram. Each still counts as bytes from the client.
VOID TEST(AppRtcNetworkTest, PlainUdpPublisherPacketsRefusedBySession)
{
    srs_error_t err;

    RtcNetworkUtestUdp udp;
    HELPER_ASSERT_SUCCESS(udp.listen());
    int client = 0;
    HELPER_ASSERT_SUCCESS(udp.client(&client));

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());
    RtcNetworkUtestConns conns;
    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
    HELPER_ASSERT_SUCCESS(rtc_network_utest_start(publisher, conns, false, false));

    HELPER_ASSERT_SUCCESS(udp.check(client, publisher));
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(udp.recv(client), publisher));
    ISrsRtcNetwork *network = publisher.conn_->udp();
    EXPECT_TRUE(network->is_establelished());
    EXPECT_TRUE(publisher.publisher()->is_sender_started_);

    int64_t in = 0, out = 0;
    publisher.conn_->networks_->delta()->remark(&in, &out);

    std::string rtp = rtc_conn_utest_rtp(2001, 106, 100);
    HELPER_EXPECT_SUCCESS(network->on_rtp((char *)rtp.data(), (int)rtp.size()));
    EXPECT_EQ(1, server.source_->rtp_video_count_);

    std::string unknown = rtc_conn_utest_rtp(3001, 106, 101);
    err = network->on_rtp((char *)unknown.data(), (int)unknown.size());
    EXPECT_EQ(ERROR_RTC_NO_PUBLISHER, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);

    uint8_t padded[] = {0xA0, 106, 0x00, 0x66, 0x00, 0x01, 0x5F, 0x90, 0x00, 0x00, 0x07, 0xD1, 0x65, 0x88, 0x09};
    err = network->on_rtp((char *)padded, sizeof(padded));
    EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);

    uint8_t rr[] = {0x81, 201, 0x00, 0x07, 0x00, 0x00, 0x03, 0xE9, 0x00, 0x00, 0x07, 0xD1};
    err = network->on_rtcp((char *)rr, sizeof(rr));
    EXPECT_TRUE(err != srs_success);
    EXPECT_NE(ERROR_SUCCESS, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);

    EXPECT_EQ(1, server.source_->rtp_video_count_);
    publisher.conn_->networks_->delta()->remark(&in, &out);
    EXPECT_EQ((int64_t)(rtp.size() + unknown.size() + sizeof(padded) + sizeof(rr)), in);
}

// A frame of WebRTC over TCP, as RFC 4571 sends a packet: its size in 2 bytes, then the packet.
static std::string rtc_network_utest_frame(const std::string &packet)
{
    std::string b;
    b.push_back((char)(packet.size() >> 8));
    b.push_back((char)(packet.size() & 0xff));
    return b + packet;
}

// The RTC server's connection from a TCP client, whose socket reads the bytes the client sent; the connection's
// coroutine and context are the test's.
class RtcNetworkUtestTcp
{
public:
    MockProtocolReadWriterForTcpNetwork io_;
    MockContextForRtmpConn context_;
    MockInterruptableForRtcTcpConn coroutine_;
    MockContextIdSetterForRtcTcpConn cid_setter_;
    SrsRtcTcpConn *conn_;
    SrsSharedResource<ISrsRtcTcpConn> *wrapper_;

public:
    RtcNetworkUtestTcp(RtcNetworkUtestConns *conns, const std::string &bytes, std::string ip = "10.0.0.1", int port = 50000)
    {
        io_.set_read_data(bytes);
        conn_ = new SrsRtcTcpConn(&io_, ip, port);
        conn_->context_ = &context_;
        conn_->assemble();
        conn_->conn_manager_ = conns;
        wrapper_ = new SrsSharedResource<ISrsRtcTcpConn>(conn_);
        conn_->setup_owner(wrapper_, &coroutine_, &cid_setter_);
    }
    // Declared after the session, so freed before it: the session's TCP network lets go of the connection, as when it
    // closes, and the socket stays the test's.
    ~RtcNetworkUtestTcp()
    {
        if (conn_->session_) {
            ISrsRtcTcpNetwork *network = dynamic_cast<ISrsRtcTcpNetwork *>(conn_->session_->tcp());
            if (network->owner().get() == conn_) {
                network->update_sendonly_socket(NULL);
                network->set_owner(SrsSharedResource<ISrsRtcTcpConn>());
            }
        }
        conn_->skt_ = NULL;
        conn_->context_ = NULL;
        conn_->conn_manager_ = NULL;
        srs_freep(wrapper_);
    }
};

// A WebRTC publisher with encryption off connects over TCP, is published at its ICE check, and its RTP reaches the
// stream; then it sends the packet, and returns the error that ended the connection.
static srs_error_t rtc_network_utest_tcp_publisher_sends(const std::string &packet, int *nn_video, int *nn_written)
{
    srs_error_t err = srs_success;

    RtcConnUtestServer server;
    if ((err = server.initialize()) != srs_success) {
        return srs_error_wrap(err, "server");
    }
    RtcNetworkUtestConns conns;
    RtcConnUtestClient publisher(&server);
    if ((err = publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher()))) != srs_success) {
        return srs_error_wrap(err, "publish");
    }
    if ((err = rtc_network_utest_start(publisher, conns, false, false)) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    std::string bytes = rtc_network_utest_frame(rtc_network_utest_check(publisher));
    bytes += rtc_network_utest_frame(rtc_conn_utest_rtp(2001, 106, 100));
    bytes += rtc_network_utest_frame(packet);
    RtcNetworkUtestTcp tcp(&conns, bytes);

    err = tcp.conn_->do_cycle();
    *nn_video = server.source_->rtp_video_count_;
    *nn_written = (int)tcp.io_.written_data_.size();
    if (!publisher.conn_->tcp()->is_establelished()) {
        srs_freep(err);
        return srs_error_new(ERROR_RTC_TCP_PACKET, "not established");
    }
    return err;
}

// A WebRTC publisher over TCP sends a packet the session refuses after its RTP was published: the connection ends with
// the session's error. The packets: RTP of an SSRC the offer did not name, RTP whose padding is longer than the packet,
// a receiver report longer than its frame, and an ICE check cut short.
VOID TEST(AppRtcNetworkTest, TcpPublisherRefusedPacketEndsConnection)
{
    int nn_video = 0, nn_written = 0;

    srs_error_t err = rtc_network_utest_tcp_publisher_sends(rtc_conn_utest_rtp(3001, 106, 101), &nn_video, &nn_written);
    EXPECT_EQ(ERROR_RTC_NO_PUBLISHER, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, nn_video);
    EXPECT_EQ(1, nn_written);

    uint8_t padded[] = {0xA0, 106, 0x00, 0x66, 0x00, 0x01, 0x5F, 0x90, 0x00, 0x00, 0x07, 0xD1, 0x65, 0x88, 0x09};
    err = rtc_network_utest_tcp_publisher_sends(std::string((char *)padded, sizeof(padded)), &nn_video, &nn_written);
    EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, nn_video);

    uint8_t rr[] = {0x81, 201, 0x00, 0x07, 0x00, 0x00, 0x03, 0xE9, 0x00, 0x00, 0x07, 0xD1};
    err = rtc_network_utest_tcp_publisher_sends(std::string((char *)rr, sizeof(rr)), &nn_video, &nn_written);
    EXPECT_TRUE(err != srs_success);
    EXPECT_NE(ERROR_RTC_TCP_PACKET, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, nn_video);

    std::string check = stun_controlling_binding_request("srvufrag:cliufrag", "pwd", true);
    err = rtc_network_utest_tcp_publisher_sends(check.substr(0, check.size() - 8), &nn_video, &nn_written);
    EXPECT_EQ(ERROR_RTC_STUN, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, nn_video);
    EXPECT_EQ(1, nn_written);
}

// Someone who saw the username, but not the answer's ICE password, connects over TCP before the client, with a check
// not signed with it. The check is refused and not answered, and the connection lets go of the session's TCP network,
// so the client then connects over TCP and is answered.
VOID TEST(AppRtcNetworkTest, TcpCheckNotSignedWithAnswerPassword_RefusedThenClientConnects)
{
    srs_error_t err;

    RtcConnUtestServer server;
    HELPER_ASSERT_SUCCESS(server.initialize());
    RtcNetworkUtestConns conns;
    RtcConnUtestClient publisher(&server);
    HELPER_ASSERT_SUCCESS(publisher.publish(rtc_conn_utest_offer("0 1", rtc_conn_utest_opus_publisher() + rtc_conn_utest_h264_publisher())));
    HELPER_ASSERT_SUCCESS(rtc_network_utest_start(publisher, conns, true, true));
    ISrsRtcTcpNetwork *network = dynamic_cast<ISrsRtcTcpNetwork *>(publisher.conn_->tcp());

    std::string forged = stun_controlling_binding_request(publisher.conn_->username(), "not-the-answer-ice-password-123", true);
    RtcNetworkUtestTcp attacker(&conns, rtc_network_utest_frame(forged), "10.0.0.66", 40000);
    HELPER_EXPECT_SUCCESS(attacker.conn_->cycle());
    EXPECT_TRUE(attacker.io_.written_data_.empty());
    EXPECT_TRUE(NULL == network->owner().get());
    EXPECT_FALSE(network->is_establelished());

    RtcNetworkUtestTcp client(&conns, rtc_network_utest_frame(rtc_network_utest_check(publisher)));
    HELPER_EXPECT_SUCCESS(client.conn_->handshake());
    ASSERT_EQ(1, (int)client.io_.written_data_.size());
    HELPER_EXPECT_SUCCESS(rtc_network_utest_response(client.io_.written_data_[0].substr(2), publisher));
    EXPECT_TRUE(client.conn_ == network->owner().get());
    EXPECT_STREQ("10.0.0.1", dynamic_cast<SrsRtcTcpNetwork *>(network)->peer_ip_.c_str());
}

// A browser or a scanner sends an HTTP request to the WebRTC TCP port. Its first two bytes, "GE", read as a frame of
// 18245 bytes, more than a packet, so the connection is refused before it reads the rest.
VOID TEST(AppRtcNetworkTest, HttpRequestToRtcTcpPort_RefusedByFrameSize)
{
    RtcNetworkUtestConns conns;
    RtcNetworkUtestTcp tcp(&conns, "GET / HTTP/1.1\r\nHost: 10.0.0.1:8000\r\n\r\n");

    srs_error_t err = tcp.conn_->handshake();
    EXPECT_EQ(ERROR_RTC_TCP_SIZE, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(2, (int)tcp.io_.read_pos_);
    EXPECT_TRUE(tcp.io_.written_data_.empty());
}

// A client connects over TCP with the ICE check of a session the server no longer has, as after the session timed out:
// it is refused, and nothing is sent back.
VOID TEST(AppRtcNetworkTest, TcpCheckOfGoneSession_Refused)
{
    RtcNetworkUtestConns conns;
    std::string check = stun_controlling_binding_request("gonesrv:utst", "utestpassword1234567890ab", true);
    RtcNetworkUtestTcp tcp(&conns, rtc_network_utest_frame(check));

    srs_error_t err = tcp.conn_->handshake();
    EXPECT_EQ(ERROR_RTC_TCP_STUN, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_TRUE(NULL == tcp.conn_->session_);
    EXPECT_TRUE(tcp.io_.written_data_.empty());
}

// A client's first packet over TCP is an ICE check whose header counts more bytes than its frame holds, as a client
// that frames the message without its FINGERPRINT: it is refused before any session is looked up.
VOID TEST(AppRtcNetworkTest, TcpFirstCheckLongerThanItsFrame_Refused)
{
    RtcNetworkUtestConns conns;
    std::string check = stun_controlling_binding_request("srvufrag:utst", "utestpassword1234567890ab", true);
    RtcNetworkUtestTcp tcp(&conns, rtc_network_utest_frame(check.substr(0, check.size() - 8)));

    srs_error_t err = tcp.conn_->handshake();
    EXPECT_EQ(ERROR_RTC_STUN, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_TRUE(NULL == tcp.conn_->session_);
}
