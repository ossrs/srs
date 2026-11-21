//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_GB28181_HPP
#define SRS_APP_GB28181_HPP

#include <srs_core.hpp>

#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_conn.hpp>

#include <sstream>

class SrsConfDirective;
class SrsTcpListener;
class SrsResourceManager;
class SrsTcpConnection;
class ISrsCoroutine;
class SrsPackContext;
class SrsBuffer;
class SrsGbSession;
class SrsGbMediaTcpConn;
class SrsAlonePithyPrint;
class SrsGbMuxer;
class SrsSimpleRtmpClient;
struct SrsRawAacStreamCodec;
class SrsRawH264Stream;
class SrsRawHEVCStream;
class SrsMediaPacket;
class SrsPithyPrint;
class SrsRawAacStream;
class ISrsCommonHttpHandler;
class ISrsGbMuxer;
class ISrsGbSession;
class ISrsPackContext;
class ISrsMpegpsQueue;
class ISrsPsContext;
class ISrsListener;
class ISrsProtocolReadWriter;
class ISrsRawH264Stream;
class ISrsRawHEVCStream;
class ISrsRawAacStream;
class ISrsPithyPrint;
class ISrsBasicRtmpClient;
class SrsTsMessage;
class SrsPsPacket;
class ISrsGbSession;
class ISrsGbMediaTcpConn;
class ISrsAppConfig;
class ISrsApiServerOwner;
class ISrsResourceManager;
class ISrsAppFactory;
class ISrsIpListener;

enum SrsGbSessionState {
    SrsGbSessionStateInit = 0,
    SrsGbSessionStateConnecting,
    SrsGbSessionStateEstablished,
};
std::string srs_gb_session_state(SrsGbSessionState state);
std::string srs_gb_state(SrsGbSessionState ostate, SrsGbSessionState state);

// With an external SIP server, SRS is only the media relay:
//     1. The SIP server POSTs the stream ID and SSRC to the HTTP API.
//     2. SRS creates a session for the ID and SSRC, and returns the media port from the config.
//     3. The device connects to the port and sends the RTP stream with that SSRC.
//     4. SRS forwards the stream to an RTMP stream named after the ID.
//
// Request:
//      POST /gb/v1/publish/
//      {
//              "id": "...",
//              "ssrc": "..."
//      }
// Response:
//      {"code": 0, "port": 9000, "is_tcp": true}
class SrsGoApiGbPublish : public ISrsHttpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsResourceManager *gb_manager_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *conf_;

public:
    SrsGoApiGbPublish(SrsConfDirective *conf);
    virtual ~SrsGoApiGbPublish();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsJsonObject *res);
    srs_error_t bind_session(std::string stream, uint64_t ssrc);
};

class ISrsGbSession : public ISrsResource, public ISrsCoroutineHandler, public ISrsExecutorHandler
{
public:
    // Set by the external SIP server through the HTTP API; names the RTMP stream.
    std::string device_id_;

public:
    ISrsGbSession();
    virtual ~ISrsGbSession();

public:
    virtual void setup(SrsConfDirective *conf) = 0;
    virtual void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid) = 0;
    virtual void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media) = 0;
    virtual void on_media_disconnected(ISrsGbMediaTcpConn *media) = 0;
    virtual bool is_ended() = 0;

public:
    virtual void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs) = 0;
};

// Each session holds every media connection that bound it by shared ptr, and a media connection points
// back at the session by a raw pointer. The session cuts those pointers by interrupt() when its cycle
// ends and when it is freed, so a connection never points at a freed session.
class SrsGbSession : public ISrsGbSession
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The shared resource that owns this object; never free it.
    SrsSharedResource<ISrsGbSession> *wrapper_;
    ISrsInterruptable *owner_coroutine_;
    ISrsContextIdSetter *owner_cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsGbSessionState state_;

    SrsSharedResource<ISrsGbMediaTcpConn> media_;
    // The media connections that bound this session and still run, media_ among them; each points at
    // this session until its interrupt().
    std::vector<SrsSharedResource<ISrsGbMediaTcpConn> > medias_;
    ISrsGbMuxer *muxer_;
    // Whether the cycle ended, so no media connection binds the session or uses its muxer any more.
    bool cycle_done_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t connecting_starttime_;
    srs_utime_t media_connect_timeout_;
    srs_utime_t reinviting_starttime_;
    uint32_t nn_timeout_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsAlonePithyPrint *ppp_;
    srs_utime_t startime_;
    uint64_t total_packs_;
    uint64_t total_msgs_;
    uint64_t total_recovered_;
    uint64_t total_msgs_dropped_;
    uint64_t total_reserved_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint32_t media_id_;
    srs_utime_t media_starttime_;
    uint64_t media_msgs_;
    uint64_t media_packs_;
    uint64_t media_recovered_;
    uint64_t media_msgs_dropped_;
    uint64_t media_reserved_;

public:
    SrsGbSession();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsGbSession();

public:
    void setup(SrsConfDirective *conf);
    void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);

public:
    virtual void on_executor_done(ISrsInterruptable *executor);

public:
    void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);

    void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media);
    void on_media_disconnected(ISrsGbMediaTcpConn *media);
    bool is_ended();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    srs_error_t drive_state();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsGbSessionState set_state(SrsGbSessionState v);

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
};

class ISrsGbListener : public ISrsListener
{
public:
    ISrsGbListener();
    virtual ~ISrsGbListener();

public:
    virtual srs_error_t initialize(SrsConfDirective *conf) = 0;
    virtual void close() = 0;
};

class SrsGbListener : public ISrsGbListener, public ISrsTcpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsApiServerOwner *api_server_owner_;
    ISrsResourceManager *gb_manager_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsConfDirective *conf_;
    ISrsIpListener *media_listener_;

public:
    SrsGbListener(ISrsApiServerOwner *owner);
    virtual ~SrsGbListener();

public:
    virtual srs_error_t initialize(SrsConfDirective *conf);
    virtual srs_error_t listen();
    virtual void close();

public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t listen_api();
};

class ISrsPsPackHandler
{
public:
    ISrsPsPackHandler();
    virtual ~ISrsPsPackHandler();

public:
    // A pack holds one video frame with optional SPS and PPS, and generally more than one audio frame.
    virtual srs_error_t on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs) = 0;
};

class ISrsGbMediaTcpConn : public ISrsResource, public ISrsCoroutineHandler, public ISrsExecutorHandler
{
public:
    ISrsGbMediaTcpConn();
    virtual ~ISrsGbMediaTcpConn();

public:
    virtual void assemble() = 0;
    // Not in the constructor, to keep it empty.
    virtual void setup(srs_netfd_t stfd) = 0;
    virtual void setup_owner(SrsSharedResource<ISrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid) = 0;
    virtual bool is_connected() = 0;
    virtual void interrupt() = 0;
    virtual void set_cid(const SrsContextId &cid) = 0;
};

class SrsGbMediaTcpConn : public ISrsGbMediaTcpConn,
                          public ISrsPsPackHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *gb_manager_;
    ISrsContext *context_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool connected_;
    // The session this connection bound. The session sets it to NULL by interrupt() when its cycle ends
    // or it is freed, so it never points at a freed session.
    ISrsGbSession *session_;
    uint32_t nn_rtcp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The shared resource that owns this object; never free it.
    SrsSharedResource<ISrsGbMediaTcpConn> *wrapper_;
    ISrsInterruptable *owner_coroutine_;
    ISrsContextIdSetter *owner_cid_;
    SrsContextId cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsPackContext *pack_;
    ISrsProtocolReadWriter *conn_;
    uint8_t *buffer_;

public:
    SrsGbMediaTcpConn();
    virtual void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsGbMediaTcpConn();

public:
    // Not in the constructor, to keep it empty.
    void setup(srs_netfd_t stfd);
    void setup_owner(SrsSharedResource<ISrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);

public:
    virtual void on_executor_done(ISrsInterruptable *executor);

public:
    bool is_connected();
    void interrupt();
    virtual void set_cid(const SrsContextId &cid);

public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();

public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();

public:
    virtual srs_error_t on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t bind_session(uint32_t ssrc, ISrsGbSession **psession);
};

class ISrsMpegpsQueue
{
public:
    ISrsMpegpsQueue();
    virtual ~ISrsMpegpsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg) = 0;
    virtual SrsMediaPacket *dequeue() = 0;
};

// An AAC PES holds several frames that share its timestamp.
class SrsMpegpsQueue : public ISrsMpegpsQueue
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Keyed by the dts.
    std::map<int64_t, SrsMediaPacket *>
        msgs_;
    int nb_audios_;
    int nb_videos_;
    // A stream may have one track only, such as from a device with its audio off.
    bool has_audio_;
    bool has_video_;

public:
    SrsMpegpsQueue();
    virtual ~SrsMpegpsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg);
    virtual SrsMediaPacket *dequeue();
};

class ISrsGbMuxer
{
public:
    ISrsGbMuxer();
    virtual ~ISrsGbMuxer();

public:
    virtual void assemble() = 0; // Creates the members, to avoid calling functions in the constructor.
    virtual void setup(std::string output) = 0;
    virtual srs_error_t on_ts_message(SrsTsMessage *msg) = 0;
};

class SrsGbMuxer : public ISrsGbMuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The owner session; never free it.
    ISrsGbSession *session_;
    std::string output_;
    ISrsBasicRtmpClient *sdk_;
    bool connecting_;
    bool sending_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRawH264Stream *avc_;
    std::string h264_sps_;
    bool h264_sps_changed_;
    std::string h264_pps_;
    bool h264_pps_changed_;
    bool h264_sps_pps_sent_;

    ISrsRawHEVCStream *hevc_;
    bool vps_sps_pps_change_;
    std::string h265_vps_;
    std::string h265_sps_;
    std::string h265_pps_;
    bool vps_sps_pps_sent_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRawAacStream *aac_;
    std::string aac_specific_config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsMpegpsQueue *queue_;
    ISrsPithyPrint *pprint_;

public:
    SrsGbMuxer(ISrsGbSession *session);
    virtual ~SrsGbMuxer();

public:
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    void setup(std::string output);
    srs_error_t on_ts_message(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_ts_video(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t mux_h264(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(const std::string &ibps, bool keyframe, uint32_t dts, uint32_t pts);
    virtual srs_error_t mux_h265(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_h265_vps_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h265_ipb_frame(const std::string &ipbs, bool keyframe, uint32_t dts, uint32_t pts);
    virtual srs_error_t on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts);
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char *data, int size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t send_queued_messages();
    virtual srs_error_t connect();
    virtual void close();
};

class ISrsRecoverablePsContext
{
public:
    ISrsRecoverablePsContext();
    virtual ~ISrsRecoverablePsContext();

public:
};

class SrsRecoverablePsContext : public ISrsRecoverablePsContext
{
public:
    ISrsPsContext *ctx_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int recover_;

public:
    SrsRecoverablePsContext();
    virtual ~SrsRecoverablePsContext();

public:
    // The reserved bytes are left from the previous RTP packet, not parsed yet, and go before this payload.
    virtual srs_error_t decode_rtp(SrsBuffer *stream, int reserved, ISrsPsMessageHandler *handler);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t decode(SrsBuffer *stream, ISrsPsMessageHandler *handler);
    srs_error_t enter_recover_mode(SrsBuffer *stream, ISrsPsMessageHandler *handler, int pos, srs_error_t err);
    void quit_recover_mode(SrsBuffer *stream, ISrsPsMessageHandler *handler);
};

class ISrsPackContext : public ISrsPsMessageHandler
{
public:
    // Each media transport uses one context, so the context id is the media id.
    uint32_t media_id_;
    srs_utime_t media_startime_;
    uint64_t media_nn_recovered_;
    uint64_t media_nn_msgs_dropped_;
    uint64_t media_reserved_;

public:
    ISrsPackContext();
    virtual ~ISrsPackContext();
};

// A PS pack holds one video frame, which a large frame splits into several PES packets, and one or more audio
// frames. The 16-bit PES_packet_length limits a PES packet to 64 KB. The DTS and PTS of a video PES packet may be 0
// when the frame has more than one.
class SrsPackContext : public ISrsPackContext
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsPsPackHandler *handler_;
    SrsPsPacket *ps_;
    std::vector<SrsTsMessage *> msgs_;

public:
    SrsPackContext(ISrsPsPackHandler *handler);
    virtual ~SrsPackContext();

public:
    void clear();

public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);
    virtual void on_recover_mode(int nn_recover);
};

extern bool srs_skip_util_pack(SrsBuffer *stream);

extern SrsResourceManager *_srs_gb_manager;

#endif
