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
	SrsPacket* decoded_payload;
public:
	/**
	* get the decoded packet,
	* not all packets need to decode, for video/audio packet,
	* passthrough to peer are ok.
	* @remark, user must invoke decode_packet first.
	*/
	virtual SrsPacket* get_packet();
	virtual int decode_packet();
public:
	SrsMessage();
	virtual ~SrsMessage();
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
};

class SrsConnectAppPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
private:
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
* the protocol provides the rtmp-message-protocol services,
* to recv RTMP message from RTMP chunk stream,
* and to send out RTMP message over RTMP chunk stream.
*/
class SrsProtocol
{
private:
	std::map<int, SrsChunkStream*> chunk_streams;
	st_netfd_t stfd;
	SrsBuffer* buffer;
	SrsSocket* skt;
	int32_t in_chunk_size;
	int32_t out_chunk_size;
public:
	SrsProtocol(st_netfd_t client_stfd);
	virtual ~SrsProtocol();
public:
	/**
	* recv a message with raw/undecoded payload from peer.
	* the payload is not decoded, use expect_message<T> if requires specifies message.
	* @pmsg, user must free it. NULL if not success.
	* @remark, only when success, user can use and must free the pmsg.
	*/
	virtual int recv_message(SrsMessage** pmsg);
public:
	/**
	* expect a specified message, drop others util got specified one.
	* @pmsg, user must free it. NULL if not success.
	* @ppacket, store in the pmsg, user must never free it. NULL if not success.
	* @remark, only when success, user can use and must free the pmsg/ppacket.
	*/
	template<class T>
	int expect_message(SrsMessage** pmsg, T** ppacket)
	{
		*pmsg = NULL;
		*ppacket = NULL;
		
		int ret = ERROR_SUCCESS;
		
		while (true) {
			SrsMessage* msg = NULL;
			if ((ret = recv_message(&msg)) != ERROR_SUCCESS) {
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
		}
		
		return ret;
	}
private:
	virtual int recv_interlaced_message(SrsMessage** pmsg);
	virtual int read_basic_header(char& fmt, int& cid, int& size);
	virtual int read_message_header(SrsChunkStream* chunk, char fmt, int bh_size, int& mh_size);
	virtual int read_message_payload(SrsChunkStream* chunk, int bh_size, int mh_size, int& payload_size, SrsMessage** pmsg);
};

#endif