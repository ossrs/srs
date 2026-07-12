//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_hikvision.hpp>

#ifdef SRS_HIKVISION

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>

// HCNetSDK PTZ command codes (also defined in HCNetSDK.h).
#ifndef TILT_UP
#define ZOOM_IN 11
#define ZOOM_OUT 12
#define TILT_UP 21
#define TILT_DOWN 22
#define PAN_LEFT 23
#define PAN_RIGHT 24
#define UP_LEFT 25
#define UP_RIGHT 26
#define DOWN_LEFT 27
#define DOWN_RIGHT 28
#define SET_PRESET 8
#define GOTO_PRESET 39
#define TILT_DOWN_ZOOM_IN 58
#define TILT_DOWN_ZOOM_OUT 59
#define PAN_LEFT_ZOOM_IN 60
#define PAN_LEFT_ZOOM_OUT 61
#define PAN_RIGHT_ZOOM_IN 62
#define PAN_RIGHT_ZOOM_OUT 63
#define UP_LEFT_ZOOM_IN 64
#define UP_LEFT_ZOOM_OUT 65
#define UP_RIGHT_ZOOM_IN 66
#define UP_RIGHT_ZOOM_OUT 67
#define DOWN_LEFT_ZOOM_IN 68
#define DOWN_LEFT_ZOOM_OUT 69
#define DOWN_RIGHT_ZOOM_IN 70
#define DOWN_RIGHT_ZOOM_OUT 71
#define TILT_UP_ZOOM_IN 72
#define TILT_UP_ZOOM_OUT 73
#endif

// Hikvision proprietary HCNetSDK header (large).
#include <HCNetSDK.h>

using namespace std;

SrsHikvisionManager *_srs_hikvision = NULL;

// Limit queued PS fragments from SDK callback to avoid OOM.
static const int kHikvisionMaxQueuedPackets = 512;

// Forward decls for helpers defined later in this file.
static bool srs_hikvision_valid_h264_sps(char *frame, int size);
static bool srs_hikvision_valid_h264_pps(char *frame, int size);

bool srs_hikvision_parse_stream(const string &stream, string &serialno, int &channel, int &subchannel)
{
    // Parse from the right: SerialNO_CHANNEL_SUBCHANNEL
    size_t p2 = stream.find_last_of('_');
    if (p2 == string::npos || p2 == 0) {
        return false;
    }
    size_t p1 = stream.find_last_of('_', p2 - 1);
    if (p1 == string::npos || p1 == 0) {
        return false;
    }

    string ch_s = stream.substr(p1 + 1, p2 - p1 - 1);
    string sub_s = stream.substr(p2 + 1);
    if (ch_s.empty() || sub_s.empty()) {
        return false;
    }
    for (size_t i = 0; i < ch_s.size(); i++) {
        if (ch_s[i] < '0' || ch_s[i] > '9') {
            return false;
        }
    }
    for (size_t i = 0; i < sub_s.size(); i++) {
        if (sub_s[i] < '0' || sub_s[i] > '9') {
            return false;
        }
    }

    serialno = stream.substr(0, p1);
    channel = ::atoi(ch_s.c_str());
    subchannel = ::atoi(sub_s.c_str());
    if (serialno.empty() || channel <= 0 || subchannel < 0) {
        return false;
    }
    return true;
}

SrsHikvisionDeviceConfig::SrsHikvisionDeviceConfig()
{
    port_ = 8000;
}

// ---------------------------------------------------------------------------
// SrsHikvisionMuxer
// ---------------------------------------------------------------------------

SrsHikvisionMuxer::SrsHikvisionMuxer()
{
    live_sources_ = _srs_sources;
    req_ = NULL;
    publishing_ = false;
    next_publish_try_ = 0;
    has_base_dts_ = false;
    base_dts_ = 0;
    last_out_dts_ = -1;

    avc_ = new SrsRawH264Stream();
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = false;

    hevc_ = new SrsRawHEVCStream();
    vps_sps_pps_change_ = false;
    vps_sps_pps_sent_ = false;

    aac_ = new SrsRawAacStream();
    pprint_ = SrsPithyPrint::create_caster();
}

SrsHikvisionMuxer::~SrsHikvisionMuxer()
{
    close();
    srs_freep(req_);
    srs_freep(avc_);
    srs_freep(hevc_);
    srs_freep(aac_);
    srs_freep(pprint_);
    live_sources_ = NULL;
}

void SrsHikvisionMuxer::setup(string output, string stream)
{
    output_ = output;
    stream_ = stream;
}

srs_error_t SrsHikvisionMuxer::on_ts_message(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    SrsBuffer avs(msg->payload_->bytes(), msg->payload_->length());
    // Video PES stream_id 0xe0-0xef; audio 0xc0-0xdf.
    if ((msg->sid_ & 0xf0) == 0xe0) {
        if ((err = on_ts_video(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "hik ts video");
        }
    } else if ((msg->sid_ & 0xe0) == 0xc0) {
        if ((err = on_ts_audio(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "hik ts audio");
        }
    }

    return err;
}

srs_error_t SrsHikvisionMuxer::on_es_video(const char *data, int size, uint32_t dts_ms, bool is_key_hint)
{
    srs_error_t err = srs_success;
    if (!data || size <= 0) {
        return err;
    }

    if ((err = ensure_publish()) != srs_success) {
        return srs_error_wrap(err, "ensure publish");
    }

    // Normalize device absolute clock to 0-based monotonic ms (critical for VLC/HTTP-FLV).
    uint32_t dts = correct_timestamp(dts_ms);
    uint32_t pts = dts;

    SrsBuffer avs((char *)data, size);
    // Collect one access unit from this ES packet: SPS/PPS updates + VCL NALUs together.
    // Writing each NALU as a separate FLV tag causes decoder glitches and VLC seeking.
    vector<pair<char *, int> > vcl_nalus;

    while (!avs.empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = demux_h264_nalu(&avs, &frame, &frame_size)) != srs_success) {
            srs_warn("Hikvision ES: demux h264 failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            break;
        }
        if (!frame || frame_size <= 0) {
            break;
        }

        SrsAvcNaluType nt = (SrsAvcNaluType)(frame[0] & 0x1f);
        if (nt == SrsAvcNaluTypeSEI || nt == SrsAvcNaluTypeAccessUnitDelimiter ||
            nt == SrsAvcNaluTypeFilterData || nt == SrsAvcNaluTypeEOSequence || nt == SrsAvcNaluTypeEOStream) {
            continue;
        }

        if (avc_->is_sps(frame, frame_size)) {
            if (!srs_hikvision_valid_h264_sps(frame, frame_size)) {
                srs_info("Hikvision ES: skip invalid SPS size=%d first=%#x", frame_size, (uint8_t)frame[0]);
                continue;
            }
            string sps;
            if ((err = avc_->sps_demux(frame, frame_size, sps)) != srs_success) {
                srs_freep(err);
                continue;
            }
            if (h264_sps_ != sps) {
                h264_sps_changed_ = true;
                h264_sps_ = sps;
            }
            continue;
        }

        if (avc_->is_pps(frame, frame_size)) {
            if (!srs_hikvision_valid_h264_pps(frame, frame_size)) {
                srs_info("Hikvision ES: skip invalid PPS size=%d first=%#x", frame_size, (uint8_t)frame[0]);
                continue;
            }
            string pps;
            if ((err = avc_->pps_demux(frame, frame_size, pps)) != srs_success) {
                srs_freep(err);
                continue;
            }
            if (h264_pps_ != pps) {
                h264_pps_changed_ = true;
                h264_pps_ = pps;
            }
            continue;
        }

        // Only IDR / non-IDR VCL slices as picture data.
        if (nt != SrsAvcNaluTypeIDR && nt != SrsAvcNaluTypeNonIDR) {
            continue;
        }
        // Guard against garbage demux (tiny or absurdly large "slices").
        if (frame_size < 4 || frame_size > 4 * 1024 * 1024) {
            continue;
        }
        vcl_nalus.push_back(make_pair(frame, frame_size));
    }

    // Always emit SH before any VCL of this packet when SPS/PPS just became ready/changed.
    if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
        srs_warn("Hikvision ES: write sps/pps failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    if (vcl_nalus.empty()) {
        return srs_success;
    }

    if ((err = write_h264_ipb_frames(vcl_nalus, dts, pts)) != srs_success) {
        if (srs_error_code(err) == ERROR_H264_DROP_BEFORE_SPS_PPS) {
            srs_info("Hikvision ES: drop AU before sps/pps key_hint=%d nalus=%d", is_key_hint ? 1 : 0, (int)vcl_nalus.size());
            srs_freep(err);
            return srs_success;
        }
        srs_warn("Hikvision ES: write AU failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return srs_success;
}

// Find first Annex-B start code offset in buffer; -1 if none.
static int srs_hikvision_find_annexb(const char *data, int size)
{
    for (int i = 0; i + 3 < size; i++) {
        if (data[i] == 0 && data[i + 1] == 0) {
            if (data[i + 2] == 1) {
                return i;
            }
            if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
                return i;
            }
        }
    }
    return -1;
}

static bool srs_hikvision_is_h264_nalu_type(uint8_t b0)
{
    uint8_t nt = b0 & 0x1f;
    // Forbidden bit should be 0 for valid H.264 NALU.
    if ((b0 & 0x80) != 0) {
        return false;
    }
    return nt >= 1 && nt <= 23;
}

// H.264 SPS must have nal_ref_idc != 0 and reasonable size (not a whole frame mis-detected).
static bool srs_hikvision_valid_h264_sps(char *frame, int size)
{
    if (!frame || size < 4 || size > 128) {
        return false;
    }
    uint8_t b0 = (uint8_t)frame[0];
    if ((b0 & 0x1f) != 7) {
        return false;
    }
    // nal_ref_idc (bits 5-6) must be non-zero for SPS.
    if (((b0 >> 5) & 0x03) == 0) {
        return false;
    }
    // Common H.264 profile_idc values.
    uint8_t profile = (uint8_t)frame[1];
    if (profile != 66 && profile != 77 && profile != 88 && profile != 100 && profile != 110 &&
        profile != 122 && profile != 144 && profile != 244 && profile != 44) {
        return false;
    }
    return true;
}

static bool srs_hikvision_valid_h264_pps(char *frame, int size)
{
    if (!frame || size < 2 || size > 256) {
        return false;
    }
    uint8_t b0 = (uint8_t)frame[0];
    if ((b0 & 0x1f) != 8) {
        return false;
    }
    if (((b0 >> 5) & 0x03) == 0) {
        return false;
    }
    return true;
}

static bool srs_hikvision_is_h265_nalu_type(uint8_t b0)
{
    SrsHevcNaluType nt = SrsHevcNaluTypeParse(b0);
    return nt <= SrsHevcNaluType_SEI_SUFFIX;
}

// Demux one H.264 NALU. Handles Annex-B, AVCC, or raw ES where the first NALU has no start code
// but later NALUs may still be Annex-B separated (common in Hikvision PES).
srs_error_t SrsHikvisionMuxer::demux_h264_nalu(SrsBuffer *avs, char **pframe, int *pnb_frame)
{
    srs_error_t err = srs_success;
    *pframe = NULL;
    *pnb_frame = 0;

    if (avs->empty()) {
        return err;
    }

    char *data = avs->data() + avs->pos();
    int left = avs->left();

    // 1) Annex-B at current position.
    if (srs_avc_startswith_annexb(avs, NULL)) {
        return avc_->annexb_demux(avs, pframe, pnb_frame);
    }

    // 2) Small proprietary prefix then Annex-B (only short headers; never split mid-VCL).
    // NOTE: Entropy-coded slice bytes often contain 00 00 01 by chance. Treating those as
    // start codes truncates the frame (~5KB ghosts) and doubles the frame rate for VLC.
    int annexb_off = srs_hikvision_find_annexb(data, left);
    if (annexb_off > 0 && annexb_off < 64) {
        avs->skip(annexb_off);
        return avc_->annexb_demux(avs, pframe, pnb_frame);
    }

    // 3) AVCC: 4-byte big-endian NALU length + payload (length must be sane).
    if (left >= 5) {
        uint32_t nalu_len = ((uint32_t)(uint8_t)data[0] << 24) | ((uint32_t)(uint8_t)data[1] << 16) |
                            ((uint32_t)(uint8_t)data[2] << 8) | (uint32_t)(uint8_t)data[3];
        // Cap length to avoid treating random bytes as multi-KB "SPS".
        if (nalu_len >= 1 && nalu_len <= 2 * 1024 * 1024 && nalu_len <= (uint32_t)(left - 4) &&
            srs_hikvision_is_h264_nalu_type((uint8_t)data[4])) {
            uint8_t nt = (uint8_t)data[4] & 0x1f;
            // SPS/PPS AVCC units must be small.
            if ((nt == 7 || nt == 8) && nalu_len > 256) {
                // fall through
            } else if (nalu_len == (uint32_t)(left - 4) || nt == 7 || nt == 8) {
                // Full-buffer AVCC unit, or small param set — safe to take length prefix.
                avs->skip(4);
                *pframe = avs->data() + avs->pos();
                *pnb_frame = (int)nalu_len;
                avs->skip((int)nalu_len);
                return err;
            }
            // Otherwise: length prefix may be false positive inside raw ES; fall through.
        }
    }

    // 4) Raw NALU without start code (typical Hikvision ES: one complete frame per packet).
    // Hikvision often concatenates SPS+PPS+slice without start codes: 0x67.. 0x68.. 0x65..
    if (left >= 1 && srs_hikvision_is_h264_nalu_type((uint8_t)data[0])) {
        uint8_t nt = (uint8_t)data[0] & 0x1f;

        // Heuristic: split only param sets that are clearly concatenated (small window).
        if ((nt == 7 || nt == 8) && left > 256) {
            int split = -1;
            int search_end = (left < 512) ? left : 512;
            for (int i = 4; i < search_end; i++) {
                uint8_t b = (uint8_t)data[i];
                if ((b & 0x80) != 0) {
                    continue;
                }
                uint8_t n2 = b & 0x1f;
                uint8_t ref = (b >> 5) & 0x03;
                // Next param set or VCL NAL with valid ref for param sets.
                if (nt == 7 && n2 == 8 && ref != 0) {
                    split = i; // SPS -> PPS
                    break;
                }
                if ((n2 == 1 || n2 == 5) && i > 8 && i < 128) {
                    split = i; // param set -> slice (param sets are small)
                    break;
                }
                if (nt == 8 && (n2 == 1 || n2 == 5) && i > 2 && i < 128) {
                    split = i; // PPS -> slice
                    break;
                }
            }
            if (split > 0 && split < left) {
                *pframe = data;
                *pnb_frame = split;
                avs->skip(split);
                return err;
            }
            srs_info("Hikvision: drop oversized raw %s size=%d first=%#x (no split)",
                     nt == 7 ? "SPS" : "PPS", left, (uint8_t)data[0]);
            avs->skip(left);
            return err;
        }

        // VCL / other: one NALU = rest of buffer (do not search mid-stream start codes).
        *pframe = data;
        *pnb_frame = left;
        avs->skip(left);
        return err;
    }

    srs_info("Hikvision: drop %dB non-H264 payload, first=%#x", left, left > 0 ? (uint8_t)data[0] : 0);
    avs->skip(left);
    return err;
}

srs_error_t SrsHikvisionMuxer::demux_h265_nalu(SrsBuffer *avs, char **pframe, int *pnb_frame)
{
    srs_error_t err = srs_success;
    *pframe = NULL;
    *pnb_frame = 0;

    if (avs->empty()) {
        return err;
    }

    char *data = avs->data() + avs->pos();
    int left = avs->left();

    if (srs_avc_startswith_annexb(avs, NULL)) {
        return hevc_->annexb_demux(avs, pframe, pnb_frame);
    }

    int annexb_off = srs_hikvision_find_annexb(data, left);
    if (annexb_off > 0) {
        if (srs_hikvision_is_h265_nalu_type((uint8_t)data[0]) && annexb_off >= 2) {
            *pframe = data;
            *pnb_frame = annexb_off;
            avs->skip(annexb_off);
            return err;
        }
        avs->skip(annexb_off);
        return hevc_->annexb_demux(avs, pframe, pnb_frame);
    }

    if (left >= 5) {
        uint32_t nalu_len = ((uint32_t)(uint8_t)data[0] << 24) | ((uint32_t)(uint8_t)data[1] << 16) |
                            ((uint32_t)(uint8_t)data[2] << 8) | (uint32_t)(uint8_t)data[3];
        if (nalu_len >= 1 && nalu_len <= 2 * 1024 * 1024 && nalu_len <= (uint32_t)(left - 4) &&
            srs_hikvision_is_h265_nalu_type((uint8_t)data[4])) {
            SrsHevcNaluType nt = SrsHevcNaluTypeParse((uint8_t)data[4]);
            if ((nt == SrsHevcNaluType_VPS || nt == SrsHevcNaluType_SPS || nt == SrsHevcNaluType_PPS) && nalu_len > 512) {
                // fall through
            } else {
                avs->skip(4);
                *pframe = avs->data() + avs->pos();
                *pnb_frame = (int)nalu_len;
                avs->skip((int)nalu_len);
                return err;
            }
        }
    }

    if (left >= 2 && srs_hikvision_is_h265_nalu_type((uint8_t)data[0])) {
        SrsHevcNaluType nt = SrsHevcNaluTypeParse((uint8_t)data[0]);
        if ((nt == SrsHevcNaluType_VPS || nt == SrsHevcNaluType_SPS || nt == SrsHevcNaluType_PPS) && left > 512) {
            srs_warn("Hikvision: reject oversized raw HEVC param set size=%d", left);
            avs->skip(left);
            return err;
        }
        *pframe = data;
        *pnb_frame = left;
        avs->skip(left);
        return err;
    }

    srs_info("Hikvision: drop %dB non-H265 payload, first=%#x", left, left > 0 ? (uint8_t)data[0] : 0);
    avs->skip(left);
    return err;
}

srs_error_t SrsHikvisionMuxer::on_ts_video(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    if ((err = ensure_publish()) != srs_success) {
        return srs_error_wrap(err, "ensure publish");
    }

    // Prefer stream_type from PS PSM when available.
    SrsPsDecodeHelper *h = (SrsPsDecodeHelper *)msg->ps_helper_;
    if (h && h->ctx_) {
        if (h->ctx_->video_stream_type_ == SrsTsStreamVideoHEVC) {
            return mux_h265(msg, avs);
        }
        if (h->ctx_->video_stream_type_ == SrsTsStreamVideoH264) {
            return mux_h264(msg, avs);
        }
    }

    // Peek codec from first NALU header byte (after optional start code / length).
    if (!avs->empty()) {
        char *p = avs->data() + avs->pos();
        int left = avs->left();
        int off = 0;
        if (left >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
            off = 4;
        } else if (left >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) {
            off = 3;
        } else if (left >= 5) {
            // Possible AVCC length prefix.
            uint32_t nalu_len = ((uint32_t)(uint8_t)p[0] << 24) | ((uint32_t)(uint8_t)p[1] << 16) |
                                ((uint32_t)(uint8_t)p[2] << 8) | (uint32_t)(uint8_t)p[3];
            if (nalu_len > 0 && nalu_len <= (uint32_t)(left - 4)) {
                off = 4;
            }
        }
        if (left > off) {
            uint8_t b0 = (uint8_t)p[off];
            SrsHevcNaluType h265_nt = SrsHevcNaluTypeParse(b0);
            if (h265_nt == SrsHevcNaluType_VPS || h265_nt == SrsHevcNaluType_SPS || h265_nt == SrsHevcNaluType_PPS ||
                h265_nt == SrsHevcNaluType_CODED_SLICE_TRAIL_R || h265_nt == SrsHevcNaluType_CODED_SLICE_IDR ||
                h265_nt == SrsHevcNaluType_CODED_SLICE_CRA) {
                return mux_h265(msg, avs);
            }
        }
    }

    return mux_h264(msg, avs);
}

srs_error_t SrsHikvisionMuxer::mux_h264(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;
    uint32_t raw_dts = (uint32_t)(msg->dts_ / 90);
    uint32_t raw_pts = (uint32_t)(msg->pts_ / 90);
    uint32_t dts = correct_timestamp(raw_dts);
    uint32_t pts = dts;
    if (raw_pts > raw_dts) {
        pts = dts + (raw_pts - raw_dts);
    }

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = demux_h264_nalu(avs, &frame, &frame_size)) != srs_success) {
            // Soft-fail: skip rest of this PES instead of killing the stream.
            srs_warn("Hikvision: demux h264 failed, drop rest, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            break;
        }
        if (!frame || frame_size <= 0) {
            break;
        }

        SrsAvcNaluType nt = (SrsAvcNaluType)(frame[0] & 0x1f);
        if (nt != SrsAvcNaluTypeSPS && nt != SrsAvcNaluTypePPS && nt != SrsAvcNaluTypeIDR &&
            nt != SrsAvcNaluTypeNonIDR && nt != SrsAvcNaluTypeSEI && nt != SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }
        if (nt == SrsAvcNaluTypeSEI || nt == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }

        if (avc_->is_sps(frame, frame_size)) {
            if (!srs_hikvision_valid_h264_sps(frame, frame_size)) {
                srs_warn("Hikvision: skip invalid SPS size=%d first=%#x", frame_size, (uint8_t)frame[0]);
                continue;
            }
            string sps;
            if ((err = avc_->sps_demux(frame, frame_size, sps)) != srs_success) {
                srs_freep(err);
                continue;
            }
            if (h264_sps_ == sps) {
                continue;
            }
            h264_sps_changed_ = true;
            h264_sps_ = sps;
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                // Soft reconnect on publish errors.
                srs_warn("Hikvision: write sps/pps failed, reconnect, err=%s", srs_error_desc(err).c_str());
                srs_freep(err);
                close();
                break;
            }
            continue;
        }

        if (avc_->is_pps(frame, frame_size)) {
            if (!srs_hikvision_valid_h264_pps(frame, frame_size)) {
                srs_warn("Hikvision: skip invalid PPS size=%d first=%#x", frame_size, (uint8_t)frame[0]);
                continue;
            }
            string pps;
            if ((err = avc_->pps_demux(frame, frame_size, pps)) != srs_success) {
                srs_freep(err);
                continue;
            }
            if (h264_pps_ == pps) {
                continue;
            }
            h264_pps_changed_ = true;
            h264_pps_ = pps;
            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                srs_warn("Hikvision: write sps/pps failed, reconnect, err=%s", srs_error_desc(err).c_str());
                srs_freep(err);
                close();
                break;
            }
            continue;
        }

        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            // Drop frames before SPS/PPS rather than fail the stream.
            if (srs_error_code(err) == ERROR_H264_DROP_BEFORE_SPS_PPS) {
                srs_info("Hikvision: drop frame before sps/pps");
                srs_freep(err);
                continue;
            }
            // Socket write / publish failure: soft reconnect, keep PS state.
            srs_warn("Hikvision: write frame failed, reconnect, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            close();
            break;
        }
    }

    return err;
}

srs_error_t SrsHikvisionMuxer::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    if (!h264_sps_changed_ || !h264_pps_changed_) {
        return err;
    }

    string sh;
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

    if ((err = rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }

    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = true;
    return err;
}

srs_error_t SrsHikvisionMuxer::write_h264_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts)
{
    vector<pair<char *, int> > nalus;
    nalus.push_back(make_pair(frame, frame_size));
    return write_h264_ipb_frames(nalus, dts, pts);
}

srs_error_t SrsHikvisionMuxer::write_h264_ipb_frames(const vector<pair<char *, int> > &nalus, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    if (!h264_sps_pps_sent_) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop for no sps/pps");
    }
    if (nalus.empty()) {
        return err;
    }

    bool is_key = false;
    int nb_payload = 0;
    for (size_t i = 0; i < nalus.size(); i++) {
        char *nal = nalus[i].first;
        int nal_size = nalus[i].second;
        if (!nal || nal_size <= 0) {
            continue;
        }
        if (((SrsAvcNaluType)(nal[0] & 0x1f)) == SrsAvcNaluTypeIDR) {
            is_key = true;
        }
        nb_payload += 4 + nal_size;
    }
    if (nb_payload <= 0) {
        return err;
    }

    // Build length-prefixed AVCC NALU blob for one access unit.
    string ibp;
    ibp.reserve(nb_payload);
    for (size_t i = 0; i < nalus.size(); i++) {
        char *nal = nalus[i].first;
        int nal_size = nalus[i].second;
        if (!nal || nal_size <= 0) {
            continue;
        }
        char len[4];
        len[0] = (char)((nal_size >> 24) & 0xff);
        len[1] = (char)((nal_size >> 16) & 0xff);
        len[2] = (char)((nal_size >> 8) & 0xff);
        len[3] = (char)(nal_size & 0xff);
        ibp.append(len, 4);
        ibp.append(nal, nal_size);
    }

    SrsVideoAvcFrameType frame_type = is_key ? SrsVideoAvcFrameTypeKeyFrame : SrsVideoAvcFrameTypeInterFrame;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc_->mux_avc2flv(ibp, frame_type, SrsVideoAvcFrameTraitNALU, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }

    return rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv);
}

srs_error_t SrsHikvisionMuxer::mux_h265(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;
    uint32_t raw_dts = (uint32_t)(msg->dts_ / 90);
    uint32_t raw_pts = (uint32_t)(msg->pts_ / 90);
    uint32_t dts = correct_timestamp(raw_dts);
    uint32_t pts = dts;
    if (raw_pts > raw_dts) {
        pts = dts + (raw_pts - raw_dts);
    }

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = demux_h265_nalu(avs, &frame, &frame_size)) != srs_success) {
            srs_warn("Hikvision: demux h265 failed, drop rest, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            break;
        }
        if (!frame || frame_size <= 0) {
            break;
        }

        SrsHevcNaluType nt = SrsHevcNaluTypeParse(frame[0]);
        if (nt == SrsHevcNaluType_SEI || nt == SrsHevcNaluType_SEI_SUFFIX || nt == SrsHevcNaluType_ACCESS_UNIT_DELIMITER) {
            continue;
        }

        if (hevc_->is_vps(frame, frame_size)) {
            string vps;
            if ((err = hevc_->vps_demux(frame, frame_size, vps)) != srs_success) {
                return srs_error_wrap(err, "demux vps");
            }
            if (h265_vps_ == vps) {
                continue;
            }
            vps_sps_pps_change_ = true;
            h265_vps_ = vps;
            if ((err = write_h265_vps_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write vps");
            }
            continue;
        }

        if (hevc_->is_sps(frame, frame_size)) {
            string sps;
            if ((err = hevc_->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }
            if (h265_sps_ == sps) {
                continue;
            }
            vps_sps_pps_change_ = true;
            h265_sps_ = sps;
            if ((err = write_h265_vps_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps");
            }
            continue;
        }

        if (hevc_->is_pps(frame, frame_size)) {
            string pps;
            if ((err = hevc_->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }
            if (h265_pps_ == pps) {
                continue;
            }
            vps_sps_pps_change_ = true;
            h265_pps_ = pps;
            if ((err = write_h265_vps_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write pps");
            }
            continue;
        }

        if ((err = write_h265_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            if (srs_error_code(err) == ERROR_H264_DROP_BEFORE_SPS_PPS) {
                srs_info("Hikvision: drop hevc frame before vps/sps/pps");
                srs_freep(err);
                continue;
            }
            return srs_error_wrap(err, "write frame");
        }
    }

    return err;
}

srs_error_t SrsHikvisionMuxer::write_h265_vps_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    if (!vps_sps_pps_change_) {
        return err;
    }
    if (h265_vps_.empty() || h265_sps_.empty() || h265_pps_.empty()) {
        return err;
    }

    vector<string> h265_pps;
    h265_pps.push_back(h265_pps_);
    string sh;
    if ((err = hevc_->mux_sequence_header(h265_vps_, h265_sps_, h265_pps, sh)) != srs_success) {
        return srs_error_wrap(err, "hevc mux sequence header");
    }

    char *flv = NULL;
    int nb_flv = 0;
    if ((err = hevc_->mux_hevc2flv(sh, SrsVideoAvcFrameTypeKeyFrame, SrsVideoAvcFrameTraitSequenceHeader, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc to flv");
    }
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc write packet");
    }

    vps_sps_pps_change_ = false;
    vps_sps_pps_sent_ = true;
    return err;
}

srs_error_t SrsHikvisionMuxer::write_h265_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;
    if (!vps_sps_pps_sent_) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop for no vps/sps/pps");
    }

    SrsHevcNaluType nt = SrsHevcNaluTypeParse(frame[0]);
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (SrsIsIRAP(nt)) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    string ipb;
    if ((err = hevc_->mux_ipb_frame(frame, frame_size, ipb)) != srs_success) {
        return srs_error_wrap(err, "hevc mux ipb frame");
    }

    char *flv = NULL;
    int nb_flv = 0;
    if ((err = hevc_->mux_hevc2flv(ipb, frame_type, SrsVideoAvcFrameTraitNALU, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc to flv");
    }
    return rtmp_write_packet(SrsFrameTypeVideo, dts, flv, nb_flv);
}

srs_error_t SrsHikvisionMuxer::on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    if ((err = ensure_publish()) != srs_success) {
        return srs_error_wrap(err, "ensure publish");
    }

    uint32_t dts = correct_timestamp((uint32_t)(msg->dts_ / 90));
    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac_->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            // Non-AAC (e.g. G.711) is common on Hikvision; ignore quietly.
            return srs_success;
        }
        if (frame_size <= 0) {
            continue;
        }

        if (aac_specific_config_.empty()) {
            string sh;
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

srs_error_t SrsHikvisionMuxer::write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts)
{
    srs_error_t err = srs_success;
    char *data = NULL;
    int size = 0;
    if ((err = aac_->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }
    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

uint32_t SrsHikvisionMuxer::correct_timestamp(uint32_t dts_ms)
{
    // dts_ms==0 is common for SDK file-head packets; do not re-base on it.
    if (dts_ms == 0) {
        if (!has_base_dts_) {
            return 0;
        }
        // Step ~1 frame at 25fps to keep monotonic when device stamp is missing.
        if (last_out_dts_ < 0) {
            last_out_dts_ = 0;
            return 0;
        }
        last_out_dts_ += 40;
        return (uint32_t)last_out_dts_;
    }

    if (!has_base_dts_) {
        has_base_dts_ = true;
        base_dts_ = (int64_t)dts_ms;
        last_out_dts_ = 0;
        return 0;
    }

    int64_t rel = (int64_t)dts_ms - base_dts_;

    // Backward jump / wrap: keep monotonic with +1ms.
    if (rel <= last_out_dts_) {
        last_out_dts_ += 1;
        return (uint32_t)last_out_dts_;
    }

    // Cap large forward jumps (device glitch) so VLC does not seek.
    if (last_out_dts_ >= 0 && rel > last_out_dts_ + 1000) {
        rel = last_out_dts_ + 40;
    }

    last_out_dts_ = rel;
    return (uint32_t)rel;
}

srs_error_t SrsHikvisionMuxer::rtmp_write_packet(char type, uint32_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = ensure_publish()) != srs_success) {
        srs_freepa(data);
        return srs_error_wrap(err, "ensure publish");
    }

    // PS path still feeds absolute / 90k-derived stamps; normalize here once.
    // ES path already calls correct_timestamp(); re-applying is safe (already 0-based).
    // Detect "already corrected" by has_base and stamp near last_out: always re-correct
    // only when stamp looks like absolute device time (very large vs last_out).
    // Simpler: always pass through correct_timestamp for raw device/ps stamps at call sites;
    // here only enforce monotonic as last line of defense.
    if (last_out_dts_ >= 0 && (int64_t)timestamp < last_out_dts_) {
        timestamp = (uint32_t)(last_out_dts_ + 1);
        last_out_dts_ = timestamp;
    } else if ((int64_t)timestamp > last_out_dts_) {
        last_out_dts_ = timestamp;
        if (!has_base_dts_) {
            has_base_dts_ = true;
            base_dts_ = 0;
        }
    }

    // Inject into LiveSource directly in arrival order (no A/V queue).
    // SrsMpegtsQueue requires both audio+video and buffers 100 video frames when
    // audio is absent, which reorders SH after VCL and makes VLC jump.
    SrsRtmpCommonMessage *cmsg = NULL;
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, 1, &cmsg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }

    SrsMediaPacket *msg = new SrsMediaPacket();
    cmsg->to_msg(msg);
    srs_freep(cmsg);

    if (pprint_->can_print()) {
        srs_trace("Hikvision: inject msg %s age=%d, dts=%" PRId64 ", size=%d, stream=%s",
                  msg->is_audio() ? "A" : msg->is_video() ? "V"
                                                          : "N",
                  pprint_->age(), msg->timestamp_, msg->size(), stream_.c_str());
    }

    if ((err = source_->on_frame(msg)) != srs_success) {
        srs_freep(msg);
        return srs_error_wrap(err, "on_frame");
    }
    srs_freep(msg);

    return err;
}

srs_error_t SrsHikvisionMuxer::ensure_publish()
{
    srs_error_t err = srs_success;

    if (publishing_ && source_.get()) {
        return err;
    }

    // Backoff when stream is busy (e.g. external publisher still holding source).
    if (next_publish_try_ != 0 && srs_time_now_cached() < next_publish_try_) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "hikvision publish backoff stream=%s", stream_.c_str());
    }

    if (!req_) {
        // output template e.g. rtmp://127.0.0.1/live/[stream]
        string url = srs_strings_replace(output_, "[stream]", stream_);
        string tcUrl, stream_name;
        srs_net_url_parse_rtmp_url(url, tcUrl, stream_name);
        if (stream_name.empty()) {
            stream_name = stream_;
        }

        SrsRequest *req = new SrsRequest();
        req->ip_ = "127.0.0.1";
        req->protocol_ = "rtmp";
        // Parse tcUrl + stream together so app/stream split is correct
        // (passing empty stream appends a trailing slash and corrupts app).
        srs_net_url_parse_tcurl(tcUrl, req->schema_, req->host_, req->vhost_, req->app_,
                                stream_name, req->port_, req->param_);
        req->stream_ = stream_name;
        req->tcUrl_ = tcUrl;
        if (req->app_.empty()) {
            req->app_ = "live";
        }
        // Match HTTP-FLV/HLS players: without explicit vhost they use __defaultVhost__,
        // so sid becomes /live/<stream>. Using host IP as vhost would create a different source.
        if (req->vhost_.empty() || req->vhost_ == req->host_) {
            req->vhost_ = SRS_CONSTS_RTMP_DEFAULT_VHOST;
        }
        req->strip();
        req_ = req;
        srs_trace("Hikvision: prepare live source url=%s", req_->get_stream_url().c_str());
    }

    if ((err = live_sources_->fetch_or_create(req_, source_)) != srs_success) {
        return srs_error_wrap(err, "fetch_or_create source");
    }
    srs_assert(source_.get() != NULL);

    if (!source_->can_publish(false)) {
        next_publish_try_ = srs_time_now_cached() + 1 * SRS_UTIME_SECONDS;
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "stream %s busy", req_->get_stream_url().c_str());
    }

    bool enabled_cache = _srs_config->get_gop_cache(req_->vhost_);
    int gcmf = _srs_config->get_gop_cache_max_frames(req_->vhost_);
    source_->set_cache(enabled_cache);
    source_->set_gop_cache_max_frames(gcmf);

    // Bridge LiveSource frames to WebRTC (same path as RTMP publish + rtmp_to_rtc).
    // Without this, HTTP-FLV works but WebRTC play has no RTP (only STUN).
#if defined(SRS_FFMPEG_FIT)
    bool rtc_server_enabled = _srs_config->get_rtc_server_enabled();
    bool rtc_enabled = _srs_config->get_rtc_enabled(req_->vhost_);
    bool edge = _srs_config->get_vhost_is_edge(req_->vhost_);
    bool rtmp_to_rtc = _srs_config->get_rtc_from_rtmp(req_->vhost_);
    if (rtmp_to_rtc && edge) {
        rtmp_to_rtc = false;
        srs_warn("Hikvision: disable rtmp_to_rtc for edge vhost=%s", req_->vhost_.c_str());
    }

    if (rtc_server_enabled && rtc_enabled && rtmp_to_rtc) {
        SrsSharedPtr<SrsRtcSource> rtc;
        if ((err = _srs_rtc_sources->fetch_or_create(req_, rtc)) != srs_success) {
            return srs_error_wrap(err, "create rtc source");
        }
        // Attach bridge only when RTC source is free to "publish" into.
        if (rtc.get() && rtc->can_publish()) {
            SrsRtmpBridge *bridge = new SrsRtmpBridge(_srs_app_factory);
            bridge->enable_rtmp2rtc(rtc);
            if ((err = bridge->initialize(req_)) != srs_success) {
                srs_freep(bridge);
                return srs_error_wrap(err, "rtmp2rtc bridge init");
            }
            source_->set_bridge(bridge);
            srs_trace("Hikvision: rtmp_to_rtc bridge enabled for %s", req_->get_stream_url().c_str());
        } else {
            srs_warn("Hikvision: rtc source busy, skip rtmp_to_rtc bridge for %s",
                     req_->get_stream_url().c_str());
        }
    }
#endif

    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "on_publish %s", req_->get_stream_url().c_str());
    }

    publishing_ = true;
    next_publish_try_ = 0;
    srs_trace("Hikvision: live source published %s", req_->get_stream_url().c_str());
    return err;
}

void SrsHikvisionMuxer::close()
{
    if (publishing_ && source_.get()) {
        source_->on_unpublish();
        srs_trace("Hikvision: live source unpublished %s",
                  req_ ? req_->get_stream_url().c_str() : stream_.c_str());
    }
    publishing_ = false;
    next_publish_try_ = 0;
    // Keep source_ shared ptr (manager may still have consumers); drop ownership of publish only.
    source_ = SrsSharedPtr<SrsLiveSource>(NULL);

    has_base_dts_ = false;
    base_dts_ = 0;
    last_out_dts_ = -1;

    aac_specific_config_ = "";
    h264_sps_pps_sent_ = false;
    h264_sps_ = "";
    h264_pps_ = "";
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    vps_sps_pps_sent_ = false;
    vps_sps_pps_change_ = false;
    h265_vps_ = "";
    h265_sps_ = "";
    h265_pps_ = "";
}

// ---------------------------------------------------------------------------
// SrsHikvisionDevice
// ---------------------------------------------------------------------------

SrsHikvisionDevice::SrsHikvisionDevice(const SrsHikvisionDeviceConfig &conf)
{
    conf_ = conf;
    user_id_ = -1;
    logged_in_ = false;
}

SrsHikvisionDevice::~SrsHikvisionDevice()
{
    logout();
}

const SrsHikvisionDeviceConfig &SrsHikvisionDevice::conf() const
{
    return conf_;
}

long SrsHikvisionDevice::user_id() const
{
    return user_id_;
}

srs_error_t SrsHikvisionDevice::ensure_login()
{
    srs_error_t err = srs_success;
    if (logged_in_ && user_id_ >= 0) {
        return err;
    }

    NET_DVR_USER_LOGIN_INFO login_info;
    NET_DVR_DEVICEINFO_V40 device_info;
    memset(&login_info, 0, sizeof(login_info));
    memset(&device_info, 0, sizeof(device_info));

    strncpy(login_info.sDeviceAddress, conf_.host_.c_str(), sizeof(login_info.sDeviceAddress) - 1);
    login_info.wPort = (WORD)conf_.port_;
    strncpy(login_info.sUserName, conf_.user_.c_str(), sizeof(login_info.sUserName) - 1);
    strncpy(login_info.sPassword, conf_.password_.c_str(), sizeof(login_info.sPassword) - 1);
    login_info.bUseAsynLogin = FALSE;

    user_id_ = NET_DVR_Login_V40(&login_info, &device_info);
    if (user_id_ < 0) {
        DWORD e = NET_DVR_GetLastError();
        return srs_error_new(ERROR_HIKVISION_SDK, "login %s:%d serial=%s failed, err=%u",
                             conf_.host_.c_str(), conf_.port_, conf_.serialno_.c_str(), (unsigned)e);
    }

    logged_in_ = true;
    srs_trace("Hikvision: login ok serial=%s host=%s:%d user_id=%ld",
              conf_.serialno_.c_str(), conf_.host_.c_str(), conf_.port_, (long)user_id_);
    return err;
}

void SrsHikvisionDevice::logout()
{
    if (logged_in_ && user_id_ >= 0) {
        NET_DVR_Logout(user_id_);
        srs_trace("Hikvision: logout serial=%s user_id=%ld", conf_.serialno_.c_str(), (long)user_id_);
    }
    user_id_ = -1;
    logged_in_ = false;
}

srs_error_t SrsHikvisionDevice::ptz_control(int channel, int command, bool stop, int speed)
{
    srs_error_t err = srs_success;
    if ((err = ensure_login()) != srs_success) {
        return srs_error_wrap(err, "login for ptz");
    }

    // dwStop: 0=start, 1=stop
    DWORD dw_stop = stop ? 1 : 0;
    DWORD dw_speed = (DWORD)srs_max(1, srs_min(7, speed));
    if (!NET_DVR_PTZControlWithSpeed_Other(user_id_, channel, (DWORD)command, dw_stop, dw_speed)) {
        return srs_error_new(ERROR_HIKVISION_SDK, "PTZControl serial=%s ch=%d cmd=%d stop=%d speed=%d err=%u",
                             conf_.serialno_.c_str(), channel, command, stop ? 1 : 0, (int)dw_speed,
                             (unsigned)NET_DVR_GetLastError());
    }
    srs_trace("Hikvision: PTZ serial=%s ch=%d cmd=%d stop=%d speed=%d",
              conf_.serialno_.c_str(), channel, command, stop ? 1 : 0, (int)dw_speed);
    return err;
}

srs_error_t SrsHikvisionDevice::ptz_preset(int channel, int preset_cmd, int preset_index)
{
    srs_error_t err = srs_success;
    if ((err = ensure_login()) != srs_success) {
        return srs_error_wrap(err, "login for preset");
    }
    if (!NET_DVR_PTZPreset_Other(user_id_, channel, (DWORD)preset_cmd, (DWORD)preset_index)) {
        return srs_error_new(ERROR_HIKVISION_SDK, "PTZPreset serial=%s ch=%d cmd=%d preset=%d err=%u",
                             conf_.serialno_.c_str(), channel, preset_cmd, preset_index,
                             (unsigned)NET_DVR_GetLastError());
    }
    srs_trace("Hikvision: Preset serial=%s ch=%d cmd=%d preset=%d",
              conf_.serialno_.c_str(), channel, preset_cmd, preset_index);
    return err;
}

SrsHikvisionPtzSession::SrsHikvisionPtzSession()
{
    for (int i = 0; i < 6; i++) {
        move_dir_flags_[i] = false;
    }
    last_ptz_command_ = -1;
    last_speed_ = 4;
    ptz_speed_ = 4;
    last_active_ = srs_time_now_cached();
}

// ---------------------------------------------------------------------------
// SrsHikvisionStream
// ---------------------------------------------------------------------------

static void CALLBACK srs_hikvision_realdata_cb(LONG lRealHandle, DWORD dwDataType, BYTE *pBuffer, DWORD dwBufSize, void *pUser)
{
    SrsHikvisionStream *stream = (SrsHikvisionStream *)pUser;
    if (!stream || !pBuffer || dwBufSize == 0) {
        return;
    }

    // SYSHEAD is often a proprietary PlayM4 header, not MPEG-PS. Only feed it when it
    // looks like a PS/PES start code (00 00 01 xx); otherwise STREAMDATA alone is enough.
    if (dwDataType == NET_DVR_SYSHEAD) {
        if (dwBufSize >= 4 && pBuffer[0] == 0x00 && pBuffer[1] == 0x00 && pBuffer[2] == 0x01) {
            stream->on_sdk_data((int)dwDataType, (const char *)pBuffer, (int)dwBufSize);
        }
        return;
    }

    if (dwDataType == NET_DVR_STREAMDATA || dwDataType == NET_DVR_AUDIOSTREAMDATA) {
        stream->on_sdk_data((int)dwDataType, (const char *)pBuffer, (int)dwBufSize);
    }
}

// Preferred path: structured ES frames with type + timestamp.
static void CALLBACK srs_hikvision_es_cb(LONG lPreviewHandle, NET_DVR_PACKET_INFO_EX *pack, void *pUser)
{
    SrsHikvisionStream *stream = (SrsHikvisionStream *)pUser;
    if (!stream || !pack || !pack->pPacketBuffer || pack->dwPacketSize == 0) {
        return;
    }
    // Prefer low 32-bit device timestamp when present.
    uint32_t dts_ms = pack->dwTimeStamp;
    stream->on_es_packet((int)pack->dwPacketType, dts_ms, (const char *)pack->pPacketBuffer, (int)pack->dwPacketSize);
}

SrsHikvisionEsPacket::SrsHikvisionEsPacket(int packet_type, uint32_t dts_ms, const char *data, int size)
{
    packet_type_ = packet_type;
    dts_ms_ = dts_ms;
    if (data && size > 0) {
        data_.assign(data, size);
    }
}

SrsHikvisionStream::SrsHikvisionStream(SrsHikvisionDevice *device, const string &stream_name, int channel, int subchannel)
{
    device_ = device;
    stream_name_ = stream_name;
    channel_ = channel;
    subchannel_ = subchannel;
    ref_count_ = 0;
    last_active_ = srs_time_now_cached();
    stopping_ = false;
    use_es_ = false;
    es_active_ = false;
    es_log_count_ = 0;
    real_handle_ = -1;
    muxer_ = new SrsHikvisionMuxer();
    ps_ctx_ = new SrsPsContext();
    trd_ = new SrsSTCoroutine("hik-stream", this);
    pthread_mutex_init(&lock_, NULL);
    wake_pipe_[0] = wake_pipe_[1] = -1;
    wake_fd_ = NULL;
}

SrsHikvisionStream::~SrsHikvisionStream()
{
    stop();
    srs_freep(trd_);
    srs_freep(muxer_);
    srs_freep(ps_ctx_);
    clear_packets();
    if (wake_fd_) {
        srs_close_stfd(wake_fd_);
        wake_fd_ = NULL;
    }
    if (wake_pipe_[0] > 0) {
        ::close(wake_pipe_[0]);
        wake_pipe_[0] = -1;
    }
    if (wake_pipe_[1] > 0) {
        ::close(wake_pipe_[1]);
        wake_pipe_[1] = -1;
    }
    pthread_mutex_destroy(&lock_);
}

string SrsHikvisionStream::stream_name()
{
    return stream_name_;
}

void SrsHikvisionStream::add_ref()
{
    ref_count_++;
    last_active_ = srs_time_now_cached();
}

int SrsHikvisionStream::release_ref()
{
    if (ref_count_ > 0) {
        ref_count_--;
    }
    last_active_ = srs_time_now_cached();
    return ref_count_;
}

int SrsHikvisionStream::ref_count()
{
    return ref_count_;
}

srs_utime_t SrsHikvisionStream::last_active()
{
    return last_active_;
}

void SrsHikvisionStream::on_sdk_data(int /*data_type*/, const char *data, int size)
{
    // PS path is fallback only. Some channels (esp. sub-stream) may not
    // deliver ES callbacks even when SetESRealPlayCallBack returns success.
    if (stopping_ || size <= 0 || !data) {
        return;
    }

    pthread_mutex_lock(&lock_);
    // Once ES media has arrived, never accept PS (prevents 2x frames / VLC seeking).
    if (es_active_ || !es_packets_.empty()) {
        pthread_mutex_unlock(&lock_);
        return;
    }

    string *pkt = new string(data, size);
    if ((int)ps_packets_.size() >= kHikvisionMaxQueuedPackets) {
        string *old = ps_packets_.front();
        ps_packets_.erase(ps_packets_.begin());
        srs_freep(old);
    }
    ps_packets_.push_back(pkt);
    pthread_mutex_unlock(&lock_);

    if (wake_pipe_[1] > 0) {
        char c = 1;
        ssize_t n = ::write(wake_pipe_[1], &c, 1);
        (void)n;
    }
}

void SrsHikvisionStream::on_es_packet(int packet_type, uint32_t dts_ms, const char *data, int size)
{
    if (stopping_ || size <= 0 || !data) {
        return;
    }
    // Keep video head + I/P/B frames; ignore audio/private for now.
    // packet_type: 0-file head, 1-I, 2-B, 3-P, 10-audio, 11-private
    if (packet_type != 0 && packet_type != 1 && packet_type != 2 && packet_type != 3) {
        return;
    }

    SrsHikvisionEsPacket *pkt = new SrsHikvisionEsPacket(packet_type, dts_ms, data, size);

    pthread_mutex_lock(&lock_);
    // Lock out PS for the rest of this RealPlay session.
    es_active_ = true;
    if (es_log_count_ < 8) {
        es_log_count_++;
        srs_trace("Hikvision: ES pkt type=%d size=%d dts=%u stream=%s",
                  packet_type, size, (unsigned)dts_ms, stream_name_.c_str());
    }
    if ((int)es_packets_.size() >= kHikvisionMaxQueuedPackets) {
        SrsHikvisionEsPacket *old = es_packets_.front();
        es_packets_.erase(es_packets_.begin());
        srs_freep(old);
    }
    es_packets_.push_back(pkt);
    // Drop any PS backlog to avoid mixing paths.
    for (size_t i = 0; i < ps_packets_.size(); i++) {
        srs_freep(ps_packets_[i]);
    }
    ps_packets_.clear();
    pthread_mutex_unlock(&lock_);

    if (wake_pipe_[1] > 0) {
        char c = 1;
        ssize_t n = ::write(wake_pipe_[1], &c, 1);
        (void)n;
    }
}

srs_error_t SrsHikvisionStream::start(const string &output)
{
    srs_error_t err = srs_success;

    if ((err = device_->ensure_login()) != srs_success) {
        return srs_error_wrap(err, "login");
    }

    if (pipe(wake_pipe_) < 0) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "create wake pipe");
    }
    // Non-blocking write end so callback never blocks.
    int flags = fcntl(wake_pipe_[1], F_GETFL, 0);
    fcntl(wake_pipe_[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(wake_pipe_[0], F_GETFL, 0);
    fcntl(wake_pipe_[0], F_SETFL, flags | O_NONBLOCK);

    if ((wake_fd_ = srs_netfd_open(wake_pipe_[0])) == NULL) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "open wake pipe");
    }
    wake_pipe_[0] = -1; // owned by stfd

    output_ = output;
    muxer_->setup(output, stream_name_);

    // Stream name SerialNO_CHANNEL_SUBCHANNEL maps to RealPlay_V40:
    //   lChannel     = CHANNEL     (1-based device channel)
    //   dwStreamType = SUBCHANNEL  (0=main, 1=sub, 2=stream3, ...)
    NET_DVR_PREVIEWINFO preview;
    memset(&preview, 0, sizeof(preview));
    preview.lChannel = channel_;
    preview.dwStreamType = (DWORD)subchannel_;
    preview.dwLinkMode = 0; // TCP
    preview.hPlayWnd = 0;
    // Non-blocking connect avoids long stalls in ST coroutine.
    preview.bBlocked = 0;

    srs_trace("Hikvision: RealPlay_V40 serial=%s lChannel=%d dwStreamType=%u (0=main,1=sub)",
              device_->conf().serialno_.c_str(), (int)preview.lChannel, (unsigned)preview.dwStreamType);

    // Always pass real-data callback (some SDK builds reject NULL callback).
    // Prefer ES structured frames when SetESRealPlayCallBack works; otherwise use PS demux.
    real_handle_ = NET_DVR_RealPlay_V40(device_->user_id(), &preview, srs_hikvision_realdata_cb, this);
    if (real_handle_ < 0) {
        DWORD e = NET_DVR_GetLastError();
        // NET_DVR_CHAN_NOTSUPPORT = 91
        const char *hint = "";
        if (e == 91) {
            hint = " (NET_DVR_CHAN_NOTSUPPORT: this lChannel may not support dwStreamType; "
                   "confirm NVR enabled sub-stream on that channel)";
        } else if (e == 1) {
            hint = " (user/password error)";
        } else if (e == 7) {
            hint = " (connect device failed)";
        }
        return srs_error_new(ERROR_HIKVISION_SDK,
                             "RealPlay serial=%s lChannel=%d dwStreamType=%u err=%u%s",
                             device_->conf().serialno_.c_str(), channel_, subchannel_, (unsigned)e, hint);
    }

    use_es_ = false;
    es_active_ = false;
    if (NET_DVR_SetESRealPlayCallBack(real_handle_, srs_hikvision_es_cb, this)) {
        use_es_ = true;
        srs_trace("Hikvision: ES callback enabled stream=%s", stream_name_.c_str());
    } else {
        srs_warn("Hikvision: ES callback failed err=%u, use PS demux stream=%s",
                 (unsigned)NET_DVR_GetLastError(), stream_name_.c_str());
    }

    if ((err = trd_->start()) != srs_success) {
        NET_DVR_StopRealPlay(real_handle_);
        real_handle_ = -1;
        return srs_error_wrap(err, "start stream coroutine");
    }

    srs_trace("Hikvision: RealPlay ok stream=%s handle=%ld ch=%d sub=%d es=%d",
              stream_name_.c_str(), (long)real_handle_, channel_, subchannel_, use_es_ ? 1 : 0);
    return err;
}

void SrsHikvisionStream::stop()
{
    // Must only be called from outside the stream coroutine (manager dispose / destructor).
    stopping_ = true;
    if (real_handle_ >= 0) {
        NET_DVR_StopRealPlay(real_handle_);
        srs_trace("Hikvision: StopRealPlay stream=%s handle=%ld", stream_name_.c_str(), (long)real_handle_);
        real_handle_ = -1;
    }
    if (trd_) {
        trd_->stop();
    }
    clear_packets();
}

srs_error_t SrsHikvisionStream::cycle()
{
    srs_error_t err = do_cycle();

    // Running ON this coroutine — do not trd_->stop()/join self (deadlock/assert).
    // Only release the SDK preview handle; outer stop()/destructor joins the thread.
    stopping_ = true;
    if (real_handle_ >= 0) {
        NET_DVR_StopRealPlay(real_handle_);
        srs_trace("Hikvision: StopRealPlay stream=%s handle=%ld (cycle end)", stream_name_.c_str(), (long)real_handle_);
        real_handle_ = -1;
    }
    clear_packets();

    return err;
}

srs_error_t SrsHikvisionStream::do_cycle()
{
    srs_error_t err = srs_success;

    while (!stopping_) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // Wait for wake or timeout to poll queue.
        char buf[256];
        srs_read(wake_fd_, buf, sizeof(buf), 100 * SRS_UTIME_MILLISECONDS);

        if ((err = consume_packets()) != srs_success) {
            // Only reset PS demuxer on PS parse errors; muxer soft-handles RTMP write failures.
            srs_warn("Hikvision: consume failed stream=%s, err=%s", stream_name_.c_str(), srs_error_desc(err).c_str());
            srs_freep(err);
            srs_freep(ps_ctx_);
            ps_ctx_ = new SrsPsContext();
        }
    }

    return err;
}

srs_error_t SrsHikvisionStream::consume_packets()
{
    // Prefer ES when available or once ES has been active; otherwise fall back to PS.
    // Sub-stream on some NVRs only delivers via real-data PS path.
    bool has_es = false;
    bool es_active = false;
    pthread_mutex_lock(&lock_);
    has_es = !es_packets_.empty();
    es_active = es_active_;
    pthread_mutex_unlock(&lock_);

    if (has_es || es_active) {
        return consume_es_packets();
    }
    return consume_ps_packets();
}

srs_error_t SrsHikvisionStream::consume_es_packets()
{
    srs_error_t err = srs_success;

    vector<SrsHikvisionEsPacket *> local;
    pthread_mutex_lock(&lock_);
    local.swap(es_packets_);
    pthread_mutex_unlock(&lock_);

    for (size_t i = 0; i < local.size(); i++) {
        SrsHikvisionEsPacket *pkt = local[i];
        if ((err = process_es_video(pkt)) != srs_success) {
            for (size_t j = i; j < local.size(); j++) {
                srs_freep(local[j]);
            }
            return srs_error_wrap(err, "es video");
        }
        srs_freep(pkt);
    }

    return err;
}

srs_error_t SrsHikvisionStream::process_es_video(SrsHikvisionEsPacket *pkt)
{
    srs_error_t err = srs_success;
    if (!pkt) {
        return err;
    }

    // packet_type: 0 head, 1 I, 2 B, 3 P
    // type=0 is often a tiny config blob; still feed demux for SPS/PPS if present.
    bool is_key = (pkt->packet_type_ == 0 || pkt->packet_type_ == 1);
    // dts_ms==0: muxer correct_timestamp() steps monotonically; do not mix wall-clock.
    uint32_t dts_ms = pkt->dts_ms_;

    if ((err = muxer_->on_es_video(pkt->data_.data(), (int)pkt->data_.size(), dts_ms, is_key)) != srs_success) {
        return srs_error_wrap(err, "mux es");
    }
    return err;
}

srs_error_t SrsHikvisionStream::consume_ps_packets()
{
    srs_error_t err = srs_success;

    vector<string *> local;
    pthread_mutex_lock(&lock_);
    local.swap(ps_packets_);
    pthread_mutex_unlock(&lock_);

    for (size_t i = 0; i < local.size(); i++) {
        string *pkt = local[i];
        SrsBuffer stream((char *)pkt->data(), (int)pkt->size());
        if ((err = ps_ctx_->decode(&stream, this)) != srs_success) {
            for (size_t j = i; j < local.size(); j++) {
                srs_freep(local[j]);
            }
            return srs_error_wrap(err, "ps decode");
        }
        srs_freep(pkt);
    }

    return err;
}

void SrsHikvisionStream::clear_packets()
{
    pthread_mutex_lock(&lock_);
    for (size_t i = 0; i < ps_packets_.size(); i++) {
        srs_freep(ps_packets_[i]);
    }
    ps_packets_.clear();
    for (size_t i = 0; i < es_packets_.size(); i++) {
        srs_freep(es_packets_[i]);
    }
    es_packets_.clear();
    pthread_mutex_unlock(&lock_);
}

srs_error_t SrsHikvisionStream::on_ts_message(SrsTsMessage *msg)
{
    return muxer_->on_ts_message(msg);
}

void SrsHikvisionStream::on_recover_mode(int /*nn_recover*/)
{
    // Drop partial state; next pack restarts.
}

// ---------------------------------------------------------------------------
// SrsHikvisionManager
// ---------------------------------------------------------------------------

SrsHikvisionManager::SrsHikvisionManager()
{
    config_ = _srs_config;
    sdk_inited_ = false;
    idle_trd_ = new SrsSTCoroutine("hik-idle", this);
}

SrsHikvisionManager::~SrsHikvisionManager()
{
    dispose();
    srs_freep(idle_trd_);
    config_ = NULL;
}

bool SrsHikvisionManager::enabled()
{
    return config_->get_hikvision_enabled();
}

srs_error_t SrsHikvisionManager::initialize()
{
    srs_error_t err = srs_success;

    if (!enabled()) {
        srs_trace("Hikvision: disabled");
        return err;
    }

    if ((err = load_devices()) != srs_success) {
        return srs_error_wrap(err, "load devices");
    }
    if ((err = init_sdk()) != srs_success) {
        return srs_error_wrap(err, "init sdk");
    }
    if ((err = idle_trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start idle reaper");
    }

    srs_trace("Hikvision: initialized, devices=%d, output=%s, idle_timeout=%ds, ptz_timeout=%ds",
              (int)devices_.size(), config_->get_hikvision_output().c_str(),
              (int)(config_->get_hikvision_idle_timeout() / SRS_UTIME_SECONDS),
              (int)(config_->get_hikvision_ptz_timeout() / SRS_UTIME_SECONDS));
    return err;
}

void SrsHikvisionManager::dispose()
{
    if (idle_trd_) {
        idle_trd_->stop();
    }

    for (map<string, SrsHikvisionStream *>::iterator it = streams_.begin(); it != streams_.end(); ++it) {
        srs_freep(it->second);
    }
    streams_.clear();

    for (map<string, SrsHikvisionPtzSession *>::iterator it = ptz_sessions_.begin(); it != ptz_sessions_.end(); ++it) {
        srs_freep(it->second);
    }
    ptz_sessions_.clear();

    for (map<string, SrsHikvisionDevice *>::iterator it = devices_.begin(); it != devices_.end(); ++it) {
        srs_freep(it->second);
    }
    devices_.clear();

    cleanup_sdk();
}

srs_error_t SrsHikvisionManager::load_devices()
{
    srs_error_t err = srs_success;

    vector<SrsConfDirective *> dirs = config_->get_hikvision_devices();
    if (dirs.empty()) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "no hikvision device configured");
    }

    for (size_t i = 0; i < dirs.size(); i++) {
        SrsConfDirective *d = dirs[i];
        SrsHikvisionDeviceConfig conf;
        conf.serialno_ = config_->get_hikvision_device_serialno(d);
        conf.host_ = config_->get_hikvision_device_host(d);
        conf.port_ = config_->get_hikvision_device_port(d);
        conf.user_ = config_->get_hikvision_device_user(d);
        conf.password_ = config_->get_hikvision_device_password(d);

        if (conf.serialno_.empty() || conf.host_.empty()) {
            return srs_error_new(ERROR_HIKVISION_CONFIG, "device requires serialno and host");
        }
        if (devices_.find(conf.serialno_) != devices_.end()) {
            return srs_error_new(ERROR_HIKVISION_CONFIG, "duplicate serialno %s", conf.serialno_.c_str());
        }

        devices_[conf.serialno_] = new SrsHikvisionDevice(conf);
        srs_trace("Hikvision: device serial=%s host=%s:%d user=%s",
                  conf.serialno_.c_str(), conf.host_.c_str(), conf.port_, conf.user_.c_str());
    }

    return err;
}

srs_error_t SrsHikvisionManager::init_sdk()
{
    srs_error_t err = srs_success;
    if (sdk_inited_) {
        return err;
    }

    string sdk_path = config_->get_hikvision_sdk_path();
    if (!sdk_path.empty()) {
        NET_DVR_LOCAL_SDK_PATH struComPath;
        memset(&struComPath, 0, sizeof(struComPath));
        strncpy(struComPath.sPath, sdk_path.c_str(), sizeof(struComPath.sPath) - 1);
        if (!NET_DVR_SetSDKInitCfg(NET_SDK_INIT_CFG_SDK_PATH, (void *)&struComPath)) {
            srs_warn("Hikvision: SetSDKInitCfg path=%s failed err=%u", sdk_path.c_str(), (unsigned)NET_DVR_GetLastError());
        } else {
            srs_trace("Hikvision: sdk_path=%s", sdk_path.c_str());
        }
    }

    if (!NET_DVR_Init()) {
        return srs_error_new(ERROR_HIKVISION_SDK, "NET_DVR_Init failed err=%u", (unsigned)NET_DVR_GetLastError());
    }

    NET_DVR_SetConnectTime(3000, 3);
    NET_DVR_SetReconnect(10000, TRUE);
    sdk_inited_ = true;
    srs_trace("Hikvision: NET_DVR_Init ok, sdk=0x%x", (unsigned)NET_DVR_GetSDKVersion());
    return err;
}

void SrsHikvisionManager::cleanup_sdk()
{
    if (sdk_inited_) {
        NET_DVR_Cleanup();
        sdk_inited_ = false;
        srs_trace("Hikvision: NET_DVR_Cleanup");
    }
}

SrsHikvisionDevice *SrsHikvisionManager::find_device(const string &serialno)
{
    map<string, SrsHikvisionDevice *>::iterator it = devices_.find(serialno);
    if (it == devices_.end()) {
        return NULL;
    }
    return it->second;
}

srs_error_t SrsHikvisionManager::on_play(const string &stream_name)
{
    srs_error_t err = srs_success;

    if (!enabled() || !sdk_inited_) {
        return err;
    }

    string serialno;
    int channel = 0;
    int subchannel = 0;
    if (!srs_hikvision_parse_stream(stream_name, serialno, channel, subchannel)) {
        // Not a hikvision stream name; ignore.
        return err;
    }

    SrsHikvisionDevice *device = find_device(serialno);
    if (!device) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "unknown serialno %s for stream %s",
                             serialno.c_str(), stream_name.c_str());
    }

    map<string, SrsHikvisionStream *>::iterator it = streams_.find(stream_name);
    if (it != streams_.end()) {
        it->second->add_ref();
        srs_trace("Hikvision: reuse stream=%s ref=%d", stream_name.c_str(), it->second->ref_count());
        return err;
    }

    SrsHikvisionStream *stream = new SrsHikvisionStream(device, stream_name, channel, subchannel);
    if ((err = stream->start(config_->get_hikvision_output())) != srs_success) {
        srs_freep(stream);
        return srs_error_wrap(err, "start stream %s", stream_name.c_str());
    }
    stream->add_ref();
    streams_[stream_name] = stream;
    srs_trace("Hikvision: start stream=%s serial=%s ch=%d sub=%d",
              stream_name.c_str(), serialno.c_str(), channel, subchannel);
    return err;
}

void SrsHikvisionManager::on_stop(const string &stream_name)
{
    if (!enabled()) {
        return;
    }

    map<string, SrsHikvisionStream *>::iterator it = streams_.find(stream_name);
    if (it == streams_.end()) {
        return;
    }

    int left = it->second->release_ref();
    srs_trace("Hikvision: stop play stream=%s ref=%d", stream_name.c_str(), left);

    // Free RealPlay promptly when last player leaves so other sub-streams can start.
    if (left <= 0) {
        SrsHikvisionStream *s = it->second;
        streams_.erase(it);
        srs_freep(s); // destructor stops RealPlay + coroutine
        srs_trace("Hikvision: disposed stream=%s on last player stop", stream_name.c_str());
    }
}

string SrsHikvisionManager::ptz_key(const string &serialno, int channel)
{
    return serialno + "_" + srs_fmt_sprintf("%d", channel);
}

SrsHikvisionPtzSession *SrsHikvisionManager::get_or_create_ptz(const string &serialno, int channel)
{
    string key = ptz_key(serialno, channel);
    map<string, SrsHikvisionPtzSession *>::iterator it = ptz_sessions_.find(key);
    if (it != ptz_sessions_.end()) {
        return it->second;
    }
    SrsHikvisionPtzSession *sess = new SrsHikvisionPtzSession();
    ptz_sessions_[key] = sess;
    return sess;
}

// moveDirFlags: [0=up, 1=right, 2=down, 3=left, 4=zoomin, 5=zoomout]
int SrsHikvisionManager::compute_ptz_command(SrsHikvisionPtzSession *sess)
{
    bool *f = sess->move_dir_flags_;
    int cmd = -1;

    if (f[0] && f[3]) { // up + left
        cmd = UP_LEFT;
        if (f[4])
            cmd = UP_LEFT_ZOOM_IN;
        else if (f[5])
            cmd = UP_LEFT_ZOOM_OUT;
    } else if (f[0] && f[1]) { // up + right
        cmd = UP_RIGHT;
        if (f[4])
            cmd = UP_RIGHT_ZOOM_IN;
        else if (f[5])
            cmd = UP_RIGHT_ZOOM_OUT;
    } else if (f[2] && f[3]) { // down + left
        cmd = DOWN_LEFT;
        if (f[4])
            cmd = DOWN_LEFT_ZOOM_IN;
        else if (f[5])
            cmd = DOWN_LEFT_ZOOM_OUT;
    } else if (f[2] && f[1]) { // down + right
        cmd = DOWN_RIGHT;
        if (f[4])
            cmd = DOWN_RIGHT_ZOOM_IN;
        else if (f[5])
            cmd = DOWN_RIGHT_ZOOM_OUT;
    } else if (f[0]) {
        cmd = TILT_UP;
        if (f[4])
            cmd = TILT_UP_ZOOM_IN;
        else if (f[5])
            cmd = TILT_UP_ZOOM_OUT;
    } else if (f[2]) {
        cmd = TILT_DOWN;
        if (f[4])
            cmd = TILT_DOWN_ZOOM_IN;
        else if (f[5])
            cmd = TILT_DOWN_ZOOM_OUT;
    } else if (f[1]) {
        cmd = PAN_RIGHT;
        if (f[4])
            cmd = PAN_RIGHT_ZOOM_IN;
        else if (f[5])
            cmd = PAN_RIGHT_ZOOM_OUT;
    } else if (f[3]) {
        cmd = PAN_LEFT;
        if (f[4])
            cmd = PAN_LEFT_ZOOM_IN;
        else if (f[5])
            cmd = PAN_LEFT_ZOOM_OUT;
    } else if (f[4]) {
        cmd = ZOOM_IN;
    } else if (f[5]) {
        cmd = ZOOM_OUT;
    }

    return cmd;
}

srs_error_t SrsHikvisionManager::stop_ptz(SrsHikvisionDevice *device, int channel, SrsHikvisionPtzSession *sess)
{
    srs_error_t err = srs_success;
    for (int i = 0; i < 6; i++) {
        sess->move_dir_flags_[i] = false;
    }
    if (sess->last_ptz_command_ >= 0) {
        if ((err = device->ptz_control(channel, sess->last_ptz_command_, true, sess->ptz_speed_)) != srs_success) {
            srs_warn("Hikvision: PTZ stop failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        sess->last_ptz_command_ = -1;
    }
    sess->last_active_ = srs_time_now_cached();
    return srs_success;
}

srs_error_t SrsHikvisionManager::apply_ptz_dir(SrsHikvisionDevice *device, int channel, SrsHikvisionPtzSession *sess,
                                              const string &dir, int speed)
{
    srs_error_t err = srs_success;

    if (speed >= 1 && speed <= 7) {
        sess->ptz_speed_ = speed;
    }

    // Update direction flags (opposing directions cancel).
    if (dir == "up") {
        sess->move_dir_flags_[0] = true;
        sess->move_dir_flags_[2] = false;
    } else if (dir == "down") {
        sess->move_dir_flags_[0] = false;
        sess->move_dir_flags_[2] = true;
    } else if (dir == "left") {
        sess->move_dir_flags_[1] = false;
        sess->move_dir_flags_[3] = true;
    } else if (dir == "right") {
        sess->move_dir_flags_[1] = true;
        sess->move_dir_flags_[3] = false;
    } else if (dir == "zoomin") {
        sess->move_dir_flags_[4] = true;
        sess->move_dir_flags_[5] = false;
    } else if (dir == "zoomout") {
        sess->move_dir_flags_[4] = false;
        sess->move_dir_flags_[5] = true;
    } else {
        // stop / unknown: clear all
        return stop_ptz(device, channel, sess);
    }

    int do_cmd = compute_ptz_command(sess);
    if (sess->last_ptz_command_ != do_cmd || sess->last_speed_ != sess->ptz_speed_) {
        sess->last_speed_ = sess->ptz_speed_;
        if (sess->last_ptz_command_ >= 0) {
            if ((err = device->ptz_control(channel, sess->last_ptz_command_, true, sess->ptz_speed_)) != srs_success) {
                return srs_error_wrap(err, "stop previous ptz");
            }
        }
        if (do_cmd >= 0) {
            if ((err = device->ptz_control(channel, do_cmd, false, sess->ptz_speed_)) != srs_success) {
                return srs_error_wrap(err, "start ptz");
            }
        }
        sess->last_ptz_command_ = do_cmd;
    }

    sess->last_active_ = srs_time_now_cached();
    return err;
}

srs_error_t SrsHikvisionManager::handle_control_json(const string &json, const string &stream_context)
{
    SrsJsonAny *any = SrsJsonAny::loads(json);
    if (!any || !any->is_object()) {
        srs_freep(any);
        return srs_error_new(ERROR_HIKVISION_CONFIG, "invalid control json");
    }
    SrsUniquePtr<SrsJsonObject> req(any->to_object());
    return handle_control(req.get(), stream_context);
}

srs_error_t SrsHikvisionManager::handle_control(SrsJsonObject *req, const string &stream_context)
{
    srs_error_t err = srs_success;

    if (!enabled() || !sdk_inited_) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "hikvision not enabled");
    }
    if (!req) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "null request");
    }

    string serialno;
    int channel = 0;

    SrsJsonAny *prop = NULL;
    if ((prop = req->ensure_property_string("stream")) != NULL) {
        string stream = prop->to_str();
        int sub = 0;
        if (!srs_hikvision_parse_stream(stream, serialno, channel, sub)) {
            return srs_error_new(ERROR_HIKVISION_STREAM, "invalid stream %s", stream.c_str());
        }
    } else if ((prop = req->ensure_property_string("serialno")) != NULL) {
        serialno = prop->to_str();
        if ((prop = req->ensure_property_integer("channel")) == NULL) {
            return srs_error_new(ERROR_HIKVISION_CONFIG, "channel required with serialno");
        }
        channel = (int)prop->to_integer();
    } else if (!stream_context.empty()) {
        int sub = 0;
        if (!srs_hikvision_parse_stream(stream_context, serialno, channel, sub)) {
            return srs_error_new(ERROR_HIKVISION_STREAM, "invalid stream context %s", stream_context.c_str());
        }
    } else {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "need stream or serialno+channel");
    }

    if (channel <= 0) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "invalid channel %d", channel);
    }

    SrsHikvisionDevice *device = find_device(serialno);
    if (!device) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "unknown serialno %s", serialno.c_str());
    }

    if ((prop = req->ensure_property_string("cmd")) == NULL) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "cmd required");
    }
    string cmd = prop->to_str();

    if (cmd == "ptz") {
        string dir = "stop";
        if ((prop = req->ensure_property_string("dir")) != NULL) {
            dir = prop->to_str();
        }
        int speed = 4;
        if ((prop = req->ensure_property_integer("speed")) != NULL) {
            speed = (int)prop->to_integer();
        }
        SrsHikvisionPtzSession *sess = get_or_create_ptz(serialno, channel);
        if ((err = apply_ptz_dir(device, channel, sess, dir, speed)) != srs_success) {
            return srs_error_wrap(err, "ptz dir=%s", dir.c_str());
        }
        return err;
    }

    if (cmd == "preset" || cmd == "save_preset") {
        if ((prop = req->ensure_property_integer("preset")) == NULL) {
            return srs_error_new(ERROR_HIKVISION_CONFIG, "preset required");
        }
        int preset = (int)prop->to_integer();
        int preset_cmd = (cmd == "save_preset") ? SET_PRESET : GOTO_PRESET;
        if ((err = device->ptz_preset(channel, preset_cmd, preset)) != srs_success) {
            return srs_error_wrap(err, "%s", cmd.c_str());
        }
        return err;
    }

    return srs_error_new(ERROR_HIKVISION_CONFIG, "unknown cmd %s", cmd.c_str());
}

void SrsHikvisionManager::reap_ptz_timeouts()
{
    srs_utime_t timeout = config_->get_hikvision_ptz_timeout();
    srs_utime_t now = srs_time_now_cached();

    for (map<string, SrsHikvisionPtzSession *>::iterator it = ptz_sessions_.begin(); it != ptz_sessions_.end(); ++it) {
        SrsHikvisionPtzSession *sess = it->second;
        if (sess->last_ptz_command_ < 0) {
            continue;
        }
        if ((now - sess->last_active_) < timeout) {
            continue;
        }

        // Key is serialno_channel — parse channel from end.
        string key = it->first;
        size_t p = key.find_last_of('_');
        if (p == string::npos) {
            continue;
        }
        string serialno = key.substr(0, p);
        int channel = ::atoi(key.substr(p + 1).c_str());
        SrsHikvisionDevice *device = find_device(serialno);
        if (!device) {
            continue;
        }

        srs_trace("Hikvision: PTZ auto-stop key=%s after %ds", key.c_str(),
                  (int)(timeout / SRS_UTIME_SECONDS));
        srs_error_t err = stop_ptz(device, channel, sess);
        if (err != srs_success) {
            srs_warn("Hikvision: PTZ auto-stop failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
}

srs_error_t SrsHikvisionManager::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = idle_trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "idle pull");
        }

        srs_usleep(1 * SRS_UTIME_SECONDS);

        // Auto-stop PTZ when no stop command within ptz_timeout.
        reap_ptz_timeouts();

        srs_utime_t idle = config_->get_hikvision_idle_timeout();
        srs_utime_t now = srs_time_now_cached();

        vector<string> to_remove;
        for (map<string, SrsHikvisionStream *>::iterator it = streams_.begin(); it != streams_.end(); ++it) {
            SrsHikvisionStream *s = it->second;
            if (s->ref_count() <= 0 && (now - s->last_active()) >= idle) {
                to_remove.push_back(it->first);
            }
        }

        for (size_t i = 0; i < to_remove.size(); i++) {
            map<string, SrsHikvisionStream *>::iterator it = streams_.find(to_remove[i]);
            if (it == streams_.end()) {
                continue;
            }
            srs_trace("Hikvision: idle dispose stream=%s", it->first.c_str());
            srs_freep(it->second);
            streams_.erase(it);
        }
    }

    return err;
}

// ---------------------------------------------------------------------------
// SrsGoApiHikvisionControl
// ---------------------------------------------------------------------------

SrsGoApiHikvisionControl::SrsGoApiHikvisionControl()
{
}

SrsGoApiHikvisionControl::~SrsGoApiHikvisionControl()
{
}

srs_error_t SrsGoApiHikvisionControl::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());

    if (!_srs_hikvision || !_srs_hikvision->enabled()) {
        res->set("code", SrsJsonAny::integer(ERROR_HIKVISION_CONFIG));
        res->set("msg", SrsJsonAny::str("hikvision disabled"));
        return srs_api_response(w, r, res->dumps());
    }

    string body;
    if ((err = r->body_read_all(body)) != srs_success) {
        return srs_error_wrap(err, "read body");
    }

    if ((err = _srs_hikvision->handle_control_json(body)) != srs_success) {
        int code = srs_error_code(err);
        string msg = srs_error_summary(err);
        srs_warn("Hikvision control error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        res->set("code", SrsJsonAny::integer(code));
        res->set("msg", SrsJsonAny::str(msg.c_str()));
        return srs_api_response(w, r, res->dumps());
    }

    res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    res->set("msg", SrsJsonAny::str("ok"));
    return srs_api_response(w, r, res->dumps());
}

#endif // SRS_HIKVISION
