//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FACTORY_HPP
#define SRS_APP_FACTORY_HPP

#include <srs_core.hpp>

#include <srs_kernel_factory.hpp>

class ISrsFileWriter;
class ISrsFileReader;
class SrsPath;
class SrsLiveSource;
class ISrsOriginHub;
class ISrsHourGlass;
class ISrsHourGlassHandler;
class ISrsBasicRtmpClient;
class ISrsHttpClient;
class ISrsFileReader;
class ISrsFlvDecoder;
class ISrsHttpResponseReader;
class ISrsRtspSendTrack;
class ISrsRtspConnection;
class SrsRtcTrackDescription;
class ISrsFlvTransmuxer;
class ISrsMp4Encoder;
class ISrsDvrSegmenter;
class ISrsGbMediaTcpConn;
class ISrsGbSession;
class ISrsFragment;
class ISrsInitMp4;
class ISrsFragmentWindow;
class ISrsFragmentedMp4;
class SrsFinalFactory;
class ISrsIpListener;
class ISrsTcpHandler;
class ISrsRtcConnection;
class ISrsExecRtcAsyncTask;
class ISrsFFMPEG;
class ISrsIngesterFFMPEG;

// The factory to create app objects.
class ISrsAppFactory : public ISrsKernelFactory
{
public:
    ISrsAppFactory();
    virtual ~ISrsAppFactory();

public:
    virtual ISrsFileWriter *create_file_writer() = 0;
    virtual ISrsFileWriter *create_enc_file_writer() = 0;
    virtual ISrsFileReader *create_file_reader() = 0;
    virtual SrsPath *create_path() = 0;
    virtual SrsLiveSource *create_live_source() = 0;
    virtual ISrsOriginHub *create_origin_hub() = 0;
    virtual ISrsHourGlass *create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval) = 0;
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto) = 0;
    virtual ISrsHttpClient *create_http_client() = 0;
    virtual ISrsFileReader *create_http_file_reader(ISrsHttpResponseReader *r) = 0;
    virtual ISrsFlvDecoder *create_flv_decoder() = 0;
#ifdef SRS_RTSP
    virtual ISrsRtspSendTrack *create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc) = 0;
    virtual ISrsRtspSendTrack *create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc) = 0;
#endif
    virtual ISrsFlvTransmuxer *create_flv_transmuxer() = 0;
    virtual ISrsMp4Encoder *create_mp4_encoder() = 0;
    virtual ISrsDvrSegmenter *create_dvr_flv_segmenter() = 0;
    virtual ISrsDvrSegmenter *create_dvr_mp4_segmenter() = 0;
#ifdef SRS_GB28181
    virtual ISrsGbMediaTcpConn *create_gb_media_tcp_conn() = 0;
    virtual ISrsGbSession *create_gb_session() = 0;
#endif
    virtual ISrsInitMp4 *create_init_mp4() = 0;
    virtual ISrsFragmentWindow *create_fragment_window() = 0;
    virtual ISrsFragmentedMp4 *create_fragmented_mp4() = 0;
    virtual ISrsIpListener *create_tcp_listener(ISrsTcpHandler *handler) = 0;
    virtual ISrsRtcConnection *create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid) = 0;
    virtual ISrsFFMPEG *create_ffmpeg(std::string ffmpeg_bin) = 0;
    virtual ISrsIngesterFFMPEG *create_ingester_ffmpeg() = 0;
};

// The factory to create app objects.
class SrsAppFactory : public ISrsAppFactory
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsKernelFactory *kernel_factory_;

public:
    SrsAppFactory();
    virtual ~SrsAppFactory();

public:
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

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
};

extern ISrsAppFactory *_srs_app_factory;

// The factory to create kernel objects.
class SrsFinalFactory : public ISrsKernelFactory
{
public:
    SrsFinalFactory();
    virtual ~SrsFinalFactory();

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
};

// The proxy for config.
class SrsConfigProxy : public ISrsConfig
{
public:
    SrsConfigProxy();
    virtual ~SrsConfigProxy();

public:
    virtual srs_utime_t get_pithy_print();
    virtual std::string get_default_app_name();
};

// The time to use system time.
class SrsTrueTime : public ISrsTime
{
public:
    SrsTrueTime();
    virtual ~SrsTrueTime();

public:
    virtual void usleep(srs_utime_t duration);
};

#endif
