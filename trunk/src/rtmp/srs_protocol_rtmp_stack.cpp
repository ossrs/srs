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

#include <srs_protocol_rtmp_stack.hpp>

#include <srs_kernel_log.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_io.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>

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
#define RTMP_MSG_SetChunkSize                 0x01
#define RTMP_MSG_AbortMessage                 0x02
#define RTMP_MSG_Acknowledgement             0x03
#define RTMP_MSG_UserControlMessage         0x04
#define RTMP_MSG_WindowAcknowledgementSize     0x05
#define RTMP_MSG_SetPeerBandwidth             0x06
#define RTMP_MSG_EdgeAndOriginServerCommand 0x07
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
#define RTMP_MSG_AMF3CommandMessage         17 // 0x11
#define RTMP_MSG_AMF0CommandMessage         20 // 0x14
/**
3.2. Data message
The client or the server sends this message to send Metadata or any
user data to the peer. Metadata includes details about the
data(audio, video etc.) like creation time, duration, theme and so
on. These messages have been assigned message type value of 18 for
AMF0 and message type value of 15 for AMF3.        
*/
#define RTMP_MSG_AMF0DataMessage             18 // 0x12
#define RTMP_MSG_AMF3DataMessage             15 // 0x0F
/**
3.3. Shared object message
A shared object is a Flash object (a collection of name value pairs)
that are in synchronization across multiple clients, instances, and
so on. The message types kMsgContainer=19 for AMF0 and
kMsgContainerEx=16 for AMF3 are reserved for shared object events.
Each message can contain multiple events.
*/
#define RTMP_MSG_AMF3SharedObject             16 // 0x10
#define RTMP_MSG_AMF0SharedObject             19 // 0x13
/**
3.4. Audio message
The client or the server sends this message to send audio data to the
peer. The message type value of 8 is reserved for audio messages.
*/
#define RTMP_MSG_AudioMessage                 8 // 0x08
/* *
3.5. Video message
The client or the server sends this message to send video data to the
peer. The message type value of 9 is reserved for video messages.
These messages are large and can delay the sending of other type of
messages. To avoid such a situation, the video message is assigned
the lowest priority.
*/
#define RTMP_MSG_VideoMessage                 9 // 0x09
/**
3.6. Aggregate message
An aggregate message is a single message that contains a list of submessages.
The message type value of 22 is reserved for aggregate
messages.
*/
#define RTMP_MSG_AggregateMessage             22 // 0x16

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
#define RTMP_FMT_TYPE0                         0
// 6.1.2.2. Type 1
// Chunks of Type 1 are 7 bytes long. The message stream ID is not
// included; this chunk takes the same stream ID as the preceding chunk.
// Streams with variable-sized messages (for example, many video
// formats) SHOULD use this format for the first chunk of each new
// message after the first.
#define RTMP_FMT_TYPE1                         1
// 6.1.2.3. Type 2
// Chunks of Type 2 are 3 bytes long. Neither the stream ID nor the
// message length is included; this chunk has the same stream ID and
// message length as the preceding chunk. Streams with constant-sized
// messages (for example, some audio and data formats) SHOULD use this
// format for the first chunk of each message after the first.
#define RTMP_FMT_TYPE2                         2
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
#define RTMP_FMT_TYPE3                         3

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* 6. Chunking
* The chunk size is configurable. It can be set using a control
* message(Set Chunk Size) as described in section 7.1. The maximum
* chunk size can be 65536 bytes and minimum 128 bytes. Larger values
* reduce CPU usage, but also commit to larger writes that can delay
* other content on lower bandwidth connections. Smaller chunks are not
* good for high-bit rate streaming. Chunk size is maintained
* independently for each direction.
*/
#define RTMP_DEFAULT_CHUNK_SIZE             128
#define RTMP_MIN_CHUNK_SIZE                 128
#define RTMP_MAX_CHUNK_SIZE                    65536

/**
* 6.1. Chunk Format
* Extended timestamp: 0 or 4 bytes
* This field MUST be sent when the normal timsestamp is set to
* 0xffffff, it MUST NOT be sent if the normal timestamp is set to
* anything else. So for values less than 0xffffff the normal
* timestamp field SHOULD be used in which case the extended timestamp
* MUST NOT be present. For values greater than or equal to 0xffffff
* the normal timestamp field MUST NOT be used and MUST be set to
* 0xffffff and the extended timestamp MUST be sent.
*/
#define RTMP_EXTENDED_TIMESTAMP             0xFFFFFF

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* amf0 command message, command name macros
*/
#define RTMP_AMF0_COMMAND_CONNECT            "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM        "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM      "closeStream"
#define RTMP_AMF0_COMMAND_PLAY                "play"
#define RTMP_AMF0_COMMAND_PAUSE                "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE        "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS            "onStatus"
#define RTMP_AMF0_COMMAND_RESULT            "_result"
#define RTMP_AMF0_COMMAND_ERROR                "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM    "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH        "FCPublish"
#define RTMP_AMF0_COMMAND_UNPUBLISH            "FCUnpublish"
#define RTMP_AMF0_COMMAND_PUBLISH            "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS        "|RtmpSampleAccess"
#define RTMP_AMF0_DATA_SET_DATAFRAME        "@setDataFrame"
#define RTMP_AMF0_DATA_ON_METADATA            "onMetaData"

/**
* band width check method name, which will be invoked by client.
* band width check mothods use SrsBandwidthPacket as its internal packet type,
* so ensure you set command name when you use it.
*/
// server play control
#define SRS_BW_CHECK_START_PLAY         "onSrsBandCheckStartPlayBytes"
#define SRS_BW_CHECK_STARTING_PLAY      "onSrsBandCheckStartingPlayBytes"
#define SRS_BW_CHECK_STOP_PLAY          "onSrsBandCheckStopPlayBytes"
#define SRS_BW_CHECK_STOPPED_PLAY       "onSrsBandCheckStoppedPlayBytes"

// server publish control
#define SRS_BW_CHECK_START_PUBLISH      "onSrsBandCheckStartPublishBytes"
#define SRS_BW_CHECK_STARTING_PUBLISH   "onSrsBandCheckStartingPublishBytes"
#define SRS_BW_CHECK_STOP_PUBLISH       "onSrsBandCheckStopPublishBytes"
#define SRS_BW_CHECK_STOPPED_PUBLISH    "onSrsBandCheckStoppedPublishBytes"

// EOF control.
#define SRS_BW_CHECK_FINISHED           "onSrsBandCheckFinished"
// for flash, it will sendout a final call, 
// used to confirm got the report.
// actually, client send out this packet and close the connection,
// so server may cannot got this packet, ignore is ok.
#define SRS_BW_CHECK_FLASH_FINAL        "finalClientPacket"

// client only
#define SRS_BW_CHECK_PLAYING            "onSrsBandCheckPlaying"
#define SRS_BW_CHECK_PUBLISHING         "onSrsBandCheckPublishing"

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* the chunk stream id used for some under-layer message,
* for example, the PC(protocol control) message.
*/
#define RTMP_CID_ProtocolControl 0x02
/**
* the AMF0/AMF3 command message, invoke method and return the result, over NetConnection.
* generally use 0x03.
*/
#define RTMP_CID_OverConnection 0x03
/**
* the AMF0/AMF3 command message, invoke method and return the result, over NetConnection, 
* the midst state(we guess).
* rarely used, e.g. onStatus(NetStream.Play.Reset).
*/
#define RTMP_CID_OverConnection2 0x04
/**
* the stream message(amf0/amf3), over NetStream.
* generally use 0x05.
*/
#define RTMP_CID_OverStream 0x05
/**
* the stream message(amf0/amf3), over NetStream, the midst state(we guess).
* rarely used, e.g. play("mp4:mystram.f4v")
*/
#define RTMP_CID_OverStream2 0x08
/**
* the stream message(video), over NetStream
* generally use 0x06.
*/
#define RTMP_CID_Video 0x06
/**
* the stream message(audio), over NetStream.
* generally use 0x07.
*/
#define RTMP_CID_Audio 0x07

/****************************************************************************
*****************************************************************************
****************************************************************************/

SrsProtocol::AckWindowSize::AckWindowSize()
{
    ack_window_size = acked_size = 0;
}

SrsProtocol::SrsProtocol(ISrsProtocolReaderWriter* io)
{
    buffer = new SrsBuffer();
    decode_stream = new SrsStream();
    skt = io;
    
    in_chunk_size = out_chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
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
    
    srs_freep(decode_stream);
    srs_freep(buffer);
}

string SrsProtocol::get_request_name(double transcationId)
{
    if (requests.find(transcationId) == requests.end()) {
        return "";
    }
    
    return requests[transcationId];
}

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

int SrsProtocol::recv_message(SrsMessage** pmsg)
{
    *pmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsMessage* msg = NULL;
        
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

int SrsProtocol::decode_message(SrsMessage* msg, SrsPacket** ppacket)
{
    *ppacket = NULL;
    
    int ret = ERROR_SUCCESS;
    
    srs_assert(msg != NULL);
    srs_assert(msg->payload != NULL);
    srs_assert(msg->size > 0);

    // initialize the decode stream for all message,
    // it's ok for the initialize if fast and without memory copy.
    if ((ret = decode_stream->initialize((char*)(msg->payload), msg->size)) != ERROR_SUCCESS) {
        srs_error("initialize stream failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("decode stream initialized success");
    
    // decode the packet.
    SrsPacket* packet = NULL;
    if ((ret = do_decode_message(msg->header, decode_stream, &packet)) != ERROR_SUCCESS) {
        srs_freep(packet);
        return ret;
    }
    
    // set to output ppacket only when success.
    *ppacket = packet;
    
    return ret;
}

int SrsProtocol::do_send_and_free_message(SrsMessage* msg, SrsPacket* packet)
{
    int ret = ERROR_SUCCESS;
    
    // always free msg.
    srs_assert(msg);
    SrsAutoFree(SrsMessage, msg);
    
    // we donot use the complex basic header,
    // ensure the basic header is 1bytes.
    if (msg->header.perfer_cid < 2) {
        srs_warn("change the chunk_id=%d to default=%d", 
            msg->header.perfer_cid, RTMP_CID_ProtocolControl);
        msg->header.perfer_cid = RTMP_CID_ProtocolControl;
    }

    // p set to current write position,
    // it's ok when payload is NULL and size is 0.
    char* p = (char*)msg->payload;
    
    // always write the header event payload is empty.
    do {
        // generate the header.
        char* pheader = out_header_cache;
        
        if (p == (char*)msg->payload) {
            // write new chunk stream header, fmt is 0
            *pheader++ = 0x00 | (msg->header.perfer_cid & 0x3F);
            
            // chunk message header, 11 bytes
            // timestamp, 3bytes, big-endian
            u_int32_t timestamp = (u_int32_t)msg->header.timestamp;
            if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
                *pheader++ = 0xFF;
                *pheader++ = 0xFF;
                *pheader++ = 0xFF;
            } else {
                pp = (char*)&timestamp;
                *pheader++ = pp[2];
                *pheader++ = pp[1];
                *pheader++ = pp[0];
            }
            
            // message_length, 3bytes, big-endian
            pp = (char*)&msg->header.payload_length;
            *pheader++ = pp[2];
            *pheader++ = pp[1];
            *pheader++ = pp[0];
            
            // message_type, 1bytes
            *pheader++ = msg->header.message_type;
            
            // message_length, 3bytes, little-endian
            pp = (char*)&msg->header.stream_id;
            *pheader++ = pp[0];
            *pheader++ = pp[1];
            *pheader++ = pp[2];
            *pheader++ = pp[3];
            
            // chunk extended timestamp header, 0 or 4 bytes, big-endian
            if(timestamp >= RTMP_EXTENDED_TIMESTAMP){
                pp = (char*)&timestamp;
                *pheader++ = pp[3];
                *pheader++ = pp[2];
                *pheader++ = pp[1];
                *pheader++ = pp[0];
            }
        } else {
            // write no message header chunk stream, fmt is 3
            *pheader++ = 0xC0 | (msg->header.perfer_cid & 0x3F);
            
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
            u_int32_t timestamp = (u_int32_t)msg->header.timestamp;
            if(timestamp >= RTMP_EXTENDED_TIMESTAMP){
                pp = (char*)&timestamp;
                *pheader++ = pp[3];
                *pheader++ = pp[2];
                *pheader++ = pp[1];
                *pheader++ = pp[0];
            }
        }
        
        // sendout header and payload by writev.
        // decrease the sys invoke count to get higher performance.
        int payload_size = msg->size - (p - (char*)msg->payload);
        payload_size = srs_min(payload_size, out_chunk_size);
        
        // always has header
        int header_size = pheader - out_header_cache;
        srs_assert(header_size > 0);
        
        // send by writev
        iovec iov[2];
        iov[0].iov_base = out_header_cache;
        iov[0].iov_len = header_size;
        iov[1].iov_base = p;
        iov[1].iov_len = payload_size;
        
        ssize_t nwrite;
        if ((ret = skt->writev(iov, 2, &nwrite)) != ERROR_SUCCESS) {
            srs_error("send with writev failed. ret=%d", ret);
            return ret;
        }
        
        // consume sendout bytes when not empty packet.
        if (msg->payload && msg->size > 0) {
            p += payload_size;
        }
    } while (p < (char*)msg->payload + msg->size);
    
    if ((ret = on_send_message(msg, packet)) != ERROR_SUCCESS) {
        srs_error("hook the send message failed. ret=%d", ret);
        return ret;
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
            stream->reset();
            if (header.is_amf3_command()) {
                stream->skip(1);
            }
            
            std::string request_name = get_request_name(transactionId);
            if (request_name.empty()) {
                ret = ERROR_RTMP_NO_REQUEST;
                srs_error("decode AMF0/AMF3 request failed. ret=%d", ret);
                return ret;
            }
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
        stream->reset();
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
            || command == SRS_BW_CHECK_FLASH_FINAL)
        {
            srs_info("decode the AMF0/AMF3 band width check message.");
            *ppacket = packet = new SrsBandwidthPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_CLOSE_STREAM) {
            srs_info("decode the AMF0/AMF3 closeStream message.");
            *ppacket = packet = new SrsCloseStreamPacket();
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
        srs_trace("drop unknown message, type=%d", header.message_type);
    }
    
    return ret;
}

int SrsProtocol::send_and_free_message(SrsMessage* msg)
{
    return do_send_and_free_message(msg, NULL);
}

int SrsProtocol::send_and_free_packet(SrsPacket* packet, int stream_id)
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
    SrsMessage* msg = new SrsCommonMessage();
    
    msg->payload = (int8_t*)payload;
    msg->size = (int32_t)size;
    
    msg->header.payload_length = size;
    msg->header.message_type = packet->get_message_type();
    msg->header.stream_id = stream_id;
    msg->header.perfer_cid = packet->get_perfer_cid();

    if ((ret = do_send_and_free_message(msg, packet)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsProtocol::recv_interlaced_message(SrsMessage** pmsg)
{
    int ret = ERROR_SUCCESS;
    
    // chunk stream basic header.
    char fmt = 0;
    int cid = 0;
    int bh_size = 0;
    if ((ret = read_basic_header(fmt, cid, bh_size)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read basic header failed. ret=%d", ret);
        }
        return ret;
    }
    srs_verbose("read basic header success. fmt=%d, cid=%d, bh_size=%d", fmt, cid, bh_size);
    
    // once we got the chunk message header, 
    // that is there is a real message in cache,
    // increase the timeout to got it.
    // For example, in the play loop, we set timeout to 100ms,
    // when we got a chunk header, we should increase the timeout,
    // or we maybe timeout and disconnect the client.
    int64_t timeout_us = skt->get_recv_timeout();
    if (!skt->is_never_timeout(timeout_us)) {
        int64_t pkt_timeout_us = srs_max(timeout_us, SRS_MIN_RECV_TIMEOUT_US);
        skt->set_recv_timeout(pkt_timeout_us);
        srs_verbose("change recv timeout_us "
            "from %"PRId64" to %"PRId64"", timeout_us, pkt_timeout_us);
    }
    
    // get the cached chunk stream.
    SrsChunkStream* chunk = NULL;
    
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

    // chunk stream message header
    int mh_size = 0;
    if ((ret = read_message_header(chunk, fmt, bh_size, mh_size)) != ERROR_SUCCESS) {
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
    SrsMessage* msg = NULL;
    int payload_size = 0;
    if ((ret = read_message_payload(chunk, bh_size, mh_size, payload_size, &msg)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read message payload failed. ret=%d", ret);
        }
        return ret;
    }
    
    // reset the recv timeout
    if (!skt->is_never_timeout(timeout_us)) {
        skt->set_recv_timeout(timeout_us);
        srs_verbose("reset recv timeout_us to %"PRId64"", timeout_us);
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

int SrsProtocol::read_basic_header(char& fmt, int& cid, int& bh_size)
{
    int ret = ERROR_SUCCESS;
    
    int required_size = 1;
    if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read 1bytes basic header failed. required_size=%d, ret=%d", required_size, ret);
        }
        return ret;
    }
    
    char* p = buffer->bytes();
    
    fmt = (*p >> 6) & 0x03;
    cid = *p & 0x3f;
    bh_size = 1;
    
    if (cid > 1) {
        srs_verbose("%dbytes basic header parsed. fmt=%d, cid=%d", bh_size, fmt, cid);
        return ret;
    }

    if (cid == 0) {
        required_size = 2;
        if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("read 2bytes basic header failed. required_size=%d, ret=%d", required_size, ret);
            }
            return ret;
        }
        
        cid = 64;
        cid += *(++p);
        bh_size = 2;
        srs_verbose("%dbytes basic header parsed. fmt=%d, cid=%d", bh_size, fmt, cid);
    } else if (cid == 1) {
        required_size = 3;
        if ((ret = buffer->ensure_buffer_bytes(skt, 3)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("read 3bytes basic header failed. required_size=%d, ret=%d", required_size, ret);
            }
            return ret;
        }
        
        cid = 64;
        cid += *(++p);
        cid += *(++p) * 256;
        bh_size = 3;
        srs_verbose("%dbytes basic header parsed. fmt=%d, cid=%d", bh_size, fmt, cid);
    } else {
        srs_error("invalid path, impossible basic header.");
        srs_assert(false);
    }
    
    return ret;
}

int SrsProtocol::read_message_header(SrsChunkStream* chunk, char fmt, int bh_size, int& mh_size)
{
    int ret = ERROR_SUCCESS;
    
    /**
    * we should not assert anything about fmt, for the first packet.
    * (when first packet, the chunk->msg is NULL).
    * the fmt maybe 0/1/2/3, the FMLE will send a 0xC4 for some audio packet.
    * the previous packet is:
    *     04             // fmt=0, cid=4
    *     00 00 1a     // timestamp=26
    *    00 00 9d     // payload_length=157
    *     08             // message_type=8(audio)
    *     01 00 00 00 // stream_id=1
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
    bool is_fresh_packet = !chunk->msg;
    
    // but, we can ensure that when a chunk stream is fresh, 
    // the fmt must be 0, a new stream.
    if (chunk->msg_count == 0 && fmt != RTMP_FMT_TYPE0) {
        ret = ERROR_RTMP_CHUNK_START;
        srs_error("chunk stream is fresh, fmt must be %d, actual is %d. cid=%d, ret=%d", 
            RTMP_FMT_TYPE0, fmt, chunk->cid, ret);
        return ret;
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
    bool is_first_chunk_of_msg = false;
    if (!chunk->msg) {
        is_first_chunk_of_msg = true;
        chunk->msg = new SrsCommonMessage();
        srs_verbose("create message for new chunk, fmt=%d, cid=%d", fmt, chunk->cid);
    }

    // read message header from socket to buffer.
    static char mh_sizes[] = {11, 7, 3, 0};
    mh_size = mh_sizes[(int)fmt];
    srs_verbose("calc chunk message header size. fmt=%d, mh_size=%d", fmt, mh_size);
    
    int required_size = bh_size + mh_size;
    if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read %dbytes message header failed. required_size=%d, ret=%d", mh_size, required_size, ret);
        }
        return ret;
    }
    char* p = buffer->bytes() + bh_size;
    
    /**
    * parse the message header.
    *   3bytes: timestamp delta,    fmt=0,1,2
    *   3bytes: payload length,     fmt=0,1
    *   1bytes: message type,       fmt=0,1
    *   4bytes: stream id,          fmt=0
    */
    // see also: ngx_rtmp_recv
    if (fmt <= RTMP_FMT_TYPE2) {
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
            pp = (char*)&chunk->header.payload_length;
            pp[2] = *p++;
            pp[1] = *p++;
            pp[0] = *p++;
            pp[3] = 0;
            
            // if msg exists in cache, the size must not changed.
            if (chunk->msg->size > 0 && chunk->msg->size != chunk->header.payload_length) {
                ret = ERROR_RTMP_PACKET_SIZE;
                srs_error("msg exists in chunk cache, "
                    "size=%d cannot change to %d, ret=%d", 
                    chunk->msg->size, chunk->header.payload_length, ret);
                return ret;
            }
            
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
        // update the timestamp even fmt=3 for first stream
        if (is_fresh_packet && !chunk->extended_timestamp) {
            chunk->header.timestamp += chunk->header.timestamp_delta;
        }
        srs_verbose("header read completed. fmt=%d, size=%d, ext_time=%d", 
            fmt, mh_size, chunk->extended_timestamp);
    }
    
    // read extended-timestamp
    if (chunk->extended_timestamp) {
        mh_size += 4;
        required_size = bh_size + mh_size;
        srs_verbose("read header ext time. fmt=%d, ext_time=%d, mh_size=%d", fmt, chunk->extended_timestamp, mh_size);
        if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
            if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
                srs_error("read %dbytes message header failed. required_size=%d, ret=%d", mh_size, required_size, ret);
            }
            return ret;
        }

        u_int32_t timestamp = 0x00;
        char* pp = (char*)&timestamp;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        
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
        u_int32_t chunk_timestamp = chunk->header.timestamp;
        
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
            srs_warn("no 4bytes extended timestamp in the continued chunk");
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
    if (chunk->header.timestamp > 0x7fffffff) {
        srs_warn("RTMP 31bits timestamp overflow, time=%"PRId64, chunk->header.timestamp);
    }
    chunk->header.timestamp &= 0x7fffffff;
    
    // valid message
    if (chunk->header.payload_length < 0) {
        ret = ERROR_RTMP_MSG_INVLIAD_SIZE;
        srs_error("RTMP message size must not be negative. size=%d, ret=%d", 
            chunk->header.payload_length, ret);
        return ret;
    }
    
    // copy header to msg
    chunk->msg->header = chunk->header;
    
    // increase the msg count, the chunk stream can accept fmt=1/2/3 message now.
    chunk->msg_count++;
    
    return ret;
}

int SrsProtocol::read_message_payload(SrsChunkStream* chunk, int bh_size, int mh_size, int& payload_size, SrsMessage** pmsg)
{
    int ret = ERROR_SUCCESS;
    
    // empty message
    if (chunk->header.payload_length <= 0) {
        // need erase the header in buffer.
        buffer->erase(bh_size + mh_size);
        
        srs_trace("get an empty RTMP "
                "message(type=%d, size=%d, time=%"PRId64", sid=%d)", chunk->header.message_type, 
                chunk->header.payload_length, chunk->header.timestamp, chunk->header.stream_id);
        
        *pmsg = chunk->msg;
        chunk->msg = NULL;
                
        return ret;
    }
    srs_assert(chunk->header.payload_length > 0);
    
    // the chunk payload size.
    payload_size = chunk->header.payload_length - chunk->msg->size;
    payload_size = srs_min(payload_size, in_chunk_size);
    srs_verbose("chunk payload size is %d, message_size=%d, received_size=%d, in_chunk_size=%d", 
        payload_size, chunk->header.payload_length, chunk->msg->size, in_chunk_size);

    // create msg payload if not initialized
    if (!chunk->msg->payload) {
        chunk->msg->payload = new int8_t[chunk->header.payload_length];
        memset(chunk->msg->payload, 0, chunk->header.payload_length);
        srs_verbose("create empty payload for RTMP message. size=%d", chunk->header.payload_length);
    }
    
    // read payload to buffer
    int required_size = bh_size + mh_size + payload_size;
    if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
        if (ret != ERROR_SOCKET_TIMEOUT && !srs_is_client_gracefully_close(ret)) {
            srs_error("read payload failed. required_size=%d, ret=%d", required_size, ret);
        }
        return ret;
    }
    memcpy(chunk->msg->payload + chunk->msg->size, buffer->bytes() + bh_size + mh_size, payload_size);
    buffer->erase(bh_size + mh_size + payload_size);
    chunk->msg->size += payload_size;
    
    srs_verbose("chunk payload read completed. bh_size=%d, mh_size=%d, payload_size=%d", bh_size, mh_size, payload_size);
    
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

int SrsProtocol::on_recv_message(SrsMessage* msg)
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
                srs_trace("set ack window size to %d", pkt->ackowledgement_window_size);
            } else {
                srs_warn("ignored. set ack window size is %d", pkt->ackowledgement_window_size);
            }
            break;
        }
        case RTMP_MSG_SetChunkSize: {
            SrsSetChunkSizePacket* pkt = dynamic_cast<SrsSetChunkSizePacket*>(packet);
            srs_assert(pkt != NULL);
            
            in_chunk_size = pkt->chunk_size;
            
            srs_trace("set input chunk size to %d", pkt->chunk_size);
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
    }
    
    return ret;
}

int SrsProtocol::on_send_message(SrsMessage* msg, SrsPacket* packet)
{
    int ret = ERROR_SUCCESS;
    
    // ignore raw bytes oriented RTMP message.
    if (!packet) {
        return ret;
    }
    
    switch (msg->header.message_type) {
        case RTMP_MSG_SetChunkSize: {
            SrsSetChunkSizePacket* pkt = dynamic_cast<SrsSetChunkSizePacket*>(packet);
            srs_assert(pkt != NULL);
            
            out_chunk_size = pkt->chunk_size;
            
            srs_trace("set output chunk size to %d", pkt->chunk_size);
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
    }
    
    return ret;
}

int SrsProtocol::response_acknowledgement_message()
{
    int ret = ERROR_SUCCESS;
    
    SrsAcknowledgementPacket* pkt = new SrsAcknowledgementPacket();
    in_ack_size.acked_size = pkt->sequence_number = skt->get_recv_bytes();
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
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
    
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send ping response failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("send ping response success.");
    
    return ret;
}

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

SrsMessage::SrsMessage()
{
    payload = NULL;
    size = 0;
}

SrsMessage::~SrsMessage()
{
}

SrsCommonMessage::SrsCommonMessage()
{
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
}

SrsSharedPtrMessage::__SrsSharedPtr::~__SrsSharedPtr()
{
    srs_freep(payload);
}

SrsSharedPtrMessage::SrsSharedPtrMessage()
{
    ptr = NULL;
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
}

int SrsSharedPtrMessage::initialize(SrsMessage* source)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = initialize(&source->header, (char*)source->payload, source->size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // detach the payload from source
    source->payload = NULL;
    source->size = 0;
    
    return ret;
}

int SrsSharedPtrMessage::initialize(SrsMessageHeader* source, char* payload, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(source != NULL);
    if (ptr) {
        ret = ERROR_SYSTEM_ASSERT_FAILED;
        srs_error("should not set the payload twice. ret=%d", ret);
        srs_assert(false);
        
        return ret;
    }
    
    header = *source;
    header.payload_length = size;
    
    ptr = new __SrsSharedPtr();
    
    // direct attach the data of common message.
    ptr->payload = payload;
    ptr->size = size;
    
    SrsMessage::payload = (int8_t*)ptr->payload;
    SrsMessage::size = ptr->size;
    
    return ret;
}

SrsSharedPtrMessage* SrsSharedPtrMessage::copy()
{
    if (!ptr) {
        srs_error("invoke initialize to initialize the ptr.");
        srs_assert(false);
        return NULL;
    }
    
    SrsSharedPtrMessage* copy = new SrsSharedPtrMessage();
    
    copy->header = header;
    
    copy->ptr = ptr;
    ptr->shared_count++;
    
    copy->payload = (int8_t*)ptr->payload;
    copy->size = ptr->size;
    
    return copy;
}

SrsPacket::SrsPacket()
{
}

SrsPacket::~SrsPacket()
{
}

int SrsPacket::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(stream != NULL);

    ret = ERROR_SYSTEM_PACKET_INVALID;
    srs_error("current packet is not support to decode. "
        "paket=%s, ret=%d", get_class_name(), ret);
    
    return ret;
}

int SrsPacket::get_perfer_cid()
{
    return 0;
}

int SrsPacket::get_message_type()
{
    return 0;
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

int SrsPacket::get_size()
{
    return 0;
}

int SrsPacket::encode_packet(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(stream != NULL);

    ret = ERROR_SYSTEM_PACKET_INVALID;
    srs_error("current packet is not support to encode. "
        "paket=%s, ret=%d", get_class_name(), ret);
    
    return ret;
}

SrsConnectAppPacket::SrsConnectAppPacket()
{
    command_name = RTMP_AMF0_COMMAND_CONNECT;
    transaction_id = 1;
    command_object = SrsAmf0Any::object();
}

SrsConnectAppPacket::~SrsConnectAppPacket()
{
    srs_freep(command_object);
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
    
    srs_info("amf0 decode connect packet success");
    
    return ret;
}

int SrsConnectAppPacket::get_perfer_cid()
{
    return RTMP_CID_OverConnection;
}

int SrsConnectAppPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsConnectAppPacket::get_size()
{
    return SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::object(command_object);
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

int SrsConnectAppResPacket::get_perfer_cid()
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

int SrsCreateStreamPacket::get_perfer_cid()
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

int SrsCreateStreamResPacket::get_perfer_cid()
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

int SrsFMLEStartPacket::get_perfer_cid()
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

int SrsFMLEStartResPacket::get_perfer_cid()
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

int SrsPublishPacket::get_perfer_cid()
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
            reset = (reset_value->to_number() == 0 ? false : true);
        } else {
            ret = ERROR_RTMP_AMF0_DECODE;
            srs_error("amf0 invalid type=%#x, requires number or bool, ret=%d", reset_value->marker, ret);
            return ret;
        }
    }

    srs_info("amf0 decode play packet success");
    
    return ret;
}

int SrsPlayPacket::get_perfer_cid()
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

int SrsPlayResPacket::get_perfer_cid()
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

int SrsOnBWDonePacket::get_perfer_cid()
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

int SrsOnStatusCallPacket::get_perfer_cid()
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

int SrsBandwidthPacket::get_perfer_cid()
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

int SrsBandwidthPacket::decode(SrsStream *stream)
{
    int ret = ERROR_SUCCESS;

    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        srs_error("amf0 decode play command_name failed. ret=%d", ret);
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
    
    // @remark, for bandwidth test, ignore the data field.

    srs_info("decode SrsBandwidthPacket success.");

    return ret;
}

bool SrsBandwidthPacket::is_starting_play()
{
    return command_name == SRS_BW_CHECK_STARTING_PLAY;
}

bool SrsBandwidthPacket::is_stopped_play()
{
    return command_name == SRS_BW_CHECK_STOPPED_PLAY;
}

bool SrsBandwidthPacket::is_starting_publish()
{
    return command_name == SRS_BW_CHECK_STARTING_PUBLISH;
}

bool SrsBandwidthPacket::is_stopped_publish()
{
    return command_name == SRS_BW_CHECK_STOPPED_PUBLISH;
}

bool SrsBandwidthPacket::is_flash_final()
{
    return command_name == SRS_BW_CHECK_FLASH_FINAL;
}

SrsBandwidthPacket* SrsBandwidthPacket::create_finish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_FINISHED);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_start_play()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_START_PLAY);
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

SrsBandwidthPacket* SrsBandwidthPacket::create_start_publish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_START_PUBLISH);
}

SrsBandwidthPacket* SrsBandwidthPacket::create_stop_publish()
{
    SrsBandwidthPacket* pkt = new SrsBandwidthPacket();
    return pkt->set_command(SRS_BW_CHECK_STOP_PUBLISH);
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

int SrsOnStatusDataPacket::get_perfer_cid()
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

int SrsSampleAccessPacket::get_perfer_cid()
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
            metadata->set(arr->key_at(i), arr->value_at(i));
        }
        
        arr->clear();
        srs_info("decode metadata array success");
    }
    
    return ret;
}

int SrsOnMetaDataPacket::get_perfer_cid()
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

int SrsSetWindowAckSizePacket::get_perfer_cid()
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

int SrsAcknowledgementPacket::get_perfer_cid()
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
    chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
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
    
    if (chunk_size < RTMP_MIN_CHUNK_SIZE) {
        ret = ERROR_RTMP_CHUNK_SIZE;
        srs_error("invalid chunk size. min=%d, actual=%d, ret=%d", 
            ERROR_RTMP_CHUNK_SIZE, chunk_size, ret);
        return ret;
    }
    if (chunk_size > RTMP_MAX_CHUNK_SIZE) {
        ret = ERROR_RTMP_CHUNK_SIZE;
        srs_error("invalid chunk size. max=%d, actual=%d, ret=%d", 
            RTMP_MAX_CHUNK_SIZE, chunk_size, ret);
        return ret;
    }
    
    return ret;
}

int SrsSetChunkSizePacket::get_perfer_cid()
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
    type = 2;
}

SrsSetPeerBandwidthPacket::~SrsSetPeerBandwidthPacket()
{
}

int SrsSetPeerBandwidthPacket::get_perfer_cid()
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

int SrsUserControlPacket::get_perfer_cid()
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
        stream->write_2bytes(extra_data);
        srs_verbose("user control message, buffer_length=%d", extra_data);
    }
    
    srs_verbose("encode user control packet success. "
        "event_type=%d, event_data=%d", event_type, event_data);
    
    return ret;
}

