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

#include <srs_protocol_stack.hpp>

#include <srs_protocol_amf0.hpp>
#include <srs_protocol_io.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_buffer.hpp>
#include <srs_protocol_utility.hpp>

#include <stdlib.h>
using namespace std;

// when got a messae header, there must be some data,
// increase recv timeout to got an entire message.
#define SRS_MIN_RECV_TIMEOUT_US (int64_t)(60*1000*1000LL)

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
5. Protocol Control Messages
RTMP reserves message type IDs 1-7 for protocol control messages.
These messages contain information needed by the RTM Chunk Stream
protocol or RTMP itself. Protocol messages with IDs 1 & 2 are
reserved for usage with RTM Chunk Stream protocol. Protocol messages
with IDs 3-6 are reserved for usage of RTMP. Protocol message with ID
7 is used between edge server and origin server.
*/
#define RTMP_MSG_SetChunkSize                   0x01
#define RTMP_MSG_AbortMessage                   0x02
#define RTMP_MSG_Acknowledgement                0x03
#define RTMP_MSG_UserControlMessage             0x04
#define RTMP_MSG_WindowAcknowledgementSize      0x05
#define RTMP_MSG_SetPeerBandwidth               0x06
#define RTMP_MSG_EdgeAndOriginServerCommand     0x07
/**
3. Types of messages
The server and the client send messages over the network to
communicate with each other. The messages can be of any type which
includes audio messages, video messages, command messages, shared
object messages, data messages, and user control messages.
3.1. Command message
Command messages carry the AMF-encoded commands between the client
and the server. These messages have been assigned message type value
of 20 for AMF0 encoding and message type value of 17 for AMF3
encoding. These messages are sent to perform some operations like
connect, createStream, publish, play, pause on the peer. Command
messages like onstatus, result etc. are used to inform the sender
about the status of the requested commands. A command message
consists of command name, transaction ID, and command object that
contains related parameters. A client or a server can request Remote
Procedure Calls (RPC) over streams that are communicated using the
command messages to the peer.
*/
#define RTMP_MSG_AMF3CommandMessage             17 // 0x11
#define RTMP_MSG_AMF0CommandMessage             20 // 0x14
/**
3.2. Data message
The client or the server sends this message to send Metadata or any
user data to the peer. Metadata includes details about the
data(audio, video etc.) like creation time, duration, theme and so
on. These messages have been assigned message type value of 18 for
AMF0 and message type value of 15 for AMF3.        
*/
#define RTMP_MSG_AMF0DataMessage                18 // 0x12
#define RTMP_MSG_AMF3DataMessage                15 // 0x0F
/**
3.3. Shared object message
A shared object is a Flash object (a collection of name value pairs)
that are in synchronization across multiple clients, instances, and
so on. The message types kMsgContainer=19 for AMF0 and
kMsgContainerEx=16 for AMF3 are reserved for shared object events.
Each message can contain multiple events.
*/
#define RTMP_MSG_AMF3SharedObject               16 // 0x10
#define RTMP_MSG_AMF0SharedObject               19 // 0x13
/**
3.4. Audio message
The client or the server sends this message to send audio data to the
peer. The message type value of 8 is reserved for audio messages.
*/
#define RTMP_MSG_AudioMessage                   8 // 0x08
/* *
3.5. Video message
The client or the server sends this message to send video data to the
peer. The message type value of 9 is reserved for video messages.
These messages are large and can delay the sending of other type of
messages. To avoid such a situation, the video message is assigned
the lowest priority.
*/
#define RTMP_MSG_VideoMessage                   9 // 0x09
/**
3.6. Aggregate message
An aggregate message is a single message that contains a list of submessages.
The message type value of 22 is reserved for aggregate
messages.
*/
#define RTMP_MSG_AggregateMessage               22 // 0x16

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* 6.1.2. Chunk Message Header
* There are four different formats for the chunk message header,
* selected by the "fmt" field in the chunk basic header.
*/
// 6.1.2.1. Type 0
// Chunks of Type 0 are 11 bytes long. This type MUST be used at the
// start of a chunk stream, and whenever the stream timestamp goes
// backward (e.g., because of a backward seek).
#define RTMP_FMT_TYPE0                          0
// 6.1.2.2. Type 1
// Chunks of Type 1 are 7 bytes long. The message stream ID is not
// included; this chunk takes the same stream ID as the preceding chunk.
// Streams with variable-sized messages (for example, many video
// formats) SHOULD use this format for the first chunk of each new
// message after the first.
#define RTMP_FMT_TYPE1                          1
// 6.1.2.3. Type 2
// Chunks of Type 2 are 3 bytes long. Neither the stream ID nor the
// message length is included; this chunk has the same stream ID and
// message length as the preceding chunk. Streams with constant-sized
// messages (for example, some audio and data formats) SHOULD use this
// format for the first chunk of each message after the first.
#define RTMP_FMT_TYPE2                          2
// 6.1.2.4. Type 3
// Chunks of Type 3 have no header. Stream ID, message length and
// timestamp delta are not present; chunks of this type take values from
// the preceding chunk. When a single message is split into chunks, all
// chunks of a message except the first one, SHOULD use this type. Refer
// to example 2 in section 6.2.2. Stream consisting of messages of
// exactly the same size, stream ID and spacing in time SHOULD use this
// type for all chunks after chunk of Type 2. Refer to example 1 in
// section 6.2.1. If the delta between the first message and the second
// message is same as the time stamp of first message, then chunk of
// type 3 would immediately follow the chunk of type 0 as there is no
// need for a chunk of type 2 to register the delta. If Type 3 chunk
// follows a Type 0 chunk, then timestamp delta for this Type 3 chunk is
// the same as the timestamp of Type 0 chunk.
#define RTMP_FMT_TYPE3                          3

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* amf0 command message, command name macros
*/
#define RTMP_AMF0_COMMAND_CONNECT               "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM         "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM          "closeStream"
#define RTMP_AMF0_COMMAND_PLAY                  "play"
#define RTMP_AMF0_COMMAND_PAUSE                 "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE            "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS             "onStatus"
#define RTMP_AMF0_COMMAND_RESULT                "_result"
#define RTMP_AMF0_COMMAND_ERROR                 "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM        "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH            "FCPublish"
#define RTMP_AMF0_COMMAND_UNPUBLISH             "FCUnpublish"
#define RTMP_AMF0_COMMAND_PUBLISH               "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS            "|RtmpSampleAccess"
#define RTMP_AMF0_DATA_SET_DATAFRAME            "@setDataFrame"
#define RTMP_AMF0_DATA_ON_METADATA              "onMetaData"

/**
* band width check method name, which will be invoked by client.
* band width check mothods use SrsBandwidthPacket as its internal packet type,
* so ensure you set command name when you use it.
*/
// server play control
#define SRS_BW_CHECK_START_PLAY                 "onSrsBandCheckStartPlayBytes"
#define SRS_BW_CHECK_STARTING_PLAY              "onSrsBandCheckStartingPlayBytes"
#define SRS_BW_CHECK_STOP_PLAY                  "onSrsBandCheckStopPlayBytes"
#define SRS_BW_CHECK_STOPPED_PLAY               "onSrsBandCheckStoppedPlayBytes"

// server publish control
#define SRS_BW_CHECK_START_PUBLISH              "onSrsBandCheckStartPublishBytes"
#define SRS_BW_CHECK_STARTING_PUBLISH           "onSrsBandCheckStartingPublishBytes"
#define SRS_BW_CHECK_STOP_PUBLISH               "onSrsBandCheckStopPublishBytes"
// @remark, flash never send out this packet, for its queue is full.
#define SRS_BW_CHECK_STOPPED_PUBLISH            "onSrsBandCheckStoppedPublishBytes"

// EOF control.
// the report packet when check finished.
#define SRS_BW_CHECK_FINISHED                   "onSrsBandCheckFinished"
// @remark, flash never send out this packet, for its queue is full.
#define SRS_BW_CHECK_FINAL                      "finalClientPacket"

// data packets
#define SRS_BW_CHECK_PLAYING                    "onSrsBandCheckPlaying"
#define SRS_BW_CHECK_PUBLISHING                 "onSrsBandCheckPublishing"

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* the chunk stream id used for some under-layer message,
* for example, the PC(protocol control) message.
*/
#define RTMP_CID_ProtocolControl                0x02
/**
* the AMF0/AMF3 command message, invoke method and return the result, over NetConnection.
* generally use 0x03.
*/
#define RTMP_CID_OverConnection                 0x03
/**
* the AMF0/AMF3 command message, invoke method and return the result, over NetConnection, 
* the midst state(we guess).
* rarely used, e.g. onStatus(NetStream.Play.Reset).
*/
#define RTMP_CID_OverConnection2                0x04
/**
* the stream message(amf0/amf3), over NetStream.
* generally use 0x05.
*/
#define RTMP_CID_OverStream                     0x05
/**
* the stream message(amf0/amf3), over NetStream, the midst state(we guess).
* rarely used, e.g. play("mp4:mystram.f4v")
*/
#define RTMP_CID_OverStream2                    0x08
/**
* the stream message(video), over NetStream
* generally use 0x06.
*/
#define RTMP_CID_Video                          0x06
/**
* the stream message(audio), over NetStream.
* generally use 0x07.
*/
#define RTMP_CID_Audio                          0x07

/****************************************************************************
*****************************************************************************
****************************************************************************/

SrsMessageHeader::SrsMessageHeader()
{
    message_type = 0;
    payload_length = 0;
    timestamp_delta = 0;
    stream_id = 0;
    
    timestamp = 0;
    // we always use the connection chunk-id
    perfer_cid = RTMP_CID_OverConnection;
}

SrsMessageHeader::~SrsMessageHeader()
{
}

bool SrsMessageHeader::is_audio()
{
    return message_type == RTMP_MSG_AudioMessage;
}

bool SrsMessageHeader::is_video()
{
    return message_type == RTMP_MSG_VideoMessage;
}

bool SrsMessageHeader::is_amf0_command()
{
    return message_type == RTMP_MSG_AMF0CommandMessage;
}

bool SrsMessageHeader::is_amf0_data()
{
    return message_type == RTMP_MSG_AMF0DataMessage;
}

bool SrsMessageHeader::is_amf3_command()
{
    return message_type == RTMP_MSG_AMF3CommandMessage;
}

bool SrsMessageHeader::is_amf3_data()
{
    return message_type == RTMP_MSG_AMF3DataMessage;
}

bool SrsMessageHeader::is_window_ackledgement_size()
{
    return message_type == RTMP_MSG_WindowAcknowledgementSize;
}

bool SrsMessageHeader::is_ackledgement()
{
    return message_type == RTMP_MSG_Acknowledgement;
}

bool SrsMessageHeader::is_set_chunk_size()
{
    return message_type == RTMP_MSG_SetChunkSize;
}

bool SrsMessageHeader::is_user_control_message()
{
    return message_type == RTMP_MSG_UserControlMessage;
}

bool SrsMessageHeader::is_set_peer_bandwidth()
{
    return message_type == RTMP_MSG_SetPeerBandwidth;
}

bool SrsMessageHeader::is_aggregate()
{
    return message_type == RTMP_MSG_AggregateMessage;
}

void SrsMessageHeader::initialize_amf0_script(int size, int stream)
{
    message_type = RTMP_MSG_AMF0DataMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)0;
    timestamp = (int64_t)0;
    stream_id = (int32_t)stream;
    
    // amf0 script use connection2 chunk-id
    perfer_cid = RTMP_CID_OverConnection2;
}

void SrsMessageHeader::initialize_audio(int size, u_int32_t time, int stream)
{
    message_type = RTMP_MSG_AudioMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)time;
    timestamp = (int64_t)time;
    stream_id = (int32_t)stream;
    
    // audio chunk-id
    perfer_cid = RTMP_CID_Audio;
}

void SrsMessageHeader::initialize_video(int size, u_int32_t time, int stream)
{
    message_type = RTMP_MSG_VideoMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)time;
    timestamp = (int64_t)time;
    stream_id = (int32_t)stream;
    
    // video chunk-id
    perfer_cid = RTMP_CID_Video;
}

SrsCommonMessage::SrsCommonMessage()
{
    payload = NULL;
    size = 0;
}

SrsCommonMessage::~SrsCommonMessage()
{
    srs_freep(payload);
}

SrsSharedPtrMessage::__SrsSharedPtr::__SrsSharedPtr()
{
    payload = NULL;
    size = 0;
    shared_count = 0;

#ifdef SRS_PERF_MW_MSG_IOVS_CACHE
    nb_iovs = 0;
    iovs = NULL;
    chunk_size = 0;
#endif
}

SrsSharedPtrMessage::__SrsSharedPtr::~__SrsSharedPtr()
{
    srs_freep(payload);

#ifdef SRS_PERF_MW_MSG_IOVS_CACHE
    srs_freep(iovs);
#endif
}

#ifdef SRS_PERF_MW_MSG_IOVS_CACHE
int SrsSharedPtrMessage::__SrsSharedPtr::mic_evaluate(
    SrsMessageHeader* mh, int chunk_size
) {
    int ret = ERROR_SUCCESS;
    
    // use the chunk size, shuold not be changed.
    this->chunk_size = chunk_size;
    
    // ignore size
    srs_chunk_header(mic_c0, mh, true);
    mic_c3 = 0xC0 | (mh->perfer_cid & 0x3F);
    
    // calc number of iovs
    nb_chunks = mh->payload_length / chunk_size;
    if (mh->payload_length % chunk_size) {
        nb_chunks++;
    }
    nb_iovs = 1/*cid*/ + 1/*size*/ + 1 /*type*/+ 1/*chunk*/;
    // left chunks, always cid+chunk.
    if (nb_chunks > 0) {
        nb_iovs += (nb_chunks - 1) * 2;
    }
    
    // create iovs
    srs_freep(iovs);
    iovs = new iovec[nb_iovs];
    
    // for payload chunks.
    char* p = payload;
    char* end = p + size;
    iovec* iov = iovs + 0;
    while (p < end) {
        // size of payload.
        int payload_size = srs_min(chunk_size, end - p);
        
        // header, c0 or c3
        if (p == payload) {
            // c0, cid+size+type
            // cid, 1B
            iov[0].iov_base = mic_c0;
            iov[0].iov_len = 1;
            
            // size(payload length), 3B
            iov[1].iov_base = mic_c0 + 4;
            iov[1].iov_len = 3;
            
            // type(message type)
            iov[2].iov_base = mic_c0 + 7;
            iov[2].iov_len = 1;
        
            // chunk
            iov[3].iov_base = p;
            iov[3].iov_len = payload_size;
            
            // move to next iovs.
            iov += 4;
        } else {
            // c3
            iov[0].iov_base = &mic_c3;
            iov[0].iov_len = 1;
        
            // chunk
            iov[1].iov_base = p;
            iov[1].iov_len = payload_size;
            
            // move to next iovs.
            iov += 2;
        }
        
        // to next chunk
        p += payload_size;
    }
    
    return ret;
}
#endif

SrsSharedPtrMessage::SrsSharedPtrMessage()
{
    ptr = NULL;
    
#ifdef SRS_PERF_MW_MSG_IOVS_CACHE
    mic_etime_present = false;
    iovs = NULL;
    nb_iovs = 0;
#endif
}

SrsSharedPtrMessage::~SrsSharedPtrMessage()
{
    if (ptr) {
        if (ptr->shared_count == 0) {
            srs_freep(ptr);
        } else {
            ptr->shared_count--;
        }
    }
    
#ifdef SRS_PERF_MW_MSG_IOVS_CACHE
    srs_freep(iovs);
#endif
}

int SrsSharedPtrMessage::create(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;

    if ((ret = create(&msg->header, msg->payload, msg->size)) != ERROR_SUCCESS) {
        return ret;
    }

    // to prevent double free of payload:
    // initialize already attach the payload of msg,
    // detach the payload to transfer the owner to shared ptr.
    msg->payload = NULL;
    msg->size = 0;

    return ret;
}

int SrsSharedPtrMessage::create(SrsMessageHeader* pheader, char* payload, int size)
{
    int ret = ERROR_SUCCESS;

    srs_assert(pheader != NULL);
    if (ptr) {
        ret = ERROR_SYSTEM_ASSERT_FAILED;
        srs_error("should not set the payload twice. ret=%d", ret);
        srs_assert(false);

        return ret;
    }

    header = *pheader;
    header.payload_length = size;

    ptr = new __SrsSharedPtr();

    // direct attach the data.
    ptr->payload = payload;
    ptr->size = size;

    // message can access it.
    this->payload = ptr->payload;
    this->size = ptr->size;

    return ret;
}

int SrsSharedPtrMessage::count()
{
    srs_assert(ptr);
    return ptr->shared_count;
}

SrsSharedPtrMessage* SrsSharedPtrMessage::copy()
{
    srs_assert(ptr);

    SrsSharedPtrMessage* copy = new SrsSharedPtrMessage();

    copy->header = header;

    copy->ptr = ptr;
    ptr->shared_count++;

    copy->payload = ptr->payload;
    copy->size = ptr->size;

    return copy;
}

#ifdef SRS_PERF_MW_MSG_IOVS_CACHE
int SrsSharedPtrMessage::mic_evaluate(int chunk_size)
{
    int ret = ERROR_SUCCESS;
    
    // when chunk size changed, error to disconnect the client..
    if (ptr->chunk_size > 0 && chunk_size != ptr->chunk_size) {
        ret = ERROR_RTMP_MIC_CHUNKSIZE_CHANGED;
        srs_warn("mic chunk size changed %d=>%d, ret=%d", 
            ptr->chunk_size, chunk_size, ret);
        return ret;
    }
    
    // calc the shared ptr iovs at the first time.
    if (ptr->chunk_size <= 0) {
        if ((ret = ptr->mic_evaluate(&header, chunk_size)) != ERROR_SUCCESS) {
            srs_warn("mic evaluate source iovs failed. ret=%d", ret);
            return ret;
        }
    }
    
    // calc the private iovs
    char* pp = NULL;
    
    // timestamp for c0/c3
    u_int32_t timestamp = (u_int32_t)header.timestamp;
    mic_etime_present = timestamp >= RTMP_EXTENDED_TIMESTAMP;
    
    // chunk message header, 11 bytes
    // timestamp, 3bytes, big-endian
    char* p = mic_c0_time;
    if (!mic_etime_present) {
        pp = (char*)&timestamp;
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    } else {
        *p++ = 0xFF;
        *p++ = 0xFF;
        *p++ = 0xFF;
    }
        
    // stream_id, 4bytes, little-endian
    p = mic_c0_sid;
    pp = (char*)&header.stream_id;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
    
    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    p = mic_etime;
    if (mic_etime_present) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }
    
    // calc number of iovs.
    nb_iovs = 1/*time*/ + 1/*sid*/;
    // insert etime before all chunks.
    if (mic_etime_present) {
        nb_iovs += ptr->nb_chunks;
    }
    
    // create iovs
    srs_freep(iovs);
    iovs = new iovec[nb_iovs];
    
    // time, 3B
    iovs[0].iov_base = mic_c0_time;
    iovs[0].iov_len = 3;
    
    // sid, 4B
    iovs[1].iov_base = mic_c0_sid;
    iovs[1].iov_len = 4;
    
    // etime, 4B for each chunks.
    for (int i = 2; i < nb_iovs; i++) {
        iovs[i].iov_base = mic_etime;
        iovs[i].iov_len = 4;
    }
    
    return ret;
}

int SrsSharedPtrMessage::mic_iovs_count()
{
    return nb_iovs + ptr->nb_iovs;
}

int SrsSharedPtrMessage::mic_iovs_dump(iovec* _iovs, int _nb_iovs)
{
    int shared_index = 0;
    int private_index = 0;
    int index = 0;
    
    // dumps all.
    srs_assert(nb_iovs + ptr->nb_iovs <= _nb_iovs);
    
    // dump the c0 chunk
    _iovs[index++] = ptr->iovs[shared_index++]; // cid
    _iovs[index++] = iovs[private_index++]; // time
    _iovs[index++] = ptr->iovs[shared_index++]; // size
    _iovs[index++] = ptr->iovs[shared_index++]; // type
    _iovs[index++] = iovs[private_index++]; // sid
    if (mic_etime_present) {
        _iovs[index++] = iovs[private_index++]; // etime
    }
    _iovs[index++] = ptr->iovs[shared_index++]; // chunk
    
    // dump left c3 chunks
    for (int i = 1; i < ptr->nb_chunks; i++) {
        _iovs[index++] = ptr->iovs[shared_index++]; // cid
        if (mic_etime_present) {
            _iovs[index++] = iovs[private_index++]; // etime
        }
        _iovs[index++] = ptr->iovs[shared_index++]; // chunk
    }
    
    srs_assert(index == private_index + shared_index);
    srs_assert(index == nb_iovs + ptr->nb_iovs);
    srs_assert(index <= _nb_iovs);
    
    return nb_iovs + ptr->nb_iovs;
}
#endif

SrsProtocol::AckWindowSize::AckWindowSize()
{
    ack_window_size = 0;
    acked_size = 0;
}

SrsProtocol::SrsProtocol(ISrsProtocolReaderWriter* io)
{
    in_buffer = new SrsFastBuffer();
    skt = io;
    
    in_chunk_size = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
    out_chunk_size = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
    
    nb_out_iovs = SRS_CONSTS_IOVS_MAX;
    out_iovs = (iovec*)malloc(sizeof(iovec) * nb_out_iovs);
    // each chunk consumers atleast 2 iovs
    srs_assert(nb_out_iovs >= 2);
    
#ifndef SRS_PERF_MW_MSG_IOVS_CACHE
    warned_c0c3_cache_dry = false;
    auto_response_when_recv = true;
#endif
    
    cs_cache = NULL;
    if (SRS_PERF_CHUNK_STREAM_CACHE > 0) {
        cs_cache = new SrsChunkStream*[SRS_PERF_CHUNK_STREAM_CACHE];
    }
    for (int cid = 0; cid < SRS_PERF_CHUNK_STREAM_CACHE; cid++) {
        SrsChunkStream* cs = new SrsChunkStream(cid);
        // set the perfer cid of chunk,
        // which will copy to the message received.
        cs->header.perfer_cid = cid;
        
        cs_cache[cid] = cs;
    }
}

SrsProtocol::~SrsProtocol()
{
    if (true) {
        std::map<int, SrsChunkStream*>::iterator it;
        
        for (it = chunk_streams.begin(); it != chunk_streams.end(); ++it) {
            SrsChunkStream* stream = it->second;
            srs_freep(stream);
        }
    
        chunk_streams.clear();
    }
    
    if (true) {
        std::vector<SrsPacket*>::iterator it;
        for (it = manual_response_queue.begin(); it != manual_response_queue.end(); ++it) {
            SrsPacket* pkt = *it;
            srs_freep(pkt);
        }
        manual_response_queue.clear();
    }
    
    srs_freep(in_buffer);
    
    // alloc by malloc, use free directly.
    if (out_iovs) {
        free(out_iovs);
        out_iovs = NULL;
    }
    
    // free all chunk stream cache.
    for (int i = 0; i < SRS_PERF_CHUNK_STREAM_CACHE; i++) {
        SrsChunkStream* cs = cs_cache[i];
        srs_freep(cs);
    }
    srs_freep(cs_cache);
}

void SrsProtocol::set_auto_response(bool v)
{
    auto_response_when_recv = v;
}

int SrsProtocol::manual_response_flush()
{
    int ret = ERROR_SUCCESS;
    
    if (manual_response_queue.empty()) {
        return ret;
    }
    
    std::vector<SrsPacket*>::iterator it;
    for (it = manual_response_queue.begin(); it != manual_response_queue.end();) {
        SrsPacket* pkt = *it;
        
        // erase this packet, the send api always free it.
        it = manual_response_queue.erase(it);
        
        // use underlayer api to send, donot flush again.
        if ((ret = do_send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

#ifdef SRS_PERF_MERGED_READ
void SrsProtocol::set_merge_read(bool v, IMergeReadHandler* handler)
{
    in_buffer->set_merge_read(v, handler);
}

void SrsProtocol::set_recv_buffer(int buffer_size)
{
    in_buffer->set_buffer(buffer_size);
}
#endif

void SrsProtocol::set_recv_timeout(int64_t timeout_us)
{
    return skt->set_recv_timeout(timeout_us);
}

int64_t SrsProtocol::get_recv_timeout()
{
    return skt->get_recv_timeout();
}

void SrsProtocol::set_send_timeout(int64_t timeout_us)
{
    return skt->set_send_timeout(timeout_us);
}

int64_t SrsProtocol::get_send_timeout()
{
    return skt->get_send_timeout();
}

int64_t SrsProtocol::get_recv_bytes()
{
    return skt->get_recv_bytes();
}

int64_t SrsProtocol::get_send_bytes()
{
    return skt->get_send_bytes();
}

int SrsProtocol::recv_message(SrsCommonMessage** pmsg)
{
    *pmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        
        if ((ret = recv_interlaced_message(&msg)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("recv interlaced message failed. ret=%d", ret);
            }
            srs_freep(msg);
            return ret;
        }
        srs_verbose("entire msg received");
        
        if (!msg) {
            continue;
        }
        
        if (msg->size <= 0 || msg->header.payload_length <= 0) {
            srs_trace("ignore empty message(type=%d, size=%d, time=%"PRId64", sid=%d).",
                msg->header.message_type, msg->header.payload_length,
                msg->header.timestamp, msg->header.stream_id);
            srs_freep(msg);
            continue;
        }
        
        if ((ret = on_recv_message(msg)) != ERROR_SUCCESS) {
            srs_error("hook the received msg failed. ret=%d", ret);
            srs_freep(msg);
            return ret;
        }
        
        srs_verbose("got a msg, cid=%d, type=%d, size=%d, time=%"PRId64, 
            msg->header.perfer_cid, msg->header.message_type, msg->header.payload_length, 
            msg->header.timestamp);
        *pmsg = msg;
        break;
    }
    
    return ret;
}

int SrsProtocol::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    *ppacket = NULL;
    
    int ret = ERROR_SUCCESS;
    
    srs_assert(msg != NULL);
    srs_assert(msg->payload != NULL);
    srs_assert(msg->size > 0);
    
    SrsStream stream;

    // initialize the decode stream for all message,
    // it's ok for the initialize if fast and without memory copy.
    if ((ret = stream.initialize(msg->payload, msg->size)) != ERROR_SUCCESS) {
        srs_error("initialize stream failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("decode stream initialized success");
    
    // decode the packet.
    SrsPacket* packet = NULL;
    if ((ret = do_decode_message(msg->header, &stream, &packet)) != ERROR_SUCCESS) {
        srs_freep(packet);
        return ret;
    }
    
    // set to output ppacket only when success.
    *ppacket = packet;
    
    return ret;
}

int SrsProtocol::do_send_messages(SrsSharedPtrMessage** msgs, int nb_msgs)
{
    int ret = ERROR_SUCCESS;
    
#ifndef SRS_PERF_MW_MSG_IOVS_CACHE
    // TODO: FIXME: use cache system instead.
    int iov_index = 0;
    iovec* iov = out_iovs + iov_index;
    
    int c0c3_cache_index = 0;
    char* c0c3_cache = out_c0c3_caches + c0c3_cache_index;

    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
    
        // ignore empty message.
        if (!msg->payload || msg->size <= 0) {
            srs_info("ignore empty message.");
            continue;
        }
        
        // we donot use the complex basic header,
        // ensure the basic header is 1bytes.
        if (msg->header.perfer_cid < 2) {
            srs_info("change the chunk_id=%d to default=%d", 
                msg->header.perfer_cid, RTMP_CID_ProtocolControl);
            msg->header.perfer_cid = RTMP_CID_ProtocolControl;
        }
    
        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char* p = msg->payload;
        char* pend = msg->payload + msg->size;
        
        // always write the header event payload is empty.
        while (p < pend) {
            // always has header
            int nbh = srs_chunk_header(c0c3_cache, &msg->header, p == msg->payload);
            srs_assert(nbh > 0);
            
            // header iov
            iov[0].iov_base = c0c3_cache;
            iov[0].iov_len = nbh;
            
            // payload iov
            int payload_size = pend - p;
            if (payload_size > out_chunk_size) {
                payload_size = out_chunk_size;
            }
            iov[1].iov_base = p;
            iov[1].iov_len = payload_size;
            
            // consume sendout bytes.
            p += payload_size;
            
            // realloc the iovs if exceed,
            // for we donot know how many messges maybe to send entirely,
            // we just alloc the iovs, it's ok.
            if (iov_index >= nb_out_iovs - 2) {
                srs_warn("resize iovs %d => %d, max_msgs=%d", 
                    nb_out_iovs, nb_out_iovs + SRS_CONSTS_IOVS_MAX, 
                    SRS_PERF_MW_MSGS);
                    
                nb_out_iovs += SRS_CONSTS_IOVS_MAX;
                int realloc_size = sizeof(iovec) * nb_out_iovs;
                out_iovs = (iovec*)realloc(out_iovs, realloc_size);
            }
            
            // to next pair of iovs
            iov_index += 2;
            iov = out_iovs + iov_index;

            // to next c0c3 header cache
            c0c3_cache_index += nbh;
            c0c3_cache = out_c0c3_caches + c0c3_cache_index;
            
            // the cache header should never be realloc again,
            // for the ptr is set to iovs, so we just warn user to set larger
            // and use another loop to send again.
            int c0c3_left = SRS_CONSTS_C0C3_HEADERS_MAX - c0c3_cache_index;
            if (c0c3_left < SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
                // only warn once for a connection.
                if (!warned_c0c3_cache_dry) {
                    srs_warn("c0c3 cache header too small, recoment to %d", 
                        SRS_CONSTS_C0C3_HEADERS_MAX + SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE);
                    warned_c0c3_cache_dry = true;
                }
                
                // when c0c3 cache dry,
                // sendout all messages and reset the cache, then send again.
                if ((ret = skt->writev(out_iovs, iov_index, NULL)) != ERROR_SUCCESS) {
                    if (!srs_is_client_gracefully_close(ret)) {
                        srs_error("send msgs with writev failed. ret=%d", ret);
                    }
                    return ret;
                }
    
                // reset caches, while these cache ensure 
                // atleast we can sendout a chunk.
                iov_index = 0;
                iov = out_iovs + iov_index;
                
                c0c3_cache_index = 0;
                c0c3_cache = out_c0c3_caches + c0c3_cache_index;
            }
        }
    }
#else
    // send all iovs for all msgs.
    int total_iovs = 0;
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        if ((ret = msg->mic_evaluate(out_chunk_size)) != ERROR_SUCCESS) {
            srs_error("mic evaluate failed, chunk=%d. ret=%d", out_chunk_size, ret);
            return ret;
        }
        total_iovs += msg->mic_iovs_count();
    }
    srs_verbose("mic nb_iovs=%d, max=%d", total_iovs, nb_out_iovs);

    // realloc the iovs if exceed,
    // for we donot know how many messges maybe to send entirely,
    // we just alloc the iovs, it's ok.
    if (total_iovs > nb_out_iovs) {
        srs_warn("resize iovs %d => %d, msgs=%d, max_msgs=%d", 
            nb_out_iovs, total_iovs, nb_msgs, SRS_PERF_MW_MSGS);
            
        nb_out_iovs = total_iovs;
        int realloc_size = sizeof(iovec) * nb_out_iovs;
        out_iovs = (iovec*)realloc(out_iovs, realloc_size);
    }
    
    // dumps iovs
    int iov_index = 0;
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        iov_index += msg->mic_iovs_dump(
            out_iovs + iov_index, nb_out_iovs - iov_index
        );
    }
#endif
    
    // maybe the iovs already sendout when c0c3 cache dry,
    // so just ignore when no iovs to send.
    if (iov_index <= 0) {
        return ret;
    }
#if 0
    // calc the bytes of iovs, for debug.
    int nb_bytes = 0;
    for (int i = 0; i < iov_index; i++) {
        iovec* iov = out_iovs + i;
        nb_bytes += iov->iov_len;
    }
    #ifndef SRS_PERF_MW_MSG_IOVS_CACHE
    srs_info("mw %d msgs %dB in %d iovs, max_msgs=%d, nb_out_iovs=%d",
        nb_msgs, nb_bytes, iov_index, SRS_PERF_MW_MSGS, nb_out_iovs);
    #else
    srs_info("mic nb_iovs=%d, max=%d, msgs=%d %dB", 
        total_iovs, nb_out_iovs, nb_msgs, nb_bytes);
    #endif
#else
    #ifndef SRS_PERF_MW_MSG_IOVS_CACHE
    srs_info("mw %d msgs in %d iovs, max_msgs=%d, nb_out_iovs=%d",
        nb_msgs, iov_index, SRS_PERF_MW_MSGS, nb_out_iovs);
    #else
    srs_info("mic nb_iovs=%d, max=%d, msgs=%d", 
        total_iovs, nb_out_iovs, nb_msgs);
    #endif
#endif
    
    // the limits of writev iovs.
    static int limits = sysconf(_SC_IOV_MAX);
    
    // send in a time.
    if (iov_index < limits) {
        if ((ret = skt->writev(out_iovs, iov_index, NULL)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("send with writev failed. ret=%d", ret);
            }
            return ret;
        }
        return ret;
    }
    
    // send in multiple times.
    int cur_iov = 0;
    while (cur_iov < iov_index) {
        int cur_count = srs_min(limits, iov_index - cur_iov);
        if ((ret = skt->writev(out_iovs + cur_iov, cur_count, NULL)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("send with writev failed. ret=%d", ret);
            }
            return ret;
        }
        cur_iov += cur_count;
    }
    
    return ret;
}

int SrsProtocol::do_send_and_free_packet(SrsPacket* packet, int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(packet);
    SrsAutoFree(SrsPacket, packet);
    
    int size = 0;
    char* payload = NULL;
    if ((ret = packet->encode(size, payload)) != ERROR_SUCCESS) {
        srs_error("encode RTMP packet to bytes oriented RTMP message failed. ret=%d", ret);
        return ret;
    }
    
    // encode packet to payload and size.
    if (size <= 0 || payload == NULL) {
        srs_warn("packet is empty, ignore empty message.");
        return ret;
    }
    
    // to message
    SrsMessageHeader header;
    header.payload_length = size;
    header.message_type = packet->get_message_type();
    header.stream_id = stream_id;
    header.perfer_cid = packet->get_prefer_cid();
    
    ret = do_simple_send(&header, payload, size);
    if (ret == ERROR_SUCCESS) {
        ret = on_send_packet(&header, packet);
    }
    
    return ret;
}

int SrsProtocol::do_simple_send(SrsMessageHeader* mh, char* payload, int size)
{
    int ret = ERROR_SUCCESS;
    
    // we directly send out the packet,
    // use very simple algorithm, not very fast,
    // but it's ok.
    char* p = payload;
    char* end = p + size;
    char c0c3[SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE];
    while (p < end) {
        int nbh = srs_chunk_header(c0c3, mh, p == payload);
        
        iovec iovs[2];
        iovs[0].iov_base = c0c3;
        iovs[0].iov_len = nbh;
        
        int payload_size = srs_min(end - p, out_chunk_size);
        iovs[1].iov_base = p;
        iovs[1].iov_len = payload_size;
        p += payload_size;
        
        if ((ret = skt->writev(iovs, 2, NULL)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("send packet with writev failed. ret=%d", ret);
            }
            return ret;
        }
    }
    
    return ret;
}

int SrsProtocol::do_decode_message(SrsMessageHeader& header, SrsStream* stream, SrsPacket** ppacket)
{
    int ret = ERROR_SUCCESS;
    
    SrsPacket* packet = NULL;
    
    // decode specified packet type
    if (header.is_amf0_command() || header.is_amf3_command() || header.is_amf0_data() || header.is_amf3_data()) {
        srs_verbose("start to decode AMF0/AMF3 command message.");
        
        // skip 1bytes to decode the amf3 command.
        if (header.is_amf3_command() && stream->require(1)) {
            srs_verbose("skip 1bytes to decode AMF3 command");
            stream->skip(1);
        }
        
        // amf0 command message.
        // need to read the command name.
        std::string command;
        if ((ret = srs_amf0_read_string(stream, command)) != ERROR_SUCCESS) {
            srs_error("decode AMF0/AMF3 command name failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("AMF0/AMF3 command message, command_name=%s", command.c_str());
        
        // result/error packet
        if (command == RTMP_AMF0_COMMAND_RESULT || command == RTMP_AMF0_COMMAND_ERROR) {
            double transactionId = 0.0;
            if ((ret = srs_amf0_read_number(stream, transactionId)) != ERROR_SUCCESS) {
                srs_error("decode AMF0/AMF3 transcationId failed. ret=%d", ret);
                return ret;
            }
            srs_verbose("AMF0/AMF3 command id, transcationId=%.2f", transactionId);
            
            // reset stream, for header read completed.
            stream->skip(-1 * stream->pos());
            if (header.is_amf3_command()) {
                stream->skip(1);
            }
            
            // find the call name
            if (requests.find(transactionId) == requests.end()) {
                ret = ERROR_RTMP_NO_REQUEST;
                srs_error("decode AMF0/AMF3 request failed. ret=%d", ret);
                return ret;
            }
            
            std::string request_name = requests[transactionId];
            srs_verbose("AMF0/AMF3 request parsed. request_name=%s", request_name.c_str());

            if (request_name == RTMP_AMF0_COMMAND_CONNECT) {
                srs_info("decode the AMF0/AMF3 response command(%s message).", request_name.c_str());
                *ppacket = packet = new SrsConnectAppResPacket();
                return packet->decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_CREATE_STREAM) {
                srs_info("decode the AMF0/AMF3 response command(%s message).", request_name.c_str());
                *ppacket = packet = new SrsCreateStreamResPacket(0, 0);
                return packet->decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_RELEASE_STREAM
                || request_name == RTMP_AMF0_COMMAND_FC_PUBLISH
                || request_name == RTMP_AMF0_COMMAND_UNPUBLISH) {
                srs_info("decode the AMF0/AMF3 response command(%s message).", request_name.c_str());
                *ppacket = packet = new SrsFMLEStartResPacket(0);
                return packet->decode(stream);
            } else {
                ret = ERROR_RTMP_NO_REQUEST;
                srs_error("decode AMF0/AMF3 request failed. "
                    "request_name=%s, transactionId=%.2f, ret=%d", 
                    request_name.c_str(), transactionId, ret);
                return ret;
            }
        }
        
        // reset to zero(amf3 to 1) to restart decode.
        stream->skip(-1 * stream->pos());
        if (header.is_amf3_command()) {
            stream->skip(1);
        }
        
        // decode command object.
        if (command == RTMP_AMF0_COMMAND_CONNECT) {
            srs_info("decode the AMF0/AMF3 command(connect vhost/app message).");
            *ppacket = packet = new SrsConnectAppPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_CREATE_STREAM) {
            srs_info("decode the AMF0/AMF3 command(createStream message).");
            *ppacket = packet = new SrsCreateStreamPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_PLAY) {
            srs_info("decode the AMF0/AMF3 command(paly message).");
            *ppacket = packet = new SrsPlayPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_PAUSE) {
            srs_info("decode the AMF0/AMF3 command(pause message).");
            *ppacket = packet = new SrsPausePacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
            srs_info("decode the AMF0/AMF3 command(FMLE releaseStream message).");
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_FC_PUBLISH) {
            srs_info("decode the AMF0/AMF3 command(FMLE FCPublish message).");
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_PUBLISH) {
            srs_info("decode the AMF0/AMF3 command(publish message).");
            *ppacket = packet = new SrsPublishPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_UNPUBLISH) {
            srs_info("decode the AMF0/AMF3 command(unpublish message).");
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_DATA_SET_DATAFRAME || command == RTMP_AMF0_DATA_ON_METADATA) {
            srs_info("decode the AMF0/AMF3 data(onMetaData message).");
            *ppacket = packet = new SrsOnMetaDataPacket();
            return packet->decode(stream);
        } else if(command == SRS_BW_CHECK_FINISHED
            || command == SRS_BW_CHECK_PLAYING
            || command == SRS_BW_CHECK_PUBLISHING
            || command == SRS_BW_CHECK_STARTING_PLAY
            || command == SRS_BW_CHECK_STARTING_PUBLISH
            || command == SRS_BW_CHECK_START_PLAY
            || command == SRS_BW_CHECK_START_PUBLISH
            || command == SRS_BW_CHECK_STOPPED_PLAY
            || command == SRS_BW_CHECK_STOP_PLAY
            || command == SRS_BW_CHECK_STOP_PUBLISH
            || command == SRS_BW_CHECK_STOPPED_PUBLISH
            || command == SRS_BW_CHECK_FINAL)
        {
            srs_info("decode the AMF0/AMF3 band width check message.");
            *ppacket = packet = new SrsBandwidthPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_CLOSE_STREAM) {
            srs_info("decode the AMF0/AMF3 closeStream message.");
            *ppacket = packet = new SrsCloseStreamPacket();
            return packet->decode(stream);
        } else if (header.is_amf0_command() || header.is_amf3_command()) {
            srs_info("decode the AMF0/AMF3 call message.");
            *ppacket = packet = new SrsCallPacket();
            return packet->decode(stream);
        }
        
        // default packet to drop message.
        srs_info("drop the AMF0/AMF3 command message, command_name=%s", command.c_str());
        *ppacket = packet = new SrsPacket();
        return ret;
    } else if(header.is_user_control_message()) {
        srs_verbose("start to decode user control message.");
        *ppacket = packet = new SrsUserControlPacket();
        return packet->decode(stream);
    } else if(header.is_window_ackledgement_size()) {
        srs_verbose("start to decode set ack window size message.");
        *ppacket = packet = new SrsSetWindowAckSizePacket();
        return packet->decode(stream);
    } else if(header.is_set_chunk_size()) {
        srs_verbose("start to decode set chunk size message.");
        *ppacket = packet = new SrsSetChunkSizePacket();
        return packet->decode(stream);
    } else {
        if (!header.is_set_peer_bandwidth() && !header.is_ackledgement()) {
            srs_trace("drop unknown message, type=%d", header.message_type);
        }
    }
    
    return ret;
}

int SrsProtocol::send_and_free_message(SrsSharedPtrMessage* msg, int stream_id)
{
    return send_and_free_messages(&msg, 1, stream_id);
}

int SrsProtocol::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id)
{
    // always not NULL msg.
    srs_assert(msgs);
    srs_assert(nb_msgs > 0);
    
    // update the stream id in header.
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        // we assume that the stream_id in a group must be the same.
        if (msg->header.stream_id == stream_id) {
            break;
        }
        msg->header.stream_id = stream_id;
    }
    
    // donot use the auto free to free the msg,
    // for performance issue.
    int ret = do_send_messages(msgs, nb_msgs);
    
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
    }
    
    // donot flush when send failed
    if (ret != ERROR_SUCCESS) {
        return ret;
    }
    
    // flush messages in manual queue
    if ((ret = manual_response_flush()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsProtocol::send_and_free_packet(SrsPacket* packet, int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = do_send_and_free_packet(packet, stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // flush messages in manual queue
    if ((ret = manual_response_flush()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsProtocol::recv_interlaced_message(SrsCommonMessage** pmsg)
{
    int ret = ERROR_SUCCESS;
    
    // chunk stream basic header.
    char fmt = 0;
    int cid = 0;
    if ((ret = read_basic_header(fmt, cid)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read basic header failed. ret=%d", ret);
        }
        return ret;
    }
    srs_verbose("read basic header success. fmt=%d, cid=%d", fmt, cid);
    
    // the cid must not negative.
    srs_assert(cid >= 0);
    
    // get the cached chunk stream.
    SrsChunkStream* chunk = NULL;
    
    // use chunk stream cache to get the chunk info.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/249
    if (cid < SRS_PERF_CHUNK_STREAM_CACHE) {
        // chunk stream cache hit.
        srs_verbose("cs-cache hit, cid=%d", cid);
        // already init, use it direclty
        chunk = cs_cache[cid];
        srs_verbose("cached chunk stream: fmt=%d, cid=%d, size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)",
            chunk->fmt, chunk->cid, (chunk->msg? chunk->msg->size : 0), chunk->header.message_type, chunk->header.payload_length,
            chunk->header.timestamp, chunk->header.stream_id);
    } else {
        // chunk stream cache miss, use map.
        if (chunk_streams.find(cid) == chunk_streams.end()) {
            chunk = chunk_streams[cid] = new SrsChunkStream(cid);
            // set the perfer cid of chunk,
            // which will copy to the message received.
            chunk->header.perfer_cid = cid;
            srs_verbose("cache new chunk stream: fmt=%d, cid=%d", fmt, cid);
        } else {
            chunk = chunk_streams[cid];
            srs_verbose("cached chunk stream: fmt=%d, cid=%d, size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)",
                chunk->fmt, chunk->cid, (chunk->msg? chunk->msg->size : 0), chunk->header.message_type, chunk->header.payload_length,
                chunk->header.timestamp, chunk->header.stream_id);
        }
    }

    // chunk stream message header
    if ((ret = read_message_header(chunk, fmt)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read message header failed. ret=%d", ret);
        }
        return ret;
    }
    srs_verbose("read message header success. "
            "fmt=%d, mh_size=%d, ext_time=%d, size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)", 
            fmt, mh_size, chunk->extended_timestamp, (chunk->msg? chunk->msg->size : 0), chunk->header.message_type, 
            chunk->header.payload_length, chunk->header.timestamp, chunk->header.stream_id);
    
    // read msg payload from chunk stream.
    SrsCommonMessage* msg = NULL;
    if ((ret = read_message_payload(chunk, &msg)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read message payload failed. ret=%d", ret);
        }
        return ret;
    }
    
    // not got an entire RTMP message, try next chunk.
    if (!msg) {
        srs_verbose("get partial message success. chunk_payload_size=%d, size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)",
                payload_size, (msg? msg->size : (chunk->msg? chunk->msg->size : 0)), chunk->header.message_type, chunk->header.payload_length,
                chunk->header.timestamp, chunk->header.stream_id);
        return ret;
    }
    
    *pmsg = msg;
    srs_info("get entire message success. chunk_payload_size=%d, size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)",
            payload_size, (msg? msg->size : (chunk->msg? chunk->msg->size : 0)), chunk->header.message_type, chunk->header.payload_length,
            chunk->header.timestamp, chunk->header.stream_id);
            
    return ret;
}

/**
* 6.1.1. Chunk Basic Header
* The Chunk Basic Header encodes the chunk stream ID and the chunk
* type(represented by fmt field in the figure below). Chunk type
* determines the format of the encoded message header. Chunk Basic
* Header field may be 1, 2, or 3 bytes, depending on the chunk stream
* ID.
* 
* The bits 0–5 (least significant) in the chunk basic header represent
* the chunk stream ID.
*
* Chunk stream IDs 2-63 can be encoded in the 1-byte version of this
* field.
*    0 1 2 3 4 5 6 7
*   +-+-+-+-+-+-+-+-+
*   |fmt|   cs id   |
*   +-+-+-+-+-+-+-+-+
*   Figure 6 Chunk basic header 1
*
* Chunk stream IDs 64-319 can be encoded in the 2-byte version of this
* field. ID is computed as (the second byte + 64).
*   0                   1
*   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
*   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*   |fmt|    0      | cs id - 64    |
*   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*   Figure 7 Chunk basic header 2
*
* Chunk stream IDs 64-65599 can be encoded in the 3-byte version of
* this field. ID is computed as ((the third byte)*256 + the second byte
* + 64).
*    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
*   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*   |fmt|     1     |         cs id - 64            |
*   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*   Figure 8 Chunk basic header 3
*
* cs id: 6 bits
* fmt: 2 bits
* cs id - 64: 8 or 16 bits
* 
* Chunk stream IDs with values 64-319 could be represented by both 2-
* byte version and 3-byte version of this field.
*/
int SrsProtocol::read_basic_header(char& fmt, int& cid)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = in_buffer->grow(skt, 1)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read 1bytes basic header failed. required_size=%d, ret=%d", 1, ret);
        }
        return ret;
    }
    
    fmt = in_buffer->read_1byte();
    cid = fmt & 0x3f;
    fmt = (fmt >> 6) & 0x03;
    
    // 2-63, 1B chunk header
    if (cid > 1) {
        srs_verbose("basic header parsed. fmt=%d, cid=%d", fmt, cid);
        return ret;
    }

    // 64-319, 2B chunk header
    if (cid == 0) {
        if ((ret = in_buffer->grow(skt, 1)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("read 2bytes basic header failed. required_size=%d, ret=%d", 1, ret);
            }
            return ret;
        }
        
        cid = 64;
        cid += (u_int8_t)in_buffer->read_1byte();
        srs_verbose("2bytes basic header parsed. fmt=%d, cid=%d", fmt, cid);
    // 64-65599, 3B chunk header
    } else if (cid == 1) {
        if ((ret = in_buffer->grow(skt, 2)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("read 3bytes basic header failed. required_size=%d, ret=%d", 2, ret);
            }
            return ret;
        }
        
        cid = 64;
        cid += (u_int8_t)in_buffer->read_1byte();
        cid += ((u_int8_t)in_buffer->read_1byte()) * 256;
        srs_verbose("3bytes basic header parsed. fmt=%d, cid=%d", fmt, cid);
    } else {
        srs_error("invalid path, impossible basic header.");
        srs_assert(false);
    }
    
    return ret;
}

/**
* parse the message header.
*   3bytes: timestamp delta,    fmt=0,1,2
*   3bytes: payload length,     fmt=0,1
*   1bytes: message type,       fmt=0,1
*   4bytes: stream id,          fmt=0
* where:
*   fmt=0, 0x0X
*   fmt=1, 0x4X
*   fmt=2, 0x8X
*   fmt=3, 0xCX
*/
int SrsProtocol::read_message_header(SrsChunkStream* chunk, char fmt)
{
    int ret = ERROR_SUCCESS;
    
    /**
    * we should not assert anything about fmt, for the first packet.
    * (when first packet, the chunk->msg is NULL).
    * the fmt maybe 0/1/2/3, the FMLE will send a 0xC4 for some audio packet.
    * the previous packet is:
    *     04                // fmt=0, cid=4
    *     00 00 1a          // timestamp=26
    *     00 00 9d          // payload_length=157
    *     08                // message_type=8(audio)
    *     01 00 00 00       // stream_id=1
    * the current packet maybe:
    *     c4             // fmt=3, cid=4
    * it's ok, for the packet is audio, and timestamp delta is 26.
    * the current packet must be parsed as:
    *     fmt=0, cid=4
    *     timestamp=26+26=52
    *     payload_length=157
    *     message_type=8(audio)
    *     stream_id=1
    * so we must update the timestamp even fmt=3 for first packet.
    */
    // fresh packet used to update the timestamp even fmt=3 for first packet.
    // fresh packet always means the chunk is the first one of message.
    bool is_first_chunk_of_msg = !chunk->msg;
    
    // but, we can ensure that when a chunk stream is fresh, 
    // the fmt must be 0, a new stream.
    if (chunk->msg_count == 0 && fmt != RTMP_FMT_TYPE0) {
        // for librtmp, if ping, it will send a fresh stream with fmt=1,
        // 0x42             where: fmt=1, cid=2, protocol contorl user-control message
        // 0x00 0x00 0x00   where: timestamp=0
        // 0x00 0x00 0x06   where: payload_length=6
        // 0x04             where: message_type=4(protocol control user-control message)
        // 0x00 0x06            where: event Ping(0x06)
        // 0x00 0x00 0x0d 0x0f  where: event data 4bytes ping timestamp.
        // @see: https://github.com/winlinvip/simple-rtmp-server/issues/98
        if (chunk->cid == RTMP_CID_ProtocolControl && fmt == RTMP_FMT_TYPE1) {
            srs_warn("accept cid=2, fmt=1 to make librtmp happy.");
        } else {
            // must be a RTMP protocol level error.
            ret = ERROR_RTMP_CHUNK_START;
            srs_error("chunk stream is fresh, fmt must be %d, actual is %d. cid=%d, ret=%d", 
                RTMP_FMT_TYPE0, fmt, chunk->cid, ret);
            return ret;
        }
    }

    // when exists cache msg, means got an partial message,
    // the fmt must not be type0 which means new message.
    if (chunk->msg && fmt == RTMP_FMT_TYPE0) {
        ret = ERROR_RTMP_CHUNK_START;
        srs_error("chunk stream exists, "
            "fmt must not be %d, actual is %d. ret=%d", RTMP_FMT_TYPE0, fmt, ret);
        return ret;
    }
    
    // create msg when new chunk stream start
    if (!chunk->msg) {
        chunk->msg = new SrsCommonMessage();
        srs_verbose("create message for new chunk, fmt=%d, cid=%d", fmt, chunk->cid);
    }

    // read message header from socket to buffer.
    static char mh_sizes[] = {11, 7, 3, 0};
    int mh_size = mh_sizes[(int)fmt];
    srs_verbose("calc chunk message header size. fmt=%d, mh_size=%d", fmt, mh_size);
    
    if (mh_size > 0 && (ret = in_buffer->grow(skt, mh_size)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read %dbytes message header failed. ret=%d", mh_size, ret);
        }
        return ret;
    }
    
    /**
    * parse the message header.
    *   3bytes: timestamp delta,    fmt=0,1,2
    *   3bytes: payload length,     fmt=0,1
    *   1bytes: message type,       fmt=0,1
    *   4bytes: stream id,          fmt=0
    * where:
    *   fmt=0, 0x0X
    *   fmt=1, 0x4X
    *   fmt=2, 0x8X
    *   fmt=3, 0xCX
    */
    // see also: ngx_rtmp_recv
    if (fmt <= RTMP_FMT_TYPE2) {
        char* p = in_buffer->read_slice(mh_size);
    
        char* pp = (char*)&chunk->header.timestamp_delta;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        pp[3] = 0;
        
        // fmt: 0
        // timestamp: 3 bytes
        // If the timestamp is greater than or equal to 16777215
        // (hexadecimal 0x00ffffff), this value MUST be 16777215, and the
        // ‘extended timestamp header’ MUST be present. Otherwise, this value
        // SHOULD be the entire timestamp.
        //
        // fmt: 1 or 2
        // timestamp delta: 3 bytes
        // If the delta is greater than or equal to 16777215 (hexadecimal
        // 0x00ffffff), this value MUST be 16777215, and the ‘extended
        // timestamp header’ MUST be present. Otherwise, this value SHOULD be
        // the entire delta.
        chunk->extended_timestamp = (chunk->header.timestamp_delta >= RTMP_EXTENDED_TIMESTAMP);
        if (!chunk->extended_timestamp) {
            // Extended timestamp: 0 or 4 bytes
            // This field MUST be sent when the normal timsestamp is set to
            // 0xffffff, it MUST NOT be sent if the normal timestamp is set to
            // anything else. So for values less than 0xffffff the normal
            // timestamp field SHOULD be used in which case the extended timestamp
            // MUST NOT be present. For values greater than or equal to 0xffffff
            // the normal timestamp field MUST NOT be used and MUST be set to
            // 0xffffff and the extended timestamp MUST be sent.
            if (fmt == RTMP_FMT_TYPE0) {
                // 6.1.2.1. Type 0
                // For a type-0 chunk, the absolute timestamp of the message is sent
                // here.
                chunk->header.timestamp = chunk->header.timestamp_delta;
            } else {
                // 6.1.2.2. Type 1
                // 6.1.2.3. Type 2
                // For a type-1 or type-2 chunk, the difference between the previous
                // chunk's timestamp and the current chunk's timestamp is sent here.
                chunk->header.timestamp += chunk->header.timestamp_delta;
            }
        }
        
        if (fmt <= RTMP_FMT_TYPE1) {
            int32_t payload_length = 0;
            pp = (char*)&payload_length;
            pp[2] = *p++;
            pp[1] = *p++;
            pp[0] = *p++;
            pp[3] = 0;
            
            // for a message, if msg exists in cache, the size must not changed.
            // always use the actual msg size to compare, for the cache payload length can changed,
            // for the fmt type1(stream_id not changed), user can change the payload 
            // length(it's not allowed in the continue chunks).
            if (!is_first_chunk_of_msg && chunk->header.payload_length != payload_length) {
                ret = ERROR_RTMP_PACKET_SIZE;
                srs_error("msg exists in chunk cache, "
                    "size=%d cannot change to %d, ret=%d", 
                    chunk->header.payload_length, payload_length, ret);
                return ret;
            }
            
            chunk->header.payload_length = payload_length;
            chunk->header.message_type = *p++;
            
            if (fmt == RTMP_FMT_TYPE0) {
                pp = (char*)&chunk->header.stream_id;
                pp[0] = *p++;
                pp[1] = *p++;
                pp[2] = *p++;
                pp[3] = *p++;
                srs_verbose("header read completed. fmt=%d, mh_size=%d, ext_time=%d, time=%"PRId64", payload=%d, type=%d, sid=%d", 
                    fmt, mh_size, chunk->extended_timestamp, chunk->header.timestamp, chunk->header.payload_length, 
                    chunk->header.message_type, chunk->header.stream_id);
            } else {
                srs_verbose("header read completed. fmt=%d, mh_size=%d, ext_time=%d, time=%"PRId64", payload=%d, type=%d", 
                    fmt, mh_size, chunk->extended_timestamp, chunk->header.timestamp, chunk->header.payload_length, 
                    chunk->header.message_type);
            }
        } else {
            srs_verbose("header read completed. fmt=%d, mh_size=%d, ext_time=%d, time=%"PRId64"", 
                fmt, mh_size, chunk->extended_timestamp, chunk->header.timestamp);
        }
    } else {
        // update the timestamp even fmt=3 for first chunk packet
        if (is_first_chunk_of_msg && !chunk->extended_timestamp) {
            chunk->header.timestamp += chunk->header.timestamp_delta;
        }
        srs_verbose("header read completed. fmt=%d, size=%d, ext_time=%d", 
            fmt, mh_size, chunk->extended_timestamp);
    }
    
    // read extended-timestamp
    if (chunk->extended_timestamp) {
        mh_size += 4;
        srs_verbose("read header ext time. fmt=%d, ext_time=%d, mh_size=%d", fmt, chunk->extended_timestamp, mh_size);
        if ((ret = in_buffer->grow(skt, 4)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("read %dbytes message header failed. required_size=%d, ret=%d", mh_size, 4, ret);
            }
            return ret;
        }
        // the ptr to the slice maybe invalid when grow()
        // reset the p to get 4bytes slice.
        char* p = in_buffer->read_slice(4);

        u_int32_t timestamp = 0x00;
        char* pp = (char*)&timestamp;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;

        // always use 31bits timestamp, for some server may use 32bits extended timestamp.
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/111
        timestamp &= 0x7fffffff;
        
        /**
        * RTMP specification and ffmpeg/librtmp is false,
        * but, adobe changed the specification, so flash/FMLE/FMS always true.
        * default to true to support flash/FMLE/FMS.
        * 
        * ffmpeg/librtmp may donot send this filed, need to detect the value.
        * @see also: http://blog.csdn.net/win_lin/article/details/13363699
        * compare to the chunk timestamp, which is set by chunk message header
        * type 0,1 or 2.
        *
        * @remark, nginx send the extended-timestamp in sequence-header,
        * and timestamp delta in continue C1 chunks, and so compatible with ffmpeg,
        * that is, there is no continue chunks and extended-timestamp in nginx-rtmp.
        *
        * @remark, srs always send the extended-timestamp, to keep simple,
        * and compatible with adobe products.
        */
        u_int32_t chunk_timestamp = (u_int32_t)chunk->header.timestamp;
        
        /**
        * if chunk_timestamp<=0, the chunk previous packet has no extended-timestamp,
        * always use the extended timestamp.
        */
        /**
        * about the is_first_chunk_of_msg.
        * @remark, for the first chunk of message, always use the extended timestamp.
        */
        if (!is_first_chunk_of_msg && chunk_timestamp > 0 && chunk_timestamp != timestamp) {
            mh_size -= 4;
            in_buffer->skip(-4);
            srs_info("no 4bytes extended timestamp in the continued chunk");
        } else {
            chunk->header.timestamp = timestamp;
        }
        srs_verbose("header read ext_time completed. time=%"PRId64"", chunk->header.timestamp);
    }
    
    // the extended-timestamp must be unsigned-int,
    //         24bits timestamp: 0xffffff = 16777215ms = 16777.215s = 4.66h
    //         32bits timestamp: 0xffffffff = 4294967295ms = 4294967.295s = 1193.046h = 49.71d
    // because the rtmp protocol says the 32bits timestamp is about "50 days":
    //         3. Byte Order, Alignment, and Time Format
    //                Because timestamps are generally only 32 bits long, they will roll
    //                over after fewer than 50 days.
    // 
    // but, its sample says the timestamp is 31bits:
    //         An application could assume, for example, that all 
    //        adjacent timestamps are within 2^31 milliseconds of each other, so
    //        10000 comes after 4000000000, while 3000000000 comes before
    //        4000000000.
    // and flv specification says timestamp is 31bits:
    //        Extension of the Timestamp field to form a SI32 value. This
    //        field represents the upper 8 bits, while the previous
    //        Timestamp field represents the lower 24 bits of the time in
    //        milliseconds.
    // in a word, 31bits timestamp is ok.
    // convert extended timestamp to 31bits.
    chunk->header.timestamp &= 0x7fffffff;
    
    // valid message, the payload_length is 24bits,
    // so it should never be negative.
    srs_assert(chunk->header.payload_length >= 0);
    
    // copy header to msg
    chunk->msg->header = chunk->header;
    
    // increase the msg count, the chunk stream can accept fmt=1/2/3 message now.
    chunk->msg_count++;
    
    return ret;
}

int SrsProtocol::read_message_payload(SrsChunkStream* chunk, SrsCommonMessage** pmsg)
{
    int ret = ERROR_SUCCESS;
    
    // empty message
    if (chunk->header.payload_length <= 0) {
        srs_trace("get an empty RTMP "
                "message(type=%d, size=%d, time=%"PRId64", sid=%d)", chunk->header.message_type, 
                chunk->header.payload_length, chunk->header.timestamp, chunk->header.stream_id);
        
        *pmsg = chunk->msg;
        chunk->msg = NULL;
                
        return ret;
    }
    srs_assert(chunk->header.payload_length > 0);
    
    // the chunk payload size.
    int payload_size = chunk->header.payload_length - chunk->msg->size;
    payload_size = srs_min(payload_size, in_chunk_size);
    srs_verbose("chunk payload size is %d, message_size=%d, received_size=%d, in_chunk_size=%d", 
        payload_size, chunk->header.payload_length, chunk->msg->size, in_chunk_size);

    // create msg payload if not initialized
    if (!chunk->msg->payload) {
        chunk->msg->payload = new char[chunk->header.payload_length];
        srs_verbose("create payload for RTMP message. size=%d", chunk->header.payload_length);
    }
    
    // read payload to buffer
    if ((ret = in_buffer->grow(skt, payload_size)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read payload failed. required_size=%d, ret=%d", payload_size, ret);
        }
        return ret;
    }
    memcpy(chunk->msg->payload + chunk->msg->size, in_buffer->read_slice(payload_size), payload_size);
    chunk->msg->size += payload_size;
    
    srs_verbose("chunk payload read completed. payload_size=%d", payload_size);
    
    // got entire RTMP message?
    if (chunk->header.payload_length == chunk->msg->size) {
        *pmsg = chunk->msg;
        chunk->msg = NULL;
        srs_verbose("get entire RTMP message(type=%d, size=%d, time=%"PRId64", sid=%d)", 
                chunk->header.message_type, chunk->header.payload_length, 
                chunk->header.timestamp, chunk->header.stream_id);
        return ret;
    }
    
    srs_verbose("get partial RTMP message(type=%d, size=%d, time=%"PRId64", sid=%d), partial size=%d", 
            chunk->header.message_type, chunk->header.payload_length, 
            chunk->header.timestamp, chunk->header.stream_id,
            chunk->msg->size);
            
    return ret;
}

int SrsProtocol::on_recv_message(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(msg != NULL);
        
    // acknowledgement
    if (in_ack_size.ack_window_size > 0 
        && skt->get_recv_bytes() - in_ack_size.acked_size > in_ack_size.ack_window_size
    ) {
        if ((ret = response_acknowledgement_message()) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    SrsPacket* packet = NULL;
    switch (msg->header.message_type) {
        case RTMP_MSG_SetChunkSize:
        case RTMP_MSG_UserControlMessage:
        case RTMP_MSG_WindowAcknowledgementSize:
            if ((ret = decode_message(msg, &packet)) != ERROR_SUCCESS) {
                srs_error("decode packet from message payload failed. ret=%d", ret);
                return ret;
            }
            srs_verbose("decode packet from message payload success.");
            break;
        default:
            return ret;
    }
    
    srs_assert(packet);
    
    // always free the packet.
    SrsAutoFree(SrsPacket, packet);
    
    switch (msg->header.message_type) {
        case RTMP_MSG_WindowAcknowledgementSize: {
            SrsSetWindowAckSizePacket* pkt = dynamic_cast<SrsSetWindowAckSizePacket*>(packet);
            srs_assert(pkt != NULL);
            
            if (pkt->ackowledgement_window_size > 0) {
                in_ack_size.ack_window_size = pkt->ackowledgement_window_size;
                // @remakr, we ignore this message, for user noneed to care.
                // but it's important for dev, for client/server will block if required 
                // ack msg not arrived.
                srs_info("set ack window size to %d", pkt->ackowledgement_window_size);
            } else {
                srs_warn("ignored. set ack window size is %d", pkt->ackowledgement_window_size);
            }
            break;
        }
        case RTMP_MSG_SetChunkSize: {
            SrsSetChunkSizePacket* pkt = dynamic_cast<SrsSetChunkSizePacket*>(packet);
            srs_assert(pkt != NULL);

            // for some server, the actual chunk size can greater than the max value(65536),
            // so we just warning the invalid chunk size, and actually use it is ok,
            // @see: https://github.com/winlinvip/simple-rtmp-server/issues/160
            if (pkt->chunk_size < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE 
                || pkt->chunk_size > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE) 
            {
                srs_warn("accept chunk size %d, but should in [%d, %d], "
                    "@see: https://github.com/winlinvip/simple-rtmp-server/issues/160",
                    pkt->chunk_size, SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, 
                    SRS_CONSTS_RTMP_MAX_CHUNK_SIZE);
            }

            in_chunk_size = pkt->chunk_size;
            srs_trace("input chunk size to %d", pkt->chunk_size);

            break;
        }
        case RTMP_MSG_UserControlMessage: {
            SrsUserControlPacket* pkt = dynamic_cast<SrsUserControlPacket*>(packet);
            srs_assert(pkt != NULL);
            
            if (pkt->event_type == SrcPCUCSetBufferLength) {
                srs_trace("ignored. set buffer length to %d", pkt->extra_data);
            }
            if (pkt->event_type == SrcPCUCPingRequest) {
                if ((ret = response_ping_message(pkt->event_data)) != ERROR_SUCCESS) {
                    return ret;
                }
            }
            break;
        }
        default:
            break;
    }
    
    return ret;
}

int SrsProtocol::on_send_packet(SrsMessageHeader* mh, SrsPacket* packet)
{
    int ret = ERROR_SUCCESS;
    
    // ignore raw bytes oriented RTMP message.
    if (packet == NULL) {
        return ret;
    }
    
    switch (mh->message_type) {
        case RTMP_MSG_SetChunkSize: {
            SrsSetChunkSizePacket* pkt = dynamic_cast<SrsSetChunkSizePacket*>(packet);
            srs_assert(pkt != NULL);
            
            out_chunk_size = pkt->chunk_size;
            
            srs_trace("out chunk size to %d", pkt->chunk_size);
            break;
        }
        case RTMP_MSG_AMF0CommandMessage:
        case RTMP_MSG_AMF3CommandMessage: {
            if (true) {
                SrsConnectAppPacket* pkt = dynamic_cast<SrsConnectAppPacket*>(packet);
                if (pkt) {
                    requests[pkt->transaction_id] = pkt->command_name;
                    break;
                }
            }
            if (true) {
                SrsCreateStreamPacket* pkt = dynamic_cast<SrsCreateStreamPacket*>(packet);
                if (pkt) {
                    requests[pkt->transaction_id] = pkt->command_name;
                    break;
                }
            }
            if (true) {
                SrsFMLEStartPacket* pkt = dynamic_cast<SrsFMLEStartPacket*>(packet);
                if (pkt) {
                    requests[pkt->transaction_id] = pkt->command_name;
                    break;
                }
            }
            break;
        }
        default:
            break;
    }
    
    return ret;
}

int SrsProtocol::response_acknowledgement_message()
{
    int ret = ERROR_SUCCESS;
    
    SrsAcknowledgementPacket* pkt = new SrsAcknowledgementPacket();
    in_ack_size.acked_size = skt->get_recv_bytes();
    pkt->sequence_number = (int32_t)in_ack_size.acked_size;
    
    // cache the message and use flush to send.
    if (!auto_response_when_recv) {
        manual_response_queue.push_back(pkt);
        return ret;
    }
    
    // use underlayer api to send, donot flush again.
    if ((ret = do_send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send acknowledgement failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("send acknowledgement success.");
    
    return ret;
}

int SrsProtocol::response_ping_message(int32_t timestamp)
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("get a ping request, response it. timestamp=%d", timestamp);
    
    SrsUserControlPacket* pkt = new SrsUserControlPacket();
    
    pkt->event_type = SrcPCUCPingResponse;
    pkt->event_data = timestamp;
    
    // cache the message and use flush to send.
    if (!auto_response_when_recv) {
        manual_response_queue.push_back(pkt);
        return ret;
    }
    
    // use underlayer api to send, donot flush again.
    if ((ret = do_send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send ping response failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("send ping response success.");
    
    return ret;
}

SrsChunkStream::SrsChunkStream(int _cid)
{
    fmt = 0;
    cid = _cid;
    extended_timestamp = false;
    msg = NULL;
    msg_count = 0;
}

SrsChunkStream::~SrsChunkStream()
{
    srs_freep(msg);
}

SrsPacket::SrsPacket()
{
}

SrsPacket::~SrsPacket()
{
}

int SrsPacket::encode(int& psize, char*& ppayload)
{
    int ret = ERROR_SUCCESS;
    
    int size = get_size();
    char* payload = NULL;
    
    SrsStream stream;
    
    if (size > 0) {
        payload = new char[size];
        
        if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
            srs_error("initialize the stream failed. ret=%d", ret);
            srs_freep(payload);
            return ret;
        }
    }
    
    if ((ret = encode_packet(&stream)) != ERROR_SUCCESS) {
        srs_error("encode the packet failed. ret=%d", ret);
        srs_freep(payload);
        return ret;
    }
    
    psize = size;
    ppayload = payload;
    srs_verbose("encode the packet success. size=%d", size);
    
    return ret;
}

int SrsPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(stream != NULL);

    ret = ERROR_SYSTEM_PACKET_INVALID;
    srs_error("current packet is not support to decode. ret=%d", ret);
    
    return ret;
}

int SrsPacket::get_prefer_cid()
{
    return 0;
}

int SrsPacket::get_message_type()
{
    return 0;
}

int SrsPacket::get_size()
{
    return 0;
}

int SrsPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(stream != NULL);

    ret = ERROR_SYSTEM_PACKET_INVALID;
    srs_error("current packet is not support to encode. ret=%d", ret);
    
    return ret;
}

SrsConnectAppPacket::SrsConnectAppPacket()
{
    command_name = RTMP_AMF0_COMMAND_CONNECT;
    transaction_id = 1;
    command_object = SrsAmf0Any::object();
    // optional
    args = NULL;
}

SrsConnectAppPacket::~SrsConnectAppPacket()
{
    srs_freep(command_object);
    srs_freep(args);
}

int SrsConnectAppPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_CONNECT) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode connect command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    // some client donot send id=1.0, so we only warn user if not match.
    if (transaction_id != 1.0) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_warn("amf0 decode connect transaction_id failed. "
            "required=%.1f, actual=%.1f, ret=%d", 1.0, transaction_id, ret);
        ret = ERROR_SUCCESS;
    }
    
    if ((ret = command_object->read(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect command_object failed. ret=%d", ret);
        return ret;
    }
    
    if (!stream->empty()) {
        srs_freep(args);
        
        // see: https://github.com/winlinvip/simple-rtmp-server/issues/186
        // the args maybe any amf0, for instance, a string. we should drop if not object.
        SrsAmf0Any* any = NULL;
        if ((ret = SrsAmf0Any::discovery(stream, &any)) != ERROR_SUCCESS) {
            srs_error("amf0 find connect args failed. ret=%d", ret);
            return ret;
        }
        srs_assert(any);
        
        // read the instance
        if ((ret = any->read(stream)) != ERROR_SUCCESS) {
            srs_error("amf0 decode connect args failed. ret=%d", ret);
            srs_freep(any);
            return ret;
        }
        
        // drop when not an AMF0 object.
        if (!any->is_object()) {
            srs_warn("drop the args, see: '4.1.1. connect', marker=%#x", any->marker);
            srs_freep(any);
        } else {
            args = any->to_object();
        }
    }
    
    srs_info("amf0 decode connect packet success");
    
    return ret;
}

int SrsConnectAppPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsConnectAppPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsConnectAppPacket::get_size()
{
    int size = 0;
    
    size += SrsAmf0Size::str(command_name);
    size += SrsAmf0Size::number();
    size += SrsAmf0Size::object(command_object);
    if (args) {
        size += SrsAmf0Size::object(args);
    }
    
    return size;
}

int SrsConnectAppPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = command_object->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if (args && (ret = args->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode args failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode args success.");
    
    srs_info("encode connect app request packet success.");
    
    return ret;
}

SrsConnectAppResPacket::SrsConnectAppResPacket()
{
    command_name = RTMP_AMF0_COMMAND_RESULT;
    transaction_id = 1;
    props = SrsAmf0Any::object();
    info = SrsAmf0Any::object();
}

SrsConnectAppResPacket::~SrsConnectAppResPacket()
{
    srs_freep(props);
    srs_freep(info);
}

int SrsConnectAppResPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode connect command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    // some client donot send id=1.0, so we only warn user if not match.
    if (transaction_id != 1.0) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_warn("amf0 decode connect transaction_id failed. "
            "required=%.1f, actual=%.1f, ret=%d", 1.0, transaction_id, ret);
        ret = ERROR_SUCCESS;
    }
    
    if ((ret = props->read(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect props failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = info->read(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode connect info failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode connect response packet success");
    
    return ret;
}

int SrsConnectAppResPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsConnectAppResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsConnectAppResPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number() 
        + SrsAmf0Size::object(props) + SrsAmf0Size::object(info);
}

int SrsConnectAppResPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = props->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode props failed. ret=%d", ret);
        return ret;
    }

    srs_verbose("encode props success.");
    
    if ((ret = info->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode info failed. ret=%d", ret);
        return ret;
    }

    srs_verbose("encode info success.");
    
    srs_info("encode connect app response packet success.");
    
    return ret;
}

SrsCallPacket::SrsCallPacket()
{
    command_name = "";
    transaction_id = 0;
    command_object = NULL;
    arguments = NULL;
}

SrsCallPacket::~SrsCallPacket()
{
    srs_freep(command_object);
    srs_freep(arguments);
}

int SrsCallPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode call command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty()) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode call command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode call transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    srs_freep(command_object);
    if ((ret = SrsAmf0Any::discovery(stream, &command_object)) != ERROR_SUCCESS) {
        srs_error("amf0 discovery call command_object failed. ret=%d", ret);
        return ret;
    }
    if ((ret = command_object->read(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode call command_object failed. ret=%d", ret);
        return ret;
    }
    
    if (!stream->empty()) {
        srs_freep(arguments);
        if ((ret = SrsAmf0Any::discovery(stream, &arguments)) != ERROR_SUCCESS) {
            srs_error("amf0 discovery call arguments failed. ret=%d", ret);
            return ret;
        }
        if ((ret = arguments->read(stream)) != ERROR_SUCCESS) {
            srs_error("amf0 decode call arguments failed. ret=%d", ret);
            return ret;
        }
    }
    
    srs_info("amf0 decode call packet success");
    
    return ret;
}

int SrsCallPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsCallPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCallPacket::get_size()
{
    int size = 0;
    
    size += SrsAmf0Size::str(command_name) + SrsAmf0Size::number();
    
    if (command_object) {
        size += command_object->total_size();
    }
    
    if (arguments) {
        size += arguments->total_size();
    }
    
    return size;
}

int SrsCallPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if (command_object && (ret = command_object->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if (arguments && (ret = arguments->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode arguments failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode arguments success.");
    
    srs_info("encode create stream request packet success.");
    
    return ret;
}

SrsCallResPacket::SrsCallResPacket(double _transaction_id)
{
    command_name = RTMP_AMF0_COMMAND_RESULT;
    transaction_id = _transaction_id;
    command_object = NULL;
    response = NULL;
}

SrsCallResPacket::~SrsCallResPacket()
{
    srs_freep(command_object);
    srs_freep(response);
}

int SrsCallResPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsCallResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCallResPacket::get_size()
{
    int size = 0;
    
    size += SrsAmf0Size::str(command_name) + SrsAmf0Size::number();
    
    if (command_object) {
        size += command_object->total_size();
    }
    
    if (response) {
        size += response->total_size();
    }
    
    return size;
}

int SrsCallResPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if (command_object && (ret = command_object->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if (response && (ret = response->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode response failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode response success.");
    
    
    srs_info("encode call response packet success.");
    
    return ret;
}

SrsCreateStreamPacket::SrsCreateStreamPacket()
{
    command_name = RTMP_AMF0_COMMAND_CREATE_STREAM;
    transaction_id = 2;
    command_object = SrsAmf0Any::null();
}

SrsCreateStreamPacket::~SrsCreateStreamPacket()
{
    srs_freep(command_object);
}

int SrsCreateStreamPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_CREATE_STREAM) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode createStream command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream command_object failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode createStream packet success");
    
    return ret;
}

int SrsCreateStreamPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsCreateStreamPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCreateStreamPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null();
}

int SrsCreateStreamPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    srs_info("encode create stream request packet success.");
    
    return ret;
}

SrsCreateStreamResPacket::SrsCreateStreamResPacket(double _transaction_id, double _stream_id)
{
    command_name = RTMP_AMF0_COMMAND_RESULT;
    transaction_id = _transaction_id;
    command_object = SrsAmf0Any::null();
    stream_id = _stream_id;
}

SrsCreateStreamResPacket::~SrsCreateStreamResPacket()
{
    srs_freep(command_object);
}

int SrsCreateStreamResPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode createStream command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream command_object failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode createStream stream_id failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode createStream response packet success");
    
    return ret;
}

int SrsCreateStreamResPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsCreateStreamResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCreateStreamResPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::number();
}

int SrsCreateStreamResPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if ((ret = srs_amf0_write_number(stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("encode stream_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode stream_id success.");
    
    
    srs_info("encode createStream response packet success.");
    
    return ret;
}

SrsCloseStreamPacket::SrsCloseStreamPacket()
{
    command_name = RTMP_AMF0_COMMAND_CLOSE_STREAM;
    transaction_id = 0;
    command_object = SrsAmf0Any::null();
}

SrsCloseStreamPacket::~SrsCloseStreamPacket()
{
    srs_freep(command_object);
}

int SrsCloseStreamPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode closeStream command_name failed. ret=%d", ret);
        return ret;
    }

    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode closeStream transaction_id failed. ret=%d", ret);
        return ret;
    }

    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode closeStream command_object failed. ret=%d", ret);
        return ret;
    }
    srs_info("amf0 decode closeStream packet success");

    return ret;
}

SrsFMLEStartPacket::SrsFMLEStartPacket()
{
    command_name = RTMP_AMF0_COMMAND_RELEASE_STREAM;
    transaction_id = 0;
    command_object = SrsAmf0Any::null();
}

SrsFMLEStartPacket::~SrsFMLEStartPacket()
{
    srs_freep(command_object);
}

int SrsFMLEStartPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() 
        || (command_name != RTMP_AMF0_COMMAND_RELEASE_STREAM 
        && command_name != RTMP_AMF0_COMMAND_FC_PUBLISH
        && command_name != RTMP_AMF0_COMMAND_UNPUBLISH)
    ) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode FMLE start command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start command_object failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_string(stream, stream_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start stream_name failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode FMLE start packet success");
    
    return ret;
}

int SrsFMLEStartPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsFMLEStartPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsFMLEStartPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name);
}

int SrsFMLEStartPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if ((ret = srs_amf0_write_string(stream, stream_name)) != ERROR_SUCCESS) {
        srs_error("encode stream_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode stream_name success.");
    
    
    srs_info("encode FMLE start response packet success.");
    
    return ret;
}

SrsFMLEStartPacket* SrsFMLEStartPacket::create_release_stream(string stream)
{
    SrsFMLEStartPacket* pkt = new SrsFMLEStartPacket();
    
    pkt->command_name = RTMP_AMF0_COMMAND_RELEASE_STREAM;
    pkt->transaction_id = 2;
    pkt->stream_name = stream;
    
    return pkt;
}

SrsFMLEStartPacket* SrsFMLEStartPacket::create_FC_publish(string stream)
{
    SrsFMLEStartPacket* pkt = new SrsFMLEStartPacket();
    
    pkt->command_name = RTMP_AMF0_COMMAND_FC_PUBLISH;
    pkt->transaction_id = 3;
    pkt->stream_name = stream;
    
    return pkt;
}

SrsFMLEStartResPacket::SrsFMLEStartResPacket(double _transaction_id)
{
    command_name = RTMP_AMF0_COMMAND_RESULT;
    transaction_id = _transaction_id;
    command_object = SrsAmf0Any::null();
    args = SrsAmf0Any::undefined();
}

SrsFMLEStartResPacket::~SrsFMLEStartResPacket()
{
    srs_freep(command_object);
    srs_freep(args);
}

int SrsFMLEStartResPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start response command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode FMLE start response command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start response transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start response command_object failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_undefined(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode FMLE start response stream_id failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode FMLE start packet success");
    
    return ret;
}

int SrsFMLEStartResPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsFMLEStartResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsFMLEStartResPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::undefined();
}

int SrsFMLEStartResPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if ((ret = srs_amf0_write_undefined(stream)) != ERROR_SUCCESS) {
        srs_error("encode args failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode args success.");
    
    
    srs_info("encode FMLE start response packet success.");
    
    return ret;
}

SrsPublishPacket::SrsPublishPacket()
{
    command_name = RTMP_AMF0_COMMAND_PUBLISH;
    transaction_id = 0;
    command_object = SrsAmf0Any::null();
    type = "live";
}

SrsPublishPacket::~SrsPublishPacket()
{
    srs_freep(command_object);
}

int SrsPublishPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode publish command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PUBLISH) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode publish command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode publish transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode publish command_object failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_string(stream, stream_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode publish stream_name failed. ret=%d", ret);
        return ret;
    }
    
    if (!stream->empty() && (ret = srs_amf0_read_string(stream, type)) != ERROR_SUCCESS) {
        srs_error("amf0 decode publish type failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode publish packet success");
    
    return ret;
}

int SrsPublishPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsPublishPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsPublishPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name)
        + SrsAmf0Size::str(type);
}

int SrsPublishPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if ((ret = srs_amf0_write_string(stream, stream_name)) != ERROR_SUCCESS) {
        srs_error("encode stream_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode stream_name success.");
    
    if ((ret = srs_amf0_write_string(stream, type)) != ERROR_SUCCESS) {
        srs_error("encode type failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode type success.");
    
    srs_info("encode play request packet success.");
    
    return ret;
}

SrsPausePacket::SrsPausePacket()
{
    command_name = RTMP_AMF0_COMMAND_PAUSE;
    transaction_id = 0;
    command_object = SrsAmf0Any::null();

    time_ms = 0;
    is_pause = true;
}

SrsPausePacket::~SrsPausePacket()
{
    srs_freep(command_object);
}

int SrsPausePacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode pause command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PAUSE) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode pause command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode pause transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode pause command_object failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_boolean(stream, is_pause)) != ERROR_SUCCESS) {
        srs_error("amf0 decode pause is_pause failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, time_ms)) != ERROR_SUCCESS) {
        srs_error("amf0 decode pause time_ms failed. ret=%d", ret);
        return ret;
    }
    
    srs_info("amf0 decode pause packet success");
    
    return ret;
}

SrsPlayPacket::SrsPlayPacket()
{
    command_name = RTMP_AMF0_COMMAND_PLAY;
    transaction_id = 0;
    command_object = SrsAmf0Any::null();

    start = -2;
    duration = -1;
    reset = true;
}

SrsPlayPacket::~SrsPlayPacket()
{
    srs_freep(command_object);
}

int SrsPlayPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play command_name failed. ret=%d", ret);
        return ret;
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PLAY) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 decode play command_name failed. "
            "command_name=%s, ret=%d", command_name.c_str(), ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play transaction_id failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play command_object failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = srs_amf0_read_string(stream, stream_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play stream_name failed. ret=%d", ret);
        return ret;
    }
    
    if (!stream->empty() && (ret = srs_amf0_read_number(stream, start)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play start failed. ret=%d", ret);
        return ret;
    }
    if (!stream->empty() && (ret = srs_amf0_read_number(stream, duration)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play duration failed. ret=%d", ret);
        return ret;
    }

    if (stream->empty()) {
        return ret;
    }
    
    SrsAmf0Any* reset_value = NULL;
    if ((ret = srs_amf0_read_any(stream, &reset_value)) != ERROR_SUCCESS) {
        ret = ERROR_RTMP_AMF0_DECODE;
        srs_error("amf0 read play reset marker failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsAmf0Any, reset_value);
    
    if (reset_value) {
        // check if the value is bool or number
        // An optional Boolean value or number that specifies whether
        // to flush any previous playlist
        if (reset_value->is_boolean()) {
            reset = reset_value->to_boolean();
        } else if (reset_value->is_number()) {
            reset = (reset_value->to_number() != 0);
        } else {
            ret = ERROR_RTMP_AMF0_DECODE;
            srs_error("amf0 invalid type=%#x, requires number or bool, ret=%d", reset_value->marker, ret);
            return ret;
        }
    }

    srs_info("amf0 decode play packet success");
    
    return ret;
}

int SrsPlayPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsPlayPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsPlayPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name)
        + SrsAmf0Size::number() + SrsAmf0Size::number()
        + SrsAmf0Size::boolean();
}

int SrsPlayPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if ((ret = srs_amf0_write_string(stream, stream_name)) != ERROR_SUCCESS) {
        srs_error("encode stream_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode stream_name success.");
    
    if ((ret = srs_amf0_write_number(stream, start)) != ERROR_SUCCESS) {
        srs_error("encode start failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode start success.");
    
    if ((ret = srs_amf0_write_number(stream, duration)) != ERROR_SUCCESS) {
        srs_error("encode duration failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode duration success.");
    
    if ((ret = srs_amf0_write_boolean(stream, reset)) != ERROR_SUCCESS) {
        srs_error("encode reset failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode reset success.");
    
    srs_info("encode play request packet success.");
    
    return ret;
}

SrsPlayResPacket::SrsPlayResPacket()
{
    command_name = RTMP_AMF0_COMMAND_RESULT;
    transaction_id = 0;
    command_object = SrsAmf0Any::null();
    desc = SrsAmf0Any::object();
}

SrsPlayResPacket::~SrsPlayResPacket()
{
    srs_freep(command_object);
    srs_freep(desc);
}

int SrsPlayResPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsPlayResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsPlayResPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::object(desc);
}

int SrsPlayResPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode command_object failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_object success.");
    
    if ((ret = desc->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode desc failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode desc success.");
    
    
    srs_info("encode play response packet success.");
    
    return ret;
}

SrsOnBWDonePacket::SrsOnBWDonePacket()
{
    command_name = RTMP_AMF0_COMMAND_ON_BW_DONE;
    transaction_id = 0;
    args = SrsAmf0Any::null();
}

SrsOnBWDonePacket::~SrsOnBWDonePacket()
{
    srs_freep(args);
}

int SrsOnBWDonePacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsOnBWDonePacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsOnBWDonePacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null();
}

int SrsOnBWDonePacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode args failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode args success.");
    
    srs_info("encode onBWDone packet success.");
    
    return ret;
}

SrsOnStatusCallPacket::SrsOnStatusCallPacket()
{
    command_name = RTMP_AMF0_COMMAND_ON_STATUS;
    transaction_id = 0;
    args = SrsAmf0Any::null();
    data = SrsAmf0Any::object();
}

SrsOnStatusCallPacket::~SrsOnStatusCallPacket()
{
    srs_freep(args);
    srs_freep(data);
}

int SrsOnStatusCallPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsOnStatusCallPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsOnStatusCallPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::object(data);
}

int SrsOnStatusCallPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode args failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode args success.");;
    
    if ((ret = data->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode data failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode data success.");
    
    srs_info("encode onStatus(Call) packet success.");
    
    return ret;
}

SrsBandwidthPacket::SrsBandwidthPacket()
{
    command_name = RTMP_AMF0_COMMAND_ON_STATUS;
    transaction_id = 0;
    args = SrsAmf0Any::null();
    data = SrsAmf0Any::object();
}

SrsBandwidthPacket::~SrsBandwidthPacket()
{
    srs_freep(args);
    srs_freep(data);
}

int SrsBandwidthPacket::decode(SrsStream *stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode bwtc command_name failed. ret=%d", ret);
        return ret;
    }

    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("amf0 decode bwtc transaction_id failed. ret=%d", ret);
        return ret;
    }

    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        srs_error("amf0 decode bwtc command_object failed. ret=%d", ret);
        return ret;
    }
    
    // @remark, for bandwidth test, ignore the data field.
    // only decode the stop-play, start-publish and finish packet.
    if (is_stop_play() || is_start_publish() || is_finish()) {
        if ((ret = data->read(stream)) != ERROR_SUCCESS) {
            srs_error("amf0 decode bwtc command_object failed. ret=%d", ret);
            return ret;
        }
    }

    srs_info("decode SrsBandwidthPacket success.");

    return ret;
}

int SrsBandwidthPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsBandwidthPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsBandwidthPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::object(data);
}

int SrsBandwidthPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        srs_error("encode transaction_id failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode transaction_id success.");
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        srs_error("encode args failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode args success.");;
    
    if ((ret = data->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode data failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode data success.");
    
    srs_info("encode onStatus(Call) packet success.");
    
    return ret;
}

bool SrsBandwidthPacket::is_start_play()
{
    return command_name == SRS_BW_CHECK_START_PLAY;
}

bool SrsBandwidthPacket::is_starting_play()
{
    return command_name == SRS_BW_CHECK_STARTING_PLAY;
}

bool SrsBandwidthPacket::is_stop_play()
{
    return command_name == SRS_BW_CHECK_STOP_PLAY;
}

bool SrsBandwidthPacket::is_stopped_play()
{
    return command_name == SRS_BW_CHECK_STOPPED_PLAY;
}

bool SrsBandwidthPacket::is_start_publish()
{
    return command_name == SRS_BW_CHECK_START_PUBLISH;
}

bool SrsBandwidthPacket::is_starting_publish()
{
    return command_name == SRS_BW_CHECK_STARTING_PUBLISH;
}

bool SrsBandwidthPacket::is_stop_publish()
{
    return command_name == SRS_BW_CHECK_STOP_PUBLISH;
}

bool SrsBandwidthPacket::is_stopped_publish()
{
    return command_name == SRS_BW_CHECK_STOPPED_PUBLISH;
}

bool SrsBandwidthPacket::is_finish()
{
    return command_name == SRS_BW_CHECK_FINISHED;
}

bool SrsBandwidthPacket::is_final()
{
    return command_name == SRS_BW_CHECK_FINAL;
}

SrsBandwidthPacket* SrsBandwidthPacket::create_start_play()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_START_PLAY);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_starting_play()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STARTING_PLAY);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_playing()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_PLAYING);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_stop_play()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STOP_PLAY);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_stopped_play()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STOPPED_PLAY);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_start_publish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_START_PUBLISH);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_starting_publish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STARTING_PUBLISH);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_publishing()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_PUBLISHING);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_stop_publish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STOP_PUBLISH);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_stopped_publish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STOPPED_PUBLISH);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_finish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_FINISHED);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_final()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_FINAL);
}

SrsBandwidthPacket* SrsBandwidthPacket::set_command(string command)
{
    command_name = command;
    
    return this;
}

SrsOnStatusDataPacket::SrsOnStatusDataPacket()
{
    command_name = RTMP_AMF0_COMMAND_ON_STATUS;
    data = SrsAmf0Any::object();
}

SrsOnStatusDataPacket::~SrsOnStatusDataPacket()
{
    srs_freep(data);
}

int SrsOnStatusDataPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsOnStatusDataPacket::get_message_type()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SrsOnStatusDataPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::object(data);
}

int SrsOnStatusDataPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = data->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode data failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode data success.");
    
    srs_info("encode onStatus(Data) packet success.");
    
    return ret;
}

SrsSampleAccessPacket::SrsSampleAccessPacket()
{
    command_name = RTMP_AMF0_DATA_SAMPLE_ACCESS;
    video_sample_access = false;
    audio_sample_access = false;
}

SrsSampleAccessPacket::~SrsSampleAccessPacket()
{
}

int SrsSampleAccessPacket::get_prefer_cid()
{
    return RTMP_CID_OverStream;
}

int SrsSampleAccessPacket::get_message_type()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SrsSampleAccessPacket::get_size()
{
    return SrsAmf0Size::str(command_name)
        + SrsAmf0Size::boolean() + SrsAmf0Size::boolean();
}

int SrsSampleAccessPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("encode command_name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode command_name success.");
    
    if ((ret = srs_amf0_write_boolean(stream, video_sample_access)) != ERROR_SUCCESS) {
        srs_error("encode video_sample_access failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode video_sample_access success.");
    
    if ((ret = srs_amf0_write_boolean(stream, audio_sample_access)) != ERROR_SUCCESS) {
        srs_error("encode audio_sample_access failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode audio_sample_access success.");;
    
    srs_info("encode |RtmpSampleAccess packet success.");
    
    return ret;
}

SrsOnMetaDataPacket::SrsOnMetaDataPacket()
{
    name = RTMP_AMF0_DATA_ON_METADATA;
    metadata = SrsAmf0Any::object();
}

SrsOnMetaDataPacket::~SrsOnMetaDataPacket()
{
    srs_freep(metadata);
}

int SrsOnMetaDataPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_read_string(stream, name)) != ERROR_SUCCESS) {
        srs_error("decode metadata name failed. ret=%d", ret);
        return ret;
    }

    // ignore the @setDataFrame
    if (name == RTMP_AMF0_DATA_SET_DATAFRAME) {
        if ((ret = srs_amf0_read_string(stream, name)) != ERROR_SUCCESS) {
            srs_error("decode metadata name failed. ret=%d", ret);
            return ret;
        }
    }
    
    srs_verbose("decode metadata name success. name=%s", name.c_str());
    
    // the metadata maybe object or ecma array
    SrsAmf0Any* any = NULL;
    if ((ret = srs_amf0_read_any(stream, &any)) != ERROR_SUCCESS) {
        srs_error("decode metadata metadata failed. ret=%d", ret);
        return ret;
    }
    
    srs_assert(any);
    if (any->is_object()) {
        srs_freep(metadata);
        metadata = any->to_object();
        srs_info("decode metadata object success");
        return ret;
    }
    
    SrsAutoFree(SrsAmf0Any, any);
    
    if (any->is_ecma_array()) {
        SrsAmf0EcmaArray* arr = any->to_ecma_array();
    
        // if ecma array, copy to object.
        for (int i = 0; i < arr->count(); i++) {
            metadata->set(arr->key_at(i), arr->value_at(i)->copy());
        }
        
        srs_info("decode metadata array success");
    }
    
    return ret;
}

int SrsOnMetaDataPacket::get_prefer_cid()
{
    return RTMP_CID_OverConnection2;
}

int SrsOnMetaDataPacket::get_message_type()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SrsOnMetaDataPacket::get_size()
{
    return SrsAmf0Size::str(name) + SrsAmf0Size::object(metadata);
}

int SrsOnMetaDataPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_write_string(stream, name)) != ERROR_SUCCESS) {
        srs_error("encode name failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode name success.");
    
    if ((ret = metadata->write(stream)) != ERROR_SUCCESS) {
        srs_error("encode metadata failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode metadata success.");
    
    srs_info("encode onMetaData packet success.");
    return ret;
}

SrsSetWindowAckSizePacket::SrsSetWindowAckSizePacket()
{
    ackowledgement_window_size = 0;
}

SrsSetWindowAckSizePacket::~SrsSetWindowAckSizePacket()
{
}

int SrsSetWindowAckSizePacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(4)) {
        ret = ERROR_RTMP_MESSAGE_DECODE;
        srs_error("decode ack window size failed. ret=%d", ret);
        return ret;
    }
    
    ackowledgement_window_size = stream->read_4bytes();
    srs_info("decode ack window size success");
    
    return ret;
}

int SrsSetWindowAckSizePacket::get_prefer_cid()
{
    return RTMP_CID_ProtocolControl;
}

int SrsSetWindowAckSizePacket::get_message_type()
{
    return RTMP_MSG_WindowAcknowledgementSize;
}

int SrsSetWindowAckSizePacket::get_size()
{
    return 4;
}

int SrsSetWindowAckSizePacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(4)) {
        ret = ERROR_RTMP_MESSAGE_ENCODE;
        srs_error("encode ack size packet failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_4bytes(ackowledgement_window_size);
    
    srs_verbose("encode ack size packet "
        "success. ack_size=%d", ackowledgement_window_size);
    
    return ret;
}

SrsAcknowledgementPacket::SrsAcknowledgementPacket()
{
    sequence_number = 0;
}

SrsAcknowledgementPacket::~SrsAcknowledgementPacket()
{
}

int SrsAcknowledgementPacket::get_prefer_cid()
{
    return RTMP_CID_ProtocolControl;
}

int SrsAcknowledgementPacket::get_message_type()
{
    return RTMP_MSG_Acknowledgement;
}

int SrsAcknowledgementPacket::get_size()
{
    return 4;
}

int SrsAcknowledgementPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(4)) {
        ret = ERROR_RTMP_MESSAGE_ENCODE;
        srs_error("encode acknowledgement packet failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_4bytes(sequence_number);
    
    srs_verbose("encode acknowledgement packet "
        "success. sequence_number=%d", sequence_number);
    
    return ret;
}

SrsSetChunkSizePacket::SrsSetChunkSizePacket()
{
    chunk_size = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
}

SrsSetChunkSizePacket::~SrsSetChunkSizePacket()
{
}

int SrsSetChunkSizePacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(4)) {
        ret = ERROR_RTMP_MESSAGE_DECODE;
        srs_error("decode chunk size failed. ret=%d", ret);
        return ret;
    }
    
    chunk_size = stream->read_4bytes();
    srs_info("decode chunk size success. chunk_size=%d", chunk_size);
    
    return ret;
}

int SrsSetChunkSizePacket::get_prefer_cid()
{
    return RTMP_CID_ProtocolControl;
}

int SrsSetChunkSizePacket::get_message_type()
{
    return RTMP_MSG_SetChunkSize;
}

int SrsSetChunkSizePacket::get_size()
{
    return 4;
}

int SrsSetChunkSizePacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(4)) {
        ret = ERROR_RTMP_MESSAGE_ENCODE;
        srs_error("encode chunk packet failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_4bytes(chunk_size);
    
    srs_verbose("encode chunk packet success. ack_size=%d", chunk_size);
    
    return ret;
}

SrsSetPeerBandwidthPacket::SrsSetPeerBandwidthPacket()
{
    bandwidth = 0;
    type = SrsPeerBandwidthDynamic;
}

SrsSetPeerBandwidthPacket::~SrsSetPeerBandwidthPacket()
{
}

int SrsSetPeerBandwidthPacket::get_prefer_cid()
{
    return RTMP_CID_ProtocolControl;
}

int SrsSetPeerBandwidthPacket::get_message_type()
{
    return RTMP_MSG_SetPeerBandwidth;
}

int SrsSetPeerBandwidthPacket::get_size()
{
    return 5;
}

int SrsSetPeerBandwidthPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(5)) {
        ret = ERROR_RTMP_MESSAGE_ENCODE;
        srs_error("encode set bandwidth packet failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_4bytes(bandwidth);
    stream->write_1bytes(type);
    
    srs_verbose("encode set bandwidth packet "
        "success. bandwidth=%d, type=%d", bandwidth, type);
    
    return ret;
}

SrsUserControlPacket::SrsUserControlPacket()
{
    event_type = 0;
    event_data = 0;
    extra_data = 0;
}

SrsUserControlPacket::~SrsUserControlPacket()
{
}

int SrsUserControlPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(6)) {
        ret = ERROR_RTMP_MESSAGE_DECODE;
        srs_error("decode user control failed. ret=%d", ret);
        return ret;
    }
    
    event_type = stream->read_2bytes();
    event_data = stream->read_4bytes();
    
    if (event_type == SrcPCUCSetBufferLength) {
        if (!stream->require(4)) {
            ret = ERROR_RTMP_MESSAGE_ENCODE;
            srs_error("decode user control packet failed. ret=%d", ret);
            return ret;
        }
        extra_data = stream->read_4bytes();
    }
    
    srs_info("decode user control success. "
        "event_type=%d, event_data=%d, extra_data=%d", 
        event_type, event_data, extra_data);
    
    return ret;
}

int SrsUserControlPacket::get_prefer_cid()
{
    return RTMP_CID_ProtocolControl;
}

int SrsUserControlPacket::get_message_type()
{
    return RTMP_MSG_UserControlMessage;
}

int SrsUserControlPacket::get_size()
{
    if (event_type == SrcPCUCSetBufferLength) {
        return 2 + 4 + 4;
    } else {
        return 2 + 4;
    }
}

int SrsUserControlPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(get_size())) {
        ret = ERROR_RTMP_MESSAGE_ENCODE;
        srs_error("encode user control packet failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_2bytes(event_type);
    stream->write_4bytes(event_data);

    // when event type is set buffer length,
    // write the extra buffer length.
    if (event_type == SrcPCUCSetBufferLength) {
        stream->write_4bytes(extra_data);
        srs_verbose("user control message, buffer_length=%d", extra_data);
    }
    
    srs_verbose("encode user control packet success. "
        "event_type=%d, event_data=%d", event_type, event_data);
    
    return ret;
}


