//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HIKVISION_HPP
#define SRS_APP_HIKVISION_HPP

#include <srs_core.hpp>

#ifdef SRS_HIKVISION

#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_protocol_http_stack.hpp>

#include <map>
#include <pthread.h>
#include <string>
#include <vector>

class SrsConfDirective;
class ISrsAppConfig;
class ISrsLiveSourceManager;
class SrsLiveSource;
class ISrsRequest;
class ISrsRawH264Stream;
class ISrsRawHEVCStream;
class ISrsRawAacStream;
class ISrsPithyPrint;
class SrsMediaPacket;
class SrsTsMessage;
class SrsBuffer;
struct SrsRawAacStreamCodec;
class SrsHikvisionDevice;
class SrsHikvisionStream;
class SrsHikvisionMuxer;
class SrsHikvisionManager;
class SrsMpegtsQueue;
class SrsPsContext;
class SrsJsonObject;
class ISrsHttpResponseWriter;
class ISrsHttpMessage;
class ISrsHttpHandler;

// Parse stream name SerialNO_CHANNEL_SUBCHANNEL from the right:
//   last token  = subchannel (0=main, 1=sub, ...)
//   2nd last    = channel (1-based logical channel)
//   remaining   = serialno (may contain underscores)
// Returns true when parsed successfully.
extern bool srs_hikvision_parse_stream(const std::string &stream, std::string &serialno, int &channel, int &subchannel);

// Device credentials from config.
struct SrsHikvisionDeviceConfig {
    std::string serialno_;
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;

    SrsHikvisionDeviceConfig();
};

// Mux Hikvision media into SrsLiveSource (no RTMP loopback — avoids StreamBusy races).
class SrsHikvisionMuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsLiveSourceManager *live_sources_;
    std::string output_;
    std::string stream_;
    ISrsRequest *req_;
    SrsSharedPtr<SrsLiveSource> source_;
    bool publishing_;
    srs_utime_t next_publish_try_;

    // Device timestamps are absolute and huge; normalize to 0-based monotonic ms for players.
    bool has_base_dts_;
    int64_t base_dts_;
    int64_t last_out_dts_;

    ISrsRawH264Stream *avc_;
    std::string h264_sps_;
    bool h264_sps_changed_;
    std::string h264_pps_;
    bool h264_pps_changed_;
    bool h264_sps_pps_sent_;

    ISrsRawHEVCStream *hevc_;
    bool vps_sps_pps_change_;
    std::string h265_vps_;
    std::string h265_sps_;
    std::string h265_pps_;
    bool vps_sps_pps_sent_;

    ISrsRawAacStream *aac_;
    std::string aac_specific_config_;

    ISrsPithyPrint *pprint_;

public:
    SrsHikvisionMuxer();
    virtual ~SrsHikvisionMuxer();

public:
    void setup(std::string output, std::string stream);
    srs_error_t on_ts_message(SrsTsMessage *msg);
    // Direct ES path (from SetESRealPlayCallBack): demux NALUs and publish.
    // is_key_hint: true for I-frame packets from SDK.
    srs_error_t on_es_video(const char *data, int size, uint32_t dts_ms, bool is_key_hint);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_ts_video(SrsTsMessage *msg, SrsBuffer *avs);
    // Demux one NALU from PES payload. Supports Annex-B, AVCC (4-byte length), or raw single NALU.
    srs_error_t demux_h264_nalu(SrsBuffer *avs, char **pframe, int *pnb_frame);
    srs_error_t demux_h265_nalu(SrsBuffer *avs, char **pframe, int *pnb_frame);
    srs_error_t mux_h264(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    srs_error_t write_h264_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts);
    // One FLV/AVC tag with multiple VCL NALUs (one access unit).
    srs_error_t write_h264_ipb_frames(const std::vector<std::pair<char *, int> > &nalus, uint32_t dts, uint32_t pts);
    srs_error_t mux_h265(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t write_h265_vps_sps_pps(uint32_t dts, uint32_t pts);
    srs_error_t write_h265_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts);
    srs_error_t on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs);
    srs_error_t write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts);
    // Normalize device/PS timestamps to 0-based monotonic milliseconds.
    uint32_t correct_timestamp(uint32_t dts_ms);
    srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char *data, int size);
    srs_error_t ensure_publish();
    void close();
};

// ES packet from NET_DVR_SetESRealPlayCallBack (preferred path).
struct SrsHikvisionEsPacket {
    // 0-file head, 1-I, 2-B, 3-P, 10-audio, 11-private (SDK definition)
    int packet_type_;
    uint32_t dts_ms_;
    std::string data_;

    SrsHikvisionEsPacket(int packet_type, uint32_t dts_ms, const char *data, int size);
};

// One on-demand RealPlay session for SerialNO_CHANNEL_SUBCHANNEL.
class SrsHikvisionStream : public ISrsCoroutineHandler, public ISrsPsMessageHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHikvisionDevice *device_;
    std::string stream_name_;
    std::string output_;
    int channel_;
    int subchannel_;
    int ref_count_;
    srs_utime_t last_active_;
    bool stopping_;
    // Prefer structured ES callback over raw PS demux.
    bool use_es_;
    // Once any ES media packet arrives, lock out PS forever for this session
    // (avoids double-publish / 2x frame rate when ES queue is briefly empty).
    bool es_active_;
    int es_log_count_;

    long real_handle_;
    SrsHikvisionMuxer *muxer_;
    SrsPsContext *ps_ctx_;
    ISrsCoroutine *trd_;

    // Cross-thread queue: SDK callback (pthread) -> ST coroutine.
    pthread_mutex_t lock_;
    // PS path: raw stream chunks. ES path: structured frames.
    std::vector<std::string *> ps_packets_;
    std::vector<SrsHikvisionEsPacket *> es_packets_;
    int wake_pipe_[2];
    srs_netfd_t wake_fd_;

public:
    SrsHikvisionStream(SrsHikvisionDevice *device, const std::string &stream_name, int channel, int subchannel);
    virtual ~SrsHikvisionStream();

public:
    srs_error_t start(const std::string &output);
    void stop();
    void add_ref();
    int release_ref();
    int ref_count();
    srs_utime_t last_active();
    std::string stream_name();
    // Called from HCNetSDK worker thread (PS path fallback).
    void on_sdk_data(int data_type, const char *data, int size);
    // Called from ES RealPlay callback thread.
    void on_es_packet(int packet_type, uint32_t dts_ms, const char *data, int size);

    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

    // Interface ISrsPsMessageHandler / ISrsTsHandler (PS fallback only)
public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);
    virtual void on_recover_mode(int nn_recover);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_cycle();
    srs_error_t consume_packets();
    srs_error_t consume_es_packets();
    srs_error_t consume_ps_packets();
    srs_error_t process_es_video(SrsHikvisionEsPacket *pkt);
    void clear_packets();
};

// Per-device login session (shared by channels of the same serialno).
class SrsHikvisionDevice
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHikvisionDeviceConfig conf_;
    long user_id_;
    bool logged_in_;

public:
    SrsHikvisionDevice(const SrsHikvisionDeviceConfig &conf);
    virtual ~SrsHikvisionDevice();

public:
    const SrsHikvisionDeviceConfig &conf() const;
    long user_id() const;
    srs_error_t ensure_login();
    void logout();
    // PTZ with speed. stop=true means stop the command.
    srs_error_t ptz_control(int channel, int command, bool stop, int speed);
    // preset_cmd: SET_PRESET=8, GOTO_PRESET=39
    srs_error_t ptz_preset(int channel, int preset_cmd, int preset_index);
};

// moveDirFlags: [up, right, down, left, zoomin, zoomout]
struct SrsHikvisionPtzSession {
    bool move_dir_flags_[6];
    int last_ptz_command_; // -1 if none
    int last_speed_;
    int ptz_speed_;
    srs_utime_t last_active_;

    SrsHikvisionPtzSession();
};

// Global Hikvision manager: config, SDK lifecycle, on-demand streams, PTZ control.
class SrsHikvisionManager : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    bool sdk_inited_;
    std::map<std::string, SrsHikvisionDevice *> devices_;
    std::map<std::string, SrsHikvisionStream *> streams_;
    // Key: serialno_channel
    std::map<std::string, SrsHikvisionPtzSession *> ptz_sessions_;
    ISrsCoroutine *idle_trd_;

public:
    SrsHikvisionManager();
    virtual ~SrsHikvisionManager();

public:
    srs_error_t initialize();
    void dispose();
    // Called when a player requests stream SerialNO_CHANNEL_SUBCHANNEL.
    srs_error_t on_play(const std::string &stream_name);
    // Called when a player stops.
    void on_stop(const std::string &stream_name);
    bool enabled();

    // Control API (HTTP / future DataChannel). JSON body examples:
    //   {"stream":"SN_1_0","cmd":"ptz","dir":"up","speed":4}
    //   {"stream":"SN_1_0","cmd":"ptz","dir":"stop"}
    //   {"stream":"SN_1_0","cmd":"preset","preset":1}
    //   {"stream":"SN_1_0","cmd":"save_preset","preset":1}
    //   {"serialno":"SN","channel":1,"cmd":"ptz","dir":"left","speed":4}
    // stream_context: optional default stream name when JSON omits stream/serialno
    //                 (e.g. WebRTC session stream when DataChannel is available).
    srs_error_t handle_control_json(const std::string &json, const std::string &stream_context = "");
    srs_error_t handle_control(SrsJsonObject *req, const std::string &stream_context = "");

    // Interface ISrsCoroutineHandler (idle reaper + PTZ auto-stop)
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t load_devices();
    srs_error_t init_sdk();
    void cleanup_sdk();
    SrsHikvisionDevice *find_device(const std::string &serialno);
    SrsHikvisionPtzSession *get_or_create_ptz(const std::string &serialno, int channel);
    std::string ptz_key(const std::string &serialno, int channel);
    srs_error_t apply_ptz_dir(SrsHikvisionDevice *device, int channel, SrsHikvisionPtzSession *sess, const std::string &dir, int speed);
    srs_error_t stop_ptz(SrsHikvisionDevice *device, int channel, SrsHikvisionPtzSession *sess);
    int compute_ptz_command(SrsHikvisionPtzSession *sess);
    void reap_ptz_timeouts();
};

// HTTP API: POST /api/v1/hikvision/control
// Same JSON as handle_control_json. Used until WebRTC DataChannel (SCTP) is available.
class SrsGoApiHikvisionControl : public ISrsHttpHandler
{
public:
    SrsGoApiHikvisionControl();
    virtual ~SrsGoApiHikvisionControl();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// Global manager instance (created when --hikvision=on and config enabled).
extern SrsHikvisionManager *_srs_hikvision;

#endif // SRS_HIKVISION

#endif
