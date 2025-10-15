//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP16_HPP
#define SRS_UTEST_APP16_HPP

/*
#include <srs_utest_app16.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_ffmpeg.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_app10.hpp>
#include <srs_utest_app11.hpp>
#include <srs_utest_app6.hpp>

// Mock ISrsSrtSocket for testing SrsSrtConnection
class MockSrtSocket : public ISrsSrtSocket
{
public:
    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;
    int64_t recv_bytes_;
    int64_t send_bytes_;
    srs_error_t recvmsg_error_;
    srs_error_t sendmsg_error_;
    int recvmsg_called_count_;
    int sendmsg_called_count_;
    std::string last_recv_data_;
    std::string last_send_data_;

public:
    MockSrtSocket();
    virtual ~MockSrtSocket();

public:
    virtual srs_error_t recvmsg(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t sendmsg(void *buf, size_t size, ssize_t *nwrite);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
};

// Mock ISrsUdpHandler for testing SrsUdpListener
class MockUdpHandler : public ISrsUdpHandler
{
public:
    bool on_udp_packet_called_;
    int packet_count_;
    std::string last_packet_data_;
    int last_packet_size_;

public:
    MockUdpHandler();
    virtual ~MockUdpHandler();

public:
    virtual srs_error_t on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf);
};

// Mock ISrsUdpMuxHandler for testing SrsUdpMuxListener
class MockUdpMuxHandler : public ISrsUdpMuxHandler
{
public:
    bool on_udp_packet_called_;
    int packet_count_;
    std::string last_peer_ip_;
    int last_peer_port_;
    std::string last_packet_data_;
    int last_packet_size_;

public:
    MockUdpMuxHandler();
    virtual ~MockUdpMuxHandler();

public:
    virtual srs_error_t on_udp_packet(ISrsUdpMuxSocket *skt);
};

// Mock ISrsProtocolReadWriter for testing SrsSrtRecvThread
class MockSrtProtocolReadWriter : public ISrsProtocolReadWriter
{
public:
    srs_error_t read_error_;
    int read_count_;
    bool simulate_timeout_;
    std::string test_data_;
    srs_utime_t recv_timeout_;
    srs_utime_t send_timeout_;
    int64_t recv_bytes_;
    int64_t send_bytes_;

public:
    MockSrtProtocolReadWriter();
    virtual ~MockSrtProtocolReadWriter();

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
};

// Mock ISrsCoroutine for testing SrsSrtRecvThread
class MockSrtCoroutine : public ISrsCoroutine
{
public:
    srs_error_t pull_error_;
    int pull_count_;
    bool started_;
    SrsContextId cid_;

public:
    MockSrtCoroutine();
    virtual ~MockSrtCoroutine();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual void interrupt();
    virtual srs_error_t pull();
    virtual const SrsContextId &cid();
    virtual void set_cid(const SrsContextId &cid);
};

// Mock SrsSrtSource for testing SrsMpegtsSrtConn::on_srt_packet
class MockSrtSourceForPacket : public SrsSrtSource
{
public:
    int on_packet_called_count_;
    srs_error_t on_packet_error_;
    SrsSrtPacket *last_packet_;

public:
    MockSrtSourceForPacket();
    virtual ~MockSrtSourceForPacket();
    virtual srs_error_t on_packet(SrsSrtPacket *packet);
};

// Mock ISrsAppConfig for testing SrsMpegtsSrtConn HTTP hooks
class MockAppConfigForSrtHooks : public MockAppConfig
{
public:
    SrsConfDirective *on_connect_directive_;
    SrsConfDirective *on_close_directive_;
    SrsConfDirective *on_publish_directive_;
    SrsConfDirective *on_unpublish_directive_;
    SrsConfDirective *on_play_directive_;
    SrsConfDirective *on_stop_directive_;

public:
    MockAppConfigForSrtHooks();
    virtual ~MockAppConfigForSrtHooks();
    virtual SrsConfDirective *get_vhost_on_connect(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_close(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_publish(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_unpublish(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_play(std::string vhost);
    virtual SrsConfDirective *get_vhost_on_stop(std::string vhost);
    void set_on_connect_urls(const std::vector<std::string> &urls);
    void set_on_close_urls(const std::vector<std::string> &urls);
    void set_on_publish_urls(const std::vector<std::string> &urls);
    void set_on_unpublish_urls(const std::vector<std::string> &urls);
    void set_on_play_urls(const std::vector<std::string> &urls);
    void set_on_stop_urls(const std::vector<std::string> &urls);
    void clear_on_connect_directive();
    void clear_on_close_directive();
    void clear_on_publish_directive();
    void clear_on_unpublish_directive();
    void clear_on_play_directive();
    void clear_on_stop_directive();
};

// Mock ISrsHttpHooks for testing SrsMpegtsSrtConn HTTP hooks
class MockHttpHooksForSrt : public ISrsHttpHooks
{
public:
    std::vector<std::pair<std::string, ISrsRequest *> > on_connect_calls_;
    int on_connect_count_;
    srs_error_t on_connect_error_;
    std::vector<std::tuple<std::string, ISrsRequest *, int64_t, int64_t> > on_close_calls_;
    int on_close_count_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_publish_calls_;
    int on_publish_count_;
    srs_error_t on_publish_error_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_unpublish_calls_;
    int on_unpublish_count_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_play_calls_;
    int on_play_count_;
    srs_error_t on_play_error_;
    std::vector<std::pair<std::string, ISrsRequest *> > on_stop_calls_;
    int on_stop_count_;

public:
    MockHttpHooksForSrt();
    virtual ~MockHttpHooksForSrt();
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

// Mock SrsProtocolUtility for testing discover_candidates
class MockProtocolUtility : public SrsProtocolUtility
{
public:
    std::vector<SrsIPAddress *> mock_ips_;

public:
    MockProtocolUtility();
    virtual ~MockProtocolUtility();
    virtual std::vector<SrsIPAddress *> &local_ips();
    void add_ip(std::string ip, std::string ifname, bool is_ipv4, bool is_loopback, bool is_internet);
    void clear_ips();
};

// Mock ISrsAppConfig for testing discover_candidates
class MockAppConfigForDiscoverCandidates : public MockAppConfig
{
public:
    std::string rtc_server_candidates_;
    bool use_auto_detect_network_ip_;
    std::string rtc_server_ip_family_;

public:
    MockAppConfigForDiscoverCandidates();
    virtual ~MockAppConfigForDiscoverCandidates();
    virtual std::string get_rtc_server_candidates();
    virtual bool get_use_auto_detect_network_ip();
    virtual std::string get_rtc_server_ip_family();
};

// Mock ISrsStreamPublishTokenManager for testing SrsRtcSessionManager
class MockStreamPublishTokenManager : public ISrsStreamPublishTokenManager
{
public:
    srs_error_t acquire_token_error_;
    int acquire_token_count_;
    int release_token_count_;
    SrsStreamPublishToken *token_to_return_;

public:
    MockStreamPublishTokenManager();
    virtual ~MockStreamPublishTokenManager();

public:
    virtual srs_error_t acquire_token(ISrsRequest *req, SrsStreamPublishToken *&token);
    virtual void release_token(const std::string &stream_url);
    void set_acquire_token_error(srs_error_t err);
    void reset();
};

// Mock ISrsRtcConnection for testing SrsRtcSessionManager
// Note: This is a simplified mock that only implements the methods needed for testing
class MockRtcConnectionForSessionManager
{
public:
    bool add_publisher_called_;
    bool add_player_called_;
    bool set_all_tracks_status_called_;
    bool set_publish_token_called_;
    srs_error_t add_publisher_error_;
    srs_error_t add_player_error_;
    std::string username_;
    std::string token_;
    SrsSharedPtr<SrsStreamPublishToken> publish_token_;

public:
    MockRtcConnectionForSessionManager();
    virtual ~MockRtcConnectionForSessionManager();

public:
    srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status);
    void set_publish_token(SrsSharedPtr<SrsStreamPublishToken> publish_token);

    void reset();
};

// Mock ISrsAppFactory for testing SrsRtcSessionManager
class MockAppFactoryForSessionManager : public SrsAppFactory
{
public:
    MockRtcConnectionForSessionManager *mock_connection_;
    int create_rtc_connection_count_;

public:
    MockAppFactoryForSessionManager();
    virtual ~MockAppFactoryForSessionManager();

public:
    virtual ISrsRtcConnection *create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid);

    void reset();
};

// Mock ISrsRtcConnection for testing SrsRtcSessionManager::srs_update_rtc_sessions
class MockRtcConnectionForUpdateSessions : public ISrsRtcConnection
{
public:
    bool is_alive_;
    bool is_disposing_;
    std::string username_;
    bool switch_to_context_called_;
    bool alive_called_;
    SrsContextId cid_;
    ISrsRtcNetwork *udp_network_;

public:
    MockRtcConnectionForUpdateSessions();
    virtual ~MockRtcConnectionForUpdateSessions();

public:
    // ISrsResource interface
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual void on_disposing(ISrsResource *c);

public:
    // ISrsDisposingHandler interface
    virtual void on_before_dispose(ISrsResource *c);

public:
    // ISrsExpire interface
    virtual void expire();

public:
    // ISrsRtcPacketSender interface
    virtual srs_error_t send_rtcp(char *data, int nb_data);
    virtual srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    virtual srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    virtual void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    virtual srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);

public:
    // ISrsRtcPacketReceiver interface
    virtual srs_error_t do_check_send_nacks();

public:
    // ISrsRtcConnectionNackTimerHandler interface
    virtual void on_timer_nack();

public:
    // ISrsRtcConnection interface
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_rtp_cipher(char *data, int nb_data);
    virtual srs_error_t on_rtp_plaintext(char *data, int nb_data);
    virtual srs_error_t on_rtcp(char *data, int nb_data);
    virtual srs_error_t on_binding_request(SrsStunPacket *r, std::string &ice_pwd);
    virtual ISrsRtcNetwork *udp();
    virtual ISrsRtcNetwork *tcp();
    virtual void alive();
    virtual bool is_alive();
    virtual bool is_disposing();
    virtual void switch_to_context();
    virtual srs_error_t add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    virtual srs_error_t add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp);
    virtual void set_all_tracks_status(std::string stream_uri, bool is_publish, bool status);
    virtual void set_remote_sdp(const SrsSdp &sdp);
    virtual void set_local_sdp(const SrsSdp &sdp);
    virtual void set_state_as_waiting_stun();
    virtual srs_error_t initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username);
    virtual std::string username();
    virtual std::string token();
    virtual void set_publish_token(SrsSharedPtr<SrsStreamPublishToken> publish_token);
    virtual void simulate_drop_packet(bool v, int nn);
    virtual void simulate_nack_drop(int nn);
};

// Mock ISrsResourceManager for testing SrsRtcSessionManager::srs_update_rtc_sessions
class MockResourceManagerForUpdateSessions : public ISrsResourceManager
{
public:
    std::vector<ISrsResource *> resources_;
    std::vector<ISrsResource *> removed_resources_;
    std::map<std::string, ISrsResource *> id_map_;
    std::map<uint64_t, ISrsResource *> fast_id_map_;
    std::map<std::string, ISrsResource *> name_map_;

public:
    MockResourceManagerForUpdateSessions();
    virtual ~MockResourceManagerForUpdateSessions();

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

// Mock ISrsRtcNetwork for testing SrsRtcSessionManager::on_udp_packet
class MockRtcNetworkForUdpNetwork : public ISrsRtcNetwork
{
public:
    bool on_stun_called_;
    bool on_rtp_called_;
    bool on_rtcp_called_;
    bool on_dtls_called_;

public:
    MockRtcNetworkForUdpNetwork();
    virtual ~MockRtcNetworkForUdpNetwork();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg, bool dtls, bool srtp);
    virtual void set_state(SrsRtcNetworkState state);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t on_stun(SrsStunPacket *r, char *data, int nb_data);
    virtual srs_error_t on_rtp(char *data, int nb_data);
    virtual srs_error_t on_rtcp(char *data, int nb_data);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    virtual bool is_establelished();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

// Mock ISrsFFMPEG for testing SrsIngesterFFMPEG
class MockFFMPEG : public ISrsFFMPEG
{
public:
    bool start_called_;
    bool stop_called_;
    bool cycle_called_;
    bool fast_stop_called_;
    bool fast_kill_called_;
    srs_error_t start_error_;
    srs_error_t cycle_error_;

public:
    MockFFMPEG();
    virtual ~MockFFMPEG();

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
};

// Mock ISrsIngesterFFMPEG for testing SrsIngester
class MockIngesterFFMPEG : public ISrsIngesterFFMPEG
{
public:
    bool fast_stop_called_;
    bool fast_kill_called_;
    std::string vhost_;
    std::string id_;

public:
    MockIngesterFFMPEG();
    virtual ~MockIngesterFFMPEG();

public:
    virtual srs_error_t initialize(ISrsFFMPEG *ff, std::string v, std::string i);
    virtual std::string uri();
    virtual srs_utime_t alive();
    virtual bool equals(std::string v, std::string i);
    virtual bool equals(std::string v);
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t cycle();
    virtual void fast_stop();
    virtual void fast_kill();
};

// Mock ISrsAppFactory for testing SrsIngester
class MockAppFactoryForIngester : public ISrsAppFactory
{
public:
    MockSrtCoroutine *mock_coroutine_;
    ISrsTime *mock_time_;
    int create_coroutine_count_;
    int create_time_count_;

public:
    MockAppFactoryForIngester();
    virtual ~MockAppFactoryForIngester();

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
    virtual ISrsFileReader *create_http_file_reader(ISrsHttpResponseReader *r);
    virtual ISrsFlvDecoder *create_flv_decoder();
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
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
    void reset();
};

// Mock ISrsAppConfig for testing SrsIngester
class MockAppConfigForIngester : public MockAppConfig
{
public:
    std::vector<SrsConfDirective *> vhosts_;

public:
    MockAppConfigForIngester();
    virtual ~MockAppConfigForIngester();

public:
    virtual void get_vhosts(std::vector<SrsConfDirective *> &vhosts);
    virtual std::vector<std::string> get_listens();
    virtual std::vector<SrsConfDirective *> get_ingesters(std::string vhost);
    virtual bool get_ingest_enabled(SrsConfDirective *conf);
    virtual std::string get_ingest_ffmpeg(SrsConfDirective *conf);
    virtual std::string get_ingest_input_type(SrsConfDirective *conf);
    virtual std::string get_ingest_input_url(SrsConfDirective *conf);
    virtual std::vector<SrsConfDirective *> get_transcode_engines(SrsConfDirective *conf);
    virtual bool get_engine_enabled(SrsConfDirective *conf);
    virtual std::string get_engine_output(SrsConfDirective *conf);
    virtual std::string get_engine_vcodec(SrsConfDirective *conf);
    virtual std::string get_engine_acodec(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_perfile(SrsConfDirective *conf);
    virtual std::string get_engine_iformat(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_vfilter(SrsConfDirective *conf);
    virtual int get_engine_vbitrate(SrsConfDirective *conf);
    virtual double get_engine_vfps(SrsConfDirective *conf);
    virtual int get_engine_vwidth(SrsConfDirective *conf);
    virtual int get_engine_vheight(SrsConfDirective *conf);
    virtual int get_engine_vthreads(SrsConfDirective *conf);
    virtual std::string get_engine_vprofile(SrsConfDirective *conf);
    virtual std::string get_engine_vpreset(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_vparams(SrsConfDirective *conf);
    virtual int get_engine_abitrate(SrsConfDirective *conf);
    virtual int get_engine_asample_rate(SrsConfDirective *conf);
    virtual int get_engine_achannels(SrsConfDirective *conf);
    virtual std::vector<std::string> get_engine_aparams(SrsConfDirective *conf);
    virtual std::string get_engine_oformat(SrsConfDirective *conf);
    virtual bool get_vhost_enabled(SrsConfDirective *conf);
    void add_vhost(SrsConfDirective *vhost);
    void clear_vhosts();
};

// Mock ISrsTime for testing SrsIngester
class MockTimeForIngester : public ISrsTime
{
public:
    int usleep_count_;
    srs_utime_t last_usleep_duration_;

public:
    MockTimeForIngester();
    virtual ~MockTimeForIngester();

public:
    virtual void usleep(srs_utime_t duration);
    void reset();
};

#endif
