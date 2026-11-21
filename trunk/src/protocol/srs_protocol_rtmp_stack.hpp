//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_RTMP_HPP
#define SRS_PROTOCOL_RTMP_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#ifndef _WIN32
#endif

#include <srs_core_performance.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

class SrsFastStream;
class SrsBuffer;
class SrsAmf0Any;
class SrsMessageHeader;
class SrsChunkStream;
class SrsMediaPacket;

class SrsProtocol;
class ISrsProtocolReader;
class ISrsProtocolReadWriter;
class SrsProtocolUtility;
class SrsCreateStreamPacket;
class SrsFMLEStartPacket;
class SrsPublishPacket;
class SrsOnMetaDataPacket;
class SrsPlayPacket;
class SrsRtmpCommonMessage;
class SrsRtmpCommand;
class SrsAmf0Object;
class IMergeReadHandler;
class SrsCallPacket;

#define RTMP_AMF0_COMMAND_CONNECT "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM "closeStream"
#define RTMP_AMF0_COMMAND_PLAY "play"
#define RTMP_AMF0_COMMAND_PAUSE "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS "onStatus"
#define RTMP_AMF0_COMMAND_RESULT "_result"
#define RTMP_AMF0_COMMAND_ERROR "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH "FCPublish"
#define RTMP_AMF0_COMMAND_UNPUBLISH "FCUnpublish"
#define RTMP_AMF0_COMMAND_PUBLISH "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS "|RtmpSampleAccess"

#define RTMP_SIG_FMS_VER "3,5,3,888"
#define RTMP_SIG_AMF0_VER 0
#define RTMP_SIG_CLIENT_ID "ASAICiss"

#define StatusLevel "level"
#define StatusCode "code"
#define StatusDescription "description"
#define StatusDetails "details"
#define StatusClientId "clientid"
#define StatusLevelStatus "status"
#define StatusLevelError "error"
#define StatusCodeConnectSuccess "NetConnection.Connect.Success"
#define StatusCodeConnectRejected "NetConnection.Connect.Rejected"
#define StatusCodeStreamReset "NetStream.Play.Reset"
#define StatusCodeStreamStart "NetStream.Play.Start"
#define StatusCodeStreamPause "NetStream.Pause.Notify"
#define StatusCodeStreamUnpause "NetStream.Unpause.Notify"
#define StatusCodePublishStart "NetStream.Publish.Start"
#define StatusCodeDataStart "NetStream.Data.Start"
#define StatusCodeUnpublishSuccess "NetStream.Unpublish.Success"

// The packet is the payload of the message, not a subclass of it: the packet holds the
// domain data, and the message holds the protocol data, such as the header.
class SrsRtmpCommand
{
public:
    SrsRtmpCommand();
    virtual ~SrsRtmpCommand();

public:
    virtual srs_error_t to_msg(SrsRtmpCommonMessage *msg, int stream_id);

public:
    virtual srs_error_t encode(int &size, char *&payload);

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int
    get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

class SrsProtocol
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    class AckWindowSize
    {
    public:
        uint32_t window_;
        int64_t nb_recv_bytes_;
        // The sequence number of the last acknowledgement sent.
        uint32_t sequence_number_;

        AckWindowSize();
    };

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsProtocolReadWriter *skt_;
    // The requests sent out, to decode their responses. Key is the transaction ID, value is the
    // request command name.
    std::map<double, std::string> requests_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::map<int, SrsChunkStream *>
        chunk_streams_;
    SrsChunkStream **cs_cache_;
    SrsFastStream *in_buffer_;
    int32_t in_chunk_size_;
    // The window to acknowledge the bytes received from the peer, such as an encoder.
    AckWindowSize in_ack_size_;
    // The window the peer is asked to acknowledge.
    AckWindowSize out_ack_size_;
    int32_t in_buffer_length_;
    bool show_debug_info_;
    bool auto_response_when_recv_;
    std::vector<SrsRtmpCommand *> manual_response_queue_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // It is safe to realloc, unlike the c0c3 cache.
    iovec *out_iovs_;
    int nb_out_iovs_;
    // Each chunk header uses up to SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE bytes: 11 bytes for
    // type0 (15 with extended timestamp), or 1 byte for type3 (5 with extended timestamp).
    // It cannot be realloced, since the iovs point into it. It is in the heap to make VS2015 happy.
    char *out_c0c3_caches_;
    bool warned_c0c3_cache_dry_;
    int32_t out_chunk_size_;
    SrsProtocolUtility *protocol_utility_;

public:
    SrsProtocol(ISrsProtocolReadWriter *io);
    virtual ~SrsProtocol();

public:
    virtual void set_auto_response(bool v);
    virtual srs_error_t manual_response_flush();

public:
#ifdef SRS_PERF_MERGED_READ
    // When merged read is on and a read gets few bytes, it sleeps to wait for more data, to
    // read more bytes at a time.
    virtual void set_merge_read(bool v, IMergeReadHandler *handler);
    // When the buffer grows, the previous pointers into it may be invalid.
    virtual void set_recv_buffer(int buffer_size);
#endif
public:
    // On timeout, recv and send return ERROR_SOCKET_TIMEOUT.
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();

public:
    // The peer usually sets the window by a message, but some encoders never send it and still
    // wait for acknowledgements, so they block after publishing for a while, when the server
    // keeps the default 0, which means no acknowledgement.
    virtual srs_error_t set_in_window_ack_size(int ack_size);

public:
    // Drops a message whose payload is empty.
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    // The ppacket may be NULL on success, such as for an unknown message type.
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    // The send functions always free the msg or packet, even on error. The stream_id is 0 for
    // control messages.
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg, int stream_id);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id);
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id);

public:
    // Drops other messages until one decodes as T. On success, the caller frees pmsg and ppacket.
    template <class T>
    srs_error_t expect_message(SrsRtmpCommonMessage **pmsg, T **ppacket)
    {
        *pmsg = NULL;
        *ppacket = NULL;

        srs_error_t err = srs_success;

        while (true) {
            SrsRtmpCommonMessage *msg = NULL;
            if ((err = recv_message(&msg)) != srs_success) {
                return srs_error_wrap(err, "recv message");
            }

            SrsRtmpCommand *packet = NULL;
            if ((err = decode_message(msg, &packet)) != srs_success) {
                srs_freep(msg);
                srs_freep(packet);
                return srs_error_wrap(err, "decode message");
            }

            T *pkt = dynamic_cast<T *>(packet);
            if (!pkt) {
                srs_freep(msg);
                srs_freep(packet);
                continue;
            }

            *pmsg = msg;
            *ppacket = pkt;
            break;
        }

        return err;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The caller frees the msgs.
    virtual srs_error_t do_send_messages(SrsMediaPacket **msgs, int nb_msgs);
    virtual srs_error_t do_iovs_send(iovec *iovs, int size);
    virtual srs_error_t do_send_and_free_packet(SrsRtmpCommand *packet, int stream_id);
    virtual srs_error_t do_decode_message(SrsMessageHeader &header, SrsBuffer *stream, SrsRtmpCommand **ppacket);
    // The pmsg is NULL on success when the chunk does not complete a message.
    virtual srs_error_t recv_interlaced_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t read_basic_header(char &fmt, int &cid);
    virtual srs_error_t read_message_header(SrsChunkStream *chunk, char fmt);
    virtual srs_error_t read_message_payload(SrsChunkStream *chunk, SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t on_recv_message(SrsRtmpCommonMessage *msg);
    virtual srs_error_t on_send_packet(SrsMessageHeader *mh, SrsRtmpCommand *packet);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t response_acknowledgement_message();
    virtual srs_error_t response_ping_message(int32_t timestamp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void print_debug_info();
};

// The chunks of the input chunk streams may be interlaced, so each chunk stream keeps its
// partly read message.
class SrsChunkStream
{
public:
    char fmt_;
    int cid_;
    SrsMessageHeader header_;
    bool has_extended_timestamp_;
    SrsRtmpCommonMessage *msg_;
    char *writing_pos_;
    // Zero for a fresh chunk stream.
    int64_t msg_count_;
    // Because the extended timestamp may be a delta timestamp, it can differ
    // from the timestamp in the header, so it should be stored as a distinct field
    // for comparison with the extended timestamp of subsequent chunks.
    // See https://github.com/ossrs/srs/pull/4356 for details.
    int32_t extended_timestamp_;

public:
    SrsChunkStream(int cid);
    virtual ~SrsChunkStream();
};

class ISrsRequest
{
public:
    std::string ip_;

public:
    // The vhost may be in the RTMP URL, such as:
    //    rtmp://VHOST:port/app/stream
    //    rtmp://ip:port/app/stream?vhost=VHOST
    //    rtmp://ip:port/app?vhost=VHOST/stream
    //    rtmp://ip:port/app...vhost...VHOST/stream
    // The tcUrl is the URL without the stream.
    std::string tcUrl_;

public:
    std::string pageUrl_;
    std::string swfUrl_;
    double objectEncoding_;

public:
    // Parsed from the tcUrl, and the stream from play or publish.
    std::string schema_;
    std::string vhost_;
    std::string host_;
    int port_;
    // Without the param.
    std::string app_;
    std::string param_;
    std::string stream_;
    // Set by the user, for test only.
    std::string ice_ufrag_;
    // Set by the user, for test only.
    std::string ice_pwd_;
    // A live stream player stops after this duration.
    srs_utime_t duration_;
    // The token in the connect request, for edge to origin authentication.
    // @see https://github.com/ossrs/srs/issues/104
    SrsAmf0Object *args_;

public:
    // Such as rtmp, flv (HTTP-FLV) or flvs (HTTPS-FLV).
    std::string protocol_;

public:
    ISrsRequest();
    virtual ~ISrsRequest();

public:
    // The source keeps a copy, since the request it was created with may be invalid when the
    // config reloads.
    virtual ISrsRequest *copy() = 0;
    // Updates the auth info in place, since many components keep a pointer to the request.
    virtual void update_auth(ISrsRequest *req) = 0;
    virtual std::string get_stream_url() = 0;
    // Call it after the URL is updated.
    virtual void strip() = 0;

public:
    virtual ISrsRequest *as_http() = 0;

public:
    virtual void on_source_created();
};

class SrsRequest : public ISrsRequest
{
public:
    SrsRequest();
    virtual ~SrsRequest();

public:
    virtual ISrsRequest *copy();
    virtual void update_auth(ISrsRequest *req);
    virtual std::string get_stream_url();
    virtual void strip();

public:
    virtual ISrsRequest *as_http();
};

class SrsResponse
{
public:
    // The stream ID in the createStream response.
    int stream_id_;

public:
    SrsResponse();
    virtual ~SrsResponse();
};

enum SrsRtmpConnType {
    SrsRtmpConnUnknown = 0x0000,
    SrsRtmpConnPlay = 0x0100,
    SrsHlsPlay = 0x0101,
    SrsFlvPlay = 0x0102,
    SrsTsPlay = 0x0103,
    SrsMp3Play = 0x0104,
    SrsAacPlay = 0x0105,
    SrsRtcConnPlay = 0x0110,
    SrsSrtConnPlay = 0x0120,
    SrsRtspConnPlay = 0x0130,
    SrsRtmpConnFMLEPublish = 0x0200,
    SrsRtmpConnFlashPublish = 0x0201,
    SrsRtmpConnHaivisionPublish = 0x0202,
    SrsRtcConnPublish = 0x0210,
    SrsSrtConnPublish = 0x0220,
};
std::string srs_client_type_string(SrsRtmpConnType type);
bool srs_client_type_is_publish(SrsRtmpConnType type);

// Keeps the handshake bytes, to switch between the complex and simple handshake.
class SrsHandshakeBytes
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRand *rand_;

public:
    // For RTMP proxy, the real IP.
    uint32_t proxy_real_ip_;
    // [1+1536]
    char *c0c1_;
    // [1+1536+1536]
    char *s0s1s2_;
    // [1536]
    char *c2_;

public:
    SrsHandshakeBytes();
    virtual ~SrsHandshakeBytes();

public:
    virtual void dispose();

public:
    virtual srs_error_t read_c0c1(ISrsProtocolReader *io);
    virtual srs_error_t read_s0s1s2(ISrsProtocolReader *io);
    virtual srs_error_t read_c2(ISrsProtocolReader *io);
    virtual srs_error_t create_c0c1();
    virtual srs_error_t create_s0s1s2(const char *c1 = NULL);
    virtual srs_error_t create_c2();
};

struct SrsServerInfo {
    std::string ip_;
    std::string sig_;
    int pid_;
    int cid_;
    int major_;
    int minor_;
    int revision_;
    int build_;

    SrsServerInfo();
};

class SrsRtmpClient
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHandshakeBytes *hs_bytes_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsProtocol *protocol_;
    ISrsProtocolReadWriter *io_;

public:
    SrsRtmpClient(ISrsProtocolReadWriter *skt);
    virtual ~SrsRtmpClient();

public:
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void set_send_timeout(srs_utime_t tm);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg, int stream_id);
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id);
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id);

public:
    virtual srs_error_t handshake();
    virtual srs_error_t simple_handshake();
    virtual srs_error_t complex_handshake();
    // The r and si may be NULL. The r gives the swfUrl, the pageUrl, and the tcUrl when set. With
    // dsu, the args of r are sent to the upnode.
    virtual srs_error_t connect_app(std::string app, std::string tcUrl, ISrsRequest *r, bool dsu, SrsServerInfo *si);
    virtual srs_error_t create_stream(int &stream_id);
    virtual srs_error_t play(std::string stream, int stream_id, int chunk_size);
    // The flash publish workflow: connect-app, create-stream, then publish.
    virtual srs_error_t publish(std::string stream, int stream_id, int chunk_size);
    // The FMLE publish workflow: connect-app, then FMLE publish.
    virtual srs_error_t fmle_publish(std::string stream, int &stream_id);

public:
    // Drops other messages until one decodes as T. On success, the caller frees pmsg and ppacket.
    template <class T>
    srs_error_t expect_message(SrsRtmpCommonMessage **pmsg, T **ppacket)
    {
        return protocol_->expect_message<T>(pmsg, ppacket);
    }
};

class ISrsRtmpServer
{
public:
    ISrsRtmpServer();
    virtual ~ISrsRtmpServer();

public:
    virtual void set_recv_timeout(srs_utime_t tm) = 0;
    virtual void set_send_timeout(srs_utime_t tm) = 0;
    virtual srs_error_t handshake() = 0;
    virtual srs_error_t connect_app(ISrsRequest *req) = 0;
    virtual uint32_t proxy_real_ip() = 0;
    virtual srs_error_t set_window_ack_size(int ack_size) = 0;
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type) = 0;
    virtual srs_error_t set_chunk_size(int chunk_size) = 0;
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip = NULL) = 0;
    virtual srs_error_t on_bw_done() = 0;
    virtual srs_error_t identify_client(int stream_id, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration) = 0;
    virtual srs_error_t start_play(int stream_id) = 0;
    virtual srs_error_t start_fmle_publish(int stream_id) = 0;
    virtual srs_error_t start_haivision_publish(int stream_id) = 0;
    virtual srs_error_t fmle_unpublish(int stream_id, double unpublish_tid) = 0;
    virtual srs_error_t start_flash_publish(int stream_id) = 0;
    virtual srs_error_t start_publishing(int stream_id) = 0;
    virtual srs_error_t redirect(ISrsRequest *r, std::string url, bool &accepted) = 0;
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id) = 0;
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket) = 0;
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id) = 0;
    virtual srs_error_t on_play_client_pause(int stream_id, bool is_pause) = 0;
    virtual srs_error_t set_in_window_ack_size(int ack_size) = 0;
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg) = 0;
    virtual void set_auto_response(bool v) = 0;
    virtual void set_merge_read(bool v, IMergeReadHandler *handler) = 0;
    virtual void set_recv_buffer(int buffer_size) = 0;
};

class SrsRtmpServer : public ISrsRtmpServer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHandshakeBytes *hs_bytes_;
    SrsProtocol *protocol_;
    ISrsProtocolReadWriter *io_;

public:
    SrsRtmpServer(ISrsProtocolReadWriter *skt);
    virtual ~SrsRtmpServer();

public:
    // For RTMP proxy, the real IP. 0 if no proxy.
    // @doc https://github.com/ossrs/go-oryx/wiki/RtmpProxy
    virtual uint32_t proxy_real_ip();

public:
    virtual void set_auto_response(bool v);
#ifdef SRS_PERF_MERGED_READ
    virtual void set_merge_read(bool v, IMergeReadHandler *handler);
    virtual void set_recv_buffer(int buffer_size);
#endif
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg, int stream_id);
    // Sends many messages at a time for performance, to support 6k+ clients at 250kbps.
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id);
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id);

public:
    virtual srs_error_t handshake();
    virtual srs_error_t connect_app(ISrsRequest *req);
    // The client sends an acknowledgement for each window.
    virtual srs_error_t set_window_ack_size(int ack_size);
    virtual srs_error_t set_in_window_ack_size(int ack_size);
    // The type is the Limit type: hard (0), soft (1), or dynamic (2).
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type);
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip = NULL);
    // The accepted is whether the client accepts the redirect.
    virtual srs_error_t redirect(ISrsRequest *r, std::string url, bool &accepted);
    virtual void response_connect_reject(ISrsRequest *req, const char *desc);
    virtual srs_error_t on_bw_done();
    // The stream_id is for the response to the createStream of a flash client.
    virtual srs_error_t identify_client(int stream_id, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration);
    virtual srs_error_t set_chunk_size(int chunk_size);
    virtual srs_error_t start_play(int stream_id);
    virtual srs_error_t on_play_client_pause(int stream_id, bool is_pause);
    virtual srs_error_t start_fmle_publish(int stream_id);
    // For the Haivision encoder.
    // @see https://github.com/ossrs/srs/issues/844
    virtual srs_error_t start_haivision_publish(int stream_id);
    virtual srs_error_t fmle_unpublish(int stream_id, double unpublish_tid);
    virtual srs_error_t start_flash_publish(int stream_id);
    // Call it after the hooks verify the publish. For OBS to stop reconnecting when the hooks
    // fail the publish, never send onStatus(NetStream.Publish.Start) before that failure.
    // See https://github.com/ossrs/srs/issues/4037
    virtual srs_error_t start_publishing(int stream_id);

public:
    // Drops other messages until one decodes as T. On success, the caller frees pmsg and ppacket.
    template <class T>
    srs_error_t expect_message(SrsRtmpCommonMessage **pmsg, T **ppacket)
    {
        return protocol_->expect_message<T>(pmsg, ppacket);
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t identify_create_stream_client(SrsCreateStreamPacket *req, int stream_id, int depth, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration);
    virtual srs_error_t identify_fmle_publish_client(SrsFMLEStartPacket *req, SrsRtmpConnType &type, std::string &stream_name);
    virtual srs_error_t identify_haivision_publish_client(SrsFMLEStartPacket *req, SrsRtmpConnType &type, std::string &stream_name);
    virtual srs_error_t identify_flash_publish_client(SrsPublishPacket *req, SrsRtmpConnType &type, std::string &stream_name);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t identify_play_client(SrsPlayPacket *req, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration);
};

// 4.1.1. connect
class SrsConnectAppPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    // Created by the constructor; never set a new one, which leaks the old one.
    SrsAmf0Object *command_object_;
    // May be NULL.
    SrsAmf0Object *args_;

public:
    SrsConnectAppPacket();
    virtual ~SrsConnectAppPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};
class SrsConnectAppResPacket : public SrsRtmpCommand
{
public:
    // The _result or _error.
    std::string command_name_;
    double transaction_id_;
    // The connection properties, such as fmsver.
    SrsAmf0Object *props_;
    // The response, such as its code, level and description.
    SrsAmf0Object *info_;

public:
    SrsConnectAppResPacket();
    virtual ~SrsConnectAppResPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.1.2. Call
class SrsCallPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    // 0 when no response is expected.
    double transaction_id_;
    // May be NULL.
    SrsAmf0Any *command_object_;
    // May be NULL.
    SrsAmf0Any *arguments_;

public:
    SrsCallPacket();
    virtual ~SrsCallPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};
class SrsCallResPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    // May be NULL.
    SrsAmf0Any *command_object_;
    // May be NULL.
    SrsAmf0Any *response_;

public:
    SrsCallResPacket(double transaction_id);
    virtual ~SrsCallResPacket();

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.1.3. createStream
class SrsCreateStreamPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
public:
    SrsCreateStreamPacket();
    virtual ~SrsCreateStreamPacket();

public:
    void set_command_object(SrsAmf0Any *v);

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};
class SrsCreateStreamResPacket : public SrsRtmpCommand
{
public:
    // The _result or _error.
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    double stream_id_;

public:
    SrsCreateStreamResPacket(double transaction_id, double stream_id);
    virtual ~SrsCreateStreamResPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

class SrsCloseStreamPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
public:
    SrsCloseStreamPacket();
    virtual ~SrsCloseStreamPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);
};

// The FMLE releaseStream, FCPublish or FCUnpublish.
class SrsFMLEStartPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    std::string stream_name_;

public:
    SrsFMLEStartPacket();
    virtual ~SrsFMLEStartPacket();

public:
    void set_command_object(SrsAmf0Any *v);

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);

public:
    static SrsFMLEStartPacket *create_release_stream(std::string stream);
    static SrsFMLEStartPacket *create_FC_publish(std::string stream);
};
class SrsFMLEStartResPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    SrsAmf0Any *args_;
public:
    SrsFMLEStartResPacket(double transaction_id);
    virtual ~SrsFMLEStartResPacket();

public:
    void set_args(SrsAmf0Any *v);
    void set_command_object(SrsAmf0Any *v);

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.2.6. Publish, for FMLE and flash.
class SrsPublishPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    std::string stream_name_;
    // The "live", "record" or "append". SRS supports only live.
    std::string type_;

public:
    SrsPublishPacket();
    virtual ~SrsPublishPacket();

public:
    void set_command_object(SrsAmf0Any *v);

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.2.8. pause
class SrsPausePacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    // False to resume playing.
    bool is_pause_;
    // The client's stream time at the pause or resume.
    double time_ms_;

public:
    SrsPausePacket();
    virtual ~SrsPausePacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);
};

// 4.2.1. play
class SrsPlayPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    // A recorded file has a prefix, such as "mp3:sample" or "mp4:sample.m4v".
    std::string stream_name_;
    // The start time in seconds: -2 plays the live stream, or the recorded stream when no live
    // stream has the name; -1 plays only the live stream; 0 or more plays the recorded stream
    // from that time.
    double start_;
    // The spec gives the duration in seconds: -1 (or any other negative) plays until the stream
    // ends, 0 plays a single frame of a recorded stream, and a positive number plays for that
    // long.
    double duration_;
    // Whether to flush any previous playlist.
    bool reset_;

public:
    SrsPlayPacket();
    virtual ~SrsPlayPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

class SrsPlayResPacket : public SrsRtmpCommand
{
public:
    // The onStatus when the play succeeds.
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *command_object_;
    // Such as NetStream.Play.Start, or NetStream.Play.StreamNotFound when no stream has the name.
    SrsAmf0Object *desc_;

public:
    SrsPlayResPacket();
    virtual ~SrsPlayResPacket();

public:
    void set_command_object(SrsAmf0Any *v);
    void set_desc(SrsAmf0Object *v);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

class SrsOnBWDonePacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *args_;
public:
    SrsOnBWDonePacket();
    virtual ~SrsOnBWDonePacket();

public:
    void set_args(SrsAmf0Any *v);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// The onStatus as an AMF0 command.
class SrsOnStatusCallPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    double transaction_id_;
    SrsAmf0Any *args_;
    // Such as the code, level and description.
    SrsAmf0Object *data_;

public:
    SrsOnStatusCallPacket();
    virtual ~SrsOnStatusCallPacket();

public:
    void set_args(SrsAmf0Any *v);
    void set_data(SrsAmf0Object *v);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// The onStatus as AMF0 data.
class SrsOnStatusDataPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    // Such as the code.
    SrsAmf0Object *data_;

public:
    SrsOnStatusDataPacket();
    virtual ~SrsOnStatusDataPacket();

public:
    void set_data(SrsAmf0Object *v);
    SrsAmf0Object *get_data();

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// The |RtmpSampleAccess as AMF0 data.
class SrsNaluSampleAccessPacket : public SrsRtmpCommand
{
public:
    std::string command_name_;
    // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#videoSampleAccess
    bool video_sample_access_;
    // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#audioSampleAccess
    bool audio_sample_access_;

public:
    SrsNaluSampleAccessPacket();
    virtual ~SrsNaluSampleAccessPacket();

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// FMLE sends the metadata as @setDataFrame, others as onMetaData.
class SrsOnMetaDataPacket : public SrsRtmpCommand
{
public:
    std::string name_;
    SrsAmf0Object *metadata_;

public:
    SrsOnMetaDataPacket();
    virtual ~SrsOnMetaDataPacket();

public:
    void set_metadata(SrsAmf0Object *v);

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 5.5. Window Acknowledgement Size (5)
// The window size the peer uses to send acknowledgements.
class SrsSetWindowAckSizePacket : public SrsRtmpCommand
{
public:
    int32_t ackowledgement_window_size_;

public:
    SrsSetWindowAckSizePacket();
    virtual ~SrsSetWindowAckSizePacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 5.3. Acknowledgement (3)
// Sent to the peer after receiving bytes equal to the window size.
class SrsAcknowledgementPacket : public SrsRtmpCommand
{
public:
    uint32_t sequence_number_;

public:
    SrsAcknowledgementPacket();
    virtual ~SrsAcknowledgementPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 7.1. Set Chunk Size
class SrsSetChunkSizePacket : public SrsRtmpCommand
{
public:
    // At most 65536 bytes. Each direction has its own chunk size.
    int32_t chunk_size_;

public:
    SrsSetChunkSizePacket();
    virtual ~SrsSetChunkSizePacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// The Limit type of 5.6. Set Peer Bandwidth (6).
enum SrsPeerBandwidthType {
    SrsPeerBandwidthHard = 0,
    SrsPeerBandwidthSoft = 1,
    SrsPeerBandwidthDynamic = 2,
};

// 5.6. Set Peer Bandwidth (6)
// Sent to update the output bandwidth of the peer.
class SrsSetPeerBandwidthPacket : public SrsRtmpCommand
{
public:
    int32_t bandwidth_;
    // The SrsPeerBandwidthType.
    int8_t type_;

public:
    SrsSetPeerBandwidthPacket();
    virtual ~SrsSetPeerBandwidthPacket();

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 3.7. User Control message. The event data is a 4-byte stream ID, unless noted.
enum SrcPCUCEventType {
    // Sent by the server on stream 0 after the connect command by default.
    SrcPCUCStreamBegin = 0x00,
    // The playback is over; the client discards the messages it receives for the stream.
    SrcPCUCStreamEOF = 0x01,
    // No message on the stream for a while.
    SrcPCUCStreamDry = 0x02,
    // Sent by the client before the server processes the stream. The event data is the 4-byte
    // stream ID, then the 4-byte buffer length in ms.
    SrcPCUCSetBufferLength = 0x03,
    SrcPCUCStreamIsRecorded = 0x04,
    // The event data is a 4-byte timestamp, the server time when sent. The client answers with
    // SrcPCUCPingResponse, which echoes the timestamp.
    SrcPCUCPingRequest = 0x06,
    SrcPCUCPingResponse = 0x07,

    // A FMS control event with a 3-byte payload, such as "00 1A 01": the event data is only
    // 1 byte.
    SrsPCUCFmsEvent0 = 0x1a,
};

// 5.4. User Control Message (4)
class SrsUserControlPacket : public SrsRtmpCommand
{
public:
    // The SrcPCUCEventType.
    int16_t event_type_;
    int32_t event_data_;
    // The buffer length for SrcPCUCSetBufferLength, else 0.
    int32_t extra_data_;

public:
    SrsUserControlPacket();
    virtual ~SrsUserControlPacket();

public:
    virtual srs_error_t decode(SrsBuffer *stream);

public:
    virtual int get_prefer_cid();
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

#endif
