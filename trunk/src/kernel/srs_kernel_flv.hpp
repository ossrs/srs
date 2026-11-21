//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_FLV_HPP
#define SRS_KERNEL_FLV_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_packet.hpp>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

class SrsBuffer;
class ISrsWriter;
class ISrsReader;
class ISrsFileReader;
class SrsRtmpCommand;
class SrsNaluSample;

#define SRS_FLV_TAG_HEADER_SIZE 11
#define SRS_FLV_PREVIOUS_TAG_SIZE 4
// RTMP spec 5. Protocol Control Messages: IDs 1 and 2 are for the chunk stream protocol,
// 3 to 6 for RTMP, and 7 between an edge server and an origin server.
#define RTMP_MSG_SetChunkSize 0x01
#define RTMP_MSG_AbortMessage 0x02
#define RTMP_MSG_Acknowledgement 0x03
#define RTMP_MSG_UserControlMessage 0x04
#define RTMP_MSG_WindowAcknowledgementSize 0x05
#define RTMP_MSG_SetPeerBandwidth 0x06
#define RTMP_MSG_EdgeAndOriginServerCommand 0x07
// RTMP spec 3.1 to 3.6, the message types. Video messages are large and can delay other
// messages, so the spec gives them the lowest priority.
#define RTMP_MSG_AMF3CommandMessage 17
#define RTMP_MSG_AMF0CommandMessage 20
#define RTMP_MSG_AMF0DataMessage 18
#define RTMP_MSG_AMF3DataMessage 15
#define RTMP_MSG_AMF3SharedObject 16
#define RTMP_MSG_AMF0SharedObject 19
#define RTMP_MSG_AudioMessage 8
#define RTMP_MSG_VideoMessage 9
#define RTMP_MSG_AggregateMessage 22
#define RTMP_CID_ProtocolControl 0x02
#define RTMP_CID_OverConnection 0x03
// Rarely used, such as for onStatus(NetStream.Play.Reset).
#define RTMP_CID_OverConnection2 0x04
#define RTMP_CID_OverStream 0x05
// Rarely used, such as for play("mp4:mystream.f4v").
#define RTMP_CID_OverStream2 0x08
#define RTMP_CID_Video 0x06
#define RTMP_CID_Audio 0x07

// RTMP spec 6.1. Chunk Format: a timestamp of 0xFFFFFF or more is sent as 0xFFFFFF, followed
// by a 4-byte extended timestamp.
#define RTMP_EXTENDED_TIMESTAMP 0xFFFFFF

// RTMP spec 4.1. Message Header.
class SrsMessageHeader
{
public:
    // 3 bytes. Only read when decoding a message from a chunk stream.
    int32_t timestamp_delta_;
    // 3 bytes, big-endian.
    int32_t payload_length_;
    int8_t message_type_;
    // 4 bytes, little-endian.
    int32_t stream_id_;
    // The chunk stream ID preferred when writing this message back to RTMP.
    int32_t prefer_cid_;

    // 4 bytes, big-endian. 64 bits here, for large times in jitter detection and HLS.
    int64_t timestamp_;

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
    void initialize_amf0_script(int size, int stream);
    void initialize_audio(int size, uint32_t time, int stream);
    void initialize_video(int size, uint32_t time, int stream);
};

// An RTMP message as received from the chunk stream; SrsMediaPacket is the one shared to
// copy and send.
class SrsRtmpCommonMessage
{
public:
    // RTMP spec 4.1. Message Header.
    SrsMessageHeader header_;

public:
    // RTMP spec 4.2. Message Payload.
    SrsSharedPtr<SrsMemoryBlock> payload_;

public:
    SrsRtmpCommonMessage();
    virtual ~SrsRtmpCommonMessage();

public:
    char *payload() { return payload_.get() ? payload_->payload() : NULL; }
    int size() { return payload_.get() ? payload_->size() : 0; }

public:
    virtual void create_payload(int size);

public:
    // Takes the body, so the caller must not free it.
    virtual srs_error_t create(SrsMessageHeader *pheader, char *body, int size);

    void to_msg(SrsMediaPacket *msg);
};

class ISrsFlvTransmuxer
{
public:
    ISrsFlvTransmuxer();
    virtual ~ISrsFlvTransmuxer();

public:
    virtual srs_error_t initialize(ISrsWriter *fw) = 0;
    virtual void set_drop_if_not_match(bool v) = 0;
    virtual bool drop_if_not_match() = 0;

public:
    // FLV spec E.2 The FLV header, then PreviousTagSize0, which is always 0.
    virtual srs_error_t write_header(bool has_video = true, bool has_audio = true) = 0;
    virtual srs_error_t write_header(char flv_header[9]) = 0;
    // The type is a SrsFrameType, and the data is the AMF0 string onMetaData and the
    // AMF0 metadata object.
    virtual srs_error_t write_metadata(char type, char *data, int size) = 0;
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size) = 0;
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size) = 0;

public:
    virtual srs_error_t write_tags(SrsMediaPacket **msgs, int count) = 0;
};

class SrsFlvTransmuxer : public ISrsFlvTransmuxer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool has_audio_;
    bool has_video_;
    bool drop_if_not_match_;
    ISrsWriter *writer_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char tag_header_[SRS_FLV_TAG_HEADER_SIZE];

public:
    SrsFlvTransmuxer();
    virtual ~SrsFlvTransmuxer();

public:
    virtual srs_error_t initialize(ISrsWriter *fw);
    void set_drop_if_not_match(bool v);
    bool drop_if_not_match();

public:
    virtual srs_error_t write_header(bool has_video = true, bool has_audio = true);
    virtual srs_error_t write_header(char flv_header[9]);
    virtual srs_error_t write_metadata(char type, char *data, int size);
    virtual srs_error_t write_audio(int64_t timestamp, char *data, int size);
    virtual srs_error_t write_video(int64_t timestamp, char *data, int size);

public:
    static int size_tag(int data_size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int nb_tag_headers_;
    char *tag_headers_;
    // The cache of PreviousTagSize fields.
    int nb_ppts_;
    char *ppts_;
    int nb_iovss_cache_;
    iovec *iovss_cache_;

public:
    virtual srs_error_t write_tags(SrsMediaPacket **msgs, int count);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void cache_metadata(char type, char *data, int size, char *cache);
    virtual void cache_audio(int64_t timestamp, char *data, int size, char *cache);
    virtual void cache_video(int64_t timestamp, char *data, int size, char *cache);
    virtual void cache_pts(int size, char *cache);
    virtual srs_error_t write_tag(char *header, int header_size, char *tag, int tag_size);
};

class ISrsFlvDecoder
{
public:
    ISrsFlvDecoder();
    virtual ~ISrsFlvDecoder();

public:
    virtual srs_error_t initialize(ISrsReader *fr) = 0;
    virtual srs_error_t read_header(char header[9]) = 0;
    virtual srs_error_t read_tag_header(char *ptype, int32_t *pdata_size, uint32_t *ptime) = 0;
    virtual srs_error_t read_tag_data(char *data, int32_t size) = 0;
    virtual srs_error_t read_previous_tag_size(char previous_tag_size[4]) = 0;
};

class SrsFlvDecoder : public ISrsFlvDecoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsReader *reader_;

public:
    SrsFlvDecoder();
    virtual ~SrsFlvDecoder();

public:
    virtual srs_error_t initialize(ISrsReader *fr);

public:
    virtual srs_error_t read_header(char header[9]);
    virtual srs_error_t read_tag_header(char *ptype, int32_t *pdata_size, uint32_t *ptime);
    virtual srs_error_t read_tag_data(char *data, int32_t size);
    virtual srs_error_t read_previous_tag_size(char previous_tag_size[4]);
};

// For FLV VOD: reads the header and the sequence headers, then seeks to the offset to play from.
class SrsFlvVodStreamDecoder
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsFileReader *reader_;

public:
    SrsFlvVodStreamDecoder();
    virtual ~SrsFlvVodStreamDecoder();

public:
    virtual srs_error_t initialize(ISrsReader *fr);

public:
    // The 9-byte FLV header and the 4-byte PreviousTagSize0.
    virtual srs_error_t read_header_ext(char header[13]);
    // Takes the first audio and video tags as the sequence headers.
    virtual srs_error_t read_sequence_header_summary(int64_t *pstart, int *psize);

public:
    virtual srs_error_t seek2(int64_t offset);
};

extern int srs_rtmp_prefer_cid(int message_type);

extern int srs_rtmp_write_chunk_header(SrsMediaPacket *msg, char *cache, int nb_cache, bool c0);

extern int srs_chunk_header_c0(int prefer_cid, uint32_t timestamp, int32_t payload_length, int8_t message_type, int32_t stream_id, char *cache, int nb_cache);

extern int srs_chunk_header_c3(int prefer_cid, uint32_t timestamp, char *cache, int nb_cache);

#endif
