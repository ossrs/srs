/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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

srs_error_t SrsPacket::encode(int& psize, char*& ppayload)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    int size = get_size();
    char* payload = NULL;
    
    SrsBuffer stream;
    
    if (size > 0) {
        payload = new char[size];
        
        if ((ret = stream.initialize(payload, size)) != ERROR_SUCCESS) {
            srs_freepa(payload);
            return srs_error_new(ret, "init stream");
        }
    }
    
    if ((err = encode_packet(&stream)) != srs_success) {
        srs_freepa(payload);
        return srs_error_wrap(err, "encode packet");
    }
    
    psize = size;
    ppayload = payload;
    
    return err;
}

srs_error_t SrsPacket::decode(SrsBuffer* stream)
{
    srs_assert(stream != NULL);
    return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "not implement");
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

srs_error_t SrsPacket::encode_packet(SrsBuffer* stream)
{
    return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "not implement");
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

srs_error_t SrsProtocol::manual_response_flush()
{
    srs_error_t err = srs_success;
    
    if (manual_response_queue.empty()) {
        return err;
    }
    
    std::vector<SrsPacket*>::iterator it;
    for (it = manual_response_queue.begin(); it != manual_response_queue.end();) {
        SrsPacket* pkt = *it;
        
        // erase this packet, the send api always free it.
        it = manual_response_queue.erase(it);
        
        // use underlayer api to send, donot flush again.
        if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }
    
    return err;
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

srs_error_t SrsProtocol::set_in_window_ack_size(int ack_size)
{
    in_ack_size.window = ack_size;
    return srs_success;
}

srs_error_t SrsProtocol::recv_message(SrsCommonMessage** pmsg)
{
    *pmsg = NULL;
    
    srs_error_t err = srs_success;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        
        if ((err = recv_interlaced_message(&msg)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "recv interlaced message");
        }
        
        if (!msg) {
            continue;
        }
        
        if (msg->size <= 0 || msg->header.payload_length <= 0) {
            srs_trace("ignore empty message(type=%d, size=%d, time=%" PRId64 ", sid=%d).",
                      msg->header.message_type, msg->header.payload_length, msg->header.timestamp, msg->header.stream_id);
            srs_freep(msg);
            continue;
        }
        
        if ((err = on_recv_message(msg)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "on message");
        }
        
        *pmsg = msg;
        break;
    }
    
    return err;
}

srs_error_t SrsProtocol::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    *ppacket = NULL;
    
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    srs_assert(msg != NULL);
    srs_assert(msg->payload != NULL);
    srs_assert(msg->size > 0);
    
    SrsBuffer stream;
    
    // initialize the decode stream for all message,
    // it's ok for the initialize if fast and without memory copy.
    if ((ret = stream.initialize(msg->payload, msg->size)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "init stream");
    }
    
    // decode the packet.
    SrsPacket* packet = NULL;
    if ((err = do_decode_message(msg->header, &stream, &packet)) != srs_success) {
        srs_freep(packet);
        return srs_error_wrap(err, "decode message");
    }
    
    // set to output ppacket only when success.
    *ppacket = packet;
    
    return err;
}

srs_error_t SrsProtocol::do_send_messages(SrsSharedPtrMessage** msgs, int nb_msgs)
{
    srs_error_t err = srs_success;
    
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
                srs_warn("resize iovs %d => %d, max_msgs=%d", nb_out_iovs, nb_out_iovs + SRS_CONSTS_IOVS_MAX, SRS_PERF_MW_MSGS);
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
                    srs_warn("c0c3 cache header too small, recoment to %d", SRS_CONSTS_C0C3_HEADERS_MAX + SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE);
                    warned_c0c3_cache_dry = true;
                }
                
                // when c0c3 cache dry,
                // sendout all messages and reset the cache, then send again.
                if ((err = do_iovs_send(out_iovs, iov_index)) != srs_success) {
                    return srs_error_wrap(err, "send iovs");
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
        return err;
    }
    
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
            
            int ret = ERROR_SUCCESS;
            if ((ret = skt->writev(iovs, 2, NULL)) != ERROR_SUCCESS) {
                return srs_error_new(ret, "send packet");
            }
        }
    }
    
    return err;
#endif
}

srs_error_t SrsProtocol::do_iovs_send(iovec* iovs, int size)
{
    return srs_write_large_iovs(skt, iovs, size);
}

srs_error_t SrsProtocol::do_send_and_free_packet(SrsPacket* packet, int stream_id)
{
    srs_error_t err = srs_success;
    
    srs_assert(packet);
    SrsAutoFree(SrsPacket, packet);
    
    int size = 0;
    char* payload = NULL;
    if ((err = packet->encode(size, payload)) != srs_success) {
        return srs_error_wrap(err, "encode packet");
    }
    
    // encode packet to payload and size.
    if (size <= 0 || payload == NULL) {
        srs_warn("rtmp: ignore empty packet");
        return err;
    }
    
    // to message
    SrsMessageHeader header;
    header.payload_length = size;
    header.message_type = packet->get_message_type();
    header.stream_id = stream_id;
    header.perfer_cid = packet->get_prefer_cid();
    
    err = do_simple_send(&header, payload, size);
    srs_freepa(payload);
    if (err == srs_success) {
        err = on_send_packet(&header, packet);
    }
    
    return err;
}

srs_error_t SrsProtocol::do_simple_send(SrsMessageHeader* mh, char* payload, int size)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // we directly send out the packet,
    // use very simple algorithm, not very fast,
    // but it's ok.
    char* p = payload;
    char* end = p + size;
    char c0c3[SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE];
    while (p < end) {
        int nbh = 0;
        if (p == payload) {
            nbh = srs_chunk_header_c0(mh->perfer_cid, (uint32_t)mh->timestamp, mh->payload_length, mh->message_type, mh->stream_id, c0c3, sizeof(c0c3));
        } else {
            nbh = srs_chunk_header_c3(mh->perfer_cid, (uint32_t)mh->timestamp, c0c3, sizeof(c0c3));
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
            return srs_error_new(ret, "writev");
        }
    }
    
    return err;
}

srs_error_t SrsProtocol::do_decode_message(SrsMessageHeader& header, SrsBuffer* stream, SrsPacket** ppacket)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    SrsPacket* packet = NULL;
    
    // decode specified packet type
    if (header.is_amf0_command() || header.is_amf3_command() || header.is_amf0_data() || header.is_amf3_data()) {
        // skip 1bytes to decode the amf3 command.
        if (header.is_amf3_command() && stream->require(1)) {
            stream->skip(1);
        }
        
        // amf0 command message.
        // need to read the command name.
        std::string command;
        if ((ret = srs_amf0_read_string(stream, command)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "AMF command name");
        }
        
        // result/error packet
        if (command == RTMP_AMF0_COMMAND_RESULT || command == RTMP_AMF0_COMMAND_ERROR) {
            double transactionId = 0.0;
            if ((ret = srs_amf0_read_number(stream, transactionId)) != ERROR_SUCCESS) {
                return srs_error_new(ret, "AMF transaction id");
            }
            
            // reset stream, for header read completed.
            stream->skip(-1 * stream->pos());
            if (header.is_amf3_command()) {
                stream->skip(1);
            }
            
            // find the call name
            if (requests.find(transactionId) == requests.end()) {
                return srs_error_new(ERROR_RTMP_NO_REQUEST, "no request");
            }
            
            std::string request_name = requests[transactionId];
            if (request_name == RTMP_AMF0_COMMAND_CONNECT) {
                *ppacket = packet = new SrsConnectAppResPacket();
                return packet->decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_CREATE_STREAM) {
                *ppacket = packet = new SrsCreateStreamResPacket(0, 0);
                return packet->decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
                *ppacket = packet = new SrsFMLEStartResPacket(0);
                return packet->decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_FC_PUBLISH) {
                *ppacket = packet = new SrsFMLEStartResPacket(0);
                return packet->decode(stream);
            } else if (request_name == RTMP_AMF0_COMMAND_UNPUBLISH) {
                *ppacket = packet = new SrsFMLEStartResPacket(0);
                return packet->decode(stream);
            } else {
                return srs_error_new(ERROR_RTMP_NO_REQUEST, "invalid AMF request=%s, transactionId=%.2f", request_name.c_str(), transactionId);
            }
        }
        
        // reset to zero(amf3 to 1) to restart decode.
        stream->skip(-1 * stream->pos());
        if (header.is_amf3_command()) {
            stream->skip(1);
        }
        
        // decode command object.
        if (command == RTMP_AMF0_COMMAND_CONNECT) {
            *ppacket = packet = new SrsConnectAppPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_CREATE_STREAM) {
            *ppacket = packet = new SrsCreateStreamPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_PLAY) {
            *ppacket = packet = new SrsPlayPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_PAUSE) {
            *ppacket = packet = new SrsPausePacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_FC_PUBLISH) {
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_PUBLISH) {
            *ppacket = packet = new SrsPublishPacket();
            return packet->decode(stream);
        } else if(command == RTMP_AMF0_COMMAND_UNPUBLISH) {
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if(command == SRS_CONSTS_RTMP_SET_DATAFRAME || command == SRS_CONSTS_RTMP_ON_METADATA) {
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
                  || command == SRS_BW_CHECK_FINAL) {
            *ppacket = packet = new SrsBandwidthPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_CLOSE_STREAM) {
            *ppacket = packet = new SrsCloseStreamPacket();
            return packet->decode(stream);
        } else if (header.is_amf0_command() || header.is_amf3_command()) {
            *ppacket = packet = new SrsCallPacket();
            return packet->decode(stream);
        }
        
        // default packet to drop message.
        *ppacket = packet = new SrsPacket();
        return err;
    } else if(header.is_user_control_message()) {
        *ppacket = packet = new SrsUserControlPacket();
        return packet->decode(stream);
    } else if(header.is_window_ackledgement_size()) {
        *ppacket = packet = new SrsSetWindowAckSizePacket();
        return packet->decode(stream);
    } else if(header.is_set_chunk_size()) {
        *ppacket = packet = new SrsSetChunkSizePacket();
        return packet->decode(stream);
    } else {
        if (!header.is_set_peer_bandwidth() && !header.is_ackledgement()) {
            srs_trace("drop unknown message, type=%d", header.message_type);
        }
    }
    
    return err;
}

srs_error_t SrsProtocol::send_and_free_message(SrsSharedPtrMessage* msg, int stream_id)
{
    return send_and_free_messages(&msg, 1, stream_id);
}

srs_error_t SrsProtocol::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id)
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
    srs_error_t err = do_send_messages(msgs, nb_msgs);
    
    for (int i = 0; i < nb_msgs; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        srs_freep(msg);
    }
    
    // donot flush when send failed
    if (err != srs_success) {
        return srs_error_wrap(err, "send packet");
    }
    
    // flush messages in manual queue
    if ((err = manual_response_flush()) != srs_success) {
        return srs_error_wrap(err, "flush response");
    }
    
    print_debug_info();
    
    return err;
}

srs_error_t SrsProtocol::send_and_free_packet(SrsPacket* packet, int stream_id)
{
    srs_error_t err = srs_success;
    
    if ((err = do_send_and_free_packet(packet, stream_id)) != srs_success) {
        return srs_error_wrap(err, "send packet");
    }
    
    // flush messages in manual queue
    if ((err = manual_response_flush()) != srs_success) {
        return srs_error_wrap(err, "flush response");
    }
    
    return err;
}

srs_error_t SrsProtocol::recv_interlaced_message(SrsCommonMessage** pmsg)
{
    srs_error_t err = srs_success;
    
    // chunk stream basic header.
    char fmt = 0;
    int cid = 0;
    if ((err = read_basic_header(fmt, cid)) != srs_success) {
        return srs_error_wrap(err, "read basic header");
    }
    
    // the cid must not negative.
    srs_assert(cid >= 0);
    
    // get the cached chunk stream.
    SrsChunkStream* chunk = NULL;
    
    // use chunk stream cache to get the chunk info.
    // @see https://github.com/ossrs/srs/issues/249
    if (cid < SRS_PERF_CHUNK_STREAM_CACHE) {
        // already init, use it direclty
        chunk = cs_cache[cid];
    } else {
        // chunk stream cache miss, use map.
        if (chunk_streams.find(cid) == chunk_streams.end()) {
            chunk = chunk_streams[cid] = new SrsChunkStream(cid);
            // set the perfer cid of chunk,
            // which will copy to the message received.
            chunk->header.perfer_cid = cid;
        } else {
            chunk = chunk_streams[cid];
        }
    }
    
    // chunk stream message header
    if ((err = read_message_header(chunk, fmt)) != srs_success) {
        return srs_error_wrap(err, "read message header");
    }
    
    // read msg payload from chunk stream.
    SrsCommonMessage* msg = NULL;
    if ((err = read_message_payload(chunk, &msg)) != srs_success) {
        return srs_error_wrap(err, "read message payload");
    }
    
    // not got an entire RTMP message, try next chunk.
    if (!msg) {
        return err;
    }
    
    *pmsg = msg;
    
    return err;
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
srs_error_t SrsProtocol::read_basic_header(char& fmt, int& cid)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = in_buffer->grow(skt, 1)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "buffer grow");
    }
    
    fmt = in_buffer->read_1byte();
    cid = fmt & 0x3f;
    fmt = (fmt >> 6) & 0x03;
    
    // 2-63, 1B chunk header
    if (cid > 1) {
        return err;
    }
    
    // 64-319, 2B chunk header
    if (cid == 0) {
        if ((ret = in_buffer->grow(skt, 1)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "buffer grow");
        }
        
        cid = 64;
        cid += (uint8_t)in_buffer->read_1byte();
        // 64-65599, 3B chunk header
    } else if (cid == 1) {
        if ((ret = in_buffer->grow(skt, 2)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "buffer grow");
        }
        
        cid = 64;
        cid += (uint8_t)in_buffer->read_1byte();
        cid += ((uint8_t)in_buffer->read_1byte()) * 256;
    } else {
        srs_error("invalid path, impossible basic header.");
        srs_assert(false);
    }
    
    return err;
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
srs_error_t SrsProtocol::read_message_header(SrsChunkStream* chunk, char fmt)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
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
            return srs_error_new(ERROR_RTMP_CHUNK_START, "fresh chunk=%d fmt expect=%d actual=%d", chunk->cid, RTMP_FMT_TYPE0, fmt);
        }
    }
    
    // when exists cache msg, means got an partial message,
    // the fmt must not be type0 which means new message.
    if (chunk->msg && fmt == RTMP_FMT_TYPE0) {
        return srs_error_new(ERROR_RTMP_CHUNK_START, "exists chunk=%d invalid fmt=%d", chunk->cid, RTMP_FMT_TYPE0);
    }
    
    // create msg when new chunk stream start
    if (!chunk->msg) {
        chunk->msg = new SrsCommonMessage();
    }
    
    // read message header from socket to buffer.
    static char mh_sizes[] = {11, 7, 3, 0};
    int mh_size = mh_sizes[(int)fmt];
    
    if (mh_size > 0 && (ret = in_buffer->grow(skt, mh_size)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "buffer grow");
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
                return srs_error_new(ERROR_RTMP_PACKET_SIZE, "exists msg change size=%d to %d", chunk->header.payload_length, payload_length);
            }
            
            chunk->header.payload_length = payload_length;
            chunk->header.message_type = *p++;
            
            if (fmt == RTMP_FMT_TYPE0) {
                pp = (char*)&chunk->header.stream_id;
                pp[0] = *p++;
                pp[1] = *p++;
                pp[2] = *p++;
                pp[3] = *p++;
            }
        }
    } else {
        // update the timestamp even fmt=3 for first chunk packet
        if (is_first_chunk_of_msg && !chunk->extended_timestamp) {
            chunk->header.timestamp += chunk->header.timestamp_delta;
        }
    }
    
    // read extended-timestamp
    if (chunk->extended_timestamp) {
        mh_size += 4;
        if ((ret = in_buffer->grow(skt, 4)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "buffer grow");
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
        } else {
            chunk->header.timestamp = timestamp;
        }
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
    
    return err;
}

srs_error_t SrsProtocol::read_message_payload(SrsChunkStream* chunk, SrsCommonMessage** pmsg)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    // empty message
    if (chunk->header.payload_length <= 0) {
        srs_trace("empty message(type=%d, size=%d, time=%d, sid=%d)",
            chunk->header.message_type, chunk->header.payload_length, (int)chunk->header.timestamp, chunk->header.stream_id);
        
        *pmsg = chunk->msg;
        chunk->msg = NULL;
        
        return err;
    }
    srs_assert(chunk->header.payload_length > 0);
    
    // the chunk payload size.
    int payload_size = chunk->header.payload_length - chunk->msg->size;
    payload_size = srs_min(payload_size, in_chunk_size);
    
    // create msg payload if not initialized
    if (!chunk->msg->payload) {
        chunk->msg->create_payload(chunk->header.payload_length);
    }
    
    // read payload to buffer
    if ((ret = in_buffer->grow(skt, payload_size)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "buffer grow");
    }
    memcpy(chunk->msg->payload + chunk->msg->size, in_buffer->read_slice(payload_size), payload_size);
    chunk->msg->size += payload_size;
    
    // got entire RTMP message?
    if (chunk->header.payload_length == chunk->msg->size) {
        *pmsg = chunk->msg;
        chunk->msg = NULL;
        return err;
    }
    
    return err;
}

srs_error_t SrsProtocol::on_recv_message(SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    srs_assert(msg != NULL);
    
    // try to response acknowledgement
    if ((err = response_acknowledgement_message()) != srs_success) {
        return srs_error_wrap(err, "response ack");
    }
    
    SrsPacket* packet = NULL;
    switch (msg->header.message_type) {
        case RTMP_MSG_SetChunkSize:
        case RTMP_MSG_UserControlMessage:
        case RTMP_MSG_WindowAcknowledgementSize:
            if ((err = decode_message(msg, &packet)) != srs_success) {
                return srs_error_wrap(err, "decode message");
            }
            break;
        case RTMP_MSG_VideoMessage:
        case RTMP_MSG_AudioMessage:
            print_debug_info();
        default:
            return err;
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
                srs_warn("ignore ack window size %d", pkt->ackowledgement_window_size);
            }
            break;
        }
        case RTMP_MSG_SetChunkSize: {
            SrsSetChunkSizePacket* pkt = dynamic_cast<SrsSetChunkSizePacket*>(packet);
            srs_assert(pkt != NULL);
            
            // for some server, the actual chunk size can greater than the max value(65536),
            // so we just warning the invalid chunk size, and actually use it is ok,
            // @see: https://github.com/ossrs/srs/issues/160
            if (pkt->chunk_size < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE || pkt->chunk_size > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE) {
                srs_warn("accept chunk=%d, should in [%d, %d], please see #160",
                    pkt->chunk_size, SRS_CONSTS_RTMP_MIN_CHUNK_SIZE,  SRS_CONSTS_RTMP_MAX_CHUNK_SIZE);
            }
            
            // @see: https://github.com/ossrs/srs/issues/541
            if (pkt->chunk_size < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE) {
                return srs_error_new(ERROR_RTMP_CHUNK_SIZE, "invalid chunk size %d", pkt->chunk_size);
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
                srs_info("buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d",
                    pkt->extra_data, in_ack_size.window, out_ack_size.window, in_chunk_size, out_chunk_size);
            }
            if (pkt->event_type == SrcPCUCPingRequest) {
                if ((err = response_ping_message(pkt->event_data)) != srs_success) {
                    return srs_error_wrap(err, "response ping");
                }
            }
            break;
        }
        default:
            break;
    }
    
    return err;
}

srs_error_t SrsProtocol::on_send_packet(SrsMessageHeader* mh, SrsPacket* packet)
{
    srs_error_t err = srs_success;
    
    // ignore raw bytes oriented RTMP message.
    if (packet == NULL) {
        return err;
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
    
    return err;
}

srs_error_t SrsProtocol::response_acknowledgement_message()
{
    srs_error_t err = srs_success;
    
    if (in_ack_size.window <= 0) {
        return err;
    }
    
    // ignore when delta bytes not exceed half of window(ack size).
    uint32_t delta = (uint32_t)(skt->get_recv_bytes() - in_ack_size.nb_recv_bytes);
    if (delta < in_ack_size.window / 2) {
        return err;
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
        return err;
    }
    
    // use underlayer api to send, donot flush again.
    if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send packet");
    }
    
    return err;
}

srs_error_t SrsProtocol::response_ping_message(int32_t timestamp)
{
    srs_error_t err = srs_success;
    
    srs_trace("get a ping request, response it. timestamp=%d", timestamp);
    
    SrsUserControlPacket* pkt = new SrsUserControlPacket();
    
    pkt->event_type = SrcPCUCPingResponse;
    pkt->event_data = timestamp;
    
    // cache the message and use flush to send.
    if (!auto_response_when_recv) {
        manual_response_queue.push_back(pkt);
        return err;
    }
    
    // use underlayer api to send, donot flush again.
    if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send packet");
    }
    
    return err;
}

void SrsProtocol::print_debug_info()
{
    if (show_debug_info) {
        show_debug_info = false;
        srs_trace("protocol in.buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d",
            in_buffer_length, in_ack_size.window, out_ack_size.window, in_chunk_size, out_chunk_size);
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
        case SrsRtmpConnHaivisionPublish: return "haivision-publish";
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

srs_error_t SrsHandshakeBytes::read_c0c1(ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (c0c1) {
        return err;
    }
    
    ssize_t nsize;
    
    c0c1 = new char[1537];
    if ((ret = io->read_fully(c0c1, 1537, &nsize)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "read c0c1");
    }
    
    return err;
}

srs_error_t SrsHandshakeBytes::read_s0s1s2(ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (s0s1s2) {
        return err;
    }
    
    ssize_t nsize;
    
    s0s1s2 = new char[3073];
    if ((ret = io->read_fully(s0s1s2, 3073, &nsize)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "read s0s1s2");
    }
    
    return err;
}

srs_error_t SrsHandshakeBytes::read_c2(ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (c2) {
        return err;
    }
    
    ssize_t nsize;
    
    c2 = new char[1536];
    if ((ret = io->read_fully(c2, 1536, &nsize)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "read c2");
    }
    
    return err;
}

srs_error_t SrsHandshakeBytes::create_c0c1()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (c0c1) {
        return err;
    }
    
    c0c1 = new char[1537];
    srs_random_generate(c0c1, 1537);
    
    // plain text required.
    SrsBuffer stream;
    if ((ret = stream.initialize(c0c1, 9)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "init stream");
    }
    stream.write_1bytes(0x03);
    stream.write_4bytes((int32_t)::time(NULL));
    stream.write_4bytes(0x00);
    
    return err;
}

srs_error_t SrsHandshakeBytes::create_s0s1s2(const char* c1)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (s0s1s2) {
        return err;
    }
    
    s0s1s2 = new char[3073];
    srs_random_generate(s0s1s2, 3073);
    
    // plain text required.
    SrsBuffer stream;
    if ((ret = stream.initialize(s0s1s2, 9)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "init stream");
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
    
    return err;
}

srs_error_t SrsHandshakeBytes::create_c2()
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if (c2) {
        return err;
    }
    
    c2 = new char[1536];
    srs_random_generate(c2, 1536);
    
    // time
    SrsBuffer stream;
    if ((ret = stream.initialize(c2, 8)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "init stream");
    }
    stream.write_4bytes((int32_t)::time(NULL));
    // c2 time2 copy from s1
    if (s0s1s2) {
        stream.write_bytes(s0s1s2 + 1, 4);
    }
    
    return err;
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

srs_error_t SrsRtmpClient::recv_message(SrsCommonMessage** pmsg)
{
    return protocol->recv_message(pmsg);
}

srs_error_t SrsRtmpClient::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    return protocol->decode_message(msg, ppacket);
}

srs_error_t SrsRtmpClient::send_and_free_message(SrsSharedPtrMessage* msg, int stream_id)
{
    return protocol->send_and_free_message(msg, stream_id);
}

srs_error_t SrsRtmpClient::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id)
{
    return protocol->send_and_free_messages(msgs, nb_msgs, stream_id);
}

srs_error_t SrsRtmpClient::send_and_free_packet(SrsPacket* packet, int stream_id)
{
    return protocol->send_and_free_packet(packet, stream_id);
}

srs_error_t SrsRtmpClient::handshake()
{
    srs_error_t err = srs_success;
    
    srs_assert(hs_bytes);
    
    // maybe st has problem when alloc object on stack, always alloc object at heap.
    // @see https://github.com/ossrs/srs/issues/509
    SrsComplexHandshake* complex_hs = new SrsComplexHandshake();
    SrsAutoFree(SrsComplexHandshake, complex_hs);
    
    if ((err = complex_hs->handshake_with_server(hs_bytes, io)) != srs_success) {
        if (srs_error_code(err) == ERROR_RTMP_TRY_SIMPLE_HS) {
            srs_freep(err);
            
            // always alloc object at heap.
            // @see https://github.com/ossrs/srs/issues/509
            SrsSimpleHandshake* simple_hs = new SrsSimpleHandshake();
            SrsAutoFree(SrsSimpleHandshake, simple_hs);
            
            if ((err = simple_hs->handshake_with_server(hs_bytes, io)) != srs_success) {
                return srs_error_wrap(err, "simple handshake");
            }
        }
        return srs_error_wrap(err, "complex handshake");
    }
    
    srs_freep(hs_bytes);
    
    return err;
}

srs_error_t SrsRtmpClient::simple_handshake()
{
    srs_error_t err = srs_success;
    
    srs_assert(hs_bytes);
    
    SrsSimpleHandshake simple_hs;
    if ((err = simple_hs.handshake_with_server(hs_bytes, io)) != srs_success) {
        return srs_error_wrap(err, "simple handshake");
    }
    
    srs_freep(hs_bytes);
    
    return err;
}

srs_error_t SrsRtmpClient::complex_handshake()
{
    srs_error_t err = srs_success;
    
    srs_assert(hs_bytes);
    
    SrsComplexHandshake complex_hs;
    if ((err = complex_hs.handshake_with_server(hs_bytes, io)) != srs_success) {
        return srs_error_wrap(err, "complex handshake");
    }
    
    srs_freep(hs_bytes);
    
    return err;
}

srs_error_t SrsRtmpClient::connect_app(string app, string tcUrl, SrsRequest* r, bool dsu, SrsServerInfo* si)
{
    srs_error_t err = srs_success;
    
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
        
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "connect tcUrl");
        }
    }
    
    // Set Window Acknowledgement size(2500000)
    if (true) {
        SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
        pkt->ackowledgement_window_size = 2500000;
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "set ack window size");
        }
    }
    
    // expect connect _result
    SrsCommonMessage* msg = NULL;
    SrsConnectAppResPacket* pkt = NULL;
    if ((err = expect_message<SrsConnectAppResPacket>(&msg, &pkt)) != srs_success) {
        return srs_error_wrap(err, "expect connect response");
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
    
    return err;
}

srs_error_t SrsRtmpClient::create_stream(int& stream_id)
{
    srs_error_t err = srs_success;
    
    // CreateStream
    if (true) {
        SrsCreateStreamPacket* pkt = new SrsCreateStreamPacket();
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "create stream");
        }
    }
    
    // CreateStream _result.
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCreateStreamResPacket* pkt = NULL;
        if ((err = expect_message<SrsCreateStreamResPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "expect create stream response");
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCreateStreamResPacket, pkt);
        
        stream_id = (int)pkt->stream_id;
    }
    
    return err;
}

srs_error_t SrsRtmpClient::play(string stream, int stream_id)
{
    srs_error_t err = srs_success;
    
    // Play(stream)
    if (true) {
        SrsPlayPacket* pkt = new SrsPlayPacket();
        pkt->stream_name = stream;
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "play stream=%s, id=%d", stream.c_str(), stream_id);
        }
    }
    
    // SetBufferLength(1000ms)
    int buffer_length_ms = 1000;
    if (true) {
        SrsUserControlPacket* pkt = new SrsUserControlPacket();
        
        pkt->event_type = SrcPCUCSetBufferLength;
        pkt->event_data = stream_id;
        pkt->extra_data = buffer_length_ms;
        
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "set buffer=%d, stream=%s", buffer_length_ms, stream.c_str());
        }
    }
    
    // SetChunkSize
    if (true) {
        SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size = SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "set chunk_size=%d, stream=%s", SRS_CONSTS_RTMP_SRS_CHUNK_SIZE, stream.c_str());
        }
    }
    
    return err;
}

srs_error_t SrsRtmpClient::publish(string stream, int stream_id)
{
    srs_error_t err = srs_success;
    
    // SetChunkSize
    if (true) {
        SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size = SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "set chunk_size=%d, stream=%s", SRS_CONSTS_RTMP_SRS_CHUNK_SIZE, stream.c_str());
        }
    }
    
    // publish(stream)
    if (true) {
        SrsPublishPacket* pkt = new SrsPublishPacket();
        pkt->stream_name = stream;
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "publish stream=%s", stream.c_str());
        }
    }
    
    return err;
}

srs_error_t SrsRtmpClient::fmle_publish(string stream, int& stream_id)
{
    stream_id = 0;
    
    srs_error_t err = srs_success;
    
    // SrsFMLEStartPacket
    if (true) {
        SrsFMLEStartPacket* pkt = SrsFMLEStartPacket::create_release_stream(stream);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "FMLE publish stream=%s", stream.c_str());
        }
    }
    
    // FCPublish
    if (true) {
        SrsFMLEStartPacket* pkt = SrsFMLEStartPacket::create_FC_publish(stream);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "FMLE FC publish stream=%s", stream.c_str());
        }
    }
    
    // CreateStream
    if (true) {
        SrsCreateStreamPacket* pkt = new SrsCreateStreamPacket();
        pkt->transaction_id = 4;
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "create stream=%s", stream.c_str());
        }
    }
    
    // expect result of CreateStream
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCreateStreamResPacket* pkt = NULL;
        if ((err = expect_message<SrsCreateStreamResPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "expect create stream response");
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCreateStreamResPacket, pkt);
        
        stream_id = (int)pkt->stream_id;
    }
    
    // publish(stream)
    if (true) {
        SrsPublishPacket* pkt = new SrsPublishPacket();
        pkt->stream_name = stream;
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "FMLE publish stream=%s", stream.c_str());
        }
    }
    
    return err;
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

srs_error_t SrsRtmpServer::recv_message(SrsCommonMessage** pmsg)
{
    return protocol->recv_message(pmsg);
}

srs_error_t SrsRtmpServer::decode_message(SrsCommonMessage* msg, SrsPacket** ppacket)
{
    return protocol->decode_message(msg, ppacket);
}

srs_error_t SrsRtmpServer::send_and_free_message(SrsSharedPtrMessage* msg, int stream_id)
{
    return protocol->send_and_free_message(msg, stream_id);
}

srs_error_t SrsRtmpServer::send_and_free_messages(SrsSharedPtrMessage** msgs, int nb_msgs, int stream_id)
{
    return protocol->send_and_free_messages(msgs, nb_msgs, stream_id);
}

srs_error_t SrsRtmpServer::send_and_free_packet(SrsPacket* packet, int stream_id)
{
    return protocol->send_and_free_packet(packet, stream_id);
}

srs_error_t SrsRtmpServer::handshake()
{
    srs_error_t err = srs_success;
    
    srs_assert(hs_bytes);
    
    SrsComplexHandshake complex_hs;
    if ((err = complex_hs.handshake_with_client(hs_bytes, io)) != srs_success) {
        if (srs_error_code(err) == ERROR_RTMP_TRY_SIMPLE_HS) {
            srs_freep(err);
            
            SrsSimpleHandshake simple_hs;
            if ((err = simple_hs.handshake_with_client(hs_bytes, io)) != srs_success) {
                return srs_error_wrap(err, "simple handshake");
            }
        }
        
        return srs_error_wrap(err, "complex handshake");
    }
    
    srs_freep(hs_bytes);
    
    return err;
}

srs_error_t SrsRtmpServer::connect_app(SrsRequest* req)
{
    srs_error_t err = srs_success;
    
    SrsCommonMessage* msg = NULL;
    SrsConnectAppPacket* pkt = NULL;
    if ((err = expect_message<SrsConnectAppPacket>(&msg, &pkt)) != srs_success) {
        return srs_error_wrap(err, "expect connect app response");
    }
    SrsAutoFree(SrsCommonMessage, msg);
    SrsAutoFree(SrsConnectAppPacket, pkt);
    
    SrsAmf0Any* prop = NULL;
    
    if ((prop = pkt->command_object->ensure_property_string("tcUrl")) == NULL) {
        return srs_error_new(ERROR_RTMP_REQ_CONNECT, "no tcUrl");
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
    }
    
    srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->port, req->param);
    req->strip();
    
    return err;
}

srs_error_t SrsRtmpServer::set_window_ack_size(int ack_size)
{
    srs_error_t err = srs_success;
    
    SrsSetWindowAckSizePacket* pkt = new SrsSetWindowAckSizePacket();
    pkt->ackowledgement_window_size = ack_size;
    if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "set ack size");
    }
    
    return err;
}

srs_error_t SrsRtmpServer::set_in_window_ack_size(int ack_size)
{
    return protocol->set_in_window_ack_size(ack_size);
}

srs_error_t SrsRtmpServer::set_peer_bandwidth(int bandwidth, int type)
{
    srs_error_t err = srs_success;
    
    SrsSetPeerBandwidthPacket* pkt = new SrsSetPeerBandwidthPacket();
    pkt->bandwidth = bandwidth;
    pkt->type = type;
    if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "set peer bandwidth");
    }
    
    return err;
}

srs_error_t SrsRtmpServer::response_connect_app(SrsRequest *req, const char* server_ip)
{
    srs_error_t err = srs_success;
    
    SrsConnectAppResPacket* pkt = new SrsConnectAppResPacket();
    
    pkt->props->set("fmsVer", SrsAmf0Any::str("FMS/" RTMP_SIG_FMS_VER));
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
    
    if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "response connect app");
    }
    
    return err;
}

#define SRS_RTMP_REDIRECT_TMMS 3000
srs_error_t SrsRtmpServer::redirect(SrsRequest* r, string host, int port, bool& accepted)
{
    srs_error_t err = srs_success;
    
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
        
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "redirect");
        }
    }
    
    // client must response a call message.
    // or we never know whether the client is ok to redirect.
    protocol->set_recv_timeout(SRS_RTMP_REDIRECT_TMMS);
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCallPacket* pkt = NULL;
        if ((err = expect_message<SrsCallPacket>(&msg, &pkt)) != srs_success) {
            srs_freep(err);
            // ignore any error of redirect response.
            return srs_success;
        }
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCallPacket, pkt);
        
        string message;
        if (pkt->arguments && pkt->arguments->is_string()) {
            message = pkt->arguments->to_str();
            srs_info("confirm redirected to %s", message.c_str());
            accepted = true;
        }
    }
    
    return err;
}

void SrsRtmpServer::response_connect_reject(SrsRequest* /*req*/, const char* desc)
{
    srs_error_t err = srs_success;
    
    SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
    pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelError));
    pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectRejected));
    pkt->data->set(StatusDescription, SrsAmf0Any::str(desc));
    
    if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
        srs_error("reject error %s", srs_error_desc(err).c_str());
        return;
    }
    
    return;
}

srs_error_t SrsRtmpServer::on_bw_done()
{
    srs_error_t err = srs_success;
    
    SrsOnBWDonePacket* pkt = new SrsOnBWDonePacket();
    if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "onBWDone");
    }
    
    return err;
}

srs_error_t SrsRtmpServer::identify_client(int stream_id, SrsRtmpConnType& type, string& stream_name, double& duration)
{
    type = SrsRtmpConnUnknown;
    srs_error_t err = srs_success;
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        if ((err = protocol->recv_message(&msg)) != srs_success) {
            return srs_error_wrap(err, "identify");
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsMessageHeader& h = msg->header;
        
        if (h.is_ackledgement() || h.is_set_chunk_size() || h.is_window_ackledgement_size() || h.is_user_control_message()) {
            continue;
        }
        
        if (!h.is_amf0_command() && !h.is_amf3_command()) {
            srs_trace("ignore unless AMF message, type=%#x", h.message_type);
            continue;
        }
        
        SrsPacket* pkt = NULL;
        if ((err = protocol->decode_message(msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        
        SrsAutoFree(SrsPacket, pkt);
        
        if (dynamic_cast<SrsCreateStreamPacket*>(pkt)) {
            return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket*>(pkt), stream_id, type, stream_name, duration);
        }
        if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
            return identify_fmle_publish_client(dynamic_cast<SrsFMLEStartPacket*>(pkt), type, stream_name);
        }
        if (dynamic_cast<SrsPlayPacket*>(pkt)) {
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
            if ((err = protocol->send_and_free_packet(res, 0)) != srs_success) {
                return srs_error_wrap(err, "call");
            }
            
            // For encoder of Haivision, it always send a _checkbw call message.
            // @remark the next message is createStream, so we continue to identify it.
            // @see https://github.com/ossrs/srs/issues/844
            if (call->command_name == "_checkbw") {
                continue;
            }
            continue;
        }
        
        srs_trace("ignore AMF0/AMF3 command message.");
    }
    
    return err;
}

srs_error_t SrsRtmpServer::set_chunk_size(int chunk_size)
{
    srs_error_t err = srs_success;
    
    SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
    pkt->chunk_size = chunk_size;
    if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "set chunk size");
    }
    
    return err;
}

srs_error_t SrsRtmpServer::start_play(int stream_id)
{
    srs_error_t err = srs_success;
    
    // StreamBegin
    if (true) {
        SrsUserControlPacket* pkt = new SrsUserControlPacket();
        pkt->event_type = SrcPCUCStreamBegin;
        pkt->event_data = stream_id;
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "stream begin");
        }
    }
    
    // onStatus(NetStream.Play.Reset)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamReset));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Playing and resetting stream."));
        pkt->data->set(StatusDetails, SrsAmf0Any::str("stream"));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus play reset");
        }
    }
    
    // onStatus(NetStream.Play.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started playing stream."));
        pkt->data->set(StatusDetails, SrsAmf0Any::str("stream"));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus play start");
        }
    }
    
    // |RtmpSampleAccess(false, false)
    if (true) {
        SrsSampleAccessPacket* pkt = new SrsSampleAccessPacket();
        
        // allow audio/video sample.
        // @see: https://github.com/ossrs/srs/issues/49
        pkt->audio_sample_access = true;
        pkt->video_sample_access = true;
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "rtmp sample access");
        }
    }
    
    // onStatus(NetStream.Data.Start)
    if (true) {
        SrsOnStatusDataPacket* pkt = new SrsOnStatusDataPacket();
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeDataStart));
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus data start");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::on_play_client_pause(int stream_id, bool is_pause)
{
    srs_error_t err = srs_success;
    
    if (is_pause) {
        // onStatus(NetStream.Pause.Notify)
        if (true) {
            SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
            
            pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
            pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamPause));
            pkt->data->set(StatusDescription, SrsAmf0Any::str("Paused stream."));
            
            if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
                return srs_error_wrap(err, "onStatus pause notify");
            }
        }
        // StreamEOF
        if (true) {
            SrsUserControlPacket* pkt = new SrsUserControlPacket();
            
            pkt->event_type = SrcPCUCStreamEOF;
            pkt->event_data = stream_id;
            
            if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
                return srs_error_wrap(err, "stream EOF");
            }
        }
    } else {
        // onStatus(NetStream.Unpause.Notify)
        if (true) {
            SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
            
            pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
            pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamUnpause));
            pkt->data->set(StatusDescription, SrsAmf0Any::str("Unpaused stream."));
            
            if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
                return srs_error_wrap(err, "onStatus unpause notify");
            }
        }
        // StreamBegin
        if (true) {
            SrsUserControlPacket* pkt = new SrsUserControlPacket();
            
            pkt->event_type = SrcPCUCStreamBegin;
            pkt->event_data = stream_id;
            
            if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
                return srs_error_wrap(err, "stream begin");
            }
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::start_fmle_publish(int stream_id)
{
    srs_error_t err = srs_success;
    
    // FCPublish
    double fc_publish_tid = 0;
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsFMLEStartPacket* pkt = NULL;
        if ((err = expect_message<SrsFMLEStartPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "FCPublish");
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsFMLEStartPacket, pkt);
        
        fc_publish_tid = pkt->transaction_id;
    }
    // FCPublish response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(fc_publish_tid);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "response FCPublish");
        }
    }
    
    // createStream
    double create_stream_tid = 0;
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsCreateStreamPacket* pkt = NULL;
        if ((err = expect_message<SrsCreateStreamPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "create stream");
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsCreateStreamPacket, pkt);
        
        create_stream_tid = pkt->transaction_id;
    }
    // createStream response
    if (true) {
        SrsCreateStreamResPacket* pkt = new SrsCreateStreamResPacket(create_stream_tid, stream_id);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "response create stream");
        }
    }
    
    // publish
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsPublishPacket* pkt = NULL;
        if ((err = expect_message<SrsPublishPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "publish");
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsPublishPacket, pkt);
    }
    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onFCPublish");
        }
    }
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus publish start");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::start_haivision_publish(int stream_id)
{
    srs_error_t err = srs_success;
    
    // publish
    if (true) {
        SrsCommonMessage* msg = NULL;
        SrsPublishPacket* pkt = NULL;
        if ((err = expect_message<SrsPublishPacket>(&msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "publish");
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsAutoFree(SrsPublishPacket, pkt);
    }
    
    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onFCPublish");
        }
    }
    
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus publish start");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::fmle_unpublish(int stream_id, double unpublish_tid)
{
    srs_error_t err = srs_success;
    
    // publish response onFCUnpublish(NetStream.unpublish.Success)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->command_name = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Stop publishing stream."));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onFCUnpublish");
        }
    }
    // FCUnpublish response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(unpublish_tid);
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "FCUnpublish");
        }
    }
    // publish response onStatus(NetStream.Unpublish.Success)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Stream is now unpublished"));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus unpublish");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::start_flash_publish(int stream_id)
{
    srs_error_t err = srs_success;
    
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket* pkt = new SrsOnStatusCallPacket();
        
        pkt->data->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        pkt->data->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));
        
        if ((err = protocol->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "onStatus publish start");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::identify_create_stream_client(SrsCreateStreamPacket* req, int stream_id, SrsRtmpConnType& type, string& stream_name, double& duration)
{
    srs_error_t err = srs_success;
    
    if (true) {
        SrsCreateStreamResPacket* pkt = new SrsCreateStreamResPacket(req->transaction_id, stream_id);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "response create stream");
        }
    }
    
    while (true) {
        SrsCommonMessage* msg = NULL;
        if ((err = protocol->recv_message(&msg)) != srs_success) {
            return srs_error_wrap(err, "receive message");
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        SrsMessageHeader& h = msg->header;
        
        if (h.is_ackledgement() || h.is_set_chunk_size() || h.is_window_ackledgement_size() || h.is_user_control_message()) {
            continue;
        }
        
        if (!h.is_amf0_command() && !h.is_amf3_command()) {
            srs_trace("ignore identify unless AMF message type=%#x", h.message_type);
            continue;
        }
        
        SrsPacket* pkt = NULL;
        if ((err = protocol->decode_message(msg, &pkt)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        
        SrsAutoFree(SrsPacket, pkt);
        
        if (dynamic_cast<SrsPlayPacket*>(pkt)) {
            return identify_play_client(dynamic_cast<SrsPlayPacket*>(pkt), type, stream_name, duration);
        }
        if (dynamic_cast<SrsPublishPacket*>(pkt)) {
            return identify_flash_publish_client(dynamic_cast<SrsPublishPacket*>(pkt), type, stream_name);
        }
        if (dynamic_cast<SrsCreateStreamPacket*>(pkt)) {
            return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket*>(pkt), stream_id, type, stream_name, duration);
        }
        if (dynamic_cast<SrsFMLEStartPacket*>(pkt)) {
            return identify_haivision_publish_client(dynamic_cast<SrsFMLEStartPacket*>(pkt), type, stream_name);
        }
        
        srs_trace("ignore AMF0/AMF3 command message.");
    }
    
    return err;
}

srs_error_t SrsRtmpServer::identify_fmle_publish_client(SrsFMLEStartPacket* req, SrsRtmpConnType& type, string& stream_name)
{
    srs_error_t err = srs_success;
    
    type = SrsRtmpConnFMLEPublish;
    stream_name = req->stream_name;
    
    // releaseStream response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(req->transaction_id);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "response releaseStream");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::identify_haivision_publish_client(SrsFMLEStartPacket* req, SrsRtmpConnType& type, string& stream_name)
{
    srs_error_t err = srs_success;
    
    type = SrsRtmpConnHaivisionPublish;
    stream_name = req->stream_name;
    
    // FCPublish response
    if (true) {
        SrsFMLEStartResPacket* pkt = new SrsFMLEStartResPacket(req->transaction_id);
        if ((err = protocol->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "response FCPublish");
        }
    }
    
    return err;
}

srs_error_t SrsRtmpServer::identify_flash_publish_client(SrsPublishPacket* req, SrsRtmpConnType& type, string& stream_name)
{
    type = SrsRtmpConnFlashPublish;
    stream_name = req->stream_name;
    
    return srs_success;
}

srs_error_t SrsRtmpServer::identify_play_client(SrsPlayPacket* req, SrsRtmpConnType& type, string& stream_name, double& duration)
{
    type = SrsRtmpConnPlay;
    stream_name = req->stream_name;
    duration = req->duration;
    
    srs_info("identity client type=play, stream_name=%s, duration=%.2f", stream_name.c_str(), duration);
    
    return srs_success;
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

srs_error_t SrsConnectAppPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_CONNECT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "commmand name %s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "tid");
    }
    
    // some client donot send id=1.0, so we only warn user if not match.
    if (transaction_id != 1.0) {
        srs_warn("invalid tid=%.1f", transaction_id);
        srs_error_reset(err);
    }
    
    if ((ret = command_object->read(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "command object");
    }
    
    if (!stream->empty()) {
        srs_freep(args);
        
        // see: https://github.com/ossrs/srs/issues/186
        // the args maybe any amf0, for instance, a string. we should drop if not object.
        SrsAmf0Any* any = NULL;
        if ((ret = SrsAmf0Any::discovery(stream, &any)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "discovery connect args");
        }
        srs_assert(any);
        
        // read the instance
        if ((ret = any->read(stream)) != ERROR_SUCCESS) {
            srs_freep(any);
            return srs_error_new(ret, "decode connect args");
        }
        
        // drop when not an AMF0 object.
        if (!any->is_object()) {
            srs_warn("drop the args, see: '4.1.1. connect', marker=%#x", any->marker);
            srs_freep(any);
        } else {
            args = any->to_object();
        }
    }
    
    return err;
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

srs_error_t SrsConnectAppPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = command_object->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if (args && (ret = args->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode args");
    }
    
    return err;
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

srs_error_t SrsConnectAppResPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    // some client donot send id=1.0, so we only warn user if not match.
    if (transaction_id != 1.0) {
        srs_warn("ignore invalid tid=%.1f", transaction_id);
        srs_error_reset(err);
    }
    
    // for RED5(1.0.6), the props is NULL, we must ignore it.
    // @see https://github.com/ossrs/srs/issues/418
    if (!stream->empty()) {
        SrsAmf0Any* p = NULL;
        if ((ret = srs_amf0_read_any(stream, &p)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "read any");
        }
        
        // ignore when props is not amf0 object.
        if (!p->is_object()) {
            srs_warn("ignore connect response props marker=%#x.", (uint8_t)p->marker);
            srs_freep(p);
        } else {
            srs_freep(props);
            props = p->to_object();
        }
    }
    
    if ((ret = info->read(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode info");
    }
    
    return err;
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

srs_error_t SrsConnectAppResPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = props->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode props");
    }
    
    if ((ret = info->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode info");
    }
    
    return err;
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

srs_error_t SrsCallPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty()) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    srs_freep(command_object);
    if ((ret = SrsAmf0Any::discovery(stream, &command_object)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "discovery command object");
    }
    if ((ret = command_object->read(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if (!stream->empty()) {
        srs_freep(arguments);
        if ((ret = SrsAmf0Any::discovery(stream, &arguments)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "disconvery call");
        }
        if ((ret = arguments->read(stream)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "decode call");
        }
    }
    
    return err;
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

srs_error_t SrsCallPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if (command_object && (ret = command_object->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if (arguments && (ret = arguments->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode args");
    }
    
    return err;
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

srs_error_t SrsCallResPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if (command_object && (ret = command_object->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if (response && (ret = response->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode response");
    }
    
    return err;
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

srs_error_t SrsCreateStreamPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_CREATE_STREAM) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    return err;
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

srs_error_t SrsCreateStreamPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    return err;
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

srs_error_t SrsCreateStreamResPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if ((ret = srs_amf0_read_number(stream, stream_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode stream id");
    }
    
    return err;
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

srs_error_t SrsCreateStreamResPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if ((ret = srs_amf0_write_number(stream, stream_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode stream id");
    }
    
    return err;
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

srs_error_t SrsCloseStreamPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    return err;
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

srs_error_t SrsFMLEStartPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name != RTMP_AMF0_COMMAND_RELEASE_STREAM && command_name != RTMP_AMF0_COMMAND_FC_PUBLISH && command_name != RTMP_AMF0_COMMAND_UNPUBLISH) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if ((ret = srs_amf0_read_string(stream, stream_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode stream name");
    }
    
    return err;
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

srs_error_t SrsFMLEStartPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if ((ret = srs_amf0_write_string(stream, stream_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode stream name");
    }
    
    return err;
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

srs_error_t SrsFMLEStartResPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if ((ret = srs_amf0_read_undefined(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode stream id");
    }
    
    return err;
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

srs_error_t SrsFMLEStartResPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if ((ret = srs_amf0_write_undefined(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode args");
    }
    
    return err;
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

srs_error_t SrsPublishPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PUBLISH) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if ((ret = srs_amf0_read_string(stream, stream_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode stream name");
    }
    
    if (!stream->empty() && (ret = srs_amf0_read_string(stream, type)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode publish type");
    }
    
    return err;
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

srs_error_t SrsPublishPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if ((ret = srs_amf0_write_string(stream, stream_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode stream name");
    }
    
    if ((ret = srs_amf0_write_string(stream, type)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode type");
    }
    
    return err;
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

srs_error_t SrsPausePacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PAUSE) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if ((ret = srs_amf0_read_boolean(stream, is_pause)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode is_pause");
    }
    
    if ((ret = srs_amf0_read_number(stream, time_ms)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode pause time");
    }
    
    return err;
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

srs_error_t SrsPlayPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PLAY) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command name=%s", command_name.c_str());
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    if ((ret = srs_amf0_read_string(stream, stream_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode stream name");
    }
    
    if (!stream->empty() && (ret = srs_amf0_read_number(stream, start)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode play start");
    }
    if (!stream->empty() && (ret = srs_amf0_read_number(stream, duration)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode play duration");
    }
    
    if (stream->empty()) {
        return err;
    }
    
    SrsAmf0Any* reset_value = NULL;
    if ((ret = srs_amf0_read_any(stream, &reset_value)) != ERROR_SUCCESS) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "read play reset");
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
            return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid reset type=%#x", reset_value->marker);
        }
    }
    
    return err;
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

srs_error_t SrsPlayPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if ((ret = srs_amf0_write_string(stream, stream_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode stream name");
    }
    
    if ((start != -2 || duration != -1 || !reset) && (ret = srs_amf0_write_number(stream, start)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode start");
    }
    
    if ((duration != -1 || !reset) && (ret = srs_amf0_write_number(stream, duration)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode duration");
    }
    
    if (!reset && (ret = srs_amf0_write_boolean(stream, reset)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode reset");
    }
    
    return err;
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

srs_error_t SrsPlayResPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command object");
    }
    
    if ((ret = desc->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode desc");
    }
    
    return err;
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

srs_error_t SrsOnBWDonePacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode args");
    }
    
    return err;
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

srs_error_t SrsOnStatusCallPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode args");
    }
    
    if ((ret = data->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode data");
    }
    
    return err;
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

srs_error_t SrsBandwidthPacket::decode(SrsBuffer *stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command name");
    }
    
    if ((ret = srs_amf0_read_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode tid");
    }
    
    if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode command object");
    }
    
    // @remark, for bandwidth test, ignore the data field.
    // only decode the stop-play, start-publish and finish packet.
    if (is_stop_play() || is_start_publish() || is_finish()) {
        if ((ret = data->read(stream)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "decode command object");
        }
    }
    
    return err;
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

srs_error_t SrsBandwidthPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_number(stream, transaction_id)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode tid");
    }
    
    if ((ret = srs_amf0_write_null(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode args");
    }
    
    if ((ret = data->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode data");
    }
    
    return err;
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

srs_error_t SrsOnStatusDataPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = data->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode data");
    }
    
    return err;
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

srs_error_t SrsSampleAccessPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, command_name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode command name");
    }
    
    if ((ret = srs_amf0_write_boolean(stream, video_sample_access)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode video sample access");
    }
    
    if ((ret = srs_amf0_write_boolean(stream, audio_sample_access)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode audio sample access");
    }
    
    return err;
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

srs_error_t SrsOnMetaDataPacket::decode(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_read_string(stream, name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode name");
    }
    
    // ignore the @setDataFrame
    if (name == SRS_CONSTS_RTMP_SET_DATAFRAME) {
        if ((ret = srs_amf0_read_string(stream, name)) != ERROR_SUCCESS) {
            return srs_error_new(ret, "decode name");
        }
    }
    
    // the metadata maybe object or ecma array
    SrsAmf0Any* any = NULL;
    if ((ret = srs_amf0_read_any(stream, &any)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "decode metadata");
    }
    
    srs_assert(any);
    if (any->is_object()) {
        srs_freep(metadata);
        metadata = any->to_object();
        return err;
    }
    
    SrsAutoFree(SrsAmf0Any, any);
    
    if (any->is_ecma_array()) {
        SrsAmf0EcmaArray* arr = any->to_ecma_array();
        
        // if ecma array, copy to object.
        for (int i = 0; i < arr->count(); i++) {
            metadata->set(arr->key_at(i), arr->value_at(i)->copy());
        }
    }
    
    return err;
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

srs_error_t SrsOnMetaDataPacket::encode_packet(SrsBuffer* stream)
{
    int ret = ERROR_SUCCESS;
    srs_error_t err = srs_success;
    
    if ((ret = srs_amf0_write_string(stream, name)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode name");
    }
    
    if ((ret = metadata->write(stream)) != ERROR_SUCCESS) {
        return srs_error_new(ret, "encode metadata");
    }
    
    return err;
}

SrsSetWindowAckSizePacket::SrsSetWindowAckSizePacket()
{
    ackowledgement_window_size = 0;
}

SrsSetWindowAckSizePacket::~SrsSetWindowAckSizePacket()
{
}

srs_error_t SrsSetWindowAckSizePacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "ack window size");
    }
    
    ackowledgement_window_size = stream->read_4bytes();
    
    return err;
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

srs_error_t SrsSetWindowAckSizePacket::encode_packet(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "encode ack size");
    }
    
    stream->write_4bytes(ackowledgement_window_size);
    
    return err;
}

SrsAcknowledgementPacket::SrsAcknowledgementPacket()
{
    sequence_number = 0;
}

SrsAcknowledgementPacket::~SrsAcknowledgementPacket()
{
}

srs_error_t SrsAcknowledgementPacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "decode ack");
    }
    
    sequence_number = (uint32_t)stream->read_4bytes();
    
    return err;
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

srs_error_t SrsAcknowledgementPacket::encode_packet(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "encode ack");
    }
    
    stream->write_4bytes(sequence_number);
    
    return err;
}

SrsSetChunkSizePacket::SrsSetChunkSizePacket()
{
    chunk_size = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
}

SrsSetChunkSizePacket::~SrsSetChunkSizePacket()
{
}

srs_error_t SrsSetChunkSizePacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "decode set chunk size");
    }
    
    chunk_size = stream->read_4bytes();
    
    return err;
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

srs_error_t SrsSetChunkSizePacket::encode_packet(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "encode set chunk size");
    }
    
    stream->write_4bytes(chunk_size);
    
    return err;
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

srs_error_t SrsSetPeerBandwidthPacket::encode_packet(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(5)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "encode set bandwidth");
    }
    
    stream->write_4bytes(bandwidth);
    stream->write_1bytes(type);
    
    return err;
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

srs_error_t SrsUserControlPacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(2)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "decode event type");
    }
    
    event_type = stream->read_2bytes();
    
    if (event_type == SrsPCUCFmsEvent0) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "decode FMS event");
        }
        event_data = stream->read_1bytes();
    } else {
        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "decode event data");
        }
        event_data = stream->read_4bytes();
    }
    
    if (event_type == SrcPCUCSetBufferLength) {
        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "decode set buffer");
        }
        extra_data = stream->read_4bytes();
    }
    
    return err;
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

srs_error_t SrsUserControlPacket::encode_packet(SrsBuffer* stream)
{
    srs_error_t err = srs_success;
    
    if (!stream->require(get_size())) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "encode event type");
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
    }
    
    return err;
}


