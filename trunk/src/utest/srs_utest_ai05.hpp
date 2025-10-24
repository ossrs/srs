//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI05_HPP
#define SRS_UTEST_AI05_HPP

/*
#include <srs_utest_ai05.hpp>
*/
#include <srs_utest.hpp>

#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_io.hpp>
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

public:
    MockSrsFastTimer();
    virtual ~MockSrsFastTimer();
    virtual srs_error_t on_timer(srs_utime_t interval);
    void clear();
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
