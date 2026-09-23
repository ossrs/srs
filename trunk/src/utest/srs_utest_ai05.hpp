//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI05_HPP
#define SRS_UTEST_AI05_HPP

/*
#include <srs_utest_ai05.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_factory.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_kernel_rtc_queue.hpp>
#include <srs_utest_manual_kernel.hpp>

// Mock classes for IO testing
class MockSrsReader : public ISrsReader
{
public:
    std::string data_;
    size_t pos_;
    srs_error_t read_error_;

public:
    MockSrsReader(const std::string &data);
    virtual ~MockSrsReader();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    void set_error(srs_error_t err);
};

class MockSrsWriter : public ISrsWriter
{
public:
    std::string written_data_;
    srs_error_t write_error_;

public:
    MockSrsWriter();
    virtual ~MockSrsWriter();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
    void set_error(srs_error_t err);
};

class MockSrsSeeker : public ISrsSeeker
{
public:
    off_t position_;
    srs_error_t seek_error_;

public:
    MockSrsSeeker();
    virtual ~MockSrsSeeker();
    virtual srs_error_t lseek(off_t offset, int whence, off_t *seeked);
    void set_error(srs_error_t err);
};

// Mock classes for resource testing
class MockSrsResource : public ISrsResource
{
public:
    SrsContextId cid_;
    std::string desc_;

public:
    MockSrsResource();
    virtual ~MockSrsResource();
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    void set_id(const SrsContextId &cid);
    void set_desc(const std::string &desc);
};

class MockSrsDisposingHandler : public ISrsDisposingHandler
{
public:
    std::vector<ISrsResource *> before_dispose_calls_;
    std::vector<ISrsResource *> disposing_calls_;

public:
    MockSrsDisposingHandler();
    virtual ~MockSrsDisposingHandler();
    virtual void on_before_dispose(ISrsResource *c);
    virtual void on_disposing(ISrsResource *c);
};

// Mock classes for hourglass testing
class MockSrsHourGlass : public ISrsHourGlassHandler
{
public:
    std::vector<int> events_;
    std::vector<srs_utime_t> intervals_;
    std::vector<srs_utime_t> ticks_;
    // The error to return from notify, owned by the hourglass under test.
    srs_error_t notify_error_;

public:
    MockSrsHourGlass();
    virtual ~MockSrsHourGlass();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);
    void clear();
};

class MockSrsFastTimer : public ISrsFastTimerHandler
{
public:
    std::vector<srs_utime_t> timer_calls_;
    // The error to return from on_timer, owned by the timer under test.
    srs_error_t on_timer_error_;

public:
    MockSrsFastTimer();
    virtual ~MockSrsFastTimer();
    virtual srs_error_t on_timer(srs_utime_t interval);
    void clear();
};

// Mock ISrsFastTimerHandler that unsubscribes a handler while the timer is dispatching, to cover
// the subscriber list changing under SrsFastTimer::cycle().
class MockUnsubscribingFastTimer : public ISrsFastTimerHandler
{
public:
    // The timer to unsubscribe from, and the handler to remove, which may be this one.
    ISrsFastTimer *timer_;
    ISrsFastTimerHandler *target_;
    int on_timer_count_;

public:
    MockUnsubscribingFastTimer();
    virtual ~MockUnsubscribingFastTimer();

public:
    virtual srs_error_t on_timer(srs_utime_t interval);
};

// Mock ISrsCoroutine for testing SrsFastTimer::cycle()
class MockCoroutineForFastTimer : public ISrsCoroutine
{
public:
    int pull_count_;
    // Return success for the first pull_success_ pulls, then pull_error_.
    int pull_success_;
    int start_count_;
    int stop_count_;
    srs_error_t start_error_;
    SrsContextId cid_;

public:
    MockCoroutineForFastTimer();
    virtual ~MockCoroutineForFastTimer();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

// Mock ISrsTime for testing SrsFastTimer::cycle()
class MockTimeForFastTimer : public ISrsTime
{
public:
    std::vector<srs_utime_t> usleep_calls_;

public:
    MockTimeForFastTimer();
    virtual ~MockTimeForFastTimer();

public:
    virtual void usleep(srs_utime_t duration);
};

// Mock ISrsContext for testing SrsFastTimer::assemble()
class MockContextForFastTimer : public ISrsContext
{
public:
    SrsContextId id_;
    int get_id_count_;

public:
    MockContextForFastTimer();
    virtual ~MockContextForFastTimer();

public:
    virtual SrsContextId generate_id();
    virtual const SrsContextId &get_id();
    virtual const SrsContextId &set_id(const SrsContextId &v);
};

// Mock ISrsKernelFactory for testing SrsFastTimer::assemble()
class MockKernelFactoryForFastTimer : public ISrsKernelFactory
{
public:
    // The objects returned by the factory, borrowed and not owned by the mock.
    ISrsCoroutine *coroutine_;
    ISrsTime *time_;
    // What the factory was asked to create.
    int create_coroutine_count_;
    int create_time_count_;
    std::string coroutine_name_;
    ISrsCoroutineHandler *coroutine_handler_;
    SrsContextId coroutine_cid_;

public:
    MockKernelFactoryForFastTimer();
    virtual ~MockKernelFactoryForFastTimer();

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
};

// Mock RTP ring buffer for testing NACK receiver
class MockRtpRingBuffer : public SrsRtpRingBuffer
{
public:
    std::vector<uint16_t> dropped_seqs_;
    bool nack_list_full_called_;

public:
    MockRtpRingBuffer();
    virtual ~MockRtpRingBuffer();
    virtual void notify_drop_seq(uint16_t seq);
    virtual void notify_nack_list_full();
    void clear_mock_data();
};

#endif
