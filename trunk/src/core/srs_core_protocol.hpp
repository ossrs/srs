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
class SrsCommonMessage;
class SrsChunkStream;
class SrsAmf0Object;
class SrsAmf0Null;
class SrsAmf0Undefined;
class ISrsMessage;

// convert class name to string.
#define CLASS_NAME_STRING(className) #className

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
private:
	struct AckWindowSize
	{
		int ack_window_size;
		int64_t acked_size;
		
		AckWindowSize();
	};
// peer in/out
private:
	st_netfd_t stfd;
	SrsSocket* skt;
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
	SrsBuffer* buffer;
	int32_t in_chunk_size;
	AckWindowSize in_ack_size;
// peer out
private:
	char out_header_fmt0[RTMP_MAX_FMT0_HEADER_SIZE];
	char out_header_fmt3[RTMP_MAX_FMT3_HEADER_SIZE];
	int32_t out_chunk_size;
public:
	SrsProtocol(st_netfd_t client_stfd);
	virtual ~SrsProtocol();
public:
	std::string get_request_name(double transcationId);
	/**
	* set the timeout in us.
	* if timeout, recv/send message return ERROR_SOCKET_TIMEOUT.
	*/
	virtual void set_recv_timeout(int64_t timeout_us);
	virtual int64_t get_recv_timeout();
	virtual void set_send_timeout(int64_t timeout_us);
	virtual int64_t get_recv_bytes();
	virtual int64_t get_send_bytes();
	virtual int get_recv_kbps();
	virtual int get_send_kbps();
	/**
	* recv a message with raw/undecoded payload from peer.
	* the payload is not decoded, use srs_rtmp_expect_message<T> if requires 
	* specifies message.
	* @pmsg, user must free it. NULL if not success.
	* @remark, only when success, user can use and must free the pmsg.
	*/
	virtual int recv_message(SrsCommonMessage** pmsg);
	/**
	* send out message with encoded payload to peer.
	* use the message encode method to encode to payload,
	* then sendout over socket.
	* @msg this method will free it whatever return value.
	*/
	virtual int send_message(ISrsMessage* msg);
private:
	/**
	* when recv message, update the context.
	*/
	virtual int on_recv_message(SrsCommonMessage* msg);
	virtual int response_acknowledgement_message();
	virtual int response_ping_message(int32_t timestamp);
	/**
	* when message sentout, update the context.
	*/
	virtual int on_send_message(ISrsMessage* msg);
	/**
	* try to recv interlaced message from peer,
	* return error if error occur and nerver set the pmsg,
	* return success and pmsg set to NULL if no entire message got,
	* return success and pmsg set to entire message if got one.
	*/
	virtual int recv_interlaced_message(SrsCommonMessage** pmsg);
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
	virtual int read_message_payload(SrsChunkStream* chunk, int bh_size, int mh_size, int& payload_size, SrsCommonMessage** pmsg);
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
	*/
	u_int32_t timestamp;
	
	SrsMessageHeader();
	virtual ~SrsMessageHeader();

	bool is_audio();
	bool is_video();
	bool is_amf0_command();
	bool is_amf0_data();
	bool is_amf3_command();
	bool is_amf3_data();
	bool is_window_ackledgement_size();
	bool is_set_chunk_size();
	bool is_user_control_message();
    bool is_windows_ackledgement();
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
	SrsCommonMessage* msg;
	/**
	* decoded msg count, to identify whether the chunk stream is fresh.
	*/
	int64_t msg_count;
public:
	SrsChunkStream(int _cid);
	virtual ~SrsChunkStream();
};

/**
* message to output.
*/
class ISrsMessage
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
public:
	ISrsMessage();
	virtual ~ISrsMessage();
public:
	/**
	* whether message canbe decoded.
	* only update the context when message canbe decoded.
	*/
	virtual bool can_decode() = 0;
/**
* encode functions.
*/
public:
	/**
	* get the perfered cid(chunk stream id) which sendout over.
	*/
	virtual int get_perfer_cid() = 0;
	/**
	* encode the packet to message payload bytes.
	* @remark there exists empty packet, so maybe the payload is NULL.
	*/
	virtual int encode_packet() = 0;
};

/**
* common RTMP message defines in rtmp.part2.Message-Formats.pdf.
* cannbe parse and decode.
*/
class SrsCommonMessage : public ISrsMessage
{
private:
	typedef ISrsMessage super;
// decoded message payload.
private:
	SrsStream* stream;
	SrsPacket* packet;
public:
	SrsCommonMessage();
	virtual ~SrsCommonMessage();
public:
	virtual bool can_decode();
/**
* decode functions.
*/
public:
	/**
	* decode packet from message payload.
	*/
	// TODO: use protocol to decode it.
	virtual int decode_packet(SrsProtocol* protocol);
	/**
	* get the decoded packet which decoded by decode_packet().
	* @remark, user never free the pkt, the message will auto free it.
	*/
	virtual SrsPacket* get_packet();
/**
* encode functions.
*/
public:
	/**
	* get the perfered cid(chunk stream id) which sendout over.
	*/
	virtual int get_perfer_cid();
	/**
	* set the encoded packet to encode_packet() to payload.
	* @stream_id, the id of stream which is created by createStream.
	* @remark, user never free the pkt, the message will auto free it.
	*/
	// TODO: refine the send methods.
	virtual void set_packet(SrsPacket* pkt, int stream_id);
	/**
	* encode the packet to message payload bytes.
	* @remark there exists empty packet, so maybe the payload is NULL.
	*/
	virtual int encode_packet();
};

/**
* shared ptr message.
* for audio/video/data message that need less memory copy.
* and only for output.
*/
class SrsSharedPtrMessage : public ISrsMessage
{
private:
	typedef ISrsMessage super;
private:
	struct SrsSharedPtr
	{
		char* payload;
		int size;
		int perfer_cid;
		int shared_count;
		
		SrsSharedPtr();
		virtual ~SrsSharedPtr();
	};
	SrsSharedPtr* ptr;
public:
	SrsSharedPtrMessage();
	virtual ~SrsSharedPtrMessage();
public:
	virtual bool can_decode();
public:
	/**
	* set the shared payload.
	* we will detach the payload of source,
	* so ensure donot use it before.
	*/
	virtual int initialize(SrsCommonMessage* source);
	/**
	* set the shared payload.
	* we will use the payload, donot use the payload of source.
	*/
	virtual int initialize(SrsCommonMessage* source, char* payload, int size);
	virtual SrsSharedPtrMessage* copy();
public:
	/**
	* get the perfered cid(chunk stream id) which sendout over.
	*/
	virtual int get_perfer_cid();
	/**
	* ignored.
	* for shared message, nothing should be done.
	* use initialize() to set the data.
	*/
	virtual int encode_packet();
};

/**
* the decoded message payload.
* @remark we seperate the packet from message,
*		for the packet focus on logic and domain data,
*		the message bind to the protocol and focus on protocol, such as header.
* 		we can merge the message and packet, using OOAD hierachy, packet extends from message,
* 		it's better for me to use components -- the message use the packet as payload.
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
	virtual int get_payload_length();
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsConnectAppPacket);
	}
public:
	std::string command_name;
	double transaction_id;
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
private:
	typedef SrsPacket super;
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsCreateStreamPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsCreateStreamResPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
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
* FMLE start publish: ReleaseStream/PublishStream
*/
class SrsFMLEStartPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsFMLEStartPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
	std::string stream_name;
public:
	SrsFMLEStartPacket();
	virtual ~SrsFMLEStartPacket();
public:
	virtual int decode(SrsStream* stream);
};
/**
* response for SrsFMLEStartPacket.
*/
class SrsFMLEStartResPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsFMLEStartResPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
	SrsAmf0Undefined* args;
public:
	SrsFMLEStartResPacket(double _transaction_id);
	virtual ~SrsFMLEStartResPacket();
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsPublishPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsPausePacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsPlayPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsPlayResPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* command_object;
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
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsOnBWDonePacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* args;
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
* band width check method name, which will be invoked by client.
* band width check mothods use SrsOnStatusCallPacket as its internal packet type,
* so ensure you set command name when you use it.
*/
// for play
#define SRS_BW_CHECK_START_PLAY         "onSrsBandCheckStartPlayBytes"
#define SRS_BW_CHECK_STARTING_PLAY      "onSrsBandCheckStartingPlayBytes"
#define SRS_BW_CHECK_STOP_PLAY          "onSrsBandCheckStopPlayBytes"
#define SRS_BW_CHECK_STOPPED_PLAY       "onSrsBandCheckStoppedPlayBytes"
#define SRS_BW_CHECK_PLAYING            "onSrsBandCheckPlaying"

// for publish
#define SRS_BW_CHECK_START_PUBLISH      "onSrsBandCheckStartPublishBytes"
#define SRS_BW_CHECK_STARTING_PUBLISH   "onSrsBandCheckStartingPublishBytes"
#define SRS_BW_CHECK_STOP_PUBLISH       "onSrsBandCheckStopPublishBytes"
#define SRS_BW_CHECK_FINISHED           "onSrsBandCheckFinished"
#define SRS_BW_CHECK_PUBLISHING         "onSrsBandCheckPublishing"

/**
* onStatus command, AMF0 Call
* @remark, user must set the stream_id by SrsMessage.set_packet().
*/
class SrsOnStatusCallPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
protected:
	virtual const char* get_class_name()
	{
		return CLASS_NAME_STRING(SrsOnStatusCallPacket);
	}
public:
	std::string command_name;
	double transaction_id;
	SrsAmf0Null* args;
	SrsAmf0Object* data;
public:
	SrsOnStatusCallPacket();
	virtual ~SrsOnStatusCallPacket();

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
* onStatus data, AMF0 Data
* @remark, user must set the stream_id by SrsMessage.set_packet().
*/
class SrsOnStatusDataPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
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
private:
	typedef SrsPacket super;
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
private:
	typedef SrsPacket super;
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
private:
	typedef SrsPacket super;
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
private:
	typedef SrsPacket super;
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
    virtual int decode(SrsStream *stream);

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
private:
	typedef SrsPacket super;
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
private:
	typedef SrsPacket super;
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
	SrcPCUCStreamBegin 			= 0x00,
	SrcPCUCStreamEOF 			= 0x01,
	SrcPCUCStreamDry 			= 0x02,
	SrcPCUCSetBufferLength 		= 0x03, // 8bytes event-data
	SrcPCUCStreamIsRecorded 	= 0x04,
	SrcPCUCPingRequest 			= 0x06,
	SrcPCUCPingResponse 		= 0x07,
};

/**
* for the EventData is 4bytes.
* Stream Begin(=0)			4-bytes stream ID
* Stream EOF(=1)			4-bytes stream ID
* StreamDry(=2)				4-bytes stream ID
* SetBufferLength(=3)		8-bytes 4bytes stream ID, 4bytes buffer length.
* StreamIsRecorded(=4)		4-bytes stream ID
* PingRequest(=6)			4-bytes timestamp local server time
* PingResponse(=7)			4-bytes timestamp received ping request.
* 
* 3.7. User Control message
* +------------------------------+-------------------------
* | Event Type ( 2- bytes ) | Event Data
* +------------------------------+-------------------------
* Figure 5 Pay load for the ‘User Control Message’.
*/
class SrsUserControlPacket : public SrsPacket
{
private:
	typedef SrsPacket super;
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
*/
template<class T>
int srs_rtmp_expect_message(SrsProtocol* protocol, SrsCommonMessage** pmsg, T** ppacket)
{
	*pmsg = NULL;
	*ppacket = NULL;
	
	int ret = ERROR_SUCCESS;
	
	while (true) {
		SrsCommonMessage* msg = NULL;
		if ((ret = protocol->recv_message(&msg)) != ERROR_SUCCESS) {
			srs_error("recv message failed. ret=%d", ret);
			return ret;
		}
		srs_verbose("recv message success.");
		
		if ((ret = msg->decode_packet(protocol)) != ERROR_SUCCESS) {
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
