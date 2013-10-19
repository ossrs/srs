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

#include <st.h>

class SrsSocket;
class SrsBuffer;
class SrsMessage;
class SrsChunkStream;

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
public:
	SrsProtocol(st_netfd_t client_stfd);
	virtual ~SrsProtocol();
public:
	virtual int recv_message(SrsMessage** pmsg);
private:
	virtual int read_basic_header(char& fmt, int& cid, int& size);
	virtual int read_message_header(SrsChunkStream* chunk, char fmt, SrsMessage** pmsg);
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
	* partially read message.
	*/
	SrsMessage* msg;
public:
	SrsChunkStream(int _cid);
	virtual ~SrsChunkStream();
public:
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
	int8_t* payload;
public:
	SrsMessage();
	virtual ~SrsMessage();
public:
};

#endif