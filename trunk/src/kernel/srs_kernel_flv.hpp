//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_FLV_HPP
#define SRS_KERNEL_FLV_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

class SrsBuffer;
class ISrsWriter;
class ISrsReader;
class SrsFileReader;
class SrsPacket;
class SrsSample;

#define SRS_FLV_TAG_HEADER_SIZE 11
#define SRS_FLV_PREVIOUS_TAG_SIZE 4
// 5. Protocol Control Messages
// RTMP reserves message type IDs 1-7 for protocol control messages.
// These messages contain information needed by the RTM Chunk Stream
// protocol or RTMP itself. Protocol messages with IDs 1 & 2 are
// reserved for usage with RTM Chunk Stream protocol. Protocol messages
// with IDs 3-6 are reserved for usage of RTMP. Protocol message with ID
// 7 is used between edge server and origin server.
#define RTMP_MSG_SetChunkSize                   0x01
#define RTMP_MSG_AbortMessage                   0x02
#define RTMP_MSG_Acknowledgement                0x03
#define RTMP_MSG_UserControlMessage             0x04
#define RTMP_MSG_WindowAcknowledgementSize      0x05
#define RTMP_MSG_SetPeerBandwidth               0x06
#define RTMP_MSG_EdgeAndOriginServerCommand     0x07
// 3. Types of messages
// The server and the client send messages over the network to
// communicate with each other. The messages can be of any type which
// includes audio messages, video messages, command messages, shared
// object messages, data messages, and user control messages.
// 3.1. Command message
// Command messages carry the AMF-encoded commands between the client
// and the server. These messages have been assigned message type value
// of 20 for AMF0 encoding and message type value of 17 for AMF3
// encoding. These messages are sent to perform some operations like
// connect, createStream, publish, play, pause on the peer. Command
// messages like onstatus, result etc. are used to inform the sender
// about the status of the requested commands. A command message
// consists of command name, transaction ID, and command object that
// contains related parameters. A client or a server can request Remote
// Procedure Calls (RPC) over streams that are communicated using the
// command messages to the peer.
#define RTMP_MSG_AMF3CommandMessage             17 // 0x11
#define RTMP_MSG_AMF0CommandMessage             20 // 0x14
// 3.2. Data message
// The client or the server sends this message to send Metadata or any
// user data to the peer. Metadata includes details about the
// data(audio, video etc.) like creation time, duration, theme and so
// on. These messages have been assigned message type value of 18 for
// AMF0 and message type value of 15 for AMF3.
#define RTMP_MSG_AMF0DataMessage                18 // 0x12
#define RTMP_MSG_AMF3DataMessage                15 // 0x0F
// 3.3. Shared object message
// A shared object is a Flash object (a collection of name value pairs)
// that are in synchronization across multiple clients, instances, and
// so on. The message types kMsgContainer=19 for AMF0 and
// kMsgContainerEx=16 for AMF3 are reserved for shared object events.
// Each message can contain multiple events.
#define RTMP_MSG_AMF3SharedObject               16 // 0x10
#define RTMP_MSG_AMF0SharedObject               19 // 0x13
// 3.4. Audio message
// The client or the server sends this message to send audio data to the
// peer. The message type value of 8 is reserved for audio messages.
#define RTMP_MSG_AudioMessage                   8 // 0x08
// 3.5. Video message
// The client or the server sends this message to send video data to the
// peer. The message type value of 9 is reserved for video messages.
// These messages are large and can delay the sending of other type of
// messages. To avoid such a situation, the video message is assigned
// The lowest priority.
#define RTMP_MSG_VideoMessage                   9 // 0x09
// 3.6. Aggregate message
// An aggregate message is a single message that contains a list of submessages.
// The message type value of 22 is reserved for aggregate
// messages.
#define RTMP_MSG_AggregateMessage               22 // 0x16
// The chunk stream id used for some under-layer message,
// For example, the PC(protocol control) message.
#define RTMP_CID_ProtocolControl                0x02
// The AMF0/AMF3 command message, invoke method and return the result, over NetConnection.
// generally use 0x03.
#define RTMP_CID_OverConnection                 0x03
// The AMF0/AMF3 command message, invoke method and return the result, over NetConnection,
// The midst state(we guess).
// rarely used, e.g. onStatus(NetStream.Play.Reset).
#define RTMP_CID_OverConnection2                0x04
// The stream message(amf0/amf3), over NetStream.
// generally use 0x05.
#define RTMP_CID_OverStream                     0x05
// The stream message(amf0/amf3), over NetStream, the midst state(we guess).
// rarely used, e.g. play("mp4:mystram.f4v")
#define RTMP_CID_OverStream2                    0x08
// The stream message(video), over NetStream
// generally use 0x06.
#define RTMP_CID_Video                          0x06
// The stream message(audio), over NetStream.
// generally use 0x07.
#define RTMP_CID_Audio                          0x07

// 6.1. Chunk Format
// Extended timestamp: 0 or 4 bytes
// This field MUST be sent when the normal timsestamp is set to
// 0xffffff, it MUST NOT be sent if the normal timestamp is set to
// anything else. So for values less than 0xffffff the normal
// timestamp field SHOULD be used in which case the extended timestamp
// MUST NOT be present. For values greater than or equal to 0xffffff
// The normal timestamp field MUST NOT be used and MUST be set to
// 0xffffff and the extended timestamp MUST be sent.
#define RTMP_EXTENDED_TIMESTAMP                 0xFFFFFF

// 4.1. Message Header
class SrsMessageHeader
{
public:
    // 3bytes.
    // Three-byte field that contains a timestamp delta of the message.
    // @remark, only used for decoding message from chunk stream.
    int32_t timestamp_delta;
    // 3bytes.
    // Three-byte field that represents the size of the payload in bytes.
    // It is set in big-endian format.
    int32_t payload_length;
    // 1byte.
    // One byte field to represent the message type. A range of type IDs
    // (1-7) are reserved for protocol control messages.
    // For example, RTMP_MSG_AudioMessage or RTMP_MSG_VideoMessage.
    int8_t message_type;
    // 4bytes.
    // Four-byte field that identifies the stream of the message. These
    // bytes are set in little-endian format.
    int32_t stream_id;
    
    // Four-byte field that contains a timestamp of the message.
    // The 4 bytes are packed in the big-endian order.
    // @remark, used as calc timestamp when decode and encode time.
    // @remark, we use 64bits for large time for jitter detect and hls.
    int64_t timestamp;
public:
    // Get the prefered cid(chunk stream id) which sendout over.
    // set at decoding, and canbe used for directly send message,
    // For example, dispatch to all connections.
    int prefer_cid;
public:
    SrsMessageHeader();
    virtual ~SrsMessageHeader();
public:
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
    bool is_set_peer_bandwidth();
    bool is_aggregate();
public:
    // Create a amf0 script header, set the size and stream_id.
    void initialize_amf0_script(int size, int stream);
    // Create a audio header, set the size, timestamp and stream_id.
    void initialize_audio(int size, uint32_t time, int stream);
    // Create a video header, set the size, timestamp and stream_id.
    void initialize_video(int size, uint32_t time, int stream);
};

// The message is raw data RTMP message, bytes oriented,
// protcol always recv RTMP message, and can send RTMP message or RTMP packet.
// The common message is read from underlay protocol sdk.
// while the shared ptr message used to copy and send.
class SrsCommonMessage
{
// 4.1. Message Header
public:
    SrsMessageHeader header;
// 4.2. Message Payload
public:
    // The current message parsed size,
    //       size <= header.payload_length
    // For the payload maybe sent in multiple chunks.
    int size;
    // The payload of message, the SrsCommonMessage never know about the detail of payload,
    // user must use SrsProtocol.decode_message to get concrete packet.
    // @remark, not all message payload can be decoded to packet. for example,
    //       video/audio packet use raw bytes, no video/audio packet.
    char* payload;
public:
    SrsCommonMessage();
    virtual ~SrsCommonMessage();
public:
    // Alloc the payload to specified size of bytes.
    virtual void create_payload(int size);
public:
    // Create common message,
    // from the header and body.
    // @remark user should never free the body.
    // @param pheader, the header to copy to the message. NULL to ignore.
    virtual srs_error_t create(SrsMessageHeader* pheader, char* body, int size);
};

// The message header for shared ptr message.
// only the message for all msgs are same.
class SrsSharedMessageHeader
{
public:
    // 3bytes.
    // Three-byte field that represents the size of the payload in bytes.
    // It is set in big-endian format.
    int32_t payload_length;
    // 1byte.
    // One byte field to represent the message type. A range of type IDs
    // (1-7) are reserved for protocol control messages.
    // For example, RTMP_MSG_AudioMessage or RTMP_MSG_VideoMessage.
    int8_t message_type;
    // Get the prefered cid(chunk stream id) which sendout over.
    // set at decoding, and canbe used for directly send message,
    // For example, dispatch to all connections.
    int prefer_cid;
public:
    SrsSharedMessageHeader();
    virtual ~SrsSharedMessageHeader();
};

// The shared ptr message.
// For audio/video/data message that need less memory copy.
// and only for output.
//
// Create first object by constructor and create(),
// use copy if need reference count message.
class SrsSharedPtrMessage
{
// 4.1. Message Header
public:
    // The header can shared, only set the timestamp and stream id.
    //SrsSharedMessageHeader header;
    // Four-byte field that contains a timestamp of the message.
    // The 4 bytes are packed in the big-endian order.
    // @remark, used as calc timestamp when decode and encode time.
    // @remark, we use 64bits for large time for jitter detect and hls.
    int64_t timestamp;
    // 4bytes.
    // Four-byte field that identifies the stream of the message. These
    // bytes are set in big-endian format.
    int32_t stream_id;
    // 4.2. Message Payload
public:
    // The current message parsed size,
    //       size <= header.payload_length
    // For the payload maybe sent in multiple chunks.
    int size;
    // The payload of message, the SrsCommonMessage never know about the detail of payload,
    // user must use SrsProtocol.decode_message to get concrete packet.
    // @remark, not all message payload can be decoded to packet. for example,
    //       video/audio packet use raw bytes, no video/audio packet.
    char* payload;

private:
    class SrsSharedPtrPayload
    {
    public:
        // The shared message header.
        SrsSharedMessageHeader header;
        // The actual shared payload.
        char* payload;
        // The size of payload.
        int size;
        // The reference count
        int shared_count;
    public:
        SrsSharedPtrPayload();
        virtual ~SrsSharedPtrPayload();
    };
    SrsSharedPtrPayload* ptr;
public:
    SrsSharedPtrMessage();
    virtual ~SrsSharedPtrMessage();
public:
    // Create shared ptr message,
    // copy header, manage the payload of msg,
    // set the payload to NULL to prevent double free.
    // @remark payload of msg set to NULL if success.
    // @remark User should free the msg.
    virtual srs_error_t create(SrsCommonMessage* msg);
    // Create shared ptr message,
    // from the header and payload.
    // @remark user should never free the payload.
    // @param pheader, the header to copy to the message. NULL to ignore.
    virtual srs_error_t create(SrsMessageHeader* pheader, char* payload, int size);
    // Create shared ptr message from RAW payload.
    // @remark Note that the header is set to zero.
    virtual void wrap(char* payload, int size);
    // Get current reference count.
    // when this object created, count set to 0.
    // if copy() this object, count increase 1.
    // if this or copy deleted, free payload when count is 0, or count--.
    // @remark, assert object is created.
    virtual int count();
    // check prefer cid and stream id.
    // @return whether stream id already set.
    virtual bool check(int stream_id);
public:
    virtual bool is_av();
    virtual bool is_audio();
    virtual bool is_video();
public:
    // generate the chunk header to cache.
    // @return the size of header.
    virtual int chunk_header(char* cache, int nb_cache, bool c0);
public:
    // copy current shared ptr message, use ref-count.
    // @remark, assert object is created.
    virtual SrsSharedPtrMessage* copy();
    // Only copy the buffer, without header fields.
    virtual SrsSharedPtrMessage* copy2();
};

// Transmux RTMP packets to FLV stream.
class SrsFlvTransmuxer
{
private:
    bool has_audio_;
    bool has_video_;
    bool drop_if_not_match_;
    ISrsWriter* writer;
private:
    char tag_header[SRS_FLV_TAG_HEADER_SIZE];
public:
    SrsFlvTransmuxer();
    virtual ~SrsFlvTransmuxer();
public:
    // Initialize the underlayer file stream.
    // @remark user can initialize multiple times to encode multiple flv files.
    // @remark, user must free the @param fw, flv encoder never close/free it.
    virtual srs_error_t initialize(ISrsWriter* fw);
    // Drop packet if not match FLV header.
    void set_drop_if_not_match(bool v);
    bool drop_if_not_match();
public:
    // Write flv header.
    // Write following:
    //   1. E.2 The FLV header
    //   2. PreviousTagSize0 UI32 Always 0
    // that is, 9+4=13bytes.
    virtual srs_error_t write_header(bool has_video = true, bool has_audio = true);
    virtual srs_error_t write_header(char flv_header[9]);
    // Write flv metadata.
    // @param type, the type of data, or other message type.
    //       @see SrsFrameType
    // @param data, the amf0 metadata which serialize from:
    //   AMF0 string: onMetaData,
    //   AMF0 object: the metadata object.
    // @remark assert data is not NULL.
    virtual srs_error_t write_metadata(char type, char* data, int size);
    // Write audio/video packet.
    // @remark assert data is not NULL.
    virtual srs_error_t write_audio(int64_t timestamp, char* data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char* data, int size);
public:
    // Get the tag size,
    // including the tag header, body, and 4bytes previous tag size.
    // @remark assert data_size is not negative.
    static int size_tag(int data_size);
private:
    // The cache tag header.
    int nb_tag_headers;
    char* tag_headers;
    // The cache pps(previous tag size)
    int nb_ppts;
    char* ppts;
    // The cache iovss.
    int nb_iovss_cache;
    iovec* iovss_cache;
public:
    // Write the tags in a time.
    virtual srs_error_t write_tags(SrsSharedPtrMessage** msgs, int count);
private:
    virtual void cache_metadata(char type, char* data, int size, char* cache);
    virtual void cache_audio(int64_t timestamp, char* data, int size, char* cache);
    virtual void cache_video(int64_t timestamp, char* data, int size, char* cache);
    virtual void cache_pts(int size, char* cache);
    virtual srs_error_t write_tag(char* header, int header_size, char* tag, int tag_size);
};

// Decode flv file.
class SrsFlvDecoder
{
private:
    ISrsReader* reader;
public:
    SrsFlvDecoder();
    virtual ~SrsFlvDecoder();
public:
    // Initialize the underlayer file stream
    // @remark user can initialize multiple times to decode multiple flv files.
    // @remark user must free the @param fr, flv decoder never close/free it
    virtual srs_error_t initialize(ISrsReader* fr);
public:
    // Read the flv header, donot including the 4bytes previous tag size.
    // @remark assert header not NULL.
    virtual srs_error_t read_header(char header[9]);
    // Read the tag header infos.
    // @remark assert ptype/pdata_size/ptime not NULL.
    virtual srs_error_t read_tag_header(char* ptype, int32_t* pdata_size, uint32_t* ptime);
    // Read the tag data.
    // @remark assert data not NULL.
    virtual srs_error_t read_tag_data(char* data, int32_t size);
    // Read the 4bytes previous tag size.
    // @remark assert previous_tag_size not NULL.
    virtual srs_error_t read_previous_tag_size(char previous_tag_size[4]);
};

// Decode flv fast by only decoding the header and tag.
// used for vod flv stream to read the header and sequence header,
// then seek to specified offset.
class SrsFlvVodStreamDecoder
{
private:
    SrsFileReader* reader;
public:
    SrsFlvVodStreamDecoder();
    virtual ~SrsFlvVodStreamDecoder();
public:
    // Initialize the underlayer file stream
    // @remark user can initialize multiple times to decode multiple flv files.
    // @remark user must free the @param fr, flv decoder never close/free it.
    virtual srs_error_t initialize(ISrsReader* fr);
public:
    // Read the flv header and its size.
    // @param header, fill it 13bytes(9bytes header, 4bytes previous tag size).
    // @remark assert header not NULL.
    virtual srs_error_t read_header_ext(char header[13]);
    // Read the sequence header tags offset and its size.
    // @param pstart, the start offset of sequence header.
    // @param psize, output the size, (tag header)+(tag body)+(4bytes previous tag size).
    // @remark we think the first audio/video is sequence header.
    // @remark assert pstart/psize not NULL.
    virtual srs_error_t read_sequence_header_summary(int64_t* pstart, int* psize);
public:
    // For start offset, seed to this position and response flv stream.
    virtual srs_error_t seek2(int64_t offset);
};

#endif

