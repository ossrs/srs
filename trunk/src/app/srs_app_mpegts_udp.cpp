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

#include <srs_app_mpegts_udp.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include <srs_rtmp_stack.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_raw_avc.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>

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

int SrsMpegtsQueue::push(SrsSharedPtrMessage* msg)
{
    int ret = ERROR_SUCCESS;

    // TODO: FIXME: use right way.
    for (int i = 0; i < 10; i++) {
        if (msgs.find(msg->timestamp) == msgs.end()) {
            break;
        }

        // adjust the ts, add 1ms.
        msg->timestamp += 1;

        if (i >= 5) {
            srs_warn("mpegts: free the msg for dts exists, dts=%"PRId64, msg->timestamp);
            srs_freep(msg);
            return ret;
        }
    }

    if (msg->is_audio()) {
        nb_audios++;
    }

    if (msg->is_video()) {
        nb_videos++;
    }

    msgs[msg->timestamp] = msg;

    return ret;
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

SrsMpegtsOverUdp::SrsMpegtsOverUdp(SrsConfDirective* c)
{
    stream = new SrsBuffer();
    context = new SrsTsContext();
    buffer = new SrsSimpleStream();
    output = _srs_config->get_stream_caster_output(c);
    
    req = NULL;
    sdk = new SrsSimpleRtmpClient();
    
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

    srs_freep(sdk);
    srs_freep(buffer);
    srs_freep(stream);
    srs_freep(context);
    srs_freep(avc);
    srs_freep(aac);
    srs_freep(queue);
    srs_freep(pprint);
}

int SrsMpegtsOverUdp::on_udp_packet(sockaddr_in* from, char* buf, int nb_buf)
{
    std::string peer_ip = inet_ntoa(from->sin_addr);
    int peer_port = ntohs(from->sin_port);

    // append to buffer.
    buffer->append(buf, nb_buf);

    srs_info("udp: got %s:%d packet %d/%d bytes",
        peer_ip.c_str(), peer_port, nb_buf, buffer->length());
        
    return on_udp_bytes(peer_ip, peer_port, buf, nb_buf);
}

int SrsMpegtsOverUdp::on_udp_bytes(string host, int port, char* buf, int nb_buf)
{
    int ret = ERROR_SUCCESS;

    // collect nMB data to parse in a time.
    // TODO: FIXME: comment the following for release.
    //if (buffer->length() < 3 * 1024 * 1024) return ret;
    // TODO: FIXME: remove the debug to file.
#if 0
    SrsFileWriter fw;
    if ((ret = fw.open("latest.ts")) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = fw.write(buffer->bytes(), buffer->length(), NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    fw.close();
#endif
#if 0
    SrsFileReader fr;
    if ((ret = fr.open("latest.ts")) != ERROR_SUCCESS) {
        return ret;
    }
    buffer->erase(buffer->length());
    int nb_fbuf = fr.filesize();
    char* fbuf = new char[nb_fbuf];
    SrsAutoFreeA(char, fbuf);
    if ((ret = fr.read(fbuf, nb_fbuf, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    fr.close();
    buffer->append(fbuf, nb_fbuf);
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
        return ret;
    }

    // use stream to parse ts packet.
    int nb_packet = buffer->length() / SRS_TS_PACKET_SIZE;
    for (int i = 0; i < nb_packet; i++) {
        char* p = buffer->bytes() + (i * SRS_TS_PACKET_SIZE);
        if ((ret = stream->initialize(p, SRS_TS_PACKET_SIZE)) != ERROR_SUCCESS) {
            return ret;
        }

        // process each ts packet
        if ((ret = context->decode(stream, this)) != ERROR_SUCCESS) {
            srs_warn("mpegts: ignore parse ts packet failed. ret=%d", ret);
            continue;
        }
        srs_info("mpegts: parse ts packet completed");
    }
    srs_info("mpegts: parse udp packet completed");

    // erase consumed bytes
    if (nb_packet > 0) {
        buffer->erase(nb_packet * SRS_TS_PACKET_SIZE);
    }

    return ret;
}

int SrsMpegtsOverUdp::on_ts_message(SrsTsMessage* msg)
{
    int ret = ERROR_SUCCESS;

    pprint->elapse();

    // about the bytes of msg, specified by elementary stream which indicates by PES_packet_data_byte and stream_id
    // for example, when SrsTsStream of SrsTsChannel indicates stream_type is SrsTsStreamVideoMpeg4 and SrsTsStreamAudioMpeg4,
    // the elementary stream can be mux in "2.11 Carriage of ISO/IEC 14496 data" in hls-mpeg-ts-iso13818-1.pdf, page 103
    // @remark, the most popular stream_id is 0xe0 for h.264 over mpegts, which indicates the stream_id is video and 
    //      stream_number is 0, where I guess the elementary is specified in annexb format(H.264-AVC-ISO_IEC_14496-10.pdf, page 211).
    //      because when audio stream_number is 0, the elementary is ADTS(aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75, 1.A.2.2 ADTS).

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
        srs_trace("<- "SRS_CONSTS_LOG_STREAM_CASTER" mpegts: got %s age=%d stream=%s, dts=%"PRId64", pts=%"PRId64", size=%d, us=%d, cc=%d, sid=%#x(%s-%d)",
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
        ret = ERROR_STREAM_CASTER_TS_ES;
        srs_error("mpegts: unsupported stream format, sid=%#x(%s-%d). ret=%d", 
            msg->sid, msg->is_audio()? "A":msg->is_video()? "V":"N", msg->stream_number(), ret);
        return ret;
    }

    // check supported codec
    if (msg->channel->stream != SrsTsStreamVideoH264 && msg->channel->stream != SrsTsStreamAudioAAC) {
        ret = ERROR_STREAM_CASTER_TS_CODEC;
        srs_error("mpegts: unsupported stream codec=%d. ret=%d", msg->channel->stream, ret);
        return ret;
    }

    // parse the stream.
    SrsBuffer avs;
    if ((ret = avs.initialize(msg->payload->bytes(), msg->payload->length())) != ERROR_SUCCESS) {
        srs_error("mpegts: initialize av stream failed. ret=%d", ret);
        return ret;
    }

    // publish audio or video.
    if (msg->channel->stream == SrsTsStreamVideoH264) {
        return on_ts_video(msg, &avs);
    }
    if (msg->channel->stream == SrsTsStreamAudioAAC) {
        return on_ts_audio(msg, &avs);
    }

    // TODO: FIXME: implements it.
    return ret;
}

int SrsMpegtsOverUdp::on_ts_video(SrsTsMessage* msg, SrsBuffer* avs)
{
    int ret = ERROR_SUCCESS;

    // ensure rtmp connected.
    if ((ret = connect()) != ERROR_SUCCESS) {
        return ret;
    }

    // ts tbn to flv tbn.
    u_int32_t dts = (u_int32_t)(msg->dts / 90);
    u_int32_t pts = (u_int32_t)(msg->dts / 90);
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        if ((ret = avc->annexb_demux(avs, &frame, &frame_size)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // 5bits, 7.3.1 NAL unit syntax,
        // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
        
        // ignore the nalu type sps(7), pps(8), aud(9)
        if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }

        // for sps
        if (avc->is_sps(frame, frame_size)) {
            std::string sps;
            if ((ret = avc->sps_demux(frame, frame_size, sps)) != ERROR_SUCCESS) {
                return ret;
            }
        
            if (h264_sps == sps) {
                continue;
            }
            h264_sps_changed = true;
            h264_sps = sps;
        
            if ((ret = write_h264_sps_pps(dts, pts)) != ERROR_SUCCESS) {
                return ret;
            }
            continue;
        }

        // for pps
        if (avc->is_pps(frame, frame_size)) {
            std::string pps;
            if ((ret = avc->pps_demux(frame, frame_size, pps)) != ERROR_SUCCESS) {
                return ret;
            }
        
            if (h264_pps == pps) {
                continue;
            }
            h264_pps_changed = true;
            h264_pps = pps;
        
            if ((ret = write_h264_sps_pps(dts, pts)) != ERROR_SUCCESS) {
                return ret;
            }
            continue;
        }
        
        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("mpegts: demux avc ibp frame size=%d, dts=%d", ibpframe_size, dts);
        if ((ret = write_h264_ipb_frame(frame, frame_size, dts, pts)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMpegtsOverUdp::write_h264_sps_pps(u_int32_t dts, u_int32_t pts)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIMXE: there exists bug, see following comments.
    // when sps or pps changed, update the sequence header,
    // for the pps maybe not changed while sps changed.
    // so, we must check when each video ts message frame parsed.
    if (!h264_sps_changed || !h264_pps_changed) {
        return ret;
    }
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((ret = avc->mux_sequence_header(h264_sps, h264_pps, dts, pts, sh)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsCodecVideoAVCFrameKeyFrame;
    int8_t avc_packet_type = SrsCodecVideoAVCTypeSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((ret = avc->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    if ((ret = rtmp_write_packet(SrsCodecFlvTagVideo, timestamp, flv, nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reset sps and pps.
    h264_sps_changed = false;
    h264_pps_changed = false;
    h264_sps_pps_sent = true;

    return ret;
}

int SrsMpegtsOverUdp::write_h264_ipb_frame(char* frame, int frame_size, u_int32_t dts, u_int32_t pts) 
{
    int ret = ERROR_SUCCESS;
    
    // when sps or pps not sent, ignore the packet.
    // @see https://github.com/ossrs/srs/issues/203
    if (!h264_sps_pps_sent) {
        return ERROR_H264_DROP_BEFORE_SPS_PPS;
    }
    
    // 5bits, 7.3.1 NAL unit syntax,
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsCodecVideoAVCFrame frame_type = SrsCodecVideoAVCFrameInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsCodecVideoAVCFrameKeyFrame;
    }

    std::string ibp;
    if ((ret = avc->mux_ipb_frame(frame, frame_size, ibp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int8_t avc_packet_type = SrsCodecVideoAVCTypeNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((ret = avc->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    return rtmp_write_packet(SrsCodecFlvTagVideo, timestamp, flv, nb_flv);
}

int SrsMpegtsOverUdp::on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs)
{
    int ret = ERROR_SUCCESS;

    // ensure rtmp connected.
    if ((ret = connect()) != ERROR_SUCCESS) {
        return ret;
    }

    // ts tbn to flv tbn.
    u_int32_t dts = (u_int32_t)(msg->dts / 90);
    
    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((ret = aac->adts_demux(avs, &frame, &frame_size, codec)) != ERROR_SUCCESS) {
            return ret;
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
            if ((ret = aac->mux_sequence_header(&codec, sh)) != ERROR_SUCCESS) {
                return ret;
            }
            aac_specific_config = sh;

            codec.aac_packet_type = 0;

            if ((ret = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != ERROR_SUCCESS) {
                return ret;
            }
        }

        // audio raw data.
        codec.aac_packet_type = 1;
        if ((ret = write_audio_raw_frame(frame, frame_size, &codec, dts)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    return ret;
}

int SrsMpegtsOverUdp::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, u_int32_t dts)
{
    int ret = ERROR_SUCCESS;

    char* data = NULL;
    int size = 0;
    if ((ret = aac->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return rtmp_write_packet(SrsCodecFlvTagAudio, dts, data, size);
}

int SrsMpegtsOverUdp::rtmp_write_packet(char type, u_int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    SrsSharedPtrMessage* msg = NULL;
    
    if ((ret = sdk->rtmp_create_msg(type, timestamp, data, size, &msg)) != ERROR_SUCCESS) {
        srs_error("mpegts: create shared ptr msg failed. ret=%d", ret);
        return ret;
    }
    srs_assert(msg);

    // push msg to queue.
    if ((ret = queue->push(msg)) != ERROR_SUCCESS) {
        srs_error("mpegts: push msg to queue failed. ret=%d", ret);
        return ret;
    }

    // for all ready msg, dequeue and send out.
    for (;;) {
        if ((msg = queue->dequeue()) == NULL) {
            break;
        }

        if (pprint->can_print()) {
            srs_trace("mpegts: send msg %s age=%d, dts=%"PRId64", size=%d",
                msg->is_audio()? "A":msg->is_video()? "V":"N", pprint->age(), msg->timestamp, msg->size);
        }
    
        // send out encoded msg.
        if ((ret = sdk->send_and_free_message(msg)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsMpegtsOverUdp::connect()
{
    int ret = ERROR_SUCCESS;

    // when ok, ignore.
    // TODO: FIXME: should reconnect when disconnected.
    if (sdk->connected()) {
        return ret;
    }
    
    int64_t cto = SRS_CONSTS_RTMP_TIMEOUT_US;
    int64_t sto = SRS_CONSTS_RTMP_PULSE_TIMEOUT_US;
    if ((ret = sdk->connect(output, cto, sto)) != ERROR_SUCCESS) {
        srs_error("mpegts: connect %s failed, cto=%"PRId64", sto=%"PRId64". ret=%d", output.c_str(), cto, sto, ret);
        return ret;
    }
    
    if ((ret = sdk->publish()) != ERROR_SUCCESS) {
        srs_error("mpegts: publish failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

void SrsMpegtsOverUdp::close()
{
    srs_freep(req);
    sdk->close();
}

#endif
