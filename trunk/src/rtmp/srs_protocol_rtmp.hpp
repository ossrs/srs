/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SRS_RTMP_PROTOCOL_RTMP_HPP
#define SRS_RTMP_PROTOCOL_RTMP_HPP

/*
#include <srs_protocol_rtmp.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <srs_protocol_stack.hpp>

class SrsProtocol;
class ISrsProtocolReaderWriter;
class ISrsMessage;
class SrsCommonMessage;
class SrsCreateStreamPacket;
class SrsFMLEStartPacket;
class SrsPublishPacket;
class SrsOnMetaDataPacket;
class SrsPlayPacket;
class SrsMessage;
class SrsPacket;
class SrsAmf0Object;

/**
* the original request from client.
*/
class SrsRequest
{
public:
    /**
    * tcUrl: rtmp://request_vhost:port/app/stream
    * support pass vhost in query string, such as:
    *    rtmp://ip:port/app?vhost=request_vhost/stream
    *    rtmp://ip:port/app...vhost...request_vhost/stream
    */
    std::string tcUrl;
    std::string pageUrl;
    std::string swfUrl;
    double objectEncoding;
// data discovery from request.
public:
    // discovery from tcUrl and play/publish.
    std::string schema;
    // the vhost in tcUrl.
    std::string vhost;
    // the host in tcUrl.
    std::string host;
    // the port in tcUrl.
    std::string port;
    // the app in tcUrl, without param.
    std::string app;
    // the param in tcUrl(app).
    std::string param;
    // the stream in play/publish
    std::string stream;
    // for play live stream, 
    // used to specified the stop when exceed the duration.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/45
    // in ms.
    double duration;
    // the token in the connect request,
    // used for edge traverse to origin authentication,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/104
    SrsAmf0Object* args;
public:
    SrsRequest();
    virtual ~SrsRequest();
public:
    /**
    * deep copy the request, for source to use it to support reload,
    * for when initialize the source, the request is valid,
    * when reload it, the request maybe invalid, so need to copy it.
    */
    virtual SrsRequest* copy();
    /**
    * update the auth info of request,
    * to keep the current request ptr is ok,
    * for many components use the ptr of request.
    */
    virtual void update_auth(SrsRequest* req);
    /**
    * get the stream identify, vhost/app/stream.
    */
    virtual std::string get_stream_url();
    /**
    * strip url, user must strip when update the url.
    */
    virtual void strip();
};

/**
* the response to client.
*/
class SrsResponse
{
public:
    /**
    * the stream id to response client createStream.
    */
    int stream_id;
public:
    SrsResponse();
    virtual ~SrsResponse();
};

/**
* the rtmp client type.
*/
enum SrsRtmpConnType
{
    SrsRtmpConnUnknown,
    SrsRtmpConnPlay,
    SrsRtmpConnFMLEPublish,
    SrsRtmpConnFlashPublish,
};
std::string srs_client_type_string(SrsRtmpConnType type);

/**
* store the handshake bytes, 
* for smart switch between complex and simple handshake.
*/
class SrsHandshakeBytes
{
public:
    // [1+1536]
    char* c0c1;
    // [1+1536+1536]
    char* s0s1s2;
    // [1536]
    char* c2;
public:
    SrsHandshakeBytes();
    virtual ~SrsHandshakeBytes();
public:
    virtual int read_c0c1(ISrsProtocolReaderWriter* io);
    virtual int read_s0s1s2(ISrsProtocolReaderWriter* io);
    virtual int read_c2(ISrsProtocolReaderWriter* io);
    virtual int create_c0c1();
    virtual int create_s0s1s2(const char* c1 = NULL);
    virtual int create_c2();
};

/**
* implements the client role protocol.
*/
class SrsRtmpClient
{
private:
    SrsHandshakeBytes* hs_bytes;
protected:
    SrsProtocol* protocol;
    ISrsProtocolReaderWriter* io;
public:
    SrsRtmpClient(ISrsProtocolReaderWriter* skt);
    virtual ~SrsRtmpClient();
// protocol methods proxy
public:
    /**
    * set the recv timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_recv_timeout(int64_t timeout_us);
    /**
    * set the send timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_send_timeout(int64_t timeout_us);
    /**
    * get recv/send bytes.
    */
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    /**
    * recv a RTMP message, which is bytes oriented.
    * user can use decode_message to get the decoded RTMP packet.
    * @param pmsg, set the received message, 
    *       always NULL if error, 
    *       NULL for unknown packet but return success.
    *       never NULL if decode success.
    * @remark, drop message when msg is empty or payload length is empty.
    */
    virtual int recv_message(SrsMessage** pmsg);
    /**
    * decode bytes oriented RTMP message to RTMP packet,
    * @param ppacket, output decoded packet, 
    *       always NULL if error, never NULL if success.
    * @return error when unknown packet, error when decode failed.
    */
    virtual int decode_message(SrsMessage* msg, SrsPacket** ppacket);
    /**
    * send the RTMP message and always free it.
    * user must never free or use the msg after this method,
    * for it will always free the msg.
    * @param msg, the msg to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_message(SrsMessage* msg, int stream_id);
    /**
    * send the RTMP packet and always free it.
    * user must never free or use the packet after this method,
    * for it will always free the packet.
    * @param packet, the packet to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    /**
    * handshake with server, try complex, then simple handshake.
    */
    virtual int handshake();
    /**
    * only use simple handshake
    */
    virtual int simple_handshake();
    /**
    * only use complex handshake
    */
    virtual int complex_handshake();
    /**
    * set req to use the original request of client:
    *      pageUrl and swfUrl for refer antisuck.
    *      args for edge to origin traverse auth, @see SrsRequest.args
    */
    virtual int connect_app(std::string app, std::string tc_url, 
        SrsRequest* req, bool debug_srs_upnode);
    /**
    * connect to server, get the debug srs info.
    * 
    * @param app, the app to connect at.
    * @param tc_url, the tcUrl to connect at.
    * @param req, the optional req object, use the swfUrl/pageUrl if specified. NULL to ignore.
    * 
    * SRS debug info:
    * @param srs_server_ip, debug info, server ip client connected at.
    * @param srs_server, server info.
    * @param srs_primary_authors, primary authors.
    * @param srs_id, int, debug info, client id in server log.
    * @param srs_pid, int, debug info, server pid in log.
    */
    virtual int connect_app2(
        std::string app, std::string tc_url, SrsRequest* req, bool debug_srs_upnode,
        std::string& srs_server_ip, std::string& srs_server, std::string& srs_primary_authors, 
        std::string& srs_version, int& srs_id, int& srs_pid
    );
    /**
    * create a stream, then play/publish data over this stream.
    */
    virtual int create_stream(int& stream_id);
    /**
    * start play stream.
    */
    virtual int play(std::string stream, int stream_id);
    /**
    * start publish stream. use flash publish workflow:
    *       connect-app => create-stream => flash-publish
    */
    virtual int publish(std::string stream, int stream_id);
    /**
    * start publish stream. use FMLE publish workflow:
    *       connect-app => FMLE publish
    */
    virtual int fmle_publish(std::string stream, int& stream_id);
public:
    /**
    * expect a specified message, drop others util got specified one.
    * @pmsg, user must free it. NULL if not success.
    * @ppacket, store in the pmsg, user must never free it. NULL if not success.
    * @remark, only when success, user can use and must free the pmsg/ppacket.
    * for example:
             SrsCommonMessage* msg = NULL;
            SrsConnectAppResPacket* pkt = NULL;
            if ((ret = srs_rtmp_expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
                return ret;
            }
            // use pkt
    * user should never recv message and convert it, use this method instead.
    * if need to set timeout, use set timeout of SrsProtocol.
    */
    template<class T>
    int expect_message(SrsMessage** pmsg, T** ppacket)
    {
        return protocol->expect_message<T>(pmsg, ppacket);
    }
};

/**
* the rtmp provices rtmp-command-protocol services,
* a high level protocol, media stream oriented services,
* such as connect to vhost/app, play stream, get audio/video data.
*/
class SrsRtmpServer
{
private:
    SrsHandshakeBytes* hs_bytes;
    SrsProtocol* protocol;
    ISrsProtocolReaderWriter* io;
public:
    SrsRtmpServer(ISrsProtocolReaderWriter* skt);
    virtual ~SrsRtmpServer();
// protocol methods proxy
public:
    /**
    * set/get the recv timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    /**
    * set/get the send timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    /**
    * get recv/send bytes.
    */
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    /**
    * recv a RTMP message, which is bytes oriented.
    * user can use decode_message to get the decoded RTMP packet.
    * @param pmsg, set the received message, 
    *       always NULL if error, 
    *       NULL for unknown packet but return success.
    *       never NULL if decode success.
    * @remark, drop message when msg is empty or payload length is empty.
    */
    virtual int recv_message(SrsMessage** pmsg);
    /**
    * decode bytes oriented RTMP message to RTMP packet,
    * @param ppacket, output decoded packet, 
    *       always NULL if error, never NULL if success.
    * @return error when unknown packet, error when decode failed.
    */
    virtual int decode_message(SrsMessage* msg, SrsPacket** ppacket);
    /**
    * send the RTMP message and always free it.
    * user must never free or use the msg after this method,
    * for it will always free the msg.
    * @param msg, the msg to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_message(SrsMessage* msg, int stream_id);
    /**
    * send the RTMP message and always free it.
    * user must never free or use the msg after this method,
    * for it will always free the msg.
    * @param msgs, the msgs to send out, never be NULL.
    * @param nb_msgs, the size of msgs to send out.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    *
    * @remark performance issue, to support 6k+ 250kbps client,
    *       @see https://github.com/winlinvip/simple-rtmp-server/issues/194
    */
    virtual int send_and_free_messages(SrsMessage** msgs, int nb_msgs, int stream_id);
    /**
    * send the RTMP packet and always free it.
    * user must never free or use the packet after this method,
    * for it will always free the packet.
    * @param packet, the packet to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    /**
    * handshake with client, try complex then simple.
    */
    virtual int handshake();
    /**
    * do connect app with client, to discovery tcUrl.
    */
    virtual int connect_app(SrsRequest* req);
    /**
    * set ack size to client, client will send ack-size for each ack window
    */
    virtual int set_window_ack_size(int ack_size);
    /**
    * @type: The sender can mark this message hard (0), soft (1), or dynamic (2)
    * using the Limit type field.
    */
    virtual int set_peer_bandwidth(int bandwidth, int type);
    /**
    * @param server_ip the ip of server.
    */
    virtual int response_connect_app(SrsRequest* req, const char* server_ip = NULL);
    /**
    * reject the connect app request.
    */
    virtual void response_connect_reject(SrsRequest* req, const char* desc);
    /**
    * response client the onBWDone message.
    */
    virtual int on_bw_done();
    /**
    * recv some message to identify the client.
    * @stream_id, client will createStream to play or publish by flash, 
    *         the stream_id used to response the createStream request.
    * @type, output the client type.
    * @stream_name, output the client publish/play stream name. @see: SrsRequest.stream
    * @duration, output the play client duration. @see: SrsRequest.duration
    */
    virtual int identify_client(int stream_id, SrsRtmpConnType& type, std::string& stream_name, double& duration);
    /**
    * set the chunk size when client type identified.
    */
    virtual int set_chunk_size(int chunk_size);
    /**
    * when client type is play, response with packets:
    * StreamBegin, 
    * onStatus(NetStream.Play.Reset), onStatus(NetStream.Play.Start).,
    * |RtmpSampleAccess(false, false),
    * onStatus(NetStream.Data.Start).
    */
    virtual int start_play(int stream_id);
    /**
    * when client(type is play) send pause message,
    * if is_pause, response the following packets:
    *     onStatus(NetStream.Pause.Notify)
    *     StreamEOF
    * if not is_pause, response the following packets:
    *     onStatus(NetStream.Unpause.Notify)
    *     StreamBegin
    */
    virtual int on_play_client_pause(int stream_id, bool is_pause);
    /**
    * when client type is publish, response with packets:
    * releaseStream response
    * FCPublish
    * FCPublish response
    * createStream response
    * onFCPublish(NetStream.Publish.Start)
    * onStatus(NetStream.Publish.Start)
    */
    virtual int start_fmle_publish(int stream_id);
    /**
    * process the FMLE unpublish event.
    * @unpublish_tid the unpublish request transaction id.
    */
    virtual int fmle_unpublish(int stream_id, double unpublish_tid);
    /**
    * when client type is publish, response with packets:
    * onStatus(NetStream.Publish.Start)
    */
    virtual int start_flash_publish(int stream_id);
public:
    /**
    * expect a specified message, drop others util got specified one.
    * @pmsg, user must free it. NULL if not success.
    * @ppacket, store in the pmsg, user must never free it. NULL if not success.
    * @remark, only when success, user can use and must free the pmsg/ppacket.
    * for example:
             SrsCommonMessage* msg = NULL;
            SrsConnectAppResPacket* pkt = NULL;
            if ((ret = srs_rtmp_expect_message<SrsConnectAppResPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
                return ret;
            }
            // use pkt
    * user should never recv message and convert it, use this method instead.
    * if need to set timeout, use set timeout of SrsProtocol.
    */
    template<class T>
    int expect_message(SrsMessage** pmsg, T** ppacket)
    {
        return protocol->expect_message<T>(pmsg, ppacket);
    }
private:
    virtual int identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsRtmpConnType& type, std::string& stream_name, double& duration);
    virtual int identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsRtmpConnType& type, std::string& stream_name);
    virtual int identify_flash_publish_client(SrsPublishPacket* req, SrsRtmpConnType& type, std::string& stream_name);
private:
    virtual int identify_play_client(SrsPlayPacket* req, SrsRtmpConnType& type, std::string& stream_name, double& duration);
};

#endif

