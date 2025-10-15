//
// Copyright (c) 2013-2025 The SRS Authors
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
#include <sys/uio.h>
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

// The amf0 command message, command name macros
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

// The signature for packets to client.
#define RTMP_SIG_FMS_VER "3,5,3,888"
#define RTMP_SIG_AMF0_VER 0
#define RTMP_SIG_CLIENT_ID "ASAICiss"

// The onStatus consts.
#define StatusLevel "level"
#define StatusCode "code"
#define StatusDescription "description"
#define StatusDetails "details"
#define StatusClientId "clientid"
// The status value
#define StatusLevelStatus "status"
// The status error
#define StatusLevelError "error"
// The code value
#define StatusCodeConnectSuccess "NetConnection.Connect.Success"
#define StatusCodeConnectRejected "NetConnection.Connect.Rejected"
#define StatusCodeStreamReset "NetStream.Play.Reset"
#define StatusCodeStreamStart "NetStream.Play.Start"
#define StatusCodeStreamPause "NetStream.Pause.Notify"
#define StatusCodeStreamUnpause "NetStream.Unpause.Notify"
#define StatusCodePublishStart "NetStream.Publish.Start"
#define StatusCodeDataStart "NetStream.Data.Start"
#define StatusCodeUnpublishSuccess "NetStream.Unpublish.Success"

// The message payload is decoded as RTMP packet.
// @remark we seperate the packet from message,
//        for the packet focus on logic and domain data,
//        the message bind to the protocol and focus on protocol, such as header.
//         we can merge the message and packet, using OOAD hierachy, packet extends from message,
//         it's better for me to use components -- the message use the packet as payload.
class SrsRtmpCommand
{
public:
    SrsRtmpCommand();
    virtual ~SrsRtmpCommand();

public:
    // Covert packet to common message.
    virtual srs_error_t to_msg(SrsRtmpCommonMessage *msg, int stream_id);

public:
    // The subpacket can override this encode,
    // For example, video and audio will directly set the payload withou memory copy,
    // other packet which need to serialize/encode to bytes by override the
    // get_size and encode_packet.
    virtual srs_error_t encode(int &size, char *&payload);
    // Decode functions for concrete packet to override.
public:
    // The subpacket must override to decode packet from stream.
    // @remark never invoke the super.decode, it always failed.
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    // The subpacket must override to provide the right message type.
    // The message type set the RTMP message type in header.
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The subpacket can override to calc the packet size.
    virtual int
    get_size();
    // The subpacket can override to encode the payload to stream.
    // @remark never invoke the super.encode_packet, it always failed.
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// The protocol provides the rtmp-message-protocol services,
// To recv RTMP message from RTMP chunk stream,
// and to send out RTMP message over RTMP chunk stream.
class SrsProtocol
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    class AckWindowSize
    {
    public:
        uint32_t window_;
        // number of received bytes.
        int64_t nb_recv_bytes_;
        // previous responsed sequence number.
        uint32_t sequence_number_;

        AckWindowSize();
    };
    // For peer in/out
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The underlayer socket object, send/recv bytes.
    ISrsProtocolReadWriter *skt_;
    // The requests sent out, used to build the response.
    // key: transactionId
    // value: the request command name
    std::map<double, std::string> requests_;
    // For peer in
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The chunk stream to decode RTMP messages.
    std::map<int, SrsChunkStream *>
        chunk_streams_;
    // Cache some frequently used chunk header.
    // cs_cache, the chunk stream cache.
    SrsChunkStream **cs_cache_;
    // The bytes buffer cache, recv from skt, provide services for stream.
    SrsFastStream *in_buffer_;
    // The input chunk size, default to 128, set by peer packet.
    int32_t in_chunk_size_;
    // The input ack window, to response acknowledge to peer,
    // For example, to respose the encoder, for server got lots of packets.
    AckWindowSize in_ack_size_;
    // The output ack window, to require peer to response the ack.
    AckWindowSize out_ack_size_;
    // The buffer length set by peer.
    int32_t in_buffer_length_;
    // Whether print the protocol level debug info.
    // Generally we print the debug info when got or send first A/V packet.
    bool show_debug_info_;
    // Whether auto response when recv messages.
    // default to true for it's very easy to use the protocol stack.
    bool auto_response_when_recv_;
    // When not auto response message, manual flush the messages in queue.
    std::vector<SrsRtmpCommand *> manual_response_queue_;
    // For peer out
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Cache for multiple messages send,
    // initialize to iovec[SRS_CONSTS_IOVS_MAX] and realloc when consumed,
    // it's ok to realloc the iovs cache, for all ptr is ok.
    iovec *out_iovs_;
    int nb_out_iovs_;
    // The output header cache.
    // used for type0, 11bytes(or 15bytes with extended timestamp) header.
    // or for type3, 1bytes(or 5bytes with extended timestamp) header.
    // The c0c3 caches must use unit SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE bytes.
    //
    // @remark, the c0c3 cache cannot be realloc.
    // To allocate it in heap to make VS2015 happy.
    char *out_c0c3_caches_;
    // Whether warned user to increase the c0c3 header cache.
    bool warned_c0c3_cache_dry_;
    // The output chunk size, default to 128, set by config.
    int32_t out_chunk_size_;
    // Protocol utility for writing large iovs.
    SrsProtocolUtility *protocol_utility_;

public:
    SrsProtocol(ISrsProtocolReadWriter *io);
    virtual ~SrsProtocol();

public:
    // Set the auto response message when recv for protocol stack.
    // @param v, whether auto response message when recv message.
    virtual void set_auto_response(bool v);
    // Flush for manual response when the auto response is disabled
    // by set_auto_response(false), we default use auto response, so donot
    // need to call this api(the protocol sdk will auto send message).
    // @see the auto_response_when_recv and manual_response_queue.
    virtual srs_error_t manual_response_flush();

public:
#ifdef SRS_PERF_MERGED_READ
    // To improve read performance, merge some packets then read,
    // When it on and read small bytes, we sleep to wait more data.,
    // that is, we merge some data to read together.
    // @param v true to ename merged read.
    // @param handler the handler when merge read is enabled.
    virtual void set_merge_read(bool v, IMergeReadHandler *handler);
    // Create buffer with specifeid size.
    // @param buffer the size of buffer.
    // @remark when MR(SRS_PERF_MERGED_READ) disabled, always set to 8K.
    // @remark when buffer changed, the previous ptr maybe invalid.
    virtual void set_recv_buffer(int buffer_size);
#endif
public:
    // To set/get the recv timeout in srs_utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    // To set/get the send timeout in srs_utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    // Get recv/send bytes.
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();

public:
    // Set the input default ack size. This is generally set by the message from peer,
    // but for some encoder, it never send the ack message while it default to a none zone size.
    // This will cause the encoder to block after publishing some messages to server,
    // because it wait for server to send acknowledge, but server default to 0 which means no need
    // To ack encoder. We can change the default input ack size. We will always response the
    // ack size whatever the encoder set or not.
    virtual srs_error_t set_in_window_ack_size(int ack_size);

public:
    // Recv a RTMP message, which is bytes oriented.
    // user can use decode_message to get the decoded RTMP packet.
    // @param pmsg, set the received message,
    //       always NULL if error,
    //       NULL for unknown packet but return success.
    //       never NULL if decode success.
    // @remark, drop message when msg is empty or payload length is empty.
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    // Decode bytes oriented RTMP message to RTMP packet,
    // @param ppacket, output decoded packet,
    //       always NULL if error, never NULL if success.
    // @return error when unknown packet, error when decode failed.
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msg, the msg to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg, int stream_id);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msgs, the msgs to send out, never be NULL.
    // @param nb_msgs, the size of msgs to send out.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id);
    // Send the RTMP packet and always free it.
    // user must never free or use the packet after this method,
    // For it will always free the packet.
    // @param packet, the packet to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id);

public:
    // Expect a specified message, drop others util got specified one.
    // @pmsg, user must free it. NULL if not success.
    // @ppacket, user must free it, which decode from payload of message. NULL if not success.
    // @remark, only when success, user can use and must free the pmsg and ppacket.
    // For example:
    //          SrsRtmpCommonMessage* msg = NULL;
    //          SrsConnectAppResPacket* pkt = NULL;
    //          if ((ret = protocol->expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
    //              return ret;
    //          }
    //          // Use then free msg and pkt
    //          srs_freep(msg);
    //          srs_freep(pkt);
    // user should never recv message and convert it, use this method instead.
    // if need to set timeout, use set timeout of SrsProtocol.
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
    // Send out the messages, donot free it,
    // The caller must free the param msgs.
    virtual srs_error_t
    do_send_messages(SrsMediaPacket **msgs, int nb_msgs);
    // Send iovs. send multiple times if exceed limits.
    virtual srs_error_t do_iovs_send(iovec *iovs, int size);
    // The underlayer api for send and free packet.
    virtual srs_error_t do_send_and_free_packet(SrsRtmpCommand *packet, int stream_id);
    // The imp for decode_message
    virtual srs_error_t do_decode_message(SrsMessageHeader &header, SrsBuffer *stream, SrsRtmpCommand **ppacket);
    // Recv bytes oriented RTMP message from protocol stack.
    // return error if error occur and nerver set the pmsg,
    // return success and pmsg set to NULL if no entire message got,
    // return success and pmsg set to entire message if got one.
    virtual srs_error_t recv_interlaced_message(SrsRtmpCommonMessage **pmsg);
    // Read the chunk basic header(fmt, cid) from chunk stream.
    // user can discovery a SrsChunkStream by cid.
    virtual srs_error_t read_basic_header(char &fmt, int &cid);
    // Read the chunk message header(timestamp, payload_length, message_type, stream_id)
    // From chunk stream and save to SrsChunkStream.
    virtual srs_error_t read_message_header(SrsChunkStream *chunk, char fmt);
    // Read the chunk payload, remove the used bytes in buffer,
    // if got entire message, set the pmsg.
    virtual srs_error_t read_message_payload(SrsChunkStream *chunk, SrsRtmpCommonMessage **pmsg);
    // When recv message, update the context.
    virtual srs_error_t on_recv_message(SrsRtmpCommonMessage *msg);
    // When message sentout, update the context.
    virtual srs_error_t on_send_packet(SrsMessageHeader *mh, SrsRtmpCommand *packet);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Auto response the ack message.
    virtual srs_error_t
    response_acknowledgement_message();
    // Auto response the ping message.
    virtual srs_error_t response_ping_message(int32_t timestamp);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void print_debug_info();
};

// incoming chunk stream maybe interlaced,
// Use the chunk stream to cache the input RTMP chunk streams.
class SrsChunkStream
{
public:
    // Represents the basic header fmt,
    // which used to identify the variant message header type.
    char fmt_;
    // Represents the basic header cid,
    // which is the chunk stream id.
    int cid_;
    // Cached message header
    SrsMessageHeader header_;
    // Whether the chunk message header has extended timestamp.
    bool has_extended_timestamp_;
    // The partially read message.
    SrsRtmpCommonMessage *msg_;
    // Current writing position of message.
    char *writing_pos_;
    // Decoded msg count, to identify whether the chunk stream is fresh.
    int64_t msg_count_;
    // Because the extended timestamp may be a delta timestamp, it can differ
    // from the timestamp in the header, so it should be stored as a distinct field
    // for comparison with the extended timestamp of subsequent chunks.
    // See https://github.com/ossrs/srs/pull/4356 for details.
    int32_t extended_timestamp_;

public:
    SrsChunkStream(int _cid);
    virtual ~SrsChunkStream();
};

class ISrsRequest
{
public:
    // The client ip.
    std::string ip_;

public:
    // Support pass vhost in RTMP URL, such as:
    //    rtmp://VHOST:port/app/stream
    //    rtmp://ip:port/app/stream?vhost=VHOST
    //    rtmp://ip:port/app?vhost=VHOST/stream
    //    rtmp://ip:port/app...vhost...VHOST/stream
    // While tcUrl is url without stream.
    std::string tcUrl_;

public:
    std::string pageUrl_;
    std::string swfUrl_;
    double objectEncoding_;
    // The data discovery from request.
public:
    // Discovery from tcUrl and play/publish.
    std::string schema_;
    // The vhost in tcUrl.
    std::string vhost_;
    // The host in tcUrl.
    std::string host_;
    // The port in tcUrl.
    int port_;
    // The app in tcUrl, without param.
    std::string app_;
    // The param in tcUrl(app).
    std::string param_;
    // The stream in play/publish
    std::string stream_;
    // User specify the ice-ufrag, the username of ice, for test only.
    std::string ice_ufrag_;
    // User specify the ice-pwd, the password of ice, for test only.
    std::string ice_pwd_;
    // For play live stream,
    // used to specified the stop when exceed the duration.
    // in srs_utime_t.
    srs_utime_t duration_;
    // The token in the connect request,
    // used for edge traverse to origin authentication,
    // @see https://github.com/ossrs/srs/issues/104
    SrsAmf0Object *args_;

public:
    // The protocol of client:
    //      rtmp, Adobe RTMP protocol.
    //      flv, HTTP-FLV protocol.
    //      flvs, HTTPS-FLV protocol.
    std::string protocol_;

public:
    ISrsRequest();
    virtual ~ISrsRequest();

public:
    // Deep copy the request, for source to use it to support reload,
    // For when initialize the source, the request is valid,
    // When reload it, the request maybe invalid, so need to copy it.
    virtual ISrsRequest *copy() = 0;
    // update the auth info of request,
    // To keep the current request ptr is ok,
    // For many components use the ptr of request.
    virtual void update_auth(ISrsRequest *req) = 0;
    // Get the stream identify, vhost/app/stream.
    virtual std::string get_stream_url() = 0;
    // To strip url, user must strip when update the url.
    virtual void strip() = 0;

public:
    // Transform it as HTTP request.
    virtual ISrsRequest *as_http() = 0;

public:
    // Callback when source is created.
    virtual void on_source_created();
};

// The original request from client.
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

// The response to client.
class SrsResponse
{
public:
    // The stream id to response client createStream.
    int stream_id_;

public:
    SrsResponse();
    virtual ~SrsResponse();
};

// The rtmp client type.
enum SrsRtmpConnType {
    SrsRtmpConnUnknown = 0x0000,
    // All players.
    SrsRtmpConnPlay = 0x0100,
    SrsHlsPlay = 0x0101,
    SrsFlvPlay = 0x0102,
    SrsRtcConnPlay = 0x0110,
    SrsSrtConnPlay = 0x0120,
    // All publishers.
    SrsRtmpConnFMLEPublish = 0x0200,
    SrsRtmpConnFlashPublish = 0x0201,
    SrsRtmpConnHaivisionPublish = 0x0202,
    SrsRtcConnPublish = 0x0210,
    SrsSrtConnPublish = 0x0220,
};
std::string srs_client_type_string(SrsRtmpConnType type);
bool srs_client_type_is_publish(SrsRtmpConnType type);

// store the handshake bytes,
// For smart switch between complex and simple handshake.
class SrsHandshakeBytes
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRand rand_;

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

// The information return from RTMP server.
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

// implements the client role protocol.
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
    // Protocol methods proxy
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
    // handshake with server, try complex, then simple handshake.
    virtual srs_error_t handshake();
    // only use simple handshake
    virtual srs_error_t simple_handshake();
    // only use complex handshake
    virtual srs_error_t complex_handshake();
    // Connect to RTMP tcUrl and app, get the server info.
    //
    // @param app, The app to connect at, for example, live.
    // @param tcUrl, The tcUrl to connect at, for example, rtmp://ossrs.net/live.
    // @param req, the optional req object, use the swfUrl/pageUrl if specified. NULL to ignore.
    // @param dsu, Whether debug SRS upnode. For edge, set to true to send its info to upnode.
    // @param si, The server information, retrieve from response of connect app request. NULL to ignore.
    virtual srs_error_t connect_app(std::string app, std::string tcUrl, ISrsRequest *r, bool dsu, SrsServerInfo *si);
    // Create a stream, then play/publish data over this stream.
    virtual srs_error_t create_stream(int &stream_id);
    // start play stream.
    virtual srs_error_t play(std::string stream, int stream_id, int chunk_size);
    // start publish stream. use flash publish workflow:
    //       connect-app => create-stream => flash-publish
    virtual srs_error_t publish(std::string stream, int stream_id, int chunk_size);
    // start publish stream. use FMLE publish workflow:
    //       connect-app => FMLE publish
    virtual srs_error_t fmle_publish(std::string stream, int &stream_id);

public:
    // Expect a specified message, drop others util got specified one.
    // @pmsg, user must free it. NULL if not success.
    // @ppacket, user must free it, which decode from payload of message. NULL if not success.
    // @remark, only when success, user can use and must free the pmsg and ppacket.
    // For example:
    //          SrsRtmpCommonMessage* msg = NULL;
    //          SrsConnectAppResPacket* pkt = NULL;
    //          if ((ret = client->expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
    //              return ret;
    //          }
    //          // Use then free msg and pkt
    //          srs_freep(msg);
    //          srs_freep(pkt);
    // user should never recv message and convert it, use this method instead.
    // if need to set timeout, use set timeout of SrsProtocol.
    template <class T>
    srs_error_t expect_message(SrsRtmpCommonMessage **pmsg, T **ppacket)
    {
        return protocol_->expect_message<T>(pmsg, ppacket);
    }
};

// The rtmp server interface.
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

// The rtmp provices rtmp-command-protocol services,
// a high level protocol, media stream oriented services,
// such as connect to vhost/app, play stream, get audio/video data.
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
    // Protocol methods proxy
public:
    // Set the auto response message when recv for protocol stack.
    // @param v, whether auto response message when recv message.
    virtual void set_auto_response(bool v);
#ifdef SRS_PERF_MERGED_READ
    // To improve read performance, merge some packets then read,
    // When it on and read small bytes, we sleep to wait more data.,
    // that is, we merge some data to read together.
    // @param v true to ename merged read.
    // @param handler the handler when merge read is enabled.
    virtual void set_merge_read(bool v, IMergeReadHandler *handler);
    // Create buffer with specifeid size.
    // @param buffer the size of buffer.
    // @remark when MR(SRS_PERF_MERGED_READ) disabled, always set to 8K.
    // @remark when buffer changed, the previous ptr maybe invalid.
    virtual void set_recv_buffer(int buffer_size);
#endif
    // To set/get the recv timeout in srs_utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual srs_utime_t get_recv_timeout();
    // To set/get the send timeout in srs_utime_t.
    // if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    virtual void set_send_timeout(srs_utime_t tm);
    virtual srs_utime_t get_send_timeout();
    // Get recv/send bytes.
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    // Recv a RTMP message, which is bytes oriented.
    // user can use decode_message to get the decoded RTMP packet.
    // @param pmsg, set the received message,
    //       always NULL if error,
    //       NULL for unknown packet but return success.
    //       never NULL if decode success.
    // @remark, drop message when msg is empty or payload length is empty.
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    // Decode bytes oriented RTMP message to RTMP packet,
    // @param ppacket, output decoded packet,
    //       always NULL if error, never NULL if success.
    // @return error when unknown packet, error when decode failed.
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msg, the msg to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg, int stream_id);
    // Send the RTMP message and always free it.
    // user must never free or use the msg after this method,
    // For it will always free the msg.
    // @param msgs, the msgs to send out, never be NULL.
    // @param nb_msgs, the size of msgs to send out.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    //
    // @remark performance issue, to support 6k+ 250kbps client,
    virtual srs_error_t send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id);
    // Send the RTMP packet and always free it.
    // user must never free or use the packet after this method,
    // For it will always free the packet.
    // @param packet, the packet to send out, never be NULL.
    // @param stream_id, the stream id of packet to send over, 0 for control message.
    virtual srs_error_t send_and_free_packet(SrsRtmpCommand *packet, int stream_id);

public:
    // Do handshake with client, try complex then simple.
    virtual srs_error_t handshake();
    // Do connect app with client, to discovery tcUrl.
    virtual srs_error_t connect_app(ISrsRequest *req);
    // Set output ack size to client, client will send ack-size for each ack window
    virtual srs_error_t set_window_ack_size(int ack_size);
    // Set the default input ack size value.
    virtual srs_error_t set_in_window_ack_size(int ack_size);
    // @type: The sender can mark this message hard (0), soft (1), or dynamic (2)
    // using the Limit type field.
    virtual srs_error_t set_peer_bandwidth(int bandwidth, int type);
    // @param server_ip the ip of server.
    virtual srs_error_t response_connect_app(ISrsRequest *req, const char *server_ip = NULL);
    // Redirect the connection to another rtmp server.
    // @param a RTMP url to redirect to.
    // @param whether the client accept the redirect.
    virtual srs_error_t redirect(ISrsRequest *r, std::string url, bool &accepted);
    // Reject the connect app request.
    virtual void response_connect_reject(ISrsRequest *req, const char *desc);
    // Response  client the onBWDone message.
    virtual srs_error_t on_bw_done();
    // Recv some message to identify the client.
    // @stream_id, client will createStream to play or publish by flash,
    //         the stream_id used to response the createStream request.
    // @type, output the client type.
    // @stream_name, output the client publish/play stream name. @see: SrsRequest.stream
    // @duration, output the play client duration. @see: SrsRequest.duration
    virtual srs_error_t identify_client(int stream_id, SrsRtmpConnType &type, std::string &stream_name, srs_utime_t &duration);
    // Set the chunk size when client type identified.
    virtual srs_error_t set_chunk_size(int chunk_size);
    // When client type is play, response with packets:
    // StreamBegin,
    // onStatus(NetStream.Play.Reset), onStatus(NetStream.Play.Start).,
    // |RtmpSampleAccess(false, false),
    // onStatus(NetStream.Data.Start).
    virtual srs_error_t start_play(int stream_id);
    // When client(type is play) send pause message,
    // if is_pause, response the following packets:
    //     onStatus(NetStream.Pause.Notify)
    //     StreamEOF
    // if not is_pause, response the following packets:
    //     onStatus(NetStream.Unpause.Notify)
    //     StreamBegin
    virtual srs_error_t on_play_client_pause(int stream_id, bool is_pause);
    // When client type is publish, response with packets:
    // releaseStream response
    // FCPublish
    // FCPublish response
    // createStream response
    // onFCPublish(NetStream.Publish.Start)
    // onStatus(NetStream.Publish.Start)
    virtual srs_error_t start_fmle_publish(int stream_id);
    // For encoder of Haivision, response the startup request.
    // @see https://github.com/ossrs/srs/issues/844
    virtual srs_error_t start_haivision_publish(int stream_id);
    // process the FMLE unpublish event.
    // @unpublish_tid the unpublish request transaction id.
    virtual srs_error_t fmle_unpublish(int stream_id, double unpublish_tid);
    // When client type is publish, response with packets:
    // onStatus(NetStream.Publish.Start)
    virtual srs_error_t start_flash_publish(int stream_id);
    // Response the start publishing message after hooks verified. To stop reconnecting of
    // OBS when publish failed, we should never send the onStatus(NetStream.Publish.Start)
    // message before failure caused by hooks. See https://github.com/ossrs/srs/issues/4037
    virtual srs_error_t start_publishing(int stream_id);

public:
    // Expect a specified message, drop others util got specified one.
    // @pmsg, user must free it. NULL if not success.
    // @ppacket, user must free it, which decode from payload of message. NULL if not success.
    // @remark, only when success, user can use and must free the pmsg and ppacket.
    // For example:
    //          SrsRtmpCommonMessage* msg = NULL;
    //          SrsConnectAppResPacket* pkt = NULL;
    //          if ((ret = server->expect_message<SrsConnectAppResPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
    //              return ret;
    //          }
    //          // Use then free msg and pkt
    //          srs_freep(msg);
    //          srs_freep(pkt);
    // user should never recv message and convert it, use this method instead.
    // if need to set timeout, use set timeout of SrsProtocol.
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
// The client sends the connect command to the server to request
// connection to a server application instance.
class SrsConnectAppPacket : public SrsRtmpCommand
{
public:
    // Name of the command. Set to "connect".
    std::string command_name_;
    // Always set to 1.
    double transaction_id_;
    // Command information object which has the name-value pairs.
    // @remark: alloc in packet constructor, user can directly use it,
    //       user should never alloc it again which will cause memory leak.
    // @remark, never be NULL.
    SrsAmf0Object *command_object_;
    // Any optional information
    // @remark, optional, init to and maybe NULL.
    SrsAmf0Object *args_;

public:
    SrsConnectAppPacket();
    virtual ~SrsConnectAppPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};
// Response  for SrsConnectAppPacket.
class SrsConnectAppResPacket : public SrsRtmpCommand
{
public:
    // The _result or _error; indicates whether the response is result or error.
    std::string command_name_;
    // Transaction ID is 1 for call connect responses
    double transaction_id_;
    // Name-value pairs that describe the properties(fmsver etc.) of the connection.
    // @remark, never be NULL.
    SrsAmf0Object *props_;
    // Name-value pairs that describe the response from|the server. 'code',
    // 'level', 'description' are names of few among such information.
    // @remark, never be NULL.
    SrsAmf0Object *info_;

public:
    SrsConnectAppResPacket();
    virtual ~SrsConnectAppResPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.1.2. Call
// The call method of the NetConnection object runs remote procedure
// calls (RPC) at the receiving end. The called RPC name is passed as a
// parameter to the call command.
class SrsCallPacket : public SrsRtmpCommand
{
public:
    // Name of the remote procedure that is called.
    std::string command_name_;
    // If a response is expected we give a transaction Id. Else we pass a value of 0
    double transaction_id_;
    // If there exists any command info this
    // is set, else this is set to null type.
    // @remark, optional, init to and maybe NULL.
    SrsAmf0Any *command_object_;
    // Any optional arguments to be provided
    // @remark, optional, init to and maybe NULL.
    SrsAmf0Any *arguments_;

public:
    SrsCallPacket();
    virtual ~SrsCallPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};
// Response  for SrsCallPacket.
class SrsCallResPacket : public SrsRtmpCommand
{
public:
    // Name of the command.
    std::string command_name_;
    // ID of the command, to which the response belongs to
    double transaction_id_;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, optional, init to and maybe NULL.
    SrsAmf0Any *command_object_;
    // Response from the method that was called.
    // @remark, optional, init to and maybe NULL.
    SrsAmf0Any *response_;

public:
    SrsCallResPacket(double _transaction_id);
    virtual ~SrsCallResPacket();
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.1.3. createStream
// The client sends this command to the server to create a logical
// channel for message communication The publishing of audio, video, and
// metadata is carried out over stream channel created using the
// createStream command.
class SrsCreateStreamPacket : public SrsRtmpCommand
{
public:
    // Name of the command. Set to "createStream".
    std::string command_name_;
    // Transaction ID of the command.
    double transaction_id_;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
public:
    SrsCreateStreamPacket();
    virtual ~SrsCreateStreamPacket();

public:
    void set_command_object(SrsAmf0Any *v);
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};
// Response  for SrsCreateStreamPacket.
class SrsCreateStreamResPacket : public SrsRtmpCommand
{
public:
    // The _result or _error; indicates whether the response is result or error.
    std::string command_name_;
    // ID of the command that response belongs to.
    double transaction_id_;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // The return value is either a stream ID or an error information object.
    double stream_id_;

public:
    SrsCreateStreamResPacket(double _transaction_id, double _stream_id);
    virtual ~SrsCreateStreamResPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// client close stream packet.
class SrsCloseStreamPacket : public SrsRtmpCommand
{
public:
    // Name of the command, set to "closeStream".
    std::string command_name_;
    // Transaction ID set to 0.
    double transaction_id_;
    // Command information object does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
public:
    SrsCloseStreamPacket();
    virtual ~SrsCloseStreamPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
};

// FMLE start publish: ReleaseStream/PublishStream/FCPublish/FCUnpublish
class SrsFMLEStartPacket : public SrsRtmpCommand
{
public:
    // Name of the command
    std::string command_name_;
    // The transaction ID to get the response.
    double transaction_id_;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // The stream name to start publish or release.
    std::string stream_name_;

public:
    SrsFMLEStartPacket();
    virtual ~SrsFMLEStartPacket();

public:
    void set_command_object(SrsAmf0Any *v);
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
    // Factory method to create specified FMLE packet.
public:
    static SrsFMLEStartPacket *create_release_stream(std::string stream);
    static SrsFMLEStartPacket *create_FC_publish(std::string stream);
};
// Response  for SrsFMLEStartPacket.
class SrsFMLEStartResPacket : public SrsRtmpCommand
{
public:
    // Name of the command
    std::string command_name_;
    // The transaction ID to get the response.
    double transaction_id_;
    // If there exists any command info this is set, else this is set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // The optional args, set to undefined.
    // @remark, never be NULL, an AMF0 undefined instance.
    SrsAmf0Any *args_; // undefined
public:
    SrsFMLEStartResPacket(double _transaction_id);
    virtual ~SrsFMLEStartResPacket();

public:
    void set_args(SrsAmf0Any *v);
    void set_command_object(SrsAmf0Any *v);
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// FMLE/flash publish
// 4.2.6. Publish
// The client sends the publish command to publish a named stream to the
// server. Using this name, any client can play this stream and receive
// The published audio, video, and data messages.
class SrsPublishPacket : public SrsRtmpCommand
{
public:
    // Name of the command, set to "publish".
    std::string command_name_;
    // Transaction ID set to 0.
    double transaction_id_;
    // Command information object does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // Name with which the stream is published.
    std::string stream_name_;
    // Type of publishing. Set to "live", "record", or "append".
    //   record: The stream is published and the data is recorded to a new file.The file
    //           is stored on the server in a subdirectory within the directory that
    //           contains the server application. If the file already exists, it is
    //           overwritten.
    //   append: The stream is published and the data is appended to a file. If no file
    //           is found, it is created.
    //   live: Live data is published without recording it in a file.
    // @remark, SRS only support live.
    // @remark, optional, default to live.
    std::string type_;

public:
    SrsPublishPacket();
    virtual ~SrsPublishPacket();

public:
    void set_command_object(SrsAmf0Any *v);
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 4.2.8. pause
// The client sends the pause command to tell the server to pause or
// start playing.
class SrsPausePacket : public SrsRtmpCommand
{
public:
    // Name of the command, set to "pause".
    std::string command_name_;
    // There is no transaction ID for this command. Set to 0.
    double transaction_id_;
    // Command information object does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // true or false, to indicate pausing or resuming play
    bool is_pause_;
    // Number of milliseconds at which the the stream is paused or play resumed.
    // This is the current stream time at the Client when stream was paused. When the
    // playback is resumed, the server will only send messages with timestamps
    // greater than this value.
    double time_ms_;

public:
    SrsPausePacket();
    virtual ~SrsPausePacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
};

// 4.2.1. play
// The client sends this command to the server to play a stream.
class SrsPlayPacket : public SrsRtmpCommand
{
public:
    // Name of the command. Set to "play".
    std::string command_name_;
    // Transaction ID set to 0.
    double transaction_id_;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // Name of the stream to play.
    // To play video (FLV) files, specify the name of the stream without a file
    //       extension (for example, "sample").
    // To play back MP3 or ID3 tags, you must precede the stream name with mp3:
    //       (for example, "mp3:sample".)
    // To play H.264/AAC files, you must precede the stream name with mp4: and specify the
    //       file extension. For example, to play the file sample.m4v, specify
    //       "mp4:sample.m4v"
    std::string stream_name_;
    // An optional parameter that specifies the start time in seconds.
    // The default value is -2, which means the subscriber first tries to play the live
    //       stream specified in the Stream Name field. If a live stream of that name is
    //       not found, it plays the recorded stream specified in the Stream Name field.
    // If you pass -1 in the Start field, only the live stream specified in the Stream
    //       Name field is played.
    // If you pass 0 or a positive number in the Start field, a recorded stream specified
    //       in the Stream Name field is played beginning from the time specified in the
    //       Start field.
    // If no recorded stream is found, the next item in the playlist is played.
    double start_;
    // An optional parameter that specifies the duration of playback in seconds.
    // The default value is -1. The -1 value means a live stream is played until it is no
    //       longer available or a recorded stream is played until it ends.
    // If u pass 0, it plays the single frame since the time specified in the Start field
    //       from the beginning of a recorded stream. It is assumed that the value specified
    //       in the Start field is equal to or greater than 0.
    // If you pass a positive number, it plays a live stream for the time period specified
    //       in the Duration field. After that it becomes available or plays a recorded
    //       stream for the time specified in the Duration field. (If a stream ends before the
    //       time specified in the Duration field, playback ends when the stream ends.)
    // If you pass a negative number other than -1 in the Duration field, it interprets the
    //       value as if it were -1.
    double duration_;
    // An optional Boolean value or number that specifies whether to flush any
    // previous playlist.
    bool reset_;

public:
    SrsPlayPacket();
    virtual ~SrsPlayPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// Response  for SrsPlayPacket.
// @remark, user must set the stream_id in header.
class SrsPlayResPacket : public SrsRtmpCommand
{
public:
    // Name of the command. If the play command is successful, the command
    // name is set to onStatus.
    std::string command_name_;
    // Transaction ID set to 0.
    double transaction_id_;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *command_object_; // null
    // If the play command is successful, the client receives OnStatus message from
    // server which is NetStream.Play.Start. If the specified stream is not found,
    // NetStream.Play.StreamNotFound is received.
    // @remark, never be NULL, an AMF0 object instance.
    SrsAmf0Object *desc_;

public:
    SrsPlayResPacket();
    virtual ~SrsPlayResPacket();

public:
    void set_command_object(SrsAmf0Any *v);
    void set_desc(SrsAmf0Object *v);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// When bandwidth test done, notice client.
class SrsOnBWDonePacket : public SrsRtmpCommand
{
public:
    // Name of command. Set to "onBWDone"
    std::string command_name_;
    // Transaction ID set to 0.
    double transaction_id_;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *args_; // null
public:
    SrsOnBWDonePacket();
    virtual ~SrsOnBWDonePacket();

public:
    void set_args(SrsAmf0Any *v);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// onStatus command, AMF0 Call
// @remark, user must set the stream_id by SrsRtmpCommonMessage.set_packet().
class SrsOnStatusCallPacket : public SrsRtmpCommand
{
public:
    // Name of command. Set to "onStatus"
    std::string command_name_;
    // Transaction ID set to 0.
    double transaction_id_;
    // Command information does not exist. Set to null type.
    // @remark, never be NULL, an AMF0 null instance.
    SrsAmf0Any *args_; // null
    // Name-value pairs that describe the response from the server.
    // 'code','level', 'description' are names of few among such information.
    // @remark, never be NULL, an AMF0 object instance.
    SrsAmf0Object *data_;

public:
    SrsOnStatusCallPacket();
    virtual ~SrsOnStatusCallPacket();

public:
    void set_args(SrsAmf0Any *v);
    void set_data(SrsAmf0Object *v);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// onStatus data, AMF0 Data
// @remark, user must set the stream_id by SrsRtmpCommonMessage.set_packet().
class SrsOnStatusDataPacket : public SrsRtmpCommand
{
public:
    // Name of command. Set to "onStatus"
    std::string command_name_;
    // Name-value pairs that describe the response from the server.
    // 'code', are names of few among such information.
    // @remark, never be NULL, an AMF0 object instance.
    SrsAmf0Object *data_;

public:
    SrsOnStatusDataPacket();
    virtual ~SrsOnStatusDataPacket();

public:
    void set_data(SrsAmf0Object *v);
    SrsAmf0Object *get_data();
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// AMF0Data RtmpSampleAccess
// @remark, user must set the stream_id by SrsRtmpCommonMessage.set_packet().
class SrsNaluSampleAccessPacket : public SrsRtmpCommand
{
public:
    // Name of command. Set to "|RtmpSampleAccess".
    std::string command_name_;
    // Whether allow access the sample of video.
    // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#videoSampleAccess
    bool video_sample_access_;
    // Whether allow access the sample of audio.
    // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#audioSampleAccess
    bool audio_sample_access_;

public:
    SrsNaluSampleAccessPacket();
    virtual ~SrsNaluSampleAccessPacket();
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// The stream metadata.
// FMLE: @setDataFrame
// others: onMetaData
class SrsOnMetaDataPacket : public SrsRtmpCommand
{
public:
    // Name of metadata. Set to "onMetaData"
    std::string name_;
    // Metadata of stream.
    // @remark, never be NULL, an AMF0 object instance.
    SrsAmf0Object *metadata_;

public:
    SrsOnMetaDataPacket();
    virtual ~SrsOnMetaDataPacket();

public:
    void set_metadata(SrsAmf0Object *v);
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 5.5. Window Acknowledgement Size (5)
// The client or the server sends this message to inform the peer which
// window size to use when sending acknowledgment.
class SrsSetWindowAckSizePacket : public SrsRtmpCommand
{
public:
    int32_t ackowledgement_window_size_;

public:
    SrsSetWindowAckSizePacket();
    virtual ~SrsSetWindowAckSizePacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 5.3. Acknowledgement (3)
// The client or the server sends the acknowledgment to the peer after
// receiving bytes equal to the window size.
class SrsAcknowledgementPacket : public SrsRtmpCommand
{
public:
    uint32_t sequence_number_;

public:
    SrsAcknowledgementPacket();
    virtual ~SrsAcknowledgementPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 7.1. Set Chunk Size
// Protocol control message 1, Set Chunk Size, is used to notify the
// peer about the new maximum chunk size.
class SrsSetChunkSizePacket : public SrsRtmpCommand
{
public:
    // The maximum chunk size can be 65536 bytes. The chunk size is
    // maintained independently for each direction.
    int32_t chunk_size_;

public:
    SrsSetChunkSizePacket();
    virtual ~SrsSetChunkSizePacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 5.6. Set Peer Bandwidth (6)
enum SrsPeerBandwidthType {
    // The sender can mark this message hard (0), soft (1), or dynamic (2)
    // using the Limit type field.
    SrsPeerBandwidthHard = 0,
    SrsPeerBandwidthSoft = 1,
    SrsPeerBandwidthDynamic = 2,
};

// 5.6. Set Peer Bandwidth (6)
// The client or the server sends this message to update the output
// bandwidth of the peer.
class SrsSetPeerBandwidthPacket : public SrsRtmpCommand
{
public:
    int32_t bandwidth_;
    // @see: SrsPeerBandwidthType
    int8_t type_;

public:
    SrsSetPeerBandwidthPacket();
    virtual ~SrsSetPeerBandwidthPacket();
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

// 3.7. User Control message
enum SrcPCUCEventType {
    // Generally, 4bytes event-data

    // The server sends this event to notify the client
    // that a stream has become functional and can be
    // used for communication. By default, this event
    // is sent on ID 0 after the application connect
    // command is successfully received from the
    // client. The event data is 4-byte and represents
    // The stream ID of the stream that became
    // Functional.
    SrcPCUCStreamBegin = 0x00,

    // The server sends this event to notify the client
    // that the playback of data is over as requested
    // on this stream. No more data is sent without
    // issuing additional commands. The client discards
    // The messages received for the stream. The
    // 4 bytes of event data represent the ID of the
    // stream on which playback has ended.
    SrcPCUCStreamEOF = 0x01,

    // The server sends this event to notify the client
    // that there is no more data on the stream. If the
    // server does not detect any message for a time
    // period, it can notify the subscribed clients
    // that the stream is dry. The 4 bytes of event
    // data represent the stream ID of the dry stream.
    SrcPCUCStreamDry = 0x02,

    // The client sends this event to inform the server
    // of the buffer size (in milliseconds) that is
    // used to buffer any data coming over a stream.
    // This event is sent before the server starts
    // processing the stream. The first 4 bytes of the
    // event data represent the stream ID and the next
    // 4 bytes represent the buffer length, in
    // milliseconds.
    SrcPCUCSetBufferLength = 0x03, // 8bytes event-data

    // The server sends this event to notify the client
    // that the stream is a recorded stream. The
    // 4 bytes event data represent the stream ID of
    // The recorded stream.
    SrcPCUCStreamIsRecorded = 0x04,

    // The server sends this event to test whether the
    // client is reachable. Event data is a 4-byte
    // timestamp, representing the local server time
    // When the server dispatched the command. The
    // client responds with kMsgPingResponse on
    // receiving kMsgPingRequest.
    SrcPCUCPingRequest = 0x06,

    // The client sends this event to the server in
    // Response  to the ping request. The event data is
    // a 4-byte timestamp, which was received with the
    // kMsgPingRequest request.
    SrcPCUCPingResponse = 0x07,

    // For PCUC size=3, for example the payload is "00 1A 01",
    // it's a FMS control event, where the event type is 0x001a and event data is 0x01,
    // please notice that the event data is only 1 byte for this event.
    SrsPCUCFmsEvent0 = 0x1a,
};

// 5.4. User Control Message (4)
//
// For the EventData is 4bytes.
// Stream Begin(=0)              4-bytes stream ID
// Stream EOF(=1)                4-bytes stream ID
// StreamDry(=2)                 4-bytes stream ID
// SetBufferLength(=3)           8-bytes 4bytes stream ID, 4bytes buffer length.
// StreamIsRecorded(=4)          4-bytes stream ID
// PingRequest(=6)               4-bytes timestamp local server time
// PingResponse(=7)              4-bytes timestamp received ping request.
//
// 3.7. User Control message
// +------------------------------+-------------------------
// | Event Type ( 2- bytes ) | Event Data
// +------------------------------+-------------------------
// Figure 5 Pay load for the 'User Control Message'.
class SrsUserControlPacket : public SrsRtmpCommand
{
public:
    // Event type is followed by Event data.
    // @see: SrcPCUCEventType
    int16_t event_type_;
    // The event data generally in 4bytes.
    // @remark for event type is 0x001a, only 1bytes.
    // @see SrsPCUCFmsEvent0
    int32_t event_data_;
    // 4bytes if event_type is SetBufferLength; otherwise 0.
    int32_t extra_data_;

public:
    SrsUserControlPacket();
    virtual ~SrsUserControlPacket();
    // Decode functions for concrete packet to override.
public:
    virtual srs_error_t decode(SrsBuffer *stream);
    // Encode functions for concrete packet to override.
public:
    virtual int get_message_type();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual int get_size();
    virtual srs_error_t encode_packet(SrsBuffer *stream);
};

#endif
