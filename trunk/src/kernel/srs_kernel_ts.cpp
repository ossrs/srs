/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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

#include <srs_kernel_ts.hpp>

#if !defined(SRS_EXPORT_LIBRTMP)

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>

// in ms, for HLS aac sync time.
#define SRS_CONF_DEFAULT_AAC_SYNC 100

// @see: ngx_rtmp_hls_audio
/* We assume here AAC frame size is 1024
 * Need to handle AAC frames with frame size of 960 */
#define _SRS_AAC_SAMPLE_SIZE 1024

// the mpegts header specifed the video/audio pid.
#define TS_PMT_NUMBER 1
#define TS_PMT_PID 0x1001
#define TS_VIDEO_AVC_PID 0x100
#define TS_AUDIO_AAC_PID 0x101
#define TS_AUDIO_MP3_PID 0x102

string srs_ts_stream2string(SrsTsStream stream)
{
    switch (stream) {
        case SrsTsStreamReserved: return "Reserved";
        case SrsTsStreamAudioMp3: return "MP3";
        case SrsTsStreamAudioAAC: return "AAC";
        case SrsTsStreamAudioAC3: return "AC3";
        case SrsTsStreamAudioDTS: return "AudioDTS";
        case SrsTsStreamVideoH264: return "H.264";
        case SrsTsStreamVideoMpeg4: return "MP4";
        case SrsTsStreamAudioMpeg4: return "MP4A";
        default: return "Other";
    }
}

SrsTsChannel::SrsTsChannel()
{
    pid = 0;
    apply = SrsTsPidApplyReserved;
    stream = SrsTsStreamReserved;
    msg = NULL;
    continuity_counter = 0;
    context = NULL;
}

SrsTsChannel::~SrsTsChannel()
{
    srs_freep(msg);
}

SrsTsMessage::SrsTsMessage(SrsTsChannel* c, SrsTsPacket* p)
{
    channel = c;
    packet = p;

    dts = pts = 0;
    sid = (SrsTsPESStreamId)0x00;
    continuity_counter = 0;
    PES_packet_length = 0;
    payload = new SrsSimpleBuffer();
    is_discontinuity = false;

    start_pts = 0;
    write_pcr = false;
}

SrsTsMessage::~SrsTsMessage()
{
    srs_freep(payload);
}

int SrsTsMessage::dump(SrsStream* stream, int* pnb_bytes)
{
    int ret = ERROR_SUCCESS;

    if (stream->empty()) {
        return ret;
    }

    // xB
    int nb_bytes = stream->size() - stream->pos();
    if (PES_packet_length > 0) {
        nb_bytes = srs_min(nb_bytes, PES_packet_length - payload->length());
    }

    if (nb_bytes > 0) {
        if (!stream->require(nb_bytes)) {
            ret = ERROR_STREAM_CASTER_TS_PSE;
            srs_error("ts: dump PSE bytes failed, requires=%dB. ret=%d", nb_bytes, ret);
            return ret;
        }

        payload->append(stream->data() + stream->pos(), nb_bytes);
        stream->skip(nb_bytes);
    }

    *pnb_bytes = nb_bytes;

    return ret;
}

bool SrsTsMessage::completed(int8_t payload_unit_start_indicator)
{
    if (PES_packet_length == 0) {
        return payload_unit_start_indicator;
    }
    return payload->length() >= PES_packet_length;
}

bool SrsTsMessage::fresh()
{
    return payload->length() == 0;
}

bool SrsTsMessage::is_audio()
{
    return ((sid >> 5) & 0x07) == SrsTsPESStreamIdAudioChecker;
}

bool SrsTsMessage::is_video()
{
    return ((sid >> 4) & 0x0f) == SrsTsPESStreamIdVideoChecker;
}

int SrsTsMessage::stream_number()
{
    if (is_audio()) {
        return sid & 0x1f;
    } else if (is_video()) {
        return sid & 0x0f;
    }
    return -1;
}

SrsTsMessage* SrsTsMessage::detach()
{
    // @remark the packet cannot be used, but channel is ok.
    SrsTsMessage* cp = new SrsTsMessage(channel, NULL);
    cp->start_pts = start_pts;
    cp->write_pcr = write_pcr;
    cp->is_discontinuity = is_discontinuity;
    cp->dts = dts;
    cp->pts = pts;
    cp->sid = sid;
    cp->PES_packet_length = PES_packet_length;
    cp->continuity_counter = continuity_counter;
    cp->payload = payload;
    payload = NULL;
    return cp;
}

ISrsTsHandler::ISrsTsHandler()
{
}

ISrsTsHandler::~ISrsTsHandler()
{
}

SrsTsContext::SrsTsContext()
{
    pure_audio = false;
    vcodec = SrsCodecVideoReserved;
    acodec = SrsCodecAudioReserved1;
}

SrsTsContext::~SrsTsContext()
{
    std::map<int, SrsTsChannel*>::iterator it;
    for (it = pids.begin(); it != pids.end(); ++it) {
        SrsTsChannel* channel = it->second;
        srs_freep(channel);
    }
    pids.clear();
}

bool SrsTsContext::is_pure_audio()
{
    return pure_audio;
}

void SrsTsContext::on_pmt_parsed()
{
    pure_audio = true;
    
    std::map<int, SrsTsChannel*>::iterator it;
    for (it = pids.begin(); it != pids.end(); ++it) {
        SrsTsChannel* channel = it->second;
        if (channel->apply == SrsTsPidApplyVideo) {
            pure_audio = false;
        }
    }
}

void SrsTsContext::reset()
{
    vcodec = SrsCodecVideoReserved;
    acodec = SrsCodecAudioReserved1;
}

SrsTsChannel* SrsTsContext::get(int pid)
{
    if (pids.find(pid) == pids.end()) {
        return NULL;
    }
    return pids[pid];
}

void SrsTsContext::set(int pid, SrsTsPidApply apply_pid, SrsTsStream stream)
{
    SrsTsChannel* channel = NULL;

    if (pids.find(pid) == pids.end()) {
        channel = new SrsTsChannel();
        channel->context = this;
        pids[pid] = channel;
    } else {
        channel = pids[pid];
    }

    channel->pid = pid;
    channel->apply = apply_pid;
    channel->stream = stream;
}

int SrsTsContext::decode(SrsStream* stream, ISrsTsHandler* handler)
{
    int ret = ERROR_SUCCESS;

    // parse util EOF of stream.
    // for example, parse multiple times for the PES_packet_length(0) packet.
    while (!stream->empty()) {
        SrsTsPacket* packet = new SrsTsPacket(this);
        SrsAutoFree(SrsTsPacket, packet);

        SrsTsMessage* msg = NULL;
        if ((ret = packet->decode(stream, &msg)) != ERROR_SUCCESS) {
            srs_error("mpegts: decode ts packet failed. ret=%d", ret);
            return ret;
        }

        if (!msg) {
            continue;
        }
        SrsAutoFree(SrsTsMessage, msg);

        if ((ret = handler->on_ts_message(msg)) != ERROR_SUCCESS) {
            srs_error("mpegts: handler ts message failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

int SrsTsContext::encode(SrsFileWriter* writer, SrsTsMessage* msg, SrsCodecVideo vc, SrsCodecAudio ac)
{
    int ret = ERROR_SUCCESS;

    SrsTsStream vs, as;
    int16_t video_pid = 0, audio_pid = 0;
    switch (vc) {
        case SrsCodecVideoAVC: 
            vs = SrsTsStreamVideoH264; 
            video_pid = TS_VIDEO_AVC_PID;
            break;
        case SrsCodecVideoDisabled:
            vs = SrsTsStreamReserved;
            break;
        case SrsCodecVideoReserved:
        case SrsCodecVideoReserved1:
        case SrsCodecVideoReserved2:
        case SrsCodecVideoSorensonH263:
        case SrsCodecVideoScreenVideo:
        case SrsCodecVideoOn2VP6:
        case SrsCodecVideoOn2VP6WithAlphaChannel:
        case SrsCodecVideoScreenVideoVersion2:
            vs = SrsTsStreamReserved;
            break;
    }
    switch (ac) {
        case SrsCodecAudioAAC:
            as = SrsTsStreamAudioAAC; 
            audio_pid = TS_AUDIO_AAC_PID;
            break;
        case SrsCodecAudioMP3:
            as = SrsTsStreamAudioMp3; 
            audio_pid = TS_AUDIO_MP3_PID;
            break;
        case SrsCodecAudioDisabled:
            as = SrsTsStreamReserved;
            break;
        case SrsCodecAudioReserved1:
        case SrsCodecAudioLinearPCMPlatformEndian:
        case SrsCodecAudioADPCM:
        case SrsCodecAudioLinearPCMLittleEndian:
        case SrsCodecAudioNellymoser16kHzMono:
        case SrsCodecAudioNellymoser8kHzMono:
        case SrsCodecAudioNellymoser:
        case SrsCodecAudioReservedG711AlawLogarithmicPCM:
        case SrsCodecAudioReservedG711MuLawLogarithmicPCM:
        case SrsCodecAudioReserved:
        case SrsCodecAudioSpeex:
        case SrsCodecAudioReservedMP3_8kHz:
        case SrsCodecAudioReservedDeviceSpecificSound:
            as = SrsTsStreamReserved;
            break;
    }
    
    if (as == SrsTsStreamReserved && vs == SrsTsStreamReserved) {
        ret = ERROR_HLS_NO_STREAM;
        srs_error("hls: no video or audio stream, vcodec=%d, acodec=%d. ret=%d", vc, ac, ret);
        return ret;
    }
    
    // when any codec changed, write PAT/PMT table.
    if (vcodec != vc || acodec != ac) {
        vcodec = vc;
        acodec = ac;
        if ((ret = encode_pat_pmt(writer, video_pid, vs, audio_pid, as)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // encode the media frame to PES packets over TS.
    if (msg->is_audio()) {
        return encode_pes(writer, msg, audio_pid, as, vs == SrsTsStreamReserved);
    } else {
        return encode_pes(writer, msg, video_pid, vs, vs == SrsTsStreamReserved);
    }
}

int SrsTsContext::encode_pat_pmt(SrsFileWriter* writer, int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as)
{
    int ret = ERROR_SUCCESS;
    
    if (vs != SrsTsStreamVideoH264 && as != SrsTsStreamAudioAAC && as != SrsTsStreamAudioMp3) {
        ret = ERROR_HLS_NO_STREAM;
        srs_error("hls: no pmt pcr pid, vs=%d, as=%d. ret=%d", vs, as, ret);
        return ret;
    }

    int16_t pmt_number = TS_PMT_NUMBER;
    int16_t pmt_pid = TS_PMT_PID;
    if (true) {
        SrsTsPacket* pkt = SrsTsPacket::create_pat(this, pmt_number, pmt_pid);
        SrsAutoFree(SrsTsPacket, pkt);

        char* buf = new char[SRS_TS_PACKET_SIZE];
        SrsAutoFreeA(char, buf);

        // set the left bytes with 0xFF.
        int nb_buf = pkt->size();
        srs_assert(nb_buf < SRS_TS_PACKET_SIZE);
        memset(buf + nb_buf, 0xFF, SRS_TS_PACKET_SIZE - nb_buf);

        SrsStream stream;
        if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
            return ret;
        }
        if ((ret = pkt->encode(&stream)) != ERROR_SUCCESS) {
            srs_error("ts encode ts packet failed. ret=%d", ret);
            return ret;
        }
        if ((ret = writer->write(buf, SRS_TS_PACKET_SIZE, NULL)) != ERROR_SUCCESS) {
            srs_error("ts write ts packet failed. ret=%d", ret);
            return ret;
        }
    }
    if (true) {
        SrsTsPacket* pkt = SrsTsPacket::create_pmt(this, pmt_number, pmt_pid, vpid, vs, apid, as);
        SrsAutoFree(SrsTsPacket, pkt);

        char* buf = new char[SRS_TS_PACKET_SIZE];
        SrsAutoFreeA(char, buf);

        // set the left bytes with 0xFF.
        int nb_buf = pkt->size();
        srs_assert(nb_buf < SRS_TS_PACKET_SIZE);
        memset(buf + nb_buf, 0xFF, SRS_TS_PACKET_SIZE - nb_buf);

        SrsStream stream;
        if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
            return ret;
        }
        if ((ret = pkt->encode(&stream)) != ERROR_SUCCESS) {
            srs_error("ts encode ts packet failed. ret=%d", ret);
            return ret;
        }
        if ((ret = writer->write(buf, SRS_TS_PACKET_SIZE, NULL)) != ERROR_SUCCESS) {
            srs_error("ts write ts packet failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

int SrsTsContext::encode_pes(SrsFileWriter* writer, SrsTsMessage* msg, int16_t pid, SrsTsStream sid, bool pure_audio)
{
    int ret = ERROR_SUCCESS;

    if (msg->payload->length() == 0) {
        return ret;
    }

    if (sid != SrsTsStreamVideoH264 && sid != SrsTsStreamAudioMp3 && sid != SrsTsStreamAudioAAC) {
        srs_info("ts: ignore the unknown stream, sid=%d", sid);
        return ret;
    }

    SrsTsChannel* channel = get(pid);
    srs_assert(channel);

    char* start = msg->payload->bytes();
    char* end = start + msg->payload->length();
    char* p = start;

    while (p < end) {
        SrsTsPacket* pkt = NULL;
        if (p == start) {
            // write pcr according to message.
            bool write_pcr = msg->write_pcr;
            
            // for pure audio, always write pcr.
            // TODO: FIXME: maybe only need to write at begin and end of ts.
            if (pure_audio && msg->is_audio()) {
                write_pcr = true;
            }

            // it's ok to set pcr equals to dts,
            // @see https://github.com/ossrs/srs/issues/311
            // Fig. 3.18. Program Clock Reference of Digital-Video-and-Audio-Broadcasting-Technology, page 65
            // In MPEG-2, these are the "Program Clock Refer- ence" (PCR) values which are
            // nothing else than an up-to-date copy of the STC counter fed into the transport
            // stream at a certain time. The data stream thus carries an accurate internal
            // "clock time". All coding and de- coding processes are controlled by this clock
            // time. To do this, the receiver, i.e. the MPEG decoder, must read out the
            // "clock time", namely the PCR values, and compare them with its own internal
            // system clock, that is to say its own 42 bit counter.
            int64_t pcr = write_pcr? msg->dts : -1;
            
            // TODO: FIXME: finger it why use discontinuity of msg.
            pkt = SrsTsPacket::create_pes_first(this, 
                pid, msg->sid, channel->continuity_counter++, msg->is_discontinuity,
                pcr, msg->dts, msg->pts, msg->payload->length()
            );
        } else {
            pkt = SrsTsPacket::create_pes_continue(this, 
                pid, msg->sid, channel->continuity_counter++
            );
        }
        SrsAutoFree(SrsTsPacket, pkt);

        char* buf = new char[SRS_TS_PACKET_SIZE];
        SrsAutoFreeA(char, buf);

        // set the left bytes with 0xFF.
        int nb_buf = pkt->size();
        srs_assert(nb_buf < SRS_TS_PACKET_SIZE);

        int left = (int)srs_min(end - p, SRS_TS_PACKET_SIZE - nb_buf);
        int nb_stuffings = SRS_TS_PACKET_SIZE - nb_buf - left;
        if (nb_stuffings > 0) {
            // set all bytes to stuffings.
            memset(buf, 0xFF, SRS_TS_PACKET_SIZE);

            // padding with stuffings.
            pkt->padding(nb_stuffings);

            // size changed, recalc it.
            nb_buf = pkt->size();
            srs_assert(nb_buf < SRS_TS_PACKET_SIZE);

            left = (int)srs_min(end - p, SRS_TS_PACKET_SIZE - nb_buf);
            nb_stuffings = SRS_TS_PACKET_SIZE - nb_buf - left;
            srs_assert(nb_stuffings == 0);
        }
        memcpy(buf + nb_buf, p, left);
        p += left;

        SrsStream stream;
        if ((ret = stream.initialize(buf, nb_buf)) != ERROR_SUCCESS) {
            return ret;
        }
        if ((ret = pkt->encode(&stream)) != ERROR_SUCCESS) {
            srs_error("ts encode ts packet failed. ret=%d", ret);
            return ret;
        }
        if ((ret = writer->write(buf, SRS_TS_PACKET_SIZE, NULL)) != ERROR_SUCCESS) {
            srs_error("ts write ts packet failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

SrsTsPacket::SrsTsPacket(SrsTsContext* c)
{
    context = c;

    sync_byte = 0;
    transport_error_indicator = 0;
    payload_unit_start_indicator = 0;
    transport_priority = 0;
    pid = SrsTsPidPAT;
    transport_scrambling_control = SrsTsScrambledDisabled;
    adaption_field_control = SrsTsAdaptationFieldTypeReserved;
    continuity_counter = 0;
    adaptation_field = NULL;
    payload = NULL;
}

SrsTsPacket::~SrsTsPacket()
{
    srs_freep(adaptation_field);
    srs_freep(payload);
}

int SrsTsPacket::decode(SrsStream* stream, SrsTsMessage** ppmsg)
{
    int ret = ERROR_SUCCESS;

    int pos = stream->pos();

    // 4B ts packet header.
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_HEADER;
        srs_error("ts: demux header failed. ret=%d", ret);
        return ret;
    }

    sync_byte = stream->read_1bytes();
    if (sync_byte != 0x47) {
        ret = ERROR_STREAM_CASTER_TS_SYNC_BYTE;
        srs_error("ts: sync_bytes must be 0x47, actual=%#x. ret=%d", sync_byte, ret);
        return ret;
    }
    
    int16_t pidv = stream->read_2bytes();
    transport_error_indicator = (pidv >> 15) & 0x01;
    payload_unit_start_indicator = (pidv >> 14) & 0x01;
    transport_priority = (pidv >> 13) & 0x01;
    pid = (SrsTsPid)(pidv & 0x1FFF);

    int8_t ccv = stream->read_1bytes();
    transport_scrambling_control = (SrsTsScrambled)((ccv >> 6) & 0x03);
    adaption_field_control = (SrsTsAdaptationFieldType)((ccv >> 4) & 0x03);
    continuity_counter = ccv & 0x0F;

    // TODO: FIXME: create pids map when got new pid.
    
    srs_info("ts: header sync=%#x error=%d unit_start=%d priotiry=%d pid=%d scrambling=%d adaption=%d counter=%d",
        sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
        transport_scrambling_control, adaption_field_control, continuity_counter);

    // optional: adaptation field
    if (adaption_field_control == SrsTsAdaptationFieldTypeAdaptionOnly || adaption_field_control == SrsTsAdaptationFieldTypeBoth) {
        srs_freep(adaptation_field);
        adaptation_field = new SrsTsAdaptationField(this);

        if ((ret = adaptation_field->decode(stream)) != ERROR_SUCCESS) {
            srs_error("ts: demux af faield. ret=%d", ret);
            return ret;
        }
        srs_verbose("ts: demux af ok.");
    }

    // calc the user defined data size for payload.
    int nb_payload = SRS_TS_PACKET_SIZE - (stream->pos() - pos);

    // optional: payload.
    if (adaption_field_control == SrsTsAdaptationFieldTypePayloadOnly || adaption_field_control == SrsTsAdaptationFieldTypeBoth) {
        if (pid == SrsTsPidPAT) {
            // 2.4.4.3 Program association Table
            srs_freep(payload);
            payload = new SrsTsPayloadPAT(this);
        } else {
            SrsTsChannel* channel = context->get(pid);
            if (channel && channel->apply == SrsTsPidApplyPMT) {
                // 2.4.4.8 Program Map Table
                srs_freep(payload);
                payload = new SrsTsPayloadPMT(this);
            } else if (channel && (channel->apply == SrsTsPidApplyVideo || channel->apply == SrsTsPidApplyAudio)) {
                // 2.4.3.6 PES packet
                srs_freep(payload);
                payload = new SrsTsPayloadPES(this);
            } else {
                // left bytes as reserved.
                stream->skip(nb_payload);
            }
        }

        if (payload && (ret = payload->decode(stream, ppmsg)) != ERROR_SUCCESS) {
            srs_error("ts: demux payload failed. ret=%d", ret);
            return ret;
        }
    }

    return ret;
}

int SrsTsPacket::size()
{
    int sz = 4;
    
    sz += adaptation_field? adaptation_field->size() : 0;
    sz += payload? payload->size() : 0;

    return sz;
}

int SrsTsPacket::encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // 4B ts packet header.
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_HEADER;
        srs_error("ts: mux header failed. ret=%d", ret);
        return ret;
    }

    stream->write_1bytes(sync_byte);

    int16_t pidv = pid & 0x1FFF;
    pidv |= (transport_priority << 13) & 0x2000;
    pidv |= (transport_error_indicator << 15) & 0x8000;
    pidv |= (payload_unit_start_indicator << 14) & 0x4000;
    stream->write_2bytes(pidv);

    int8_t ccv = continuity_counter & 0x0F;
    ccv |= (transport_scrambling_control << 6) & 0xC0;
    ccv |= (adaption_field_control << 4) & 0x30;
    stream->write_1bytes(ccv);
    
    srs_info("ts: header sync=%#x error=%d unit_start=%d priotiry=%d pid=%d scrambling=%d adaption=%d counter=%d",
        sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
        transport_scrambling_control, adaption_field_control, continuity_counter);

    // optional: adaptation field
    if (adaptation_field) {
        if ((ret = adaptation_field->encode(stream)) != ERROR_SUCCESS) {
            srs_error("ts: mux af faield. ret=%d", ret);
            return ret;
        }
        srs_verbose("ts: mux af ok.");
    }

    // optional: payload.
    if (payload) {
        if ((ret = payload->encode(stream)) != ERROR_SUCCESS) {
            srs_error("ts: mux payload failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("ts: mux payload ok.");
    }

    return ret;
}

void SrsTsPacket::padding(int nb_stuffings)
{
    if (!adaptation_field) {
        SrsTsAdaptationField* af = new SrsTsAdaptationField(this);
        adaptation_field = af;

        af->adaption_field_length = 0; // calc in size.
        af->discontinuity_indicator = 0;
        af->random_access_indicator = 0;
        af->elementary_stream_priority_indicator = 0;
        af->PCR_flag = 0;
        af->OPCR_flag = 0;
        af->splicing_point_flag = 0;
        af->transport_private_data_flag = 0;
        af->adaptation_field_extension_flag = 0;

        // consume the af size if possible.
        nb_stuffings = srs_max(0, nb_stuffings - af->size());
    }

    adaptation_field->nb_af_reserved = nb_stuffings;

    // set payload with af.
    if (adaption_field_control == SrsTsAdaptationFieldTypePayloadOnly) {
        adaption_field_control = SrsTsAdaptationFieldTypeBoth;
    }
}

SrsTsPacket* SrsTsPacket::create_pat(SrsTsContext* context, int16_t pmt_number, int16_t pmt_pid)
{
    SrsTsPacket* pkt = new SrsTsPacket(context);
    pkt->sync_byte = 0x47;
    pkt->transport_error_indicator = 0;
    pkt->payload_unit_start_indicator = 1;
    pkt->transport_priority = 0;
    pkt->pid = SrsTsPidPAT;
    pkt->transport_scrambling_control = SrsTsScrambledDisabled;
    pkt->adaption_field_control = SrsTsAdaptationFieldTypePayloadOnly;
    pkt->continuity_counter = 0;
    pkt->adaptation_field = NULL;
    SrsTsPayloadPAT* pat = new SrsTsPayloadPAT(pkt);
    pkt->payload = pat;

    pat->pointer_field = 0;
    pat->table_id = SrsTsPsiIdPas;
    pat->section_syntax_indicator = 1;
    pat->section_length = 0; // calc in size.
    pat->transport_stream_id = 1;
    pat->version_number = 0;
    pat->current_next_indicator = 1;
    pat->section_number = 0;
    pat->last_section_number = 0;
    pat->programs.push_back(new SrsTsPayloadPATProgram(pmt_number, pmt_pid));
    pat->CRC_32 = 0; // calc in encode.
    return pkt;
}

SrsTsPacket* SrsTsPacket::create_pmt(SrsTsContext* context, int16_t pmt_number, int16_t pmt_pid, int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as)
{
    SrsTsPacket* pkt = new SrsTsPacket(context);
    pkt->sync_byte = 0x47;
    pkt->transport_error_indicator = 0;
    pkt->payload_unit_start_indicator = 1;
    pkt->transport_priority = 0;
    pkt->pid = (SrsTsPid)pmt_pid;
    pkt->transport_scrambling_control = SrsTsScrambledDisabled;
    pkt->adaption_field_control = SrsTsAdaptationFieldTypePayloadOnly;
    // TODO: FIXME: maybe should continuous in channel.
    pkt->continuity_counter = 0;
    pkt->adaptation_field = NULL;
    SrsTsPayloadPMT* pmt = new SrsTsPayloadPMT(pkt);
    pkt->payload = pmt;

    pmt->pointer_field = 0;
    pmt->table_id = SrsTsPsiIdPms;
    pmt->section_syntax_indicator = 1;
    pmt->section_length = 0; // calc in size.
    pmt->program_number = pmt_number;
    pmt->version_number = 0;
    pmt->current_next_indicator = 1;
    pmt->section_number = 0;
    pmt->last_section_number = 0;
    pmt->program_info_length = 0;
    
    // must got one valid codec.
    srs_assert(vs == SrsTsStreamVideoH264 || as == SrsTsStreamAudioAAC || as == SrsTsStreamAudioMp3);
    
    // if mp3 or aac specified, use audio to carry pcr.
    if (as == SrsTsStreamAudioAAC || as == SrsTsStreamAudioMp3) {
        // use audio to carray pcr by default.
        // for hls, there must be atleast one audio channel.
        pmt->PCR_PID = apid;
        pmt->infos.push_back(new SrsTsPayloadPMTESInfo(as, apid));
    }
    
    // if h.264 specified, use video to carry pcr.
    if (vs == SrsTsStreamVideoH264) {
        pmt->PCR_PID = vpid;
        pmt->infos.push_back(new SrsTsPayloadPMTESInfo(vs, vpid));
    }
    
    pmt->CRC_32 = 0; // calc in encode.
    return pkt;
}

SrsTsPacket* SrsTsPacket::create_pes_first(SrsTsContext* context, 
    int16_t pid, SrsTsPESStreamId sid, u_int8_t continuity_counter, bool discontinuity, 
    int64_t pcr, int64_t dts, int64_t pts, int size
) {
    SrsTsPacket* pkt = new SrsTsPacket(context);
    pkt->sync_byte = 0x47;
    pkt->transport_error_indicator = 0;
    pkt->payload_unit_start_indicator = 1;
    pkt->transport_priority = 0;
    pkt->pid = (SrsTsPid)pid;
    pkt->transport_scrambling_control = SrsTsScrambledDisabled;
    pkt->adaption_field_control = SrsTsAdaptationFieldTypePayloadOnly;
    pkt->continuity_counter = continuity_counter;
    pkt->adaptation_field = NULL;
    SrsTsPayloadPES* pes = new SrsTsPayloadPES(pkt);
    pkt->payload = pes;

    if (pcr >= 0) {
        SrsTsAdaptationField* af = new SrsTsAdaptationField(pkt);
        pkt->adaptation_field = af;
        pkt->adaption_field_control = SrsTsAdaptationFieldTypeBoth;

        af->adaption_field_length = 0; // calc in size.
        af->discontinuity_indicator = discontinuity;
        af->random_access_indicator = 0;
        af->elementary_stream_priority_indicator = 0;
        af->PCR_flag = 1;
        af->OPCR_flag = 0;
        af->splicing_point_flag = 0;
        af->transport_private_data_flag = 0;
        af->adaptation_field_extension_flag = 0;
        af->program_clock_reference_base = pcr;
        af->program_clock_reference_extension = 0;
    }

    pes->packet_start_code_prefix = 0x01;
    pes->stream_id = (u_int8_t)sid;
    pes->PES_packet_length = (size > 0xFFFF)? 0:size;
    pes->PES_scrambling_control = 0;
    pes->PES_priority = 0;
    pes->data_alignment_indicator = 0;
    pes->copyright = 0;
    pes->original_or_copy = 0;
    pes->PTS_DTS_flags = (dts == pts)? 0x02:0x03;
    pes->ESCR_flag = 0;
    pes->ES_rate_flag = 0;
    pes->DSM_trick_mode_flag = 0;
    pes->additional_copy_info_flag = 0;
    pes->PES_CRC_flag = 0;
    pes->PES_extension_flag = 0;
    pes->PES_header_data_length = 0; // calc in size.
    pes->pts = pts;
    pes->dts = dts;
    return pkt;
}

SrsTsPacket* SrsTsPacket::create_pes_continue(SrsTsContext* context, 
    int16_t pid, SrsTsPESStreamId sid, u_int8_t continuity_counter
) {
    SrsTsPacket* pkt = new SrsTsPacket(context);
    pkt->sync_byte = 0x47;
    pkt->transport_error_indicator = 0;
    pkt->payload_unit_start_indicator = 0;
    pkt->transport_priority = 0;
    pkt->pid = (SrsTsPid)pid;
    pkt->transport_scrambling_control = SrsTsScrambledDisabled;
    pkt->adaption_field_control = SrsTsAdaptationFieldTypePayloadOnly;
    pkt->continuity_counter = continuity_counter;
    pkt->adaptation_field = NULL;
    pkt->payload = NULL;

    return pkt;
}

SrsTsAdaptationField::SrsTsAdaptationField(SrsTsPacket* pkt)
{
    packet = pkt;

    adaption_field_length = 0;
    discontinuity_indicator = 0;
    random_access_indicator = 0;
    elementary_stream_priority_indicator = 0;
    PCR_flag = 0;
    OPCR_flag = 0;
    splicing_point_flag = 0;
    transport_private_data_flag = 0;
    adaptation_field_extension_flag = 0;
    program_clock_reference_base = 0;
    program_clock_reference_extension = 0;
    original_program_clock_reference_base = 0;
    original_program_clock_reference_extension = 0;
    splice_countdown = 0;
    transport_private_data_length = 0;
    transport_private_data = NULL;
    adaptation_field_extension_length = 0;
    ltw_flag = 0;
    piecewise_rate_flag = 0;
    seamless_splice_flag = 0;
    ltw_valid_flag = 0;
    ltw_offset = 0;
    piecewise_rate = 0;
    splice_type = 0;
    DTS_next_AU0 = 0;
    marker_bit0 = 0;
    DTS_next_AU1 = 0;
    marker_bit1 = 0;
    DTS_next_AU2 = 0;
    marker_bit2 = 0;
    nb_af_ext_reserved = 0;
    nb_af_reserved = 0;

    const1_value0 = 0x3F;
    const1_value1 = 0x1F;
    const1_value2 = 0x3F;
}

SrsTsAdaptationField::~SrsTsAdaptationField()
{
    srs_freepa(transport_private_data);
}

int SrsTsAdaptationField::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if (!stream->require(2)) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: demux af failed. ret=%d", ret);
        return ret;
    }
    adaption_field_length = stream->read_1bytes();

    // When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
    // be in the range 0 to 182. 
    if (packet->adaption_field_control == SrsTsAdaptationFieldTypeBoth && adaption_field_length > 182) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: demux af length failed, must in [0, 182], actual=%d. ret=%d", adaption_field_length, ret);
        return ret;
    }
    // When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
    // be 183.
    if (packet->adaption_field_control == SrsTsAdaptationFieldTypeAdaptionOnly && adaption_field_length != 183) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: demux af length failed, must be 183, actual=%d. ret=%d", adaption_field_length, ret);
        return ret;
    }
    
    // no adaptation field.
    if (adaption_field_length == 0) {
        srs_info("ts: demux af empty.");
        return ret;
    }

    // the adaptation field start at here.
    int pos_af = stream->pos();
    int8_t tmpv = stream->read_1bytes();
    
    discontinuity_indicator              =   (tmpv >> 7) & 0x01;
    random_access_indicator              =   (tmpv >> 6) & 0x01;
    elementary_stream_priority_indicator =   (tmpv >> 5) & 0x01;
    PCR_flag                             =   (tmpv >> 4) & 0x01;
    OPCR_flag                            =   (tmpv >> 3) & 0x01;
    splicing_point_flag                  =   (tmpv >> 2) & 0x01;
    transport_private_data_flag          =   (tmpv >> 1) & 0x01;
    adaptation_field_extension_flag      =   tmpv & 0x01;
    
    if (PCR_flag) {
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af PCR_flag failed. ret=%d", ret);
            return ret;
        }

        char* pp = NULL;
        char* p = stream->data() + stream->pos();
        stream->skip(6);

        int64_t pcrv = 0;
        pp = (char*)&pcrv;
        pp[5] = *p++;
        pp[4] = *p++;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        
        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        program_clock_reference_extension = pcrv & 0x1ff;
        const1_value0 = (pcrv >> 9) & 0x3F;
        program_clock_reference_base = (pcrv >> 15) & 0x1ffffffffLL;
    }

    if (OPCR_flag) {
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af OPCR_flag failed. ret=%d", ret);
            return ret;
        }

        char* pp = NULL;
        char* p = stream->data() + stream->pos();
        stream->skip(6);
        
        int64_t opcrv = 0;
        pp = (char*)&opcrv;
        pp[5] = *p++;
        pp[4] = *p++;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;
        
        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        original_program_clock_reference_extension = opcrv & 0x1ff;
        const1_value2 = (opcrv >> 9) & 0x3F;
        original_program_clock_reference_base = (opcrv >> 15) & 0x1ffffffffLL;
    }

    if (splicing_point_flag) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af splicing_point_flag failed. ret=%d", ret);
            return ret;
        }
        splice_countdown = stream->read_1bytes();
    }
    
    if (transport_private_data_flag) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af transport_private_data_flag failed. ret=%d", ret);
            return ret;
        }
        transport_private_data_length = (u_int8_t)stream->read_1bytes();

        if (transport_private_data_length> 0) {
            if (!stream->require(transport_private_data_length)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af transport_private_data_flag failed. ret=%d", ret);
                return ret;
            }
            srs_freepa(transport_private_data);
            transport_private_data = new char[transport_private_data_length];
            stream->read_bytes(transport_private_data, transport_private_data_length);
        }
    }
    
    if (adaptation_field_extension_flag) {
        int pos_af_ext = stream->pos();

        if (!stream->require(2)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af adaptation_field_extension_flag failed. ret=%d", ret);
            return ret;
        }
        adaptation_field_extension_length = (u_int8_t)stream->read_1bytes();
        int8_t ltwfv = stream->read_1bytes();
        
        piecewise_rate_flag = (ltwfv >> 6) & 0x01;
        seamless_splice_flag = (ltwfv >> 5) & 0x01;
        ltw_flag = (ltwfv >> 7) & 0x01;
        const1_value1 = ltwfv & 0x1F;

        if (ltw_flag) {
            if (!stream->require(2)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af ltw_flag failed. ret=%d", ret);
                return ret;
            }
            ltw_offset = stream->read_2bytes();
            
            ltw_valid_flag = (ltw_offset >> 15) &0x01;
            ltw_offset &= 0x7FFF;
        }

        if (piecewise_rate_flag) {
            if (!stream->require(3)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af piecewise_rate_flag failed. ret=%d", ret);
                return ret;
            }
            piecewise_rate = stream->read_3bytes();

            piecewise_rate &= 0x3FFFFF;
        }

        if (seamless_splice_flag) {
            if (!stream->require(5)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: demux af seamless_splice_flag failed. ret=%d", ret);
                return ret;
            }
            marker_bit0 = stream->read_1bytes();
            DTS_next_AU1 = stream->read_2bytes();
            DTS_next_AU2 = stream->read_2bytes();
            
            splice_type = (marker_bit0 >> 4) & 0x0F;
            DTS_next_AU0 = (marker_bit0 >> 1) & 0x07;
            marker_bit0 &= 0x01;
            
            marker_bit1 = DTS_next_AU1 & 0x01;
            DTS_next_AU1 = (DTS_next_AU1 >> 1) & 0x7FFF;
            
            marker_bit2 = DTS_next_AU2 & 0x01;
            DTS_next_AU2 = (DTS_next_AU2 >> 1) & 0x7FFF;
        }

        nb_af_ext_reserved = adaptation_field_extension_length - (stream->pos() - pos_af_ext);
        stream->skip(nb_af_ext_reserved);
    }

    nb_af_reserved = adaption_field_length - (stream->pos() - pos_af);
    stream->skip(nb_af_reserved);
    
    srs_info("ts: af parsed, discontinuity=%d random=%d priority=%d PCR=%d OPCR=%d slicing=%d private=%d extension=%d/%d pcr=%"PRId64"/%d opcr=%"PRId64"/%d",
        discontinuity_indicator, random_access_indicator, elementary_stream_priority_indicator, PCR_flag, OPCR_flag, splicing_point_flag,
        transport_private_data_flag, adaptation_field_extension_flag, adaptation_field_extension_length, program_clock_reference_base, 
        program_clock_reference_extension, original_program_clock_reference_base, original_program_clock_reference_extension);

    return ret;
}

int SrsTsAdaptationField::size()
{
    int sz = 2;

    sz += PCR_flag? 6 : 0;
    sz += OPCR_flag? 6 : 0;
    sz += splicing_point_flag? 1 : 0;
    sz += transport_private_data_flag? 1 + transport_private_data_length : 0;
    sz += adaptation_field_extension_flag? 2 + adaptation_field_extension_length : 0;
    sz += nb_af_ext_reserved;
    sz += nb_af_reserved;

    adaption_field_length = sz - 1;

    return sz;
}

int SrsTsAdaptationField::encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if (!stream->require(2)) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: mux af failed. ret=%d", ret);
        return ret;
    }
    stream->write_1bytes(adaption_field_length);

    // When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
    // be in the range 0 to 182. 
    if (packet->adaption_field_control == SrsTsAdaptationFieldTypeBoth && adaption_field_length > 182) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: mux af length failed, must in [0, 182], actual=%d. ret=%d", adaption_field_length, ret);
        return ret;
    }
    // When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
    // be 183.
    if (packet->adaption_field_control == SrsTsAdaptationFieldTypeAdaptionOnly && adaption_field_length != 183) {
        ret = ERROR_STREAM_CASTER_TS_AF;
        srs_error("ts: mux af length failed, must be 183, actual=%d. ret=%d", adaption_field_length, ret);
        return ret;
    }
    
    // no adaptation field.
    if (adaption_field_length == 0) {
        srs_info("ts: mux af empty.");
        return ret;
    }
    int8_t tmpv = adaptation_field_extension_flag & 0x01;
    tmpv |= (discontinuity_indicator << 7) & 0x80;
    tmpv |= (random_access_indicator << 6) & 0x40;
    tmpv |= (elementary_stream_priority_indicator << 5) & 0x20;
    tmpv |= (PCR_flag << 4) & 0x10;
    tmpv |= (OPCR_flag << 3) & 0x08;
    tmpv |= (splicing_point_flag << 2) & 0x04;
    tmpv |= (transport_private_data_flag << 1) & 0x02;
    stream->write_1bytes(tmpv);
    
    if (PCR_flag) {
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: mux af PCR_flag failed. ret=%d", ret);
            return ret;
        }

        char* pp = NULL;
        char* p = stream->data() + stream->pos();
        stream->skip(6);
        
        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        int64_t pcrv = program_clock_reference_extension & 0x1ff;
        pcrv |= (const1_value0 << 9) & 0x7E00;
        pcrv |= (program_clock_reference_base << 15) & 0xFFFFFFFF8000LL;

        pp = (char*)&pcrv;
        *p++ = pp[5];
        *p++ = pp[4];
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }

    if (OPCR_flag) {
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: demux af OPCR_flag failed. ret=%d", ret);
            return ret;
        }
        stream->skip(6);
        srs_warn("ts: mux af ignore OPCR");
    }

    if (splicing_point_flag) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: mux af splicing_point_flag failed. ret=%d", ret);
            return ret;
        }
        stream->write_1bytes(splice_countdown);
    }
    
    if (transport_private_data_flag) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: mux af transport_private_data_flag failed. ret=%d", ret);
            return ret;
        }
        stream->write_1bytes(transport_private_data_length);

        if (transport_private_data_length> 0) {
            if (!stream->require(transport_private_data_length)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: mux af transport_private_data_flag failed. ret=%d", ret);
                return ret;
            }
            stream->write_bytes(transport_private_data, transport_private_data_length);
        }
    }
    
    if (adaptation_field_extension_flag) {
        if (!stream->require(2)) {
            ret = ERROR_STREAM_CASTER_TS_AF;
            srs_error("ts: mux af adaptation_field_extension_flag failed. ret=%d", ret);
            return ret;
        }
        stream->write_1bytes(adaptation_field_extension_length);
        int8_t ltwfv = const1_value1 & 0x1F;
        ltwfv |= (ltw_flag << 7) & 0x80;
        ltwfv |= (piecewise_rate_flag << 6) & 0x40;
        ltwfv |= (seamless_splice_flag << 5) & 0x20;
        stream->write_1bytes(ltwfv);

        if (ltw_flag) {
            if (!stream->require(2)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: mux af ltw_flag failed. ret=%d", ret);
                return ret;
            }
            stream->skip(2);
            srs_warn("ts: mux af ignore ltw");
        }

        if (piecewise_rate_flag) {
            if (!stream->require(3)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: mux af piecewise_rate_flag failed. ret=%d", ret);
                return ret;
            }
            stream->skip(3);
            srs_warn("ts: mux af ignore piecewise_rate");
        }

        if (seamless_splice_flag) {
            if (!stream->require(5)) {
                ret = ERROR_STREAM_CASTER_TS_AF;
                srs_error("ts: mux af seamless_splice_flag failed. ret=%d", ret);
                return ret;
            }
            stream->skip(5);
            srs_warn("ts: mux af ignore seamless_splice");
        }

        if (nb_af_ext_reserved) {
            stream->skip(nb_af_ext_reserved);
        }
    }

    if (nb_af_reserved) {
        stream->skip(nb_af_reserved);
    }
    
    srs_info("ts: af parsed, discontinuity=%d random=%d priority=%d PCR=%d OPCR=%d slicing=%d private=%d extension=%d/%d pcr=%"PRId64"/%d opcr=%"PRId64"/%d",
        discontinuity_indicator, random_access_indicator, elementary_stream_priority_indicator, PCR_flag, OPCR_flag, splicing_point_flag,
        transport_private_data_flag, adaptation_field_extension_flag, adaptation_field_extension_length, program_clock_reference_base, 
        program_clock_reference_extension, original_program_clock_reference_base, original_program_clock_reference_extension);

    return ret;
}

SrsTsPayload::SrsTsPayload(SrsTsPacket* p)
{
    packet = p;
}

SrsTsPayload::~SrsTsPayload()
{
}

SrsTsPayloadPES::SrsTsPayloadPES(SrsTsPacket* p) : SrsTsPayload(p)
{
    PES_private_data = NULL;
    pack_field = NULL;
    PES_extension_field = NULL;
    nb_stuffings = 0;
    nb_bytes = 0;
    nb_paddings = 0;
    const2bits = 0x02;
    const1_value0 = 0x07;
}

SrsTsPayloadPES::~SrsTsPayloadPES()
{
    srs_freepa(PES_private_data);
    srs_freepa(pack_field);
    srs_freepa(PES_extension_field);
}

int SrsTsPayloadPES::decode(SrsStream* stream, SrsTsMessage** ppmsg)
{
    int ret = ERROR_SUCCESS;

    // find the channel from chunk.
    SrsTsChannel* channel = packet->context->get(packet->pid);
    if (!channel) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: demux PES no channel for pid=%#x. ret=%d", packet->pid, ret);
        return ret;
    }

    // init msg.
    SrsTsMessage* msg = channel->msg;
    if (!msg) {
        msg = new SrsTsMessage(channel, packet);
        channel->msg = msg;
    }
    
    // we must cache the fresh state of msg,
    // for the PES_packet_length is 0, the first payload_unit_start_indicator always 1,
    // so should check for the fresh and not completed it.
    bool is_fresh_msg = msg->fresh();

    // check when fresh, the payload_unit_start_indicator
    // should be 1 for the fresh msg.
    if (is_fresh_msg && !packet->payload_unit_start_indicator) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: PES fresh packet length=%d, us=%d, cc=%d. ret=%d",
            msg->PES_packet_length, packet->payload_unit_start_indicator, packet->continuity_counter,
            ret);
        return ret;
    }

    // check when not fresh and PES_packet_length>0,
    // the payload_unit_start_indicator should never be 1 when not completed.
    if (!is_fresh_msg && msg->PES_packet_length > 0
        && !msg->completed(packet->payload_unit_start_indicator)
        && packet->payload_unit_start_indicator
    ) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: PES packet length=%d, payload=%d, us=%d, cc=%d. ret=%d",
            msg->PES_packet_length, msg->payload->length(), packet->payload_unit_start_indicator, 
            packet->continuity_counter, ret);

        // reparse current msg.
        stream->skip(stream->pos() * -1);
        srs_freep(msg);
        channel->msg = NULL;
        return ERROR_SUCCESS;
    }

    // check the continuity counter
    if (!is_fresh_msg) {
        // late-incoming or duplicated continuity, drop message.
        // @remark check overflow, the counter plus 1 should greater when invalid.
        if (msg->continuity_counter >= packet->continuity_counter
            && ((msg->continuity_counter + 1) & 0x0f) > packet->continuity_counter
        ) {
            srs_warn("ts: drop PES %dB for duplicated cc=%#x", msg->continuity_counter);
            stream->skip(stream->size() - stream->pos());
            return ret;
        }

        // when got partially message, the continous count must be continuous, or drop it.
        if (((msg->continuity_counter + 1) & 0x0f) != packet->continuity_counter) {
            ret = ERROR_STREAM_CASTER_TS_PSE;
            srs_error("ts: continuity must be continous, msg=%#x, packet=%#x. ret=%d",
                msg->continuity_counter, packet->continuity_counter, ret);

            // reparse current msg.
            stream->skip(stream->pos() * -1);
            srs_freep(msg);
            channel->msg = NULL;
            return ERROR_SUCCESS;
        }
    }
    msg->continuity_counter = packet->continuity_counter;

    // for the PES_packet_length(0), reap when completed.
    if (!is_fresh_msg && msg->completed(packet->payload_unit_start_indicator)) {
        // reap previous PES packet.
        *ppmsg = msg;
        channel->msg = NULL;

        // reparse current msg.
        stream->skip(stream->pos() * -1);
        return ret;
    }

    // contious packet, append bytes for unit start is 0
    if (!packet->payload_unit_start_indicator) {
        if ((ret = msg->dump(stream, &nb_bytes)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // when unit start, parse the fresh msg.
    if (packet->payload_unit_start_indicator) {
        // 6B fixed header.
        if (!stream->require(6)) {
            ret = ERROR_STREAM_CASTER_TS_PSE;
            srs_error("ts: demux PSE failed. ret=%d", ret);
            return ret;
        }
        // 3B
        packet_start_code_prefix = stream->read_3bytes();
        // 1B
        stream_id = stream->read_1bytes();
        // 2B
        PES_packet_length = stream->read_2bytes();

        // check the packet start prefix.
        packet_start_code_prefix &= 0xFFFFFF;
        if (packet_start_code_prefix != 0x01) {
            ret = ERROR_STREAM_CASTER_TS_PSE;
            srs_error("ts: demux PES start code failed, expect=0x01, actual=%#x. ret=%d", packet_start_code_prefix, ret);
            return ret;
        }
        int pos_packet = stream->pos();

        // @remark the sid indicates the elementary stream format.
        //      the SrsTsPESStreamIdAudio and SrsTsPESStreamIdVideo is start by 0b110 or 0b1110
        SrsTsPESStreamId sid = (SrsTsPESStreamId)stream_id;
        msg->sid = sid;

        if (sid != SrsTsPESStreamIdProgramStreamMap
            && sid != SrsTsPESStreamIdPaddingStream
            && sid != SrsTsPESStreamIdPrivateStream2
            && sid != SrsTsPESStreamIdEcmStream
            && sid != SrsTsPESStreamIdEmmStream
            && sid != SrsTsPESStreamIdProgramStreamDirectory
            && sid != SrsTsPESStreamIdDsmccStream
            && sid != SrsTsPESStreamIdH2221TypeE
        ) {
            // 3B flags.
            if (!stream->require(3)) {
                ret = ERROR_STREAM_CASTER_TS_PSE;
                srs_error("ts: demux PES flags failed. ret=%d", ret);
                return ret;
            }
            // 1B
            int8_t oocv = stream->read_1bytes();
            // 1B
            int8_t pefv = stream->read_1bytes();
            // 1B
            PES_header_data_length = stream->read_1bytes();
            // position of header start.
            int pos_header = stream->pos();

            const2bits = (oocv >> 6) & 0x03;
            PES_scrambling_control = (oocv >> 4) & 0x03;
            PES_priority = (oocv >> 3) & 0x01;
            data_alignment_indicator = (oocv >> 2) & 0x01;
            copyright = (oocv >> 1) & 0x01;
            original_or_copy = oocv & 0x01;

            PTS_DTS_flags = (pefv >> 6) & 0x03;
            ESCR_flag = (pefv >> 5) & 0x01;
            ES_rate_flag = (pefv >> 4) & 0x01;
            DSM_trick_mode_flag = (pefv >> 3) & 0x01;
            additional_copy_info_flag = (pefv >> 2) & 0x01;
            PES_CRC_flag = (pefv >> 1) & 0x01;
            PES_extension_flag = pefv & 0x01;

            // check required together.
            int nb_required = 0;
            nb_required += (PTS_DTS_flags == 0x2)? 5:0;
            nb_required += (PTS_DTS_flags == 0x3)? 10:0;
            nb_required += ESCR_flag? 6:0;
            nb_required += ES_rate_flag? 3:0;
            nb_required += DSM_trick_mode_flag? 1:0;
            nb_required += additional_copy_info_flag? 1:0;
            nb_required += PES_CRC_flag? 2:0;
            nb_required += PES_extension_flag? 1:0;
            if (!stream->require(nb_required)) {
                ret = ERROR_STREAM_CASTER_TS_PSE;
                srs_error("ts: demux PES payload failed. ret=%d", ret);
                return ret;
            }

            // 5B
            if (PTS_DTS_flags == 0x2) {
                if ((ret = decode_33bits_dts_pts(stream, &pts)) != ERROR_SUCCESS) {
                    return ret;
                }
                dts = pts;

                // update the dts and pts of message.
                msg->dts = dts;
                msg->pts = pts;
            }

            // 10B
            if (PTS_DTS_flags == 0x3) {
                if ((ret = decode_33bits_dts_pts(stream, &pts)) != ERROR_SUCCESS) {
                    return ret;
                }
                if ((ret = decode_33bits_dts_pts(stream, &dts)) != ERROR_SUCCESS) {
                    return ret;
                }

                // check sync, the diff of dts and pts should never greater than 1s.
                if (dts - pts > 90000 || pts - dts > 90000) {
                    srs_warn("ts: sync dts=%"PRId64", pts=%"PRId64, dts, pts);
                }

                // update the dts and pts of message.
                msg->dts = dts;
                msg->pts = pts;
            }

            // 6B
            if (ESCR_flag) {
                ESCR_extension = 0;
                ESCR_base = 0;

                stream->skip(6);
                srs_warn("ts: demux PES, ignore the escr.");
            }

            // 3B
            if (ES_rate_flag) {
                ES_rate = stream->read_3bytes();

                ES_rate = ES_rate >> 1;
                ES_rate &= 0x3FFFFF;
            }

            // 1B
            if (DSM_trick_mode_flag) {
                trick_mode_control = stream->read_1bytes();

                trick_mode_value = trick_mode_control & 0x1f;
                trick_mode_control = (trick_mode_control >> 5) & 0x03;
            }

            // 1B
            if (additional_copy_info_flag) {
                additional_copy_info = stream->read_1bytes();

                additional_copy_info &= 0x7f;
            }

            // 2B
            if (PES_CRC_flag) {
                previous_PES_packet_CRC = stream->read_2bytes();
            }

            // 1B
            if (PES_extension_flag) {
                int8_t efv = stream->read_1bytes();

                PES_private_data_flag = (efv >> 7) & 0x01;
                pack_header_field_flag = (efv >> 6) & 0x01;
                program_packet_sequence_counter_flag = (efv >> 5) & 0x01;
                P_STD_buffer_flag = (efv >> 4) & 0x01;
                const1_value0 = (efv >> 1) & 0x07;
                PES_extension_flag_2 = efv & 0x01;

                nb_required = 0;
                nb_required += PES_private_data_flag? 16:0;
                nb_required += pack_header_field_flag? 1:0; // 1+x bytes.
                nb_required += program_packet_sequence_counter_flag? 2:0;
                nb_required += P_STD_buffer_flag? 2:0;
                nb_required += PES_extension_flag_2? 1:0; // 1+x bytes.
                if (!stream->require(nb_required)) {
                    ret = ERROR_STREAM_CASTER_TS_PSE;
                    srs_error("ts: demux PSE ext payload failed. ret=%d", ret);
                    return ret;
                }

                // 16B
                if (PES_private_data_flag) {
                    srs_freepa(PES_private_data);
                    PES_private_data = new char[16];
                    stream->read_bytes(PES_private_data, 16);
                }

                // (1+x)B
                if (pack_header_field_flag) {
                    pack_field_length = stream->read_1bytes();
                    if (pack_field_length > 0) {
                        // the adjust required bytes.
                        nb_required = nb_required - 16 - 1 + pack_field_length;
                        if (!stream->require(nb_required)) {
                            ret = ERROR_STREAM_CASTER_TS_PSE;
                            srs_error("ts: demux PSE ext pack failed. ret=%d", ret);
                            return ret;
                        }
                        srs_freepa(pack_field);
                        pack_field = new char[pack_field_length];
                        stream->read_bytes(pack_field, pack_field_length);
                    }
                }

                // 2B
                if (program_packet_sequence_counter_flag) {
                    program_packet_sequence_counter = stream->read_1bytes();
                    program_packet_sequence_counter &= 0x7f;

                    original_stuff_length = stream->read_1bytes();
                    MPEG1_MPEG2_identifier = (original_stuff_length >> 6) & 0x01;
                    original_stuff_length &= 0x3f;
                }

                // 2B
                if (P_STD_buffer_flag) {
                    P_STD_buffer_size = stream->read_2bytes();

                    // '01'
                    //int8_t const2bits = (P_STD_buffer_scale >>14) & 0x03;

                    P_STD_buffer_scale = (P_STD_buffer_scale >>13) & 0x01;
                    P_STD_buffer_size &= 0x1FFF;
                }

                // (1+x)B
                if (PES_extension_flag_2) {
                    PES_extension_field_length = stream->read_1bytes();
                    PES_extension_field_length &= 0x07;

                    if (PES_extension_field_length > 0) {
                        if (!stream->require(PES_extension_field_length)) {
                            ret = ERROR_STREAM_CASTER_TS_PSE;
                            srs_error("ts: demux PSE ext field failed. ret=%d", ret);
                            return ret;
                        }
                        srs_freepa(PES_extension_field);
                        PES_extension_field = new char[PES_extension_field_length];
                        stream->read_bytes(PES_extension_field, PES_extension_field_length);
                    }
                }
            }

            // stuffing_byte
            nb_stuffings = PES_header_data_length - (stream->pos() - pos_header);
            if (nb_stuffings > 0) {
                if (!stream->require(nb_stuffings)) {
                    ret = ERROR_STREAM_CASTER_TS_PSE;
                    srs_error("ts: demux PSE stuffings failed. ret=%d", ret);
                    return ret;
                }
                stream->skip(nb_stuffings);
            }

            // PES_packet_data_byte, page58.
            // the packet size contains the header size.
            // The number of PES_packet_data_bytes, N, is specified by the
            // PES_packet_length field. N shall be equal to the value
            // indicated in the PES_packet_length minus the number of bytes
            // between the last byte of the PES_packet_length field and the
            // first PES_packet_data_byte.
            /**
            * when actual packet length > 0xffff(65535),
            * which exceed the max u_int16_t packet length,
            * use 0 packet length, the next unit start indicates the end of packet.
            */
            if (PES_packet_length > 0) {
                int nb_packet = PES_packet_length - (stream->pos() - pos_packet);
                msg->PES_packet_length = srs_max(0, nb_packet);
            }

            // xB
            if ((ret = msg->dump(stream, &nb_bytes)) != ERROR_SUCCESS) {
                return ret;
            }
        } else if (sid == SrsTsPESStreamIdProgramStreamMap
            || sid == SrsTsPESStreamIdPrivateStream2
            || sid == SrsTsPESStreamIdEcmStream
            || sid == SrsTsPESStreamIdEmmStream
            || sid == SrsTsPESStreamIdProgramStreamDirectory
            || sid == SrsTsPESStreamIdDsmccStream
            || sid == SrsTsPESStreamIdH2221TypeE
        ) {
            // for (i = 0; i < PES_packet_length; i++) {
            //         PES_packet_data_byte
            // }

            // xB
            if ((ret = msg->dump(stream, &nb_bytes)) != ERROR_SUCCESS) {
                return ret;
            }
        } else if (sid == SrsTsPESStreamIdPaddingStream) {
            // for (i = 0; i < PES_packet_length; i++) {
            //         padding_byte
            // }
            nb_paddings = stream->size() - stream->pos();
            stream->skip(nb_paddings);
            srs_info("ts: drop %dB padding bytes", nb_paddings);
        } else {
            int nb_drop = stream->size() - stream->pos();
            stream->skip(nb_drop);
            srs_warn("ts: drop the pes packet %dB for stream_id=%#x", nb_drop, stream_id);
        }
    }
    
    // when fresh and the PES_packet_length is 0,
    // the payload_unit_start_indicator always be 1,
    // the message should never EOF for the first packet.
    if (is_fresh_msg && msg->PES_packet_length == 0) {
        return ret;
    }

    // check msg, reap when completed.
    if (msg->completed(packet->payload_unit_start_indicator)) {
        *ppmsg = msg;
        channel->msg = NULL;
        srs_info("ts: reap msg for completed.");
    }

    return ret;
}

int SrsTsPayloadPES::size()
{
    int sz = 0;
    
    PES_header_data_length = 0;
    SrsTsPESStreamId sid = (SrsTsPESStreamId)stream_id;

    if (sid != SrsTsPESStreamIdProgramStreamMap
        && sid != SrsTsPESStreamIdPaddingStream
        && sid != SrsTsPESStreamIdPrivateStream2
        && sid != SrsTsPESStreamIdEcmStream
        && sid != SrsTsPESStreamIdEmmStream
        && sid != SrsTsPESStreamIdProgramStreamDirectory
        && sid != SrsTsPESStreamIdDsmccStream
        && sid != SrsTsPESStreamIdH2221TypeE
    ) {
        sz += 6;
        sz += 3;
        PES_header_data_length = sz;

        sz += (PTS_DTS_flags == 0x2)? 5:0;
        sz += (PTS_DTS_flags == 0x3)? 10:0;
        sz += ESCR_flag? 6:0;
        sz += ES_rate_flag? 3:0;
        sz += DSM_trick_mode_flag? 1:0;
        sz += additional_copy_info_flag? 1:0;
        sz += PES_CRC_flag? 2:0;
        sz += PES_extension_flag? 1:0;

        if (PES_extension_flag) {
            sz += PES_private_data_flag? 16:0;
            sz += pack_header_field_flag? 1 + pack_field_length:0; // 1+x bytes.
            sz += program_packet_sequence_counter_flag? 2:0;
            sz += P_STD_buffer_flag? 2:0;
            sz += PES_extension_flag_2? 1 + PES_extension_field_length:0; // 1+x bytes.
        }
        PES_header_data_length = sz - PES_header_data_length;

        sz += nb_stuffings;

        // packet bytes
    } else if (sid == SrsTsPESStreamIdProgramStreamMap
        || sid == SrsTsPESStreamIdPrivateStream2
        || sid == SrsTsPESStreamIdEcmStream
        || sid == SrsTsPESStreamIdEmmStream
        || sid == SrsTsPESStreamIdProgramStreamDirectory
        || sid == SrsTsPESStreamIdDsmccStream
        || sid == SrsTsPESStreamIdH2221TypeE
    ) {
        // packet bytes
    } else {
        // nb_drop
    }

    return sz;
}

int SrsTsPayloadPES::encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // 6B fixed header.
    if (!stream->require(6)) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: mux PSE failed. ret=%d", ret);
        return ret;
    }

    // 3B
    stream->write_3bytes(packet_start_code_prefix);
    // 1B
    stream->write_1bytes(stream_id);
    // 2B
    // the PES_packet_length is the actual bytes size, the pplv write to ts
    // is the actual bytes plus the header size.
    int32_t pplv = 0;
    if (PES_packet_length > 0) {
        pplv = PES_packet_length + 3 + PES_header_data_length;
        pplv = (pplv > 0xFFFF)? 0 : pplv;
    }
    stream->write_2bytes(pplv);

    // check the packet start prefix.
    packet_start_code_prefix &= 0xFFFFFF;
    if (packet_start_code_prefix != 0x01) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: mux PSE start code failed, expect=0x01, actual=%#x. ret=%d", packet_start_code_prefix, ret);
        return ret;
    }

    // 3B flags.
    if (!stream->require(3)) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: mux PSE flags failed. ret=%d", ret);
        return ret;
    }
    // 1B
    int8_t oocv = original_or_copy & 0x01;
    oocv |= (const2bits << 6) & 0xC0;
    oocv |= (PES_scrambling_control << 4) & 0x30;
    oocv |= (PES_priority << 3) & 0x08;
    oocv |= (data_alignment_indicator << 2) & 0x04;
    oocv |= (copyright << 1) & 0x02;
    stream->write_1bytes(oocv);
    // 1B
    int8_t pefv = PES_extension_flag & 0x01;
    pefv |= (PTS_DTS_flags << 6) & 0xC0;
    pefv |= (ESCR_flag << 5) & 0x20;
    pefv |= (ES_rate_flag << 4) & 0x10;
    pefv |= (DSM_trick_mode_flag << 3) & 0x08;
    pefv |= (additional_copy_info_flag << 2) & 0x04;
    pefv |= (PES_CRC_flag << 1) & 0x02;
    stream->write_1bytes(pefv);
    // 1B
    stream->write_1bytes(PES_header_data_length);

    // check required together.
    int nb_required = 0;
    nb_required += (PTS_DTS_flags == 0x2)? 5:0;
    nb_required += (PTS_DTS_flags == 0x3)? 10:0;
    nb_required += ESCR_flag? 6:0;
    nb_required += ES_rate_flag? 3:0;
    nb_required += DSM_trick_mode_flag? 1:0;
    nb_required += additional_copy_info_flag? 1:0;
    nb_required += PES_CRC_flag? 2:0;
    nb_required += PES_extension_flag? 1:0;
    if (!stream->require(nb_required)) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: mux PSE payload failed. ret=%d", ret);
        return ret;
    }

    // 5B
    if (PTS_DTS_flags == 0x2) {
        if ((ret = encode_33bits_dts_pts(stream, 0x02, pts)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    // 10B
    if (PTS_DTS_flags == 0x3) {
        if ((ret = encode_33bits_dts_pts(stream, 0x03, pts)) != ERROR_SUCCESS) {
            return ret;
        }
        if ((ret = encode_33bits_dts_pts(stream, 0x01, dts)) != ERROR_SUCCESS) {
            return ret;
        }

        // check sync, the diff of dts and pts should never greater than 1s.
        if (dts - pts > 90000 || pts - dts > 90000) {
            srs_warn("ts: sync dts=%"PRId64", pts=%"PRId64, dts, pts);
        }
    }

    // 6B
    if (ESCR_flag) {
        stream->skip(6);
        srs_warn("ts: demux PES, ignore the escr.");
    }

    // 3B
    if (ES_rate_flag) {
        stream->skip(3);
        srs_warn("ts: demux PES, ignore the ES_rate.");
    }

    // 1B
    if (DSM_trick_mode_flag) {
        stream->skip(1);
        srs_warn("ts: demux PES, ignore the DSM_trick_mode.");
    }

    // 1B
    if (additional_copy_info_flag) {
        stream->skip(1);
        srs_warn("ts: demux PES, ignore the additional_copy_info.");
    }

    // 2B
    if (PES_CRC_flag) {
        stream->skip(2);
        srs_warn("ts: demux PES, ignore the PES_CRC.");
    }

    // 1B
    if (PES_extension_flag) {
        int8_t efv = PES_extension_flag_2 & 0x01;
        efv |= (PES_private_data_flag << 7) & 0x80;
        efv |= (pack_header_field_flag << 6) & 0x40;
        efv |= (program_packet_sequence_counter_flag << 5) & 0x20;
        efv |= (P_STD_buffer_flag << 4) & 0x10;
        efv |= (const1_value0 << 1) & 0xE0;
        stream->write_1bytes(efv);

        nb_required = 0;
        nb_required += PES_private_data_flag? 16:0;
        nb_required += pack_header_field_flag? 1+pack_field_length:0; // 1+x bytes.
        nb_required += program_packet_sequence_counter_flag? 2:0;
        nb_required += P_STD_buffer_flag? 2:0;
        nb_required += PES_extension_flag_2? 1+PES_extension_field_length:0; // 1+x bytes.
        if (!stream->require(nb_required)) {
            ret = ERROR_STREAM_CASTER_TS_PSE;
            srs_error("ts: mux PSE ext payload failed. ret=%d", ret);
            return ret;
        }
        stream->skip(nb_required);
        srs_warn("ts: demux PES, ignore the PES_extension.");
    }

    // stuffing_byte
    if (nb_stuffings) {
        stream->skip(nb_stuffings);
        srs_warn("ts: demux PES, ignore the stuffings.");
    }

    return ret;
}

int SrsTsPayloadPES::decode_33bits_dts_pts(SrsStream* stream, int64_t* pv)
{
    int ret = ERROR_SUCCESS;

    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: demux PSE dts/pts failed. ret=%d", ret);
        return ret;
    }

    // decode the 33bits schema.
    // ===========1B
    // 4bits const maybe '0001', '0010' or '0011'.
    // 3bits DTS/PTS [32..30]
    // 1bit const '1'
    int64_t dts_pts_30_32 = stream->read_1bytes();
    if ((dts_pts_30_32 & 0x01) != 0x01) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: demux PSE dts/pts 30-32 failed. ret=%d", ret);
        return ret;
    }
    // @remark, we donot check the high 4bits, maybe '0001', '0010' or '0011'.
    //      so we just ensure the high 4bits is not 0x00.
    if (((dts_pts_30_32 >> 4) & 0x0f) == 0x00) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: demux PSE dts/pts 30-32 failed. ret=%d", ret);
        return ret;
    }
    dts_pts_30_32 = (dts_pts_30_32 >> 1) & 0x07;

    // ===========2B
    // 15bits DTS/PTS [29..15]
    // 1bit const '1'
    int64_t dts_pts_15_29 = stream->read_2bytes();
    if ((dts_pts_15_29 & 0x01) != 0x01) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: demux PSE dts/pts 15-29 failed. ret=%d", ret);
        return ret;
    }
    dts_pts_15_29 = (dts_pts_15_29 >> 1) & 0x7fff;

    // ===========2B
    // 15bits DTS/PTS [14..0]
    // 1bit const '1'
    int64_t dts_pts_0_14 = stream->read_2bytes();
    if ((dts_pts_0_14 & 0x01) != 0x01) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: demux PSE dts/pts 0-14 failed. ret=%d", ret);
        return ret;
    }
    dts_pts_0_14 = (dts_pts_0_14 >> 1) & 0x7fff;

    int64_t v = 0x00;
    v |= (dts_pts_30_32 << 30) & 0x1c0000000LL;
    v |= (dts_pts_15_29 << 15) & 0x3fff8000LL;
    v |= dts_pts_0_14 & 0x7fff;
    *pv = v;

    return ret;
}

int SrsTsPayloadPES::encode_33bits_dts_pts(SrsStream* stream, u_int8_t fb, int64_t v)
{
    int ret = ERROR_SUCCESS;

    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PSE;
        srs_error("ts: mux PSE dts/pts failed. ret=%d", ret);
        return ret;
    }

    char* p = stream->data() + stream->pos();
    stream->skip(5);

    int32_t val = 0;
    
    val = fb << 4 | (((v >> 30) & 0x07) << 1) | 1;
    *p++ = val;
    
    val = (((v >> 15) & 0x7fff) << 1) | 1;
    *p++ = (val >> 8);
    *p++ = val;
    
    val = (((v) & 0x7fff) << 1) | 1;
    *p++ = (val >> 8);
    *p++ = val;

    return ret;
}

SrsTsPayloadPSI::SrsTsPayloadPSI(SrsTsPacket* p) : SrsTsPayload(p)
{
    pointer_field = 0;
    const0_value = 0;
    const1_value = 3;
    CRC_32 = 0;
}

SrsTsPayloadPSI::~SrsTsPayloadPSI()
{
}

int SrsTsPayloadPSI::decode(SrsStream* stream, SrsTsMessage** /*ppmsg*/)
{
    int ret = ERROR_SUCCESS;

    /**
    * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
    * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
    * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
    * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
    * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
    * stream_type 5 (refer to Table 2-29).
    */
    if (packet->payload_unit_start_indicator) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_PSI;
            srs_error("ts: demux PSI failed. ret=%d", ret);
            return ret;
        }
        pointer_field = stream->read_1bytes();
    }

    // to calc the crc32
    char* ppat = stream->data() + stream->pos();
    int pat_pos = stream->pos();

    // atleast 3B for all psi.
    if (!stream->require(3)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: demux PSI failed. ret=%d", ret);
        return ret;
    }
    // 1B
    table_id = (SrsTsPsiId)stream->read_1bytes();
    
    // 2B
    int16_t slv = stream->read_2bytes();
    
    section_syntax_indicator = (slv >> 15) & 0x01;
    const0_value = (slv >> 14) & 0x01;
    const1_value = (slv >> 12) & 0x03;
    section_length = slv & 0x0FFF;

    // no section, ignore.
    if (section_length == 0) {
        srs_warn("ts: demux PAT ignore empty section");
        return ret;
    }

    if (!stream->require(section_length)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: demux PAT section failed. ret=%d", ret);
        return ret;
    }

    // call the virtual method of actual PSI.
    if ((ret = psi_decode(stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // 4B
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: demux PSI crc32 failed. ret=%d", ret);
        return ret;
    }
    CRC_32 = stream->read_4bytes();

    // verify crc32.
    int32_t crc32 = srs_crc32(ppat, stream->pos() - pat_pos - 4);
    if (crc32 != CRC_32) {
        ret = ERROR_STREAM_CASTER_TS_CRC32;
        srs_error("ts: verify PSI crc32 failed. ret=%d", ret);
        return ret;
    }

    // consume left stuffings
    if (!stream->empty()) {
        int nb_stuffings = stream->size() - stream->pos();
        char* stuffing = stream->data() + stream->pos();

        // all stuffing must be 0xff.
        // TODO: FIXME: maybe need to remove the following.
        for (int i = 0; i < nb_stuffings; i++) {
            if ((u_int8_t)stuffing[i] != 0xff) {
                srs_warn("ts: stuff is not 0xff, actual=%#x", stuffing[i]);
                break;
            }
        }

        stream->skip(nb_stuffings);
    }

    return ret;
}

int SrsTsPayloadPSI::size()
{
    int sz = 0;

    // section size is the sl plus the crc32
    section_length = psi_size() + 4;

     sz += packet->payload_unit_start_indicator? 1:0;
     sz += 3;
     sz += section_length;

    return sz;
}

int SrsTsPayloadPSI::encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    if (packet->payload_unit_start_indicator) {
        if (!stream->require(1)) {
            ret = ERROR_STREAM_CASTER_TS_PSI;
            srs_error("ts: mux PSI failed. ret=%d", ret);
            return ret;
        }
        stream->write_1bytes(pointer_field);
    }

    // to calc the crc32
    char* ppat = stream->data() + stream->pos();
    int pat_pos = stream->pos();

    // atleast 3B for all psi.
    if (!stream->require(3)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: mux PSI failed. ret=%d", ret);
        return ret;
    }
    // 1B
    stream->write_1bytes(table_id);
    
    // 2B
    int16_t slv = section_length & 0x0FFF;
    slv |= (section_syntax_indicator << 15) & 0x8000;
    slv |= (const0_value << 14) & 0x4000;
    slv |= (const1_value << 12) & 0x3000;
    stream->write_2bytes(slv);

    // no section, ignore.
    if (section_length == 0) {
        srs_warn("ts: mux PAT ignore empty section");
        return ret;
    }

    if (!stream->require(section_length)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: mux PAT section failed. ret=%d", ret);
        return ret;
    }

    // call the virtual method of actual PSI.
    if ((ret = psi_encode(stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // 4B
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_PSI;
        srs_error("ts: mux PSI crc32 failed. ret=%d", ret);
        return ret;
    }
    CRC_32 = srs_crc32(ppat, stream->pos() - pat_pos);
    stream->write_4bytes(CRC_32);

    return ret;
}

SrsTsPayloadPATProgram::SrsTsPayloadPATProgram(int16_t n, int16_t p)
{
    number = n;
    pid = p;
    const1_value = 0x07;
}

SrsTsPayloadPATProgram::~SrsTsPayloadPATProgram()
{
}

int SrsTsPayloadPATProgram::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 4B for PAT program specified
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_PAT;
        srs_error("ts: demux PAT failed. ret=%d", ret);
        return ret;
    }

    int tmpv = stream->read_4bytes();
    number = (int16_t)((tmpv >> 16) & 0xFFFF);
    const1_value = (int16_t)((tmpv >> 13) & 0x07);
    pid = (int16_t)(tmpv & 0x1FFF);

    return ret;
}

int SrsTsPayloadPATProgram::size()
{
    return 4;
}

int SrsTsPayloadPATProgram::encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 4B for PAT program specified
    if (!stream->require(4)) {
        ret = ERROR_STREAM_CASTER_TS_PAT;
        srs_error("ts: mux PAT failed. ret=%d", ret);
        return ret;
    }

    int tmpv = pid & 0x1FFF;
    tmpv |= (number << 16) & 0xFFFF0000;
    tmpv |= (const1_value << 13) & 0xE000;
    stream->write_4bytes(tmpv);

    return ret;
}

SrsTsPayloadPAT::SrsTsPayloadPAT(SrsTsPacket* p) : SrsTsPayloadPSI(p)
{
    const3_value = 3;
}

SrsTsPayloadPAT::~SrsTsPayloadPAT()
{
    std::vector<SrsTsPayloadPATProgram*>::iterator it;
    for (it = programs.begin(); it != programs.end(); ++it) {
        SrsTsPayloadPATProgram* program = *it;
        srs_freep(program);
    }
    programs.clear();
}

int SrsTsPayloadPAT::psi_decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 5B for PAT specified
    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PAT;
        srs_error("ts: demux PAT failed. ret=%d", ret);
        return ret;
    }

    int pos = stream->pos();

    // 2B
    transport_stream_id = stream->read_2bytes();
    
    // 1B
    int8_t cniv = stream->read_1bytes();
    
    const3_value = (cniv >> 6) & 0x03;
    version_number = (cniv >> 1) & 0x1F;
    current_next_indicator = cniv & 0x01;

    // TODO: FIXME: check the indicator.
    
    // 1B
    section_number = stream->read_1bytes();
    // 1B
    last_section_number = stream->read_1bytes();

    // multiple 4B program data.
    int program_bytes = section_length - 4 - (stream->pos() - pos);
    for (int i = 0; i < program_bytes; i += 4) {
        SrsTsPayloadPATProgram* program = new SrsTsPayloadPATProgram();

        if ((ret = program->decode(stream)) != ERROR_SUCCESS) {
            return ret;
        }

        // update the apply pid table.
        packet->context->set(program->pid, SrsTsPidApplyPMT);

        programs.push_back(program);
    }

    // update the apply pid table.
    packet->context->set(packet->pid, SrsTsPidApplyPAT);
    packet->context->on_pmt_parsed();

    return ret;
}

int SrsTsPayloadPAT::psi_size()
{
    int sz = 5;
    for (int i = 0; i < (int)programs.size(); i ++) {
        SrsTsPayloadPATProgram* program = programs.at(i);
        sz += program->size();
    }
    return sz;
}

int SrsTsPayloadPAT::psi_encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 5B for PAT specified
    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PAT;
        srs_error("ts: mux PAT failed. ret=%d", ret);
        return ret;
    }

    // 2B
    stream->write_2bytes(transport_stream_id);
    
    // 1B
    int8_t cniv = current_next_indicator & 0x01;
    cniv |= (version_number << 1) & 0x3E;
    cniv |= (const1_value << 6) & 0xC0;
    stream->write_1bytes(cniv);
    
    // 1B
    stream->write_1bytes(section_number);
    // 1B
    stream->write_1bytes(last_section_number);

    // multiple 4B program data.
    for (int i = 0; i < (int)programs.size(); i ++) {
        SrsTsPayloadPATProgram* program = programs.at(i);
        if ((ret = program->encode(stream)) != ERROR_SUCCESS) {
            return ret;
        }

        // update the apply pid table.
        packet->context->set(program->pid, SrsTsPidApplyPMT);
    }

    // update the apply pid table.
    packet->context->set(packet->pid, SrsTsPidApplyPAT);

    return ret;
}

SrsTsPayloadPMTESInfo::SrsTsPayloadPMTESInfo(SrsTsStream st, int16_t epid)
{
    stream_type = st;
    elementary_PID = epid;

    const1_value0 = 7;
    const1_value1 = 0x0f;
    ES_info_length = 0;
    ES_info = NULL;
}

SrsTsPayloadPMTESInfo::~SrsTsPayloadPMTESInfo()
{
    srs_freepa(ES_info);
}

int SrsTsPayloadPMTESInfo::decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // 5B
    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PMT;
        srs_error("ts: demux PMT es info failed. ret=%d", ret);
        return ret;
    }

    stream_type = (SrsTsStream)stream->read_1bytes();

    int16_t epv = stream->read_2bytes();
    const1_value0 = (epv >> 13) & 0x07;
    elementary_PID = epv & 0x1FFF;
    
    int16_t eilv = stream->read_2bytes();
    const1_value1 = (epv >> 12) & 0x0f;
    ES_info_length = eilv & 0x0FFF;

    if (ES_info_length > 0) {
        if (!stream->require(ES_info_length)) {
            ret = ERROR_STREAM_CASTER_TS_PMT;
            srs_error("ts: demux PMT es info data failed. ret=%d", ret);
            return ret;
        }
        srs_freepa(ES_info);
        ES_info = new char[ES_info_length];
        stream->read_bytes(ES_info, ES_info_length);
    }

    return ret;
}

int SrsTsPayloadPMTESInfo::size()
{
    return 5 + ES_info_length;
}

int SrsTsPayloadPMTESInfo::encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // 5B
    if (!stream->require(5)) {
        ret = ERROR_STREAM_CASTER_TS_PMT;
        srs_error("ts: mux PMT es info failed. ret=%d", ret);
        return ret;
    }

    stream->write_1bytes(stream_type);

    int16_t epv = elementary_PID & 0x1FFF;
    epv |= (const1_value0 << 13) & 0xE000;
    stream->write_2bytes(epv);
    
    int16_t eilv = ES_info_length & 0x0FFF;
    eilv |= (const1_value1 << 12) & 0xF000;
    stream->write_2bytes(eilv);

    if (ES_info_length > 0) {
        if (!stream->require(ES_info_length)) {
            ret = ERROR_STREAM_CASTER_TS_PMT;
            srs_error("ts: mux PMT es info data failed. ret=%d", ret);
            return ret;
        }
        stream->write_bytes(ES_info, ES_info_length);
    }

    return ret;
}

SrsTsPayloadPMT::SrsTsPayloadPMT(SrsTsPacket* p) : SrsTsPayloadPSI(p)
{
    const1_value0 = 3;
    const1_value1 = 7;
    const1_value2 = 0x0f;
    program_info_length = 0;
    program_info_desc = NULL;
}

SrsTsPayloadPMT::~SrsTsPayloadPMT()
{
    srs_freepa(program_info_desc);

    std::vector<SrsTsPayloadPMTESInfo*>::iterator it;
    for (it = infos.begin(); it != infos.end(); ++it) {
        SrsTsPayloadPMTESInfo* info = *it;
        srs_freep(info);
    }
    infos.clear();
}

int SrsTsPayloadPMT::psi_decode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 9B for PMT specified
    if (!stream->require(9)) {
        ret = ERROR_STREAM_CASTER_TS_PMT;
        srs_error("ts: demux PMT failed. ret=%d", ret);
        return ret;
    }

    // 2B
    program_number = stream->read_2bytes();
    
    // 1B
    int8_t cniv = stream->read_1bytes();
    
    const1_value0 = (cniv >> 6) & 0x03;
    version_number = (cniv >> 1) & 0x1F;
    current_next_indicator = cniv & 0x01;
    
    // 1B
    section_number = stream->read_1bytes();
    
    // 1B
    last_section_number = stream->read_1bytes();

    // 2B
    int16_t ppv = stream->read_2bytes();
    const1_value1 = (ppv >> 13) & 0x07;
    PCR_PID = ppv & 0x1FFF;
    
    // 2B
    int16_t pilv = stream->read_2bytes();
    const1_value2 = (pilv >> 12) & 0x0F;
    program_info_length = pilv & 0xFFF;

    if (program_info_length > 0) {
        if (!stream->require(program_info_length)) {
            ret = ERROR_STREAM_CASTER_TS_PMT;
            srs_error("ts: demux PMT program info failed. ret=%d", ret);
            return ret;
        }

        srs_freepa(program_info_desc);
        program_info_desc = new char[program_info_length];
        stream->read_bytes(program_info_desc, program_info_length);
    }

    // [section_length] - 4(CRC) - 9B - [program_info_length]
    int ES_EOF_pos = stream->pos() + section_length - 4 - 9 - program_info_length;
    while (stream->pos() < ES_EOF_pos) {
        SrsTsPayloadPMTESInfo* info = new SrsTsPayloadPMTESInfo();
        infos.push_back(info);

        if ((ret = info->decode(stream)) != ERROR_SUCCESS) {
            return ret;
        }

        // update the apply pid table
        switch (info->stream_type) {
            case SrsTsStreamVideoH264:
            case SrsTsStreamVideoMpeg4:
                packet->context->set(info->elementary_PID, SrsTsPidApplyVideo, info->stream_type);
                break;
            case SrsTsStreamAudioAAC:
            case SrsTsStreamAudioAC3:
            case SrsTsStreamAudioDTS:
            case SrsTsStreamAudioMp3:
                packet->context->set(info->elementary_PID, SrsTsPidApplyAudio, info->stream_type);
                break;
            default:
                srs_warn("ts: drop pid=%#x, stream=%#x", info->elementary_PID, info->stream_type);
                break;
        }
    }

    // update the apply pid table.
    packet->context->set(packet->pid, SrsTsPidApplyPMT);

    return ret;
}

int SrsTsPayloadPMT::psi_size()
{
    int sz = 9;
    sz += program_info_length;
    for (int i = 0; i < (int)infos.size(); i ++) {
        SrsTsPayloadPMTESInfo* info = infos.at(i);
        sz += info->size();
    }
    return sz;
}

int SrsTsPayloadPMT::psi_encode(SrsStream* stream)
{
    int ret = ERROR_SUCCESS;

    // atleast 9B for PMT specified
    if (!stream->require(9)) {
        ret = ERROR_STREAM_CASTER_TS_PMT;
        srs_error("ts: mux PMT failed. ret=%d", ret);
        return ret;
    }

    // 2B
    stream->write_2bytes(program_number);
    
    // 1B
    int8_t cniv = current_next_indicator & 0x01;
    cniv |= (const1_value0 << 6) & 0xC0;
    cniv |= (version_number << 1) & 0xFE;
    stream->write_1bytes(cniv);
    
    // 1B
    stream->write_1bytes(section_number);
    
    // 1B
    stream->write_1bytes(last_section_number);

    // 2B
    int16_t ppv = PCR_PID & 0x1FFF;
    ppv |= (const1_value1 << 13) & 0xE000;
    stream->write_2bytes(ppv);
    
    // 2B
    int16_t pilv = program_info_length & 0xFFF;
    pilv |= (const1_value2 << 12) & 0xF000;
    stream->write_2bytes(pilv);

    if (program_info_length > 0) {
        if (!stream->require(program_info_length)) {
            ret = ERROR_STREAM_CASTER_TS_PMT;
            srs_error("ts: mux PMT program info failed. ret=%d", ret);
            return ret;
        }

        stream->write_bytes(program_info_desc, program_info_length);
    }

    for (int i = 0; i < (int)infos.size(); i ++) {
        SrsTsPayloadPMTESInfo* info = infos.at(i);
        if ((ret = info->encode(stream)) != ERROR_SUCCESS) {
            return ret;
        }

        // update the apply pid table
        switch (info->stream_type) {
            case SrsTsStreamVideoH264:
            case SrsTsStreamVideoMpeg4:
                packet->context->set(info->elementary_PID, SrsTsPidApplyVideo, info->stream_type);
                break;
            case SrsTsStreamAudioAAC:
            case SrsTsStreamAudioAC3:
            case SrsTsStreamAudioDTS:
            case SrsTsStreamAudioMp3:
                packet->context->set(info->elementary_PID, SrsTsPidApplyAudio, info->stream_type);
                break;
            default:
                srs_warn("ts: drop pid=%#x, stream=%#x", info->elementary_PID, info->stream_type);
                break;
        }
    }

    // update the apply pid table.
    packet->context->set(packet->pid, SrsTsPidApplyPMT);

    return ret;
}

SrsTSMuxer::SrsTSMuxer(SrsFileWriter* w, SrsTsContext* c, SrsCodecAudio ac, SrsCodecVideo vc)
{
    writer = w;
    context = c;

    acodec = ac;
    vcodec = vc;
}

SrsTSMuxer::~SrsTSMuxer()
{
    close();
}

int SrsTSMuxer::open(string p)
{
    int ret = ERROR_SUCCESS;
    
    path = p;
    
    close();
    
    // reset the context for a new ts start.
    context->reset();
    
    if ((ret = writer->open(path)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTSMuxer::update_acodec(SrsCodecAudio ac)
{
    acodec = ac;
    return ERROR_SUCCESS;
}

int SrsTSMuxer::write_audio(SrsTsMessage* audio)
{
    int ret = ERROR_SUCCESS;

    srs_info("hls: write audio pts=%"PRId64", dts=%"PRId64", size=%d", 
        audio->pts, audio->dts, audio->PES_packet_length);
    
    if ((ret = context->encode(writer, audio, vcodec, acodec)) != ERROR_SUCCESS) {
        srs_error("hls encode audio failed. ret=%d", ret);
        return ret;
    }
    srs_info("hls encode audio ok");
    
    return ret;
}

int SrsTSMuxer::write_video(SrsTsMessage* video)
{
    int ret = ERROR_SUCCESS;

    srs_info("hls: write video pts=%"PRId64", dts=%"PRId64", size=%d", 
        video->pts, video->dts, video->PES_packet_length);
    
    if ((ret = context->encode(writer, video, vcodec, acodec)) != ERROR_SUCCESS) {
        srs_error("hls encode video failed. ret=%d", ret);
        return ret;
    }
    srs_info("hls encode video ok");
    
    return ret;
}

void SrsTSMuxer::close()
{
    writer->close();
}

SrsCodecVideo SrsTSMuxer::video_codec()
{
    return vcodec;
}

SrsTsCache::SrsTsCache()
{
    audio = NULL;
    video = NULL;
}

SrsTsCache::~SrsTsCache()
{
    srs_freep(audio);
    srs_freep(video);
}
    
int SrsTsCache::cache_audio(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;

    // create the ts audio message.
    if (!audio) {
        audio = new SrsTsMessage();
        audio->write_pcr = false;
        audio->dts = audio->pts = audio->start_pts = dts;
    }

    // TODO: FIXME: refine code.
    //audio->dts = dts;
    //audio->pts = audio->dts;
    audio->sid = SrsTsPESStreamIdAudioCommon;
    
    // must be aac or mp3
    SrsCodecAudio acodec = (SrsCodecAudio)codec->audio_codec_id;
    srs_assert(acodec == SrsCodecAudioAAC || acodec == SrsCodecAudioMP3);
    
    // write video to cache.
    if (codec->audio_codec_id == SrsCodecAudioAAC) {
        if ((ret = do_cache_aac(codec, sample)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        if ((ret = do_cache_mp3(codec, sample)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}
    
int SrsTsCache::cache_video(SrsAvcAacCodec* codec, int64_t dts, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // create the ts video message.
    if (!video) {
        video = new SrsTsMessage();
        video->write_pcr = sample->frame_type == SrsCodecVideoAVCFrameKeyFrame;
        video->start_pts = dts;
    }

    video->dts = dts;
    video->pts = video->dts + sample->cts * 90;
    video->sid = SrsTsPESStreamIdVideoCommon;
    
    // write video to cache.
    if ((ret = do_cache_avc(codec, sample)) != ERROR_SUCCESS) {
        return ret;
    }

    return ret;
}

int SrsTsCache::do_cache_mp3(SrsAvcAacCodec* codec, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
        
    // for mp3, directly write to cache.
    // TODO: FIXME: implements the ts jitter.
    for (int i = 0; i < sample->nb_sample_units; i++) {
        SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
        audio->payload->append(sample_unit->bytes, sample_unit->size);
    }
    
    return ret;
}

int SrsTsCache::do_cache_aac(SrsAvcAacCodec* codec, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < sample->nb_sample_units; i++) {
        SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
        int32_t size = sample_unit->size;
        
        if (!sample_unit->bytes || size <= 0 || size > 0x1fff) {
            ret = ERROR_HLS_AAC_FRAME_LENGTH;
            srs_error("invalid aac frame length=%d, ret=%d", size, ret);
            return ret;
        }
        
        // the frame length is the AAC raw data plus the adts header size.
        int32_t frame_length = size + 7;
        
        // AAC-ADTS
        // 6.2 Audio Data Transport Stream, ADTS
        // in aac-iso-13818-7.pdf, page 26.
        // fixed 7bytes header
        u_int8_t adts_header[7] = {0xff, 0xf9, 0x00, 0x00, 0x00, 0x0f, 0xfc};
        /*
        // adts_fixed_header
        // 2B, 16bits
        int16_t syncword; //12bits, '1111 1111 1111'
        int8_t ID; //1bit, '1'
        int8_t layer; //2bits, '00'
        int8_t protection_absent; //1bit, can be '1'
        // 12bits
        int8_t profile; //2bit, 7.1 Profiles, page 40
        TSAacSampleFrequency sampling_frequency_index; //4bits, Table 35, page 46
        int8_t private_bit; //1bit, can be '0'
        int8_t channel_configuration; //3bits, Table 8
        int8_t original_or_copy; //1bit, can be '0'
        int8_t home; //1bit, can be '0'
        
        // adts_variable_header
        // 28bits
        int8_t copyright_identification_bit; //1bit, can be '0'
        int8_t copyright_identification_start; //1bit, can be '0'
        int16_t frame_length; //13bits
        int16_t adts_buffer_fullness; //11bits, 7FF signals that the bitstream is a variable rate bitstream.
        int8_t number_of_raw_data_blocks_in_frame; //2bits, 0 indicating 1 raw_data_block()
        */
        // profile, 2bits
        SrsAacProfile aac_profile = srs_codec_aac_rtmp2ts(codec->aac_object);
        adts_header[2] = (aac_profile << 6) & 0xc0;
        // sampling_frequency_index 4bits
        adts_header[2] |= (codec->aac_sample_rate << 2) & 0x3c;
        // channel_configuration 3bits
        adts_header[2] |= (codec->aac_channels >> 2) & 0x01;
        adts_header[3] = (codec->aac_channels << 6) & 0xc0;
        // frame_length 13bits
        adts_header[3] |= (frame_length >> 11) & 0x03;
        adts_header[4] = (frame_length >> 3) & 0xff;
        adts_header[5] = ((frame_length << 5) & 0xe0);
        // adts_buffer_fullness; //11bits
        adts_header[5] |= 0x1f;

        // copy to audio buffer
        audio->payload->append((const char*)adts_header, sizeof(adts_header));
        audio->payload->append(sample_unit->bytes, sample_unit->size);
    }
    
    return ret;
}

int SrsTsCache::do_cache_avc(SrsAvcAacCodec* codec, SrsCodecSample* sample)
{
    int ret = ERROR_SUCCESS;
    
    // mux the samples in annexb format,
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 324.
    /**
     * 00 00 00 01 // header
     *       xxxxxxx // data bytes
     * 00 00 01 // continue header
     *       xxxxxxx // data bytes.
     *
     * nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
     * Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
     * H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 83.
     *      1, Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
     *      2, Coded slice data partition A slice_data_partition_a_layer_rbsp( )
     *      3, Coded slice data partition B slice_data_partition_b_layer_rbsp( )
     *      4, Coded slice data partition C slice_data_partition_c_layer_rbsp( )
     *      5, Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
     *      6, Supplemental enhancement information (SEI) sei_rbsp( )
     *      7, Sequence parameter set seq_parameter_set_rbsp( )
     *      8, Picture parameter set pic_parameter_set_rbsp( )
     *      9, Access unit delimiter access_unit_delimiter_rbsp( )
     *      10, End of sequence end_of_seq_rbsp( )
     *      11, End of stream end_of_stream_rbsp( )
     *      12, Filler data filler_data_rbsp( )
     *      13, Sequence parameter set extension seq_parameter_set_extension_rbsp( )
     *      14, Prefix NAL unit prefix_nal_unit_rbsp( )
     *      15, Subset sequence parameter set subset_seq_parameter_set_rbsp( )
     *      19, Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
     *      20, Coded slice extension slice_layer_extension_rbsp( )
     * the first ts message of apple sample:
     *      annexb 4B header, 2B aud(nal_unit_type:6)(0x09 0xf0)(AUD)
     *      annexb 4B header, 19B sps(nal_unit_type:7)(SPS)
     *      annexb 3B header, 4B pps(nal_unit_type:8)(PPS)
     *      annexb 3B header, 12B nalu(nal_unit_type:6)(SEI)
     *      annexb 3B header, 21B nalu(nal_unit_type:6)(SEI)
     *      annexb 3B header, 2762B nalu(nal_unit_type:5)(IDR)
     *      annexb 3B header, 3535B nalu(nal_unit_type:5)(IDR)
     * the second ts message of apple ts sample:
     *      annexb 4B header, 2B aud(nal_unit_type:6)(0x09 0xf0)(AUD)
     *      annexb 3B header, 21B nalu(nal_unit_type:6)(SEI)
     *      annexb 3B header, 379B nalu(nal_unit_type:1)(non-IDR,P/B)
     *      annexb 3B header, 406B nalu(nal_unit_type:1)(non-IDR,P/B)
     * @remark we use the sequence of apple samples http://ossrs.net/apple-sample/bipbopall.m3u8
     */
    static u_int8_t fresh_nalu_header[] = { 0x00, 0x00, 0x00, 0x01 };
    static u_int8_t cont_nalu_header[] = { 0x00, 0x00, 0x01 };
    
    // the aud(access unit delimiter) before each frame.
    // 7.3.2.4 Access unit delimiter RBSP syntax
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 66.
    //
    // primary_pic_type u(3), the first 3bits, primary_pic_type indicates that the slice_type values
    //      for all slices of the primary coded picture are members of the set listed in Table 7-5 for
    //      the given value of primary_pic_type.
    //      0, slice_type 2, 7
    //      1, slice_type 0, 2, 5, 7
    //      2, slice_type 0, 1, 2, 5, 6, 7
    //      3, slice_type 4, 9
    //      4, slice_type 3, 4, 8, 9
    //      5, slice_type 2, 4, 7, 9
    //      6, slice_type 0, 2, 3, 4, 5, 7, 8, 9
    //      7, slice_type 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    // 7.4.2.4 Access unit delimiter RBSP semantics
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 102.
    //
    // slice_type specifies the coding type of the slice according to Table 7-6.
    //      0, P (P slice)
    //      1, B (B slice)
    //      2, I (I slice)
    //      3, SP (SP slice)
    //      4, SI (SI slice)
    //      5, P (P slice)
    //      6, B (B slice)
    //      7, I (I slice)
    //      8, SP (SP slice)
    //      9, SI (SI slice)
    // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 105.
    static u_int8_t aud_nalu_7[] = { 0x09, 0xf0};
    
    // For NonIDR(open gop), we directly appends all frames.
    if (sample->has_non_idr || (sample->has_aud && sample->has_sei)) {
        for (int i = 0; i < sample->nb_sample_units; i++) {
            SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
            int32_t size = sample_unit->size;
            
            if (!sample_unit->bytes || size <= 0) {
                ret = ERROR_HLS_AVC_SAMPLE_SIZE;
                srs_error("invalid avc sample length=%d, ret=%d", size, ret);
                return ret;
            }
            
            // insert nalu header before rbsp.
            if (i == 0) {
                video->payload->append((const char*)fresh_nalu_header, 4);
            } else {
                video->payload->append((const char*)cont_nalu_header, 3);
            }
            
            // sample data
            video->payload->append(sample_unit->bytes, sample_unit->size);
        }
        
        return ret;
    }
    
    // always append a aud nalu for each frame.
    video->payload->append((const char*)fresh_nalu_header, 4);
    video->payload->append((const char*)aud_nalu_7, 2);
    
    // when ts message(samples) contains IDR, insert sps+pps.
    if (sample->has_idr) {
        // fresh nalu header before sps.
        if (codec->sequenceParameterSetLength > 0) {
            // AnnexB prefix, for sps always 4 bytes header
            video->payload->append((const char*)fresh_nalu_header, 4);
            // sps
            video->payload->append(codec->sequenceParameterSetNALUnit, codec->sequenceParameterSetLength);
        }
        // cont nalu header before pps.
        if (codec->pictureParameterSetLength > 0) {
            // AnnexB prefix, for pps always 3 bytes header
            video->payload->append((const char*)cont_nalu_header, 3);
            // pps
            video->payload->append(codec->pictureParameterSetNALUnit, codec->pictureParameterSetLength);
        }
    }
    
    // all sample use cont nalu header, except the sps-pps before IDR frame.
    for (int i = 0; i < sample->nb_sample_units; i++) {
        SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
        int32_t size = sample_unit->size;
        
        if (!sample_unit->bytes || size <= 0) {
            ret = ERROR_HLS_AVC_SAMPLE_SIZE;
            srs_error("invalid avc sample length=%d, ret=%d", size, ret);
            return ret;
        }
        
        // 5bits, 7.3.1 NAL unit syntax,
        // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 83.
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(sample_unit->bytes[0] & 0x1f);
        
        // ignore SPS/PPS/AUD
        switch (nal_unit_type) {
            case SrsAvcNaluTypeSPS:
            case SrsAvcNaluTypePPS:
            case SrsAvcNaluTypeAccessUnitDelimiter:
                continue;
            default:
                break;
        }
        
        // insert cont nalu header before frame.
        video->payload->append((const char*)cont_nalu_header, 3);
        
        // sample data
        video->payload->append(sample_unit->bytes, sample_unit->size);
    }
    
    return ret;
}

SrsTsEncoder::SrsTsEncoder()
{
    writer = NULL;
    codec = new SrsAvcAacCodec();
    sample = new SrsCodecSample();
    cache = new SrsTsCache();
    context = new SrsTsContext();
    muxer = NULL;
}

SrsTsEncoder::~SrsTsEncoder()
{
    srs_freep(codec);
    srs_freep(sample);
    srs_freep(cache);
    srs_freep(muxer);
    srs_freep(context);
}

int SrsTsEncoder::initialize(SrsFileWriter* fw)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fw);
    
    if (!fw->is_open()) {
        ret = ERROR_KERNEL_FLV_STREAM_CLOSED;
        srs_warn("stream is not open for encoder. ret=%d", ret);
        return ret;
    }
    
    writer = fw;

    srs_freep(muxer);
    muxer = new SrsTSMuxer(fw, context, SrsCodecAudioAAC, SrsCodecVideoAVC);

    if ((ret = muxer->open("")) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsTsEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    sample->clear();
    if ((ret = codec->audio_aac_demux(data, size, sample)) != ERROR_SUCCESS) {
        if (ret != ERROR_HLS_TRY_MP3) {
            srs_error("http: ts aac demux audio failed. ret=%d", ret);
            return ret;
        }
        if ((ret = codec->audio_mp3_demux(data, size, sample)) != ERROR_SUCCESS) {
            srs_error("http: ts mp3 demux audio failed. ret=%d", ret);
            return ret;
        }
    }
    SrsCodecAudio acodec = (SrsCodecAudio)codec->audio_codec_id;
    
    // ts support audio codec: aac/mp3
    if (acodec != SrsCodecAudioAAC && acodec != SrsCodecAudioMP3) {
        return ret;
    }

    // when codec changed, write new header.
    if ((ret = muxer->update_acodec(acodec)) != ERROR_SUCCESS) {
        srs_error("http: ts audio write header failed. ret=%d", ret);
        return ret;
    }
    
    // for aac: ignore sequence header
    if (acodec == SrsCodecAudioAAC && sample->aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
        return ret;
    }

    // the dts calc from rtmp/flv header.
    // @remark for http ts stream, the timestamp is always monotonically increase,
    //      for the packet is filtered by consumer.
    int64_t dts = timestamp * 90;
    
    // write audio to cache.
    if ((ret = cache->cache_audio(codec, dts, sample)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // TODO: FIXME: for pure audio, aggregate some frame to one.
    
    // always flush audio frame by frame.
    // @see https://github.com/ossrs/srs/issues/512
    return flush_audio();
}

int SrsTsEncoder::write_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    sample->clear();
    if ((ret = codec->video_avc_demux(data, size, sample)) != ERROR_SUCCESS) {
        srs_error("http: ts codec demux video failed. ret=%d", ret);
        return ret;
    }
    
    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
        return ret;
    }
    
    if (codec->video_codec_id != SrsCodecVideoAVC) {
        return ret;
    }
    
    // ignore sequence header
    if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
         && sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
        return ret;
    }
    
    int64_t dts = timestamp * 90;
    
    // write video to cache.
    if ((ret = cache->cache_video(codec, dts, sample)) != ERROR_SUCCESS) {
        return ret;
    }

    return flush_video();
}

int SrsTsEncoder::flush_audio()
{
    int ret = ERROR_SUCCESS;

    if ((ret = muxer->write_audio(cache->audio)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the ts message.
    srs_freep(cache->audio);

    return ret;
}

int SrsTsEncoder::flush_video()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = muxer->write_video(cache->video)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // write success, clear and free the ts message.
    srs_freep(cache->video);

    return ret;
}

#endif

