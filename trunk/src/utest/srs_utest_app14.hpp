//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_APP14_HPP
#define SRS_UTEST_APP14_HPP

/*
#include <srs_utest_app14.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_factory.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_server.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_utest_app11.hpp>
#include <srs_utest_app6.hpp>

#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#endif

// Mock ISrsGbMuxer for testing SrsGbSession
class MockGbMuxer : public ISrsGbMuxer
{
public:
    bool setup_called_;
    std::string setup_output_;
    bool on_ts_message_called_;
    srs_error_t on_ts_message_error_;

public:
    MockGbMuxer();
    virtual ~MockGbMuxer();

public:
    virtual void setup(std::string output);
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);
    void reset();
};

// Mock ISrsAppConfig for testing SrsGbSession
class MockAppConfigForGbSession : public MockAppConfig
{
public:
    std::string stream_caster_output_;

public:
    MockAppConfigForGbSession();
    virtual ~MockAppConfigForGbSession();

public:
    virtual std::string get_stream_caster_output(SrsConfDirective *conf);
    void set_stream_caster_output(const std::string &output);
};

// Mock ISrsPackContext for testing SrsGbSession::on_ps_pack
class MockPackContext : public ISrsPackContext
{
public:
    MockPackContext();
    virtual ~MockPackContext();

public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);
    virtual void on_recover_mode(int nn_recover);
};

// Mock ISrsGbMediaTcpConn for testing SrsGbSession::on_media_transport
class MockGbMediaTcpConn : public ISrsGbMediaTcpConn
{
public:
    bool set_cid_called_;
    SrsContextId received_cid_;
    bool is_connected_;

public:
    MockGbMediaTcpConn();
    virtual ~MockGbMediaTcpConn();

public:
    virtual void setup(srs_netfd_t stfd);
    virtual void setup_owner(SrsSharedResource<ISrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);
    virtual bool is_connected();
    virtual void interrupt();
    virtual void set_cid(const SrsContextId &cid);
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual srs_error_t cycle();
    virtual void on_executor_done(ISrsInterruptable *executor);
    void reset();
};

// Mock ISrsRtcConnection for testing SrsRtcUdpNetwork
class MockRtcConnectionForUdpNetwork : public ISrsRtcConnection
{
public:
    srs_error_t on_dtls_alert_error_;
    std::string last_alert_type_;
    std::string last_alert_desc_;
    bool on_rtp_cipher_called_;
    bool on_rtp_plaintext_called_;
    bool on_rtcp_called_;

public:
    MockRtcConnectionForUdpNetwork();
    virtual ~MockRtcConnectionForUdpNetwork();

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
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);
    virtual srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    virtual srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    virtual void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    virtual srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);

public:
    // ISrsRtcPacketReceiver interface
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
    virtual srs_error_t on_rtcp(SrsRtcpCommon *rtcp);

public:
    // ISrsRtcConnectionNackTimerHandler interface
    virtual srs_error_t do_check_send_nacks();

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
    virtual void simulate_nack_drop(int nn);

public:
    void set_on_dtls_alert_error(srs_error_t err);
    void reset();
};

// Mock ISrsIpListener for testing SrsGbListener::initialize
class MockIpListener : public ISrsIpListener
{
public:
    std::string endpoint_ip_;
    int endpoint_port_;
    std::string label_;
    bool set_endpoint_called_;
    bool set_label_called_;

public:
    MockIpListener();
    virtual ~MockIpListener();

public:
    virtual ISrsListener *set_endpoint(const std::string &i, int p);
    virtual ISrsListener *set_label(const std::string &label);
    virtual srs_error_t listen();
    virtual void close();
    void reset();
};

// Mock ISrsAppConfig for testing SrsGbListener::initialize
class MockAppConfigForGbListener : public MockAppConfig
{
public:
    int stream_caster_listen_port_;

public:
    MockAppConfigForGbListener();
    virtual ~MockAppConfigForGbListener();

public:
    virtual int get_stream_caster_listen(SrsConfDirective *conf);
};

// Mock ISrsCommonHttpHandler for testing SrsGbListener::listen_api
class MockHttpServeMuxForGbListener : public ISrsCommonHttpHandler
{
public:
    bool handle_called_;
    std::string handle_pattern_;
    ISrsHttpHandler *handle_handler_;

public:
    MockHttpServeMuxForGbListener();
    virtual ~MockHttpServeMuxForGbListener();

public:
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
    void reset();
};

// Mock ISrsApiServerOwner for testing SrsGbListener::listen_api
class MockApiServerOwnerForGbListener : public ISrsApiServerOwner
{
public:
    ISrsCommonHttpHandler *mux_;

public:
    MockApiServerOwnerForGbListener();
    virtual ~MockApiServerOwnerForGbListener();

public:
    virtual ISrsCommonHttpHandler *api_server();
};

// Mock ISrsIpListener for testing SrsGbListener::listen
class MockIpListenerForGbListen : public ISrsIpListener
{
public:
    bool listen_called_;

public:
    MockIpListenerForGbListen();
    virtual ~MockIpListenerForGbListen();

public:
    virtual ISrsListener *set_endpoint(const std::string &i, int p);
    virtual ISrsListener *set_label(const std::string &label);
    virtual srs_error_t listen();
    virtual void close();
    void reset();
};

// Mock ISrsGbSession for testing SrsGbMediaTcpConn::on_ps_pack
class MockGbSessionForMediaConn : public ISrsGbSession
{
public:
    bool on_ps_pack_called_;
    ISrsPackContext *received_pack_;
    SrsPsPacket *received_ps_;
    std::vector<SrsTsMessage *> received_msgs_;
    bool on_media_transport_called_;
    SrsSharedResource<ISrsGbMediaTcpConn> received_media_;

public:
    MockGbSessionForMediaConn();
    virtual ~MockGbSessionForMediaConn();

public:
    virtual void setup(SrsConfDirective *conf);
    virtual void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);
    virtual void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media);
    virtual void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual srs_error_t cycle();
    virtual void on_executor_done(ISrsInterruptable *executor);
    void reset();
};

// Mock ISrsBasicRtmpClient for testing SrsGbMuxer
class MockGbRtmpClient : public ISrsBasicRtmpClient
{
public:
    bool connect_called_;
    bool publish_called_;
    bool close_called_;
    srs_error_t connect_error_;
    srs_error_t publish_error_;
    int stream_id_;

public:
    MockGbRtmpClient();
    virtual ~MockGbRtmpClient();

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
    void reset();
};

// Mock ISrsRawAacStream for testing SrsGbMuxer
class MockGbRawAacStream : public ISrsRawAacStream
{
public:
    bool adts_demux_called_;
    bool mux_sequence_header_called_;
    bool mux_aac2flv_called_;
    srs_error_t adts_demux_error_;
    srs_error_t mux_sequence_header_error_;
    srs_error_t mux_aac2flv_error_;
    std::string sequence_header_output_;
    int demux_frame_size_;

public:
    MockGbRawAacStream();
    virtual ~MockGbRawAacStream();

public:
    virtual srs_error_t adts_demux(SrsBuffer *stream, char **pframe, int *pnb_frame, SrsRawAacStreamCodec &codec);
    virtual srs_error_t mux_sequence_header(SrsRawAacStreamCodec *codec, std::string &sh);
    virtual srs_error_t mux_aac2flv(char *frame, int nb_frame, SrsRawAacStreamCodec *codec, uint32_t dts, char **flv, int *nb_flv);
    void reset();
};

// Mock ISrsMpegpsQueue for testing SrsGbMuxer
class MockGbMpegpsQueue : public ISrsMpegpsQueue
{
public:
    bool push_called_;
    srs_error_t push_error_;
    int push_count_;

public:
    MockGbMpegpsQueue();
    virtual ~MockGbMpegpsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg);
    virtual SrsMediaPacket *dequeue();
    void reset();
};

// Mock ISrsGbSession for testing SrsGbMuxer
class MockGbSessionForMuxer : public ISrsGbSession
{
public:
    std::string device_id_;

public:
    MockGbSessionForMuxer();
    virtual ~MockGbSessionForMuxer();

public:
    virtual void setup(SrsConfDirective *conf);
    virtual void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);
    virtual void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media);
    virtual void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual srs_error_t cycle();
    virtual void on_executor_done(ISrsInterruptable *executor);
};

// Mock ISrsInterruptable for testing SrsRtcTcpConn
class MockInterruptableForRtcTcpConn : public ISrsInterruptable
{
public:
    bool interrupt_called_;
    srs_error_t pull_error_;

public:
    MockInterruptableForRtcTcpConn();
    virtual ~MockInterruptableForRtcTcpConn();

public:
    virtual void interrupt();
    virtual srs_error_t pull();
    void reset();
};

// Mock ISrsContextIdSetter for testing SrsRtcTcpConn
class MockContextIdSetterForRtcTcpConn : public ISrsContextIdSetter
{
public:
    bool set_cid_called_;
    SrsContextId received_cid_;

public:
    MockContextIdSetterForRtcTcpConn();
    virtual ~MockContextIdSetterForRtcTcpConn();

public:
    virtual void set_cid(const SrsContextId &cid);
    void reset();
};

// Mock ISrsRtcConnection for testing SrsRtcTcpConn
class MockRtcConnectionForTcpConn : public ISrsRtcConnection
{
public:
    MockRtcConnectionForTcpConn();
    virtual ~MockRtcConnectionForTcpConn();

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual void on_disposing(ISrsResource *c);
    virtual void on_before_dispose(ISrsResource *c);
    virtual void expire();
    virtual srs_error_t send_rtcp(char *data, int nb_data);
    virtual srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    virtual srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    virtual void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    virtual srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);
    virtual srs_error_t do_check_send_nacks();
    virtual void on_connection_established();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t on_rtp_cipher(char *data, int nb_data);
    virtual srs_error_t on_rtp_plaintext(char *buf, int nb_buf);
    virtual srs_error_t on_rtcp(char *buf, int nb_buf);
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
    virtual void simulate_nack_drop(int nn);
};

// Mock ISrsPsPackHandler for testing SrsPackContext
class MockPsPackHandler : public ISrsPsPackHandler
{
public:
    bool on_ps_pack_called_;
    int on_ps_pack_count_;
    uint32_t last_pack_id_;
    int last_msgs_count_;
    srs_error_t on_ps_pack_error_;

public:
    MockPsPackHandler();
    virtual ~MockPsPackHandler();

public:
    virtual srs_error_t on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);
    void reset();
};

// Mock ISrsHttpMessage for testing SrsGoApiGbPublish
class MockHttpMessageForGbPublish : public SrsHttpMessage
{
public:
    std::string body_content_;
    MockHttpConn *mock_conn_;

public:
    MockHttpMessageForGbPublish();
    virtual ~MockHttpMessageForGbPublish();

public:
    virtual srs_error_t body_read_all(std::string &body);
};

// Mock ISrsResourceManager for testing SrsGoApiGbPublish
class MockResourceManagerForGbPublish : public ISrsResourceManager
{
public:
    std::map<std::string, ISrsResource *> id_map_;
    std::map<uint64_t, ISrsResource *> fast_id_map_;

public:
    MockResourceManagerForGbPublish();
    virtual ~MockResourceManagerForGbPublish();

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

// Mock ISrsGbSession for testing SrsGoApiGbPublish
class MockGbSessionForApiPublish : public ISrsGbSession
{
public:
    bool setup_called_;
    bool setup_owner_called_;
    SrsConfDirective *setup_conf_;
    ISrsInterruptable *owner_coroutine_;

public:
    MockGbSessionForApiPublish();
    virtual ~MockGbSessionForApiPublish();

public:
    virtual void setup(SrsConfDirective *conf);
    virtual void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);
    virtual void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media);
    virtual void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual srs_error_t cycle();
    virtual void on_executor_done(ISrsInterruptable *executor);
    void reset();
};

// Mock ISrsAppFactory for testing SrsGoApiGbPublish
class MockAppFactoryForGbPublish : public ISrsAppFactory
{
public:
    MockGbSessionForApiPublish *mock_gb_session_;

public:
    MockAppFactoryForGbPublish();
    virtual ~MockAppFactoryForGbPublish();

public:
    virtual ISrsFileWriter *create_file_writer();
    virtual ISrsFileWriter *create_enc_file_writer();
    virtual ISrsFileReader *create_file_reader();
    virtual SrsPath *create_path();
    virtual SrsLiveSource *create_live_source();
    virtual ISrsOriginHub *create_origin_hub();
    virtual ISrsHourGlass *create_hourglass(const std::string &name, ISrsHourGlassHandler *handler, srs_utime_t interval);
    virtual ISrsBasicRtmpClient *create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto);
    virtual SrsHttpClient *create_http_client();
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
    virtual SrsDvrFlvSegmenter *create_dvr_flv_segmenter();
    virtual SrsDvrMp4Segmenter *create_dvr_mp4_segmenter();
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
    void reset();
};

// Mock ISrsRtcNetwork for testing SrsRtcNetworks
class MockRtcNetworkForNetworks : public ISrsRtcNetwork
{
public:
    srs_error_t initialize_error_;
    bool initialize_called_;
    SrsSessionConfig *last_cfg_;
    bool last_dtls_;
    bool last_srtp_;
    SrsRtcNetworkState state_;
    bool is_established_;

public:
    MockRtcNetworkForNetworks();
    virtual ~MockRtcNetworkForNetworks();

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
    void set_initialize_error(srs_error_t err);
};

// Mock ISrsRtcTransport for testing SrsRtcUdpNetwork RTP/RTCP handling
class MockRtcTransportForUdpNetwork : public ISrsRtcTransport
{
public:
    srs_error_t unprotect_rtp_error_;
    srs_error_t unprotect_rtcp_error_;
    bool unprotect_rtp_called_;
    bool unprotect_rtcp_called_;
    int unprotected_rtp_size_;
    int unprotected_rtcp_size_;

public:
    MockRtcTransportForUdpNetwork();
    virtual ~MockRtcTransportForUdpNetwork();

public:
    virtual srs_error_t initialize(SrsSessionConfig *cfg);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char *data, int nb_data);
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher);
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    virtual srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    virtual srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
    // ISrsDtlsCallback interface
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t write_dtls_data(void *data, int size);

public:
    void reset();
    void set_unprotect_rtp_error(srs_error_t err);
    void set_unprotect_rtcp_error(srs_error_t err);
};

// Mock ISrsResourceManager for testing SrsRtcUdpNetwork::update_sendonly_socket
class MockResourceManagerForUdpNetwork : public ISrsResourceManager
{
public:
    std::map<std::string, ISrsResource *> id_map_;
    std::map<uint64_t, ISrsResource *> fast_id_map_;

public:
    MockResourceManagerForUdpNetwork();
    virtual ~MockResourceManagerForUdpNetwork();

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

// Mock ISrsProtocolReadWriter for testing SrsRtcTcpNetwork write operations
class MockProtocolReadWriterForTcpNetwork : public ISrsProtocolReadWriter
{
public:
    std::vector<std::string> written_data_;
    srs_error_t write_error_;
    int64_t send_bytes_;
    int64_t recv_bytes_;
    srs_utime_t send_timeout_;
    srs_utime_t recv_timeout_;
    std::string read_data_;
    size_t read_pos_;

public:
    MockProtocolReadWriterForTcpNetwork();
    virtual ~MockProtocolReadWriterForTcpNetwork();

public:
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread);
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual int64_t get_recv_bytes();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_send_bytes();
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite);
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);

public:
    void reset();
    void set_write_error(srs_error_t err);
    void set_read_data(const std::string &data);
};

// Mock ISrsResourceManager for testing SrsRtcTcpConn::handshake
class MockResourceManagerForTcpConnHandshake : public ISrsResourceManager
{
public:
    ISrsResource *session_to_return_;

public:
    MockResourceManagerForTcpConnHandshake();
    virtual ~MockResourceManagerForTcpConnHandshake();

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

// Mock ISrsRtcConnection for testing SrsRtcTcpConn::handshake
class MockRtcConnectionForTcpConnHandshake : public ISrsRtcConnection
{
public:
    ISrsRtcNetwork *tcp_network_;
    std::string ice_pwd_;
    bool switch_to_context_called_;
    bool on_binding_request_called_;

public:
    MockRtcConnectionForTcpConnHandshake();
    virtual ~MockRtcConnectionForTcpConnHandshake();

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    virtual void on_disposing(ISrsResource *c);
    virtual void on_before_dispose(ISrsResource *c);
    virtual void expire();
    virtual srs_error_t send_rtcp(char *data, int nb_data);
    virtual srs_error_t send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp);
    virtual srs_error_t send_rtcp_xr_rrtr(uint32_t ssrc);
    virtual void check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks);
    virtual srs_error_t send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber);
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);
    virtual srs_error_t do_check_send_nacks();
    virtual void on_connection_established();
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc);
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char *data, const int len);
    virtual srs_error_t on_rtp_cipher(char *data, int nb_data);
    virtual srs_error_t on_rtp_plaintext(char *buf, int nb_buf);
    virtual srs_error_t on_rtcp(char *buf, int nb_buf);
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
    virtual void simulate_nack_drop(int nn);

public:
    void reset();
};

#endif
