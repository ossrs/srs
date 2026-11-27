//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_CONFIG_HPP
#define SRS_APP_CONFIG_HPP

#include <srs_core.hpp>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <srs_app_async_call.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_factory.hpp>

class ISrsRequest;
class SrsFileWriter;
class SrsJsonObject;
class SrsJsonArray;
class SrsJsonAny;

class SrsConfig;
class SrsJsonArray;
class SrsConfDirective;
class ISrsRand;

template <typename T>
bool srs_vector_actual_equals(const std::vector<T> &a, const std::vector<T> &b)
{
    for (int i = 0; i < (int)a.size(); i++) {
        const T &e = a.at(i);
        if (std::find(b.begin(), b.end(), e) == b.end()) {
            return false;
        }
    }

    for (int i = 0; i < (int)b.size(); i++) {
        const T &e = b.at(i);
        if (std::find(a.begin(), a.end(), e) == a.end()) {
            return false;
        }
    }

    return true;
}

namespace srs_internal
{
class SrsConfigBuffer
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    char *last_;
    char *end_;
    char *start_;

public:
    char *pos_;
    int line_;

public:
    SrsConfigBuffer();
    virtual ~SrsConfigBuffer();

public:
    virtual srs_error_t fullfill(const char *filename);
    virtual bool empty();
};
}; // namespace srs_internal

extern bool srs_directive_equals(SrsConfDirective *a, SrsConfDirective *b);
extern bool srs_directive_equals(SrsConfDirective *a, SrsConfDirective *b, std::string except);

extern bool srs_config_hls_is_on_error_ignore(std::string strategy);
extern bool srs_config_hls_is_on_error_continue(std::string strategy);
extern bool srs_config_ingest_is_file(std::string type);
extern bool srs_config_ingest_is_stream(std::string type);
extern bool srs_config_dvr_is_plan_segment(std::string plan);
extern bool srs_config_dvr_is_plan_session(std::string plan);
extern bool srs_stream_caster_is_udp(std::string caster);
extern bool srs_stream_caster_is_gb28181(std::string caster);
extern bool srs_config_apply_filter(SrsConfDirective *dvr_apply, ISrsRequest *req);

extern std::string srs_config_bool2switch(std::string sbool);

// Older SRS versions wrote some vhost directives in another layout, such as SRS2's
// mr { enabled on; latency 350; } for SRS3's publish { mr on; mr_latency 350; },
// so every loaded config is transformed to the current layout.
extern srs_error_t srs_config_transform_vhost(SrsConfDirective *root);

// The cgroup file is the init process's, such as /proc/1/cgroup.
extern srs_error_t srs_detect_docker(std::string cgroup_file);

extern SrsConfig *_srs_config;

// A directive has a name, args and child directives. For example:
//      vhost vhost.ossrs.net {
//       enabled         on;
//       ingest livestream {
//      	 enabled      on;
//      	 ffmpeg       /bin/ffmpeg;
//       }
//      }
// is parsed to:
//      SrsConfDirective: name="vhost", arg0="vhost.ossrs.net", child-directives=[
//          SrsConfDirective: name="enabled", arg0="on", child-directives=[]
//          SrsConfDirective: name="ingest", arg0="livestream", child-directives=[
//              SrsConfDirective: name="enabled", arg0="on", child-directives=[]
//              SrsConfDirective: name="ffmpeg", arg0="/bin/ffmpeg", child-directives=[]
//          ]
//      ]
// An empty block such as "dir0 {}" is allowed; an empty name such as ";" or "{dir0 arg0;}" is not.
class SrsConfDirective
{
public:
    int conf_line_;
    std::string name_;
    std::vector<std::string> args_;
    std::vector<SrsConfDirective *> directives_;

public:
    SrsConfDirective();
    virtual ~SrsConfDirective();

public:
    virtual SrsConfDirective *copy();
    virtual SrsConfDirective *copy(std::string except);

public:
    virtual std::string arg0();
    virtual std::string arg1();
    virtual std::string arg2();
    virtual std::string arg3();

public:
    virtual SrsConfDirective *at(int index);
    virtual SrsConfDirective *get(std::string name);
    virtual SrsConfDirective *get(std::string name, std::string arg0);

public:
    virtual SrsConfDirective *get_or_create(std::string n);
    virtual SrsConfDirective *get_or_create(std::string n, std::string a0);
    virtual SrsConfDirective *get_or_create(std::string n, std::string a0, std::string a1);
    virtual SrsConfDirective *set_arg0(std::string a0);
    // The caller frees v.
    virtual void remove(SrsConfDirective *v);

public:
    virtual bool is_vhost();
    virtual bool is_stream_caster();

public:
    virtual srs_error_t parse(srs_internal::SrsConfigBuffer *buffer, SrsConfig *conf = NULL);
    // The root is level 0, its directives level 1, and so on.
    virtual srs_error_t persistence(SrsFileWriter *writer, int level);
    virtual SrsJsonArray *dumps_args();
    virtual SrsJsonAny *dumps_arg0_to_str();
    virtual SrsJsonAny *dumps_arg0_to_integer();
    virtual SrsJsonAny *dumps_arg0_to_number();
    virtual SrsJsonAny *dumps_arg0_to_boolean();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    enum SrsDirectiveContext {
        SrsDirectiveContextFile,
        SrsDirectiveContextBlock,
    };
    enum SrsDirectiveState {
        SrsDirectiveStateInit,
        // ';' found.
        SrsDirectiveStateEntire,
        // '{' found.
        SrsDirectiveStateBlockStart,
        // '}' found.
        SrsDirectiveStateBlockEnd,
        SrsDirectiveStateEOF,
    };
    virtual srs_error_t parse_conf(srs_internal::SrsConfigBuffer *buffer, SrsDirectiveContext ctx, SrsConfig *conf);
    virtual srs_error_t read_token(srs_internal::SrsConfigBuffer *buffer, std::vector<std::string> &args, int &line_start, SrsDirectiveState &state);
};

enum SrsReloadState {
    SrsReloadStateInit = 0,
    SrsReloadStateParsing = 10,
    SrsReloadStateTransforming = 20,
    SrsReloadStateApplying = 30,
    SrsReloadStateFinished = 90,
};

class ISrsReloadStatus
{
public:
    ISrsReloadStatus();
    virtual ~ISrsReloadStatus();

public:
    virtual void reset() = 0;
    // Keeps a copy of err.
    virtual void update(SrsReloadState state, srs_error_t err) = 0;
    virtual SrsReloadState state() = 0;
    // Still owned by the status.
    virtual srs_error_t error() = 0;
    virtual std::string id() = 0;
};

class SrsReloadStatus : public ISrsReloadStatus
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsReloadState state_;
    srs_error_t err_;
    std::string id_;
    ISrsRand *rand_;

public:
    SrsReloadStatus();
    virtual ~SrsReloadStatus();

public:
    virtual void reset();
    virtual void update(SrsReloadState state, srs_error_t err);
    virtual SrsReloadState state();
    virtual srs_error_t error();
    virtual std::string id();
};

class ISrsAppConfig : public ISrsConfig
{
public:
    ISrsAppConfig();
    virtual ~ISrsAppConfig();

public:
    virtual void subscribe(ISrsReloadHandler *handler) = 0;
    virtual void unsubscribe(ISrsReloadHandler *handler) = 0;
    virtual srs_error_t reload(SrsReloadState *pstate) = 0;
    virtual srs_error_t persistence() = 0;
    virtual std::string config() = 0;
    virtual SrsConfDirective *get_root() = 0;
    virtual std::string cwd() = 0;
    virtual std::string argv() = 0;

public:
    virtual int get_max_connections() = 0;
    virtual std::string get_pid_file() = 0;
    virtual std::string get_server_id() = 0;
    virtual bool empty_ip_ok() = 0;
    virtual srs_utime_t get_grace_start_wait() = 0;
    virtual srs_utime_t get_grace_final_wait() = 0;
    virtual bool is_force_grace_quit() = 0;
    virtual bool inotify_auto_reload() = 0;
    virtual bool auto_reload_for_docker() = 0;

public:
    virtual std::vector<std::string> get_listens() = 0;
    virtual bool get_rtmps_enabled() = 0;
    virtual std::vector<std::string> get_rtmps_listen() = 0;

public:
    virtual bool get_http_api_enabled() = 0;
    virtual std::vector<std::string> get_http_api_listens() = 0;
    virtual bool get_https_api_enabled() = 0;
    virtual std::vector<std::string> get_https_api_listens() = 0;
    virtual std::string get_https_api_ssl_key() = 0;
    virtual std::string get_https_api_ssl_cert() = 0;
    virtual bool get_http_api_crossdomain() = 0;
    virtual bool get_raw_api() = 0;
    virtual bool get_raw_api_allow_reload() = 0;
    virtual bool get_raw_api_allow_query() = 0;
    virtual bool get_raw_api_allow_update() = 0;
    virtual bool get_http_api_auth_enabled() = 0;
    virtual std::string get_http_api_auth_type() = 0;
    virtual std::string get_http_api_auth_token() = 0;
    virtual bool get_http_api_auth_rtc_bearer_enabled() = 0;
    virtual std::string get_http_api_auth_username() = 0;
    virtual std::string get_http_api_auth_password() = 0;
    virtual srs_error_t raw_to_json(SrsJsonObject *obj) = 0;

public:
    virtual bool get_http_stream_enabled() = 0;
    virtual std::vector<std::string> get_http_stream_listens() = 0;
    virtual bool get_https_stream_enabled() = 0;
    virtual std::vector<std::string> get_https_stream_listens() = 0;
    virtual std::string get_https_stream_ssl_key() = 0;
    virtual std::string get_https_stream_ssl_cert() = 0;
    virtual std::string get_http_stream_dir() = 0;
    virtual bool get_http_stream_crossdomain() = 0;
    virtual bool get_vhost_http_enabled(std::string vhost) = 0;
    virtual std::string get_vhost_http_mount(std::string vhost) = 0;
    virtual std::string get_vhost_http_dir(std::string vhost) = 0;

public:
    virtual bool get_rtc_server_enabled() = 0;
    virtual bool get_rtc_server_tcp_enabled() = 0;
    virtual std::vector<std::string> get_rtc_server_tcp_listens() = 0;
    virtual std::string get_rtc_server_protocol() = 0;
    virtual std::vector<std::string> get_rtc_server_listens() = 0;
    virtual int get_rtc_server_reuseport() = 0;
    virtual bool get_rtc_server_encrypt() = 0;
    virtual bool get_rtc_server_ecdsa() = 0;
    virtual bool get_rtc_server_black_hole() = 0;
    virtual std::string get_rtc_server_black_hole_addr() = 0;
    virtual bool get_api_as_candidates() = 0;
    virtual bool get_resolve_api_domain() = 0;
    virtual bool get_keep_api_domain() = 0;
    virtual std::string get_rtc_server_candidates() = 0;
    virtual bool get_use_auto_detect_network_ip() = 0;
    virtual std::string get_rtc_server_ip_family() = 0;

public:
    virtual bool get_rtsp_server_enabled() = 0;
    virtual std::vector<std::string> get_rtsp_server_listens() = 0;

public:
    virtual std::vector<std::string> get_srt_listens() = 0;
    virtual bool get_srt_bonding() = 0;
    virtual int64_t get_srto_maxbw() = 0;
    virtual int get_srto_mss() = 0;
    virtual bool get_srto_tsbpdmode() = 0;
    virtual int get_srto_latency() = 0;
    virtual int get_srto_recv_latency() = 0;
    virtual int get_srto_peer_latency() = 0;
    virtual bool get_srto_tlpktdrop() = 0;
    virtual srs_utime_t get_srto_conntimeout() = 0;
    virtual srs_utime_t get_srto_peeridletimeout() = 0;
    virtual int get_srto_sendbuf() = 0;
    virtual int get_srto_recvbuf() = 0;
    virtual int get_srto_payloadsize() = 0;
    virtual std::string get_srto_passphrase() = 0;
    virtual int get_srto_pbkeylen() = 0;

public:
    virtual std::vector<SrsConfDirective *> get_stream_casters() = 0;
    virtual bool get_stream_caster_enabled(SrsConfDirective *conf) = 0;
    virtual std::string get_stream_caster_engine(SrsConfDirective *conf) = 0;
    virtual std::string get_stream_caster_output(SrsConfDirective *conf) = 0;
    virtual int get_stream_caster_listen(SrsConfDirective *conf) = 0;
    virtual srs_utime_t get_stream_caster_media_connect_timeout(SrsConfDirective *conf) = 0;

public:
    virtual bool get_exporter_enabled() = 0;
    virtual std::string get_exporter_listen() = 0;
    virtual std::string get_exporter_label() = 0;
    virtual std::string get_exporter_tag() = 0;

public:
    virtual bool get_stats_enabled() = 0;
    virtual int get_stats_network() = 0;

public:
    virtual bool get_heartbeat_enabled() = 0;
    virtual srs_utime_t get_heartbeat_interval() = 0;
    virtual std::string get_heartbeat_url() = 0;
    virtual std::string get_heartbeat_device_id() = 0;
    virtual bool get_heartbeat_summaries() = 0;
    virtual bool get_heartbeat_ports() = 0;
    virtual bool get_heartbeat_auth_enabled() = 0;
    virtual std::string get_heartbeat_auth_type() = 0;
    virtual std::string get_heartbeat_auth_token() = 0;

public:
    virtual bool get_circuit_breaker() = 0;
    virtual int get_high_threshold() = 0;
    virtual int get_high_pulse() = 0;
    virtual int get_critical_threshold() = 0;
    virtual int get_critical_pulse() = 0;
    virtual int get_dying_threshold() = 0;
    virtual int get_dying_pulse() = 0;

public:
    virtual std::string get_rtmps_ssl_cert() = 0;
    virtual std::string get_rtmps_ssl_key() = 0;

public:
    virtual void get_vhosts(std::vector<SrsConfDirective *> &vhosts) = 0;
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true) = 0;
    virtual bool get_vhost_enabled(std::string vhost) = 0;
    virtual bool get_vhost_enabled(SrsConfDirective *conf) = 0;
    virtual bool get_debug_srs_upnode(std::string vhost) = 0;
    virtual int get_out_ack_size(std::string vhost) = 0;
    virtual int get_in_ack_size(std::string vhost) = 0;
    virtual int get_chunk_size(std::string vhost) = 0;
    virtual bool get_gop_cache(std::string vhost) = 0;
    virtual int get_gop_cache_max_frames(std::string vhost) = 0;
    virtual bool get_tcp_nodelay(std::string vhost) = 0;
    virtual srs_utime_t get_mw_sleep(std::string vhost, bool is_rtc = false) = 0;
    virtual srs_utime_t get_send_min_interval(std::string vhost) = 0;
    virtual bool get_mr_enabled(std::string vhost) = 0;
    virtual srs_utime_t get_mr_sleep(std::string vhost) = 0;
    virtual srs_utime_t get_publish_1stpkt_timeout(std::string vhost) = 0;
    virtual srs_utime_t get_publish_normal_timeout(std::string vhost) = 0;
    virtual srs_utime_t get_publish_kickoff_for_idle(std::string vhost) = 0;

public:
    virtual bool get_vhost_origin_cluster(std::string vhost) = 0;
    virtual std::vector<std::string> get_vhost_coworkers(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_edge_origin(std::string vhost) = 0;

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_connect(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_close(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_publish(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost) = 0;

public:
    virtual bool get_rtc_enabled(std::string vhost) = 0;

public:
    virtual bool get_rtsp_enabled(std::string vhost) = 0;

public:
    virtual bool get_rtc_from_rtmp(std::string vhost) = 0;
    virtual bool get_rtsp_from_rtmp(std::string vhost) = 0;

public:
    virtual bool get_rtc_nack_enabled(std::string vhost) = 0;
    virtual bool get_rtc_nack_no_copy(std::string vhost) = 0;
    virtual bool get_rtc_nack_prefer_rtx(std::string vhost) = 0;
    virtual bool get_realtime_enabled(std::string vhost, bool is_rtc) = 0;
    virtual int get_mw_msgs(std::string vhost, bool is_realtime, bool is_rtc) = 0;
    virtual SrsConfDirective *get_vhost_on_unpublish(std::string vhost) = 0;
    virtual int get_rtc_drop_for_pt(std::string vhost) = 0;
    virtual bool get_rtc_twcc_enabled(std::string vhost) = 0;
    virtual int get_rtc_aac_bitrate(std::string vhost) = 0;
    virtual bool get_rtc_init_rate_from_sdp(std::string vhost) = 0;
    virtual bool get_rtc_keep_original_ssrc(std::string vhost) = 0;
    virtual bool get_srt_enabled() = 0;
    virtual bool get_srt_enabled(std::string vhost) = 0;
    virtual std::string get_srt_default_streamid() = 0;
    virtual std::string get_srt_default_mode() = 0;
    virtual bool get_srt_to_rtmp(std::string vhost) = 0;
    virtual bool get_rtc_to_rtmp(std::string vhost) = 0;
    virtual srs_utime_t get_rtc_stun_timeout(std::string vhost) = 0;
    virtual bool get_rtc_stun_strict_check(std::string vhost) = 0;
    virtual std::string get_rtc_dtls_role(std::string vhost) = 0;
    virtual std::string get_rtc_dtls_version(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_hls(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_hls_notify(std::string vhost) = 0;
    virtual bool get_hls_enabled(std::string vhost) = 0;
    virtual bool get_hls_enabled(SrsConfDirective *vhost) = 0;
    virtual bool get_hls_use_fmp4(std::string vhost) = 0;
    virtual std::string get_hls_entry_prefix(std::string vhost) = 0;
    virtual std::string get_hls_path(std::string vhost) = 0;
    virtual std::string get_hls_m3u8_file(std::string vhost) = 0;
    virtual std::string get_hls_ts_file(std::string vhost) = 0;
    virtual std::string get_hls_fmp4_file(std::string vhost) = 0;
    virtual std::string get_hls_init_file(std::string vhost) = 0;
    virtual bool get_hls_ts_floor(std::string vhost) = 0;
    virtual srs_utime_t get_hls_fragment(std::string vhost) = 0;
    virtual double get_hls_td_ratio(std::string vhost) = 0;
    virtual double get_hls_aof_ratio(std::string vhost) = 0;
    virtual srs_utime_t get_hls_window(std::string vhost) = 0;
    virtual std::string get_hls_on_error(std::string vhost) = 0;
    virtual bool get_hls_cleanup(std::string vhost) = 0;
    virtual srs_utime_t get_hls_dispose(std::string vhost) = 0;
    virtual bool get_hls_wait_keyframe(std::string vhost) = 0;
    virtual bool get_hls_keys(std::string vhost) = 0;
    virtual int get_hls_fragments_per_key(std::string vhost) = 0;
    virtual std::string get_hls_key_file(std::string vhost) = 0;
    virtual std::string get_hls_key_file_path(std::string vhost) = 0;
    virtual std::string get_hls_key_url(std::string vhost) = 0;
    virtual int get_vhost_hls_nb_notify(std::string vhost) = 0;
    virtual bool get_vhost_hls_dts_directly(std::string vhost) = 0;
    virtual bool get_hls_ctx_enabled(std::string vhost) = 0;
    virtual bool get_hls_ts_ctx_enabled(std::string vhost) = 0;
    virtual bool get_hls_master_m3u8_path_relative(std::string vhost) = 0;
    virtual bool get_hls_recover(std::string vhost) = 0;
    virtual bool get_dash_enabled(std::string vhost) = 0;
    virtual bool get_dash_enabled(SrsConfDirective *vhost) = 0;
    virtual srs_utime_t get_dash_fragment(std::string vhost) = 0;
    virtual srs_utime_t get_dash_update_period(std::string vhost) = 0;
    virtual srs_utime_t get_dash_timeshift(std::string vhost) = 0;
    virtual std::string get_dash_path(std::string vhost) = 0;
    virtual std::string get_dash_mpd_file(std::string vhost) = 0;
    virtual int get_dash_window_size(std::string vhost) = 0;
    virtual bool get_dash_cleanup(std::string vhost) = 0;
    virtual srs_utime_t get_dash_dispose(std::string vhost) = 0;
    virtual bool get_forward_enabled(std::string vhost) = 0;
    virtual SrsConfDirective *get_forwards(std::string vhost) = 0;
    virtual srs_utime_t get_queue_length(std::string vhost) = 0;
    virtual SrsConfDirective *get_forward_backend(std::string vhost) = 0;
    virtual bool get_atc(std::string vhost) = 0;
    virtual int get_time_jitter(std::string vhost) = 0;
    virtual bool get_mix_correct(std::string vhost) = 0;
    virtual bool try_annexb_first(std::string vhost) = 0;
    virtual bool get_rtc_keep_bframe(std::string vhost) = 0;
    virtual bool get_rtc_keep_avc_nalu_sei(std::string vhost) = 0;
    virtual bool get_rtc_server_merge_nalus() = 0;
    virtual srs_utime_t get_rtc_pli_for_rtmp(std::string vhost) = 0;
    virtual int get_rtc_opus_bitrate(std::string vhost) = 0;
    virtual bool get_vhost_is_edge(std::string vhost) = 0;
    virtual bool get_reduce_sequence_header(std::string vhost) = 0;
    virtual bool get_parse_sps(std::string vhost) = 0;

public:
    virtual std::string get_dvr_path(std::string vhost) = 0;
    virtual int get_dvr_time_jitter(std::string vhost) = 0;
    virtual bool get_dvr_wait_keyframe(std::string vhost) = 0;

public:
    virtual bool get_vhost_http_remux_enabled(std::string vhost) = 0;
    virtual bool get_vhost_http_remux_enabled(SrsConfDirective *vhost) = 0;
    virtual srs_utime_t get_vhost_http_remux_fast_cache(std::string vhost) = 0;
    virtual bool get_vhost_http_remux_drop_if_not_match(std::string vhost) = 0;
    virtual bool get_vhost_http_remux_has_audio(std::string vhost) = 0;
    virtual bool get_vhost_http_remux_has_video(std::string vhost) = 0;
    virtual bool get_vhost_http_remux_guess_has_av(std::string vhost) = 0;
    virtual std::string get_vhost_http_remux_mount(std::string vhost) = 0;

public:
    virtual std::string get_vhost_edge_protocol(std::string vhost) = 0;
    virtual bool get_vhost_edge_follow_client(std::string vhost) = 0;
    virtual std::string get_vhost_edge_transform_vhost(std::string vhost) = 0;
    virtual SrsConfDirective *get_vhost_on_dvr(std::string vhost) = 0;
    virtual std::string get_dvr_plan(std::string vhost) = 0;
    virtual bool get_dvr_enabled(std::string vhost) = 0;
    virtual SrsConfDirective *get_dvr_apply(std::string vhost) = 0;
    virtual srs_utime_t get_dvr_duration(std::string vhost) = 0;

public:
    virtual bool get_exec_enabled(std::string vhost) = 0;
    virtual std::vector<SrsConfDirective *> get_exec_publishs(std::string vhost) = 0;

public:
    virtual std::vector<SrsConfDirective *> get_ingesters(std::string vhost) = 0;
    virtual SrsConfDirective *get_ingest_by_id(std::string vhost, std::string ingest_id) = 0;
    virtual bool get_ingest_enabled(SrsConfDirective *conf) = 0;
    virtual std::string get_ingest_ffmpeg(SrsConfDirective *conf) = 0;
    virtual std::string get_ingest_input_type(SrsConfDirective *conf) = 0;
    virtual std::string get_ingest_input_url(SrsConfDirective *conf) = 0;

public:
    virtual bool get_ff_log_enabled() = 0;
    virtual std::string get_ff_log_dir() = 0;
    virtual std::string get_ff_log_level() = 0;

public:
    virtual SrsConfDirective *get_transcode(std::string vhost, std::string scope) = 0;
    virtual bool get_transcode_enabled(SrsConfDirective *conf) = 0;
    virtual std::string get_transcode_ffmpeg(SrsConfDirective *conf) = 0;
    virtual std::vector<SrsConfDirective *> get_transcode_engines(SrsConfDirective *conf) = 0;
    virtual bool get_engine_enabled(SrsConfDirective *conf) = 0;
    virtual std::vector<std::string> get_engine_perfile(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_iformat(SrsConfDirective *conf) = 0;
    virtual std::vector<std::string> get_engine_vfilter(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_vcodec(SrsConfDirective *conf) = 0;
    virtual int get_engine_vbitrate(SrsConfDirective *conf) = 0;
    virtual double get_engine_vfps(SrsConfDirective *conf) = 0;
    virtual int get_engine_vwidth(SrsConfDirective *conf) = 0;
    virtual int get_engine_vheight(SrsConfDirective *conf) = 0;
    virtual int get_engine_vthreads(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_vprofile(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_vpreset(SrsConfDirective *conf) = 0;
    virtual std::vector<std::string> get_engine_vparams(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_acodec(SrsConfDirective *conf) = 0;
    virtual int get_engine_abitrate(SrsConfDirective *conf) = 0;
    virtual int get_engine_asample_rate(SrsConfDirective *conf) = 0;
    virtual int get_engine_achannels(SrsConfDirective *conf) = 0;
    virtual std::vector<std::string> get_engine_aparams(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_oformat(SrsConfDirective *conf) = 0;
    virtual std::string get_engine_output(SrsConfDirective *conf) = 0;
    virtual bool get_security_enabled(std::string vhost) = 0;
    virtual SrsConfDirective *get_security_rules(std::string vhost) = 0;
    virtual bool get_log_tank_file() = 0;
    virtual std::string get_log_file() = 0;
    virtual std::string get_log_level() = 0;
    virtual std::string get_log_level_v2() = 0;
    virtual bool get_utc_time() = 0;
};

// A reload frees the old directives, so never keep a SrsConfDirective* across a coroutine switch.
class SrsConfig : public ISrsAppConfig
{
    friend class SrsConfDirective;
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool show_help_;
    bool test_conf_;
    bool show_version_;
    bool show_signature_;
    // Set by -e or SRS_ENV_ONLY.
    bool env_only_;
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The argv of main joined by spaces.
    std::string argv_;
    std::string cwd_;
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string config_file_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsConfDirective *root_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *env_cache_;
    ISrsRand *rand_;
    std::string default_server_id_;
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsReloadHandler *>
        subscribes_;

public:
    SrsConfig();
    virtual ~SrsConfig();

public:
    virtual void subscribe(ISrsReloadHandler *handler);
    virtual void unsubscribe(ISrsReloadHandler *handler);
    virtual srs_error_t reload(SrsReloadState *pstate);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // Protected so the utest can mock it.
    virtual srs_error_t reload_conf(SrsConfig *conf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
public:
    virtual srs_error_t parse_options(int argc, char **argv);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t parse_config();

public:
    virtual srs_error_t initialize_cwd();
    virtual srs_error_t persistence();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_persistence(SrsFileWriter *fw);

public:
    virtual srs_error_t raw_to_json(SrsJsonObject *obj);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
public:
    virtual std::string
    config();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t parse_argv(int &i, char **argv);
    virtual void print_help(char **argv);

public:
    virtual srs_error_t parse_file(const char *filename);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The src is the config content or a filename.
    virtual srs_error_t build_buffer(std::string src, srs_internal::SrsConfigBuffer **pbuffer);

public:
    virtual srs_error_t check_config();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t check_normal_config();
    virtual srs_error_t check_number_connections();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The caller frees buffer. Protected so the utest can mock it.
    virtual srs_error_t parse_buffer(srs_internal::SrsConfigBuffer *buffer);

public:
    virtual std::string cwd();
    virtual std::string argv();

public:
    virtual SrsConfDirective *get_root();
    // Daemon mode forks twice, so the grandchild is reaped by init.
    virtual bool get_daemon();
    virtual bool get_in_docker();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual bool
    is_full_config();

public:
    virtual std::string get_server_id();
    // Linux limits the connections of each process, so for 10000+ connections, also raise it,
    // such as with "ulimit -HSn 10000".
    virtual int get_max_connections();
    virtual std::vector<std::string> get_listens();
    // The pid file is locked to prevent starting SRS twice, so each SRS instance, such as one per CPU,
    // needs its own pid file.
    virtual std::string get_pid_file();
    // Each RTMP connection prints at most one message per this interval.
    virtual srs_utime_t get_pithy_print();
    virtual bool get_utc_time();
    virtual std::string get_work_dir();
    virtual bool empty_ip_ok();
    virtual srs_utime_t get_grace_start_wait();
    virtual srs_utime_t get_grace_final_wait();
    // A fast quit signal quits gracefully too.
    virtual bool is_force_grace_quit();
    virtual bool disable_daemon_for_docker();
    virtual bool inotify_auto_reload();
    virtual bool auto_reload_for_docker();
    virtual double tcmalloc_release_rate();

public:
    virtual bool get_circuit_breaker();
    virtual int get_high_threshold();
    virtual int get_high_pulse();
    virtual int get_critical_threshold();
    virtual int get_critical_pulse();
    virtual int get_dying_threshold();
    virtual int get_dying_pulse();

public:
    virtual std::vector<SrsConfDirective *> get_stream_casters();
    virtual bool get_stream_caster_enabled(SrsConfDirective *conf);
    virtual std::string get_stream_caster_engine(SrsConfDirective *conf);
    virtual std::string get_stream_caster_output(SrsConfDirective *conf);
    virtual int get_stream_caster_listen(SrsConfDirective *conf);
    // The wait for a GB28181 media connection.
    virtual srs_utime_t get_stream_caster_media_connect_timeout(SrsConfDirective *conf);

public:
    virtual bool get_rtsp_server_enabled();
    virtual bool get_rtsp_server_enabled(SrsConfDirective *conf);
    virtual std::vector<std::string> get_rtsp_server_listens();

public:
    SrsConfDirective *get_rtsp(std::string vhost);
    bool get_rtsp_enabled(std::string vhost);
    bool get_rtsp_from_rtmp(std::string vhost);

public:
    virtual bool get_rtc_server_enabled();
    virtual bool get_rtc_server_enabled(SrsConfDirective *conf);
    virtual std::vector<std::string> get_rtc_server_listens();
    virtual std::string get_rtc_server_candidates();
    virtual bool get_api_as_candidates();
    virtual bool get_resolve_api_domain();
    virtual bool get_keep_api_domain();
    virtual bool get_use_auto_detect_network_ip();
    virtual bool get_rtc_server_tcp_enabled();
    virtual std::vector<std::string> get_rtc_server_tcp_listens();
    virtual std::string get_rtc_server_protocol();
    virtual std::string get_rtc_server_ip_family();
    virtual bool get_rtc_server_ecdsa();
    virtual bool get_rtc_server_encrypt();
    virtual int get_rtc_server_reuseport();
    virtual bool get_rtc_server_merge_nalus();

public:
    virtual bool get_rtc_server_black_hole();
    virtual std::string get_rtc_server_black_hole_addr();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual int get_rtc_server_reuseport2();

public:
    SrsConfDirective *get_rtc(std::string vhost);
    bool get_rtc_enabled(std::string vhost);
    bool get_rtc_keep_bframe(std::string vhost);
    bool get_rtc_keep_avc_nalu_sei(std::string vhost);
    bool get_rtc_from_rtmp(std::string vhost);
    srs_utime_t get_rtc_stun_timeout(std::string vhost);
    bool get_rtc_stun_strict_check(std::string vhost);
    std::string get_rtc_dtls_role(std::string vhost);
    std::string get_rtc_dtls_version(std::string vhost);
    int get_rtc_drop_for_pt(std::string vhost);
    bool get_rtc_to_rtmp(std::string vhost);
    srs_utime_t get_rtc_pli_for_rtmp(std::string vhost);
    bool get_rtc_nack_enabled(std::string vhost);
    bool get_rtc_nack_no_copy(std::string vhost);
    bool get_rtc_nack_prefer_rtx(std::string vhost);
    bool get_rtc_twcc_enabled(std::string vhost);
    int get_rtc_opus_bitrate(std::string vhost);
    int get_rtc_aac_bitrate(std::string vhost);
    bool get_rtc_init_rate_from_sdp(std::string vhost);
    bool get_rtc_keep_original_ssrc(std::string vhost);

public:
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true);
    virtual void get_vhosts(std::vector<SrsConfDirective *> &vhosts);
    virtual bool get_vhost_enabled(std::string vhost);
    virtual bool get_vhost_enabled(SrsConfDirective *conf);
    // The cached last GOP lets players start fast.
    virtual bool get_gop_cache(std::string vhost);
    virtual int get_gop_cache_max_frames(std::string vhost);
    // Some servers, such as Flussonic, do not support debug_srs_upnode.
    // @see https://github.com/ossrs/srs/issues/160
    virtual bool get_debug_srs_upnode(std::string vhost);
    // With ATC, SRS keeps the encoder timestamps and never adjusts them.
    virtual bool get_atc(std::string vhost);
    // Returns a SrsRtmpJitterAlgorithm.
    virtual int get_time_jitter(std::string vhost);
    // Mixes audio and video so the timestamps increase monotonically.
    virtual bool get_mix_correct(std::string vhost);
    virtual srs_utime_t get_queue_length(std::string vhost);
    virtual int get_in_ack_size(std::string vhost);
    virtual int get_out_ack_size(std::string vhost);
    virtual int get_chunk_size(std::string vhost);
    virtual bool get_parse_sps(std::string vhost);
    virtual bool try_annexb_first(std::string vhost);
    virtual bool get_mr_enabled(std::string vhost);
    // TODO: FIXME: add utest for mr config.
    virtual srs_utime_t get_mr_sleep(std::string vhost);
    // TODO: FIXME: add utest for mw config.
    virtual srs_utime_t get_mw_sleep(std::string vhost, bool is_rtc = false);
    // TODO: FIXME: add utest for mw config.
    virtual int get_mw_msgs(std::string vhost, bool is_realtime, bool is_rtc);
    // TODO: FIXME: add utest for min_latency.
    virtual bool get_realtime_enabled(std::string vhost, bool is_rtc);
    virtual bool get_tcp_nodelay(std::string vhost);
    virtual srs_utime_t get_send_min_interval(std::string vhost);
    virtual bool get_reduce_sequence_header(std::string vhost);
    virtual srs_utime_t get_publish_1stpkt_timeout(std::string vhost);
    virtual srs_utime_t get_publish_normal_timeout(std::string vhost);
    virtual srs_utime_t get_publish_kickoff_for_idle(std::string vhost);
    virtual srs_utime_t get_publish_kickoff_for_idle(SrsConfDirective *vhost);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual int
    get_global_chunk_size();

public:
    virtual bool get_forward_enabled(std::string vhost);
    virtual bool get_forward_enabled(SrsConfDirective *vhost);
    virtual SrsConfDirective *get_forwards(std::string vhost);
    virtual SrsConfDirective *get_forward_backend(std::string vhost);

public:
    virtual bool get_srt_enabled();
    virtual std::vector<std::string> get_srt_listens();
    // Whether the SRT listeners accept bonded callers (libsrt socket groups). A restart changes it.
    virtual bool get_srt_bonding();
    virtual int64_t get_srto_maxbw();
    virtual int get_srto_mss();
    virtual bool get_srto_tsbpdmode();
    // SRTO_LATENCY sets both the recv and peer latency. libsrt defaults to 120ms recv latency and 0 peer latency.
    virtual int get_srto_latency();
    virtual int get_srto_recv_latency();
    virtual int get_srto_peer_latency();
    virtual bool get_srt_sei_filter();
    virtual bool get_srto_tlpktdrop();
    virtual srs_utime_t get_srto_conntimeout();
    virtual srs_utime_t get_srto_peeridletimeout();
    virtual int get_srto_sendbuf();
    virtual int get_srto_recvbuf();
    virtual int get_srto_payloadsize();
    virtual std::string get_srto_passphrase();
    virtual int get_srto_pbkeylen();
    virtual std::string get_default_app_name();
    // For a client that sends no streamid.
    virtual std::string get_srt_default_streamid();
    // The mode of a short streamid, "publish" or "request".
    virtual std::string get_srt_default_mode();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *get_srt(std::string vhost);

public:
    // TODO: FIXME: Rename to get_vhost_srt_enabled.
    bool get_srt_enabled(std::string vhost);
    bool get_srt_to_rtmp(std::string vhost);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *
    get_vhost_http_hooks(std::string vhost);

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual bool get_vhost_http_hooks_enabled(SrsConfDirective *vhost);
    // The args of each hook directive are the URLs to call.
    virtual SrsConfDirective *get_vhost_on_connect(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_close(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_publish(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_unpublish(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_dvr(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_hls(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_hls_notify(std::string vhost);

public:
    // An edge proxies each publisher to the upnode, and its players share one connection to the upnode.
    virtual bool get_vhost_is_edge(std::string vhost);
    virtual bool get_vhost_is_edge(SrsConfDirective *conf);
    // Its args are the origin addresses.
    virtual SrsConfDirective *get_vhost_edge_origin(std::string vhost);
    virtual std::string get_vhost_edge_protocol(std::string vhost);
    virtual bool get_vhost_edge_follow_client(std::string vhost);
    virtual std::string get_vhost_edge_transform_vhost(std::string vhost);
    // @see https://ossrs.io/lts/en-us/docs/v7/doc/origin-cluster#legacy
    virtual bool get_vhost_origin_cluster(std::string vhost);
    virtual bool get_vhost_origin_cluster(SrsConfDirective *conf);
    // @see https://ossrs.io/lts/en-us/docs/v7/doc/origin-cluster#legacy
    virtual std::vector<std::string> get_vhost_coworkers(std::string vhost);

public:
    virtual bool get_security_enabled(std::string vhost);
    virtual bool get_security_enabled(SrsConfDirective *vhost);
    virtual SrsConfDirective *get_security_rules(std::string vhost);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *get_security_rules_from_env();

public:
    // The scope is empty, an app or an app/stream; "transcode live {...}" matches the scope "live". See
    // app.transcode.srs.com and stream.transcode.srs.com in full.conf.
    virtual SrsConfDirective *get_transcode(std::string vhost, std::string scope);
    virtual bool get_transcode_enabled(SrsConfDirective *conf);
    virtual std::string get_transcode_ffmpeg(SrsConfDirective *conf);
    virtual std::vector<SrsConfDirective *> get_transcode_engines(SrsConfDirective *conf);
    virtual bool get_engine_enabled(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_perfile(SrsConfDirective *conf);
    virtual std::string get_engine_iformat(SrsConfDirective *conf);
    // Set before the FFmpeg vcodec.
    virtual std::vector<std::string> get_engine_vfilter(SrsConfDirective *conf);
    // Such as vn, copy or libx264.
    virtual std::string get_engine_vcodec(SrsConfDirective *conf);
    // In kbps.
    virtual int get_engine_vbitrate(SrsConfDirective *conf);
    virtual double get_engine_vfps(SrsConfDirective *conf);
    virtual int get_engine_vwidth(SrsConfDirective *conf);
    virtual int get_engine_vheight(SrsConfDirective *conf);
    // The libx264 threads.
    virtual int get_engine_vthreads(SrsConfDirective *conf);
    // The libx264 profile: high, main or baseline.
    virtual std::string get_engine_vprofile(SrsConfDirective *conf);
    // The libx264 preset: ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow or placebo.
    virtual std::string get_engine_vpreset(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_vparams(SrsConfDirective *conf);
    // Such as an, copy or libfdk_aac.
    virtual std::string get_engine_acodec(SrsConfDirective *conf);
    // In kbps.
    virtual int get_engine_abitrate(SrsConfDirective *conf);
    // In Hz, such as 44100.
    virtual int get_engine_asample_rate(SrsConfDirective *conf);
    // 1 for mono, 2 for stereo.
    virtual int get_engine_achannels(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_aparams(SrsConfDirective *conf);
    virtual std::string get_engine_oformat(SrsConfDirective *conf);
    // Such as rtmp://localhost/live/livestream, where variables such as [vhost] are replaced.
    virtual std::string get_engine_output(SrsConfDirective *conf);
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *
    get_exec(std::string vhost);

public:
    virtual bool get_exec_enabled(std::string vhost);
    virtual bool get_exec_enabled(SrsConfDirective *vhost);
    virtual std::vector<SrsConfDirective *> get_exec_publishs(std::string vhost);

public:
    virtual std::vector<SrsConfDirective *> get_ingesters(std::string vhost);
    virtual SrsConfDirective *get_ingest_by_id(std::string vhost, std::string ingest_id);
    virtual bool get_ingest_enabled(SrsConfDirective *conf);
    virtual std::string get_ingest_ffmpeg(SrsConfDirective *conf);
    // File or stream.
    virtual std::string get_ingest_input_type(SrsConfDirective *conf);
    virtual std::string get_ingest_input_url(SrsConfDirective *conf);

public:
    virtual bool get_log_tank_file();
    virtual std::string get_log_level();
    virtual std::string get_log_level_v2();
    virtual std::string get_log_file();
    virtual bool get_ff_log_enabled();
    virtual std::string get_ff_log_dir();
    virtual std::string get_ff_log_level();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *get_dash(std::string vhost);

public:
    virtual bool get_dash_enabled(std::string vhost);
    virtual bool get_dash_enabled(SrsConfDirective *vhost);
    virtual srs_utime_t get_dash_fragment(std::string vhost);
    virtual srs_utime_t get_dash_update_period(std::string vhost);
    virtual srs_utime_t get_dash_timeshift(std::string vhost);
    virtual std::string get_dash_path(std::string vhost);
    virtual std::string get_dash_mpd_file(std::string vhost);
    // The number of segments in the MPD.
    virtual int get_dash_window_size(std::string vhost);
    virtual bool get_dash_cleanup(std::string vhost);
    virtual srs_utime_t get_dash_dispose(std::string vhost);
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *
    get_hls(std::string vhost);

public:
    virtual bool get_hls_enabled(std::string vhost);
    virtual bool get_hls_enabled(SrsConfDirective *vhost);
    virtual bool get_hls_use_fmp4(std::string vhost);
    // The prefix of each segment URL in the m3u8.
    virtual std::string get_hls_entry_prefix(std::string vhost);
    virtual std::string get_hls_path(std::string vhost);
    virtual std::string get_hls_m3u8_file(std::string vhost);
    virtual std::string get_hls_ts_file(std::string vhost);
    virtual std::string get_hls_fmp4_file(std::string vhost);
    virtual std::string get_hls_init_file(std::string vhost);
    // Uses floor(timestamp/hls_fragment), for variable timestamps.
    virtual bool get_hls_ts_floor(std::string vhost);
    virtual srs_utime_t get_hls_fragment(std::string vhost);
    // The target duration ratio.
    virtual double get_hls_td_ratio(std::string vhost);
    // The audio overflow ratio.
    virtual double get_hls_aof_ratio(std::string vhost);
    // The duration of the segments listed in the m3u8.
    virtual srs_utime_t get_hls_window(std::string vhost);
    virtual std::string get_hls_on_error(std::string vhost);
    virtual bool get_hls_cleanup(std::string vhost);
    virtual srs_utime_t get_hls_dispose(std::string vhost);
    virtual bool get_hls_wait_keyframe(std::string vhost);
    // Whether to encrypt the segments.
    virtual bool get_hls_keys(std::string vhost);
    virtual int get_hls_fragments_per_key(std::string vhost);
    virtual std::string get_hls_key_file(std::string vhost);
    virtual std::string get_hls_key_file_path(std::string vhost);
    // The key URL written in the m3u8.
    virtual std::string get_hls_key_url(std::string vhost);
    // The most bytes to read of the on_hls_notify response, before a timeout or an error.
    virtual int get_vhost_hls_nb_notify(std::string vhost);
    // Uses the FLV timestamp as the TS DTS.
    virtual bool get_vhost_hls_dts_directly(std::string vhost);
    virtual bool get_hls_ctx_enabled(std::string vhost);
    // The hls_ctx session for the segment files too: the .ts files, or the .m4s and init.mp4 files of fMP4.
    virtual bool get_hls_ts_ctx_enabled(std::string vhost);
    // The media playlist URL in the master playlist: on is relative, such as "livestream.m3u8?hls_ctx=xxx", and
    // off is absolute, such as "/live/livestream.m3u8?hls_ctx=xxx". Off by default for backward compatibility.
    virtual bool get_hls_master_m3u8_path_relative(std::string vhost);
    // The sequence number continues from where it stopped last time, and old segments are kept.
    virtual bool get_hls_recover(std::string vhost);
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *
    get_dvr(std::string vhost);

public:
    virtual bool get_dvr_enabled(std::string vhost);
    virtual bool get_dvr_enabled(SrsConfDirective *vhost);
    // Check it with srs_config_apply_filter.
    virtual SrsConfDirective *get_dvr_apply(std::string vhost);
    virtual std::string get_dvr_path(std::string vhost);
    virtual std::string get_dvr_plan(std::string vhost);
    virtual srs_utime_t get_dvr_duration(std::string vhost);
    virtual bool get_dvr_wait_keyframe(std::string vhost);
    virtual int get_dvr_time_jitter(std::string vhost);
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual bool
    get_http_api_enabled(SrsConfDirective *conf);

public:
    virtual bool get_http_api_enabled();
    virtual std::vector<std::string> get_http_api_listens();
    virtual bool get_http_api_crossdomain();
    virtual bool get_raw_api();
    virtual bool get_raw_api_allow_reload();
    virtual bool get_raw_api_allow_query();
    virtual bool get_raw_api_allow_update();
    virtual bool get_http_api_auth_enabled();
    virtual std::string get_http_api_auth_type();
    virtual std::string get_http_api_auth_token();
    // Whether Bearer authentication also protects the WebRTC signaling APIs.
    virtual bool get_http_api_auth_rtc_bearer_enabled();
    virtual std::string get_http_api_auth_username();
    virtual std::string get_http_api_auth_password();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *get_https_api();

public:
    virtual bool get_https_api_enabled();
    virtual std::vector<std::string> get_https_api_listens();
    virtual std::string get_https_api_ssl_key();
    virtual std::string get_https_api_ssl_cert();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual bool
    get_http_stream_enabled(SrsConfDirective *conf);

public:
    virtual bool get_http_stream_enabled();
    virtual std::vector<std::string> get_http_stream_listens();
    virtual std::string get_http_stream_dir();
    // For both the HTTP static and stream server.
    virtual bool get_http_stream_crossdomain();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *get_https_stream();

public:
    virtual bool get_https_stream_enabled();
    virtual std::vector<std::string> get_https_stream_listens();
    virtual std::string get_https_stream_ssl_key();
    virtual std::string get_https_stream_ssl_cert();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *get_rtmps();

public:
    virtual bool get_rtmps_enabled();
    virtual std::vector<std::string> get_rtmps_listen();
    virtual std::string get_rtmps_ssl_key();
    virtual std::string get_rtmps_ssl_cert();

public:
    virtual bool get_vhost_http_enabled(std::string vhost);
    // Such as http://vhost/live/livestream
    virtual std::string get_vhost_http_mount(std::string vhost);
    // The disk path served at the mount.
    virtual std::string get_vhost_http_dir(std::string vhost);

public:
    virtual bool get_vhost_http_remux_enabled(std::string vhost);
    virtual bool get_vhost_http_remux_enabled(SrsConfDirective *vhost);
    // For the HTTP audio stream.
    virtual srs_utime_t get_vhost_http_remux_fast_cache(std::string vhost);
    // Whether to drop the packets the FLV header does not announce.
    bool get_vhost_http_remux_drop_if_not_match(std::string vhost);
    bool get_vhost_http_remux_has_audio(std::string vhost);
    bool get_vhost_http_remux_has_video(std::string vhost);
    bool get_vhost_http_remux_guess_has_av(std::string vhost);
    virtual std::string get_vhost_http_remux_mount(std::string vhost);
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *
    get_heartbeat();

public:
    virtual bool get_heartbeat_enabled();
    virtual srs_utime_t get_heartbeat_interval();
    virtual std::string get_heartbeat_url();
    virtual std::string get_heartbeat_device_id();
    // Whether to report the /api/v1/summaries of the HTTP API.
    virtual bool get_heartbeat_summaries();
    virtual bool get_heartbeat_ports();
    virtual bool get_heartbeat_auth_enabled();
    virtual std::string get_heartbeat_auth_type();
    virtual std::string get_heartbeat_auth_token();
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsConfDirective *
    get_stats();

public:
    virtual bool get_stats_enabled();
    // The index of the network device whose IP is the local IP, such as 0 for eth0.
    virtual int get_stats_network();
    virtual SrsConfDirective *get_stats_disk_device();

public:
    // For Prometheus.
    virtual bool get_exporter_enabled();
    virtual std::string get_exporter_listen();
    virtual std::string get_exporter_label();
    virtual std::string get_exporter_tag();
};

#endif
