//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai32.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>

using namespace std;

MockStageManagerForPithyPrint::MockStageManagerForPithyPrint()
{
}

MockStageManagerForPithyPrint::~MockStageManagerForPithyPrint()
{
    map<int, SrsStageInfo *>::iterator it;
    for (it = stages_.begin(); it != stages_.end(); ++it) {
        SrsStageInfo *stage = it->second;
        srs_freep(stage);
    }
}

SrsStageInfo *MockStageManagerForPithyPrint::fetch_or_create(int stage_id, bool *pnew)
{
    fetch_ids_.push_back(stage_id);

    map<int, SrsStageInfo *>::iterator it = stages_.find(stage_id);
    if (it != stages_.end()) {
        if (pnew) {
            *pnew = false;
        }
        return it->second;
    }

    SrsStageInfo *stage = new SrsStageInfo(stage_id);
    stages_[stage_id] = stage;

    if (pnew) {
        *pnew = true;
    }

    return stage;
}

MockConfigForStageInfo::MockConfigForStageInfo()
{
    pithy_print_ = 0;
}

MockConfigForStageInfo::~MockConfigForStageInfo()
{
}

srs_utime_t MockConfigForStageInfo::get_pithy_print()
{
    return pithy_print_;
}

string MockConfigForStageInfo::get_default_app_name()
{
    return "live";
}

string MockConfigForStageInfo::get_srt_default_mode()
{
    return "request";
}

MockKernelFactoryForStageInfo::MockKernelFactoryForStageInfo()
{
    pithy_print_ = 0;
    create_config_count_ = 0;
}

MockKernelFactoryForStageInfo::~MockKernelFactoryForStageInfo()
{
}

ISrsCoroutine *MockKernelFactoryForStageInfo::create_coroutine(const string &name, ISrsCoroutineHandler *handler,
                                                               SrsContextId cid)
{
    return NULL;
}

ISrsTime *MockKernelFactoryForStageInfo::create_time()
{
    return NULL;
}

ISrsConfig *MockKernelFactoryForStageInfo::create_config()
{
    create_config_count_++;

    MockConfigForStageInfo *config = new MockConfigForStageInfo();
    config->pithy_print_ = pithy_print_;
    return config;
}

ISrsCond *MockKernelFactoryForStageInfo::create_cond()
{
    return NULL;
}

MockClockForPithyPrint::MockClockForPithyPrint()
{
    now_ = 0;
}

MockClockForPithyPrint::~MockClockForPithyPrint()
{
}

srs_utime_t MockClockForPithyPrint::now()
{
    return now_;
}

// The printer enters its stage from assemble(), through the injected manager, not from the constructor.
VOID TEST(PithyPrintStageTest, AssembleEntersStageThroughInjectedManager)
{
    MockStageManagerForPithyPrint stages;
    MockClockForPithyPrint clk;

    SrsPithyPrint pprint(9001);
    pprint.stages_ = &stages;
    pprint.clk_ = &clk;

    clk.now_ = 30 * SRS_UTIME_SECONDS;
    pprint.assemble();

    ASSERT_EQ(1, (int)stages.fetch_ids_.size());
    EXPECT_EQ(9001, stages.fetch_ids_[0]);
    EXPECT_EQ(1, stages.stages_[9001]->nb_clients_);
    EXPECT_EQ(0, pprint.client_id_);
    EXPECT_EQ(30 * SRS_UTIME_SECONDS, pprint.previous_tick_);
}

// Constructing a printer has no observable effect on its stage, so a test can inject before anything happens.
VOID TEST(PithyPrintStageTest, ConstructorEntersNoStage)
{
    SrsStageInfo *stage = _srs_stages->fetch_or_create(9002);
    int nb_clients = stage->nb_clients_;

    if (true) {
        SrsPithyPrint pprint(9002);
        EXPECT_EQ(nb_clients, stage->nb_clients_);

        // Never entered, so do not leave: the printer must not decrement a stage it did not enter.
        pprint.stages_ = NULL;
    }

    EXPECT_EQ(nb_clients, stage->nb_clients_);
}

// Each printer leaves the stage it entered, through the same manager it entered by.
VOID TEST(PithyPrintStageTest, DestructorLeavesStageThroughInjectedManager)
{
    MockStageManagerForPithyPrint stages;
    MockClockForPithyPrint clk;
    SrsStageInfo *stage = NULL;

    if (true) {
        SrsPithyPrint first(9003);
        first.stages_ = &stages;
        first.clk_ = &clk;
        first.assemble();

        SrsPithyPrint second(9003);
        second.stages_ = &stages;
        second.clk_ = &clk;
        second.assemble();

        stage = stages.stages_[9003];
        ASSERT_TRUE(stage != NULL);
        EXPECT_EQ(2, stage->nb_clients_);
        EXPECT_EQ(0, first.client_id_);
        EXPECT_EQ(1, second.client_id_);
    }

    EXPECT_EQ(0, stage->nb_clients_);
}

// The elapsed time comes from the injected clock, and feeds both the client age and the shared stage.
VOID TEST(PithyPrintStageTest, ElapseAccumulatesInjectedClockDelta)
{
    MockStageManagerForPithyPrint stages;
    MockClockForPithyPrint clk;

    clk.now_ = 10 * SRS_UTIME_SECONDS;

    SrsPithyPrint pprint(9004);
    pprint.stages_ = &stages;
    pprint.clk_ = &clk;
    pprint.assemble();

    SrsStageInfo *stage = stages.stages_[9004];
    ASSERT_TRUE(stage != NULL);
    EXPECT_EQ(0, pprint.age());
    EXPECT_EQ(0, stage->age_);

    clk.now_ = 13 * SRS_UTIME_SECONDS;
    pprint.elapse();
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, pprint.age());
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, stage->age_);

    clk.now_ = 14 * SRS_UTIME_SECONDS;
    pprint.elapse();
    EXPECT_EQ(4 * SRS_UTIME_SECONDS, pprint.age());
    EXPECT_EQ(4 * SRS_UTIME_SECONDS, stage->age_);

    // The stage is looked up once by assemble() and once by the first elapse(), then cached.
    EXPECT_EQ(2, (int)stages.fetch_ids_.size());
}

// A clock that moves backwards contributes nothing, rather than a negative age.
VOID TEST(PithyPrintStageTest, ElapseClampsBackwardClock)
{
    MockStageManagerForPithyPrint stages;
    MockClockForPithyPrint clk;

    clk.now_ = 10 * SRS_UTIME_SECONDS;

    SrsPithyPrint pprint(9005);
    pprint.stages_ = &stages;
    pprint.clk_ = &clk;
    pprint.assemble();

    clk.now_ = 5 * SRS_UTIME_SECONDS;
    pprint.elapse();

    SrsStageInfo *stage = stages.stages_[9005];
    ASSERT_TRUE(stage != NULL);
    EXPECT_EQ(0, pprint.age());
    EXPECT_EQ(0, stage->age_);
    EXPECT_EQ(5 * SRS_UTIME_SECONDS, pprint.previous_tick_);
}

// The clients of a stage share its print interval: with two clients, one prints every two intervals.
VOID TEST(PithyPrintStageTest, CanPrintSharesIntervalAmongClients)
{
    MockStageManagerForPithyPrint stages;
    MockClockForPithyPrint clk;

    SrsPithyPrint first(9006);
    first.stages_ = &stages;
    first.clk_ = &clk;
    first.assemble();

    SrsPithyPrint second(9006);
    second.stages_ = &stages;
    second.clk_ = &clk;
    second.assemble();

    SrsStageInfo *stage = stages.stages_[9006];
    ASSERT_TRUE(stage != NULL);
    stage->interval_ = 3 * SRS_UTIME_SECONDS;
    stage->age_ = 0;
    EXPECT_EQ(2, stage->nb_clients_);

    clk.now_ = 5 * SRS_UTIME_SECONDS;
    first.elapse();
    EXPECT_FALSE(first.can_print());

    clk.now_ = 6 * SRS_UTIME_SECONDS;
    first.elapse();
    EXPECT_TRUE(first.can_print());

    // Printing resets the age of the shared stage, but not the age of the client.
    EXPECT_EQ(0, stage->age_);
    EXPECT_EQ(6 * SRS_UTIME_SECONDS, first.age());
    EXPECT_FALSE(second.can_print());
}

// Constructing a stage creates nothing, so a test can replace the factory before it is used.
VOID TEST(PithyPrintStageTest, StageInfoConstructorCreatesNoConfig)
{
    SrsStageInfo stage(9007, 1.0);

    EXPECT_TRUE(stage.config_ == NULL);
    EXPECT_EQ(0, stage.interval_);
}

// The print interval comes from a config the stage creates through the injected factory.
VOID TEST(PithyPrintStageTest, StageInfoAssembleReadsIntervalThroughFactory)
{
    MockKernelFactoryForStageInfo factory;
    factory.pithy_print_ = 7 * SRS_UTIME_SECONDS;

    SrsStageInfo stage(9008, 1.0);
    stage.factory_ = &factory;
    stage.assemble();

    EXPECT_EQ(1, factory.create_config_count_);
    EXPECT_TRUE(stage.config_ != NULL);
    EXPECT_EQ(7 * SRS_UTIME_SECONDS, stage.interval_);
}

// update_print_time() re-reads the interval from the config the stage already holds.
VOID TEST(PithyPrintStageTest, StageInfoUpdatePrintTimeRereadsTheConfig)
{
    MockConfigForStageInfo *config = new MockConfigForStageInfo();
    config->pithy_print_ = 9 * SRS_UTIME_SECONDS;

    SrsStageInfo stage(9012, 1.0);
    srs_freep(stage.config_); // whatever the stage holds is owned by it: free, then replace
    stage.config_ = config;

    stage.update_print_time();
    EXPECT_EQ(9 * SRS_UTIME_SECONDS, stage.interval_);
}

// A manager assembles the stages it creates, so every stage it hands out has its interval.
// Like FactoriesAssembleThePrinter below, this passes before and after the refactor: it exists to
// prove the move of the config read out of the constructor changed nothing a caller can see.
VOID TEST(PithyPrintStageTest, StageManagerAssemblesTheStagesItCreates)
{
    SrsStageManager manager;

    SrsStageInfo *stage = manager.fetch_or_create(9009);
    ASSERT_TRUE(stage != NULL);
    EXPECT_TRUE(stage->config_ != NULL);
    EXPECT_EQ(stage->config_->get_pithy_print(), stage->interval_);
}

// A standalone printer creates nothing either, and it is one client of its own stage.
VOID TEST(PithyPrintStageTest, AlonePithyPrintConstructorIsQuiescent)
{
    SrsAlonePithyPrint pprint;

    EXPECT_TRUE(pprint.info_.config_ == NULL);
    EXPECT_EQ(0, pprint.previous_tick_);
    EXPECT_EQ(1, pprint.info_.nb_clients_);
}

// The standalone printer ages on the injected clock and prints once its own interval passes.
VOID TEST(PithyPrintStageTest, AlonePithyPrintElapsesOnInjectedClock)
{
    MockKernelFactoryForStageInfo factory;
    factory.pithy_print_ = 7 * SRS_UTIME_SECONDS;
    MockClockForPithyPrint clk;

    SrsAlonePithyPrint pprint;
    pprint.info_.factory_ = &factory;
    pprint.clk_ = &clk;

    clk.now_ = 20 * SRS_UTIME_SECONDS;
    pprint.assemble();

    EXPECT_EQ(7 * SRS_UTIME_SECONDS, pprint.info_.interval_);
    EXPECT_EQ(20 * SRS_UTIME_SECONDS, pprint.previous_tick_);

    clk.now_ = 26 * SRS_UTIME_SECONDS;
    pprint.elapse();
    EXPECT_EQ(6 * SRS_UTIME_SECONDS, pprint.info_.age_);
    EXPECT_FALSE(pprint.can_print());

    clk.now_ = 27 * SRS_UTIME_SECONDS;
    pprint.elapse();
    EXPECT_EQ(7 * SRS_UTIME_SECONDS, pprint.info_.age_);
    EXPECT_TRUE(pprint.can_print());
    EXPECT_EQ(0, pprint.info_.age_);

    // A clock that moves backwards contributes nothing.
    clk.now_ = 1 * SRS_UTIME_SECONDS;
    pprint.elapse();
    EXPECT_EQ(0, pprint.info_.age_);
}

// The error printer rate-limits one error code on the injected clock.
VOID TEST(PithyPrintStageTest, ErrorPithyPrintRateLimitsOnInjectedClock)
{
    MockClockForPithyPrint clk;
    // The clock starts at a wall-clock value rather than zero, because can_print() reads a stored
    // tick of zero as "this code has no tick yet"; srs_time_now_cached() is never zero in production.
    clk.now_ = 1000 * SRS_UTIME_SECONDS;

    SrsErrorPithyPrint epp(1.0);
    epp.clk_ = &clk;

    // The first error of a code is always printed, and opens its stage.
    uint32_t nn = 0;
    EXPECT_TRUE(epp.can_print(9010, &nn));
    EXPECT_EQ(1, nn);

    SrsStageInfo *stage = epp.stages_.fetch_or_create(9010);
    ASSERT_TRUE(stage != NULL);
    stage->interval_ = 5 * SRS_UTIME_SECONDS;
    EXPECT_EQ(1, stage->nb_clients_);

    clk.now_ = 1003 * SRS_UTIME_SECONDS;
    EXPECT_FALSE(epp.can_print(9010, &nn));
    EXPECT_EQ(2, nn);
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, stage->age_);

    clk.now_ = 1005 * SRS_UTIME_SECONDS;
    EXPECT_TRUE(epp.can_print(9010, &nn));
    EXPECT_EQ(3, nn);
    EXPECT_EQ(0, stage->age_);

    // A clock that moves backwards contributes nothing.
    clk.now_ = 1001 * SRS_UTIME_SECONDS;
    EXPECT_FALSE(epp.can_print(9010, &nn));
    EXPECT_EQ(0, stage->age_);

    // Each error code is rate limited on its own, and the total count covers them all.
    EXPECT_TRUE(epp.can_print(9011, &nn));
    EXPECT_EQ(1, nn);
    EXPECT_EQ(5, epp.nn_count_);
}

// Every factory hands back an assembled printer, which enters and leaves the stage of its kind.
VOID TEST(PithyPrintStageTest, FactoriesAssembleThePrinter)
{
    // The stage ids of srs_kernel_pithy_print.cpp, which the factories share by kind.
    struct {
        SrsPithyPrint *(*create)();
        int stage_id;
    } factories[] = {
        {SrsPithyPrint::create_rtmp_play, 1},         {SrsPithyPrint::create_rtmp_publish, 2},
        {SrsPithyPrint::create_forwarder, 3},         {SrsPithyPrint::create_encoder, 4},
        {SrsPithyPrint::create_hls, 5},               {SrsPithyPrint::create_ingester, 6},
        {SrsPithyPrint::create_edge, 7},              {SrsPithyPrint::create_caster, 8},
        {SrsPithyPrint::create_http_stream, 9},       {SrsPithyPrint::create_http_stream_cache, 10},
        {SrsPithyPrint::create_exec, 11},             {SrsPithyPrint::create_rtc_play, 12},
        {SrsPithyPrint::create_srt_play, 15},         {SrsPithyPrint::create_srt_publish, 16},
    };

    for (int i = 0; i < (int)(sizeof(factories) / sizeof(factories[0])); i++) {
        SrsStageInfo *stage = _srs_stages->fetch_or_create(factories[i].stage_id);
        int nb_clients = stage->nb_clients_;

        if (true) {
            SrsUniquePtr<SrsPithyPrint> pprint(factories[i].create());
            EXPECT_EQ(nb_clients + 1, stage->nb_clients_) << "stage " << factories[i].stage_id;
        }

        EXPECT_EQ(nb_clients, stage->nb_clients_) << "stage " << factories[i].stage_id;
    }

    // The RTC sender and receiver get a stage of their own for each fd.
    SrsStageInfo *send = _srs_stages->fetch_or_create(7 << 16 | 13);
    int nb_send = send->nb_clients_;
    SrsStageInfo *recv = _srs_stages->fetch_or_create(7 << 16 | 14);
    int nb_recv = recv->nb_clients_;

    if (true) {
        SrsUniquePtr<SrsPithyPrint> sender(SrsPithyPrint::create_rtc_send(7));
        SrsUniquePtr<SrsPithyPrint> receiver(SrsPithyPrint::create_rtc_recv(7));
        EXPECT_EQ(nb_send + 1, send->nb_clients_);
        EXPECT_EQ(nb_recv + 1, recv->nb_clients_);
    }

    EXPECT_EQ(nb_send, send->nb_clients_);
    EXPECT_EQ(nb_recv, recv->nb_clients_);
}
