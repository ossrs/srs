//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_hikvision.hpp>

#ifdef SRS_HIKVISION

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_client.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_st.hpp>
#include <srs_app_stream_bridge.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}
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
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_stack.hpp>
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

// Format HCNetSDK last error as "err=<code> msg=<NET_DVR_GetErrorMsg>".
// Pass already-captured code when GetLastError was called earlier; otherwise
// reads NET_DVR_GetLastError() now.
static string srs_hikvision_sdk_errmsg(DWORD err_code = (DWORD)-1)
{
    LONG e = (err_code == (DWORD)-1) ? (LONG)NET_DVR_GetLastError() : (LONG)err_code;
    char *msg = NET_DVR_GetErrorMsg(&e);
    if (msg && msg[0]) {
        return srs_fmt_sprintf("err=%ld msg=%s", (long)e, msg);
    }
    return srs_fmt_sprintf("err=%ld", (long)e);
}

// Resolve sdk_path to an absolute path. Relative paths follow process cwd, so
// systemd (often cwd=/) vs manual start (cwd=trunk) behave differently unless
// we canonicalize before NET_DVR_SetSDKInitCfg.
static string srs_hikvision_resolve_sdk_path(const string &configured)
{
    if (configured.empty()) {
        return configured;
    }

    string candidate = configured;
    if (configured[0] != '/') {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            candidate = string(cwd) + "/" + configured;
        }
    }

    char resolved[PATH_MAX];
    if (realpath(candidate.c_str(), resolved) != NULL) {
        return string(resolved);
    }
    // realpath fails when path does not exist; still pass absolute-ish candidate.
    return candidate;
}

// Preload HCNetSDK shared objects by absolute path. setenv(LD_LIBRARY_PATH)
// after process start does NOT affect glibc dlopen search; without this,
// RealPlay often fails with err=136 (HCPreview version mismatch) under systemd
// when cwd=/ and components cannot resolve libhpr/libHCCore.
// Returns srs_error if a critical library is missing or fails to load.
static srs_error_t srs_hikvision_preload_sdk_libs(const string &sdk_path)
{
    srs_error_t err = srs_success;

    // order matters: base deps first, then HCPreview and helpers.
    static const char *kCritical[] = {
        "libhpr.so",
        "libHCCore.so",
        "libhcnetsdk.so",
        "HCNetSDKCom/libHCCoreDevCfg.so",
        "HCNetSDKCom/libHCPreview.so",
        NULL,
    };
    static const char *kOptional[] = {
        "HCNetSDKCom/libStreamTransClient.so",
        "HCNetSDKCom/libSystemTransform.so",
        "HCNetSDKCom/libanalyzedata.so",
        "HCNetSDKCom/libHCVoiceTalk.so",
        "HCNetSDKCom/libAudioIntercom.so",
        "libcrypto.so",
        "libssl.so",
        NULL,
    };

    SrsPath path;
    int loaded = 0;
    for (int i = 0; kCritical[i]; i++) {
        string full = sdk_path + "/" + kCritical[i];
        if (!path.exists(full)) {
            return srs_error_new(ERROR_HIKVISION_SDK,
                                 "preload missing critical %s (sdk_path=%s). "
                                 "Copy a complete HCNetSDK tree (libhcnetsdk.so + HCNetSDKCom/) "
                                 "or fix hikvision.sdk_path",
                                 full.c_str(), sdk_path.c_str());
        }
        // Clear stale error then load.
        dlerror();
        void *h = dlopen(full.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            const char *dle = dlerror();
            return srs_error_new(ERROR_HIKVISION_SDK,
                                 "dlopen critical %s failed: %s (cwd-independent absolute load; "
                                 "check arch and deps with: ldd %s)",
                                 full.c_str(), dle ? dle : "unknown", full.c_str());
        }
        loaded++;
        srs_trace("Hikvision: preloaded critical %s", full.c_str());
    }
    for (int i = 0; kOptional[i]; i++) {
        string full = sdk_path + "/" + kOptional[i];
        if (!path.exists(full)) {
            srs_warn("Hikvision: preload skip optional missing %s", full.c_str());
            continue;
        }
        dlerror();
        void *h = dlopen(full.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            const char *dle = dlerror();
            srs_warn("Hikvision: dlopen optional %s failed: %s", full.c_str(), dle ? dle : "unknown");
        } else {
            loaded++;
            srs_trace("Hikvision: preloaded optional %s", full.c_str());
        }
    }
    srs_trace("Hikvision: preload done count=%d sdk_path=%s", loaded, sdk_path.c_str());
    return err;
}

// Unix timestamps used for playback start are always >= 1e9 (2001-09-09).
// Subchannel values are small (0,1,2,...), so this separates live vs VOD names.
static const int64_t kHikvisionPlaybackTsMin = 1000000000LL;

static bool srs_hikvision_all_digits(const string &s)
{
    if (s.empty()) {
        return false;
    }
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

static void srs_hikvision_unix_to_dvr_time(time_t t, NET_DVR_TIME *out)
{
    memset(out, 0, sizeof(*out));
    struct tm tm_buf;
    // Device OSD / FindFile are typically local wall clock on the NVR; use localtime.
    localtime_r(&t, &tm_buf);
    out->dwYear = (DWORD)(tm_buf.tm_year + 1900);
    out->dwMonth = (DWORD)(tm_buf.tm_mon + 1);
    out->dwDay = (DWORD)tm_buf.tm_mday;
    out->dwHour = (DWORD)tm_buf.tm_hour;
    out->dwMinute = (DWORD)tm_buf.tm_min;
    out->dwSecond = (DWORD)tm_buf.tm_sec;
}

static int64_t srs_hikvision_dvr_fields_to_unix(DWORD year, DWORD month, DWORD day, DWORD hour, DWORD minute, DWORD second)
{
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }
    struct tm tm_buf;
    memset(&tm_buf, 0, sizeof(tm_buf));
    tm_buf.tm_year = (int)year - 1900;
    tm_buf.tm_mon = (int)month - 1;
    tm_buf.tm_mday = (int)day;
    tm_buf.tm_hour = (int)hour;
    tm_buf.tm_min = (int)minute;
    tm_buf.tm_sec = (int)second;
    tm_buf.tm_isdst = -1;
    time_t t = mktime(&tm_buf);
    if (t == (time_t)-1) {
        return 0;
    }
    return (int64_t)t;
}

bool srs_hikvision_parse_stream(const string &stream, string &serialno, int &channel, int &subchannel, int64_t *start_unix_ts)
{
    if (start_unix_ts) {
        *start_unix_ts = 0;
    }

    // Parse from the right: SerialNO_CHANNEL_SUBCHANNEL or SerialNO_CHANNEL_UNIXTS
    size_t p2 = stream.find_last_of('_');
    if (p2 == string::npos || p2 == 0) {
        return false;
    }
    size_t p1 = stream.find_last_of('_', p2 - 1);
    if (p1 == string::npos || p1 == 0) {
        return false;
    }

    string ch_s = stream.substr(p1 + 1, p2 - p1 - 1);
    string last_s = stream.substr(p2 + 1);
    if (!srs_hikvision_all_digits(ch_s) || !srs_hikvision_all_digits(last_s)) {
        return false;
    }

    serialno = stream.substr(0, p1);
    channel = ::atoi(ch_s.c_str());
    if (serialno.empty() || channel <= 0) {
        return false;
    }

    // Playback: last token is unix timestamp (>= 1e9).
    // Live: last token is subchannel (0,1,2,...).
    if (last_s.size() >= 10) {
        int64_t ts = (int64_t)strtoll(last_s.c_str(), NULL, 10);
        if (ts >= kHikvisionPlaybackTsMin) {
            subchannel = 0; // main stream for VOD by default
            if (start_unix_ts) {
                *start_unix_ts = ts;
            }
            return true;
        }
    }

    subchannel = ::atoi(last_s.c_str());
    if (subchannel < 0) {
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
    g711_aac_enc_ = NULL;
    g711_aac_frame_ = NULL;
    g711_aac_pkt_ = NULL;
    g711_aac_ready_ = false;
    g711_aac_sh_sent_ = false;
    pprint_ = SrsPithyPrint::create_caster();
}

SrsHikvisionMuxer::~SrsHikvisionMuxer()
{
    close();
    if (g711_aac_enc_) {
        avcodec_free_context((AVCodecContext **)&g711_aac_enc_);
        g711_aac_enc_ = NULL;
    }
    if (g711_aac_frame_) {
        av_frame_free((AVFrame **)&g711_aac_frame_);
        g711_aac_frame_ = NULL;
    }
    if (g711_aac_pkt_) {
        av_packet_free((AVPacket **)&g711_aac_pkt_);
        g711_aac_pkt_ = NULL;
    }
    g711_pcm_f_.clear();
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
    if (!avs || avs->empty()) {
        return err;
    }

    // Peek: AAC ADTS starts with 0xFFF sync; otherwise Hikvision often uses raw G.711.
    int left = avs->left();
    char *p = avs->data() + avs->pos();
    bool maybe_adts = (left >= 2 && (unsigned char)p[0] == 0xff && ((unsigned char)p[1] & 0xf0) == 0xf0);

    if (!maybe_adts) {
        // Original stream audio for live (always publish, independent of talk).
        return on_es_g711(p, left, dts, true);
    }

    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac_->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            // Fallback remaining bytes as G.711 (common on NVR).
            srs_freep(err);
            int rem = avs->left();
            if (rem > 0) {
                return on_es_g711(avs->data() + avs->pos(), rem, dts, true);
            }
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

// ITU-T G.711 A-law → linear 16-bit (software, no libavcodec pcm_alaw needed).
static int16_t srs_hik_alaw_to_linear(uint8_t a_val)
{
    a_val ^= 0x55;
    int t = (a_val & 0x0f) << 4;
    int seg = (a_val & 0x70) >> 4;
    switch (seg) {
    case 0:
        t += 8;
        break;
    case 1:
        t += 0x108;
        break;
    default:
        t += 0x108;
        t <<= (seg - 1);
        break;
    }
    return (a_val & 0x80) ? t : -t;
}

// ITU-T G.711 μ-law → linear 16-bit.
static int16_t srs_hik_ulaw_to_linear(uint8_t u_val)
{
    u_val = ~u_val;
    int t = ((u_val & 0x0f) << 3) + 0x84;
    t <<= ((unsigned)u_val & 0x70) >> 4;
    return (u_val & 0x80) ? (0x84 - t) : (t - 0x84);
}

// Linear 16-bit → G.711 μ-law (Sun/CCITT).
static uint8_t srs_hik_linear_to_ulaw(int16_t sample)
{
    const int BIAS = 0x84;
    const int CLIP = 32635;
    int pcm = (int)sample;
    int sign = (pcm < 0) ? 0x80 : 0;
    if (pcm < 0) {
        pcm = -pcm;
        if (pcm > 32767) {
            pcm = 32767;
        }
    }
    if (pcm > CLIP) {
        pcm = CLIP;
    }
    pcm += BIAS;
    int exponent = 7;
    for (int mask = 0x4000; (pcm & mask) == 0 && exponent > 0; exponent--, mask >>= 1) {
    }
    int mantissa = (pcm >> (exponent + 3)) & 0x0f;
    return (uint8_t)(~(sign | (exponent << 4) | mantissa));
}

// Linear 16-bit → G.711 A-law (Sun polarity: +ve uses 0xD5 mask).
static uint8_t srs_hik_linear_to_alaw(int16_t sample)
{
    const int ALAW_MAX = 0xFFF;
    int pcm = (int)sample;
    int mask;
    if (pcm >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        pcm = -pcm - 8;
        if (pcm < 0) {
            pcm = 0;
        }
    }
    if (pcm > 32767) {
        pcm = 32767;
    }
    pcm >>= 3;
    if (pcm > ALAW_MAX) {
        pcm = ALAW_MAX;
    }
    int exponent = 7;
    for (int exp_mask = 0x400; (pcm & exp_mask) == 0 && exponent > 0; exponent--, exp_mask >>= 1) {
    }
    int mantissa = (pcm >> ((exponent == 0) ? 4 : (exponent + 3))) & 0x0f;
    return (uint8_t)(((exponent << 4) | mantissa) ^ mask);
}

// Encode n samples S16LE → G.711 bytes (1:1). alaw=true → A-law else μ-law.
static void srs_hik_encode_pcm_to_g711(const int16_t *pcm, int n, bool alaw, uint8_t *out)
{
    for (int i = 0; i < n; i++) {
        out[i] = alaw ? srs_hik_linear_to_alaw(pcm[i]) : srs_hik_linear_to_ulaw(pcm[i]);
    }
}

// Decode n G.711 bytes → S16LE. alaw=true → A-law else μ-law.
static void srs_hik_decode_g711_to_pcm(const uint8_t *g711, int n, bool alaw, int16_t *out)
{
    for (int i = 0; i < n; i++) {
        out[i] = alaw ? srs_hik_alaw_to_linear(g711[i]) : srs_hik_ulaw_to_linear(g711[i]);
    }
}

// Linear resample S16LE mono from src_rate → dst_rate (simple lerp).
static void srs_hik_resample_s16(const int16_t *src, int src_n, int src_rate, int dst_rate, std::vector<int16_t> &dst)
{
    dst.clear();
    if (src_n <= 0 || src_rate <= 0 || dst_rate <= 0) {
        return;
    }
    if (src_rate == dst_rate) {
        dst.assign(src, src + src_n);
        return;
    }
    int dst_n = (int)((int64_t)src_n * dst_rate / src_rate);
    if (dst_n <= 0) {
        return;
    }
    dst.resize((size_t)dst_n);
    for (int i = 0; i < dst_n; i++) {
        double pos = (double)i * (double)src_rate / (double)dst_rate;
        int i0 = (int)pos;
        int i1 = i0 + 1;
        if (i0 >= src_n) {
            i0 = src_n - 1;
        }
        if (i1 >= src_n) {
            i1 = src_n - 1;
        }
        double f = pos - (double)i0;
        double v = (1.0 - f) * (double)src[i0] + f * (double)src[i1];
        if (v > 32767.0) {
            v = 32767.0;
        }
        if (v < -32768.0) {
            v = -32768.0;
        }
        dst[(size_t)i] = (int16_t)v;
    }
}

srs_error_t SrsHikvisionMuxer::ensure_g711_aac_encoder()
{
    srs_error_t err = srs_success;
    if (g711_aac_ready_) {
        return err;
    }

    const AVCodec *codec = avcodec_find_encoder_by_name("aac");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    }
    if (!codec) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "AAC encoder not found in libavcodec");
    }

    AVCodecContext *enc = avcodec_alloc_context3(codec);
    if (!enc) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "alloc AAC encoder context failed");
    }
    enc->sample_rate = 8000;
    enc->channels = 1;
    enc->channel_layout = AV_CH_LAYOUT_MONO;
    enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc->bit_rate = 24000;
    enc->time_base = (AVRational){1, 8000};
    enc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    if (codec->sample_fmts) {
        enc->sample_fmt = codec->sample_fmts[0];
    }
    if (avcodec_open2(enc, codec, NULL) < 0) {
        avcodec_free_context(&enc);
        return srs_error_new(ERROR_HIKVISION_STREAM, "open AAC encoder failed");
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    if (!frame || !pkt) {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&enc);
        return srs_error_new(ERROR_HIKVISION_STREAM, "alloc AAC frame/packet failed");
    }
    frame->nb_samples = enc->frame_size > 0 ? enc->frame_size : 1024;
    frame->format = enc->sample_fmt;
    frame->channel_layout = enc->channel_layout;
    frame->sample_rate = enc->sample_rate;
    frame->channels = enc->channels;
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&enc);
        return srs_error_new(ERROR_HIKVISION_STREAM, "alloc AAC frame buffer failed");
    }

    g711_aac_enc_ = enc;
    g711_aac_frame_ = frame;
    g711_aac_pkt_ = pkt;
    g711_aac_ready_ = true;
    g711_aac_sh_sent_ = false;
    g711_pcm_f_.clear();
    srs_trace("Hikvision: G711→AAC encoder ready rate=%d frame=%d stream=%s", enc->sample_rate, frame->nb_samples,
              stream_.c_str());
    return err;
}

srs_error_t SrsHikvisionMuxer::encode_g711_pcm_to_aac(uint32_t dts)
{
    srs_error_t err = srs_success;
    AVCodecContext *enc = (AVCodecContext *)g711_aac_enc_;
    AVFrame *frame = (AVFrame *)g711_aac_frame_;
    AVPacket *pkt = (AVPacket *)g711_aac_pkt_;
    if (!enc || !frame || !pkt) {
        return err;
    }

    const int need = frame->nb_samples;
    while ((int)g711_pcm_f_.size() >= need) {
        if (av_frame_make_writable(frame) < 0) {
            return srs_error_new(ERROR_HIKVISION_STREAM, "AAC frame not writable");
        }
        // FLTP mono: plane 0
        float *dst = (float *)frame->data[0];
        for (int i = 0; i < need; i++) {
            dst[i] = g711_pcm_f_[i];
        }
        g711_pcm_f_.erase(g711_pcm_f_.begin(), g711_pcm_f_.begin() + need);

        frame->pts = AV_NOPTS_VALUE;
        int ret = avcodec_send_frame(enc, frame);
        if (ret < 0) {
            return srs_error_new(ERROR_HIKVISION_STREAM, "AAC send_frame failed ret=%d", ret);
        }
        while (ret >= 0) {
            ret = avcodec_receive_packet(enc, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                return srs_error_new(ERROR_HIKVISION_STREAM, "AAC receive_packet failed ret=%d", ret);
            }

            // Sequence header once from extradata (AudioSpecificConfig).
            if (!g711_aac_sh_sent_ && enc->extradata && enc->extradata_size > 0) {
                char *sh = new char[2 + enc->extradata_size];
                sh[0] = (char)0xae; // AAC + mono
                sh[1] = 0x00;
                memcpy(sh + 2, enc->extradata, enc->extradata_size);
                if ((err = rtmp_write_packet(SrsFrameTypeAudio, dts, sh, 2 + enc->extradata_size)) != srs_success) {
                    av_packet_unref(pkt);
                    return srs_error_wrap(err, "write aac sh");
                }
                g711_aac_sh_sent_ = true;
                srs_trace("Hikvision: inject AAC SH from G711 size=%d stream=%s", enc->extradata_size, stream_.c_str());
            }

            char *flv = new char[2 + pkt->size];
            flv[0] = (char)0xae;
            flv[1] = 0x01;
            memcpy(flv + 2, pkt->data, pkt->size);
            if ((err = rtmp_write_packet(SrsFrameTypeAudio, dts, flv, 2 + pkt->size)) != srs_success) {
                av_packet_unref(pkt);
                return srs_error_wrap(err, "write aac raw");
            }
            static int aac_log = 0;
            if (aac_log < 5) {
                aac_log++;
                srs_trace("Hikvision: inject AAC from G711 size=%d dts=%u stream=%s", pkt->size, dts, stream_.c_str());
            }
            av_packet_unref(pkt);
        }
    }
    return err;
}

srs_error_t SrsHikvisionMuxer::on_es_g711(const char *data, int size, uint32_t dts_ms, bool alaw)
{
    srs_error_t err = srs_success;
    if (!data || size <= 0) {
        return err;
    }
    if ((err = ensure_publish()) != srs_success) {
        return srs_error_wrap(err, "ensure publish for g711");
    }
    if ((err = ensure_g711_aac_encoder()) != srs_success) {
        return srs_error_wrap(err, "ensure g711 aac encoder");
    }

    uint32_t dts = correct_timestamp(dts_ms);

    // Software G.711 → float PCM @ 8kHz mono (matches AAC encoder sample_rate).
    g711_pcm_f_.reserve(g711_pcm_f_.size() + (size_t)size);
    for (int i = 0; i < size; i++) {
        int16_t s = alaw ? srs_hik_alaw_to_linear((uint8_t)data[i]) : srs_hik_ulaw_to_linear((uint8_t)data[i]);
        g711_pcm_f_.push_back((float)s / 32768.0f);
    }

    if ((err = encode_g711_pcm_to_aac(dts)) != srs_success) {
        return srs_error_wrap(err, "encode g711 aac");
    }
    return err;
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
    start_dchan_ = 0;
    start_dtalk_chan_ = 0;
    channel_info_loaded_ = false;
    channel_info_ok_ = false;
    channel_info_loaded_at_ = 0;
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

int SrsHikvisionDevice::start_dtalk_chan() const
{
    return start_dtalk_chan_;
}

// Match magicbear/py-hikevent startVoiceTalk:
//   if (cameraNo >= 1) cameraNo = byStartDTalkChan + cameraNo - 1;
//   else cameraNo = byStartDChan;
int SrsHikvisionDevice::voice_channel_for(int camera_channel) const
{
    if (camera_channel >= 1) {
        if (start_dtalk_chan_ > 0) {
            return start_dtalk_chan_ + camera_channel - 1;
        }
        return camera_channel;
    }
    if (start_dchan_ > 0) {
        return start_dchan_;
    }
    return 1;
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
        return srs_error_new(ERROR_HIKVISION_SDK, "login %s:%d serial=%s failed, %s",
                             conf_.host_.c_str(), conf_.port_, conf_.serialno_.c_str(),
                             srs_hikvision_sdk_errmsg().c_str());
    }

    start_dchan_ = (int)device_info.struDeviceV30.byStartDChan;
    start_dtalk_chan_ = (int)device_info.struDeviceV30.byStartDTalkChan;
    logged_in_ = true;
    srs_trace("Hikvision: login ok serial=%s host=%s:%d user_id=%ld startDChan=%d startDTalkChan=%d",
              conf_.serialno_.c_str(), conf_.host_.c_str(), conf_.port_, (long)user_id_, start_dchan_,
              start_dtalk_chan_);

    // Soft-load ISAPI channel list so play can fail fast on missing stream types.
    srs_error_t ch_err = ensure_channel_info();
    if (ch_err != srs_success) {
        srs_warn("Hikvision: channel info load failed serial=%s, err=%s",
                 conf_.serialno_.c_str(), srs_error_desc(ch_err).c_str());
        srs_freep(ch_err);
    }
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
    start_dchan_ = 0;
    start_dtalk_chan_ = 0;
    streaming_channel_ids_.clear();
    channel_info_loaded_ = false;
    channel_info_ok_ = false;
    channel_info_loaded_at_ = 0;
}

// ISAPI StreamingChannel id: ch*100 + streamType, streamType 1=main,2=sub,3=third,4=event.
static int srs_hikvision_isapi_stream_id(int channel, int subchannel)
{
    if (channel <= 0) {
        return 0;
    }
    int st = srs_max(0, srs_min(3, subchannel)) + 1;
    return channel * 100 + st;
}

srs_error_t SrsHikvisionDevice::ensure_channel_info()
{
    srs_error_t err = srs_success;

    // Refresh every 5 minutes so NVR channel changes are picked up without restart.
    const srs_utime_t kRefresh = 5 * 60 * SRS_UTIME_SECONDS;
    if (channel_info_loaded_ && channel_info_ok_ && channel_info_loaded_at_ != 0 &&
        srs_time_now_cached() - channel_info_loaded_at_ < kRefresh) {
        return err;
    }

    // ISAPI is HTTP (port 80), not SDK port 8000. Same credentials as SDK login.
    // Basic auth works on this NVR series; Digest also accepted by device.
    SrsHttpClient hc;
    if ((err = hc.initialize("http", conf_.host_, 80, 3 * SRS_UTIME_SECONDS)) != srs_success) {
        channel_info_loaded_ = true;
        channel_info_ok_ = false;
        return srs_error_wrap(err, "isapi connect %s:80", conf_.host_.c_str());
    }

    string token;
    if ((err = srs_av_base64_encode(conf_.user_ + ":" + conf_.password_, token)) != srs_success) {
        channel_info_loaded_ = true;
        channel_info_ok_ = false;
        return srs_error_wrap(err, "isapi auth encode");
    }
    hc.set_header("Authorization", "Basic " + token);
    hc.set_header("Accept", "application/xml");

    ISrsHttpMessage *msg_raw = NULL;
    if ((err = hc.get("/ISAPI/Streaming/channels", "", &msg_raw)) != srs_success) {
        channel_info_loaded_ = true;
        channel_info_ok_ = false;
        return srs_error_wrap(err, "isapi GET Streaming/channels");
    }
    SrsUniquePtr<ISrsHttpMessage> msg(msg_raw);

    int code = msg->status_code();
    string body;
    if ((err = msg->body_read_all(body)) != srs_success) {
        channel_info_loaded_ = true;
        channel_info_ok_ = false;
        return srs_error_wrap(err, "isapi read body code=%d", code);
    }
    if (code != 200) {
        channel_info_loaded_ = true;
        channel_info_ok_ = false;
        return srs_error_new(ERROR_HIKVISION_SDK, "isapi Streaming/channels HTTP %d body=%s",
                             code, body.substr(0, 200).c_str());
    }

    // Parse all <id>NNN</id> under StreamingChannelList (simple scan is enough).
    set<int> ids;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t a = body.find("<id>", pos);
        if (a == string::npos) {
            break;
        }
        a += 4;
        size_t b = body.find("</id>", a);
        if (b == string::npos) {
            break;
        }
        string num = body.substr(a, b - a);
        int id = ::atoi(num.c_str());
        if (id > 0) {
            ids.insert(id);
        }
        pos = b + 5;
    }

    if (ids.empty()) {
        channel_info_loaded_ = true;
        channel_info_ok_ = false;
        return srs_error_new(ERROR_HIKVISION_SDK, "isapi Streaming/channels empty ids body=%s",
                             body.substr(0, 200).c_str());
    }

    streaming_channel_ids_.swap(ids);
    channel_info_loaded_ = true;
    channel_info_ok_ = true;
    channel_info_loaded_at_ = srs_time_now_cached();

    // Compact summary for logs (first N ids).
    string sample;
    int n = 0;
    for (set<int>::iterator it = streaming_channel_ids_.begin();
         it != streaming_channel_ids_.end() && n < 16; ++it, ++n) {
        if (!sample.empty()) {
            sample += ",";
        }
        sample += srs_fmt_sprintf("%d", *it);
    }
    srs_trace("Hikvision: ISAPI channel info ok serial=%s count=%d sample=[%s%s]",
              conf_.serialno_.c_str(), (int)streaming_channel_ids_.size(), sample.c_str(),
              streaming_channel_ids_.size() > 16 ? ",..." : "");
    return err;
}

srs_error_t SrsHikvisionDevice::check_stream_available(int channel, int subchannel)
{
    srs_error_t err = srs_success;

    // Refresh cache if needed (soft).
    if ((err = ensure_channel_info()) != srs_success) {
        srs_warn("Hikvision: skip stream precheck (no channel info) serial=%s ch=%d sub=%d err=%s",
                 conf_.serialno_.c_str(), channel, subchannel, srs_error_desc(err).c_str());
        srs_freep(err);
        return srs_success;
    }
    if (!channel_info_ok_ || streaming_channel_ids_.empty()) {
        return srs_success;
    }

    int id = srs_hikvision_isapi_stream_id(channel, subchannel);
    if (id <= 0) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "invalid channel/sub ch=%d sub=%d", channel, subchannel);
    }
    if (streaming_channel_ids_.find(id) != streaming_channel_ids_.end()) {
        return srs_success;
    }

    // Build hint: which sibling streams exist for this channel.
    string have;
    for (int st = 1; st <= 4; st++) {
        int sid = channel * 100 + st;
        if (streaming_channel_ids_.count(sid)) {
            if (!have.empty()) {
                have += ",";
            }
            have += srs_fmt_sprintf("%d", sid);
        }
    }
    return srs_error_new(ERROR_HIKVISION_STREAM,
                         "stream not supported on NVR: serial=%s ch=%d sub=%d isapi_id=%d "
                         "(not in /ISAPI/Streaming/channels; available for ch=%d: [%s]; "
                         "try main stream sub=0 isapi_id=%d)",
                         conf_.serialno_.c_str(), channel, subchannel, id, channel, have.c_str(),
                         channel * 100 + 1);
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
        return srs_error_new(ERROR_HIKVISION_SDK, "PTZControl serial=%s ch=%d cmd=%d stop=%d speed=%d %s",
                             conf_.serialno_.c_str(), channel, command, stop ? 1 : 0, (int)dw_speed,
                             srs_hikvision_sdk_errmsg().c_str());
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
        return srs_error_new(ERROR_HIKVISION_SDK, "PTZPreset serial=%s ch=%d cmd=%d preset=%d %s",
                             conf_.serialno_.c_str(), channel, preset_cmd, preset_index,
                             srs_hikvision_sdk_errmsg().c_str());
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

// Preferred path: structured ES frames with type + timestamp (live + playback).
static void srs_hikvision_handle_es_pack(SrsHikvisionStream *stream, NET_DVR_PACKET_INFO_EX *pack)
{
    if (!stream || !pack || !pack->pPacketBuffer || pack->dwPacketSize == 0) {
        return;
    }
    uint32_t dts_ms = pack->dwTimeStamp;
    int64_t abs_ts = srs_hikvision_dvr_fields_to_unix(pack->dwYear, pack->dwMonth, pack->dwDay,
                                                       pack->dwHour, pack->dwMinute, pack->dwSecond);
    stream->on_es_packet((int)pack->dwPacketType, dts_ms, abs_ts, (const char *)pack->pPacketBuffer,
                         (int)pack->dwPacketSize);
}

static void CALLBACK srs_hikvision_es_cb(LONG /*lPreviewHandle*/, NET_DVR_PACKET_INFO_EX *pack, void *pUser)
{
    srs_hikvision_handle_es_pack((SrsHikvisionStream *)pUser, pack);
}

static void CALLBACK srs_hikvision_playback_es_cb(LONG /*lPlayHandle*/, NET_DVR_PACKET_INFO_EX *pack, void *pUser)
{
    srs_hikvision_handle_es_pack((SrsHikvisionStream *)pUser, pack);
}

static void CALLBACK srs_hikvision_playback_data_cb(LONG /*lPlayHandle*/, DWORD dwDataType, BYTE *pBuffer, DWORD dwBufSize,
                                                   void *pUser)
{
    SrsHikvisionStream *stream = (SrsHikvisionStream *)pUser;
    if (!stream) {
        return;
    }
    // NET_DVR_PLAYBACK_ALLFILEEND = 12: file / time-range finished.
    if (dwDataType == NET_DVR_PLAYBACK_ALLFILEEND) {
        stream->on_playback_eof();
        return;
    }
    if (dwDataType == NET_DVR_SYSHEAD) {
        if (dwBufSize >= 4 && pBuffer && pBuffer[0] == 0x00 && pBuffer[1] == 0x00 && pBuffer[2] == 0x01) {
            stream->on_sdk_data((int)dwDataType, (const char *)pBuffer, (int)dwBufSize);
        }
        return;
    }
    if (dwDataType == NET_DVR_STREAMDATA || dwDataType == NET_DVR_AUDIOSTREAMDATA) {
        if (pBuffer && dwBufSize > 0) {
            stream->on_sdk_data((int)dwDataType, (const char *)pBuffer, (int)dwBufSize);
        }
    }
}

SrsHikvisionEsPacket::SrsHikvisionEsPacket(int packet_type, uint32_t dts_ms, int64_t abs_unix_ts, const char *data, int size)
{
    packet_type_ = packet_type;
    dts_ms_ = dts_ms;
    abs_unix_ts_ = abs_unix_ts;
    if (data && size > 0) {
        data_.assign(data, size);
    }
}

SrsHikvisionStream::SrsHikvisionStream(SrsHikvisionDevice *device, const string &stream_name, int channel, int subchannel,
                                       int64_t start_unix_ts)
{
    device_ = device;
    stream_name_ = stream_name;
    channel_ = channel;
    subchannel_ = subchannel;
    start_unix_ts_ = start_unix_ts;
    is_playback_ = (start_unix_ts >= kHikvisionPlaybackTsMin);
    pb_aligned_ = !is_playback_;
    pb_need_normal_speed_ = false;
    pb_skip_count_ = 0;
    pb_pace_inited_ = false;
    pb_pace_first_dts_ms_ = 0;
    pb_pace_origin_wall_ = 0;
    pb_pace_last_delta_ms_ = 0;
    ref_count_ = 0;
    last_active_ = srs_time_now_cached();
    stopping_ = false;
    use_es_ = false;
    es_active_ = false;
    es_log_count_ = 0;
    ps_log_count_ = 0;
    nn_es_pkts_ = 0;
    nn_ps_pkts_ = 0;
    nn_sdk_cbs_ = 0;
    stream_start_wall_ = 0;
    no_media_warned_ = false;
    real_handle_ = -1;
    muxer_ = new SrsHikvisionMuxer();
    ps_ctx_ = new SrsPsContext();
    trd_ = new SrsSTCoroutine(is_playback_ ? "hik-vod" : "hik-stream", this);
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

void SrsHikvisionStream::on_sdk_data(int data_type, const char *data, int size)
{
    // PS path is fallback for video. Audio can arrive as:
    // - ES packet_type=10 via SetESRealPlayCallBack
    // - NET_DVR_AUDIOSTREAMDATA (3) via RealPlay real-data callback (even when ES video is active)
    if (stopping_ || size <= 0 || !data) {
        return;
    }

    // Always accept dedicated audio stream for live preview (independent of talk).
    // NET_DVR_AUDIOSTREAMDATA == 3
    if (data_type == 3) {
        static int astream_log = 0;
        if (astream_log < 5) {
            astream_log++;
            srs_trace("Hikvision: AUDIOSTREAMDATA size=%d stream=%s", size, stream_name_.c_str());
        }
        // Treat as ES audio type 10; muxer injects G.711 into live.
        on_es_packet(10, 0, 0, data, size);
        return;
    }

    pthread_mutex_lock(&lock_);
    nn_sdk_cbs_++;
    // Once ES media has arrived, never accept PS video (prevents 2x frames / VLC seeking).
    if (es_active_ || !es_packets_.empty()) {
        pthread_mutex_unlock(&lock_);
        return;
    }

    nn_ps_pkts_++;
    if (ps_log_count_ < 8) {
        ps_log_count_++;
        srs_trace("Hikvision: PS chunk size=%d stream=%s (fallback path)", size, stream_name_.c_str());
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

void SrsHikvisionStream::on_es_packet(int packet_type, uint32_t dts_ms, int64_t abs_unix_ts, const char *data, int size)
{
    if (stopping_ || size <= 0 || !data) {
        return;
    }
    // packet_type: 0-file head, 1-I, 2-B, 3-P, 10-audio, 11-private
    if (packet_type != 0 && packet_type != 1 && packet_type != 2 && packet_type != 3 && packet_type != 10) {
        return;
    }

    // Playback: NVR often starts at file head before requested time — drop until aligned.
    if (should_drop_playback_frame(packet_type, abs_unix_ts)) {
        return;
    }

    SrsHikvisionEsPacket *pkt = new SrsHikvisionEsPacket(packet_type, dts_ms, abs_unix_ts, data, size);

    pthread_mutex_lock(&lock_);
    // Lock out PS for the rest of this session.
    es_active_ = true;
    nn_es_pkts_++;
    if (es_log_count_ < 8) {
        es_log_count_++;
        srs_trace("Hikvision: ES pkt type=%d size=%d dts=%u abs=%lld stream=%s",
                  packet_type, size, (unsigned)dts_ms, (long long)abs_unix_ts, stream_name_.c_str());
    }
    // VOD: keep a short pre-buffer only. SDK bursts + large queue → LiveSource shrink.
    int max_q = is_playback_ ? 90 : kHikvisionMaxQueuedPackets;
    if ((int)es_packets_.size() >= max_q) {
        // Prefer dropping non-keyframes when over limit.
        bool dropped = false;
        for (size_t i = 0; i < es_packets_.size(); i++) {
            if (es_packets_[i]->packet_type_ != 1 && es_packets_[i]->packet_type_ != 0) {
                SrsHikvisionEsPacket *old = es_packets_[i];
                es_packets_.erase(es_packets_.begin() + i);
                srs_freep(old);
                dropped = true;
                break;
            }
        }
        if (!dropped) {
            SrsHikvisionEsPacket *old = es_packets_.front();
            es_packets_.erase(es_packets_.begin());
            srs_freep(old);
        }
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

void SrsHikvisionStream::on_playback_eof()
{
    srs_trace("Hikvision: playback EOF stream=%s", stream_name_.c_str());
    stopping_ = true;
    if (wake_pipe_[1] > 0) {
        char c = 1;
        ssize_t n = ::write(wake_pipe_[1], &c, 1);
        (void)n;
    }
}

bool SrsHikvisionStream::should_drop_playback_frame(int packet_type, int64_t abs_unix_ts)
{
    if (!is_playback_ || pb_aligned_) {
        return false;
    }

    // File/sys head: always drop until we lock onto requested time (muxer does not need it).
    if (packet_type == 0) {
        pb_skip_count_++;
        return true;
    }

    // OSD missing/zero: after a few frames just start on first I-frame (avoid black forever).
    // Also if we have already skipped a lot (NVR 1x from file head, or TZ mismatch),
    // force-align on next I-frame so the player is not stuck "starting".
    const int kForceAlignAfterSkips = 90; // ~3s at 30fps of drops, or sooner with I-frames only
    bool force_align = (pb_skip_count_ >= kForceAlignAfterSkips);

    // If OSD time available: drop frames strictly before requested start (5s grace for clock skew).
    if (!force_align && abs_unix_ts > 0 && abs_unix_ts + 5 < start_unix_ts_) {
        pb_skip_count_++;
        if ((pb_skip_count_ % 60) == 1) {
            srs_trace("Hikvision: playback skip pre-start frame abs=%lld need>=%lld skipped=%d stream=%s",
                      (long long)abs_unix_ts, (long long)start_unix_ts_, pb_skip_count_, stream_name_.c_str());
        }
        return true;
    }

    // Align output on first I-frame at/after start so FLV starts cleanly.
    if (packet_type != 1) {
        pb_skip_count_++;
        return true;
    }

    pb_aligned_ = true;
    // Request normal speed after any PLAYFAST done at start (best-effort).
    pb_need_normal_speed_ = true;
    srs_trace("Hikvision: playback aligned stream=%s request_ts=%lld first_abs=%lld skipped=%d force=%d",
              stream_name_.c_str(), (long long)start_unix_ts_, (long long)abs_unix_ts, pb_skip_count_,
              force_align ? 1 : 0);
    return false;
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

    if (is_playback_) {
        if ((err = start_playback()) != srs_success) {
            return srs_error_wrap(err, "start playback");
        }
    } else {
        if ((err = start_live()) != srs_success) {
            return srs_error_wrap(err, "start live");
        }
    }

    // RealPlay may return success while NVR never delivers this stream type
    // (e.g. sub-stream notSupport). Fail play request instead of hanging FLV/RTC.
    // Live: 5s is enough. VOD: allow longer for NVR to open recording.
    srs_utime_t media_timeout = is_playback_ ? 10 * SRS_UTIME_SECONDS : 5 * SRS_UTIME_SECONDS;
    if ((err = wait_first_media(media_timeout)) != srs_success) {
        stop_sdk_handle();
        return err;
    }

    if ((err = trd_->start()) != srs_success) {
        stop_sdk_handle();
        return srs_error_wrap(err, "start stream coroutine");
    }

    return err;
}

bool SrsHikvisionStream::has_media_packets()
{
    pthread_mutex_lock(&lock_);
    bool ok = (nn_es_pkts_ > 0) || (nn_ps_pkts_ > 0) || !es_packets_.empty() || !ps_packets_.empty();
    pthread_mutex_unlock(&lock_);
    return ok;
}

srs_error_t SrsHikvisionStream::wait_first_media(srs_utime_t timeout)
{
    srs_error_t err = srs_success;
    if (timeout <= 0) {
        return err;
    }

    srs_utime_t start = srs_time_now_realtime();
    while (!has_media_packets()) {
        if (stopping_) {
            return srs_error_new(ERROR_HIKVISION_STREAM, "stream stopped before media stream=%s",
                                 stream_name_.c_str());
        }
        srs_utime_t elapsed = srs_time_now_realtime() - start;
        if (elapsed >= timeout) {
            int64_t es_n = 0, ps_n = 0, sdk_n = 0;
            pthread_mutex_lock(&lock_);
            es_n = nn_es_pkts_;
            ps_n = nn_ps_pkts_;
            sdk_n = nn_sdk_cbs_;
            pthread_mutex_unlock(&lock_);
            return srs_error_new(ERROR_HIKVISION_STREAM,
                                 "no media within %dms stream=%s ch=%d sub=%d es_pkts=%lld ps_pkts=%lld sdk_cbs=%lld "
                                 "(channel/stream type may not exist on NVR; try main stream *_0)",
                                 (int)srsu2ms(timeout), stream_name_.c_str(), channel_, subchannel_,
                                 (long long)es_n, (long long)ps_n, (long long)sdk_n);
        }
        // Yield so SDK callbacks can enqueue; short sleep for low latency fail-path.
        srs_usleep(50 * SRS_UTIME_MILLISECONDS);
    }

    srs_trace("Hikvision: first media ready stream=%s wait=%dms",
              stream_name_.c_str(), (int)srsu2ms(srs_time_now_realtime() - start));
    return err;
}

srs_error_t SrsHikvisionStream::start_live()
{
    srs_error_t err = srs_success;

    // Pre-check via ISAPI Streaming/channels (fail fast when NVR has no this stream type).
    // e.g. ch6 only has 601+604, not 602 → G75965391_6_1 rejected without RealPlay hang.
    if ((err = device_->check_stream_available(channel_, subchannel_)) != srs_success) {
        return srs_error_wrap(err, "stream precheck");
    }

    // Stream name SerialNO_CHANNEL_SUBCHANNEL maps to RealPlay_V40:
    //   lChannel     = CHANNEL     (1-based device channel)
    //   dwStreamType = SUBCHANNEL  (0=main, 1=sub, 2=stream3, ...)
    NET_DVR_PREVIEWINFO preview;
    memset(&preview, 0, sizeof(preview));
    preview.lChannel = channel_;
    preview.dwStreamType = (DWORD)subchannel_;
    preview.dwLinkMode = 0; // TCP
    preview.hPlayWnd = 0;
    preview.bBlocked = 0;

    srs_trace("Hikvision: RealPlay_V40 serial=%s lChannel=%d dwStreamType=%u (0=main,1=sub)",
              device_->conf().serialno_.c_str(), (int)preview.lChannel, (unsigned)preview.dwStreamType);

    real_handle_ = NET_DVR_RealPlay_V40(device_->user_id(), &preview, srs_hikvision_realdata_cb, this);
    if (real_handle_ < 0) {
        DWORD e = NET_DVR_GetLastError();
        const char *hint = "";
        if (e == 91) {
            hint = " (this lChannel may not support dwStreamType; confirm NVR enabled sub-stream)";
        } else if (e == 1) {
            hint = " (user/password error)";
        } else if (e == 7) {
            hint = " (connect device failed)";
        } else if (e == 136) {
            hint = " (HCPreview component version mismatch with libhcnetsdk; check sdk_path/HCNetSDKCom)";
        }
        return srs_error_new(ERROR_HIKVISION_SDK,
                             "RealPlay serial=%s lChannel=%d dwStreamType=%u %s%s",
                             device_->conf().serialno_.c_str(), channel_, subchannel_,
                             srs_hikvision_sdk_errmsg(e).c_str(), hint);
    }

    use_es_ = false;
    es_active_ = false;
    if (NET_DVR_SetESRealPlayCallBack(real_handle_, srs_hikvision_es_cb, this)) {
        use_es_ = true;
        srs_trace("Hikvision: ES callback enabled stream=%s", stream_name_.c_str());
    } else {
        srs_warn("Hikvision: ES callback failed %s, use PS demux stream=%s",
                 srs_hikvision_sdk_errmsg().c_str(), stream_name_.c_str());
    }

    stream_start_wall_ = srs_time_now_cached();
    srs_trace("Hikvision: RealPlay ok stream=%s handle=%ld ch=%d sub=%d es=%d",
              stream_name_.c_str(), (long)real_handle_, channel_, subchannel_, use_es_ ? 1 : 0);
    return err;
}

srs_error_t SrsHikvisionStream::start_playback()
{
    srs_error_t err = srs_success;

    // PlayBackByTime_V40: stream SerialNO_CHANNEL_UNIXTS from start_unix_ts_ for 24h window.
    NET_DVR_VOD_PARA vod;
    memset(&vod, 0, sizeof(vod));
    vod.dwSize = sizeof(vod);
    vod.struIDInfo.dwSize = sizeof(vod.struIDInfo);
    vod.struIDInfo.dwChannel = (DWORD)channel_;
    vod.byStreamType = (BYTE)srs_max(0, srs_min(3, subchannel_));
    vod.hWnd = 0;
    vod.byDrawFrame = 0;

    time_t begin = (time_t)start_unix_ts_;
    // 24h window; if still in the future, clamp end to now+1s so range is valid.
    time_t end = begin + 24 * 3600;
    time_t now = time(NULL);
    if (end < begin + 60) {
        end = begin + 3600;
    }
    if (begin > now + 60) {
        srs_warn("Hikvision: playback start_ts=%lld is in the future (now=%lld)",
                 (long long)begin, (long long)now);
    }

    srs_hikvision_unix_to_dvr_time(begin, &vod.struBeginTime);
    srs_hikvision_unix_to_dvr_time(end, &vod.struEndTime);

    srs_trace("Hikvision: PlayBackByTime serial=%s ch=%d streamType=%u begin=%04u-%02u-%02u %02u:%02u:%02u "
              "end=%04u-%02u-%02u %02u:%02u:%02u (unix=%lld)",
              device_->conf().serialno_.c_str(), channel_, (unsigned)vod.byStreamType,
              (unsigned)vod.struBeginTime.dwYear, (unsigned)vod.struBeginTime.dwMonth,
              (unsigned)vod.struBeginTime.dwDay, (unsigned)vod.struBeginTime.dwHour,
              (unsigned)vod.struBeginTime.dwMinute, (unsigned)vod.struBeginTime.dwSecond,
              (unsigned)vod.struEndTime.dwYear, (unsigned)vod.struEndTime.dwMonth,
              (unsigned)vod.struEndTime.dwDay, (unsigned)vod.struEndTime.dwHour,
              (unsigned)vod.struEndTime.dwMinute, (unsigned)vod.struEndTime.dwSecond,
              (long long)start_unix_ts_);

    real_handle_ = NET_DVR_PlayBackByTime_V40(device_->user_id(), &vod);
    if (real_handle_ < 0) {
        return srs_error_new(ERROR_HIKVISION_SDK,
                             "PlayBackByTime serial=%s ch=%d start_ts=%lld %s",
                             device_->conf().serialno_.c_str(), channel_, (long long)start_unix_ts_,
                             srs_hikvision_sdk_errmsg().c_str());
    }

    use_es_ = false;
    es_active_ = false;
    if (NET_DVR_SetPlayBackESCallBack(real_handle_, srs_hikvision_playback_es_cb, this)) {
        use_es_ = true;
        srs_trace("Hikvision: playback ES callback enabled stream=%s", stream_name_.c_str());
    } else {
        srs_warn("Hikvision: SetPlayBackESCallBack failed %s, try PS data callback stream=%s",
                 srs_hikvision_sdk_errmsg().c_str(), stream_name_.c_str());
        if (!NET_DVR_SetPlayDataCallBack_V40(real_handle_, srs_hikvision_playback_data_cb, this)) {
            stop_sdk_handle();
            return srs_error_new(ERROR_HIKVISION_SDK, "SetPlayDataCallBack_V40 failed %s",
                                 srs_hikvision_sdk_errmsg().c_str());
        }
    }

    // Start streaming data (required after PlayBackByTime_*).
    if (!NET_DVR_PlayBackControl_V40(real_handle_, NET_DVR_PLAYSTART, NULL, 0, NULL, NULL)) {
        stop_sdk_handle();
        return srs_error_new(ERROR_HIKVISION_SDK, "PlayBackControl PLAYSTART failed %s",
                             srs_hikvision_sdk_errmsg().c_str());
    }

    // Seek to requested wall time. Do NOT leave PLAYFAST on — it floods the live
    // source queue (see "shrinking, removed=700+") and breaks HTTP-FLV/WebRTC.
    NET_DVR_TIME seek_t;
    srs_hikvision_unix_to_dvr_time(begin, &seek_t);
    if (!NET_DVR_PlayBackControl_V40(real_handle_, NET_DVR_PLAYSETTIME, &seek_t, sizeof(seek_t), NULL, NULL)) {
        srs_warn("Hikvision: PLAYSETTIME failed %s (will drop pre-start frames in software)",
                 srs_hikvision_sdk_errmsg().c_str());
    } else {
        srs_trace("Hikvision: PLAYSETTIME ok to %04u-%02u-%02u %02u:%02u:%02u",
                  (unsigned)seek_t.dwYear, (unsigned)seek_t.dwMonth, (unsigned)seek_t.dwDay,
                  (unsigned)seek_t.dwHour, (unsigned)seek_t.dwMinute, (unsigned)seek_t.dwSecond);
    }
    // Force 1x immediately after start/seek.
    if (!NET_DVR_PlayBackControl_V40(real_handle_, NET_DVR_PLAYNORMAL, NULL, 0, NULL, NULL)) {
        srs_warn("Hikvision: PLAYNORMAL at start failed %s", srs_hikvision_sdk_errmsg().c_str());
    }

    stream_start_wall_ = srs_time_now_cached();
    srs_trace("Hikvision: PlayBack ok stream=%s handle=%ld ch=%d start_ts=%lld es=%d",
              stream_name_.c_str(), (long)real_handle_, channel_, (long long)start_unix_ts_, use_es_ ? 1 : 0);
    return err;
}

void SrsHikvisionStream::stop_sdk_handle()
{
    if (real_handle_ < 0) {
        return;
    }
    if (is_playback_) {
        NET_DVR_StopPlayBack(real_handle_);
        srs_trace("Hikvision: StopPlayBack stream=%s handle=%ld", stream_name_.c_str(), (long)real_handle_);
    } else {
        NET_DVR_StopRealPlay(real_handle_);
        srs_trace("Hikvision: StopRealPlay stream=%s handle=%ld", stream_name_.c_str(), (long)real_handle_);
    }
    real_handle_ = -1;
}

void SrsHikvisionStream::stop()
{
    // Must only be called from outside the stream coroutine (manager dispose / destructor).
    stopping_ = true;
    stop_sdk_handle();
    if (trd_) {
        trd_->stop();
    }
    clear_packets();
}

srs_error_t SrsHikvisionStream::cycle()
{
    srs_error_t err = do_cycle();

    // Running ON this coroutine — do not trd_->stop()/join self (deadlock/assert).
    // Only release the SDK handle; outer stop()/destructor joins the thread.
    stopping_ = true;
    if (real_handle_ >= 0) {
        if (is_playback_) {
            NET_DVR_StopPlayBack(real_handle_);
            srs_trace("Hikvision: StopPlayBack stream=%s handle=%ld (cycle end)", stream_name_.c_str(),
                      (long)real_handle_);
        } else {
            NET_DVR_StopRealPlay(real_handle_);
            srs_trace("Hikvision: StopRealPlay stream=%s handle=%ld (cycle end)", stream_name_.c_str(),
                      (long)real_handle_);
        }
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

        // After catch-up (PLAYFAST at start), restore normal speed from ST thread.
        if (pb_need_normal_speed_ && real_handle_ >= 0) {
            pb_need_normal_speed_ = false;
            if (!NET_DVR_PlayBackControl_V40(real_handle_, NET_DVR_PLAYNORMAL, NULL, 0, NULL, NULL)) {
                srs_warn("Hikvision: PLAYNORMAL failed %s", srs_hikvision_sdk_errmsg().c_str());
            } else {
                srs_trace("Hikvision: PLAYNORMAL after align stream=%s", stream_name_.c_str());
            }
        }

        // Wait for wake or timeout to poll queue.
        char buf[256];
        srs_read(wake_fd_, buf, sizeof(buf), 100 * SRS_UTIME_MILLISECONDS);

        // RealPlay may succeed while NVR never delivers media (sub-stream disabled, etc.).
        if (!no_media_warned_ && stream_start_wall_ != 0 &&
            srs_time_now_cached() - stream_start_wall_ > 3 * SRS_UTIME_SECONDS) {
            int64_t es_n = 0, ps_n = 0, sdk_n = 0;
            pthread_mutex_lock(&lock_);
            es_n = nn_es_pkts_;
            ps_n = nn_ps_pkts_;
            sdk_n = nn_sdk_cbs_;
            pthread_mutex_unlock(&lock_);
            if (es_n == 0 && ps_n == 0) {
                no_media_warned_ = true;
                srs_warn("Hikvision: no media after 3s stream=%s ch=%d sub=%d es_cb_set=%d "
                         "es_pkts=%lld ps_pkts=%lld sdk_cbs=%lld "
                         "(check NVR dual-stream/sub-stream encoding for this channel; try main stream *_0)",
                         stream_name_.c_str(), channel_, subchannel_, use_es_ ? 1 : 0,
                         (long long)es_n, (long long)ps_n, (long long)sdk_n);
            }
        }

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
        if (pkt && pkt->packet_type_ == 10) {
            // SDK audio ES: typically raw G.711 A-law @ 8kHz mono.
            uint32_t dts_ms = pkt->dts_ms_;
            if ((err = muxer_->on_es_g711(pkt->data_.data(), (int)pkt->data_.size(), dts_ms, true)) != srs_success) {
                srs_warn("Hikvision: es g711 audio failed stream=%s, err=%s", stream_name_.c_str(),
                         srs_error_desc(err).c_str());
                srs_freep(err);
            }
            srs_freep(pkt);
            continue;
        }
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

    // packet_type: 0 head, 1 I, 2 B, 3 P (audio handled in consume_es_packets)
    // type=0 is often a tiny config blob; still feed demux for SPS/PPS if present.
    bool is_key = (pkt->packet_type_ == 0 || pkt->packet_type_ == 1);
    // dts_ms==0: muxer correct_timestamp() steps monotonically; do not mix wall-clock.
    uint32_t dts_ms = pkt->dts_ms_;

    // VOD pacing: HCNetSDK often delivers frames as a burst (or residual fast mode).
    // Without sleeping to media timeline, LiveSource queue exceeds 30s and shrinks
    // ("shrinking, removed=700+"), so players see stalls / only SPS.
    if (is_playback_ && pb_aligned_) {
        if (!pb_pace_inited_) {
            pb_pace_inited_ = true;
            pb_pace_first_dts_ms_ = (int64_t)dts_ms;
            pb_pace_origin_wall_ = srs_time_now_realtime();
            pb_pace_last_delta_ms_ = 0;
        } else {
            int64_t delta_ms = (int64_t)dts_ms - pb_pace_first_dts_ms_;
            if (delta_ms < 0) {
                // wrap / reset base
                pb_pace_first_dts_ms_ = (int64_t)dts_ms;
                pb_pace_origin_wall_ = srs_time_now_realtime();
                pb_pace_last_delta_ms_ = 0;
                delta_ms = 0;
            }
            // Cap pathological jumps so we do not sleep for hours.
            if (delta_ms > pb_pace_last_delta_ms_ + 1000) {
                delta_ms = pb_pace_last_delta_ms_ + 40;
                // Re-anchor so subsequent frames stay consistent.
                pb_pace_first_dts_ms_ = (int64_t)dts_ms - delta_ms;
            }
            pb_pace_last_delta_ms_ = delta_ms;

            srs_utime_t target = pb_pace_origin_wall_ + (srs_utime_t)delta_ms * SRS_UTIME_MILLISECONDS;
            srs_utime_t now = srs_time_now_realtime();
            // Allow small lead (50ms) so we stay slightly ahead of players.
            if (target > now + 50 * SRS_UTIME_MILLISECONDS) {
                srs_utime_t sleep_for = target - now;
                // Never sleep more than 2s per frame (safety).
                if (sleep_for > 2 * SRS_UTIME_SECONDS) {
                    sleep_for = 2 * SRS_UTIME_SECONDS;
                }
                srs_usleep(sleep_for);
            }
        }
    }

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
// Voice talk (WebRTC DataChannel ↔ NET_DVR VoiceCom MR)
// ---------------------------------------------------------------------------

ISrsHikvisionTalkListener::ISrsHikvisionTalkListener()
{
}

ISrsHikvisionTalkListener::~ISrsHikvisionTalkListener()
{
}

srs_error_t ISrsHikvisionTalkListener::dc_send_text(const string & /*s*/)
{
    return srs_error_new(ERROR_HIKVISION_CONFIG, "dc_send_text requires WebRTC DataChannel peer");
}

srs_error_t ISrsHikvisionTalkListener::dc_send_binary(const char * /*data*/, int /*len*/)
{
    return srs_error_new(ERROR_HIKVISION_CONFIG, "dc_send_binary requires WebRTC DataChannel peer");
}

void ISrsHikvisionTalkListener::dc_acquire()
{
}

void ISrsHikvisionTalkListener::dc_release()
{
}

void ISrsHikvisionTalkListener::dc_note_play_ack(int64_t /*got*/)
{
}

int64_t ISrsHikvisionTalkListener::dc_play_ack_got() const
{
    return 0;
}

bool ISrsHikvisionTalkListener::dc_alive() const
{
    return true;
}

static void CALLBACK srs_hikvision_voice_cb(LONG /*lVoiceComHandle*/, char *pRecvDataBuffer, DWORD dwBufSize,
                                           BYTE byAudioFlag, void *pUser)
{
    SrsHikvisionTalkSession *sess = (SrsHikvisionTalkSession *)pUser;
    if (!sess || !pRecvDataBuffer || dwBufSize == 0) {
        return;
    }
    sess->on_voice_data(pRecvDataBuffer, (int)dwBufSize, (int)byAudioFlag);
}

SrsHikvisionTalkSession::SrsHikvisionTalkSession(SrsHikvisionDevice *device, int channel)
{
    device_ = device;
    channel_ = channel;
    // Placeholder; start() remaps via device_->voice_channel_for (byStartDTalkChan + ch - 1).
    voice_chan_ = channel > 0 ? channel : 1;
    voice_handle_ = -1;
    audio_enc_type_ = 2; // G711_A default
    sample_rate_hz_ = 8000;
    client_pcm_rate_hz_ = 8000; // browser always sends S16LE at this rate
    stopping_ = false;
    last_active_ = srs_time_now_cached();
    trd_ = new SrsSTCoroutine("hik-talk", this);
    pthread_mutex_init(&lock_, NULL);
    wake_pipe_[0] = wake_pipe_[1] = -1;
    wake_fd_ = NULL;
    g711_enc_ = NULL;
    // hikevent: InitG711Encoder then for G711_U in_frame_size/=2 → typically 320 PCM → 160 G711.
    g711_in_frame_bytes_ = 320;
}

SrsHikvisionTalkSession::~SrsHikvisionTalkSession()
{
    stop();
    srs_freep(trd_);
    clear_downlink();
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
    device_ = NULL;
}

int SrsHikvisionTalkSession::audio_enc_type() const
{
    return audio_enc_type_;
}

int SrsHikvisionTalkSession::sample_rate_hz() const
{
    return sample_rate_hz_;
}

int SrsHikvisionTalkSession::client_pcm_rate_hz() const
{
    return client_pcm_rate_hz_;
}

int SrsHikvisionTalkSession::channel() const
{
    return channel_;
}

srs_utime_t SrsHikvisionTalkSession::last_active() const
{
    return last_active_;
}

void SrsHikvisionTalkSession::touch()
{
    last_active_ = srs_time_now_cached();
}

void SrsHikvisionTalkSession::add_listener(ISrsHikvisionTalkListener *l)
{
    if (!l) {
        return;
    }
    for (size_t i = 0; i < listeners_.size(); i++) {
        if (listeners_[i] == l) {
            return;
        }
    }
    listeners_.push_back(l);
    touch();
}

int SrsHikvisionTalkSession::remove_listener(ISrsHikvisionTalkListener *l)
{
    for (size_t i = 0; i < listeners_.size(); i++) {
        if (listeners_[i] == l) {
            listeners_.erase(listeners_.begin() + i);
            break;
        }
    }
    return (int)listeners_.size();
}

int SrsHikvisionTalkSession::listener_count()
{
    return (int)listeners_.size();
}

bool SrsHikvisionTalkSession::has_listener(ISrsHikvisionTalkListener *l) const
{
    if (!l) {
        return false;
    }
    for (size_t i = 0; i < listeners_.size(); i++) {
        if (listeners_[i] == l) {
            return true;
        }
    }
    return false;
}

// Map NET_DVR_COMPRESSION_AUDIO.byAudioSamplingRate → Hz.
// 0=default, 1=16k, 2=32k, 3=48k, 4=44.1k, 5=8k
static int srs_hik_audio_sampling_rate_hz(BYTE rate_code, int enc_type)
{
    switch (rate_code) {
    case 1:
        return 16000;
    case 2:
        return 32000;
    case 3:
        return 48000;
    case 4:
        return 44100;
    case 5:
        return 8000;
    default:
        break;
    }
    // G.711 is always 8 kHz in practice; G722 often 16 kHz.
    if (enc_type == AUDIOTALKTYPE_G711_MU || enc_type == AUDIOTALKTYPE_G711_A) {
        return 8000;
    }
    if (enc_type == AUDIOTALKTYPE_G722 || enc_type == AUDIOTALKTYPE_G722C) {
        return 16000;
    }
    return 8000;
}

static const char *srs_hik_audio_enc_name(int enc)
{
    switch (enc) {
    case 0:
        return "G722";
    case 1:
        return "G711_U";
    case 2:
        return "G711_A";
    case 5:
        return "MP2L2";
    case 6:
        return "G726";
    case 7:
        return "AAC";
    case 8:
        return "PCM";
    case 9:
        return "G722.1C";
    default:
        return "unknown";
    }
}

srs_error_t SrsHikvisionTalkSession::start()
{
    srs_error_t err = srs_success;
    if (voice_handle_ >= 0) {
        return err;
    }
    if ((err = device_->ensure_login()) != srs_success) {
        return srs_error_wrap(err, "login for talk");
    }

    // Map logical camera channel → VoiceCom channel like py-hikevent (NOT raw video ch).
    voice_chan_ = device_->voice_channel_for(channel_);
    srs_trace("Hikvision: talk map camera_ch=%d -> voice_chan=%d (startDTalkChan=%d)", channel_,
              voice_chan_, device_->start_dtalk_chan());

    if (pipe(wake_pipe_) < 0) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "talk wake pipe");
    }
    int flags = fcntl(wake_pipe_[1], F_GETFL, 0);
    fcntl(wake_pipe_[1], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(wake_pipe_[0], F_GETFL, 0);
    fcntl(wake_pipe_[0], F_SETFL, flags | O_NONBLOCK);
    if ((wake_fd_ = srs_netfd_open(wake_pipe_[0])) == NULL) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "talk open wake");
    }
    wake_pipe_[0] = -1;

    // MR = no local sound card. Match hikevent: StartVoiceCom first, then GetCurrentAudioCompress_V50.
    voice_handle_ = NET_DVR_StartVoiceCom_MR_V30(device_->user_id(), (DWORD)voice_chan_,
                                                 srs_hikvision_voice_cb, this);
    if (voice_handle_ < 0) {
        // Fallback: try raw channel (some devices / IPC without DTalk base).
        if (voice_chan_ != channel_ && channel_ > 0) {
            srs_warn("Hikvision: StartVoiceCom_MR voice_chan=%d failed %s, retry camera_ch=%d",
                     voice_chan_, srs_hikvision_sdk_errmsg().c_str(), channel_);
            voice_chan_ = channel_;
            voice_handle_ = NET_DVR_StartVoiceCom_MR_V30(device_->user_id(), (DWORD)voice_chan_,
                                                         srs_hikvision_voice_cb, this);
        }
    }
    if (voice_handle_ < 0 && voice_chan_ != 1) {
        srs_warn("Hikvision: StartVoiceCom_MR ch=%d failed %s, retry voice_chan=1",
                 voice_chan_, srs_hikvision_sdk_errmsg().c_str());
        voice_chan_ = 1;
        voice_handle_ = NET_DVR_StartVoiceCom_MR_V30(device_->user_id(), (DWORD)voice_chan_,
                                                     srs_hikvision_voice_cb, this);
    }
    if (voice_handle_ < 0) {
        return srs_error_new(ERROR_HIKVISION_SDK, "StartVoiceCom_MR_V30 camera_ch=%d voice_chan=%d %s",
                             channel_, voice_chan_, srs_hikvision_sdk_errmsg().c_str());
    }

    // Query compress on the *mapped* voice channel (hikevent order).
    NET_DVR_COMPRESSION_AUDIO ca;
    memset(&ca, 0, sizeof(ca));
    bool got_compress = false;
    const char *compress_src = "default";
    NET_DVR_AUDIO_CHANNEL ach;
    memset(&ach, 0, sizeof(ach));
    ach.dwChannelNum = (DWORD)voice_chan_;
    if (NET_DVR_GetCurrentAudioCompress_V50(device_->user_id(), &ach, &ca)) {
        got_compress = true;
        compress_src = "GetCurrentAudioCompress_V50";
    } else {
        srs_warn("Hikvision: GetCurrentAudioCompress_V50 voice_chan=%u failed %s, try global",
                 (unsigned)ach.dwChannelNum, srs_hikvision_sdk_errmsg().c_str());
        memset(&ca, 0, sizeof(ca));
        if (NET_DVR_GetCurrentAudioCompress(device_->user_id(), &ca)) {
            got_compress = true;
            compress_src = "GetCurrentAudioCompress";
        }
    }

    if (got_compress) {
        audio_enc_type_ = (int)ca.byAudioEncType;
        sample_rate_hz_ = srs_hik_audio_sampling_rate_hz(ca.byAudioSamplingRate, audio_enc_type_);
        srs_trace("Hikvision: talk compress src=%s enc=%d(%s) samplingCode=%u rate=%dHz bitRate=%u support=0x%02x voice_chan=%u",
                  compress_src, audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_),
                  (unsigned)ca.byAudioSamplingRate, sample_rate_hz_, (unsigned)ca.byAudioBitRate,
                  (unsigned)ca.bySupport, (unsigned)voice_chan_);
    } else {
        audio_enc_type_ = AUDIOTALKTYPE_G711_A;
        sample_rate_hz_ = 8000;
        srs_warn("Hikvision: talk compress unavailable, default G711_A@8k");
    }

    // G.711 is always 8 kHz.
    if (audio_enc_type_ == AUDIOTALKTYPE_G711_MU || audio_enc_type_ == AUDIOTALKTYPE_G711_A) {
        sample_rate_hz_ = 8000;
    }

    if (audio_enc_type_ != AUDIOTALKTYPE_G711_MU && audio_enc_type_ != AUDIOTALKTYPE_G711_A &&
        audio_enc_type_ != AUDIOTALKTYPE_PCM) {
        NET_DVR_StopVoiceCom(voice_handle_);
        voice_handle_ = -1;
        return srs_error_new(ERROR_HIKVISION_SDK,
                             "talk codec type=%d(%s) unsupported (need G711_U=1, G711_A=2, or PCM=8)",
                             audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_));
    }

    // Init G.711 encoder like hikevent sendVoice (do NOT force in_frame_size before Init).
    if (audio_enc_type_ == AUDIOTALKTYPE_G711_MU || audio_enc_type_ == AUDIOTALKTYPE_G711_A) {
        NET_DVR_AUDIOENC_INFO enc_info;
        memset(&enc_info, 0, sizeof(enc_info));
        g711_enc_ = NET_DVR_InitG711Encoder(&enc_info);
        // hikevent treats -1 as failure; also treat NULL as failure.
        if (!g711_enc_ || (long)g711_enc_ == -1) {
            g711_enc_ = NULL;
            srs_warn("Hikvision: InitG711Encoder failed %s, software G.711 fallback",
                     srs_hikvision_sdk_errmsg().c_str());
            // hikevent G711 path: out=160 → PCM in=320 bytes (160 samples).
            g711_in_frame_bytes_ = 320;
        } else {
            // SDK fills in_frame_size; hikevent then halves it for G711_U only.
            int in_sz = (int)enc_info.in_frame_size;
            if (in_sz <= 0) {
                in_sz = 640;
            }
            if (audio_enc_type_ == AUDIOTALKTYPE_G711_MU) {
                in_sz /= 2; // exact match hikevent.cpp after InitG711Encoder
            }
            if (in_sz < 2) {
                in_sz = 320;
            }
            g711_in_frame_bytes_ = in_sz;
            srs_trace("Hikvision: InitG711Encoder ok in_frame=%d enc=%d(%s)", g711_in_frame_bytes_,
                      audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_));
        }
    }

    stopping_ = false;
    if ((err = trd_->start()) != srs_success) {
        if (g711_enc_) {
            NET_DVR_ReleaseG711Encoder(g711_enc_);
            g711_enc_ = NULL;
        }
        NET_DVR_StopVoiceCom(voice_handle_);
        voice_handle_ = -1;
        return srs_error_wrap(err, "start talk coroutine");
    }

    client_pcm_rate_hz_ = sample_rate_hz_ > 0 ? sample_rate_hz_ : 8000;
    pcm_uplink_.clear();

    srs_trace("Hikvision: talk started serial=%s camera_ch=%d voice_chan=%d handle=%ld codec=%d(%s) rate=%d pcm_frame=%d",
              device_->conf().serialno_.c_str(), channel_, voice_chan_, (long)voice_handle_,
              audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_), sample_rate_hz_,
              g711_in_frame_bytes_);
    touch();
    return err;
}

void SrsHikvisionTalkSession::stop()
{
    stopping_ = true;
    if (voice_handle_ >= 0) {
        NET_DVR_StopVoiceCom(voice_handle_);
        srs_trace("Hikvision: StopVoiceCom handle=%ld ch=%d", (long)voice_handle_, channel_);
        voice_handle_ = -1;
    }
    if (g711_enc_) {
        NET_DVR_ReleaseG711Encoder(g711_enc_);
        g711_enc_ = NULL;
    }
    if (trd_) {
        trd_->stop();
    }
    clear_downlink();
    pcm_uplink_.clear();
}

// Prefer large PCM flush for type=8 (matches common sendVoice 4096-byte chunks).
static const int SRS_HIK_TALK_PCM_SEND_BYTES = 4096;

srs_error_t SrsHikvisionTalkSession::flush_uplink_encoded()
{
    srs_error_t err = srs_success;
    if (voice_handle_ < 0) {
        return srs_error_new(ERROR_HIKVISION_SDK, "talk not started");
    }

    // PCM device: send raw S16LE in 4096-byte chunks (user's previous sendVoice pattern).
    if (audio_enc_type_ == AUDIOTALKTYPE_PCM) {
        while ((int)pcm_uplink_.size() >= SRS_HIK_TALK_PCM_SEND_BYTES) {
            if (!NET_DVR_VoiceComSendData(voice_handle_, (char *)pcm_uplink_.data(),
                                          (DWORD)SRS_HIK_TALK_PCM_SEND_BYTES)) {
                return srs_error_new(ERROR_HIKVISION_SDK, "VoiceComSendData pcm len=%d %s",
                                     SRS_HIK_TALK_PCM_SEND_BYTES, srs_hikvision_sdk_errmsg().c_str());
            }
            pcm_uplink_.erase(0, (size_t)SRS_HIK_TALK_PCM_SEND_BYTES);
            static int ul_pcm = 0;
            static srs_utime_t ul_pcm_last = 0;
            ul_pcm++;
            srs_utime_t now = srs_time_now_cached();
            if (ul_pcm <= 3 || (now - ul_pcm_last) >= 1 * SRS_UTIME_SECONDS) {
                ul_pcm_last = now;
                srs_trace("Hikvision: talk uplink PCM n=%d last_len=%d handle=%ld ch=%d", ul_pcm,
                          SRS_HIK_TALK_PCM_SEND_BYTES, (long)voice_handle_, channel_);
            }
        }
        return err;
    }

    // G.711 U/A — match hikevent sendVoice:
    //   g711_type 0=μ (enc=1), 1=A (enc=2)
    //   EncodeG711Frame then force out_frame_size = 160
    //   VoiceComSendData(..., 160)
    if (audio_enc_type_ != AUDIOTALKTYPE_G711_MU && audio_enc_type_ != AUDIOTALKTYPE_G711_A) {
        return srs_error_new(ERROR_HIKVISION_SDK, "talk encode unsupported codec=%d(%s)",
                             audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_));
    }
    const int g711_type = (audio_enc_type_ == AUDIOTALKTYPE_G711_A) ? 1 : 0; // 0=μ 1=A
    const bool alaw = (audio_enc_type_ == AUDIOTALKTYPE_G711_A);
    const int pcm_need = g711_in_frame_bytes_ > 0 ? g711_in_frame_bytes_ : 320;
    if (pcm_need < 2 || (pcm_need % 2) != 0 || pcm_need > 8192) {
        return srs_error_new(ERROR_HIKVISION_SDK, "invalid g711 pcm frame size %d", pcm_need);
    }
    const int samples = pcm_need / 2;
    // hikevent always sends 160-byte G711 frames.
    const int g711_out_len = 160;
    std::vector<BYTE> pcm_in((size_t)pcm_need);
    std::vector<BYTE> g711_out((size_t)srs_max(samples, g711_out_len) + 64);

    // At most a few frames per uplink callback to avoid bursting (hikevent sleeps 20ms/frame).
    int sent = 0;
    const int max_per_call = 4;
    while ((int)pcm_uplink_.size() >= pcm_need && sent < max_per_call) {
        memcpy(pcm_in.data(), pcm_uplink_.data(), (size_t)pcm_need);
        int out_len = g711_out_len;
        const char *enc_how = "soft";
        BOOL ok = FALSE;
        if (g711_enc_) {
            NET_DVR_AUDIOENC_PROCESS_PARAM ep;
            memset(&ep, 0, sizeof(ep));
            ep.in_buf = pcm_in.data();
            ep.out_buf = g711_out.data();
            ep.out_frame_size = 0;
            ep.g711_type = g711_type;
            ok = NET_DVR_EncodeG711Frame(g711_enc_, &ep);
            if (ok) {
                enc_how = "sdk";
                // hikevent hardcodes 160 after EncodeG711Frame for type 1/2.
                out_len = g711_out_len;
                if (ep.out_frame_size > 0 && (int)ep.out_frame_size < out_len) {
                    out_len = (int)ep.out_frame_size;
                }
            }
        }
        if (!ok) {
            // Software: encode exactly g711_out_len samples if we have enough PCM.
            int soft_samples = srs_min(samples, g711_out_len);
            if (soft_samples < g711_out_len && samples >= g711_out_len) {
                soft_samples = g711_out_len;
            }
            // Prefer 160 samples → 160 bytes (matches hikevent VoiceComSendData size).
            soft_samples = g711_out_len;
            if (pcm_need < soft_samples * 2) {
                // Not enough PCM in this frame size config; encode what we have.
                soft_samples = samples;
            }
            srs_hik_encode_pcm_to_g711((const int16_t *)pcm_in.data(), soft_samples, alaw, g711_out.data());
            out_len = soft_samples;
            enc_how = "soft";
            static int soft_once = 0;
            if (soft_once < 3) {
                soft_once++;
                srs_warn("Hikvision: EncodeG711Frame sdk miss, software (%s) samples=%d",
                         alaw ? "A-law" : "μ-law", soft_samples);
            }
        }
        if (!NET_DVR_VoiceComSendData(voice_handle_, (char *)g711_out.data(), (DWORD)out_len)) {
            return srs_error_new(ERROR_HIKVISION_SDK,
                                 "VoiceComSendData g711 len=%d enc=%d(%s) g711_type=%d voice_chan=%d %s",
                                 out_len, audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_),
                                 g711_type, voice_chan_, srs_hikvision_sdk_errmsg().c_str());
        }
        pcm_uplink_.erase(0, (size_t)pcm_need);
        sent++;
        static int ul_g711 = 0;
        static srs_utime_t ul_g711_last = 0;
        ul_g711++;
        srs_utime_t now = srs_time_now_cached();
        if (ul_g711 <= 3 || (now - ul_g711_last) >= 1 * SRS_UTIME_SECONDS) {
            ul_g711_last = now;
            srs_trace("Hikvision: talk uplink G711 n=%d last_len=%d enc=%d(%s) g711_type=%d via=%s "
                      "handle=%ld camera_ch=%d voice_chan=%d",
                      ul_g711, out_len, audio_enc_type_, srs_hik_audio_enc_name(audio_enc_type_),
                      g711_type, enc_how, (long)voice_handle_, channel_, voice_chan_);
        }
    }
    return err;
}

srs_error_t SrsHikvisionTalkSession::send_uplink(const char *data, int len)
{
    if (voice_handle_ < 0 || !data || len <= 0) {
        return srs_error_new(ERROR_HIKVISION_SDK, "talk not started or empty frame");
    }
    // Browser always sends raw PCM S16LE mono. Odd trailing byte is dropped.
    int usable = len & ~1;
    if (usable <= 0) {
        return srs_success;
    }
    touch();

    const int16_t *src = (const int16_t *)data;
    int src_n = usable / (int)sizeof(int16_t);
    int src_rate = client_pcm_rate_hz_ > 0 ? client_pcm_rate_hz_ : 8000;
    int dst_rate = sample_rate_hz_ > 0 ? sample_rate_hz_ : 8000;

    vector<int16_t> resampled;
    if (src_rate != dst_rate) {
        srs_hik_resample_s16(src, src_n, src_rate, dst_rate, resampled);
        if (!resampled.empty()) {
            pcm_uplink_.append((const char *)resampled.data(), resampled.size() * sizeof(int16_t));
        }
    } else {
        pcm_uplink_.append(data, (size_t)usable);
    }

    // Cap buffer (~1s @ 8k S16) to avoid unbounded growth if encode stalls.
    const size_t max_pcm = (size_t)dst_rate * sizeof(int16_t);
    if (pcm_uplink_.size() > max_pcm * 2) {
        pcm_uplink_.erase(0, pcm_uplink_.size() - max_pcm);
        srs_warn("Hikvision: talk pcm uplink overflow, drop old ch=%d", channel_);
    }

    return flush_uplink_encoded();
}

std::string SrsHikvisionTalkSession::decode_downlink_to_pcm(const char *data, int size)
{
    string out;
    if (!data || size <= 0) {
        return out;
    }
    // Device already PCM: pass through.
    if (audio_enc_type_ == 8) {
        out.assign(data, (size_t)size);
        return out;
    }
    if (audio_enc_type_ != 1 && audio_enc_type_ != 2) {
        // Unknown: still try pass-through so client can ignore.
        out.assign(data, (size_t)size);
        return out;
    }
    const bool alaw = (audio_enc_type_ == 2);
    vector<int16_t> pcm((size_t)size);
    srs_hik_decode_g711_to_pcm((const uint8_t *)data, size, alaw, pcm.data());
    out.assign((const char *)pcm.data(), pcm.size() * sizeof(int16_t));
    return out;
}

void SrsHikvisionTalkSession::on_voice_data(const char * /*data*/, int /*size*/, int /*audio_flag*/)
{
    // Talk is uplink-only: browser mic → VoiceCom → camera speaker.
    // Local listen uses the existing RealPlay/WebRTC preview audio — do not
    // push a second VoiceCom downlink stream to the client (causes noise/duplex).
    return;
}

void SrsHikvisionTalkSession::clear_downlink()
{
    pthread_mutex_lock(&lock_);
    for (size_t i = 0; i < downlink_.size(); i++) {
        srs_freep(downlink_[i]);
    }
    downlink_.clear();
    pthread_mutex_unlock(&lock_);
}

srs_error_t SrsHikvisionTalkSession::cycle()
{
    srs_error_t err = do_cycle();
    stopping_ = true;
    if (voice_handle_ >= 0) {
        NET_DVR_StopVoiceCom(voice_handle_);
        voice_handle_ = -1;
    }
    if (g711_enc_) {
        NET_DVR_ReleaseG711Encoder(g711_enc_);
        g711_enc_ = NULL;
    }
    clear_downlink();
    pcm_uplink_.clear();
    return err;
}

srs_error_t SrsHikvisionTalkSession::do_cycle()
{
    srs_error_t err = srs_success;
    string key = device_->conf().serialno_ + "_" + srs_fmt_sprintf("%d", channel_);

    while (!stopping_) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "talk pull");
        }
        char buf[64];
        srs_read(wake_fd_, buf, sizeof(buf), 200 * SRS_UTIME_MILLISECONDS);

        vector<string *> local;
        pthread_mutex_lock(&lock_);
        local.swap(downlink_);
        pthread_mutex_unlock(&lock_);

        for (size_t i = 0; i < local.size(); i++) {
            string *pkt = local[i];
            static int dl_log = 0;
            if (dl_log < 8) {
                dl_log++;
                srs_trace("Hikvision: talk downlink PCM len=%d listeners=%d ch=%d dev_codec=%d rate=%d",
                          (int)pkt->size(), (int)listeners_.size(), channel_, audio_enc_type_, sample_rate_hz_);
            }
            for (size_t j = 0; j < listeners_.size(); j++) {
                if (listeners_[j]) {
                    // Always PCM S16LE for browser (device G.711 already decoded).
                    listeners_[j]->on_talk_downlink(key, pkt->data(), (int)pkt->size());
                }
            }
            srs_freep(pkt);
        }
        if (!local.empty()) {
            touch();
        }
    }
    return err;
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

    for (map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.begin(); it != talk_sessions_.end(); ++it) {
        srs_freep(it->second);
    }
    talk_sessions_.clear();

    for (map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.begin(); it != playback_cache_.end();
         ++it) {
        srs_freep(it->second);
    }
    playback_cache_.clear();

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

    char cwd_buf[PATH_MAX];
    const char *cwd = (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) ? cwd_buf : "(unknown)";

    string sdk_path_conf = config_->get_hikvision_sdk_path();
    string sdk_path = srs_hikvision_resolve_sdk_path(sdk_path_conf);
    if (!sdk_path.empty()) {
        SrsPath path;
        string lib_core = sdk_path + "/libhcnetsdk.so";
        string lib_preview = sdk_path + "/HCNetSDKCom/libHCPreview.so";
        string com_dir = sdk_path + "/HCNetSDKCom";
        if (!path.exists(sdk_path)) {
            srs_warn("Hikvision: sdk_path missing conf=%s resolved=%s cwd=%s "
                     "(relative path is cwd-dependent under systemd; use absolute sdk_path or work_dir)",
                     sdk_path_conf.c_str(), sdk_path.c_str(), cwd);
        } else if (!path.exists(lib_core) || !path.exists(lib_preview)) {
            srs_warn("Hikvision: sdk_path incomplete resolved=%s need libhcnetsdk.so + HCNetSDKCom/libHCPreview.so "
                     "core_ok=%d preview_ok=%d cwd=%s",
                     sdk_path.c_str(), path.exists(lib_core) ? 1 : 0, path.exists(lib_preview) ? 1 : 0, cwd);
        } else {
            srs_trace("Hikvision: sdk_path conf=%s resolved=%s cwd=%s",
                      sdk_path_conf.c_str(), sdk_path.c_str(), cwd);
        }

        // Also set env for child tools; does not fix this process's dlopen alone.
        string ld_extra = sdk_path + ":" + com_dir;
        const char *old_ld = getenv("LD_LIBRARY_PATH");
        string new_ld = old_ld && old_ld[0] ? (ld_extra + ":" + old_ld) : ld_extra;
        setenv("LD_LIBRARY_PATH", new_ld.c_str(), 1);

        // Absolute dlopen so HCPreview/deps resolve without relying on cwd/rpath.
        // Fail hard: missing HCPreview here is the usual cause of RealPlay err=136.
        if ((err = srs_hikvision_preload_sdk_libs(sdk_path)) != srs_success) {
            return srs_error_wrap(err, "preload HCNetSDK from %s", sdk_path.c_str());
        }

        // Must be before NET_DVR_Init: directory that contains libhcnetsdk.so + HCNetSDKCom/.
        NET_DVR_LOCAL_SDK_PATH struComPath;
        memset(&struComPath, 0, sizeof(struComPath));
        strncpy(struComPath.sPath, sdk_path.c_str(), sizeof(struComPath.sPath) - 1);
        if (!NET_DVR_SetSDKInitCfg(NET_SDK_INIT_CFG_SDK_PATH, (void *)&struComPath)) {
            srs_warn("Hikvision: SetSDKInitCfg SDK_PATH=%s failed %s", sdk_path.c_str(),
                     srs_hikvision_sdk_errmsg().c_str());
        }

        // Prefer SDK-bundled OpenSSL so component load does not pick host libs.
        string crypto = sdk_path + "/libcrypto.so";
        string ssl = sdk_path + "/libssl.so";
        if (path.exists(crypto)) {
            if (!NET_DVR_SetSDKInitCfg(NET_SDK_INIT_CFG_LIBEAY_PATH, (void *)crypto.c_str())) {
                srs_warn("Hikvision: SetSDKInitCfg LIBEAY_PATH=%s failed %s", crypto.c_str(),
                         srs_hikvision_sdk_errmsg().c_str());
            }
        }
        if (path.exists(ssl)) {
            if (!NET_DVR_SetSDKInitCfg(NET_SDK_INIT_CFG_SSLEAY_PATH, (void *)ssl.c_str())) {
                srs_warn("Hikvision: SetSDKInitCfg SSLEAY_PATH=%s failed %s", ssl.c_str(),
                         srs_hikvision_sdk_errmsg().c_str());
            }
        }
    } else {
        return srs_error_new(ERROR_HIKVISION_SDK,
                             "hikvision.sdk_path empty (cwd=%s); set absolute path e.g. "
                             "sdk_path /opt/hik-lib/lib-amd64;",
                             cwd);
    }

    if (!NET_DVR_Init()) {
        return srs_error_new(ERROR_HIKVISION_SDK, "NET_DVR_Init failed %s",
                             srs_hikvision_sdk_errmsg().c_str());
    }

    NET_DVR_SetConnectTime(3000, 3);
    NET_DVR_SetReconnect(10000, TRUE);
    sdk_inited_ = true;
    // Marker line to confirm this build has absolute preload (deploy check).
    srs_trace("Hikvision: NET_DVR_Init ok, sdk=0x%x build=0x%x preload=1 sdk_path=%s cwd=%s",
              (unsigned)NET_DVR_GetSDKVersion(), (unsigned)NET_DVR_GetSDKBuildVersion(),
              sdk_path.c_str(), cwd);
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
    int64_t start_unix_ts = 0;
    if (!srs_hikvision_parse_stream(stream_name, serialno, channel, subchannel, &start_unix_ts)) {
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

    SrsHikvisionStream *stream = new SrsHikvisionStream(device, stream_name, channel, subchannel, start_unix_ts);
    if ((err = stream->start(config_->get_hikvision_output())) != srs_success) {
        srs_freep(stream);
        return srs_error_wrap(err, "start stream %s", stream_name.c_str());
    }
    stream->add_ref();
    streams_[stream_name] = stream;
    if (start_unix_ts > 0) {
        srs_trace("Hikvision: start playback stream=%s serial=%s ch=%d start_ts=%lld",
                  stream_name.c_str(), serialno.c_str(), channel, (long long)start_unix_ts);
    } else {
        srs_trace("Hikvision: start live stream=%s serial=%s ch=%d sub=%d",
                  stream_name.c_str(), serialno.c_str(), channel, subchannel);
    }
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

srs_error_t SrsHikvisionManager::handle_control_json(const string &json, const string &stream_context,
                                                     ISrsHikvisionTalkListener *talk_listener, string *out_reply,
                                                     string *out_binary)
{
    SrsJsonAny *any = SrsJsonAny::loads(json);
    if (!any || !any->is_object()) {
        srs_freep(any);
        return srs_error_new(ERROR_HIKVISION_CONFIG, "invalid control json");
    }
    SrsUniquePtr<SrsJsonObject> req(any->to_object());
    return handle_control(req.get(), stream_context, talk_listener, out_reply, out_binary);
}

srs_error_t SrsHikvisionManager::search_records(SrsHikvisionDevice *device, int channel, int64_t start_unix,
                                                int64_t end_unix, int file_type, int stream_type, int max_results,
                                                string *out_json)
{
    srs_error_t err = srs_success;
    if (!device || !out_json) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "search_records invalid args");
    }
    if ((err = device->ensure_login()) != srs_success) {
        return srs_error_wrap(err, "login for search");
    }
    if (channel <= 0) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "invalid channel %d", channel);
    }
    if (end_unix <= start_unix) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "end must be > start");
    }
    // Cap range to 7 days to avoid long FindFile blocks.
    if (end_unix - start_unix > 7 * 24 * 3600) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "search range too large (max 7 days)");
    }
    max_results = srs_max(1, srs_min(1000, max_results));
    if (file_type < 0) {
        file_type = 0xff;
    }
    if (stream_type < 0) {
        stream_type = 0; // auto prefer main
    }

    NET_DVR_FILECOND_V40 cond;
    memset(&cond, 0, sizeof(cond));
    cond.lChannel = channel;
    cond.dwFileType = (DWORD)file_type;
    cond.dwIsLocked = 0xff;
    cond.dwUseCardNo = 0;
    srs_hikvision_unix_to_dvr_time((time_t)start_unix, &cond.struStartTime);
    srs_hikvision_unix_to_dvr_time((time_t)end_unix, &cond.struStopTime);
    cond.byDrawFrame = 0;
    cond.byFindType = 0;
    cond.byQuickSearch = 0;
    cond.byStreamType = (BYTE)stream_type;

    LONG find_h = NET_DVR_FindFile_V40(device->user_id(), &cond);
    if (find_h < 0) {
        return srs_error_new(ERROR_HIKVISION_SDK, "FindFile_V40 ch=%d %s", channel,
                             srs_hikvision_sdk_errmsg().c_str());
    }

    SrsJsonArray *arr = SrsJsonAny::array();
    int count = 0;
    int finding_spins = 0;
    const int kMaxFindingSpins = 2000; // ~20s if 10ms each

    while (count < max_results) {
        NET_DVR_FINDDATA_V40 fd;
        memset(&fd, 0, sizeof(fd));
        LONG st = NET_DVR_FindNextFile_V40(find_h, &fd);
        if (st == NET_DVR_FILE_SUCCESS) {
            finding_spins = 0;
            int64_t t0 = srs_hikvision_dvr_fields_to_unix(fd.struStartTime.dwYear, fd.struStartTime.dwMonth,
                                                          fd.struStartTime.dwDay, fd.struStartTime.dwHour,
                                                          fd.struStartTime.dwMinute, fd.struStartTime.dwSecond);
            int64_t t1 = srs_hikvision_dvr_fields_to_unix(fd.struStopTime.dwYear, fd.struStopTime.dwMonth,
                                                          fd.struStopTime.dwDay, fd.struStopTime.dwHour,
                                                          fd.struStopTime.dwMinute, fd.struStopTime.dwSecond);
            SrsJsonObject *item = SrsJsonAny::object();
            item->set("fileName", SrsJsonAny::str(fd.sFileName));
            item->set("start", SrsJsonAny::integer(t0));
            item->set("end", SrsJsonAny::integer(t1));
            item->set("size", SrsJsonAny::integer((int64_t)fd.dwFileSize));
            item->set("fileType", SrsJsonAny::integer((int)fd.byFileType));
            item->set("streamType", SrsJsonAny::integer((int)fd.byStreamType));
            item->set("locked", SrsJsonAny::integer((int)fd.byLocked));
            item->set("fileIndex", SrsJsonAny::integer((int64_t)fd.dwFileIndex));
            // Convenience for VOD play: SerialNO_CHANNEL_UNIXTS
            string vod = device->conf().serialno_ + "_" + srs_fmt_sprintf("%d", channel) + "_" +
                         srs_fmt_sprintf("%lld", (long long)t0);
            item->set("vodStream", SrsJsonAny::str(vod.c_str()));
            arr->add(item);
            count++;
            continue;
        }
        if (st == NET_DVR_ISFINDING) {
            finding_spins++;
            if (finding_spins > kMaxFindingSpins) {
                NET_DVR_FindClose_V30(find_h);
                srs_freep(arr);
                return srs_error_new(ERROR_HIKVISION_SDK, "FindFile timeout ch=%d", channel);
            }
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
            continue;
        }
        if (st == NET_DVR_NOMOREFILE || st == NET_DVR_FILE_NOFIND) {
            break;
        }
        // Exception / other
        DWORD e = NET_DVR_GetLastError();
        NET_DVR_FindClose_V30(find_h);
        srs_freep(arr);
        return srs_error_new(ERROR_HIKVISION_SDK, "FindNextFile_V40 st=%ld ch=%d %s",
                             (long)st, channel, srs_hikvision_sdk_errmsg(e).c_str());
    }

    NET_DVR_FindClose_V30(find_h);

    SrsUniquePtr<SrsJsonObject> root(SrsJsonAny::object());
    root->set("code", SrsJsonAny::integer(0));
    root->set("msg", SrsJsonAny::str("ok"));
    root->set("cmd", SrsJsonAny::str("search_record"));
    // Align with existing external protocol naming (Search*Result).
    root->set("id", SrsJsonAny::str("HIK::SearchRecordResult"));
    root->set("channel", SrsJsonAny::integer(channel));
    root->set("serialno", SrsJsonAny::str(device->conf().serialno_.c_str()));
    root->set("start", SrsJsonAny::integer(start_unix));
    root->set("end", SrsJsonAny::integer(end_unix));
    root->set("count", SrsJsonAny::integer(count));
    root->set("result", arr); // root owns arr

    *out_json = root->dumps();
    srs_trace("Hikvision: search_record ok serial=%s ch=%d count=%d range=%lld..%lld",
              device->conf().serialno_.c_str(), channel, count, (long long)start_unix, (long long)end_unix);
    return err;
}

// Worker: HCNetSDK PlayBack + ffmpeg off ST thread.
// Prefer a *separate* NET_DVR login so live RealPlay is not blocked/deadlocked.
// If NVR rejects 2nd login (common for viewer accounts), fall back to shared user_id.
// Output: MPEG-TS H.264+AAC for mpegts.js.
struct SrsHikvisionDlWorkerCtx {
    string host;
    int port;
    string user;
    string password;
    LONG shared_user_id; // live session; used only if own-login fails
    int channel;
    int64_t start_unix;
    int64_t end_unix;
    string raw_path;
    string ts_path;
    string ts_out;
    int64_t raw_sz;
    int64_t progress_bytes; // ST may read for logs
    bool play_done;
    int err_code; // 0=ok
    string err_msg;
    volatile int finished; // 0 running, 1 done
};

// At most one download at a time (NVR/ffmpeg load).
static volatile int g_hik_dl_inflight = 0;

static bool srs_hikvision_looks_like_mpegts(const string &data)
{
    if (data.size() < 188) {
        return false;
    }
    const unsigned char *p = (const unsigned char *)data.data();
    size_t n = data.size();
    if (p[0] == 0x47) {
        return true;
    }
    size_t limit = srs_min(n, (size_t)1024);
    for (size_t i = 0; i + 188 <= limit; i++) {
        if (p[i] == 0x47 && (i + 188 >= n || p[i + 188] == 0x47)) {
            return true;
        }
    }
    return false;
}

static int64_t srs_hikvision_file_size(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    fseek(fp, 0, SEEK_END);
    int64_t sz = (int64_t)ftell(fp);
    fclose(fp);
    return sz > 0 ? sz : 0;
}

static void srs_hikvision_dl_mark_done(SrsHikvisionDlWorkerCtx *w)
{
    __sync_synchronize();
    w->finished = 1;
    __sync_synchronize();
}

static int srs_hikvision_system_status(int st)
{
    if (st == -1) {
        return -1;
    }
    if (WIFEXITED(st)) {
        return WEXITSTATUS(st);
    }
    return st;
}

static void srs_hikvision_read_fferr(const string &log_path, string &err_tail)
{
    err_tail.clear();
    FILE *lf = fopen(log_path.c_str(), "rb");
    if (!lf) {
        return;
    }
    fseek(lf, 0, SEEK_END);
    long sz = ftell(lf);
    if (sz > 0) {
        long off = sz > 400 ? sz - 400 : 0;
        fseek(lf, off, SEEK_SET);
        char buf[512];
        size_t n = fread(buf, 1, sizeof(buf) - 1, lf);
        buf[n] = 0;
        err_tail = buf;
        for (size_t i = 0; i < err_tail.size(); i++) {
            char c = err_tail[i];
            if (c == '\n' || c == '\r' || c == '"' || c == '\\') {
                err_tail[i] = ' ';
            }
        }
    }
    fclose(lf);
    ::unlink(log_path.c_str());
}

// Run ffmpeg → MPEG-TS. Try autodect then -f mpeg (IMKH/PS). No -map ?: (old ffmpeg).
static int srs_hikvision_run_ffmpeg_mpegts(const string &raw_path, const string &ts_path, bool audio,
                                           string &err_tail)
{
    string log_path = ts_path + ".fferr";
    const char *timeout_bin = NULL;
    if (::access("/usr/bin/timeout", X_OK) == 0) {
        timeout_bin = "/usr/bin/timeout 90 ";
    } else if (::access("/bin/timeout", X_OK) == 0) {
        timeout_bin = "/bin/timeout 90 ";
    } else {
        timeout_bin = "";
    }

    // demux_hint: "" auto, or "-f mpeg "
    const char *hints[] = {"", "-f mpeg "};
    for (int hi = 0; hi < 2; hi++) {
        string cmd;
        if (audio) {
            cmd = srs_fmt_sprintf(
                "%sffmpeg -y -hide_banner -loglevel error %s-i '%s' "
                "-c:v copy -c:a aac -b:a 64k -ar 16000 -ac 1 -shortest -f mpegts '%s' 2>'%s'",
                timeout_bin, hints[hi], raw_path.c_str(), ts_path.c_str(), log_path.c_str());
        } else {
            cmd = srs_fmt_sprintf(
                "%sffmpeg -y -hide_banner -loglevel error %s-i '%s' "
                "-an -c:v copy -f mpegts '%s' 2>'%s'",
                timeout_bin, hints[hi], raw_path.c_str(), ts_path.c_str(), log_path.c_str());
        }
        int st = srs_hikvision_system_status(::system(cmd.c_str()));
        srs_hikvision_read_fferr(log_path, err_tail);
        if (st == 0 && srs_hikvision_file_size(ts_path.c_str()) >= 188) {
            return 0;
        }
        ::unlink(ts_path.c_str());
    }
    return err_tail.empty() ? -1 : 1;
}

static void *srs_hikvision_dl_worker(void *arg)
{
    SrsHikvisionDlWorkerCtx *w = (SrsHikvisionDlWorkerCtx *)arg;
    w->err_code = 0;
    w->raw_sz = 0;
    w->progress_bytes = 0;
    w->play_done = false;
    w->ts_out.clear();

    LONG uid = -1;
    bool own_login = false;

    // 1) Prefer dedicated login (isolates from live RealPlay).
    NET_DVR_USER_LOGIN_INFO login_info;
    NET_DVR_DEVICEINFO_V40 device_info;
    memset(&login_info, 0, sizeof(login_info));
    memset(&device_info, 0, sizeof(device_info));
    strncpy(login_info.sDeviceAddress, w->host.c_str(), sizeof(login_info.sDeviceAddress) - 1);
    login_info.wPort = (WORD)w->port;
    strncpy(login_info.sUserName, w->user.c_str(), sizeof(login_info.sUserName) - 1);
    strncpy(login_info.sPassword, w->password.c_str(), sizeof(login_info.sPassword) - 1);
    login_info.bUseAsynLogin = FALSE;

    uid = NET_DVR_Login_V40(&login_info, &device_info);
    if (uid >= 0) {
        own_login = true;
    } else {
        // 2) Viewer often allows only 1 session — reuse live login.
        if (w->shared_user_id >= 0) {
            uid = w->shared_user_id;
            own_login = false;
        } else {
            w->err_code = ERROR_HIKVISION_SDK;
            w->err_msg = string("download login ") + srs_hikvision_sdk_errmsg() + " (no shared session)";
            srs_hikvision_dl_mark_done(w);
            return NULL;
        }
    }

    NET_DVR_VOD_PARA vod;
    memset(&vod, 0, sizeof(vod));
    vod.dwSize = sizeof(vod);
    vod.struIDInfo.dwSize = sizeof(vod.struIDInfo);
    vod.struIDInfo.dwChannel = (DWORD)w->channel;
    vod.byStreamType = 0;
    vod.hWnd = 0;
    srs_hikvision_unix_to_dvr_time((time_t)w->start_unix, &vod.struBeginTime);
    srs_hikvision_unix_to_dvr_time((time_t)w->end_unix, &vod.struEndTime);

    LONG pb = NET_DVR_PlayBackByTime_V40(uid, &vod);
    if (pb < 0) {
        w->err_code = ERROR_HIKVISION_SDK;
        w->err_msg = string("PlayBackByTime_V40 ") + srs_hikvision_sdk_errmsg() +
                     (own_login ? " (own-login)" : " (shared-login)");
        if (own_login) {
            NET_DVR_Logout(uid);
        }
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }

    if (!NET_DVR_PlayBackSaveData(pb, (char *)w->raw_path.c_str())) {
        NET_DVR_StopPlayBack(pb);
        if (own_login) {
            NET_DVR_Logout(uid);
        }
        w->err_code = ERROR_HIKVISION_SDK;
        w->err_msg = string("PlayBackSaveData ") + srs_hikvision_sdk_errmsg();
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }
    if (!NET_DVR_PlayBackControl_V40(pb, NET_DVR_PLAYSTART, NULL, 0, NULL, NULL)) {
        NET_DVR_StopPlayBackSave(pb);
        NET_DVR_StopPlayBack(pb);
        if (own_login) {
            NET_DVR_Logout(uid);
        }
        w->err_code = ERROR_HIKVISION_SDK;
        w->err_msg = string("PLAYSTART ") + srs_hikvision_sdk_errmsg();
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }
    for (int i = 0; i < 4; i++) {
        NET_DVR_PlayBackControl_V40(pb, NET_DVR_PLAYFAST, NULL, 0, NULL, NULL);
    }

    // OS thread only — never srs_usleep here.
    int64_t last_sz = -1;
    int stable = 0;
    time_t t0 = time(NULL);
    const int kTimeoutSec = 90;
    while ((int)(time(NULL) - t0) < kTimeoutSec) {
        int pos = 0;
        DWORD npos = sizeof(pos);
        if (NET_DVR_PlayBackControl_V40(pb, NET_DVR_PLAYGETPOS, NULL, 0, &pos, &npos)) {
            if (pos == 100) {
                w->play_done = true;
                break;
            }
            if (pos > 100) {
                NET_DVR_StopPlayBackSave(pb);
                NET_DVR_StopPlayBack(pb);
                if (own_login) {
                    NET_DVR_Logout(uid);
                }
                ::unlink(w->raw_path.c_str());
                w->err_code = ERROR_HIKVISION_SDK;
                w->err_msg = srs_fmt_sprintf("download PLAYGETPOS=%d %s", pos, srs_hikvision_sdk_errmsg().c_str());
                srs_hikvision_dl_mark_done(w);
                return NULL;
            }
        }
        int64_t sz = srs_hikvision_file_size(w->raw_path.c_str());
        w->progress_bytes = sz;
        if (sz > 0 && sz == last_sz) {
            stable++;
            if (stable >= 15 && sz > 1024) {
                w->play_done = true;
                break;
            }
            if (stable >= 12 && pos >= 90) {
                w->play_done = true;
                break;
            }
        } else {
            stable = 0;
            last_sz = sz;
        }
        ::usleep(100 * 1000);
    }

    NET_DVR_StopPlayBackSave(pb);
    NET_DVR_StopPlayBack(pb);
    if (own_login) {
        NET_DVR_Logout(uid);
    }

    w->raw_sz = srs_hikvision_file_size(w->raw_path.c_str());
    w->progress_bytes = w->raw_sz;
    if (w->raw_sz < 1024) {
        ::unlink(w->raw_path.c_str());
        w->err_code = ERROR_HIKVISION_SDK;
        w->err_msg = srs_fmt_sprintf("SDK download empty/small size=%lld done=%d", (long long)w->raw_sz,
                                     w->play_done ? 1 : 0);
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }

    // AAC for mpegts.js; fallback video-only. Use -f mpeg for IMKH/PS.
    string ff_err;
    int st = srs_hikvision_run_ffmpeg_mpegts(w->raw_path, w->ts_path, true, ff_err);
    if (st != 0) {
        ::unlink(w->ts_path.c_str());
        st = srs_hikvision_run_ffmpeg_mpegts(w->raw_path, w->ts_path, false, ff_err);
    }
    ::unlink(w->raw_path.c_str());
    if (st != 0) {
        ::unlink(w->ts_path.c_str());
        w->err_code = ERROR_HIKVISION_STREAM;
        w->err_msg = srs_fmt_sprintf("ffmpeg remux mpegts failed status=%d %s", st, ff_err.c_str());
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }

    FILE *ffp = fopen(w->ts_path.c_str(), "rb");
    if (!ffp) {
        w->err_code = ERROR_HIKVISION_STREAM;
        w->err_msg = "open mpegts failed";
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }
    fseek(ffp, 0, SEEK_END);
    long fsz = ftell(ffp);
    fseek(ffp, 0, SEEK_SET);
    if (fsz < 188) {
        fclose(ffp);
        ::unlink(w->ts_path.c_str());
        w->err_code = ERROR_HIKVISION_STREAM;
        w->err_msg = srs_fmt_sprintf("mpegts too small size=%ld", fsz);
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }
    w->ts_out.resize((size_t)fsz);
    size_t nr = fread((void *)w->ts_out.data(), 1, (size_t)fsz, ffp);
    fclose(ffp);
    ::unlink(w->ts_path.c_str());
    if ((long)nr != fsz) {
        w->ts_out.clear();
        w->err_code = ERROR_HIKVISION_STREAM;
        w->err_msg = srs_fmt_sprintf("fread mpegts incomplete %zu/%ld", nr, fsz);
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }
    if (!srs_hikvision_looks_like_mpegts(w->ts_out)) {
        w->ts_out.clear();
        w->err_code = ERROR_HIKVISION_STREAM;
        w->err_msg = "remux output is not MPEG-TS";
        srs_hikvision_dl_mark_done(w);
        return NULL;
    }

    w->err_code = 0;
    srs_hikvision_dl_mark_done(w);
    return NULL;
}

srs_error_t SrsHikvisionManager::download_playback_ts(SrsHikvisionDevice *device, int channel, int64_t start_unix,
                                                      int64_t end_unix, string &ts_out)
{
    srs_error_t err = srs_success;
    ts_out.clear();

    if (!device || channel <= 0 || start_unix <= 0 || end_unix <= start_unix) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "invalid download range ch=%d start=%lld end=%lld", channel,
                             (long long)start_unix, (long long)end_unix);
    }
    const int64_t kMaxDur = 300;
    if (end_unix - start_unix > kMaxDur) {
        end_unix = start_unix + kMaxDur;
        srs_warn("Hikvision: download_playback_ts clamp duration to %llds", (long long)kMaxDur);
    }

    // One download at a time (ST-visible); live stream keeps using device login.
    if (__sync_lock_test_and_set(&g_hik_dl_inflight, 1)) {
        return srs_error_new(ERROR_HIKVISION_SDK, "another playback download is in progress");
    }

    string tag = srs_fmt_sprintf("%d_%lld_%lld", channel, (long long)start_unix, (long long)srs_time_now_realtime());
    string raw_path = string("/tmp/hik_dl_") + tag + ".bin";
    string ts_path = string("/tmp/hik_dl_") + tag + ".ts";
    ::unlink(raw_path.c_str());
    ::unlink(ts_path.c_str());

    // Ensure live login exists so we can fall back if 2nd login is rejected.
    if ((err = device->ensure_login()) != srs_success) {
        __sync_lock_release(&g_hik_dl_inflight);
        return srs_error_wrap(err, "login before download");
    }

    const SrsHikvisionDeviceConfig &conf = device->conf();
    SrsHikvisionDlWorkerCtx *work = new SrsHikvisionDlWorkerCtx();
    work->host = conf.host_;
    work->port = conf.port_;
    work->user = conf.user_;
    work->password = conf.password_;
    work->shared_user_id = (LONG)device->user_id();
    work->channel = channel;
    work->start_unix = start_unix;
    work->end_unix = end_unix;
    work->raw_path = raw_path;
    work->ts_path = ts_path;
    work->raw_sz = 0;
    work->progress_bytes = 0;
    work->play_done = false;
    work->err_code = 0;
    work->finished = 0;

    srs_trace("Hikvision: SDK download worker start ch=%d range=%lld..%lld host=%s shared_uid=%ld (mpegts)", channel,
              (long long)start_unix, (long long)end_unix, conf.host_.c_str(), (long)work->shared_user_id);

    pthread_t tid;
    int pr = pthread_create(&tid, NULL, srs_hikvision_dl_worker, work);
    if (pr != 0) {
        delete work;
        __sync_lock_release(&g_hik_dl_inflight);
        return srs_error_new(ERROR_HIKVISION_SDK, "pthread_create download worker failed ret=%d", pr);
    }

    // ST poll — yields so RTC/HTTP/SCTP stay alive. Worker self-limits (~90s + 60s ffmpeg).
    srs_utime_t t0 = srs_time_now_realtime();
    srs_utime_t last_log = t0;
    while (!work->finished) {
        srs_utime_t now = srs_time_now_realtime();
        if (now - last_log > 5 * SRS_UTIME_SECONDS) {
            srs_trace("Hikvision: download progress ch=%d raw_bytes=%lld elapsed=%ds", channel,
                      (long long)work->progress_bytes, (int)((now - t0) / SRS_UTIME_SECONDS));
            last_log = now;
        }
        srs_usleep(50 * SRS_UTIME_MILLISECONDS);
    }
    pthread_join(tid, NULL);

    if (work->err_code != 0) {
        err = srs_error_new(work->err_code, "%s", work->err_msg.c_str());
        delete work;
        __sync_lock_release(&g_hik_dl_inflight);
        return err;
    }

    ts_out.swap(work->ts_out);
    srs_trace("Hikvision: SDK download_playback_ts ok ch=%d size=%d raw=%lld range=%lld..%lld done=%d", channel,
              (int)ts_out.size(), (long long)work->raw_sz, (long long)start_unix, (long long)end_unix,
              work->play_done ? 1 : 0);
    delete work;
    __sync_lock_release(&g_hik_dl_inflight);
    return err;
}

// Async job: SDK download + HTTP cache (MPEG-TS). Never bulk-send over DataChannel.
class SrsHikvisionDcPlayJob : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsHikvisionManager *mgr_;
    SrsHikvisionDevice *device_;
    int channel_;
    int64_t start_unix_;
    int64_t end_unix_;
    ISrsHikvisionTalkListener *dc_;
    ISrsCoroutine *trd_;

public:
    SrsHikvisionDcPlayJob(SrsHikvisionManager *mgr, SrsHikvisionDevice *device, int channel, int64_t start_unix,
                          int64_t end_unix, ISrsHikvisionTalkListener *dc)
    {
        mgr_ = mgr;
        device_ = device;
        channel_ = channel;
        start_unix_ = start_unix;
        end_unix_ = end_unix;
        dc_ = dc;
        trd_ = new SrsSTCoroutine("hik-dc-play", this);
    }
    virtual ~SrsHikvisionDcPlayJob()
    {
        srs_freep(trd_);
    }

    srs_error_t start()
    {
        // No dc_acquire: we only send one small JSON notify (no bulk yields holding SCTP).
        return trd_->start();
    }

    virtual srs_error_t cycle()
    {
        srs_error_t err = srs_success;
        string ts;
        if ((err = mgr_->download_playback_ts(device_, channel_, start_unix_, end_unix_, ts)) != srs_success) {
            string detail = srs_error_summary(err);
            srs_warn("Hikvision: async play download failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            if (dc_ && dc_->dc_alive()) {
                // Escape for JSON string value.
                string safe;
                for (size_t i = 0; i < detail.size() && i < 200; i++) {
                    char c = detail[i];
                    if (c == '"' || c == '\\') {
                        safe.push_back('\\');
                    }
                    if (c == '\n' || c == '\r') {
                        safe.push_back(' ');
                        continue;
                    }
                    if ((unsigned char)c >= 0x20) {
                        safe.push_back(c);
                    }
                }
                if (safe.empty()) {
                    safe = "download failed";
                }
                string ej = srs_fmt_sprintf(
                    "{\"code\":-1,\"cmd\":\"play\",\"id\":\"HIK::PlaybackFileError\",\"msg\":\"%s\","
                    "\"playback\":%lld,\"playback_stop\":%lld}",
                    safe.c_str(), (long long)start_unix_, (long long)end_unix_);
                srs_error_t se = dc_->dc_send_text(ej);
                srs_freep(se);
            }
            trd_ = NULL;
            delete this;
            return srs_success;
        }

        string token = mgr_->store_playback_ts(ts, start_unix_, end_unix_);
        string url = "/api/v1/hikvision/playback?token=" + token;
        srs_trace("Hikvision: playback ready format=mpegts size=%d token=%s range=%lld..%lld", (int)ts.size(),
                  token.c_str(), (long long)start_unix_, (long long)end_unix_);

        if (dc_ && dc_->dc_alive()) {
            // Small JSON only — client downloads MPEG-TS via HTTP for mpegts.js.
            string ready = srs_fmt_sprintf(
                "{\"code\":0,\"msg\":\"ok\",\"cmd\":\"play\",\"id\":\"HIK::PlaybackFileReady\","
                "\"format\":\"mpegts\",\"mime\":\"video/mp2t\",\"size\":%d,\"url\":\"%s\",\"token\":\"%s\","
                "\"via\":\"http\",\"player\":\"mpegts.js\","
                "\"playback\":%lld,\"playback_stop\":%lld}",
                (int)ts.size(), url.c_str(), token.c_str(), (long long)start_unix_, (long long)end_unix_);
            srs_error_t se = dc_->dc_send_text(ready);
            if (se != srs_success) {
                srs_warn("Hikvision: notify PlaybackFileReady failed, err=%s", srs_error_desc(se).c_str());
                srs_freep(se);
            }
        }

        trd_ = NULL;
        delete this;
        return srs_success;
    }
};

// Unclaimed MPEG-TS (client never HTTP GET): TTL 2min, max 4 entries / ~64MB.
string SrsHikvisionManager::store_playback_ts(const string &ts, int64_t start_unix, int64_t end_unix)
{
    const size_t kMaxEntries = 4;
    const size_t kMaxBytes = 64 * 1024 * 1024;
    const srs_utime_t kTtl = 2 * 60 * SRS_UTIME_SECONDS;

    reap_playback_cache();

    while (!playback_cache_.empty()) {
        size_t bytes = 0;
        for (map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.begin();
             it != playback_cache_.end(); ++it) {
            bytes += it->second->body.size();
        }
        if (playback_cache_.size() < kMaxEntries && bytes + ts.size() <= kMaxBytes) {
            break;
        }
        map<string, SrsHikvisionPlaybackCache *>::iterator victim = playback_cache_.begin();
        for (map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.begin();
             it != playback_cache_.end(); ++it) {
            if (it->second->expire_at < victim->second->expire_at) {
                victim = it;
            }
        }
        srs_warn("Hikvision: playback cache drop unclaimed token=%s size=%d (limit)", victim->first.c_str(),
                 (int)victim->second->body.size());
        srs_freep(victim->second);
        playback_cache_.erase(victim);
    }

    string token = srs_fmt_sprintf("%lld%04d%04d", (long long)srs_time_now_realtime(),
                                   (int)(::rand() % 10000), (int)(::rand() % 10000));
    SrsHikvisionPlaybackCache *c = new SrsHikvisionPlaybackCache();
    c->body = ts;
    c->start_unix = start_unix;
    c->end_unix = end_unix;
    c->expire_at = srs_time_now_realtime() + kTtl;
    playback_cache_[token] = c;

    size_t bytes = 0;
    for (map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.begin(); it != playback_cache_.end();
         ++it) {
        bytes += it->second->body.size();
    }
    srs_trace("Hikvision: playback cache store token=%s size=%d entries=%d bytes=%d ttl=%ds format=mpegts",
              token.c_str(), (int)ts.size(), (int)playback_cache_.size(), (int)bytes,
              (int)(kTtl / SRS_UTIME_SECONDS));
    return token;
}

bool SrsHikvisionManager::take_playback_ts(const string &token, string &ts_out)
{
    if (token.empty()) {
        return false;
    }
    map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.find(token);
    if (it == playback_cache_.end()) {
        return false;
    }
    SrsHikvisionPlaybackCache *c = it->second;
    if (srs_time_now_realtime() > c->expire_at) {
        srs_warn("Hikvision: playback cache expired token=%s size=%d (client never downloaded)", token.c_str(),
                 (int)c->body.size());
        srs_freep(c);
        playback_cache_.erase(it);
        return false;
    }
    ts_out.swap(c->body);
    srs_freep(c);
    playback_cache_.erase(it);
    return true;
}

void SrsHikvisionManager::reap_playback_cache()
{
    srs_utime_t now = srs_time_now_realtime();
    vector<string> dead;
    for (map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.begin(); it != playback_cache_.end();
         ++it) {
        if (now > it->second->expire_at) {
            dead.push_back(it->first);
        }
    }
    for (size_t i = 0; i < dead.size(); i++) {
        map<string, SrsHikvisionPlaybackCache *>::iterator it = playback_cache_.find(dead[i]);
        if (it == playback_cache_.end()) {
            continue;
        }
        srs_warn("Hikvision: playback cache reap unclaimed token=%s size=%d", it->first.c_str(),
                 (int)it->second->body.size());
        srs_freep(it->second);
        playback_cache_.erase(it);
    }
}

srs_error_t SrsHikvisionManager::start_dc_play_job(SrsHikvisionDevice *device, int channel, int64_t start_unix,
                                                    int64_t end_unix, ISrsHikvisionTalkListener *dc)
{
    if (!device || !dc) {
        return srs_error_new(ERROR_HIKVISION_CONFIG, "start_dc_play_job need device+dc");
    }
    SrsHikvisionDcPlayJob *job = new SrsHikvisionDcPlayJob(this, device, channel, start_unix, end_unix, dc);
    srs_error_t err = job->start();
    if (err != srs_success) {
        delete job;
        return srs_error_wrap(err, "start dc play job");
    }
    // job self-deletes in cycle().
    return srs_success;
}

srs_error_t SrsHikvisionManager::handle_control(SrsJsonObject *req, const string &stream_context,
                                                ISrsHikvisionTalkListener *talk_listener, string *out_reply,
                                                string *out_binary)
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
        // DataChannel path: stream comes from WebRTC play session (set_sctp_stream_context).
        int sub = 0;
        if (!srs_hikvision_parse_stream(stream_context, serialno, channel, sub)) {
            return srs_error_new(ERROR_HIKVISION_STREAM, "invalid stream context %s", stream_context.c_str());
        }
    } else {
        return srs_error_new(ERROR_HIKVISION_CONFIG,
                             "need stream/serialno+channel (HTTP), or DataChannel on a play session");
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

    if (cmd == "talk") {
        string action = "start";
        if ((prop = req->ensure_property_string("action")) != NULL) {
            action = prop->to_str();
        }
        // Optional talk channel override (DC/HTTP). Default = stream/session channel.
        // Accept: channel | talk_channel | camChannel (compat).
        int talk_ch = channel;
        if ((prop = req->ensure_property_integer("channel")) != NULL) {
            int ov = (int)prop->to_integer();
            if (ov > 0) {
                talk_ch = ov;
            }
        } else if ((prop = req->ensure_property_integer("talk_channel")) != NULL) {
            int ov = (int)prop->to_integer();
            if (ov > 0) {
                talk_ch = ov;
            }
        } else if ((prop = req->ensure_property_integer("camChannel")) != NULL) {
            int ov = (int)prop->to_integer();
            if (ov > 0) {
                talk_ch = ov;
            }
        }
        if (talk_ch <= 0) {
            return srs_error_new(ERROR_HIKVISION_CONFIG, "invalid talk channel %d", talk_ch);
        }
        if (talk_ch != channel) {
            srs_trace("Hikvision: talk channel override stream_ch=%d -> talk_ch=%d serial=%s", channel,
                      talk_ch, serialno.c_str());
        }

        if (action == "start") {
            int codec = 0, rate = 8000;
            if ((err = talk_start(serialno, talk_ch, talk_listener, &codec, &rate)) != srs_success) {
                return srs_error_wrap(err, "talk start");
            }
            srs_trace("Hikvision: talk control start ok codec=%d rate=%d format=pcm serial=%s ch=%d",
                      codec, rate, serialno.c_str(), talk_ch);
            if (out_reply) {
                // Browser always uses PCM; codec is device-side only (server encodes).
                *out_reply = srs_fmt_sprintf(
                    "{\"code\":0,\"msg\":\"ok\",\"cmd\":\"talk\",\"action\":\"start\","
                    "\"codec\":%d,\"rate\":%d,\"channel\":%d,\"format\":\"pcm\",\"uplink\":\"pcm\","
                    "\"downlink\":\"preview\"}",
                    codec, rate, talk_ch);
            }
            return err;
        }
        if (action == "stop") {
            if ((err = talk_stop(serialno, talk_ch, talk_listener)) != srs_success) {
                return srs_error_wrap(err, "talk stop");
            }
            if (out_reply) {
                *out_reply = srs_fmt_sprintf(
                    "{\"code\":0,\"msg\":\"ok\",\"cmd\":\"talk\",\"action\":\"stop\",\"channel\":%d}",
                    talk_ch);
            }
            return err;
        }
        return srs_error_new(ERROR_HIKVISION_CONFIG, "talk action must be start|stop");
    }

    // Recording list: NET_DVR_FindFile_V40 (录像列表).
    // Request: {"cmd":"search_record","stream":"SN_6_0","start":unix,"end":unix}
    // Optional: file_type (default 0xff all), stream_type (0 auto), max (default 200)
    // Also accept meta.start/meta.end style via top-level start/end.
    if (cmd == "search_record" || cmd == "records" || cmd == "SearchRecord") {
        int64_t start_unix = 0, end_unix = 0;
        if ((prop = req->ensure_property_integer("start")) != NULL) {
            start_unix = prop->to_integer();
        }
        if ((prop = req->ensure_property_integer("end")) != NULL) {
            end_unix = prop->to_integer();
        }
        // Nested meta (compatible with external websocket clients).
        if ((start_unix <= 0 || end_unix <= 0) && (prop = req->ensure_property_object("meta")) != NULL) {
            SrsJsonObject *meta = prop->to_object();
            SrsJsonAny *mp = NULL;
            if (start_unix <= 0 && (mp = meta->ensure_property_integer("start")) != NULL) {
                start_unix = mp->to_integer();
            }
            if (end_unix <= 0 && (mp = meta->ensure_property_integer("end")) != NULL) {
                end_unix = mp->to_integer();
            }
            if ((mp = meta->ensure_property_integer("channel")) != NULL) {
                channel = (int)mp->to_integer();
            }
        }
        if (start_unix <= 0 || end_unix <= 0) {
            return srs_error_new(ERROR_HIKVISION_CONFIG, "search_record needs start/end unix timestamps");
        }
        int file_type = 0xff;
        if ((prop = req->ensure_property_integer("file_type")) != NULL) {
            file_type = (int)prop->to_integer();
        }
        int stream_type = 0;
        if ((prop = req->ensure_property_integer("stream_type")) != NULL) {
            stream_type = (int)prop->to_integer();
        }
        int max_results = 200;
        if ((prop = req->ensure_property_integer("max")) != NULL) {
            max_results = (int)prop->to_integer();
        }

        string reply;
        if ((err = search_records(device, channel, start_unix, end_unix, file_type, stream_type, max_results, &reply)) !=
            srs_success) {
            return srs_error_wrap(err, "search_record");
        }
        // Optional token/from for client correlation (websocket-style).
        string from;
        if ((prop = req->ensure_property_string("from")) != NULL) {
            from = prop->to_str();
        } else if ((prop = req->get_property("meta")) != NULL && prop->is_object()) {
            SrsJsonAny *mp = prop->to_object()->ensure_property_string("from");
            if (mp) {
                from = mp->to_str();
            }
        }
        if (!from.empty() && !reply.empty() && reply[reply.size() - 1] == '}') {
            // from is client-provided; strip quotes to keep JSON valid.
            string safe;
            for (size_t i = 0; i < from.size(); i++) {
                char c = from[i];
                if (c == '"' || c == '\\') {
                    safe.push_back('\\');
                }
                if ((unsigned char)c >= 0x20) {
                    safe.push_back(c);
                }
            }
            reply.resize(reply.size() - 1);
            reply += ",\"token\":[\"" + safe + "\"]}";
        }
        if (out_reply) {
            *out_reply = reply;
        }
        return err;
    }

    // Client FLV flow-control ACK during bulk DC transfer.
    // {"cmd":"play_ack","got":bytesReceived}
    if (cmd == "play_ack" || cmd == "flv_ack") {
        int64_t got = 0;
        if ((prop = req->ensure_property_integer("got")) != NULL) {
            got = prop->to_integer();
        } else if ((prop = req->ensure_property_integer("offset")) != NULL) {
            got = prop->to_integer();
        }
        if (talk_listener && got >= 0) {
            talk_listener->dc_note_play_ack(got);
        }
        if (out_reply) {
            *out_reply = "__noreply__";
        }
        return err;
    }

    // Bulk file download via SDK (PlayBackSaveData + PLAYFAST), remux to MPEG-TS for mpegts.js.
    // {"cmd":"play","playback":unixStart,"playback_stop":unixStop}
    if (cmd == "play") {
        int64_t start_unix = 0, end_unix = 0;
        if ((prop = req->ensure_property_integer("playback")) != NULL) {
            start_unix = prop->to_integer();
        } else if ((prop = req->ensure_property_integer("start")) != NULL) {
            start_unix = prop->to_integer();
        }
        if ((prop = req->ensure_property_integer("playback_stop")) != NULL) {
            end_unix = prop->to_integer();
        } else if ((prop = req->ensure_property_integer("end")) != NULL) {
            end_unix = prop->to_integer();
        }
        if (start_unix <= 0 || end_unix <= start_unix) {
            return srs_error_new(ERROR_HIKVISION_CONFIG,
                                 "cmd=play needs playback + playback_stop (unix seconds, stop>start)");
        }

        // DataChannel: async SDK download → HTTP token URL (MPEG-TS). No bulk on SCTP.
        if (talk_listener) {
            if ((err = start_dc_play_job(device, channel, start_unix, end_unix, talk_listener)) != srs_success) {
                return srs_error_wrap(err, "start async play job");
            }
            if (out_reply) {
                *out_reply = srs_fmt_sprintf(
                    "{\"code\":0,\"msg\":\"accepted\",\"cmd\":\"play\",\"id\":\"HIK::PlaybackAccepted\","
                    "\"format\":\"mpegts\",\"via\":\"http\",\"player\":\"mpegts.js\","
                    "\"playback\":%lld,\"playback_stop\":%lld}",
                    (long long)start_unix, (long long)end_unix);
            }
            return err;
        }
        // HTTP: sync download + raw MPEG-TS body.
        string ts;
        if ((err = download_playback_ts(device, channel, start_unix, end_unix, ts)) != srs_success) {
            return srs_error_wrap(err, "sdk download mpegts");
        }
        if (out_binary) {
            *out_binary = ts;
            if (out_reply) {
                *out_reply = srs_fmt_sprintf(
                    "{\"code\":0,\"msg\":\"ok\",\"cmd\":\"play\",\"id\":\"HIK::PlaybackFile\","
                    "\"format\":\"mpegts\",\"mime\":\"video/mp2t\",\"size\":%d,"
                    "\"playback\":%lld,\"playback_stop\":%lld,\"via\":\"sdk_playback_save\","
                    "\"player\":\"mpegts.js\"}",
                    (int)ts.size(), (long long)start_unix, (long long)end_unix);
            }
            return err;
        }
        return srs_error_new(ERROR_HIKVISION_CONFIG,
                             "cmd=play file transfer needs DataChannel peer or HTTP binary response");
    }

    return srs_error_new(ERROR_HIKVISION_CONFIG, "unknown cmd %s", cmd.c_str());
}

srs_error_t SrsHikvisionManager::talk_start(const string &serialno, int channel, ISrsHikvisionTalkListener *listener,
                                            int *out_codec, int *out_rate)
{
    srs_error_t err = srs_success;
    SrsHikvisionDevice *device = find_device(serialno);
    if (!device) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "unknown serialno %s", serialno.c_str());
    }
    string key = ptz_key(serialno, channel);
    SrsHikvisionTalkSession *sess = NULL;
    map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.find(key);
    if (it != talk_sessions_.end()) {
        sess = it->second;
    } else {
        sess = new SrsHikvisionTalkSession(device, channel);
        if ((err = sess->start()) != srs_success) {
            srs_freep(sess);
            return srs_error_wrap(err, "start talk session");
        }
        talk_sessions_[key] = sess;
    }
    if (listener) {
        sess->add_listener(listener);
    }
    sess->touch();
    if (out_codec) {
        *out_codec = sess->audio_enc_type();
    }
    if (out_rate) {
        *out_rate = sess->sample_rate_hz();
    }
    return err;
}

srs_error_t SrsHikvisionManager::talk_stop(const string &serialno, int channel, ISrsHikvisionTalkListener *listener)
{
    string key = ptz_key(serialno, channel);
    map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.find(key);
    if (it == talk_sessions_.end()) {
        return srs_success;
    }
    SrsHikvisionTalkSession *sess = it->second;
    int left = listener ? sess->remove_listener(listener) : 0;
    if (left <= 0) {
        srs_freep(sess);
        talk_sessions_.erase(it);
        srs_trace("Hikvision: talk session disposed key=%s", key.c_str());
    }
    return srs_success;
}

srs_error_t SrsHikvisionManager::talk_send_uplink(const string &stream_context, const char *data, int len,
                                                  ISrsHikvisionTalkListener *listener)
{
    // Prefer the session this peer joined (talk channel may differ from preview stream ch).
    if (listener) {
        for (map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.begin();
             it != talk_sessions_.end(); ++it) {
            if (it->second && it->second->has_listener(listener)) {
                return it->second->send_uplink(data, len);
            }
        }
    }

    string serialno;
    int channel = 0, sub = 0;
    if (!srs_hikvision_parse_stream(stream_context, serialno, channel, sub)) {
        return srs_error_new(ERROR_HIKVISION_STREAM, "invalid stream for talk uplink %s", stream_context.c_str());
    }
    string key = ptz_key(serialno, channel);
    map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.find(key);
    if (it != talk_sessions_.end()) {
        return it->second->send_uplink(data, len);
    }
    // Fallback: any talk session for this serialno (single active talk path).
    string prefix = serialno + "_";
    for (it = talk_sessions_.begin(); it != talk_sessions_.end(); ++it) {
        if (it->first.size() >= prefix.size() && it->first.compare(0, prefix.size(), prefix) == 0) {
            return it->second->send_uplink(data, len);
        }
    }
    return srs_error_new(ERROR_HIKVISION_SDK, "talk not started for %s (or peer not joined)", key.c_str());
}

void SrsHikvisionManager::talk_remove_listener(ISrsHikvisionTalkListener *listener)
{
    if (!listener) {
        return;
    }
    vector<string> empty_keys;
    for (map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.begin(); it != talk_sessions_.end(); ++it) {
        if (it->second->remove_listener(listener) <= 0) {
            empty_keys.push_back(it->first);
        }
    }
    for (size_t i = 0; i < empty_keys.size(); i++) {
        map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.find(empty_keys[i]);
        if (it == talk_sessions_.end()) {
            continue;
        }
        srs_trace("Hikvision: talk dispose after listener leave key=%s", it->first.c_str());
        srs_freep(it->second);
        talk_sessions_.erase(it);
    }
}

void SrsHikvisionManager::reap_talk_timeouts()
{
    srs_utime_t timeout = 120 * SRS_UTIME_SECONDS;
    srs_utime_t now = srs_time_now_cached();
    vector<string> to_remove;
    for (map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.begin(); it != talk_sessions_.end(); ++it) {
        if (it->second->listener_count() <= 0 || (now - it->second->last_active()) >= timeout) {
            to_remove.push_back(it->first);
        }
    }
    for (size_t i = 0; i < to_remove.size(); i++) {
        map<string, SrsHikvisionTalkSession *>::iterator it = talk_sessions_.find(to_remove[i]);
        if (it == talk_sessions_.end()) {
            continue;
        }
        srs_trace("Hikvision: talk idle dispose key=%s", it->first.c_str());
        srs_freep(it->second);
        talk_sessions_.erase(it);
    }
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
        reap_talk_timeouts();
        reap_playback_cache();

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

    string reply;
    string binary;
    if ((err = _srs_hikvision->handle_control_json(body, "", NULL, &reply, &binary)) != srs_success) {
        int code = srs_error_code(err);
        string msg = srs_error_summary(err);
        srs_warn("Hikvision control error %s", srs_error_desc(err).c_str());
        srs_freep(err);
        res->set("code", SrsJsonAny::integer(code));
        res->set("msg", SrsJsonAny::str(msg.c_str()));
        return srs_api_response(w, r, res->dumps());
    }

    // cmd=play may return raw MPEG-TS body (mpegts.js / video/mp2t).
    if (!binary.empty()) {
        w->header()->set_content_type("video/mp2t");
        w->header()->set_content_length((int)binary.size());
        w->header()->set("Content-Disposition", "attachment; filename=\"playback.ts\"");
        if ((err = w->write((char *)binary.data(), (int)binary.size())) != srs_success) {
            return srs_error_wrap(err, "write mpegts body");
        }
        return w->final_request();
    }

    // search_record returns full payload; other cmds use generic envelope.
    if (!reply.empty()) {
        return srs_api_response(w, r, reply);
    }
    res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    res->set("msg", SrsJsonAny::str("ok"));
    return srs_api_response(w, r, res->dumps());
}

// ---------------------------------------------------------------------------
// SrsGoApiHikvisionPlayback — GET cached MPEG-TS (token from DC PlaybackFileReady)
// ---------------------------------------------------------------------------

SrsGoApiHikvisionPlayback::SrsGoApiHikvisionPlayback()
{
}

SrsGoApiHikvisionPlayback::~SrsGoApiHikvisionPlayback()
{
}

srs_error_t SrsGoApiHikvisionPlayback::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    if (!_srs_hikvision || !_srs_hikvision->enabled()) {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_ServiceUnavailable);
    }

    // Only GET — POST is control API.
    if (!r->is_http_get()) {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_MethodNotAllowed);
    }

    string token = r->query_get("token");
    if (token.empty()) {
        return srs_go_http_error(w, SRS_CONSTS_HTTP_BadRequest);
    }

    string ts;
    if (!_srs_hikvision->take_playback_ts(token, ts) || ts.empty()) {
        srs_warn("Hikvision: playback token missing/expired token=%s", token.c_str());
        return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound);
    }

    // mpegts.js: type mse/mpegts, mime video/mp2t (or application/octet-stream).
    w->header()->set_content_type("video/mp2t");
    w->header()->set_content_length((int)ts.size());
    w->header()->set("Content-Disposition", "attachment; filename=\"playback.ts\"");
    w->header()->set("Cache-Control", "no-store");
    w->header()->set("Access-Control-Allow-Origin", "*");

    if ((err = w->write((char *)ts.data(), (int)ts.size())) != srs_success) {
        return srs_error_wrap(err, "write playback mpegts");
    }
    srs_trace("Hikvision: HTTP playback mpegts served token=%s size=%d", token.c_str(), (int)ts.size());
    return w->final_request();
}

#endif // SRS_HIKVISION
