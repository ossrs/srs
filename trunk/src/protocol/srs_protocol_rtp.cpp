//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtp.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>

using namespace std;

SrsRtpVideoBuilder::SrsRtpVideoBuilder()
{
    video_sequence_ = 0;
    video_ssrc_ = 0;
    video_payload_type_ = 0;
    format_ = NULL;
}

SrsRtpVideoBuilder::~SrsRtpVideoBuilder()
{
}

srs_error_t SrsRtpVideoBuilder::initialize(SrsFormat *format, uint32_t ssrc, uint8_t payload_type)
{
    format_ = format;
    video_ssrc_ = ssrc;
    video_payload_type_ = payload_type;
    return srs_success;
}

srs_error_t SrsRtpVideoBuilder::package_stap_a(SrsMediaPacket *msg, SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

    SrsFormat *format = format_;
    if (!format || !format->vcodec_) {
        return err;
    }

    pkt->header_.set_payload_type(video_payload_type_);
    pkt->header_.set_ssrc(video_ssrc_);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->header_.set_marker(false);
    pkt->header_.set_sequence(video_sequence_++);
    pkt->header_.set_timestamp(msg->timestamp_ * 90);

    ISrsRtpPayloader *stap = NULL;
    vector<vector<char> *> params;
    int size = 0;

    if (format->vcodec_->id_ == SrsVideoCodecIdHEVC) {
        for (size_t i = 0; i < format->vcodec_->hevc_dec_conf_record_.nalu_vec_.size(); i++) {
            const SrsHevcHvccNalu &nalu = format->vcodec_->hevc_dec_conf_record_.nalu_vec_[i];
            if (nalu.nal_unit_type_ == SrsHevcNaluType_VPS || nalu.nal_unit_type_ == SrsHevcNaluType_SPS || nalu.nal_unit_type_ == SrsHevcNaluType_PPS) {
                const SrsHevcNalData &nal_data = nalu.nal_data_vec_[0];
                params.push_back(&(vector<char> &)nal_data.nal_unit_data_);
                size += nal_data.nal_unit_length_;
            }
        }

        stap = new SrsRtpSTAPPayloadHevc();
        pkt->set_payload(stap, SrsRtpPacketPayloadTypeSTAPHevc);
        pkt->nalu_type_ = kStapHevc;
    } else if (format->vcodec_->id_ == SrsVideoCodecIdAVC) {
        params.push_back(&format->vcodec_->sequenceParameterSetNALUnit_);
        params.push_back(&format->vcodec_->pictureParameterSetNALUnit_);
        size = format->vcodec_->sequenceParameterSetNALUnit_.size() + format->vcodec_->pictureParameterSetNALUnit_.size();

        stap = new SrsRtpSTAPPayload();
        pkt->set_payload(stap, SrsRtpPacketPayloadTypeSTAP);
        pkt->nalu_type_ = kStapA;
    }

    if (size == 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "vps/sps/pps empty");
    }
    char *payload = pkt->wrap(size);

    for (vector<vector<char> *>::iterator it = params.begin(); it != params.end(); ++it) {
        vector<char> *param = *it;
        SrsNaluSample *sample = new SrsNaluSample();
        sample->bytes_ = payload;
        sample->size_ = param->size();
        if (format->vcodec_->id_ == SrsVideoCodecIdHEVC) {
            static_cast<SrsRtpSTAPPayloadHevc *>(stap)->nalus_.push_back(sample);
        } else {
            static_cast<SrsRtpSTAPPayload *>(stap)->nalus_.push_back(sample);
        }

        memcpy(payload, (char *)param->data(), param->size());
        payload += (int)param->size();
    }

    return err;
}

srs_error_t SrsRtpVideoBuilder::package_nalus(SrsMediaPacket *msg, const vector<SrsNaluSample *> &samples, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsFormat *format = format_;
    if (!format || !format->vcodec_) {
        return err;
    }
    bool is_hevc = format->vcodec_->id_ == SrsVideoCodecIdHEVC;

    SrsRtpRawNALUs *raw_raw = new SrsRtpRawNALUs();
    uint8_t first_nalu_type = 0;

    for (int i = 0; i < (int)samples.size(); i++) {
        SrsNaluSample *sample = samples[i];

        if (!sample->size_) {
            continue;
        }

        if (first_nalu_type == 0) {
            first_nalu_type = is_hevc ? uint8_t(SrsHevcNaluTypeParse(sample->bytes_[0])) : uint8_t(SrsAvcNaluTypeParse(sample->bytes_[0]));
        }

        raw_raw->push_back(sample->copy());
    }

    // Ignore empty.
    int nn_bytes = raw_raw->nb_bytes();
    if (nn_bytes <= 0) {
        srs_freep(raw_raw);
        return err;
    }

    if (nn_bytes < kRtpMaxPayloadSize) {
        // Package NALUs in a single RTP packet.
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header_.set_payload_type(video_payload_type_);
        pkt->header_.set_ssrc(video_ssrc_);
        pkt->frame_type_ = SrsFrameTypeVideo;
        pkt->nalu_type_ = first_nalu_type;
        pkt->header_.set_sequence(video_sequence_++);
        pkt->header_.set_timestamp(msg->timestamp_ * 90);
        pkt->set_payload(raw_raw, SrsRtpPacketPayloadTypeNALU);
        pkt->wrap(msg->payload_);
    } else {
        // We must free it, should never use RTP packets to free it,
        // because more than one RTP packet will refer to it.
        SrsUniquePtr<SrsRtpRawNALUs> raw(raw_raw);

        int header_size = is_hevc ? SrsHevcNaluHeaderSize : SrsAvcNaluHeaderSize;

        // Package NALUs in FU-A RTP packets.
        int fu_payload_size = kRtpMaxPayloadSize;

        // The first byte is store in FU-A header.
        uint8_t header = raw->skip_bytes(header_size);

        int nb_left = nn_bytes - header_size;

        int num_of_packet = 1 + (nn_bytes - 1) / fu_payload_size;
        for (int i = 0; i < num_of_packet; ++i) {
            int packet_size = srs_min(nb_left, fu_payload_size);

            SrsRtpPacket *pkt = new SrsRtpPacket();
            pkts.push_back(pkt);

            pkt->header_.set_payload_type(video_payload_type_);
            pkt->header_.set_ssrc(video_ssrc_);
            pkt->frame_type_ = SrsFrameTypeVideo;
            pkt->nalu_type_ = kFuA;
            pkt->header_.set_sequence(video_sequence_++);
            pkt->header_.set_timestamp(msg->timestamp_ * 90);

            if (is_hevc) {
                SrsRtpFUAPayloadHevc *fua = new SrsRtpFUAPayloadHevc();
                if ((err = raw->read_samples(fua->nalus_, packet_size)) != srs_success) {
                    srs_freep(fua);
                    return srs_error_wrap(err, "read hevc samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
                }
                fua->nalu_type_ = SrsHevcNaluTypeParse(header);
                fua->start_ = bool(i == 0);
                fua->end_ = bool(i == num_of_packet - 1);

                pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUAHevc);
            } else {
                SrsRtpFUAPayload *fua = new SrsRtpFUAPayload();
                if ((err = raw->read_samples(fua->nalus_, packet_size)) != srs_success) {
                    srs_freep(fua);
                    return srs_error_wrap(err, "read samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
                }
                fua->nalu_type_ = SrsAvcNaluTypeParse(header);
                fua->start_ = bool(i == 0);
                fua->end_ = bool(i == num_of_packet - 1);

                pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUA);
            }

            pkt->wrap(msg->payload_);

            nb_left -= packet_size;
        }
    }

    return err;
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtpVideoBuilder::package_single_nalu(SrsMediaPacket *msg, SrsNaluSample *sample, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsRtpPacket *pkt = new SrsRtpPacket();
    pkts.push_back(pkt);

    pkt->header_.set_payload_type(video_payload_type_);
    pkt->header_.set_ssrc(video_ssrc_);
    pkt->frame_type_ = SrsFrameTypeVideo;
    pkt->header_.set_sequence(video_sequence_++);
    pkt->header_.set_timestamp(msg->timestamp_ * 90);

    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    raw->payload_ = sample->bytes_;
    raw->nn_payload_ = sample->size_;

    pkt->wrap(msg->payload_);

    return err;
}

srs_error_t SrsRtpVideoBuilder::package_fu_a(SrsMediaPacket *msg, SrsNaluSample *sample, int fu_payload_size, vector<SrsRtpPacket *> &pkts)
{
    srs_error_t err = srs_success;

    SrsFormat *format = format_;
    if (!format || !format->vcodec_) {
        return err;
    }

    bool is_hevc = format->vcodec_->id_ == SrsVideoCodecIdHEVC;
    int header_size = is_hevc ? SrsHevcNaluHeaderSize : SrsAvcNaluHeaderSize;
    srs_assert(sample->size_ >= header_size);

    char *p = sample->bytes_ + header_size;
    int nb_left = sample->size_ - header_size;
    uint8_t header = sample->bytes_[0];

    int num_of_packet = 1 + (nb_left - 1) / fu_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        int packet_size = srs_min(nb_left, fu_payload_size);

        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header_.set_payload_type(video_payload_type_);
        pkt->header_.set_ssrc(video_ssrc_);
        pkt->frame_type_ = SrsFrameTypeVideo;
        pkt->header_.set_sequence(video_sequence_++);
        pkt->header_.set_timestamp(msg->timestamp_ * 90);
        pkt->nalu_type_ = is_hevc ? kFuHevc : kFuA;

        if (is_hevc) {
            // H265 FU-A header
            SrsRtpFUAPayloadHevc2 *fua = new SrsRtpFUAPayloadHevc2();
            pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUAHevc2);

            fua->nalu_type_ = SrsHevcNaluTypeParse(header);
            fua->start_ = bool(i == 0);
            fua->end_ = bool(i == num_of_packet - 1);

            fua->payload_ = p;
            fua->size_ = packet_size;
        } else {
            // H264 FU-A header
            SrsRtpFUAPayload2 *fua = new SrsRtpFUAPayload2();
            pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUA2);

            fua->nri_ = (SrsAvcNaluType)header;
            fua->nalu_type_ = SrsAvcNaluTypeParse(header);
            fua->start_ = bool(i == 0);
            fua->end_ = bool(i == num_of_packet - 1);

            fua->payload_ = p;
            fua->size_ = packet_size;
        }

        pkt->wrap(msg->payload_);

        p += packet_size;
        nb_left -= packet_size;
    }

    return err;
}
