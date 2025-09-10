//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtmp_stack.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_rtmp_handshake.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_protocol_utility.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#include <unistd.h>

#include <stdlib.h>
using namespace std;

// FMLE
#define RTMP_AMF0_COMMAND_ON_FC_PUBLISH "onFCPublish"
#define RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH "onFCUnpublish"

// default stream id for response the createStream request.
#define SRS_DEFAULT_SID 1

// when got a messae header, there must be some data,
// increase recv timeout to got an entire message.
#define SRS_MIN_RECV_TIMEOUT_US (int64_t)(60 * 1000 * 1000LL)

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
#define RTMP_FMT_TYPE0 0
// 6.1.2.2. Type 1
// Chunks of Type 1 are 7 bytes long. The message stream ID is not
// included; this chunk takes the same stream ID as the preceding chunk.
// Streams with variable-sized messages (for example, many video
// formats) SHOULD use this format for the first chunk of each new
// message after the first.
#define RTMP_FMT_TYPE1 1
// 6.1.2.3. Type 2
// Chunks of Type 2 are 3 bytes long. Neither the stream ID nor the
// message length is included; this chunk has the same stream ID and
// message length as the preceding chunk. Streams with constant-sized
// messages (for example, some audio and data formats) SHOULD use this
// format for the first chunk of each message after the first.
#define RTMP_FMT_TYPE2 2
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
#define RTMP_FMT_TYPE3 3

/****************************************************************************
 *****************************************************************************
 ****************************************************************************/

SrsRtmpCommand::SrsRtmpCommand()
{
}

SrsRtmpCommand::~SrsRtmpCommand()
{
}

srs_error_t SrsRtmpCommand::to_msg(SrsRtmpCommonMessage *msg, int stream_id)
{
    srs_error_t err = srs_success;

    int size = 0;
    char *payload = NULL;
    if ((err = encode(size, payload)) != srs_success) {
        return srs_error_wrap(err, "encode packet");
    }

    // encode packet to payload and size.
    if (size <= 0 || payload == NULL) {
        srs_warn("packet is empty, ignore empty message.");
        return err;
    }

    // to message
    SrsMessageHeader header;
    header.payload_length_ = size;
    header.message_type_ = get_message_type();
    header.stream_id_ = stream_id;

    if ((err = msg->create(&header, payload, size)) != srs_success) {
        return srs_error_wrap(err, "create %dB message", size);
    }

    return err;
}

srs_error_t SrsRtmpCommand::encode(int &psize, char *&ppayload)
{
    srs_error_t err = srs_success;

    int size = get_size();
    char *payload = NULL;

    if (size > 0) {
        payload = new char[size];
        SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(payload, size));

        if ((err = encode_packet(stream.get())) != srs_success) {
            srs_freepa(payload);
            return srs_error_wrap(err, "encode packet");
        }
    }

    psize = size;
    ppayload = payload;

    return err;
}

srs_error_t SrsRtmpCommand::decode(SrsBuffer *stream)
{
    return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "decode");
}

int SrsRtmpCommand::get_message_type()
{
    return 0;
}

int SrsRtmpCommand::get_size()
{
    return 0;
}

srs_error_t SrsRtmpCommand::encode_packet(SrsBuffer *stream)
{
    return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "encode");
}

SrsProtocol::AckWindowSize::AckWindowSize()
{
    window_ = 0;
    sequence_number_ = 0;
    nb_recv_bytes_ = 0;
}

SrsProtocol::SrsProtocol(ISrsProtocolReadWriter *io)
{
    in_buffer_ = new SrsFastStream();
    skt_ = io;

    in_chunk_size_ = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
    out_chunk_size_ = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;

    nb_out_iovs_ = 8 * SRS_CONSTS_IOVS_MAX;
    out_iovs_ = (iovec *)malloc(sizeof(iovec) * nb_out_iovs_);
    // each chunk consumers atleast 2 iovs
    srs_assert(nb_out_iovs_ >= 2);

    warned_c0c3_cache_dry_ = false;
    auto_response_when_recv_ = true;
    show_debug_info_ = true;
    in_buffer_length_ = 0;

    cs_cache_ = NULL;
    if (SRS_PERF_CHUNK_STREAM_CACHE > 0) {
        cs_cache_ = new SrsChunkStream *[SRS_PERF_CHUNK_STREAM_CACHE];
    }
    for (int cid = 0; cid < SRS_PERF_CHUNK_STREAM_CACHE; cid++) {
        SrsChunkStream *cs = new SrsChunkStream(cid);
        cs_cache_[cid] = cs;
    }

    out_c0c3_caches_ = new char[SRS_CONSTS_C0C3_HEADERS_MAX];
}

SrsProtocol::~SrsProtocol()
{
    if (true) {
        std::map<int, SrsChunkStream *>::iterator it;

        for (it = chunk_streams_.begin(); it != chunk_streams_.end(); ++it) {
            SrsChunkStream *stream = it->second;
            srs_freep(stream);
        }

        chunk_streams_.clear();
    }

    if (true) {
        std::vector<SrsRtmpCommand *>::iterator it;
        for (it = manual_response_queue_.begin(); it != manual_response_queue_.end(); ++it) {
            SrsRtmpCommand *pkt = *it;
            srs_freep(pkt);
        }
        manual_response_queue_.clear();
    }

    srs_freep(in_buffer_);

    // alloc by malloc, use free directly.
    if (out_iovs_) {
        free(out_iovs_);
        out_iovs_ = NULL;
    }

    // free all chunk stream cache.
    for (int i = 0; i < SRS_PERF_CHUNK_STREAM_CACHE; i++) {
        SrsChunkStream *cs = cs_cache_[i];
        srs_freep(cs);
    }
    srs_freepa(cs_cache_);

    srs_freepa(out_c0c3_caches_);
}

void SrsProtocol::set_auto_response(bool v)
{
    auto_response_when_recv_ = v;
}

srs_error_t SrsProtocol::manual_response_flush()
{
    srs_error_t err = srs_success;

    if (manual_response_queue_.empty()) {
        return err;
    }

    std::vector<SrsRtmpCommand *>::iterator it;
    for (it = manual_response_queue_.begin(); it != manual_response_queue_.end();) {
        SrsRtmpCommand *pkt = *it;

        // erase this packet, the send api always free it.
        it = manual_response_queue_.erase(it);

        // use underlayer api to send, donot flush again.
        if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }

    return err;
}

#ifdef SRS_PERF_MERGED_READ
void SrsProtocol::set_merge_read(bool v, IMergeReadHandler *handler)
{
    in_buffer_->set_merge_read(v, handler);
}

void SrsProtocol::set_recv_buffer(int buffer_size)
{
    in_buffer_->set_buffer(buffer_size);
}
#endif

void SrsProtocol::set_recv_timeout(srs_utime_t tm)
{
    return skt_->set_recv_timeout(tm);
}

srs_utime_t SrsProtocol::get_recv_timeout()
{
    return skt_->get_recv_timeout();
}

void SrsProtocol::set_send_timeout(srs_utime_t tm)
{
    return skt_->set_send_timeout(tm);
}

srs_utime_t SrsProtocol::get_send_timeout()
{
    return skt_->get_send_timeout();
}

int64_t SrsProtocol::get_recv_bytes()
{
    return skt_->get_recv_bytes();
}

int64_t SrsProtocol::get_send_bytes()
{
    return skt_->get_send_bytes();
}

srs_error_t SrsProtocol::set_in_window_ack_size(int ack_size)
{
    in_ack_size_.window_ = ack_size;
    return srs_success;
}

srs_error_t SrsProtocol::recv_message(SrsRtmpCommonMessage **pmsg)
{
    *pmsg = NULL;

    srs_error_t err = srs_success;

    while (true) {
        SrsRtmpCommonMessage *msg = NULL;

        if ((err = recv_interlaced_message(&msg)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "recv interlaced message");
        }

        if (!msg) {
            continue;
        }

        if (msg->size() <= 0 || msg->header_.payload_length_ <= 0) {
            srs_trace("ignore empty message(type=%d, size=%d, time=%" PRId64 ", sid=%d).",
                      msg->header_.message_type_, msg->header_.payload_length_,
                      msg->header_.timestamp_, msg->header_.stream_id_);
            srs_freep(msg);
            continue;
        }

        if ((err = on_recv_message(msg)) != srs_success) {
            srs_freep(msg);
            return srs_error_wrap(err, "on recv message");
        }

        *pmsg = msg;
        break;
    }

    return err;
}

srs_error_t SrsProtocol::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    *ppacket = NULL;

    srs_error_t err = srs_success;

    srs_assert(msg != NULL);
    srs_assert(msg->payload() != NULL);
    srs_assert(msg->size() > 0);

    SrsBuffer stream(msg->payload(), msg->size());

    // decode the packet.
    SrsRtmpCommand *packet = NULL;
    if ((err = do_decode_message(msg->header_, &stream, &packet)) != srs_success) {
        srs_freep(packet);
        return srs_error_wrap(err, "decode message");
    }

    // set to output ppacket only when success.
    *ppacket = packet;

    return err;
}

srs_error_t SrsProtocol::do_send_messages(SrsMediaPacket **msgs, int nb_msgs)
{
    srs_error_t err = srs_success;

#ifdef SRS_PERF_COMPLEX_SEND
    int iov_index = 0;
    iovec *iovs = out_iovs_ + iov_index;

    int c0c3_cache_index = 0;
    char *c0c3_cache = out_c0c3_caches_ + c0c3_cache_index;

    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SrsMediaPacket *msg = msgs[i];

        if (!msg) {
            continue;
        }

        // ignore empty message.
        if (!msg->payload() || msg->size() <= 0) {
            continue;
        }

        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char *p = msg->payload();
        char *pend = msg->payload() + msg->size();

        // always write the header event payload is empty.
        while (p < pend) {
            // always has header
            int nb_cache = SRS_CONSTS_C0C3_HEADERS_MAX - c0c3_cache_index;
            int nbh = srs_rtmp_write_chunk_header(msg, c0c3_cache, nb_cache, p == msg->payload());
            srs_assert(nbh > 0);

            // header iov
            iovs[0].iov_base = c0c3_cache;
            iovs[0].iov_len = nbh;

            // payload iov
            int payload_size = srs_min(out_chunk_size_, (int)(pend - p));
            iovs[1].iov_base = p;
            iovs[1].iov_len = payload_size;

            // consume sendout bytes.
            p += payload_size;

            // realloc the iovs if exceed,
            // for we donot know how many messges maybe to send entirely,
            // we just alloc the iovs, it's ok.
            if (iov_index >= nb_out_iovs_ - 2) {
                int ov = nb_out_iovs_;
                nb_out_iovs_ = 2 * nb_out_iovs_;
                int realloc_size = sizeof(iovec) * nb_out_iovs_;
                out_iovs_ = (iovec *)realloc(out_iovs_, realloc_size);
                srs_warn("resize iovs %d => %d, max_msgs=%d", ov, nb_out_iovs_, SRS_PERF_MW_MSGS);
            }

            // to next pair of iovs
            iov_index += 2;
            iovs = out_iovs_ + iov_index;

            // to next c0c3 header cache
            c0c3_cache_index += nbh;
            c0c3_cache = out_c0c3_caches_ + c0c3_cache_index;

            // the cache header should never be realloc again,
            // for the ptr is set to iovs, so we just warn user to set larger
            // and use another loop to send again.
            int c0c3_left = SRS_CONSTS_C0C3_HEADERS_MAX - c0c3_cache_index;
            if (c0c3_left < SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
                // only warn once for a connection.
                if (!warned_c0c3_cache_dry_) {
                    srs_warn("c0c3 cache header too small, recoment to %d", SRS_CONSTS_C0C3_HEADERS_MAX + SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE);
                    warned_c0c3_cache_dry_ = true;
                }

                // when c0c3 cache dry,
                // sendout all messages and reset the cache, then send again.
                if ((err = do_iovs_send(out_iovs_, iov_index)) != srs_success) {
                    return srs_error_wrap(err, "send iovs");
                }

                // reset caches, while these cache ensure
                // atleast we can sendout a chunk.
                iov_index = 0;
                iovs = out_iovs_ + iov_index;

                c0c3_cache_index = 0;
                c0c3_cache = out_c0c3_caches_ + c0c3_cache_index;
            }
        }
    }

    // maybe the iovs already sendout when c0c3 cache dry,
    // so just ignore when no iovs to send.
    if (iov_index <= 0) {
        return err;
    }

    // Send out iovs at a time.
    if ((err = do_iovs_send(out_iovs_, iov_index)) != srs_success) {
        return srs_error_wrap(err, "send iovs");
    }

    return err;
#else
    // try to send use the c0c3 header cache,
    // if cache is consumed, try another loop.
    for (int i = 0; i < nb_msgs; i++) {
        SrsMediaPacket *msg = msgs[i];

        if (!msg) {
            continue;
        }

        // ignore empty message.
        if (!msg->payload || msg->size <= 0) {
            continue;
        }

        // p set to current write position,
        // it's ok when payload is NULL and size is 0.
        char *p = msg->payload;
        char *pend = msg->payload + msg->size;

        // always write the header event payload is empty.
        while (p < pend) {
            // for simple send, send each chunk one by one
            iovec *iovs = out_iovs_;
            char *c0c3_cache = out_c0c3_caches_;
            int nb_cache = SRS_CONSTS_C0C3_HEADERS_MAX;

            // always has header
            int nbh = srs_rtmp_write_chunk_header(msg, c0c3_cache, nb_cache, p == msg->payload);
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

            if ((err = skt->writev(iovs, 2, NULL)) != srs_success) {
                return srs_error_wrap(err, "writev");
            }
        }
    }

    return err;
#endif
}

srs_error_t SrsProtocol::do_iovs_send(iovec *iovs, int size)
{
    return srs_write_large_iovs(skt_, iovs, size);
}

srs_error_t SrsProtocol::do_send_and_free_packet(SrsRtmpCommand *packet_raw, int stream_id)
{
    srs_error_t err = srs_success;

    srs_assert(packet_raw);
    SrsUniquePtr<SrsRtmpCommand> packet(packet_raw);
    SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());

    if ((err = packet->to_msg(msg.get(), stream_id)) != srs_success) {
        return srs_error_wrap(err, "to message");
    }

    SrsMediaPacket *shared_msg = new SrsMediaPacket();
    msg->to_msg(shared_msg);

    if ((err = send_and_free_message(shared_msg, stream_id)) != srs_success) {
        return srs_error_wrap(err, "send packet");
    }

    if ((err = on_send_packet(&msg->header_, packet.get())) != srs_success) {
        return srs_error_wrap(err, "on send packet");
    }

    return err;
}

srs_error_t SrsProtocol::do_decode_message(SrsMessageHeader &header, SrsBuffer *stream, SrsRtmpCommand **ppacket)
{
    srs_error_t err = srs_success;

    SrsRtmpCommand *packet = NULL;

    // decode specified packet type
    if (header.is_amf0_command() || header.is_amf3_command() || header.is_amf0_data() || header.is_amf3_data()) {
        // Ignore FFmpeg timecode, see https://github.com/ossrs/srs/issues/3803
        if (stream->left() == 4 && (uint8_t)*stream->head() == 0x00) {
            srs_warn("Ignore FFmpeg timecode, data=[%s]", srs_strings_dumps_hex(stream->head(), 4).c_str());
            return err;
        }

        // skip 1bytes to decode the amf3 command.
        if (header.is_amf3_command() && stream->require(1)) {
            stream->skip(1);
        }

        // amf0 command message.
        // need to read the command name.
        std::string command;
        if ((err = srs_amf0_read_string(stream, command)) != srs_success) {
            return srs_error_wrap(err, "decode command name");
        }

        // result/error packet
        if (command == RTMP_AMF0_COMMAND_RESULT || command == RTMP_AMF0_COMMAND_ERROR) {
            double transactionId = 0.0;
            if ((err = srs_amf0_read_number(stream, transactionId)) != srs_success) {
                return srs_error_wrap(err, "decode tid for %s", command.c_str());
            }

            // reset stream, for header read completed.
            stream->skip(-1 * stream->pos());
            if (header.is_amf3_command()) {
                stream->skip(1);
            }

            // find the call name
            if (requests_.find(transactionId) == requests_.end()) {
                return srs_error_new(ERROR_RTMP_NO_REQUEST, "find request for command=%s, tid=%.2f", command.c_str(), transactionId);
            }

            std::string request_name = requests_[transactionId];
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
                return srs_error_new(ERROR_RTMP_NO_REQUEST, "request=%s, tid=%.2f", request_name.c_str(), transactionId);
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
        } else if (command == RTMP_AMF0_COMMAND_CREATE_STREAM) {
            *ppacket = packet = new SrsCreateStreamPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_PLAY) {
            *ppacket = packet = new SrsPlayPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_PAUSE) {
            *ppacket = packet = new SrsPausePacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_FC_PUBLISH) {
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_PUBLISH) {
            *ppacket = packet = new SrsPublishPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_UNPUBLISH) {
            *ppacket = packet = new SrsFMLEStartPacket();
            return packet->decode(stream);
        } else if (command == SRS_CONSTS_RTMP_SET_DATAFRAME) {
            *ppacket = packet = new SrsOnMetaDataPacket();
            return packet->decode(stream);
        } else if (command == SRS_CONSTS_RTMP_ON_METADATA) {
            *ppacket = packet = new SrsOnMetaDataPacket();
            return packet->decode(stream);
        } else if (command == RTMP_AMF0_COMMAND_CLOSE_STREAM) {
            *ppacket = packet = new SrsCloseStreamPacket();
            return packet->decode(stream);
        } else if (header.is_amf0_command() || header.is_amf3_command()) {
            *ppacket = packet = new SrsCallPacket();
            return packet->decode(stream);
        }

        // default packet to drop message.
        *ppacket = packet = new SrsRtmpCommand();
        return err;
    } else if (header.is_user_control_message()) {
        *ppacket = packet = new SrsUserControlPacket();
        return packet->decode(stream);
    } else if (header.is_window_ackledgement_size()) {
        *ppacket = packet = new SrsSetWindowAckSizePacket();
        return packet->decode(stream);
    } else if (header.is_ackledgement()) {
        *ppacket = packet = new SrsAcknowledgementPacket();
        return packet->decode(stream);
    } else if (header.is_set_chunk_size()) {
        *ppacket = packet = new SrsSetChunkSizePacket();
        return packet->decode(stream);
    } else {
        if (!header.is_set_peer_bandwidth() && !header.is_ackledgement()) {
            srs_trace("drop unknown message, type=%d", header.message_type_);
        }
    }

    return err;
}

srs_error_t SrsProtocol::send_and_free_message(SrsMediaPacket *msg, int stream_id)
{
    return send_and_free_messages(&msg, 1, stream_id);
}

srs_error_t SrsProtocol::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id)
{
    // always not NULL msg.
    srs_assert(msgs);
    srs_assert(nb_msgs > 0);

    // update the stream id in header.
    for (int i = 0; i < nb_msgs; i++) {
        SrsMediaPacket *msg = msgs[i];

        if (!msg) {
            continue;
        }

        // check prefer cid and stream,
        // when one msg stream id is ok, ignore left.
        if (msg->check(stream_id)) {
            break;
        }
    }

    // donot use the auto free to free the msg,
    // for performance issue.
    srs_error_t err = do_send_messages(msgs, nb_msgs);

    for (int i = 0; i < nb_msgs; i++) {
        SrsMediaPacket *msg = msgs[i];
        srs_freep(msg);
    }

    // donot flush when send failed
    if (err != srs_success) {
        return srs_error_wrap(err, "send messages");
    }

    // flush messages in manual queue
    if ((err = manual_response_flush()) != srs_success) {
        return srs_error_wrap(err, "manual flush response");
    }

    print_debug_info();

    return err;
}

srs_error_t SrsProtocol::send_and_free_packet(SrsRtmpCommand *packet, int stream_id)
{
    srs_error_t err = srs_success;

    if ((err = do_send_and_free_packet(packet, stream_id)) != srs_success) {
        return srs_error_wrap(err, "send packet");
    }

    // flush messages in manual queue
    if ((err = manual_response_flush()) != srs_success) {
        return srs_error_wrap(err, "manual flush response");
    }

    return err;
}

srs_error_t SrsProtocol::recv_interlaced_message(SrsRtmpCommonMessage **pmsg)
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
    SrsChunkStream *chunk = NULL;

    // use chunk stream cache to get the chunk info.
    if (cid < SRS_PERF_CHUNK_STREAM_CACHE) {
        // already init, use it direclty
        chunk = cs_cache_[cid];
    } else {
        // chunk stream cache miss, use map.
        if (chunk_streams_.find(cid) == chunk_streams_.end()) {
            chunk = chunk_streams_[cid] = new SrsChunkStream(cid);
        } else {
            chunk = chunk_streams_[cid];
        }
    }

    // chunk stream message header
    if ((err = read_message_header(chunk, fmt)) != srs_success) {
        return srs_error_wrap(err, "read message header");
    }

    // read msg payload from chunk stream.
    SrsRtmpCommonMessage *msg = NULL;
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
srs_error_t SrsProtocol::read_basic_header(char &fmt, int &cid)
{
    srs_error_t err = srs_success;

    if ((err = in_buffer_->grow(skt_, 1)) != srs_success) {
        return srs_error_wrap(err, "basic header requires 1 bytes");
    }

    fmt = in_buffer_->read_1byte();
    cid = fmt & 0x3f;
    fmt = (fmt >> 6) & 0x03;

    // 2-63, 1B chunk header
    if (cid > 1) {
        return err;
        // 64-319, 2B chunk header
    } else if (cid == 0) {
        if ((err = in_buffer_->grow(skt_, 1)) != srs_success) {
            return srs_error_wrap(err, "basic header requires 2 bytes");
        }

        cid = 64;
        cid += (uint8_t)in_buffer_->read_1byte();
        // 64-65599, 3B chunk header
    } else {
        srs_assert(cid == 1);

        if ((err = in_buffer_->grow(skt_, 2)) != srs_success) {
            return srs_error_wrap(err, "basic header requires 3 bytes");
        }

        cid = 64;
        cid += (uint8_t)in_buffer_->read_1byte();
        cid += ((uint8_t)in_buffer_->read_1byte()) * 256;
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
srs_error_t SrsProtocol::read_message_header(SrsChunkStream *chunk, char fmt)
{
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
    bool is_first_chunk_of_msg = !chunk->msg_;

    // but, we can ensure that when a chunk stream is fresh,
    // the fmt must be 0, a new stream.
    if (chunk->msg_count_ == 0 && fmt != RTMP_FMT_TYPE0) {
        // for librtmp, if ping, it will send a fresh stream with fmt=1,
        // 0x42             where: fmt=1, cid=2, protocol contorl user-control message
        // 0x00 0x00 0x00   where: timestamp=0
        // 0x00 0x00 0x06   where: payload_length=6
        // 0x04             where: message_type=4(protocol control user-control message)
        // 0x00 0x06            where: event Ping(0x06)
        // 0x00 0x00 0x0d 0x0f  where: event data 4bytes ping timestamp.
        if (fmt == RTMP_FMT_TYPE1) {
            srs_warn("fresh chunk starts with fmt=1");
        } else {
            // must be a RTMP protocol level error.
            return srs_error_new(ERROR_RTMP_CHUNK_START, "fresh chunk expect fmt=0, actual=%d, cid=%d", fmt, chunk->cid_);
        }
    }

    // when exists cache msg, means got an partial message,
    // the fmt must not be type0 which means new message.
    if (chunk->msg_ && fmt == RTMP_FMT_TYPE0) {
        return srs_error_new(ERROR_RTMP_CHUNK_START, "for existed chunk, fmt should not be 0");
    }

    // create msg when new chunk stream start
    if (!chunk->msg_) {
        chunk->msg_ = new SrsRtmpCommonMessage();
        chunk->writing_pos_ = chunk->msg_->payload();
    }

    // read message header from socket to buffer.
    static char mh_sizes[] = {11, 7, 3, 0};
    int mh_size = mh_sizes[(int)fmt];

    if (mh_size > 0 && (err = in_buffer_->grow(skt_, mh_size)) != srs_success) {
        return srs_error_wrap(err, "read %d bytes message header", mh_size);
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
        char *p = in_buffer_->read_slice(mh_size);

        char *pp = (char *)&chunk->header_.timestamp_delta_;
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
        chunk->has_extended_timestamp_ = (chunk->header_.timestamp_delta_ >= RTMP_EXTENDED_TIMESTAMP);

        if (fmt <= RTMP_FMT_TYPE1) {
            int32_t payload_length = 0;
            pp = (char *)&payload_length;
            pp[2] = *p++;
            pp[1] = *p++;
            pp[0] = *p++;
            pp[3] = 0;

            // for a message, if msg exists in cache, the size must not changed.
            // always use the actual msg size to compare, for the cache payload length can changed,
            // for the fmt type1(stream_id not changed), user can change the payload
            // length(it's not allowed in the continue chunks).
            if (!is_first_chunk_of_msg && chunk->header_.payload_length_ != payload_length) {
                return srs_error_new(ERROR_RTMP_PACKET_SIZE, "msg in chunk cache, size=%d cannot change to %d", chunk->header_.payload_length_, payload_length);
            }

            chunk->header_.payload_length_ = payload_length;
            chunk->header_.message_type_ = *p++;

            if (fmt == RTMP_FMT_TYPE0) {
                pp = (char *)&chunk->header_.stream_id_;
                pp[0] = *p++;
                pp[1] = *p++;
                pp[2] = *p++;
                pp[3] = *p++;
            }
        }
    }

    // read extended-timestamp
    if (chunk->has_extended_timestamp_) {
        if ((err = in_buffer_->grow(skt_, 4)) != srs_success) {
            return srs_error_wrap(err, "read 4 bytes ext timestamp");
        }
        // the ptr to the slice maybe invalid when grow()
        // reset the p to get 4bytes slice.
        char *p = in_buffer_->read_slice(4);

        uint32_t timestamp = 0x00;
        char *pp = (char *)&timestamp;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;

        // always use 31bits timestamp, for some server may use 32bits extended timestamp.
        timestamp &= 0x7fffffff;

        /**
         * For the RTMP v1 2009 version
         *      6.1.3. Extended Timestamp, This field is transmitted only when the normal time
         *      stamp in the chunk message header is set to 0x00ffffff. If normal time stamp is
         *      set to any value less than 0x00ffffff, this field MUST NOT be present. This field
         *      MUST NOT be present if the timestamp field is not present. Type 3 chunks MUST
         *      NOT have this field.
         *
         * For the RTMP v1 2012 version
         *      5.3.1.3. Extended Timestamp, The Extended Timestamp field is used to encode
         *      timestamps or timestamp deltas that are greater than 16777215 (0xFFFFFF); that
         *      is, for timestamps or timestamp deltas that don’t fit in the 24 bit fields of
         *      Type 0, 1, or 2 chunks. This field encodes the complete 32-bit timestamp or
         *      timestamp delta. The presence of this field is indicated by setting the timestamp
         *      field of a Type 0 chunk, or the timestamp delta field of a Type 1 or 2 chunk, to
         *      16777215 (0xFFFFFF). This field is present in Type 3 chunks when the most recent
         *      Type 0, 1, or 2 chunk for the same chunk stream ID indicated the presence of an
         *      extended timestamp field.
         *
         * FMLE/FMS/Flash Player always send the extended-timestamp, followed the 2012 version,
         * which means always send the extended timestamp in Type 3 chunks.
         *
         * librtmp may donot send this filed, need to detect the value.
         * @see also: http://blog.csdn.net/win_lin/article/details/13363699
         * compare to the chunk timestamp, which is set by chunk message header
         * type 0,1 or 2.
         *
         * @remark, ffmpeg/nginx send the extended-timestamp in sequence-header,
         * and timestamp delta in continue C1 chunks, and so compatible with ffmpeg,
         * that is, there is no continue chunks and extended-timestamp in nginx-rtmp.
         *
         * @remark, srs always send the extended-timestamp, to keep simple,
         * and compatible with adobe products.
         *
         * If the extended timestamp is present (RTMP v1 2012 version), it MUST be equal to the
         * previous one in the same chunk. Should skip back 4 bytes if the extended timestamp
         * is not present (RTMP v1 2009 version). See https://github.com/veovera/enhanced-rtmp/issues/42
         * for details.
         */
        uint32_t chunk_extended_timestamp = (uint32_t)chunk->extended_timestamp_;
        if (!is_first_chunk_of_msg && chunk_extended_timestamp > 0 && chunk_extended_timestamp != timestamp) {
            in_buffer_->skip(-4);
        } else {
            chunk->extended_timestamp_ = timestamp;
        }
    }

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
    uint32_t timestamp = chunk->has_extended_timestamp_ ? chunk->extended_timestamp_ : chunk->header_.timestamp_delta_;
    if (fmt == RTMP_FMT_TYPE0) {
        // 6.1.2.1. Type 0
        // For a type-0 chunk, the absolute timestamp of the message is sent
        // here.
        chunk->header_.timestamp_ = timestamp;
    } else if (is_first_chunk_of_msg) {
        // 6.1.2.2. Type 1
        // 6.1.2.3. Type 2
        // For a type-1 or type-2 chunk, the difference between the previous
        // chunk's timestamp and the current chunk's timestamp is sent here.
        chunk->header_.timestamp_ += timestamp;
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
    chunk->header_.timestamp_ &= 0x7fffffff;

    // valid message, the payload_length is 24bits,
    // so it should never be negative.
    srs_assert(chunk->header_.payload_length_ >= 0);

    // copy header to msg
    chunk->msg_->header_ = chunk->header_;

    // increase the msg count, the chunk stream can accept fmt=1/2/3 message now.
    chunk->msg_count_++;

    return err;
}

srs_error_t SrsProtocol::read_message_payload(SrsChunkStream *chunk, SrsRtmpCommonMessage **pmsg)
{
    srs_error_t err = srs_success;

    // empty message
    if (chunk->header_.payload_length_ <= 0) {
        srs_trace("get an empty RTMP message(type=%d, size=%d, time=%" PRId64 ", sid=%d)", chunk->header_.message_type_,
                  chunk->header_.payload_length_, chunk->header_.timestamp_, chunk->header_.stream_id_);

        *pmsg = chunk->msg_;

        chunk->msg_ = NULL;
        chunk->writing_pos_ = NULL;

        return err;
    }
    srs_assert(chunk->header_.payload_length_ > 0);

    // the chunk payload size.
    int nn_written = (int)(chunk->writing_pos_ - chunk->msg_->payload());
    int payload_size = chunk->header_.payload_length_ - nn_written; // Left bytes to read.
    payload_size = srs_min(payload_size, in_chunk_size_);           // Restrict to chunk size.

    // create msg payload if not initialized
    if (!chunk->msg_->payload()) {
        chunk->msg_->create_payload(chunk->header_.payload_length_);
        chunk->writing_pos_ = chunk->msg_->payload();
    }

    // read payload to buffer
    if ((err = in_buffer_->grow(skt_, payload_size)) != srs_success) {
        return srs_error_wrap(err, "read %d bytes payload", payload_size);
    }
    memcpy(chunk->writing_pos_, in_buffer_->read_slice(payload_size), payload_size);
    chunk->writing_pos_ += payload_size;

    // got entire RTMP message?
    nn_written = (int)(chunk->writing_pos_ - chunk->msg_->payload());
    if (nn_written == chunk->header_.payload_length_) {
        *pmsg = chunk->msg_;

        chunk->msg_ = NULL;
        chunk->writing_pos_ = NULL;
        return err;
    }

    return err;
}

srs_error_t SrsProtocol::on_recv_message(SrsRtmpCommonMessage *msg)
{
    srs_error_t err = srs_success;

    srs_assert(msg != NULL);

    // try to response acknowledgement
    if ((err = response_acknowledgement_message()) != srs_success) {
        return srs_error_wrap(err, "response ack");
    }

    SrsRtmpCommand *packet_raw = NULL;
    switch (msg->header_.message_type_) {
    case RTMP_MSG_SetChunkSize:
    case RTMP_MSG_UserControlMessage:
    case RTMP_MSG_WindowAcknowledgementSize:
        if ((err = decode_message(msg, &packet_raw)) != srs_success) {
            return srs_error_wrap(err, "decode message");
        }
        break;
    case RTMP_MSG_VideoMessage:
    case RTMP_MSG_AudioMessage:
        print_debug_info();
    default:
        return err;
    }

    // always free the packet.
    srs_assert(packet_raw);
    SrsUniquePtr<SrsRtmpCommand> packet(packet_raw);

    switch (msg->header_.message_type_) {
    case RTMP_MSG_WindowAcknowledgementSize: {
        SrsSetWindowAckSizePacket *pkt = dynamic_cast<SrsSetWindowAckSizePacket *>(packet.get());
        srs_assert(pkt != NULL);

        if (pkt->ackowledgement_window_size_ > 0) {
            in_ack_size_.window_ = (uint32_t)pkt->ackowledgement_window_size_;
            // @remark, we ignore this message, for user noneed to care.
            // but it's important for dev, for client/server will block if required
            // ack msg not arrived.
        }
        break;
    }
    case RTMP_MSG_SetChunkSize: {
        SrsSetChunkSizePacket *pkt = dynamic_cast<SrsSetChunkSizePacket *>(packet.get());
        srs_assert(pkt != NULL);

        // for some server, the actual chunk size can greater than the max value(65536),
        // so we just warning the invalid chunk size, and actually use it is ok,
        // @see: https://github.com/ossrs/srs/issues/160
        if (pkt->chunk_size_ < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE || pkt->chunk_size_ > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE) {
            srs_warn("accept chunk=%d, should in [%d, %d], please see #160",
                     pkt->chunk_size_, SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, SRS_CONSTS_RTMP_MAX_CHUNK_SIZE);
        }

        // @see: https://github.com/ossrs/srs/issues/541
        if (pkt->chunk_size_ < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE) {
            return srs_error_new(ERROR_RTMP_CHUNK_SIZE, "chunk size should be %d+, value=%d", SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, pkt->chunk_size_);
        }

        in_chunk_size_ = pkt->chunk_size_;
        break;
    }
    case RTMP_MSG_UserControlMessage: {
        SrsUserControlPacket *pkt = dynamic_cast<SrsUserControlPacket *>(packet.get());
        srs_assert(pkt != NULL);

        if (pkt->event_type_ == SrcPCUCSetBufferLength) {
            in_buffer_length_ = pkt->extra_data_;
        }
        if (pkt->event_type_ == SrcPCUCPingRequest) {
            if ((err = response_ping_message(pkt->event_data_)) != srs_success) {
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

srs_error_t SrsProtocol::on_send_packet(SrsMessageHeader *mh, SrsRtmpCommand *packet)
{
    srs_error_t err = srs_success;

    // ignore raw bytes oriented RTMP message.
    if (packet == NULL) {
        return err;
    }

    switch (mh->message_type_) {
    case RTMP_MSG_SetChunkSize: {
        SrsSetChunkSizePacket *pkt = dynamic_cast<SrsSetChunkSizePacket *>(packet);
        out_chunk_size_ = pkt->chunk_size_;
        break;
    }
    case RTMP_MSG_WindowAcknowledgementSize: {
        SrsSetWindowAckSizePacket *pkt = dynamic_cast<SrsSetWindowAckSizePacket *>(packet);
        out_ack_size_.window_ = (uint32_t)pkt->ackowledgement_window_size_;
        break;
    }
    case RTMP_MSG_AMF0CommandMessage:
    case RTMP_MSG_AMF3CommandMessage: {
        if (true) {
            SrsConnectAppPacket *pkt = dynamic_cast<SrsConnectAppPacket *>(packet);
            if (pkt) {
                requests_[pkt->transaction_id_] = pkt->command_name_;
                break;
            }
        }
        if (true) {
            SrsCreateStreamPacket *pkt = dynamic_cast<SrsCreateStreamPacket *>(packet);
            if (pkt) {
                requests_[pkt->transaction_id_] = pkt->command_name_;
                break;
            }
        }
        if (true) {
            SrsFMLEStartPacket *pkt = dynamic_cast<SrsFMLEStartPacket *>(packet);
            if (pkt) {
                requests_[pkt->transaction_id_] = pkt->command_name_;
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

    if (in_ack_size_.window_ <= 0) {
        return err;
    }

    // ignore when delta bytes not exceed half of window(ack size).
    uint32_t delta = (uint32_t)(skt_->get_recv_bytes() - in_ack_size_.nb_recv_bytes_);
    if (delta < in_ack_size_.window_ / 2) {
        return err;
    }
    in_ack_size_.nb_recv_bytes_ = skt_->get_recv_bytes();

    // when the sequence number overflow, reset it.
    uint32_t sequence_number = in_ack_size_.sequence_number_ + delta;
    if (sequence_number > 0xf0000000) {
        sequence_number = delta;
    }
    in_ack_size_.sequence_number_ = sequence_number;

    SrsAcknowledgementPacket *pkt = new SrsAcknowledgementPacket();
    pkt->sequence_number_ = sequence_number;

    // cache the message and use flush to send.
    if (!auto_response_when_recv_) {
        manual_response_queue_.push_back(pkt);
        return err;
    }

    // use underlayer api to send, donot flush again.
    if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send ack");
    }

    return err;
}

srs_error_t SrsProtocol::response_ping_message(int32_t timestamp)
{
    srs_error_t err = srs_success;

    srs_trace("get a ping request, response it. timestamp=%d", timestamp);

    SrsUserControlPacket *pkt = new SrsUserControlPacket();

    pkt->event_type_ = SrcPCUCPingResponse;
    pkt->event_data_ = timestamp;

    // cache the message and use flush to send.
    if (!auto_response_when_recv_) {
        manual_response_queue_.push_back(pkt);
        return err;
    }

    // use underlayer api to send, donot flush again.
    if ((err = do_send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "ping response");
    }

    return err;
}

void SrsProtocol::print_debug_info()
{
    if (show_debug_info_) {
        show_debug_info_ = false;
        srs_trace("protocol in.buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d", in_buffer_length_,
                  in_ack_size_.window_, out_ack_size_.window_, in_chunk_size_, out_chunk_size_);
    }
}

SrsChunkStream::SrsChunkStream(int _cid)
{
    fmt_ = 0;
    cid_ = _cid;
    has_extended_timestamp_ = false;
    msg_ = NULL;
    writing_pos_ = NULL;
    msg_count_ = 0;
    extended_timestamp_ = 0;
}

SrsChunkStream::~SrsChunkStream()
{
    srs_freep(msg_);
}

ISrsRequest::ISrsRequest()
{
}

ISrsRequest::~ISrsRequest()
{
}

void ISrsRequest::on_source_created()
{
}

SrsRequest::SrsRequest()
{
    objectEncoding_ = RTMP_SIG_AMF0_VER;
    duration_ = -1;
    port_ = SRS_CONSTS_RTMP_DEFAULT_PORT;
    args_ = NULL;

    protocol_ = "rtmp";
}

SrsRequest::~SrsRequest()
{
    srs_freep(args_);
}

ISrsRequest *SrsRequest::copy()
{
    SrsRequest *cp = new SrsRequest();

    cp->ip_ = ip_;
    cp->vhost_ = vhost_;
    cp->app_ = app_;
    cp->objectEncoding_ = objectEncoding_;
    cp->pageUrl_ = pageUrl_;
    cp->host_ = host_;
    cp->port_ = port_;
    cp->param_ = param_;
    cp->schema_ = schema_;
    cp->stream_ = stream_;
    cp->swfUrl_ = swfUrl_;
    cp->tcUrl_ = tcUrl_;
    cp->duration_ = duration_;
    if (args_) {
        cp->args_ = args_->copy()->to_object();
    }

    cp->protocol_ = protocol_;

    return cp;
}

void SrsRequest::update_auth(ISrsRequest *req)
{
    pageUrl_ = req->pageUrl_;
    swfUrl_ = req->swfUrl_;
    tcUrl_ = req->tcUrl_;
    param_ = req->param_;

    ip_ = req->ip_;
    vhost_ = req->vhost_;
    app_ = req->app_;
    objectEncoding_ = req->objectEncoding_;
    host_ = req->host_;
    port_ = req->port_;
    param_ = req->param_;
    schema_ = req->schema_;
    duration_ = req->duration_;

    if (args_) {
        srs_freep(args_);
    }
    if (req->args_) {
        args_ = req->args_->copy()->to_object();
    }

    protocol_ = req->protocol_;

    srs_info("update req of soruce for auth ok");
}

string SrsRequest::get_stream_url()
{
    return srs_net_url_encode_sid(vhost_, app_, stream_);
}

void SrsRequest::strip()
{
    // remove the unsupported chars in names.
    host_ = srs_strings_remove(host_, "/ \n\r\t");
    vhost_ = srs_strings_remove(vhost_, "/ \n\r\t");
    app_ = srs_strings_remove(app_, " \n\r\t");
    stream_ = srs_strings_remove(stream_, " \n\r\t");

    // remove end slash of app/stream
    app_ = srs_strings_trim_end(app_, "/");
    stream_ = srs_strings_trim_end(stream_, "/");

    // remove start slash of app/stream
    app_ = srs_strings_trim_start(app_, "/");
    stream_ = srs_strings_trim_start(stream_, "/");
}

ISrsRequest *SrsRequest::as_http()
{
    schema_ = "http";
    tcUrl_ = srs_net_url_encode_tcurl(schema_, host_, vhost_, app_, port_);
    return this;
}

SrsResponse::SrsResponse()
{
    stream_id_ = SRS_DEFAULT_SID;
}

SrsResponse::~SrsResponse()
{
}

string srs_client_type_string(SrsRtmpConnType type)
{
    switch (type) {
    case SrsRtmpConnPlay:
        return "rtmp-play";
    case SrsHlsPlay:
        return "hls-play";
    case SrsFlvPlay:
        return "flv-play";
    case SrsRtcConnPlay:
        return "rtc-play";
    case SrsRtmpConnFlashPublish:
        return "flash-publish";
    case SrsRtmpConnFMLEPublish:
        return "fmle-publish";
    case SrsRtmpConnHaivisionPublish:
        return "haivision-publish";
    case SrsRtcConnPublish:
        return "rtc-publish";
    case SrsSrtConnPlay:
        return "srt-play";
    case SrsSrtConnPublish:
        return "srt-publish";
    default:
        return "Unknown";
    }
}

bool srs_client_type_is_publish(SrsRtmpConnType type)
{
    return (type & 0xff00) == 0x0200;
}

SrsHandshakeBytes::SrsHandshakeBytes()
{
    c0c1_ = s0s1s2_ = c2_ = NULL;
    proxy_real_ip_ = 0;
}

SrsHandshakeBytes::~SrsHandshakeBytes()
{
    dispose();
}

void SrsHandshakeBytes::dispose()
{
    srs_freepa(c0c1_);
    srs_freepa(s0s1s2_);
    srs_freepa(c2_);
}

srs_error_t SrsHandshakeBytes::read_c0c1(ISrsProtocolReader *io)
{
    srs_error_t err = srs_success;

    if (c0c1_) {
        return err;
    }

    ssize_t nsize;

    c0c1_ = new char[1537];
    if ((err = io->read_fully(c0c1_, 1537, &nsize)) != srs_success) {
        return srs_error_wrap(err, "read c0c1");
    }

    // Whether RTMP proxy, @see https://github.com/ossrs/go-oryx/wiki/RtmpProxy
    if (uint8_t(c0c1_[0]) == 0xF3) {
        uint16_t nn = uint16_t(c0c1_[1]) << 8 | uint16_t(c0c1_[2]);
        ssize_t nn_consumed = 3 + nn;
        if (nn > 1024) {
            return srs_error_new(ERROR_RTMP_PROXY_EXCEED, "proxy exceed max size, nn=%d", nn);
        }

        // 4B client real IP.
        if (nn >= 4) {
            proxy_real_ip_ = uint32_t(c0c1_[3]) << 24 | uint32_t(c0c1_[4]) << 16 | uint32_t(c0c1_[5]) << 8 | uint32_t(c0c1_[6]);
            nn -= 4;
        }

        memmove(c0c1_, c0c1_ + nn_consumed, 1537 - nn_consumed);
        if ((err = io->read_fully(c0c1_ + 1537 - nn_consumed, nn_consumed, &nsize)) != srs_success) {
            return srs_error_wrap(err, "read c0c1");
        }
    }

    return err;
}

srs_error_t SrsHandshakeBytes::read_s0s1s2(ISrsProtocolReader *io)
{
    srs_error_t err = srs_success;

    if (s0s1s2_) {
        return err;
    }

    ssize_t nsize;

    s0s1s2_ = new char[3073];
    if ((err = io->read_fully(s0s1s2_, 3073, &nsize)) != srs_success) {
        return srs_error_wrap(err, "read s0s1s2");
    }

    return err;
}

srs_error_t SrsHandshakeBytes::read_c2(ISrsProtocolReader *io)
{
    srs_error_t err = srs_success;

    if (c2_) {
        return err;
    }

    ssize_t nsize;

    c2_ = new char[1536];
    if ((err = io->read_fully(c2_, 1536, &nsize)) != srs_success) {
        return srs_error_wrap(err, "read c2");
    }

    return err;
}

srs_error_t SrsHandshakeBytes::create_c0c1()
{
    srs_error_t err = srs_success;

    if (c0c1_) {
        return err;
    }

    c0c1_ = new char[1537];
    srs_rand_gen_bytes(c0c1_, 1537);

    // plain text required.
    SrsBuffer stream(c0c1_, 9);

    stream.write_1bytes(0x03);
    stream.write_4bytes((int32_t)::time(NULL));
    stream.write_4bytes(0x00);

    return err;
}

srs_error_t SrsHandshakeBytes::create_s0s1s2(const char *c1)
{
    srs_error_t err = srs_success;

    if (s0s1s2_) {
        return err;
    }

    s0s1s2_ = new char[3073];
    srs_rand_gen_bytes(s0s1s2_, 3073);

    // plain text required.
    SrsBuffer stream(s0s1s2_, 9);

    stream.write_1bytes(0x03);
    stream.write_4bytes((int32_t)::time(NULL));
    // s1 time2 copy from c1
    if (c0c1_) {
        stream.write_bytes(c0c1_ + 1, 4);
    }

    // if c1 specified, copy c1 to s2.
    // @see: https://github.com/ossrs/srs/issues/46
    if (c1) {
        memcpy(s0s1s2_ + 1537, c1, 1536);
    }

    return err;
}

srs_error_t SrsHandshakeBytes::create_c2()
{
    srs_error_t err = srs_success;

    if (c2_) {
        return err;
    }

    c2_ = new char[1536];
    srs_rand_gen_bytes(c2_, 1536);

    // time
    SrsBuffer stream(c2_, 8);

    stream.write_4bytes((int32_t)::time(NULL));
    // c2 time2 copy from s1
    if (s0s1s2_) {
        stream.write_bytes(s0s1s2_ + 1, 4);
    }

    return err;
}

SrsServerInfo::SrsServerInfo()
{
    pid_ = cid_ = 0;
    major_ = minor_ = revision_ = build_ = 0;
}

SrsRtmpClient::SrsRtmpClient(ISrsProtocolReadWriter *skt)
{
    io_ = skt;
    protocol_ = new SrsProtocol(skt);
    hs_bytes_ = new SrsHandshakeBytes();
}

SrsRtmpClient::~SrsRtmpClient()
{
    srs_freep(protocol_);
    srs_freep(hs_bytes_);
}

void SrsRtmpClient::set_recv_timeout(srs_utime_t tm)
{
    protocol_->set_recv_timeout(tm);
}

void SrsRtmpClient::set_send_timeout(srs_utime_t tm)
{
    protocol_->set_send_timeout(tm);
}

int64_t SrsRtmpClient::get_recv_bytes()
{
    return protocol_->get_recv_bytes();
}

int64_t SrsRtmpClient::get_send_bytes()
{
    return protocol_->get_send_bytes();
}

srs_error_t SrsRtmpClient::recv_message(SrsRtmpCommonMessage **pmsg)
{
    return protocol_->recv_message(pmsg);
}

srs_error_t SrsRtmpClient::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    return protocol_->decode_message(msg, ppacket);
}

srs_error_t SrsRtmpClient::send_and_free_message(SrsMediaPacket *msg, int stream_id)
{
    return protocol_->send_and_free_message(msg, stream_id);
}

srs_error_t SrsRtmpClient::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id)
{
    return protocol_->send_and_free_messages(msgs, nb_msgs, stream_id);
}

srs_error_t SrsRtmpClient::send_and_free_packet(SrsRtmpCommand *packet, int stream_id)
{
    return protocol_->send_and_free_packet(packet, stream_id);
}

srs_error_t SrsRtmpClient::handshake()
{
    srs_error_t err = srs_success;

    srs_assert(hs_bytes_);

    // maybe st has problem when alloc object on stack, always alloc object at heap.
    // @see https://github.com/ossrs/srs/issues/509
    SrsUniquePtr<SrsComplexHandshake> complex_hs(new SrsComplexHandshake());

    if ((err = complex_hs->handshake_with_server(hs_bytes_, io_)) != srs_success) {
        // As client, we never verify s0s1s2, because some server doesn't follow the RTMP spec.
        // So we never have chance to use simple handshake.
        return srs_error_wrap(err, "complex handshake");
    }

    hs_bytes_->dispose();

    return err;
}

srs_error_t SrsRtmpClient::simple_handshake()
{
    srs_error_t err = srs_success;

    srs_assert(hs_bytes_);

    SrsSimpleHandshake simple_hs;
    if ((err = simple_hs.handshake_with_server(hs_bytes_, io_)) != srs_success) {
        return srs_error_wrap(err, "simple handshake");
    }

    hs_bytes_->dispose();

    return err;
}

srs_error_t SrsRtmpClient::complex_handshake()
{
    srs_error_t err = srs_success;

    srs_assert(hs_bytes_);

    SrsComplexHandshake complex_hs;
    if ((err = complex_hs.handshake_with_server(hs_bytes_, io_)) != srs_success) {
        return srs_error_wrap(err, "complex handshake");
    }

    hs_bytes_->dispose();

    return err;
}

srs_error_t SrsRtmpClient::connect_app(string app, string tcUrl, ISrsRequest *r, bool dsu, SrsServerInfo *si)
{
    srs_error_t err = srs_success;

    // Connect(vhost, app)
    if (true) {
        SrsConnectAppPacket *pkt = new SrsConnectAppPacket();

        pkt->command_object_->set("app", SrsAmf0Any::str(app.c_str()));
        pkt->command_object_->set("flashVer", SrsAmf0Any::str("WIN 15,0,0,239"));
        if (r) {
            pkt->command_object_->set("swfUrl", SrsAmf0Any::str(r->swfUrl_.c_str()));
        } else {
            pkt->command_object_->set("swfUrl", SrsAmf0Any::str());
        }
        if (r && r->tcUrl_ != "") {
            pkt->command_object_->set("tcUrl", SrsAmf0Any::str(r->tcUrl_.c_str()));
        } else {
            pkt->command_object_->set("tcUrl", SrsAmf0Any::str(tcUrl.c_str()));
        }
        pkt->command_object_->set("fpad", SrsAmf0Any::boolean(false));
        pkt->command_object_->set("capabilities", SrsAmf0Any::number(239));
        pkt->command_object_->set("audioCodecs", SrsAmf0Any::number(3575));
        pkt->command_object_->set("videoCodecs", SrsAmf0Any::number(252));
        pkt->command_object_->set("videoFunction", SrsAmf0Any::number(1));
        if (r) {
            pkt->command_object_->set("pageUrl", SrsAmf0Any::str(r->pageUrl_.c_str()));
        } else {
            pkt->command_object_->set("pageUrl", SrsAmf0Any::str());
        }
        pkt->command_object_->set("objectEncoding", SrsAmf0Any::number(0));

        // @see https://github.com/ossrs/srs/issues/160
        // the debug_srs_upnode is config in vhost and default to true.
        if (dsu && r && r->args_ && r->args_->count() > 0) {
            srs_freep(pkt->args_);
            pkt->args_ = r->args_->copy()->to_object();
        }

        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }

    // Set Window Acknowledgement size(2500000)
    if (true) {
        SrsSetWindowAckSizePacket *pkt = new SrsSetWindowAckSizePacket();
        pkt->ackowledgement_window_size_ = 2500000;
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }

    // expect connect _result
    SrsRtmpCommonMessage *msg_raw = NULL;
    SrsConnectAppResPacket *pkt_raw = NULL;
    if ((err = expect_message<SrsConnectAppResPacket>(&msg_raw, &pkt_raw)) != srs_success) {
        return srs_error_wrap(err, "expect connect app response");
    }

    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
    SrsUniquePtr<SrsConnectAppResPacket> pkt(pkt_raw);

    // server info
    SrsAmf0Any *data = pkt->info_->get_property("data");
    if (si && data && data->is_ecma_array()) {
        SrsAmf0EcmaArray *arr = data->to_ecma_array();

        SrsAmf0Any *prop = NULL;
        if ((prop = arr->ensure_property_string("srs_server_ip")) != NULL) {
            si->ip_ = prop->to_str();
        }
        if ((prop = arr->ensure_property_string("srs_server")) != NULL) {
            si->sig_ = prop->to_str();
        }
        if ((prop = arr->ensure_property_number("srs_id")) != NULL) {
            si->cid_ = (int)prop->to_number();
        }
        if ((prop = arr->ensure_property_number("srs_pid")) != NULL) {
            si->pid_ = (int)prop->to_number();
        }
        if ((prop = arr->ensure_property_string("srs_version")) != NULL) {
            vector<string> versions = srs_strings_split(prop->to_str(), ".");
            if (versions.size() > 0) {
                si->major_ = ::atoi(versions.at(0).c_str());
                if (versions.size() > 1) {
                    si->minor_ = ::atoi(versions.at(1).c_str());
                    if (versions.size() > 2) {
                        si->revision_ = ::atoi(versions.at(2).c_str());
                        if (versions.size() > 3) {
                            si->build_ = ::atoi(versions.at(3).c_str());
                        }
                    }
                }
            }
        }
    }

    if (si) {
        srs_trace("connected, version=%d.%d.%d.%d, ip=%s, pid=%d, id=%d, dsu=%d",
                  si->major_, si->minor_, si->revision_, si->build_, si->ip_.c_str(), si->pid_, si->cid_, dsu);
    } else {
        srs_trace("connected, dsu=%d", dsu);
    }

    return err;
}

srs_error_t SrsRtmpClient::create_stream(int &stream_id)
{
    srs_error_t err = srs_success;

    // CreateStream
    if (true) {
        SrsCreateStreamPacket *pkt = new SrsCreateStreamPacket();
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send packet");
        }
    }

    // CreateStream _result.
    if (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsCreateStreamResPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsCreateStreamResPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "expect create stream response");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsCreateStreamResPacket> pkt(pkt_raw);

        stream_id = (int)pkt->stream_id_;
    }

    return err;
}

srs_error_t SrsRtmpClient::play(string stream, int stream_id, int chunk_size)
{
    srs_error_t err = srs_success;

    // Play(stream)
    if (true) {
        SrsPlayPacket *pkt = new SrsPlayPacket();
        pkt->stream_name_ = stream;
        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send play stream failed. stream=%s, stream_id=%d", stream.c_str(), stream_id);
        }
    }

    // SetBufferLength(1000ms)
    int buffer_length_ms = 1000;
    if (true) {
        SrsUserControlPacket *pkt = new SrsUserControlPacket();

        pkt->event_type_ = SrcPCUCSetBufferLength;
        pkt->event_data_ = stream_id;
        pkt->extra_data_ = buffer_length_ms;

        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send set buffer length failed. stream=%s, stream_id=%d, bufferLength=%d", stream.c_str(), stream_id, buffer_length_ms);
        }
    }

    // SetChunkSize
    if (chunk_size != SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE) {
        SrsSetChunkSizePacket *pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size_ = chunk_size;
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send set chunk size failed. stream=%s, chunk_size=%d", stream.c_str(), chunk_size);
        }
    }

    return err;
}

srs_error_t SrsRtmpClient::publish(string stream, int stream_id, int chunk_size)
{
    srs_error_t err = srs_success;

    // SetChunkSize
    if (chunk_size != SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE) {
        SrsSetChunkSizePacket *pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size_ = chunk_size;
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send set chunk size failed. stream=%s, chunk_size=%d", stream.c_str(), chunk_size);
        }
    }

    // publish(stream)
    if (true) {
        SrsPublishPacket *pkt = new SrsPublishPacket();
        pkt->stream_name_ = stream;
        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send publish message failed. stream=%s, stream_id=%d", stream.c_str(), stream_id);
        }
    }

    return err;
}

srs_error_t SrsRtmpClient::fmle_publish(string stream, int &stream_id)
{
    stream_id = 0;

    srs_error_t err = srs_success;

    // SrsFMLEStartPacket
    if (true) {
        SrsFMLEStartPacket *pkt = SrsFMLEStartPacket::create_release_stream(stream);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send FMLE publish release stream failed. stream=%s", stream.c_str());
        }
    }

    // FCPublish
    if (true) {
        SrsFMLEStartPacket *pkt = SrsFMLEStartPacket::create_FC_publish(stream);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send FMLE publish FCPublish failed. stream=%s", stream.c_str());
        }
    }

    // CreateStream
    if (true) {
        SrsCreateStreamPacket *pkt = new SrsCreateStreamPacket();
        pkt->transaction_id_ = 4;
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send FMLE publish createStream failed. stream=%s", stream.c_str());
        }
    }

    // expect result of CreateStream
    if (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsCreateStreamResPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsCreateStreamResPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "expect create stream response message failed");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsCreateStreamResPacket> pkt(pkt_raw);

        stream_id = (int)pkt->stream_id_;
    }

    // publish(stream)
    if (true) {
        SrsPublishPacket *pkt = new SrsPublishPacket();
        pkt->stream_name_ = stream;
        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send FMLE publish publish failed. stream=%s, stream_id=%d", stream.c_str(), stream_id);
        }
    }

    return err;
}

SrsRtmpServer::SrsRtmpServer(ISrsProtocolReadWriter *skt)
{
    io_ = skt;
    protocol_ = new SrsProtocol(skt);
    hs_bytes_ = new SrsHandshakeBytes();
}

SrsRtmpServer::~SrsRtmpServer()
{
    srs_freep(protocol_);
    srs_freep(hs_bytes_);
}

uint32_t SrsRtmpServer::proxy_real_ip()
{
    return hs_bytes_->proxy_real_ip_;
}

void SrsRtmpServer::set_auto_response(bool v)
{
    protocol_->set_auto_response(v);
}

#ifdef SRS_PERF_MERGED_READ
void SrsRtmpServer::set_merge_read(bool v, IMergeReadHandler *handler)
{
    protocol_->set_merge_read(v, handler);
}

void SrsRtmpServer::set_recv_buffer(int buffer_size)
{
    protocol_->set_recv_buffer(buffer_size);
}
#endif

void SrsRtmpServer::set_recv_timeout(srs_utime_t tm)
{
    protocol_->set_recv_timeout(tm);
}

srs_utime_t SrsRtmpServer::get_recv_timeout()
{
    return protocol_->get_recv_timeout();
}

void SrsRtmpServer::set_send_timeout(srs_utime_t tm)
{
    protocol_->set_send_timeout(tm);
}

srs_utime_t SrsRtmpServer::get_send_timeout()
{
    return protocol_->get_send_timeout();
}

int64_t SrsRtmpServer::get_recv_bytes()
{
    return protocol_->get_recv_bytes();
}

int64_t SrsRtmpServer::get_send_bytes()
{
    return protocol_->get_send_bytes();
}

srs_error_t SrsRtmpServer::recv_message(SrsRtmpCommonMessage **pmsg)
{
    return protocol_->recv_message(pmsg);
}

srs_error_t SrsRtmpServer::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    return protocol_->decode_message(msg, ppacket);
}

srs_error_t SrsRtmpServer::send_and_free_message(SrsMediaPacket *msg, int stream_id)
{
    return protocol_->send_and_free_message(msg, stream_id);
}

srs_error_t SrsRtmpServer::send_and_free_messages(SrsMediaPacket **msgs, int nb_msgs, int stream_id)
{
    return protocol_->send_and_free_messages(msgs, nb_msgs, stream_id);
}

srs_error_t SrsRtmpServer::send_and_free_packet(SrsRtmpCommand *packet, int stream_id)
{
    return protocol_->send_and_free_packet(packet, stream_id);
}

srs_error_t SrsRtmpServer::handshake()
{
    srs_error_t err = srs_success;

    srs_assert(hs_bytes_);

    SrsComplexHandshake complex_hs;
    if ((err = complex_hs.handshake_with_client(hs_bytes_, io_)) != srs_success) {
        if (srs_error_code(err) == ERROR_RTMP_TRY_SIMPLE_HS) {
            srs_freep(err);

            SrsSimpleHandshake simple_hs;
            if ((err = simple_hs.handshake_with_client(hs_bytes_, io_)) != srs_success) {
                return srs_error_wrap(err, "simple handshake");
            }
        } else {
            return srs_error_wrap(err, "complex handshake");
        }
    }

    hs_bytes_->dispose();

    return err;
}

srs_error_t SrsRtmpServer::connect_app(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    SrsRtmpCommonMessage *msg_raw = NULL;
    SrsConnectAppPacket *pkt_raw = NULL;
    if ((err = expect_message<SrsConnectAppPacket>(&msg_raw, &pkt_raw)) != srs_success) {
        return srs_error_wrap(err, "expect connect app response");
    }

    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
    SrsUniquePtr<SrsConnectAppPacket> pkt(pkt_raw);

    SrsAmf0Any *prop = NULL;

    if ((prop = pkt->command_object_->ensure_property_string("tcUrl")) == NULL) {
        return srs_error_new(ERROR_RTMP_REQ_CONNECT, "invalid request without tcUrl");
    }
    req->tcUrl_ = prop->to_str();

    if ((prop = pkt->command_object_->ensure_property_string("pageUrl")) != NULL) {
        req->pageUrl_ = prop->to_str();
    }

    if ((prop = pkt->command_object_->ensure_property_string("swfUrl")) != NULL) {
        req->swfUrl_ = prop->to_str();
    }

    if ((prop = pkt->command_object_->ensure_property_number("objectEncoding")) != NULL) {
        req->objectEncoding_ = prop->to_number();
    }

    if (pkt->args_) {
        srs_freep(req->args_);
        req->args_ = pkt->args_->copy()->to_object();
    }

    srs_net_url_parse_tcurl(req->tcUrl_, req->schema_, req->host_, req->vhost_, req->app_, req->stream_, req->port_, req->param_);
    req->strip();

    return err;
}

srs_error_t SrsRtmpServer::set_window_ack_size(int ack_size)
{
    srs_error_t err = srs_success;

    SrsSetWindowAckSizePacket *pkt = new SrsSetWindowAckSizePacket();
    pkt->ackowledgement_window_size_ = ack_size;
    if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send ack");
    }

    return err;
}

srs_error_t SrsRtmpServer::set_in_window_ack_size(int ack_size)
{
    return protocol_->set_in_window_ack_size(ack_size);
}

srs_error_t SrsRtmpServer::set_peer_bandwidth(int bandwidth, int type)
{
    srs_error_t err = srs_success;

    SrsSetPeerBandwidthPacket *pkt = new SrsSetPeerBandwidthPacket();
    pkt->bandwidth_ = bandwidth;
    pkt->type_ = type;
    if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send set peer bandwidth");
    }

    return err;
}

srs_error_t SrsRtmpServer::response_connect_app(ISrsRequest *req, const char *server_ip)
{
    srs_error_t err = srs_success;

    SrsConnectAppResPacket *pkt = new SrsConnectAppResPacket();

    // @remark For windows, there must be a space between const string and macro.
    pkt->props_->set("fmsVer", SrsAmf0Any::str("FMS/" RTMP_SIG_FMS_VER));
    pkt->props_->set("capabilities", SrsAmf0Any::number(127));
    pkt->props_->set("mode", SrsAmf0Any::number(1));

    pkt->info_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
    pkt->info_->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectSuccess));
    pkt->info_->set(StatusDescription, SrsAmf0Any::str("Connection succeeded"));
    pkt->info_->set("objectEncoding", SrsAmf0Any::number(req->objectEncoding_));
    SrsAmf0EcmaArray *data = SrsAmf0Any::ecma_array();
    pkt->info_->set("data", data);

    data->set("version", SrsAmf0Any::str(RTMP_SIG_FMS_VER));
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_SERVER));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    data->set("srs_authors", SrsAmf0Any::str(RTMP_SIG_SRS_AUTHORS));

    if (server_ip) {
        data->set("srs_server_ip", SrsAmf0Any::str(server_ip));
    }
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::str(_srs_context->get_id().c_str()));

    if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send connect app response");
    }

    return err;
}

#define SRS_RTMP_REDIRECT_TIMEOUT (3 * SRS_UTIME_SECONDS)
srs_error_t SrsRtmpServer::redirect(ISrsRequest *r, string url, bool &accepted)
{
    srs_error_t err = srs_success;

    if (true) {
        SrsAmf0Object *ex = SrsAmf0Any::object();
        ex->set("code", SrsAmf0Any::number(302));

        // The redirect is tcUrl while redirect2 is RTMP URL.
        // https://github.com/ossrs/srs/issues/1575#issuecomment-574999798
        string tcUrl = srs_path_filepath_dir(url);
        ex->set("redirect", SrsAmf0Any::str(tcUrl.c_str()));
        ex->set("redirect2", SrsAmf0Any::str(url.c_str()));

        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelError));
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectRejected));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("RTMP 302 Redirect"));
        pkt->data_->set("ex", ex);

        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send redirect/reject");
        }
    }

    // client must response a call message.
    // or we never know whether the client is ok to redirect.
    protocol_->set_recv_timeout(SRS_RTMP_REDIRECT_TIMEOUT);
    if (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsCallPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsCallPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            srs_freep(err);
            // ignore any error of redirect response.
            return srs_success;
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsCallPacket> pkt(pkt_raw);

        string message;
        if (pkt->arguments_ && pkt->arguments_->is_string()) {
            message = pkt->arguments_->to_str();
            accepted = true;
        }
    }

    return err;
}

void SrsRtmpServer::response_connect_reject(ISrsRequest * /*req*/, const char *desc)
{
    srs_error_t err = srs_success;

    SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();
    pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelError));
    pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeConnectRejected));
    pkt->data_->set(StatusDescription, SrsAmf0Any::str(desc));

    if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
        srs_warn("send reject response err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return;
}

srs_error_t SrsRtmpServer::on_bw_done()
{
    srs_error_t err = srs_success;

    SrsOnBWDonePacket *pkt = new SrsOnBWDonePacket();
    if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send onBWDone");
    }

    return err;
}

srs_error_t SrsRtmpServer::identify_client(int stream_id, SrsRtmpConnType &type, string &stream_name, srs_utime_t &duration)
{
    type = SrsRtmpConnUnknown;
    srs_error_t err = srs_success;

    while (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        if ((err = protocol_->recv_message(&msg_raw)) != srs_success) {
            return srs_error_wrap(err, "recv identify message");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsMessageHeader &h = msg->header_;

        if (h.is_ackledgement() || h.is_set_chunk_size() || h.is_window_ackledgement_size() || h.is_user_control_message()) {
            continue;
        }

        if (!h.is_amf0_command() && !h.is_amf3_command()) {
            srs_trace("ignore message type=%#x", h.message_type_);
            continue;
        }

        SrsRtmpCommand *pkt_raw = NULL;
        if ((err = protocol_->decode_message(msg.get(), &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "decode identify");
        }
        SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

        if (dynamic_cast<SrsCreateStreamPacket *>(pkt.get())) {
            return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket *>(pkt.get()), stream_id, 3, type, stream_name, duration);
        }
        if (dynamic_cast<SrsFMLEStartPacket *>(pkt.get())) {
            return identify_fmle_publish_client(dynamic_cast<SrsFMLEStartPacket *>(pkt.get()), type, stream_name);
        }
        if (dynamic_cast<SrsPlayPacket *>(pkt.get())) {
            return identify_play_client(dynamic_cast<SrsPlayPacket *>(pkt.get()), type, stream_name, duration);
        }

        // call msg,
        // support response null first,
        // TODO: FIXME: response in right way, or forward in edge mode.
        SrsCallPacket *call = dynamic_cast<SrsCallPacket *>(pkt.get());
        if (call) {
            SrsCallResPacket *res = new SrsCallResPacket(call->transaction_id_);
            res->command_object_ = SrsAmf0Any::null();
            res->response_ = SrsAmf0Any::null();
            if ((err = protocol_->send_and_free_packet(res, 0)) != srs_success) {
                return srs_error_wrap(err, "response call");
            }

            // For encoder of Haivision, it always send a _checkbw call message.
            // @remark the next message is createStream, so we continue to identify it.
            // @see https://github.com/ossrs/srs/issues/844
            if (call->command_name_ == "_checkbw") {
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

    SrsSetChunkSizePacket *pkt = new SrsSetChunkSizePacket();
    pkt->chunk_size_ = chunk_size;
    if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send set chunk size");
    }

    return err;
}

srs_error_t SrsRtmpServer::start_play(int stream_id)
{
    srs_error_t err = srs_success;

    // StreamBegin
    if (true) {
        SrsUserControlPacket *pkt = new SrsUserControlPacket();
        pkt->event_type_ = SrcPCUCStreamBegin;
        pkt->event_data_ = stream_id;
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send StreamBegin");
        }
    }

    // onStatus(NetStream.Play.Reset)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamReset));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Playing and resetting stream."));
        pkt->data_->set(StatusDetails, SrsAmf0Any::str("stream"));
        pkt->data_->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Play.Reset");
        }
    }

    // onStatus(NetStream.Play.Start)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamStart));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Started playing stream."));
        pkt->data_->set(StatusDetails, SrsAmf0Any::str("stream"));
        pkt->data_->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Play.Start");
        }
    }

    // |RtmpSampleAccess(false, false)
    if (true) {
        SrsNaluSampleAccessPacket *pkt = new SrsNaluSampleAccessPacket();

        // allow audio/video sample.
        // @see: https://github.com/ossrs/srs/issues/49
        pkt->audio_sample_access_ = true;
        pkt->video_sample_access_ = true;

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send |RtmpSampleAccess true");
        }
    }

    // onStatus(NetStream.Data.Start)
    // We should not response this packet, or there is an empty stream "Stream #0:0: Data: none" in FFmpeg.
    if (false) {
        SrsOnStatusDataPacket *pkt = new SrsOnStatusDataPacket();
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeDataStart));
        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Data.Start");
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
            SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

            pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
            pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamPause));
            pkt->data_->set(StatusDescription, SrsAmf0Any::str("Paused stream."));

            if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
                return srs_error_wrap(err, "send NetStream.Pause.Notify");
            }
        }
        // StreamEOF
        if (true) {
            SrsUserControlPacket *pkt = new SrsUserControlPacket();

            pkt->event_type_ = SrcPCUCStreamEOF;
            pkt->event_data_ = stream_id;

            if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
                return srs_error_wrap(err, "send StreamEOF");
            }
        }
    } else {
        // onStatus(NetStream.Unpause.Notify)
        if (true) {
            SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

            pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
            pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeStreamUnpause));
            pkt->data_->set(StatusDescription, SrsAmf0Any::str("Unpaused stream."));

            if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
                return srs_error_wrap(err, "send NetStream.Unpause.Notify");
            }
        }
        // StreamBegin
        if (true) {
            SrsUserControlPacket *pkt = new SrsUserControlPacket();

            pkt->event_type_ = SrcPCUCStreamBegin;
            pkt->event_data_ = stream_id;

            if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
                return srs_error_wrap(err, "send StreamBegin");
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
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsFMLEStartPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsFMLEStartPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "recv FCPublish");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsFMLEStartPacket> pkt(pkt_raw);

        fc_publish_tid = pkt->transaction_id_;
    }
    // FCPublish response
    if (true) {
        SrsFMLEStartResPacket *pkt = new SrsFMLEStartResPacket(fc_publish_tid);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send FCPublish response");
        }
    }

    // createStream
    double create_stream_tid = 0;
    if (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsCreateStreamPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsCreateStreamPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "recv createStream");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsCreateStreamPacket> pkt(pkt_raw);

        create_stream_tid = pkt->transaction_id_;
    }
    // createStream response
    if (true) {
        SrsCreateStreamResPacket *pkt = new SrsCreateStreamResPacket(create_stream_tid, stream_id);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send createStream response");
        }
    }

    // publish
    if (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsPublishPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsPublishPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "recv publish");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsPublishPacket> pkt(pkt_raw);
    }
    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->command_name_ = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Publish.Start");
        }
    }

    return err;
}

srs_error_t SrsRtmpServer::start_haivision_publish(int stream_id)
{
    srs_error_t err = srs_success;

    // publish
    if (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        SrsPublishPacket *pkt_raw = NULL;
        if ((err = expect_message<SrsPublishPacket>(&msg_raw, &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "recv publish");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsUniquePtr<SrsPublishPacket> pkt(pkt_raw);
    }

    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->command_name_ = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Publish.Start");
        }
    }

    return err;
}

srs_error_t SrsRtmpServer::fmle_unpublish(int stream_id, double unpublish_tid)
{
    srs_error_t err = srs_success;

    // publish response onFCUnpublish(NetStream.unpublish.Success)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->command_name_ = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Stop publishing stream."));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.unpublish.Success");
        }
    }
    // FCUnpublish response
    if (true) {
        SrsFMLEStartResPacket *pkt = new SrsFMLEStartResPacket(unpublish_tid);
        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send FCUnpublish response");
        }
    }
    // publish response onStatus(NetStream.Unpublish.Success)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodeUnpublishSuccess));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Stream is now unpublished"));
        pkt->data_->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Unpublish.Success");
        }
    }

    return err;
}

srs_error_t SrsRtmpServer::start_flash_publish(int stream_id)
{
    srs_error_t err = srs_success;
    return err;
}

srs_error_t SrsRtmpServer::start_publishing(int stream_id)
{
    srs_error_t err = srs_success;

    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
        SrsOnStatusCallPacket *pkt = new SrsOnStatusCallPacket();

        pkt->data_->set(StatusLevel, SrsAmf0Any::str(StatusLevelStatus));
        pkt->data_->set(StatusCode, SrsAmf0Any::str(StatusCodePublishStart));
        pkt->data_->set(StatusDescription, SrsAmf0Any::str("Started publishing stream."));
        pkt->data_->set(StatusClientId, SrsAmf0Any::str(RTMP_SIG_CLIENT_ID));

        if ((err = protocol_->send_and_free_packet(pkt, stream_id)) != srs_success) {
            return srs_error_wrap(err, "send NetStream.Publish.Start");
        }
    }

    return err;
}

srs_error_t SrsRtmpServer::identify_create_stream_client(SrsCreateStreamPacket *req, int stream_id, int depth, SrsRtmpConnType &type, string &stream_name, srs_utime_t &duration)
{
    srs_error_t err = srs_success;

    if (depth <= 0) {
        return srs_error_new(ERROR_RTMP_CREATE_STREAM_DEPTH, "create stream recursive depth");
    }

    if (true) {
        SrsCreateStreamResPacket *pkt = new SrsCreateStreamResPacket(req->transaction_id_, stream_id);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send createStream response");
        }
    }

    while (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        if ((err = protocol_->recv_message(&msg_raw)) != srs_success) {
            return srs_error_wrap(err, "recv identify");
        }

        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
        SrsMessageHeader &h = msg->header_;

        if (h.is_ackledgement() || h.is_set_chunk_size() || h.is_window_ackledgement_size() || h.is_user_control_message()) {
            continue;
        }

        if (!h.is_amf0_command() && !h.is_amf3_command()) {
            srs_trace("ignore message type=%#x", h.message_type_);
            continue;
        }

        SrsRtmpCommand *pkt_raw = NULL;
        if ((err = protocol_->decode_message(msg.get(), &pkt_raw)) != srs_success) {
            return srs_error_wrap(err, "decode identify");
        }
        SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

        if (dynamic_cast<SrsPlayPacket *>(pkt.get())) {
            return identify_play_client(dynamic_cast<SrsPlayPacket *>(pkt.get()), type, stream_name, duration);
        }
        if (dynamic_cast<SrsPublishPacket *>(pkt.get())) {
            return identify_flash_publish_client(dynamic_cast<SrsPublishPacket *>(pkt.get()), type, stream_name);
        }
        if (dynamic_cast<SrsCreateStreamPacket *>(pkt.get())) {
            return identify_create_stream_client(dynamic_cast<SrsCreateStreamPacket *>(pkt.get()), stream_id, depth - 1, type, stream_name, duration);
        }
        if (dynamic_cast<SrsFMLEStartPacket *>(pkt.get())) {
            return identify_haivision_publish_client(dynamic_cast<SrsFMLEStartPacket *>(pkt.get()), type, stream_name);
        }

        srs_trace("ignore AMF0/AMF3 command message.");
    }

    return err;
}

srs_error_t SrsRtmpServer::identify_fmle_publish_client(SrsFMLEStartPacket *req, SrsRtmpConnType &type, string &stream_name)
{
    srs_error_t err = srs_success;

    type = SrsRtmpConnFMLEPublish;
    stream_name = req->stream_name_;

    // releaseStream response
    if (true) {
        SrsFMLEStartResPacket *pkt = new SrsFMLEStartResPacket(req->transaction_id_);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send releaseStream response");
        }
    }

    return err;
}

srs_error_t SrsRtmpServer::identify_haivision_publish_client(SrsFMLEStartPacket *req, SrsRtmpConnType &type, string &stream_name)
{
    srs_error_t err = srs_success;

    type = SrsRtmpConnHaivisionPublish;
    stream_name = req->stream_name_;

    // FCPublish response
    if (true) {
        SrsFMLEStartResPacket *pkt = new SrsFMLEStartResPacket(req->transaction_id_);
        if ((err = protocol_->send_and_free_packet(pkt, 0)) != srs_success) {
            return srs_error_wrap(err, "send FCPublish");
        }
    }

    return err;
}

srs_error_t SrsRtmpServer::identify_flash_publish_client(SrsPublishPacket *req, SrsRtmpConnType &type, string &stream_name)
{
    type = SrsRtmpConnFlashPublish;
    stream_name = req->stream_name_;

    return srs_success;
}

srs_error_t SrsRtmpServer::identify_play_client(SrsPlayPacket *req, SrsRtmpConnType &type, string &stream_name, srs_utime_t &duration)
{
    type = SrsRtmpConnPlay;
    stream_name = req->stream_name_;
    duration = srs_utime_t(req->duration_) * SRS_UTIME_MILLISECONDS;

    return srs_success;
}

SrsConnectAppPacket::SrsConnectAppPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_CONNECT;
    transaction_id_ = 1;
    command_object_ = SrsAmf0Any::object();
    // optional
    args_ = NULL;
}

SrsConnectAppPacket::~SrsConnectAppPacket()
{
    srs_freep(command_object_);
    srs_freep(args_);
}

srs_error_t SrsConnectAppPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_CONNECT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    // some client donot send id=1.0, so we only warn user if not match.
    if (transaction_id_ != 1.0) {
        srs_warn("invalid transaction_id=%.2f", transaction_id_);
    }

    if ((err = command_object_->read(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if (!stream->empty()) {
        srs_freep(args_);

        // see: https://github.com/ossrs/srs/issues/186
        // the args maybe any amf0, for instance, a string. we should drop if not object.
        SrsAmf0Any *any = NULL;
        if ((err = SrsAmf0Any::discovery(stream, &any)) != srs_success) {
            return srs_error_wrap(err, "args");
        }
        srs_assert(any);

        // read the instance
        if ((err = any->read(stream)) != srs_success) {
            srs_freep(any);
            return srs_error_wrap(err, "args");
        }

        // drop when not an AMF0 object.
        if (!any->is_object()) {
            srs_warn("drop the args, see: '4.1.1. connect', marker=%#x", (uint8_t)any->marker_);
            srs_freep(any);
        } else {
            args_ = any->to_object();
        }
    }

    return err;
}

int SrsConnectAppPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsConnectAppPacket::get_size()
{
    int size = 0;

    size += SrsAmf0Size::str(command_name_);
    size += SrsAmf0Size::number();
    size += SrsAmf0Size::object(command_object_);
    if (args_) {
        size += SrsAmf0Size::object(args_);
    }

    return size;
}

srs_error_t SrsConnectAppPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = command_object_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if (args_ && (err = args_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "args");
    }

    return err;
}

SrsConnectAppResPacket::SrsConnectAppResPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_RESULT;
    transaction_id_ = 1;
    props_ = SrsAmf0Any::object();
    info_ = SrsAmf0Any::object();
}

SrsConnectAppResPacket::~SrsConnectAppResPacket()
{
    srs_freep(props_);
    srs_freep(info_);
}

srs_error_t SrsConnectAppResPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_RESULT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    // some client donot send id=1.0, so we only warn user if not match.
    if (transaction_id_ != 1.0) {
        srs_warn("invalid transaction_id=%.2f", transaction_id_);
    }

    // for RED5(1.0.6), the props is NULL, we must ignore it.
    // @see https://github.com/ossrs/srs/issues/418
    if (!stream->empty()) {
        SrsAmf0Any *p = NULL;
        if ((err = srs_amf0_read_any(stream, &p)) != srs_success) {
            return srs_error_wrap(err, "args");
        }

        // ignore when props is not amf0 object.
        if (!p->is_object()) {
            srs_warn("ignore connect response props marker=%#x.", (uint8_t)p->marker_);
            srs_freep(p);
        } else {
            srs_freep(props_);
            props_ = p->to_object();
        }
    }

    if ((err = info_->read(stream)) != srs_success) {
        return srs_error_wrap(err, "args");
    }

    return err;
}

int SrsConnectAppResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsConnectAppResPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::object(props_) + SrsAmf0Size::object(info_);
}

srs_error_t SrsConnectAppResPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = props_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "props");
    }

    if ((err = info_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "info");
    }

    return err;
}

SrsCallPacket::SrsCallPacket()
{
    command_name_ = "";
    transaction_id_ = 0;
    command_object_ = NULL;
    arguments_ = NULL;
}

SrsCallPacket::~SrsCallPacket()
{
    srs_freep(command_object_);
    srs_freep(arguments_);
}

srs_error_t SrsCallPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty()) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "empty command_name");
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    srs_freep(command_object_);
    if ((err = SrsAmf0Any::discovery(stream, &command_object_)) != srs_success) {
        return srs_error_wrap(err, "discovery command_object");
    }
    if ((err = command_object_->read(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if (!stream->empty()) {
        srs_freep(arguments_);
        if ((err = SrsAmf0Any::discovery(stream, &arguments_)) != srs_success) {
            return srs_error_wrap(err, "discovery args");
        }
        if ((err = arguments_->read(stream)) != srs_success) {
            return srs_error_wrap(err, "read args");
        }
    }

    return err;
}

int SrsCallPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCallPacket::get_size()
{
    int size = 0;

    size += SrsAmf0Size::str(command_name_) + SrsAmf0Size::number();

    if (command_object_) {
        size += command_object_->total_size();
    }

    if (arguments_) {
        size += arguments_->total_size();
    }

    return size;
}

srs_error_t SrsCallPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if (command_object_ && (err = command_object_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if (arguments_ && (err = arguments_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "args");
    }

    return err;
}

SrsCallResPacket::SrsCallResPacket(double _transaction_id)
{
    command_name_ = RTMP_AMF0_COMMAND_RESULT;
    transaction_id_ = _transaction_id;
    command_object_ = NULL;
    response_ = NULL;
}

SrsCallResPacket::~SrsCallResPacket()
{
    srs_freep(command_object_);
    srs_freep(response_);
}

int SrsCallResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCallResPacket::get_size()
{
    int size = 0;

    size += SrsAmf0Size::str(command_name_) + SrsAmf0Size::number();

    if (command_object_) {
        size += command_object_->total_size();
    }

    if (response_) {
        size += response_->total_size();
    }

    return size;
}

srs_error_t SrsCallResPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if (command_object_ && (err = command_object_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if (response_ && (err = response_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "response");
    }

    return err;
}

SrsCreateStreamPacket::SrsCreateStreamPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_CREATE_STREAM;
    transaction_id_ = 2;
    command_object_ = SrsAmf0Any::null();
}

SrsCreateStreamPacket::~SrsCreateStreamPacket()
{
    srs_freep(command_object_);
}

void SrsCreateStreamPacket::set_command_object(SrsAmf0Any *v)
{
    srs_freep(command_object_);
    command_object_ = v;
}

srs_error_t SrsCreateStreamPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_CREATE_STREAM) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    return err;
}

int SrsCreateStreamPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCreateStreamPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null();
}

srs_error_t SrsCreateStreamPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    return err;
}

SrsCreateStreamResPacket::SrsCreateStreamResPacket(double _transaction_id, double _stream_id)
{
    command_name_ = RTMP_AMF0_COMMAND_RESULT;
    transaction_id_ = _transaction_id;
    command_object_ = SrsAmf0Any::null();
    stream_id_ = _stream_id;
}

SrsCreateStreamResPacket::~SrsCreateStreamResPacket()
{
    srs_freep(command_object_);
}

srs_error_t SrsCreateStreamResPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_RESULT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_read_number(stream, stream_id_)) != srs_success) {
        return srs_error_wrap(err, "stream_id");
    }

    return err;
}

int SrsCreateStreamResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsCreateStreamResPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::number();
}

srs_error_t SrsCreateStreamResPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_write_number(stream, stream_id_)) != srs_success) {
        return srs_error_wrap(err, "stream_id");
    }

    return err;
}

SrsCloseStreamPacket::SrsCloseStreamPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_CLOSE_STREAM;
    transaction_id_ = 0;
    command_object_ = SrsAmf0Any::null();
}

SrsCloseStreamPacket::~SrsCloseStreamPacket()
{
    srs_freep(command_object_);
}

srs_error_t SrsCloseStreamPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    return err;
}

SrsFMLEStartPacket::SrsFMLEStartPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_RELEASE_STREAM;
    transaction_id_ = 0;
    command_object_ = SrsAmf0Any::null();
}

SrsFMLEStartPacket::~SrsFMLEStartPacket()
{
    srs_freep(command_object_);
}

void SrsFMLEStartPacket::set_command_object(SrsAmf0Any *v)
{
    srs_freep(command_object_);
    command_object_ = v;
}

srs_error_t SrsFMLEStartPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    bool invalid_command_name = (command_name_ != RTMP_AMF0_COMMAND_RELEASE_STREAM && command_name_ != RTMP_AMF0_COMMAND_FC_PUBLISH && command_name_ != RTMP_AMF0_COMMAND_UNPUBLISH);
    if (command_name_.empty() || invalid_command_name) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_read_string(stream, stream_name_)) != srs_success) {
        return srs_error_wrap(err, "stream_name");
    }

    return err;
}

int SrsFMLEStartPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsFMLEStartPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name_);
}

srs_error_t SrsFMLEStartPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_write_string(stream, stream_name_)) != srs_success) {
        return srs_error_wrap(err, "stream_name");
    }

    return err;
}

SrsFMLEStartPacket *SrsFMLEStartPacket::create_release_stream(string stream)
{
    SrsFMLEStartPacket *pkt = new SrsFMLEStartPacket();

    pkt->command_name_ = RTMP_AMF0_COMMAND_RELEASE_STREAM;
    pkt->transaction_id_ = 2;
    pkt->stream_name_ = stream;

    return pkt;
}

SrsFMLEStartPacket *SrsFMLEStartPacket::create_FC_publish(string stream)
{
    SrsFMLEStartPacket *pkt = new SrsFMLEStartPacket();

    pkt->command_name_ = RTMP_AMF0_COMMAND_FC_PUBLISH;
    pkt->transaction_id_ = 3;
    pkt->stream_name_ = stream;

    return pkt;
}

SrsFMLEStartResPacket::SrsFMLEStartResPacket(double _transaction_id)
{
    command_name_ = RTMP_AMF0_COMMAND_RESULT;
    transaction_id_ = _transaction_id;
    command_object_ = SrsAmf0Any::null();
    args_ = SrsAmf0Any::undefined();
}

SrsFMLEStartResPacket::~SrsFMLEStartResPacket()
{
    srs_freep(command_object_);
    srs_freep(args_);
}

void SrsFMLEStartResPacket::set_args(SrsAmf0Any *v)
{
    srs_freep(args_);
    args_ = v;
}

void SrsFMLEStartResPacket::set_command_object(SrsAmf0Any *v)
{
    srs_freep(command_object_);
    command_object_ = v;
}

srs_error_t SrsFMLEStartResPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_RESULT) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_read_undefined(stream)) != srs_success) {
        return srs_error_wrap(err, "stream_id");
    }

    return err;
}

int SrsFMLEStartResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsFMLEStartResPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::undefined();
}

srs_error_t SrsFMLEStartResPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_write_undefined(stream)) != srs_success) {
        return srs_error_wrap(err, "args");
    }

    return err;
}

SrsPublishPacket::SrsPublishPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_PUBLISH;
    transaction_id_ = 0;
    command_object_ = SrsAmf0Any::null();
    type_ = "live";
}

SrsPublishPacket::~SrsPublishPacket()
{
    srs_freep(command_object_);
}

void SrsPublishPacket::set_command_object(SrsAmf0Any *v)
{
    srs_freep(command_object_);
    command_object_ = v;
}

srs_error_t SrsPublishPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_PUBLISH) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_read_string(stream, stream_name_)) != srs_success) {
        return srs_error_wrap(err, "stream_name");
    }

    if (!stream->empty() && (err = srs_amf0_read_string(stream, type_)) != srs_success) {
        return srs_error_wrap(err, "publish type");
    }

    return err;
}

int SrsPublishPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsPublishPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name_) + SrsAmf0Size::str(type_);
}

srs_error_t SrsPublishPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_write_string(stream, stream_name_)) != srs_success) {
        return srs_error_wrap(err, "stream_name");
    }

    if ((err = srs_amf0_write_string(stream, type_)) != srs_success) {
        return srs_error_wrap(err, "type");
    }

    return err;
}

SrsPausePacket::SrsPausePacket()
{
    command_name_ = RTMP_AMF0_COMMAND_PAUSE;
    transaction_id_ = 0;
    command_object_ = SrsAmf0Any::null();

    time_ms_ = 0;
    is_pause_ = true;
}

SrsPausePacket::~SrsPausePacket()
{
    srs_freep(command_object_);
}

srs_error_t SrsPausePacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_PAUSE) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_read_boolean(stream, is_pause_)) != srs_success) {
        return srs_error_wrap(err, "is_pause");
    }

    if ((err = srs_amf0_read_number(stream, time_ms_)) != srs_success) {
        return srs_error_wrap(err, "time");
    }

    return err;
}

SrsPlayPacket::SrsPlayPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_PLAY;
    transaction_id_ = 0;
    command_object_ = SrsAmf0Any::null();

    start_ = -2;
    duration_ = -1;
    reset_ = true;
}

SrsPlayPacket::~SrsPlayPacket()
{
    srs_freep(command_object_);
}

srs_error_t SrsPlayPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }
    if (command_name_.empty() || command_name_ != RTMP_AMF0_COMMAND_PLAY) {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name_.c_str());
    }

    if ((err = srs_amf0_read_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_read_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_read_string(stream, stream_name_)) != srs_success) {
        return srs_error_wrap(err, "stream_name");
    }

    if (!stream->empty() && (err = srs_amf0_read_number(stream, start_)) != srs_success) {
        return srs_error_wrap(err, "start");
    }
    if (!stream->empty() && (err = srs_amf0_read_number(stream, duration_)) != srs_success) {
        return srs_error_wrap(err, "duration");
    }

    if (stream->empty()) {
        return err;
    }

    SrsAmf0Any *reset_value_raw = NULL;
    if ((err = srs_amf0_read_any(stream, &reset_value_raw)) != srs_success) {
        return srs_error_wrap(err, "reset");
    }
    SrsUniquePtr<SrsAmf0Any> reset_value(reset_value_raw);

    if (reset_value.get()) {
        // check if the value is bool or number
        // An optional Boolean value or number that specifies whether
        // to flush any previous playlist
        if (reset_value->is_boolean()) {
            reset_ = reset_value->to_boolean();
        } else if (reset_value->is_number()) {
            reset_ = (reset_value->to_number() != 0);
        } else {
            return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid marker=%#x", (uint8_t)reset_value->marker_);
        }
    }

    return err;
}

int SrsPlayPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsPlayPacket::get_size()
{
    int size = SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::str(stream_name_);

    if (start_ != -2 || duration_ != -1 || !reset_) {
        size += SrsAmf0Size::number();
    }

    if (duration_ != -1 || !reset_) {
        size += SrsAmf0Size::number();
    }

    if (!reset_) {
        size += SrsAmf0Size::boolean();
    }

    return size;
}

srs_error_t SrsPlayPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = srs_amf0_write_string(stream, stream_name_)) != srs_success) {
        return srs_error_wrap(err, "stream_name");
    }

    if ((start_ != -2 || duration_ != -1 || !reset_) && (err = srs_amf0_write_number(stream, start_)) != srs_success) {
        return srs_error_wrap(err, "start");
    }

    if ((duration_ != -1 || !reset_) && (err = srs_amf0_write_number(stream, duration_)) != srs_success) {
        return srs_error_wrap(err, "duration");
    }

    if (!reset_ && (err = srs_amf0_write_boolean(stream, reset_)) != srs_success) {
        return srs_error_wrap(err, "reset");
    }

    return err;
}

SrsPlayResPacket::SrsPlayResPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_RESULT;
    transaction_id_ = 0;
    command_object_ = SrsAmf0Any::null();
    desc_ = SrsAmf0Any::object();
}

SrsPlayResPacket::~SrsPlayResPacket()
{
    srs_freep(command_object_);
    srs_freep(desc_);
}

void SrsPlayResPacket::set_command_object(SrsAmf0Any *v)
{
    srs_freep(command_object_);
    command_object_ = v;
}

void SrsPlayResPacket::set_desc(SrsAmf0Object *v)
{
    srs_freep(desc_);
    desc_ = v;
}

int SrsPlayResPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsPlayResPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::object(desc_);
}

srs_error_t SrsPlayResPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "command_object");
    }

    if ((err = desc_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "desc");
    }

    return err;
}

SrsOnBWDonePacket::SrsOnBWDonePacket()
{
    command_name_ = RTMP_AMF0_COMMAND_ON_BW_DONE;
    transaction_id_ = 0;
    args_ = SrsAmf0Any::null();
}

SrsOnBWDonePacket::~SrsOnBWDonePacket()
{
    srs_freep(args_);
}

void SrsOnBWDonePacket::set_args(SrsAmf0Any *v)
{
    srs_freep(args_);
    args_ = v;
}

int SrsOnBWDonePacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsOnBWDonePacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null();
}

srs_error_t SrsOnBWDonePacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "args");
    }

    return err;
}

SrsOnStatusCallPacket::SrsOnStatusCallPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_ON_STATUS;
    transaction_id_ = 0;
    args_ = SrsAmf0Any::null();
    data_ = SrsAmf0Any::object();
}

SrsOnStatusCallPacket::~SrsOnStatusCallPacket()
{
    srs_freep(args_);
    srs_freep(data_);
}

void SrsOnStatusCallPacket::set_args(SrsAmf0Any *v)
{
    srs_freep(args_);
    args_ = v;
}

void SrsOnStatusCallPacket::set_data(SrsAmf0Object *v)
{
    srs_freep(data_);
    data_ = v;
}

int SrsOnStatusCallPacket::get_message_type()
{
    return RTMP_MSG_AMF0CommandMessage;
}

int SrsOnStatusCallPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::number() + SrsAmf0Size::null() + SrsAmf0Size::object(data_);
}

srs_error_t SrsOnStatusCallPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_number(stream, transaction_id_)) != srs_success) {
        return srs_error_wrap(err, "transaction_id");
    }

    if ((err = srs_amf0_write_null(stream)) != srs_success) {
        return srs_error_wrap(err, "args");
    }

    if ((err = data_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "data");
    }

    return err;
}

SrsOnStatusDataPacket::SrsOnStatusDataPacket()
{
    command_name_ = RTMP_AMF0_COMMAND_ON_STATUS;
    data_ = SrsAmf0Any::object();
}

SrsOnStatusDataPacket::~SrsOnStatusDataPacket()
{
    srs_freep(data_);
}

void SrsOnStatusDataPacket::set_data(SrsAmf0Object *v)
{
    srs_freep(data_);
    data_ = v;
}

int SrsOnStatusDataPacket::get_message_type()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SrsOnStatusDataPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::object(data_);
}

srs_error_t SrsOnStatusDataPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = data_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "data");
    }

    return err;
}

SrsNaluSampleAccessPacket::SrsNaluSampleAccessPacket()
{
    command_name_ = RTMP_AMF0_DATA_SAMPLE_ACCESS;
    video_sample_access_ = false;
    audio_sample_access_ = false;
}

SrsNaluSampleAccessPacket::~SrsNaluSampleAccessPacket()
{
}

int SrsNaluSampleAccessPacket::get_message_type()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SrsNaluSampleAccessPacket::get_size()
{
    return SrsAmf0Size::str(command_name_) + SrsAmf0Size::boolean() + SrsAmf0Size::boolean();
}

srs_error_t SrsNaluSampleAccessPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, command_name_)) != srs_success) {
        return srs_error_wrap(err, "command_name");
    }

    if ((err = srs_amf0_write_boolean(stream, video_sample_access_)) != srs_success) {
        return srs_error_wrap(err, "video sample access");
    }

    if ((err = srs_amf0_write_boolean(stream, audio_sample_access_)) != srs_success) {
        return srs_error_wrap(err, "audio sample access");
    }

    return err;
}

SrsOnMetaDataPacket::SrsOnMetaDataPacket()
{
    name_ = SRS_CONSTS_RTMP_ON_METADATA;
    metadata_ = SrsAmf0Any::object();
}

SrsOnMetaDataPacket::~SrsOnMetaDataPacket()
{
    srs_freep(metadata_);
}

void SrsOnMetaDataPacket::set_metadata(SrsAmf0Object *v)
{
    srs_freep(metadata_);
    metadata_ = v;
}

srs_error_t SrsOnMetaDataPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_read_string(stream, name_)) != srs_success) {
        return srs_error_wrap(err, "name");
    }

    // ignore the @setDataFrame
    if (name_ == SRS_CONSTS_RTMP_SET_DATAFRAME) {
        if ((err = srs_amf0_read_string(stream, name_)) != srs_success) {
            return srs_error_wrap(err, "name");
        }
    }

    // Allows empty body metadata.
    if (stream->empty()) {
        return err;
    }

    // the metadata maybe object or ecma array
    SrsAmf0Any *any_raw = NULL;
    if ((err = srs_amf0_read_any(stream, &any_raw)) != srs_success) {
        return srs_error_wrap(err, "metadata");
    }

    srs_assert(any_raw);
    if (any_raw->is_object()) {
        srs_freep(metadata_);
        metadata_ = any_raw->to_object();
        return err;
    }

    SrsUniquePtr<SrsAmf0Any> any(any_raw);
    if (any->is_ecma_array()) {
        SrsAmf0EcmaArray *arr = any->to_ecma_array();

        // if ecma array, copy to object.
        for (int i = 0; i < arr->count(); i++) {
            metadata_->set(arr->key_at(i), arr->value_at(i)->copy());
        }
    }

    return err;
}

int SrsOnMetaDataPacket::get_message_type()
{
    return RTMP_MSG_AMF0DataMessage;
}

int SrsOnMetaDataPacket::get_size()
{
    return SrsAmf0Size::str(name_) + SrsAmf0Size::object(metadata_);
}

srs_error_t SrsOnMetaDataPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if ((err = srs_amf0_write_string(stream, name_)) != srs_success) {
        return srs_error_wrap(err, "name");
    }

    if ((err = metadata_->write(stream)) != srs_success) {
        return srs_error_wrap(err, "metadata");
    }

    return err;
}

SrsSetWindowAckSizePacket::SrsSetWindowAckSizePacket()
{
    ackowledgement_window_size_ = 0;
}

SrsSetWindowAckSizePacket::~SrsSetWindowAckSizePacket()
{
}

srs_error_t SrsSetWindowAckSizePacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->left());
    }

    ackowledgement_window_size_ = stream->read_4bytes();

    return err;
}

int SrsSetWindowAckSizePacket::get_message_type()
{
    return RTMP_MSG_WindowAcknowledgementSize;
}

int SrsSetWindowAckSizePacket::get_size()
{
    return 4;
}

srs_error_t SrsSetWindowAckSizePacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->left());
    }

    stream->write_4bytes(ackowledgement_window_size_);

    return err;
}

SrsAcknowledgementPacket::SrsAcknowledgementPacket()
{
    sequence_number_ = 0;
}

SrsAcknowledgementPacket::~SrsAcknowledgementPacket()
{
}

srs_error_t SrsAcknowledgementPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->left());
    }

    sequence_number_ = (uint32_t)stream->read_4bytes();

    return err;
}

int SrsAcknowledgementPacket::get_message_type()
{
    return RTMP_MSG_Acknowledgement;
}

int SrsAcknowledgementPacket::get_size()
{
    return 4;
}

srs_error_t SrsAcknowledgementPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->left());
    }

    stream->write_4bytes(sequence_number_);

    return err;
}

SrsSetChunkSizePacket::SrsSetChunkSizePacket()
{
    chunk_size_ = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
}

SrsSetChunkSizePacket::~SrsSetChunkSizePacket()
{
}

srs_error_t SrsSetChunkSizePacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->left());
    }

    chunk_size_ = stream->read_4bytes();

    return err;
}

int SrsSetChunkSizePacket::get_message_type()
{
    return RTMP_MSG_SetChunkSize;
}

int SrsSetChunkSizePacket::get_size()
{
    return 4;
}

srs_error_t SrsSetChunkSizePacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(4)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->left());
    }

    stream->write_4bytes(chunk_size_);

    return err;
}

SrsSetPeerBandwidthPacket::SrsSetPeerBandwidthPacket()
{
    bandwidth_ = 0;
    type_ = SrsPeerBandwidthDynamic;
}

SrsSetPeerBandwidthPacket::~SrsSetPeerBandwidthPacket()
{
}

int SrsSetPeerBandwidthPacket::get_message_type()
{
    return RTMP_MSG_SetPeerBandwidth;
}

int SrsSetPeerBandwidthPacket::get_size()
{
    return 5;
}

srs_error_t SrsSetPeerBandwidthPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(5)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires 5 only %d bytes", stream->left());
    }

    stream->write_4bytes(bandwidth_);
    stream->write_1bytes(type_);

    return err;
}

SrsUserControlPacket::SrsUserControlPacket()
{
    event_type_ = 0;
    event_data_ = 0;
    extra_data_ = 0;
}

SrsUserControlPacket::~SrsUserControlPacket()
{
}

srs_error_t SrsUserControlPacket::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(2)) {
        return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 2 only %d bytes", stream->left());
    }

    event_type_ = stream->read_2bytes();

    if (event_type_ == SrsPCUCFmsEvent0) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 1 only %d bytes", stream->left());
        }
        event_data_ = stream->read_1bytes();
    } else {
        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->left());
        }
        event_data_ = stream->read_4bytes();
    }

    if (event_type_ == SrcPCUCSetBufferLength) {
        if (!stream->require(4)) {
            return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires 4 only %d bytes", stream->left());
        }
        extra_data_ = stream->read_4bytes();
    }

    return err;
}

int SrsUserControlPacket::get_message_type()
{
    return RTMP_MSG_UserControlMessage;
}

int SrsUserControlPacket::get_size()
{
    int size = 2;

    if (event_type_ == SrsPCUCFmsEvent0) {
        size += 1;
    } else {
        size += 4;
    }

    if (event_type_ == SrcPCUCSetBufferLength) {
        size += 4;
    }

    return size;
}

srs_error_t SrsUserControlPacket::encode_packet(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(get_size())) {
        return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires %d only %d bytes", get_size(), stream->left());
    }

    stream->write_2bytes(event_type_);

    if (event_type_ == SrsPCUCFmsEvent0) {
        stream->write_1bytes(event_data_);
    } else {
        stream->write_4bytes(event_data_);
    }

    // when event type is set buffer length,
    // write the extra buffer length.
    if (event_type_ == SrcPCUCSetBufferLength) {
        stream->write_4bytes(extra_data_);
    }

    return err;
}
