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
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai38.hpp>
#include <srs_utest_manual_config.hpp>

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
