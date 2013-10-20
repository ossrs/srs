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

#ifndef SRS_CORE_PROTOCOL_HPP
#define SRS_CORE_PROTOCOL_HPP

/*
#include <srs_core_protocol.hpp>
*/

#include <srs_core.hpp>

#include <map>
#include <string>

#include <st.h>

#include <srs_core_log.hpp>
#include <srs_core_error.hpp>

class SrsSocket;
class SrsBuffer;
class SrsPacket;
class SrsStream;
class SrsMessage;
class SrsChunkStream;
class SrsAmf0Object;

/**
* max rtmp header size:
* 	1bytes basic header,
* 	11bytes message header,
* 	4bytes timestamp header,
* that is, 1+11+4=16bytes.
*/
#define RTMP_MAX_FMT0_HEADER_SIZE 16
/**
* max rtmp header size:
* 	1bytes basic header,
* 	4bytes timestamp header,
* that is, 1+4=5bytes.
*/
#define RTMP_MAX_FMT3_HEADER_SIZE 5

/**
* the protocol provides the rtmp-message-protocol services,
* to recv RTMP message from RTMP chunk stream,
* and to send out RTMP message over RTMP chunk stream.
*/
class SrsProtocol
{
// peer in/out
private:
	st_netfd_t stfd;
	SrsSocket* skt;
	char* pp;
// peer in
private:
	std::map<int, SrsChunkStream*> chunk_streams;
	SrsBuffer* buffer;
	int32_t in_chunk_size;
// peer out
private:
	char out_header_fmt0[RTMP_MAX_FMT0_HEADER_SIZE];
	char out_header_fmt3[RTMP_MAX_FMT3_HEADER_SIZE];
	int32_t out_chunk_size;
public:
	SrsProtocol(st_netfd_t client_stfd);
	virtual ~SrsProtocol();
public:
	/**
	* recv a message with raw/undecoded payload from peer.
	* the payload is not decoded, use srs_rtmp_expect_message<T> if requires 
	* specifies message.
	* @pmsg, user must free it. NULL if not success.
	* @remark, only when success, user can use and must free the pmsg.
	*/
	virtual int recv_message(SrsMessage** pmsg);
	/**
	* send out message with encoded payload to peer.
	* use the message encode method to encode to payload,
	* then sendout over socket.
	* @msg this method will free it whatever return value.
	*/
	virtual int send_message(SrsMessage* msg);
private:
	/**
	* try to recv interlaced message from peer,
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
	* Four-byte field that contains a timestamp of the message.
	* The 4 bytes are packed in the big-endian order.
	*/
	int32_t timestamp;
	/**
	* Three-byte field that identifies the stream of the message. These
	* bytes are set in big-endian format.
	*/
	int32_t stream_id;
	
	SrsMessageHeader();
	virtual ~SrsMessageHeader();
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
public:
	SrsChunkStream(int _cid);
	virtual ~SrsChunkStream();
};

/**
* common RTMP message defines in rtmp.part2.Message-Formats.pdf.
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
// decoded message payload.
private:
	SrsStream* stream;
	SrsPacket* packet;
public:
	SrsMessage();
	virtual ~SrsMessage();
public:
	/**
	* decode packet from message payload.
	*/
	virtual int decode_packet();
	/**
	* get the decoded packet which decoded by decode_packet().
	* @remark, user never free the pkt, the message will auto free it.
	*/
	virtual SrsPacket* get_packet();
public:
	/**
	* get the perfered cid(chunk stream id) which sendout over.
	*/
	virtual int get_perfer_cid();
	/**
	* set the encoded packet to encode_packet() to payload.
	* @remark, user never free the pkt, the message will auto free it.
	*/
	virtual void set_packet(SrsPacket* pkt);
	/**
	* encode the packet to message payload bytes.
	* @remark there exists empty packet, so maybe the payload is NULL.
	*/
	virtual int encode_packet();
};

/**
* the decoded message payload.
*/
class SrsPacket
{
public:
	SrsPacket();
	virtual ~SrsPacket();
public:
	virtual int decode(SrsStream* stream);
public:
	virtual int get_perfer_cid();
};

/**
* 4.1.1. connect
* The client sends the connect command to the server to request
* connection to a server application instance.
*/
class SrsConnectAppPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Object* command_object;
public:
	SrsConnectAppPacket();
	virtual ~SrsConnectAppPacket();
public:
	virtual int decode(SrsStream* stream);
};

/**
* 5.5. Window Acknowledgement Size (5)
* The client or the server sends this message to inform the peer which
* window size to use when sending acknowledgment.
*/
class SrsSetWindowAckSizePacket : public SrsPacket
{
private:
	typedef SrsPacket super;
public:
	int32_t ackowledgement_window_size;
public:
	SrsSetWindowAckSizePacket();
	virtual ~SrsSetWindowAckSizePacket();
public:
	virtual int decode(SrsStream* stream);
public:
	virtual int get_perfer_cid();
};

/**
* expect a specified message, drop others util got specified one.
* @pmsg, user must free it. NULL if not success.
* @ppacket, store in the pmsg, user must never free it. NULL if not success.
* @remark, only when success, user can use and must free the pmsg/ppacket.
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
		
		if ((ret = msg->decode_packet()) != ERROR_SUCCESS) {
			delete msg;
			srs_error("decode message failed. ret=%d", ret);
			return ret;
		}
		
		T* pkt = dynamic_cast<T*>(msg->get_packet());
		if (!pkt) {
			delete msg;
			srs_trace("drop message(type=%d, size=%d, time=%d, sid=%d).", 
				msg->header.message_type, msg->header.payload_length,
				msg->header.timestamp, msg->header.stream_id);
			continue;
		}
		
		*pmsg = msg;
		*ppacket = pkt;
		break;
	}
	
	return ret;
}

#endif