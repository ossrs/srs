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
#include <set>
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

// Parse stream name from the right:
// Live:     SerialNO_CHANNEL_SUBCHANNEL
//             last = subchannel (0=main, 1=sub, ...), 2nd last = channel
// Playback: SerialNO_CHANNEL_UNIXTS
//             last = unix timestamp (>= 1e9), 2nd last = channel, subchannel defaults 0
//             e.g. G75965391_2_1783880413 -> ch=2 from 2026-...
// Returns true when parsed successfully. start_unix_ts=0 means live RealPlay.
extern bool srs_hikvision_parse_stream(const std::string &stream, std::string &serialno, int &channel, int &subchannel,
                                       int64_t *start_unix_ts = NULL);

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

// ES packet from NET_DVR_SetESRealPlayCallBack / SetPlayBackESCallBack.
struct SrsHikvisionEsPacket {
    // 0-file head, 1-I, 2-B, 3-P, 10-audio, 11-private (SDK definition)
    int packet_type_;
    uint32_t dts_ms_;
    // Absolute wall time from SDK OSD fields (0 if unknown).
    int64_t abs_unix_ts_;
    std::string data_;

    SrsHikvisionEsPacket(int packet_type, uint32_t dts_ms, int64_t abs_unix_ts, const char *data, int size);
};

// One on-demand RealPlay or playback session.
// Live:      SerialNO_CHANNEL_SUBCHANNEL
// Playback:  SerialNO_CHANNEL_UNIXTS
class SrsHikvisionStream : public ISrsCoroutineHandler, public ISrsPsMessageHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHikvisionDevice *device_;
    std::string stream_name_;
    std::string output_;
    int channel_;
    int subchannel_;
    // 0 = live; >0 = playback from this unix timestamp.
    int64_t start_unix_ts_;
    bool is_playback_;
    // Drop frames until OSD time reaches start_unix_ts_ and first I-frame.
    bool pb_aligned_;
    bool pb_need_normal_speed_;
    int pb_skip_count_;
    // Real-time pacing for VOD (SDK often pushes faster than 1x).
    bool pb_pace_inited_;
    int64_t pb_pace_first_dts_ms_;
    srs_utime_t pb_pace_origin_wall_;
    int64_t pb_pace_last_delta_ms_;
    int ref_count_;
    srs_utime_t last_active_;
    bool stopping_;
    // Prefer structured ES callback over raw PS demux.
    bool use_es_;
    // Once any ES media packet arrives, lock out PS forever for this session
    // (avoids double-publish / 2x frame rate when ES queue is briefly empty).
    bool es_active_;
    int es_log_count_;
    int ps_log_count_;
    // Diagnostics: RealPlay may succeed while NVR never sends media (e.g. sub-stream off).
    int64_t nn_es_pkts_;
    int64_t nn_ps_pkts_;
    int64_t nn_sdk_cbs_;
    srs_utime_t stream_start_wall_;
    bool no_media_warned_;

    // RealPlay or PlayBack handle.
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
    SrsHikvisionStream(SrsHikvisionDevice *device, const std::string &stream_name, int channel, int subchannel,
                       int64_t start_unix_ts = 0);
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
    // Called from ES RealPlay / PlayBack callback thread.
    // abs_unix_ts: wall time from packet OSD (0 if unavailable).
    void on_es_packet(int packet_type, uint32_t dts_ms, int64_t abs_unix_ts, const char *data, int size);
    void on_playback_eof();

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
    srs_error_t start_live();
    srs_error_t start_playback();
    // After RealPlay/PlayBack: wait for first media packet or fail fast (no hang).
    srs_error_t wait_first_media(srs_utime_t timeout);
    bool has_media_packets();
    void stop_sdk_handle();
    srs_error_t consume_packets();
    srs_error_t consume_es_packets();
    srs_error_t consume_ps_packets();
    srs_error_t process_es_video(SrsHikvisionEsPacket *pkt);
    bool should_drop_playback_frame(int packet_type, int64_t abs_unix_ts);
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
    // ISAPI StreamingChannel ids present on NVR, e.g. 601=ch6 main, 602=ch6 sub.
    // Empty + channel_info_ok_=false means probe failed (skip pre-check).
    std::set<int> streaming_channel_ids_;
    bool channel_info_loaded_;
    bool channel_info_ok_;
    srs_utime_t channel_info_loaded_at_;

public:
    SrsHikvisionDevice(const SrsHikvisionDeviceConfig &conf);
    virtual ~SrsHikvisionDevice();

public:
    const SrsHikvisionDeviceConfig &conf() const;
    long user_id() const;
    srs_error_t ensure_login();
    void logout();
    // Load /ISAPI/Streaming/channels once (cached). Best-effort; failure is soft.
    srs_error_t ensure_channel_info();
    // Hikvision id = channel*100 + (subchannel+1): 0=main(...01), 1=sub(...02), ...
    // Returns error if ISAPI list is known and id is absent (channel/stream notSupport).
    // If channel info unavailable, returns success (caller may still use media timeout).
    srs_error_t check_stream_available(int channel, int subchannel);
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

// Downlink audio from NVR VoiceCom → WebRTC DataChannel peers.
class ISrsHikvisionTalkListener
{
public:
    ISrsHikvisionTalkListener();
    virtual ~ISrsHikvisionTalkListener();

public:
    // Encoded talk frame (G.711 etc.) from device, ready to send to browser as DC binary.
    virtual void on_talk_downlink(const std::string &talk_key, const char *data, int len) = 0;
};

// One VoiceCom session per device channel (SDK typically allows one talk path).
class SrsHikvisionTalkSession : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHikvisionDevice *device_;
    int channel_;
    int voice_chan_;
    long voice_handle_;
    // NET_DVR_COMPRESSION_AUDIO.byAudioEncType: 1=G711_U, 2=G711_A, 8=PCM, ...
    int audio_enc_type_;
    int sample_rate_hz_;
    bool stopping_;
    srs_utime_t last_active_;
    std::vector<ISrsHikvisionTalkListener *> listeners_;

    ISrsCoroutine *trd_;
    pthread_mutex_t lock_;
    std::vector<std::string *> downlink_;
    int wake_pipe_[2];
    srs_netfd_t wake_fd_;

public:
    SrsHikvisionTalkSession(SrsHikvisionDevice *device, int channel);
    virtual ~SrsHikvisionTalkSession();

public:
    srs_error_t start();
    void stop();
    int audio_enc_type() const;
    int sample_rate_hz() const;
    int channel() const;
    srs_utime_t last_active() const;
    void touch();
    void add_listener(ISrsHikvisionTalkListener *l);
    // Returns remaining listener count.
    int remove_listener(ISrsHikvisionTalkListener *l);
    int listener_count();
    srs_error_t send_uplink(const char *data, int len);
    // Called from HCNetSDK voice callback thread.
    void on_voice_data(const char *data, int size, int audio_flag);

    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t do_cycle();
    void clear_downlink();
};

// Global Hikvision manager: config, SDK lifecycle, on-demand streams, PTZ, talk.
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
    // Key: serialno_channel
    std::map<std::string, SrsHikvisionTalkSession *> talk_sessions_;
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

    // Control API (HTTP / DataChannel). JSON body examples:
    //   {"stream":"SN_1_0","cmd":"ptz","dir":"up","speed":4}   // HTTP: stream required
    //   {"cmd":"ptz","dir":"up"}                                  // DataChannel: stream from RTC play context
    //   {"cmd":"talk","action":"start"}
    //   {"cmd":"search_record","start":1700000000,"end":1700086400}
    // stream_context: default stream for DataChannel (from WebRTC play session).
    // talk_listener: optional DC peer for downlink audio (talk start).
    // out_reply: optional full JSON response (e.g. search results); if empty on success, caller uses generic ok.
    srs_error_t handle_control_json(const std::string &json, const std::string &stream_context = "",
                                    ISrsHikvisionTalkListener *talk_listener = NULL, std::string *out_reply = NULL);
    srs_error_t handle_control(SrsJsonObject *req, const std::string &stream_context = "",
                               ISrsHikvisionTalkListener *talk_listener = NULL, std::string *out_reply = NULL);

    // Binary G.711 (etc.) uplink from browser DataChannel → VoiceComSendData.
    srs_error_t talk_send_uplink(const std::string &stream_context, const char *data, int len);
    // Drop listener from all talk sessions (RTC dispose).
    void talk_remove_listener(ISrsHikvisionTalkListener *listener);

    // Interface ISrsCoroutineHandler (idle reaper + PTZ auto-stop + talk idle)
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
    srs_error_t talk_start(const std::string &serialno, int channel, ISrsHikvisionTalkListener *listener, int *out_codec, int *out_rate);
    srs_error_t talk_stop(const std::string &serialno, int channel, ISrsHikvisionTalkListener *listener);
    void reap_talk_timeouts();
    // NET_DVR_FindFile_V40 recording list. start/end unix seconds (local device clock).
    srs_error_t search_records(SrsHikvisionDevice *device, int channel, int64_t start_unix, int64_t end_unix,
                               int file_type, int stream_type, int max_results, std::string *out_json);
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
