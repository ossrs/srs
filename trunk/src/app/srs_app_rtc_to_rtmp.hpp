/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Lixin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_RTC_TO_RTMP_HPP
#define SRS_APP_RTC_TO_RTMP_HPP

#include <srs_core.hpp>
#include <srs_app_listener.hpp>
#include <srs_service_st.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_jitbuffer.hpp>
#include <srs_kernel_file.hpp>
#include <srs_raw_avc.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_kernel_log.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_statistic.hpp>
#include <srs_rtmp_msg_array.hpp>

#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>

class SrsSimpleRtmpClient;
class SrsRawH264Stream;
class SrsRawAacStream;
class SrsRtpTimeJitter;
class SrsRtcRtmpMuxer;
class SrsRtcRtmpRecv;
class SrsRtcPublishStream;

//It is used for RTMP client in RTC rtmpmuxer to receive RTMP stream
class SrsRtcRtmpRecv : virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler
{
private:
    SrsContextId cid_;
    SrsCoroutine* trd;
    SrsRtcConnection* session_;
    SrsRtcRtmpMuxer* muxer_;
    srs_mutex_t locker_;
private:
    SrsSimpleRtmpClient* sdk;
    bool is_started;
public:
    SrsRtcRtmpRecv(SrsRtcConnection* s, SrsRtcRtmpMuxer* r, const SrsContextId& cid);
    virtual ~SrsRtcRtmpRecv();
public:
    void set_rtmp_client(SrsSimpleRtmpClient* c);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
    virtual const SrsContextId& context_id();
public:
    virtual srs_error_t start();
    virtual void stop();
public:
    virtual srs_error_t cycle();
};

//RTC rtp stream muxer rtmp stream, then  push rtmp server
class SrsRtcRtmpMuxer : virtual public ISrsCoroutineHandler, virtual public ISrsReloadHandler
{
private:
    SrsContextId cid_;
    SrsCoroutine* trd;
    SrsRtcConnection* session_;
    SrsRtcRtmpRecv *rtmp_recv_;
    SrsRtcPublishStream* publish_;
private:
    SrsRequest* req_;
    SrsRtcStream* source_;

    // The pithy print for special stage.
    //SrsErrorPithyPrint* nack_epp;
private:
    // For merged-write messages.
    int mw_msgs;
    bool realtime;

private:
    // Whether palyer started.
    bool is_started;
    // The statistic for consumer to send packets to player.
    // SrsRtcPlayStreamStatistic info;
private:
    SrsSimpleRtmpClient* sdk;
    SrsRawH264Stream* avc;
    std::string h264_sps;
    std::string h264_pps;
    uint32_t video_ssrc;

    bool send_video_seqheader;
    bool send_auido_seqheader;
    srs_utime_t recv_video_rtp_time;
    srs_utime_t recv_audio_rtp_time;
    srs_utime_t req_keyframe_time;
    bool republish_video;
    bool republish_audio;
private:
    SrsRawAacStream* aac;
    SrsRawAacStreamCodec* acodec;
    std::string aac_specific_config;
    SrsRtpTimeJitter* vjitter;
    SrsRtpTimeJitter* ajitter;
    SrsRtpJitterBuffer *jitter_buffer;

    char *frame_data_buffer;
    int frame_data_buflen;

    SrsSource* rtmp_source;
    bool source_copy;

    int totals_frame_count;
    srs_utime_t fps_sample_time;
    int frame_rate;

    int req_kerframe;
    int key_frame_count;
    int kerframe_interal_print;
    u_int8_t opus_payload_type;

    bool audio_enabled;
    std::string audio_format;
 
    std::string record_path;
    bool record_video;
    bool record_auido;
    SrsFileWriter *fw_video;

    SrsMessageQueue* queue;

public:
    SrsRtcRtmpMuxer(SrsRtcConnection* s, SrsRtcPublishStream* p, const SrsContextId& cid);
    virtual ~SrsRtcRtmpMuxer();
public:
    srs_error_t initialize(SrsRequest* request);
// interface ISrsReloadHandler
public:
    virtual srs_error_t on_reload_vhost_play(std::string vhost);
    virtual srs_error_t on_reload_vhost_realtime(std::string vhost);
    virtual const SrsContextId& context_id();
public:
    virtual srs_error_t start();
    virtual void stop();
public:
    virtual srs_error_t cycle();
private:
    srs_error_t on_rtp_packets(SrsRtcStream* source, const std::vector<SrsRtpPacket2*>& pkts);

public:
    srs_error_t write_h264_file(SrsRtpPacket2* pkt, SrsFileWriter *fw);
    srs_error_t write_h264_file_by_jitbuffer(char *buf, int size, SrsFileWriter *fw);
    
    SrsSimpleRtmpClient* get_rtmp_client();
    void rtmp_close();
    std::string stream_url();
    void republish();

private:
    srs_error_t on_rtp_video(SrsRtpPacket2* pkt);
    srs_error_t on_rtp_audio(SrsRtpPacket2* pkt);
    //srs_error_t kickoff_audio_cache(SrsRtpPacket2* pkt, int64_t dts);
private:
    srs_error_t rtmp_connect();
    srs_error_t replace_startcode_with_nalulen(char *video_data, int &size, uint32_t pts, uint32_t dts);
    srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    srs_error_t decode_h264_sps_pps(char *frame, int frame_size, uint32_t pts, uint32_t dts);
    srs_error_t write_h264_ipb_frame(SrsAvcNaluType type, char* frame, int frame_size, uint32_t dts, uint32_t pts);
    srs_error_t write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);
    srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);
    srs_error_t mux_opusflv_rtp(SrsRtpPacket2* pkt, char** flv, int* nb_flv);
    srs_error_t mux_opusflv(SrsRtpPacket2* pkt, char** flv, int* nb_flv);
    srs_error_t rtmp_write_packet_by_source(char type, uint32_t timestamp, char* data, int size);
    void statistics_fps ();
    void request_keyframe(uint32_t ssrc);
};

#endif

