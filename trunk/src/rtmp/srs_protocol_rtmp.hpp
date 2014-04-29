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
    
    std::string schema;
    std::string vhost;
    std::string host;
    std::string port;
    std::string app;
    std::string stream;
    
    // for play live stream, 
    // used to specified the stop when exceed the duration.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/45
    // in ms.
    double duration;
    
    SrsRequest();
    virtual ~SrsRequest();

    /**
    * deep copy the request, for source to use it to support reload,
    * for when initialize the source, the request is valid,
    * when reload it, the request maybe invalid, so need to copy it.
    */
    virtual SrsRequest* copy();
    
    /**
    * disconvery vhost/app from tcUrl.
    */
    virtual int discovery_app();
    virtual std::string get_stream_url();
    
    // strip url, user must strip when update the url.
    virtual void strip();
};

/**
* the response to client.
*/
struct SrsResponse
{
    int stream_id;
    
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
public:
    virtual SrsProtocol* get_protocol();
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual int get_recv_kbps();
    virtual int get_send_kbps();
    virtual int recv_message(SrsMessage** pmsg);
    virtual int decode_message(SrsMessage* msg, SrsPacket** ppacket);
    virtual int send_and_free_message(SrsMessage* msg);
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    // try complex, then simple handshake.
    virtual int handshake();
    // only use simple handshake
    virtual int simple_handshake();
    // only use complex handshake
    virtual int complex_handshake();
    virtual int connect_app(std::string app, std::string tc_url);
    virtual int create_stream(int& stream_id);
    virtual int play(std::string stream, int stream_id);
    // flash publish schema:
    // connect-app => create-stream => flash-publish
    virtual int publish(std::string stream, int stream_id);
    // FMLE publish schema:
    // connect-app => FMLE publish
    virtual int fmle_publish(std::string stream, int& stream_id);
};

/**
* the rtmp provices rtmp-command-protocol services,
* a high level protocol, media stream oriented services,
* such as connect to vhost/app, play stream, get audio/video data.
*/
// TODO: FIXME: rename to SrsRtmpServer
class SrsRtmpServer
{
private:
    SrsHandshakeBytes* hs_bytes;
    SrsProtocol* protocol;
    ISrsProtocolReaderWriter* io;
public:
    SrsRtmpServer(ISrsProtocolReaderWriter* skt);
    virtual ~SrsRtmpServer();
public:
    virtual SrsProtocol* get_protocol();
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual int get_recv_kbps();
    virtual int get_send_kbps();
    virtual int recv_message(SrsMessage** pmsg);
    virtual int decode_message(SrsMessage* msg, SrsPacket** ppacket);
    virtual int send_and_free_message(SrsMessage* msg);
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
public:
    virtual int handshake();
    virtual int connect_app(SrsRequest* req);
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
    virtual void response_connect_reject(SrsRequest* req, const char* desc);
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
private:
    virtual int identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsRtmpConnType& type, std::string& stream_name, double& duration);
    virtual int identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsRtmpConnType& type, std::string& stream_name);
    virtual int identify_flash_publish_client(SrsPublishPacket* req, SrsRtmpConnType& type, std::string& stream_name);
private:
    virtual int identify_play_client(SrsPlayPacket* req, SrsRtmpConnType& type, std::string& stream_name, double& duration);
};

#endif
