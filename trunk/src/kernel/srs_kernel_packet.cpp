//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_kernel_packet.hpp>

#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

using namespace std;

SrsNaluSample::SrsNaluSample()
{
    size_ = 0;
    bytes_ = NULL;
}

SrsNaluSample::SrsNaluSample(char *b, int s)
{
    size_ = s;
    bytes_ = b;
}

SrsNaluSample::~SrsNaluSample()
{
}

SrsNaluSample *SrsNaluSample::copy()
{
    SrsNaluSample *p = new SrsNaluSample();
    p->bytes_ = bytes_;
    p->size_ = size_;
    return p;
}

SrsMediaPacket::SrsMediaPacket()
{
    timestamp_ = 0;
    stream_id_ = 0;
    message_type_ = SrsFrameTypeForbidden;
    payload_ = SrsSharedPtr<SrsMemoryBlock>(NULL);

    ++_srs_pps_objs_msgs->sugar_;
}

SrsMediaPacket::~SrsMediaPacket()
{
    // payload_ automatically cleaned up by SrsSharedPtr
}

void SrsMediaPacket::wrap(char *payload, int size)
{
    // Create new memory block and wrap the payload
    payload_ = SrsSharedPtr<SrsMemoryBlock>(new SrsMemoryBlock());
    payload_->attach(payload, size);
}

bool SrsMediaPacket::check(int stream_id)
{
    // Ignore error when message has no payload.
    if (!payload_.get()) {
        return true;
    }

    // we assume that the stream_id in a group must be the same.
    if (this->stream_id_ == stream_id) {
        return true;
    }
    this->stream_id_ = stream_id;

    return false;
}

bool SrsMediaPacket::is_av()
{
    return message_type_ == SrsFrameTypeAudio || message_type_ == SrsFrameTypeVideo;
}

bool SrsMediaPacket::is_audio()
{
    return message_type_ == SrsFrameTypeAudio;
}

bool SrsMediaPacket::is_video()
{
    return message_type_ == SrsFrameTypeVideo;
}

SrsMediaPacket *SrsMediaPacket::copy()
{
    SrsMediaPacket *copy = new SrsMediaPacket();

    copy->timestamp_ = timestamp_;
    copy->stream_id_ = stream_id_;
    copy->message_type_ = message_type_;
    copy->payload_ = payload_;

    return copy;
}

SrsParsedPacket::SrsParsedPacket()
{
    codec_ = NULL;
    nb_samples_ = 0;
    dts_ = 0;
    cts_ = 0;
}

SrsParsedPacket::~SrsParsedPacket()
{
}

srs_error_t SrsParsedPacket::initialize(SrsCodecConfig *c)
{
    codec_ = c;
    nb_samples_ = 0;
    dts_ = 0;
    cts_ = 0;
    return srs_success;
}

srs_error_t SrsParsedPacket::add_sample(char *bytes, int size)
{
    srs_error_t err = srs_success;

    // Ignore empty sample.
    if (!bytes || size <= 0)
        return err;

    if (nb_samples_ >= SrsMaxNbSamples) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "Frame samples overflow");
    }

    SrsNaluSample *sample = &samples_[nb_samples_++];
    sample->bytes_ = bytes;
    sample->size_ = size;

    return err;
}

SrsParsedPacket *SrsParsedPacket::copy()
{
    SrsParsedPacket *p = new SrsParsedPacket();
    do_copy(p);
    return p;
}

void SrsParsedPacket::do_copy(SrsParsedPacket *p)
{
    p->codec_ = codec_;
    p->nb_samples_ = nb_samples_;
    for (int i = 0; i < nb_samples_; i++) {
        p->samples_[i].size_ = samples_[i].size_;
        p->samples_[i].bytes_ = samples_[i].bytes_;
    }
    p->dts_ = dts_;
    p->cts_ = cts_;
}

SrsParsedAudioPacket::SrsParsedAudioPacket()
{
    aac_packet_type_ = SrsAudioAacFrameTraitForbidden;
}

SrsParsedAudioPacket::~SrsParsedAudioPacket()
{
}

SrsAudioCodecConfig *SrsParsedAudioPacket::acodec()
{
    return (SrsAudioCodecConfig *)codec_;
}

SrsParsedAudioPacket *SrsParsedAudioPacket::copy()
{
    SrsParsedAudioPacket *p = new SrsParsedAudioPacket();

    do_copy(p);
    p->aac_packet_type_ = aac_packet_type_;

    return p;
}

SrsParsedVideoPacket::SrsParsedVideoPacket()
{
    frame_type_ = SrsVideoAvcFrameTypeForbidden;
    avc_packet_type_ = SrsVideoAvcFrameTraitForbidden;
    has_idr_ = has_aud_ = has_sps_pps_ = false;
    first_nalu_type_ = SrsAvcNaluTypeForbidden;
}

SrsParsedVideoPacket::~SrsParsedVideoPacket()
{
}

srs_error_t SrsParsedVideoPacket::initialize(SrsCodecConfig *c)
{
    first_nalu_type_ = SrsAvcNaluTypeForbidden;
    has_idr_ = has_sps_pps_ = has_aud_ = false;
    return SrsParsedPacket::initialize(c);
}

srs_error_t SrsParsedVideoPacket::add_sample(char *bytes, int size)
{
    srs_error_t err = srs_success;

    if ((err = SrsParsedPacket::add_sample(bytes, size)) != srs_success) {
        return srs_error_wrap(err, "add frame");
    }

    SrsVideoCodecConfig *c = vcodec();
    if (!bytes || size <= 0)
        return err;

    // For HEVC(H.265), try to parse the IDR from NALUs.
    if (c && c->id_ == SrsVideoCodecIdHEVC) {
        SrsHevcNaluType nalu_type = SrsHevcNaluTypeParse(bytes[0]);
        has_idr_ = SrsIsIRAP(nalu_type);
        return err;
    }

    // By default, use AVC(H.264) to parse NALU.
    // For video, parse the nalu type, set the IDR flag.
    SrsAvcNaluType nal_unit_type = SrsAvcNaluTypeParse(bytes[0]);

    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        has_idr_ = true;
    } else if (nal_unit_type == SrsAvcNaluTypeSPS || nal_unit_type == SrsAvcNaluTypePPS) {
        has_sps_pps_ = true;
    } else if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
        has_aud_ = true;
    }

    if (first_nalu_type_ == SrsAvcNaluTypeReserved) {
        first_nalu_type_ = nal_unit_type;
    }

    return err;
}

SrsParsedVideoPacket *SrsParsedVideoPacket::copy()
{
    SrsParsedVideoPacket *p = new SrsParsedVideoPacket();

    do_copy(p);
    p->frame_type_ = frame_type_;
    p->avc_packet_type_ = avc_packet_type_;
    p->has_idr_ = has_idr_;
    p->has_aud_ = has_aud_;
    p->has_sps_pps_ = has_sps_pps_;
    p->first_nalu_type_ = first_nalu_type_;

    return p;
}

SrsVideoCodecConfig *SrsParsedVideoPacket::vcodec()
{
    return (SrsVideoCodecConfig *)codec_;
}

srs_error_t SrsParsedVideoPacket::parse_avc_nalu_type(const SrsNaluSample *sample, SrsAvcNaluType &avc_nalu_type)
{
    srs_error_t err = srs_success;

    if (sample == NULL || sample->size_ < 1) {
        return srs_error_new(ERROR_NALU_EMPTY, "empty nalu");
    }

    uint8_t header = sample->bytes_[0];
    avc_nalu_type = SrsAvcNaluTypeParse(header);

    return err;
}

srs_error_t SrsParsedVideoPacket::parse_avc_bframe(const SrsNaluSample *sample, bool &is_b_frame)
{
    srs_error_t err = srs_success;

    SrsAvcNaluType nalu_type;
    if ((err = parse_avc_nalu_type(sample, nalu_type)) != srs_success) {
        return srs_error_wrap(err, "parse avc nalu type error");
    }

    if (nalu_type != SrsAvcNaluTypeNonIDR && nalu_type != SrsAvcNaluTypeDataPartitionA && nalu_type != SrsAvcNaluTypeDataPartitionB && nalu_type != SrsAvcNaluTypeDataPartitionC) {
        is_b_frame = false;
        return err;
    }

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(sample->bytes_, sample->size_));

    // Skip nalu header.
    stream->skip(1);

    SrsBitBuffer bitstream(stream.get());
    int32_t first_mb_in_slice = 0;
    if ((err = srs_avc_nalu_read_uev(&bitstream, first_mb_in_slice)) != srs_success) {
        return srs_error_wrap(err, "nalu read uev");
    }

    int32_t slice_type_v = 0;
    if ((err = srs_avc_nalu_read_uev(&bitstream, slice_type_v)) != srs_success) {
        return srs_error_wrap(err, "nalu read uev");
    }
    SrsAvcSliceType slice_type = (SrsAvcSliceType)slice_type_v;

    is_b_frame = slice_type == SrsAvcSliceTypeB || slice_type == SrsAvcSliceTypeB1;
    if (is_b_frame) {
        srs_verbose("nalu_type=%d, slice type=%d", nalu_type, slice_type);
    }

    return err;
}

srs_error_t SrsParsedVideoPacket::parse_hevc_nalu_type(const SrsNaluSample *sample, SrsHevcNaluType &hevc_nalu_type)
{
    srs_error_t err = srs_success;

    if (sample == NULL || sample->size_ < 1) {
        return srs_error_new(ERROR_NALU_EMPTY, "empty hevc nalu");
    }

    uint8_t header = sample->bytes_[0];
    hevc_nalu_type = SrsHevcNaluTypeParse(header);

    return err;
}

srs_error_t SrsParsedVideoPacket::parse_hevc_bframe(const SrsNaluSample *sample, SrsFormat *format, bool &is_b_frame)
{
    srs_error_t err = srs_success;

    SrsHevcNaluType nalu_type;
    if ((err = parse_hevc_nalu_type(sample, nalu_type)) != srs_success) {
        return srs_error_wrap(err, "parse hevc nalu type error");
    }

    if (nalu_type > SrsHevcNaluType_CODED_SLICE_TFD) {
        is_b_frame = false;
        return err;
    }

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(sample->bytes_, sample->size_));
    stream->skip(2);

    // @see 7.3.6.1 General slice segment header syntax
    // @doc ITU-T-H.265-2021.pdf, page 66.
    SrsBitBuffer bs(stream.get());

    uint8_t first_slice_segment_in_pic_flag = bs.read_bit();

    uint32_t slice_pic_parameter_set_id;
    if ((err = bs.read_bits_ue(slice_pic_parameter_set_id)) != srs_success) {
        return srs_error_wrap(err, "read slice pic parameter set id");
    }

    if (slice_pic_parameter_set_id >= SrsHevcMax_PPS_COUNT) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "slice pic parameter set id out of range: %d", slice_pic_parameter_set_id);
    }

    SrsHevcRbspPps *pps = &(format->vcodec_->hevc_dec_conf_record_.pps_table_[slice_pic_parameter_set_id]);
    if (!pps) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps not found");
    }

    uint8_t dependent_slice_segment_flag = 0;
    if (!first_slice_segment_in_pic_flag) {
        if (pps->dependent_slice_segments_enabled_flag_) {
            dependent_slice_segment_flag = bs.read_bit();
        }
    }

    if (dependent_slice_segment_flag) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "dependent slice segment flag is not supported");
    }

    for (int i = 0; i < pps->num_extra_slice_header_bits_; i++) {
        bs.skip_bits(1);
    }

    uint32_t slice_type;
    if ((err = bs.read_bits_ue(slice_type)) != srs_success) {
        return srs_error_wrap(err, "read slice type");
    }

    is_b_frame = slice_type == SrsHevcSliceTypeB;
    if (is_b_frame) {
        srs_verbose("nalu_type=%d, slice type=%d", nalu_type, slice_type);
    }

    // no need to evaluate the rest

    return err;
}

ISrsFormat::ISrsFormat()
{
}

ISrsFormat::~ISrsFormat()
{
}

SrsFormat::SrsFormat()
{
    acodec_ = NULL;
    vcodec_ = NULL;
    audio_ = NULL;
    video_ = NULL;
    avc_parse_sps_ = true;
    try_annexb_first_ = true;
    raw_ = NULL;
    nb_raw_ = 0;
}

SrsFormat::~SrsFormat()
{
    srs_freep(audio_);
    srs_freep(video_);
    srs_freep(acodec_);
    srs_freep(vcodec_);
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsFormat::initialize()
{
    if (!vcodec_) {
        vcodec_ = new SrsVideoCodecConfig();
    }

    return srs_success;
}

srs_error_t SrsFormat::on_audio(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if (!data || size <= 0) {
        srs_info("no audio present, ignore it.");
        return err;
    }

    SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(data, size));

    // We already checked the size is positive and data is not NULL.
    srs_assert(buffer->require(1));

    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    uint8_t v = buffer->read_1bytes();
    SrsAudioCodecId codec = (SrsAudioCodecId)((v >> 4) & 0x0f);

    if (codec != SrsAudioCodecIdMP3 && codec != SrsAudioCodecIdAAC && codec != SrsAudioCodecIdOpus) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "unsupported audio codec=%d(%s)", codec, srs_audio_codec_id2str(codec).c_str());
    }

    bool fresh = !acodec_;
    if (!acodec_) {
        acodec_ = new SrsAudioCodecConfig();
    }
    if (!audio_) {
        audio_ = new SrsParsedAudioPacket();
    }

    if ((err = audio_->initialize(acodec_)) != srs_success) {
        return srs_error_wrap(err, "init audio");
    }

    // Parse by specified codec.
    buffer->skip(-1 * buffer->pos());

    if (codec == SrsAudioCodecIdMP3) {
        return audio_mp3_demux(buffer.get(), timestamp, fresh);
    } else if (codec == SrsAudioCodecIdAAC) {
        return audio_aac_demux(buffer.get(), timestamp);
    } else {
        return srs_error_new(ERROR_NOT_IMPLEMENTED, "opus demuxer not implemented");
    }
}

srs_error_t SrsFormat::on_video(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if (!data || size <= 0) {
        srs_trace("no video present, ignore it.");
        return err;
    }

    SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(data, size));
    return video_avc_demux(buffer.get(), timestamp);
}

srs_error_t SrsFormat::on_aac_sequence_header(char *data, int size)
{
    srs_error_t err = srs_success;

    if (!acodec_) {
        acodec_ = new SrsAudioCodecConfig();
    }
    if (!audio_) {
        audio_ = new SrsParsedAudioPacket();
    }

    if ((err = audio_->initialize(acodec_)) != srs_success) {
        return srs_error_wrap(err, "init audio");
    }

    return audio_aac_sequence_header_demux(data, size);
}

bool SrsFormat::is_aac_sequence_header()
{
    return acodec_ && acodec_->id_ == SrsAudioCodecIdAAC && audio_ && audio_->aac_packet_type_ == SrsAudioAacFrameTraitSequenceHeader;
}

bool SrsFormat::is_mp3_sequence_header()
{
    return acodec_ && acodec_->id_ == SrsAudioCodecIdMP3 && audio_ && audio_->aac_packet_type_ == SrsAudioMp3FrameTraitSequenceHeader;
}

bool SrsFormat::is_avc_sequence_header()
{
    bool h264 = (vcodec_ && vcodec_->id_ == SrsVideoCodecIdAVC);
    bool h265 = (vcodec_ && vcodec_->id_ == SrsVideoCodecIdHEVC);
    bool av1 = (vcodec_ && vcodec_->id_ == SrsVideoCodecIdAV1);
    return vcodec_ && (h264 || h265 || av1) && video_ && video_->avc_packet_type_ == SrsVideoAvcFrameTraitSequenceHeader;
}

SrsParsedAudioPacket *SrsFormat::audio()
{
    return audio_;
}

SrsAudioCodecConfig *SrsFormat::acodec()
{
    return acodec_;
}

SrsParsedVideoPacket *SrsFormat::video()
{
    return video_;
}

SrsVideoCodecConfig *SrsFormat::vcodec()
{
    return vcodec_;
}

// Remove the emulation bytes from stream, and return num of bytes of the rbsp.
int srs_rbsp_remove_emulation_bytes(SrsBuffer *stream, std::vector<uint8_t> &rbsp)
{
    int nb_rbsp = 0;
    while (!stream->empty()) {
        rbsp[nb_rbsp] = stream->read_1bytes();

        // .. 00 00 03 xx, the 03 byte should be drop where xx represents any
        // 2 bit pattern: 00, 01, 10, or 11.
        if (nb_rbsp >= 2 && rbsp[nb_rbsp - 2] == 0 && rbsp[nb_rbsp - 1] == 0 && rbsp[nb_rbsp] == 3) {
            // read 1byte more.
            if (stream->empty()) {
                nb_rbsp++;
                break;
            }

            // |---------------------|----------------------------|
            // |      rbsp           |  nalu with emulation bytes |
            // |---------------------|----------------------------|
            // | 0x00 0x00 0x00      |     0x00 0x00 0x03 0x00    |
            // | 0x00 0x00 0x01      |     0x00 0x00 0x03 0x01    |
            // | 0x00 0x00 0x02      |     0x00 0x00 0x03 0x02    |
            // | 0x00 0x00 0x03      |     0x00 0x00 0x03 0x03    |
            // | 0x00 0x00 0x03 0x04 |     0x00 0x00 0x03 0x04    |
            // |---------------------|----------------------------|
            uint8_t ev = stream->read_1bytes();
            if (ev > 3) {
                nb_rbsp++;
            }
            rbsp[nb_rbsp] = ev;
        }

        nb_rbsp++;
    }

    return nb_rbsp;
}

srs_error_t SrsFormat::video_avc_demux(SrsBuffer *stream, int64_t timestamp)
{
    srs_error_t err = srs_success;

    if (!stream->require(1)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "video avc demux shall atleast 1bytes");
    }

    // Parse the frame type and the first bit indicates the ext header.
    uint8_t frame_type = stream->read_1bytes();
    bool is_ext_header = frame_type & 0x80;

    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    SrsVideoCodecId codec_id = SrsVideoCodecIdForbidden;
    SrsVideoAvcFrameTrait packet_type = SrsVideoAvcFrameTraitForbidden;
    if (!is_ext_header) {
        // See rtmp_specification_1.0.pdf
        codec_id = (SrsVideoCodecId)(frame_type & 0x0f);
        frame_type = (frame_type >> 4) & 0x0f;
    } else {
        // See https://github.com/veovera/enhanced-rtmp
        packet_type = (SrsVideoAvcFrameTrait)(frame_type & 0x0f);
        frame_type = (frame_type >> 4) & 0x07;

        if (!stream->require(4)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "fourCC requires 4bytes, only %dbytes", stream->left());
        }

        uint32_t four_cc = stream->read_4bytes();
        if (four_cc == 0x68766331) { // 'hvc1'=0x68766331
            codec_id = SrsVideoCodecIdHEVC;
        }
    }

    if (!vcodec_) {
        vcodec_ = new SrsVideoCodecConfig();
    }

    if (!video_) {
        video_ = new SrsParsedVideoPacket();
    }

    if ((err = video_->initialize(vcodec_)) != srs_success) {
        return srs_error_wrap(err, "init video");
    }

    video_->frame_type_ = (SrsVideoAvcFrameType)frame_type;

    // ignore info frame without error,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    if (video_->frame_type_ == SrsVideoAvcFrameTypeVideoInfoFrame) {
        srs_warn("avc ignore the info frame");
        return err;
    }

    // Check codec for H.264 and H.265.
    bool codec_ok = (codec_id == SrsVideoCodecIdAVC);
    codec_ok = codec_ok ? true : (codec_id == SrsVideoCodecIdHEVC);
    if (!codec_ok) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "only support video H.264/H.265, actual=%d", codec_id);
    }
    vcodec_->id_ = codec_id;

    int32_t composition_time = 0;
    if (!is_ext_header) {
        // See rtmp_specification_1.0.pdf
        if (!stream->require(4)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "requires 4bytes, only %dbytes", stream->left());
        }
        packet_type = (SrsVideoAvcFrameTrait)stream->read_1bytes();
        composition_time = stream->read_3bytes();
    } else {
        // See https://github.com/veovera/enhanced-rtmp
        if (packet_type == SrsVideoHEVCFrameTraitPacketTypeCodedFrames) {
            if (!stream->require(3)) {
                return srs_error_new(ERROR_HLS_DECODE_ERROR, "requires 3 bytes, only %dbytes", stream->left());
            }
            composition_time = stream->read_3bytes();
        }
    }

    // pts = dts + cts.
    video_->dts_ = timestamp;
    video_->cts_ = composition_time;
    video_->avc_packet_type_ = packet_type;

    // Update the RAW AVC data.
    raw_ = stream->data() + stream->pos();
    nb_raw_ = stream->size() - stream->pos();

    // Parse sequence header for H.265/HEVC.
    if (codec_id == SrsVideoCodecIdHEVC) {
        if (packet_type == SrsVideoAvcFrameTraitSequenceHeader) {
            // TODO: demux vps/sps/pps for hevc
            if ((err = hevc_demux_hvcc(stream)) != srs_success) {
                return srs_error_wrap(err, "demux hevc VPS/SPS/PPS");
            }
        } else if (packet_type == SrsVideoAvcFrameTraitNALU || packet_type == SrsVideoHEVCFrameTraitPacketTypeCodedFramesX) {
            // TODO: demux nalu for hevc
            if ((err = video_nalu_demux(stream)) != srs_success) {
                return srs_error_wrap(err, "demux hevc NALU");
            }
        }
        return err;
    }

    // Parse sequence header for H.264/AVC.
    if (packet_type == SrsVideoAvcFrameTraitSequenceHeader) {
        // TODO: FIXME: Maybe we should ignore any error for parsing sps/pps.
        if ((err = avc_demux_sps_pps(stream)) != srs_success) {
            return srs_error_wrap(err, "demux SPS/PPS");
        }
    } else if (packet_type == SrsVideoAvcFrameTraitNALU) {
        if ((err = video_nalu_demux(stream)) != srs_success) {
            return srs_error_wrap(err, "demux NALU");
        }
    } else {
        // ignored.
    }

    return err;
}

// For media server, we don't care the codec, so we just try to parse sps-pps, and we could ignore any error if fail.
// LCOV_EXCL_START

// struct ptl
SrsHevcProfileTierLevel::SrsHevcProfileTierLevel()
{
    general_profile_space_ = 0;
    general_tier_flag_ = 0;
    general_profile_idc_ = 0;
    memset(general_profile_compatibility_flag_, 0, 32);
    general_progressive_source_flag_ = 0;
    general_interlaced_source_flag_ = 0;
    general_non_packed_constraint_flag_ = 0;
    general_frame_only_constraint_flag_ = 0;
    general_max_12bit_constraint_flag_ = 0;
    general_max_10bit_constraint_flag_ = 0;
    general_max_8bit_constraint_flag_ = 0;
    general_max_422chroma_constraint_flag_ = 0;
    general_max_420chroma_constraint_flag_ = 0;
    general_max_monochrome_constraint_flag_ = 0;
    general_intra_constraint_flag_ = 0;
    general_one_picture_only_constraint_flag_ = 0;
    general_lower_bit_rate_constraint_flag_ = 0;
    general_max_14bit_constraint_flag_ = 0;
    general_reserved_zero_7bits_ = 0;
    general_reserved_zero_33bits_ = 0;
    general_reserved_zero_34bits_ = 0;
    general_reserved_zero_35bits_ = 0;
    general_reserved_zero_43bits_ = 0;
    general_inbld_flag_ = 0;
    general_reserved_zero_bit_ = 0;
    general_level_idc_ = 0;
    memset(reserved_zero_2bits_, 0, 8);
}

SrsHevcProfileTierLevel::~SrsHevcProfileTierLevel()
{
}

// Parse the hevc vps/sps/pps
srs_error_t SrsFormat::hevc_demux_hvcc(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    int avc_extra_size = stream->size() - stream->pos();
    if (avc_extra_size > 0) {
        char *copy_stream_from = stream->data() + stream->pos();
        vcodec_->avc_extra_data_ = std::vector<char>(copy_stream_from, copy_stream_from + avc_extra_size);
    }

    const int HEVC_MIN_SIZE = 23; // From configuration_version to numOfArrays
    if (!stream->require(HEVC_MIN_SIZE)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "requires %d only %d bytes", HEVC_MIN_SIZE, stream->left());
    }

    SrsHevcDecoderConfigurationRecord *dec_conf_rec_p = &(vcodec_->hevc_dec_conf_record_);
    dec_conf_rec_p->configuration_version_ = stream->read_1bytes();
    if (dec_conf_rec_p->configuration_version_ != 1) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "invalid version=%d", dec_conf_rec_p->configuration_version_);
    }

    // Read general_profile_space(2bits), general_tier_flag(1bit), general_profile_idc(5bits)
    uint8_t data_byte = stream->read_1bytes();
    dec_conf_rec_p->general_profile_space_ = (data_byte >> 6) & 0x03;
    dec_conf_rec_p->general_tier_flag_ = (data_byte >> 5) & 0x01;
    dec_conf_rec_p->general_profile_idc_ = data_byte & 0x1F;
    srs_info("hevc version:%d, general_profile_space:%d, general_tier_flag:%d, general_profile_idc:%d",
             dec_conf_rec_p->configuration_version_, dec_conf_rec_p->general_profile_space_, dec_conf_rec_p->general_tier_flag_,
             dec_conf_rec_p->general_profile_idc_);

    // general_profile_compatibility_flags: 32bits
    dec_conf_rec_p->general_profile_compatibility_flags_ = (uint32_t)stream->read_4bytes();

    // general_constraint_indicator_flags: 48bits
    uint64_t data_64bit = (uint64_t)stream->read_4bytes();
    data_64bit = (data_64bit << 16) | (stream->read_2bytes());
    dec_conf_rec_p->general_constraint_indicator_flags_ = data_64bit;

    // general_level_idc: 8bits
    dec_conf_rec_p->general_level_idc_ = stream->read_1bytes();
    // min_spatial_segmentation_idc: xxxx 14bits
    dec_conf_rec_p->min_spatial_segmentation_idc_ = stream->read_2bytes() & 0x0fff;
    // parallelism_type: xxxx xx 2bits
    dec_conf_rec_p->parallelism_type_ = stream->read_1bytes() & 0x03;
    // chroma_format: xxxx xx 2bits
    dec_conf_rec_p->chroma_format_ = stream->read_1bytes() & 0x03;
    // bit_depth_luma_minus8: xxxx x 3bits
    dec_conf_rec_p->bit_depth_luma_minus8_ = stream->read_1bytes() & 0x07;
    // bit_depth_chroma_minus8: xxxx x 3bits
    dec_conf_rec_p->bit_depth_chroma_minus8_ = stream->read_1bytes() & 0x07;
    srs_info("general_constraint_indicator_flags:0x%x, general_level_idc:%d, min_spatial_segmentation_idc:%d, parallelism_type:%d, chroma_format:%d, bit_depth_luma_minus8:%d, bit_depth_chroma_minus8:%d",
             dec_conf_rec_p->general_constraint_indicator_flags_, dec_conf_rec_p->general_level_idc_,
             dec_conf_rec_p->min_spatial_segmentation_idc_, dec_conf_rec_p->parallelism_type_, dec_conf_rec_p->chroma_format_,
             dec_conf_rec_p->bit_depth_luma_minus8_, dec_conf_rec_p->bit_depth_chroma_minus8_);

    // avg_frame_rate: 16bits
    vcodec_->frame_rate_ = dec_conf_rec_p->avg_frame_rate_ = stream->read_2bytes();
    // 8bits: constant_frame_rate(2bits), num_temporal_layers(3bits),
    //        temporal_id_nested(1bit), length_size_minus_one(2bits)
    data_byte = stream->read_1bytes();
    dec_conf_rec_p->constant_frame_rate_ = (data_byte >> 6) & 0x03;
    dec_conf_rec_p->num_temporal_layers_ = (data_byte >> 3) & 0x07;
    dec_conf_rec_p->temporal_id_nested_ = (data_byte >> 2) & 0x01;

    // Parse the NALU size.
    dec_conf_rec_p->length_size_minus_one_ = data_byte & 0x03;
    vcodec_->NAL_unit_length_ = dec_conf_rec_p->length_size_minus_one_;

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    if (vcodec_->NAL_unit_length_ == 2) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "sps lengthSizeMinusOne should never be 2");
    }

    uint8_t numOfArrays = stream->read_1bytes();
    srs_info("avg_frame_rate:%d, constant_frame_rate:%d, num_temporal_layers:%d, temporal_id_nested:%d, length_size_minus_one:%d, numOfArrays:%d",
             dec_conf_rec_p->avg_frame_rate, dec_conf_rec_p->constant_frame_rate, dec_conf_rec_p->num_temporal_layers,
             dec_conf_rec_p->temporal_id_nested, dec_conf_rec_p->length_size_minus_one, numOfArrays);

    // parse vps/pps/sps
    dec_conf_rec_p->nalu_vec_.clear();
    for (int index = 0; index < numOfArrays; index++) {
        if (!stream->require(3)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "requires 3 only %d bytes", stream->left());
        }
        data_byte = stream->read_1bytes();

        SrsHevcHvccNalu hevc_unit;
        hevc_unit.array_completeness_ = (data_byte >> 7) & 0x01;
        hevc_unit.nal_unit_type_ = data_byte & 0x3f;
        hevc_unit.num_nalus_ = stream->read_2bytes();

        for (int i = 0; i < hevc_unit.num_nalus_; i++) {
            if (!stream->require(2)) {
                return srs_error_new(ERROR_HEVC_DECODE_ERROR, "num_nalus requires 2 only %d bytes", stream->left());
            }

            SrsHevcNalData data_item;
            data_item.nal_unit_length_ = stream->read_2bytes();

            if (!stream->require(data_item.nal_unit_length_)) {
                return srs_error_new(ERROR_HEVC_DECODE_ERROR, "requires %d only %d bytes",
                                     data_item.nal_unit_length_, stream->left());
            }
            // copy vps/pps/sps data
            data_item.nal_unit_data_.resize(data_item.nal_unit_length_);

            stream->read_bytes((char *)(&data_item.nal_unit_data_[0]), data_item.nal_unit_length_);
            srs_info("hevc nalu type:%d, array_completeness:%d, num_nalus:%d, i:%d, nal_unit_length:%d",
                     hevc_unit.nal_unit_type_, hevc_unit.array_completeness_, hevc_unit.num_nalus_, i, data_item.nal_unit_length_);
            hevc_unit.nal_data_vec_.push_back(data_item);
        }
        dec_conf_rec_p->nalu_vec_.push_back(hevc_unit);

        // demux nalu
        if ((err = hevc_demux_vps_sps_pps(&hevc_unit)) != srs_success) {
            return srs_error_wrap(err, "hevc demux vps/sps/pps failed");
        }
    }

    return err;
}

srs_error_t SrsFormat::hevc_demux_vps_sps_pps(SrsHevcHvccNalu *nal)
{
    srs_error_t err = srs_success;

    if (nal->nal_data_vec_.empty()) {
        return err;
    }

    // TODO: FIXME: Support for multiple VPS/SPS/PPS, then pick the first non-empty one.
    char *frame = (char *)(&nal->nal_data_vec_[0].nal_unit_data_[0]);
    int nb_frame = nal->nal_data_vec_[0].nal_unit_length_;
    SrsBuffer stream(frame, nb_frame);

    // nal data
    switch (nal->nal_unit_type_) {
    case SrsHevcNaluType_VPS:
        err = hevc_demux_vps(&stream);
        break;
    case SrsHevcNaluType_SPS:
        err = hevc_demux_sps(&stream);
        break;
    case SrsHevcNaluType_PPS:
        err = hevc_demux_pps(&stream);
        break;
    default:
        break;
    }

    return err;
}

srs_error_t SrsFormat::hevc_demux_vps(SrsBuffer *stream)
{
    // for NALU, ITU-T H.265 7.3.2.1 Video parameter set RBSP syntax
    // @see 7.3.1.2 NAL unit header syntax
    // @doc ITU-T-H.265-2021.pdf, page 53.

    if (!stream->require(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "decode hevc vps requires 1 only %d bytes", stream->left());
    }
    int8_t nutv = stream->read_1bytes();

    // forbidden_zero_bit shall be equal to 0.
    int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "hevc forbidden_zero_bit=%d shall be equal to 0", forbidden_zero_bit);
    }

    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    // @see 7.4.2.2 NAL unit header semantics
    // @doc ITU-T-H.265-2021.pdf, page 86.
    SrsHevcNaluType nal_unit_type = (SrsHevcNaluType)((nutv >> 1) & 0x3f);
    if (nal_unit_type != SrsHevcNaluType_VPS) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "hevc vps nal_unit_type=%d shall be equal to 33", nal_unit_type);
    }

    // nuh_layer_id + nuh_temporal_id_plus1
    stream->skip(1);

    // decode the rbsp from vps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    std::vector<uint8_t> rbsp(stream->size());

    int nb_rbsp = srs_rbsp_remove_emulation_bytes(stream, rbsp);

    return hevc_demux_vps_rbsp((char *)&rbsp[0], nb_rbsp);
}

srs_error_t SrsFormat::hevc_demux_vps_rbsp(char *rbsp, int nb_rbsp)
{
    srs_error_t err = srs_success;

    // reparse the rbsp.
    SrsBuffer stream(rbsp, nb_rbsp);

    // H265 VPS (video_parameter_set_rbsp()) NAL Unit.
    // Section 7.3.2.1 ("Video parameter set RBSP syntax") of the H.265
    // ITU-T-H.265-2021.pdf, page 54.
    if (!stream.require(4)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "vps requires 4 only %d bytes", stream.left());
    }

    SrsBitBuffer bs(&stream);

    // vps_video_parameter_set_id  u(4)
    int vps_video_parameter_set_id = bs.read_bits(4);
    if (vps_video_parameter_set_id < 0 || vps_video_parameter_set_id > SrsHevcMax_VPS_COUNT) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "vps id out of range: %d", vps_video_parameter_set_id);
    }

    // select table
    SrsHevcDecoderConfigurationRecord *dec_conf_rec = &(vcodec_->hevc_dec_conf_record_);
    SrsHevcRbspVps *vps = &(dec_conf_rec->vps_table_[vps_video_parameter_set_id]);

    vps->vps_video_parameter_set_id_ = vps_video_parameter_set_id;
    // vps_base_layer_internal_flag  u(1)
    vps->vps_base_layer_internal_flag_ = bs.read_bit();
    // vps_base_layer_available_flag  u(1)
    vps->vps_base_layer_available_flag_ = bs.read_bit();
    // vps_max_layers_minus1  u(6)
    vps->vps_max_layers_minus1_ = bs.read_bits(6);
    // vps_max_sub_layers_minus1  u(3)
    vps->vps_max_sub_layers_minus1_ = bs.read_bits(3);
    // vps_temporal_id_nesting_flag  u(1)
    vps->vps_temporal_id_nesting_flag_ = bs.read_bit();
    // vps_reserved_0xffff_16bits  u(16)
    vps->vps_reserved_0xffff_16bits_ = bs.read_bits(16);

    // profile_tier_level(1, vps_max_sub_layers_minus1)
    if ((err = hevc_demux_rbsp_ptl(&bs, &vps->ptl_, 1, vps->vps_max_sub_layers_minus1_)) != srs_success) {
        return srs_error_wrap(err, "vps rbsp ptl vps_max_sub_layers_minus1=%d", vps->vps_max_sub_layers_minus1_);
    }

    dec_conf_rec->general_profile_idc_ = vps->ptl_.general_profile_idc_;
    dec_conf_rec->general_level_idc_ = vps->ptl_.general_level_idc_;
    dec_conf_rec->general_tier_flag_ = vps->ptl_.general_tier_flag_;

    if (!bs.require_bits(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "sublayer flag requires 1 only %d bits", bs.left_bits());
    }

    // vps_sub_layer_ordering_info_present_flag  u(1)
    vps->vps_sub_layer_ordering_info_present_flag_ = bs.read_bit();

    for (int i = (vps->vps_sub_layer_ordering_info_present_flag_ ? 0 : vps->vps_max_sub_layers_minus1_);
         i <= vps->vps_max_sub_layers_minus1_; i++) {
        // vps_max_dec_pic_buffering_minus1[i]  ue(v)
        if ((err = bs.read_bits_ue(vps->vps_max_dec_pic_buffering_minus1_[i])) != srs_success) {
            return srs_error_wrap(err, "max_dec_pic_buffering_minus1");
        }
        // vps_max_num_reorder_pics[i]  ue(v)
        if ((err = bs.read_bits_ue(vps->vps_max_num_reorder_pics_[i])) != srs_success) {
            return srs_error_wrap(err, "max_num_reorder_pics");
        }
        // vps_max_latency_increase_plus1[i]  ue(v)
        if ((err = bs.read_bits_ue(vps->vps_max_latency_increase_plus1_[i])) != srs_success) {
            return srs_error_wrap(err, "max_latency_increase_plus1");
        }
    }

    if (!bs.require_bits(6)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "vps maxlayer requires 10 only %d bits", bs.left_bits());
    }

    // vps_max_layer_id  u(6)
    vps->vps_max_layer_id_ = bs.read_bits(6);

    // vps_num_layer_sets_minus1  ue(v)
    if ((err = bs.read_bits_ue(vps->vps_num_layer_sets_minus1_)) != srs_success) {
        return srs_error_wrap(err, "num_layer_sets_minus1");
    }

    // TODO: FIXME: Implements it, you might parse remain bits for video_parameter_set_rbsp.
    // @see 7.3.2.1 Video parameter set RBSP
    // @doc ITU-T-H.265-2021.pdf, page 54.

    return err;
}

srs_error_t SrsFormat::hevc_demux_sps(SrsBuffer *stream)
{
    // for NALU, ITU-T H.265 7.3.2.2 Sequence parameter set RBSP syntax
    // @see 7.3.2.2.1 General sequence parameter set RBSP syntax
    // @doc ITU-T-H.265-2021.pdf, page 55.

    if (!stream->require(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "decode hevc sps requires 1 only %d bytes", stream->left());
    }
    int8_t nutv = stream->read_1bytes();

    // forbidden_zero_bit shall be equal to 0.
    int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "hevc forbidden_zero_bit=%d shall be equal to 0", forbidden_zero_bit);
    }

    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    // @see 7.4.2.2 NAL unit header semantics
    // @doc ITU-T-H.265-2021.pdf, page 86.
    SrsHevcNaluType nal_unit_type = (SrsHevcNaluType)((nutv >> 1) & 0x3f);
    if (nal_unit_type != SrsHevcNaluType_SPS) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "hevc sps nal_unit_type=%d shall be equal to 33", nal_unit_type);
    }

    // nuh_layer_id + nuh_temporal_id_plus1
    stream->skip(1);

    // decode the rbsp from sps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    std::vector<uint8_t> rbsp(stream->size());

    int nb_rbsp = srs_rbsp_remove_emulation_bytes(stream, rbsp);

    return hevc_demux_sps_rbsp((char *)&rbsp[0], nb_rbsp);
}

srs_error_t SrsFormat::hevc_demux_sps_rbsp(char *rbsp, int nb_rbsp)
{
    srs_error_t err = srs_success;

    // we donot parse the detail of sps.
    // @see https://github.com/ossrs/srs/issues/474
    if (!avc_parse_sps_) {
        return err;
    }

    // reparse the rbsp.
    SrsBuffer stream(rbsp, nb_rbsp);

    // H265 SPS Nal Unit (seq_parameter_set_rbsp()) parser.
    // Section 7.3.2.2 ("Sequence parameter set RBSP syntax") of the H.265
    // ITU-T-H.265-2021.pdf, page 55.
    if (!stream.require(2)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "sps requires 2 only %d bytes", stream.left());
    }
    uint8_t nutv = stream.read_1bytes();

    // sps_video_parameter_set_id  u(4)
    int sps_video_parameter_set_id = (nutv >> 4) & 0x0f;
    // sps_max_sub_layers_minus1  u(3)
    int sps_max_sub_layers_minus1 = (nutv >> 1) & 0x07;
    // sps_temporal_id_nesting_flag  u(1)
    int sps_temporal_id_nesting_flag = nutv & 0x01;

    SrsBitBuffer bs(&stream);

    // profile tier level...
    SrsHevcProfileTierLevel profile_tier_level;
    // profile_tier_level(1, sps_max_sub_layers_minus1)
    if ((err = hevc_demux_rbsp_ptl(&bs, &profile_tier_level, 1, sps_max_sub_layers_minus1)) != srs_success) {
        return srs_error_wrap(err, "sps rbsp ptl sps_max_sub_layers_minus1=%d", sps_max_sub_layers_minus1);
    }

    vcodec_->hevc_profile_ = (SrsHevcProfile)profile_tier_level.general_profile_idc_;
    vcodec_->hevc_level_ = (SrsHevcLevel)profile_tier_level.general_level_idc_;

    // sps_seq_parameter_set_id  ue(v)
    uint32_t sps_seq_parameter_set_id = 0;
    if ((err = bs.read_bits_ue(sps_seq_parameter_set_id)) != srs_success) {
        return srs_error_wrap(err, "sps_seq_parameter_set_id");
    }
    if (sps_seq_parameter_set_id < 0 || sps_seq_parameter_set_id >= SrsHevcMax_SPS_COUNT) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "sps id out of range: %d", sps_seq_parameter_set_id);
    }

    // for sps_table
    SrsHevcDecoderConfigurationRecord *dec_conf_rec = &(vcodec_->hevc_dec_conf_record_);
    SrsHevcRbspSps *sps = &(dec_conf_rec->sps_table_[sps_seq_parameter_set_id]);

    sps->sps_video_parameter_set_id_ = sps_video_parameter_set_id;
    sps->sps_max_sub_layers_minus1_ = sps_max_sub_layers_minus1;
    sps->sps_temporal_id_nesting_flag_ = sps_temporal_id_nesting_flag;
    sps->sps_seq_parameter_set_id_ = sps_seq_parameter_set_id;
    sps->ptl_ = profile_tier_level;

    // chroma_format_idc  ue(v)
    if ((err = bs.read_bits_ue(sps->chroma_format_idc_)) != srs_success) {
        return srs_error_wrap(err, "chroma_format_idc");
    }

    if (sps->chroma_format_idc_ == 3) {
        if (!bs.require_bits(1)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "separate_colour_plane_flag requires 1 only %d bits", bs.left_bits());
        }

        // separate_colour_plane_flag  u(1)
        sps->separate_colour_plane_flag_ = bs.read_bit();
    }

    // pic_width_in_luma_samples  ue(v)
    if ((err = bs.read_bits_ue(sps->pic_width_in_luma_samples_)) != srs_success) {
        return srs_error_wrap(err, "pic_width_in_luma_samples");
    }

    // pic_height_in_luma_samples  ue(v)
    if ((err = bs.read_bits_ue(sps->pic_height_in_luma_samples_)) != srs_success) {
        return srs_error_wrap(err, "pic_height_in_luma_samples");
    }

    vcodec_->width_ = sps->pic_width_in_luma_samples_;
    vcodec_->height_ = sps->pic_height_in_luma_samples_;

    if (!bs.require_bits(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "conformance_window_flag requires 1 only %d bits", bs.left_bits());
    }

    // conformance_window_flag  u(1)
    sps->conformance_window_flag_ = bs.read_bit();
    if (sps->conformance_window_flag_) {
        // conf_win_left_offset  ue(v)
        if ((err = bs.read_bits_ue(sps->conf_win_left_offset_)) != srs_success) {
            return srs_error_wrap(err, "conf_win_left_offset");
        }
        // conf_win_right_offset  ue(v)
        if ((err = bs.read_bits_ue(sps->conf_win_right_offset_)) != srs_success) {
            return srs_error_wrap(err, "conf_win_right_offset");
        }
        // conf_win_top_offset  ue(v)
        if ((err = bs.read_bits_ue(sps->conf_win_top_offset_)) != srs_success) {
            return srs_error_wrap(err, "conf_win_top_offset");
        }
        // conf_win_bottom_offset  ue(v)
        if ((err = bs.read_bits_ue(sps->conf_win_bottom_offset_)) != srs_success) {
            return srs_error_wrap(err, "conf_win_bottom_offset");
        }

        // Table 6-1, 7.4.3.2.1
        // ITU-T-H.265-2021.pdf, page 42.
        // Recalculate width and height
        // Note: 1 is added to the manual, but it is not actually used
        // https://gitlab.com/mbunkus/mkvtoolnix/-/issues/1152
        int sub_width_c = ((1 == sps->chroma_format_idc_) || (2 == sps->chroma_format_idc_)) && (0 == sps->separate_colour_plane_flag_) ? 2 : 1;
        int sub_height_c = (1 == sps->chroma_format_idc_) && (0 == sps->separate_colour_plane_flag_) ? 2 : 1;
        vcodec_->width_ -= (sub_width_c * sps->conf_win_right_offset_ + sub_width_c * sps->conf_win_left_offset_);
        vcodec_->height_ -= (sub_height_c * sps->conf_win_bottom_offset_ + sub_height_c * sps->conf_win_top_offset_);
    }

    // bit_depth_luma_minus8  ue(v)
    if ((err = bs.read_bits_ue(sps->bit_depth_luma_minus8_)) != srs_success) {
        return srs_error_wrap(err, "bit_depth_luma_minus8");
    }
    // bit_depth_chroma_minus8  ue(v)
    if ((err = bs.read_bits_ue(sps->bit_depth_chroma_minus8_)) != srs_success) {
        return srs_error_wrap(err, "bit_depth_chroma_minus8");
    }

    // bit depth
    dec_conf_rec->bit_depth_luma_minus8_ = sps->bit_depth_luma_minus8_ + 8;
    dec_conf_rec->bit_depth_chroma_minus8_ = sps->bit_depth_chroma_minus8_ + 8;

    // log2_max_pic_order_cnt_lsb_minus4  ue(v)
    if ((err = bs.read_bits_ue(sps->log2_max_pic_order_cnt_lsb_minus4_)) != srs_success) {
        return srs_error_wrap(err, "log2_max_pic_order_cnt_lsb_minus4");
    }

    // TODO: FIXME: Implements it, you might parse remain bits for seq_parameter_set_rbsp.
    // 7.3.2.2 Sequence parameter set RBSP syntax
    // ITU-T-H.265-2021.pdf, page 55 ~ page 57.

    // 7.3.2.11 RBSP trailing bits syntax
    // ITU-T-H.265-2021.pdf, page 61.
    // rbsp_trailing_bits()

    return err;
}

srs_error_t SrsFormat::hevc_demux_pps(SrsBuffer *stream)
{
    // for NALU, ITU-T H.265 7.3.2.3 Picture parameter set RBSP syntax
    // @see 7.3.2.3 Picture parameter set RBSP syntax
    // @doc ITU-T-H.265-2021.pdf, page 57.
    if (!stream->require(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "decode hevc pps requires 1 only %d bytes", stream->left());
    }
    int8_t nutv = stream->read_1bytes();

    // forbidden_zero_bit shall be equal to 0.
    int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "hevc forbidden_zero_bit=%d shall be equal to 0", forbidden_zero_bit);
    }

    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    // @see 7.4.2.2 NAL unit header semantics
    // @doc ITU-T-H.265-2021.pdf, page 86.
    SrsHevcNaluType nal_unit_type = (SrsHevcNaluType)((nutv >> 1) & 0x3f);
    if (nal_unit_type != SrsHevcNaluType_PPS) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "hevc pps nal_unit_type=%d shall be equal to 33", nal_unit_type);
    }

    // nuh_layer_id + nuh_temporal_id_plus1
    stream->skip(1);

    // decode the rbsp from pps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    std::vector<uint8_t> rbsp(stream->size());

    int nb_rbsp = srs_rbsp_remove_emulation_bytes(stream, rbsp);

    return hevc_demux_pps_rbsp((char *)&rbsp[0], nb_rbsp);
}

srs_error_t SrsFormat::hevc_demux_pps_rbsp(char *rbsp, int nb_rbsp)
{
    srs_error_t err = srs_success;

    // reparse the rbsp.
    SrsBuffer stream(rbsp, nb_rbsp);

    // H265 PPS NAL Unit (pic_parameter_set_rbsp()) parser.
    // Section 7.3.2.3 ("Picture parameter set RBSP syntax") of the H.265
    // ITU-T-H.265-2021.pdf, page 57.
    SrsBitBuffer bs(&stream);

    // pps_pic_parameter_set_id  ue(v)
    uint32_t pps_pic_parameter_set_id = 0;
    if ((err = bs.read_bits_ue(pps_pic_parameter_set_id)) != srs_success) {
        return srs_error_wrap(err, "pps_pic_parameter_set_id");
    }
    if (pps_pic_parameter_set_id < 0 || pps_pic_parameter_set_id >= SrsHevcMax_PPS_COUNT) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps id out of range: %d", pps_pic_parameter_set_id);
    }

    // select table
    SrsHevcDecoderConfigurationRecord *dec_conf_rec = &(vcodec_->hevc_dec_conf_record_);
    SrsHevcRbspPps *pps = &(dec_conf_rec->pps_table_[pps_pic_parameter_set_id]);
    pps->pps_pic_parameter_set_id_ = pps_pic_parameter_set_id;

    // pps_seq_parameter_set_id  ue(v)
    uint32_t pps_seq_parameter_set_id = 0;
    if ((err = bs.read_bits_ue(pps_seq_parameter_set_id)) != srs_success) {
        return srs_error_wrap(err, "pps_seq_parameter_set_id");
    }
    pps->pps_seq_parameter_set_id_ = pps_seq_parameter_set_id;

    if (!bs.require_bits(7)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps slice requires 7 only %d bits", bs.left_bits());
    }

    // dependent_slice_segments_enabled_flag  u(1)
    pps->dependent_slice_segments_enabled_flag_ = bs.read_bit();
    // output_flag_present_flag  u(1)
    pps->output_flag_present_flag_ = bs.read_bit();
    // num_extra_slice_header_bits  u(3)
    pps->num_extra_slice_header_bits_ = bs.read_bits(3);
    // sign_data_hiding_enabled_flag  u(1)
    pps->sign_data_hiding_enabled_flag_ = bs.read_bit();
    // cabac_init_present_flag  u(1)
    pps->cabac_init_present_flag_ = bs.read_bit();

    // num_ref_idx_l0_default_active_minus1  ue(v)
    if ((err = bs.read_bits_ue(pps->num_ref_idx_l0_default_active_minus1_)) != srs_success) {
        return srs_error_wrap(err, "num_ref_idx_l0_default_active_minus1");
    }
    // num_ref_idx_l1_default_active_minus1  ue(v)
    if ((err = bs.read_bits_ue(pps->num_ref_idx_l1_default_active_minus1_)) != srs_success) {
        return srs_error_wrap(err, "num_ref_idx_l1_default_active_minus1");
    }
    // init_qp_minus26  se(v)
    if ((err = bs.read_bits_se(pps->init_qp_minus26_)) != srs_success) {
        return srs_error_wrap(err, "init_qp_minus26");
    }

    if (!bs.require_bits(3)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps requires 3 only %d bits", bs.left_bits());
    }

    // constrained_intra_pred_flag  u(1)
    pps->constrained_intra_pred_flag_ = bs.read_bit();
    // transform_skip_enabled_flag  u(1)
    pps->transform_skip_enabled_flag_ = bs.read_bit();
    // cu_qp_delta_enabled_flag  u(1)
    pps->cu_qp_delta_enabled_flag_ = bs.read_bit();
    if (pps->cu_qp_delta_enabled_flag_) {
        // diff_cu_qp_delta_depth  ue(v)
        if ((err = bs.read_bits_ue(pps->diff_cu_qp_delta_depth_)) != srs_success) {
            return srs_error_wrap(err, "diff_cu_qp_delta_depth");
        }
    }
    // pps_cb_qp_offset  se(v)
    if ((err = bs.read_bits_se(pps->pps_cb_qp_offset_)) != srs_success) {
        return srs_error_wrap(err, "pps_cb_qp_offset");
    }
    // pps_cr_qp_offset  se(v)
    if ((err = bs.read_bits_se(pps->pps_cr_qp_offset_)) != srs_success) {
        return srs_error_wrap(err, "pps_cr_qp_offset");
    }

    if (!bs.require_bits(6)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps slice_chroma_qp requires 6 only %d bits", bs.left_bits());
    }

    // pps_slice_chroma_qp_offsets_present_flag  u(1)
    pps->pps_slice_chroma_qp_offsets_present_flag_ = bs.read_bit();
    // weighted_pred_flag  u(1)
    pps->weighted_pred_flag_ = bs.read_bit();
    // weighted_bipred_flag  u(1)
    pps->weighted_bipred_flag_ = bs.read_bit();
    // transquant_bypass_enabled_flag  u(1)
    pps->transquant_bypass_enabled_flag_ = bs.read_bit();
    // tiles_enabled_flag  u(1)
    pps->tiles_enabled_flag_ = bs.read_bit();
    // entropy_coding_sync_enabled_flag  u(1)
    pps->entropy_coding_sync_enabled_flag_ = bs.read_bit();

    if (pps->tiles_enabled_flag_) {
        // num_tile_columns_minus1  ue(v)
        if ((err = bs.read_bits_ue(pps->num_tile_columns_minus1_)) != srs_success) {
            return srs_error_wrap(err, "num_tile_columns_minus1");
        }
        // num_tile_rows_minus1  ue(v)
        if ((err = bs.read_bits_ue(pps->num_tile_rows_minus1_)) != srs_success) {
            return srs_error_wrap(err, "num_tile_rows_minus1");
        }

        if (!bs.require_bits(1)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "uniform_spacing_flag requires 1 only %d bits", bs.left_bits());
        }

        // uniform_spacing_flag  u(1)
        pps->uniform_spacing_flag_ = bs.read_bit();
        if (!pps->uniform_spacing_flag_) {
            pps->column_width_minus1_.resize(pps->num_tile_columns_minus1_);
            pps->row_height_minus1_.resize(pps->num_tile_rows_minus1_);

            for (int i = 0; i < (int)pps->num_tile_columns_minus1_; i++) {
                // column_width_minus1[i]  ue(v)
                if ((err = bs.read_bits_ue(pps->column_width_minus1_[i])) != srs_success) {
                    return srs_error_wrap(err, "column_width_minus1");
                }
            }

            for (int i = 0; i < (int)pps->num_tile_rows_minus1_; i++) {
                // row_height_minus1[i]  ue(v)
                if ((err = bs.read_bits_ue(pps->row_height_minus1_[i])) != srs_success) {
                    return srs_error_wrap(err, "row_height_minus1");
                }
            }
        }

        if (!bs.require_bits(1)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "loop_filter_across_tiles_enabled_flag requires 1 only %d bits", bs.left_bits());
        }

        // loop_filter_across_tiles_enabled_flag u(1)
        pps->loop_filter_across_tiles_enabled_flag_ = bs.read_bit();
    }

    if (!bs.require_bits(2)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps loop deblocking filter requires 2 only %d bits", bs.left_bits());
    }

    // pps_loop_filter_across_slices_enabled_flag u(1)
    pps->pps_loop_filter_across_slices_enabled_flag_ = bs.read_bit();
    // deblocking_filter_control_present_flag  u(1)
    pps->deblocking_filter_control_present_flag_ = bs.read_bit();
    if (pps->deblocking_filter_control_present_flag_) {
        if (!bs.require_bits(2)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps loop deblocking filter flag requires 2 only %d bits", bs.left_bits());
        }

        // deblocking_filter_override_enabled_flag u(1)
        pps->deblocking_filter_override_enabled_flag_ = bs.read_bit();
        // pps_deblocking_filter_disabled_flag  u(1)
        pps->pps_deblocking_filter_disabled_flag_ = bs.read_bit();
        if (!pps->pps_deblocking_filter_disabled_flag_) {
            // pps_beta_offset_div2  se(v)
            if ((err = bs.read_bits_se(pps->pps_beta_offset_div2_)) != srs_success) {
                return srs_error_wrap(err, "pps_beta_offset_div2");
            }
            // pps_tc_offset_div2  se(v)
            if ((err = bs.read_bits_se(pps->pps_tc_offset_div2_)) != srs_success) {
                return srs_error_wrap(err, "pps_tc_offset_div2");
            }
        }
    }

    if (!bs.require_bits(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps scaling_list_data requires 1 only %d bits", bs.left_bits());
    }

    // pps_scaling_list_data_present_flag  u(1)
    pps->pps_scaling_list_data_present_flag_ = bs.read_bit();
    if (pps->pps_scaling_list_data_present_flag_) {
        // 7.3.4  Scaling list data syntax
        SrsHevcScalingListData *sld = &pps->scaling_list_data_;
        for (int sizeId = 0; sizeId < 4; sizeId++) {
            for (int matrixId = 0; matrixId < 6; matrixId += (sizeId == 3) ? 3 : 1) {
                // scaling_list_pred_mode_flag  u(1)
                sld->scaling_list_pred_mode_flag_[sizeId][matrixId] = bs.read_bit();
                if (!sld->scaling_list_pred_mode_flag_[sizeId][matrixId]) {
                    // scaling_list_pred_matrix_id_delta  ue(v)
                    if ((err = bs.read_bits_ue(sld->scaling_list_pred_matrix_id_delta_[sizeId][matrixId])) != srs_success) {
                        return srs_error_wrap(err, "scaling_list_pred_matrix_id_delta");
                    }
                } else {
                    int nextCoef = 8;
                    int coefNum = srs_min(64, (1 << (4 + (sizeId << 1))));
                    sld->coefNum_ = coefNum; // tmp store
                    if (sizeId > 1) {
                        // scaling_list_dc_coef_minus8  se(v)
                        if ((err = bs.read_bits_se(sld->scaling_list_dc_coef_minus8_[sizeId - 2][matrixId])) != srs_success) {
                            return srs_error_wrap(err, "scaling_list_dc_coef_minus8");
                        }
                        nextCoef = sld->scaling_list_dc_coef_minus8_[sizeId - 2][matrixId] + 8;
                    }

                    for (int i = 0; i < sld->coefNum_; i++) {
                        // scaling_list_delta_coef  se(v)
                        int scaling_list_delta_coef = 0;
                        if ((err = bs.read_bits_se(scaling_list_delta_coef)) != srs_success) {
                            return srs_error_wrap(err, "scaling_list_delta_coef");
                        }
                        nextCoef = (nextCoef + scaling_list_delta_coef + 256) % 256;
                        sld->ScalingList_[sizeId][matrixId][i] = nextCoef;
                    }
                }
            }
        }
    }

    if (!bs.require_bits(1)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "lists_modification_present_flag requires 1 only %d bits", bs.left_bits());
    }
    // lists_modification_present_flag  u(1)
    pps->lists_modification_present_flag_ = bs.read_bit();

    // log2_parallel_merge_level_minus2  ue(v)
    if ((err = bs.read_bits_ue(pps->log2_parallel_merge_level_minus2_)) != srs_success) {
        return srs_error_wrap(err, "log2_parallel_merge_level_minus2");
    }

    if (!bs.require_bits(2)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "extension_present_flag requires 2 only %d bits", bs.left_bits());
    }

    // slice_segment_header_extension_present_flag  u(1)
    pps->slice_segment_header_extension_present_flag_ = bs.read_bit();
    // pps_extension_present_flag  u(1)
    pps->pps_extension_present_flag_ = bs.read_bit();
    if (pps->pps_extension_present_flag_) {
        if (!bs.require_bits(8)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "pps_range_extension_flag requires 8 only %d bits", bs.left_bits());
        }

        // pps_range_extension_flag  u(1)
        pps->pps_range_extension_flag_ = bs.read_bit();
        // pps_multilayer_extension_flag  u(1)
        pps->pps_multilayer_extension_flag_ = bs.read_bit();
        // pps_3d_extension_flag  u(1)
        pps->pps_3d_extension_flag_ = bs.read_bit();
        // pps_scc_extension_flag  u(1)
        pps->pps_scc_extension_flag_ = bs.read_bit();
        // pps_extension_4bits  u(4)
        pps->pps_extension_4bits_ = bs.read_bits(4);
    }

    // TODO: FIXME: Implements it, you might parse remain bits for pic_parameter_set_rbsp.
    // @see 7.3.2.3 Picture parameter set RBSP syntax
    // @doc ITU-T-H.265-2021.pdf, page 59.

    // TODO: FIXME: rbsp_trailing_bits

    return err;
}

srs_error_t SrsFormat::hevc_demux_rbsp_ptl(SrsBitBuffer *bs, SrsHevcProfileTierLevel *ptl, int profile_present_flag, int max_sub_layers_minus1)
{
    srs_error_t err = srs_success;

    // profile_tier_level() parser.
    // Section 7.3.3 ("Profile, tier and level syntax") of the H.265
    // ITU-T-H.265-2021.pdf, page 62.
    if (profile_present_flag) {
        if (!bs->require_bits(88)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "ptl profile requires 88 only %d bits", bs->left_bits());
        }

        // profile_space  u(2)
        ptl->general_profile_space_ = bs->read_bits(2);
        // tier_flag  u(1)
        ptl->general_tier_flag_ = bs->read_bit();
        // profile_idc  u(5)
        ptl->general_profile_idc_ = bs->read_bits(5);
        for (int i = 0; i < 32; i++) {
            // profile_compatibility_flag[j]  u(1)
            ptl->general_profile_compatibility_flag_[i] = bs->read_bit();
        }
        // progressive_source_flag  u(1)
        ptl->general_progressive_source_flag_ = bs->read_bit();
        // interlaced_source_flag  u(1)
        ptl->general_interlaced_source_flag_ = bs->read_bit();
        // non_packed_constraint_flag  u(1)
        ptl->general_non_packed_constraint_flag_ = bs->read_bit();
        // frame_only_constraint_flag  u(1)
        ptl->general_frame_only_constraint_flag_ = bs->read_bit();
        if (ptl->general_profile_idc_ == 4 || ptl->general_profile_compatibility_flag_[4] ||
            ptl->general_profile_idc_ == 5 || ptl->general_profile_compatibility_flag_[5] ||
            ptl->general_profile_idc_ == 6 || ptl->general_profile_compatibility_flag_[6] ||
            ptl->general_profile_idc_ == 7 || ptl->general_profile_compatibility_flag_[7] ||
            ptl->general_profile_idc_ == 8 || ptl->general_profile_compatibility_flag_[8] ||
            ptl->general_profile_idc_ == 9 || ptl->general_profile_compatibility_flag_[9] ||
            ptl->general_profile_idc_ == 10 || ptl->general_profile_compatibility_flag_[10] ||
            ptl->general_profile_idc_ == 11 || ptl->general_profile_compatibility_flag_[11]) {
            // The number of bits in this syntax structure is not affected by this condition
            // max_12bit_constraint_flag  u(1)
            ptl->general_max_12bit_constraint_flag_ = bs->read_bit();
            // max_10bit_constraint_flag  u(1)
            ptl->general_max_10bit_constraint_flag_ = bs->read_bit();
            // max_8bit_constraint_flag  u(1)
            ptl->general_max_8bit_constraint_flag_ = bs->read_bit();
            // max_422chroma_constraint_flag  u(1)
            ptl->general_max_422chroma_constraint_flag_ = bs->read_bit();
            // max_420chroma_constraint_flag  u(1)
            ptl->general_max_420chroma_constraint_flag_ = bs->read_bit();
            // max_monochrome_constraint_flag  u(1)
            ptl->general_max_monochrome_constraint_flag_ = bs->read_bit();
            // intra_constraint_flag  u(1)
            ptl->general_intra_constraint_flag_ = bs->read_bit();
            // one_picture_only_constraint_flag  u(1)
            ptl->general_one_picture_only_constraint_flag_ = bs->read_bit();
            // lower_bit_rate_constraint_flag  u(1)
            ptl->general_lower_bit_rate_constraint_flag_ = bs->read_bit();

            if (ptl->general_profile_idc_ == 5 || ptl->general_profile_compatibility_flag_[5] == 1 ||
                ptl->general_profile_idc_ == 9 || ptl->general_profile_compatibility_flag_[9] == 1 ||
                ptl->general_profile_idc_ == 10 || ptl->general_profile_compatibility_flag_[10] == 1 ||
                ptl->general_profile_idc_ == 11 || ptl->general_profile_compatibility_flag_[11] == 1) {
                // max_14bit_constraint_flag  u(1)
                ptl->general_max_14bit_constraint_flag_ = bs->read_bit();
                // reserved_zero_33bits  u(33)
                uint32_t bits_tmp_hi = bs->read_bit();
                uint32_t bits_tmp = bs->read_bits(32);
                ptl->general_reserved_zero_33bits_ = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
            } else {
                // reserved_zero_34bits  u(34)
                uint32_t bits_tmp_hi = bs->read_bits(2);
                uint32_t bits_tmp = bs->read_bits(32);
                ptl->general_reserved_zero_34bits_ = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
            }
        } else if (ptl->general_profile_idc_ == 2 || ptl->general_profile_compatibility_flag_[2]) {
            // general_reserved_zero_7bits  u(7)
            ptl->general_reserved_zero_7bits_ = bs->read_bits(7);
            // general_one_picture_only_constraint_flag  u(1)
            ptl->general_one_picture_only_constraint_flag_ = bs->read_bit();
            // general_reserved_zero_35bits  u(35)
            uint32_t bits_tmp_hi = bs->read_bits(3);
            uint32_t bits_tmp = bs->read_bits(32);
            ptl->general_reserved_zero_35bits_ = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
        } else {
            // reserved_zero_43bits  u(43)
            uint32_t bits_tmp_hi = bs->read_bits(11);
            uint32_t bits_tmp = bs->read_bits(32);
            ptl->general_reserved_zero_43bits_ = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
        }

        // The number of bits in this syntax structure is not affected by this condition
        if (ptl->general_profile_idc_ == 1 || ptl->general_profile_compatibility_flag_[1] ||
            ptl->general_profile_idc_ == 2 || ptl->general_profile_compatibility_flag_[2] ||
            ptl->general_profile_idc_ == 3 || ptl->general_profile_compatibility_flag_[3] ||
            ptl->general_profile_idc_ == 4 || ptl->general_profile_compatibility_flag_[4] ||
            ptl->general_profile_idc_ == 5 || ptl->general_profile_compatibility_flag_[5] ||
            ptl->general_profile_idc_ == 9 || ptl->general_profile_compatibility_flag_[9] ||
            ptl->general_profile_idc_ == 11 || ptl->general_profile_compatibility_flag_[11]) {
            // inbld_flag  u(1)
            ptl->general_inbld_flag_ = bs->read_bit();
        } else {
            // reserved_zero_bit  u(1)
            ptl->general_reserved_zero_bit_ = bs->read_bit();
        }
    }

    if (!bs->require_bits(8)) {
        return srs_error_new(ERROR_HEVC_DECODE_ERROR, "ptl level requires 8 only %d bits", bs->left_bits());
    }

    // general_level_idc  u(8)
    ptl->general_level_idc_ = bs->read_8bits();

    ptl->sub_layer_profile_present_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_level_present_flag_.resize(max_sub_layers_minus1);
    for (int i = 0; i < max_sub_layers_minus1; i++) {
        if (!bs->require_bits(2)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "ptl present_flag requires 2 only %d bits", bs->left_bits());
        }
        // sub_layer_profile_present_flag[i]  u(1)
        ptl->sub_layer_profile_present_flag_[i] = bs->read_bit();
        // sub_layer_level_present_flag[i]  u(1)
        ptl->sub_layer_level_present_flag_[i] = bs->read_bit();
    }

    for (int i = max_sub_layers_minus1; max_sub_layers_minus1 > 0 && i < 8; i++) {
        if (!bs->require_bits(2)) {
            return srs_error_new(ERROR_HEVC_DECODE_ERROR, "ptl reserved_zero requires 2 only %d bits", bs->left_bits());
        }
        // reserved_zero_2bits[i]  u(2)
        ptl->reserved_zero_2bits_[i] = bs->read_bits(2);
    }

    ptl->sub_layer_profile_space_.resize(max_sub_layers_minus1);
    ptl->sub_layer_tier_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_profile_idc_.resize(max_sub_layers_minus1);
    ptl->sub_layer_profile_compatibility_flag_.resize(max_sub_layers_minus1);
    for (int i = 0; i < max_sub_layers_minus1; i++) {
        ptl->sub_layer_profile_compatibility_flag_[i].resize(32);
    }
    ptl->sub_layer_progressive_source_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_interlaced_source_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_non_packed_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_frame_only_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_max_12bit_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_max_10bit_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_max_8bit_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_max_422chroma_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_max_420chroma_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_max_monochrome_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_intra_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_one_picture_only_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_lower_bit_rate_constraint_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_reserved_zero_34bits_.resize(max_sub_layers_minus1);
    ptl->sub_layer_reserved_zero_43bits_.resize(max_sub_layers_minus1);
    ptl->sub_layer_inbld_flag_.resize(max_sub_layers_minus1);
    ptl->sub_layer_reserved_zero_bit_.resize(max_sub_layers_minus1);
    ptl->sub_layer_level_idc_.resize(max_sub_layers_minus1);
    for (int i = 0; i < max_sub_layers_minus1; i++) {
        if (ptl->sub_layer_profile_present_flag_[i]) {
            if (!bs->require_bits(88)) {
                return srs_error_new(ERROR_HEVC_DECODE_ERROR, "ptl sub_layer_profile requires 88 only %d bits", bs->left_bits());
            }
            // profile_space  u(2)
            ptl->sub_layer_profile_space_[i] = bs->read_bits(2);
            // tier_flag  u(1)
            ptl->sub_layer_tier_flag_[i] = bs->read_bit();
            // profile_idc  u(5)
            ptl->sub_layer_profile_idc_[i] = bs->read_bits(5);
            for (int j = 0; j < 32; j++) {
                // profile_compatibility_flag[j]  u(1)
                ptl->sub_layer_profile_compatibility_flag_[i][j] = bs->read_bit();
            }
            // progressive_source_flag  u(1)
            ptl->sub_layer_progressive_source_flag_[i] = bs->read_bit();
            // interlaced_source_flag  u(1)
            ptl->sub_layer_interlaced_source_flag_[i] = bs->read_bit();
            // non_packed_constraint_flag  u(1)
            ptl->sub_layer_non_packed_constraint_flag_[i] = bs->read_bit();
            // frame_only_constraint_flag  u(1)
            ptl->sub_layer_frame_only_constraint_flag_[i] = bs->read_bit();
            if (ptl->sub_layer_profile_idc_[i] == 4 || ptl->sub_layer_profile_compatibility_flag_[i][4] ||
                ptl->sub_layer_profile_idc_[i] == 5 || ptl->sub_layer_profile_compatibility_flag_[i][5] ||
                ptl->sub_layer_profile_idc_[i] == 6 || ptl->sub_layer_profile_compatibility_flag_[i][6] ||
                ptl->sub_layer_profile_idc_[i] == 7 || ptl->sub_layer_profile_compatibility_flag_[i][7] ||
                ptl->sub_layer_profile_idc_[i] == 8 || ptl->sub_layer_profile_compatibility_flag_[i][8] ||
                ptl->sub_layer_profile_idc_[i] == 9 || ptl->sub_layer_profile_compatibility_flag_[i][9] ||
                ptl->sub_layer_profile_idc_[i] == 10 || ptl->sub_layer_profile_compatibility_flag_[i][10] ||
                ptl->sub_layer_profile_idc_[i] == 11 || ptl->sub_layer_profile_compatibility_flag_[i][11]) {
                // The number of bits in this syntax structure is not affected by this condition.
                // max_12bit_constraint_flag  u(1)
                ptl->sub_layer_max_12bit_constraint_flag_[i] = bs->read_bit();
                // max_10bit_constraint_flag  u(1)
                ptl->sub_layer_max_10bit_constraint_flag_[i] = bs->read_bit();
                // max_8bit_constraint_flag  u(1)
                ptl->sub_layer_max_8bit_constraint_flag_[i] = bs->read_bit();
                // max_422chroma_constraint_flag  u(1)
                ptl->sub_layer_max_422chroma_constraint_flag_[i] = bs->read_bit();
                // max_420chroma_constraint_flag  u(1)
                ptl->sub_layer_max_420chroma_constraint_flag_[i] = bs->read_bit();
                // max_monochrome_constraint_flag  u(1)
                ptl->sub_layer_max_monochrome_constraint_flag_[i] = bs->read_bit();
                // intra_constraint_flag  u(1)
                ptl->sub_layer_intra_constraint_flag_[i] = bs->read_bit();
                // one_picture_only_constraint_flag  u(1)
                ptl->sub_layer_one_picture_only_constraint_flag_[i] = bs->read_bit();
                // lower_bit_rate_constraint_flag  u(1)
                ptl->sub_layer_lower_bit_rate_constraint_flag_[i] = bs->read_bit();

                if (ptl->sub_layer_profile_idc_[i] == 5 ||
                    ptl->sub_layer_profile_compatibility_flag_[i][5] == 1 ||
                    ptl->sub_layer_profile_idc_[i] == 9 ||
                    ptl->sub_layer_profile_compatibility_flag_[i][9] == 1 ||
                    ptl->sub_layer_profile_idc_[i] == 10 ||
                    ptl->sub_layer_profile_compatibility_flag_[i][10] == 1 ||
                    ptl->sub_layer_profile_idc_[i] == 11 ||
                    ptl->sub_layer_profile_compatibility_flag_[i][11] == 1) {
                    // max_14bit_constraint_flag  u(1)
                    ptl->general_max_14bit_constraint_flag_ = bs->read_bit();
                    // reserved_zero_33bits  u(33)
                    uint32_t bits_tmp_hi = bs->read_bit();
                    uint32_t bits_tmp = bs->read_bits(32);
                    ptl->sub_layer_reserved_zero_33bits_[i] = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
                } else {
                    // reserved_zero_34bits  u(34)
                    uint32_t bits_tmp_hi = bs->read_bits(2);
                    uint32_t bits_tmp = bs->read_bits(32);
                    ptl->sub_layer_reserved_zero_34bits_[i] = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
                }
            } else if (ptl->sub_layer_profile_idc_[i] == 2 || ptl->sub_layer_profile_compatibility_flag_[i][2]) {
                // sub_layer_reserved_zero_7bits  u(7)
                ptl->sub_layer_reserved_zero_7bits_[i] = bs->read_bits(7);
                // sub_layer_one_picture_only_constraint_flag  u(1)
                ptl->sub_layer_one_picture_only_constraint_flag_[i] = bs->read_bit();
                // sub_layer_reserved_zero_35bits  u(35)
                uint32_t bits_tmp_hi = bs->read_bits(3);
                uint32_t bits_tmp = bs->read_bits(32);
                ptl->sub_layer_reserved_zero_35bits_[i] = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
            } else {
                // reserved_zero_43bits  u(43)
                uint32_t bits_tmp_hi = bs->read_bits(11);
                uint32_t bits_tmp = bs->read_bits(32);
                ptl->sub_layer_reserved_zero_43bits_[i] = ((uint64_t)bits_tmp_hi << 32) | bits_tmp;
            }

            // The number of bits in this syntax structure is not affected by this condition
            if (ptl->sub_layer_profile_idc_[i] == 1 || ptl->sub_layer_profile_compatibility_flag_[i][1] ||
                ptl->sub_layer_profile_idc_[i] == 2 || ptl->sub_layer_profile_compatibility_flag_[i][2] ||
                ptl->sub_layer_profile_idc_[i] == 3 || ptl->sub_layer_profile_compatibility_flag_[i][3] ||
                ptl->sub_layer_profile_idc_[i] == 4 || ptl->sub_layer_profile_compatibility_flag_[i][4] ||
                ptl->sub_layer_profile_idc_[i] == 5 || ptl->sub_layer_profile_compatibility_flag_[i][5] ||
                ptl->sub_layer_profile_idc_[i] == 9 || ptl->sub_layer_profile_compatibility_flag_[i][9] ||
                ptl->sub_layer_profile_idc_[i] == 11 || ptl->sub_layer_profile_compatibility_flag_[i][11]) {
                // inbld_flag  u(1)
                ptl->sub_layer_inbld_flag_[i] = bs->read_bit();
            } else {
                // reserved_zero_bit  u(1)
                ptl->sub_layer_reserved_zero_bit_[i] = bs->read_bit();
            }
        }

        if (ptl->sub_layer_level_present_flag_[i]) {
            if (!bs->require_bits(8)) {
                return srs_error_new(ERROR_HEVC_DECODE_ERROR, "ptl sub_layer_level requires 8 only %d bits", bs->left_bits());
            }
            // sub_layer_level_idc  u(8)
            ptl->sub_layer_level_idc_[i] = bs->read_bits(8);
        }
    }

    return err;
}

srs_error_t SrsFormat::avc_demux_sps_pps(SrsBuffer *stream)
{
    // AVCDecoderConfigurationRecord
    // 5.2.4.1.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    int avc_extra_size = stream->size() - stream->pos();
    if (avc_extra_size > 0) {
        char *copy_stream_from = stream->data() + stream->pos();
        vcodec_->avc_extra_data_ = std::vector<char>(copy_stream_from, copy_stream_from + avc_extra_size);
    }

    if (!stream->require(6)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "avc decode sequence header");
    }
    // int8_t configuration_version = stream->read_1bytes();
    stream->read_1bytes();
    // int8_t AVCProfileIndication = stream->read_1bytes();
    vcodec_->avc_profile_ = (SrsAvcProfile)stream->read_1bytes();
    // int8_t profile_compatibility = stream->read_1bytes();
    stream->read_1bytes();
    // int8_t AVCLevelIndication = stream->read_1bytes();
    vcodec_->avc_level_ = (SrsAvcLevel)stream->read_1bytes();

    // parse the NALU size.
    int8_t lengthSizeMinusOne = stream->read_1bytes();
    lengthSizeMinusOne &= 0x03;
    vcodec_->NAL_unit_length_ = lengthSizeMinusOne;

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    if (vcodec_->NAL_unit_length_ == 2) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps lengthSizeMinusOne should never be 2");
    }

    // 1 sps, 7.3.2.1 Sequence parameter set RBSP syntax
    // ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    if (!stream->require(1)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    int8_t numOfSequenceParameterSets = stream->read_1bytes();
    numOfSequenceParameterSets &= 0x1f;
    if (numOfSequenceParameterSets < 1) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    // Support for multiple SPS, then pick the first non-empty one.
    for (int i = 0; i < numOfSequenceParameterSets; ++i) {
        if (!stream->require(2)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS size");
        }
        uint16_t sequenceParameterSetLength = stream->read_2bytes();
        if (!stream->require(sequenceParameterSetLength)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS data");
        }
        if (sequenceParameterSetLength > 0) {
            vcodec_->sequenceParameterSetNALUnit_.resize(sequenceParameterSetLength);
            stream->read_bytes(&vcodec_->sequenceParameterSetNALUnit_[0], sequenceParameterSetLength);
        }
    }

    // 1 pps
    if (!stream->require(1)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS");
    }
    int8_t numOfPictureParameterSets = stream->read_1bytes();
    numOfPictureParameterSets &= 0x1f;
    if (numOfPictureParameterSets < 1) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    // Support for multiple PPS, then pick the first non-empty one.
    for (int i = 0; i < numOfPictureParameterSets; ++i) {
        if (!stream->require(2)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS size");
        }
        uint16_t pictureParameterSetLength = stream->read_2bytes();
        if (!stream->require(pictureParameterSetLength)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS data");
        }
        if (pictureParameterSetLength > 0) {
            vcodec_->pictureParameterSetNALUnit_.resize(pictureParameterSetLength);
            stream->read_bytes(&vcodec_->pictureParameterSetNALUnit_[0], pictureParameterSetLength);
        }
    }
    return avc_demux_sps();
}

srs_error_t SrsFormat::avc_demux_sps()
{
    srs_error_t err = srs_success;

    if (vcodec_->sequenceParameterSetNALUnit_.empty()) {
        return err;
    }

    char *sps = &vcodec_->sequenceParameterSetNALUnit_[0];
    int nbsps = (int)vcodec_->sequenceParameterSetNALUnit_.size();

    SrsBuffer stream(sps, nbsps);

    // for NALU, 7.3.1 NAL unit syntax
    // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
    if (!stream.require(1)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
    }
    int8_t nutv = stream.read_1bytes();

    // forbidden_zero_bit shall be equal to 0.
    int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
    if (forbidden_zero_bit) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "forbidden_zero_bit shall be equal to 0");
    }

    // nal_ref_idc not equal to 0 specifies that the content of the NAL unit contains a sequence parameter set or a picture
    // parameter set or a slice of a reference picture or a slice data partition of a reference picture.
    int8_t nal_ref_idc = (nutv >> 5) & 0x03;
    if (!nal_ref_idc) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "for sps, nal_ref_idc shall be not be equal to 0");
    }

    // 7.4.1 NAL unit semantics
    // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
    // nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
    SrsAvcNaluType nal_unit_type = SrsAvcNaluTypeParse(nutv);
    if (nal_unit_type != 7) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "for sps, nal_unit_type shall be equal to 7");
    }

    // decode the rbsp from sps.
    // rbsp[ i ] a raw byte sequence payload is specified as an ordered sequence of bytes.
    std::vector<uint8_t> rbsp(vcodec_->sequenceParameterSetNALUnit_.size());

    int nb_rbsp = srs_rbsp_remove_emulation_bytes(&stream, rbsp);

    return avc_demux_sps_rbsp((char *)&rbsp[0], nb_rbsp);
}

srs_error_t SrsFormat::avc_demux_sps_rbsp(char *rbsp, int nb_rbsp)
{
    srs_error_t err = srs_success;

    // we donot parse the detail of sps.
    // @see https://github.com/ossrs/srs/issues/474
    if (!avc_parse_sps_) {
        return err;
    }

    // reparse the rbsp.
    SrsBuffer stream(rbsp, nb_rbsp);

    // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
    // ISO_IEC_14496-10-AVC-2012.pdf, page 62.
    if (!stream.require(3)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps shall atleast 3bytes");
    }
    uint8_t profile_idc = stream.read_1bytes();
    if (!profile_idc) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the profile_idc invalid");
    }

    int8_t flags = stream.read_1bytes();
    if (flags & 0x03) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the flags invalid");
    }

    uint8_t level_idc = stream.read_1bytes();
    if (!level_idc) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the level_idc invalid");
    }

    SrsBitBuffer bs(&stream);

    int32_t seq_parameter_set_id = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, seq_parameter_set_id)) != srs_success) {
        return srs_error_wrap(err, "read seq_parameter_set_id");
    }
    if (seq_parameter_set_id < 0) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the seq_parameter_set_id invalid");
    }

    int32_t chroma_format_idc = -1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 || profile_idc == 128) {
        if ((err = srs_avc_nalu_read_uev(&bs, chroma_format_idc)) != srs_success) {
            return srs_error_wrap(err, "read chroma_format_idc");
        }
        if (chroma_format_idc == 3) {
            int8_t separate_colour_plane_flag = -1;
            if ((err = srs_avc_nalu_read_bit(&bs, separate_colour_plane_flag)) != srs_success) {
                return srs_error_wrap(err, "read separate_colour_plane_flag");
            }
        }

        int32_t bit_depth_luma_minus8 = -1;
        if ((err = srs_avc_nalu_read_uev(&bs, bit_depth_luma_minus8)) != srs_success) {
            return srs_error_wrap(err, "read bit_depth_luma_minus8");
            ;
        }

        int32_t bit_depth_chroma_minus8 = -1;
        if ((err = srs_avc_nalu_read_uev(&bs, bit_depth_chroma_minus8)) != srs_success) {
            return srs_error_wrap(err, "read bit_depth_chroma_minus8");
            ;
        }

        int8_t qpprime_y_zero_transform_bypass_flag = -1;
        if ((err = srs_avc_nalu_read_bit(&bs, qpprime_y_zero_transform_bypass_flag)) != srs_success) {
            return srs_error_wrap(err, "read qpprime_y_zero_transform_bypass_flag");
            ;
        }

        int8_t seq_scaling_matrix_present_flag = -1;
        if ((err = srs_avc_nalu_read_bit(&bs, seq_scaling_matrix_present_flag)) != srs_success) {
            return srs_error_wrap(err, "read seq_scaling_matrix_present_flag");
            ;
        }
        if (seq_scaling_matrix_present_flag) {
            int nb_scmpfs = ((chroma_format_idc != 3) ? 8 : 12);
            for (int i = 0; i < nb_scmpfs; i++) {
                int8_t seq_scaling_matrix_present_flag_i = -1;
                if ((err = srs_avc_nalu_read_bit(&bs, seq_scaling_matrix_present_flag_i)) != srs_success) {
                    return srs_error_wrap(err, "read seq_scaling_matrix_present_flag_i");
                    ;
                }
            }
        }
    }

    int32_t log2_max_frame_num_minus4 = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, log2_max_frame_num_minus4)) != srs_success) {
        return srs_error_wrap(err, "read log2_max_frame_num_minus4");
        ;
    }

    int32_t pic_order_cnt_type = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, pic_order_cnt_type)) != srs_success) {
        return srs_error_wrap(err, "read pic_order_cnt_type");
        ;
    }

    if (pic_order_cnt_type == 0) {
        int32_t log2_max_pic_order_cnt_lsb_minus4 = -1;
        if ((err = srs_avc_nalu_read_uev(&bs, log2_max_pic_order_cnt_lsb_minus4)) != srs_success) {
            return srs_error_wrap(err, "read log2_max_pic_order_cnt_lsb_minus4");
            ;
        }
    } else if (pic_order_cnt_type == 1) {
        int8_t delta_pic_order_always_zero_flag = -1;
        if ((err = srs_avc_nalu_read_bit(&bs, delta_pic_order_always_zero_flag)) != srs_success) {
            return srs_error_wrap(err, "read delta_pic_order_always_zero_flag");
            ;
        }

        int32_t offset_for_non_ref_pic = -1;
        if ((err = srs_avc_nalu_read_uev(&bs, offset_for_non_ref_pic)) != srs_success) {
            return srs_error_wrap(err, "read offset_for_non_ref_pic");
            ;
        }

        int32_t offset_for_top_to_bottom_field = -1;
        if ((err = srs_avc_nalu_read_uev(&bs, offset_for_top_to_bottom_field)) != srs_success) {
            return srs_error_wrap(err, "read offset_for_top_to_bottom_field");
            ;
        }

        int32_t num_ref_frames_in_pic_order_cnt_cycle = -1;
        if ((err = srs_avc_nalu_read_uev(&bs, num_ref_frames_in_pic_order_cnt_cycle)) != srs_success) {
            return srs_error_wrap(err, "read num_ref_frames_in_pic_order_cnt_cycle");
            ;
        }
        if (num_ref_frames_in_pic_order_cnt_cycle < 0) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the num_ref_frames_in_pic_order_cnt_cycle");
        }
        for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            int32_t offset_for_ref_frame_i = -1;
            if ((err = srs_avc_nalu_read_uev(&bs, offset_for_ref_frame_i)) != srs_success) {
                return srs_error_wrap(err, "read offset_for_ref_frame_i");
                ;
            }
        }
    }

    int32_t max_num_ref_frames = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, max_num_ref_frames)) != srs_success) {
        return srs_error_wrap(err, "read max_num_ref_frames");
        ;
    }

    int8_t gaps_in_frame_num_value_allowed_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, gaps_in_frame_num_value_allowed_flag)) != srs_success) {
        return srs_error_wrap(err, "read gaps_in_frame_num_value_allowed_flag");
        ;
    }

    int32_t pic_width_in_mbs_minus1 = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, pic_width_in_mbs_minus1)) != srs_success) {
        return srs_error_wrap(err, "read pic_width_in_mbs_minus1");
        ;
    }

    int32_t pic_height_in_map_units_minus1 = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, pic_height_in_map_units_minus1)) != srs_success) {
        return srs_error_wrap(err, "read pic_height_in_map_units_minus1");
        ;
    }

    int8_t frame_mbs_only_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, frame_mbs_only_flag)) != srs_success) {
        return srs_error_wrap(err, "read frame_mbs_only_flag");
        ;
    }
    if (!frame_mbs_only_flag) {
        /* Skip mb_adaptive_frame_field_flag */
        int8_t mb_adaptive_frame_field_flag = -1;
        if ((err = srs_avc_nalu_read_bit(&bs, mb_adaptive_frame_field_flag)) != srs_success) {
            return srs_error_wrap(err, "read mb_adaptive_frame_field_flag");
            ;
        }
    }

    /* Skip direct_8x8_inference_flag */
    int8_t direct_8x8_inference_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, direct_8x8_inference_flag)) != srs_success) {
        return srs_error_wrap(err, "read direct_8x8_inference_flag");
        ;
    }

    /* We need the following value to evaluate offsets, if any */
    int8_t frame_cropping_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, frame_cropping_flag)) != srs_success) {
        return srs_error_wrap(err, "read frame_cropping_flag");
        ;
    }
    int32_t frame_crop_left_offset = 0, frame_crop_right_offset = 0,
            frame_crop_top_offset = 0, frame_crop_bottom_offset = 0;
    if (frame_cropping_flag) {
        if ((err = srs_avc_nalu_read_uev(&bs, frame_crop_left_offset)) != srs_success) {
            return srs_error_wrap(err, "read frame_crop_left_offset");
            ;
        }
        if ((err = srs_avc_nalu_read_uev(&bs, frame_crop_right_offset)) != srs_success) {
            return srs_error_wrap(err, "read frame_crop_right_offset");
            ;
        }
        if ((err = srs_avc_nalu_read_uev(&bs, frame_crop_top_offset)) != srs_success) {
            return srs_error_wrap(err, "read frame_crop_top_offset");
            ;
        }
        if ((err = srs_avc_nalu_read_uev(&bs, frame_crop_bottom_offset)) != srs_success) {
            return srs_error_wrap(err, "read frame_crop_bottom_offset");
            ;
        }
    }

    /* Skip vui_parameters_present_flag */
    int8_t vui_parameters_present_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, vui_parameters_present_flag)) != srs_success) {
        return srs_error_wrap(err, "read vui_parameters_present_flag");
        ;
    }

    vcodec_->width_ = ((pic_width_in_mbs_minus1 + 1) * 16) - frame_crop_left_offset * 2 - frame_crop_right_offset * 2;
    vcodec_->height_ = ((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16) - (frame_crop_top_offset * 2) - (frame_crop_bottom_offset * 2);

    return err;
}

// LCOV_EXCL_STOP

srs_error_t SrsFormat::video_nalu_demux(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // ensure the sequence header demuxed
    if (!vcodec_->is_avc_codec_ok()) {
        srs_warn("avc ignore type=%d for no sequence header", SrsVideoAvcFrameTraitNALU);
        return err;
    }

    if (vcodec_->id_ == SrsVideoCodecIdHEVC) {
        // TODO: FIXME: Might need to guess format?
        return do_avc_demux_ibmf_format(stream);
    }

    // Parse the SPS/PPS in ANNEXB or IBMF format.
    if (vcodec_->payload_format_ == SrsAvcPayloadFormatIbmf) {
        if ((err = avc_demux_ibmf_format(stream)) != srs_success) {
            return srs_error_wrap(err, "avc demux ibmf");
        }
    } else if (vcodec_->payload_format_ == SrsAvcPayloadFormatAnnexb) {
        if ((err = avc_demux_annexb_format(stream)) != srs_success) {
            return srs_error_wrap(err, "avc demux annexb");
        }
    } else {
        if ((err = try_annexb_first_ ? avc_demux_annexb_format(stream) : avc_demux_ibmf_format(stream)) == srs_success) {
            vcodec_->payload_format_ = try_annexb_first_ ? SrsAvcPayloadFormatAnnexb : SrsAvcPayloadFormatIbmf;
        } else {
            srs_freep(err);
            if ((err = try_annexb_first_ ? avc_demux_ibmf_format(stream) : avc_demux_annexb_format(stream)) == srs_success) {
                vcodec_->payload_format_ = try_annexb_first_ ? SrsAvcPayloadFormatIbmf : SrsAvcPayloadFormatAnnexb;
            } else {
                return srs_error_wrap(err, "avc demux try_annexb_first=%d", try_annexb_first_);
            }
        }
    }

    return err;
}

srs_error_t SrsFormat::avc_demux_annexb_format(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    int pos = stream->pos();
    err = do_avc_demux_annexb_format(stream);

    // Restore the stream if error.
    if (err != srs_success) {
        stream->skip(pos - stream->pos());
    }

    return err;
}

srs_error_t SrsFormat::do_avc_demux_annexb_format(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // not annexb, try others
    if (!srs_avc_startswith_annexb(stream, NULL)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "not annexb");
    }

    // AnnexB
    // B.1.1 Byte stream NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    while (!stream->empty()) {
        // find start code
        int nb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &nb_start_code)) {
            return err;
        }

        // skip the start code.
        if (nb_start_code > 0) {
            stream->skip(nb_start_code);
        }

        // the NALU start bytes.
        char *p = stream->data() + stream->pos();

        // get the last matched NALU
        while (!stream->empty()) {
            if (srs_avc_startswith_annexb(stream, NULL)) {
                break;
            }

            stream->skip(1);
        }

        char *pp = stream->data() + stream->pos();

        // skip the empty.
        if (pp - p <= 0) {
            continue;
        }

        // got the NALU.
        if ((err = video_->add_sample(p, (int)(pp - p))) != srs_success) {
            return srs_error_wrap(err, "add video frame");
        }
    }

    return err;
}

srs_error_t SrsFormat::avc_demux_ibmf_format(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    int pos = stream->pos();
    err = do_avc_demux_ibmf_format(stream);

    // Restore the stream if error.
    if (err != srs_success) {
        stream->skip(pos - stream->pos());
    }

    return err;
}

srs_error_t SrsFormat::do_avc_demux_ibmf_format(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    int PictureLength = stream->size() - stream->pos();

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // 5.2.4.1 AVC decoder configuration record
    // 5.2.4.1.2 Semantics
    // The value of this field shall be one of 0, 1, or 3 corresponding to a
    // length encoded with 1, 2, or 4 bytes, respectively.
    srs_assert(vcodec_->NAL_unit_length_ != 2);

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    for (int i = 0; i < PictureLength;) {
        // unsigned int((NAL_unit_length+1)*8) NALUnitLength;
        // TODO: FIXME: Should ignore error? See https://github.com/ossrs/srs-gb28181/commit/a13b9b54938a14796abb9011e7a8ee779439a452
        if (!stream->require(vcodec_->NAL_unit_length_ + 1)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "PictureLength:%d, i:%d, NaluLength:%d, left:%d",
                                 PictureLength, i, vcodec_->NAL_unit_length_, stream->left());
        }
        int32_t NALUnitLength = 0;
        if (vcodec_->NAL_unit_length_ == 3) {
            NALUnitLength = stream->read_4bytes();
        } else if (vcodec_->NAL_unit_length_ == 1) {
            NALUnitLength = stream->read_2bytes();
        } else {
            NALUnitLength = stream->read_1bytes();
        }

        // The stream format mighe be incorrect, see: https://github.com/ossrs/srs/issues/183
        if (NALUnitLength < 0) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "PictureLength:%d, i:%d, NaluLength:%d, left:%d, NALUnitLength:%d",
                                 PictureLength, i, vcodec_->NAL_unit_length_, stream->left(), NALUnitLength);
        }

        // NALUnit
        if (!stream->require(NALUnitLength)) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "PictureLength:%d, i:%d, NaluLength:%d, left:%d, NALUnitLength:%d",
                                 PictureLength, i, vcodec_->NAL_unit_length_, stream->left(), NALUnitLength);
        }
        // 7.3.1 NAL unit syntax, ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        if ((err = video_->add_sample(stream->data() + stream->pos(), NALUnitLength)) != srs_success) {
            return srs_error_wrap(err, "avc add video frame");
        }

        stream->skip(NALUnitLength);
        i += vcodec_->NAL_unit_length_ + 1 + NALUnitLength;
    }

    return err;
}

srs_error_t SrsFormat::audio_aac_demux(SrsBuffer *stream, int64_t timestamp)
{
    srs_error_t err = srs_success;

    audio_->cts_ = 0;
    audio_->dts_ = timestamp;

    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();

    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;

    SrsAudioCodecId codec_id = (SrsAudioCodecId)sound_format;
    acodec_->id_ = codec_id;

    acodec_->sound_type_ = (SrsAudioChannels)sound_type;
    acodec_->sound_rate_ = (SrsAudioSampleRate)sound_rate;
    acodec_->sound_size_ = (SrsAudioSampleBits)sound_size;

    // we support h.264+mp3 for hls.
    if (codec_id == SrsAudioCodecIdMP3) {
        return srs_error_new(ERROR_HLS_TRY_MP3, "try mp3");
    }

    // only support aac
    if (codec_id != SrsAudioCodecIdAAC) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "not supported codec %d", codec_id);
    }

    if (!stream->require(1)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "aac decode aac_packet_type");
    }

    SrsAudioAacFrameTrait aac_packet_type = (SrsAudioAacFrameTrait)stream->read_1bytes();
    audio_->aac_packet_type_ = (SrsAudioAacFrameTrait)aac_packet_type;

    // Update the RAW AAC data.
    raw_ = stream->data() + stream->pos();
    nb_raw_ = stream->size() - stream->pos();

    if (aac_packet_type == SrsAudioAacFrameTraitSequenceHeader) {
        // AudioSpecificConfig
        // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
        int aac_extra_size = stream->size() - stream->pos();
        if (aac_extra_size > 0) {
            char *copy_stream_from = stream->data() + stream->pos();
            acodec_->aac_extra_data_ = std::vector<char>(copy_stream_from, copy_stream_from + aac_extra_size);

            if ((err = audio_aac_sequence_header_demux(&acodec_->aac_extra_data_[0], aac_extra_size)) != srs_success) {
                return srs_error_wrap(err, "demux aac sh");
            }
        }
    } else if (aac_packet_type == SrsAudioAacFrameTraitRawData) {
        // ensure the sequence header demuxed
        if (!acodec_->is_aac_codec_ok()) {
            srs_warn("aac ignore type=%d for no sequence header", aac_packet_type);
            return err;
        }

        // Raw AAC frame data in UI8 []
        // 6.3 Raw Data, ISO_IEC_13818-7-AAC-2004.pdf, page 28
        if ((err = audio_->add_sample(stream->data() + stream->pos(), stream->size() - stream->pos())) != srs_success) {
            return srs_error_wrap(err, "add audio frame");
        }
    } else {
        // ignored.
    }

    // reset the sample rate by sequence header
    if (acodec_->aac_sample_rate_ != SrsAacSampleRateUnset) {
        static int srs_aac_srates[] = {
            96000, 88200, 64000, 48000,
            44100, 32000, 24000, 22050,
            16000, 12000, 11025, 8000,
            7350, 0, 0, 0};
        switch (srs_aac_srates[acodec_->aac_sample_rate_]) {
        case 11025:
            acodec_->sound_rate_ = SrsAudioSampleRate11025;
            break;
        case 22050:
            acodec_->sound_rate_ = SrsAudioSampleRate22050;
            break;
        case 44100:
            acodec_->sound_rate_ = SrsAudioSampleRate44100;
            break;
        default:
            break;
        };
    }

    return err;
}

srs_error_t SrsFormat::audio_mp3_demux(SrsBuffer *stream, int64_t timestamp, bool fresh)
{
    srs_error_t err = srs_success;

    audio_->cts_ = 0;
    audio_->dts_ = timestamp;
    audio_->aac_packet_type_ = fresh ? SrsAudioMp3FrameTraitSequenceHeader : SrsAudioMp3FrameTraitRawData;

    // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
    int8_t sound_format = stream->read_1bytes();

    int8_t sound_type = sound_format & 0x01;
    int8_t sound_size = (sound_format >> 1) & 0x01;
    int8_t sound_rate = (sound_format >> 2) & 0x03;
    sound_format = (sound_format >> 4) & 0x0f;

    SrsAudioCodecId codec_id = (SrsAudioCodecId)sound_format;
    acodec_->id_ = codec_id;

    acodec_->sound_type_ = (SrsAudioChannels)sound_type;
    acodec_->sound_rate_ = (SrsAudioSampleRate)sound_rate;
    acodec_->sound_size_ = (SrsAudioSampleBits)sound_size;

    // we always decode aac then mp3.
    srs_assert(acodec_->id_ == SrsAudioCodecIdMP3);

    // Update the RAW MP3 data. Note the start is 12 bits syncword 0xFFF, so we should not skip any bytes, for detail
    // please see ISO_IEC_11172-3-MP3-1993.pdf page 20 and 26.
    raw_ = stream->data() + stream->pos();
    nb_raw_ = stream->size() - stream->pos();

    // mp3 payload.
    if ((err = audio_->add_sample(raw_, nb_raw_)) != srs_success) {
        return srs_error_wrap(err, "add audio frame");
    }

    return err;
}

srs_error_t SrsFormat::audio_aac_sequence_header_demux(char *data, int size)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsBuffer> buffer(new SrsBuffer(data, size));

    // only need to decode the first 2bytes:
    //      audioObjectType, aac_profile, 5bits.
    //      samplingFrequencyIndex, aac_sample_rate, 4bits.
    //      channelConfiguration, aac_channels, 4bits
    if (!buffer->require(2)) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "audio codec decode aac sh");
    }
    uint8_t profile_ObjectType = buffer->read_1bytes();
    uint8_t samplingFrequencyIndex = buffer->read_1bytes();

    acodec_->aac_channels_ = (samplingFrequencyIndex >> 3) & 0x0f;
    samplingFrequencyIndex = ((profile_ObjectType << 1) & 0x0e) | ((samplingFrequencyIndex >> 7) & 0x01);
    profile_ObjectType = (profile_ObjectType >> 3) & 0x1f;

    // set the aac sample rate.
    acodec_->aac_sample_rate_ = samplingFrequencyIndex;

    // convert the object type in sequence header to aac profile of ADTS.
    acodec_->aac_object_ = (SrsAacObjectType)profile_ObjectType;
    if (acodec_->aac_object_ == SrsAacObjectTypeReserved) {
        return srs_error_new(ERROR_HLS_DECODE_ERROR, "aac decode sh object %d", profile_ObjectType);
    }

    // TODO: FIXME: to support aac he/he-v2, see: ngx_rtmp_codec_parse_aac_header
    // @see: https://github.com/winlinvip/nginx-rtmp-module/commit/3a5f9eea78fc8d11e8be922aea9ac349b9dcbfc2
    //
    // donot force to LC, @see: https://github.com/ossrs/srs/issues/81
    // the source will print the sequence header info.
    // if (aac_profile > 3) {
    // Mark all extended profiles as LC
    // to make Android as happy as possible.
    // @see: ngx_rtmp_hls_parse_aac_header
    // aac_profile = 1;
    //}

    return err;
}
