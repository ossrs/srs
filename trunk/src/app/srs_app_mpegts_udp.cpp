//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_mpegts_udp.hpp>

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_file.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_utility.hpp>

SrsUdpCasterListener::SrsUdpCasterListener()
{
    caster_ = new SrsMpegtsOverUdp();
    listener_ = new SrsUdpListener(caster_);
}

SrsUdpCasterListener::~SrsUdpCasterListener()
{
    srs_freep(listener_);
    srs_freep(caster_);
}

srs_error_t SrsUdpCasterListener::initialize(SrsConfDirective* conf)
{
    srs_error_t err = srs_success;

    int port = _srs_config->get_stream_caster_listen(conf);
    if (port <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_PORT, "invalid port=%d", port);
    }

    listener_->set_endpoint(srs_any_address_for_listener(), port)->set_label("MPEGTS");

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

SrsMpegtsQueue::SrsMpegtsQueue()
{
    nb_audios = nb_videos = 0;
}

SrsMpegtsQueue::~SrsMpegtsQueue()
{
    std::map<int64_t, SrsSharedPtrMessage*>::iterator it;
    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsSharedPtrMessage* msg = it->second;
        srs_freep(msg);
    }
    msgs.clear();
}

srs_error_t SrsMpegtsQueue::push(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;
    
    // TODO: FIXME: use right way.
    for (int i = 0; i < 10; i++) {
        if (msgs.find(msg->timestamp) == msgs.end()) {
            break;
        }
        
        // adjust the ts, add 1ms.
        msg->timestamp += 1;
        
        if (i >= 100) {
            srs_warn("mpegts: free the msg for dts exists, dts=%" PRId64, msg->timestamp);
            srs_freep(msg);
            return err;
        }
    }
    
    if (msg->is_audio()) {
        nb_audios++;
    }
    
    if (msg->is_video()) {
        nb_videos++;
    }
    
    msgs[msg->timestamp] = msg;
    
    return err;
}

SrsSharedPtrMessage* SrsMpegtsQueue::dequeue()
{
    // got 2+ videos and audios, ok to dequeue.
    bool av_ok = nb_videos >= 2 && nb_audios >= 2;
    // 100 videos about 30s, while 300 audios about 30s
    bool av_overflow = nb_videos > 100 || nb_audios > 300;
    
    if (av_ok || av_overflow) {
        std::map<int64_t, SrsSharedPtrMessage*>::iterator it = msgs.begin();
        SrsSharedPtrMessage* msg = it->second;
        msgs.erase(it);
        
        if (msg->is_audio()) {
            nb_audios--;
        }
        
        if (msg->is_video()) {
            nb_videos--;
        }
        
        return msg;
    }
    
    return NULL;
}

SrsMpegtsOverUdp::SrsMpegtsOverUdp()
{
    context = new SrsTsContext();
    buffer = new SrsSimpleStream();
    
    sdk = NULL;
    
    avc = new SrsRawH264Stream();
    aac = new SrsRawAacStream();
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = false;
    queue = new SrsMpegtsQueue();
    pprint = SrsPithyPrint::create_caster();
}

SrsMpegtsOverUdp::~SrsMpegtsOverUdp()
{
    close();
    
    srs_freep(buffer);
    srs_freep(context);
    srs_freep(avc);
    srs_freep(aac);
    srs_freep(queue);
    srs_freep(pprint);
}

srs_error_t SrsMpegtsOverUdp::initialize(SrsConfDirective* c)
{
    output = _srs_config->get_stream_caster_output(c);
    return srs_success;
}

srs_error_t SrsMpegtsOverUdp::on_udp_packet(const sockaddr* from, const int fromlen, char* buf, int nb_buf)
{
    char address_string[64];
    char port_string[16];
    if(getnameinfo(from, fromlen, 
                   (char*)&address_string, sizeof(address_string),
                   (char*)&port_string, sizeof(port_string),
                   NI_NUMERICHOST|NI_NUMERICSERV)) {
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "bad address");
    }
    std::string peer_ip = std::string(address_string);
    int peer_port = atoi(port_string);
    
    // append to buffer.
    buffer->append(buf, nb_buf);
    
    srs_error_t err = on_udp_bytes(peer_ip, peer_port, buf, nb_buf);
    if (err != srs_success) {
        return srs_error_wrap(err, "process udp");
    }
    return err;
}

srs_error_t SrsMpegtsOverUdp::on_udp_bytes(string host, int port, char* buf, int nb_buf)
{
    srs_error_t err = srs_success;
    
    // collect nMB data to parse in a time.
    // TODO: FIXME: comment the following for release.
    //if (buffer->length() < 3 * 1024 * 1024) return ret;
    // TODO: FIXME: remove the debug to file.
#if 0
    SrsFileWriter fw;
    if ((err = fw.open("latest.ts")) != srs_success) {
        return srs_error_wrap(err, "open file");
    }
    if ((err = fw.write(buffer->bytes(), buffer->length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "write data");
    }
    fw.close();
#endif
#if 0
    SrsFileReader fr;
    if ((err = fr.open("latest.ts")) != srs_success) {
        return srs_error_wrap(err, "open file");
    }
    buffer->erase(buffer->length());
    int nb_fbuf = fr.filesize();
    SrsUniquePtr<char[]> fbuf(new char[nb_fbuf]);
    if ((err = fr.read(fbuf.get(), nb_fbuf, NULL)) != srs_success) {
        return srs_error_wrap(err, "read data");
    }
    fr.close();
    buffer->append(fbuf.get(), nb_fbuf);
#endif
    
    // find the sync byte of mpegts.
    char* p = buffer->bytes();
    for (int i = 0; i < buffer->length(); i++) {
        if (p[i] != 0x47) {
            continue;
        }
        
        if (i > 0) {
            buffer->erase(i);
        }
        break;
    }
    
    // drop ts packet when size not modulus by 188
    if (buffer->length() < SRS_TS_PACKET_SIZE) {
        srs_warn("udp: wait %s:%d packet %d/%d bytes", host.c_str(), port, nb_buf, buffer->length());
        return err;
    }
    
    // use stream to parse ts packet.
    int nb_packet = buffer->length() / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char* p = buffer->bytes() + (i * SRS_TS_PACKET_SIZE);
        SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(p, SRS_TS_PACKET_SIZE));

        // process each ts packet
        if ((err = context->decode(stream.get(), this)) != srs_success) {
            srs_info("parse ts packet err=%s", srs_error_desc(err).c_str());
            srs_error_reset(err);
            continue;
        }
    }
    
    // erase consumed bytes
    if (nb_packet > 0) {
        buffer->erase(nb_packet * SRS_TS_PACKET_SIZE);
    }
    
    return err;
}

srs_error_t SrsMpegtsOverUdp::on_ts_message(SrsTsMessage* msg)
{
    srs_error_t err = srs_success;
    
    pprint->elapse();
    
    // about the bytes of msg, specified by elementary stream which indicates by PES_packet_data_byte and stream_id
    // for example, when SrsTsStream of SrsTsChannel indicates stream_type is SrsTsStreamVideoMpeg4 and SrsTsStreamAudioMpeg4,
    // the elementary stream can be mux in "2.11 Carriage of ISO/IEC 14496 data" in hls-mpeg-ts-iso13818-1.pdf, page 103
    // @remark, the most popular stream_id is 0xe0 for h.264 over mpegts, which indicates the stream_id is video and
    //      stream_number is 0, where I guess the elementary is specified in annexb format(ISO_IEC_14496-10-AVC-2003.pdf, page 211).
    //      because when audio stream_number is 0, the elementary is ADTS(ISO_IEC_14496-3-AAC-2001.pdf, page 75, 1.A.2.2 ADTS).
    
    // about the bytes of PES_packet_data_byte, defined in hls-mpeg-ts-iso13818-1.pdf, page 58
    // PES_packet_data_byte "C PES_packet_data_bytes shall be contiguous bytes of data from the elementary stream
    // indicated by the packets stream_id or PID. When the elementary stream data conforms to ITU-T
    // Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 13818-3, the PES_packet_data_bytes shall be byte aligned to the bytes of this
    // Recommendation | International Standard. The byte-order of the elementary stream shall be preserved. The number of
    // PES_packet_data_bytes, N, is specified by the PES_packet_length field. N shall be equal to the value indicated in the
    // PES_packet_length minus the number of bytes between the last byte of the PES_packet_length field and the first
    // PES_packet_data_byte.
    //
    // In the case of a private_stream_1, private_stream_2, ECM_stream, or EMM_stream, the contents of the
    // PES_packet_data_byte field are user definable and will not be specified by ITU-T | ISO/IEC in the future.
    
    // about the bytes of stream_id, define in  hls-mpeg-ts-iso13818-1.pdf, page 49
    // stream_id "C In Program Streams, the stream_id specifies the type and number of the elementary stream as defined by the
    // stream_id Table 2-18. In Transport Streams, the stream_id may be set to any valid value which correctly describes the
    // elementary stream type as defined in Table 2-18. In Transport Streams, the elementary stream type is specified in the
    // Program Specific Information as specified in 2.4.4.
    
    // about the stream_id table, define in Table 2-18 "C Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
    //
    // 110x xxxx
    // ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC
    // 14496-3 audio stream number x xxxx
    // ((sid >> 5) & 0x07) == SrsTsPESStreamIdAudio
    //
    // 1110 xxxx
    // ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC
    // 14496-2 video stream number xxxx
    // ((stream_id >> 4) & 0x0f) == SrsTsPESStreamIdVideo
    
    if (pprint->can_print()) {
        srs_trace("<- " SRS_CONSTS_LOG_STREAM_CASTER " mpegts: got %s age=%d stream=%s, dts=%" PRId64 ", pts=%" PRId64 ", size=%d, us=%d, cc=%d, sid=%#x(%s-%d)",
                  (msg->channel->apply == SrsTsPidApplyVideo)? "Video":"Audio", pprint->age(), srs_ts_stream2string(msg->channel->stream).c_str(),
                  msg->dts, msg->pts, msg->payload->length(), msg->packet->payload_unit_start_indicator, msg->continuity_counter, msg->sid,
                  msg->is_audio()? "A":msg->is_video()? "V":"N", msg->stream_number());
    }
    
    // When the audio SID is private stream 1, we use common audio.
    // @see https://github.com/ossrs/srs/issues/740
    if (msg->channel->apply == SrsTsPidApplyAudio && msg->sid == SrsTsPESStreamIdPrivateStream1) {
        msg->sid = SrsTsPESStreamIdAudioCommon;
    }
    
    // when not audio/video, or not adts/annexb format, donot support.
    if (msg->stream_number() != 0) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_ES, "ts: unsupported stream format, sid=%#x(%s-%d)",
            msg->sid, msg->is_audio()? "A":msg->is_video()? "V":"N", msg->stream_number());
    }
    
    // check supported codec
    if (msg->channel->stream != SrsTsStreamVideoH264 && msg->channel->stream != SrsTsStreamAudioAAC) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "ts: unsupported stream codec=%d", msg->channel->stream);
    }
    
    // parse the stream.
    SrsBuffer avs(msg->payload->bytes(), msg->payload->length());
    
    // publish audio or video.
    if (msg->channel->stream == SrsTsStreamVideoH264) {
        if ((err = on_ts_video(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume video");
        }
    }
    if (msg->channel->stream == SrsTsStreamAudioAAC) {
        if ((err = on_ts_audio(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume audio");
        }
    }
    
    // TODO: FIXME: implements it.
    return err;
}

srs_error_t SrsMpegtsOverUdp::on_ts_video(SrsTsMessage* msg, SrsBuffer* avs)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        if ((err = avc->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux annexb");
        }
        
        // 5bits, 7.3.1 NAL unit syntax,
        // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
        
        // ignore the nalu type sps(7), pps(8), aud(9)
        if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }
        
        // for sps
        if (avc->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = avc->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }
            
            if (h264_sps == sps) {
                continue;
            }
            h264_sps_changed = true;
            h264_sps = sps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // for pps
        if (avc->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = avc->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }
            
            if (h264_pps == pps) {
                continue;
            }
            h264_pps_changed = true;
            h264_pps = pps;
            
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }
        
        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("mpegts: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }
    
    return err;
}

srs_error_t SrsMpegtsOverUdp::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // TODO: FIMXE: there exists bug, see following comments.
    // when sps or pps changed, update the sequence header,
    // for the pps maybe not changed while sps changed.
    // so, we must check when each video ts message frame parsed.
    if (!h264_sps_changed || !h264_pps_changed) {
        return err;
    }
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc->mux_sequence_header(h264_sps, h264_pps, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }
    
    // reset sps and pps.
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = true;
    
    return err;
}

srs_error_t SrsMpegtsOverUdp::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    
    // when sps or pps not sent, ignore the packet.
    if (!h264_sps_pps_sent) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop sps/pps");
    }
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }
    
    std::string ibp;
    if ((err = avc->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux frame");
    }
    
    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }
    
    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t SrsMpegtsOverUdp::on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs)
{
    srs_error_t err = srs_success;
    
    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }
        
        // ignore invalid frame,
        //  * atleast 1bytes for aac to decode the data.
        if (frame_size <= 0) {
            continue;
        }
        srs_info("mpegts: demux aac frame size=%d, dts=%d", frame_size, dts);
        
        // generate sh.
        if (aac_specific_config.empty()) {
            std::string sh;
            if ((err = aac->mux_sequence_header(&codec, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }
            aac_specific_config = sh;
            
            codec.aac_packet_type = 0;
            
            if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != srs_success) {
                return srs_error_wrap(err, "write raw audio frame");
            }
        }
        
        // audio raw data.
        codec.aac_packet_type = 1;
        if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }
    
    return err;
}

srs_error_t SrsMpegtsOverUdp::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    
    char* data = NULL;
    int size = 0;
    if ((err = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsMpegtsOverUdp::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    
    SrsSharedPtrMessage* msg = NULL;
    
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);
    
    // push msg to queue.
    if ((err = queue->push(msg)) != srs_success) {
        return srs_error_wrap(err, "push to queue");
    }
    
    // for all ready msg, dequeue and send out.
    for (;;) {
        if ((msg = queue->dequeue()) == NULL) {
            break;
        }
        
        if (pprint->can_print()) {
            srs_trace("mpegts: send msg %s age=%d, dts=%" PRId64 ", size=%d",
                      msg->is_audio()? "A":msg->is_video()? "V":"N", pprint->age(), msg->timestamp, msg->size);
        }
        
        // send out encoded msg.
        if ((err = sdk->send_and_free_message(msg)) != srs_success) {
            close();
            return srs_error_wrap(err, "send messages");
        }
    }
    
    return err;
}

srs_error_t SrsMpegtsOverUdp::connect()
{
    srs_error_t err = srs_success;
    
    // Ignore when connected.
    if (sdk) {
        return err;
    }
    
    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk = new SrsSimpleRtmpClient(output, cto, sto);
    
    if ((err = sdk->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", output.c_str(), srsu2msi(cto), srsu2msi(sto));
    }
    
    if ((err = sdk->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish");
    }
    
    return err;
}

void SrsMpegtsOverUdp::close()
{
    srs_freep(sdk);
}

