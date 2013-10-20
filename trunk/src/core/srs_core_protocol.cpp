/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#include <srs_core_protocol.hpp>

#include <srs_core_log.hpp>
#include <srs_core_amf0.hpp>
#include <srs_core_error.hpp>
#include <srs_core_socket.hpp>
#include <srs_core_buffer.hpp>
#include <srs_core_stream.hpp>

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
#define RTMP_MSG_SetChunkSize 				0x01
#define RTMP_MSG_AbortMessage 				0x02
#define RTMP_MSG_Acknowledgement 			0x03
#define RTMP_MSG_UserControlMessage 		0x04
#define RTMP_MSG_WindowAcknowledgementSize 	0x05
#define RTMP_MSG_SetPeerBandwidth 			0x06
#define RTMP_MSG_EdgeAndOriginServerCommand 0x07
/**
* The server sends this event to test whether the client is reachable. 
* 
* Event data is a 4-byte timestamp, representing the local server time when the server dispatched the command. 
* The client responds with PingResponse on receiving PingRequest.
*/
#define RTMP_MSG_PCUC_PingRequest 			0x06

/**
* The client sends this event to the server in response to the ping request. 
* 
* The event data is a 4-byte timestamp, which was received with the PingRequest request.
*/
#define RTMP_MSG_PCUC_PingResponse 			0x07
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
#define RTMP_MSG_AMF3CommandMessage 		17 // 0x11
#define RTMP_MSG_AMF0CommandMessage 		20 // 0x14
/**
3.2. Data message
The client or the server sends this message to send Metadata or any
user data to the peer. Metadata includes details about the
data(audio, video etc.) like creation time, duration, theme and so
on. These messages have been assigned message type value of 18 for
AMF0 and message type value of 15 for AMF3.        
*/
#define RTMP_MSG_AMF0DataMessage 			18 // 0x12
#define RTMP_MSG_AMF3DataMessage 			15 // 0x0F
/**
3.3. Shared object message
A shared object is a Flash object (a collection of name value pairs)
that are in synchronization across multiple clients, instances, and
so on. The message types kMsgContainer=19 for AMF0 and
kMsgContainerEx=16 for AMF3 are reserved for shared object events.
Each message can contain multiple events.
*/
#define RTMP_MSG_AMF3SharedObject 			16 // 0x10
#define RTMP_MSG_AMF0SharedObject 			19 // 0x13
/**
3.4. Audio message
The client or the server sends this message to send audio data to the
peer. The message type value of 8 is reserved for audio messages.
*/
#define RTMP_MSG_AudioMessage 				8 // 0x08
/* *
3.5. Video message
The client or the server sends this message to send video data to the
peer. The message type value of 9 is reserved for video messages.
These messages are large and can delay the sending of other type of
messages. To avoid such a situation, the video message is assigned
the lowest priority.
*/
#define RTMP_MSG_VideoMessage 				9 // 0x09
/**
3.6. Aggregate message
An aggregate message is a single message that contains a list of submessages.
The message type value of 22 is reserved for aggregate
messages.
*/
#define RTMP_MSG_AggregateMessage 			22 // 0x16

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
#define RTMP_FMT_TYPE0 						0
// 6.1.2.2. Type 1
// Chunks of Type 1 are 7 bytes long. The message stream ID is not
// included; this chunk takes the same stream ID as the preceding chunk.
// Streams with variable-sized messages (for example, many video
// formats) SHOULD use this format for the first chunk of each new
// message after the first.
#define RTMP_FMT_TYPE1 						1
// 6.1.2.3. Type 2
// Chunks of Type 2 are 3 bytes long. Neither the stream ID nor the
// message length is included; this chunk has the same stream ID and
// message length as the preceding chunk. Streams with constant-sized
// messages (for example, some audio and data formats) SHOULD use this
// format for the first chunk of each message after the first.
#define RTMP_FMT_TYPE2 						2
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
#define RTMP_FMT_TYPE3 						3

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
#define RTMP_DEFAULT_CHUNK_SIZE 			128

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
#define RTMP_EXTENDED_TIMESTAMP 			0xFFFFFF

/****************************************************************************
*****************************************************************************
****************************************************************************/
/**
* amf0 command message, command name: "connect"
*/
#define RTMP_AMF0_COMMAND_CONNECT			"connect"

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

SrsProtocol::SrsProtocol(st_netfd_t client_stfd)
{
	stfd = client_stfd;
	buffer = new SrsBuffer();
	skt = new SrsSocket(stfd);
	
	in_chunk_size = out_chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
}

SrsProtocol::~SrsProtocol()
{
	std::map<int, SrsChunkStream*>::iterator it;
	
	for (it = chunk_streams.begin(); it != chunk_streams.end(); ++it) {
		SrsChunkStream* stream = it->second;
		
		if (stream) {
			delete stream;
		}
	}

	chunk_streams.clear();
	
	if (buffer) {
		delete buffer;
		buffer = NULL;
	}
	
	if (skt) {
		delete skt;
		skt = NULL;
	}
}

int SrsProtocol::recv_message(SrsMessage** pmsg)
{
	*pmsg = NULL;
	
	int ret = ERROR_SUCCESS;
	
	while (true) {
		SrsMessage* msg = NULL;
		
		if ((ret = recv_interlaced_message(&msg)) != ERROR_SUCCESS) {
			srs_error("recv interlaced message failed. ret=%d", ret);
			return ret;
		}
		srs_verbose("entire msg received");
		
		if (!msg) {
			continue;
		}
		
		if (msg->size <= 0 || msg->header.payload_length <= 0) {
			srs_trace("ignore empty message(type=%d, size=%d, time=%d, sid=%d).",
				msg->header.message_type, msg->header.payload_length,
				msg->header.timestamp, msg->header.stream_id);
			delete msg;
			continue;
		}
		
		srs_verbose("get a msg with raw/undecoded payload");
		*pmsg = msg;
		break;
	}
	
	return ret;
}

int SrsProtocol::send_message(SrsMessage* msg)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = msg->encode_packet()) != ERROR_SUCCESS) {
		srs_error("encode packet to message payload failed. ret=%d", ret);
		return ret;
	}
	srs_info("encode packet to message payload success");

	// p set to current write position,
	// it's ok when payload is NULL and size is 0.
	char* p = (char*)msg->payload;
	
	// always write the header event payload is empty.
	do {
		// generate the header.
		char* pheader = NULL;
		int header_size = 0;
		
		if (p == (char*)msg->payload) {
			// write new chunk stream header, fmt is 0
			pheader = out_header_fmt0;
			*pheader++ = 0x00 | (msg->get_perfer_cid() & 0x3F);
			
		    // chunk message header, 11 bytes
		    // timestamp, 3bytes, big-endian
			if (msg->header.timestamp >= RTMP_EXTENDED_TIMESTAMP) {
		        *pheader++ = 0xFF;
		        *pheader++ = 0xFF;
		        *pheader++ = 0xFF;
			} else {
		        pp = (char*)&msg->header.timestamp; 
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
		    if(msg->header.timestamp >= RTMP_EXTENDED_TIMESTAMP){
		        pp = (char*)&msg->header.timestamp; 
		        *pheader++ = pp[3];
		        *pheader++ = pp[2];
		        *pheader++ = pp[1];
		        *pheader++ = pp[0];
		    }
			
			header_size = pheader - out_header_fmt0;
			pheader = out_header_fmt0;
		} else {
			// write no message header chunk stream, fmt is 3
			pheader = out_header_fmt3;
			*pheader++ = 0xC0 | (msg->get_perfer_cid() & 0x3F);
		    
		    // chunk extended timestamp header, 0 or 4 bytes, big-endian
		    if(msg->header.timestamp >= RTMP_EXTENDED_TIMESTAMP){
		        pp = (char*)&msg->header.timestamp; 
		        *pheader++ = pp[3];
		        *pheader++ = pp[2];
		        *pheader++ = pp[1];
		        *pheader++ = pp[0];
		    }
			
			header_size = pheader - out_header_fmt3;
			pheader = out_header_fmt3;
		}
		
		// sendout header and payload by writev.
		// decrease the sys invoke count to get higher performance.
		int payload_size = msg->size - ((char*)msg->payload - p);
		if (payload_size > out_chunk_size) {
			payload_size = out_chunk_size;
		}
		
		// send by writev
		iovec iov[2];
		iov[0].iov_base = pheader;
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
		srs_error("read basic header failed. ret=%d", ret);
		return ret;
	}
	srs_info("read basic header success. fmt=%d, cid=%d, bh_size=%d", fmt, cid, bh_size);
	
	// get the cached chunk stream.
	SrsChunkStream* chunk = NULL;
	
	if (chunk_streams.find(cid) == chunk_streams.end()) {
		chunk = chunk_streams[cid] = new SrsChunkStream(cid);
		srs_info("cache new chunk stream: fmt=%d, cid=%d", fmt, cid);
	} else {
		chunk = chunk_streams[cid];
		srs_info("cached chunk stream: fmt=%d, cid=%d, size=%d, message(type=%d, size=%d, time=%d, sid=%d)",
			chunk->fmt, chunk->cid, (chunk->msg? chunk->msg->size : 0), chunk->header.message_type, chunk->header.payload_length,
			chunk->header.timestamp, chunk->header.stream_id);
	}

	// chunk stream message header
	int mh_size = 0;
	if ((ret = read_message_header(chunk, fmt, bh_size, mh_size)) != ERROR_SUCCESS) {
		srs_error("read message header failed. ret=%d", ret);
		return ret;
	}
	srs_info("read message header success. "
			"fmt=%d, mh_size=%d, ext_time=%d, size=%d, message(type=%d, size=%d, time=%d, sid=%d)", 
			fmt, mh_size, chunk->extended_timestamp, (chunk->msg? chunk->msg->size : 0), chunk->header.message_type, 
			chunk->header.payload_length, chunk->header.timestamp, chunk->header.stream_id);
	
	// read msg payload from chunk stream.
	SrsMessage* msg = NULL;
	int payload_size = 0;
	if ((ret = read_message_payload(chunk, bh_size, mh_size, payload_size, &msg)) != ERROR_SUCCESS) {
		srs_error("read message payload failed. ret=%d", ret);
		return ret;
	}
	
	// not got an entire RTMP message, try next chunk.
	if (!msg) {
		srs_info("get partial message success. chunk_payload_size=%d, size=%d, message(type=%d, size=%d, time=%d, sid=%d)",
				payload_size, (msg? msg->size : (chunk->msg? chunk->msg->size : 0)), chunk->header.message_type, chunk->header.payload_length,
				chunk->header.timestamp, chunk->header.stream_id);
		return ret;
	}
	
	*pmsg = msg;
	srs_info("get entire message success. chunk_payload_size=%d, size=%d, message(type=%d, size=%d, time=%d, sid=%d)",
			payload_size, (msg? msg->size : (chunk->msg? chunk->msg->size : 0)), chunk->header.message_type, chunk->header.payload_length,
			chunk->header.timestamp, chunk->header.stream_id);
			
	return ret;
}

int SrsProtocol::read_basic_header(char& fmt, int& cid, int& bh_size)
{
	int ret = ERROR_SUCCESS;
	
	int required_size = 1;
	if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
		srs_error("read 1bytes basic header failed. required_size=%d, ret=%d", required_size, ret);
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
			srs_error("read 2bytes basic header failed. required_size=%d, ret=%d", required_size, ret);
			return ret;
		}
		
		cid = 64;
		cid += *(++p);
    	bh_size = 2;
		srs_verbose("%dbytes basic header parsed. fmt=%d, cid=%d", bh_size, fmt, cid);
	} else if (cid == 1) {
		required_size = 3;
		if ((ret = buffer->ensure_buffer_bytes(skt, 3)) != ERROR_SUCCESS) {
			srs_error("read 3bytes basic header failed. required_size=%d, ret=%d", required_size, ret);
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
	
	// when not exists cached msg, means get an new message,
	// the fmt must be type0 which means new message.
	if (!chunk->msg && fmt != RTMP_FMT_TYPE0) {
		ret = ERROR_RTMP_CHUNK_START;
		srs_error("chunk stream start, "
			"fmt must be %d, actual is %d. ret=%d", RTMP_FMT_TYPE0, fmt, ret);
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
	if (!chunk->msg) {
		srs_assert(fmt == RTMP_FMT_TYPE0);
		chunk->msg = new SrsMessage();
		srs_verbose("create message for new chunk, fmt=%d, cid=%d", fmt, chunk->cid);
	}

	// read message header from socket to buffer.
	static char mh_sizes[] = {11, 7, 1, 0};
	mh_size = mh_sizes[(int)fmt];
	srs_verbose("calc chunk message header size. fmt=%d, mh_size=%d", fmt, mh_size);
	
	int required_size = bh_size + mh_size;
	if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
		srs_error("read %dbytes message header failed. required_size=%d, ret=%d", mh_size, required_size, ret);
		return ret;
	}
	char* p = buffer->bytes() + bh_size;
	
	// parse the message header.
	// see also: ngx_rtmp_recv
	if (fmt <= RTMP_FMT_TYPE2) {
		int32_t timestamp_delta;
        char* pp = (char*)&timestamp_delta;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        pp[3] = 0;
        
        if (fmt == RTMP_FMT_TYPE0) {
			// 6.1.2.1. Type 0
			// For a type-0 chunk, the absolute timestamp of the message is sent
			// here.
            chunk->header.timestamp = timestamp_delta;
        } else {
            // 6.1.2.2. Type 1
            // 6.1.2.3. Type 2
            // For a type-1 or type-2 chunk, the difference between the previous
            // chunk's timestamp and the current chunk's timestamp is sent here.
            chunk->header.timestamp += timestamp_delta;
        }
        
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
        chunk->extended_timestamp = (timestamp_delta >= RTMP_EXTENDED_TIMESTAMP);
        if (chunk->extended_timestamp) {
			chunk->header.timestamp = RTMP_EXTENDED_TIMESTAMP;
        }
        
        if (fmt <= RTMP_FMT_TYPE1) {
            pp = (char*)&chunk->header.payload_length;
            pp[2] = *p++;
            pp[1] = *p++;
            pp[0] = *p++;
            pp[3] = 0;
            
            chunk->header.message_type = *p++;
            
            if (fmt == 0) {
                pp = (char*)&chunk->header.stream_id;
                pp[0] = *p++;
                pp[1] = *p++;
                pp[2] = *p++;
                pp[3] = *p++;
				srs_verbose("header read completed. fmt=%d, mh_size=%d, ext_time=%d, time=%d, payload=%d, type=%d, sid=%d", 
					fmt, mh_size, chunk->extended_timestamp, chunk->header.timestamp, chunk->header.payload_length, 
					chunk->header.message_type, chunk->header.stream_id);
			} else {
				srs_verbose("header read completed. fmt=%d, mh_size=%d, ext_time=%d, time=%d, payload=%d, type=%d", 
					fmt, mh_size, chunk->extended_timestamp, chunk->header.timestamp, chunk->header.payload_length, 
					chunk->header.message_type);
			}
		} else {
			srs_verbose("header read completed. fmt=%d, mh_size=%d, ext_time=%d, time=%d", 
				fmt, mh_size, chunk->extended_timestamp, chunk->header.timestamp);
		}
	} else {
		srs_verbose("header read completed. fmt=%d, size=%d, ext_time=%d", 
			fmt, mh_size, chunk->extended_timestamp);
	}
	
	if (chunk->extended_timestamp) {
		mh_size += 4;
		required_size = bh_size + mh_size;
		srs_verbose("read header ext time. fmt=%d, ext_time=%d, mh_size=%d", fmt, chunk->extended_timestamp, mh_size);
		if ((ret = buffer->ensure_buffer_bytes(skt, required_size)) != ERROR_SUCCESS) {
			srs_error("read %dbytes message header failed. required_size=%d, ret=%d", mh_size, required_size, ret);
			return ret;
		}

        char* pp = (char*)&chunk->header.timestamp;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
		srs_verbose("header read ext_time completed. time=%d", chunk->header.timestamp);
	}
	
	// valid message
	if (chunk->header.payload_length < 0) {
		ret = ERROR_RTMP_MSG_INVLIAD_SIZE;
		srs_error("RTMP message size must not be negative. size=%d, ret=%d", 
			chunk->header.payload_length, ret);
		return ret;
	}
	
	// copy header to msg
	chunk->msg->header = chunk->header;
	
	return ret;
}

int SrsProtocol::read_message_payload(SrsChunkStream* chunk, int bh_size, int mh_size, int& payload_size, SrsMessage** pmsg)
{
	int ret = ERROR_SUCCESS;
	
	// empty message
	if (chunk->header.payload_length == 0) {
		// need erase the header in buffer.
		buffer->erase(bh_size + mh_size);
		
		srs_trace("get an empty RTMP "
				"message(type=%d, size=%d, time=%d, sid=%d)", chunk->header.message_type, 
				chunk->header.payload_length, chunk->header.timestamp, chunk->header.stream_id);
		
		*pmsg = chunk->msg;
		chunk->msg = NULL;
				
		return ret;
	}
	srs_assert(chunk->header.payload_length > 0);
	
	// the chunk payload size.
	payload_size = chunk->header.payload_length - chunk->msg->size;
	if (payload_size > in_chunk_size) {
		payload_size = in_chunk_size;
	}
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
		srs_error("read payload failed. required_size=%d, ret=%d", required_size, ret);
		return ret;
	}
	memcpy(chunk->msg->payload + chunk->msg->size, buffer->bytes() + bh_size + mh_size, payload_size);
	buffer->erase(bh_size + mh_size + payload_size);
	chunk->msg->size += payload_size;
	
	srs_verbose("chunk payload read complted. bh_size=%d, mh_size=%d, payload_size=%d", bh_size, mh_size, payload_size);
	
	// got entire RTMP message?
	if (chunk->header.payload_length == chunk->msg->size) {
		*pmsg = chunk->msg;
		chunk->msg = NULL;
		srs_verbose("get entire RTMP message(type=%d, size=%d, time=%d, sid=%d)", 
				chunk->header.message_type, chunk->header.payload_length, 
				chunk->header.timestamp, chunk->header.stream_id);
		return ret;
	}
	
	srs_verbose("get partial RTMP message(type=%d, size=%d, time=%d, sid=%d), partial size=%d", 
			chunk->header.message_type, chunk->header.payload_length, 
			chunk->header.timestamp, chunk->header.stream_id,
			chunk->msg->size);
	
	return ret;
}

SrsMessageHeader::SrsMessageHeader()
{
	message_type = 0;
	payload_length = 0;
	timestamp = 0;
	stream_id = 0;
}

SrsMessageHeader::~SrsMessageHeader()
{
}

SrsChunkStream::SrsChunkStream(int _cid)
{
	fmt = 0;
	cid = _cid;
	extended_timestamp = false;
	msg = NULL;
}

SrsChunkStream::~SrsChunkStream()
{
	if (msg) {
		delete msg;
		msg = NULL;
	}
}

SrsMessage::SrsMessage()
{
	size = 0;
	stream = NULL;
	payload = NULL;
	packet = NULL;
}

SrsMessage::~SrsMessage()
{	
	if (payload) {
		delete[] payload;
		payload = NULL;
	}
	
	if (packet) {
		delete packet;
		packet = NULL;
	}
	
	if (stream) {
		delete stream;
		stream = NULL;
	}
}

int SrsMessage::decode_packet()
{
	int ret = ERROR_SUCCESS;
	
	srs_assert(payload != NULL);
	srs_assert(size > 0);
	
	if (!stream) {
		srs_verbose("create decode stream for message.");
		stream = new SrsStream();
	}
	
	if (header.message_type == RTMP_MSG_AMF0CommandMessage) {
		srs_verbose("start to decode AMF0 command message.");
		
		// amf0 command message.
		// need to read the command name.
		if ((ret = stream->initialize((char*)payload, size)) != ERROR_SUCCESS) {
			srs_error("initialize stream failed. ret=%d", ret);
			return ret;
		}
		srs_verbose("decode stream initialized success");
		
		std::string command;
		if ((ret = srs_amf0_read_string(stream, command)) != ERROR_SUCCESS) {
			srs_error("decode AMF0 command name failed. ret=%d", ret);
			return ret;
		}
		srs_verbose("AMF0 command message, command_name=%s", command.c_str());
		
		stream->reset();
		if (command == RTMP_AMF0_COMMAND_CONNECT) {
			srs_info("decode the AMF0 command(connect vhost/app message).");
			packet = new SrsConnectAppPacket();
			return packet->decode(stream);
		}
		
		// default packet to drop message.
		srs_trace("drop the AMF0 command message, command_name=%s", command.c_str());
		packet = new SrsPacket();
		return ret;
	}
	
	// default packet to drop message.
	srs_trace("drop the unknown message, type=%d", header.message_type);
	packet = new SrsPacket();
	
	return ret;
}

SrsPacket* SrsMessage::get_packet()
{
	if (!packet) {
		srs_error("the payload is raw/undecoded, invoke decode_packet to decode it.");
	}
	srs_assert(packet != NULL);
	
	return packet;
}

int SrsMessage::get_perfer_cid()
{
	if (!packet) {
		return RTMP_CID_ProtocolControl;
	}
	
	// we donot use the complex basic header,
	// ensure the basic header is 1bytes.
	if (packet->get_perfer_cid() < 2) {
		return packet->get_perfer_cid();
	}
	
	return packet->get_perfer_cid();
}

void SrsMessage::set_packet(SrsPacket* pkt, int stream_id)
{
	if (packet) {
		delete packet;
	}

	packet = pkt;
	
	header.message_type = packet->get_message_type();
	header.payload_length = packet->get_payload_length();
	header.stream_id = stream_id;
}

int SrsMessage::encode_packet()
{
	int ret = ERROR_SUCCESS;
	
	if (packet == NULL) {
		srs_warn("packet is empty, send out empty message.");
		return ret;
	}
	// realloc the payload.
	size = 0;
	if (payload) {
		delete[] payload;
		payload = NULL;
	}
	
	return packet->encode(size, (char*&)payload);
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

int SrsPacket::get_payload_length()
{
	return get_size();
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
			delete[] payload;
			return ret;
		}
	}
	
	if ((ret = encode_packet(&stream)) != ERROR_SUCCESS) {
		srs_error("encode the packet failed. ret=%d", ret);
		delete[] payload;
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
	command_object = NULL;
}

SrsConnectAppPacket::~SrsConnectAppPacket()
{
	if (command_object) {
		delete command_object;
		command_object = NULL;
	}
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
	if (transaction_id != 1.0) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 decode connect transaction_id failed. "
			"required=%.1f, actual=%.1f, ret=%d", 1.0, transaction_id, ret);
		return ret;
	}
	
	if ((ret = srs_amf0_read_object(stream, command_object)) != ERROR_SUCCESS) {
		srs_error("amf0 decode connect command_object failed. ret=%d", ret);
		return ret;
	}
	if (command_object == NULL) {
		ret = ERROR_RTMP_AMF0_DECODE;
		srs_error("amf0 decode connect command_object failed. ret=%d", ret);
		return ret;
	}
	
	srs_info("amf0 decode connect packet success");
	
	return ret;
}

SrsConnectAppResPacket::SrsConnectAppResPacket()
{
	command_name = RTMP_AMF0_COMMAND_CONNECT;
	transaction_id = 1;
	props = new SrsAmf0Object();
	info = new SrsAmf0Object();
}

SrsConnectAppResPacket::~SrsConnectAppResPacket()
{
	if (props) {
		delete props;
		props = NULL;
	}
	
	if (info) {
		delete info;
		info = NULL;
	}
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
	return srs_amf0_get_string_size(command_name) + srs_amf0_get_number_size()
		+ srs_amf0_get_object_size(props)+ srs_amf0_get_object_size(info);
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
	
	if ((ret = srs_amf0_write_object(stream, props)) != ERROR_SUCCESS) {
		srs_error("encode props failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("encode props success.");
	
	if ((ret = srs_amf0_write_object(stream, info)) != ERROR_SUCCESS) {
		srs_error("encode info failed. ret=%d", ret);
		return ret;
	}
	srs_verbose("encode info success.");
	
	
	srs_info("encode connect app response packet success.");
	
	return ret;
}

SrsSetWindowAckSizePacket::SrsSetWindowAckSizePacket()
{
	ackowledgement_window_size = 0;
}

SrsSetWindowAckSizePacket::~SrsSetWindowAckSizePacket()
{
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

