//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_ts.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#include <unistd.h>

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <cstring>
#include <openssl/aes.h>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>

#define HLS_AES_ENCRYPT_BLOCK_LENGTH SRS_TS_PACKET_SIZE * 4

// the mpegts header specifed the video/audio pid.
#define TS_PMT_NUMBER 1
#define TS_PMT_PID 0x1001
#define TS_VIDEO_AVC_PID 0x100
#define TS_AUDIO_AAC_PID 0x101
#define TS_AUDIO_MP3_PID 0x102

// @see pycrc reflect at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L107
uint64_t __crc32_reflect(uint64_t data, int width)
{
    uint64_t res = data & 0x01;

    for (int i = 0; i < (int)width - 1; i++) {
        data >>= 1;
        res = (res << 1) | (data & 0x01);
    }

    return res;
}

// @see pycrc gen_table at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L178
void __crc32_make_table(uint32_t t[256], uint32_t poly, bool reflect_in)
{
    int width = 32; // 32bits checksum.
    uint64_t msb_mask = (uint32_t)(0x01 << (width - 1));
    uint64_t mask = (uint32_t)(((msb_mask - 1) << 1) | 1);

    int tbl_idx_width = 8;                 // table index size.
    int tbl_width = 0x01 << tbl_idx_width; // table size: 256

    for (int i = 0; i < (int)tbl_width; i++) {
        uint64_t reg = uint64_t(i);

        if (reflect_in) {
            reg = __crc32_reflect(reg, tbl_idx_width);
        }

        reg = reg << (width - tbl_idx_width);
        for (int j = 0; j < tbl_idx_width; j++) {
            if ((reg & msb_mask) != 0) {
                reg = (reg << 1) ^ poly;
            } else {
                reg = reg << 1;
            }
        }

        if (reflect_in) {
            reg = __crc32_reflect(reg, width);
        }

        t[i] = (uint32_t)(reg & mask);
    }
}

// @see pycrc table_driven at https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L207
uint32_t __crc32_table_driven(uint32_t *t, const void *buf, int size, uint32_t previous, bool reflect_in, uint32_t xor_in, bool reflect_out, uint32_t xor_out)
{
    int width = 32; // 32bits checksum.
    uint64_t msb_mask = (uint32_t)(0x01 << (width - 1));
    uint64_t mask = (uint32_t)(((msb_mask - 1) << 1) | 1);

    int tbl_idx_width = 8; // table index size.

    uint8_t *p = (uint8_t *)buf;
    uint64_t reg = 0;

    if (!reflect_in) {
        reg = xor_in;

        for (int i = 0; i < (int)size; i++) {
            uint8_t tblidx = (uint8_t)((reg >> (width - tbl_idx_width)) ^ p[i]);
            reg = t[tblidx] ^ (reg << tbl_idx_width);
        }
    } else {
        reg = previous ^ __crc32_reflect(xor_in, width);

        for (int i = 0; i < (int)size; i++) {
            uint8_t tblidx = (uint8_t)(reg ^ p[i]);
            reg = t[tblidx] ^ (reg >> tbl_idx_width);
        }

        reg = __crc32_reflect(reg, width);
    }

    if (reflect_out) {
        reg = __crc32_reflect(reg, width);
    }

    reg ^= xor_out;
    return (uint32_t)(reg & mask);
}

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/algorithms.py#L238
// IEEETable is the table for the MPEG polynomial.
static uint32_t __crc32_MPEG_table[256];
static bool __crc32_MPEG_table_initialized = false;

// @see pycrc https://github.com/winlinvip/pycrc/blob/master/pycrc/models.py#L238
//      crc32('123456789') = 0x0376e6e7
// where it's defined as model:
//      'name':         'crc-32',
//      'width':         32,
//      'poly':          0x4c11db7,
//      'reflect_in':    False,
//      'xor_in':        0xffffffff,
//      'reflect_out':   False,
//      'xor_out':       0x0,
//      'check':         0x0376e6e7,
uint32_t srs_crc32_mpegts(const void *buf, int size)
{
    // @see golang IEEE of hash/crc32/crc32.go
    // IEEE is by far and away the most common CRC-32 polynomial.
    // Used by ethernet (IEEE 802.3), v.42, fddi, gzip, zip, png, ...
    // @remark The poly of CRC32 IEEE is 0x04C11DB7, its reverse is 0xEDB88320,
    //      please read https://en.wikipedia.org/wiki/Cyclic_redundancy_check
    uint32_t poly = 0x04C11DB7;

    bool reflect_in = false;
    uint32_t xor_in = 0xffffffff;
    bool reflect_out = false;
    uint32_t xor_out = 0x0;

    if (!__crc32_MPEG_table_initialized) {
        __crc32_make_table(__crc32_MPEG_table, poly, reflect_in);
        __crc32_MPEG_table_initialized = true;
    }

    return __crc32_table_driven(__crc32_MPEG_table, buf, size, 0x00, reflect_in, xor_in, reflect_out, xor_out);
}

string srs_ts_stream2string(SrsTsStream stream)
{
    switch (stream) {
    case SrsTsStreamReserved:
        return "Reserved";
    case SrsTsStreamAudioMp3:
        return "MP3";
    case SrsTsStreamAudioAAC:
        return "AAC";
    case SrsTsStreamAudioAC3:
        return "AC3";
    case SrsTsStreamAudioDTS:
        return "AudioDTS";
    case SrsTsStreamVideoH264:
        return "H.264";
    case SrsTsStreamVideoHEVC:
        return "H.265";
    case SrsTsStreamVideoMpeg4:
        return "MP4";
    case SrsTsStreamAudioMpeg4:
        return "MP4A";
    default:
        return "Other";
    }
}

SrsTsChannel::SrsTsChannel()
{
    pid_ = 0;
    apply_ = SrsTsPidApplyReserved;
    stream_ = SrsTsStreamReserved;
    msg_ = NULL;
    continuity_counter_ = 0;
    context_ = NULL;
}

SrsTsChannel::~SrsTsChannel()
{
    srs_freep(msg_);
}

SrsTsMessage::SrsTsMessage(SrsTsChannel *c, SrsTsPacket *p)
{
    channel_ = c;
    packet_ = p;
    ps_helper_ = NULL;

    dts_ = pts_ = 0;
    sid_ = (SrsTsPESStreamId)0x00;
    continuity_counter_ = 0;
    PES_packet_length_ = 0;
    payload_ = new SrsSimpleStream();
    is_discontinuity_ = false;

    start_pts_ = 0;
    write_pcr_ = false;
}

SrsTsMessage::~SrsTsMessage()
{
    srs_freep(payload_);
}

srs_error_t SrsTsMessage::dump(SrsBuffer *stream, int *pnb_bytes)
{
    srs_error_t err = srs_success;

    if (stream->empty()) {
        return err;
    }

    // xB
    int nb_bytes = stream->size() - stream->pos();
    if (PES_packet_length_ > 0) {
        nb_bytes = srs_min(nb_bytes, PES_packet_length_ - payload_->length());
    }

    if (nb_bytes > 0) {
        if (!stream->require(nb_bytes)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: dump PSE bytes failed, requires=%dB", nb_bytes);
        }

        payload_->append(stream->data() + stream->pos(), nb_bytes);
        stream->skip(nb_bytes);
    }

    if (pnb_bytes)
        *pnb_bytes = nb_bytes;

    return err;
}

bool SrsTsMessage::completed(int8_t payload_unit_start_indicator)
{
    if (PES_packet_length_ == 0) {
        return payload_unit_start_indicator;
    }
    return payload_->length() >= PES_packet_length_;
}

bool SrsTsMessage::fresh()
{
    // Note that both must be 0. For PS stream, the payload might be empty but PES_packet_length is not, see
    // PsPacketDecodePrivateStream of KernelPSTest. For TS stream, both should be 0 in the same time.
    return PES_packet_length_ == 0 && payload_->length() == 0;
}

bool SrsTsMessage::is_audio()
{
    return ((sid_ >> 5) & 0x07) == SrsTsPESStreamIdAudioChecker;
}

bool SrsTsMessage::is_video()
{
    return ((sid_ >> 4) & 0x0f) == SrsTsPESStreamIdVideoChecker;
}

int SrsTsMessage::stream_number()
{
    if (is_audio()) {
        return sid_ & 0x1f;
    } else if (is_video()) {
        return sid_ & 0x0f;
    }
    return -1;
}

SrsTsMessage *SrsTsMessage::detach()
{
    // @remark the packet cannot be used, but channel is ok.
    SrsTsMessage *cp = new SrsTsMessage(channel_, NULL);
    cp->ps_helper_ = ps_helper_;
    cp->start_pts_ = start_pts_;
    cp->write_pcr_ = write_pcr_;
    cp->is_discontinuity_ = is_discontinuity_;
    cp->dts_ = dts_;
    cp->pts_ = pts_;
    cp->sid_ = sid_;
    cp->PES_packet_length_ = PES_packet_length_;
    cp->continuity_counter_ = continuity_counter_;

    srs_freep(cp->payload_);
    cp->payload_ = payload_;
    payload_ = NULL;

    return cp;
}

ISrsTsHandler::ISrsTsHandler()
{
}

ISrsTsHandler::~ISrsTsHandler()
{
}

ISrsTsContext::ISrsTsContext()
{
}

ISrsTsContext::~ISrsTsContext()
{
}

SrsTsContext::SrsTsContext()
{
    ready_ = false;
    pure_audio_ = false;
    sync_byte_ = 0x47; // ts default sync byte.
    vcodec_ = SrsVideoCodecIdReserved;
    acodec_ = SrsAudioCodecIdReserved1;
}

SrsTsContext::~SrsTsContext()
{
    std::map<int, SrsTsChannel *>::iterator it;
    for (it = pids_.begin(); it != pids_.end(); ++it) {
        SrsTsChannel *channel = it->second;
        srs_freep(channel);
    }
    pids_.clear();
}

bool SrsTsContext::is_pure_audio()
{
    return pure_audio_;
}

void SrsTsContext::on_pmt_parsed()
{
    pure_audio_ = true;

    std::map<int, SrsTsChannel *>::iterator it;
    for (it = pids_.begin(); it != pids_.end(); ++it) {
        SrsTsChannel *channel = it->second;
        if (channel->apply_ == SrsTsPidApplyVideo) {
            pure_audio_ = false;
        }
    }
}

void SrsTsContext::reset()
{
    ready_ = false;
    vcodec_ = SrsVideoCodecIdReserved;
    acodec_ = SrsAudioCodecIdReserved1;
}

SrsTsChannel *SrsTsContext::get(int pid)
{
    if (pids_.find(pid) == pids_.end()) {
        return NULL;
    }
    return pids_[pid];
}

void SrsTsContext::set(int pid, SrsTsPidApply apply_pid, SrsTsStream stream)
{
    SrsTsChannel *channel = NULL;

    if (pids_.find(pid) == pids_.end()) {
        channel = new SrsTsChannel();
        channel->context_ = this;
        pids_[pid] = channel;
    } else {
        channel = pids_[pid];
    }

    channel->pid_ = pid;
    channel->apply_ = apply_pid;
    channel->stream_ = stream;
}

srs_error_t SrsTsContext::decode(SrsBuffer *stream, ISrsTsHandler *handler)
{
    srs_error_t err = srs_success;

    // parse util EOF of stream.
    // for example, parse multiple times for the PES_packet_length(0) packet.
    while (!stream->empty()) {
        SrsUniquePtr<SrsTsPacket> packet(new SrsTsPacket(this));

        SrsTsMessage *msg_raw = NULL;
        if ((err = packet->decode(stream, &msg_raw)) != srs_success) {
            return srs_error_wrap(err, "ts: ts packet decode");
        }

        if (!msg_raw) {
            continue;
        }

        SrsUniquePtr<SrsTsMessage> msg(msg_raw);

        if ((err = handler->on_ts_message(msg.get())) != srs_success) {
            return srs_error_wrap(err, "ts: handle ts message");
        }
    }

    return err;
}

srs_error_t SrsTsContext::encode(ISrsStreamWriter *writer, SrsTsMessage *msg, SrsVideoCodecId vc, SrsAudioCodecId ac)
{
    srs_error_t err = srs_success;

    SrsTsStream vs, as;
    int16_t video_pid = 0, audio_pid = 0;
    switch (vc) {
    case SrsVideoCodecIdAVC:
        vs = SrsTsStreamVideoH264;
        video_pid = TS_VIDEO_AVC_PID;
        break;
    case SrsVideoCodecIdHEVC:
        vs = SrsTsStreamVideoHEVC;
        video_pid = TS_VIDEO_AVC_PID;
        break;
    case SrsVideoCodecIdDisabled:
        vs = SrsTsStreamReserved;
        break;
    case SrsVideoCodecIdReserved:
    case SrsVideoCodecIdReserved1:
    case SrsVideoCodecIdReserved2:
    case SrsVideoCodecIdSorensonH263:
    case SrsVideoCodecIdScreenVideo:
    case SrsVideoCodecIdOn2VP6:
    case SrsVideoCodecIdOn2VP6WithAlphaChannel:
    case SrsVideoCodecIdScreenVideoVersion2:
    case SrsVideoCodecIdAV1:
        vs = SrsTsStreamReserved;
        break;
    }
    switch (ac) {
    case SrsAudioCodecIdAAC:
        as = SrsTsStreamAudioAAC;
        audio_pid = TS_AUDIO_AAC_PID;
        break;
    case SrsAudioCodecIdMP3:
        as = SrsTsStreamAudioMp3;
        audio_pid = TS_AUDIO_MP3_PID;
        break;
    case SrsAudioCodecIdDisabled:
        as = SrsTsStreamReserved;
        break;
    case SrsAudioCodecIdReserved1:
    case SrsAudioCodecIdLinearPCMPlatformEndian:
    case SrsAudioCodecIdADPCM:
    case SrsAudioCodecIdLinearPCMLittleEndian:
    case SrsAudioCodecIdNellymoser16kHzMono:
    case SrsAudioCodecIdNellymoser8kHzMono:
    case SrsAudioCodecIdNellymoser:
    case SrsAudioCodecIdReservedG711AlawLogarithmicPCM:
    case SrsAudioCodecIdReservedG711MuLawLogarithmicPCM:
    case SrsAudioCodecIdReserved:
    case SrsAudioCodecIdSpeex:
    case SrsAudioCodecIdReservedMP3_8kHz:
    case SrsAudioCodecIdReservedDeviceSpecificSound:
    case SrsAudioCodecIdOpus:
        as = SrsTsStreamReserved;
        break;
    }

    if (as == SrsTsStreamReserved && vs == SrsTsStreamReserved) {
        return srs_error_new(ERROR_HLS_NO_STREAM, "ts: no a/v stream, vcodec=%d, acodec=%d", vc, ac);
    }

    // When any codec changed, write PAT/PMT table.
    if (vcodec_ != vc || acodec_ != ac) {
        if (vcodec_ != SrsVideoCodecIdReserved || acodec_ != SrsAudioCodecIdReserved1) {
            srs_trace("TS: Refresh PMT when vcodec=%d=>%d, acodec=%d=>%d", vcodec_, vc, acodec_, ac);
        }
        vcodec_ = vc;
        acodec_ = ac;

        if ((err = encode_pat_pmt(writer, video_pid, vs, audio_pid, as)) != srs_success) {
            return srs_error_wrap(err, "ts: encode PAT/PMT");
        }
    }

    // encode the media frame to PES packets over TS.
    if (msg->is_audio()) {
        return encode_pes(writer, msg, audio_pid, as, vs == SrsTsStreamReserved);
    } else {
        return encode_pes(writer, msg, video_pid, vs, vs == SrsTsStreamReserved);
    }
}

srs_error_t SrsTsContext::encode_pat_pmt(ISrsStreamWriter *writer, int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as)
{
    srs_error_t err = srs_success;

    bool codec_ok = (vs == SrsTsStreamVideoH264 || as == SrsTsStreamAudioAAC || as == SrsTsStreamAudioMp3);
    codec_ok = codec_ok ? true : (vs == SrsTsStreamVideoHEVC);
    if (!codec_ok) {
        return srs_error_new(ERROR_HLS_NO_STREAM, "ts: no PID, vs=%d, as=%d", vs, as);
    }

    int16_t pmt_number = TS_PMT_NUMBER;
    int16_t pmt_pid = TS_PMT_PID;
    if (true) {
        SrsUniquePtr<SrsTsPacket> pkt(SrsTsPacket::create_pat(this, pmt_number, pmt_pid));

        pkt->sync_byte_ = sync_byte_;

        SrsUniquePtr<char[]> buf(new char[SRS_TS_PACKET_SIZE]);

        // set the left bytes with 0xFF.
        int nb_buf = pkt->size();
        srs_assert(nb_buf < SRS_TS_PACKET_SIZE);
        memset(buf.get() + nb_buf, 0xFF, SRS_TS_PACKET_SIZE - nb_buf);

        SrsBuffer stream(buf.get(), nb_buf);
        if ((err = pkt->encode(&stream)) != srs_success) {
            return srs_error_wrap(err, "ts: encode packet");
        }
        if ((err = writer->write(buf.get(), SRS_TS_PACKET_SIZE, NULL)) != srs_success) {
            return srs_error_wrap(err, "ts: write packet");
        }
    }
    if (true) {
        SrsUniquePtr<SrsTsPacket> pkt(SrsTsPacket::create_pmt(this, pmt_number, pmt_pid, vpid, vs, apid, as));

        pkt->sync_byte_ = sync_byte_;

        SrsUniquePtr<char[]> buf(new char[SRS_TS_PACKET_SIZE]);

        // set the left bytes with 0xFF.
        int nb_buf = pkt->size();
        srs_assert(nb_buf < SRS_TS_PACKET_SIZE);
        memset(buf.get() + nb_buf, 0xFF, SRS_TS_PACKET_SIZE - nb_buf);

        SrsBuffer stream(buf.get(), nb_buf);
        if ((err = pkt->encode(&stream)) != srs_success) {
            return srs_error_wrap(err, "ts: encode packet");
        }
        if ((err = writer->write(buf.get(), SRS_TS_PACKET_SIZE, NULL)) != srs_success) {
            return srs_error_wrap(err, "ts: write packet");
        }
    }

    // When PAT and PMT are writen, the context is ready now.
    ready_ = true;

    return err;
}

srs_error_t SrsTsContext::encode_pes(ISrsStreamWriter *writer, SrsTsMessage *msg, int16_t pid, SrsTsStream sid, bool pure_audio)
{
    srs_error_t err = srs_success;

    // Sometimes, the context is not ready(PAT/PMT write failed), error in this situation.
    if (!ready_) {
        return srs_error_new(ERROR_TS_CONTEXT_NOT_READY, "ts: not ready");
    }

    if (msg->payload_->length() == 0) {
        return err;
    }

    bool codec_ok = (sid == SrsTsStreamVideoH264 || sid == SrsTsStreamAudioAAC || sid == SrsTsStreamAudioMp3);
    codec_ok = codec_ok ? true : (sid == SrsTsStreamVideoHEVC);
    if (!codec_ok) {
        srs_info("ts: ignore the unknown stream, sid=%d", sid);
        return err;
    }

    SrsTsChannel *channel = get(pid);
    srs_assert(channel);

    char *start = msg->payload_->bytes();
    char *end = start + msg->payload_->length();
    char *p = start;

    while (p < end) {
        SrsTsPacket *pkt_raw = NULL;
        if (p == start) {
            // write pcr according to message.
            bool write_pcr = msg->write_pcr_;

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
            int64_t pcr = write_pcr ? msg->dts_ : -1;

            // TODO: FIXME: finger it why use discontinuity of msg.
            pkt_raw = SrsTsPacket::create_pes_first(this,
                                                    pid, msg->sid_, channel->continuity_counter_++, msg->is_discontinuity_,
                                                    pcr, msg->dts_, msg->pts_, msg->payload_->length());
        } else {
            pkt_raw = SrsTsPacket::create_pes_continue(this, pid, msg->sid_, channel->continuity_counter_++);
        }
        SrsUniquePtr<SrsTsPacket> pkt(pkt_raw);

        pkt->sync_byte_ = sync_byte_;

        SrsUniquePtr<char[]> buf(new char[SRS_TS_PACKET_SIZE]);

        // set the left bytes with 0xFF.
        int nb_buf = pkt->size();
        srs_assert(nb_buf < SRS_TS_PACKET_SIZE);

        int left = (int)srs_min(end - p, SRS_TS_PACKET_SIZE - nb_buf);
        int nb_stuffings = SRS_TS_PACKET_SIZE - nb_buf - left;
        if (nb_stuffings > 0) {
            // set all bytes to stuffings.
            memset(buf.get(), 0xFF, SRS_TS_PACKET_SIZE);

            // padding with stuffings.
            pkt->padding(nb_stuffings);

            // size changed, recalc it.
            nb_buf = pkt->size();
            srs_assert(nb_buf < SRS_TS_PACKET_SIZE);

            left = (int)srs_min(end - p, SRS_TS_PACKET_SIZE - nb_buf);
            nb_stuffings = SRS_TS_PACKET_SIZE - nb_buf - left;
            srs_assert(nb_stuffings == 0);
        }
        memcpy(buf.get() + nb_buf, p, left);
        p += left;

        SrsBuffer stream(buf.get(), nb_buf);
        if ((err = pkt->encode(&stream)) != srs_success) {
            return srs_error_wrap(err, "ts: encode packet");
        }
        if ((err = writer->write(buf.get(), SRS_TS_PACKET_SIZE, NULL)) != srs_success) {
            return srs_error_wrap(err, "ts: write packet");
        }
    }

    return err;
}

SrsTsPacket::SrsTsPacket(ISrsTsContext *c)
{
    context_ = c;

    sync_byte_ = 0;
    transport_error_indicator_ = 0;
    payload_unit_start_indicator_ = 0;
    transport_priority_ = 0;
    pid_ = SrsTsPidPAT;
    transport_scrambling_control_ = SrsTsScrambledDisabled;
    adaption_field_control_ = SrsTsAdaptationFieldTypeReserved;
    continuity_counter_ = 0;
    adaptation_field_ = NULL;
    payload_ = NULL;
}

SrsTsPacket::~SrsTsPacket()
{
    srs_freep(adaptation_field_);
    srs_freep(payload_);
}

srs_error_t SrsTsPacket::decode(SrsBuffer *stream, SrsTsMessage **ppmsg)
{
    srs_error_t err = srs_success;

    int pos = stream->pos();

    // 4B ts packet header.
    if (!stream->require(4)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_HEADER, "ts: decode packet");
    }

    sync_byte_ = stream->read_1bytes();
    if (sync_byte_ != 0x47) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_SYNC_BYTE, "ts: sync_bytes must be 0x47, actual=%#x", sync_byte_);
    }

    int16_t pidv = stream->read_2bytes();
    transport_error_indicator_ = (pidv >> 15) & 0x01;
    payload_unit_start_indicator_ = (pidv >> 14) & 0x01;
    transport_priority_ = (pidv >> 13) & 0x01;
    pid_ = (SrsTsPid)(pidv & 0x1FFF);

    int8_t ccv = stream->read_1bytes();
    transport_scrambling_control_ = (SrsTsScrambled)((ccv >> 6) & 0x03);
    adaption_field_control_ = (SrsTsAdaptationFieldType)((ccv >> 4) & 0x03);
    continuity_counter_ = ccv & 0x0F;

    // TODO: FIXME: create pids map when got new pid.

    srs_info("ts: header sync=%#x error=%d unit_start=%d priotiry=%d pid=%d scrambling=%d adaption=%d counter=%d",
             sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
             transport_scrambling_control, adaption_field_control, continuity_counter);

    // optional: adaptation field
    if (adaption_field_control_ == SrsTsAdaptationFieldTypeAdaptionOnly || adaption_field_control_ == SrsTsAdaptationFieldTypeBoth) {
        srs_freep(adaptation_field_);
        adaptation_field_ = new SrsTsAdaptationField(this);

        if ((err = adaptation_field_->decode(stream)) != srs_success) {
            return srs_error_wrap(err, "ts: demux af field");
        }
        srs_verbose("ts: demux af ok.");
    }

    // calc the user defined data size for payload.
    int nb_payload = SRS_TS_PACKET_SIZE - (stream->pos() - pos);

    // optional: payload.
    if (adaption_field_control_ == SrsTsAdaptationFieldTypePayloadOnly || adaption_field_control_ == SrsTsAdaptationFieldTypeBoth) {
        if (pid_ == SrsTsPidPAT) {
            // 2.4.4.3 Program association Table
            srs_freep(payload_);
            payload_ = new SrsTsPayloadPAT(this);
        } else {
            SrsTsChannel *channel = context_->get(pid_);
            if (channel && channel->apply_ == SrsTsPidApplyPMT) {
                // 2.4.4.8 Program Map Table
                srs_freep(payload_);
                payload_ = new SrsTsPayloadPMT(this);
            } else if (channel && (channel->apply_ == SrsTsPidApplyVideo || channel->apply_ == SrsTsPidApplyAudio)) {
                // 2.4.3.6 PES packet
                srs_freep(payload_);
                payload_ = new SrsTsPayloadPES(this);
            } else {
                // left bytes as reserved.
                stream->skip(srs_min(stream->left(), nb_payload));
            }
        }

        if (payload_ && (err = payload_->decode(stream, ppmsg)) != srs_success) {
            return srs_error_wrap(err, "ts: demux payload");
        }
    }

    return err;
}

int SrsTsPacket::size()
{
    int sz = 4;

    sz += adaptation_field_ ? adaptation_field_->size() : 0;
    sz += payload_ ? payload_->size() : 0;

    return sz;
}

srs_error_t SrsTsPacket::encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // 4B ts packet header.
    if (!stream->require(4)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_HEADER, "ts: requires 4+ bytes");
    }

    stream->write_1bytes(sync_byte_);

    int16_t pidv = pid_ & 0x1FFF;
    pidv |= (transport_priority_ << 13) & 0x2000;
    pidv |= (transport_error_indicator_ << 15) & 0x8000;
    pidv |= (payload_unit_start_indicator_ << 14) & 0x4000;
    stream->write_2bytes(pidv);

    int8_t ccv = continuity_counter_ & 0x0F;
    ccv |= (transport_scrambling_control_ << 6) & 0xC0;
    ccv |= (adaption_field_control_ << 4) & 0x30;
    stream->write_1bytes(ccv);

    srs_info("ts: header sync=%#x error=%d unit_start=%d priotiry=%d pid=%d scrambling=%d adaption=%d counter=%d",
             sync_byte, transport_error_indicator, payload_unit_start_indicator, transport_priority, pid,
             transport_scrambling_control, adaption_field_control, continuity_counter);

    // optional: adaptation field
    if (adaptation_field_) {
        if ((err = adaptation_field_->encode(stream)) != srs_success) {
            return srs_error_wrap(err, "ts: mux af field");
        }
        srs_verbose("ts: mux af ok.");
    }

    // optional: payload.
    if (payload_) {
        if ((err = payload_->encode(stream)) != srs_success) {
            return srs_error_wrap(err, "ts: mux payload");
        }
        srs_verbose("ts: mux payload ok.");
    }

    return err;
}

void SrsTsPacket::padding(int nb_stuffings)
{
    if (!adaptation_field_) {
        SrsTsAdaptationField *af = new SrsTsAdaptationField(this);
        adaptation_field_ = af;

        af->adaption_field_length_ = 0; // calc in size.
        af->discontinuity_indicator_ = 0;
        af->random_access_indicator_ = 0;
        af->elementary_stream_priority_indicator_ = 0;
        af->PCR_flag_ = 0;
        af->OPCR_flag_ = 0;
        af->splicing_point_flag_ = 0;
        af->transport_private_data_flag_ = 0;
        af->adaptation_field_extension_flag_ = 0;

        // consume the af size if possible.
        nb_stuffings = srs_max(0, nb_stuffings - af->size());
    }

    adaptation_field_->nb_af_reserved_ = nb_stuffings;

    // set payload with af.
    if (adaption_field_control_ == SrsTsAdaptationFieldTypePayloadOnly) {
        adaption_field_control_ = SrsTsAdaptationFieldTypeBoth;
    }
}

SrsTsPacket *SrsTsPacket::create_pat(ISrsTsContext *context, int16_t pmt_number, int16_t pmt_pid)
{
    SrsTsPacket *pkt = new SrsTsPacket(context);
    pkt->sync_byte_ = 0x47;
    pkt->transport_error_indicator_ = 0;
    pkt->payload_unit_start_indicator_ = 1;
    pkt->transport_priority_ = 0;
    pkt->pid_ = SrsTsPidPAT;
    pkt->transport_scrambling_control_ = SrsTsScrambledDisabled;
    pkt->adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;
    pkt->continuity_counter_ = 0;
    pkt->adaptation_field_ = NULL;
    SrsTsPayloadPAT *pat = new SrsTsPayloadPAT(pkt);
    pkt->payload_ = pat;

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

SrsTsPacket *SrsTsPacket::create_pmt(ISrsTsContext *context,
                                     int16_t pmt_number, int16_t pmt_pid, int16_t vpid, SrsTsStream vs, int16_t apid, SrsTsStream as)
{
    SrsTsPacket *pkt = new SrsTsPacket(context);
    pkt->sync_byte_ = 0x47;
    pkt->transport_error_indicator_ = 0;
    pkt->payload_unit_start_indicator_ = 1;
    pkt->transport_priority_ = 0;
    pkt->pid_ = (SrsTsPid)pmt_pid;
    pkt->transport_scrambling_control_ = SrsTsScrambledDisabled;
    pkt->adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;
    // TODO: FIXME: maybe should continuous in channel.
    pkt->continuity_counter_ = 0;
    pkt->adaptation_field_ = NULL;
    SrsTsPayloadPMT *pmt = new SrsTsPayloadPMT(pkt);
    pkt->payload_ = pmt;

    pmt->pointer_field = 0;
    pmt->table_id = SrsTsPsiIdPms;
    pmt->section_syntax_indicator = 1;
    pmt->section_length = 0; // calc in size.
    pmt->program_number = pmt_number;
    pmt->version_number = 0;
    pmt->current_next_indicator = 1;
    pmt->section_number = 0;
    pmt->last_section_number = 0;

    // Here we must get the correct codec.
    bool codec_ok = (vs == SrsTsStreamVideoH264 || as == SrsTsStreamAudioAAC || as == SrsTsStreamAudioMp3);
    codec_ok = codec_ok ? true : (vs == SrsTsStreamVideoHEVC);
    srs_assert(codec_ok);

    // if mp3 or aac specified, use audio to carry pcr.
    if (as == SrsTsStreamAudioAAC || as == SrsTsStreamAudioMp3) {
        // use audio to carray pcr by default.
        // for hls, there must be atleast one audio channel.
        pmt->PCR_PID = apid;
        pmt->infos.push_back(new SrsTsPayloadPMTESInfo(as, apid));
    }

    // If h.264/h.265 specified, use video to carry pcr.
    codec_ok = (vs == SrsTsStreamVideoH264);
    codec_ok = codec_ok ? true : (vs == SrsTsStreamVideoHEVC);
    if (codec_ok) {
        pmt->PCR_PID = vpid;
        pmt->infos.push_back(new SrsTsPayloadPMTESInfo(vs, vpid));
    }

    pmt->CRC_32 = 0; // calc in encode.
    return pkt;
}

SrsTsPacket *SrsTsPacket::create_pes_first(ISrsTsContext *context,
                                           int16_t pid, SrsTsPESStreamId sid, uint8_t continuity_counter, bool discontinuity,
                                           int64_t pcr, int64_t dts, int64_t pts, int size)
{
    SrsTsPacket *pkt = new SrsTsPacket(context);
    pkt->sync_byte_ = 0x47;
    pkt->transport_error_indicator_ = 0;
    pkt->payload_unit_start_indicator_ = 1;
    pkt->transport_priority_ = 0;
    pkt->pid_ = (SrsTsPid)pid;
    pkt->transport_scrambling_control_ = SrsTsScrambledDisabled;
    pkt->adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;
    pkt->continuity_counter_ = continuity_counter;
    pkt->adaptation_field_ = NULL;
    SrsTsPayloadPES *pes = new SrsTsPayloadPES(pkt);
    pkt->payload_ = pes;

    if (pcr >= 0) {
        // Ignore coverage for PCR, we don't use it in HLS.
        // LCOV_EXCL_START
        SrsTsAdaptationField *af = new SrsTsAdaptationField(pkt);
        pkt->adaptation_field_ = af;
        pkt->adaption_field_control_ = SrsTsAdaptationFieldTypeBoth;

        af->adaption_field_length_ = 0; // calc in size.
        af->discontinuity_indicator_ = discontinuity;
        af->random_access_indicator_ = 0;
        af->elementary_stream_priority_indicator_ = 0;
        af->PCR_flag_ = 1;
        af->OPCR_flag_ = 0;
        af->splicing_point_flag_ = 0;
        af->transport_private_data_flag_ = 0;
        af->adaptation_field_extension_flag_ = 0;
        af->program_clock_reference_base_ = pcr;
        af->program_clock_reference_extension_ = 0;
        // LCOV_EXCL_STOP
    }

    pes->pes_.packet_start_code_prefix_ = 0x01;
    pes->pes_.stream_id_ = (uint8_t)sid;
    pes->pes_.PES_packet_length_ = (size > 0xFFFF) ? 0 : size;
    pes->pes_.PES_scrambling_control_ = 0;
    pes->pes_.PES_priority_ = 0;
    pes->pes_.data_alignment_indicator_ = 0;
    pes->pes_.copyright_ = 0;
    pes->pes_.original_or_copy_ = 0;
    pes->pes_.PTS_DTS_flags_ = (dts == pts) ? 0x02 : 0x03;
    pes->pes_.ESCR_flag_ = 0;
    pes->pes_.ES_rate_flag_ = 0;
    pes->pes_.DSM_trick_mode_flag_ = 0;
    pes->pes_.additional_copy_info_flag_ = 0;
    pes->pes_.PES_CRC_flag_ = 0;
    pes->pes_.PES_extension_flag_ = 0;
    pes->pes_.PES_header_data_length_ = 0; // calc in size.
    pes->pes_.pts_ = pts;
    pes->pes_.dts_ = dts;
    return pkt;
}

SrsTsPacket *SrsTsPacket::create_pes_continue(ISrsTsContext *context, int16_t pid, SrsTsPESStreamId sid, uint8_t continuity_counter)
{
    SrsTsPacket *pkt = new SrsTsPacket(context);
    pkt->sync_byte_ = 0x47;
    pkt->transport_error_indicator_ = 0;
    pkt->payload_unit_start_indicator_ = 0;
    pkt->transport_priority_ = 0;
    pkt->pid_ = (SrsTsPid)pid;
    pkt->transport_scrambling_control_ = SrsTsScrambledDisabled;
    pkt->adaption_field_control_ = SrsTsAdaptationFieldTypePayloadOnly;
    pkt->continuity_counter_ = continuity_counter;
    pkt->adaptation_field_ = NULL;
    pkt->payload_ = NULL;

    return pkt;
}

SrsTsAdaptationField::SrsTsAdaptationField(SrsTsPacket *pkt)
{
    packet_ = pkt;

    adaption_field_length_ = 0;
    discontinuity_indicator_ = 0;
    random_access_indicator_ = 0;
    elementary_stream_priority_indicator_ = 0;
    PCR_flag_ = 0;
    OPCR_flag_ = 0;
    splicing_point_flag_ = 0;
    transport_private_data_flag_ = 0;
    adaptation_field_extension_flag_ = 0;
    program_clock_reference_base_ = 0;
    program_clock_reference_extension_ = 0;
    original_program_clock_reference_base_ = 0;
    original_program_clock_reference_extension_ = 0;
    splice_countdown_ = 0;
    adaptation_field_extension_length_ = 0;
    ltw_flag_ = 0;
    piecewise_rate_flag_ = 0;
    seamless_splice_flag_ = 0;
    ltw_valid_flag_ = 0;
    ltw_offset_ = 0;
    piecewise_rate_ = 0;
    splice_type_ = 0;
    DTS_next_AU0_ = 0;
    marker_bit0_ = 0;
    DTS_next_AU1_ = 0;
    marker_bit1_ = 0;
    DTS_next_AU2_ = 0;
    marker_bit2_ = 0;
    nb_af_ext_reserved_ = 0;
    nb_af_reserved_ = 0;

    const1_value0_ = 0x3F;
    const1_value1_ = 0x1F;
    const1_value2_ = 0x3F;
}

SrsTsAdaptationField::~SrsTsAdaptationField()
{
}

srs_error_t SrsTsAdaptationField::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(2)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: decode af");
    }
    adaption_field_length_ = stream->read_1bytes();

    // When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
    // be in the range 0 to 182.
    if (packet_->adaption_field_control_ == SrsTsAdaptationFieldTypeBoth && adaption_field_length_ > 182) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af length failed, must in [0, 182], actual=%d", adaption_field_length_);
    }
    // When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
    // be 183.
    if (packet_->adaption_field_control_ == SrsTsAdaptationFieldTypeAdaptionOnly && adaption_field_length_ != 183) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af length failed, must be 183, actual=%d", adaption_field_length_);
    }

    // no adaptation field.
    if (adaption_field_length_ == 0) {
        srs_info("ts: demux af empty.");
        return err;
    }

    // the adaptation field start at here.
    int pos_af = stream->pos();
    int8_t tmpv = stream->read_1bytes();

    discontinuity_indicator_ = (tmpv >> 7) & 0x01;
    random_access_indicator_ = (tmpv >> 6) & 0x01;
    elementary_stream_priority_indicator_ = (tmpv >> 5) & 0x01;
    PCR_flag_ = (tmpv >> 4) & 0x01;
    OPCR_flag_ = (tmpv >> 3) & 0x01;
    splicing_point_flag_ = (tmpv >> 2) & 0x01;
    transport_private_data_flag_ = (tmpv >> 1) & 0x01;
    adaptation_field_extension_flag_ = tmpv & 0x01;

    if (PCR_flag_) {
        if (!stream->require(6)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af PCR_flag");
        }

        char *pp = NULL;
        char *p = stream->data() + stream->pos();
        stream->skip(6);

        int64_t pcrv = 0;
        pp = (char *)&pcrv;
        pp[5] = *p++;
        pp[4] = *p++;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;

        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        program_clock_reference_extension_ = pcrv & 0x1ff;
        const1_value0_ = (pcrv >> 9) & 0x3F;
        program_clock_reference_base_ = (pcrv >> 15) & 0x1ffffffffLL;
    }

    // Ignore coverage for bellow, we don't use it in HLS.
    // LCOV_EXCL_START
    if (OPCR_flag_) {
        if (!stream->require(6)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af OPCR_flag");
        }

        char *pp = NULL;
        char *p = stream->data() + stream->pos();
        stream->skip(6);

        int64_t opcrv = 0;
        pp = (char *)&opcrv;
        pp[5] = *p++;
        pp[4] = *p++;
        pp[3] = *p++;
        pp[2] = *p++;
        pp[1] = *p++;
        pp[0] = *p++;

        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        original_program_clock_reference_extension_ = opcrv & 0x1ff;
        const1_value2_ = (opcrv >> 9) & 0x3F;
        original_program_clock_reference_base_ = (opcrv >> 15) & 0x1ffffffffLL;
    }

    if (splicing_point_flag_) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af splicing_point_flag");
        }
        splice_countdown_ = stream->read_1bytes();
    }

    if (transport_private_data_flag_) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af transport_private_data_flag");
        }
        /**
         * The transport_private_data_length is an 8-bit field specifying the number of
         * private_data bytes immediately following the transport private_data_length field. The number of private_data bytes shall
         * not be such that private data extends beyond the adaptation field.
         */
        uint8_t transport_private_data_length = (uint8_t)stream->read_1bytes();

        if (transport_private_data_length > 0) {
            if (!stream->require(transport_private_data_length)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af transport_private_data");
            }
            transport_private_data_.resize(transport_private_data_length);
            stream->read_bytes(&transport_private_data_[0], transport_private_data_length);
        }
    }

    if (adaptation_field_extension_flag_) {
        int pos_af_ext = stream->pos();

        if (!stream->require(2)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af adaptation_field_extension_flag");
        }
        adaptation_field_extension_length_ = (uint8_t)stream->read_1bytes();
        int8_t ltwfv = stream->read_1bytes();

        piecewise_rate_flag_ = (ltwfv >> 6) & 0x01;
        seamless_splice_flag_ = (ltwfv >> 5) & 0x01;
        ltw_flag_ = (ltwfv >> 7) & 0x01;
        const1_value1_ = ltwfv & 0x1F;

        if (ltw_flag_) {
            if (!stream->require(2)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af ltw_flag");
            }
            ltw_offset_ = stream->read_2bytes();

            ltw_valid_flag_ = (ltw_offset_ >> 15) & 0x01;
            ltw_offset_ &= 0x7FFF;
        }

        if (piecewise_rate_flag_) {
            if (!stream->require(3)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af piecewise_rate_flag");
            }
            piecewise_rate_ = stream->read_3bytes();

            piecewise_rate_ &= 0x3FFFFF;
        }

        if (seamless_splice_flag_) {
            if (!stream->require(5)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: demux af seamless_splice_flag");
            }
            marker_bit0_ = stream->read_1bytes();
            DTS_next_AU1_ = stream->read_2bytes();
            DTS_next_AU2_ = stream->read_2bytes();

            splice_type_ = (marker_bit0_ >> 4) & 0x0F;
            DTS_next_AU0_ = (marker_bit0_ >> 1) & 0x07;
            marker_bit0_ &= 0x01;

            marker_bit1_ = DTS_next_AU1_ & 0x01;
            DTS_next_AU1_ = (DTS_next_AU1_ >> 1) & 0x7FFF;

            marker_bit2_ = DTS_next_AU2_ & 0x01;
            DTS_next_AU2_ = (DTS_next_AU2_ >> 1) & 0x7FFF;
        }

        nb_af_ext_reserved_ = adaptation_field_extension_length_ - (stream->pos() - pos_af_ext);
        stream->skip(nb_af_ext_reserved_);
    }
    // LCOV_EXCL_STOP

    nb_af_reserved_ = adaption_field_length_ - (stream->pos() - pos_af);
    stream->skip(nb_af_reserved_);

    srs_info("ts: af parsed, discontinuity=%d random=%d priority=%d PCR=%d OPCR=%d slicing=%d private=%d extension=%d/%d pcr=%" PRId64 "/%d opcr=%" PRId64 "/%d",
             discontinuity_indicator, random_access_indicator, elementary_stream_priority_indicator, PCR_flag, OPCR_flag, splicing_point_flag,
             transport_private_data_flag, adaptation_field_extension_flag, adaptation_field_extension_length, program_clock_reference_base,
             program_clock_reference_extension, original_program_clock_reference_base, original_program_clock_reference_extension);

    return err;
}

int SrsTsAdaptationField::size()
{
    int sz = 2;

    sz += PCR_flag_ ? 6 : 0;
    sz += OPCR_flag_ ? 6 : 0;
    sz += splicing_point_flag_ ? 1 : 0;
    sz += transport_private_data_flag_ ? 1 + transport_private_data_.size() : 0;
    sz += adaptation_field_extension_flag_ ? 2 + adaptation_field_extension_length_ : 0;
    sz += nb_af_ext_reserved_;
    sz += nb_af_reserved_;

    adaption_field_length_ = sz - 1;

    return sz;
}

srs_error_t SrsTsAdaptationField::encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (!stream->require(2)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af");
    }
    stream->write_1bytes(adaption_field_length_);

    // When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
    // be in the range 0 to 182.
    if (packet_->adaption_field_control_ == SrsTsAdaptationFieldTypeBoth && adaption_field_length_ > 182) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af length failed, must in [0, 182], actual=%d", adaption_field_length_);
    }
    // When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
    // be 183.
    if (packet_->adaption_field_control_ == SrsTsAdaptationFieldTypeAdaptionOnly && adaption_field_length_ != 183) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af length failed, must be 183, actual=%d", adaption_field_length_);
    }

    // no adaptation field.
    if (adaption_field_length_ == 0) {
        srs_info("ts: mux af empty.");
        return err;
    }
    int8_t tmpv = adaptation_field_extension_flag_ & 0x01;
    tmpv |= (discontinuity_indicator_ << 7) & 0x80;
    tmpv |= (random_access_indicator_ << 6) & 0x40;
    tmpv |= (elementary_stream_priority_indicator_ << 5) & 0x20;
    tmpv |= (PCR_flag_ << 4) & 0x10;
    tmpv |= (OPCR_flag_ << 3) & 0x08;
    tmpv |= (splicing_point_flag_ << 2) & 0x04;
    tmpv |= (transport_private_data_flag_ << 1) & 0x02;
    stream->write_1bytes(tmpv);

    // Ignore the coverage bellow, for we don't use them in HLS.
    // LCOV_EXCL_START
    if (PCR_flag_) {
        if (!stream->require(6)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af PCR_flag");
        }

        char *pp = NULL;
        char *p = stream->data() + stream->pos();
        stream->skip(6);

        // @remark, use pcr base and ignore the extension
        // @see https://github.com/ossrs/srs/issues/250#issuecomment-71349370
        int64_t pcrv = program_clock_reference_extension_ & 0x1ff;
        pcrv |= (const1_value0_ << 9) & 0x7E00;
        pcrv |= (program_clock_reference_base_ << 15) & 0xFFFFFFFF8000LL;

        pp = (char *)&pcrv;
        *p++ = pp[5];
        *p++ = pp[4];
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }

    if (OPCR_flag_) {
        if (!stream->require(6)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af OPCR_flag");
        }
        stream->skip(6);
        srs_warn("ts: mux af ignore OPCR");
    }

    if (splicing_point_flag_) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af splicing_point_flag");
        }
        stream->write_1bytes(splice_countdown_);
    }

    if (transport_private_data_flag_) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af transport_private_data_flag");
        }
        stream->write_1bytes(transport_private_data_.size());

        if (!transport_private_data_.empty()) {
            if (!stream->require((int)transport_private_data_.size())) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af transport_private_data");
            }
            stream->write_bytes(&transport_private_data_[0], (int)transport_private_data_.size());
        }
    }

    if (adaptation_field_extension_flag_) {
        if (!stream->require(2)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af adaptation_field_extension_flag");
        }
        stream->write_1bytes(adaptation_field_extension_length_);
        int8_t ltwfv = const1_value1_ & 0x1F;
        ltwfv |= (ltw_flag_ << 7) & 0x80;
        ltwfv |= (piecewise_rate_flag_ << 6) & 0x40;
        ltwfv |= (seamless_splice_flag_ << 5) & 0x20;
        stream->write_1bytes(ltwfv);

        if (ltw_flag_) {
            if (!stream->require(2)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af ltw_flag");
            }
            stream->skip(2);
            srs_warn("ts: mux af ignore ltw");
        }

        if (piecewise_rate_flag_) {
            if (!stream->require(3)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af piecewise_rate_flag");
            }
            stream->skip(3);
            srs_warn("ts: mux af ignore piecewise_rate");
        }

        if (seamless_splice_flag_) {
            if (!stream->require(5)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_AF, "ts: mux af seamless_splice_flag");
            }
            stream->skip(5);
            srs_warn("ts: mux af ignore seamless_splice");
        }

        if (nb_af_ext_reserved_) {
            stream->skip(nb_af_ext_reserved_);
        }
    }
    // LCOV_EXCL_STOP

    if (nb_af_reserved_) {
        stream->skip(nb_af_reserved_);
    }

    srs_info("ts: af parsed, discontinuity=%d random=%d priority=%d PCR=%d OPCR=%d slicing=%d private=%d extension=%d/%d pcr=%" PRId64 "/%d opcr=%" PRId64 "/%d",
             discontinuity_indicator, random_access_indicator, elementary_stream_priority_indicator, PCR_flag, OPCR_flag, splicing_point_flag,
             transport_private_data_flag, adaptation_field_extension_flag, adaptation_field_extension_length, program_clock_reference_base,
             program_clock_reference_extension, original_program_clock_reference_base, original_program_clock_reference_extension);

    return err;
}

SrsTsPayload::SrsTsPayload(SrsTsPacket *p)
{
    packet_ = p;
}

SrsTsPayload::~SrsTsPayload()
{
}

SrsMpegPES::SrsMpegPES()
{
    nb_stuffings_ = 0;
    nb_bytes_ = 0;
    nb_paddings_ = 0;
    const2bits_ = 0x02;
    const1_value0_ = 0x07;

    packet_start_code_prefix_ = 0;
    stream_id_ = 0;
    PES_packet_length_ = 0;
    PES_scrambling_control_ = 0;
    PES_priority_ = 0;
    data_alignment_indicator_ = 0;
    copyright_ = 0;
    original_or_copy_ = 0;
    PTS_DTS_flags_ = 0;
    ESCR_flag_ = 0;
    ES_rate_flag_ = 0;
    DSM_trick_mode_flag_ = 0;
    additional_copy_info_flag_ = 0;
    PES_CRC_flag_ = 0;
    PES_extension_flag_ = 0;
    PES_header_data_length_ = 0;
    pts_ = dts_ = 0;
    ESCR_base_ = 0;
    ESCR_extension_ = 0;
    ES_rate_ = 0;
    trick_mode_control_ = 0;
    trick_mode_value_ = 0;
    additional_copy_info_ = 0;
    previous_PES_packet_CRC_ = 0;
    PES_private_data_flag_ = 0;
    pack_header_field_flag_ = 0;
    program_packet_sequence_counter_flag_ = 0;
    P_STD_buffer_flag_ = 0;
    PES_extension_flag_2_ = 0;
    program_packet_sequence_counter_ = 0;
    MPEG1_MPEG2_identifier_ = 0;
    original_stuff_length_ = 0;
    P_STD_buffer_scale_ = 0;
    P_STD_buffer_size_ = 0;

    has_payload_ = false;
    nb_payload_ = 0;
}

SrsMpegPES::~SrsMpegPES()
{
}

srs_error_t SrsMpegPES::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // 6B fixed header.
    if (!stream->require(6)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE");
    }
    // 3B
    packet_start_code_prefix_ = stream->read_3bytes();
    // 1B
    stream_id_ = stream->read_1bytes();
    // 2B
    PES_packet_length_ = stream->read_2bytes();

    // check the packet start prefix.
    packet_start_code_prefix_ &= 0xFFFFFF;
    if (packet_start_code_prefix_ != 0x01) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PES start code failed, expect=0x01, actual=%#x", packet_start_code_prefix_);
    }
    int pos_packet = stream->pos();

    // @remark the sid indicates the elementary stream format.
    //      the SrsTsPESStreamIdAudio and SrsTsPESStreamIdVideo is start by 0b110 or 0b1110
    SrsTsPESStreamId sid = (SrsTsPESStreamId)stream_id_;
    if (sid != SrsTsPESStreamIdProgramStreamMap && sid != SrsTsPESStreamIdPaddingStream && sid != SrsTsPESStreamIdPrivateStream2 && sid != SrsTsPESStreamIdEcmStream && sid != SrsTsPESStreamIdEmmStream && sid != SrsTsPESStreamIdProgramStreamDirectory && sid != SrsTsPESStreamIdDsmccStream && sid != SrsTsPESStreamIdH2221TypeE) {
        // 3B flags.
        if (!stream->require(3)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE flags");
        }
        // 1B
        int8_t oocv = stream->read_1bytes();
        // 1B
        int8_t pefv = stream->read_1bytes();
        // 1B
        PES_header_data_length_ = stream->read_1bytes();
        // position of header start.
        int pos_header = stream->pos();

        const2bits_ = (oocv >> 6) & 0x03;
        PES_scrambling_control_ = (oocv >> 4) & 0x03;
        PES_priority_ = (oocv >> 3) & 0x01;
        data_alignment_indicator_ = (oocv >> 2) & 0x01;
        copyright_ = (oocv >> 1) & 0x01;
        original_or_copy_ = oocv & 0x01;

        PTS_DTS_flags_ = (pefv >> 6) & 0x03;
        ESCR_flag_ = (pefv >> 5) & 0x01;
        ES_rate_flag_ = (pefv >> 4) & 0x01;
        DSM_trick_mode_flag_ = (pefv >> 3) & 0x01;
        additional_copy_info_flag_ = (pefv >> 2) & 0x01;
        PES_CRC_flag_ = (pefv >> 1) & 0x01;
        PES_extension_flag_ = pefv & 0x01;

        // check required together.
        int nb_required = 0;
        nb_required += (PTS_DTS_flags_ == 0x2) ? 5 : 0;
        nb_required += (PTS_DTS_flags_ == 0x3) ? 10 : 0;
        nb_required += ESCR_flag_ ? 6 : 0;
        nb_required += ES_rate_flag_ ? 3 : 0;
        nb_required += DSM_trick_mode_flag_ ? 1 : 0;
        nb_required += additional_copy_info_flag_ ? 1 : 0;
        nb_required += PES_CRC_flag_ ? 2 : 0;
        nb_required += PES_extension_flag_ ? 1 : 0;
        if (!stream->require(nb_required)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE payload");
        }

        // 5B
        if (PTS_DTS_flags_ == 0x2) {
            if ((err = decode_33bits_dts_pts(stream, &pts_)) != srs_success) {
                return srs_error_wrap(err, "dts/pts");
            }
            dts_ = pts_;
        }

        // 10B
        if (PTS_DTS_flags_ == 0x3) {
            if ((err = decode_33bits_dts_pts(stream, &pts_)) != srs_success) {
                return srs_error_wrap(err, "dts/pts");
            }
            if ((err = decode_33bits_dts_pts(stream, &dts_)) != srs_success) {
                return srs_error_wrap(err, "dts/pts");
            }

            // check sync, the diff of dts and pts should never greater than 1s.
            if (dts_ - pts_ > 90000 || pts_ - dts_ > 90000) {
                srs_warn("ts: sync dts=%" PRId64 ", pts=%" PRId64, dts_, pts_);
            }
        }

        // Ignore coverage bellow, for we don't use them in HLS.
        // LCOV_EXCL_START

        // 6B
        if (ESCR_flag_) {
            ESCR_extension_ = 0;
            ESCR_base_ = 0;

            stream->skip(6);
            srs_warn("ts: demux PES, ignore the escr.");
        }

        // 3B
        if (ES_rate_flag_) {
            ES_rate_ = stream->read_3bytes();

            ES_rate_ = ES_rate_ >> 1;
            ES_rate_ &= 0x3FFFFF;
        }

        // 1B
        if (DSM_trick_mode_flag_) {
            trick_mode_control_ = stream->read_1bytes();

            trick_mode_value_ = trick_mode_control_ & 0x1f;
            trick_mode_control_ = (trick_mode_control_ >> 5) & 0x03;
        }

        // 1B
        if (additional_copy_info_flag_) {
            additional_copy_info_ = stream->read_1bytes();

            additional_copy_info_ &= 0x7f;
        }

        // 2B
        if (PES_CRC_flag_) {
            previous_PES_packet_CRC_ = stream->read_2bytes();
        }

        // 1B
        if (PES_extension_flag_) {
            int8_t efv = stream->read_1bytes();

            PES_private_data_flag_ = (efv >> 7) & 0x01;
            pack_header_field_flag_ = (efv >> 6) & 0x01;
            program_packet_sequence_counter_flag_ = (efv >> 5) & 0x01;
            P_STD_buffer_flag_ = (efv >> 4) & 0x01;
            const1_value0_ = (efv >> 1) & 0x07;
            PES_extension_flag_2_ = efv & 0x01;

            nb_required = 0;
            nb_required += PES_private_data_flag_ ? 16 : 0;
            nb_required += pack_header_field_flag_ ? 1 : 0; // 1+x bytes.
            nb_required += program_packet_sequence_counter_flag_ ? 2 : 0;
            nb_required += P_STD_buffer_flag_ ? 2 : 0;
            nb_required += PES_extension_flag_2_ ? 1 : 0; // 1+x bytes.
            if (!stream->require(nb_required)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE ext payload");
            }

            // 16B
            if (PES_private_data_flag_) {
                PES_private_data_.resize(16);
                stream->read_bytes(&PES_private_data_[0], 16);
            }

            // (1+x)B
            if (pack_header_field_flag_) {
                // This is an 8-bit field which indicates the length, in bytes, of the pack_header_field()
                uint8_t pack_field_length = stream->read_1bytes();
                if (pack_field_length > 0) {
                    // the adjust required bytes.
                    nb_required = nb_required - 16 - 1 + pack_field_length;
                    if (!stream->require(nb_required)) {
                        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE ext pack");
                    }
                    pack_field_.resize(pack_field_length);
                    stream->read_bytes(&pack_field_[0], pack_field_length);
                }
            }

            // 2B
            if (program_packet_sequence_counter_flag_) {
                program_packet_sequence_counter_ = stream->read_1bytes();
                program_packet_sequence_counter_ &= 0x7f;

                original_stuff_length_ = stream->read_1bytes();
                MPEG1_MPEG2_identifier_ = (original_stuff_length_ >> 6) & 0x01;
                original_stuff_length_ &= 0x3f;
            }

            // 2B
            if (P_STD_buffer_flag_) {
                P_STD_buffer_size_ = stream->read_2bytes();

                // '01'
                // int8_t const2bits = (P_STD_buffer_scale >>14) & 0x03;

                P_STD_buffer_scale_ = (P_STD_buffer_size_ >> 13) & 0x01;
                P_STD_buffer_size_ &= 0x1FFF;
            }

            // (1+x)B
            if (PES_extension_flag_2_) {
                /**
                 * This is a 7-bit field which specifies the length, in bytes, of the data following this field in
                 * the PES extension field up to and including any reserved bytes.
                 */
                uint8_t PES_extension_field_length = stream->read_1bytes();
                PES_extension_field_length &= 0x7F;

                if (PES_extension_field_length > 0) {
                    if (!stream->require(PES_extension_field_length)) {
                        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE ext field");
                    }
                    PES_extension_field_.resize(PES_extension_field_length);
                    stream->read_bytes(&PES_extension_field_[0], PES_extension_field_length);
                }
            }
        }

        // stuffing_byte
        nb_stuffings_ = PES_header_data_length_ - (stream->pos() - pos_header);
        if (nb_stuffings_ > 0) {
            if (!stream->require(nb_stuffings_)) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE stuffings");
            }
            stream->skip(nb_stuffings_);
        }

        // LCOV_EXCL_STOP

        // PES_packet_data_byte, page58.
        // the packet size contains the header size.
        // The number of PES_packet_data_bytes, N, is specified by the
        // PES_packet_length field. N shall be equal to the value
        // indicated in the PES_packet_length minus the number of bytes
        // between the last byte of the PES_packet_length field and the
        // first PES_packet_data_byte.
        //
        // If the actual size > uin16_t, which exceed the PES_packet_length, then PES_packet_length is 0, and we
        // should dump all left bytes in stream to message util next unit start packet.
        // Otherwise, the PES_packet_length should greater than 0, which is a specified length, then we also dump
        // the left bytes in stream, in such case, the nb_payload_ is the actual size of payload.
        if (PES_packet_length_ > 0) {
            int nb_packet = PES_packet_length_ - (stream->pos() - pos_packet);
            if (nb_packet < 0) {
                return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: Invalid PES_packet_length=%d, pos_packet=%d, pos=%d", PES_packet_length_, pos_packet, stream->pos());
            }
            nb_payload_ = nb_packet;
        }

        // Now, it has payload. The size is specified by PES_packet_length, which might be:
        //      0, Dump all bytes in stream util next unit start packet.
        //      nb_payload_, Dump specified bytes in stream.
        has_payload_ = true;

        // Ignore coverage bellow, for we don't use them in HLS.
        // LCOV_EXCL_START
    } else if (sid == SrsTsPESStreamIdProgramStreamMap || sid == SrsTsPESStreamIdPrivateStream2 || sid == SrsTsPESStreamIdEcmStream || sid == SrsTsPESStreamIdEmmStream || sid == SrsTsPESStreamIdProgramStreamDirectory || sid == SrsTsPESStreamIdDsmccStream || sid == SrsTsPESStreamIdH2221TypeE) {
        // for (i = 0; i < PES_packet_length; i++) {
        //         PES_packet_data_byte
        // }

        // For PS, the PES packet should never be empty, because there is no continuity for PS packet.
        if (PES_packet_length_ <= 0) {
            return srs_error_new(ERROR_GB_PS_PSE, "ts: Invalid PES_packet_length=%d for PS", PES_packet_length_);
        }

        // The pos_packet equals to stream pos, so the PES_packet_length is actually the payload length.
        nb_payload_ = PES_packet_length_;
        has_payload_ = true;
    } else if (sid == SrsTsPESStreamIdPaddingStream) {
        // for (i = 0; i < PES_packet_length; i++) {
        //         padding_byte
        // }
        nb_paddings_ = stream->size() - stream->pos();
        stream->skip(nb_paddings_);
        srs_info("ts: drop %dB padding bytes", nb_paddings);

        // LCOV_EXCL_STOP
    } else {
        int nb_drop = stream->size() - stream->pos();
        stream->skip(nb_drop);
        srs_warn("ts: drop the pes packet %dB for stream_id=%#x", nb_drop, stream_id_);
    }

    return err;
}

int SrsMpegPES::size()
{
    int sz = 0;

    PES_header_data_length_ = 0;
    SrsTsPESStreamId sid = (SrsTsPESStreamId)stream_id_;

    if (sid != SrsTsPESStreamIdProgramStreamMap && sid != SrsTsPESStreamIdPaddingStream && sid != SrsTsPESStreamIdPrivateStream2 && sid != SrsTsPESStreamIdEcmStream && sid != SrsTsPESStreamIdEmmStream && sid != SrsTsPESStreamIdProgramStreamDirectory && sid != SrsTsPESStreamIdDsmccStream && sid != SrsTsPESStreamIdH2221TypeE) {
        sz += 6;
        sz += 3;
        PES_header_data_length_ = sz;

        sz += (PTS_DTS_flags_ == 0x2) ? 5 : 0;
        sz += (PTS_DTS_flags_ == 0x3) ? 10 : 0;
        sz += ESCR_flag_ ? 6 : 0;
        sz += ES_rate_flag_ ? 3 : 0;
        sz += DSM_trick_mode_flag_ ? 1 : 0;
        sz += additional_copy_info_flag_ ? 1 : 0;
        sz += PES_CRC_flag_ ? 2 : 0;
        sz += PES_extension_flag_ ? 1 : 0;

        if (PES_extension_flag_) {
            // Ignore coverage bellow, for we don't use them in HLS.
            // LCOV_EXCL_START
            sz += PES_private_data_flag_ ? 16 : 0;
            sz += pack_header_field_flag_ ? 1 + pack_field_.size() : 0; // 1+x bytes.
            sz += program_packet_sequence_counter_flag_ ? 2 : 0;
            sz += P_STD_buffer_flag_ ? 2 : 0;
            sz += PES_extension_flag_2_ ? 1 + PES_extension_field_.size() : 0; // 1+x bytes.
            // LCOV_EXCL_STOP
        }
        PES_header_data_length_ = sz - PES_header_data_length_;

        sz += nb_stuffings_;

        // packet bytes
    } else if (sid == SrsTsPESStreamIdProgramStreamMap || sid == SrsTsPESStreamIdPrivateStream2 || sid == SrsTsPESStreamIdEcmStream || sid == SrsTsPESStreamIdEmmStream || sid == SrsTsPESStreamIdProgramStreamDirectory || sid == SrsTsPESStreamIdDsmccStream || sid == SrsTsPESStreamIdH2221TypeE) {
        // packet bytes
    } else {
        // nb_drop
    }

    return sz;
}

srs_error_t SrsMpegPES::encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // 6B fixed header.
    if (!stream->require(6)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: mux PSE");
    }

    // 3B
    stream->write_3bytes(packet_start_code_prefix_);
    // 1B
    stream->write_1bytes(stream_id_);
    // 2B
    // the PES_packet_length is the actual bytes size, the pplv write to ts
    // is the actual bytes plus the header size.
    int32_t pplv = 0;
    if (PES_packet_length_ > 0) {
        pplv = PES_packet_length_ + 3 + PES_header_data_length_;
        pplv = (pplv > 0xFFFF) ? 0 : pplv;
    }
    stream->write_2bytes(pplv);

    // check the packet start prefix.
    packet_start_code_prefix_ &= 0xFFFFFF;
    if (packet_start_code_prefix_ != 0x01) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: mux PSE start code failed, expect=0x01, actual=%#x", packet_start_code_prefix_);
    }

    // 3B flags.
    if (!stream->require(3)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: mux PSE flags");
    }
    // 1B
    int8_t oocv = original_or_copy_ & 0x01;
    oocv |= (const2bits_ << 6) & 0xC0;
    oocv |= (PES_scrambling_control_ << 4) & 0x30;
    oocv |= (PES_priority_ << 3) & 0x08;
    oocv |= (data_alignment_indicator_ << 2) & 0x04;
    oocv |= (copyright_ << 1) & 0x02;
    stream->write_1bytes(oocv);
    // 1B
    int8_t pefv = PES_extension_flag_ & 0x01;
    pefv |= (PTS_DTS_flags_ << 6) & 0xC0;
    pefv |= (ESCR_flag_ << 5) & 0x20;
    pefv |= (ES_rate_flag_ << 4) & 0x10;
    pefv |= (DSM_trick_mode_flag_ << 3) & 0x08;
    pefv |= (additional_copy_info_flag_ << 2) & 0x04;
    pefv |= (PES_CRC_flag_ << 1) & 0x02;
    stream->write_1bytes(pefv);
    // 1B
    stream->write_1bytes(PES_header_data_length_);

    // check required together.
    int nb_required = 0;
    nb_required += (PTS_DTS_flags_ == 0x2) ? 5 : 0;
    nb_required += (PTS_DTS_flags_ == 0x3) ? 10 : 0;
    nb_required += ESCR_flag_ ? 6 : 0;
    nb_required += ES_rate_flag_ ? 3 : 0;
    nb_required += DSM_trick_mode_flag_ ? 1 : 0;
    nb_required += additional_copy_info_flag_ ? 1 : 0;
    nb_required += PES_CRC_flag_ ? 2 : 0;
    nb_required += PES_extension_flag_ ? 1 : 0;
    if (!stream->require(nb_required)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: mux PSE payload");
    }

    // 5B
    if (PTS_DTS_flags_ == 0x2) {
        if ((err = encode_33bits_dts_pts(stream, 0x02, pts_)) != srs_success) {
            return srs_error_wrap(err, "dts/pts");
        }
    }

    // 10B
    if (PTS_DTS_flags_ == 0x3) {
        if ((err = encode_33bits_dts_pts(stream, 0x03, pts_)) != srs_success) {
            return srs_error_wrap(err, "dts/pts");
        }
        if ((err = encode_33bits_dts_pts(stream, 0x01, dts_)) != srs_success) {
            return srs_error_wrap(err, "dts/pts");
        }

        // check sync, the diff of dts and pts should never greater than 1s.
        if (dts_ - pts_ > 90000 || pts_ - dts_ > 90000) {
            srs_warn("ts: sync dts=%" PRId64 ", pts=%" PRId64, dts_, pts_);
        }
    }

    // Ignore coverage bellow, for we don't use them in HLS.
    // LCOV_EXCL_START

    // 6B
    if (ESCR_flag_) {
        stream->skip(6);
        srs_warn("ts: demux PES, ignore the escr.");
    }

    // 3B
    if (ES_rate_flag_) {
        stream->skip(3);
        srs_warn("ts: demux PES, ignore the ES_rate.");
    }

    // 1B
    if (DSM_trick_mode_flag_) {
        stream->skip(1);
        srs_warn("ts: demux PES, ignore the DSM_trick_mode.");
    }

    // 1B
    if (additional_copy_info_flag_) {
        stream->skip(1);
        srs_warn("ts: demux PES, ignore the additional_copy_info.");
    }

    // 2B
    if (PES_CRC_flag_) {
        stream->skip(2);
        srs_warn("ts: demux PES, ignore the PES_CRC.");
    }

    // 1B
    if (PES_extension_flag_) {
        int8_t efv = PES_extension_flag_2_ & 0x01;
        efv |= (PES_private_data_flag_ << 7) & 0x80;
        efv |= (pack_header_field_flag_ << 6) & 0x40;
        efv |= (program_packet_sequence_counter_flag_ << 5) & 0x20;
        efv |= (P_STD_buffer_flag_ << 4) & 0x10;
        efv |= (const1_value0_ << 1) & 0xE0;
        stream->write_1bytes(efv);

        nb_required = 0;
        nb_required += PES_private_data_flag_ ? 16 : 0;
        nb_required += pack_header_field_flag_ ? 1 + pack_field_.size() : 0; // 1+x bytes.
        nb_required += program_packet_sequence_counter_flag_ ? 2 : 0;
        nb_required += P_STD_buffer_flag_ ? 2 : 0;
        nb_required += PES_extension_flag_2_ ? 1 + PES_extension_field_.size() : 0; // 1+x bytes.
        if (!stream->require(nb_required)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: mux PSE ext payload");
        }
        stream->skip(nb_required);
        srs_warn("ts: demux PES, ignore the PES_extension.");
    }

    // stuffing_byte
    if (nb_stuffings_) {
        stream->skip(nb_stuffings_);
        srs_warn("ts: demux PES, ignore the stuffings.");
    }

    // LCOV_EXCL_STOP

    return err;
}

srs_error_t SrsMpegPES::decode_33bits_dts_pts(SrsBuffer *stream, int64_t *pv)
{
    srs_error_t err = srs_success;

    if (!stream->require(5)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE dts/pts");
    }

    // decode the 33bits schema.
    // --------------1B
    // 4bits const maybe '0001', '0010' or '0011'.
    // 3bits DTS/PTS [32..30]
    // 1bit const '1'
    int64_t dts_pts_30_32 = stream->read_1bytes();
    if ((dts_pts_30_32 & 0x01) != 0x01) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE dts/pts 30-32");
    }
    // @remark, we donot check the high 4bits, maybe '0001', '0010' or '0011'.
    //      so we just ensure the high 4bits is not 0x00.
    if (((dts_pts_30_32 >> 4) & 0x0f) == 0x00) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE dts/pts 30-32");
    }
    dts_pts_30_32 = (dts_pts_30_32 >> 1) & 0x07;

    // --------------2B
    // 15bits DTS/PTS [29..15]
    // 1bit const '1'
    int64_t dts_pts_15_29 = stream->read_2bytes();
    if ((dts_pts_15_29 & 0x01) != 0x01) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE dts/pts 15-29");
    }
    dts_pts_15_29 = (dts_pts_15_29 >> 1) & 0x7fff;

    // --------------2B
    // 15bits DTS/PTS [14..0]
    // 1bit const '1'
    int64_t dts_pts_0_14 = stream->read_2bytes();
    if ((dts_pts_0_14 & 0x01) != 0x01) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PSE dts/pts 0-14");
    }
    dts_pts_0_14 = (dts_pts_0_14 >> 1) & 0x7fff;

    int64_t v = 0x00;
    v |= (dts_pts_30_32 << 30) & 0x1c0000000LL;
    v |= (dts_pts_15_29 << 15) & 0x3fff8000LL;
    v |= dts_pts_0_14 & 0x7fff;
    *pv = v;

    return err;
}

srs_error_t SrsMpegPES::encode_33bits_dts_pts(SrsBuffer *stream, uint8_t fb, int64_t v)
{
    srs_error_t err = srs_success;

    if (!stream->require(5)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: mux PSE dts/pts");
    }

    char *p = stream->data() + stream->pos();
    stream->skip(5);

    int32_t val = 0;

    val = int32_t(fb << 4 | (((v >> 30) & 0x07) << 1) | 1);
    *p++ = val;

    val = int32_t((((v >> 15) & 0x7fff) << 1) | 1);
    *p++ = (val >> 8);
    *p++ = val;

    val = int32_t((((v) & 0x7fff) << 1) | 1);
    *p++ = (val >> 8);
    *p++ = val;

    return err;
}

SrsTsPayloadPES::SrsTsPayloadPES(SrsTsPacket *p) : SrsTsPayload(p)
{
}

SrsTsPayloadPES::~SrsTsPayloadPES()
{
}

srs_error_t SrsTsPayloadPES::decode(SrsBuffer *stream, SrsTsMessage **ppmsg)
{
    srs_error_t err = srs_success;

    // find the channel from chunk.
    SrsTsChannel *channel = packet_->context_->get(packet_->pid_);
    if (!channel) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSE, "ts: demux PES no channel for pid=%#x", packet_->pid_);
    }

    // init msg.
    SrsTsMessage *msg = channel->msg_;
    if (!msg) {
        msg = new SrsTsMessage(channel, packet_);
        channel->msg_ = msg;
    }

    // we must cache the fresh state of msg,
    // for the PES_packet_length is 0, the first payload_unit_start_indicator always 1,
    // so should check for the fresh and not completed it.
    bool is_fresh_msg = msg->fresh();

    // check when fresh, the payload_unit_start_indicator
    // should be 1 for the fresh msg.
    if (is_fresh_msg && !packet_->payload_unit_start_indicator_) {
        srs_warn("ts: PES fresh packet length=%d, us=%d, cc=%d",
                 msg->PES_packet_length_, packet_->payload_unit_start_indicator_, packet_->continuity_counter_);

        stream->skip(stream->size() - stream->pos());
        srs_freep(msg);
        channel->msg_ = NULL;
        return err;
    }

    // check when not fresh and PES_packet_length>0,
    // the payload_unit_start_indicator should never be 1 when not completed.
    if (!is_fresh_msg && msg->PES_packet_length_ > 0 && !msg->completed(packet_->payload_unit_start_indicator_) && packet_->payload_unit_start_indicator_) {
        srs_warn("ts: ignore PES packet length=%d, payload=%d, us=%d, cc=%d",
                 msg->PES_packet_length_, msg->payload_->length(), packet_->payload_unit_start_indicator_, packet_->continuity_counter_);

        // reparse current msg.
        stream->skip(stream->pos() * -1);
        srs_freep(msg);
        channel->msg_ = NULL;
        return err;
    }

    // check the continuity counter
    if (!is_fresh_msg) {
        // late-incoming or duplicated continuity, drop message.
        // @remark check overflow, the counter plus 1 should greater when invalid.
        if (msg->continuity_counter_ >= packet_->continuity_counter_ && ((msg->continuity_counter_ + 1) & 0x0f) > packet_->continuity_counter_) {
            srs_warn("ts: drop PES %dB for duplicated cc=%#x", msg->continuity_counter_);
            stream->skip(stream->size() - stream->pos());
            return err;
        }

        // when got partially message, the continous count must be continuous, or drop it.
        if (((msg->continuity_counter_ + 1) & 0x0f) != packet_->continuity_counter_) {
            srs_warn("ts: ignore continuity must be continous, msg=%#x, packet=%#x", msg->continuity_counter_, packet_->continuity_counter_);

            // reparse current msg.
            stream->skip(stream->pos() * -1);
            srs_freep(msg);
            channel->msg_ = NULL;
            return err;
        }
    }
    msg->continuity_counter_ = packet_->continuity_counter_;

    // for the PES_packet_length(0), reap when completed.
    if (!is_fresh_msg && msg->completed(packet_->payload_unit_start_indicator_)) {
        // reap previous PES packet.
        *ppmsg = msg;
        channel->msg_ = NULL;

        // reparse current msg.
        stream->skip(stream->pos() * -1);
        return err;
    }

    // contious packet, append bytes for unit start is 0
    if (!packet_->payload_unit_start_indicator_) {
        if ((err = msg->dump(stream, &pes_.nb_bytes_)) != srs_success) {
            return srs_error_wrap(err, "ts: pes dump");
        }
    }

    // when unit start, parse the fresh msg.
    if (packet_->payload_unit_start_indicator_) {
        if ((err = pes_.decode(stream)) != srs_success) {
            return srs_error_wrap(err, "header");
        }

        // Update message when decode the first PES packet.
        msg->sid_ = (SrsTsPESStreamId)pes_.stream_id_;
        if (pes_.PTS_DTS_flags_ == 0x02 || pes_.PTS_DTS_flags_ == 0x03) {
            msg->dts_ = pes_.dts_;
            msg->pts_ = pes_.pts_;
        }
        if (pes_.has_payload_) {
            // The size of message, might be 0 or a positive value.
            msg->PES_packet_length_ = pes_.nb_payload_;

            // xB
            if ((err = msg->dump(stream, &pes_.nb_bytes_)) != srs_success) {
                return srs_error_wrap(err, "dump pes");
            }
        }
    }

    // when fresh and the PES_packet_length is 0,
    // the payload_unit_start_indicator always be 1,
    // the message should never EOF for the first packet.
    if (is_fresh_msg && msg->PES_packet_length_ == 0) {
        return err;
    }

    // check msg, reap when completed.
    if (msg->completed(packet_->payload_unit_start_indicator_)) {
        *ppmsg = msg;
        channel->msg_ = NULL;
        srs_info("ts: reap msg for completed.");
    }

    return err;
}

int SrsTsPayloadPES::size()
{
    return pes_.size();
}

srs_error_t SrsTsPayloadPES::encode(SrsBuffer *stream)
{
    return pes_.encode(stream);
}

SrsTsPayloadPSI::SrsTsPayloadPSI(SrsTsPacket *p) : SrsTsPayload(p)
{
    pointer_field = 0;
    const0_value = 0;
    const1_value = 3;
    CRC_32 = 0;
    section_length = 0;
    section_syntax_indicator = 0;
    table_id = SrsTsPsiIdPas;
}

SrsTsPayloadPSI::~SrsTsPayloadPSI()
{
}

srs_error_t SrsTsPayloadPSI::decode(SrsBuffer *stream, SrsTsMessage ** /*ppmsg*/)
{
    srs_error_t err = srs_success;

    /**
     * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
     * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
     * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
     * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
     * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
     * stream_type 5 (refer to Table 2-29).
     */
    if (packet_->payload_unit_start_indicator_) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: demux PSI");
        }
        pointer_field = stream->read_1bytes();
    }

    // to calc the crc32
    char *ppat = stream->data() + stream->pos();
    int pat_pos = stream->pos();

    // atleast 3B for all psi.
    if (!stream->require(3)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: demux PSI");
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
        return err;
    }

    if (!stream->require(section_length)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: demux PSI section");
    }

    // call the virtual method of actual PSI.
    if ((err = psi_decode(stream)) != srs_success) {
        return srs_error_wrap(err, "demux PSI");
    }

    // 4B
    if (!stream->require(4)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: demux PSI crc32");
    }
    CRC_32 = stream->read_4bytes();

    // verify crc32.
    int32_t crc32 = srs_crc32_mpegts(ppat, stream->pos() - pat_pos - 4);
    if (crc32 != CRC_32) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: verify PSI crc32");
    }

    // consume left stuffings
    if (!stream->empty()) {
        int nb_stuffings = stream->size() - stream->pos();
        char *stuffing = stream->data() + stream->pos();

        // all stuffing must be 0xff.
        // TODO: FIXME: maybe need to remove the following.
        for (int i = 0; i < nb_stuffings; i++) {
            if ((uint8_t)stuffing[i] != 0xff) {
                srs_warn("ts: stuff is not 0xff, actual=%#x", stuffing[i]);
                break;
            }
        }

        stream->skip(nb_stuffings);
    }

    return err;
}

int SrsTsPayloadPSI::size()
{
    int sz = 0;

    // section size is the sl plus the crc32
    section_length = psi_size() + 4;

    sz += packet_->payload_unit_start_indicator_ ? 1 : 0;
    sz += 3;
    sz += section_length;

    return sz;
}

srs_error_t SrsTsPayloadPSI::encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    if (packet_->payload_unit_start_indicator_) {
        if (!stream->require(1)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: mux PSI");
        }
        stream->write_1bytes(pointer_field);
    }

    // to calc the crc32
    char *ppat = stream->data() + stream->pos();
    int pat_pos = stream->pos();

    // atleast 3B for all psi.
    if (!stream->require(3)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: mux PSI");
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
        return err;
    }

    if (!stream->require(section_length)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: mux PSI section");
    }

    // call the virtual method of actual PSI.
    if ((err = psi_encode(stream)) != srs_success) {
        return srs_error_wrap(err, "mux PSI");
    }

    // 4B
    if (!stream->require(4)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PSI, "ts: mux PSI crc32");
    }
    CRC_32 = srs_crc32_mpegts(ppat, stream->pos() - pat_pos);
    stream->write_4bytes(CRC_32);

    return err;
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

srs_error_t SrsTsPayloadPATProgram::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // atleast 4B for PAT program specified
    if (!stream->require(4)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PAT, "ts: demux PAT");
    }

    int tmpv = stream->read_4bytes();
    number = (int16_t)((tmpv >> 16) & 0xFFFF);
    const1_value = (int16_t)((tmpv >> 13) & 0x07);
    pid = (int16_t)(tmpv & 0x1FFF);

    return err;
}

int SrsTsPayloadPATProgram::size()
{
    return 4;
}

srs_error_t SrsTsPayloadPATProgram::encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // atleast 4B for PAT program specified
    if (!stream->require(4)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PAT, "ts: mux PAT");
    }

    int tmpv = pid & 0x1FFF;
    tmpv |= (number << 16) & 0xFFFF0000;
    tmpv |= (const1_value << 13) & 0xE000;
    stream->write_4bytes(tmpv);

    return err;
}

SrsTsPayloadPAT::SrsTsPayloadPAT(SrsTsPacket *p) : SrsTsPayloadPSI(p)
{
    transport_stream_id = 0;
    const3_value = 3;
    version_number = 0;
    current_next_indicator = 0;
    section_number = 0;
    last_section_number = 0;
}

SrsTsPayloadPAT::~SrsTsPayloadPAT()
{
    std::vector<SrsTsPayloadPATProgram *>::iterator it;
    for (it = programs.begin(); it != programs.end(); ++it) {
        SrsTsPayloadPATProgram *program = *it;
        srs_freep(program);
    }
    programs.clear();
}

srs_error_t SrsTsPayloadPAT::psi_decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // atleast 5B for PAT specified
    if (!stream->require(5)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PAT, "ts: demux PAT");
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
        SrsTsPayloadPATProgram *program = new SrsTsPayloadPATProgram();

        if ((err = program->decode(stream)) != srs_success) {
            return srs_error_wrap(err, "demux PAT program");
        }

        // update the apply pid table.
        packet_->context_->set(program->pid, SrsTsPidApplyPMT);

        programs.push_back(program);
    }

    // update the apply pid table.
    packet_->context_->set(packet_->pid_, SrsTsPidApplyPAT);
    packet_->context_->on_pmt_parsed();

    return err;
}

int SrsTsPayloadPAT::psi_size()
{
    int sz = 5;
    for (int i = 0; i < (int)programs.size(); i++) {
        SrsTsPayloadPATProgram *program = programs.at(i);
        sz += program->size();
    }
    return sz;
}

srs_error_t SrsTsPayloadPAT::psi_encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // atleast 5B for PAT specified
    if (!stream->require(5)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PAT, "ts: mux PAT");
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
    for (int i = 0; i < (int)programs.size(); i++) {
        SrsTsPayloadPATProgram *program = programs.at(i);
        if ((err = program->encode(stream)) != srs_success) {
            return srs_error_wrap(err, "mux PAT program");
        }

        // update the apply pid table.
        packet_->context_->set(program->pid, SrsTsPidApplyPMT);
    }

    // update the apply pid table.
    packet_->context_->set(packet_->pid_, SrsTsPidApplyPAT);

    return err;
}

SrsTsPayloadPMTESInfo::SrsTsPayloadPMTESInfo(SrsTsStream st, int16_t epid)
{
    stream_type = st;
    elementary_PID = epid;

    const1_value0 = 7;
    const1_value1 = 0x0f;
}

SrsTsPayloadPMTESInfo::~SrsTsPayloadPMTESInfo()
{
}

srs_error_t SrsTsPayloadPMTESInfo::decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // 5B
    if (!stream->require(5)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: demux PMT");
    }

    stream_type = (SrsTsStream)stream->read_1bytes();

    int16_t epv = stream->read_2bytes();
    const1_value0 = (epv >> 13) & 0x07;
    elementary_PID = epv & 0x1FFF;

    int16_t eilv = stream->read_2bytes();
    const1_value1 = (eilv >> 12) & 0x0f;
    /**
     * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the number
     * of bytes of the descriptors of the associated program element immediately following the ES_info_length field.
     */
    int16_t ES_info_length = eilv & 0x0FFF;

    if (ES_info_length > 0) {
        if (!stream->require(ES_info_length)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: demux PMT ES_info");
        }
        ES_info.resize(ES_info_length);
        stream->read_bytes(&ES_info[0], ES_info_length);
    }

    return err;
}

int SrsTsPayloadPMTESInfo::size()
{
    return 5 + (int)ES_info.size();
}

srs_error_t SrsTsPayloadPMTESInfo::encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // 5B
    if (!stream->require(5)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: mux PMT");
    }

    stream->write_1bytes(stream_type);

    int16_t epv = elementary_PID & 0x1FFF;
    epv |= (const1_value0 << 13) & 0xE000;
    stream->write_2bytes(epv);

    int16_t eilv = ES_info.size() & 0x0FFF;
    eilv |= (const1_value1 << 12) & 0xF000;
    stream->write_2bytes(eilv);

    if (!ES_info.empty()) {
        if (!stream->require((int)ES_info.size())) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: mux PMT ES_info");
        }
        stream->write_bytes(&ES_info[0], (int)ES_info.size());
    }

    return err;
}

SrsTsPayloadPMT::SrsTsPayloadPMT(SrsTsPacket *p) : SrsTsPayloadPSI(p)
{
    const1_value0 = 3;
    const1_value1 = 7;
    const1_value2 = 0x0f;
    PCR_PID = 0;
    last_section_number = 0;
    program_number = 0;
    version_number = 0;
    current_next_indicator = 0;
    section_number = 0;
}

SrsTsPayloadPMT::~SrsTsPayloadPMT()
{
    std::vector<SrsTsPayloadPMTESInfo *>::iterator it;
    for (it = infos.begin(); it != infos.end(); ++it) {
        SrsTsPayloadPMTESInfo *info = *it;
        srs_freep(info);
    }
    infos.clear();
}

srs_error_t SrsTsPayloadPMT::psi_decode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // atleast 9B for PMT specified
    if (!stream->require(9)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: demux PMT");
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
    /**
     * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the
     * number of bytes of the descriptors immediately following the program_info_length field.
     */
    uint16_t program_info_length = pilv & 0xFFF;

    if (program_info_length > 0) {
        if (!stream->require(program_info_length)) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: demux PMT program info");
        }

        program_info_desc.resize(program_info_length);
        stream->read_bytes(&program_info_desc[0], program_info_length);
    }

    // [section_length] - 4(CRC) - 9B - [program_info_length]
    int ES_EOF_pos = stream->pos() + section_length - 4 - 9 - program_info_length;
    while (stream->pos() < ES_EOF_pos) {
        SrsTsPayloadPMTESInfo *info = new SrsTsPayloadPMTESInfo();
        infos.push_back(info);

        if ((err = info->decode(stream)) != srs_success) {
            return srs_error_wrap(err, "demux PMT program info");
        }

        // update the apply pid table
        switch (info->stream_type) {
        case SrsTsStreamVideoH264:
        case SrsTsStreamVideoHEVC:
        case SrsTsStreamVideoMpeg4:
            packet_->context_->set(info->elementary_PID, SrsTsPidApplyVideo, info->stream_type);
            break;
        case SrsTsStreamAudioAAC:
        case SrsTsStreamAudioAC3:
        case SrsTsStreamAudioDTS:
        case SrsTsStreamAudioMp3:
            packet_->context_->set(info->elementary_PID, SrsTsPidApplyAudio, info->stream_type);
            break;
        default:
            srs_warn("ts: drop pid=%#x, stream=%#x", info->elementary_PID, info->stream_type);
            break;
        }
    }

    // update the apply pid table.
    packet_->context_->set(packet_->pid_, SrsTsPidApplyPMT);

    return err;
}

int SrsTsPayloadPMT::psi_size()
{
    int sz = 9;
    sz += program_info_desc.size();
    for (int i = 0; i < (int)infos.size(); i++) {
        SrsTsPayloadPMTESInfo *info = infos.at(i);
        sz += info->size();
    }
    return sz;
}

srs_error_t SrsTsPayloadPMT::psi_encode(SrsBuffer *stream)
{
    srs_error_t err = srs_success;

    // atleast 9B for PMT specified
    if (!stream->require(9)) {
        return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: mux PMT");
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
    int16_t pilv = program_info_desc.size() & 0xFFF;
    pilv |= (const1_value2 << 12) & 0xF000;
    stream->write_2bytes(pilv);

    if (!program_info_desc.empty()) {
        if (!stream->require((int)program_info_desc.size())) {
            return srs_error_new(ERROR_STREAM_CASTER_TS_PMT, "ts: mux PMT program info");
        }

        stream->write_bytes(&program_info_desc[0], (int)program_info_desc.size());
    }

    for (int i = 0; i < (int)infos.size(); i++) {
        SrsTsPayloadPMTESInfo *info = infos.at(i);
        if ((err = info->encode(stream)) != srs_success) {
            return srs_error_wrap(err, "mux PMT program info");
        }

        // update the apply pid table
        switch (info->stream_type) {
        case SrsTsStreamVideoH264:
        case SrsTsStreamVideoHEVC:
        case SrsTsStreamVideoMpeg4:
            packet_->context_->set(info->elementary_PID, SrsTsPidApplyVideo, info->stream_type);
            break;
        case SrsTsStreamAudioAAC:
        case SrsTsStreamAudioAC3:
        case SrsTsStreamAudioDTS:
        case SrsTsStreamAudioMp3:
            packet_->context_->set(info->elementary_PID, SrsTsPidApplyAudio, info->stream_type);
            break;
        default:
            srs_warn("ts: drop pid=%#x, stream=%#x", info->elementary_PID, info->stream_type);
            break;
        }
    }

    // update the apply pid table.
    packet_->context_->set(packet_->pid_, SrsTsPidApplyPMT);

    return err;
}

ISrsTsContextWriter::ISrsTsContextWriter()
{
}

ISrsTsContextWriter::~ISrsTsContextWriter()
{
}

SrsTsContextWriter::SrsTsContextWriter(ISrsStreamWriter *w, ISrsTsContext *c, SrsAudioCodecId ac, SrsVideoCodecId vc)
{
    writer_ = w;
    context_ = c;

    acodec_ = ac;
    vcodec_ = vc;
}

SrsTsContextWriter::~SrsTsContextWriter()
{
}

srs_error_t SrsTsContextWriter::write_audio(SrsTsMessage *audio)
{
    srs_error_t err = srs_success;

    srs_info("hls: write audio codec=%d/%d, pts=%" PRId64 ", dts=%" PRId64 ", size=%d",
             acodec_, vcodec_, audio->pts, audio->dts, audio->PES_packet_length);

    if ((err = context_->encode(writer_, audio, vcodec_, acodec_)) != srs_success) {
        return srs_error_wrap(err, "ts: write audio");
    }
    srs_info("hls encode audio ok");

    return err;
}

srs_error_t SrsTsContextWriter::write_video(SrsTsMessage *video)
{
    srs_error_t err = srs_success;

    srs_info("hls: write video codec=%d/%d, pts=%" PRId64 ", dts=%" PRId64 ", size=%d",
             acodec_, vcodec_, video->pts, video->dts, video->PES_packet_length);

    if ((err = context_->encode(writer_, video, vcodec_, acodec_)) != srs_success) {
        return srs_error_wrap(err, "ts: write video");
    }
    srs_info("hls encode video ok");

    return err;
}

SrsVideoCodecId SrsTsContextWriter::vcodec()
{
    return vcodec_;
}

void SrsTsContextWriter::set_vcodec(SrsVideoCodecId v)
{
    vcodec_ = v;
}

SrsAudioCodecId SrsTsContextWriter::acodec()
{
    return acodec_;
}

void SrsTsContextWriter::set_acodec(SrsAudioCodecId v)
{
    acodec_ = v;
}

SrsEncFileWriter::SrsEncFileWriter()
{
    memset(iv, 0, 16);

    buf = new char[HLS_AES_ENCRYPT_BLOCK_LENGTH];
    memset(buf, 0, HLS_AES_ENCRYPT_BLOCK_LENGTH);

    nb_buf = 0;
    key = (unsigned char *)new AES_KEY();
}

SrsEncFileWriter::~SrsEncFileWriter()
{
    srs_freepa(buf);

    AES_KEY *k = (AES_KEY *)key;
    srs_freep(k);
}

srs_error_t SrsEncFileWriter::write(void *data, size_t count, ssize_t *pnwrite)
{
    srs_error_t err = srs_success;

    srs_assert(count == SRS_TS_PACKET_SIZE);

    if (nb_buf < HLS_AES_ENCRYPT_BLOCK_LENGTH) {
        memcpy(buf + nb_buf, (char *)data, SRS_TS_PACKET_SIZE);
        nb_buf += SRS_TS_PACKET_SIZE;
    }

    if (nb_buf == HLS_AES_ENCRYPT_BLOCK_LENGTH) {
        nb_buf = 0;

        SrsUniquePtr<char[]> cipher(new char[HLS_AES_ENCRYPT_BLOCK_LENGTH]);

        AES_KEY *k = (AES_KEY *)key;
        AES_cbc_encrypt((unsigned char *)buf, (unsigned char *)cipher.get(), HLS_AES_ENCRYPT_BLOCK_LENGTH, k, iv, AES_ENCRYPT);

        if ((err = SrsFileWriter::write(cipher.get(), HLS_AES_ENCRYPT_BLOCK_LENGTH, pnwrite)) != srs_success) {
            return srs_error_wrap(err, "write cipher");
        }
    }

    return err;
}

srs_error_t SrsEncFileWriter::config_cipher(unsigned char *key, unsigned char *iv)
{
    srs_error_t err = srs_success;

    memcpy(this->iv, iv, 16);

    AES_KEY *k = (AES_KEY *)this->key;
    if (AES_set_encrypt_key(key, 16 * 8, k)) {
        return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "set aes key failed");
    }

    return err;
}

void SrsEncFileWriter::close()
{
    if (nb_buf > 0) {
        int nb_padding = 16 - (nb_buf % 16);
        if (nb_padding > 0) {
            memset(buf + nb_buf, nb_padding, nb_padding);
        }

        SrsUniquePtr<char[]> cipher(new char[nb_buf + nb_padding]);

        AES_KEY *k = (AES_KEY *)key;
        AES_cbc_encrypt((unsigned char *)buf, (unsigned char *)cipher.get(), nb_buf + nb_padding, k, iv, AES_ENCRYPT);

        srs_error_t err = srs_success;
        if ((err = SrsFileWriter::write(cipher.get(), nb_buf + nb_padding, NULL)) != srs_success) {
            srs_warn("ignore err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }

        nb_buf = 0;
    }

    SrsFileWriter::close();
}

ISrsTsMessageCache::ISrsTsMessageCache()
{
}

ISrsTsMessageCache::~ISrsTsMessageCache()
{
}

SrsTsMessageCache::SrsTsMessageCache()
{
    audio_ = NULL;
    video_ = NULL;
}

SrsTsMessageCache::~SrsTsMessageCache()
{
    srs_freep(audio_);
    srs_freep(video_);
}

srs_error_t SrsTsMessageCache::cache_audio(SrsParsedAudioPacket *frame, int64_t dts)
{
    srs_error_t err = srs_success;

    // create the ts audio message.
    if (!audio_) {
        audio_ = new SrsTsMessage();
        audio_->write_pcr_ = false;
        audio_->dts_ = audio_->pts_ = audio_->start_pts_ = dts;
    }

    // TODO: FIXME: refine code.
    // audio->dts = dts;
    // audio->pts = audio->dts;
    audio_->sid_ = SrsTsPESStreamIdAudioCommon;

    // must be aac or mp3
    SrsAudioCodecConfig *acodec = frame->acodec();
    srs_assert(acodec->id_ == SrsAudioCodecIdAAC || acodec->id_ == SrsAudioCodecIdMP3);

    // write video to cache.
    if (acodec->id_ == SrsAudioCodecIdAAC) {
        if ((err = do_cache_aac(frame)) != srs_success) {
            return srs_error_wrap(err, "ts: cache aac");
        }
    } else {
        if ((err = do_cache_mp3(frame)) != srs_success) {
            return srs_error_wrap(err, "ts: cache mp3");
        }
    }

    return err;
}

srs_error_t SrsTsMessageCache::cache_video(SrsParsedVideoPacket *frame, int64_t dts)
{
    srs_error_t err = srs_success;

    // create the ts video message.
    if (!video_) {
        video_ = new SrsTsMessage();
        video_->write_pcr_ = (frame->frame_type_ == SrsVideoAvcFrameTypeKeyFrame);
        video_->start_pts_ = dts;
    }

    video_->dts_ = dts;
    video_->pts_ = video_->dts_ + frame->cts_ * 90;
    video_->sid_ = SrsTsPESStreamIdVideoCommon;

    // Write H.265 video frame to cache.
    if (frame && frame->vcodec()->id_ == SrsVideoCodecIdHEVC) {
        return do_cache_hevc(frame);
    }

    // Write H.264 video frame to cache.
    if ((err = do_cache_avc(frame)) != srs_success) {
        return srs_error_wrap(err, "ts: cache avc");
    }

    return err;
}

SrsTsMessage *SrsTsMessageCache::audio()
{
    return audio_;
}

void SrsTsMessageCache::set_audio(SrsTsMessage *msg)
{
    audio_ = msg;
}

SrsTsMessage *SrsTsMessageCache::video()
{
    return video_;
}

void SrsTsMessageCache::set_video(SrsTsMessage *msg)
{
    video_ = msg;
}

srs_error_t SrsTsMessageCache::do_cache_mp3(SrsParsedAudioPacket *frame)
{
    srs_error_t err = srs_success;

    // for mp3, directly write to cache.
    // TODO: FIXME: implements the ts jitter.
    for (int i = 0; i < frame->nb_samples_; i++) {
        SrsNaluSample *sample = &frame->samples_[i];
        audio_->payload_->append(sample->bytes_, sample->size_);
    }

    return err;
}

srs_error_t SrsTsMessageCache::do_cache_aac(SrsParsedAudioPacket *frame)
{
    srs_error_t err = srs_success;

    SrsAudioCodecConfig *codec = frame->acodec();
    srs_assert(codec);

    for (int i = 0; i < frame->nb_samples_; i++) {
        SrsNaluSample *sample = &frame->samples_[i];
        int32_t size = sample->size_;

        if (!sample->bytes_ || size <= 0 || size > 0x1fff) {
            return srs_error_new(ERROR_HLS_AAC_FRAME_LENGTH, "ts: invalid aac frame length=%d", size);
        }

        // the frame length is the AAC raw data plus the adts header size.
        int32_t frame_length = size + 7;

        // AAC-ADTS
        // 6.2 Audio Data Transport Stream, ADTS
        // in ISO_IEC_13818-7-AAC-2004.pdf, page 26.
        // fixed 7bytes header
        uint8_t adts_header[7] = {0xff, 0xf9, 0x00, 0x00, 0x00, 0x0f, 0xfc};
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
        SrsAacProfile aac_profile = srs_aac_rtmp2ts(codec->aac_object_);
        adts_header[2] = (aac_profile << 6) & 0xc0;
        // sampling_frequency_index 4bits
        adts_header[2] |= (codec->aac_sample_rate_ << 2) & 0x3c;
        // channel_configuration 3bits
        adts_header[2] |= (codec->aac_channels_ >> 2) & 0x01;
        adts_header[3] = (codec->aac_channels_ << 6) & 0xc0;
        // frame_length 13bits
        adts_header[3] |= (frame_length >> 11) & 0x03;
        adts_header[4] = (frame_length >> 3) & 0xff;
        adts_header[5] = ((frame_length << 5) & 0xe0);
        // adts_buffer_fullness; //11bits
        adts_header[5] |= 0x1f;

        // copy to audio buffer
        audio_->payload_->append((const char *)adts_header, sizeof(adts_header));
        audio_->payload_->append(sample->bytes_, sample->size_);
    }

    return err;
}

void srs_avc_insert_aud(SrsSimpleStream *payload, bool aud_inserted)
{
    // mux the samples in annexb format,
    // ISO_IEC_14496-10-AVC-2012.pdf, page 324.
    /**
     * 00 00 00 01 // header
     *       xxxxxxx // data bytes
     * 00 00 01 // continue header
     *       xxxxxxx // data bytes.
     *
     * nal_unit_type specifies the type of RBSP data structure contained in the NAL unit as specified in Table 7-1.
     * Table 7-1 - NAL unit type codes, syntax element categories, and NAL unit type classes
     * ISO_IEC_14496-10-AVC-2012.pdf, page 83.
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
    static uint8_t fresh_nalu_header[] = {0x00, 0x00, 0x00, 0x01};
    static uint8_t cont_nalu_header[] = {0x00, 0x00, 0x01};

    if (!aud_inserted) {
        aud_inserted = true;
        payload->append((const char *)fresh_nalu_header, 4);
    } else {
        payload->append((const char *)cont_nalu_header, 3);
    }
}

srs_error_t SrsTsMessageCache::do_cache_avc(SrsParsedVideoPacket *frame)
{
    srs_error_t err = srs_success;

    // Whether aud inserted.
    bool aud_inserted = false;

    // Insert a default AUD NALU when no AUD in samples.
    if (!frame->has_aud_) {
        // the aud(access unit delimiter) before each frame.
        // 7.3.2.4 Access unit delimiter RBSP syntax
        // ISO_IEC_14496-10-AVC-2012.pdf, page 66.
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
        // ISO_IEC_14496-10-AVC-2012.pdf, page 102.
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
        // ISO_IEC_14496-10-AVC-2012.pdf, page 105.
        static uint8_t default_aud_nalu[] = {0x09, 0xf0};
        srs_avc_insert_aud(video_->payload_, aud_inserted);
        video_->payload_->append((const char *)default_aud_nalu, 2);
    }

    SrsVideoCodecConfig *codec = frame->vcodec();
    srs_assert(codec);

    bool is_sps_pps_appended = false;

    // all sample use cont nalu header, except the sps-pps before IDR frame.
    for (int i = 0; i < frame->nb_samples_; i++) {
        SrsNaluSample *sample = &frame->samples_[i];
        int32_t size = sample->size_;

        if (!sample->bytes_ || size <= 0) {
            return srs_error_new(ERROR_HLS_AVC_SAMPLE_SIZE, "ts: invalid avc sample length=%d", size);
        }

        // 5bits, 7.3.1 NAL unit syntax,
        // ISO_IEC_14496-10-AVC-2012.pdf, page 83.
        SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(sample->bytes_[0] & 0x1f);

        // Insert sps/pps before IDR when there is no sps/pps in samples.
        // The sps/pps is parsed from sequence header(generally the first flv packet).
        if (nal_unit_type == SrsAvcNaluTypeIDR && !frame->has_sps_pps_ && !is_sps_pps_appended) {
            if (!codec->sequenceParameterSetNALUnit_.empty()) {
                srs_avc_insert_aud(video_->payload_, aud_inserted);
                video_->payload_->append(&codec->sequenceParameterSetNALUnit_[0], (int)codec->sequenceParameterSetNALUnit_.size());
            }
            if (!codec->pictureParameterSetNALUnit_.empty()) {
                srs_avc_insert_aud(video_->payload_, aud_inserted);
                video_->payload_->append(&codec->pictureParameterSetNALUnit_[0], (int)codec->pictureParameterSetNALUnit_.size());
            }
            is_sps_pps_appended = true;
        }

        // Insert the NALU to video in annexb.
        srs_avc_insert_aud(video_->payload_, aud_inserted);
        video_->payload_->append(sample->bytes_, sample->size_);
    }

    return err;
}

srs_error_t SrsTsMessageCache::do_cache_hevc(SrsParsedVideoPacket *frame)
{
    srs_error_t err = srs_success;

    // Whether aud inserted.
    bool aud_inserted = false;

    SrsVideoCodecConfig *codec = frame->vcodec();
    srs_assert(codec);

    bool is_sps_pps_appended = false;

    // all sample use cont nalu header, except the sps-pps before IDR frame.
    for (int i = 0; i < frame->nb_samples_; i++) {
        SrsNaluSample *sample = &frame->samples_[i];
        int32_t size = sample->size_;

        if (!sample->bytes_ || size <= 0) {
            return srs_error_new(ERROR_HLS_AVC_SAMPLE_SIZE, "ts: invalid avc sample length=%d", size);
        }

        // Insert aud before NALU for HEVC.
        SrsHevcNaluType nalu_type = (SrsHevcNaluType)SrsHevcNaluTypeParse(sample->bytes_[0]);
        if (SrsIsIRAP(nalu_type) && !frame->has_sps_pps_ && !is_sps_pps_appended) {
            for (size_t i = 0; i < codec->hevc_dec_conf_record_.nalu_vec_.size(); i++) {
                const SrsHevcHvccNalu &nalu = codec->hevc_dec_conf_record_.nalu_vec_[i];
                if (nalu.num_nalus_ <= 0 || nalu.nal_data_vec_.empty())
                    continue;

                srs_avc_insert_aud(video_->payload_, aud_inserted);
                const SrsHevcNalData &data = nalu.nal_data_vec_.at(0);
                video_->payload_->append((char *)&data.nal_unit_data_[0], (int)data.nal_unit_data_.size());
                is_sps_pps_appended = true;
            }
        }

        // Insert the NALU to video in annexb.
        srs_avc_insert_aud(video_->payload_, aud_inserted);
        video_->payload_->append(sample->bytes_, sample->size_);
    }

    return err;
}

ISrsTsTransmuxer::ISrsTsTransmuxer()
{
}

ISrsTsTransmuxer::~ISrsTsTransmuxer()
{
}

SrsTsTransmuxer::SrsTsTransmuxer()
{
    writer_ = NULL;
    format_ = new SrsFormat();
    tsmc_ = new SrsTsMessageCache();
    context_ = new SrsTsContext();
    tscw_ = NULL;
    has_audio_ = has_video_ = true;
    guess_has_av_ = true;
}

SrsTsTransmuxer::~SrsTsTransmuxer()
{
    srs_freep(format_);
    srs_freep(tsmc_);
    srs_freep(tscw_);
    srs_freep(context_);
}

void SrsTsTransmuxer::set_has_audio(bool v)
{
    has_audio_ = v;

    if (tscw_ != NULL && !v) {
        tscw_->set_acodec(SrsAudioCodecIdForbidden);
    }
}

void SrsTsTransmuxer::set_has_video(bool v)
{
    has_video_ = v;

    if (tscw_ != NULL && !v) {
        tscw_->set_vcodec(SrsVideoCodecIdForbidden);
    }
}

void SrsTsTransmuxer::set_guess_has_av(bool v)
{
    guess_has_av_ = v;
    if (tscw_ != NULL && v) {
        tscw_->set_acodec(SrsAudioCodecIdForbidden);
        tscw_->set_vcodec(SrsVideoCodecIdForbidden);
    }
}

srs_error_t SrsTsTransmuxer::initialize(ISrsStreamWriter *fw)
{
    srs_error_t err = srs_success;

    if ((err = format_->initialize()) != srs_success) {
        return srs_error_wrap(err, "ts: init format");
    }

    srs_assert(fw);

    writer_ = fw;

    SrsAudioCodecId acodec = has_audio_ ? SrsAudioCodecIdAAC : SrsAudioCodecIdForbidden;
    SrsVideoCodecId vcodec = has_video_ ? SrsVideoCodecIdAVC : SrsVideoCodecIdForbidden;

    if (guess_has_av_) {
        acodec = SrsAudioCodecIdForbidden;
        vcodec = SrsVideoCodecIdForbidden;
    }

    srs_freep(tscw_);
    tscw_ = new SrsTsContextWriter(fw, context_, acodec, vcodec);

    return err;
}

srs_error_t SrsTsTransmuxer::write_audio(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = format_->on_audio(timestamp, data, size)) != srs_success) {
        return srs_error_wrap(err, "ts: format on audio");
    }

    if (!format_->acodec()) {
        return err;
    }

    // ts support audio codec: aac/mp3
    srs_assert(format_->acodec() && format_->audio());
    if (format_->acodec()->id_ != SrsAudioCodecIdAAC && format_->acodec()->id_ != SrsAudioCodecIdMP3) {
        return err;
    }

    // for aac: ignore sequence header
    if (format_->acodec()->id_ == SrsAudioCodecIdAAC && format_->audio()->aac_packet_type_ == SrsAudioAacFrameTraitSequenceHeader) {
        return err;
    }

    // Switch audio codec if not AAC.
    if (tscw_->acodec() != format_->acodec()->id_) {
        srs_trace("TS: Switch audio codec %d(%s) to %d(%s)", tscw_->acodec(), srs_audio_codec_id2str(tscw_->acodec()).c_str(),
                  format_->acodec()->id_, srs_audio_codec_id2str(format_->acodec()->id_).c_str());
        tscw_->set_acodec(format_->acodec()->id_);
    }

    // the dts calc from rtmp/flv header.
    // @remark for http ts stream, the timestamp is always monotonically increase,
    //      for the packet is filtered by consumer.
    int64_t dts = timestamp * 90;

    // write audio to cache.
    if ((err = tsmc_->cache_audio(format_->audio(), dts)) != srs_success) {
        return srs_error_wrap(err, "ts: cache audio");
    }

    // TODO: FIXME: for pure audio, aggregate some frame to one.

    // always flush audio frame by frame.
    // @see https://github.com/ossrs/srs/issues/512
    return flush_audio();
}

srs_error_t SrsTsTransmuxer::write_video(int64_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = format_->on_video(timestamp, data, size)) != srs_success) {
        return srs_error_wrap(err, "ts: on video");
    }

    if (!format_->vcodec()) {
        return err;
    }

    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    srs_assert(format_->video() && format_->vcodec());
    if (format_->video()->frame_type_ == SrsVideoAvcFrameTypeVideoInfoFrame) {
        return err;
    }

    bool codec_ok = (format_->vcodec()->id_ == SrsVideoCodecIdAVC);
    codec_ok = codec_ok ? true : (format_->vcodec()->id_ == SrsVideoCodecIdHEVC);
    if (!codec_ok) {
        return err;
    }

    // The video codec might change during streaming.
    tscw_->set_vcodec(format_->vcodec()->id_);

    // ignore sequence header
    if (format_->video()->frame_type_ == SrsVideoAvcFrameTypeKeyFrame && format_->video()->avc_packet_type_ == SrsVideoAvcFrameTraitSequenceHeader) {
        return err;
    }

    int64_t dts = timestamp * 90;

    // write video to cache.
    if ((err = tsmc_->cache_video(format_->video(), dts)) != srs_success) {
        return srs_error_wrap(err, "ts: cache video");
    }

    return flush_video();
}

srs_error_t SrsTsTransmuxer::flush_audio()
{
    srs_error_t err = srs_success;

    if ((err = tscw_->write_audio(tsmc_->audio())) != srs_success) {
        return srs_error_wrap(err, "ts: write audio");
    }

    // write success, clear and free the ts message.
    SrsTsMessage *audio_msg = tsmc_->audio();
    srs_freep(audio_msg);
    tsmc_->set_audio(NULL);

    return err;
}

srs_error_t SrsTsTransmuxer::flush_video()
{
    srs_error_t err = srs_success;

    if ((err = tscw_->write_video(tsmc_->video())) != srs_success) {
        return srs_error_wrap(err, "ts: write video");
    }

    // write success, clear and free the ts message.
    SrsTsMessage *video_msg = tsmc_->video();
    srs_freep(video_msg);
    tsmc_->set_video(NULL);

    return err;
}
