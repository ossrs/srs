//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai20.hpp>

using namespace std;

#include <srs_app_log.hpp>
#include <srs_app_server.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_manual_mock.hpp>
#include <fcntl.h>
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

// The file the descriptor 0 tests put on descriptor 0, in place of the standard input of the test.
#define MOCK_FILE_LOG_FD0 "./objs/srs_utest_ai20_fd0.log"

VOID TEST(FileLogTest, ReopenClearsTheDescriptorWhenTheTankIsConsole)
{
    SrsFileLog log;

    // The logger holds an open log file, as it does after a start with the file tank.
    log.fd_ = ::open("/dev/null", O_RDWR);
    ASSERT_TRUE(log.fd_ > 0);
    int previous = log.fd_;

    // A reload moved the logs to the console, then SIGUSR1 asked for a reopen.
    log.log_to_file_tank_ = false;
    log.reopen();

    // GOAL: the descriptor is closed and the member no longer names it.
    EXPECT_EQ(-1, ::fcntl(previous, F_GETFD));
    EXPECT_EQ(-1, log.fd_);
}

VOID TEST(FileLogTest, WriteLogWritesToDescriptorZero)
{
    // The process was started with its standard input closed, so the log file is descriptor 0.
    int stdin_fd = ::dup(0);
    ASSERT_TRUE(stdin_fd > 0);
    int fd = ::open(MOCK_FILE_LOG_FD0, O_RDWR | O_CREAT | O_TRUNC, 0644);
    ASSERT_TRUE(fd > 0);
    ::dup2(fd, 0);
    ::close(fd);

    if (true) {
        SrsFileLog log;
        log.log_to_file_tank_ = true;
        log.fd_ = 0;

        char str_log[16] = "hello";
        log.write_log(log.fd_, str_log, 5, SrsLogLevelTrace);

        // The logger must not close the standard input of the test on the way out.
        log.fd_ = -1;
    }

    ::dup2(stdin_fd, 0);
    ::close(stdin_fd);

    // GOAL: the line reaches descriptor 0.
    char buf[16] = {0};
    fd = ::open(MOCK_FILE_LOG_FD0, O_RDONLY);
    ASSERT_TRUE(fd > 0);
    ssize_t nn = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    ::unlink(MOCK_FILE_LOG_FD0);
    EXPECT_EQ(6, (int)nn);
    EXPECT_STREQ("hello\n", buf);
}

VOID TEST(FileLogTest, ReopenClosesDescriptorZero)
{
    int stdin_fd = ::dup(0);
    ASSERT_TRUE(stdin_fd > 0);
    int fd = ::open(MOCK_FILE_LOG_FD0, O_RDWR | O_CREAT | O_TRUNC, 0644);
    ASSERT_TRUE(fd > 0);
    ::dup2(fd, 0);
    ::close(fd);

    bool closed = false;
    int fd_after = 0;
    if (true) {
        SrsFileLog log;
        log.log_to_file_tank_ = false;
        log.fd_ = 0;

        log.reopen();

        closed = (::fcntl(0, F_GETFD) == -1);
        fd_after = log.fd_;
        log.fd_ = -1;
    }

    ::dup2(stdin_fd, 0);
    ::close(stdin_fd);
    ::unlink(MOCK_FILE_LOG_FD0);

    // GOAL: descriptor 0 is closed like any other log file, and the logger holds no descriptor.
    EXPECT_TRUE(closed);
    EXPECT_EQ(-1, fd_after);
}

VOID TEST(FileLogTest, DestructorClosesDescriptorZero)
{
    int stdin_fd = ::dup(0);
    ASSERT_TRUE(stdin_fd > 0);
    int fd = ::open(MOCK_FILE_LOG_FD0, O_RDWR | O_CREAT | O_TRUNC, 0644);
    ASSERT_TRUE(fd > 0);
    ::dup2(fd, 0);
    ::close(fd);

    SrsFileLog *log = new SrsFileLog();
    log->fd_ = 0;
    srs_freep(log);

    bool closed = (::fcntl(0, F_GETFD) == -1);

    ::dup2(stdin_fd, 0);
    ::close(stdin_fd);
    ::unlink(MOCK_FILE_LOG_FD0);

    // GOAL: descriptor 0 was closed on the way out.
    EXPECT_TRUE(closed);
}
