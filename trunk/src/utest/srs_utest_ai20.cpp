//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai20.hpp>

using namespace std;

#include <srs_app_server.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_manual_mock.hpp>
#include <unistd.h>

// Mock ISrsSignalHandler implementation
MockSignalHandlerForManager::MockSignalHandlerForManager()
{
    signal_reload_count_ = 0;
    signal_fast_quit_count_ = 0;
    signal_gracefully_quit_count_ = 0;
    signal_reopen_log_count_ = 0;
    signal_persistence_config_count_ = 0;
    signal_assert_abort_count_ = 0;
    last_signo_ = 0;
}

MockSignalHandlerForManager::~MockSignalHandlerForManager()
{
}

void MockSignalHandlerForManager::on_signal(int signo)
{
    last_signo_ = signo;

    if (signo == SRS_SIGNAL_RELOAD) {
        signal_reload_count_++;
    } else if (signo == SRS_SIGNAL_FAST_QUIT) {
        signal_fast_quit_count_++;
    } else if (signo == SRS_SIGNAL_GRACEFULLY_QUIT) {
        signal_gracefully_quit_count_++;
    } else if (signo == SRS_SIGNAL_REOPEN_LOG) {
        signal_reopen_log_count_++;
    } else if (signo == SRS_SIGNAL_PERSISTENCE_CONFIG) {
        signal_persistence_config_count_++;
    } else if (signo == SRS_SIGNAL_ASSERT_ABORT) {
        signal_assert_abort_count_++;
    }
}

void MockSignalHandlerForManager::reset()
{
    signal_reload_count_ = 0;
    signal_fast_quit_count_ = 0;
    signal_gracefully_quit_count_ = 0;
    signal_reopen_log_count_ = 0;
    signal_persistence_config_count_ = 0;
    signal_assert_abort_count_ = 0;
    last_signo_ = 0;
}

VOID TEST(SignalManagerTest, MajorWorkflow)
{
    srs_error_t err;

    // Create mock signal handler
    MockSignalHandlerForManager mock_handler;

    // Create SrsSignalManager instance
    SrsUniquePtr<SrsSignalManager> signal_manager(new SrsSignalManager(&mock_handler));

    // Test 1: Initialize the signal manager (creates pipe)
    HELPER_EXPECT_SUCCESS(signal_manager->initialize());

    // Verify pipe was created
    EXPECT_TRUE(signal_manager->sig_pipe_[0] > 0);
    EXPECT_TRUE(signal_manager->sig_pipe_[1] > 0);
    EXPECT_TRUE(signal_manager->signal_read_stfd_ != NULL);

    // Test 2: Start the signal manager (starts coroutine)
    HELPER_EXPECT_SUCCESS(signal_manager->start());

    // Give coroutine time to start
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Test 3: Write SRS_SIGNAL_RELOAD directly to the pipe
    int signo = SRS_SIGNAL_RELOAD;
    ssize_t nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    // Give time for signal to be processed
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the signal was received and processed
    EXPECT_EQ(1, mock_handler.signal_reload_count_);
    EXPECT_EQ(SRS_SIGNAL_RELOAD, mock_handler.last_signo_);

    // Test 4: Write SRS_SIGNAL_REOPEN_LOG directly to the pipe
    signo = SRS_SIGNAL_REOPEN_LOG;
    nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    // Give time for signal to be processed
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the signal was received and processed
    EXPECT_EQ(1, mock_handler.signal_reopen_log_count_);
    EXPECT_EQ(SRS_SIGNAL_REOPEN_LOG, mock_handler.last_signo_);

    // Test 5: Write SRS_SIGNAL_FAST_QUIT directly to the pipe
    signo = SRS_SIGNAL_FAST_QUIT;
    nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    // Give time for signal to be processed
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the signal was received and processed
    EXPECT_EQ(1, mock_handler.signal_fast_quit_count_);
    EXPECT_EQ(SRS_SIGNAL_FAST_QUIT, mock_handler.last_signo_);

    // Test 6: Write SRS_SIGNAL_GRACEFULLY_QUIT directly to the pipe
    signo = SRS_SIGNAL_GRACEFULLY_QUIT;
    nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    // Give time for signal to be processed
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the signal was received and processed
    EXPECT_EQ(1, mock_handler.signal_gracefully_quit_count_);
    EXPECT_EQ(SRS_SIGNAL_GRACEFULLY_QUIT, mock_handler.last_signo_);

    // Test 7: Write SRS_SIGNAL_PERSISTENCE_CONFIG directly to the pipe
    signo = SRS_SIGNAL_PERSISTENCE_CONFIG;
    nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    // Give time for signal to be processed
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the signal was received and processed
    EXPECT_EQ(1, mock_handler.signal_persistence_config_count_);
    EXPECT_EQ(SRS_SIGNAL_PERSISTENCE_CONFIG, mock_handler.last_signo_);

    // Test 8: Write multiple signals in sequence
    signo = SRS_SIGNAL_RELOAD;
    nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    signo = SRS_SIGNAL_RELOAD;
    nwrite = ::write(signal_manager->sig_pipe_[1], &signo, sizeof(int));
    EXPECT_EQ((ssize_t)sizeof(int), nwrite);

    // Give time for signals to be processed
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the signal was received multiple times (now 3 times total)
    EXPECT_EQ(3, mock_handler.signal_reload_count_);
    EXPECT_EQ(SRS_SIGNAL_RELOAD, mock_handler.last_signo_);
}
