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

#ifndef SRS_RTMP_PROTOCOL_RTMP_STACK_HPP
#define SRS_RTMP_PROTOCOL_RTMP_STACK_HPP

/*
#include <srs_protocol_rtmp_stack.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <string>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>

class ISrsProtocolReaderWriter;
class SrsBuffer;
class SrsPacket;
class SrsStream;
class SrsAmf0Object;
class SrsAmf0Any;
class SrsMessageHeader;
class SrsMessage;
class SrsChunkStream;
 
// the following is the timeout for rtmp protocol, 
// to avoid death connection.

// the timeout to wait client data,
// if timeout, close the connection.
#define SRS_SEND_TIMEOUT_US (int64_t)(30*1000*1000LL)

// the timeout to send data to client,
// if timeout, close the connection.
#define SRS_RECV_TIMEOUT_US (int64_t)(30*1000*1000LL)

// the timeout to wait for client control message,
// if timeout, we generally ignore and send the data to client,
// generally, it's the pulse time for data seding.
#define SRS_PULSE_TIMEOUT_US (int64_t)(200*1000LL)

// convert class name to string.
#define CLASS_NAME_STRING(className) #className

/**
* max rtmp header size:
*     1bytes basic header,
*     11bytes message header,
*     4bytes timestamp header,
* that is, 1+11+4=16bytes.
*/
#define RTMP_MAX_FMT0_HEADER_SIZE 16
/**
* max rtmp header size:
*     1bytes basic header,
*     4bytes timestamp header,
* that is, 1+4=5bytes.
*/
// always use fmt0 as cache.
//#define RTMP_MAX_FMT3_HEADER_SIZE 5

/**
* the protocol provides the rtmp-message-protocol services,
* to recv RTMP message from RTMP chunk stream,
* and to send out RTMP message over RTMP chunk stream.
*/
class SrsProtocol
{
private:
    struct AckWindowSize
    {
        int ack_window_size;
        int64_t acked_size;
        
        AckWindowSize();
    };
// peer in/out
private:
    ISrsProtocolReaderWriter* skt;
    char* pp;
    /**
    * requests sent out, used to build the response.
    * key: transactionId
    * value: the request command name
    */
    std::map<double, std::string> requests;
// peer in
private:
    std::map<int, SrsChunkStream*> chunk_streams;
    SrsStream* decode_stream;
    SrsBuffer* buffer;
    int32_t in_chunk_size;
    AckWindowSize in_ack_size;
// peer out
private:
    char out_header_cache[RTMP_MAX_FMT0_HEADER_SIZE];
    int32_t out_chunk_size;
public:
    /**
    * use io to create the protocol stack,
    * @param io, provides io interfaces, user must free it.
    */
    SrsProtocol(ISrsProtocolReaderWriter* io);
    virtual ~SrsProtocol();
public:
    // TODO: FIXME: to private.
    std::string get_request_name(double transcationId);
    /**
    * set the timeout in us.
    * if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
    */
    virtual void set_recv_timeout(int64_t timeout_us);
    virtual int64_t get_recv_timeout();
    virtual void set_send_timeout(int64_t timeout_us);
    virtual int64_t get_send_timeout();
    virtual int64_t get_recv_bytes();
    virtual int64_t get_send_bytes();
    virtual int get_recv_kbps();
    virtual int get_send_kbps();
public:
    /**
    * recv a RTMP message, which is bytes oriented.
    * user can use decode_message to get the decoded RTMP packet.
    * @param pmsg, set the received message, 
    *       always NULL if error, 
    *       NULL for unknown packet but return success.
    *       never NULL if decode success.
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
    */
    virtual int send_and_free_message(SrsMessage* msg);
    /**
    * send the RTMP packet and always free it.
    * user must never free or use the packet after this method,
    * for it will always free the packet.
    * @param packet, the packet to send out, never be NULL.
    * @param stream_id, the stream id of packet to send over, 0 for control message.
    */
    virtual int send_and_free_packet(SrsPacket* packet, int stream_id);
private:
    /**
    * imp for send_and_free_message
    * @param packet the packet of message, NULL for raw message.
    */
    virtual int do_send_and_free_message(SrsMessage* msg, SrsPacket* packet);
    /**
    * imp for decode_message
    */
    virtual int do_decode_message(SrsMessageHeader& header, SrsStream* stream, SrsPacket** ppacket);
    /**
    * recv bytes oriented RTMP message from protocol stack.
    * return error if error occur and nerver set the pmsg,
    * return success and pmsg set to NULL if no entire message got,
    * return success and pmsg set to entire message if got one.
    */
    virtual int recv_interlaced_message(SrsMessage** pmsg);
    /**
    * read the chunk basic header(fmt, cid) from chunk stream.
    * user can discovery a SrsChunkStream by cid.
    * @bh_size return the chunk basic header size, to remove the used bytes when finished.
    */
    virtual int read_basic_header(char& fmt, int& cid, int& bh_size);
    /**
    * read the chunk message header(timestamp, payload_length, message_type, stream_id) 
    * from chunk stream and save to SrsChunkStream.
    * @mh_size return the chunk message header size, to remove the used bytes when finished.
    */
    virtual int read_message_header(SrsChunkStream* chunk, char fmt, int bh_size, int& mh_size);
    /**
    * read the chunk payload, remove the used bytes in buffer,
    * if got entire message, set the pmsg.
    * @payload_size read size in this roundtrip, generally a chunk size or left message size.
    */
    virtual int read_message_payload(SrsChunkStream* chunk, int bh_size, int mh_size, int& payload_size, SrsMessage** pmsg);
    /**
    * when recv message, update the context.
    */
    virtual int on_recv_message(SrsMessage* msg);
    /**
    * when message sentout, update the context.
    */
    virtual int on_send_message(SrsMessage* msg, SrsPacket* packet);
private:
    virtual int response_acknowledgement_message();
    virtual int response_ping_message(int32_t timestamp);
};

/**
* 4.1. Message Header
*/
struct SrsMessageHeader
{
    /**
    * One byte field to represent the message type. A range of type IDs
    * (1-7) are reserved for protocol control messages.
    */
    int8_t message_type;
    /**
    * Three-byte field that represents the size of the payload in bytes.
    * It is set in big-endian format.
    */
    int32_t payload_length;
    /**
    * Three-byte field that contains a timestamp delta of the message.
    * The 4 bytes are packed in the big-endian order.
    * @remark, only used for decoding message from chunk stream.
    */
    int32_t timestamp_delta;
    /**
    * Three-byte field that identifies the stream of the message. These
    * bytes are set in big-endian format.
    */
    int32_t stream_id;
    
    /**
    * Four-byte field that contains a timestamp of the message.
    * The 4 bytes are packed in the big-endian order.
    * @remark, used as calc timestamp when decode and encode time.
    * @remark, we use 64bits for large time for jitter detect and hls.
    */
    int64_t timestamp;
    
public:
    /**
    * get the perfered cid(chunk stream id) which sendout over.
    * set at decoding, and canbe used for directly send message,
    * for example, dispatch to all connections.
    */
    int perfer_cid;
    
public:
    SrsMessageHeader();
    virtual ~SrsMessageHeader();

    bool is_audio();
    bool is_video();
    bool is_amf0_command();
    bool is_amf0_data();
    bool is_amf3_command();
    bool is_amf3_data();
    bool is_window_ackledgement_size();
    bool is_ackledgement();
    bool is_set_chunk_size();
    bool is_user_control_message();
    
    void initialize_amf0_script(int size, int stream);
    void initialize_audio(int size, u_int32_t time, int stream);
    void initialize_video(int size, u_int32_t time, int stream);
};

/**
* incoming chunk stream maybe interlaced,
* use the chunk stream to cache the input RTMP chunk streams.
*/
class SrsChunkStream
{
public:
    /**
    * represents the basic header fmt,
    * which used to identify the variant message header type.
    */
    char fmt;
    /**
    * represents the basic header cid,
    * which is the chunk stream id.
    */
    int cid;
    /**
    * cached message header
    */
    SrsMessageHeader header;
    /**
    * whether the chunk message header has extended timestamp.
    */
    bool extended_timestamp;
    /**
    * partially read message.
    */
    SrsMessage* msg;
    /**
    * decoded msg count, to identify whether the chunk stream is fresh.
    */
    int64_t msg_count;
public:
    SrsChunkStream(int _cid);
    virtual ~SrsChunkStream();
};

/**
* message is raw data RTMP message, bytes oriented,
* protcol always recv RTMP message, and can send RTMP message or RTMP packet.
* the shared-ptr message is a special RTMP message, use ref-count for performance issue.
* 
* @remark, never directly new SrsMessage, the constructor is protected,
* for in the SrsMessage, we never know whether we should free the message,
* for SrsCommonMessage, we should free the payload,
* while for SrsSharedPtrMessage, we should use ref-count to free it.
* so, use these two concrete message, SrsCommonMessage or SrsSharedPtrMessage instread.
*/
class SrsMessage
{
// 4.1. Message Header
public:
    SrsMessageHeader header;
// 4.2. Message Payload
public:
    /**
    * The other part which is the payload is the actual data that is
    * contained in the message. For example, it could be some audio samples
    * or compressed video data. The payload format and interpretation are
    * beyond the scope of this document.
    */
    int32_t size;
    int8_t* payload;
protected:
    SrsMessage();
public:
    virtual ~SrsMessage();
};

/**
* the common message used free the payload in common way.
*/
class SrsCommonMessage : public SrsMessage
{
public:
    SrsCommonMessage();
    virtual ~SrsCommonMessage();
};

/**
* shared ptr message.
* for audio/video/data message that need less memory copy.
* and only for output.
*/
class SrsSharedPtrMessage : public SrsMessage
{
private:
    struct __SrsSharedPtr
    {
        char* payload;
        int size;
        int shared_count;
        
        __SrsSharedPtr();
        virtual ~__SrsSharedPtr();
    };
    __SrsSharedPtr* ptr;
public:
    SrsSharedPtrMessage();
    virtual ~SrsSharedPtrMessage();
public:
    /**
    * set the shared payload.
    * we will detach the payload of source,
    * so ensure donot use it before.
    */
    virtual int initialize(SrsMessage* source);
    /**
    * set the shared payload.
    * use source header, and specified param payload.
    */
    virtual int initialize(SrsMessageHeader* source, char* payload, int size);
public:
    /**
    * copy current shared ptr message, use ref-count.
    */
    virtual SrsSharedPtrMessage* copy();
};

/**
* the decoded message payload.
* @remark we seperate the packet from message,
*        for the packet focus on logic and domain data,
*        the message bind to the protocol and focus on protocol, such as header.
*         we can merge the message and packet, using OOAD hierachy, packet extends from message,
*         it's better for me to use components -- the message use the packet as payload.
*/
class SrsPacket
{
protected:
    /**
    * subpacket must override to provide the right class name.
    */
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsPacket);
    }
public:
    SrsPacket();
    virtual ~SrsPacket();
/**
* decode functions.
*/
public:
    /**
    * subpacket must override to decode packet from stream.
    * @remark never invoke the super.decode, it always failed.
    */
    virtual int decode(SrsStream* stream);
/**
* encode functions.
*/
public:
    virtual int get_perfer_cid();
public:
    /**
    * subpacket must override to provide the right message type.
    */
    virtual int get_message_type();
    /**
    * the subpacket can override this encode,
    * for example, video and audio will directly set the payload withou memory copy,
    * other packet which need to serialize/encode to bytes by override the 
    * get_size and encode_packet.
    */
    virtual int encode(int& size, char*& payload);
protected:
    /**
    * subpacket can override to calc the packet size.
    */
    virtual int get_size();
    /**
    * subpacket can override to encode the payload to stream.
    * @remark never invoke the super.encode_packet, it always failed.
    */
    virtual int encode_packet(SrsStream* stream);
};

/**
* 4.1.1. connect
* The client sends the connect command to the server to request
* connection to a server application instance.
*/
class SrsConnectAppPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsConnectAppPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    /**
    * alloc in packet constructor,
    * so, directly use it, never alloc again.
    */
    SrsAmf0Object* command_object;
public:
    SrsConnectAppPacket();
    virtual ~SrsConnectAppPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* response for SrsConnectAppPacket.
*/
class SrsConnectAppResPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsConnectAppResPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Object* props;
    SrsAmf0Object* info;
public:
    SrsConnectAppResPacket();
    virtual ~SrsConnectAppResPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 4.1.3. createStream
* The client sends this command to the server to create a logical
* channel for message communication The publishing of audio, video, and
* metadata is carried out over stream channel created using the
* createStream command.
*/
class SrsCreateStreamPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsCreateStreamPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
public:
    SrsCreateStreamPacket();
    virtual ~SrsCreateStreamPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* response for SrsCreateStreamPacket.
*/
class SrsCreateStreamResPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsCreateStreamResPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    double stream_id;
public:
    SrsCreateStreamResPacket(double _transaction_id, double _stream_id);
    virtual ~SrsCreateStreamResPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* client close stream packet.
*/
class SrsCloseStreamPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsCloseStreamPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
public:
    SrsCloseStreamPacket();
    virtual ~SrsCloseStreamPacket();
public:
    virtual int decode(SrsStream* stream);
};

/**
* FMLE start publish: ReleaseStream/PublishStream
*/
class SrsFMLEStartPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsFMLEStartPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    std::string stream_name;
public:
    SrsFMLEStartPacket();
    virtual ~SrsFMLEStartPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
public:
    static SrsFMLEStartPacket* create_release_stream(std::string stream);
    static SrsFMLEStartPacket* create_FC_publish(std::string stream);
};
/**
* response for SrsFMLEStartPacket.
*/
class SrsFMLEStartResPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsFMLEStartResPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    SrsAmf0Any* args; // undefined
public:
    SrsFMLEStartResPacket(double _transaction_id);
    virtual ~SrsFMLEStartResPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* FMLE/flash publish
* 4.2.6. Publish
* The client sends the publish command to publish a named stream to the
* server. Using this name, any client can play this stream and receive
* the published audio, video, and data messages.
*/
class SrsPublishPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsPublishPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    std::string stream_name;
    // optional, default to live.
    std::string type;
public:
    SrsPublishPacket();
    virtual ~SrsPublishPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 4.2.8. pause
* The client sends the pause command to tell the server to pause or
* start playing.
*/
class SrsPausePacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsPausePacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    bool is_pause;
    double time_ms;
public:
    SrsPausePacket();
    virtual ~SrsPausePacket();
public:
    virtual int decode(SrsStream* stream);
};

/**
* 4.2.1. play
* The client sends this command to the server to play a stream.
*/
class SrsPlayPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsPlayPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    std::string stream_name;
    double start;
    double duration;
    bool reset;
public:
    SrsPlayPacket();
    virtual ~SrsPlayPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};
/**
* response for SrsPlayPacket.
* @remark, user must set the stream_id in header.
*/
class SrsPlayResPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsPlayResPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* command_object; // null
    SrsAmf0Object* desc;
public:
    SrsPlayResPacket();
    virtual ~SrsPlayResPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* when bandwidth test done, notice client.
*/
class SrsOnBWDonePacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsOnBWDonePacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* args; // null
public:
    SrsOnBWDonePacket();
    virtual ~SrsOnBWDonePacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* onStatus command, AMF0 Call
* @remark, user must set the stream_id by SrsMessage.set_packet().
*/
class SrsOnStatusCallPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsOnStatusCallPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* args; // null
    SrsAmf0Object* data;
public:
    SrsOnStatusCallPacket();
    virtual ~SrsOnStatusCallPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* the special packet for the bandwidth test.
* actually, it's a SrsOnStatusCallPacket, but
* 1. encode with data field, to send data to client.
* 2. decode ignore the data field, donot care.
*/
class SrsBandwidthPacket : public SrsPacket
{
private:
    disable_default_copy(SrsBandwidthPacket);
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsBandwidthPacket);
    }
public:
    std::string command_name;
    double transaction_id;
    SrsAmf0Any* args; // null
    SrsAmf0Object* data;
public:
    SrsBandwidthPacket();
    virtual ~SrsBandwidthPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
public:
    virtual int decode(SrsStream* stream);
public:
    virtual bool is_starting_play();
    virtual bool is_stopped_play();
    virtual bool is_starting_publish();
    virtual bool is_stopped_publish();
    virtual bool is_flash_final();
    static SrsBandwidthPacket* create_finish();
    static SrsBandwidthPacket* create_start_play();
    static SrsBandwidthPacket* create_playing();
    static SrsBandwidthPacket* create_stop_play();
    static SrsBandwidthPacket* create_start_publish();
    static SrsBandwidthPacket* create_stop_publish();
private:
    virtual SrsBandwidthPacket* set_command(std::string command);
};

/**
* onStatus data, AMF0 Data
* @remark, user must set the stream_id by SrsMessage.set_packet().
*/
class SrsOnStatusDataPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsOnStatusDataPacket);
    }
public:
    std::string command_name;
    SrsAmf0Object* data;
public:
    SrsOnStatusDataPacket();
    virtual ~SrsOnStatusDataPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* AMF0Data RtmpSampleAccess
* @remark, user must set the stream_id by SrsMessage.set_packet().
*/
class SrsSampleAccessPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsSampleAccessPacket);
    }
public:
    std::string command_name;
    bool video_sample_access;
    bool audio_sample_access;
public:
    SrsSampleAccessPacket();
    virtual ~SrsSampleAccessPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* the stream metadata.
* FMLE: @setDataFrame
* others: onMetaData
*/
class SrsOnMetaDataPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsOnMetaDataPacket);
    }
public:
    std::string name;
    SrsAmf0Object* metadata;
public:
    SrsOnMetaDataPacket();
    virtual ~SrsOnMetaDataPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 5.5. Window Acknowledgement Size (5)
* The client or the server sends this message to inform the peer which
* window size to use when sending acknowledgment.
*/
class SrsSetWindowAckSizePacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsSetWindowAckSizePacket);
    }
public:
    int32_t ackowledgement_window_size;
public:
    SrsSetWindowAckSizePacket();
    virtual ~SrsSetWindowAckSizePacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 5.3. Acknowledgement (3)
* The client or the server sends the acknowledgment to the peer after
* receiving bytes equal to the window size.
*/
class SrsAcknowledgementPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsAcknowledgementPacket);
    }
public:
    int32_t sequence_number;
public:
    SrsAcknowledgementPacket();
    virtual ~SrsAcknowledgementPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 7.1. Set Chunk Size
* Protocol control message 1, Set Chunk Size, is used to notify the
* peer about the new maximum chunk size.
*/
class SrsSetChunkSizePacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsSetChunkSizePacket);
    }
public:
    int32_t chunk_size;
public:
    SrsSetChunkSizePacket();
    virtual ~SrsSetChunkSizePacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

/**
* 5.6. Set Peer Bandwidth (6)
* The client or the server sends this message to update the output
* bandwidth of the peer.
*/
class SrsSetPeerBandwidthPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsSetPeerBandwidthPacket);
    }
public:
    int32_t bandwidth;
    int8_t type;
public:
    SrsSetPeerBandwidthPacket();
    virtual ~SrsSetPeerBandwidthPacket();
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

// 3.7. User Control message
enum SrcPCUCEventType
{
     // generally, 4bytes event-data
    SrcPCUCStreamBegin             = 0x00,
    SrcPCUCStreamEOF             = 0x01,
    SrcPCUCStreamDry             = 0x02,
    SrcPCUCSetBufferLength         = 0x03, // 8bytes event-data
    SrcPCUCStreamIsRecorded     = 0x04,
    SrcPCUCPingRequest             = 0x06,
    SrcPCUCPingResponse         = 0x07,
};

/**
* for the EventData is 4bytes.
* Stream Begin(=0)            4-bytes stream ID
* Stream EOF(=1)            4-bytes stream ID
* StreamDry(=2)                4-bytes stream ID
* SetBufferLength(=3)        8-bytes 4bytes stream ID, 4bytes buffer length.
* StreamIsRecorded(=4)        4-bytes stream ID
* PingRequest(=6)            4-bytes timestamp local server time
* PingResponse(=7)            4-bytes timestamp received ping request.
* 
* 3.7. User Control message
* +------------------------------+-------------------------
* | Event Type ( 2- bytes ) | Event Data
* +------------------------------+-------------------------
* Figure 5 Pay load for the ‘User Control Message’.
*/
class SrsUserControlPacket : public SrsPacket
{
protected:
    virtual const char* get_class_name()
    {
        return CLASS_NAME_STRING(SrsUserControlPacket);
    }
public:
    // @see: SrcPCUCEventType
    int16_t event_type;
    int32_t event_data;
    /**
    * 4bytes if event_type is SetBufferLength; otherwise 0.
    */
    int32_t extra_data;
public:
    SrsUserControlPacket();
    virtual ~SrsUserControlPacket();
public:
    virtual int decode(SrsStream* stream);
public:
    virtual int get_perfer_cid();
public:
    virtual int get_message_type();
protected:
    virtual int get_size();
    virtual int encode_packet(SrsStream* stream);
};

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
int srs_rtmp_expect_message(SrsProtocol* protocol, SrsMessage** pmsg, T** ppacket)
{
    *pmsg = NULL;
    *ppacket = NULL;
    
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsMessage* msg = NULL;
        if ((ret = protocol->recv_message(&msg)) != ERROR_SUCCESS) {
            srs_error("recv message failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("recv message success.");
        
        SrsPacket* packet = NULL;
        if ((ret = protocol->decode_message(msg, &packet)) != ERROR_SUCCESS) {
            srs_error("decode message failed. ret=%d", ret);
            srs_freep(msg);
            return ret;
        }
        
        T* pkt = dynamic_cast<T*>(packet);
        if (!pkt) {
            srs_trace("drop message(type=%d, size=%d, time=%"PRId64", sid=%d).", 
                msg->header.message_type, msg->header.payload_length,
                msg->header.timestamp, msg->header.stream_id);
            srs_freep(msg);
            continue;
        }
        
        *pmsg = msg;
        *ppacket = pkt;
        break;
    }
    
    return ret;
}

#endif
