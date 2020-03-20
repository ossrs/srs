/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
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

#ifndef SRS_APP_GB28181_HPP
#define SRS_APP_GB28181_HPP

#include <srs_core.hpp>

#include <arpa/inet.h>
#include <string>
#include <vector>
#include <map>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_listener.hpp>
#include <srs_rtsp_stack.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_log.hpp>
#include <srs_kernel_file.hpp>

class SrsStSocket;
class SrsRtpConn;
class SrsRtspConn;
class SrsRtspStack;
class SrsRtspCaster;
class SrsConfDirective;
class SrsRtpPacket;
class SrsRequest;
class SrsStSocket;
class SrsRtmpClient;
class SrsRawH264Stream;
class SrsRawAacStream;
struct SrsRawAacStreamCodec;
class SrsSharedPtrMessage;
class SrsAudioFrame;
class SrsSimpleStream;
class SrsPithyPrint;
class SrsSimpleRtmpClient;
class SrsSipStack;
class SrsGb28181Caster;
class SrsRtspJitter;
class SrsRtspAudioCache;
class SrsSipRequest;
class SrsGb28181Conn;
class SrsGb28281ClientInfo;

/* gb28181 program stream struct define

*/

struct SrsPsPacketStartCode
{
	uint8_t start_code[3];
	uint8_t stream_id[1];
};

struct SrsPsPacketHeader
{
	SrsPsPacketStartCode start;// 4
	uint8_t info[9];
	uint8_t stuffing_length;
};

struct SrsPsPacketBBHeader
{
	SrsPsPacketStartCode start;
	uint16_t    length;
};

struct SrsPsePacket
{
	SrsPsPacketStartCode     start;
	uint16_t    length;
	uint8_t         info[2];
	uint8_t         stuffing_length;
};

struct SrsPsMapPacket
{
	SrsPsPacketStartCode  start;
	uint16_t length;
};



class SrsPsRtpPacket: public SrsRtpPacket
{
public:
    SrsPsRtpPacket();
    virtual ~SrsPsRtpPacket();
public:
    virtual srs_error_t decode(SrsBuffer* stream);
};

// A rtp connection which transport a stream.
class SrsPsRtpConn: public ISrsUdpHandler
{
private:
    SrsPithyPrint* pprint;
    SrsUdpListener* listener;
    SrsGb28181Conn* gb28181;
    SrsPsRtpPacket* cache;
    std::map<uint32_t, SrsSimpleStream*> cache_payload;
    std::string session_id;
    int _port;
    uint32_t pre_timestamp;

    SrsFileWriter ps_fw;
    SrsFileWriter video_fw;
    SrsFileWriter audio_fw;

    bool first_keyframe_flag;
    bool wait_first_keyframe;
    bool audio_enable;
  
public:
    SrsPsRtpConn(SrsGb28181Conn* r, int p, std::string sid, bool a, bool k);
    virtual ~SrsPsRtpConn();

private:
   int64_t parse_ps_timestamp(const uint8_t* p);

private:
    bool can_send_ps_av_packet();
    void dispose();
public:
    virtual int port();
    virtual srs_error_t listen();
// Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
    virtual srs_error_t on_ps_stream(char* ps_data, int ps_size, uint32_t timestamp);
};

class SrsGb28281ClientInfo {
public:
    SrsGb28281ClientInfo();
    virtual ~SrsGb28281ClientInfo();

public:
    sockaddr* sock_from;
    int sock_fromlen;
    srs_netfd_t stfd;
    SrsSipRequest *req;
};

enum Srs28181CtrlStatusType{
     Srs28181Unkonw = 0,
     Srs28181RegisterOk = 1,
     Srs28181AliveOk = 2,
     Srs28181InviteOk = 3,
     Srs28181Trying = 4,
     Srs28181Bye = 5,
};

class SrsGb28181Conn : public ISrsCoroutineHandler, public ISrsConnection
{
private:
    std::string output_template;
    SrsPithyPrint* pprint;
public:
    Srs28181CtrlStatusType register_status;
    Srs28181CtrlStatusType alive_status;
    Srs28181CtrlStatusType invite_status;
    srs_utime_t register_time;
    srs_utime_t alive_time;
    srs_utime_t invite_time;
    srs_utime_t recv_rtp_time;

    std::string rtmp_url;
    int reg_expires;

private:
    std::string session_id;
    // video stream.
    int video_id;
    std::string video_codec;
    SrsPsRtpConn* video_rtp;
    // audio stream.
    int audio_id;
    std::string audio_codec;
    int audio_sample_rate;
    int audio_channel;
    SrsPsRtpConn* audio_rtp;
public:
    SrsGb28281ClientInfo* info;
private:
    SrsStSocket* skt;
    SrsSipStack* sip;
    SrsGb28181Caster* caster;
    SrsCoroutine* trd;
private:
    SrsSipRequest* req;
    SrsSimpleRtmpClient* sdk;
    SrsRtspJitter* vjitter;
    SrsRtspJitter* ajitter;
private:
    SrsRawH264Stream* avc;
    std::string h264_sps;
    std::string h264_pps;
    bool h264_sps_changed;
    bool h264_pps_changed;
    bool h264_sps_pps_sent;
private:
    SrsRawAacStream* aac;
    std::string aac_specific_config;
public:
    SrsGb28181Conn(SrsGb28181Caster* c, std::string id);
    virtual ~SrsGb28181Conn();
public:
    virtual srs_error_t serve();
    virtual std::string remote_ip();
    virtual void set_request_info(SrsSipRequest *req);
    virtual std::string get_session_id();
private:
    virtual srs_error_t do_cycle();
// internal methods
public:
    virtual srs_error_t start_rtp_listen(int port);
    virtual srs_error_t stop_rtp_listen();
// Interface ISrsOneCycleThreadHandler
public:
    virtual srs_error_t cycle();
public:
    virtual srs_error_t on_rtp_video(SrsSimpleStream* stream, int64_t dts, int keyframe);
    virtual srs_error_t on_rtp_audio(SrsSimpleStream* stream, int64_t dts);
private:
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts);
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char* data, int size);
private:
    // Connect to RTMP server.
    virtual srs_error_t connect();
    // Close the connection to RTMP server.
    virtual void close();
};

class SrsGb28181Config
{
public:
    std::string sip_host;
    std::string sip_port;
    std::string sip_serial;
    std::string sip_realm;
    int sip_ack_timeout;
    int sip_keepalive_timeout;
    int rtp_idle_timeout;
    bool audio_enable;
    std::string output;
    int rtp_port_min;
    int rtp_port_max;
    int listen_port;
    bool print_sip_message;
    bool wait_keyframe;
public:
    SrsGb28181Config(SrsConfDirective* c);
    virtual ~SrsGb28181Config();
};

//gb28181 conn manager
class SrsGb28181Caster : public ISrsUdpHandler
{
private:
    SrsGb28181Config *config;
    // The key: port, value: whether used.
    std::map<int, bool> used_ports;
    SrsSipStack *sip;
    srs_netfd_t lfd;
private:
    std::map<std::string, SrsGb28181Conn*> clients;
    SrsCoroutineManager* manager;

public:
    SrsGb28181Caster(SrsConfDirective* c);
    virtual ~SrsGb28181Caster();

private:
    srs_error_t fetch_or_create(SrsSipRequest* r, SrsGb28181Conn** gb28181);
    virtual SrsGb28181Conn* fetch(const SrsSipRequest* r);
    virtual void destroy();
public:
    // Alloc a rtp port from local ports pool.
    // @param pport output the rtp port.
    virtual srs_error_t alloc_port(int* pport);
    // Free the alloced rtp port.
    virtual void free_port(int lpmin, int lpmax);
    virtual srs_error_t initialize();

    virtual void set_stfd(srs_netfd_t fd);
    virtual SrsGb28181Config GetGb28181Config();

// Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf);
private:
    virtual srs_error_t on_udp_bytes(std::string host, int port, char* buf, int nb_buf, sockaddr* from, int fromlen);
// internal methods.
public:
    virtual srs_error_t send_message(sockaddr* f, int l, std::stringstream& ss);
    virtual srs_error_t send_bye(SrsSipRequest *req, sockaddr *f, int l);
    virtual srs_error_t send_ack(SrsSipRequest *req, sockaddr *f, int l);
    virtual srs_error_t send_invite(SrsSipRequest *req, sockaddr *f, int l, int port);
    virtual srs_error_t send_status(SrsSipRequest *req, sockaddr *f, int l);
    virtual void remove(SrsGb28181Conn* conn);
};

#endif

