//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP10_HPP
#define SRS_UTEST_APP10_HPP

/*
#include <srs_utest_app10.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_factory.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_security.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_utest_app6.hpp>

// Mock config for testing SrsServer::listen()
class MockAppConfigForServerListen : public MockAppConfig
{
public:
    std::vector<std::string> rtmp_listens_;
    bool rtmps_enabled_;
    std::vector<std::string> rtmps_listens_;
    bool http_api_enabled_;
    std::vector<std::string> http_api_listens_;
    bool https_api_enabled_;
    std::vector<std::string> https_api_listens_;
    bool http_stream_enabled_;
    std::vector<std::string> http_stream_listens_;
    bool https_stream_enabled_;
    std::vector<std::string> https_stream_listens_;
    bool rtc_server_tcp_enabled_;
    std::vector<std::string> rtc_server_tcp_listens_;
    std::string rtc_server_protocol_;
    bool rtsp_server_enabled_;
    std::vector<std::string> rtsp_server_listens_;
    bool exporter_enabled_;
    std::string exporter_listen_;

public:
    MockAppConfigForServerListen();
    virtual ~MockAppConfigForServerListen();

public:
    virtual std::vector<std::string> get_listens();
    virtual bool get_rtmps_enabled();
    virtual std::vector<std::string> get_rtmps_listen();
    virtual bool get_http_api_enabled();
    virtual std::vector<std::string> get_http_api_listens();
    virtual bool get_https_api_enabled();
    virtual std::vector<std::string> get_https_api_listens();
    virtual bool get_http_stream_enabled();
    virtual std::vector<std::string> get_http_stream_listens();
    virtual bool get_https_stream_enabled();
    virtual std::vector<std::string> get_https_stream_listens();
    virtual bool get_rtc_server_tcp_enabled();
    virtual std::vector<std::string> get_rtc_server_tcp_listens();
    virtual std::string get_rtc_server_protocol();
    virtual bool get_rtsp_server_enabled();
    virtual std::vector<std::string> get_rtsp_server_listens();
    virtual bool get_exporter_enabled();
    virtual std::string get_exporter_listen();
};

// Mock ISrsCommonHttpHandler for testing SrsServer::http_handle()
class MockHttpServeMux : public ISrsCommonHttpHandler
{
public:
    int handle_count_;
    std::vector<std::string> patterns_;

public:
    MockHttpServeMux();
    virtual ~MockHttpServeMux();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// Mock ISrsLog for testing SrsServer::on_signal()
class MockLogForSignal : public ISrsLog
{
public:
    int reopen_count_;

public:
    MockLogForSignal();
    virtual ~MockLogForSignal();

public:
    virtual srs_error_t initialize();
    virtual void reopen();
    virtual void log(SrsLogLevel level, const char *tag, const SrsContextId &context_id, const char *fmt, va_list args);
};

// Mock ISrsAppConfig for testing SrsServer::on_signal()
class MockAppConfigForSignal : public MockAppConfig
{
public:
    bool force_grace_quit_;

public:
    MockAppConfigForSignal();
    virtual ~MockAppConfigForSignal();

public:
    virtual bool is_force_grace_quit();
};

// Mock ISrsAppConfig for testing SrsServer::do2_cycle()
class MockAppConfigForDo2Cycle : public MockAppConfig
{
public:
    srs_error_t reload_error_;
    srs_error_t persistence_error_;
    int reload_count_;
    int persistence_count_;
    SrsReloadState reload_state_;

public:
    MockAppConfigForDo2Cycle();
    virtual ~MockAppConfigForDo2Cycle();

public:
    virtual srs_error_t reload(SrsReloadState *pstate);
    virtual srs_error_t persistence();
    void reset();
};

// Mock ISrsHourGlass for testing SrsServer::setup_ticks()
class MockHourGlassForSetupTicks : public ISrsHourGlass
{
public:
    int tick_count_;
    int start_count_;
    std::vector<int> tick_events_;
    std::vector<srs_utime_t> tick_intervals_;

public:
    MockHourGlassForSetupTicks();
    virtual ~MockHourGlassForSetupTicks();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t tick(srs_utime_t interval);
    virtual srs_error_t tick(int event, srs_utime_t interval);
    virtual void untick(int event);
};

// Mock ISrsAppFactory for testing SrsServer::setup_ticks()
class MockAppFactoryForSetupTicks : public SrsAppFactory
{
public:
    MockHourGlassForSetupTicks *mock_hourglass_;

public:
    MockAppFactoryForSetupTicks();
    virtual ~MockAppFactoryForSetupTicks();

public:
    virtual ISrsHourGlass *create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval);
};

// Mock ISrsAppConfig for testing SrsServer::setup_ticks()
class MockAppConfigForSetupTicks : public MockAppConfig
{
public:
    bool stats_enabled_;
    bool heartbeat_enabled_;
    srs_utime_t heartbeat_interval_;

public:
    MockAppConfigForSetupTicks();
    virtual ~MockAppConfigForSetupTicks();

public:
    virtual bool get_stats_enabled();
    virtual bool get_heartbeat_enabled();
    virtual srs_utime_t get_heartbeat_interval();
};

// Mock SrsRtcSessionManager for testing SrsServer::notify()
class MockRtcSessionManagerForNotify : public SrsRtcSessionManager
{
public:
    int update_rtc_sessions_count_;

public:
    MockRtcSessionManagerForNotify();
    virtual ~MockRtcSessionManagerForNotify();

public:
    virtual void srs_update_rtc_sessions();
};

// Mock SrsHttpHeartbeat for testing SrsServer::notify()
class MockHttpHeartbeatForNotify : public SrsHttpHeartbeat
{
public:
    int heartbeat_count_;

public:
    MockHttpHeartbeatForNotify();
    virtual ~MockHttpHeartbeatForNotify();

public:
    virtual void heartbeat();
};

// Mock connection manager for testing SrsServer::resample_kbps()
class MockConnectionManagerForResampleKbps : public ISrsResourceManager
{
public:
    std::vector<ISrsResource *> connections_;

public:
    MockConnectionManagerForResampleKbps();
    virtual ~MockConnectionManagerForResampleKbps();

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
};

// Mock statistic for testing SrsServer::resample_kbps()
class MockStatisticForResampleKbps : public ISrsStatistic
{
public:
    int kbps_add_delta_count_;
    int kbps_sample_count_;

public:
    MockStatisticForResampleKbps();
    virtual ~MockStatisticForResampleKbps();

public:
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
    void reset();
};

// Mock config for testing SrsServer::on_before_connection()
class MockAppConfigForConnectionLimit : public MockAppConfig
{
public:
    int max_connections_;

public:
    MockAppConfigForConnectionLimit();
    virtual ~MockAppConfigForConnectionLimit();

public:
    virtual int get_max_connections();
};

// Mock connection manager for testing SrsServer::on_before_connection()
class MockConnectionManagerForConnectionLimit : public ISrsResourceManager
{
public:
    size_t connection_count_;

public:
    MockConnectionManagerForConnectionLimit();
    virtual ~MockConnectionManagerForConnectionLimit();

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
};

// Mock config for testing SrsRtmpConn constructor and assemble()
class MockAppConfigForRtmpConn : public MockAppConfig
{
public:
    int subscribe_count_;
    int unsubscribe_count_;
    ISrsReloadHandler *last_subscribed_handler_;
    SrsConfDirective *vhost_directive_;

public:
    MockAppConfigForRtmpConn();
    virtual ~MockAppConfigForRtmpConn();

public:
    virtual void subscribe(ISrsReloadHandler *handler);
    virtual void unsubscribe(ISrsReloadHandler *handler);
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true);
    virtual bool get_vhost_is_edge(std::string vhost);
    void reset();
};

// Mock ISrsRtmpServer for testing SrsRtmpConn::stream_service_cycle()
class MockRtmpServerForStreamService : public ISrsRtmpServer
{
public:
    SrsRtmpConnType identify_type_;
    std::string identify_stream_;
    srs_utime_t identify_duration_;
    int start_play_count_;
    int start_fmle_publish_count_;
    int start_flash_publish_count_;
    int start_haivision_publish_count_;

public:
    MockRtmpServerForStreamService();
    virtual ~MockRtmpServerForStreamService();

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_error_t handshake();
    virtual srs_error_t connect_app(ISrsRequest *req);
    virtual uint32_t proxy_real_ip();
    virtual srs_error_t set_window_ack_size(int ack_size);
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type);
    virtual srs_error_t set_chunk_size(int chunk_size);
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip);
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

// Mock ISrsCoroutine for testing SrsRtmpConn::service_cycle()
class MockCoroutineForRtmpConn : public ISrsCoroutine
{
public:
    srs_error_t pull_error_;
    int pull_count_;

public:
    MockCoroutineForRtmpConn();
    virtual ~MockCoroutineForRtmpConn();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

// Mock ISrsRtmpTransport for testing SrsRtmpConn::do_cycle()
class MockRtmpTransportForDoCycle : public ISrsRtmpTransport
{
public:
    MockRtmpTransportForDoCycle();
    virtual ~MockRtmpTransportForDoCycle();

public:
    virtual srs_netfd_t fd();
    virtual ISrsProtocolReadWriter *io();
    virtual srs_error_t handshake();
    virtual const char *transport_type();
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v);
    virtual srs_error_t set_tcp_nodelay(bool v);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
};

// Mock ISrsSecurity for testing SrsRtmpConn::stream_service_cycle()
class MockSecurityForStreamService : public ISrsSecurity
{
public:
    MockSecurityForStreamService();
    virtual ~MockSecurityForStreamService();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);
};

// Mock ISrsRtmpServer for testing SrsRtmpConn::handle_publish_message()
class MockRtmpServerForHandlePublishMessage : public ISrsRtmpServer
{
public:
    srs_error_t decode_message_error_;
    SrsRtmpCommand *decode_message_packet_;
    int decode_message_count_;
    srs_error_t fmle_unpublish_error_;
    int fmle_unpublish_count_;

public:
    MockRtmpServerForHandlePublishMessage();
    virtual ~MockRtmpServerForHandlePublishMessage();

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_error_t handshake();
    virtual srs_error_t connect_app(ISrsRequest *req);
    virtual uint32_t proxy_real_ip();
    virtual srs_error_t set_window_ack_size(int ack_size);
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type);
    virtual srs_error_t set_chunk_size(int chunk_size);
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip);
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

    void reset();
};

// Mock ISrsRtmpServer for testing SrsRtmpConn::process_play_control_msg()
class MockRtmpServerForPlayControl : public ISrsRtmpServer
{
public:
    SrsRtmpCommand *decode_message_packet_;
    int decode_message_count_;
    int send_and_free_packet_count_;
    int on_play_client_pause_count_;
    bool last_pause_state_;

public:
    MockRtmpServerForPlayControl();
    virtual ~MockRtmpServerForPlayControl();

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_error_t handshake();
    virtual srs_error_t connect_app(ISrsRequest *req);
    virtual uint32_t proxy_real_ip();
    virtual srs_error_t set_window_ack_size(int ack_size);
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type);
    virtual srs_error_t set_chunk_size(int chunk_size);
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip);
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

    void reset();
};

// Mock SrsLiveConsumer for testing SrsRtmpConn::process_play_control_msg()
class MockLiveConsumerForPlayControl : public SrsLiveConsumer
{
public:
    int on_play_client_pause_count_;
    bool last_pause_state_;

public:
    MockLiveConsumerForPlayControl(ISrsLiveSource *source);
    virtual ~MockLiveConsumerForPlayControl();

public:
    virtual srs_error_t on_play_client_pause(bool is_pause);
};

// Mock ISrsAppConfig for testing SrsRtmpConn::http_hooks_on_connect()
class MockAppConfigForHttpHooksOnConnect : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_connect_directive_;

public:
    MockAppConfigForHttpHooksOnConnect();
    virtual ~MockAppConfigForHttpHooksOnConnect();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_connect(std::string vhost);
};

// Mock ISrsHttpHooks for testing SrsRtmpConn::http_hooks_on_connect() and http_hooks_on_close()
class MockHttpHooksForOnConnect : public ISrsHttpHooks
{
public:
    std::vector<std::pair<std::string, ISrsRequest *> > on_connect_calls_;
    int on_connect_count_;
    srs_error_t on_connect_error_;

    // For on_close tracking
    struct OnCloseCall {
        std::string url_;
        ISrsRequest *req_;
        int64_t send_bytes_;
        int64_t recv_bytes_;
    };
    std::vector<OnCloseCall> on_close_calls_;
    int on_close_count_;

    // For on_unpublish tracking
    std::vector<std::pair<std::string, ISrsRequest *> > on_unpublish_calls_;
    int on_unpublish_count_;

    // For on_stop tracking
    std::vector<std::pair<std::string, ISrsRequest *> > on_stop_calls_;
    int on_stop_count_;

public:
    MockHttpHooksForOnConnect();
    virtual ~MockHttpHooksForOnConnect();

public:
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

    void reset();
};

// Mock ISrsAppConfig for testing SrsRtmpConn::http_hooks_on_close()
class MockAppConfigForHttpHooksOnClose : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_close_directive_;

public:
    MockAppConfigForHttpHooksOnClose();
    virtual ~MockAppConfigForHttpHooksOnClose();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_close(std::string vhost);
};

// Mock ISrsAppConfig for testing SrsRtmpConn::http_hooks_on_publish()
class MockAppConfigForHttpHooksOnPublish : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_publish_directive_;

public:
    MockAppConfigForHttpHooksOnPublish();
    virtual ~MockAppConfigForHttpHooksOnPublish();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_publish(std::string vhost);
};

// Mock ISrsHttpHooks for testing SrsRtmpConn::http_hooks_on_publish()
class MockHttpHooksForOnPublish : public ISrsHttpHooks
{
public:
    std::vector<std::pair<std::string, ISrsRequest *> > on_publish_calls_;
    int on_publish_count_;
    srs_error_t on_publish_error_;

public:
    MockHttpHooksForOnPublish();
    virtual ~MockHttpHooksForOnPublish();

public:
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

    void reset();
};

// Mock ISrsAppConfig for testing SrsRtmpConn::http_hooks_on_unpublish()
class MockAppConfigForHttpHooksOnUnpublish : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_unpublish_directive_;

public:
    MockAppConfigForHttpHooksOnUnpublish();
    virtual ~MockAppConfigForHttpHooksOnUnpublish();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_unpublish(std::string vhost);
};

// Mock ISrsAppConfig for testing SrsRtmpConn::http_hooks_on_stop()
class MockAppConfigForHttpHooksOnStop : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_stop_directive_;

public:
    MockAppConfigForHttpHooksOnStop();
    virtual ~MockAppConfigForHttpHooksOnStop();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost);
};

// Mock ISrsAppConfig for testing SrsRtmpConn::http_hooks_on_play()
class MockAppConfigForHttpHooksOnPlay : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_play_directive_;

public:
    MockAppConfigForHttpHooksOnPlay();
    virtual ~MockAppConfigForHttpHooksOnPlay();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost);
};

// Mock ISrsHttpHooks for testing SrsRtmpConn::http_hooks_on_play()
class MockHttpHooksForOnPlay : public ISrsHttpHooks
{
public:
    std::vector<std::pair<std::string, ISrsRequest *> > on_play_calls_;
    int on_play_count_;
    srs_error_t on_play_error_;

public:
    MockHttpHooksForOnPlay();
    virtual ~MockHttpHooksForOnPlay();

public:
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

    void reset();
};

// Mock ISrsAppConfig for testing SrsRtmpConn::acquire_publish()
class MockAppConfigForAcquirePublish : public MockAppConfigForRtmpConn
{
public:
    bool rtc_server_enabled_;
    bool rtc_enabled_;
    bool srt_enabled_;
    bool rtsp_server_enabled_;
    bool rtsp_enabled_;

public:
    MockAppConfigForAcquirePublish();
    virtual ~MockAppConfigForAcquirePublish();

public:
    virtual bool get_rtc_server_enabled();
    virtual bool get_rtc_enabled(std::string vhost);
    virtual bool get_srt_enabled();
    virtual bool get_srt_enabled(std::string vhost);
    virtual bool get_rtsp_server_enabled();
    virtual bool get_rtsp_enabled(std::string vhost);
};

#endif
