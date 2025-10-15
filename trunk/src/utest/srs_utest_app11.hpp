//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP11_HPP
#define SRS_UTEST_APP11_HPP

/*
#include <srs_utest_app11.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_server.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_app6.hpp>

// Mock request class for testing SrsBufferCache
class MockBufferCacheRequest : public ISrsRequest
{
public:
    MockBufferCacheRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockBufferCacheRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Mock buffer cache for testing AAC stream encoder
class MockBufferCacheForAac : public ISrsBufferCache
{
public:
    int dump_cache_count_;
    ISrsLiveConsumer *last_consumer_;
    SrsRtmpJitterAlgorithm last_jitter_;

public:
    MockBufferCacheForAac();
    virtual ~MockBufferCacheForAac();
    virtual srs_error_t start();
    virtual void stop();
    virtual bool alive();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
    virtual srs_error_t update_auth(ISrsRequest *r);
};

// Mock SrsHttpxConn for testing SrsLiveStream (old version for backward compatibility)
class MockHttpxConn : public ISrsHttpConnOwner
{
public:
    bool enable_stat_;

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

// Mock SrsHttpxConn for testing SrsLiveStream - inherits from real SrsHttpxConn
class MockHttpxConnForLiveStream : public SrsHttpxConn
{
public:
    bool enable_stat_called_;

public:
    MockHttpxConnForLiveStream();
    virtual ~MockHttpxConnForLiveStream();

public:
    void set_enable_stat(bool v);
};

// Mock SrsHttpConn for testing SrsLiveStream - inherits from real SrsHttpConn
class MockHttpConnForLiveStream : public SrsHttpConn
{
public:
    MockHttpxConnForLiveStream *mock_handler_;

public:
    MockHttpConnForLiveStream();
    virtual ~MockHttpConnForLiveStream();

public:
    virtual ISrsHttpConnOwner *handler();
};

// Mock SrsHttpMessage for testing SrsLiveStream
class MockHttpMessageForLiveStream : public SrsHttpMessage
{
public:
    MockHttpConnForLiveStream *mock_conn_;

public:
    MockHttpMessageForLiveStream();
    virtual ~MockHttpMessageForLiveStream();

public:
    virtual ISrsConnection *connection();
    virtual std::string path();
};

// Mock ISrsAppConfig for testing SrsLiveStream::http_hooks_on_play() and http_hooks_on_stop()
class MockAppConfigForLiveStreamHooks : public MockAppConfig
{
public:
    bool http_hooks_enabled_;
    SrsConfDirective *on_play_directive_;
    SrsConfDirective *on_stop_directive_;

public:
    MockAppConfigForLiveStreamHooks();
    virtual ~MockAppConfigForLiveStreamHooks();

public:
    virtual bool get_vhost_http_hooks_enabled(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost);
};

// Mock ISrsHttpHooks for testing SrsLiveStream::http_hooks_on_play() and http_hooks_on_stop()
class MockHttpHooksForLiveStream : public ISrsHttpHooks
{
public:
    std::vector<std::pair<std::string, ISrsRequest *> > on_play_calls_;
    int on_play_count_;
    srs_error_t on_play_error_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_stop_calls_;
    int on_stop_count_;

public:
    MockHttpHooksForLiveStream();
    virtual ~MockHttpHooksForLiveStream();

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

// Mock ISrsAsyncCallWorker for testing SrsHttpStreamServer
class MockAsyncCallWorker : public ISrsAsyncCallWorker
{
public:
    int execute_count_;
    std::vector<ISrsAsyncCallTask *> tasks_;

public:
    MockAsyncCallWorker();
    virtual ~MockAsyncCallWorker();

public:
    virtual srs_error_t execute(ISrsAsyncCallTask *t);
    virtual srs_error_t start();
    virtual void stop();
};

// Mock ISrsLiveStream for testing SrsHttpStreamDestroy
class MockLiveStreamForDestroy : public ISrsLiveStream
{
public:
    bool alive_;
    bool expired_;

public:
    MockLiveStreamForDestroy();
    virtual ~MockLiveStreamForDestroy();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    virtual srs_error_t update_auth(ISrsRequest *r);
    virtual bool alive();
    virtual void expire();
};

// Mock ISrsStatistic for testing SrsLiveStream::serve_http_impl
class MockStatisticForLiveStream : public ISrsStatistic
{
public:
    int on_client_count_;
    srs_error_t on_client_error_;

public:
    MockStatisticForLiveStream();
    virtual ~MockStatisticForLiveStream();

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
};

// Mock ISrsSecurity for testing SrsLiveStream::serve_http_impl
class MockSecurityForLiveStream : public ISrsSecurity
{
public:
    srs_error_t check_error_;
    int check_count_;

public:
    MockSecurityForLiveStream();
    virtual ~MockSecurityForLiveStream();

public:
    virtual srs_error_t check(SrsRtmpConnType type, std::string ip, ISrsRequest *req);
};

// Mock ISrsBufferCache for testing SrsHttpStreamDestroy
class MockBufferCacheForDestroy : public ISrsBufferCache
{
public:
    bool alive_;
    bool stopped_;

public:
    MockBufferCacheForDestroy();
    virtual ~MockBufferCacheForDestroy();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual bool alive();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
    virtual srs_error_t update_auth(ISrsRequest *r);
};

// Mock ISrsBufferEncoder for testing SrsLiveStream::streaming_send_messages
class MockBufferEncoderForStreamingSend : public ISrsBufferEncoder
{
public:
    int write_audio_count_;
    int write_video_count_;
    int write_metadata_count_;
    std::vector<int64_t> audio_timestamps_;
    std::vector<int64_t> video_timestamps_;
    std::vector<int64_t> metadata_timestamps_;
    srs_error_t write_error_;

public:
    MockBufferEncoderForStreamingSend();
    virtual ~MockBufferEncoderForStreamingSend();

public:
    virtual srs_error_t initialize(SrsFileWriter *w, ISrsBufferCache *c);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_metadata(int64_t timestamp, char *data, int size);
    virtual bool has_cache();
    virtual srs_error_t dump_cache(ISrsLiveConsumer *consumer, SrsRtmpJitterAlgorithm jitter);
    void reset();
};

// Mock ISrsAppConfig for testing SrsHttpStreamServer
class MockAppConfigForHttpStreamServer : public MockAppConfig
{
public:
    bool http_remux_enabled_;
    std::string http_remux_mount_;
    SrsConfDirective *vhost_directive_;

public:
    MockAppConfigForHttpStreamServer();
    virtual ~MockAppConfigForHttpStreamServer();

public:
    virtual bool get_vhost_http_remux_enabled(std::string vhost);
    virtual std::string get_vhost_http_remux_mount(std::string vhost);
    virtual SrsConfDirective *get_vhost(std::string vhost, bool try_default_vhost = true);
    virtual bool get_vhost_enabled(SrsConfDirective *conf);
};

// Mock SrsHttpMessage for testing SrsHttpStreamServer::dynamic_match
class MockHttpMessageForDynamicMatch : public SrsHttpMessage
{
public:
    std::string path_;
    std::string ext_;
    std::string host_;
    MockHttpConn *mock_conn_;

public:
    MockHttpMessageForDynamicMatch();
    virtual ~MockHttpMessageForDynamicMatch();

public:
    virtual std::string path();
    virtual std::string ext();
    virtual std::string host();
};

// Mock SrsHttpMessage for testing HTTP API response functions
class MockHttpMessageForApiResponse : public SrsHttpMessage
{
public:
    MockHttpConn *mock_conn_;
    bool is_jsonp_;
    std::string callback_;
    std::string path_;
    std::map<std::string, std::string> query_params_;

public:
    MockHttpMessageForApiResponse();
    virtual ~MockHttpMessageForApiResponse();

public:
    virtual bool is_jsonp();
    virtual std::string query_get(std::string key);
    virtual std::string path();
};

// Mock ISrsSignalHandler for testing SrsGoApiRaw
class MockSignalHandler : public ISrsSignalHandler
{
public:
    int signal_received_;
    int signal_count_;

public:
    MockSignalHandler();
    virtual ~MockSignalHandler();

public:
    virtual void on_signal(int signo);
    void reset();
};

// Mock ISrsAppConfig for testing SrsGoApiRaw
class MockAppConfigForRawApi : public MockAppConfig
{
public:
    bool raw_api_;
    bool allow_reload_;
    bool allow_query_;
    bool allow_update_;
    srs_error_t raw_to_json_error_;

public:
    MockAppConfigForRawApi();
    virtual ~MockAppConfigForRawApi();

public:
    virtual bool get_raw_api();
    virtual bool get_raw_api_allow_reload();
    virtual bool get_raw_api_allow_query();
    virtual bool get_raw_api_allow_update();
    virtual srs_error_t raw_to_json(SrsJsonObject *obj);
};

#endif
