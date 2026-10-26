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
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai23.hpp>
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
        getsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &v, &n);
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
