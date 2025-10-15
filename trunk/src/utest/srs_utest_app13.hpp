//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP13_HPP
#define SRS_UTEST_APP13_HPP

/*
#include <srs_utest_app13.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_statistic.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#ifdef SRS_RTSP
#include <srs_protocol_rtsp_stack.hpp>
#endif
#include <srs_utest_app12.hpp>
#include <srs_utest_app6.hpp>

// Mock request class for testing edge upstream
class MockEdgeRequest : public ISrsRequest
{
public:
    MockEdgeRequest(std::string vhost = "__defaultVhost__", std::string app = "live", std::string stream = "test");
    virtual ~MockEdgeRequest();
    virtual ISrsRequest *copy();
    virtual std::string get_stream_url();
    virtual void update_auth(ISrsRequest *req);
    virtual void strip();
    virtual ISrsRequest *as_http();
};

// Mock config class for testing edge upstream
class MockEdgeConfig : public MockAppConfig
{
public:
    SrsConfDirective *edge_origin_directive_;
    std::string edge_transform_vhost_;
    int chunk_size_;

public:
    MockEdgeConfig();
    virtual ~MockEdgeConfig();
    void reset();

public:
    // Override methods needed for edge upstream testing
    virtual SrsConfDirective *get_vhost_edge_origin(std::string vhost);
    virtual std::string get_vhost_edge_transform_vhost(std::string vhost);
    virtual int get_chunk_size(std::string vhost);
    virtual srs_utime_t get_vhost_edge_origin_connect_timeout(std::string vhost);
    virtual srs_utime_t get_vhost_edge_origin_stream_timeout(std::string vhost);
    // DVR methods
    virtual std::string get_dvr_path(std::string vhost);
    virtual int get_dvr_time_jitter(std::string vhost);
    virtual bool get_dvr_wait_keyframe(std::string vhost);
    virtual bool get_dvr_enabled(std::string vhost);
    virtual srs_utime_t get_dvr_duration(std::string vhost);
    virtual SrsConfDirective *get_dvr_apply(std::string vhost);
    virtual std::string get_dvr_plan(std::string vhost);
};

// Mock RTMP client for testing edge upstream
class MockEdgeRtmpClient : public ISrsBasicRtmpClient
{
public:
    bool connect_called_;
    bool play_called_;
    bool close_called_;
    bool recv_message_called_;
    bool decode_message_called_;
    bool set_recv_timeout_called_;
    bool kbps_sample_called_;
    srs_error_t connect_error_;
    srs_error_t play_error_;
    std::string play_stream_;
    srs_utime_t recv_timeout_;
    std::string kbps_label_;
    srs_utime_t kbps_age_;

public:
    MockEdgeRtmpClient();
    virtual ~MockEdgeRtmpClient();

public:
    virtual srs_error_t connect();
    virtual void close();
    virtual srs_error_t publish(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual srs_error_t play(int chunk_size, bool with_vhost = true, std::string *pstream = NULL);
    virtual void kbps_sample(const char *label, srs_utime_t age);
    virtual int sid();
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs);
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg);
    virtual void set_recv_timeout(srs_utime_t timeout);
};

// Mock app factory for testing edge upstream
class MockEdgeAppFactory : public SrsAppFactory
{
public:
    MockEdgeRtmpClient *mock_client_;

public:
    MockEdgeAppFactory();
    virtual ~MockEdgeAppFactory();

public:
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
};

// Mock HTTP client for testing edge FLV upstream
class MockEdgeHttpClient : public ISrsHttpClient
{
public:
    bool initialize_called_;
    bool get_called_;
    bool set_recv_timeout_called_;
    bool kbps_sample_called_;
    srs_error_t initialize_error_;
    srs_error_t get_error_;
    ISrsHttpMessage *mock_response_;
    std::string schema_;
    std::string host_;
    int port_;
    std::string path_;
    std::string kbps_label_;
    srs_utime_t kbps_age_;

public:
    MockEdgeHttpClient();
    virtual ~MockEdgeHttpClient();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm);
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// Mock HTTP message for testing edge FLV upstream
class MockEdgeHttpMessage : public ISrsHttpMessage
{
public:
    int status_code_;
    SrsHttpHeader *header_;
    ISrsHttpResponseReader *body_reader_;

public:
    MockEdgeHttpMessage();
    virtual ~MockEdgeHttpMessage();

public:
    virtual uint8_t message_type();
    virtual uint8_t method();
    virtual uint16_t status_code();
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    virtual std::string uri();
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
    virtual std::string parse_rest_id(std::string pattern);
};

// Mock file reader for testing edge FLV upstream
class MockEdgeFileReader : public ISrsFileReader
{
public:
    char *data_;
    int size_;
    int pos_;

public:
    MockEdgeFileReader(const char *data, int size);
    virtual ~MockEdgeFileReader();

public:
    virtual srs_error_t open(std::string p);
    virtual void close();
    virtual bool is_open();
    virtual int64_t tellg();
    virtual void skip(int64_t size);
    virtual int64_t seek2(int64_t offset);
    virtual int64_t filesize();
    virtual srs_error_t read(void *buf, size_t count, ssize_t *pnread);
    virtual srs_error_t lseek(off_t offset, int whence, off_t *seeked);
};

// Mock FLV decoder for testing edge FLV upstream
class MockEdgeFlvDecoder : public ISrsFlvDecoder
{
public:
    bool initialize_called_;
    bool read_header_called_;
    bool read_previous_tag_size_called_;

public:
    MockEdgeFlvDecoder();
    virtual ~MockEdgeFlvDecoder();

public:
    virtual srs_error_t initialize(ISrsReader *fr);
    virtual srs_error_t read_header(char header[9]);
    virtual srs_error_t read_tag_header(char *ptype, int32_t *pdata_size, uint32_t *ptime);
    virtual srs_error_t read_tag_data(char *data, int32_t size);
    virtual srs_error_t read_previous_tag_size(char previous_tag_size[4]);
};

// Mock app factory for testing edge FLV upstream
class MockEdgeFlvAppFactory : public SrsAppFactory
{
public:
    MockEdgeHttpClient *mock_http_client_;
    MockEdgeFileReader *mock_file_reader_;
    MockEdgeFlvDecoder *mock_flv_decoder_;

public:
    MockEdgeFlvAppFactory();
    virtual ~MockEdgeFlvAppFactory();

public:
    virtual ISrsHttpClient *create_http_client();
    virtual ISrsFileReader *create_http_file_reader(ISrsHttpResponseReader *r);
    virtual ISrsFlvDecoder *create_flv_decoder();
};

// Mock play edge for testing SrsEdgeIngester
class MockPlayEdge : public ISrsPlayEdge
{
public:
    int on_ingest_play_count_;
    srs_error_t on_ingest_play_error_;

public:
    MockPlayEdge();
    virtual ~MockPlayEdge();

public:
    virtual srs_error_t on_ingest_play();
    void reset();
};

// Mock edge upstream for testing SrsEdgeIngester::process_publish_message
class MockEdgeUpstreamForIngester : public ISrsEdgeUpstream
{
public:
    bool decode_message_called_;
    SrsRtmpCommand *decode_message_packet_;
    srs_error_t decode_message_error_;

public:
    MockEdgeUpstreamForIngester();
    virtual ~MockEdgeUpstreamForIngester();

public:
    virtual srs_error_t connect(ISrsRequest *r, ISrsLbRoundRobin *lb);
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual void close();
    virtual void selected(std::string &server, int &port);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
    void reset();
};

// Mock publish edge for testing SrsEdgeForwarder
class MockPublishEdge : public ISrsPublishEdge
{
public:
    MockPublishEdge();
    virtual ~MockPublishEdge();
};

// Mock edge ingester for testing SrsPlayEdge
class MockEdgeIngester : public ISrsEdgeIngester
{
public:
    bool initialize_called_;
    bool start_called_;
    bool stop_called_;
    srs_error_t initialize_error_;
    srs_error_t start_error_;

public:
    MockEdgeIngester();
    virtual ~MockEdgeIngester();

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPlayEdge *e, ISrsRequest *r);
    virtual srs_error_t start();
    virtual void stop();
    void reset();
};

// Mock edge forwarder for testing SrsPublishEdge
class MockEdgeForwarder : public ISrsEdgeForwarder
{
public:
    bool initialize_called_;
    bool start_called_;
    bool stop_called_;
    bool set_queue_size_called_;
    bool proxy_called_;
    srs_utime_t queue_size_;
    srs_error_t initialize_error_;
    srs_error_t start_error_;
    srs_error_t proxy_error_;

public:
    MockEdgeForwarder();
    virtual ~MockEdgeForwarder();

public:
    virtual void set_queue_size(srs_utime_t queue_size);
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r);
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t proxy(SrsRtmpCommonMessage *msg);
    void reset();
};

// Mock ISrsStatistic for testing SrsRtspPlayStream
class MockStatisticForRtspPlayStream : public ISrsStatistic
{
public:
    int on_client_count_;
    srs_error_t on_client_error_;

public:
    MockStatisticForRtspPlayStream();
    virtual ~MockStatisticForRtspPlayStream();

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

// Mock ISrsRtspSourceManager for testing SrsRtspPlayStream
class MockRtspSourceManager : public ISrsRtspSourceManager
{
public:
    int fetch_or_create_count_;
    srs_error_t fetch_or_create_error_;
    SrsSharedPtr<SrsRtspSource> mock_source_;

public:
    MockRtspSourceManager();
    virtual ~MockRtspSourceManager();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps);
    virtual SrsSharedPtr<SrsRtspSource> fetch(ISrsRequest *r);
    void reset();
};

// Mock ISrsRtspSendTrack for testing SrsRtspPlayStream
class MockRtspSendTrack : public ISrsRtspSendTrack
{
public:
    std::string track_id_;
    SrsRtcTrackDescription *track_desc_;
    bool track_status_;
    int on_rtp_count_;
    uint32_t last_ssrc_;
    uint16_t last_sequence_;

public:
    MockRtspSendTrack(std::string track_id, SrsRtcTrackDescription *desc);
    virtual ~MockRtspSendTrack();

public:
    virtual bool set_track_status(bool active);
    virtual std::string get_track_id();
    virtual SrsRtcTrackDescription *track_desc();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    void reset();
};

// Mock ISrsAppFactory for testing SrsRtspPlayStream
class MockAppFactoryForRtspPlayStream : public SrsAppFactory
{
public:
    int create_rtsp_audio_send_track_count_;
    int create_rtsp_video_send_track_count_;

public:
    MockAppFactoryForRtspPlayStream();
    virtual ~MockAppFactoryForRtspPlayStream();

public:
    virtual ISrsRtspSendTrack *create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ISrsRtspSendTrack *create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    void reset();
};

// Mock ISrsRtspStack for testing SrsRtspConnection
class MockRtspStack : public ISrsRtspStack
{
public:
    bool send_message_called_;
    int last_response_seq_;
    std::string last_response_session_;
    std::string last_response_type_; // "OPTIONS", "DESCRIBE", "SETUP", "PLAY", "TEARDOWN"
    srs_error_t send_message_error_;

public:
    MockRtspStack();
    virtual ~MockRtspStack();

public:
    virtual srs_error_t recv_message(SrsRtspRequest **preq);
    virtual srs_error_t send_message(SrsRtspResponse *res);
    void reset();
};

// Mock ISrsRtspPlayStream for testing SrsRtspConnection::do_play and do_teardown
class MockRtspPlayStream : public ISrsRtspPlayStream
{
public:
    bool initialize_called_;
    bool start_called_;
    bool stop_called_;
    bool set_all_tracks_status_called_;
    bool set_all_tracks_status_value_;
    srs_error_t initialize_error_;
    srs_error_t start_error_;

public:
    MockRtspPlayStream();
    virtual ~MockRtspPlayStream();

public:
    virtual srs_error_t initialize(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations);
    virtual srs_error_t start();
    virtual void stop();
    virtual void set_all_tracks_status(bool status);
    void reset();
};

// Mock ISrsDvrPlan for testing SrsDvrSegmenter
class MockDvrPlan : public ISrsDvrPlan
{
public:
    bool on_publish_called_;
    bool on_unpublish_called_;
    srs_error_t on_publish_error_;

public:
    MockDvrPlan();
    virtual ~MockDvrPlan();

public:
    virtual srs_error_t initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r);
    virtual srs_error_t on_publish(ISrsRequest *r);
    virtual void on_unpublish();
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual srs_error_t on_reap_segment();
};

// Mock ISrsHttpHooks for testing SrsDvrAsyncCallOnDvr
class MockHttpHooksForDvrAsyncCall : public ISrsHttpHooks
{
public:
    struct OnDvrCall {
        SrsContextId cid_;
        std::string url_;
        ISrsRequest *req_;
        std::string file_;
    };
    std::vector<OnDvrCall> on_dvr_calls_;
    int on_dvr_count_;
    srs_error_t on_dvr_error_;

public:
    MockHttpHooksForDvrAsyncCall();
    virtual ~MockHttpHooksForDvrAsyncCall();

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

// Mock ISrsFlvTransmuxer for testing SrsDvrFlvSegmenter
class MockFlvTransmuxer : public ISrsFlvTransmuxer
{
public:
    bool write_header_called_;
    bool write_metadata_called_;
    char metadata_type_;
    int metadata_size_;
    srs_error_t write_metadata_error_;

public:
    MockFlvTransmuxer();
    virtual ~MockFlvTransmuxer();

public:
    virtual srs_error_t initialize(ISrsWriter *fw);
    virtual void set_drop_if_not_match(bool v);
    virtual bool drop_if_not_match();
    virtual srs_error_t write_header(bool has_video = true, bool has_audio = true);
    virtual srs_error_t write_header(char flv_header[9]);
    virtual srs_error_t write_metadata(char type, char *data, int size);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_tags(SrsMediaPacket **msgs, int count);
};

// Mock ISrsMp4Encoder for testing SrsDvrMp4Segmenter
class MockMp4Encoder : public ISrsMp4Encoder
{
public:
    bool initialize_called_;
    bool write_sample_called_;
    bool flush_called_;
    bool set_audio_codec_called_;
    SrsMp4HandlerType last_handler_type_;
    uint16_t last_frame_type_;
    uint16_t last_codec_type_;
    uint32_t last_dts_;
    uint32_t last_pts_;
    uint32_t last_sample_size_;
    SrsAudioCodecId last_audio_codec_;
    SrsAudioSampleRate last_audio_sample_rate_;
    SrsAudioSampleBits last_audio_sound_bits_;
    SrsAudioChannels last_audio_channels_;

public:
    MockMp4Encoder();
    virtual ~MockMp4Encoder();

public:
    virtual srs_error_t initialize(ISrsWriteSeeker *ws);
    virtual srs_error_t write_sample(SrsFormat *format, SrsMp4HandlerType ht, uint16_t ft, uint16_t ct,
                                     uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample);
    virtual srs_error_t flush();
    virtual void set_audio_codec(SrsAudioCodecId vcodec, SrsAudioSampleRate sample_rate, SrsAudioSampleBits sound_bits, SrsAudioChannels channels);
    void reset();
};

// Mock ISrsAppFactory for testing SrsDvrMp4Segmenter
class MockDvrAppFactory : public ISrsAppFactory
{
public:
    MockMp4Encoder *mock_mp4_encoder_;

public:
    MockDvrAppFactory();
    virtual ~MockDvrAppFactory();

public:
    virtual ISrsFileWriter *create_file_writer();
    virtual ISrsFileWriter *create_enc_file_writer();
    virtual ISrsFileReader *create_file_reader();
    virtual SrsPath *create_path();
    virtual SrsLiveSource *create_live_source();
    virtual ISrsOriginHub *create_origin_hub();
    virtual ISrsHourGlass *create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval);
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
    virtual ISrsHttpClient *create_http_client();
    virtual ISrsHttpResponseReader *create_http_response_reader(ISrsHttpResponseReader *r);
    virtual ISrsFileReader *create_http_file_reader(ISrsHttpResponseReader *r);
    virtual ISrsFlvDecoder *create_flv_decoder();
    virtual ISrsBasicRtmpClient *create_basic_rtmp_client(std::string url, srs_utime_t ctm, srs_utime_t stm);
#ifdef SRS_RTSP
    virtual ISrsRtspSendTrack *create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
    virtual ISrsRtspSendTrack *create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc);
#endif
    virtual ISrsFlvTransmuxer *create_flv_transmuxer();
    virtual ISrsMp4Encoder *create_mp4_encoder();
    virtual ISrsDvrSegmenter *create_dvr_flv_segmenter();
    virtual ISrsDvrSegmenter *create_dvr_mp4_segmenter();
#ifdef SRS_GB28181
    virtual ISrsGbMediaTcpConn *create_gb_media_tcp_conn();
    virtual ISrsGbSession *create_gb_session();
#endif
    virtual ISrsInitMp4 *create_init_mp4();
    virtual ISrsFragmentWindow *create_fragment_window();
    virtual ISrsFragmentedMp4 *create_fragmented_mp4();
    virtual ISrsIpListener *create_tcp_listener(ISrsTcpHandler *handler);
    virtual ISrsRtcConnection *create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid);
    virtual ISrsFFMPEG *create_ffmpeg(std::string ffmpeg_bin);
    virtual ISrsIngesterFFMPEG *create_ingester_ffmpeg();
    // ISrsKernelFactory interface methods
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
};

// Mock ISrsDvrSegmenter for testing SrsDvrPlan
class MockDvrSegmenter : public ISrsDvrSegmenter
{
public:
    bool write_metadata_called_;
    bool write_audio_called_;
    bool write_video_called_;
    SrsFragment *fragment_;

public:
    MockDvrSegmenter();
    virtual void assemble();
    virtual ~MockDvrSegmenter();

public:
    virtual srs_error_t initialize(ISrsDvrPlan *p, ISrsRequest *r);
    virtual SrsFragment *current();
    virtual srs_error_t open();
    virtual srs_error_t write_metadata(SrsMediaPacket *metadata);
    virtual srs_error_t write_audio(SrsMediaPacket *shared_audio, SrsFormat *format);
    virtual srs_error_t write_video(SrsMediaPacket *shared_video, SrsFormat *format);
    virtual srs_error_t close();
};

// Mock ISrsOriginHub for testing SrsDvrSegmentPlan
class MockOriginHubForDvrSegmentPlan : public ISrsOriginHub
{
public:
    int on_dvr_request_sh_count_;
    srs_error_t on_dvr_request_sh_error_;

public:
    MockOriginHubForDvrSegmentPlan();
    virtual ~MockOriginHubForDvrSegmentPlan();

public:
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
};

#endif
