//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtp.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_core_autofree.hpp>

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

srs_error_t SrsRtpVideoBuilder::initialize(SrsFormat* format, uint32_t ssrc, uint8_t payload_type)
{
    format_ = format;
    video_ssrc_ = ssrc;
    video_payload_type_ = payload_type;
    return srs_success;
}

srs_error_t SrsRtpVideoBuilder::package_stap_a(SrsSharedPtrMessage* msg, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    SrsFormat* format = format_;
    if (!format || !format->vcodec) {
        return err;
    }

    pkt->header.set_payload_type(video_payload_type_);
    pkt->header.set_ssrc(video_ssrc_);
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->header.set_marker(false);
    pkt->header.set_sequence(video_sequence_++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    ISrsRtpPayloader* stap = NULL;
    vector<vector<char>*> params;
    int size = 0;

    if (format->vcodec->id == SrsVideoCodecIdHEVC) {
        for (size_t i = 0; i < format->vcodec->hevc_dec_conf_record_.nalu_vec.size(); i++) {
            const SrsHevcHvccNalu& nalu = format->vcodec->hevc_dec_conf_record_.nalu_vec[i];
            if (nalu.nal_unit_type == SrsHevcNaluType_VPS
                || nalu.nal_unit_type == SrsHevcNaluType_SPS
                || nalu.nal_unit_type == SrsHevcNaluType_PPS) {
                const SrsHevcNalData& nal_data = nalu.nal_data_vec[0];
                params.push_back(&(vector<char>&)nal_data.nal_unit_data);
                size += nal_data.nal_unit_length;
            }
        }

        stap = new SrsRtpSTAPPayloadHevc();
        pkt->set_payload(stap, SrsRtpPacketPayloadTypeSTAPHevc);
        pkt->nalu_type = kStapHevc;
    } else if (format->vcodec->id == SrsVideoCodecIdAVC) {
        params.push_back(&format->vcodec->sequenceParameterSetNALUnit);
        params.push_back(&format->vcodec->pictureParameterSetNALUnit);
        size = format->vcodec->sequenceParameterSetNALUnit.size() + format->vcodec->pictureParameterSetNALUnit.size();

        stap = new SrsRtpSTAPPayload();
        pkt->set_payload(stap, SrsRtpPacketPayloadTypeSTAP);
        pkt->nalu_type = kStapA;
    }

    if (size == 0) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "vps/sps/pps empty");
    }
    char* payload = pkt->wrap(size);

    for (vector<vector<char>*>::iterator it = params.begin(); it != params.end(); ++it) {
        vector<char>* param = *it;
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = param->size();
        if (format->vcodec->id == SrsVideoCodecIdHEVC) {
            static_cast<SrsRtpSTAPPayloadHevc*>(stap)->nalus.push_back(sample);
        } else {
            static_cast<SrsRtpSTAPPayload*>(stap)->nalus.push_back(sample);
        }

        memcpy(payload, (char*)param->data(), param->size());
        payload += (int)param->size();
    }

    return err;
}

srs_error_t SrsRtpVideoBuilder::package_nalus(SrsSharedPtrMessage* msg, const vector<SrsSample*>& samples, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsFormat* format = format_;
    if (!format || !format->vcodec) {
        return err;
    }
    bool is_hevc = format->vcodec->id == SrsVideoCodecIdHEVC;

    SrsRtpRawNALUs* raw_raw = new SrsRtpRawNALUs();
    uint8_t first_nalu_type = 0;

    for (int i = 0; i < (int)samples.size(); i++) {
        SrsSample* sample = samples[i];

        if (!sample->size) {
            continue;
        }

        if (first_nalu_type == 0) {
            first_nalu_type = is_hevc ? uint8_t(SrsHevcNaluTypeParse(sample->bytes[0])) : uint8_t(SrsAvcNaluTypeParse(sample->bytes[0]));
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
        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header.set_payload_type(video_payload_type_);
        pkt->header.set_ssrc(video_ssrc_);
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->nalu_type = first_nalu_type;
        pkt->header.set_sequence(video_sequence_++);
        pkt->header.set_timestamp(msg->timestamp * 90);
        pkt->set_payload(raw_raw, SrsRtpPacketPayloadTypeNALU);
        pkt->wrap(msg);
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

            SrsRtpPacket* pkt = new SrsRtpPacket();
            pkts.push_back(pkt);

            pkt->header.set_payload_type(video_payload_type_);
            pkt->header.set_ssrc(video_ssrc_);
            pkt->frame_type = SrsFrameTypeVideo;
            pkt->nalu_type = kFuA;
            pkt->header.set_sequence(video_sequence_++);
            pkt->header.set_timestamp(msg->timestamp * 90);

            if (is_hevc) {
                SrsRtpFUAPayloadHevc* fua = new SrsRtpFUAPayloadHevc();
                if ((err = raw->read_samples(fua->nalus, packet_size)) != srs_success) {
                    srs_freep(fua);
                    return srs_error_wrap(err, "read hevc samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
                }
                fua->nalu_type = SrsHevcNaluTypeParse(header);
                fua->start = bool(i == 0);
                fua->end = bool(i == num_of_packet - 1);

                pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUAHevc);
            } else {
                SrsRtpFUAPayload* fua = new SrsRtpFUAPayload();
                if ((err = raw->read_samples(fua->nalus, packet_size)) != srs_success) {
                    srs_freep(fua);
                    return srs_error_wrap(err, "read samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
                }
                fua->nalu_type = SrsAvcNaluTypeParse(header);
                fua->start = bool(i == 0);
                fua->end = bool(i == num_of_packet - 1);

                pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUA);
            }

            pkt->wrap(msg);

            nb_left -= packet_size;
        }
    }

    return err;
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtpVideoBuilder::package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpPacket* pkt = new SrsRtpPacket();
    pkts.push_back(pkt);

    pkt->header.set_payload_type(video_payload_type_);
    pkt->header.set_ssrc(video_ssrc_);
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->header.set_sequence(video_sequence_++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtpPacketPayloadTypeRaw);

    raw->payload = sample->bytes;
    raw->nn_payload = sample->size;

    pkt->wrap(msg);

    return err;
}

srs_error_t SrsRtpVideoBuilder::package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsFormat* format = format_;
    if (!format || !format->vcodec) {
        return err;
    }

    bool is_hevc = format->vcodec->id == SrsVideoCodecIdHEVC;
    int header_size = is_hevc ? SrsHevcNaluHeaderSize : SrsAvcNaluHeaderSize;
    srs_assert(sample->size >= header_size);

    char* p = sample->bytes + header_size;
    int nb_left = sample->size - header_size;
    uint8_t header = sample->bytes[0];

    int num_of_packet = 1 + (nb_left - 1) / fu_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        int packet_size = srs_min(nb_left, fu_payload_size);

        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header.set_payload_type(video_payload_type_);
        pkt->header.set_ssrc(video_ssrc_);
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->header.set_sequence(video_sequence_++);
        pkt->header.set_timestamp(msg->timestamp * 90);
        pkt->nalu_type = is_hevc ? kFuHevc : kFuA;

        if (is_hevc) {
            // H265 FU-A header
            SrsRtpFUAPayloadHevc2* fua = new SrsRtpFUAPayloadHevc2();
            pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUAHevc2);

            fua->nalu_type = SrsHevcNaluTypeParse(header);
            fua->start = bool(i == 0);
            fua->end = bool(i == num_of_packet - 1);

            fua->payload = p;
            fua->size = packet_size;
        } else {
            // H264 FU-A header
            SrsRtpFUAPayload2* fua = new SrsRtpFUAPayload2();
            pkt->set_payload(fua, SrsRtpPacketPayloadTypeFUA2);

            fua->nri = (SrsAvcNaluType)header;
            fua->nalu_type = SrsAvcNaluTypeParse(header);
            fua->start = bool(i == 0);
            fua->end = bool(i == num_of_packet - 1);

            fua->payload = p;
            fua->size = packet_size;
        }

        pkt->wrap(msg);

        p += packet_size;
        nb_left -= packet_size;
    }

    return err;
}

