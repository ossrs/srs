//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI19_HPP
#define SRS_UTEST_AI19_HPP

/*
#include <srs_utest_ai19.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_manual_kernel.hpp>

// Mock ISrsAppConfig for testing SrsUdpCasterListener
class MockAppConfigForUdpCaster : public MockAppConfig
{
public:
    int stream_caster_listen_port_;

public:
    MockAppConfigForUdpCaster();
    virtual ~MockAppConfigForUdpCaster();

public:
    virtual int get_stream_caster_listen(SrsConfDirective *conf);
};

// Mock ISrsIpListener for testing SrsUdpCasterListener
class MockIpListenerForUdpCaster : public ISrsIpListener
{
public:
    std::string endpoint_ip_;
    int endpoint_port_;
    std::string label_;
    bool set_endpoint_called_;
    bool set_label_called_;
    bool listen_called_;
    bool close_called_;

public:
    MockIpListenerForUdpCaster();
    virtual ~MockIpListenerForUdpCaster();

public:
    virtual ISrsListener *set_endpoint(const std::string &i, int p);
    virtual ISrsListener *set_label(const std::string &label);
    virtual srs_error_t listen();
    virtual void close();
    void reset();
};

// Mock ISrsMpegtsOverUdp for testing SrsUdpCasterListener
class MockMpegtsOverUdp : public ISrsMpegtsOverUdp
{
public:
    int assemble_count_;
    bool initialize_called_;
    srs_error_t initialize_error_;

public:
    MockMpegtsOverUdp();
    virtual ~MockMpegtsOverUdp();

public:
    virtual void assemble();
    virtual srs_error_t initialize(SrsConfDirective *c);
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);
    virtual srs_error_t on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf);
    void reset();
};

// Mock ISrsAppConfig for testing SrsNgExec
class MockAppConfigForNgExec : public MockAppConfig
{
public:
    bool exec_enabled_;
    std::vector<SrsConfDirective *> exec_publishs_;

public:
    MockAppConfigForNgExec();
    virtual ~MockAppConfigForNgExec();

public:
    virtual bool get_exec_enabled(std::string vhost);
    virtual std::vector<SrsConfDirective *> get_exec_publishs(std::string vhost);
};

// Mock ISrsRawH264Stream for testing SrsMpegtsOverUdp::on_ts_video
class MockMpegtsRawH264Stream : public ISrsRawH264Stream
{
public:
    bool annexb_demux_called_;
    bool is_sps_called_;
    bool is_pps_called_;
    bool sps_demux_called_;
    bool pps_demux_called_;
    srs_error_t annexb_demux_error_;
    srs_error_t sps_demux_error_;
    srs_error_t pps_demux_error_;
    srs_error_t mux_ipb_frame_error_;

    // Mock return values
    char *demux_frame_;
    int demux_frame_size_;
    bool is_sps_result_;
    bool is_pps_result_;
    std::string sps_output_;
    std::string pps_output_;

public:
    MockMpegtsRawH264Stream();
    virtual ~MockMpegtsRawH264Stream();

public:
    virtual srs_error_t annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame);
    virtual bool is_sps(char *frame, int nb_frame);
    virtual bool is_pps(char *frame, int nb_frame);
    virtual srs_error_t sps_demux(char *frame, int nb_frame, std::string &sps);
    virtual srs_error_t pps_demux(char *frame, int nb_frame, std::string &pps);
    virtual srs_error_t mux_sequence_header(std::string sps, std::string pps, std::string &sh);
    virtual srs_error_t mux_ipb_frame(char *frame, int frame_size, std::string &ibp);
    virtual srs_error_t mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv);
    void reset();
};

// Mock ISrsAppFactory for testing SrsMpegtsOverUdp::rtmp_write_packet
class MockAppFactoryForMpegtsOverUdp : public SrsAppFactory
{
public:
    MockRtmpClient *mock_rtmp_client_;
    bool create_rtmp_client_called_;
    // The timeouts of the last client made.
    srs_utime_t cto_;
    srs_utime_t sto_;

public:
    MockAppFactoryForMpegtsOverUdp();
    virtual ~MockAppFactoryForMpegtsOverUdp();

public:
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
    void reset();
};

// Mock ISrsHttpResponseReader for testing SrsHttpFileReader
class MockHttpResponseReaderForHttpFileReader : public ISrsHttpResponseReader
{
public:
    std::string content_;
    size_t read_pos_;
    bool eof_;

public:
    MockHttpResponseReaderForHttpFileReader();
    virtual ~MockHttpResponseReaderForHttpFileReader();

public:
    virtual bool eof();
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    void reset();
};

// Mock ISrsPithyPrint for testing SrsIngester
class MockPithyPrintForIngester : public ISrsPithyPrint
{
public:
    bool elapse_called_;
    bool can_print_called_;
    bool can_print_result_;
    srs_utime_t age_value_;

public:
    MockPithyPrintForIngester();
    virtual ~MockPithyPrintForIngester();

public:
    virtual void elapse();
    virtual bool can_print();
    virtual srs_utime_t age();
    void reset();
};

// Mock ISrsCoroutine for testing SrsHttpConn::cycle()
class MockCoroutineForCycle : public ISrsCoroutine
{
public:
    bool pull_called_;
    srs_error_t pull_error_;

public:
    MockCoroutineForCycle();
    virtual ~MockCoroutineForCycle();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
    void reset();
};

// Mock SrsHttpMessage for testing SrsHttpConn::cycle()
class MockHttpMessageForCycle : public SrsHttpMessage
{
public:
    bool is_keep_alive_;

public:
    MockHttpMessageForCycle();
    virtual ~MockHttpMessageForCycle();

public:
    virtual bool is_keep_alive();
    virtual ISrsRequest *to_request(std::string vhost);
};

// Mock ISrsResourceManager for testing SrsHttpxConn
class MockResourceManagerForHttpxConn : public ISrsResourceManager
{
public:
    bool remove_called_;
    ISrsResource *removed_resource_;

public:
    MockResourceManagerForHttpxConn();
    virtual ~MockResourceManagerForHttpxConn();

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

// Mock ISrsStatistic for testing SrsHttpxConn
class MockStatisticForHttpxConn : public ISrsStatistic
{
public:
    bool on_disconnect_called_;
    bool kbps_add_delta_called_;
    std::string disconnect_id_;
    std::string kbps_id_;
    int disconnect_error_code_;
    ISrsKbpsDelta *kbps_delta_;

public:
    MockStatisticForHttpxConn();
    virtual ~MockStatisticForHttpxConn();

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
    virtual srs_error_t on_audio_frames(ISrsRequest *req, int nb_frames);
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr);
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count);
    virtual int count_forwards(std::string stream_id);
    virtual SrsStatisticForward *find_forward(std::string forward_id);
    virtual void on_forward_start(std::string id, ISrsRequest *req, std::string url);
    virtual void on_forward_state(std::string id, SrsForwardState state);
    virtual void on_forward_stop(std::string id);
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs);
    virtual std::string server_id();
    virtual std::string service_id();
    virtual std::string service_pid();
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid);
    virtual SrsStatisticStream *find_stream(std::string sid);
    virtual SrsStatisticStream *find_stream_by_url(std::string url);
    virtual SrsStatisticClient *find_client(std::string client_id);
    void reset();
};

// Mock ISrsHttpConn for testing SrsHttpxConn
class MockHttpConnForHttpxConn : public ISrsHttpConn
{
public:
    std::string remote_ip_;
    SrsContextId context_id_;
    ISrsKbpsDelta *delta_;
    // The CORS setting the owner gave, and whether it gave one.
    bool crossdomain_;
    bool crossdomain_set_;

public:
    MockHttpConnForHttpxConn();
    virtual ~MockHttpConnForHttpxConn();

public:
    virtual ISrsKbpsDelta *delta();
    virtual srs_error_t start();
    virtual srs_error_t cycle();
    virtual srs_error_t pull();
    virtual srs_error_t set_crossdomain_enabled(bool v);
    virtual srs_error_t set_auth_enabled(bool auth_enabled);
    virtual srs_error_t set_jsonp(bool v);
    virtual std::string remote_ip();
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual void expire();
};

// Mock ISrsAppConfig for the CORS setting of SrsHttpxConn, with the HTTP API's and the HTTP server's
// crossdomain set apart.
class MockAppConfigForHttpxConnCors : public MockAppConfig
{
public:
    bool http_api_crossdomain_;
    bool http_stream_crossdomain_;

public:
    MockAppConfigForHttpxConnCors();
    virtual ~MockAppConfigForHttpxConnCors();

public:
    virtual bool get_http_api_crossdomain();
    virtual bool get_http_stream_crossdomain();
};

// Mock ISrsFFMPEG for testing SrsEncoder
class MockFFMPEGForEncoder : public ISrsFFMPEG
{
public:
    bool initialize_called_;
    bool start_called_;
    int cycle_count_;
    int stop_count_;
    srs_error_t initialize_error_;
    srs_error_t initialize_transcode_error_;
    srs_error_t start_error_;
    srs_error_t cycle_error_;
    std::string input_;
    std::string output_;
    std::string log_;
    std::vector<std::string> iparams_;
    std::string oformat_;
    int initialize_copy_count_;

public:
    MockFFMPEGForEncoder();
    virtual ~MockFFMPEGForEncoder();

public:
    virtual void append_iparam(std::string iparam);
    virtual void set_oformat(std::string format);
    virtual std::string output();
    virtual srs_error_t initialize(std::string in, std::string out, std::string log);
    virtual srs_error_t initialize_transcode(SrsConfDirective *engine);
    virtual srs_error_t initialize_copy();
    virtual srs_error_t start();
    virtual srs_error_t cycle();
    virtual void stop();
    virtual void fast_stop();
    virtual void fast_kill();
    void reset();
};

// Mock ISrsAppConfig for testing SrsEncoder
class MockAppConfigForEncoder : public MockAppConfig
{
public:
    SrsConfDirective *transcode_directive_;
    bool transcode_enabled_;
    std::string transcode_ffmpeg_bin_;
    std::vector<SrsConfDirective *> transcode_engines_;
    bool engine_enabled_;
    std::string engine_output_;
    std::string target_scope_; // The scope for which to return transcode_directive_
    bool ff_log_enabled_;
    std::string ff_log_dir_;

public:
    MockAppConfigForEncoder();
    virtual ~MockAppConfigForEncoder();

public:
    virtual SrsConfDirective *get_transcode(std::string vhost, std::string scope);
    virtual bool get_transcode_enabled(SrsConfDirective *conf);
    virtual std::string get_transcode_ffmpeg(SrsConfDirective *conf);
    virtual std::vector<SrsConfDirective *> get_transcode_engines(SrsConfDirective *conf);
    virtual bool get_engine_enabled(SrsConfDirective *conf);
    virtual std::string get_engine_output(SrsConfDirective *conf);
    virtual bool get_ff_log_enabled();
    virtual std::string get_ff_log_dir();
    void reset();
};

// Mock ISrsAppFactory for testing SrsEncoder
class MockAppFactoryForEncoder : public SrsAppFactory
{
public:
    MockFFMPEGForEncoder *mock_ffmpeg_;
    // The coroutine for the next create_coroutine(), owned by the caller once handed out; a real one
    // is made when it is NULL.
    ISrsCoroutine *coroutine_;
    int create_coroutine_count_;

public:
    MockAppFactoryForEncoder();
    virtual ~MockAppFactoryForEncoder();

public:
    virtual ISrsFFMPEG *create_ffmpeg(std::string ffmpeg_bin);
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    void reset();
};

// Mock ISrsHttpMessage for testing SrsHttpHeartbeat
class MockHttpMessageForHeartbeat : public ISrsHttpMessage
{
public:
    int status_code_;
    std::string body_content_;
    ISrsReader *body_reader_;

public:
    MockHttpMessageForHeartbeat();
    virtual ~MockHttpMessageForHeartbeat();

public:
    virtual uint8_t method();
    virtual uint16_t status_code();
    virtual std::string method_str();
    virtual std::string url();
    virtual std::string host();
    virtual std::string path();
    virtual std::string query();
    virtual std::string ext();
    virtual srs_error_t body_read_all(std::string &body);
    virtual ISrsHttpResponseReader *body_reader();
    virtual int64_t content_length();
    virtual std::string query_get(std::string key);
    virtual SrsHttpHeader *header();
    virtual bool is_jsonp();
    virtual bool is_keep_alive();
    virtual ISrsRequest *to_request(std::string vhost);
    virtual std::string parse_rest_id(std::string pattern);
    virtual uint8_t message_type();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    virtual std::string uri();
    void reset();
};

// Mock ISrsHttpClient for testing SrsHttpHeartbeat
class MockHttpClientForHeartbeat : public ISrsHttpClient
{
public:
    bool initialize_called_;
    bool post_called_;
    std::string schema_;
    std::string host_;
    int port_;
    std::string path_;
    std::string request_body_;
    MockHttpMessageForHeartbeat *mock_response_;
    srs_error_t initialize_error_;
    srs_error_t post_error_;
    bool should_delete_response_;
    // Where post() copies the request body, because do_heartbeat() frees the client.
    std::string *request_body_out_;
    // Where post() copies the request path, for the same reason.
    std::string *path_out_;

public:
    MockHttpClientForHeartbeat();
    virtual ~MockHttpClientForHeartbeat();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm = SRS_HTTP_CLIENT_TIMEOUT);
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
    void reset();
};

// Mock ISrsAppFactory for testing SrsHttpHeartbeat
class MockAppFactoryForHeartbeat : public SrsAppFactory
{
public:
    MockHttpClientForHeartbeat *mock_http_client_;
    bool create_http_client_called_;
    bool should_delete_client_;

public:
    MockAppFactoryForHeartbeat();
    virtual ~MockAppFactoryForHeartbeat();

public:
    virtual ISrsHttpClient *create_http_client();
    void reset();
};

// Mock ISrsAppConfig for testing SrsHttpHeartbeat
class MockAppConfigForHeartbeat : public MockAppConfig
{
public:
    std::string heartbeat_url_;
    std::string heartbeat_device_id_;
    bool heartbeat_summaries_;
    bool heartbeat_ports_;
    int stats_network_;
    bool http_stream_enabled_;
    bool http_api_enabled_;
    bool srt_enabled_;
    bool rtsp_server_enabled_;
    bool rtc_server_enabled_;
    bool rtc_server_tcp_enabled_;
    std::vector<std::string> listens_;
    std::vector<std::string> http_stream_listens_;
    std::vector<std::string> http_api_listens_;
    std::vector<std::string> srt_listens_;
    std::vector<std::string> rtsp_server_listens_;
    std::vector<std::string> rtc_server_listens_;
    std::vector<std::string> rtc_server_tcp_listens_;
    std::string argv_;
    std::string cwd_;

public:
    MockAppConfigForHeartbeat();
    virtual ~MockAppConfigForHeartbeat();

public:
    virtual std::string get_heartbeat_url();
    virtual std::string get_heartbeat_device_id();
    virtual bool get_heartbeat_summaries();
    virtual bool get_heartbeat_ports();
    virtual int get_stats_network();
    virtual std::vector<std::string> get_listens();
    virtual bool get_http_stream_enabled();
    virtual std::vector<std::string> get_http_stream_listens();
    virtual bool get_http_api_enabled();
    virtual std::vector<std::string> get_http_api_listens();
    virtual bool get_srt_enabled();
    virtual std::vector<std::string> get_srt_listens();
    virtual bool get_rtsp_server_enabled();
    virtual std::vector<std::string> get_rtsp_server_listens();
    virtual bool get_rtc_server_enabled();
    virtual std::vector<std::string> get_rtc_server_listens();
    virtual bool get_rtc_server_tcp_enabled();
    virtual std::vector<std::string> get_rtc_server_tcp_listens();
    virtual std::string argv();
    virtual std::string cwd();
};

// Mock ISrsStatistic for testing the ids SrsHttpHeartbeat reports
class MockStatisticForHeartbeat : public MockAppStatistic
{
public:
    MockStatisticForHeartbeat();
    virtual ~MockStatisticForHeartbeat();

public:
    virtual std::string server_id();
    virtual std::string service_id();
    virtual std::string service_pid();
};

// Mock ISrsAppConfig for testing SrsCircuitBreaker
class MockAppConfigForCircuitBreaker : public MockAppConfig
{
public:
    bool circuit_breaker_enabled_;
    int high_threshold_;
    int high_pulse_;
    int critical_threshold_;
    int critical_pulse_;
    int dying_threshold_;
    int dying_pulse_;

public:
    MockAppConfigForCircuitBreaker();
    virtual ~MockAppConfigForCircuitBreaker();

public:
    virtual bool get_circuit_breaker();
    virtual int get_high_threshold();
    virtual int get_high_pulse();
    virtual int get_critical_threshold();
    virtual int get_critical_pulse();
    virtual int get_dying_threshold();
    virtual int get_dying_pulse();
};

// Mock ISrsFastTimer for testing SrsCircuitBreaker
class MockFastTimerForCircuitBreaker : public ISrsFastTimer
{
public:
    bool subscribe_called_;
    ISrsFastTimerHandler *subscribed_handler_;
    ISrsFastTimerHandler *unsubscribed_handler_;

public:
    MockFastTimerForCircuitBreaker();
    virtual ~MockFastTimerForCircuitBreaker();

public:
    virtual srs_error_t start();
    virtual void subscribe(ISrsFastTimerHandler *handler);
    virtual void unsubscribe(ISrsFastTimerHandler *handler);
    void reset();
};

// Mock ISrsSharedTimer for testing SrsCircuitBreaker
class MockSharedTimerForCircuitBreaker : public ISrsSharedTimer
{
public:
    MockFastTimerForCircuitBreaker *timer1s_;

public:
    MockSharedTimerForCircuitBreaker();
    virtual ~MockSharedTimerForCircuitBreaker();

public:
    virtual ISrsFastTimer *timer20ms();
    virtual ISrsFastTimer *timer100ms();
    virtual ISrsFastTimer *timer1s();
    virtual ISrsFastTimer *timer5s();
};

// Mock ISrsHost for testing SrsCircuitBreaker
class MockHostForCircuitBreaker : public ISrsHost
{
public:
    SrsProcSelfStat *proc_stat_;

public:
    MockHostForCircuitBreaker();
    virtual ~MockHostForCircuitBreaker();

public:
    virtual SrsProcSelfStat *self_proc_stat();
};

// Mock ISrsAppConfig for testing srs_path_build_timestamp and its callers
class MockAppConfigForBuildTimestamp : public MockAppConfig
{
public:
    bool utc_time_;
    int get_utc_time_count_;

public:
    MockAppConfigForBuildTimestamp();
    virtual ~MockAppConfigForBuildTimestamp();

public:
    virtual bool get_utc_time();
    virtual std::vector<std::string> get_listens();
    virtual std::string get_engine_output(SrsConfDirective *conf);
    virtual std::string get_ingest_input_type(SrsConfDirective *conf);
    virtual std::string get_ingest_input_url(SrsConfDirective *conf);
};

#endif
