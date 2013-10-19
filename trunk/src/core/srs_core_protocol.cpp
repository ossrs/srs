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
#include <srs_core_error.hpp>
#include <srs_core_socket.hpp>
#include <srs_core_buffer.hpp>

SrsProtocol::SrsProtocol(st_netfd_t client_stfd)
{
	stfd = client_stfd;
	buffer = new SrsBuffer();
	skt = new SrsSocket(stfd);
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
	int ret = ERROR_SUCCESS;
	
	while (true) {
		// chunk stream basic header.
		char fmt = 0;
		int cid = 0;
		int size = 0;
		if ((ret = read_basic_header(fmt, cid, size)) != ERROR_SUCCESS) {
			srs_error("read basic header failed. ret=%d", ret);
			return ret;
		}
		srs_info("read basic header success. fmt=%d, cid=%d, size=%d", fmt, cid, size);
		
		// get the cached chunk stream.
		SrsChunkStream* chunk = NULL;
		
		if (chunk_streams.find(cid) == chunk_streams.end()) {
			chunk = chunk_streams[cid] = new SrsChunkStream(cid);
			srs_info("cache new chunk stream: fmt=%d, cid=%d", fmt, cid);
		} else {
			chunk = chunk_streams[cid];
			srs_info("cached chunk stream: fmt=%d, cid=%d, message(type=%d, size=%d, time=%d, sid=%d)",
				chunk->fmt, chunk->cid, chunk->header.message_type, chunk->header.payload_length,
				chunk->header.timestamp, chunk->header.stream_id);
		}

		// chunk stream message header
		SrsMessage* msg = NULL;
		if ((ret = read_message_header(chunk, fmt, &msg)) != ERROR_SUCCESS) {
			srs_error("read message header failed. ret=%d", ret);
			return ret;
		}
		
		// not got an entire RTMP message, try next chunk.
		if (!msg) {
			continue;
		}
		
		// decode the msg
	}
	
	return ret;
}

int SrsProtocol::read_basic_header(char& fmt, int& cid, int& size)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = buffer->ensure_buffer_bytes(skt, 1)) != ERROR_SUCCESS) {
		return ret;
	}
	
	char* p = buffer->bytes();
	
    fmt = (*p >> 6) & 0x03;
    cid = *p & 0x3f;
    size = 1;
    
    if (cid > 1) {
        return ret;
    }

	if (cid == 0) {
		if ((ret = buffer->ensure_buffer_bytes(skt, 2)) != ERROR_SUCCESS) {
			return ret;
		}
		
		cid = 64;
		cid += *(++p);
    	size = 2;
	} else if (cid == 1) {
		if ((ret = buffer->ensure_buffer_bytes(skt, 3)) != ERROR_SUCCESS) {
			return ret;
		}
		
		cid = 64;
		cid += *(++p);
		cid += *(++p) * 256;
    	size = 3;
	} else {
		srs_error("invalid path, impossible basic header.");
		srs_assert(false);
	}
	
	return ret;
}

int SrsProtocol::read_message_header(SrsChunkStream* chunk, char fmt, SrsMessage** pmsg)
{
	int ret = ERROR_SUCCESS;
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
	payload = NULL;
}

SrsMessage::~SrsMessage()
{
	if (payload) {
		delete[] payload;
		payload = NULL;
	}
}

