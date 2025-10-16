//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_rtc_playstream.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_app.hpp>
#include <srs_utest_app6.hpp>

// Mock ISrsCoroutine implementation
MockCoroutineForPlayStream::MockCoroutineForPlayStream()
{
    start_error_ = srs_success;
    start_count_ = 0;
    stop_count_ = 0;
    interrupt_count_ = 0;
}

MockCoroutineForPlayStream::~MockCoroutineForPlayStream()
{
    srs_freep(start_error_);
}

srs_error_t MockCoroutineForPlayStream::start()
{
    start_count_++;
    if (start_error_ != srs_success) {
        return srs_error_copy(start_error_);
    }
    return srs_success;
}

void MockCoroutineForPlayStream::stop()
{
    stop_count_++;
}

void MockCoroutineForPlayStream::interrupt()
{
    interrupt_count_++;
}

srs_error_t MockCoroutineForPlayStream::pull()
{
    return srs_success;
}

const SrsContextId &MockCoroutineForPlayStream::cid()
{
    return cid_;
}

void MockCoroutineForPlayStream::set_cid(const SrsContextId &cid)
{
    cid_ = cid;
}

void MockCoroutineForPlayStream::reset()
{
    srs_freep(start_error_);
    start_count_ = 0;
    stop_count_ = 0;
    interrupt_count_ = 0;
}

void MockCoroutineForPlayStream::set_start_error(srs_error_t err)
{
    srs_freep(start_error_);
    start_error_ = srs_error_copy(err);
}

// Mock ISrsAppFactory implementation
MockAppFactoryForPlayStream::MockAppFactoryForPlayStream()
{
    mock_coroutine_ = new MockCoroutineForPlayStream();
    create_coroutine_error_ = srs_success;
    create_coroutine_count_ = 0;
    last_coroutine_handler_ = NULL;
}

MockAppFactoryForPlayStream::~MockAppFactoryForPlayStream()
{
    srs_freep(mock_coroutine_);
    srs_freep(create_coroutine_error_);
}

ISrsFileWriter *MockAppFactoryForPlayStream::create_file_writer()
{
    return NULL;
}

ISrsFileWriter *MockAppFactoryForPlayStream::create_enc_file_writer()
{
    return NULL;
}

ISrsFileReader *MockAppFactoryForPlayStream::create_file_reader()
{
    return NULL;
}

SrsPath *MockAppFactoryForPlayStream::create_path()
{
    return NULL;
}

SrsLiveSource *MockAppFactoryForPlayStream::create_live_source()
{
    return NULL;
}

ISrsOriginHub *MockAppFactoryForPlayStream::create_origin_hub()
{
    return NULL;
}

ISrsHourGlass *MockAppFactoryForPlayStream::create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval)
{
    return NULL;
}

ISrsBasicRtmpClient *MockAppFactoryForPlayStream::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    return NULL;
}

ISrsHttpClient *MockAppFactoryForPlayStream::create_http_client()
{
    return NULL;
}

ISrsFileReader *MockAppFactoryForPlayStream::create_http_file_reader(ISrsHttpResponseReader *r)
{
    return NULL;
}

ISrsFlvDecoder *MockAppFactoryForPlayStream::create_flv_decoder()
{
    return NULL;
}

#ifdef SRS_RTSP
ISrsRtspSendTrack *MockAppFactoryForPlayStream::create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
{
    return NULL;
}

ISrsRtspSendTrack *MockAppFactoryForPlayStream::create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
{
    return NULL;
}
#endif

ISrsFlvTransmuxer *MockAppFactoryForPlayStream::create_flv_transmuxer()
{
    return NULL;
}

ISrsMp4Encoder *MockAppFactoryForPlayStream::create_mp4_encoder()
{
    return NULL;
}

ISrsDvrSegmenter *MockAppFactoryForPlayStream::create_dvr_flv_segmenter()
{
    return NULL;
}

ISrsDvrSegmenter *MockAppFactoryForPlayStream::create_dvr_mp4_segmenter()
{
    return NULL;
}

#ifdef SRS_GB28181
ISrsGbMediaTcpConn *MockAppFactoryForPlayStream::create_gb_media_tcp_conn()
{
    return NULL;
}

ISrsGbSession *MockAppFactoryForPlayStream::create_gb_session()
{
    return NULL;
}
#endif

ISrsInitMp4 *MockAppFactoryForPlayStream::create_init_mp4()
{
    return NULL;
}

ISrsFragmentWindow *MockAppFactoryForPlayStream::create_fragment_window()
{
    return NULL;
}

ISrsFragmentedMp4 *MockAppFactoryForPlayStream::create_fragmented_mp4()
{
    return NULL;
}

ISrsIpListener *MockAppFactoryForPlayStream::create_tcp_listener(ISrsTcpHandler *handler)
{
    return NULL;
}

ISrsRtcConnection *MockAppFactoryForPlayStream::create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid)
{
    return NULL;
}

ISrsFFMPEG *MockAppFactoryForPlayStream::create_ffmpeg(std::string ffmpeg_bin)
{
    return NULL;
}

ISrsIngesterFFMPEG *MockAppFactoryForPlayStream::create_ingester_ffmpeg()
{
    return NULL;
}

ISrsCoroutine *MockAppFactoryForPlayStream::create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid)
{
    create_coroutine_count_++;
    last_coroutine_name_ = name;
    last_coroutine_handler_ = handler;
    last_coroutine_cid_ = cid;

    if (create_coroutine_error_ != srs_success) {
        return NULL;
    }

    return mock_coroutine_;
}

ISrsTime *MockAppFactoryForPlayStream::create_time()
{
    return NULL;
}

ISrsConfig *MockAppFactoryForPlayStream::create_config()
{
    return NULL;
}

ISrsCond *MockAppFactoryForPlayStream::create_cond()
{
    return NULL;
}

void MockAppFactoryForPlayStream::reset()
{
    mock_coroutine_->reset();
    srs_freep(create_coroutine_error_);
    create_coroutine_count_ = 0;
    last_coroutine_name_ = "";
    last_coroutine_handler_ = NULL;
}

void MockAppFactoryForPlayStream::set_create_coroutine_error(srs_error_t err)
{
    srs_freep(create_coroutine_error_);
    create_coroutine_error_ = srs_error_copy(err);
}

// Mock ISrsRtcPliWorker implementation
MockRtcPliWorkerForPlayStream::MockRtcPliWorkerForPlayStream()
{
    start_error_ = srs_success;
    start_count_ = 0;
    request_keyframe_count_ = 0;
}

MockRtcPliWorkerForPlayStream::~MockRtcPliWorkerForPlayStream()
{
    srs_freep(start_error_);
}

srs_error_t MockRtcPliWorkerForPlayStream::start()
{
    start_count_++;
    if (start_error_ != srs_success) {
        return srs_error_copy(start_error_);
    }
    return srs_success;
}

void MockRtcPliWorkerForPlayStream::request_keyframe(uint32_t ssrc, SrsContextId cid)
{
    request_keyframe_count_++;
}

srs_error_t MockRtcPliWorkerForPlayStream::cycle()
{
    return srs_success;
}

void MockRtcPliWorkerForPlayStream::reset()
{
    srs_freep(start_error_);
    start_count_ = 0;
    request_keyframe_count_ = 0;
}

void MockRtcPliWorkerForPlayStream::set_start_error(srs_error_t err)
{
    srs_freep(start_error_);
    start_error_ = srs_error_copy(err);
}

// Test SrsRtcPlayStream::start() - Basic success scenario
VOID TEST(RtcPlayStreamStartTest, StartSuccess)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockExpire mock_expire;
    MockRtcPacketSender mock_sender;
    SrsContextId cid;
    cid.set_value("test-play-stream-start-cid");

    // Create RTC play stream
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_exec, &mock_expire, &mock_sender, cid));

    // Create and inject mock app factory
    MockAppFactoryForPlayStream mock_factory;
    play_stream->app_factory_ = &mock_factory;

    // Create and inject mock PLI worker
    MockRtcPliWorkerForPlayStream *mock_pli_worker = new MockRtcPliWorkerForPlayStream();
    play_stream->pli_worker_ = mock_pli_worker;

    // Test: First call to start() should succeed
    HELPER_EXPECT_SUCCESS(play_stream->start());

    // Verify coroutine was created with correct parameters
    EXPECT_EQ(1, mock_factory.create_coroutine_count_);
    EXPECT_STREQ("rtc_sender", mock_factory.last_coroutine_name_.c_str());
    EXPECT_TRUE(mock_factory.last_coroutine_handler_ == play_stream.get());
    EXPECT_EQ(0, cid.compare(mock_factory.last_coroutine_cid_));

    // Verify coroutine start was called
    EXPECT_EQ(1, mock_factory.mock_coroutine_->start_count_);

    // Verify PLI worker start was called
    EXPECT_EQ(1, mock_pli_worker->start_count_);

    // Verify is_started_ flag is set
    EXPECT_TRUE(play_stream->is_started_);

    // Clean up - set to NULL to avoid double-free
    play_stream->trd_ = NULL; // Set to NULL before mock_factory is destroyed
    play_stream->app_factory_ = NULL;
    play_stream->pli_worker_ = NULL;
    srs_freep(mock_pli_worker);
}
