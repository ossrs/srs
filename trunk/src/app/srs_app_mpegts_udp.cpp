//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_mpegts_udp.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

ISrsUdpCasterListener::ISrsUdpCasterListener()
{
}

ISrsUdpCasterListener::~ISrsUdpCasterListener()
{
}

SrsUdpCasterListener::SrsUdpCasterListener()
{
    caster_ = new SrsMpegtsOverUdp();
    listener_ = new SrsUdpListener(caster_);

    config_ = _srs_config;
}

SrsUdpCasterListener::~SrsUdpCasterListener()
{
    srs_freep(listener_);
    srs_freep(caster_);

    config_ = NULL;
}

void SrsUdpCasterListener::assemble()
{
    caster_->assemble();
}

srs_error_t SrsUdpCasterListener::initialize(SrsConfDirective *conf)
{
    srs_error_t err = srs_success;

    int port = config_->get_stream_caster_listen(conf);
    if (port <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_PORT, "invalid port=%d", port);
    }

    listener_->set_endpoint(srs_net_address_any(), port);
    listener_->set_label("MPEGTS");

    if ((err = caster_->initialize(conf)) != srs_success) {
        return srs_error_wrap(err, "init caster port=%d", port);
    }

    return err;
}

srs_error_t SrsUdpCasterListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    return err;
}

void SrsUdpCasterListener::close()
{
    listener_->close();
}

ISrsMpegtsQueue::ISrsMpegtsQueue()
{
}

ISrsMpegtsQueue::~ISrsMpegtsQueue()
{
}

SrsMpegtsQueue::SrsMpegtsQueue()
{
    nb_audios_ = nb_videos_ = 0;
}

SrsMpegtsQueue::~SrsMpegtsQueue()
{
    clear();
}

void SrsMpegtsQueue::clear()
{
    std::map<int64_t, SrsMediaPacket *>::iterator it;
    for (it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsMediaPacket *msg = it->second;
        srs_freep(msg);
    }
    msgs_.clear();
    nb_audios_ = nb_videos_ = 0;
}

srs_error_t SrsMpegtsQueue::push(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: use right way.
    while (msgs_.find(msg->timestamp_) != msgs_.end()) {
        msg->timestamp_ += 1;
    }

    if (msg->is_audio()) {
        nb_audios_++;
    }

    if (msg->is_video()) {
        nb_videos_++;
    }

    msgs_[msg->timestamp_] = msg;

    return err;
}

SrsMediaPacket *SrsMpegtsQueue::dequeue()
{
    bool av_ok = nb_videos_ >= 2 && nb_audios_ >= 2;
    // 100 videos or 300 audios are about 30s.
    bool av_overflow = nb_videos_ > 100 || nb_audios_ > 300;

    if (av_ok || av_overflow) {
        std::map<int64_t, SrsMediaPacket *>::iterator it = msgs_.begin();
        SrsMediaPacket *msg = it->second;
        msgs_.erase(it);

        if (msg->is_audio()) {
            nb_audios_--;
        }

        if (msg->is_video()) {
            nb_videos_--;
        }

        return msg;
    }

    return NULL;
}

ISrsMpegtsOverUdp::ISrsMpegtsOverUdp()
{
}

ISrsMpegtsOverUdp::~ISrsMpegtsOverUdp()
{
}

SrsMpegtsOverUdp::SrsMpegtsOverUdp()
{
    context_ = new SrsTsContext();
    buffer_ = new SrsSimpleStream();

    sdk_ = NULL;

    avc_ = new SrsRawH264Stream();
    aac_ = new SrsRawAacStream();
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = false;
    queue_ = new SrsMpegtsQueue();
    pprint_ = NULL;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsMpegtsOverUdp::~SrsMpegtsOverUdp()
{
    close();

    srs_freep(buffer_);
    srs_freep(context_);
    srs_freep(avc_);
    srs_freep(aac_);
    srs_freep(queue_);
    srs_freep(pprint_);

    config_ = NULL;
    app_factory_ = NULL;
}

void SrsMpegtsOverUdp::assemble()
{
    pprint_ = SrsPithyPrint::create_caster();
}

srs_error_t SrsMpegtsOverUdp::initialize(SrsConfDirective *c)
{
    output_ = config_->get_stream_caster_output(c);
    return srs_success;
}

srs_error_t SrsMpegtsOverUdp::on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf)
{
    char address_string[64];
    char port_string[16];
    if (getnameinfo(from, fromlen,
                    (char *)&address_string, sizeof(address_string),
                    (char *)&port_string, sizeof(port_string),
                    NI_NUMERICHOST | NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);

    buffer_->append(buf, nb_buf);

    srs_error_t err = on_udp_bytes(peer_ip, peer_port, buf, nb_buf);
    if (err != srs_success) {
        return srs_error_wrap(err, "process udp");
    }
    return err;
}

srs_error_t SrsMpegtsOverUdp::on_udp_bytes(string host, int port, char *buf, int nb_buf)
{
    srs_error_t err = srs_success;

    // Collect some MB of data to parse at once.
    // TODO: FIXME: comment the following for release.
    // if (buffer->length() < 3 * 1024 * 1024) return ret;
    // TODO: FIXME: remove the debug to file.
#if 0
    SrsFileWriter fw;
    if ((err = fw.open("latest.ts")) != srs_success) {
        return srs_error_wrap(err, "open file");
    }
    if ((err = fw.write(buffer_->bytes(), buffer_->length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "write data");
    }
    fw.close();
#endif
#if 0
    SrsFileReader fr;
    if ((err = fr.open("latest.ts")) != srs_success) {
        return srs_error_wrap(err, "open file");
    }
    buffer_->erase(buffer_->length());
    int nb_fbuf = fr.filesize();
    SrsUniquePtr<char[]> fbuf(new char[nb_fbuf]);
    if ((err = fr.read(fbuf.get(), nb_fbuf, NULL)) != srs_success) {
        return srs_error_wrap(err, "read data");
    }
    fr.close();
    buffer_->append(fbuf.get(), nb_fbuf);
#endif

    // 0x47 is the MPEG-TS sync byte.
    char *p = buffer_->bytes();
    for (int i = 0; i < buffer_->length(); i++) {
        if (p[i] != 0x47) {
            continue;
        }

        if (i > 0) {
            buffer_->erase(i);
        }
        break;
    }

    if (buffer_->length() < SRS_TS_PACKET_SIZE) {
        srs_warn("udp: wait %s:%d packet %d/%d bytes", host.c_str(), port, nb_buf, buffer_->length());
        return err;
    }

    int nb_packet = buffer_->length() / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char *p = buffer_->bytes() + (i * SRS_TS_PACKET_SIZE);
        SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(p, SRS_TS_PACKET_SIZE));

        if ((err = context_->decode(stream.get(), this)) != srs_success) {
            srs_info("parse ts packet err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            continue;
        }
    }

    if (nb_packet > 0) {
        buffer_->erase(nb_packet * SRS_TS_PACKET_SIZE);
    }

    return err;
}

srs_error_t SrsMpegtsOverUdp::on_ts_message(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    pprint_->elapse();

    // The payload is the elementary stream that the stream_id or PID names, carried as in "2.11 Carriage of ISO/IEC
    // 14496 data", hls-mpeg-ts-iso13818-1.pdf, page 103. The most common stream_id for H.264 over MPEG-TS is 0xe0, a
    // video with stream number 0, which seems to be AnnexB (ISO_IEC_14496-10-AVC-2003.pdf, page 211), since an audio
    // with stream number 0 is ADTS (ISO_IEC_14496-3-AAC-2001.pdf, page 75, 1.A.2.2 ADTS).
    //
    // The payload of a private_stream_1, private_stream_2, ECM_stream or EMM_stream is user defined (page 58). In a
    // TS, the PSI, not the stream_id, gives the elementary stream type (page 49, 2.4.4).
    //
    // Table 2-18, Stream_id assignments, page 52: 110x xxxx is an audio stream, with stream number x xxxx, and
    // 1110 xxxx a video stream, with stream number xxxx.

    if (pprint_->can_print()) {
        srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " mpegts: got %s age=%d stream=%s, dts=%" PRId64 ", pts=%" PRId64 ", size=%d, us=%d, cc=%d, sid=%#x(%s-%d)",
                  (msg->channel_->apply_ == SrsTsPidApplyVideo) ? "Video" : "Audio", pprint_->age(), srs_ts_stream2string(msg->channel_->stream_).c_str(),
                  msg->dts_, msg->pts_, msg->payload_->length(), msg->packet_->payload_unit_start_indicator_, msg->continuity_counter_, msg->sid_,
                  msg->is_audio() ? "A" : msg->is_video() ? "V"
                                                          : "N",
                  msg->stream_number());
    }

    // When the audio SID is private stream 1, we use common audio.
    // @see https://github.com/ossrs/srs/issues/740
    if (msg->channel_->apply_ == SrsTsPidApplyAudio && msg->sid_ == SrsTsPESStreamIdPrivateStream1) {
        msg->sid_ = SrsTsPESStreamIdAudioCommon;
    }

    if (msg->stream_number() != 0) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_ES, "ts: unsupported stream format, sid=%#x(%s-%d)",
                             msg->sid_, msg->is_audio() ? "A" : msg->is_video() ? "V"
                                                                                : "N",
                             msg->stream_number());
    }

    if (msg->channel_->stream_ != SrsTsStreamVideoH264 && msg->channel_->stream_ != SrsTsStreamAudioAAC) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "ts: unsupported stream codec=%d", msg->channel_->stream_);
    }

    SrsBuffer avs(msg->payload_->bytes(), msg->payload_->length());

    if (msg->channel_->stream_ == SrsTsStreamVideoH264) {
        if ((err = on_ts_video(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume video");
        }
    }
    if (msg->channel_->stream_ == SrsTsStreamAudioAAC) {
        if ((err = on_ts_audio(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume audio");
        }
    }

    // TODO: FIXME: implements it.
    return err;
}

srs_error_t SrsMpegtsOverUdp::on_ts_video(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    // TS times are in 90 kHz and FLV times in ms. No valid stream has a pts before the dts.
    uint32_t dts = (uint32_t)(msg->dts_ / 90);
    uint32_t cts = (msg->pts_ > msg->dts_) ? (uint32_t)((msg->pts_ - msg->dts_) / 90) : 0;
    uint32_t pts = dts + cts;

    // A PES is one access unit, with NALUs such as the SEI before an IDR and every slice of a frame.
    std::string ibps;
    bool keyframe = false;

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = avc_->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux annexb");
        }

        // Two start codes in a row, or one at the end of the PES, give an empty NALU.
        if (frame == NULL || frame_size == 0) {
            continue;
        }

        // 7.3.1 NAL unit syntax, ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);

        if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }

        if (avc_->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = avc_->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }

            // A cut SPS has no profile or level to build a sequence header from.
            if (sps.empty() || h264_sps_ == sps) {
                continue;
            }
            h264_sps_changed_ = true;
            h264_sps_ = sps;

            if ((err = write_h264_sps_pps(dts, dts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }

        if (avc_->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = avc_->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }

            if (h264_pps_ == pps) {
                continue;
            }
            h264_pps_changed_ = true;
            h264_pps_ = pps;

            if ((err = write_h264_sps_pps(dts, dts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }

        srs_info("mpegts: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        std::string ibp;
        if ((err = avc_->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
            return srs_error_wrap(err, "mux frame");
        }
        ibps.append(ibp);

        if (nal_unit_type == SrsAvcNaluTypeIDR) {
            keyframe = true;
        }
    }

    if (ibps.empty()) {
        return err;
    }

    if ((err = write_h264_ipb_frame(ibps, keyframe, dts, pts)) != srs_success) {
        return srs_error_wrap(err, "write frame");
    }

    return err;
}

srs_error_t SrsMpegtsOverUdp::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: there exists bug, see following comments.
    // The sequence header must be updated when the SPS or the PPS changes, for the PPS may stay the same while the SPS
    // changes.
    if (!h264_sps_changed_ || !h264_pps_changed_) {
        return err;
    }

    std::string sh;
    if ((err = avc_->mux_sequence_header(h264_sps_, h264_pps_, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc_->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "avc to flv");
    }

    // The timestamp in the RTMP message header is the dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }

    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = true;

    return err;
}

srs_error_t SrsMpegtsOverUdp::write_h264_ipb_frame(const std::string &ibps, bool keyframe, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    if (!h264_sps_pps_sent_) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop sps/pps");
    }

    SrsVideoAvcFrameType frame_type = keyframe ? SrsVideoAvcFrameTypeKeyFrame : SrsVideoAvcFrameTypeInterFrame;

    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc_->mux_avc2flv(ibps, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }

    // The timestamp in the RTMP message header is the dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t SrsMpegtsOverUdp::on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    // A PES may carry several AAC frames, with the time of the first one, and each AAC frame holds 1024 samples.
    int frame_idx = 0;

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac_->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }

        if (frame_size <= 0) {
            continue;
        }

        int sample_rate = 44100;
        if (codec.sampling_frequency_index_ < SrsAAcSampleRateNumbers && srs_aac_srates[codec.sampling_frequency_index_] > 0) {
            sample_rate = srs_aac_srates[codec.sampling_frequency_index_];
        }

        int64_t frame_dts = msg->dts_ + (int64_t)frame_idx * 1024 * 90000 / sample_rate;
        uint32_t dts = (uint32_t)(frame_dts / 90);
        frame_idx++;
        srs_info("mpegts: demux aac frame size=%d, dts=%d", frame_size, dts);

        if (aac_specific_config_.empty()) {
            std::string sh;
            if ((err = aac_->mux_sequence_header(&codec, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }
            aac_specific_config_ = sh;

            codec.aac_packet_type_ = 0;

            if ((err = write_audio_raw_frame((char *)sh.data(), (int)sh.length(), &codec, dts)) != srs_success) {
                return srs_error_wrap(err, "write raw audio frame");
            }
        }

        codec.aac_packet_type_ = 1;
        if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }

    return err;
}

srs_error_t SrsMpegtsOverUdp::write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts)
{
    srs_error_t err = srs_success;

    char *data = NULL;
    int size = 0;
    if ((err = aac_->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }

    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsMpegtsOverUdp::rtmp_write_packet(char type, uint32_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    SrsRtmpCommonMessage *cmsg = NULL;
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk_->sid(), &cmsg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(cmsg);

    SrsMediaPacket *msg = new SrsMediaPacket();
    cmsg->to_msg(msg);
    srs_freep(cmsg);

    if ((err = queue_->push(msg)) != srs_success) {
        return srs_error_wrap(err, "push to queue");
    }

    for (;;) {
        if ((msg = queue_->dequeue()) == NULL) {
            break;
        }

        if (pprint_->can_print()) {
            srs_trace("mpegts: send msg %s age=%d, dts=%" PRId64 ", size=%d",
                      msg->is_audio() ? "A" : msg->is_video() ? "V"
                                                              : "N",
                      pprint_->age(), msg->timestamp_, msg->size());
        }

        if ((err = sdk_->send_and_free_message(msg)) != srs_success) {
            close();
            return srs_error_wrap(err, "send messages");
        }
    }

    return err;
}

srs_error_t SrsMpegtsOverUdp::connect()
{
    srs_error_t err = srs_success;

    if (sdk_) {
        return err;
    }

    // The RTMP server reads what this client sends only once its on_publish hook accepts the publish, and
    // a burst of the sender fills the socket buffers before that, so a send waits for the hook. A stream
    // timeout as short as the pulse fails the send, closes the client and drops the media until the next
    // keyframe, so wait as long as the forwarder does.
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_TIMEOUT;
    sdk_ = app_factory_->create_rtmp_client(output_, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", output_.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    if ((err = sdk_->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish");
    }

    return err;
}

void SrsMpegtsOverUdp::close()
{
    srs_freep(sdk_);

    // The next connection is a new publish, and the server has no sequence header of it until the caster sends one
    // again, from the next SPS, PPS and AAC frame of the TS. The queued messages of the closed publish would come in
    // the next one at their old times.
    h264_sps_ = h264_pps_ = "";
    h264_sps_changed_ = h264_pps_changed_ = false;
    h264_sps_pps_sent_ = false;
    aac_specific_config_ = "";
    if (queue_) {
        queue_->clear();
    }
}
