//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_RTC_PLAYSTREAM_HPP
#define SRS_UTEST_RTC_PLAYSTREAM_HPP

/*
#include <srs_utest_rtc_playstream.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_factory.hpp>

// Mock ISrsCoroutine for testing SrsRtcPlayStream::start()
class MockCoroutineForPlayStream : public ISrsCoroutine
{
public:
    srs_error_t start_error_;
    int start_count_;
    int stop_count_;
    int interrupt_count_;
    SrsContextId cid_;

public:
    MockCoroutineForPlayStream();
    virtual ~MockCoroutineForPlayStream();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);

public:
    void reset();
    void set_start_error(srs_error_t err);
};

// Mock ISrsAppFactory for testing SrsRtcPlayStream::start()
class MockAppFactoryForPlayStream : public ISrsAppFactory
{
public:
    MockCoroutineForPlayStream *mock_coroutine_;
    srs_error_t create_coroutine_error_;
    int create_coroutine_count_;
    std::string last_coroutine_name_;
    ISrsCoroutineHandler *last_coroutine_handler_;
    SrsContextId last_coroutine_cid_;

public:
    MockAppFactoryForPlayStream();
    virtual ~MockAppFactoryForPlayStream();

public:
    // ISrsAppFactory interface methods
    virtual ISrsFileWriter *create_file_writer();
    virtual ISrsFileWriter *create_enc_file_writer();
    virtual ISrsFileReader *create_file_reader();
    virtual SrsPath *create_path();
    virtual SrsLiveSource *create_live_source();
    virtual ISrsOriginHub *create_origin_hub();
    virtual ISrsHourGlass *create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval);
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
    virtual ISrsHttpClient *create_http_client();
    virtual ISrsFileReader *create_http_file_reader(ISrsHttpResponseReader *r);
    virtual ISrsFlvDecoder *create_flv_decoder();
#ifdef SRS_RTSP
    virtual ISrsRtspSendTrack *create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ISrsRtspSendTrack *create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
#endif
    virtual ISrsFlvTransmuxer *create_flv_transmuxer();
    virtual ISrsMp4Encoder *create_mp4_encoder();
    virtual ISrsDvrSegmenter *create_dvr_flv_segmenter();
    virtual ISrsDvrSegmenter *create_dvr_mp4_segmenter();
#ifdef SRS_GB28181
    virtual ISrsGbMediaTcpConn *create_gb_media_tcp_conn();
    virtual ISrsGbSession *create_gb_session();
#endif
    virtual ISrsInitMp4 *create_init_mp4();
    virtual ISrsFragmentWindow *create_fragment_window();
    virtual ISrsFragmentedMp4 *create_fragmented_mp4();
    virtual ISrsIpListener *create_tcp_listener(ISrsTcpHandler *handler);
    virtual ISrsRtcConnection *create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid);
    virtual ISrsFFMPEG *create_ffmpeg(std::string ffmpeg_bin);
    virtual ISrsIngesterFFMPEG *create_ingester_ffmpeg();

    // ISrsKernelFactory interface methods
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();

public:
    void reset();
    void set_create_coroutine_error(srs_error_t err);
};

// Mock ISrsRtcPliWorker for testing SrsRtcPlayStream::start()
class MockRtcPliWorkerForPlayStream : public ISrsRtcPliWorker
{
public:
    srs_error_t start_error_;
    int start_count_;
    int request_keyframe_count_;

public:
    MockRtcPliWorkerForPlayStream();
    virtual ~MockRtcPliWorkerForPlayStream();

public:
    virtual srs_error_t start();
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid);
    virtual srs_error_t cycle();

public:
    void reset();
    void set_start_error(srs_error_t err);
};

#endif
