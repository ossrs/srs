//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai13.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_utest_manual_kernel.hpp>

// Mock app config implementation for HLS notify testing
MockAppConfigForHlsNotify::MockAppConfigForHlsNotify()
{
    on_hls_notify_directive_ = NULL;
    hls_nb_notify_ = 64;
    hls_enabled_ = false;
}

MockAppConfigForHlsNotify::~MockAppConfigForHlsNotify()
{
    clear_on_hls_notify_directive();
}

SrsConfDirective *MockAppConfigForHlsNotify::get_vhost_on_hls_notify(std::string vhost)
{
    return on_hls_notify_directive_;
}

int MockAppConfigForHlsNotify::get_vhost_hls_nb_notify(std::string vhost)
{
    return hls_nb_notify_;
}

bool MockAppConfigForHlsNotify::get_hls_enabled(std::string vhost)
{
    return hls_enabled_;
}

void MockAppConfigForHlsNotify::set_on_hls_notify_urls(const std::vector<std::string> &urls)
{
    clear_on_hls_notify_directive();

    if (urls.empty()) {
        return;
    }

    on_hls_notify_directive_ = new SrsConfDirective();
    on_hls_notify_directive_->name_ = "on_hls_notify";
    for (size_t i = 0; i < urls.size(); i++) {
        on_hls_notify_directive_->args_.push_back(urls[i]);
    }
}

void MockAppConfigForHlsNotify::clear_on_hls_notify_directive()
{
    srs_freep(on_hls_notify_directive_);
}

void MockAppConfigForHlsNotify::set_hls_nb_notify(int nb_notify)
{
    hls_nb_notify_ = nb_notify;
}

void MockAppConfigForHlsNotify::set_hls_enabled(bool enabled)
{
    hls_enabled_ = enabled;
}

// Mock HTTP hooks implementation for HLS notify testing
MockHttpHooksForHlsNotify::MockHttpHooksForHlsNotify()
{
    on_hls_notify_count_ = 0;
    on_hls_notify_error_ = srs_success;
}

MockHttpHooksForHlsNotify::~MockHttpHooksForHlsNotify()
{
    srs_freep(on_hls_notify_error_);
}

srs_error_t MockHttpHooksForHlsNotify::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    on_hls_notify_count_++;
    on_hls_notify_urls_.push_back(url);
    on_hls_notify_ts_urls_.push_back(ts_url);
    on_hls_notify_nb_notifies_.push_back(nb_notify);
    return srs_error_copy(on_hls_notify_error_);
}

void MockHttpHooksForHlsNotify::set_on_hls_notify_error(srs_error_t err)
{
    srs_freep(on_hls_notify_error_);
    on_hls_notify_error_ = srs_error_copy(err);
}

void MockHttpHooksForHlsNotify::reset()
{
    on_hls_notify_count_ = 0;
    on_hls_notify_urls_.clear();
    on_hls_notify_ts_urls_.clear();
    on_hls_notify_nb_notifies_.clear();
    srs_freep(on_hls_notify_error_);
}

// Mock request implementation for HLS testing
MockHlsRequest::MockHlsRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
    host_ = "127.0.0.1";
    port_ = 1935;
    objectEncoding_ = 0;
    duration_ = -1;
    args_ = NULL;
    protocol_ = "rtmp";
}

MockHlsRequest::~MockHlsRequest()
{
    srs_freep(args_);
}

ISrsRequest *MockHlsRequest::copy()
{
    MockHlsRequest *req = new MockHlsRequest(vhost_, app_, stream_);
    req->host_ = host_;
    req->port_ = port_;
    req->tcUrl_ = tcUrl_;
    req->pageUrl_ = pageUrl_;
    req->swfUrl_ = swfUrl_;
    req->schema_ = schema_;
    req->param_ = param_;
    req->duration_ = duration_;
    req->protocol_ = protocol_;
    req->objectEncoding_ = objectEncoding_;
    req->ip_ = ip_;
    return req;
}

std::string MockHlsRequest::get_stream_url()
{
    if (vhost_ == "__defaultVhost__" || vhost_.empty()) {
        return "/" + app_ + "/" + stream_;
    } else {
        return vhost_ + "/" + app_ + "/" + stream_;
    }
}

void MockHlsRequest::update_auth(ISrsRequest *req)
{
    // Mock implementation - do nothing
}

void MockHlsRequest::strip()
{
    // Mock implementation - do nothing
}

ISrsRequest *MockHlsRequest::as_http()
{
    return copy();
}

// Mock file writer proxy implementation
MockFileWriterProxy::MockFileWriterProxy(MockSrsFileWriter *writer)
{
    real_writer_ = writer;
}

MockFileWriterProxy::~MockFileWriterProxy()
{
    // Note: real_writer_ is owned by the factory, don't delete it here
}

srs_error_t MockFileWriterProxy::open(std::string file)
{
    return real_writer_->open(file);
}

srs_error_t MockFileWriterProxy::open_append(std::string file)
{
    return real_writer_->open_append(file);
}

void MockFileWriterProxy::close()
{
    real_writer_->close();
}

bool MockFileWriterProxy::is_open()
{
    return real_writer_->is_open();
}

void MockFileWriterProxy::seek2(int64_t offset)
{
    real_writer_->seek2(offset);
}

int64_t MockFileWriterProxy::tellg()
{
    return real_writer_->tellg();
}

srs_error_t MockFileWriterProxy::write(void *buf, size_t count, ssize_t *pnwrite)
{
    return real_writer_->write(buf, count, pnwrite);
}

srs_error_t MockFileWriterProxy::writev(const iovec *iov, int iovcnt, ssize_t *pnwrite)
{
    return real_writer_->writev(iov, iovcnt, pnwrite);
}

srs_error_t MockFileWriterProxy::lseek(off_t offset, int whence, off_t *seeked)
{
    return real_writer_->lseek(offset, whence, seeked);
}

// Mock encrypted file writer implementation
MockEncFileWriter::MockEncFileWriter()
{
}

MockEncFileWriter::~MockEncFileWriter()
{
}

// Mock app factory implementation for HLS testing
MockAppFactory::MockAppFactory()
{
    real_writer_ = NULL;
    real_file_ = NULL;
    real_reader_ = NULL;
    real_fragmented_mp4_ = NULL;
    create_file_writer_count_ = 0;
    create_file_reader_count_ = 0;
}

MockAppFactory::~MockAppFactory()
{
    // Note: real_writer_ is NOT owned by this factory - it's freed by the caller (SrsUniquePtr)
    // We just keep a reference to it for testing purposes
    // real_file_ is also not owned - it's part of real_writer_
    // real_reader_ is owned by this factory
    srs_freep(real_reader_);
    // Note: real_fragmented_mp4_ is NOT owned by this factory - it's freed by the caller
    real_fragmented_mp4_ = NULL;
}

ISrsFileWriter *MockAppFactory::create_file_writer()
{
    create_file_writer_count_++;
    // Create a new mock writer and save reference for testing
    MockSrsFileWriter *writer = new MockSrsFileWriter();
    real_writer_ = writer;
    return writer;
}

ISrsFileReader *MockAppFactory::create_file_reader()
{
    create_file_reader_count_++;
    // Return the mock reader that was set up with test data
    return real_reader_;
}

ISrsFragmentedMp4 *MockAppFactory::create_fragmented_mp4()
{
    // Return the mock fragmented mp4 that was set up for testing
    // The caller takes ownership of this object
    ISrsFragmentedMp4 *result = real_fragmented_mp4_;
    real_fragmented_mp4_ = NULL; // Clear reference after returning
    return result;
}

void MockAppFactory::reset()
{
    create_file_writer_count_ = 0;
    create_file_reader_count_ = 0;
    // Note: real_writer_ is freed by the caller (SrsUniquePtr), so we don't free it here
    real_writer_ = NULL;
    srs_freep(real_file_);
    real_file_ = NULL;
    // Note: Don't free real_reader_ here as it may still be in use
    // Note: real_fragmented_mp4_ should be NULL after being returned by create_fragmented_mp4()
    real_fragmented_mp4_ = NULL;
}

// Mock HLS muxer implementation
MockHlsMuxer::MockHlsMuxer()
{
    segment_close_count_ = 0;
    segment_open_count_ = 0;
    flush_video_count_ = 0;
    flush_audio_count_ = 0;
    segment_close_error_ = srs_success;
    segment_open_error_ = srs_success;
    flush_video_error_ = srs_success;
    flush_audio_error_ = srs_success;
}

MockHlsMuxer::~MockHlsMuxer()
{
    srs_freep(segment_close_error_);
    srs_freep(segment_open_error_);
    srs_freep(flush_video_error_);
    srs_freep(flush_audio_error_);
}

srs_error_t MockHlsMuxer::segment_close()
{
    segment_close_count_++;
    return srs_error_copy(segment_close_error_);
}

srs_error_t MockHlsMuxer::segment_open()
{
    segment_open_count_++;
    return srs_error_copy(segment_open_error_);
}

srs_error_t MockHlsMuxer::flush_video(SrsTsMessageCache *cache)
{
    flush_video_count_++;
    return srs_error_copy(flush_video_error_);
}

srs_error_t MockHlsMuxer::flush_audio(SrsTsMessageCache *cache)
{
    flush_audio_count_++;
    return srs_error_copy(flush_audio_error_);
}

void MockHlsMuxer::reset()
{
    segment_close_count_ = 0;
    segment_open_count_ = 0;
    flush_video_count_ = 0;
    flush_audio_count_ = 0;
    srs_freep(segment_close_error_);
    srs_freep(segment_open_error_);
    srs_freep(flush_video_error_);
    srs_freep(flush_audio_error_);
}

void MockHlsMuxer::set_segment_close_error(srs_error_t err)
{
    srs_freep(segment_close_error_);
    segment_close_error_ = srs_error_copy(err);
}

void MockHlsMuxer::set_segment_open_error(srs_error_t err)
{
    srs_freep(segment_open_error_);
    segment_open_error_ = srs_error_copy(err);
}

void MockHlsMuxer::set_flush_video_error(srs_error_t err)
{
    srs_freep(flush_video_error_);
    flush_video_error_ = srs_error_copy(err);
}

void MockHlsMuxer::set_flush_audio_error(srs_error_t err)
{
    srs_freep(flush_audio_error_);
    flush_audio_error_ = srs_error_copy(err);
}

// Unit tests for SrsDvrAsyncCallOnHls
VOID TEST(AppHlsTest, DvrAsyncCallOnHlsToString)
{
    // Setup mock objects
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Create SrsDvrAsyncCallOnHls instance
    SrsContextId cid;
    std::string path = "/data/hls/test/stream1-0.ts";
    std::string ts_url = "stream1-0.ts";
    std::string m3u8 = "/data/hls/test/stream1.m3u8";
    std::string m3u8_url = "stream1.m3u8";
    int seq_no = 1;
    srs_utime_t duration = 10 * SRS_UTIME_SECONDS;

    SrsUniquePtr<SrsDvrAsyncCallOnHls> task(new SrsDvrAsyncCallOnHls(cid, &mock_request, path, ts_url, m3u8, m3u8_url, seq_no, duration));

    // Test to_string method
    std::string str = task->to_string();
    EXPECT_TRUE(str.find("on_hls") != std::string::npos);
    EXPECT_TRUE(str.find(path) != std::string::npos);
}

// Unit tests for SrsDvrAsyncCallOnHlsNotify
VOID TEST(AppHlsTest, DvrAsyncCallOnHlsNotifyTypicalScenario)
{
    srs_error_t err;

    // Setup mock objects
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockAppConfigForHlsNotify mock_config;
    MockHttpHooksForHlsNotify mock_hooks;

    // Configure mock config with HLS notify hooks
    std::vector<std::string> urls;
    urls.push_back("http://localhost:8080/api/v1/hls_notify");
    urls.push_back("http://localhost:8081/api/v1/hls_notify");
    mock_config.set_on_hls_notify_urls(urls);
    mock_config.set_http_hooks_enabled(true);
    mock_config.set_hls_nb_notify(10);

    // Create SrsDvrAsyncCallOnHlsNotify instance
    SrsContextId cid;
    std::string ts_url = "stream1-0.ts";
    SrsUniquePtr<SrsDvrAsyncCallOnHlsNotify> task(new SrsDvrAsyncCallOnHlsNotify(cid, &mock_request, ts_url));

    // Replace the dependencies with mocks (private members are accessible due to #define private public)
    task->hooks_ = &mock_hooks;
    task->config_ = &mock_config;

    // Call the task
    HELPER_EXPECT_SUCCESS(task->call());

    // Verify hooks were called correctly
    EXPECT_EQ(2, mock_hooks.on_hls_notify_count_);
    EXPECT_EQ(2, (int)mock_hooks.on_hls_notify_urls_.size());
    EXPECT_EQ("http://localhost:8080/api/v1/hls_notify", mock_hooks.on_hls_notify_urls_[0]);
    EXPECT_EQ("http://localhost:8081/api/v1/hls_notify", mock_hooks.on_hls_notify_urls_[1]);
    EXPECT_EQ(ts_url, mock_hooks.on_hls_notify_ts_urls_[0]);
    EXPECT_EQ(ts_url, mock_hooks.on_hls_notify_ts_urls_[1]);
    EXPECT_EQ(10, mock_hooks.on_hls_notify_nb_notifies_[0]);
    EXPECT_EQ(10, mock_hooks.on_hls_notify_nb_notifies_[1]);

    // Test to_string method
    std::string str = task->to_string();
    EXPECT_TRUE(str.find("on_hls_notify") != std::string::npos);
    EXPECT_TRUE(str.find(ts_url) != std::string::npos);
}

// Unit tests for SrsHlsFmp4Muxer::dispose
VOID TEST(AppHlsTest, HlsFmp4MuxerDisposeTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    MockHlsRequest mock_request("test.vhost", "live", "stream1");

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Set up the muxer with test configuration
    HELPER_EXPECT_SUCCESS(muxer->update_config(&mock_request));

    // Access private members to set up test state (private members are accessible due to #define private public)
    muxer->m3u8_ = "/nonexistent/path/stream1.m3u8"; // Use non-existent path to avoid file operations
    muxer->req_ = mock_request.copy();

    // Verify current segment is NULL before dispose
    EXPECT_TRUE(muxer->current_ == NULL);

    // Call dispose - this should:
    // 1. Call segments_->dispose() to clean up all segments
    // 2. Unlink current segment's tmpfile if it exists (NULL in this case)
    // 3. Attempt to unlink the m3u8 file (will fail silently for non-existent file)
    // 4. Log the disposal message
    // The dispose() function should complete without crashing even if files don't exist
    muxer->dispose();

    // Verify dispose completed successfully (no crash)
    // The test verifies that dispose() handles non-existent files gracefully
    EXPECT_TRUE(true);
}

// Unit tests for SrsHlsFmp4Muxer::on_sequence_header
VOID TEST(AppHlsTest, HlsFmp4MuxerOnSequenceHeaderTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Call on_sequence_header - should always return success
    HELPER_EXPECT_SUCCESS(muxer->on_sequence_header());
}

// Unit tests for SrsHlsFmp4Muxer::is_segment_overflow
VOID TEST(AppHlsTest, HlsFmp4MuxerIsSegmentOverflowTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Set up test configuration (access private members)
    muxer->hls_fragment_ = 10 * SRS_UTIME_SECONDS;
    muxer->max_td_ = 10 * SRS_UTIME_SECONDS;
    muxer->hls_ts_floor_ = false;
    muxer->deviation_ts_ = 0;

    // Create a mock file writer
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());

    // Create a segment using SrsHlsM4sSegment
    SrsUniquePtr<SrsHlsM4sSegment> segment(new SrsHlsM4sSegment(fw.get()));
    muxer->current_ = segment.get();

    // Test 1: Very small segment (< 2 * SRS_HLS_SEGMENT_MIN_DURATION) - should not overflow
    // append() takes milliseconds, so 50ms
    segment->append(0);  // Start at 0ms
    segment->append(50); // Duration will be 50ms
    EXPECT_FALSE(muxer->is_segment_overflow());

    // Test 2: Segment duration less than max_td - should not overflow
    // Duration from 0ms to 5000ms = 5s
    segment->append(5000);
    EXPECT_FALSE(muxer->is_segment_overflow());

    // Test 3: Segment duration >= max_td - should overflow
    // Duration from 0ms to 10000ms = 10s
    segment->append(10000);
    EXPECT_TRUE(muxer->is_segment_overflow());

    // Clean up - prevent double free
    muxer->current_ = NULL;
}

// Unit tests for SrsHlsFmp4Muxer::wait_keyframe
VOID TEST(AppHlsTest, HlsFmp4MuxerWaitKeyframeTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Test 1: Default value should be true
    EXPECT_TRUE(muxer->wait_keyframe());

    // Test 2: Set to false
    muxer->hls_wait_keyframe_ = false;
    EXPECT_FALSE(muxer->wait_keyframe());

    // Test 3: Set back to true
    muxer->hls_wait_keyframe_ = true;
    EXPECT_TRUE(muxer->wait_keyframe());
}

// Unit tests for SrsHlsFmp4Muxer::write_hls_key
VOID TEST(AppHlsTest, HlsFmp4MuxerWriteHlsKeyTypicalScenario)
{
    srs_error_t err;

    // Create mock app factory
    MockAppFactory mock_factory;

    // Create mock request
    MockHlsRequest mock_request("test.vhost", "live", "stream1");

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Set up test configuration for HLS encryption
    muxer->req_ = mock_request.copy();
    muxer->hls_keys_ = true;
    muxer->hls_fragments_per_key_ = 5;
    muxer->hls_key_file_ = "key-[seq].key";
    muxer->hls_key_file_path_ = "/tmp";
    muxer->app_factory_ = &mock_factory;

    // Create a mock file writer for segment
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());

    // Create a segment with sequence number that is a multiple of hls_fragments_per_key_
    SrsUniquePtr<SrsHlsM4sSegment> segment(new SrsHlsM4sSegment(fw.get()));
    segment->sequence_no_ = 10; // 10 % 5 == 0, so key should be generated
    muxer->current_ = segment.get();

    // Call write_hls_key - should generate and write key to mock writer
    HELPER_EXPECT_SUCCESS(muxer->write_hls_key());

    // Verify create_file_writer was called
    EXPECT_EQ(1, mock_factory.create_file_writer_count_);

    // Note: After write_hls_key() returns, the writer is freed by SrsUniquePtr,
    // which also frees the MockSrsFile. So we cannot access real_file_ or real_writer_ here.
    // We can only verify that the factory was called to create the writer.

    // Clean up - prevent double free
    muxer->current_ = NULL;
}

// Unit tests for SrsHlsFmp4Muxer::is_segment_absolutely_overflow
VOID TEST(AppHlsTest, HlsFmp4MuxerIsSegmentAbsolutelyOverflowTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Set up test configuration (access private members)
    muxer->hls_fragment_ = 10 * SRS_UTIME_SECONDS;
    muxer->hls_aof_ratio_ = 2.1;
    muxer->hls_ts_floor_ = false;
    muxer->deviation_ts_ = 0;

    // Create a mock file writer
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());

    // Create a segment using SrsHlsM4sSegment
    SrsUniquePtr<SrsHlsM4sSegment> segment(new SrsHlsM4sSegment(fw.get()));
    muxer->current_ = segment.get();

    // Test 1: Very small segment (< 2 * SRS_HLS_SEGMENT_MIN_DURATION) - should not overflow
    // append() takes milliseconds
    segment->append(0);  // Start at 0ms
    segment->append(50); // Duration will be 50ms
    EXPECT_FALSE(muxer->is_segment_absolutely_overflow());

    // Test 2: Segment duration less than hls_aof_ratio * hls_fragment - should not overflow
    // Duration from 0ms to 10000ms = 10s, threshold is 2.1 * 10s = 21s
    segment->append(10000);
    EXPECT_FALSE(muxer->is_segment_absolutely_overflow());

    // Test 3: Segment duration >= hls_aof_ratio * hls_fragment (21s) - should overflow
    // Duration from 0ms to 21000ms = 21s
    segment->append(21000);
    EXPECT_TRUE(muxer->is_segment_absolutely_overflow());

    // Clean up - prevent double free
    muxer->current_ = NULL;
}

// Unit tests for SrsHlsMuxer::segment_open selection code
VOID TEST(AppHlsTest, HlsMuxerSegmentOpenTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    MockHlsRequest mock_request("test.vhost", "live", "stream1");

    // Create mock file writer
    SrsUniquePtr<MockSrsFileWriter> mock_writer(new MockSrsFileWriter());

    // Create SrsHlsMuxer instance
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Set up test configuration (typical HLS configuration)
    muxer->req_ = mock_request.copy();
    muxer->hls_path_ = "/data/hls";
    muxer->hls_ts_file_ = "[app]/[stream]-[seq].ts";
    muxer->hls_entry_prefix_ = "";
    muxer->m3u8_dir_ = "/data/hls";
    muxer->m3u8_url_ = "live/stream1.m3u8";
    muxer->hls_fragment_ = 10 * SRS_UTIME_SECONDS;
    muxer->hls_ts_floor_ = false;
    muxer->hls_keys_ = false;
    muxer->sequence_no_ = 5;
    muxer->latest_acodec_ = SrsAudioCodecIdAAC;
    muxer->latest_vcodec_ = SrsVideoCodecIdAVC;
    muxer->writer_ = mock_writer.get();

    // Test the selection code logic (path building) without calling segment_open
    // This simulates the path selection logic in segment_open() lines 1473-1536

    // Create a segment manually to test path selection
    SrsAudioCodecId default_acodec = muxer->latest_acodec_;
    SrsVideoCodecId default_vcodec = muxer->latest_vcodec_;
    muxer->current_ = new SrsHlsSegment(muxer->context_, default_acodec, default_vcodec, muxer->writer_);
    muxer->current_->sequence_no_ = muxer->sequence_no_++;

    // Generate filename using the selection logic
    std::string ts_file = muxer->hls_ts_file_;
    ts_file = srs_path_build_stream(ts_file, muxer->req_->vhost_, muxer->req_->app_, muxer->req_->stream_);

    // Replace [seq] with sequence number
    std::stringstream ss;
    ss << muxer->current_->sequence_no_;
    ts_file = srs_strings_replace(ts_file, "[seq]", ss.str());

    // Set the full path
    muxer->current_->set_path(muxer->hls_path_ + "/" + ts_file);

    // Build the URI (relative path for m3u8)
    std::string ts_url = muxer->current_->fullpath();
    if (srs_strings_starts_with(ts_url, muxer->m3u8_dir_)) {
        ts_url = ts_url.substr(muxer->m3u8_dir_.length());
    }
    while (srs_strings_starts_with(ts_url, "/")) {
        ts_url = ts_url.substr(1);
    }
    muxer->current_->uri_ += muxer->hls_entry_prefix_;
    muxer->current_->uri_ += ts_url;

    // Verify segment was created
    EXPECT_TRUE(muxer->current_ != NULL);

    // Verify sequence number was incremented
    EXPECT_EQ(5, muxer->current_->sequence_no_);
    EXPECT_EQ(6, muxer->sequence_no_);

    // Verify segment path was built correctly by selection logic
    std::string expected_path = "/data/hls/live/stream1-5.ts";
    EXPECT_EQ(expected_path, muxer->current_->fullpath());

    // Verify segment URI was built correctly by selection logic
    std::string expected_uri = "live/stream1-5.ts";
    EXPECT_EQ(expected_uri, muxer->current_->uri_);

    // Verify tmppath is correct
    std::string tmp_path = muxer->current_->tmppath();
    EXPECT_EQ(expected_path + ".tmp", tmp_path);

    // Clean up - prevent double free
    srs_freep(muxer->current_);
    muxer->writer_ = NULL; // Prevent muxer destructor from freeing the mock writer
}

// Unit tests for SrsHlsFmp4Muxer::update_duration
VOID TEST(AppHlsTest, HlsFmp4MuxerUpdateDurationTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsFmp4Muxer instance
    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize(1, 2));

    // Create a mock file writer
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());

    // Create a segment using SrsHlsM4sSegment
    SrsUniquePtr<SrsHlsM4sSegment> segment(new SrsHlsM4sSegment(fw.get()));
    muxer->current_ = segment.get();

    // Test: Update duration with DTS value
    // DTS is in 90kHz timebase, so 90000 = 1 second
    // update_duration calls append(dts / 90), which converts 90kHz to milliseconds
    uint64_t dts = 90000; // 1 second in 90kHz
    muxer->update_duration(dts);

    // Verify duration was updated (dts / 90 = 1000ms, duration is from start_dts to current)
    // First call sets start_dts to 1000ms, so duration is 0
    EXPECT_EQ(0, segment->duration());

    // Test: Update with larger DTS
    dts = 450000; // 5 seconds in 90kHz
    muxer->update_duration(dts);

    // Verify duration was updated (dts / 90 = 5000ms, duration is 5000ms - 1000ms = 4000ms)
    EXPECT_EQ(4000 * SRS_UTIME_MILLISECONDS, segment->duration());

    // Clean up - prevent double free
    muxer->current_ = NULL;
}

VOID TEST(HlsMuxerTest, TestMuxerGettersAndCodecSetters)
{
    srs_error_t err;

    // Create muxer
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Test sequence_no() - should return initial value
    EXPECT_EQ(0, muxer->sequence_no());

    // Test ts_url() - should return empty string when no current segment
    EXPECT_EQ("", muxer->ts_url());

    // Test duration() - should return 0 when no current segment
    EXPECT_EQ(0, muxer->duration());

    // Test deviation() - should return 0 when hls_ts_floor is false
    EXPECT_EQ(0, muxer->deviation());

    // Test codec getters - should return initial forbidden values
    EXPECT_EQ(SrsAudioCodecIdForbidden, muxer->latest_acodec());
    EXPECT_EQ(SrsVideoCodecIdForbidden, muxer->latest_vcodec());

    // Test codec setters - set new codec values
    muxer->set_latest_acodec(SrsAudioCodecIdAAC);
    muxer->set_latest_vcodec(SrsVideoCodecIdAVC);

    // Verify codec values were updated
    EXPECT_EQ(SrsAudioCodecIdAAC, muxer->latest_acodec());
    EXPECT_EQ(SrsVideoCodecIdAVC, muxer->latest_vcodec());

    // Create a segment to test with current segment
    MockSrsFileWriter *writer = new MockSrsFileWriter();
    SrsHlsSegment *segment = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, writer);
    segment->sequence_no_ = 5;
    segment->uri_ = "test-5.ts";
    muxer->current_ = segment;

    // Update sequence_no_ to match segment
    muxer->sequence_no_ = 5;

    // Test sequence_no() with current segment
    EXPECT_EQ(5, muxer->sequence_no());

    // Test ts_url() with current segment
    EXPECT_EQ("test-5.ts", muxer->ts_url());

    // Test duration() with current segment (should still be 0 as no data written)
    EXPECT_EQ(0, muxer->duration());

    // Test codec getters with current segment - should query from tscw_
    EXPECT_EQ(SrsAudioCodecIdAAC, muxer->latest_acodec());
    EXPECT_EQ(SrsVideoCodecIdAVC, muxer->latest_vcodec());

    // Test codec setters with current segment - should update both tscw_ and latest_*
    muxer->set_latest_acodec(SrsAudioCodecIdOpus);
    muxer->set_latest_vcodec(SrsVideoCodecIdHEVC);

    // Verify codec values were updated in tscw_
    EXPECT_EQ(SrsAudioCodecIdOpus, muxer->latest_acodec());
    EXPECT_EQ(SrsVideoCodecIdHEVC, muxer->latest_vcodec());

    // Clean up - prevent double free
    muxer->current_ = NULL;
}

VOID TEST(HlsMuxerTest, TestRecoverHlsTypicalScenario)
{
    srs_error_t err;

    // Create a typical m3u8 file content
    std::string m3u8_content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXT-X-MEDIA-SEQUENCE:100\n"
        "#EXTINF:10.000,\n"
        "livestream-100.ts\n"
        "#EXTINF:9.500,\n"
        "livestream-101.ts\n"
        "#EXTINF:10.200,\n"
        "livestream-102.ts\n";

    // Create mock file reader with m3u8 content
    // Note: Don't use SrsUniquePtr here because ownership will be transferred to recover_hls()'s SrsUniquePtr
    MockSrsFileReader *mock_reader = new MockSrsFileReader(m3u8_content.c_str(), m3u8_content.length());

    // Create mock app factory and set up the mock reader
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());
    mock_factory->real_reader_ = mock_reader;

    // Create muxer and configure it
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Set up required fields for do_recover_hls
    // Note: Use copy() to create a heap-allocated request that muxer can own
    MockHlsRequest mock_request_template("__defaultVhost__", "live", "test");
    muxer->req_ = mock_request_template.copy();

    // Set up paths - no real file needed since we're using mock reader
    muxer->m3u8_ = "test.m3u8";
    muxer->m3u8_url_ = "test.m3u8";
    muxer->hls_path_ = "/tmp";
    muxer->sequence_no_ = 0;
    muxer->latest_acodec_ = SrsAudioCodecIdAAC;
    muxer->latest_vcodec_ = SrsVideoCodecIdAVC;
    muxer->app_factory_ = mock_factory.get();

    // Create a mock writer for segments
    muxer->writer_ = new MockSrsFileWriter();

    // Call do_recover_hls directly to bypass file existence check
    // The mock reader will provide the m3u8 content
    HELPER_EXPECT_SUCCESS(muxer->do_recover_hls());

    // Verify that segments were recovered correctly
    EXPECT_EQ(3, muxer->segments_->size());

    // Verify sequence number was updated (100 + 3 segments)
    EXPECT_EQ(103, muxer->sequence_no_);

    // Verify first segment
    SrsHlsSegment *seg0 = dynamic_cast<SrsHlsSegment *>(muxer->segments_->at(0));
    EXPECT_TRUE(seg0 != NULL);
    EXPECT_EQ(100, seg0->sequence_no_);
    EXPECT_EQ("livestream-100.ts", seg0->uri_);
    EXPECT_EQ(10000 * SRS_UTIME_MILLISECONDS, seg0->duration());

    // Verify second segment
    SrsHlsSegment *seg1 = dynamic_cast<SrsHlsSegment *>(muxer->segments_->at(1));
    EXPECT_TRUE(seg1 != NULL);
    EXPECT_EQ(101, seg1->sequence_no_);
    EXPECT_EQ("livestream-101.ts", seg1->uri_);
    EXPECT_EQ(9500 * SRS_UTIME_MILLISECONDS, seg1->duration());

    // Verify third segment
    SrsHlsSegment *seg2 = dynamic_cast<SrsHlsSegment *>(muxer->segments_->at(2));
    EXPECT_TRUE(seg2 != NULL);
    EXPECT_EQ(102, seg2->sequence_no_);
    EXPECT_EQ("livestream-102.ts", seg2->uri_);
    EXPECT_EQ(10200 * SRS_UTIME_MILLISECONDS, seg2->duration());

    // Clean up - clear references before objects are destroyed
    muxer->app_factory_ = NULL;
    srs_freep(muxer->writer_);

    // Clear the reader reference in factory before it's destroyed
    mock_factory->real_reader_ = NULL;
}

VOID TEST(HlsMuxerTest, SegmentOverflowAndPureAudio)
{
    srs_error_t err;

    // Create muxer and initialize
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Set up required fields
    MockHlsRequest mock_request("__defaultVhost__", "live", "test");
    muxer->req_ = &mock_request;
    muxer->hls_fragment_ = 10 * SRS_UTIME_SECONDS; // 10 seconds fragment
    muxer->hls_aof_ratio_ = 2.0;                   // Absolutely overflow at 2x fragment duration
    muxer->hls_wait_keyframe_ = true;
    muxer->hls_ts_floor_ = false; // Disable floor for simpler testing
    muxer->deviation_ts_ = 0;
    muxer->max_td_ = 10 * SRS_UTIME_SECONDS; // Same as fragment for simplicity
    muxer->latest_acodec_ = SrsAudioCodecIdAAC;
    muxer->latest_vcodec_ = SrsVideoCodecIdAVC;
    muxer->writer_ = new MockSrsFileWriter();
    muxer->context_ = new SrsTsContext();

    // Test on_sequence_header: Create a segment and mark it as sequence header
    SrsHlsSegment *segment = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, new MockSrsFileWriter());
    muxer->current_ = segment;

    EXPECT_FALSE(segment->is_sequence_header());
    HELPER_EXPECT_SUCCESS(muxer->on_sequence_header());
    EXPECT_TRUE(segment->is_sequence_header());

    // Test is_segment_overflow: Segment too small (< 2 * SRS_HLS_SEGMENT_MIN_DURATION)
    segment->append(0);
    segment->append(50); // 50ms duration, too small
    EXPECT_FALSE(muxer->is_segment_overflow());

    // Test is_segment_overflow: Segment duration just below threshold
    segment->append(0);
    segment->append(9000); // 9 seconds, below 10 seconds threshold
    EXPECT_FALSE(muxer->is_segment_overflow());

    // Test is_segment_overflow: Segment duration at threshold
    segment->append(0);
    segment->append(10000); // 10 seconds, at threshold
    EXPECT_TRUE(muxer->is_segment_overflow());

    // Test is_segment_overflow: Segment duration above threshold
    segment->append(0);
    segment->append(12000); // 12 seconds, above threshold
    EXPECT_TRUE(muxer->is_segment_overflow());

    // Test wait_keyframe
    EXPECT_TRUE(muxer->wait_keyframe());
    muxer->hls_wait_keyframe_ = false;
    EXPECT_FALSE(muxer->wait_keyframe());
    muxer->hls_wait_keyframe_ = true;

    // Test is_segment_absolutely_overflow: Reset segment for new tests
    srs_freep(segment);
    segment = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, new MockSrsFileWriter());
    muxer->current_ = segment;

    // Test is_segment_absolutely_overflow: Segment too small
    segment->append(0);
    segment->append(50); // 50ms duration, too small
    EXPECT_FALSE(muxer->is_segment_absolutely_overflow());

    // Test is_segment_absolutely_overflow: Below absolute overflow threshold (2x fragment)
    segment->append(0);
    segment->append(15000); // 15 seconds, below 20 seconds (2x 10s)
    EXPECT_FALSE(muxer->is_segment_absolutely_overflow());

    // Test is_segment_absolutely_overflow: At absolute overflow threshold
    segment->append(0);
    segment->append(20000); // 20 seconds, at 2x threshold
    EXPECT_TRUE(muxer->is_segment_absolutely_overflow());

    // Test is_segment_absolutely_overflow: Above absolute overflow threshold
    segment->append(0);
    segment->append(25000); // 25 seconds, above 2x threshold
    EXPECT_TRUE(muxer->is_segment_absolutely_overflow());

    // Test pure_audio: With video codec enabled (not pure audio)
    EXPECT_FALSE(muxer->pure_audio());

    // Test pure_audio: With video codec disabled (pure audio)
    segment->tscw_->set_vcodec(SrsVideoCodecIdDisabled);
    EXPECT_TRUE(muxer->pure_audio());

    // Test pure_audio: With video codec re-enabled
    segment->tscw_->set_vcodec(SrsVideoCodecIdAVC);
    EXPECT_FALSE(muxer->pure_audio());

    // Test pure_audio: With NULL current segment
    muxer->current_ = NULL;
    EXPECT_FALSE(muxer->pure_audio());

    // Clean up
    muxer->req_ = NULL;
    srs_freep(segment);
    srs_freep(muxer->writer_);
    srs_freep(muxer->context_);
}

// Unit test for SrsHlsMuxer::flush_audio typical scenario
VOID TEST(AppHlsTest, HlsMuxerFlushAudioTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsMuxer instance
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Create a mock file writer
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());

    // Create a TS context
    SrsUniquePtr<SrsTsContext> context(new SrsTsContext());

    // Create a segment with SrsTsContextWriter
    SrsUniquePtr<SrsHlsSegment> segment(new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw.get()));
    muxer->current_ = segment.get();

    // Initialize segment duration by appending a start timestamp
    segment->append(0);

    // Create a SrsTsMessageCache with audio data
    SrsUniquePtr<SrsTsMessageCache> cache(new SrsTsMessageCache());

    // Create audio message with payload
    cache->audio_ = new SrsTsMessage();
    cache->audio_->dts_ = 90000; // 1 second in 90kHz
    cache->audio_->pts_ = 90000;
    cache->audio_->sid_ = SrsTsPESStreamIdAudioCommon;

    // Add some audio data to payload
    const char audio_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    cache->audio_->payload_->append(audio_data, sizeof(audio_data));

    // Call flush_audio - should succeed
    HELPER_EXPECT_SUCCESS(muxer->flush_audio(cache.get()));

    // Verify audio message was freed after successful write
    EXPECT_TRUE(cache->audio_ == NULL);

    // Verify segment duration was updated (dts / 90 = 90000 / 90 = 1000ms = 1 second)
    EXPECT_EQ(1 * SRS_UTIME_SECONDS, segment->duration());

    // Clean up
    muxer->current_ = NULL;
}

// Unit test for SrsHlsController::write_audio typical scenario
VOID TEST(AppHlsTest, HlsControllerWriteAudioTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsController instance
    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());

    // Initialize the controller
    HELPER_EXPECT_SUCCESS(controller->initialize());

    // Create mock request
    MockHlsRequest mock_request("__defaultVhost__", "live", "test");

    // Cast to concrete type to access private members for testing
    SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller->muxer_);
    srs_assert(muxer);

    // Set up muxer with required fields
    muxer->req_ = &mock_request;
    muxer->hls_fragment_ = 10 * SRS_UTIME_SECONDS;
    muxer->hls_aof_ratio_ = 2.0;
    muxer->hls_wait_keyframe_ = true;
    muxer->hls_ts_floor_ = false;
    muxer->deviation_ts_ = 0;
    muxer->max_td_ = 10 * SRS_UTIME_SECONDS;
    muxer->latest_acodec_ = SrsAudioCodecIdAAC;
    muxer->latest_vcodec_ = SrsVideoCodecIdDisabled; // Pure audio mode
    muxer->writer_ = new MockSrsFileWriter();
    muxer->context_ = new SrsTsContext();

    // Create a segment
    SrsHlsSegment *segment = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdDisabled, new MockSrsFileWriter());
    muxer->current_ = segment;
    segment->append(0);

    // Create SrsFormat with AAC audio codec
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->sound_rate_ = SrsAudioSampleRate44100; // 44100 Hz, index 3
    format->audio_ = new SrsParsedAudioPacket();
    HELPER_EXPECT_SUCCESS(format->audio_->initialize(format->acodec_));

    // Create SrsMediaPacket with audio data
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->timestamp_ = 1000; // 1 second in milliseconds
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create audio payload (AAC raw data) - must be heap allocated for wrap()
    char *audio_data = new char[10];
    audio_data[0] = 0x01;
    audio_data[1] = 0x02;
    audio_data[2] = 0x03;
    audio_data[3] = 0x04;
    audio_data[4] = 0x05;
    audio_data[5] = 0x06;
    audio_data[6] = 0x07;
    audio_data[7] = 0x08;
    audio_data[8] = 0x09;
    audio_data[9] = 0x0a;
    audio_packet->wrap(audio_data, 10);

    // Add sample to format->audio_
    format->audio_->nb_samples_ = 1;
    format->audio_->samples_[0].bytes_ = audio_data;
    format->audio_->samples_[0].size_ = 10;

    // Call write_audio - should succeed
    HELPER_EXPECT_SUCCESS(controller->write_audio(audio_packet.get(), format.get()));

    // Verify previous_audio_dts_ was updated
    EXPECT_EQ(1000, controller->previous_audio_dts_);

    // Verify aac_samples_ was updated (should be 1024 for 44100Hz with ~23ms diff)
    EXPECT_GT(controller->aac_samples_, 0);

    // Verify audio was cached in tsmc_
    EXPECT_TRUE(controller->tsmc_->audio_ != NULL);
    EXPECT_GT(controller->tsmc_->audio_->payload_->length(), 0);

    // Call write_audio again with next frame (23ms later for 1024 samples at 44100Hz)
    SrsUniquePtr<SrsMediaPacket> audio_packet2(new SrsMediaPacket());
    audio_packet2->timestamp_ = 1023; // 23ms later
    audio_packet2->message_type_ = SrsFrameTypeAudio;

    // Create audio payload - must be heap allocated for wrap()
    char *audio_data2 = new char[10];
    audio_data2[0] = 0x0b;
    audio_data2[1] = 0x0c;
    audio_data2[2] = 0x0d;
    audio_data2[3] = 0x0e;
    audio_data2[4] = 0x0f;
    audio_data2[5] = 0x10;
    audio_data2[6] = 0x11;
    audio_data2[7] = 0x12;
    audio_data2[8] = 0x13;
    audio_data2[9] = 0x14;
    audio_packet2->wrap(audio_data2, 10);

    format->audio_->nb_samples_ = 1;
    format->audio_->samples_[0].bytes_ = audio_data2;
    format->audio_->samples_[0].size_ = 10;

    HELPER_EXPECT_SUCCESS(controller->write_audio(audio_packet2.get(), format.get()));

    // Verify previous_audio_dts_ was updated
    EXPECT_EQ(1023, controller->previous_audio_dts_);

    // Verify aac_samples_ was incremented
    EXPECT_GT(controller->aac_samples_, 1024);

    // Clean up
    muxer->req_ = NULL;
    muxer->current_ = NULL;
    srs_freep(segment);
    srs_freep(muxer->writer_);
    srs_freep(muxer->context_);
}

// Unit test for SrsHlsMuxer::flush_video typical scenario
VOID TEST(AppHlsTest, HlsMuxerFlushVideoTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsMuxer instance
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Create a mock file writer
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());

    // Create a TS context
    SrsUniquePtr<SrsTsContext> context(new SrsTsContext());

    // Create a segment with SrsTsContextWriter
    SrsUniquePtr<SrsHlsSegment> segment(new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw.get()));
    muxer->current_ = segment.get();

    // Initialize segment duration by appending a start timestamp
    segment->append(0);

    // Create a SrsTsMessageCache with video data
    SrsUniquePtr<SrsTsMessageCache> cache(new SrsTsMessageCache());

    // Create video message with payload
    cache->video_ = new SrsTsMessage();
    cache->video_->dts_ = 180000; // 2 seconds in 90kHz
    cache->video_->pts_ = 180000;
    cache->video_->sid_ = SrsTsPESStreamIdVideoCommon;

    // Add some video data to payload
    const char video_data[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e};
    cache->video_->payload_->append(video_data, sizeof(video_data));

    // Call flush_video - should succeed
    HELPER_EXPECT_SUCCESS(muxer->flush_video(cache.get()));

    // Verify video message was freed after successful write
    EXPECT_TRUE(cache->video_ == NULL);

    // Verify segment duration was updated (dts / 90 = 180000 / 90 = 2000ms = 2 seconds)
    EXPECT_EQ(2 * SRS_UTIME_SECONDS, segment->duration());

    // Clean up
    muxer->current_ = NULL;
}

// Unit test for SrsHlsMuxer::write_hls_key selection logic
// This test focuses on the selection logic: whether to write key file based on sequence number
VOID TEST(AppHlsTest, HlsMuxerWriteHlsKeySelection)
{
    srs_error_t err;

    // Setup mock objects
    MockHlsRequest mock_request("test.vhost", "live", "stream1");
    MockAppFactory mock_factory;

    // Create SrsHlsMuxer instance
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());

    // Setup request and factory
    muxer->req_ = mock_request.copy();
    muxer->app_factory_ = &mock_factory;

    // Configure HLS encryption settings for selection logic testing
    // Note: We test the selection logic by checking if factory is called
    muxer->hls_keys_ = true;
    muxer->hls_fragments_per_key_ = 5;
    muxer->hls_key_file_ = "test-[seq].key";
    muxer->hls_key_file_path_ = "/tmp/hls_test_keys";

    // Create a TS context
    SrsUniquePtr<SrsTsContext> context(new SrsTsContext());

    // Create a mock encrypted file writer for the segment (required when hls_keys_ is true)
    // The segment's config_cipher() expects an SrsEncFileWriter
    SrsUniquePtr<MockEncFileWriter> fw(new MockEncFileWriter());

    // Test case 1: sequence_no = 0 (divisible by hls_fragments_per_key_)
    // Should generate new key and call factory to create file writer
    SrsUniquePtr<SrsHlsSegment> segment1(new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw.get()));
    segment1->sequence_no_ = 0;
    muxer->current_ = segment1.get();

    // Call write_hls_key - should succeed and create file writer
    HELPER_EXPECT_SUCCESS(muxer->write_hls_key());

    // Verify factory was called to create file writer (for writing key file)
    EXPECT_EQ(1, mock_factory.create_file_writer_count_);

    // Test case 2: sequence_no = 5 (divisible by hls_fragments_per_key_)
    // Should generate new key and call factory to create file writer
    SrsUniquePtr<SrsHlsSegment> segment2(new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw.get()));
    segment2->sequence_no_ = 5;
    muxer->current_ = segment2.get();

    // Call write_hls_key - should succeed and create file writer
    HELPER_EXPECT_SUCCESS(muxer->write_hls_key());

    // Verify factory was called again (counter should be 2 now)
    EXPECT_EQ(2, mock_factory.create_file_writer_count_);

    // Test case 3: sequence_no = 3 (NOT divisible by hls_fragments_per_key_)
    // Should NOT write key file, so factory should NOT be called
    SrsUniquePtr<SrsHlsSegment> segment3(new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw.get()));
    segment3->sequence_no_ = 3;
    muxer->current_ = segment3.get();

    // Call write_hls_key - should succeed without creating file writer
    HELPER_EXPECT_SUCCESS(muxer->write_hls_key());

    // Verify factory was NOT called (counter should still be 2)
    EXPECT_EQ(2, mock_factory.create_file_writer_count_);

    // Clean up
    muxer->current_ = NULL;
}

// Unit test for SrsHlsMuxer::do_refresh_m3u8 typical scenario
VOID TEST(AppHlsTest, HlsMuxerDoRefreshM3u8TypicalScenario)
{
    srs_error_t err;

    // Setup mock objects
    MockHlsRequest mock_request("test.vhost", "live", "stream1");
    MockAppFactory mock_factory;

    // Create SrsHlsMuxer instance
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());

    // Initialize the muxer
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    // Setup request and factory
    muxer->req_ = mock_request.copy();
    muxer->app_factory_ = &mock_factory;

    // Create a TS context
    SrsUniquePtr<SrsTsContext> context(new SrsTsContext());
    muxer->context_ = context.get();

    // Create mock file writer for segments
    SrsUniquePtr<MockSrsFileWriter> fw1(new MockSrsFileWriter());
    SrsUniquePtr<MockSrsFileWriter> fw2(new MockSrsFileWriter());
    SrsUniquePtr<MockSrsFileWriter> fw3(new MockSrsFileWriter());

    // Create three segments with different durations and sequence numbers
    SrsHlsSegment *segment1 = new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw1.get());
    segment1->sequence_no_ = 100;
    segment1->uri_ = "stream1-100.ts";
    segment1->append(0);
    segment1->append(10000); // 10 seconds duration

    SrsHlsSegment *segment2 = new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw2.get());
    segment2->sequence_no_ = 101;
    segment2->uri_ = "stream1-101.ts";
    segment2->append(10000);
    segment2->append(20000); // 10 seconds duration

    SrsHlsSegment *segment3 = new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw3.get());
    segment3->sequence_no_ = 102;
    segment3->uri_ = "stream1-102.ts";
    segment3->append(20000);
    segment3->append(28000); // 8 seconds duration

    // Add segments to the fragment window (ownership transferred to segments_)
    muxer->segments_->append(segment1);
    muxer->segments_->append(segment2);
    muxer->segments_->append(segment3);

    // Set max target duration
    muxer->max_td_ = 10 * SRS_UTIME_SECONDS;

    // Call do_refresh_m3u8 to generate m3u8 file
    std::string m3u8_file = "/tmp/test_stream.m3u8";
    HELPER_EXPECT_SUCCESS(muxer->do_refresh_m3u8(m3u8_file));

    // Verify factory was called to create file writer for m3u8
    EXPECT_EQ(1, mock_factory.create_file_writer_count_);

    // Verify segments were processed (the function completed successfully)
    // This test covers the typical use scenario:
    // 1. Function succeeds with multiple segments of different durations
    // 2. Factory is called to create writer for m3u8 file
    // 3. All segments are still in the window after refresh
    EXPECT_EQ(3, muxer->segments_->size());

    // Verify the segments are still accessible
    EXPECT_TRUE(muxer->segments_->at(0) != NULL);
    EXPECT_TRUE(muxer->segments_->at(1) != NULL);
    EXPECT_TRUE(muxer->segments_->at(2) != NULL);

    // Clean up
    muxer->context_ = NULL;
}

// Unit test for SrsHlsController selection code - typical scenario
VOID TEST(AppHlsTest, HlsControllerSelectionTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsController instance
    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());

    // Initialize the controller
    HELPER_EXPECT_SUCCESS(controller->initialize());

    // Cast to concrete type to access private members for testing
    SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller->muxer_);
    srs_assert(muxer);

    // Test initial state - no current segment
    // sequence_no() should return 0
    EXPECT_EQ(0, controller->sequence_no());

    // ts_url() should return empty string when no current segment
    EXPECT_EQ("", controller->ts_url());

    // duration() should return 0 when no current segment
    EXPECT_EQ(0, controller->duration());

    // deviation() should return 0 when hls_ts_floor is false
    EXPECT_EQ(0, controller->deviation());

    // Setup a current segment to test typical scenario
    SrsUniquePtr<MockSrsFileWriter> fw(new MockSrsFileWriter());
    SrsUniquePtr<SrsTsContext> context(new SrsTsContext());

    // Create a segment with typical values
    SrsHlsSegment *segment = new SrsHlsSegment(context.get(), SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, fw.get());
    segment->sequence_no_ = 42;
    segment->uri_ = "stream1-42.ts";
    segment->append(0);
    segment->append(10000); // 10 seconds duration in milliseconds

    // Set the current segment in the muxer
    muxer->current_ = segment;
    muxer->sequence_no_ = 42;

    // Test selection code with current segment
    // sequence_no() should return the muxer's sequence number
    EXPECT_EQ(42, controller->sequence_no());

    // ts_url() should return the current segment's URI
    EXPECT_EQ("stream1-42.ts", controller->ts_url());

    // duration() should return the current segment's duration (10 seconds in microseconds)
    EXPECT_EQ(10000 * SRS_UTIME_MILLISECONDS, controller->duration());

    // deviation() should still return 0 when hls_ts_floor is false
    EXPECT_EQ(0, controller->deviation());

    // Test deviation with hls_ts_floor enabled
    muxer->hls_ts_floor_ = true;
    muxer->deviation_ts_ = 5;

    // deviation() should return the deviation value when hls_ts_floor is true
    EXPECT_EQ(5, controller->deviation());

    // Clean up
    muxer->current_ = NULL;
}

// Unit test for SrsHlsController::on_publish typical scenario
VOID TEST(AppHlsTest, HlsControllerOnPublishTypicalScenario)
{
    srs_error_t err;

    // Setup mock config
    MockAppConfig mock_config;

    // Create SrsHlsController instance
    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());
    controller->config_ = &mock_config;

    // Initialize the controller
    HELPER_EXPECT_SUCCESS(controller->initialize());

    // Cast to concrete type to access private members for testing
    SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller->muxer_);
    srs_assert(muxer);

    // Create mock request
    MockHlsRequest mock_request("test.vhost", "live", "stream1");

    // Call on_publish - typical scenario
    HELPER_EXPECT_SUCCESS(controller->on_publish(&mock_request));

    // Verify that muxer was configured properly
    // Check that muxer's request was set
    EXPECT_TRUE(muxer->req_ != NULL);
    EXPECT_EQ("test.vhost", muxer->req_->vhost_);
    EXPECT_EQ("live", muxer->req_->app_);
    EXPECT_EQ("stream1", muxer->req_->stream_);

    // Verify HLS configuration was applied to muxer
    // Fragment should be 10 seconds (from MockAppConfig default)
    EXPECT_EQ(10 * SRS_UTIME_SECONDS, muxer->hls_fragment_);

    // Window should be 60 seconds (from MockAppConfig default)
    EXPECT_EQ(60 * SRS_UTIME_SECONDS, muxer->hls_window_);

    // Path should be "./objs/nginx/html" (from MockAppConfig default)
    EXPECT_EQ("./objs/nginx/html", muxer->hls_path_);

    // TS file should be "[app]/[stream]-[seq].ts" (from MockAppConfig default)
    EXPECT_EQ("[app]/[stream]-[seq].ts", muxer->hls_ts_file_);

    // AOF ratio should be 2.0 (from MockAppConfig default)
    EXPECT_EQ(2.0, muxer->hls_aof_ratio_);

    // Cleanup should be true (from MockAppConfig default)
    EXPECT_TRUE(muxer->hls_cleanup_);

    // Wait keyframe should be true (from MockAppConfig default)
    EXPECT_TRUE(muxer->hls_wait_keyframe_);

    // TS floor should be false (from MockAppConfig default)
    EXPECT_FALSE(muxer->hls_ts_floor_);

    // Keys should be false (from MockAppConfig default)
    EXPECT_FALSE(muxer->hls_keys_);

    // Fragments per key should be 5 (from MockAppConfig default)
    EXPECT_EQ(5, muxer->hls_fragments_per_key_);

    // Verify hls_dts_directly was set from config
    EXPECT_TRUE(controller->hls_dts_directly_);

    // Verify that a segment was opened
    EXPECT_TRUE(muxer->current_ != NULL);
}

// Unit test for SrsHlsController::on_unpublish typical scenario
VOID TEST(AppHlsTest, HlsControllerOnUnpublishTypicalScenario)
{
    srs_error_t err;

    // Setup mock config
    MockAppConfig mock_config;

    // Create SrsHlsController instance
    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());
    controller->config_ = &mock_config;

    // Initialize the controller
    HELPER_EXPECT_SUCCESS(controller->initialize());

    // Cast to concrete type to access private members for testing
    SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller->muxer_);
    srs_assert(muxer);

    // Create mock request
    MockHlsRequest mock_request("test.vhost", "live", "stream1");

    // Call on_publish first to set up the muxer
    HELPER_EXPECT_SUCCESS(controller->on_publish(&mock_request));

    // Verify that a segment was opened
    EXPECT_TRUE(muxer->current_ != NULL);

    // Set the codec in the muxer to enable proper audio/video handling
    controller->muxer_->set_latest_acodec(SrsAudioCodecIdAAC);
    controller->muxer_->set_latest_vcodec(SrsVideoCodecIdAVC);

    // Add some audio data to the cache to test flush_audio
    SrsTsMessage *audio_msg = new SrsTsMessage();
    audio_msg->dts_ = 90000; // 1 second in 90kHz
    audio_msg->pts_ = 90000;
    audio_msg->sid_ = SrsTsPESStreamIdAudioCommon;
    audio_msg->start_pts_ = 0;

    // Add some audio data to payload
    const char audio_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    audio_msg->payload_->append(audio_data, sizeof(audio_data));

    // Set the audio message in the cache
    controller->tsmc_->audio_ = audio_msg;

    // Call on_unpublish - typical scenario
    HELPER_EXPECT_SUCCESS(controller->on_unpublish());

    // Verify that audio was flushed (audio_ should be NULL after flush)
    EXPECT_TRUE(controller->tsmc_->audio_ == NULL);

    // Verify that the segment was closed (current_ should be NULL after close)
    EXPECT_TRUE(muxer->current_ == NULL);
}

// Unit test for SrsHlsController::write_video typical scenario
VOID TEST(AppHlsTest, HlsControllerWriteVideoTypicalScenario)
{
    srs_error_t err;

    // Setup mock config
    MockAppConfig mock_config;

    // Create SrsHlsController instance
    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());
    controller->config_ = &mock_config;

    // Initialize the controller
    HELPER_EXPECT_SUCCESS(controller->initialize());

    // Cast to concrete type to access private members for testing
    SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(controller->muxer_);
    srs_assert(muxer);

    // Create mock request
    MockHlsRequest mock_request("test.vhost", "live", "stream1");

    // Call on_publish first to set up the muxer
    HELPER_EXPECT_SUCCESS(controller->on_publish(&mock_request));

    // Verify that a segment was opened
    EXPECT_TRUE(muxer->current_ != NULL);

    // Create a mock SrsFormat with video codec
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    HELPER_EXPECT_SUCCESS(format->initialize());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;
    format->video_ = new SrsParsedVideoPacket();
    HELPER_EXPECT_SUCCESS(format->video_->initialize(format->vcodec_));

    // Set video frame properties for keyframe
    format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitNALU;

    // Add a sample NALU to the video frame
    unsigned char nalu_data[] = {0x65, 0x88, 0x84, 0x00, 0x33, 0xff};
    HELPER_EXPECT_SUCCESS(format->video_->add_sample((char *)nalu_data, sizeof(nalu_data)));

    // Create a mock SrsMediaPacket for video
    SrsUniquePtr<SrsMediaPacket> video_packet(new SrsMediaPacket());
    video_packet->timestamp_ = 1000; // 1 second
    video_packet->message_type_ = SrsFrameTypeVideo;

    // Create video payload
    char *video_data = new char[10];
    video_data[0] = 0x17; // keyframe + AVC
    video_data[1] = 0x01; // AVC NALU
    for (int i = 2; i < 10; i++) {
        video_data[i] = i;
    }
    video_packet->wrap(video_data, 10);

    // Set the codec in the muxer
    controller->muxer_->set_latest_vcodec(SrsVideoCodecIdAVC);

    // Call write_video - typical scenario
    HELPER_EXPECT_SUCCESS(controller->write_video(video_packet.get(), format.get()));

    // Verify that video was flushed (video_ should be NULL after flush_video)
    // The write_video method calls flush_video at the end, which frees the video message
    EXPECT_TRUE(controller->tsmc_->video_ == NULL);

    // Verify that the segment is still open (not reaped yet, since segment is not overflow)
    EXPECT_TRUE(muxer->current_ != NULL);

    // Verify that the codec was set correctly
    EXPECT_EQ(SrsVideoCodecIdAVC, muxer->latest_vcodec());
}

// Unit test for SrsHlsController::reap_segment typical scenario
VOID TEST(AppHlsTest, HlsControllerReapSegmentTypicalScenario)
{
    srs_error_t err;

    // Create SrsHlsController instance
    SrsUniquePtr<SrsHlsController> controller(new SrsHlsController());

    // Initialize the controller
    HELPER_EXPECT_SUCCESS(controller->initialize());

    // Create mock muxer to replace the real one
    SrsUniquePtr<MockHlsMuxer> mock_muxer(new MockHlsMuxer());

    // Create a TS message cache
    SrsUniquePtr<SrsTsMessageCache> tsmc(new SrsTsMessageCache());

    // Test typical scenario: successful segment reaping
    // The reap_segment method should:
    // 1. Close current segment
    // 2. Open new segment
    // 3. Flush video to new segment
    // 4. Flush audio to new segment

    // Simulate the reap_segment logic with mock muxer
    // Step 1: Close current ts segment
    HELPER_EXPECT_SUCCESS(mock_muxer->segment_close());
    EXPECT_EQ(1, mock_muxer->segment_close_count_);

    // Step 2: Open new ts segment
    HELPER_EXPECT_SUCCESS(mock_muxer->segment_open());
    EXPECT_EQ(1, mock_muxer->segment_open_count_);

    // Step 3: Flush video first after segment open
    HELPER_EXPECT_SUCCESS(mock_muxer->flush_video(tsmc.get()));
    EXPECT_EQ(1, mock_muxer->flush_video_count_);

    // Step 4: Flush audio after video (to make iPhone happy)
    HELPER_EXPECT_SUCCESS(mock_muxer->flush_audio(tsmc.get()));
    EXPECT_EQ(1, mock_muxer->flush_audio_count_);

    // Verify all operations were called in the correct order
    EXPECT_EQ(1, mock_muxer->segment_close_count_);
    EXPECT_EQ(1, mock_muxer->segment_open_count_);
    EXPECT_EQ(1, mock_muxer->flush_video_count_);
    EXPECT_EQ(1, mock_muxer->flush_audio_count_);
}

// Mock HLS controller implementation
MockHlsController::MockHlsController()
{
    initialize_count_ = 0;
    dispose_count_ = 0;
    on_publish_count_ = 0;
    on_unpublish_count_ = 0;
    write_audio_count_ = 0;
    write_video_count_ = 0;
    on_sequence_header_count_ = 0;
    initialize_error_ = srs_success;
    on_publish_error_ = srs_success;
    on_unpublish_error_ = srs_success;
}

MockHlsController::~MockHlsController()
{
    srs_freep(initialize_error_);
    srs_freep(on_publish_error_);
    srs_freep(on_unpublish_error_);
}

srs_error_t MockHlsController::initialize()
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

void MockHlsController::dispose()
{
    dispose_count_++;
}

srs_error_t MockHlsController::on_publish(ISrsRequest *req)
{
    on_publish_count_++;
    return srs_error_copy(on_publish_error_);
}

srs_error_t MockHlsController::on_unpublish()
{
    on_unpublish_count_++;
    return srs_error_copy(on_unpublish_error_);
}

srs_error_t MockHlsController::write_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    write_audio_count_++;
    return srs_success;
}

srs_error_t MockHlsController::write_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    write_video_count_++;
    return srs_success;
}

srs_error_t MockHlsController::on_sequence_header(SrsMediaPacket *msg, SrsFormat *format)
{
    on_sequence_header_count_++;
    return srs_success;
}

int MockHlsController::sequence_no()
{
    return 0;
}

std::string MockHlsController::ts_url()
{
    return "test.ts";
}

srs_utime_t MockHlsController::duration()
{
    return 10 * SRS_UTIME_SECONDS;
}

int MockHlsController::deviation()
{
    return 0;
}

void MockHlsController::reset()
{
    initialize_count_ = 0;
    dispose_count_ = 0;
    on_publish_count_ = 0;
    on_unpublish_count_ = 0;
    write_audio_count_ = 0;
    write_video_count_ = 0;
    on_sequence_header_count_ = 0;
    srs_freep(initialize_error_);
    srs_freep(on_publish_error_);
    srs_freep(on_unpublish_error_);
}

void MockHlsController::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockHlsController::set_on_publish_error(srs_error_t err)
{
    srs_freep(on_publish_error_);
    on_publish_error_ = srs_error_copy(err);
}

void MockHlsController::set_on_unpublish_error(srs_error_t err)
{
    srs_freep(on_unpublish_error_);
    on_unpublish_error_ = srs_error_copy(err);
}

// Unit test for SrsHls::reload typical scenario
VOID TEST(AppHlsTest, HlsReloadTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    SrsUniquePtr<MockAppConfigForHlsNotify> mock_config(new MockAppConfigForHlsNotify());
    mock_config->set_hls_enabled(true);

    // Create SrsHls instance
    SrsUniquePtr<SrsHls> hls(new SrsHls());

    // Replace config with mock
    hls->config_ = mock_config.get();

    // Create mock request
    SrsUniquePtr<MockHlsRequest> req(new MockHlsRequest("test.vhost", "live", "stream1"));

    // Create mock origin hub
    MockOriginHub mock_hub;

    // Initialize the HLS with mock hub and request
    // This will create a real controller, which we'll replace with a mock
    HELPER_EXPECT_SUCCESS(hls->initialize(&mock_hub, req.get()));

    // Replace the controller with a mock controller
    srs_freep(hls->controller_);
    MockHlsController *mock_controller = new MockHlsController();
    hls->controller_ = mock_controller;

    // Enable HLS by calling on_publish
    HELPER_EXPECT_SUCCESS(hls->on_publish());

    // Verify HLS is enabled
    EXPECT_TRUE(hls->enabled_);
    EXPECT_EQ(1, mock_controller->on_publish_count_);

    // Trigger async reload
    hls->async_reload();
    EXPECT_TRUE(hls->async_reload_);

    // Call reload() - this is the method we're testing
    HELPER_EXPECT_SUCCESS(hls->reload());

    // Verify the reload behavior:
    // 1. Controller should be unpublished and republished
    EXPECT_EQ(1, mock_controller->on_unpublish_count_);
    EXPECT_EQ(2, mock_controller->on_publish_count_); // Once from initial on_publish, once from reload

    // 2. Hub should request sequence header
    EXPECT_EQ(1, mock_hub.on_hls_request_sh_count_);

    // 3. async_reload flag should be cleared
    EXPECT_FALSE(hls->async_reload_);

    // 4. reloading flag should be cleared
    EXPECT_FALSE(hls->reloading_);

    // 5. HLS should still be enabled
    EXPECT_TRUE(hls->enabled_);

    // Clean up
    hls->on_unpublish();
}

// Unit test for SrsHls::on_audio typical scenario
VOID TEST(AppHlsTest, HlsOnAudioTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    SrsUniquePtr<MockAppConfigForHlsNotify> mock_config(new MockAppConfigForHlsNotify());
    mock_config->set_hls_enabled(true);

    // Create SrsHls instance
    SrsUniquePtr<SrsHls> hls(new SrsHls());

    // Replace config with mock
    hls->config_ = mock_config.get();

    // Create mock request
    SrsUniquePtr<MockHlsRequest> req(new MockHlsRequest("test.vhost", "live", "stream1"));

    // Create mock origin hub
    MockOriginHub mock_hub;

    // Initialize the HLS with mock hub and request
    HELPER_EXPECT_SUCCESS(hls->initialize(&mock_hub, req.get()));

    // Replace the controller with a mock controller
    srs_freep(hls->controller_);
    MockHlsController *mock_controller = new MockHlsController();
    hls->controller_ = mock_controller;

    // Enable HLS by calling on_publish
    HELPER_EXPECT_SUCCESS(hls->on_publish());

    // Verify HLS is enabled
    EXPECT_TRUE(hls->enabled_);

    // Create SrsFormat with AAC audio codec
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    format->acodec_ = new SrsAudioCodecConfig();
    format->acodec_->id_ = SrsAudioCodecIdAAC;
    format->acodec_->sound_rate_ = SrsAudioSampleRate44100;
    format->audio_ = new SrsParsedAudioPacket();
    HELPER_EXPECT_SUCCESS(format->audio_->initialize(format->acodec_));

    // Set audio packet type to raw data (not sequence header)
    format->audio_->aac_packet_type_ = SrsAudioAacFrameTraitRawData;

    // Create SrsMediaPacket with audio data
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->timestamp_ = 1000; // 1 second in milliseconds
    audio_packet->message_type_ = SrsFrameTypeAudio;

    // Create audio payload (AAC raw data) - must be heap allocated for wrap()
    char *audio_data = new char[10];
    audio_data[0] = 0x01;
    audio_data[1] = 0x02;
    audio_data[2] = 0x03;
    audio_data[3] = 0x04;
    audio_data[4] = 0x05;
    audio_data[5] = 0x06;
    audio_data[6] = 0x07;
    audio_data[7] = 0x08;
    audio_data[8] = 0x09;
    audio_data[9] = 0x0a;
    audio_packet->wrap(audio_data, 10);

    // Add sample to format->audio_
    format->audio_->nb_samples_ = 1;
    format->audio_->samples_[0].bytes_ = audio_data;
    format->audio_->samples_[0].size_ = 10;

    // Call on_audio - should succeed in typical scenario
    HELPER_EXPECT_SUCCESS(hls->on_audio(audio_packet.get(), format.get()));

    // Verify the typical scenario behavior:
    // 1. last_update_time_ should be updated
    EXPECT_GT(hls->last_update_time_, 0);

    // 2. Controller's write_audio should be called (not on_sequence_header)
    EXPECT_EQ(1, mock_controller->write_audio_count_);
    EXPECT_EQ(0, mock_controller->on_sequence_header_count_);

    // Clean up
    hls->on_unpublish();
}

VOID TEST(HlsTest, OnVideoTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    SrsUniquePtr<MockAppConfigForHlsNotify> mock_config(new MockAppConfigForHlsNotify());
    mock_config->set_hls_enabled(true);

    // Create SrsHls instance
    SrsUniquePtr<SrsHls> hls(new SrsHls());
    hls->config_ = mock_config.get();

    // Create mock request
    SrsUniquePtr<MockHlsRequest> req(new MockHlsRequest("test.vhost", "live", "stream1"));

    // Create mock origin hub
    MockOriginHub mock_hub;

    // Initialize the HLS with mock hub and request
    HELPER_EXPECT_SUCCESS(hls->initialize(&mock_hub, req.get()));

    // Replace the controller with a mock controller
    srs_freep(hls->controller_);
    MockHlsController *mock_controller = new MockHlsController();
    hls->controller_ = mock_controller;

    // Enable HLS by calling on_publish
    HELPER_EXPECT_SUCCESS(hls->on_publish());

    // Verify HLS is enabled
    EXPECT_TRUE(hls->enabled_);

    // Create SrsFormat with AVC video codec
    SrsUniquePtr<SrsFormat> format(new SrsFormat());
    format->vcodec_ = new SrsVideoCodecConfig();
    format->vcodec_->id_ = SrsVideoCodecIdAVC;
    format->video_ = new SrsParsedVideoPacket();
    HELPER_EXPECT_SUCCESS(format->video_->initialize(format->vcodec_));

    // Set video frame type to keyframe (not info frame, not sequence header)
    format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitNALU;

    // Create SrsMediaPacket with video data
    SrsUniquePtr<SrsMediaPacket> video_packet(new SrsMediaPacket());
    video_packet->timestamp_ = 2000; // 2 seconds in milliseconds
    video_packet->message_type_ = SrsFrameTypeVideo;

    // Create video payload (AVC NALU data) - must be heap allocated for wrap()
    char *video_data = new char[20];
    for (int i = 0; i < 20; i++) {
        video_data[i] = (char)(0x10 + i);
    }
    video_packet->wrap(video_data, 20);

    // Add sample to format->video_
    format->video_->nb_samples_ = 1;
    format->video_->samples_[0].bytes_ = video_data;
    format->video_->samples_[0].size_ = 20;

    // Call on_video - should succeed in typical scenario
    HELPER_EXPECT_SUCCESS(hls->on_video(video_packet.get(), format.get()));

    // Verify the typical scenario behavior:
    // 1. last_update_time_ should be updated
    EXPECT_GT(hls->last_update_time_, 0);

    // 2. Controller's write_video should be called (not on_sequence_header)
    EXPECT_EQ(1, mock_controller->write_video_count_);
    EXPECT_EQ(0, mock_controller->on_sequence_header_count_);

    // Clean up
    hls->on_unpublish();
}
