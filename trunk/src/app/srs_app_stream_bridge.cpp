//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_stream_bridge.hpp>

#include <srs_app_source.hpp>
#include <srs_protocol_format.hpp>
#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_core_autofree.hpp>

#include <vector>
using namespace std;

// The RTP payload max size, reserved some paddings for SRTP as such:
//      kRtpPacketSize = kRtpMaxPayloadSize + paddings
// For example, if kRtpPacketSize is 1500, recommend to set kRtpMaxPayloadSize to 1400,
// which reserves 100 bytes for SRTP or paddings.
// otherwise, the kRtpPacketSize must less than MTU, in webrtc source code,
// the rtp max size is assigned by kVideoMtu = 1200.
// so we set kRtpMaxPayloadSize = 1200.
// see @doc https://groups.google.com/g/discuss-webrtc/c/gH5ysR3SoZI
const int kRtpMaxPayloadSize = kRtpPacketSize - 300;

// TODO: Add this function into SrsRtpMux class.
srs_error_t aac_raw_append_adts_header(SrsSharedPtrMessage* shared_audio, SrsFormat* format, char** pbuf, int* pnn_buf)
{
    srs_error_t err = srs_success;

    if (format->is_aac_sequence_header()) {
        return err;
    }

    // If no audio RAW frame, or not parsed for no sequence header, drop the packet.
    if (format->audio->nb_samples == 0) {
        srs_warn("RTC: Drop AAC %d bytes for no sample", shared_audio->size);
        return err;
    }

    if (format->audio->nb_samples != 1) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "adts samples=%d", format->audio->nb_samples);
    }

    int nb_buf = format->audio->samples[0].size + 7;
    char* buf = new char[nb_buf];
    SrsBuffer stream(buf, nb_buf);

    // TODO: Add comment.
    stream.write_1bytes(0xFF);
    stream.write_1bytes(0xF9);
    stream.write_1bytes(((format->acodec->aac_object - 1) << 6) | ((format->acodec->aac_sample_rate & 0x0F) << 2) | ((format->acodec->aac_channels & 0x04) >> 2));
    stream.write_1bytes(((format->acodec->aac_channels & 0x03) << 6) | ((nb_buf >> 11) & 0x03));
    stream.write_1bytes((nb_buf >> 3) & 0xFF);
    stream.write_1bytes(((nb_buf & 0x07) << 5) | 0x1F);
    stream.write_1bytes(0xFC);

    stream.write_bytes(format->audio->samples[0].bytes, format->audio->samples[0].size);

    *pbuf = buf;
    *pnn_buf = nb_buf;

    return err;
}

ISrsStreamBridge::ISrsStreamBridge()
{
}

ISrsStreamBridge::~ISrsStreamBridge()
{
}

SrsFrameToRtmpBridge::SrsFrameToRtmpBridge(SrsLiveSource *src)
{
    source_ = src;
}

SrsFrameToRtmpBridge::~SrsFrameToRtmpBridge()
{
}

srs_error_t SrsFrameToRtmpBridge::initialize(SrsRequest* r)
{
    return srs_success;
}

srs_error_t SrsFrameToRtmpBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    return err;
}

void SrsFrameToRtmpBridge::on_unpublish()
{
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsFrameToRtmpBridge::on_frame(SrsSharedPtrMessage* frame)
{
    return source_->on_frame(frame);
}

#ifdef SRS_FFMPEG_FIT

SrsRtmpToRtcBridge::SrsRtmpToRtcBridge(SrsRtcSource* source)
{
    req = NULL;
    source_ = source;
    format = new SrsRtmpFormat();
    codec_ = new SrsAudioTranscoder();
    latest_codec_ = SrsAudioCodecIdForbidden;
    rtmp_to_rtc = false;
    keep_bframe = false;
    merge_nalus = false;
    meta = new SrsMetaCache();
    audio_sequence = 0;
    video_sequence = 0;

    // audio track ssrc
    if (true) {
        std::vector<SrsRtcTrackDescription*> descs = source->get_track_desc("audio", "opus");
        if (!descs.empty()) {
            audio_ssrc = descs.at(0)->ssrc_;
        }
        // Note we must use the PT of source, see https://github.com/ossrs/srs/pull/3079
        audio_payload_type_ = descs.empty() ? kAudioPayloadType : descs.front()->media_->pt_;
    }

    // video track ssrc
    if (true) {
        std::vector<SrsRtcTrackDescription*> descs = source->get_track_desc("video", "H264");
        if (!descs.empty()) {
            video_ssrc = descs.at(0)->ssrc_;
        }
        // Note we must use the PT of source, see https://github.com/ossrs/srs/pull/3079
        video_payload_type_ = descs.empty() ? kVideoPayloadType : descs.front()->media_->pt_;
    }
}

SrsRtmpToRtcBridge::~SrsRtmpToRtcBridge()
{
    srs_freep(format);
    srs_freep(codec_);
    srs_freep(meta);
}

srs_error_t SrsRtmpToRtcBridge::initialize(SrsRequest* r)
{
    srs_error_t err = srs_success;

    req = r;
    rtmp_to_rtc = _srs_config->get_rtc_from_rtmp(req->vhost);

    if (rtmp_to_rtc) {
        if ((err = format->initialize()) != srs_success) {
            return srs_error_wrap(err, "format initialize");
        }

        // Setup the SPS/PPS parsing strategy.
        format->try_annexb_first = _srs_config->try_annexb_first(r->vhost);
    }

    keep_bframe = _srs_config->get_rtc_keep_bframe(req->vhost);
    merge_nalus = _srs_config->get_rtc_server_merge_nalus();
    srs_trace("RTC bridge from RTMP, rtmp2rtc=%d, keep_bframe=%d, merge_nalus=%d",
              rtmp_to_rtc, keep_bframe, merge_nalus);

    return err;
}

srs_error_t SrsRtmpToRtcBridge::on_publish()
{
    srs_error_t err = srs_success;

    if (!rtmp_to_rtc) {
        return err;
    }

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->clear();

    return err;
}

void SrsRtmpToRtcBridge::on_unpublish()
{
    if (!rtmp_to_rtc) {
        return;
    }

    // Reset the metadata cache, to make VLC happy when disable/enable stream.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta->update_previous_vsh();
    meta->update_previous_ash();

    // @remark This bridge might be disposed here, so never use it.
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsRtmpToRtcBridge::on_frame(SrsSharedPtrMessage* frame)
{
    if (frame->is_audio()) {
        return on_audio(frame);
    } else if (frame->is_video()) {
        return on_video(frame);
    }
    return srs_success;
}

srs_error_t SrsRtmpToRtcBridge::on_audio(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    if (!rtmp_to_rtc) {
        return err;
    }

    // TODO: FIXME: Support parsing OPUS for RTC.
    if ((err = format->on_audio(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume audio");
    }

    // Try to init codec when startup or codec changed.
    if (format->acodec && (err = init_codec(format->acodec->id)) != srs_success) {
        return srs_error_wrap(err, "init codec");
    }

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format->acodec) {
        return err;
    }

    // ts support audio codec: aac/mp3
    SrsAudioCodecId acodec = format->acodec->id;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return err;
    }

    // ignore sequence header
    srs_assert(format->audio);

    if (format->acodec->id == SrsAudioCodecIdMP3) {
        return transcode(format->audio);
    }

    // When drop aac audio packet, never transcode.
    if (acodec != SrsAudioCodecIdAAC) {
        return err;
    }

    char* adts_audio = NULL;
    int nn_adts_audio = 0;
    // TODO: FIXME: Reserve 7 bytes header when create shared message.
    if ((err = aac_raw_append_adts_header(msg, format, &adts_audio, &nn_adts_audio)) != srs_success) {
        return srs_error_wrap(err, "aac append header");
    }

    if (!adts_audio) {
        return err;
    }

    SrsAudioFrame aac;
    aac.dts = format->audio->dts;
    aac.cts = format->audio->cts;
    if ((err = aac.add_sample(adts_audio, nn_adts_audio)) == srs_success) {
        // If OK, transcode the AAC to Opus and consume it.
        err = transcode(&aac);
    }

    srs_freepa(adts_audio);

    return err;
}

srs_error_t SrsRtmpToRtcBridge::init_codec(SrsAudioCodecId codec)
{
    srs_error_t err = srs_success;

    // Ignore if not changed.
    if (latest_codec_ == codec) return err;

    // Create a new codec.
    srs_freep(codec_);
    codec_ = new SrsAudioTranscoder();

    // Initialize the codec according to the codec in stream.
    int bitrate = 48000; // The output bitrate in bps.
    if ((err = codec_->initialize(codec, SrsAudioCodecIdOpus, kAudioChannel, kAudioSamplerate, bitrate)) != srs_success) {
        return srs_error_wrap(err, "init codec=%d", codec);
    }

    // Update the latest codec in stream.
    if (latest_codec_ == SrsAudioCodecIdForbidden) {
        srs_trace("RTMP2RTC: Init audio codec to %d(%s)", codec, srs_audio_codec_id2str(codec).c_str());
    } else {
        srs_trace("RTMP2RTC: Switch audio codec %d(%s) to %d(%s)", latest_codec_, srs_audio_codec_id2str(latest_codec_).c_str(),
                  codec, srs_audio_codec_id2str(codec).c_str());
    }
    latest_codec_ = codec;

    return err;
}

srs_error_t SrsRtmpToRtcBridge::transcode(SrsAudioFrame* audio)
{
    srs_error_t err = srs_success;

    std::vector<SrsAudioFrame*> out_audios;
    if ((err = codec_->transcode(audio, out_audios)) != srs_success) {
        return srs_error_wrap(err, "recode error");
    }

    // Save OPUS packets in shared message.
    if (out_audios.empty()) {
        return err;
    }

    for (std::vector<SrsAudioFrame*>::iterator it = out_audios.begin(); it != out_audios.end(); ++it) {
        SrsAudioFrame* out_audio = *it;

        SrsRtpPacket* pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, pkt);

        if ((err = package_opus(out_audio, pkt)) != srs_success) {
            err = srs_error_wrap(err, "package opus");
            break;
        }

        if ((err = source_->on_rtp(pkt)) != srs_success) {
            err = srs_error_wrap(err, "consume opus");
            break;
        }
    }

    codec_->free_frames(out_audios);

    return err;
}

srs_error_t SrsRtmpToRtcBridge::package_opus(SrsAudioFrame* audio, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    pkt->header.set_payload_type(audio_payload_type_);
    pkt->header.set_ssrc(audio_ssrc);
    pkt->frame_type = SrsFrameTypeAudio;
    pkt->header.set_marker(true);
    pkt->header.set_sequence(audio_sequence++);
    pkt->header.set_timestamp(audio->dts * 48);

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtspPacketPayloadTypeRaw);

    srs_assert(audio->nb_samples == 1);
    raw->payload = pkt->wrap(audio->samples[0].bytes, audio->samples[0].size);
    raw->nn_payload = audio->samples[0].size;

    return err;
}

srs_error_t SrsRtmpToRtcBridge::on_video(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    if (!rtmp_to_rtc) {
        return err;
    }

    // cache the sequence header if h264
    bool is_sequence_header = SrsFlvVideo::sh(msg->payload, msg->size);
    if (is_sequence_header && (err = meta->update_vsh(msg)) != srs_success) {
        return srs_error_wrap(err, "meta update video");
    }

    if ((err = format->on_video(msg)) != srs_success) {
        return srs_error_wrap(err, "format consume video");
    }

    // Ignore if no format->vcodec, it means the codec is not parsed, or unsupport/unknown codec
    // such as H.263 codec
    if (!format->vcodec) {
        return err;
    }

    bool has_idr = false;
    vector<SrsSample*> samples;
    if ((err = filter(msg, format, has_idr, samples)) != srs_success) {
        return srs_error_wrap(err, "filter video");
    }
    int nn_samples = (int)samples.size();

    // Well, for each IDR, we append a SPS/PPS before it, which is packaged in STAP-A.
    if (has_idr) {
        SrsRtpPacket* pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, pkt);

        if ((err = package_stap_a(source_, msg, pkt)) != srs_success) {
            return srs_error_wrap(err, "package stap-a");
        }

        if ((err = source_->on_rtp(pkt)) != srs_success) {
            return srs_error_wrap(err, "consume sps/pps");
        }
    }

    // If merge Nalus, we pcakges all NALUs(samples) as one NALU, in a RTP or FUA packet.
    vector<SrsRtpPacket*> pkts;
    if (merge_nalus && nn_samples > 1) {
        if ((err = package_nalus(msg, samples, pkts)) != srs_success) {
            return srs_error_wrap(err, "package nalus as one");
        }
    } else {
        // By default, we package each NALU(sample) to a RTP or FUA packet.
        for (int i = 0; i < nn_samples; i++) {
            SrsSample* sample = samples[i];

            // We always ignore bframe here, if config to discard bframe,
            // the bframe flag will not be set.
            if (sample->bframe) {
                continue;
            }

            if (sample->size <= kRtpMaxPayloadSize) {
                if ((err = package_single_nalu(msg, sample, pkts)) != srs_success) {
                    return srs_error_wrap(err, "package single nalu");
                }
            } else {
                if ((err = package_fu_a(msg, sample, kRtpMaxPayloadSize, pkts)) != srs_success) {
                    return srs_error_wrap(err, "package fu-a");
                }
            }
        }
    }

    if (!pkts.empty()) {
        pkts.back()->header.set_marker(true);
    }

    return consume_packets(pkts);
}

srs_error_t SrsRtmpToRtcBridge::filter(SrsSharedPtrMessage* msg, SrsFormat* format, bool& has_idr, vector<SrsSample*>& samples)
{
    srs_error_t err = srs_success;

    // If IDR, we will insert SPS/PPS before IDR frame.
    if (format->video && format->video->has_idr) {
        has_idr = true;
    }

    // Update samples to shared frame.
    for (int i = 0; i < format->video->nb_samples; ++i) {
        SrsSample* sample = &format->video->samples[i];

        // Because RTC does not support B-frame, so we will drop them.
        // TODO: Drop B-frame in better way, which not cause picture corruption.
        if (!keep_bframe) {
            if ((err = sample->parse_bframe()) != srs_success) {
                return srs_error_wrap(err, "parse bframe");
            }
            if (sample->bframe) {
                continue;
            }
        }

        samples.push_back(sample);
    }

    return err;
}

srs_error_t SrsRtmpToRtcBridge::package_stap_a(SrsRtcSource* source, SrsSharedPtrMessage* msg, SrsRtpPacket* pkt)
{
    srs_error_t err = srs_success;

    SrsFormat* format = meta->vsh_format();
    if (!format || !format->vcodec) {
        return err;
    }

    // Note that the sps/pps may change, so we should copy it.
    const vector<char>& sps = format->vcodec->sequenceParameterSetNALUnit;
    const vector<char>& pps = format->vcodec->pictureParameterSetNALUnit;
    if (sps.empty() || pps.empty()) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "sps/pps empty");
    }

    pkt->header.set_payload_type(video_payload_type_);
    pkt->header.set_ssrc(video_ssrc);
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->nalu_type = (SrsAvcNaluType)kStapA;
    pkt->header.set_marker(false);
    pkt->header.set_sequence(video_sequence++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpSTAPPayload* stap = new SrsRtpSTAPPayload();
    pkt->set_payload(stap, SrsRtspPacketPayloadTypeSTAP);

    uint8_t header = sps[0];
    stap->nri = (SrsAvcNaluType)header;

    // Copy the SPS/PPS bytes, because it may change.
    int size = (int)(sps.size() + pps.size());
    char* payload = pkt->wrap(size);

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = (int)sps.size();
        stap->nalus.push_back(sample);

        memcpy(payload, (char*)&sps[0], sps.size());
        payload += (int)sps.size();
    }

    if (true) {
        SrsSample* sample = new SrsSample();
        sample->bytes = payload;
        sample->size = (int)pps.size();
        stap->nalus.push_back(sample);

        memcpy(payload, (char*)&pps[0], pps.size());
        payload += (int)pps.size();
    }

    srs_info("RTC STAP-A seq=%u, sps %d, pps %d bytes", pkt->header.get_sequence(), sps.size(), pps.size());

    return err;
}

srs_error_t SrsRtmpToRtcBridge::package_nalus(SrsSharedPtrMessage* msg, const vector<SrsSample*>& samples, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpRawNALUs* raw = new SrsRtpRawNALUs();
    SrsAvcNaluType first_nalu_type = SrsAvcNaluTypeReserved;

    for (int i = 0; i < (int)samples.size(); i++) {
        SrsSample* sample = samples[i];

        // We always ignore bframe here, if config to discard bframe,
        // the bframe flag will not be set.
        if (sample->bframe) {
            continue;
        }

        if (!sample->size) {
            continue;
        }

        if (first_nalu_type == SrsAvcNaluTypeReserved) {
            first_nalu_type = SrsAvcNaluType((uint8_t)(sample->bytes[0] & kNalTypeMask));
        }

        raw->push_back(sample->copy());
    }

    // Ignore empty.
    int nn_bytes = raw->nb_bytes();
    if (nn_bytes <= 0) {
        srs_freep(raw);
        return err;
    }

    if (nn_bytes < kRtpMaxPayloadSize) {
        // Package NALUs in a single RTP packet.
        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header.set_payload_type(video_payload_type_);
        pkt->header.set_ssrc(video_ssrc);
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->nalu_type = (SrsAvcNaluType)first_nalu_type;
        pkt->header.set_sequence(video_sequence++);
        pkt->header.set_timestamp(msg->timestamp * 90);
        pkt->set_payload(raw, SrsRtspPacketPayloadTypeNALU);
        pkt->wrap(msg);
    } else {
        // We must free it, should never use RTP packets to free it,
        // because more than one RTP packet will refer to it.
        SrsAutoFree(SrsRtpRawNALUs, raw);

        // Package NALUs in FU-A RTP packets.
        int fu_payload_size = kRtpMaxPayloadSize;

        // The first byte is store in FU-A header.
        uint8_t header = raw->skip_first_byte();
        uint8_t nal_type = header & kNalTypeMask;
        int nb_left = nn_bytes - 1;

        int num_of_packet = 1 + (nn_bytes - 1) / fu_payload_size;
        for (int i = 0; i < num_of_packet; ++i) {
            int packet_size = srs_min(nb_left, fu_payload_size);

            SrsRtpFUAPayload* fua = new SrsRtpFUAPayload();
            if ((err = raw->read_samples(fua->nalus, packet_size)) != srs_success) {
                srs_freep(fua);
                return srs_error_wrap(err, "read samples %d bytes, left %d, total %d", packet_size, nb_left, nn_bytes);
            }

            SrsRtpPacket* pkt = new SrsRtpPacket();
            pkts.push_back(pkt);

            pkt->header.set_payload_type(video_payload_type_);
            pkt->header.set_ssrc(video_ssrc);
            pkt->frame_type = SrsFrameTypeVideo;
            pkt->nalu_type = (SrsAvcNaluType)kFuA;
            pkt->header.set_sequence(video_sequence++);
            pkt->header.set_timestamp(msg->timestamp * 90);

            fua->nri = (SrsAvcNaluType)header;
            fua->nalu_type = (SrsAvcNaluType)nal_type;
            fua->start = bool(i == 0);
            fua->end = bool(i == num_of_packet - 1);

            pkt->set_payload(fua, SrsRtspPacketPayloadTypeFUA);
            pkt->wrap(msg);

            nb_left -= packet_size;
        }
    }

    return err;
}

// Single NAL Unit Packet @see https://tools.ietf.org/html/rfc6184#section-5.6
srs_error_t SrsRtmpToRtcBridge::package_single_nalu(SrsSharedPtrMessage* msg, SrsSample* sample, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    SrsRtpPacket* pkt = new SrsRtpPacket();
    pkts.push_back(pkt);

    pkt->header.set_payload_type(video_payload_type_);
    pkt->header.set_ssrc(video_ssrc);
    pkt->frame_type = SrsFrameTypeVideo;
    pkt->header.set_sequence(video_sequence++);
    pkt->header.set_timestamp(msg->timestamp * 90);

    SrsRtpRawPayload* raw = new SrsRtpRawPayload();
    pkt->set_payload(raw, SrsRtspPacketPayloadTypeRaw);

    raw->payload = sample->bytes;
    raw->nn_payload = sample->size;

    pkt->wrap(msg);

    return err;
}

srs_error_t SrsRtmpToRtcBridge::package_fu_a(SrsSharedPtrMessage* msg, SrsSample* sample, int fu_payload_size, vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    char* p = sample->bytes + 1;
    int nb_left = sample->size - 1;
    uint8_t header = sample->bytes[0];
    uint8_t nal_type = header & kNalTypeMask;

    int num_of_packet = 1 + (nb_left - 1) / fu_payload_size;
    for (int i = 0; i < num_of_packet; ++i) {
        int packet_size = srs_min(nb_left, fu_payload_size);

        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkts.push_back(pkt);

        pkt->header.set_payload_type(video_payload_type_);
        pkt->header.set_ssrc(video_ssrc);
        pkt->frame_type = SrsFrameTypeVideo;
        pkt->header.set_sequence(video_sequence++);
        pkt->header.set_timestamp(msg->timestamp * 90);

        SrsRtpFUAPayload2* fua = new SrsRtpFUAPayload2();
        pkt->set_payload(fua, SrsRtspPacketPayloadTypeFUA2);

        fua->nri = (SrsAvcNaluType)header;
        fua->nalu_type = (SrsAvcNaluType)nal_type;
        fua->start = bool(i == 0);
        fua->end = bool(i == num_of_packet - 1);

        fua->payload = p;
        fua->size = packet_size;

        pkt->wrap(msg);

        p += packet_size;
        nb_left -= packet_size;
    }

    return err;
}

srs_error_t SrsRtmpToRtcBridge::consume_packets(vector<SrsRtpPacket*>& pkts)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Consume a range of packets.
    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket* pkt = pkts[i];
        if ((err = source_->on_rtp(pkt)) != srs_success) {
            err = srs_error_wrap(err, "consume sps/pps");
            break;
        }
    }

    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket* pkt = pkts[i];
        srs_freep(pkt);
    }

    return err;
}
#endif

