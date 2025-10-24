//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_pithy_print.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>

// Mock configuration for testing
class MockSrsConfigForPithy : public SrsConfig
{
public:
    srs_utime_t pithy_print_interval_;

public:
    MockSrsConfigForPithy()
    {
        pithy_print_interval_ = 10 * SRS_UTIME_SECONDS; // Default 10 seconds
    }

    virtual srs_utime_t get_pithy_print()
    {
        return pithy_print_interval_;
    }
};

VOID TEST(PithyPrintTest, SrsStageInfoBasicFunctionality)
{
    // Test basic initialization and functionality
    if (true) {
        SrsStageInfo stage(100, 1.0);
        EXPECT_EQ(100, stage.stage_id_);
        EXPECT_EQ(0, stage.nb_clients_);
        EXPECT_EQ(0, stage.age_);
        EXPECT_EQ(0, stage.nn_count_);
        EXPECT_EQ(1.0, stage.interval_ratio_);
    }

    // Test elapse functionality
    if (true) {
        SrsStageInfo stage(101, 1.0);
        srs_utime_t initial_age = stage.age_;
        srs_utime_t diff = 5 * SRS_UTIME_SECONDS;

        stage.elapse(diff);
        EXPECT_EQ(initial_age + diff, stage.age_);
    }

    // Test can_print with no clients
    if (true) {
        SrsStageInfo stage(102, 1.0);
        stage.nb_clients_ = 0;
        stage.age_ = 10 * SRS_UTIME_SECONDS;

        // With no clients, should be able to print immediately
        EXPECT_TRUE(stage.can_print());
        // After printing, age should be reset
        EXPECT_EQ(0, stage.age_);
    }

    // Test can_print with clients
    if (true) {
        SrsStageInfo stage(103, 1.0);
        stage.nb_clients_ = 2;
        stage.interval_ = 10 * SRS_UTIME_SECONDS;
        stage.age_ = 15 * SRS_UTIME_SECONDS; // Less than required (2 * 10 = 20)

        EXPECT_FALSE(stage.can_print());
        EXPECT_EQ(15 * SRS_UTIME_SECONDS, stage.age_); // Age should not be reset

        stage.age_ = 25 * SRS_UTIME_SECONDS; // More than required
        EXPECT_TRUE(stage.can_print());
        EXPECT_EQ(0, stage.age_); // Age should be reset after printing
    }
}

VOID TEST(PithyPrintTest, SrsStageManagerBasicFunctionality)
{
    // Test fetch_or_create functionality
    if (true) {
        SrsStageManager manager;
        bool is_new = false;

        // Create new stage
        SrsStageInfo *stage1 = manager.fetch_or_create(200, &is_new);
        EXPECT_TRUE(stage1 != NULL);
        EXPECT_TRUE(is_new);
        EXPECT_EQ(200, stage1->stage_id_);

        // Fetch existing stage
        SrsStageInfo *stage2 = manager.fetch_or_create(200, &is_new);
        EXPECT_TRUE(stage2 != NULL);
        EXPECT_FALSE(is_new);
        EXPECT_EQ(stage1, stage2); // Should be the same object

        // Create another new stage
        SrsStageInfo *stage3 = manager.fetch_or_create(201, &is_new);
        EXPECT_TRUE(stage3 != NULL);
        EXPECT_TRUE(is_new);
        EXPECT_EQ(201, stage3->stage_id_);
        EXPECT_NE(stage1, stage3); // Should be different objects
    }

    // Test without pnew parameter
    if (true) {
        SrsStageManager manager;

        SrsStageInfo *stage = manager.fetch_or_create(300);
        EXPECT_TRUE(stage != NULL);
        EXPECT_EQ(300, stage->stage_id_);
    }
}

VOID TEST(PithyPrintTest, SrsErrorPithyPrintBasicFunctionality)
{
    // Test basic initialization
    if (true) {
        SrsErrorPithyPrint epp(2.0);
        EXPECT_EQ(0, epp.nn_count_);
    }

    // Test can_print with error codes
    if (true) {
        SrsErrorPithyPrint epp(1.0);
        uint32_t nn = 0;

        // First call should always return true (new stage)
        EXPECT_TRUE(epp.can_print(1001, &nn));
        EXPECT_EQ(1, nn);
        EXPECT_EQ(1, epp.nn_count_);

        // Subsequent calls depend on timing
        EXPECT_FALSE(epp.can_print(1001, &nn));
        EXPECT_EQ(2, nn);
        EXPECT_EQ(2, epp.nn_count_);
    }

    // Test can_print with srs_error_t
    if (true) {
        SrsErrorPithyPrint epp(1.0);
        srs_error_t err = srs_error_new(1002, "test error");
        uint32_t nn = 0;

        EXPECT_TRUE(epp.can_print(err, &nn));
        EXPECT_EQ(1, nn);

        srs_freep(err);
    }
}

VOID TEST(PithyPrintTest, SrsAlonePithyPrintBasicFunctionality)
{
    // Test basic initialization
    if (true) {
        SrsAlonePithyPrint app;
        // Should be initialized with nb_clients_ = 1
        EXPECT_EQ(1, app.info_.nb_clients_);
    }

    // Test elapse and can_print
    if (true) {
        SrsAlonePithyPrint app;

        // Initially should not be able to print
        EXPECT_FALSE(app.can_print());

        // Simulate time passage
        app.elapse();

        // Still might not be able to print depending on interval
        // This test mainly verifies no crashes occur
        app.can_print();
    }
}

VOID TEST(PithyPrintTest, SrsPithyPrintStaticCreators)
{
    // Test all static creator methods
    if (true) {
        SrsPithyPrint *rtmp_play = SrsPithyPrint::create_rtmp_play();
        EXPECT_TRUE(rtmp_play != NULL);
        srs_freep(rtmp_play);

        SrsPithyPrint *rtmp_publish = SrsPithyPrint::create_rtmp_publish();
        EXPECT_TRUE(rtmp_publish != NULL);
        srs_freep(rtmp_publish);

        SrsPithyPrint *hls = SrsPithyPrint::create_hls();
        EXPECT_TRUE(hls != NULL);
        srs_freep(hls);

        SrsPithyPrint *forwarder = SrsPithyPrint::create_forwarder();
        EXPECT_TRUE(forwarder != NULL);
        srs_freep(forwarder);

        SrsPithyPrint *encoder = SrsPithyPrint::create_encoder();
        EXPECT_TRUE(encoder != NULL);
        srs_freep(encoder);

        SrsPithyPrint *exec = SrsPithyPrint::create_exec();
        EXPECT_TRUE(exec != NULL);
        srs_freep(exec);

        SrsPithyPrint *ingester = SrsPithyPrint::create_ingester();
        EXPECT_TRUE(ingester != NULL);
        srs_freep(ingester);

        SrsPithyPrint *edge = SrsPithyPrint::create_edge();
        EXPECT_TRUE(edge != NULL);
        srs_freep(edge);

        SrsPithyPrint *caster = SrsPithyPrint::create_caster();
        EXPECT_TRUE(caster != NULL);
        srs_freep(caster);

        SrsPithyPrint *http_stream = SrsPithyPrint::create_http_stream();
        EXPECT_TRUE(http_stream != NULL);
        srs_freep(http_stream);

        SrsPithyPrint *http_stream_cache = SrsPithyPrint::create_http_stream_cache();
        EXPECT_TRUE(http_stream_cache != NULL);
        srs_freep(http_stream_cache);

        SrsPithyPrint *rtc_play = SrsPithyPrint::create_rtc_play();
        EXPECT_TRUE(rtc_play != NULL);
        srs_freep(rtc_play);

        SrsPithyPrint *rtc_send = SrsPithyPrint::create_rtc_send(100);
        EXPECT_TRUE(rtc_send != NULL);
        srs_freep(rtc_send);

        SrsPithyPrint *rtc_recv = SrsPithyPrint::create_rtc_recv(101);
        EXPECT_TRUE(rtc_recv != NULL);
        srs_freep(rtc_recv);

        SrsPithyPrint *srt_play = SrsPithyPrint::create_srt_play();
        EXPECT_TRUE(srt_play != NULL);
        srs_freep(srt_play);

        SrsPithyPrint *srt_publish = SrsPithyPrint::create_srt_publish();
        EXPECT_TRUE(srt_publish != NULL);
        srs_freep(srt_publish);
    }
}
