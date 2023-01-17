//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_ps.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>

#include <string>
using namespace std;

// The minimum required bytes to parse a PS packet.
#define SRS_PS_MIN_REQUIRED 32

SrsPsDecodeHelper::SrsPsDecodeHelper()
{;
    rtp_seq_ = 0;
    rtp_ts_ = 0;
    rtp_pt_ = 0;
    pack_id_ = 0;
    pack_first_seq_ = 0;
    pack_pre_msg_last_seq_ = 0;
    pack_nn_msgs_ = 0;

    ctx_ = NULL;
    ps_ = NULL;
}

ISrsPsMessageHandler::ISrsPsMessageHandler()
{
}

ISrsPsMessageHandler::~ISrsPsMessageHandler()
{
}

SrsPsContext::SrsPsContext()
{
    last_ = NULL;
    current_ = NULL;
    helper_.ctx_ = this;
    detect_ps_integrity_ = false;
    video_stream_type_ = SrsTsStreamReserved;
    audio_stream_type_ = SrsTsStreamReserved;
}

SrsPsContext::~SrsPsContext()
{
    srs_freep(last_);
    srs_freep(current_);
}

void SrsPsContext::set_detect_ps_integrity(bool v)
{
    detect_ps_integrity_ = v;
}

SrsTsMessage* SrsPsContext::last()
{
    if (!last_) {
        last_ = new SrsTsMessage();
        last_->ps_helper_ = &helper_;
    }
    return last_;
}

SrsTsMessage* SrsPsContext::reap()
{
    SrsTsMessage* msg = last_;
    last_ = new SrsTsMessage();
    last_->ps_helper_ = &helper_;
    return msg;
}

srs_error_t SrsPsContext::decode(SrsBuffer* stream, ISrsPsMessageHandler* handler)
{
    srs_error_t err = srs_success;

    // Decode PS packet one by one.
    while (!stream->empty()) {
        // If new PS packet but not enough packet, ignore.
        if (detect_ps_integrity_ && stream->left() >= 4) {
            uint8_t* p = (uint8_t*)stream->head();
            if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01 && stream->left() <= SRS_PS_MIN_REQUIRED) {
                break;
            }
        }

        // Try to decode the stream by one PS packet.
        // See Table 2-32 – Program Stream pack, hls-mpeg-ts-iso13818-1.pdf, page 73
        if ((err = do_decode(stream, handler)) != srs_success) {
            return srs_error_wrap(err, "decode");
        }

        // Use unit start as 1, however it has no effect because PES_packet_length should never be 0 for PS packet.
        const int payload_unit_start_indicator = 1;
        if (!last()->completed(payload_unit_start_indicator)) {
            continue; // Ignore if message not completed.
        }

        // Reap the last completed PS message.
        SrsTsMessage* msg = reap();
        SrsAutoFree(SrsTsMessage, msg);

        if (msg->sid == SrsTsPESStreamIdProgramStreamMap) {
            if (!msg->payload || !msg->payload->length()) {
                return srs_error_new(ERROR_GB_PS_HEADER, "empty PSM payload");
            }

            // Decode PSM(Program Stream map) from PES packet payload.
            SrsBuffer buf(msg->payload->bytes(), msg->payload->length());

            SrsPsPsmPacket psm;
            if ((err = psm.decode(&buf)) != srs_success) {
                return srs_error_wrap(err, "decode psm");
            }

            if (video_stream_type_ == SrsTsStreamReserved || audio_stream_type_ == SrsTsStreamReserved) {
                srs_trace("PS: Got PSM for video=%#x, audio=%#x", psm.video_elementary_stream_id_, psm.audio_elementary_stream_id_);
            } else {
                srs_info("PS: Got PSM for video=%#x, audio=%#x", psm.video_elementary_stream_id_, psm.audio_elementary_stream_id_);
            }
            video_stream_type_ = (SrsTsStream)psm.video_stream_type_;
            audio_stream_type_ = (SrsTsStream)psm.audio_stream_type_;
        } else if (msg->is_video() || msg->is_audio()) {
            // Update the total messages in pack.
            helper_.pack_pre_msg_last_seq_ = helper_.rtp_seq_;
            helper_.pack_nn_msgs_++;

            //srs_error("PS: Got message %s, dts=%" PRId64 ", payload=%dB", msg->is_video() ? "Video" : "Audio", msg->dts/9000, msg->PES_packet_length);
            if (handler && (err = handler->on_ts_message(msg)) != srs_success) {
                return srs_error_wrap(err, "handle PS message");
            }
        } else {
            srs_info("PS: Ignore message sid=%#x", msg->sid);
        }
    }

    return err;
}

srs_error_t SrsPsContext::do_decode(SrsBuffer* stream, ISrsPsMessageHandler* handler)
{
    srs_error_t err = srs_success;

    // If last message not completed, the bytes in stream must be payload.
    if (!last()->fresh()) {
        if ((err = last()->dump(stream, NULL)) != srs_success) {
            return srs_error_wrap(err, "dump pes");
        }
        return err;
    }

    // For PS pack, must always start with 00 00 01 XX.
    // See Table 2-32 – Program Stream pack, hls-mpeg-ts-iso13818-1.pdf, page 73
    if (!stream->require(4)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", stream->left());
    }
    uint8_t* p = (uint8_t*)stream->head();

    // For normal mode, should start with 00 00 01, for pack or system header or PES packet.
    if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01) {
        return srs_error_new(ERROR_GB_PS_HEADER, "Invalid PS stream %#x %#x %#x", p[0], p[1], p[2]);
    }

    // If pack start code, it's a net PS pack stream.
    if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01 && p[3] == 0xba) {
        srs_freep(current_);
    }
    if (!current_) {
        current_ = new SrsPsPacket(this);
        current_->id_ |= helper_.rtp_seq_; // The low 16 bits is reserved for RTP seq.
        helper_.pack_id_ = current_->id_;
        helper_.pack_first_seq_ = helper_.rtp_seq_;
        // Set the helper for decoder, pass-by values.
        helper_.ps_ = current_;
        helper_.pack_nn_msgs_ = 0;
    }

    // Try to decode the PS pack stream.
    int pos = stream->pos();
    if ((err = current_->decode(stream)) != srs_success) {
        err = srs_error_wrap(err, "decode start=%d, pos=%d, left=%d", pos, stream->pos(), stream->left());
        stream->skip(pos - stream->pos());
        return err;
    }

    return err;
}

SrsPsPacket::SrsPsPacket(SrsPsContext* context)
{
    context_ = context;
    has_pack_header_ = has_system_header_ = false;

    static uint32_t gid = 0;
    id_ = ((gid++) << 16) & 0xffff0000;

    pack_start_code_ = 0;
    system_clock_reference_base_ = 0;
    system_clock_reference_extension_ = 0;
    program_mux_rate_ = 0;
    pack_stuffing_length_ = 0;

    system_header_start_code_ = 0;
    header_length_ = 0;
    rate_bound_ = 0;
    audio_bound_ = 0;
    CSPS_flag_ = 0;
    system_audio_lock_flag_ = 0;
    system_video_lock_flag_ = 0;
    video_bound_ = 0;
    packet_rate_restriction_flag_ = 0;
    audio_stream_id_ = 0;
    audio_buffer_bound_scale_ = 0;
    audio_buffer_size_bound_ = 0;
    video_stream_id_ = 0;
    video_buffer_bound_scale_ = 0;
    video_buffer_size_bound_ = 0;
}

SrsPsPacket::~SrsPsPacket()
{
}

srs_error_t SrsPsPacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    // Program Stream pack header.
    if (!stream->require(4)) return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", stream->left());
    uint8_t* p = (uint8_t*)stream->head();
    if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01 && p[3] == 0xba) {
        if ((err = decode_pack(stream)) != srs_success) {
            return srs_error_wrap(err, "pack");
        }
        has_pack_header_ = true;
    }

    // Program stream system header.
    if (stream->empty()) return err; // Parsed done, OK.
    if (!stream->require(4)) return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", stream->left());
    p = (uint8_t*)stream->head();
    if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01 && p[3] == 0xbb) {
        if ((err = decode_system(stream)) != srs_success) {
            return srs_error_wrap(err, "system");
        }
        has_system_header_ = true;
    }

    // Packet start code prefix.
    while (!stream->empty()) {
        if (!stream->require(4)) return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", stream->left());
        p = (uint8_t*)stream->head();
        if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01) break;
        if (p[3] == 0xba || p[3] == 0xbb) break; // Reparse for pack or system header.

        SrsMpegPES pes;
        if ((err = pes.decode(stream)) != srs_success) {
            return srs_error_wrap(err, "decode pes");
        }

        SrsTsMessage* lm = context_->last();

        // The stream id should never change for PS stream.
        if (lm->sid != (SrsTsPESStreamId)0 && lm->sid != (SrsTsPESStreamId)pes.stream_id) {
            return srs_error_new(ERROR_GB_PS_PSE, "PS stream id change from %#x to %#x", lm->sid, pes.stream_id);
        }
        lm->sid = (SrsTsPESStreamId)pes.stream_id;

        if (pes.PTS_DTS_flags == 0x02 || pes.PTS_DTS_flags == 0x03) {
            lm->dts = pes.dts;
            lm->pts = pes.pts;
        }
        if (pes.has_payload_) {
            // The size of PS message, should be always a positive value.
            lm->PES_packet_length = pes.nb_payload_;
            if ((err = lm->dump(stream, &pes.nb_bytes)) != srs_success) {
                return srs_error_wrap(err, "dump pes");
            }
        }

        // Use unit start as 1, however it has no effect because PES_packet_length should never be 0 for PS packet.
        const int payload_unit_start_indicator = 1;
        if (lm->completed(payload_unit_start_indicator)) {
            return err; // OK, got one message, let PS context handle it.
        }
    }

    return err;
}

srs_error_t SrsPsPacket::decode_pack(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    // 14 bytes fixed header.
    if (!stream->require(14)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "ps requires 14 only %d bytes", stream->left());
    }

    pack_start_code_ = stream->read_4bytes();
    srs_assert(pack_start_code_ == 0x000001ba);

    uint64_t r0 = stream->read_4bytes();
    uint16_t r1 = stream->read_2bytes();
    system_clock_reference_extension_ = (r1 >> 1) & 0x1ff;
    system_clock_reference_base_ = 0x00
        | ((uint64_t) ((r0 >> 27) & 0x07) << 30) // 3bits
        | ((uint64_t) ((r0 >> 11) & 0x7fff) << 15) // 15bits
        | ((uint64_t) (r0 & 0x03ff) << 5) // 10bits
        | (uint64_t) ((r1 >> 11) & 0x1f); // 5bits

    program_mux_rate_ = stream->read_3bytes();
    program_mux_rate_ = (program_mux_rate_ >> 2) & 0x3fffff;

    pack_stuffing_length_ = stream->read_1bytes();
    pack_stuffing_length_ &= 0x07;
    //srs_warn("PS: New pack header clock=%" PRId64 ", rate=%d", system_clock_reference_base_, program_mux_rate_);

    if (!stream->require(pack_stuffing_length_)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires %d only %d bytes", pack_stuffing_length_, stream->left());
    }
    stream->skip(pack_stuffing_length_);

    return err;
}

srs_error_t SrsPsPacket::decode_system(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    system_header_start_code_ = stream->read_4bytes();
    srs_assert(system_header_start_code_ == 0x000001bb);

    if (!stream->require(8)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires 8 only %d bytes", stream->left());
    }

    header_length_ = stream->read_2bytes();
    if (!stream->require(header_length_)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires %d only %d bytes", header_length_, stream->left());
    }

    SrsBuffer b(stream->head(), header_length_);
    stream->skip(header_length_);

    rate_bound_ = b.read_3bytes();
    rate_bound_ = (rate_bound_ >> 1) & 0x3fffff;

    CSPS_flag_ = b.read_1bytes();
    audio_bound_ = (CSPS_flag_ >> 2) & 0x3f;
    CSPS_flag_ &= 0x01;

    video_bound_ = b.read_1bytes();
    system_audio_lock_flag_ = (video_bound_ >> 7) & 0x01;
    system_video_lock_flag_ = (video_bound_ >> 6) & 0x01;
    video_bound_ &= 0x1f;

    packet_rate_restriction_flag_ = b.read_1bytes();
    packet_rate_restriction_flag_ = (packet_rate_restriction_flag_ >> 5) & 0x01;
    //srs_warn("PS: New system header rate_bound=%d, video_bound=%d, audio_bound=%d", rate_bound_, video_bound_, audio_bound_);

    // Parse stream_id and buffer information.
    while (!b.empty()) {
        uint8_t r2 = (uint8_t) b.head()[0];
        if ((r2 & 0x80) != 0x80) break;

        if (!b.require(3)) {
            return srs_error_new(ERROR_GB_PS_HEADER, "requires 3 only %d bytes", b.left());
        }

        SrsTsPESStreamId stream_id = (SrsTsPESStreamId)(uint8_t)b.read_1bytes();
        uint16_t buffer_size_bound = b.read_2bytes();
        uint8_t buffer_bound_scale = (uint8_t)((buffer_size_bound>>13) & 0x01);
        buffer_size_bound &= 0x1fff;

        if (((stream_id>>4) & 0x0f) == SrsTsPESStreamIdVideoChecker) {
            video_stream_id_ = stream_id;
            video_buffer_bound_scale_ = buffer_bound_scale;
            video_buffer_size_bound_ = buffer_size_bound;
        } else if (((stream_id>>5) & 0x07) == SrsTsPESStreamIdAudioChecker) {
            audio_stream_id_ = stream_id;
            audio_buffer_bound_scale_ = buffer_bound_scale;
            audio_buffer_size_bound_ = buffer_size_bound;
        } else {
            srs_info("PS: Ignore stream_id=%#x, buffer_bound_scale=%d, buffer_size_bound=%d", stream_id, buffer_bound_scale, buffer_size_bound);
        }
    }

    return err;
}

SrsPsPsmPacket::SrsPsPsmPacket()
{
    current_next_indicator_ = 0;
    program_stream_map_version_ = 0;
    program_stream_info_length_ = 0;
    elementary_stream_map_length_ = 0;
    video_stream_type_ = 0;
    video_elementary_stream_id_ = 0;
    video_elementary_stream_info_length_ = 0;
    audio_stream_type_ = 0;
    audio_elementary_stream_id_ = 0;
    audio_elementary_stream_info_length_ = 0;
    CRC_32_ = 0;
}

SrsPsPsmPacket::~SrsPsPsmPacket()
{
}

srs_error_t SrsPsPsmPacket::decode(SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    // From first util program_stream_info_length field, at least 4 bytes.
    if (!stream->require(4)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", stream->left());
    }

    uint8_t r0 = stream->read_1bytes();
    program_stream_map_version_ = r0&0x1f;
    current_next_indicator_ = (r0>>7) & 0x01;
    if (!current_next_indicator_) {
        return srs_error_new(ERROR_GB_PS_HEADER, "invalid indicator of 0x%#x", r0);
    }

    uint8_t r1 = stream->read_1bytes();
    if ((r1&0x01) != 0x01) {
        return srs_error_new(ERROR_GB_PS_HEADER, "invalid marker of 0x%#x", r1);
    }

    program_stream_info_length_ = stream->read_2bytes();
    if (!stream->require(program_stream_info_length_)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires %d only %d bytes", program_stream_info_length_, stream->left());
    }
    stream->skip(program_stream_info_length_);

    // The number of ES map count, 2 bytes.
    if (!stream->require(2)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires 2 only %d bytes", stream->left());
    }
    elementary_stream_map_length_ = stream->read_2bytes();
    if (!stream->require(elementary_stream_map_length_)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires %d only %d bytes", elementary_stream_map_length_, stream->left());
    }

    SrsBuffer b(stream->head(), elementary_stream_map_length_);
    stream->skip(elementary_stream_map_length_);

    while (!b.empty()) {
        if (!b.require(4)) {
            return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", b.left());
        }

        SrsTsStream stream_type = (SrsTsStream)(uint8_t)b.read_1bytes();
        uint8_t elementary_stream_id = b.read_1bytes();
        uint16_t elementary_stream_info_length = b.read_2bytes();
        if (!b.require(elementary_stream_info_length)) {
            return srs_error_new(ERROR_GB_PS_HEADER, "requires %d only %d bytes", elementary_stream_info_length, b.left());
        }
        // Descriptor defined as bellow section, but we ignore it:
        // Table 2-40 – Video stream descriptor, hls-mpeg-ts-iso13818-1.pdf, page 82
        // Table 2-42 – Audio stream descriptor, hls-mpeg-ts-iso13818-1.pdf, page 83
        b.skip(elementary_stream_info_length);
        srs_info("PS: Ignore %d bytes descriptor for stream=%#x", elementary_stream_info_length, stream_type);

        if (stream_type == SrsTsStreamVideoH264 || stream_type == SrsTsStreamVideoHEVC) {
            video_stream_type_ = stream_type;
            video_elementary_stream_id_ = elementary_stream_id;
            video_elementary_stream_info_length_ = elementary_stream_info_length;
        } else if (stream_type == SrsTsStreamAudioAAC) {
            audio_stream_type_ = stream_type;
            audio_elementary_stream_id_ = elementary_stream_id;
            audio_elementary_stream_info_length_ = elementary_stream_info_length;
        } else {
            srs_trace("PS: Ignore stream_type=%#x, es_id=%d, es_info=%d", stream_type, elementary_stream_id, elementary_stream_info_length);
        }
    }

    // The last CRC32.
    if (!stream->require(4)) {
        return srs_error_new(ERROR_GB_PS_HEADER, "requires 4 only %d bytes", stream->left());
    }
    CRC_32_ = stream->read_4bytes();

    return err;
}

