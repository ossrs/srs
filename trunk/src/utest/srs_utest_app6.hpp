//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP6_HPP
#define SRS_UTEST_APP6_HPP

/*
#include <srs_utest_app6.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_app2.hpp>
#include <srs_utest_protocol3.hpp>

// Mock ISrsResourceManager for testing SrsGbMediaTcpConn::bind_session
class MockResourceManagerForBindSession : public ISrsResourceManager
{
public:
    ISrsResource *session_to_return_;

public:
    MockResourceManagerForBindSession();
    virtual ~MockResourceManagerForBindSession();

public:
    virtual srs_error_t start();
    virtual bool empty();
    virtual size_t size();
    virtual void add(ISrsResource *conn, bool *exists = NULL);
    virtual void add_with_id(const std::string &id, ISrsResource *conn);
    virtual void add_with_fast_id(uint64_t id, ISrsResource *conn);
    virtual void add_with_name(const std::string &name, ISrsResource *conn);
    virtual ISrsResource *at(int index);
    virtual ISrsResource *find_by_id(std::string id);
    virtual ISrsResource *find_by_fast_id(uint64_t id);
    virtual ISrsResource *find_by_name(std::string name);
    virtual void remove(ISrsResource *c);
    virtual void subscribe(ISrsDisposingHandler *h);
    virtual void unsubscribe(ISrsDisposingHandler *h);
    void reset();
};

// Mock ISrsEphemeralDelta for testing SrsRtcUdpNetwork
class MockEphemeralDelta : public ISrsEphemeralDelta
{
public:
    int64_t in_bytes_;
    int64_t out_bytes_;

public:
    MockEphemeralDelta();
    virtual ~MockEphemeralDelta();

public:
    virtual void add_delta(int64_t in, int64_t out);
    virtual void remark(int64_t *in, int64_t *out);
    void reset();
};

// Mock ISrsUdpMuxSocket for testing SrsRtcUdpNetwork STUN handling
class MockUdpMuxSocket : public ISrsUdpMuxSocket
{
public:
    srs_error_t sendto_error_;
    int sendto_called_count_;
    int last_sendto_size_;
    std::string peer_ip_;
    int peer_port_;
    std::string peer_id_;
    uint64_t fast_id_;
    char *data_;
    int size_;

public:
    MockUdpMuxSocket();
    virtual ~MockUdpMuxSocket();

public:
    virtual srs_error_t sendto(void *data, int size, srs_utime_t timeout);
    virtual std::string get_peer_ip() const;
    virtual int get_peer_port() const;
    virtual std::string peer_id();
    virtual uint64_t fast_id();
    virtual SrsUdpMuxSocket *copy_sendonly();
    virtual int recvfrom(srs_utime_t timeout);
    virtual char *data();
    virtual int size();

public:
    void reset();
    void set_sendto_error(srs_error_t err);
};

// Mock DTLS implementation for testing SrsSecurityTransport
class MockDtls : public ISrsDtls
{
public:
    srs_error_t initialize_error_;
    srs_error_t start_active_handshake_error_;
    srs_error_t on_dtls_error_;
    srs_error_t get_srtp_key_error_;
    std::string last_role_;
    std::string last_version_;
    std::string recv_key_;
    std::string send_key_;
    int initialize_count_;
    int start_active_handshake_count_;
    int on_dtls_count_;
    int get_srtp_key_count_;

public:
    MockDtls();
    virtual ~MockDtls();

public:
    virtual srs_error_t initialize(std::string role, std::string version);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);

public:
    void reset();
    void set_initialize_error(srs_error_t err);
    void set_start_active_handshake_error(srs_error_t err);
    void set_on_dtls_error(srs_error_t err);
    void set_get_srtp_key_error(srs_error_t err);
    void set_srtp_keys(const std::string &recv_key, const std::string &send_key);
};

// Mock RTC Network for testing SrsSecurityTransport
class MockRtcNetwork : public ISrsRtcNetwork
{
public:
    srs_error_t on_dtls_handshake_done_error_;
    srs_error_t on_dtls_alert_error_;
    srs_error_t protect_rtp_error_;
    srs_error_t protect_rtcp_error_;
    srs_error_t write_error_;
    std::string last_alert_type_;
    std::string last_alert_desc_;
    int on_dtls_handshake_done_count_;
    int on_dtls_alert_count_;
    int protect_rtp_count_;
    int protect_rtcp_count_;
    int write_count_;
    bool is_established_;

public:
    MockRtcNetwork();
    virtual ~MockRtcNetwork();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual void set_state(SrsRtcNetworkState state);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    virtual srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);
    virtual srs_error_t on_rtp(char *data, int nb_data);
    virtual srs_error_t on_rtcp(char *data, int nb_data);
    virtual bool is_establelished();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);

public:
    void reset();
    void set_on_dtls_handshake_done_error(srs_error_t err);
    void set_on_dtls_alert_error(srs_error_t err);
    void set_protect_rtp_error(srs_error_t err);
    void set_protect_rtcp_error(srs_error_t err);
    void set_write_error(srs_error_t err);
    void set_established(bool established);
};

// Mock SRTP implementation for testing SrsSecurityTransport
class MockSrtp : public ISrsSRTP
{
public:
    srs_error_t initialize_error_;
    srs_error_t protect_rtp_error_;
    srs_error_t protect_rtcp_error_;
    srs_error_t unprotect_rtp_error_;
    srs_error_t unprotect_rtcp_error_;
    std::string last_recv_key_;
    std::string last_send_key_;
    int initialize_count_;
    int protect_rtp_count_;
    int protect_rtcp_count_;
    int unprotect_rtp_count_;
    int unprotect_rtcp_count_;

public:
    MockSrtp();
    virtual ~MockSrtp();

public:
    virtual srs_error_t initialize(std::string recv_key, std::string send_key);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    virtual srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    virtual srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);

public:
    void reset();
    void set_initialize_error(srs_error_t err);
    void set_protect_rtp_error(srs_error_t err);
    void set_protect_rtcp_error(srs_error_t err);
    void set_unprotect_rtp_error(srs_error_t err);
    void set_unprotect_rtcp_error(srs_error_t err);
};

// Mock PLI Worker Handler for testing SrsRtcPliWorker
class MockRtcPliWorkerHandler : public ISrsRtcPliWorkerHandler
{
public:
    srs_error_t do_request_keyframe_error_;
    std::vector<std::pair<uint32_t, SrsContextId> > keyframe_requests_;
    int do_request_keyframe_count_;

public:
    MockRtcPliWorkerHandler();
    virtual ~MockRtcPliWorkerHandler();

public:
    virtual srs_error_t do_request_keyframe(uint32_t ssrc, SrsContextId cid);

public:
    void reset();
    void set_do_request_keyframe_error(srs_error_t err);
    bool has_keyframe_request(uint32_t ssrc, const SrsContextId &cid);
    int get_keyframe_request_count();
};

// Mock PLI Worker for testing SrsRtcPlayStream::on_stream_change
class MockRtcPliWorker : public SrsRtcPliWorker
{
public:
    std::vector<std::pair<uint32_t, SrsContextId> > keyframe_requests_;
    int request_keyframe_count_;

public:
    MockRtcPliWorker(ISrsRtcPliWorkerHandler *h);
    virtual ~MockRtcPliWorker();

public:
    virtual void request_keyframe(uint32_t ssrc, SrsContextId cid);

public:
    void reset();
    bool has_keyframe_request(uint32_t ssrc, const SrsContextId &cid);
    int get_keyframe_request_count();
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

// Mock context for testing SrsRtcAsyncCallOnStop
class MockContext : public ISrsContext
{
public:
    SrsContextId current_id_;
    SrsContextId get_id_result_;

public:
    MockContext();
    virtual ~MockContext();
    virtual SrsContextId generate_id();
    virtual const SrsContextId &get_id();
    virtual const SrsContextId &set_id(const SrsContextId &v);
    void set_current_id(const SrsContextId &id);
};

// Mock app config for testing SrsRtcAsyncCallOnStop
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

// Mock request for testing SrsRtcAsyncCallOnStop
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

// Mock RTC source manager for testing SrsRtcPlayStream
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

// Mock statistic for testing SrsRtcPlayStream
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

// Mock RTC async task executor for testing SrsRtcPlayStream::send_packet
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

// Mock RTC packet sender for testing SrsRtcPlayStream::send_packet
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

// Mock RTC send track for testing SrsRtcPlayStream::send_packet
class MockRtcSendTrack : public SrsRtcSendTrack
{
public:
    srs_error_t on_rtp_error_;
    srs_error_t on_nack_error_;
    int on_rtp_count_;
    int on_nack_count_;
    SrsRtpPacket *last_rtp_packet_;
    SrsRtpPacket **last_nack_packet_;
    bool nack_set_to_null_;

public:
    MockRtcSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio);
    virtual ~MockRtcSendTrack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt);
    virtual srs_error_t on_nack(SrsRtpPacket **ppkt);
    void set_on_rtp_error(srs_error_t err);
    void set_on_nack_error(srs_error_t err);
    void set_nack_set_to_null(bool v);
    void reset();
};

// Mock RTCP classes for testing SrsRtcPlayStream::on_rtcp
class MockRtcpCommon : public SrsRtcpCommon
{
public:
    uint8_t mock_type_;

public:
    MockRtcpCommon(uint8_t type);
    virtual ~MockRtcpCommon();
    virtual uint8_t type() const;
};

class MockRtcpRR : public SrsRtcpRR
{
public:
    MockRtcpRR(uint32_t sender_ssrc = 0);
    virtual ~MockRtcpRR();
};

class MockRtcpNack : public SrsRtcpNack
{
public:
    MockRtcpNack(uint32_t sender_ssrc = 0);
    virtual ~MockRtcpNack();
};

class MockRtcpFbCommon : public SrsRtcpFbCommon
{
public:
    uint8_t mock_rc_;

public:
    MockRtcpFbCommon(uint8_t rc = kPLI);
    virtual ~MockRtcpFbCommon();
    virtual uint8_t get_rc() const;
};

class MockRtcpXr : public SrsRtcpXr
{
public:
    MockRtcpXr(uint32_t ssrc = 0);
    virtual ~MockRtcpXr();
};

// Mock RTC send track with NACK response capability for testing on_rtcp_nack
class MockRtcSendTrackForNack : public SrsRtcSendTrack
{
public:
    srs_error_t on_recv_nack_error_;
    int on_recv_nack_count_;
    std::vector<uint16_t> last_lost_seqs_;
    uint32_t test_ssrc_;
    bool track_enabled_;

public:
    MockRtcSendTrackForNack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio, uint32_t ssrc);
    virtual ~MockRtcSendTrackForNack();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtpPacket *pkt);
    virtual srs_error_t on_recv_nack(const std::vector<uint16_t> &lost_seqs);
    virtual bool has_ssrc(uint32_t ssrc);
    virtual bool get_track_status();

public:
    void set_track_enabled(bool enabled);
    void set_on_recv_nack_error(srs_error_t err);
    void reset();
};

// Mock RTCP sender for testing SrsRtcPublishRtcpTimer
class MockRtcRtcpSender : public ISrsRtcRtcpSender
{
public:
    bool is_sender_started_;
    bool is_sender_twcc_enabled_;
    srs_error_t send_rtcp_rr_error_;
    srs_error_t send_rtcp_xr_rrtr_error_;
    srs_error_t send_periodic_twcc_error_;
    int send_rtcp_rr_count_;
    int send_rtcp_xr_rrtr_count_;
    int send_periodic_twcc_count_;

public:
    MockRtcRtcpSender();
    virtual ~MockRtcRtcpSender();

public:
    virtual bool is_sender_started();
    virtual srs_error_t send_rtcp_rr();
    virtual srs_error_t send_rtcp_xr_rrtr();
    virtual bool is_sender_twcc_enabled();
    virtual srs_error_t send_periodic_twcc();

public:
    void set_sender_started(bool started);
    void set_sender_twcc_enabled(bool enabled);
    void set_send_rtcp_rr_error(srs_error_t err);
    void set_send_rtcp_xr_rrtr_error(srs_error_t err);
    void set_send_periodic_twcc_error(srs_error_t err);
    void reset();
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
    void reset();
};

// Mock expire for testing SrsRtcPublishStream
class MockRtcExpire : public ISrsExpire
{
public:
    bool expired_;

public:
    MockRtcExpire();
    virtual ~MockRtcExpire();

public:
    virtual void expire();
    void reset();
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
    void reset();
};

// Mock live source for testing SrsRtcPublishStream
class MockLiveSource : public SrsLiveSource
{
public:
    bool can_publish_result_;

public:
    MockLiveSource();
    virtual ~MockLiveSource();
    virtual bool can_publish(bool is_edge);
    void set_can_publish(bool can_publish);
    virtual srs_error_t on_publish();
    virtual srs_error_t on_edge_start_publish();
};

// Mock SRT source for testing SrsRtcPublishStream
class MockSrtSource : public SrsSrtSource
{
public:
    bool can_publish_result_;

public:
    MockSrtSource();
    virtual ~MockSrtSource();
    virtual bool can_publish();
    void set_can_publish(bool can_publish);
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
    void reset();
};

#endif
