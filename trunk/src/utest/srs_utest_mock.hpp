//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_MOCK_HPP
#define SRS_UTEST_MOCK_HPP

#include <srs_utest.hpp>

#include <map>
#include <vector>

// Include necessary SRS headers for interfaces
#include <srs_app_config.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtmp_conn.hpp>

// Forward declarations
class SrsRtcTrackDescription;
class SrsRtpPacket;

// Helper class to create mock track descriptions for testing
class MockRtcTrackDescriptionFactory
{
public:
    MockRtcTrackDescriptionFactory();
    virtual ~MockRtcTrackDescriptionFactory();

public:
    // Default SSRCs for audio and video tracks
    uint32_t audio_ssrc_;
    uint32_t video_ssrc_;
    uint32_t screen_ssrc_;

public:
    // Create a map of track descriptions with audio and video tracks
    std::map<uint32_t, SrsRtcTrackDescription *> create_audio_video_tracks();

    // Create a single audio track description
    SrsRtcTrackDescription *create_audio_track(uint32_t ssrc, std::string id, std::string mid);

    // Create a single video track description
    SrsRtcTrackDescription *create_video_track(uint32_t ssrc, std::string id, std::string mid);
};

// Mock expire for testing
class MockExpire : public ISrsExpire
{
public:
    bool expired_;

public:
    MockExpire();
    virtual ~MockExpire();

public:
    virtual void expire();
};

// Mock request for testing
class MockRtcAsyncCallRequest : public ISrsRequest
{
public:
    std::string vhost_;
    std::string app_;
    std::string stream_;

public:
    MockRtcAsyncCallRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockRtcAsyncCallRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Mock RTC source manager for testing
class MockRtcSourceManager : public ISrsRtcSourceManager
{
public:
    srs_error_t initialize_error_;
    srs_error_t fetch_or_create_error_;
    int initialize_count_;
    int fetch_or_create_count_;
    SrsSharedPtr<SrsRtcSource> mock_source_;

public:
    MockRtcSourceManager();
    virtual ~MockRtcSourceManager();
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps);
    virtual SrsSharedPtr<SrsRtcSource> fetch(ISrsRequest *r);
    void set_initialize_error(srs_error_t err);
    void set_fetch_or_create_error(srs_error_t err);
    void reset();
};

// Mock statistic for testing
class MockRtcStatistic : public ISrsStatistic
{
public:
    srs_error_t on_client_error_;
    int on_client_count_;
    int on_disconnect_count_;
    std::string last_client_id_;
    ISrsRequest *last_client_req_;
    ISrsExpire *last_client_conn_;
    SrsRtmpConnType last_client_type_;

public:
    MockRtcStatistic();
    virtual ~MockRtcStatistic();
    virtual void on_disconnect(std::string id, srs_error_t err);
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id);
    virtual void on_stream_close(ISrsRequest *req);
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta);
    virtual void kbps_sample();
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames);
    virtual std::string server_id();
    virtual std::string service_id();
    virtual std::string service_pid();
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid);
    virtual SrsStatisticStream *find_stream(std::string sid);
    virtual SrsStatisticStream *find_stream_by_url(std::string url);
    virtual SrsStatisticClient *find_client(std::string client_id);
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr);
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs);
    void set_on_client_error(srs_error_t err);
    void reset();
};

// Mock RTC async task executor for testing
class MockRtcAsyncTaskExecutor : public ISrsExecRtcAsyncTask
{
public:
    srs_error_t exec_error_;
    int exec_count_;
    ISrsAsyncCallTask *last_task_;

public:
    MockRtcAsyncTaskExecutor();
    virtual ~MockRtcAsyncTaskExecutor();

public:
    virtual srs_error_t exec_rtc_async_work(ISrsAsyncCallTask *t);
    void set_exec_error(srs_error_t err);
    void reset();
};

// Mock RTC packet sender for testing
class MockRtcPacketSender : public ISrsRtcPacketSender
{
public:
    srs_error_t send_packet_error_;
    int send_packet_count_;
    SrsRtpPacket *last_sent_packet_;

public:
    MockRtcPacketSender();
    virtual ~MockRtcPacketSender();

public:
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);
    void set_send_packet_error(srs_error_t err);
    void reset();
};

// Mock app config for testing
class MockAppConfig : public ISrsAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_stop_directive_;
    SrsConfDirective *on_unpublish_directive_;
    bool rtc_nack_enabled_;
    bool rtc_nack_no_copy_;
    int rtc_drop_for_pt_;
    bool rtc_twcc_enabled_;
    bool srt_enabled_;
    bool rtc_to_rtmp_;
    srs_utime_t dash_dispose_;
    bool dash_enabled_;
    bool api_as_candidates_;
    bool resolve_api_domain_;
    bool keep_api_domain_;
    int mw_msgs_;

public:
    MockAppConfig();
    virtual ~MockAppConfig();
    // ISrsConfig methods
    virtual srs_utime_t get_pithy_print();
    virtual std::string get_default_app_name();
    virtual void subscribe(ISrsReloadHandler *handler);
    virtual void unsubscribe(ISrsReloadHandler *handler);
    virtual srs_error_t reload(SrsReloadState *pstate) { return srs_success; }
    virtual srs_error_t persistence() { return srs_success; }
    virtual std::string config() { return ""; }
    virtual SrsConfDirective *get_root() { return NULL; }
    virtual std::string cwd() { return "./"; }
    virtual int get_max_connections() { return 1000; }
    virtual std::string get_pid_file() { return ""; }
    virtual bool empty_ip_ok() { return false; }
    virtual bool get_asprocess() { return false; }
    virtual srs_utime_t get_grace_start_wait() { return 0; }
    virtual srs_utime_t get_grace_final_wait() { return 0; }
    virtual bool is_force_grace_quit() { return false; }
    virtual bool inotify_auto_reload() { return false; }
    virtual bool auto_reload_for_docker() { return false; }
    virtual std::vector<std::string> get_listens() { return std::vector<std::string>(); }
    virtual bool get_rtmps_enabled() { return false; }
    virtual std::vector<std::string> get_rtmps_listen() { return std::vector<std::string>(); }
    virtual bool get_http_api_enabled() { return false; }
    virtual std::vector<std::string> get_http_api_listens() { return std::vector<std::string>(); }
    virtual bool get_https_api_enabled() { return false; }
    virtual std::vector<std::string> get_https_api_listens() { return std::vector<std::string>(); }
    virtual std::string get_https_api_ssl_key() { return ""; }
    virtual std::string get_https_api_ssl_cert() { return ""; }
    virtual bool get_raw_api() { return false; }
    virtual bool get_raw_api_allow_reload() { return false; }
    virtual bool get_raw_api_allow_query() { return false; }
    virtual bool get_raw_api_allow_update() { return false; }
    virtual bool get_http_api_auth_enabled() { return false; }
    virtual std::string get_http_api_auth_username() { return ""; }
    virtual std::string get_http_api_auth_password() { return ""; }
    virtual srs_error_t raw_to_json(SrsJsonObject *obj) { return srs_success; }
    virtual bool get_http_stream_enabled() { return false; }
    virtual std::vector<std::string> get_http_stream_listens() { return std::vector<std::string>(); }
    virtual bool get_https_stream_enabled() { return false; }
    virtual std::vector<std::string> get_https_stream_listens() { return std::vector<std::string>(); }
    virtual std::string get_https_stream_ssl_key() { return ""; }
    virtual std::string get_https_stream_ssl_cert() { return ""; }
    virtual std::string get_http_stream_dir() { return ""; }
    virtual bool get_http_stream_crossdomain() { return false; }
    virtual bool get_rtc_server_enabled() { return false; }
    virtual bool get_rtc_server_tcp_enabled() { return false; }
    virtual std::vector<std::string> get_rtc_server_tcp_listens() { return std::vector<std::string>(); }
    virtual std::string get_rtc_server_protocol() { return "udp"; }
    virtual std::vector<std::string> get_rtc_server_listens() { return std::vector<std::string>(); }
    virtual int get_rtc_server_reuseport() { return 1; }
    virtual bool get_rtc_server_encrypt() { return false; }
    virtual bool get_api_as_candidates() { return api_as_candidates_; }
    virtual bool get_resolve_api_domain() { return resolve_api_domain_; }
    virtual bool get_keep_api_domain() { return keep_api_domain_; }
    virtual std::string get_rtc_server_candidates() { return "*"; }
    virtual bool get_use_auto_detect_network_ip() { return true; }
    virtual std::string get_rtc_server_ip_family() { return "ipv4"; }
    virtual bool get_rtsp_server_enabled() { return false; }
    virtual std::vector<std::string> get_rtsp_server_listens() { return std::vector<std::string>(); }
    virtual std::vector<std::string> get_srt_listens() { return std::vector<std::string>(); }
    virtual std::vector<SrsConfDirective *> get_stream_casters() { return std::vector<SrsConfDirective *>(); }
    virtual bool get_stream_caster_enabled(SrsConfDirective *conf) { return false; }
    virtual std::string get_stream_caster_engine(SrsConfDirective *conf) { return ""; }
    virtual std::string get_stream_caster_output(SrsConfDirective *conf) { return ""; }
    virtual int get_stream_caster_listen(SrsConfDirective *conf) { return 0; }
    virtual bool get_exporter_enabled() { return false; }
    virtual std::string get_exporter_listen() { return ""; }
    virtual std::string get_exporter_label() { return ""; }
    virtual std::string get_exporter_tag() { return ""; }
    virtual bool get_stats_enabled() { return false; }
    virtual int get_stats_network() { return 0; }
    virtual bool get_heartbeat_enabled() { return false; }
    virtual srs_utime_t get_heartbeat_interval() { return 0; }
    virtual std::string get_heartbeat_url() { return ""; }
    virtual std::string get_heartbeat_device_id() { return ""; }
    virtual bool get_heartbeat_summaries() { return false; }
    virtual bool get_heartbeat_ports() { return false; }
    virtual bool get_circuit_breaker() { return false; }
    virtual int get_high_threshold() { return 0; }
    virtual int get_high_pulse() { return 0; }
    virtual int get_critical_threshold() { return 0; }
    virtual int get_critical_pulse() { return 0; }
    virtual int get_dying_threshold() { return 0; }
    virtual int get_dying_pulse() { return 0; }
    virtual std::string get_rtmps_ssl_cert() { return ""; }
    virtual std::string get_rtmps_ssl_key() { return ""; }
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true) { return NULL; }
    virtual bool get_vhost_enabled(std::string vhost) { return true; }
    virtual bool get_debug_srs_upnode(std::string vhost) { return true; }
    virtual int get_out_ack_size(std::string vhost) { return 2500000; }
    virtual int get_in_ack_size(std::string vhost) { return 2500000; }
    virtual int get_chunk_size(std::string vhost) { return 60000; }
    virtual bool get_gop_cache(std::string vhost) { return true; }
    virtual int get_gop_cache_max_frames(std::string vhost) { return 2500; }
    virtual bool get_tcp_nodelay(std::string vhost) { return false; }
    virtual srs_utime_t get_mw_sleep(std::string vhost, bool is_rtc = false) { return 350 * SRS_UTIME_MILLISECONDS; }
    virtual srs_utime_t get_send_min_interval(std::string vhost) { return 0; }
    virtual bool get_mr_enabled(std::string vhost) { return false; }
    virtual srs_utime_t get_mr_sleep(std::string vhost) { return 350 * SRS_UTIME_MILLISECONDS; }
    virtual srs_utime_t get_publish_1stpkt_timeout(std::string vhost) { return 20000 * SRS_UTIME_MILLISECONDS; }
    virtual srs_utime_t get_publish_normal_timeout(std::string vhost) { return 5000 * SRS_UTIME_MILLISECONDS; }
    virtual srs_utime_t get_publish_kickoff_for_idle(std::string vhost) { return 0; }
    virtual bool get_refer_enabled(std::string vhost) { return false; }
    virtual SrsConfDirective *get_refer_all(std::string vhost) { return NULL; }
    virtual SrsConfDirective *get_refer_play(std::string vhost) { return NULL; }
    virtual SrsConfDirective *get_refer_publish(std::string vhost) { return NULL; }
    virtual bool get_vhost_origin_cluster(std::string vhost) { return false; }
    virtual std::vector<std::string> get_vhost_coworkers(std::string vhost) { return std::vector<std::string>(); }
    virtual bool get_vhost_edge_token_traverse(std::string vhost) { return false; }
    virtual SrsConfDirective *get_vhost_edge_origin(std::string vhost) { return NULL; }
    virtual SrsConfDirective *get_vhost_on_connect(std::string vhost) { return NULL; }
    virtual SrsConfDirective *get_vhost_on_close(std::string vhost) { return NULL; }
    virtual SrsConfDirective *get_vhost_on_publish(std::string vhost) { return NULL; }
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost) { return NULL; }
    virtual bool get_rtc_enabled(std::string vhost) { return false; }
    virtual bool get_rtsp_enabled(std::string vhost) { return false; }
    virtual bool get_rtc_from_rtmp(std::string vhost) { return false; }
    virtual bool get_rtsp_from_rtmp(std::string vhost) { return false; }
    // ISrsAppConfig methods
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_unpublish(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_dvr(std::string vhost);
    virtual bool get_rtc_nack_enabled(std::string vhost);
    virtual bool get_rtc_nack_no_copy(std::string vhost);
    virtual bool get_realtime_enabled(std::string vhost, bool is_rtc);
    virtual int get_mw_msgs(std::string vhost, bool is_realtime, bool is_rtc);
    virtual int get_rtc_drop_for_pt(std::string vhost);
    virtual bool get_rtc_twcc_enabled(std::string vhost);
    virtual bool get_srt_enabled();
    virtual bool get_srt_enabled(std::string vhost);
    virtual std::string get_srt_default_streamid();
    virtual bool get_srt_to_rtmp(std::string vhost);
    virtual bool get_rtc_to_rtmp(std::string vhost);
    virtual srs_utime_t get_rtc_stun_timeout(std::string vhost);
    virtual bool get_rtc_stun_strict_check(std::string vhost);
    virtual std::string get_rtc_dtls_role(std::string vhost);
    virtual std::string get_rtc_dtls_version(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_hls(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_hls_notify(std::string vhost);
    // HLS methods
    virtual bool get_hls_enabled(std::string vhost);
    virtual bool get_hls_enabled(SrsConfDirective *vhost);
    virtual bool get_hls_use_fmp4(std::string vhost);
    virtual std::string get_hls_entry_prefix(std::string vhost);
    virtual std::string get_hls_path(std::string vhost);
    virtual std::string get_hls_m3u8_file(std::string vhost);
    virtual std::string get_hls_ts_file(std::string vhost);
    virtual std::string get_hls_fmp4_file(std::string vhost);
    virtual std::string get_hls_init_file(std::string vhost);
    virtual bool get_hls_ts_floor(std::string vhost);
    virtual srs_utime_t get_hls_fragment(std::string vhost);
    virtual double get_hls_td_ratio(std::string vhost);
    virtual double get_hls_aof_ratio(std::string vhost);
    virtual srs_utime_t get_hls_window(std::string vhost);
    virtual std::string get_hls_on_error(std::string vhost);
    virtual bool get_hls_cleanup(std::string vhost);
    virtual srs_utime_t get_hls_dispose(std::string vhost);
    virtual bool get_hls_wait_keyframe(std::string vhost);
    virtual bool get_hls_keys(std::string vhost);
    virtual int get_hls_fragments_per_key(std::string vhost);
    virtual std::string get_hls_key_file(std::string vhost);
    virtual std::string get_hls_key_file_path(std::string vhost);
    virtual std::string get_hls_key_url(std::string vhost);
    virtual int get_vhost_hls_nb_notify(std::string vhost);
    virtual bool get_vhost_hls_dts_directly(std::string vhost);
    virtual bool get_hls_ctx_enabled(std::string vhost);
    virtual bool get_hls_ts_ctx_enabled(std::string vhost);
    virtual bool get_hls_recover(std::string vhost);
    virtual bool get_forward_enabled(std::string vhost);
    virtual SrsConfDirective *get_forwards(std::string vhost);
    virtual srs_utime_t get_queue_length(std::string vhost);
    virtual SrsConfDirective *get_forward_backend(std::string vhost);
    virtual bool get_atc(std::string vhost);
    virtual int get_time_jitter(std::string vhost);
    virtual bool get_mix_correct(std::string vhost);
    virtual bool try_annexb_first(std::string vhost);
    virtual bool get_vhost_is_edge(std::string vhost);
    virtual bool get_atc_auto(std::string vhost);
    virtual bool get_reduce_sequence_header(std::string vhost);
    virtual bool get_parse_sps(std::string vhost);
    // DVR methods
    virtual std::string get_dvr_path(std::string vhost) { return "./[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv"; }
    virtual std::string get_dvr_plan(std::string vhost) { return "session"; }
    virtual bool get_dvr_enabled(std::string vhost) { return false; }
    virtual SrsConfDirective *get_dvr_apply(std::string vhost) { return NULL; }
    virtual srs_utime_t get_dvr_duration(std::string vhost) { return 30 * SRS_UTIME_SECONDS; }
    virtual int get_dvr_time_jitter(std::string vhost) { return 0; }
    virtual bool get_dvr_wait_keyframe(std::string vhost) { return true; }
    virtual bool get_vhost_enabled(SrsConfDirective *conf) { return true; }
    virtual bool get_vhost_http_remux_enabled(std::string vhost) { return false; }
    virtual bool get_vhost_http_remux_enabled(SrsConfDirective *vhost) { return false; }
    virtual srs_utime_t get_vhost_http_remux_fast_cache(std::string vhost) { return 0; }
    virtual bool get_vhost_http_remux_drop_if_not_match(std::string vhost) { return false; }
    virtual bool get_vhost_http_remux_has_audio(std::string vhost) { return true; }
    virtual bool get_vhost_http_remux_has_video(std::string vhost) { return true; }
    virtual bool get_vhost_http_remux_guess_has_av(std::string vhost) { return true; }
    virtual std::string get_vhost_http_remux_mount(std::string vhost) { return ""; }
    virtual std::string get_vhost_edge_protocol(std::string vhost) { return "rtmp"; }
    virtual bool get_vhost_edge_follow_client(std::string vhost) { return false; }
    virtual std::string get_vhost_edge_transform_vhost(std::string vhost) { return ""; }
    // DASH methods
    virtual bool get_dash_enabled(std::string vhost) { return dash_enabled_; }
    virtual bool get_dash_enabled(SrsConfDirective *vhost) { return dash_enabled_; }
    virtual srs_utime_t get_dash_fragment(std::string vhost) { return 30 * SRS_UTIME_SECONDS; }
    virtual srs_utime_t get_dash_update_period(std::string vhost) { return 30 * SRS_UTIME_SECONDS; }
    virtual srs_utime_t get_dash_timeshift(std::string vhost) { return 300 * SRS_UTIME_SECONDS; }
    virtual std::string get_dash_path(std::string vhost) { return "./[vhost]/[app]/[stream]/"; }
    virtual std::string get_dash_mpd_file(std::string vhost) { return "[stream].mpd"; }
    virtual int get_dash_window_size(std::string vhost) { return 10; }
    virtual bool get_dash_cleanup(std::string vhost) { return true; }
    virtual srs_utime_t get_dash_dispose(std::string vhost) { return dash_dispose_; }
    // Exec config
    virtual bool get_exec_enabled(std::string vhost) { return false; }
    virtual std::vector<SrsConfDirective *> get_exec_publishs(std::string vhost) { return std::vector<SrsConfDirective *>(); }
    // Ingest config
    virtual void get_vhosts(std::vector<SrsConfDirective *> &vhosts) {}
    virtual std::vector<SrsConfDirective *> get_ingesters(std::string vhost) { return std::vector<SrsConfDirective *>(); }
    virtual SrsConfDirective *get_ingest_by_id(std::string vhost, std::string ingest_id) { return NULL; }
    virtual bool get_ingest_enabled(SrsConfDirective *conf) { return false; }
    virtual std::string get_ingest_ffmpeg(SrsConfDirective *conf) { return ""; }
    virtual std::string get_ingest_input_type(SrsConfDirective *conf) { return ""; }
    virtual std::string get_ingest_input_url(SrsConfDirective *conf) { return ""; }
    // FFmpeg log config
    virtual bool get_ff_log_enabled() { return false; }
    virtual std::string get_ff_log_dir() { return ""; }
    virtual std::string get_ff_log_level() { return ""; }
    // Transcode/Engine config
    virtual SrsConfDirective *get_transcode(std::string vhost, std::string scope) { return NULL; }
    virtual bool get_transcode_enabled(SrsConfDirective *conf) { return false; }
    virtual std::string get_transcode_ffmpeg(SrsConfDirective *conf) { return ""; }
    virtual std::vector<SrsConfDirective *> get_transcode_engines(SrsConfDirective *conf) { return std::vector<SrsConfDirective *>(); }
    virtual bool get_engine_enabled(SrsConfDirective *conf) { return false; }
    virtual std::vector<std::string> get_engine_perfile(SrsConfDirective *conf) { return std::vector<std::string>(); }
    virtual std::string get_engine_iformat(SrsConfDirective *conf) { return ""; }
    virtual std::vector<std::string> get_engine_vfilter(SrsConfDirective *conf) { return std::vector<std::string>(); }
    virtual std::string get_engine_vcodec(SrsConfDirective *conf) { return ""; }
    virtual int get_engine_vbitrate(SrsConfDirective *conf) { return 0; }
    virtual double get_engine_vfps(SrsConfDirective *conf) { return 0; }
    virtual int get_engine_vwidth(SrsConfDirective *conf) { return 0; }
    virtual int get_engine_vheight(SrsConfDirective *conf) { return 0; }
    virtual int get_engine_vthreads(SrsConfDirective *conf) { return 0; }
    virtual std::string get_engine_vprofile(SrsConfDirective *conf) { return ""; }
    virtual std::string get_engine_vpreset(SrsConfDirective *conf) { return ""; }
    virtual std::vector<std::string> get_engine_vparams(SrsConfDirective *conf) { return std::vector<std::string>(); }
    virtual std::string get_engine_acodec(SrsConfDirective *conf) { return ""; }
    virtual int get_engine_abitrate(SrsConfDirective *conf) { return 0; }
    virtual int get_engine_asample_rate(SrsConfDirective *conf) { return 0; }
    virtual int get_engine_achannels(SrsConfDirective *conf) { return 0; }
    virtual std::vector<std::string> get_engine_aparams(SrsConfDirective *conf) { return std::vector<std::string>(); }
    virtual std::string get_engine_oformat(SrsConfDirective *conf) { return ""; }
    virtual std::string get_engine_output(SrsConfDirective *conf) { return ""; }
    void set_http_hooks_enabled(bool enabled);
    void set_on_stop_urls(const std::vector<std::string> &urls);
    void clear_on_stop_directive();
    void set_on_unpublish_urls(const std::vector<std::string> &urls);
    void clear_on_unpublish_directive();
    void set_rtc_nack_enabled(bool enabled);
    void set_rtc_nack_no_copy(bool no_copy);
    void set_rtc_drop_for_pt(int pt);
    void set_rtc_twcc_enabled(bool enabled);
    void set_srt_enabled(bool enabled);
    void set_rtc_to_rtmp(bool enabled);
    void set_api_as_candidates(bool enabled);
    void set_resolve_api_domain(bool enabled);
    void set_keep_api_domain(bool enabled);
};

#endif
