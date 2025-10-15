//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_factory.hpp>

#include <srs_app_caster_flv.hpp>
#include <srs_app_config.hpp>
#include <srs_app_dash.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_fragment.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_ingest.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_st.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_st.hpp>

ISrsAppFactory::ISrsAppFactory()
{
}

ISrsAppFactory::~ISrsAppFactory()
{
}

SrsAppFactory::SrsAppFactory()
{
    kernel_factory_ = new SrsFinalFactory();
}

SrsAppFactory::~SrsAppFactory()
{
    srs_freep(kernel_factory_);
}

ISrsFileWriter *SrsAppFactory::create_file_writer()
{
    return new SrsFileWriter();
}

ISrsFileWriter *SrsAppFactory::create_enc_file_writer()
{
    return new SrsEncFileWriter();
}

ISrsFileReader *SrsAppFactory::create_file_reader()
{
    return new SrsFileReader();
}

SrsPath *SrsAppFactory::create_path()
{
    return new SrsPath();
}

SrsLiveSource *SrsAppFactory::create_live_source()
{
    return new SrsLiveSource();
}

ISrsOriginHub *SrsAppFactory::create_origin_hub()
{
    SrsOriginHub *hub = new SrsOriginHub();
    hub->assemble();
    return hub;
}

ISrsHourGlass *SrsAppFactory::create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval)
{
    return new SrsHourGlass(name, handler, interval);
}

ISrsBasicRtmpClient *SrsAppFactory::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    return new SrsSimpleRtmpClient(url, cto, sto);
}

ISrsHttpClient *SrsAppFactory::create_http_client()
{
    return new SrsHttpClient();
}

ISrsFileReader *SrsAppFactory::create_http_file_reader(ISrsHttpResponseReader *r)
{
    return new SrsHttpFileReader(r);
}

ISrsFlvDecoder *SrsAppFactory::create_flv_decoder()
{
    return new SrsFlvDecoder();
}

#ifdef SRS_RTSP
ISrsRtspSendTrack *SrsAppFactory::create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
{
    return new SrsRtspAudioSendTrack(session, track_desc);
}

ISrsRtspSendTrack *SrsAppFactory::create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
{
    return new SrsRtspVideoSendTrack(session, track_desc);
}
#endif

ISrsFlvTransmuxer *SrsAppFactory::create_flv_transmuxer()
{
    return new SrsFlvTransmuxer();
}

ISrsMp4Encoder *SrsAppFactory::create_mp4_encoder()
{
    return new SrsMp4Encoder();
}

ISrsDvrSegmenter *SrsAppFactory::create_dvr_flv_segmenter()
{
    return new SrsDvrFlvSegmenter();
}

ISrsDvrSegmenter *SrsAppFactory::create_dvr_mp4_segmenter()
{
    return new SrsDvrMp4Segmenter();
}

#ifdef SRS_GB28181
ISrsGbMediaTcpConn *SrsAppFactory::create_gb_media_tcp_conn()
{
    return new SrsGbMediaTcpConn();
}

ISrsGbSession *SrsAppFactory::create_gb_session()
{
    return new SrsGbSession();
}
#endif

ISrsInitMp4 *SrsAppFactory::create_init_mp4()
{
    return new SrsInitMp4();
}

ISrsFragmentWindow *SrsAppFactory::create_fragment_window()
{
    return new SrsFragmentWindow();
}

ISrsFragmentedMp4 *SrsAppFactory::create_fragmented_mp4()
{
    return new SrsFragmentedMp4();
}

ISrsIpListener *SrsAppFactory::create_tcp_listener(ISrsTcpHandler *handler)
{
    return new SrsTcpListener(handler);
}

ISrsRtcConnection *SrsAppFactory::create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid)
{
    SrsRtcConnection *session = new SrsRtcConnection(exec, cid);
    session->assemble();
    return session;
}

ISrsFFMPEG *SrsAppFactory::create_ffmpeg(std::string ffmpeg_bin)
{
    return new SrsFFMPEG(ffmpeg_bin);
}

ISrsIngesterFFMPEG *SrsAppFactory::create_ingester_ffmpeg()
{
    return new SrsIngesterFFMPEG();
}

ISrsCoroutine *SrsAppFactory::create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid)
{
    return kernel_factory_->create_coroutine(name, handler, cid);
}

ISrsTime *SrsAppFactory::create_time()
{
    return kernel_factory_->create_time();
}

ISrsConfig *SrsAppFactory::create_config()
{
    return kernel_factory_->create_config();
}

ISrsCond *SrsAppFactory::create_cond()
{
    return kernel_factory_->create_cond();
}

SrsFinalFactory::SrsFinalFactory()
{
}

SrsFinalFactory::~SrsFinalFactory()
{
}

ISrsCoroutine *SrsFinalFactory::create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid)
{
    return new SrsSTCoroutine(name, handler, cid);
}

ISrsTime *SrsFinalFactory::create_time()
{
    return new SrsTrueTime();
}

ISrsConfig *SrsFinalFactory::create_config()
{
    return new SrsConfigProxy();
}

ISrsCond *SrsFinalFactory::create_cond()
{
    return new SrsCond();
}

SrsConfigProxy::SrsConfigProxy()
{
}

SrsConfigProxy::~SrsConfigProxy()
{
}

srs_utime_t SrsConfigProxy::get_pithy_print()
{
    return _srs_config->get_pithy_print();
}

std::string SrsConfigProxy::get_default_app_name()
{
    return _srs_config->get_default_app_name();
}

SrsTrueTime::SrsTrueTime()
{
}

SrsTrueTime::~SrsTrueTime()
{
}

void SrsTrueTime::usleep(srs_utime_t duration)
{
    srs_usleep(duration);
}
