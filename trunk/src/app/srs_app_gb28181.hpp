//
// Copyright (c) 2013-2025 The SRS Authors
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

// The state machine for GB session.
// init:
//      to connecting: sip is registered. Note that also send invite request if media not connected.
// connecting:
//      to established: sip is stable, media is connected.
// established:
//      init: media is not connected.
//      dispose session: sip is bye.
// Please see SrsGbSession::drive_state for detail.
enum SrsGbSessionState {
    SrsGbSessionStateInit = 0,
    SrsGbSessionStateConnecting,
    SrsGbSessionStateEstablished,
};
std::string srs_gb_session_state(SrsGbSessionState state);
std::string srs_gb_state(SrsGbSessionState ostate, SrsGbSessionState state);

// For external SIP server mode, where SRS acts only as a media relay server
//     1. SIP server POST request via HTTP API with stream ID and SSRC
//     2. SRS create session using ID and SSRC, return a port for receiving media streams (indicated in conf).
//     3. External streaming service connect to the port, and send RTP stream (with the above SSRC)
//     4. SRS forward the stream to RTMP stream, named after ID
//
// Request:
//      POST /gb/v1/publish/
//      {
//              "id": "...",
//              "ssrc": "..."
//      }
// Response:
//      {"port":9000, "is_tcp": true}
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

// The interface for GB session.
class ISrsGbSession : public ISrsResource, public ISrsCoroutineHandler, public ISrsExecutorHandler
{
public:
    std::string device_id_;

public:
    ISrsGbSession();
    virtual ~ISrsGbSession();

public:
    // Initialize the GB session.
    virtual void setup(SrsConfDirective *conf) = 0;
    // Setup the owner, the wrapper is the shared ptr, the interruptable object is the coroutine, and the cid is the context id.
    virtual void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid) = 0;
    // Notice session to use current media connection.
    virtual void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media) = 0;

public:
    virtual void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs) = 0;
};

// The main logic object for GB, the session.
// Each session contains a media object, that are managed by session. This means session always
// lives longer than media, and session will dispose media when session disposed. In another word,
// media objects use directly pointer to session, while session use shared ptr.
class SrsGbSession : public ISrsGbSession
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsContextId cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The shared resource which own this object, we should never free it because it's managed by shared ptr.
    SrsSharedResource<ISrsGbSession> *wrapper_;
    // The owner coroutine, allow user to interrupt the loop.
    ISrsInterruptable *owner_coroutine_;
    ISrsContextIdSetter *owner_cid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsGbSessionState state_;

    SrsSharedResource<ISrsGbMediaTcpConn> media_;
    ISrsGbMuxer *muxer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // When wait for media connecting, timeout if exceed.
    srs_utime_t connecting_starttime_;
    // The time we enter reinviting state.
    srs_utime_t reinviting_starttime_;
    // The number of timeout, dispose session if exceed.
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
    // The device ID generated and set by external SIP server, using SRS HTTP API. This
    // device ID is used to generate the RTMP stream name.
    std::string device_id_;

public:
    SrsGbSession();
    virtual ~SrsGbSession();

public:
    // Initialize the GB session.
    void setup(SrsConfDirective *conf);
    // Setup the owner, the wrapper is the shared ptr, the interruptable object is the coroutine, and the cid is the context id.
    void setup_owner(SrsSharedResource<ISrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);
    // Interface ISrsExecutorHandler
public:
    virtual void on_executor_done(ISrsInterruptable *executor);

public:
    // When got a pack of messages.
    void on_ps_pack(ISrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);

    // When got available media transport.
    void on_media_transport(SrsSharedResource<ISrsGbMediaTcpConn> media);

    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    srs_error_t drive_state();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsGbSessionState set_state(SrsGbSessionState v);
    // Interface ISrsResource
public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
};

// The listener for GB.
class ISrsGbListener : public ISrsListener
{
public:
    ISrsGbListener();
    virtual ~ISrsGbListener();

public:
};

// The Media listener for GB.
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
    SrsGbListener();
    virtual ~SrsGbListener();

public:
    srs_error_t initialize(SrsConfDirective *conf);
    srs_error_t listen();
    void close();
    // Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t listen_api();
};

// The handler for a pack of PS PES packets.
class ISrsPsPackHandler
{
public:
    ISrsPsPackHandler();
    virtual ~ISrsPsPackHandler();

public:
    // When got a pack of PS/TS messages, contains one video frame and optional SPS/PPS message, one or generally more
    // audio frames. Note that the ps contains the pack information.
    virtual srs_error_t on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs) = 0;
};

// The interface for GB media transport.
class ISrsGbMediaTcpConn : public ISrsResource, public ISrsCoroutineHandler, public ISrsExecutorHandler
{
public:
    ISrsGbMediaTcpConn();
    virtual ~ISrsGbMediaTcpConn();

public:
    // Setup object, to keep empty constructor.
    virtual void setup(srs_netfd_t stfd) = 0;
    // Setup the owner, the wrapper is the shared ptr, the interruptable object is the coroutine, and the cid is the context id.
    virtual void setup_owner(SrsSharedResource<ISrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid) = 0;
    // Whether media is connected.
    virtual bool is_connected() = 0;
    // Interrupt transport by session.
    virtual void interrupt() = 0;
    // Set the cid of all coroutines.
    virtual void set_cid(const SrsContextId &cid) = 0;
};

// A GB28181 TCP media connection, for PS stream.
class SrsGbMediaTcpConn : public ISrsGbMediaTcpConn, // It's a resource, coroutine handler, and executor handler.
                          public ISrsPsPackHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsResourceManager *gb_manager_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool connected_;
    // The owner session object, note that we use the raw pointer and should never free it.
    ISrsGbSession *session_;
    uint32_t nn_rtcp_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The shared resource which own this object, we should never free it because it's managed by shared ptr.
    SrsSharedResource<ISrsGbMediaTcpConn> *wrapper_;
    // The owner coroutine, allow user to interrupt the loop.
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
    virtual ~SrsGbMediaTcpConn();

public:
    // Setup object, to keep empty constructor.
    void setup(srs_netfd_t stfd);
    // Setup the owner, the wrapper is the shared ptr, the interruptable object is the coroutine, and the cid is the context id.
    void setup_owner(SrsSharedResource<ISrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid);
    // Interface ISrsExecutorHandler
public:
    virtual void on_executor_done(ISrsInterruptable *executor);

public:
    // Whether media is connected.
    bool is_connected();
    // Interrupt transport by session.
    void interrupt();
    // Set the cid of all coroutines.
    virtual void set_cid(const SrsContextId &cid);
    // Interface ISrsResource
public:
    virtual const SrsContextId &get_id();
    virtual std::string desc();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle();
    // Interface ISrsPsPackHandler
public:
    virtual srs_error_t on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Create session if no one, or bind to an existed session.
    srs_error_t
    bind_session(uint32_t ssrc, ISrsGbSession **psession);
};

// The interface for mpegps queue.
class ISrsMpegpsQueue
{
public:
    ISrsMpegpsQueue();
    virtual ~ISrsMpegpsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg) = 0;
    virtual SrsMediaPacket *dequeue() = 0;
};

// The queue for mpegts over udp to send packets.
// For the aac in mpegts contains many flv packets in a pes packet,
// we must recalc the timestamp.
class SrsMpegpsQueue : public ISrsMpegpsQueue
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The key: dts, value: msg.
    std::map<int64_t, SrsMediaPacket *>
        msgs_;
    int nb_audios_;
    int nb_videos_;

public:
    SrsMpegpsQueue();
    virtual ~SrsMpegpsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg);
    virtual SrsMediaPacket *dequeue();
};

// The interface for GB muxer.
class ISrsGbMuxer
{
public:
    ISrsGbMuxer();
    virtual ~ISrsGbMuxer();

public:
    virtual void setup(std::string output) = 0;
    virtual srs_error_t on_ts_message(SrsTsMessage *msg) = 0;
};

// Mux GB28181 to RTMP.
class SrsGbMuxer : public ISrsGbMuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The owner session object, note that we use the raw pointer and should never free it.
    ISrsGbSession *session_;
    std::string output_;
    ISrsBasicRtmpClient *sdk_;

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
    void setup(std::string output);
    srs_error_t on_ts_message(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_ts_video(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t mux_h264(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t mux_h265(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_h265_vps_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h265_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts);
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char *data, int size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Connect to RTMP server.
    virtual srs_error_t
    connect();
    // Close the connection to RTMP server.
    virtual void close();
};

// The interface for recoverable PS context.
class ISrsRecoverablePsContext
{
public:
    ISrsRecoverablePsContext();
    virtual ~ISrsRecoverablePsContext();

public:
};

// Recoverable PS context for GB28181.
class SrsRecoverablePsContext : public ISrsRecoverablePsContext
{
public:
    ISrsPsContext *ctx_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // If decoding error, enter the recover mode. Drop all left bytes util next pack header.
    int recover_;

public:
    SrsRecoverablePsContext();
    virtual ~SrsRecoverablePsContext();

public:
    // Decode the PS stream in RTP payload. Note that there might be first reserved bytes of RAW data, which is not
    // parsed by previous decoding, we should move to the start of payload bytes.
    virtual srs_error_t decode_rtp(SrsBuffer *stream, int reserved, ISrsPsMessageHandler *handler);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Decode the RTP payload as PS pack stream.
    virtual srs_error_t
    decode(SrsBuffer *stream, ISrsPsMessageHandler *handler);
    // When got error, drop data and enter recover mode.
    srs_error_t enter_recover_mode(SrsBuffer *stream, ISrsPsMessageHandler *handler, int pos, srs_error_t err);
    // Quit Recover mode when got pack header.
    void quit_recover_mode(SrsBuffer *stream, ISrsPsMessageHandler *handler);
};

// The interface for PS pack context.
class ISrsPackContext : public ISrsPsMessageHandler
{
public:
    // Each media transport only use one context, so the context id is the media id.
    uint32_t media_id_;
    srs_utime_t media_startime_;
    uint64_t media_nn_recovered_;
    uint64_t media_nn_msgs_dropped_;
    uint64_t media_reserved_;

public:
    ISrsPackContext();
    virtual ~ISrsPackContext();
};

// The PS pack context, for GB28181 to process based on PS pack, which contains a video and audios messages. For large
// video frame, it might be split to multiple PES packets, which must be group to one video frame.
// Please note that a pack might contain multiple audio frames, so size of audio PES packet should not exceed 64KB,
// which is limited by the 16 bits PES_packet_length.
// We also correct the timestamp, or DTS/PTS of video frames, which might be 0 if more than one video PES packets in a
// PS pack stream.
class SrsPackContext : public ISrsPackContext
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // To process a pack of TS/PS messages.
    ISrsPsPackHandler *handler_;
    // Note that it might be freed, so never use its fields.
    SrsPsPacket *ps_;
    // The messages in current PS pack.
    std::vector<SrsTsMessage *> msgs_;

public:
    SrsPackContext(ISrsPsPackHandler *handler);
    virtual ~SrsPackContext();

public:
    void clear();

    // Interface ISrsPsMessageHandler
public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);
    virtual void on_recover_mode(int nn_recover);
};

// Find the pack header which starts with bytes (00 00 01 ba).
extern bool srs_skip_util_pack(SrsBuffer *stream);

// Manager for GB connections.
extern SrsResourceManager *_srs_gb_manager;

#endif
