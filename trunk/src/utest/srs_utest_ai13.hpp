//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI13_HPP
#define SRS_UTEST_AI13_HPP

/*
#include <srs_utest_ai13.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_manual_kernel.hpp>

// Extended mock app config for HLS notify testing
class MockAppConfigForHlsNotify : public MockAppConfig
{
public:
    SrsConfDirective *on_hls_notify_directive_;
    int hls_nb_notify_;
    bool hls_enabled_;

public:
    MockAppConfigForHlsNotify();
    virtual ~MockAppConfigForHlsNotify();
    virtual SrsConfDirective *get_vhost_on_hls_notify(std::string vhost);
    virtual int get_vhost_hls_nb_notify(std::string vhost);
    virtual bool get_hls_enabled(std::string vhost);
    void set_on_hls_notify_urls(const std::vector<std::string> &urls);
    void clear_on_hls_notify_directive();
    void set_hls_nb_notify(int nb_notify);
    void set_hls_enabled(bool enabled);
};

// Extended mock HTTP hooks for HLS notify testing
class MockHttpHooksForHlsNotify : public MockHttpHooks
{
public:
    std::vector<std::string> on_hls_notify_urls_;
    std::vector<std::string> on_hls_notify_ts_urls_;
    std::vector<int> on_hls_notify_nb_notifies_;
    int on_hls_notify_count_;
    srs_error_t on_hls_notify_error_;

public:
    MockHttpHooksForHlsNotify();
    virtual ~MockHttpHooksForHlsNotify();
    virtual srs_error_t on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify);
    void set_on_hls_notify_error(srs_error_t err);
    void reset();
};

// Mock request class for HLS testing
class MockHlsRequest : public ISrsRequest
{
public:
    MockHlsRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockHlsRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Proxy file writer that wraps MockSrsFileWriter for testing
class MockFileWriterProxy : public ISrsFileWriter
{
public:
    MockSrsFileWriter *real_writer_;

public:
    MockFileWriterProxy(MockSrsFileWriter *writer);
    virtual ~MockFileWriterProxy();
    virtual srs_error_t open(std::string file);
    virtual srs_error_t open_append(std::string file);
    virtual void close();
    virtual bool is_open();
    virtual void seek2(int64_t offset);
    virtual int64_t tellg();
    virtual srs_error_t write(void *buf, size_t count, ssize_t *pnwrite);
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite);
    virtual srs_error_t lseek(off_t offset, int whence, off_t *seeked);
};

// Mock app factory for HLS testing
// Mock encrypted file writer for HLS encryption testing
class MockEncFileWriter : public SrsEncFileWriter
{
public:
    MockEncFileWriter();
    virtual ~MockEncFileWriter();
};

class MockAppFactory : public SrsAppFactory
{
public:
    MockSrsFileWriter *real_writer_;
    MockSrsFile *real_file_;
    MockSrsFileReader *real_reader_;
    ISrsFragmentedMp4 *real_fragmented_mp4_;
    int create_file_writer_count_;
    int create_file_reader_count_;

public:
    MockAppFactory();
    virtual ~MockAppFactory();
    virtual ISrsFileWriter *create_file_writer();
    virtual ISrsFileReader *create_file_reader();
    virtual ISrsFragmentedMp4 *create_fragmented_mp4();
    void reset();
};

// Mock HLS muxer for testing SrsHlsController::reap_segment
class MockHlsMuxer
{
public:
    int segment_close_count_;
    int segment_open_count_;
    int flush_video_count_;
    int flush_audio_count_;
    srs_error_t segment_close_error_;
    srs_error_t segment_open_error_;
    srs_error_t flush_video_error_;
    srs_error_t flush_audio_error_;

public:
    MockHlsMuxer();
    virtual ~MockHlsMuxer();
    srs_error_t segment_close();
    srs_error_t segment_open();
    srs_error_t flush_video(SrsTsMessageCache *cache);
    srs_error_t flush_audio(SrsTsMessageCache *cache);
    void reset();
    void set_segment_close_error(srs_error_t err);
    void set_segment_open_error(srs_error_t err);
    void set_flush_video_error(srs_error_t err);
    void set_flush_audio_error(srs_error_t err);
};

// Mock HLS controller for testing SrsHls::reload
class MockHlsController : public ISrsHlsController
{
public:
    int initialize_count_;
    int dispose_count_;
    int on_publish_count_;
    int on_unpublish_count_;
    int write_audio_count_;
    int write_video_count_;
    int on_sequence_header_count_;
    srs_error_t initialize_error_;
    srs_error_t on_publish_error_;
    srs_error_t on_unpublish_error_;

public:
    MockHlsController();
    virtual ~MockHlsController();
    virtual srs_error_t initialize();
    virtual void dispose();
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual srs_error_t on_unpublish();
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual srs_error_t on_sequence_header(SrsMediaPacket *msg, SrsFormat *format);
    virtual int sequence_no();
    virtual std::string ts_url();
    virtual srs_utime_t duration();
    virtual int deviation();
    void reset();
    void set_initialize_error(srs_error_t err);
    void set_on_publish_error(srs_error_t err);
    void set_on_unpublish_error(srs_error_t err);
};

#endif
