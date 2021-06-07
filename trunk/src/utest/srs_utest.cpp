//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_server.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_threads.hpp>

#include <string>
using namespace std;

// Temporary disk config.
std::string _srs_tmp_file_prefix = "/tmp/srs-utest-";
// Temporary network config.
std::string _srs_tmp_host = "127.0.0.1";
int _srs_tmp_port = 11935;
srs_utime_t _srs_tmp_timeout = (100 * SRS_UTIME_MILLISECONDS);

// kernel module.
ISrsLog* _srs_log = NULL;
ISrsContext* _srs_context = NULL;
// app module.
SrsConfig* _srs_config = NULL;
SrsServer* _srs_server = NULL;
bool _srs_in_docker = false;

#include <srs_app_st.hpp>

// Initialize global settings.
srs_error_t prepare_main() {
    srs_error_t err = srs_success;

    if ((err = srs_thread_initialize()) != srs_success) {
        return srs_error_wrap(err, "init st");
    }

    srs_freep(_srs_log);
    _srs_log = new MockEmptyLog(SrsLogLevelDisabled);

    if ((err = _srs_rtc_dtls_certificate->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    srs_freep(_srs_context);
    _srs_context = new SrsThreadContext();

    return err;
}

// We could do something in the main of utest.
// Copy from gtest-1.6.0/src/gtest_main.cc
GTEST_API_ int main(int argc, char **argv) {
    srs_error_t err = srs_success;

    if ((err = prepare_main()) != srs_success) {
        fprintf(stderr, "Failed, %s\n", srs_error_desc(err).c_str());

        int ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

MockEmptyLog::MockEmptyLog(SrsLogLevel l)
{
    level = l;
}

MockEmptyLog::~MockEmptyLog()
{
}

void srs_bytes_print(char* pa, int size)
{
    for(int i = 0; i < size; i++) {
        char v = pa[i];
        printf("%#x ", v);
    }
    printf("\n");
}

// basic test and samples.
VOID TEST(SampleTest, FastSampleInt64Test) 
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

VOID TEST(SampleTest, FastSampleMacrosTest) 
{
    EXPECT_TRUE(1);
    EXPECT_FALSE(0);
    
    EXPECT_EQ(1, 1); // ==
    EXPECT_NE(1, 2); // !=
    EXPECT_LE(1, 2); // <=
    EXPECT_LT(1, 2); // <
    EXPECT_GE(2, 1); // >=
    EXPECT_GT(2, 1); // >

    EXPECT_STREQ("winlin", "winlin");
    EXPECT_STRNE("winlin", "srs");
    EXPECT_STRCASEEQ("winlin", "Winlin");
    EXPECT_STRCASENE("winlin", "srs");
    
    EXPECT_FLOAT_EQ(1.0, 1.000000000000001);
    EXPECT_DOUBLE_EQ(1.0, 1.0000000000000001);
    EXPECT_NEAR(10, 15, 5);
}

VOID TEST(SampleTest, StringEQTest)
{
    string str = "100";
    EXPECT_TRUE("100" == str);
    EXPECT_EQ("100", str);
    EXPECT_STREQ("100", str.c_str());
}

class MockSrsContextId
{
public:
    MockSrsContextId() {
        bind_ = NULL;
    }
    MockSrsContextId(const MockSrsContextId& cp){
        bind_ = NULL;
        if (cp.bind_) {
            bind_ = cp.bind_->copy();
        }
    }
    MockSrsContextId& operator= (const MockSrsContextId& cp) {
        srs_freep(bind_);
        if (cp.bind_) {
            bind_ = cp.bind_->copy();
        }
        return *this;
    }
    virtual ~MockSrsContextId() {
        srs_freep(bind_);
    }
public:
    MockSrsContextId* copy() const {
        MockSrsContextId* cp = new MockSrsContextId();
        if (bind_) {
            cp->bind_ = bind_->copy();
        }
        return cp;
    }
private:
    MockSrsContextId* bind_;
};

VOID TEST(SampleTest, ContextTest)
{
    MockSrsContextId cid;
    cid.bind_ = new MockSrsContextId();

    static std::map<int, MockSrsContextId> cache;
    cache[0] = cid;
    cache[0] = cid;
}

