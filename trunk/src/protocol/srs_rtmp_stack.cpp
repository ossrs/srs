/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_rtmp_stack.hpp>

#include <srs_protocol_amf0.hpp>
#include <srs_protocol_io.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_rtmp_handshake.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <stdlib.h>
using namespace std;

// FMLE
#define RTMP_AMF0_COMMAND_ON_FC_PUBLISH         "onFCPublish"
#define RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH       "onFCUnpublish"

// default stream id for response the createStream request.
#define SRS_DEFAULT_SID                         1

// when got a messae header, there must be some data,
// increase recv timeout to got an entire message.
#define SRS_MIN_RECV_TIMEOUT_US (int64_t)(60*1000*1000LL)

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
    
    SrsBuffer stream;
    
    if (size > 0) {
        payload = new char[size];
        
        if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
            srs_error("initialize the stream failed. ret=%d", ret);
            srs_freepa(payload);
            return ret;
        }
    }
    
    if ((ret = encode_packet(&stream)) != ERROR_SUCCESS) {
        srs_error("encode the packet failed. ret=%d", ret);
        srs_freepa(payload);
        return ret;
    }
    
    psize = size;
    ppayload = payload;
    srs_verbose("encode the packet success. size=%d", size);
    
    return ret;
}

int SrsPacket::decode(SrsBuffer* stream)
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

int SrsPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(stream != NULL);
    
    ret = ERROR_SYSTEM_PACKET_INVALID;
    srs_error("current packet is not support to encode. ret=%d", ret);
    
    return ret;
}

SrsProtocol::AckWindowSize::AckWindowSize()
{
    window = 0;
    sequence_number = nb_recv_bytes = 0;
}

SrsProtocol::SrsProtocol(ISrsProtocolReaderWriter* io)
{
    in_buffer = new SrsFastStream();
    skt = io;
    
    in_chunk_size = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
    out_chunk_size = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
    
    nb_out_iovs = SRS_CONSTS_IOVS_MAX;
    out_iovs = (iovec*)malloc(sizeof(iovec) * nb_out_iovs);
    // each chunk consumers atleast 2 iovs
    srs_assert(nb_out_iovs >= 2);
    
    warned_c0c3_cache_dry = false;
    auto_response_when_recv = true;
    show_debug_info = true;
    in_buffer_length = 0;
    
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
    srs_freepa(cs_cache);
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

void SrsProtocol::set_recv_timeout(int64_t tm)
{
    return skt->set_recv_timeout(tm);
}

int64_t SrsProtocol::get_recv_timeout()
{
    return skt->get_recv_timeout();
}

void SrsProtocol::set_send_timeout(int64_t tm)
{
    return skt->set_send_timeout(tm);
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

int SrsProtocol::set_in_window_ack_size(int ack_size)
{
    in_ack_size.window = ack_size;
    return ERROR_SUCCESS;
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
            srs_info("got empty message without error.");
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
    
    SrsBuffer stream;

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
    
#ifdef SRS_PERF_COMPLEX_SEND
    int iov_index = 0;
    iovec* iovs = out_iovs + iov_index;
    
    int c0c3_cache_index = 0;
    char* c0c3_cache = out_c0c3_caches + c0c3_cache_index;

    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        if (!msg) {
            continue;
        }
    
        // ignore empty message.
        if (!msg->payload || msg->size <= 0) {
            srs_info("ignore empty message.");
            continue;
        }
    
        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char* p = msg->payload;
        char* pend = msg->payload + msg->size;
        
        // always write the header event payload is empty.
        while (p < pend) {
            // always has header
            int nb_cache = SRS_CONSTS_C0C3_HEADERS_MAX - c0c3_cache_index;
            int nbh = msg->chunk_header(c0c3_cache, nb_cache, p == msg->payload);
            srs_assert(nbh > 0);
            
            // header iov
            iovs[0].iov_base = c0c3_cache;
            iovs[0].iov_len = nbh;
            
            // payload iov
            int payload_size = srs_min(out_chunk_size, (int)(pend - p));
            iovs[1].iov_base = p;
            iovs[1].iov_len = payload_size;
            
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
            iovs = out_iovs + iov_index;

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
                if ((ret = do_iovs_send(out_iovs, iov_index)) != ERROR_SUCCESS) {
                    return ret;
                }
    
                // reset caches, while these cache ensure 
                // atleast we can sendout a chunk.
                iov_index = 0;
                iovs = out_iovs + iov_index;
                
                c0c3_cache_index = 0;
                c0c3_cache = out_c0c3_caches + c0c3_cache_index;
            }
        }
    }
    
    // maybe the iovs already sendout when c0c3 cache dry,
    // so just ignore when no iovs to send.
    if (iov_index <= 0) {
        return ret;
    }
    srs_info("mw %d msgs in %d iovs, max_msgs=%d, nb_out_iovs=%d",
        nb_msgs, iov_index, SRS_PERF_MW_MSGS, nb_out_iovs);

    return do_iovs_send(out_iovs, iov_index);
#else
    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        if (!msg) {
            continue;
        }
    
        // ignore empty message.
        if (!msg->payload || msg->size <= 0) {
            srs_info("ignore empty message.");
            continue;
        }
    
        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char* p = msg->payload;
        char* pend = msg->payload + msg->size;
        
        // always write the header event payload is empty.
        while (p < pend) {
            // for simple send, send each chunk one by one
            iovec* iovs = out_iovs;
            char* c0c3_cache = out_c0c3_caches;
            int nb_cache = SRS_CONSTS_C0C3_HEADERS_MAX;
            
            // always has header
            int nbh = msg->chunk_header(c0c3_cache, nb_cache, p == msg->payload);
            srs_assert(nbh > 0);
            
            // header iov
            iovs[0].iov_base = c0c3_cache;
            iovs[0].iov_len = nbh;
            
            // payload iov
            int payload_size = srs_min(out_chunk_size, pend - p);
            iovs[1].iov_base = p;
            iovs[1].iov_len = payload_size;
            
            // consume sendout bytes.
            p += payload_size;

            if ((ret = skt->writev(iovs, 2, NULL)) != ERROR_SUCCESS) {
                if (!srs_is_client_gracefully_close(ret)) {
                    srs_error("send packet with writev failed. ret=%d", ret);
                }
                return ret;
            }
        }
    }
    
    return ret;
#endif   
}

int SrsProtocol::do_iovs_send(iovec* iovs, int size)
{
    return srs_write_large_iovs(skt, iovs, size);
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
    srs_freepa(payload);
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
        int nbh = 0;
        if (p == payload) {
            nbh = srs_chunk_header_c0(
                mh->perfer_cid, (uint32_t)mh->timestamp, mh->payload_length,
                mh->message_type, mh->stream_id,
                c0c3, sizeof(c0c3));
        } else {
            nbh = srs_chunk_header_c3(
                mh->perfer_cid, (uint32_t)mh->timestamp,
                c0c3, sizeof(c0c3));
        }
        srs_assert(nbh > 0);;
        
        iovec iovs[2];
        iovs[0].iov_base = c0c3;
        iovs[0].iov_len = nbh;
        
        int payload_size = srs_min((int)(end - p), out_chunk_size);
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

int SrsProtocol::do_decode_message(SrsMessageHeader& header, SrsBuffer* stream, SrsPacket** ppacket)
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
        } else if(command == SRS_CONSTS_RTMP_SET_DATAFRAME || command == SRS_CONSTS_RTMP_ON_METADATA) {
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
        
        if (!msg) {
            continue;
        }
        
        // check perfer cid and stream,
        // when one msg stream id is ok, ignore left.
        if (msg->check(stream_id)) {
            break;
        }
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
    
    print_debug_info();
    
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
    // @see https://github.com/ossrs/srs/issues/249
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
            "fmt=%d, ext_time=%d, size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)", 
            fmt, chunk->extended_timestamp, (chunk->msg? chunk->msg->size : 0), chunk->header.message_type, 
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
        srs_verbose("get partial message success. size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)",
                (msg? msg->size : (chunk->msg? chunk->msg->size : 0)), chunk->header.message_type, chunk->header.payload_length,
                chunk->header.timestamp, chunk->header.stream_id);
        return ret;
    }
    
    *pmsg = msg;
    srs_info("get entire message success. size=%d, message(type=%d, size=%d, time=%"PRId64", sid=%d)",
            (msg? msg->size : (chunk->msg? chunk->msg->size : 0)), chunk->header.message_type, chunk->header.payload_length,
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
* The bits 0-5 (least significant) in the chunk basic header represent
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
        cid += (uint8_t)in_buffer->read_1byte();
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
        cid += (uint8_t)in_buffer->read_1byte();
        cid += ((uint8_t)in_buffer->read_1byte()) * 256;
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
        // @see: https://github.com/ossrs/srs/issues/98
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
        // 'extended timestamp header' MUST be present. Otherwise, this value
        // SHOULD be the entire timestamp.
        //
        // fmt: 1 or 2
        // timestamp delta: 3 bytes
        // If the delta is greater than or equal to 16777215 (hexadecimal
        // 0x00ffffff), this value MUST be 16777215, and the 'extended
        // timestamp header' MUST be present. Otherwise, this value SHOULD be
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

        uint32_t timestamp = 0x00;
        char* pp = (char*)&timestamp;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;

        // always use 31bits timestamp, for some server may use 32bits extended timestamp.
        // @see https://github.com/ossrs/srs/issues/111
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
        uint32_t chunk_timestamp = (uint32_t)chunk->header.timestamp;
        
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
        chunk->msg->create_payload(chunk->header.payload_length);
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
        
    // try to response acknowledgement
    if ((ret = response_acknowledgement_message()) != ERROR_SUCCESS) {
        return ret;
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
        case RTMP_MSG_VideoMessage:
        case RTMP_MSG_AudioMessage:
            print_debug_info();
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
                in_ack_size.window = (uint32_t)pkt->ackowledgement_window_size;
                // @remark, we ignore this message, for user noneed to care.
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
            // @see: https://github.com/ossrs/srs/issues/160
            if (pkt->chunk_size < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE 
                || pkt->chunk_size > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE) 
            {
                srs_warn("accept chunk=%d, should in [%d, %d], please see #160",
                    pkt->chunk_size, SRS_CONSTS_RTMP_MIN_CHUNK_SIZE,  SRS_CONSTS_RTMP_MAX_CHUNK_SIZE);
            }

            // @see: https://github.com/ossrs/srs/issues/541
            if (pkt->chunk_size < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE) {
                ret = ERROR_RTMP_CHUNK_SIZE;
                srs_error("chunk size should be %d+, value=%d. ret=%d",
                    SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, pkt->chunk_size, ret);
                return ret;
            }
            
            in_chunk_size = pkt->chunk_size;
            srs_info("in.chunk=%d", pkt->chunk_size);

            break;
        }
        case RTMP_MSG_UserControlMessage: {
            SrsUserControlPacket* pkt = dynamic_cast<SrsUserControlPacket*>(packet);
            srs_assert(pkt != NULL);
            
            if (pkt->event_type == SrcPCUCSetBufferLength) {
                in_buffer_length = pkt->extra_data;
                srs_info("buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d", pkt->extra_data,
                    in_ack_size.window, out_ack_size.window, in_chunk_size, out_chunk_size);
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
            out_chunk_size = pkt->chunk_size;
            srs_info("out.chunk=%d", pkt->chunk_size);
            break;
        }
        case RTMP_MSG_WindowAcknowledgementSize: {
            SrsSetWindowAckSizePacket* pkt = dynamic_cast<SrsSetWindowAckSizePacket*>(packet);
            out_ack_size.window = (uint32_t)pkt->ackowledgement_window_size;
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
        case RTMP_MSG_VideoMessage:
        case RTMP_MSG_AudioMessage:
            print_debug_info();
        default:
            break;
    }
    
    return ret;
}

int SrsProtocol::response_acknowledgement_message()
{
    int ret = ERROR_SUCCESS;
    
    if (in_ack_size.window <= 0) {
        return ret;
    }
    
    // ignore when delta bytes not exceed half of window(ack size).
    uint32_t delta = (uint32_t)(skt->get_recv_bytes() - in_ack_size.nb_recv_bytes);
    if (delta < in_ack_size.window / 2) {
        return ret;
    }
    in_ack_size.nb_recv_bytes = skt->get_recv_bytes();
    
    // when the sequence number overflow, reset it.
    uint32_t sequence_number = in_ack_size.sequence_number + delta;
    if (sequence_number > 0xf0000000) {
        sequence_number = delta;
    }
    in_ack_size.sequence_number = sequence_number;
    
    SrsAcknowledgementPacket* pkt = new SrsAcknowledgementPacket();
    pkt->sequence_number = sequence_number;
    
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

void SrsProtocol::print_debug_info()
{
    if (show_debug_info) {
        show_debug_info = false;
        srs_trace("protocol in.buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d", in_buffer_length,
            in_ack_size.window, out_ack_size.window, in_chunk_size, out_chunk_size);
    }
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

SrsRequest::SrsRequest()
{
    objectEncoding = RTMP_SIG_AMF0_VER;
    duration = -1;
    port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    args = NULL;
}

SrsRequest::~SrsRequest()
{
    srs_freep(args);
}

SrsRequest* SrsRequest::copy()
{
    SrsRequest* cp = new SrsRequest();
    
    cp->ip = ip;
    cp->vhost = vhost;
    cp->app = app;
    cp->objectEncoding = objectEncoding;
    cp->pageUrl = pageUrl;
    cp->host = host;
    cp->port = port;
    cp->param = param;
    cp->schema = schema;
    cp->stream = stream;
    cp->swfUrl = swfUrl;
    cp->tcUrl = tcUrl;
    cp->duration = duration;
    if (args) {
        cp->args = args->copy()->to_object();
    }
    
    return cp;
}

void SrsRequest::update_auth(SrsRequest* req)
{
    pageUrl = req->pageUrl;
    swfUrl = req->swfUrl;
    tcUrl = req->tcUrl;
    
    ip = req->ip;
    vhost = req->vhost;
    app = req->app;
    objectEncoding = req->objectEncoding;
    host = req->host;
    port = req->port;
    param = req->param;
    schema = req->schema;
    duration = req->duration;
    
    if (args) {
        srs_freep(args);
    }
    if (req->args) {
        args = req->args->copy()->to_object();
    }
    
    srs_info("update req of soruce for auth ok");
}

string SrsRequest::get_stream_url()
{
    return srs_generate_stream_url(vhost, app, stream);
}

void SrsRequest::strip()
{
    // remove the unsupported chars in names.
    host = srs_string_remove(host, "/ \n\r\t");
    vhost = srs_string_remove(vhost, "/ \n\r\t");
    app = srs_string_remove(app, " \n\r\t");
    stream = srs_string_remove(stream, " \n\r\t");
    
    // remove end slash of app/stream
    app = srs_string_trim_end(app, "/");
    stream = srs_string_trim_end(stream, "/");
    
    // remove start slash of app/stream
    app = srs_string_trim_start(app, "/");
    stream = srs_string_trim_start(stream, "/");
}

SrsResponse::SrsResponse()
{
    stream_id = SRS_DEFAULT_SID;
}

SrsResponse::~SrsResponse()
{
}

string srs_client_type_string(SrsRtmpConnType type)
{
    switch (type) {
        case SrsRtmpConnPlay: return "Play";
        case SrsRtmpConnFlashPublish: return "flash-publish";
        case SrsRtmpConnFMLEPublish: return "fmle-publish";
        default: return "Unknown";
    }
}

bool srs_client_type_is_publish(SrsRtmpConnType type)
{
    return type != SrsRtmpConnPlay;
}

SrsHandshakeBytes::SrsHandshakeBytes()
{
    c0c1 = s0s1s2 = c2 = NULL;
}

SrsHandshakeBytes::~SrsHandshakeBytes()
{
    srs_freepa(c0c1);
    srs_freepa(s0s1s2);
    srs_freepa(c2);
}

int SrsHandshakeBytes::read_c0c1(ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    
    if (c0c1) {
        return ret;
    }
    
    ssize_t nsize;
    
    c0c1 = new char[1537];
    if ((ret = io->read_fully(c0c1, 1537, &nsize)) != ERROR_SUCCESS) {
        srs_warn("read c0c1 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("read c0c1 success.");
    
    return ret;
}

int SrsHandshakeBytes::read_s0s1s2(ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    
    if (s0s1s2) {
        return ret;
    }
    
    ssize_t nsize;
    
    s0s1s2 = new char[3073];
    if ((ret = io->read_fully(s0s1s2, 3073, &nsize)) != ERROR_SUCCESS) {
        srs_warn("read s0s1s2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("read s0s1s2 success.");
    
    return ret;
}

int SrsHandshakeBytes::read_c2(ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    
    if (c2) {
        return ret;
    }
    
    ssize_t nsize;
    
    c2 = new char[1536];
    if ((ret = io->read_fully(c2, 1536, &nsize)) != ERROR_SUCCESS) {
        srs_warn("read c2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("read c2 success.");
    
    return ret;
}

int SrsHandshakeBytes::create_c0c1()
{
    int ret = ERROR_SUCCESS;
    
    if (c0c1) {
        return ret;
    }
    
    c0c1 = new char[1537];
    srs_random_generate(c0c1, 1537);
    
    // plain text required.
    SrsBuffer stream;
    if ((ret = stream.initialize(c0c1, 9)) != ERROR_SUCCESS) {
        return ret;
    }
    stream.write_1bytes(0x03);
    stream.write_4bytes((int32_t)::time(NULL));
    stream.write_4bytes(0x00);
    
    return ret;
}

int SrsHandshakeBytes::create_s0s1s2(const char* c1)
{
    int ret = ERROR_SUCCESS;
    
    if (s0s1s2) {
        return ret;
    }
    
    s0s1s2 = new char[3073];
    srs_random_generate(s0s1s2, 3073);
    
    // plain text required.
    SrsBuffer stream;
    if ((ret = stream.initialize(s0s1s2, 9)) != ERROR_SUCCESS) {
        return ret;
    }
    stream.write_1bytes(0x03);
    stream.write_4bytes((int32_t)::time(NULL));
    // s1 time2 copy from c1
    if (c0c1) {
        stream.write_bytes(c0c1 + 1, 4);
    }
    
    // if c1 specified, copy c1 to s2.
    // @see: https://github.com/ossrs/srs/issues/46
    if (c1) {
        memcpy(s0s1s2 + 1537, c1, 1536);
    }
    
    return ret;
}

int SrsHandshakeBytes::create_c2()
{
    int ret = ERROR_SUCCESS;
    
    if (c2) {
        return ret;
    }
    
    c2 = new char[1536];
    srs_random_generate(c2, 1536);
    
    // time
    SrsBuffer stream;
    if ((ret = stream.initialize(c2, 8)) != ERROR_SUCCESS) {
        return ret;
    }
    stream.write_4bytes((int32_t)::time(NULL));
    // c2 time2 copy from s1
    if (s0s1s2) {
        stream.write_bytes(s0s1s2 + 1, 4);
    }
    
    return ret;
}

SrsServerInfo::SrsServerInfo()
{
    pid = cid = 0;
    major = minor = revision = build = 0;
}

SrsRtmpClient::SrsRtmpClient(ISrsProtocolReaderWriter* skt)
{
    io = skt;
    protocol = new SrsProtocol(skt);
    hs_bytes = new SrsHandshakeBytes();
}

SrsRtmpClient::~SrsRtmpClient()
{
    srs_freep(protocol);
    srs_freep(hs_bytes);
}

void SrsRtmpClient::set_recv_timeout(int64_t tm)
{
    protocol->set_recv_timeout(tm);
}

void SrsRtmpClient::set_send_timeout(int64_t tm)
{
    protocol->set_send_timeout(tm);
}

int64_t SrsRtmpClient::get_recv_bytes()
{
    return protocol->get_recv_bytes();
}

int64_t SrsRtmpClient::get_send_bytes()
{
    return protocol->get_send_bytes();
}

int SrsRtmpClient::recv_message(SrsCommonMessage** pmsg)
{
    return protocol->recv_message(pmsg);
}

int SrsRtmpClient::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    return protocol->decode_message(msg, ppacket);
}

int SrsRtmpClient::send_and_free_message(SrsSharedPtrMessage* msg, int stream_id)
{
    return protocol->send_and_free_message(msg, stream_id);
}

int SrsRtmpClient::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id)
{
    return protocol->send_and_free_messages(msgs, nb_msgs, stream_id);
}

int SrsRtmpClient::send_and_free_packet(SrsPacket* packet, int stream_id)
{
    return protocol->send_and_free_packet(packet, stream_id);
}

int SrsRtmpClient::handshake()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(hs_bytes);
    
    // maybe st has problem when alloc object on stack, always alloc object at heap.
    // @see https://github.com/ossrs/srs/issues/509
    SrsComplexHandshake* complex_hs = new SrsComplexHandshake();
    SrsAutoFree(SrsComplexHandshake, complex_hs);
    
    if ((ret = complex_hs->handshake_with_server(hs_bytes, io)) != ERROR_SUCCESS) {
        if (ret == ERROR_RTMP_TRY_SIMPLE_HS) {
            // always alloc object at heap.
            // @see https://github.com/ossrs/srs/issues/509
            SrsSimpleHandshake* simple_hs = new SrsSimpleHandshake();
            SrsAutoFree(SrsSimpleHandshake, simple_hs);
            
            if ((ret = simple_hs->handshake_with_server(hs_bytes, io)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        return ret;
    }
    
    srs_freep(hs_bytes);
    
    return ret;
}

int SrsRtmpClient::simple_handshake()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(hs_bytes);
    
    SrsSimpleHandshake simple_hs;
    if ((ret = simple_hs.handshake_with_server(hs_bytes, io)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_freep(hs_bytes);
    
    return ret;
}

int SrsRtmpClient::complex_handshake()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(hs_bytes);
    
    SrsComplexHandshake complex_hs;
    if ((ret = complex_hs.handshake_with_server(hs_bytes, io)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_freep(hs_bytes);
    
    return ret;
}

int SrsRtmpClient::connect_app(string app, string tcUrl, SrsRequest* r, bool dsu, SrsServerInfo* si)
{
    int ret = ERROR_SUCCESS;
    
    // Connect(vhost, app)
    if (true) {
        SrsConnectAppPacket* pkt = new SrsConnectAppPacket();
        
        pkt->command_object->set("app", SrsAmf0Any::str(app.c_str()));
        pkt->command_object->set("flashVer", SrsAmf0Any::str("WIN 15,0,0,239"));
        if (r) {
            pkt->command_object->set("swfUrl", SrsAmf0Any::str(r->swfUrl.c_str()));
        } else {
            pkt->command_object->set("swfUrl", SrsAmf0Any::str());
        }
        if (r && r->tcUrl != "") {
            pkt->command_object->set("tcUrl", SrsAmf0Any::str(r->tcUrl.c_str()));
        } else {
            pkt->command_object->set("tcUrl", SrsAmf0Any::str(tcUrl.c_str()));
        }
        pkt->command_object->set("fpad", SrsAmf0Any::boolean(false));
        pkt->command_object->set("capabilities", SrsAmf0Any::number(239));
        pkt->command_object->set("audioCodecs", SrsAmf0Any::number(3575));
        pkt->command_object->set("videoCodecs", SrsAmf0Any::number(252));
        pkt->command_object->set("videoFunction", SrsAmf0Any::number(1));
        if (r) {
            pkt->command_object->set("pageUrl", SrsAmf0Any::str(r->pageUrl.c_str()));
        } else {
            pkt->command_object->set("pageUrl", SrsAmf0Any::str());
        }
        pkt->command_object->set("objectEncoding", SrsAmf0Any::number(0));
        
        // @see https://github.com/ossrs/srs/issues/160
        // the debug_srs_upnode is config in vhost and default to true.
        if (dsu && r && r->args) {
            srs_freep(pkt->args);
            pkt->args = r->args->copy()->to_object();
        }
        
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // Set Window Acknowledgement size(2500000)
    if (true) {
        SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
        pkt->ackowledgement_window_size = 2500000;
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // expect connect _result
    SrsCommonMessage* msg = NULL;
    SrsConnectAppResPacket* pkt = NULL;
    if ((ret = expect_message<SrsConnectAppResPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
        srs_error("expect connect app response message failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsCommonMessage, msg);
    SrsAutoFree(SrsConnectAppResPacket, pkt);
    
    // server info
    SrsAmf0Any* data = pkt->info->get_property("data");
    if (si && data && data->is_ecma_array()) {
        SrsAmf0EcmaArray* arr = data->to_ecma_array();
        
        SrsAmf0Any* prop = NULL;
        if ((prop = arr->ensure_property_string("srs_server_ip")) != NULL) {
            si->ip = prop->to_str();
        }
        if ((prop = arr->ensure_property_string("srs_server")) != NULL) {
            si->sig = prop->to_str();
        }
        if ((prop = arr->ensure_property_number("srs_id")) != NULL) {
            si->cid = (int)prop->to_number();
        }
        if ((prop = arr->ensure_property_number("srs_pid")) != NULL) {
            si->pid = (int)prop->to_number();
        }
        if ((prop = arr->ensure_property_string("srs_version")) != NULL) {
            vector<string> versions = srs_string_split(prop->to_str(), ".");
            if (versions.size() > 0) {
                si->major = ::atoi(versions.at(0).c_str());
                if (versions.size() > 1) {
                    si->minor = ::atoi(versions.at(1).c_str());
                    if (versions.size() > 2) {
                        si->revision = ::atoi(versions.at(2).c_str());
                        if (versions.size() > 3) {
                            si->build = ::atoi(versions.at(3).c_str());
                        }
                    }
                }
            }
        }
    }
    
    if (si) {
        srs_trace("connected, version=%d.%d.%d.%d, ip=%s, pid=%d, id=%d, dsu=%d",
            si->major, si->minor, si->revision, si->build, si->ip.c_str(), si->pid, si->cid, dsu);
    } else {
        srs_trace("connected, dsu=%d", dsu);
    }
    
    return ret;
}

int SrsRtmpClient::create_stream(int& stream_id)
{
    int ret = ERROR_SUCCESS;
    
    // CreateStream
    if (true) {
        SrsCreateStreamPacket* pkt = new SrsCreateStreamPacket();
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    // CreateStream _result.
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCreateStreamResPacket* pkt = NULL;
        if ((ret = expect_message<SrsCreateStreamResPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect create stream response message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCreateStreamResPacket, pkt);
        srs_info("get create stream response message");
        
        stream_id = (int)pkt->stream_id;
    }
    
    return ret;
}

int SrsRtmpClient::play(string stream, int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    // Play(stream)
    if (true) {
        SrsPlayPacket* pkt = new SrsPlayPacket();
        pkt->stream_name = stream;
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send play stream failed. "
                "stream=%s, stream_id=%d, ret=%d", 
                stream.c_str(), stream_id, ret);
            return ret;
        }
    }
    
    // SetBufferLength(1000ms)
    int buffer_length_ms = 1000;
    if (true) {
        SrsUserControlPacket* pkt = new SrsUserControlPacket();
        
        pkt->event_type = SrcPCUCSetBufferLength;
        pkt->event_data = stream_id;
        pkt->extra_data = buffer_length_ms;
        
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send set buffer length failed. "
                "stream=%s, stream_id=%d, bufferLength=%d, ret=%d", 
                stream.c_str(), stream_id, buffer_length_ms, ret);
            return ret;
        }
    }
    
    // SetChunkSize
    if (true) {
        SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size = SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send set chunk size failed. "
                "stream=%s, chunk_size=%d, ret=%d", 
                stream.c_str(), SRS_CONSTS_RTMP_SRS_CHUNK_SIZE, ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsRtmpClient::publish(string stream, int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    // SetChunkSize
    if (true) {
        SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size = SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send set chunk size failed. "
                "stream=%s, chunk_size=%d, ret=%d", 
                stream.c_str(), SRS_CONSTS_RTMP_SRS_CHUNK_SIZE, ret);
            return ret;
        }
    }
    
    // publish(stream)
    if (true) {
        SrsPublishPacket* pkt = new SrsPublishPacket();
        pkt->stream_name = stream;
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send publish message failed. "
                "stream=%s, stream_id=%d, ret=%d", 
                stream.c_str(), stream_id, ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsRtmpClient::fmle_publish(string stream, int& stream_id)
{
    stream_id = 0;
    
    int ret = ERROR_SUCCESS;
    
    // SrsFMLEStartPacket
    if (true) {
        SrsFMLEStartPacket* pkt = SrsFMLEStartPacket::create_release_stream(stream);
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send FMLE publish "
                "release stream failed. stream=%s, ret=%d", stream.c_str(), ret);
            return ret;
        }
    }
    
    // FCPublish
    if (true) {
        SrsFMLEStartPacket* pkt = SrsFMLEStartPacket::create_FC_publish(stream);
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send FMLE publish "
                "FCPublish failed. stream=%s, ret=%d", stream.c_str(), ret);
            return ret;
        }
    }
    
    // CreateStream
    if (true) {
        SrsCreateStreamPacket* pkt = new SrsCreateStreamPacket();
        pkt->transaction_id = 4;
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send FMLE publish "
                "createStream failed. stream=%s, ret=%d", stream.c_str(), ret);
            return ret;
        }
    }
    
    // expect result of CreateStream
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCreateStreamResPacket* pkt = NULL;
        if ((ret = expect_message<SrsCreateStreamResPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect create stream response message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCreateStreamResPacket, pkt);
        srs_info("get create stream response message");

        stream_id = (int)pkt->stream_id;
    }
    
    // publish(stream)
    if (true) {
        SrsPublishPacket* pkt = new SrsPublishPacket();
        pkt->stream_name = stream;
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send FMLE publish publish failed. "
                "stream=%s, stream_id=%d, ret=%d", stream.c_str(), stream_id, ret);
            return ret;
        }
    }
    
    return ret;
}

SrsRtmpServer::SrsRtmpServer(ISrsProtocolReaderWriter* skt)
{
    io = skt;
    protocol = new SrsProtocol(skt);
    hs_bytes = new SrsHandshakeBytes();
}

SrsRtmpServer::~SrsRtmpServer()
{
    srs_freep(protocol);
    srs_freep(hs_bytes);
}

void SrsRtmpServer::set_auto_response(bool v)
{
    protocol->set_auto_response(v);
}

#ifdef SRS_PERF_MERGED_READ
void SrsRtmpServer::set_merge_read(bool v, IMergeReadHandler* handler)
{
    protocol->set_merge_read(v, handler);
}

void SrsRtmpServer::set_recv_buffer(int buffer_size)
{
    protocol->set_recv_buffer(buffer_size);
}
#endif

void SrsRtmpServer::set_recv_timeout(int64_t tm)
{
    protocol->set_recv_timeout(tm);
}

int64_t SrsRtmpServer::get_recv_timeout()
{
    return protocol->get_recv_timeout();
}

void SrsRtmpServer::set_send_timeout(int64_t tm)
{
    protocol->set_send_timeout(tm);
}

int64_t SrsRtmpServer::get_send_timeout()
{
    return protocol->get_send_timeout();
}

int64_t SrsRtmpServer::get_recv_bytes()
{
    return protocol->get_recv_bytes();
}

int64_t SrsRtmpServer::get_send_bytes()
{
    return protocol->get_send_bytes();
}

int SrsRtmpServer::recv_message(SrsCommonMessage** pmsg)
{
    return protocol->recv_message(pmsg);
}

int SrsRtmpServer::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    return protocol->decode_message(msg, ppacket);
}

int SrsRtmpServer::send_and_free_message(SrsSharedPtrMessage* msg, int stream_id)
{
    return protocol->send_and_free_message(msg, stream_id);
}

int SrsRtmpServer::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id)
{
    return protocol->send_and_free_messages(msgs, nb_msgs, stream_id);
}

int SrsRtmpServer::send_and_free_packet(SrsPacket* packet, int stream_id)
{
    return protocol->send_and_free_packet(packet, stream_id);
}

int SrsRtmpServer::handshake()
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(hs_bytes);
    
    SrsComplexHandshake complex_hs;
    if ((ret = complex_hs.handshake_with_client(hs_bytes, io)) != ERROR_SUCCESS) {
        if (ret == ERROR_RTMP_TRY_SIMPLE_HS) {
            SrsSimpleHandshake simple_hs;
            if ((ret = simple_hs.handshake_with_client(hs_bytes, io)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        return ret;
    }
    
    srs_freep(hs_bytes);
    
    return ret;
}

int SrsRtmpServer::connect_app(SrsRequest* req)
{
    int ret = ERROR_SUCCESS;
    
    SrsCommonMessage* msg = NULL;
    SrsConnectAppPacket* pkt = NULL;
    if ((ret = expect_message<SrsConnectAppPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
        srs_error("expect connect app message failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsCommonMessage, msg);
    SrsAutoFree(SrsConnectAppPacket, pkt);
    srs_info("get connect app message");
    
    SrsAmf0Any* prop = NULL;
    
    if ((prop = pkt->command_object->ensure_property_string("tcUrl")) == NULL) {
        ret = ERROR_RTMP_REQ_CONNECT;
        srs_error("invalid request, must specifies the tcUrl. ret=%d", ret);
        return ret;
    }
    req->tcUrl = prop->to_str();
    
    if ((prop = pkt->command_object->ensure_property_string("pageUrl")) != NULL) {
        req->pageUrl = prop->to_str();
    }
    
    if ((prop = pkt->command_object->ensure_property_string("swfUrl")) != NULL) {
        req->swfUrl = prop->to_str();
    }
    
    if ((prop = pkt->command_object->ensure_property_number("objectEncoding")) != NULL) {
        req->objectEncoding = prop->to_number();
    }
    
    if (pkt->args) {
        srs_freep(req->args);
        req->args = pkt->args->copy()->to_object();
        srs_info("copy edge traverse to origin auth args.");
    }
    
    srs_info("get connect app message params success.");
    
    srs_discovery_tc_url(req->tcUrl, 
        req->schema, req->host, req->vhost, req->app, req->port,
        req->param);
    req->strip();
    
    return ret;
}

int SrsRtmpServer::set_window_ack_size(int ack_size)
{
    int ret = ERROR_SUCCESS;
    
    SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
    pkt->ackowledgement_window_size = ack_size;
    if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send ack size message failed. ret=%d", ret);
        return ret;
    }
    srs_info("send ack size message success. ack_size=%d", ack_size);
    
    return ret;
}

int SrsRtmpServer::set_in_window_ack_size(int ack_size)
{
    return protocol->set_in_window_ack_size(ack_size);
}

int SrsRtmpServer::set_peer_bandwidth(int bandwidth, int type)
{
    int ret = ERROR_SUCCESS;
    
    SrsSetPeerBandwidthPacket* pkt = new SrsSetPeerBandwidthPacket();
    pkt->bandwidth = bandwidth;
    pkt->type = type;
    if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send set bandwidth message failed. ret=%d", ret);
        return ret;
    }
    srs_info("send set bandwidth message "
        "success. bandwidth=%d, type=%d", bandwidth, type);
    
    return ret;
}

int SrsRtmpServer::response_connect_app(SrsRequest *req, const char* server_ip)
{
    int ret = ERROR_SUCCESS;
    
    SrsConnectAppResPacket* pkt = new SrsConnectAppResPacket();
    
    pkt->props->set("fmsVer", SrsAmf0Any::str("FMS/"RTMP_SIG_FMS_VER));
    pkt->props->set("capabilities", SrsAmf0Any::number(127));
    pkt->props->set("mode", SrsAmf0Any::number(1));
    
    pkt->info->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
    pkt->info->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectSuccess));
    pkt->info->set(StatusDescription, SrsAmf0Any::str("Connection succeeded"));
    pkt->info->set("objectEncoding", SrsAmf0Any::number(req->objectEncoding));
    SrsAmf0EcmaArray* data = SrsAmf0Any::ecma_array();
    pkt->info->set("data", data);
    
    data->set("version", SrsAmf0Any::str(RTMP_SIG_FMS_VER));
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_role", SrsAmf0Any::str(RTMP_SIG_SRS_ROLE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    data->set("srs_site", SrsAmf0Any::str(RTMP_SIG_SRS_WEB));
    data->set("srs_email", SrsAmf0Any::str(RTMP_SIG_SRS_EMAIL));
    data->set("srs_copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("srs_primary", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY));
    data->set("srs_authors", SrsAmf0Any::str(RTMP_SIG_SRS_AUTHROS));
    
    if (server_ip) {
        data->set("srs_server_ip", SrsAmf0Any::str(server_ip));
    }
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::number(_srs_context->get_id()));
    
    if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send connect app response message failed. ret=%d", ret);
        return ret;
    }
    srs_info("send connect app response message success.");
    
    return ret;
}

#define SRS_RTMP_REDIRECT_TMMS 3000
int SrsRtmpServer::redirect(SrsRequest* r, string host, int port, bool& accepted)
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        string url = srs_generate_rtmp_url(host, port, r->vhost, r->app, "");
        
        SrsAmf0Object* ex = SrsAmf0Any::object();
        ex->set("code", SrsAmf0Any::number(302));
        ex->set("redirect", SrsAmf0Any::str(url.c_str()));
        
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelError));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectRejected));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("RTMP 302 Redirect"));
        pkt->data->set("ex", ex);
        
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send redirect/rejected message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send redirect/rejected message success.");
    }
    
    // client must response a call message.
    // or we never know whether the client is ok to redirect.
    protocol->set_recv_timeout(SRS_RTMP_REDIRECT_TMMS);
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCallPacket* pkt = NULL;
        if ((ret = expect_message<SrsCallPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            // ignore any error of redirect response.
            return ERROR_SUCCESS;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCallPacket, pkt);
        
        string message;
        if (pkt->arguments && pkt->arguments->is_string()) {
            message = pkt->arguments->to_str();
            srs_info("confirm redirected to %s", message.c_str());
            accepted = true;
        }
        srs_info("get redirect response message");
    }
    
    return ret;
}

void SrsRtmpServer::response_connect_reject(SrsRequest* /*req*/, const char* desc)
{
    int ret = ERROR_SUCCESS;
    
    SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
    pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelError));
    pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectRejected));
    pkt->data->set(StatusDescription, SrsAmf0Any::str(desc));
    
    if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send connect app response rejected message failed. ret=%d", ret);
        return;
    }
    srs_info("send connect app response rejected message success.");

    return;
}

int SrsRtmpServer::on_bw_done()
{
    int ret = ERROR_SUCCESS;
    
    SrsOnBWDonePacket* pkt = new SrsOnBWDonePacket();
    if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send onBWDone message failed. ret=%d", ret);
        return ret;
    }
    srs_info("send onBWDone message success.");
    
    return ret;
}

int SrsRtmpServer::identify_client(int stream_id, SrsRtmpConnType& type, string& stream_name, double& duration)
{
    type = SrsRtmpConnUnknown;
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        if ((ret = protocol->recv_message(&msg)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("recv identify client message failed. ret=%d", ret);
            }
            return ret;
        }

        SrsAutoFree(SrsCommonMessage, msg);
        SrsMessageHeader& h = msg->header;
        
        if (h.is_ackledgement() || h.is_set_chunk_size() || h.is_window_ackledgement_size() || h.is_user_control_message()) {
            continue;
        }
        
        if (!h.is_amf0_command() && !h.is_amf3_command()) {
            srs_trace("identify ignore messages except "
                "AMF0/AMF3 command message. type=%#x", h.message_type);
            continue;
        }
        
        SrsPacket* pkt = NULL;
        if ((ret = protocol->decode_message(msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("identify decode message failed. ret=%d", ret);
            return ret;
        }
        
        SrsAutoFree(SrsPacket, pkt);
        
        if (dynamic_cast<SrsCreateStreamPacket*>(pkt)) {
            srs_info("identify client by create stream, play or flash publish.");
            return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket*>(pkt), stream_id, type, stream_name, duration);
        }
        if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
            srs_info("identify client by releaseStream, fmle publish.");
            return identify_fmle_publish_client(dynamic_cast<SrsFMLEStartPacket*>(pkt), type, stream_name);
        }
        if (dynamic_cast<SrsPlayPacket*>(pkt)) {
            srs_info("level0 identify client by play.");
            return identify_play_client(dynamic_cast<SrsPlayPacket*>(pkt), type, stream_name, duration);
        }
        // call msg,
        // support response null first,
        // @see https://github.com/ossrs/srs/issues/106
        // TODO: FIXME: response in right way, or forward in edge mode.
        SrsCallPacket* call = dynamic_cast<SrsCallPacket*>(pkt);
        if (call) {
            SrsCallResPacket* res = new SrsCallResPacket(call->transaction_id);
            res->command_object = SrsAmf0Any::null();
            res->response = SrsAmf0Any::null();
            if ((ret = protocol->send_and_free_packet(res, 0)) != ERROR_SUCCESS) {
                if (!srs_is_system_control_error(ret) && !srs_is_client_gracefully_close(ret)) {
                    srs_warn("response call failed. ret=%d", ret);
                }
                return ret;
            }
            continue;
        }
        
        srs_trace("ignore AMF0/AMF3 command message.");
    }
    
    return ret;
}

int SrsRtmpServer::set_chunk_size(int chunk_size)
{
    int ret = ERROR_SUCCESS;
    
    SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
    pkt->chunk_size = chunk_size;
    if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send set chunk size message failed. ret=%d", ret);
        return ret;
    }
    srs_info("send set chunk size message success. chunk_size=%d", chunk_size);
    
    return ret;
}

int SrsRtmpServer::start_play(int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    // StreamBegin
    if (true) {
        SrsUserControlPacket* pkt = new SrsUserControlPacket();
        pkt->event_type = SrcPCUCStreamBegin;
        pkt->event_data = stream_id;
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send PCUC(StreamBegin) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send PCUC(StreamBegin) message success.");
    }
    
    // onStatus(NetStream.Play.Reset)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamReset));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Playing and resetting stream."));
        pkt->data->set(StatusDetails, SrsAmf0Any::str("stream"));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send onStatus(NetStream.Play.Reset) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send onStatus(NetStream.Play.Reset) message success.");
    }
    
    // onStatus(NetStream.Play.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started playing stream."));
        pkt->data->set(StatusDetails, SrsAmf0Any::str("stream"));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send onStatus(NetStream.Play.Start) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send onStatus(NetStream.Play.Start) message success.");
    }
    
    // |RtmpSampleAccess(false, false)
    if (true) {
        SrsSampleAccessPacket* pkt = new SrsSampleAccessPacket();

        // allow audio/video sample.
        // @see: https://github.com/ossrs/srs/issues/49
        pkt->audio_sample_access = true;
        pkt->video_sample_access = true;
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send |RtmpSampleAccess(false, false) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send |RtmpSampleAccess(false, false) message success.");
    }
    
    // onStatus(NetStream.Data.Start)
    if (true) {
        SrsOnStatusDataPacket* pkt = new SrsOnStatusDataPacket();
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeDataStart));
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send onStatus(NetStream.Data.Start) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send onStatus(NetStream.Data.Start) message success.");
    }
    
    srs_info("start play success.");
    
    return ret;
}

int SrsRtmpServer::on_play_client_pause(int stream_id, bool is_pause)
{
    int ret = ERROR_SUCCESS;
    
    if (is_pause) {
        // onStatus(NetStream.Pause.Notify)
        if (true) {
            SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
            
            pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
            pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamPause));
            pkt->data->set(StatusDescription, SrsAmf0Any::str("Paused stream."));
            
            if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
                srs_error("send onStatus(NetStream.Pause.Notify) message failed. ret=%d", ret);
                return ret;
            }
            srs_info("send onStatus(NetStream.Pause.Notify) message success.");
        }
        // StreamEOF
        if (true) {
            SrsUserControlPacket* pkt = new SrsUserControlPacket();
            
            pkt->event_type = SrcPCUCStreamEOF;
            pkt->event_data = stream_id;
            
            if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
                srs_error("send PCUC(StreamEOF) message failed. ret=%d", ret);
                return ret;
            }
            srs_info("send PCUC(StreamEOF) message success.");
        }
    } else {
        // onStatus(NetStream.Unpause.Notify)
        if (true) {
            SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
            
            pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
            pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamUnpause));
            pkt->data->set(StatusDescription, SrsAmf0Any::str("Unpaused stream."));
            
            if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
                srs_error("send onStatus(NetStream.Unpause.Notify) message failed. ret=%d", ret);
                return ret;
            }
            srs_info("send onStatus(NetStream.Unpause.Notify) message success.");
        }
        // StreanBegin
        if (true) {
            SrsUserControlPacket* pkt = new SrsUserControlPacket();
            
            pkt->event_type = SrcPCUCStreamBegin;
            pkt->event_data = stream_id;
            
            if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
                srs_error("send PCUC(StreanBegin) message failed. ret=%d", ret);
                return ret;
            }
            srs_info("send PCUC(StreanBegin) message success.");
        }
    }
    
    return ret;
}

int SrsRtmpServer::start_fmle_publish(int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    // FCPublish
    double fc_publish_tid = 0;
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsFMLEStartPacket* pkt = NULL;
        if ((ret = expect_message<SrsFMLEStartPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("recv FCPublish message failed. ret=%d", ret);
            return ret;
        }
        srs_info("recv FCPublish request message success.");
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsFMLEStartPacket, pkt);
    
        fc_publish_tid = pkt->transaction_id;
    }
    // FCPublish response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(fc_publish_tid);
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send FCPublish response message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send FCPublish response message success.");
    }
    
    // createStream
    double create_stream_tid = 0;
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCreateStreamPacket* pkt = NULL;
        if ((ret = expect_message<SrsCreateStreamPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("recv createStream message failed. ret=%d", ret);
            return ret;
        }
        srs_info("recv createStream request message success.");
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCreateStreamPacket, pkt);
        
        create_stream_tid = pkt->transaction_id;
    }
    // createStream response
    if (true) {
        SrsCreateStreamResPacket* pkt = new SrsCreateStreamResPacket(create_stream_tid, stream_id);
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send createStream response message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send createStream response message success.");
    }
    
    // publish
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsPublishPacket* pkt = NULL;
        if ((ret = expect_message<SrsPublishPacket>(&msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("recv publish message failed. ret=%d", ret);
            return ret;
        }
        srs_info("recv publish request message success.");
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsPublishPacket, pkt);
    }
    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send onFCPublish(NetStream.Publish.Start) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send onFCPublish(NetStream.Publish.Start) message success.");
    }
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send onStatus(NetStream.Publish.Start) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send onStatus(NetStream.Publish.Start) message success.");
    }
    
    srs_info("FMLE publish success.");
    
    return ret;
}

int SrsRtmpServer::fmle_unpublish(int stream_id, double unpublish_tid)
{
    int ret = ERROR_SUCCESS;
    
    // publish response onFCUnpublish(NetStream.unpublish.Success)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Stop publishing stream."));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            if (!srs_is_system_control_error(ret) && !srs_is_client_gracefully_close(ret)) {
                srs_error("send onFCUnpublish(NetStream.unpublish.Success) message failed. ret=%d", ret);
            }
            return ret;
        }
        srs_info("send onFCUnpublish(NetStream.unpublish.Success) message success.");
    }
    // FCUnpublish response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(unpublish_tid);
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            if (!srs_is_system_control_error(ret) && !srs_is_client_gracefully_close(ret)) {
                srs_error("send FCUnpublish response message failed. ret=%d", ret);
            }
            return ret;
        }
        srs_info("send FCUnpublish response message success.");
    }
    // publish response onStatus(NetStream.Unpublish.Success)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Stream is now unpublished"));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            if (!srs_is_system_control_error(ret) && !srs_is_client_gracefully_close(ret)) {
                srs_error("send onStatus(NetStream.Unpublish.Success) message failed. ret=%d", ret);
            }
            return ret;
        }
        srs_info("send onStatus(NetStream.Unpublish.Success) message success.");
    }
    
    srs_info("FMLE unpublish success.");
    
    return ret;
}

int SrsRtmpServer::start_flash_publish(int stream_id)
{
    int ret = ERROR_SUCCESS;
    
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((ret = protocol->send_and_free_packet(pkt, stream_id)) != ERROR_SUCCESS) {
            srs_error("send onStatus(NetStream.Publish.Start) message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send onStatus(NetStream.Publish.Start) message success.");
    }
    
    srs_info("flash publish success.");
    
    return ret;
}

int SrsRtmpServer::identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsRtmpConnType& type, string& stream_name, double& duration)
{
    int ret = ERROR_SUCCESS;
    
    if (true) {
        SrsCreateStreamResPacket* pkt = new SrsCreateStreamResPacket(req->transaction_id, stream_id);
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send createStream response message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send createStream response message success.");
    }
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        if ((ret = protocol->recv_message(&msg)) != ERROR_SUCCESS) {
            if (!srs_is_client_gracefully_close(ret)) {
                srs_error("recv identify client message failed. ret=%d", ret);
            }
            return ret;
        }

        SrsAutoFree(SrsCommonMessage, msg);
        SrsMessageHeader& h = msg->header;
        
        if (h.is_ackledgement() || h.is_set_chunk_size() || h.is_window_ackledgement_size() || h.is_user_control_message()) {
            continue;
        }
    
        if (!h.is_amf0_command() && !h.is_amf3_command()) {
            srs_trace("identify ignore messages except "
                "AMF0/AMF3 command message. type=%#x", h.message_type);
            continue;
        }
        
        SrsPacket* pkt = NULL;
        if ((ret = protocol->decode_message(msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("identify decode message failed. ret=%d", ret);
            return ret;
        }

        SrsAutoFree(SrsPacket, pkt);
        
        if (dynamic_cast<SrsPlayPacket*>(pkt)) {
            srs_info("level1 identify client by play.");
            return identify_play_client(dynamic_cast<SrsPlayPacket*>(pkt), type, stream_name, duration);
        }
        if (dynamic_cast<SrsPublishPacket*>(pkt)) {
            srs_info("identify client by publish, falsh publish.");
            return identify_flash_publish_client(dynamic_cast<SrsPublishPacket*>(pkt), type, stream_name);
        }
        if (dynamic_cast<SrsCreateStreamPacket*>(pkt)) {
            srs_info("identify client by create stream, play or flash publish.");
            return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket*>(pkt), stream_id, type, stream_name, duration);
        }
        
        srs_trace("ignore AMF0/AMF3 command message.");
    }
    
    return ret;
}

int SrsRtmpServer::identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsRtmpConnType& type, string& stream_name)
{
    int ret = ERROR_SUCCESS;
    
    type = SrsRtmpConnFMLEPublish;
    stream_name = req->stream_name;
    
    // releaseStream response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(req->transaction_id);
        if ((ret = protocol->send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send releaseStream response message failed. ret=%d", ret);
            return ret;
        }
        srs_info("send releaseStream response message success.");
    }
    
    return ret;
}

int SrsRtmpServer::identify_flash_publish_client(SrsPublishPacket* req, SrsRtmpConnType& type, string& stream_name)
{
    int ret = ERROR_SUCCESS;
    
    type = SrsRtmpConnFlashPublish;
    stream_name = req->stream_name;
    
    return ret;
}

int SrsRtmpServer::identify_play_client(SrsPlayPacket* req, SrsRtmpConnType& type, string& stream_name, double& duration)
{
    int ret = ERROR_SUCCESS;
    
    type = SrsRtmpConnPlay;
    stream_name = req->stream_name;
    duration = req->duration;
    
    srs_info("identity client type=play, stream_name=%s, duration=%.2f", stream_name.c_str(), duration);

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

int SrsConnectAppPacket::decode(SrsBuffer* stream)
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
        
        // see: https://github.com/ossrs/srs/issues/186
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

int SrsConnectAppPacket::encode_packet(SrsBuffer* stream)
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

int SrsConnectAppResPacket::decode(SrsBuffer* stream)
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
    
    // for RED5(1.0.6), the props is NULL, we must ignore it.
    // @see https://github.com/ossrs/srs/issues/418
    if (!stream->empty()) {
        SrsAmf0Any* p = NULL;
        if ((ret = srs_amf0_read_any(stream, &p)) != ERROR_SUCCESS) {
            srs_error("amf0 decode connect props failed. ret=%d", ret);
            return ret;
        }
        
        // ignore when props is not amf0 object.
        if (!p->is_object()) {
            srs_warn("ignore connect response props marker=%#x.", (uint8_t)p->marker);
            srs_freep(p);
        } else {
            srs_freep(props);
            props = p->to_object();
            srs_info("accept amf0 object connect response props");
        }
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

int SrsConnectAppResPacket::encode_packet(SrsBuffer* stream)
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

int SrsCallPacket::decode(SrsBuffer* stream)
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

int SrsCallPacket::encode_packet(SrsBuffer* stream)
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

int SrsCallResPacket::encode_packet(SrsBuffer* stream)
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

int SrsCreateStreamPacket::decode(SrsBuffer* stream)
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

int SrsCreateStreamPacket::encode_packet(SrsBuffer* stream)
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

int SrsCreateStreamResPacket::decode(SrsBuffer* stream)
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

int SrsCreateStreamResPacket::encode_packet(SrsBuffer* stream)
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

int SrsCloseStreamPacket::decode(SrsBuffer* stream)
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

int SrsFMLEStartPacket::decode(SrsBuffer* stream)
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

int SrsFMLEStartPacket::encode_packet(SrsBuffer* stream)
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

int SrsFMLEStartResPacket::decode(SrsBuffer* stream)
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

int SrsFMLEStartResPacket::encode_packet(SrsBuffer* stream)
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

int SrsPublishPacket::decode(SrsBuffer* stream)
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

int SrsPublishPacket::encode_packet(SrsBuffer* stream)
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

int SrsPausePacket::decode(SrsBuffer* stream)
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

int SrsPlayPacket::decode(SrsBuffer* stream)
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
    int size = SrsAmf0Size::str(command_name) + SrsAmf0Size::number()
        + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name);
    
    if (start != -2 || duration != -1 || !reset) {
        size += SrsAmf0Size::number();
    }
    
    if (duration != -1 || !reset) {
        size += SrsAmf0Size::number();
    }
    
    if (!reset) {
        size += SrsAmf0Size::boolean();
    }
    
    return size;
}

int SrsPlayPacket::encode_packet(SrsBuffer* stream)
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
    
    if ((start != -2 || duration != -1 || !reset) && (ret = srs_amf0_write_number(stream, start)) != ERROR_SUCCESS) {
        srs_error("encode start failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode start success.");
    
    if ((duration != -1 || !reset) && (ret = srs_amf0_write_number(stream, duration)) != ERROR_SUCCESS) {
        srs_error("encode duration failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("encode duration success.");
    
    if (!reset && (ret = srs_amf0_write_boolean(stream, reset)) != ERROR_SUCCESS) {
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

int SrsPlayResPacket::encode_packet(SrsBuffer* stream)
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

int SrsOnBWDonePacket::encode_packet(SrsBuffer* stream)
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

int SrsOnStatusCallPacket::encode_packet(SrsBuffer* stream)
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

int SrsBandwidthPacket::decode(SrsBuffer *stream)
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

int SrsBandwidthPacket::encode_packet(SrsBuffer* stream)
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

int SrsOnStatusDataPacket::encode_packet(SrsBuffer* stream)
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

int SrsSampleAccessPacket::encode_packet(SrsBuffer* stream)
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
    name = SRS_CONSTS_RTMP_ON_METADATA;
    metadata = SrsAmf0Any::object();
}

SrsOnMetaDataPacket::~SrsOnMetaDataPacket()
{
    srs_freep(metadata);
}

int SrsOnMetaDataPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_amf0_read_string(stream, name)) != ERROR_SUCCESS) {
        srs_error("decode metadata name failed. ret=%d", ret);
        return ret;
    }

    // ignore the @setDataFrame
    if (name == SRS_CONSTS_RTMP_SET_DATAFRAME) {
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

int SrsOnMetaDataPacket::encode_packet(SrsBuffer* stream)
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

int SrsSetWindowAckSizePacket::decode(SrsBuffer* stream)
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

int SrsSetWindowAckSizePacket::encode_packet(SrsBuffer* stream)
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

int SrsAcknowledgementPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(4)) {
        ret = ERROR_RTMP_MESSAGE_DECODE;
        srs_error("decode acknowledgement failed. ret=%d", ret);
        return ret;
    }
    
    sequence_number = (uint32_t)stream->read_4bytes();
    srs_info("decode acknowledgement success");
    
    return ret;
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

int SrsAcknowledgementPacket::encode_packet(SrsBuffer* stream)
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

int SrsSetChunkSizePacket::decode(SrsBuffer* stream)
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

int SrsSetChunkSizePacket::encode_packet(SrsBuffer* stream)
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

int SrsSetPeerBandwidthPacket::encode_packet(SrsBuffer* stream)
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

int SrsUserControlPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(2)) {
        ret = ERROR_RTMP_MESSAGE_DECODE;
        srs_error("decode user control failed. ret=%d", ret);
        return ret;
    }
    
    event_type = stream->read_2bytes();
    
    if (event_type == SrsPCUCFmsEvent0) {
        if (!stream->require(1)) {
            ret = ERROR_RTMP_MESSAGE_DECODE;
            srs_error("decode user control failed. ret=%d", ret);
            return ret;
        }
        event_data = stream->read_1bytes();
    } else {
        if (!stream->require(4)) {
            ret = ERROR_RTMP_MESSAGE_DECODE;
            srs_error("decode user control failed. ret=%d", ret);
            return ret;
        }
        event_data = stream->read_4bytes();
    }
    
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
    int size = 2;
    
    if (event_type == SrsPCUCFmsEvent0) {
        size += 1;
    } else {
        size += 4;
    }
    
    if (event_type == SrcPCUCSetBufferLength) {
        size += 4;
    }
    
    return size;
}

int SrsUserControlPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    
    if (!stream->require(get_size())) {
        ret = ERROR_RTMP_MESSAGE_ENCODE;
        srs_error("encode user control packet failed. ret=%d", ret);
        return ret;
    }
    
    stream->write_2bytes(event_type);
    
    if (event_type == SrsPCUCFmsEvent0) {
        stream->write_1bytes(event_data);
    } else {
        stream->write_4bytes(event_data);
    }

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


