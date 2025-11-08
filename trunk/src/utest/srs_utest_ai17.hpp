//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI17_HPP
#define SRS_UTEST_AI17_HPP

/*
#include <srs_utest_ai17.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_dash.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>

// Mock ISrsMpdWriter for testing MPD fragment generation
class MockMpdWriter : public ISrsMpdWriter
{
public:
    std::string file_home_;
    std::string file_name_;
    int64_t sequence_number_;
    bool get_fragment_called_;

public:
    MockMpdWriter();
    virtual ~MockMpdWriter();

public:
    virtual srs_error_t get_fragment(bool video, std::string &home, std::string &filename, int64_t time, int64_t &sn);

    // Stub implementations for other ISrsMpdWriter methods
    virtual void dispose() {}
    virtual srs_error_t initialize(ISrsRequest *r) { return srs_success; }
    virtual srs_error_t on_publish() { return srs_success; }
    virtual void on_unpublish() {}
    virtual srs_error_t write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments) { return srs_success; }
    virtual void set_availability_start_time(srs_utime_t t) {}
    virtual srs_utime_t get_availability_start_time() { return 0; }
};

// Mock ISrsMp4M2tsSegmentEncoder for testing MP4 encoding
class MockMp4SegmentEncoder : public ISrsMp4M2tsSegmentEncoder
{
public:
    bool initialize_called_;
    bool write_sample_called_;
    bool flush_called_;
    uint32_t last_sequence_;
    srs_utime_t last_basetime_;
    uint32_t last_tid_;
    SrsMp4HandlerType last_handler_type_;
    uint32_t last_dts_;
    uint32_t last_pts_;
    uint32_t last_sample_size_;

public:
    MockMp4SegmentEncoder();
    virtual ~MockMp4SegmentEncoder();

public:
    virtual srs_error_t initialize(ISrsWriter *w, uint32_t sequence, srs_utime_t basetime, uint32_t tid);
    virtual srs_error_t write_sample(SrsMp4HandlerType ht, uint16_t ft, uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample);
    virtual srs_error_t flush(uint64_t &dts);
};

// Mock ISrsFragment for testing SrsInitMp4 delegation
class MockFragment : public ISrsFragment
{
public:
    std::string path_;
    std::string tmppath_;
    uint64_t number_;
    srs_utime_t duration_;
    srs_utime_t start_dts_;

    bool set_path_called_;
    bool tmppath_called_;
    bool rename_called_;
    bool append_called_;
    bool create_dir_called_;
    bool set_number_called_;
    bool number_called_;
    bool duration_called_;
    bool unlink_tmpfile_called_;
    bool get_start_dts_called_;
    bool unlink_file_called_;

    int64_t append_dts_;

public:
    MockFragment();
    virtual ~MockFragment();

public:
    virtual void set_path(std::string v);
    virtual std::string tmppath();
    virtual srs_error_t rename();
    virtual void append(int64_t dts);
    virtual srs_error_t create_dir();
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
    virtual srs_utime_t duration();
    virtual srs_error_t unlink_tmpfile();
    virtual srs_utime_t get_start_dts();
    virtual srs_error_t unlink_file();
};

// Mock ISrsFragmentWindow for testing SrsDashController
class MockFragmentWindow : public ISrsFragmentWindow
{
public:
    bool dispose_called_;
    bool append_called_;
    bool shrink_called_;
    bool clear_expired_called_;

public:
    MockFragmentWindow();
    virtual ~MockFragmentWindow();

public:
    virtual void dispose();
    virtual void append(ISrsFragment *fragment);
    virtual void shrink(srs_utime_t window);
    virtual void clear_expired(bool delete_files);
    virtual srs_utime_t max_duration();
    virtual bool empty();
    virtual ISrsFragment *first();
    virtual int size();
    virtual ISrsFragment *at(int index);
};

// Mock ISrsFragmentedMp4 for testing SrsDashController
class MockFragmentedMp4 : public ISrsFragmentedMp4
{
public:
    bool initialize_called_;
    bool write_called_;
    bool reap_called_;
    bool unlink_tmpfile_called_;
    srs_error_t unlink_tmpfile_error_;
    srs_utime_t duration_;

public:
    MockFragmentedMp4();
    virtual ~MockFragmentedMp4();

public:
    virtual srs_error_t initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid);
    virtual srs_error_t write(SrsMediaPacket *shared_msg, SrsFormat *format);
    virtual srs_error_t reap(uint64_t &dts);

public:
    // ISrsFragment interface implementations
    virtual void set_path(std::string v);
    virtual std::string tmppath();
    virtual srs_error_t rename();
    virtual void append(int64_t dts);
    virtual srs_error_t create_dir();
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
    virtual srs_utime_t duration();
    virtual srs_error_t unlink_tmpfile();
    virtual srs_utime_t get_start_dts();
    virtual srs_error_t unlink_file();
};

// Forward declaration
class MockDashAppFactory;

// Mock ISrsInitMp4 for testing SrsDashController refresh_init_mp4
class MockInitMp4 : public ISrsInitMp4
{
public:
    bool set_path_called_;
    bool write_called_;
    bool rename_called_;
    std::string path_;
    bool video_;
    int tid_;
    MockDashAppFactory *factory_; // Reference to factory to copy state on destruction

public:
    MockInitMp4(MockDashAppFactory *factory);
    virtual ~MockInitMp4();

public:
    virtual srs_error_t write(SrsFormat *format, bool video, int tid);

public:
    // ISrsFragment interface implementations
    virtual void set_path(std::string v);
    virtual std::string tmppath();
    virtual srs_error_t rename();
    virtual void append(int64_t dts);
    virtual srs_error_t create_dir();
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
    virtual srs_utime_t duration();
    virtual srs_error_t unlink_tmpfile();
    virtual srs_utime_t get_start_dts();
    virtual srs_error_t unlink_file();
};

// Mock ISrsAppFactory for testing SrsDashController
class MockDashAppFactory : public SrsAppFactory
{
public:
    // Track the last created init mp4 state (before it's deleted)
    bool last_set_path_called_;
    bool last_write_called_;
    bool last_rename_called_;
    std::string last_path_;
    bool last_video_;
    int last_tid_;

public:
    MockDashAppFactory();
    virtual ~MockDashAppFactory();

public:
    virtual ISrsInitMp4 *create_init_mp4();
};

// Mock ISrsDashController for testing SrsDash lifecycle
class MockDashController : public ISrsDashController
{
public:
    bool initialize_called_;
    bool on_publish_called_;
    bool on_unpublish_called_;
    bool dispose_called_;

public:
    MockDashController();
    virtual ~MockDashController();

public:
    virtual void dispose();
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
};

// Mock SrsRtcConnection for testing NACK API
class MockRtcConnectionForNackApi
{
public:
    int simulate_nack_drop_value_;
    bool simulate_nack_drop_called_;

public:
    MockRtcConnectionForNackApi();
    ~MockRtcConnectionForNackApi();

public:
    void simulate_nack_drop(int nn);
};

// Mock ISrsRtcApiServer for testing RTC API
class MockRtcApiServer : public ISrsRtcApiServer
{
public:
    bool create_session_called_;
    std::string session_id_;
    std::string local_sdp_str_;
    MockRtcConnectionForNackApi *mock_connection_;
    std::string find_username_;

public:
    MockRtcApiServer();
    virtual ~MockRtcApiServer();

public:
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession);
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag);
};

// Mock ISrsStatistic for testing RTC API
class MockStatisticForRtcApi : public ISrsStatistic
{
public:
    std::string server_id_;
    std::string service_id_;
    std::string service_pid_;

public:
    MockStatisticForRtcApi();
    virtual ~MockStatisticForRtcApi();

public:
    virtual void on_disconnect(std::string id, srs_error_t err);
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                      SrsAudioChannels asound_type, SrsAacObjectType aac_object);
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
};

// Mock ISrsHttpMessage for testing RTC API
class MockHttpMessageForRtcApi : public SrsHttpMessage
{
public:
    MockHttpConn *mock_conn_;
    std::string body_content_;
    std::map<std::string, std::string> query_params_;
    uint8_t method_;

public:
    MockHttpMessageForRtcApi();
    virtual ~MockHttpMessageForRtcApi();

public:
    virtual srs_error_t body_read_all(std::string &body);
    virtual std::string query_get(std::string key);
    virtual uint8_t method();
    void set_method(uint8_t method);
};

// Mock ISrsAppConfig for testing SrsGoApiRtcPlay::serve_http()
class MockAppConfigForRtcPlay : public MockAppConfig
{
public:
    std::string dtls_role_;
    std::string dtls_version_;
    bool rtc_server_enabled_;
    bool rtc_enabled_;
    bool vhost_is_edge_;
    bool rtc_from_rtmp_;
    bool http_hooks_enabled_;
    SrsConfDirective *on_play_directive_;

public:
    MockAppConfigForRtcPlay();
    virtual ~MockAppConfigForRtcPlay();

public:
    virtual std::string get_rtc_dtls_role(std::string vhost);
    virtual std::string get_rtc_dtls_version(std::string vhost);
    virtual bool get_rtc_server_enabled();
    virtual bool get_rtc_enabled(std::string vhost);
    virtual bool get_vhost_is_edge(std::string vhost);
    virtual bool get_rtc_from_rtmp(std::string vhost);
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost);
};

// Mock ISrsHttpHooks for testing SrsGoApiRtcPlay::serve_http()
class MockHttpHooksForRtcPlay : public ISrsHttpHooks
{
public:
    int on_play_count_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_play_calls_;

public:
    MockHttpHooksForRtcPlay();
    virtual ~MockHttpHooksForRtcPlay();

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
};

// Mock SrsRtcConnection for testing SrsGoApiRtcPlay::serve_http()
class MockRtcConnectionForPlay
{
public:
    std::string username_;
    std::string token_;

public:
    MockRtcConnectionForPlay();
    ~MockRtcConnectionForPlay();

public:
    std::string username();
    std::string token();
};

// Mock ISrsRtcApiServer for testing SrsGoApiRtcPlay::serve_http()
class MockRtcApiServerForPlay : public ISrsRtcApiServer
{
public:
    bool create_session_called_;
    MockRtcConnectionForPlay *mock_connection_;

public:
    MockRtcApiServerForPlay();
    virtual ~MockRtcApiServerForPlay();

public:
    virtual srs_error_t create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, ISrsRtcConnection **psession);
    virtual ISrsRtcConnection *find_rtc_session_by_username(const std::string &ufrag);
};

// Mock ISrsHttpResponseReader for testing SrsHttpHooks
class MockHttpResponseReaderForHooks : public ISrsHttpResponseReader
{
public:
    std::string content_;
    size_t read_pos_;
    bool eof_;

public:
    MockHttpResponseReaderForHooks();
    virtual ~MockHttpResponseReaderForHooks();

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
    virtual bool eof();
};

// Mock ISrsHttpMessage for testing SrsHttpHooks::on_connect
class MockHttpMessageForHooks : public ISrsHttpMessage
{
public:
    int status_code_;
    std::string body_content_;
    MockHttpResponseReaderForHooks *body_reader_;

public:
    MockHttpMessageForHooks();
    virtual ~MockHttpMessageForHooks();

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
    virtual int request_header_count();
    virtual std::string request_header_key_at(int index);
    virtual std::string request_header_value_at(int index);
    virtual std::string get_request_header(std::string name);
    virtual ISrsRequest *to_request(std::string vhost);
    virtual bool is_chunked();
    virtual bool is_keep_alive();
    virtual bool is_jsonp();
    virtual std::string jsonp();
    virtual bool require_crossdomain();
    virtual srs_error_t enter_infinite_chunked();
    virtual srs_error_t end_infinite_chunked();
    virtual uint8_t message_type();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    virtual std::string uri();
    virtual std::string parse_rest_id(std::string pattern);
    virtual SrsHttpHeader *header();
};

// Mock ISrsHttpClient for testing SrsHttpHooks::on_connect
class MockHttpClientForHooks : public ISrsHttpClient
{
public:
    bool initialize_called_;
    bool post_called_;
    bool get_called_;
    std::string schema_;
    std::string host_;
    int port_;
    std::string path_;
    std::string request_body_;
    MockHttpMessageForHooks *mock_response_;
    srs_error_t initialize_error_;
    srs_error_t post_error_;
    srs_error_t get_error_;

public:
    MockHttpClientForHooks();
    virtual ~MockHttpClientForHooks();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm);
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// Mock ISrsAppFactory for testing SrsHttpHooks::on_connect
class MockAppFactoryForHooks : public SrsAppFactory
{
public:
    MockHttpClientForHooks *mock_http_client_;

public:
    MockAppFactoryForHooks();
    virtual ~MockAppFactoryForHooks();

public:
    virtual ISrsHttpClient *create_http_client();
};

// Mock ISrsStatistic for testing SrsHttpHooks::on_connect
class MockStatisticForHooks : public ISrsStatistic
{
public:
    std::string server_id_;
    std::string service_id_;

public:
    MockStatisticForHooks();
    virtual ~MockStatisticForHooks();

public:
    virtual void on_disconnect(std::string id, srs_error_t err);
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                      SrsAudioChannels asound_type, SrsAacObjectType aac_object);
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
};

#endif
