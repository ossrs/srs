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
#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_stream.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_http_conn.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

// Forward declarations
class SrsRtcTrackDescription;
class SrsRtpPacket;
class MockMp4Encoder;
class MockSrsFileWriter;
class MockSrsFile;
class MockSrsFileReader;
class MockSrtCoroutine;
class ISrsGbSession;
class ISrsProtocolUtility;
class SrsCond;

// Mock SDP factory for creating test SDP offers/answers
class MockSdpFactory
{
public:
    // Audio track properties
    uint32_t audio_ssrc_;
    uint8_t audio_pt_;

    // Video track properties
    uint32_t video_ssrc_;
    uint8_t video_pt_;

public:
    MockSdpFactory();
    virtual ~MockSdpFactory();

public:
    // Create a Chrome-like WebRTC publisher offer SDP
    std::string create_chrome_publisher_offer_with_h264();
    // Create a Chrome-like WebRTC player offer SDP
    std::string create_chrome_player_offer_with_h264();
    // Create a Chrome-like WebRTC publisher offer SDP with AV1
    std::string create_chrome_publisher_offer_with_av1();
    // Create a Chrome-like WebRTC publisher offer SDP with VP9
    std::string create_chrome_publisher_offer_with_vp9();
};

// Mock DTLS certificate for testing
class MockDtlsCertificate : public ISrsDtlsCertificate
{
public:
    std::string fingerprint_;

public:
    MockDtlsCertificate();
    virtual ~MockDtlsCertificate();

public:
    virtual srs_error_t initialize();
    virtual std::string get_fingerprint();
};

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
    uint8_t audio_pt_;
    uint8_t video_pt_;
    uint8_t screen_pt_;

public:
    // Create a map of track descriptions with audio and video tracks (for play stream)
    std::map<uint32_t, SrsRtcTrackDescription *> create_audio_video_tracks();

    // Create a stream description with audio and video tracks (for publish stream)
    SrsRtcSourceDescription *create_stream_description();

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
    MockRtcAsyncCallRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockRtcAsyncCallRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Mock RTC source for testing
class MockRtcSource : public SrsRtcSource
{
public:
    int on_rtp_count_;
    int rtp_audio_count_;
    int rtp_video_count_;

public:
    MockRtcSource();
    virtual ~MockRtcSource();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
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
};

// Mock statistic for testing
class MockAppStatistic : public ISrsStatistic
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
    MockAppStatistic();
    virtual ~MockAppStatistic();
    virtual void on_disconnect(std::string id, srs_error_t err);
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id);
    virtual void on_stream_close(ISrsRequest *req);
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta);
    virtual void kbps_sample();
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames);
    virtual srs_error_t on_audio_frames(ISrsRequest *req, int nb_frames);
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
};

// Mock RTC format for testing
class MockRtcFormat : public ISrsRtcFormat
{
public:
    srs_error_t initialize_error_;
    srs_error_t on_rtp_packet_error_;
    int initialize_count_;
    int on_rtp_packet_count_;

public:
    MockRtcFormat();
    virtual ~MockRtcFormat();

public:
    virtual srs_error_t initialize(ISrsRequest *req);
    virtual srs_error_t on_rtp_packet(SrsRtcRecvTrack *track, bool is_audio);
    void set_initialize_error(srs_error_t err);
    void set_on_rtp_packet_error(srs_error_t err);
};

// Mock app config for testing
class MockAppConfig : public ISrsAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_stop_directive_;
    SrsConfDirective *on_unpublish_directive_;
    SrsConfDirective *on_hls_directive_;
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
    srs_utime_t mw_sleep_;
    std::string rtc_dtls_role_;
    SrsConfDirective *default_vhost_;
    bool srt_to_rtmp_;
    bool rtc_from_rtmp_;
    SrsConfDirective *forwards_directive_;
    SrsConfDirective *backend_directive_;
    bool rtc_server_enabled_;
    bool rtc_enabled_;
    bool rtc_init_rate_from_sdp_;
    bool asprocess_;

public:
    MockAppConfig()
    {
        http_hooks_enabled_ = true;
        on_stop_directive_ = NULL;
        on_unpublish_directive_ = NULL;
        on_hls_directive_ = NULL;
        rtc_nack_enabled_ = true;
        rtc_nack_no_copy_ = false;
        rtc_drop_for_pt_ = 0;
        rtc_twcc_enabled_ = true;
        srt_enabled_ = false;
        rtc_to_rtmp_ = false;
        dash_dispose_ = 0;
        dash_enabled_ = false;
        api_as_candidates_ = true;
        resolve_api_domain_ = true;
        keep_api_domain_ = false;
        mw_msgs_ = 8;
        mw_sleep_ = 350 * SRS_UTIME_MILLISECONDS;
        rtc_dtls_role_ = "passive";
        default_vhost_ = NULL;
        srt_to_rtmp_ = true;
        rtc_from_rtmp_ = false;
        forwards_directive_ = NULL;
        backend_directive_ = NULL;
        rtc_server_enabled_ = false;
        rtc_enabled_ = false;
        rtc_init_rate_from_sdp_ = false;
        asprocess_ = false;
    }
    virtual ~MockAppConfig()
    {
        clear_on_stop_directive();
        clear_on_unpublish_directive();

        srs_freep(default_vhost_);
        srs_freep(forwards_directive_);
        srs_freep(backend_directive_);
        srs_freep(on_hls_directive_);
    }

public:
    // Helper methods for setting forward configuration
    void set_forward_destinations(const std::vector<std::string> &destinations)
    {
        srs_freep(forwards_directive_);
        if (!destinations.empty()) {
            forwards_directive_ = new SrsConfDirective();
            forwards_directive_->name_ = "destination";
            forwards_directive_->args_ = destinations;
        }
    }
    void set_forward_backend(const std::string &backend_url)
    {
        srs_freep(backend_directive_);
        if (!backend_url.empty()) {
            backend_directive_ = new SrsConfDirective();
            backend_directive_->name_ = "backend";
            backend_directive_->args_.push_back(backend_url);
        }
    }
    // ISrsConfig methods
    srs_utime_t get_pithy_print() { return 10 * SRS_UTIME_SECONDS; }
    std::string get_default_app_name() { return "live"; }
    void subscribe(ISrsReloadHandler *handler) {}
    void unsubscribe(ISrsReloadHandler *handler) {}
    virtual srs_error_t reload(SrsReloadState *pstate) { return srs_success; }
    virtual srs_error_t persistence() { return srs_success; }
    virtual std::string config() { return ""; }
    virtual SrsConfDirective *get_root() { return NULL; }
    virtual std::string cwd() { return "./"; }
    virtual int get_max_connections() { return 1000; }
    virtual std::string get_pid_file() { return ""; }
    virtual bool empty_ip_ok() { return false; }
    virtual bool get_asprocess() { return asprocess_; }
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
    virtual bool get_rtc_server_enabled() { return rtc_server_enabled_; }
    virtual bool get_rtc_server_tcp_enabled() { return false; }
    virtual std::vector<std::string> get_rtc_server_tcp_listens()
    {
        std::vector<std::string> v;
        v.push_back("127.0.0.1:8000");
        return v;
    }
    virtual std::string get_rtc_server_protocol() { return "udp"; }
    virtual std::vector<std::string> get_rtc_server_listens()
    {
        std::vector<std::string> v;
        v.push_back("127.0.0.1:8000");
        return v;
    }
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
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true) { return default_vhost_; }
    virtual bool get_vhost_enabled(std::string vhost) { return true; }
    virtual bool get_debug_srs_upnode(std::string vhost) { return true; }
    virtual int get_out_ack_size(std::string vhost) { return 2500000; }
    virtual int get_in_ack_size(std::string vhost) { return 2500000; }
    virtual int get_chunk_size(std::string vhost) { return 60000; }
    virtual bool get_gop_cache(std::string vhost) { return true; }
    virtual int get_gop_cache_max_frames(std::string vhost) { return 2500; }
    virtual bool get_tcp_nodelay(std::string vhost) { return false; }
    virtual srs_utime_t get_mw_sleep(std::string vhost, bool is_rtc = false) { return mw_sleep_; }
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
    virtual bool get_rtc_enabled(std::string vhost) { return rtc_enabled_; }
    virtual bool get_rtsp_enabled(std::string vhost) { return false; }
    virtual bool get_rtc_from_rtmp(std::string vhost) { return rtc_from_rtmp_; }
    virtual bool get_rtsp_from_rtmp(std::string vhost) { return false; }
    // ISrsAppConfig methods
    virtual bool get_vhost_http_hooks_enabled(std::string vhost) { return http_hooks_enabled_; }
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost) { return on_stop_directive_; }
    virtual SrsConfDirective *get_vhost_on_unpublish(std::string vhost) { return on_unpublish_directive_; }
    virtual SrsConfDirective *get_vhost_on_dvr(std::string vhost) { return NULL; }
    virtual bool get_rtc_nack_enabled(std::string vhost) { return rtc_nack_enabled_; }
    virtual bool get_rtc_nack_no_copy(std::string vhost) { return rtc_nack_no_copy_; }
    virtual bool get_realtime_enabled(std::string vhost, bool is_rtc) { return true; }
    virtual int get_mw_msgs(std::string vhost, bool is_realtime, bool is_rtc) { return mw_msgs_; }
    virtual int get_rtc_drop_for_pt(std::string vhost) { return rtc_drop_for_pt_; }
    virtual bool get_rtc_twcc_enabled(std::string vhost) { return rtc_twcc_enabled_; }
    virtual bool get_rtc_init_rate_from_sdp(std::string vhost) { return rtc_init_rate_from_sdp_; }
    virtual bool get_rtc_keep_original_ssrc(std::string vhost) { return false; }
    virtual bool get_srt_enabled() { return srt_enabled_; }
    virtual bool get_srt_enabled(std::string vhost) { return srt_enabled_; }
    virtual std::string get_srt_default_streamid() { return "#!::r=live/livestream,m=request"; }
    virtual bool get_srt_to_rtmp(std::string vhost) { return srt_to_rtmp_; }
    virtual bool get_rtc_to_rtmp(std::string vhost) { return rtc_to_rtmp_; }
    virtual srs_utime_t get_rtc_stun_timeout(std::string vhost) { return 30 * SRS_UTIME_SECONDS; }
    virtual bool get_rtc_stun_strict_check(std::string vhost) { return false; }
    virtual std::string get_rtc_dtls_role(std::string vhost) { return rtc_dtls_role_; }
    virtual std::string get_rtc_dtls_version(std::string vhost) { return "auto"; }
    virtual SrsConfDirective *get_vhost_on_hls(std::string vhost) { return on_hls_directive_; }
    virtual SrsConfDirective *get_vhost_on_hls_notify(std::string vhost) { return NULL; }
    // HLS methods
    virtual bool get_hls_enabled(std::string vhost) { return false; }
    virtual bool get_hls_enabled(SrsConfDirective *vhost) { return false; }
    virtual bool get_hls_use_fmp4(std::string vhost) { return false; }
    virtual std::string get_hls_entry_prefix(std::string vhost) { return ""; }
    virtual std::string get_hls_path(std::string vhost) { return "./objs/nginx/html"; }
    virtual std::string get_hls_m3u8_file(std::string vhost) { return "[app]/[stream].m3u8"; }
    virtual std::string get_hls_ts_file(std::string vhost) { return "[app]/[stream]-[seq].ts"; }
    virtual std::string get_hls_fmp4_file(std::string vhost) { return "[app]/[stream]-[seq].m4s"; }
    virtual std::string get_hls_init_file(std::string vhost) { return "[app]/[stream]-init.mp4"; }
    virtual bool get_hls_ts_floor(std::string vhost) { return false; }
    virtual srs_utime_t get_hls_fragment(std::string vhost) { return 10 * SRS_UTIME_SECONDS; }
    virtual double get_hls_td_ratio(std::string vhost) { return 1.5; }
    virtual double get_hls_aof_ratio(std::string vhost) { return 2.0; }
    virtual srs_utime_t get_hls_window(std::string vhost) { return 60 * SRS_UTIME_SECONDS; }
    virtual std::string get_hls_on_error(std::string vhost) { return "continue"; }
    virtual bool get_hls_cleanup(std::string vhost) { return true; }
    virtual srs_utime_t get_hls_dispose(std::string vhost) { return 120 * SRS_UTIME_SECONDS; }
    virtual bool get_hls_wait_keyframe(std::string vhost) { return true; }
    virtual bool get_hls_keys(std::string vhost) { return false; }
    virtual int get_hls_fragments_per_key(std::string vhost) { return 5; }
    virtual std::string get_hls_key_file(std::string vhost) { return "[app]/[stream]-[seq].key"; }
    virtual std::string get_hls_key_file_path(std::string vhost) { return "./objs/nginx/html"; }
    virtual std::string get_hls_key_url(std::string vhost) { return ""; }
    virtual int get_vhost_hls_nb_notify(std::string vhost) { return 64; }
    virtual bool get_vhost_hls_dts_directly(std::string vhost) { return true; }
    virtual bool get_hls_ctx_enabled(std::string vhost) { return true; }
    virtual bool get_hls_ts_ctx_enabled(std::string vhost) { return true; }
    virtual bool get_hls_master_m3u8_path_relative(std::string vhost) { return false; }
    virtual bool get_hls_recover(std::string vhost) { return true; }
    virtual bool get_forward_enabled(std::string vhost) { return forwards_directive_ != NULL || backend_directive_ != NULL; }
    virtual SrsConfDirective *get_forwards(std::string vhost) { return forwards_directive_; }
    virtual srs_utime_t get_queue_length(std::string vhost) { return 30 * SRS_UTIME_SECONDS; }
    virtual SrsConfDirective *get_forward_backend(std::string vhost) { return backend_directive_; }
    virtual bool get_atc(std::string vhost) { return false; }
    virtual int get_time_jitter(std::string vhost) { return SrsRtmpJitterAlgorithmFULL; }
    virtual bool get_mix_correct(std::string vhost) { return false; }
    virtual bool try_annexb_first(std::string vhost) { return true; }
    virtual bool get_vhost_is_edge(std::string vhost) { return false; }
    virtual bool get_atc_auto(std::string vhost) { return false; }
    virtual bool get_reduce_sequence_header(std::string vhost) { return false; }
    virtual bool get_parse_sps(std::string vhost) { return true; }
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
    void set_http_hooks_enabled(bool enabled) { http_hooks_enabled_ = enabled; }
    void set_on_stop_urls(const std::vector<std::string> &urls)
    {
        clear_on_stop_directive();
        if (!urls.empty()) {
            on_stop_directive_ = new SrsConfDirective();
            on_stop_directive_->name_ = "on_stop";
            on_stop_directive_->args_ = urls;
        }
    }
    void clear_on_stop_directive() { srs_freep(on_stop_directive_); }
    void set_on_unpublish_urls(const std::vector<std::string> &urls)
    {
        clear_on_unpublish_directive();
        if (!urls.empty()) {
            on_unpublish_directive_ = new SrsConfDirective();
            on_unpublish_directive_->name_ = "on_unpublish";
            on_unpublish_directive_->args_ = urls;
        }
    }
    void clear_on_unpublish_directive() { srs_freep(on_unpublish_directive_); }
    void set_rtc_nack_enabled(bool enabled) { rtc_nack_enabled_ = enabled; }
    void set_rtc_nack_no_copy(bool no_copy) { rtc_nack_no_copy_ = no_copy; }
    void set_rtc_drop_for_pt(int pt) { rtc_drop_for_pt_ = pt; }
    void set_rtc_twcc_enabled(bool enabled) { rtc_twcc_enabled_ = enabled; }
    void set_srt_enabled(bool enabled) { srt_enabled_ = enabled; }
    void set_rtc_to_rtmp(bool enabled) { rtc_to_rtmp_ = enabled; }
    void set_api_as_candidates(bool enabled) { api_as_candidates_ = enabled; }
    void set_resolve_api_domain(bool enabled) { resolve_api_domain_ = enabled; }
    void set_keep_api_domain(bool enabled) { keep_api_domain_ = enabled; }
    bool get_security_enabled(std::string vhost) { return false; }
    SrsConfDirective *get_security_rules(std::string vhost) { return NULL; }
};

// Mock RTC packet receiver for testing SrsRtcPublishStream
class MockRtcPacketReceiver : public ISrsRtcPacketReceiver
{
public:
    srs_error_t send_rtcp_rr_error_;
    srs_error_t send_rtcp_xr_rrtr_error_;
    srs_error_t send_rtcp_error_;
    srs_error_t send_rtcp_fb_pli_error_;
    int send_rtcp_rr_count_;
    int send_rtcp_xr_rrtr_count_;
    int send_rtcp_count_;
    int send_rtcp_fb_pli_count_;
    int check_send_nacks_count_;

public:
    MockRtcPacketReceiver();
    virtual ~MockRtcPacketReceiver();

public:
    virtual srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    virtual srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    virtual void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    virtual srs_error_t send_rtcp(char *data, int nb_data);
    virtual srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);

public:
    void set_send_rtcp_rr_error(srs_error_t err);
    void set_send_rtcp_xr_rrtr_error(srs_error_t err);
    void set_send_rtcp_error(srs_error_t err);
    void set_send_rtcp_fb_pli_error(srs_error_t err);
};

// Mock ISrsSecurity for testing
class MockSecurity : public ISrsSecurity
{
public:
    srs_error_t check_error_;
    int check_count_;

public:
    MockSecurity();
    virtual ~MockSecurity();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);
};

// Mock live source manager for testing SrsRtcPublishStream
class MockLiveSourceManager : public ISrsLiveSourceManager
{
public:
    srs_error_t fetch_or_create_error_;
    int fetch_or_create_count_;
    SrsSharedPtr<SrsLiveSource> mock_source_;
    bool can_publish_;

public:
    MockLiveSourceManager();
    virtual ~MockLiveSourceManager();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps);
    virtual SrsSharedPtr<SrsLiveSource> fetch(ISrsRequest *r);
    virtual void dispose();
    virtual srs_error_t initialize();
    void set_fetch_or_create_error(srs_error_t err);
    void set_can_publish(bool can_publish);
};

// Mock live source for testing SrsRtcPublishStream
class MockLiveSource : public SrsLiveSource
{
public:
    bool can_publish_result_;
    int on_audio_count_;
    int on_video_count_;
    int on_dump_packets_count_;
    int on_frame_count_;

public:
    MockLiveSource();
    virtual ~MockLiveSource();
    virtual bool can_publish(bool is_edge);
    void set_can_publish(bool can_publish);
    virtual srs_error_t on_publish();
    virtual srs_error_t on_edge_start_publish();
    virtual srs_error_t consumer_dumps(ISrsLiveConsumer *consumer, bool ds, bool dm, bool dg);

public:
    virtual srs_error_t on_audio(SrsRtmpCommonMessage *audio);
    virtual srs_error_t on_video(SrsRtmpCommonMessage *video);
    virtual srs_error_t on_frame(SrsMediaPacket *msg);
};

// Mock SRT source for testing SrsRtcPublishStream
class MockSrtSource : public SrsSrtSource
{
public:
    bool can_publish_result_;
    int on_publish_count_;
    int on_packet_count_;

public:
    MockSrtSource();
    virtual ~MockSrtSource();

public:
    virtual bool can_publish();
    virtual srs_error_t on_publish();
    virtual srs_error_t on_srt_packet(SrsSrtPacket *packet);

public:
    virtual void set_can_publish(bool can_publish);
};

// Mock SRT format for testing
class MockSrtFormat : public ISrsSrtFormat
{
public:
    int initialize_count_;
    int on_srt_packet_count_;
    srs_error_t initialize_error_;
    srs_error_t on_srt_packet_error_;

public:
    MockSrtFormat();
    virtual ~MockSrtFormat();

public:
    virtual srs_error_t initialize(ISrsRequest *req);
    virtual srs_error_t on_srt_packet(SrsSrtPacket *pkt);
};

// Mock SRT source manager for testing SrsRtcPublishStream
class MockSrtSourceManager : public ISrsSrtSourceManager
{
public:
    srs_error_t initialize_error_;
    srs_error_t fetch_or_create_error_;
    int initialize_count_;
    int fetch_or_create_count_;
    SrsSharedPtr<SrsSrtSource> mock_source_;
    bool can_publish_;

public:
    MockSrtSourceManager();
    virtual ~MockSrtSourceManager();
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps);
    virtual SrsSharedPtr<SrsSrtSource> fetch(ISrsRequest *r);
    void set_initialize_error(srs_error_t err);
    void set_fetch_or_create_error(srs_error_t err);
    void set_can_publish(bool can_publish);
};

class MockRtmpServer : public ISrsRtmpServer
{
public:
    SrsRtmpConnType type_;
    std::string host_;
    std::string ip_;
    std::string vhost_;
    std::string app_;
    std::string stream_;
    std::string tcUrl_;
    std::string schema_;
    int port_;
    srs_utime_t duration_;

public:
    srs_error_t recv_err_;
    std::vector<SrsRtmpCommonMessage *> recv_msgs_;
    SrsCond *cond_;

public:
    int nb_sent_messages_;
    srs_error_t start_play_error_;
    int start_play_count_;
    srs_error_t start_publish_error_;
    int start_fmle_publish_count_;
    int start_flash_publish_count_;
    int start_haivision_publish_count_;

public:
    // Fields for handle_publish_message testing
    srs_error_t decode_message_error_;
    SrsRtmpCommand *decode_message_packet_;
    int decode_message_count_;
    srs_error_t fmle_unpublish_error_;
    int fmle_unpublish_count_;

public:
    // Fields for process_play_control_msg testing
    int send_and_free_packet_count_;
    int on_play_client_pause_count_;
    bool last_pause_state_;

public:
    // Fields for set_auto_response testing
    bool set_auto_response_called_;
    bool auto_response_value_;

public:
    MockRtmpServer();
    virtual ~MockRtmpServer();

public:
    void set_request(SrsRtmpConnType type, std::string ip, std::string vhost, std::string app, std::string stream, std::string tcUrl, std::string schema, int port, std::string host);

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_error_t handshake();
    virtual srs_error_t connect_app(ISrsRequest *req);
    virtual uint32_t proxy_real_ip();
    virtual srs_error_t set_window_ack_size(int ack_size);
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type);
    virtual srs_error_t set_chunk_size(int chunk_size);
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip = NULL);
    virtual srs_error_t on_bw_done();
    virtual srs_error_t identify_client(int stream_id, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration);
    virtual srs_error_t start_play(int stream_id);
    virtual srs_error_t start_fmle_publish(int stream_id);
    virtual srs_error_t start_haivision_publish(int stream_id);
    virtual srs_error_t fmle_unpublish(int stream_id, double unpublish_tid);
    virtual srs_error_t start_flash_publish(int stream_id);
    virtual srs_error_t start_publishing(int stream_id);
    virtual srs_error_t redirect(ISrsRequest *r, std::string url, bool &accepted);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id);
    virtual srs_error_t on_play_client_pause(int stream_id, bool is_pause);
    virtual srs_error_t set_in_window_ack_size(int ack_size);
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual void set_auto_response(bool v);
    virtual void set_merge_read(bool v, IMergeReadHandler *handler);
    virtual void set_recv_buffer(int buffer_size);
};

// Mock ISrsProtocolReadWriter for testing SrsHttpConn::cycle()
class MockProtocolReadWriter : public ISrsProtocolReadWriter
{
public:
    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;
    int64_t recv_bytes_;
    int read_count_;

public:
    srs_error_t read_error_;
    std::vector<std::string> recv_msgs_;
    SrsCond *cond_;

public:
    MockProtocolReadWriter();
    virtual ~MockProtocolReadWriter();

public:
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

class MockRtmpTransport : public ISrsRtmpTransport
{
public:
    MockProtocolReadWriter mock_io_;

public:
    MockRtmpTransport();
    virtual ~MockRtmpTransport();

public:
    virtual srs_netfd_t fd();
    virtual int osfd();
    virtual ISrsProtocolReadWriter *io();
    virtual srs_error_t handshake();
    virtual const char *transport_type();
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
    virtual srs_error_t set_tcp_nodelay(bool v);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

// Mock ISrsSslConnection for testing HTTPS connections
class MockSslConnection : public ISrsSslConnection
{
public:
    bool handshake_called_;
    srs_error_t handshake_error_;
    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;
    int64_t recv_bytes_;
    int64_t send_bytes_;
    int read_count_;

public:
    srs_error_t read_error_;
    std::vector<std::string> recv_msgs_;
    SrsCond *cond_;

public:
    MockSslConnection();
    virtual ~MockSslConnection();

public:
    virtual srs_error_t handshake(std::string key_file, std::string crt_file);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
};

// Mock ISrsProtocolReadWriter for testing SrsSrtRecvThread
class MockSrtConnection : public ISrsSrtConnection
{
public:
    int read_count_;
    bool simulate_timeout_;
    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;
    int64_t recv_bytes_;
    int64_t send_bytes_;
    std::string streamid_;
    srs_srt_t srt_fd_;

public:
    srs_error_t read_error_;
    std::vector<std::string> recv_msgs_;
    SrsCond *cond_;

public:
    MockSrtConnection();
    virtual ~MockSrtConnection();

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();

public:
    virtual srs_srt_t srtfd();
    virtual srs_error_t get_streamid(std::string &streamid);
    virtual srs_error_t get_stats(SrsSrtStat &stat);
};

// Mock ISrsHttpParser for testing SrsHttpConn::cycle()
class MockHttpParser : public ISrsHttpParser
{
public:
    bool initialize_called_;
    bool parse_message_called_;
    srs_error_t initialize_error_;
    srs_error_t parse_message_error_;

public:
    SrsCond *cond_;
    std::vector<ISrsHttpMessage *> messages_;

public:
    MockHttpParser();
    virtual ~MockHttpParser();

public:
    virtual srs_error_t initialize(enum llhttp_type type);
    virtual void set_jsonp(bool allow_jsonp);
    virtual srs_error_t parse_message(ISrsReader *reader, ISrsHttpMessage **ppmsg);
};

// Mock SrsHttpxConn for testing SrsLiveStream (old version for backward compatibility)
class MockHttpxConn : public ISrsHttpConnOwner
{
public:
    bool enable_stat_;

public:
    bool on_start_called_;
    bool on_http_message_called_;
    bool on_message_done_called_;
    bool on_conn_done_called_;
    srs_error_t on_start_error_;
    srs_error_t on_http_message_error_;
    srs_error_t on_message_done_error_;
    srs_error_t on_conn_done_error_;

public:
    MockHttpxConn();
    virtual ~MockHttpxConn();

public:
    virtual void set_enable_stat(bool v);
    virtual srs_error_t on_start();
    virtual srs_error_t on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w);
    virtual srs_error_t on_conn_done(srs_error_t r0);
};

// Mock SrsHttpConn for testing SrsLiveStream (old version for backward compatibility)
class MockHttpConn : public ISrsConnection, public ISrsExpire
{
public:
    MockHttpxConn *handler_;
    std::string remote_ip_;

public:
    MockHttpConn();
    virtual ~MockHttpConn();

public:
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual void expire();
    virtual ISrsHttpConnOwner *handler();
};

// Mock SrsHttpMessage for testing SrsLiveStream (old version for backward compatibility)
class MockHttpMessage : public SrsHttpMessage
{
public:
    MockHttpConn *mock_conn_;

public:
    MockHttpMessage();
    virtual ~MockHttpMessage();

public:
    virtual std::string path();
};

// Mock ISrsCommonHttpHandler for testing SrsServer::http_handle()
class MockHttpServeMux : public ISrsCommonHttpHandler
{
public:
    int handle_count_;
    int serve_http_count_;
    std::vector<std::string> patterns_;

public:
    MockHttpServeMux();
    virtual ~MockHttpServeMux();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// Mock request class for testing SrsBufferCache
class MockRequest : public ISrsRequest
{
public:
    MockRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Mock buffer cache for testing AAC stream encoder
class MockBufferCache : public ISrsBufferCache
{
public:
    int dump_cache_count_;
    ISrsLiveConsumer *last_consumer_;
    SrsRtmpJitterAlgorithm last_jitter_;

public:
    MockBufferCache();
    virtual ~MockBufferCache();
    virtual srs_error_t start();
    virtual void stop();
    virtual bool alive();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
    virtual srs_error_t update_auth(ISrsRequest *r);
};

// Mock HTTP hooks for testing SrsRtcAsyncCallOnStop
class MockHttpHooks : public ISrsHttpHooks
{
public:
    std::vector<std::pair<std::string, ISrsRequest *> > on_stop_calls_;
    int on_stop_count_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_unpublish_calls_;
    int on_unpublish_count_;

public:
    MockHttpHooks();
    virtual ~MockHttpHooks();
    virtual srs_error_t on_connect(std::string url, ISrsRequest *req);
    virtual void on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes);
    virtual srs_error_t on_publish(std::string url, ISrsRequest *req);
    virtual void on_unpublish(std::string url, ISrsRequest *req);
    virtual srs_error_t on_play(std::string url, ISrsRequest *req);
    virtual void on_stop(std::string url, ISrsRequest *req);
    virtual srs_error_t on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file);
    virtual srs_error_t on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                               std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration);
    virtual srs_error_t on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify);
    virtual srs_error_t discover_co_workers(std::string url, std::string &host, int &port);
    virtual srs_error_t on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls);
    void clear_calls();
};

// Mock origin hub for testing
class MockOriginHub : public ISrsOriginHub
{
public:
    int initialize_count_;
    srs_error_t initialize_error_;
    int on_hls_request_sh_count_;
    srs_error_t on_hls_request_sh_error_;
    int on_forwarder_start_count_;
    srs_error_t on_forwarder_start_error_;
    int on_dvr_request_sh_count_;
    srs_error_t on_dvr_request_sh_error_;

public:
    MockOriginHub();
    virtual ~MockOriginHub();
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsRequest *r);
    virtual void dispose();
    virtual srs_error_t cycle();
    virtual bool active();
    virtual srs_utime_t cleanup_delay();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata, SrsOnMetaDataPacket *packet);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, bool is_sequence_header);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_dvr_request_sh();
    virtual srs_error_t on_hls_request_sh();
    virtual srs_error_t on_forwarder_start(SrsForwarder *forwarder);
    void set_initialize_error(srs_error_t err);
    void set_on_hls_request_sh_error(srs_error_t err);
    void set_on_forwarder_start_error(srs_error_t err);
    void set_on_dvr_request_sh_error(srs_error_t err);
};

// Mock audio cache for ISrsRtcFrameBuilderAudioPacketCache
class MockAudioCache : public ISrsRtcFrameBuilderAudioPacketCache
{
public:
    int process_packet_count_;

public:
    MockAudioCache();
    virtual ~MockAudioCache();

public:
    virtual srs_error_t process_packet(SrsRtpPacket *src, std::vector<SrsRtpPacket *> &ready_packets);
    virtual void clear_all();
};

// Mock ISrsBasicRtmpClient for testing SrsForwarder
class MockRtmpClient : public ISrsBasicRtmpClient
{
public:
    bool connect_called_;
    bool publish_called_;
    bool play_called_;
    bool close_called_;
    bool recv_message_called_;
    bool decode_message_called_;
    bool set_recv_timeout_called_;
    bool kbps_sample_called_;
    bool send_and_free_message_called_;
    srs_error_t connect_error_;
    srs_error_t publish_error_;
    srs_error_t play_error_;
    srs_error_t send_and_free_message_error_;
    std::string publish_stream_;
    std::string play_stream_;
    int publish_chunk_size_;
    int stream_id_;
    std::string url_;
    srs_utime_t recv_timeout_;
    std::string kbps_label_;
    srs_utime_t kbps_age_;
    int send_message_count_;
    int send_and_free_messages_count_;

public:
    srs_error_t recv_err_;
    std::vector<SrsRtmpCommonMessage *> recv_msgs_;
    SrsCond *cond_;

public:
    MockRtmpClient();
    virtual ~MockRtmpClient();
    virtual srs_error_t connect();
    virtual void close();
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual void kbps_sample(const char *label, srs_utime_t age);
    virtual void kbps_sample(const char *label, srs_utime_t age, int msgs);
    virtual int sid();
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs);
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg);
    virtual void set_recv_timeout(srs_utime_t timeout);

public:
    virtual void set_url(std::string url);
};

// Mock the audio transcoder ISrsAudioTranscoder.
class MockAudioTranscoder : public ISrsAudioTranscoder
{
public:
    int transcode_count_;
    std::vector<SrsParsedAudioPacket *> output_packets_;
    std::string aac_header_;

public:
    MockAudioTranscoder();
    virtual ~MockAudioTranscoder();

public:
    virtual srs_error_t initialize(SrsAudioCodecId from, SrsAudioCodecId to, int channels, int sample_rate, int bit_rate);
    virtual srs_error_t transcode(SrsParsedAudioPacket *in, std::vector<SrsParsedAudioPacket *> &outs);
    virtual void free_frames(std::vector<SrsParsedAudioPacket *> &frames);
    virtual void aac_codec_header(uint8_t **data, int *len);
};

// Mock ISrsProtocolUtility for testing RTC connections
// This class merges functionality from MockProtocolUtility in srs_utest_ai18.hpp
// It supports both simple single-IP usage (via constructor) and complex multi-IP usage (via add_ip)
class MockProtocolUtility : public ISrsProtocolUtility
{
public:
    std::vector<SrsIPAddress *> ips_;
    std::string mock_ip_;

public:
    MockProtocolUtility(std::string ip = "");
    virtual ~MockProtocolUtility();

public:
    virtual std::vector<SrsIPAddress *> &local_ips();
    void add_ip(std::string ip, std::string ifname, bool is_ipv4, bool is_loopback, bool is_internet);
    void clear_ips();
};

#endif
