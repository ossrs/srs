//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_raw_avc.hpp>

#include <string.h>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>

SrsRawH264Stream::SrsRawH264Stream()
{
}

SrsRawH264Stream::~SrsRawH264Stream()
{
}

srs_error_t SrsRawH264Stream::annexb_demux(SrsBuffer* stream, char** pframe, int* pnb_frame)
{
    srs_error_t err = srs_success;
    
    *pframe = NULL;
    *pnb_frame = 0;
    
    while (!stream->empty()) {
        // each frame must prefixed by annexb format.
        // about annexb, @see ISO_IEC_14496-10-AVC-2003.pdf, page 211.
        int pnb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &pnb_start_code)) {
            return srs_error_new(ERROR_H264_API_NO_PREFIXED, "annexb start code");
        }
        int start = stream->pos() + pnb_start_code;
        
        // find the last frame prefixed by annexb format.
        stream->skip(pnb_start_code);
        while (!stream->empty()) {
            if (srs_avc_startswith_annexb(stream, NULL)) {
                break;
            }
            stream->skip(1);
        }
        
        // demux the frame.
        *pnb_frame = stream->pos() - start;
        *pframe = stream->data() + start;
        break;
    }
    
    return err;
}

bool SrsRawH264Stream::is_sps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    uint8_t nal_unit_type = (char)frame[0] & 0x1f;
    
    return nal_unit_type == 7;
}

bool SrsRawH264Stream::is_pps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    
    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    uint8_t nal_unit_type = (char)frame[0] & 0x1f;
    
    return nal_unit_type == 8;
}

srs_error_t SrsRawH264Stream::sps_demux(char* frame, int nb_frame, string& sps)
{
    srs_error_t err = srs_success;
    
    // atleast 1bytes for SPS to decode the type, profile, constrain and level.
    if (nb_frame < 4) {
        return err;
    }

    sps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawH264Stream::pps_demux(char* frame, int nb_frame, string& pps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_AVC_PPS, "no pps");
    }

    pps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawH264Stream::mux_sequence_header(string sps, string pps, string& sh)
{
    srs_error_t err = srs_success;
    
    // 5bytes sps/pps header:
    //      configurationVersion, AVCProfileIndication, profile_compatibility,
    //      AVCLevelIndication, lengthSizeMinusOne
    // 3bytes size of sps:
    //      numOfSequenceParameterSets, sequenceParameterSetLength(2B)
    // Nbytes of sps.
    //      sequenceParameterSetNALUnit
    // 3bytes size of pps:
    //      numOfPictureParameterSets, pictureParameterSetLength
    // Nbytes of pps:
    //      pictureParameterSetNALUnit
    int nb_packet = 5 + (3 + (int)sps.length()) + (3 + (int)pps.length());
    char* packet = new char[nb_packet];
    SrsAutoFreeA(char, packet);
    
    // use stream to generate the h264 packet.
    SrsBuffer stream(packet, nb_packet);
    
    // decode the SPS:
    // @see: 7.3.2.1.1, ISO_IEC_14496-10-AVC-2012.pdf, page 62
    if (true) {
        srs_assert((int)sps.length() >= 4);
        char* frame = (char*)sps.data();
        
        // @see: Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 205
        //      Baseline profile profile_idc is 66(0x42).
        //      Main profile profile_idc is 77(0x4d).
        //      Extended profile profile_idc is 88(0x58).
        uint8_t profile_idc = frame[1];
        //uint8_t constraint_set = frame[2];
        uint8_t level_idc = frame[3];
        
        // generate the sps/pps header
        // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
        // configurationVersion
        stream.write_1bytes(0x01);
        // AVCProfileIndication
        stream.write_1bytes(profile_idc);
        // profile_compatibility
        stream.write_1bytes(0x00);
        // AVCLevelIndication
        stream.write_1bytes(level_idc);
        // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size,
        // so we always set it to 0x03.
        stream.write_1bytes(0x03);
    }
    
    // sps
    if (true) {
        // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
        // numOfSequenceParameterSets, always 1
        stream.write_1bytes(0x01);
        // sequenceParameterSetLength
        stream.write_2bytes((int16_t)sps.length());
        // sequenceParameterSetNALUnit
        stream.write_string(sps);
    }
    
    // pps
    if (true) {
        // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
        // numOfPictureParameterSets, always 1
        stream.write_1bytes(0x01);
        // pictureParameterSetLength
        stream.write_2bytes((int16_t)pps.length());
        // pictureParameterSetNALUnit
        stream.write_string(pps);
    }
    
    // TODO: FIXME: for more profile.
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 144

    sh = string(packet, nb_packet);
    
    return err;
}

srs_error_t SrsRawH264Stream::mux_ipb_frame(char* frame, int nb_frame, string& ibp)
{
    srs_error_t err = srs_success;
    
    // 4bytes size of nalu:
    //      NALUnitLength
    // Nbytes of nalu.
    //      NALUnit
    int nb_packet = 4 + nb_frame;
    char* packet = new char[nb_packet];
    SrsAutoFreeA(char, packet);
    
    // use stream to generate the h264 packet.
    SrsBuffer stream(packet, nb_packet);
    
    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
    uint32_t NAL_unit_length = nb_frame;
    
    // mux the avc NALU in "ISO Base Media File Format"
    // from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    // NALUnitLength
    stream.write_4bytes(NAL_unit_length);
    // NALUnit
    stream.write_bytes(frame, nb_frame);

    ibp = string(packet, nb_packet);
    
    return err;
}

srs_error_t SrsRawH264Stream::mux_avc2flv(string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char** flv, int* nb_flv)
{
    srs_error_t err = srs_success;
    
    // for h264 in RTMP video payload, there is 5bytes header:
    //      1bytes, FrameType | CodecID
    //      1bytes, AVCPacketType
    //      3bytes, CompositionTime, the cts.
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int size = (int)video.length() + 5;
    char* data = new char[size];
    char* p = data;
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    // Frame Type, Type of video frame.
    // CodecID, Codec Identifier.
    // set the rtmp header
    *p++ = (frame_type << 4) | SrsVideoCodecIdAVC;
    
    // AVCPacketType
    *p++ = avc_packet_type;
    
    // CompositionTime
    // pts = dts + cts, or
    // cts = pts - dts.
    // where cts is the header in rtmp video packet payload header.
    uint32_t cts = pts - dts;
    char* pp = (char*)&cts;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
    
    // h.264 raw data.
    memcpy(p, video.data(), video.length());
    
    *flv = data;
    *nb_flv = size;
    
    return err;
}

#ifdef SRS_H265

#define SRS_STREAM_BIT(ptr, off) (((ptr)[(off) / 8] >> (7 - ((off) % 8))) & 0x01)

struct mpeg4_hevc_t {
    uint8_t  configurationVersion;	// 1-only
    uint8_t  general_profile_space;	// 2bit,[0,3]
    uint8_t  general_tier_flag;		// 1bit,[0,1]
    uint8_t  general_profile_idc;	// 5bit,[0,31]
    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags;
    uint8_t  general_level_idc;
    uint16_t min_spatial_segmentation_idc;
    uint8_t  parallelismType;		// 2bit,[0,3]
    uint8_t  chromaFormat;			// 2bit,[0,3]
    uint8_t  bitDepthLumaMinus8;	// 3bit,[0,7]
    uint8_t  bitDepthChromaMinus8;	// 3bit,[0,7]
    uint16_t avgFrameRate;
    uint8_t  constantFrameRate;		// 2bit,[0,3]
    uint8_t  numTemporalLayers;		// 3bit,[0,7]
    uint8_t  temporalIdNested;		// 1bit,[0,1]
    uint8_t  lengthSizeMinusOne;	// 2bit,[0,3]

    uint8_t  numOfArrays;
    struct {
        uint8_t array_completeness;
        uint8_t type; // nalu type
        uint16_t bytes;
        uint8_t* data;
    } nalu[64];

    uint8_t array_completeness;
    uint8_t data[4 * 1024];
    int off;
};

static uint8_t mpeg4_hevc_read_ue(const uint8_t *data, int bytes, int *offset)
{
    int bit;
    int leadingZeroBits = -1;

    for (bit = 0; !bit && *offset / 8 < bytes; ++leadingZeroBits) {
        bit = (data[*offset / 8] >> (7 - (*offset % 8))) & 0x01;
        ++*offset;
    }

    srs_assert(leadingZeroBits < 32);

    bit = 0;
    for (int i = 0; i < leadingZeroBits && *offset / 8 < bytes; i++) {
        bit = (bit << 1) | ((data[*offset / 8] >> (7 - (*offset % 8))) & 0x01);
        ++*offset;
    }

    return (uint8_t)((1 << leadingZeroBits) - 1 + bit);
}

static int hevc_rbsp_decode(const uint8_t* nalu, int bytes, uint8_t* sodb)
{
    int n = 0;

    for (int i = 0; i < bytes; i++) {
        if (i + 2 < bytes && 0 == nalu[i] && 0 == nalu[i + 1] && 0x03 == nalu[i + 2]) {
            sodb[n++] = nalu[i];
            sodb[n++] = nalu[i + 1];
            i += 2;
        } else {
            sodb[n++] = nalu[i];
        }
    }

    return n;
}

static int hevc_profile_tier_level(const uint8_t* nalu, int bytes, uint8_t maxNumSubLayersMinus1, struct mpeg4_hevc_t* hevc)
{
    if (bytes < 12) return -1;
    if (maxNumSubLayersMinus1 < 1) return 12;

    hevc->general_profile_space = (nalu[0] >> 6) & 0x03;
    hevc->general_tier_flag = (nalu[0] >> 5) & 0x01;
    hevc->general_profile_idc = nalu[0] & 0x1f;

    hevc->general_profile_compatibility_flags = 0;
    hevc->general_profile_compatibility_flags |= nalu[1] << 24;
    hevc->general_profile_compatibility_flags |= nalu[2] << 16;
    hevc->general_profile_compatibility_flags |= nalu[3] << 8;
    hevc->general_profile_compatibility_flags |= nalu[4];

    hevc->general_constraint_indicator_flags = 0;
    hevc->general_constraint_indicator_flags |= ((uint64_t)nalu[5]) << 40;
    hevc->general_constraint_indicator_flags |= ((uint64_t)nalu[6]) << 32;
    hevc->general_constraint_indicator_flags |= ((uint64_t)nalu[7]) << 24;
    hevc->general_constraint_indicator_flags |= ((uint64_t)nalu[8]) << 16;
    hevc->general_constraint_indicator_flags |= ((uint64_t)nalu[9]) << 8;
    hevc->general_constraint_indicator_flags |= nalu[10];

    hevc->general_level_idc = nalu[11];

    uint8_t sub_layer_profile_present_flag[8];
    uint8_t sub_layer_level_present_flag[8];
    for (int i = 0; i < maxNumSubLayersMinus1; i++) {
        sub_layer_profile_present_flag[i] = SRS_STREAM_BIT(nalu, 12 * 8 + i * 2);
        sub_layer_level_present_flag[i] = SRS_STREAM_BIT(nalu, 12 * 8 + i * 2 + 1);
    }

    int n = 12 + 2;
    for (int i = 0; i < maxNumSubLayersMinus1; i++) {
        if (sub_layer_profile_present_flag[i]) n += 11;
        if (sub_layer_level_present_flag[i]) n += 1;
    }

    return bytes < n ? n : -1;
}

static uint8_t hevc_vps_id(const uint8_t* rbsp, int bytes, struct mpeg4_hevc_t* hevc, uint8_t* ptr)
{
    int sodb = hevc_rbsp_decode(rbsp, bytes, ptr);
    if (sodb < 16 + 2) return 0xFF;

    uint8_t vps = ptr[2] >> 4; // 2-nalu type
    uint8_t vps_max_sub_layers_minus1 = (ptr[3] >> 1) & 0x07;
    uint8_t vps_temporal_id_nesting_flag = ptr[3] & 0x01;
    hevc->numTemporalLayers = srs_max(hevc->numTemporalLayers, vps_max_sub_layers_minus1 + 1);
    hevc->temporalIdNested = (hevc->temporalIdNested || vps_temporal_id_nesting_flag) ? 1 : 0;
    hevc_profile_tier_level(ptr + 6, sodb - 6, vps_max_sub_layers_minus1, hevc);

    return vps;
}

static uint8_t hevc_sps_id(const uint8_t* rbsp, int bytes, struct mpeg4_hevc_t* hevc, uint8_t* ptr, uint8_t* vps)
{
    int sodb = hevc_rbsp_decode(rbsp, bytes, ptr);
    if (sodb < 12 + 3) return 0xFF;

    *vps = ptr[2] >> 4;  // 2-nalu type

    uint8_t sps_max_sub_layers_minus1 = (ptr[2] >> 1) & 0x07;
    uint8_t sps_temporal_id_nesting_flag = ptr[2] & 0x01;
    (void)sps_temporal_id_nesting_flag;

    int n = hevc_profile_tier_level(ptr + 3, sodb - 3, sps_max_sub_layers_minus1, hevc);
    if (n <= 0) return 0xFF;
    n = (n + 3) * 8;

    uint8_t sps = mpeg4_hevc_read_ue(ptr, sodb, &n);
    hevc->chromaFormat = mpeg4_hevc_read_ue(ptr, sodb, &n);
    if (3 == hevc->chromaFormat) n++;

    mpeg4_hevc_read_ue(ptr, sodb, &n); // pic_width_in_luma_samples
    mpeg4_hevc_read_ue(ptr, sodb, &n); // pic_height_in_luma_samples

    uint8_t conformance_window_flag = SRS_STREAM_BIT(ptr, n); n++; // conformance_window_flag
    if (conformance_window_flag) {
        mpeg4_hevc_read_ue(ptr, sodb, &n); // conf_win_left_offset
        mpeg4_hevc_read_ue(ptr, sodb, &n); // conf_win_right_offset
        mpeg4_hevc_read_ue(ptr, sodb, &n); // conf_win_top_offset
        mpeg4_hevc_read_ue(ptr, sodb, &n); // conf_win_bottom_offset
    }
    hevc->bitDepthLumaMinus8 = mpeg4_hevc_read_ue(ptr, sodb, &n);
    hevc->bitDepthChromaMinus8 = mpeg4_hevc_read_ue(ptr, sodb, &n);

    // TODO: vui_parameters
    //mp4->hevc->min_spatial_segmentation_idc; // min_spatial_segmentation_idc

    return sps;
}

static uint8_t hevc_pps_id(const uint8_t* rbsp, int bytes, struct mpeg4_hevc_t* hevc, uint8_t* ptr, uint8_t* sps)
{
    // FIXME: TODO:
    //hevc->parallelismType; // entropy_coding_sync_enabled_flag
    (void)hevc;

    int sodb = hevc_rbsp_decode(rbsp, bytes, ptr);
    if (sodb < 3) return 0xFF;

    int offset = 2 * 8; // 2-nalu type
    *sps = mpeg4_hevc_read_ue(ptr, sodb, &offset);
    return mpeg4_hevc_read_ue(ptr, sodb, &offset);
}

static void mpeg4_hevc_remove(struct mpeg4_hevc_t* hevc, uint8_t* ptr, int bytes, const uint8_t* end)
{
    srs_assert(ptr >= hevc->data && ptr + bytes <= end && end <= hevc->data + sizeof(hevc->data));

    memmove(ptr, ptr + bytes, end - ptr - bytes);

    for (int i = 0; i < hevc->numOfArrays; i++) {
        if (hevc->nalu[i].data > ptr) hevc->nalu[i].data -= bytes;
    }
}

static int mpeg4_hevc_update(struct mpeg4_hevc_t *hevc, int i, const uint8_t *nalu, int bytes)
{
    if (bytes == hevc->nalu[i].bytes && 0 == memcmp(nalu, hevc->nalu[i].data, bytes))
        return 0; // do nothing

    if (bytes > hevc->nalu[i].bytes && hevc->off + (bytes - hevc->nalu[i].bytes) > (int)sizeof(hevc->data)) {
        return -1; // too big
    }

    mpeg4_hevc_remove(hevc, hevc->nalu[i].data, hevc->nalu[i].bytes, hevc->data + hevc->off);
    hevc->off -= hevc->nalu[i].bytes;

    hevc->nalu[i].data = hevc->data + hevc->off;
    hevc->nalu[i].bytes = (uint16_t)bytes;
    memcpy(hevc->nalu[i].data, nalu, bytes);
    hevc->off += bytes;

    return 1;
}

static int mpeg4_hevc_add(struct mpeg4_hevc_t* hevc, uint8_t type, const uint8_t* nalu, int bytes)
{
    // copy new
    srs_assert(hevc->numOfArrays < (int)sizeof(hevc->nalu) / (int)sizeof(hevc->nalu[0]));

    if (hevc->numOfArrays >= (int)sizeof(hevc->nalu) / (int)sizeof(hevc->nalu[0]) || hevc->off + bytes > (int)sizeof(hevc->data)) {
        return -1;
    }

    hevc->nalu[hevc->numOfArrays].type = type;
    hevc->nalu[hevc->numOfArrays].bytes = (uint16_t)bytes;
    hevc->nalu[hevc->numOfArrays].array_completeness = 1;
    hevc->nalu[hevc->numOfArrays].data = hevc->data + hevc->off;
    memcpy(hevc->nalu[hevc->numOfArrays].data, nalu, bytes);
    hevc->off += bytes;
    ++hevc->numOfArrays;

    return 1;
}

static int h265_vps_copy(struct mpeg4_hevc_t* hevc, const uint8_t* nalu, int bytes)
{
    if (bytes < 3) return -1; // invalid length

    uint8_t vpsid = hevc_vps_id(nalu, bytes, hevc, hevc->data + hevc->off);
    for (int i = 0; i < hevc->numOfArrays; i++) {
        if (SrsHevcNaluType_VPS == hevc->nalu[i].type && vpsid == hevc_vps_id(hevc->nalu[i].data, hevc->nalu[i].bytes, hevc, hevc->data + hevc->off))
            return mpeg4_hevc_update(hevc, i, nalu, bytes);
    }

    return mpeg4_hevc_add(hevc, SrsHevcNaluType_VPS, nalu, bytes);
}

static int h265_sps_copy(struct mpeg4_hevc_t* hevc, const uint8_t* nalu, int bytes)
{
    if (bytes < 13 + 2) return -1; // invalid length

    uint8_t vpsid, vpsid2;
    uint8_t spsid = hevc_sps_id(nalu, bytes, hevc, hevc->data + hevc->off, &vpsid);
    for (int i = 0; i < hevc->numOfArrays; i++) {
        if (SrsHevcNaluType_SPS == hevc->nalu[i].type && spsid == hevc_sps_id(hevc->nalu[i].data, hevc->nalu[i].bytes, hevc, hevc->data + hevc->off, &vpsid2) && vpsid == vpsid2)
            return mpeg4_hevc_update(hevc, i, nalu, bytes);
    }

    return mpeg4_hevc_add(hevc, SrsHevcNaluType_SPS, nalu, bytes);
}

static int h265_pps_copy(struct mpeg4_hevc_t* hevc, const uint8_t* nalu, int bytes)
{
    if (bytes < 1 + 2) return -1; // invalid length

    uint8_t spsid, spsid2;
    uint8_t ppsid = hevc_pps_id(nalu, bytes, hevc, hevc->data + hevc->off, &spsid);
    for (int i = 0; i < hevc->numOfArrays; i++) {
        if (SrsHevcNaluType_PPS == hevc->nalu[i].type && ppsid == hevc_pps_id(hevc->nalu[i].data, hevc->nalu[i].bytes, hevc, hevc->data + hevc->off, &spsid2) && spsid == spsid2)
            return mpeg4_hevc_update(hevc, i, nalu, bytes);
    }

    return mpeg4_hevc_add(hevc, SrsHevcNaluType_PPS, nalu, bytes);
}

SrsRawHEVCStream::SrsRawHEVCStream()
{
}

SrsRawHEVCStream::~SrsRawHEVCStream()
{
}

srs_error_t SrsRawHEVCStream::annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame)
{
    srs_error_t err = srs_success;

    *pframe = NULL;
    *pnb_frame = 0;

    while (!stream->empty()) {
        // each frame must prefixed by annexb format.
        // about annexb, @see ISO_IEC_14496-10-AVC-2003.pdf, page 211.
        int pnb_start_code = 0;
        if (!srs_avc_startswith_annexb(stream, &pnb_start_code)) {
            return srs_error_new(ERROR_HEVC_API_NO_PREFIXED, "hevc annexb start code");
        }
        int start = stream->pos() + pnb_start_code;

        // find the last frame prefixed by annexb format.
        stream->skip(pnb_start_code);
        while (!stream->empty()) {
            if (srs_avc_startswith_annexb(stream, NULL)) {
                break;
            }
            stream->skip(1);
        }

        // demux the frame.
        *pnb_frame = stream->pos() - start;
        *pframe = stream->data() + start;
        break;
    }

    return err;
}

// whether the frame is vps or sps or pps.
bool SrsRawHEVCStream::is_vps(char *frame, int nb_frame)
{
    srs_assert(nb_frame > 0);

    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsHevcNaluType nal_unit_type = (SrsHevcNaluType)((frame[0] & 0x7E) >> 1);

    return nal_unit_type == SrsHevcNaluType_VPS;
}

bool SrsRawHEVCStream::is_sps(char *frame, int nb_frame)
{
    srs_assert(nb_frame > 0);

    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsHevcNaluType nal_unit_type = (SrsHevcNaluType)((frame[0] & 0x7E) >> 1);

    return nal_unit_type == SrsHevcNaluType_SPS;
}

bool SrsRawHEVCStream::is_pps(char *frame, int nb_frame)
{
    srs_assert(nb_frame > 0);

    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsHevcNaluType nal_unit_type = (SrsHevcNaluType)((frame[0] & 0x7E) >> 1);

    return nal_unit_type == SrsHevcNaluType_PPS;
}

srs_error_t SrsRawHEVCStream::vps_demux(char *frame, int nb_frame, std::string &vps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_VPS, "no hevc vps");
    }

    vps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::sps_demux(char *frame, int nb_frame, std::string &sps)
{
    srs_error_t err = srs_success;

    // atleast 1bytes for SPS to decode the type, profile, constrain and level.
    if (nb_frame < 4) {
        return err;
    }

    sps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::pps_demux(char *frame, int nb_frame, std::string &pps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_PPS, "no hevc pps");
    }

    pps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_sequence_header(std::string vps, std::string sps, std::string pps, std::string &hvcC)
{
    srs_error_t err = srs_success;

    uint8_t temp8bits = 0;
    struct mpeg4_hevc_t hevc_info;

    memset(&hevc_info, 0, sizeof(hevc_info));

    if (h265_vps_copy(&hevc_info, (uint8_t *)vps.data(), (int)vps.length()) < 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_VPS, "decode vps error");
    }

    if (h265_sps_copy(&hevc_info, (uint8_t *)sps.data(), (int)sps.length()) < 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_SPS, "decode sps error");
    }

    if (h265_pps_copy(&hevc_info, (uint8_t *)pps.data(), (int)pps.length()) < 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_PPS, "decode pps error");
    }

    // hevc header information:
    // 23bytes header:
    //      configurationVersion, general_profile_space, general_tier_flag, general_profile_idc
    //      general_profile_compatibility_flags, general_constraint_indicator_flags,
    //      general_level_idc, min_spatial_segmentation_idc, parallelismType,
    //      chromaFormat, bitDepthLumaMinus8, bitDepthChromaMinus8,
    //      avgFrameRate, constantFrameRate, numTemporalLayers, temporalIdNested,
    //      lengthSizeMinusOne, numOfArrays
    // 5bytes size of vps/sps/pps:
    //      array_completeness, nal_unit_type, numNalus, nalUnitLength,
    // Nbytes of vps/sps/pps.
    //      sequenceParameterSetNALUnit

    // use simple mode: nalu size + nalu data
    int nb_packet = 23 + 5 + (int)vps.length() + 5 + (int)sps.length() + 5 + (int)pps.length();
    char *packet = new char[nb_packet];
    SrsAutoFreeA(char, packet);

    // use stream to generate the hevc packet.
    SrsBuffer stream(packet, nb_packet);

    hevc_info.configurationVersion = 1;
    stream.write_1bytes(hevc_info.configurationVersion);

    temp8bits = 0;
    temp8bits |= ((hevc_info.general_profile_space << 6) & 0xc0);
    temp8bits |= ((hevc_info.general_tier_flag << 5) & 0x20);
    temp8bits |= hevc_info.general_profile_idc & 0x1f;
    stream.write_1bytes(temp8bits);

    stream.write_4bytes(hevc_info.general_profile_compatibility_flags);

    stream.write_2bytes((hevc_info.general_constraint_indicator_flags >> 32) & 0xffff);
    stream.write_4bytes(hevc_info.general_constraint_indicator_flags & 0xffffffff);

    stream.write_1bytes(hevc_info.general_level_idc);

    stream.write_2bytes(0xf000 | (hevc_info.min_spatial_segmentation_idc & 0x0fff));

    stream.write_1bytes(0xfc | (hevc_info.parallelismType & 0x03));
    stream.write_1bytes(0xfc | (hevc_info.chromaFormat & 0x03));

    stream.write_1bytes(0xf8 | (hevc_info.bitDepthLumaMinus8 & 0x07));

    stream.write_1bytes(0xf8 | (hevc_info.bitDepthChromaMinus8 & 0x07));

    stream.write_2bytes(hevc_info.avgFrameRate);

    hevc_info.lengthSizeMinusOne = 3;
    temp8bits = 0;
    temp8bits |= (hevc_info.constantFrameRate << 6) | 0xc0;
    temp8bits |= (hevc_info.numTemporalLayers << 3) | 0x38;
    temp8bits |= (hevc_info.temporalIdNested << 2) | 0x04;
    temp8bits |= hevc_info.lengthSizeMinusOne & 0x03;

    stream.write_1bytes(temp8bits);

    uint8_t numOfArrays = 3; // vps,sps,pps
    stream.write_1bytes(numOfArrays);

    uint8_t array_completeness = 0; // 1bit
    // uint8_t reserved = 0;//1bit
    uint8_t nal_unit_type = 0; // 6bits;

    // vps
    nal_unit_type = ((array_completeness << 7) & 0x80) | (SrsHevcNaluType_VPS & 0x3f);
    stream.write_1bytes(nal_unit_type);

    uint16_t namNalus = 1;
    stream.write_2bytes(namNalus);

    uint16_t nalUnitLength = vps.length();
    stream.write_2bytes(nalUnitLength);

    stream.write_string(vps);

    // sps
    nal_unit_type = ((array_completeness << 7) & 0x80) | (SrsHevcNaluType_SPS & 0x3f);
    stream.write_1bytes(nal_unit_type);

    namNalus = 1;
    stream.write_2bytes(namNalus);

    nalUnitLength = sps.length();
    stream.write_2bytes(nalUnitLength);

    stream.write_string(sps);

    // pps
    nal_unit_type = ((array_completeness << 7) & 0x80) | (SrsHevcNaluType_PPS & 0x3f);
    stream.write_1bytes(nal_unit_type);

    namNalus = 1;
    stream.write_2bytes(namNalus);

    nalUnitLength = pps.length();
    stream.write_2bytes(nalUnitLength);
    stream.write_string(pps);

    hvcC = string(packet, nb_packet);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_ipb_frame(char *frame, int nb_frame, std::string &ibp)
{
    srs_error_t err = srs_success;

    // 4bytes size of nalu:
    //      NALUnitLength
    // Nbytes of nalu.
    //      NALUnit
    int nb_packet = 4 + nb_frame;
    char *packet = new char[nb_packet];
    SrsAutoFreeA(char, packet);

    // use stream to generate the h264 packet.
    SrsBuffer stream(packet, nb_packet);

    // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
    // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
    uint32_t NAL_unit_length = nb_frame;

    // mux the avc NALU in "ISO Base Media File Format"
    // from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    // NALUnitLength
    stream.write_4bytes(NAL_unit_length);
    // NALUnit
    stream.write_bytes(frame, nb_frame);

    ibp = string(packet, nb_packet);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv)
{
    srs_error_t err = srs_success;

    // for h264 in RTMP video payload, there is 5bytes header:
    //      1bytes, FrameType | CodecID
    //      1bytes, AVCPacketType
    //      3bytes, CompositionTime, the cts.
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int size = (int)video.length() + 5;
    char *data = new char[size];
    char *p = data;

    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    // Frame Type, Type of video frame.
    // CodecID, Codec Identifier.
    // set the rtmp header
    *p++ = (frame_type << 4) | SrsVideoCodecIdHEVC;

    // AVCPacketType
    *p++ = avc_packet_type;

    // CompositionTime
    // pts = dts + cts, or
    // cts = pts - dts.
    // where cts is the header in rtmp video packet payload header.
    uint32_t cts = pts - dts;
    char *pp = (char *)&cts;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];

    // hevc raw data.
    memcpy(p, video.data(), video.length());

    *flv = data;
    *nb_flv = size;

    return err;
}
#endif

SrsRawAacStream::SrsRawAacStream()
{
}

SrsRawAacStream::~SrsRawAacStream()
{
}

srs_error_t SrsRawAacStream::adts_demux(SrsBuffer* stream, char** pframe, int* pnb_frame, SrsRawAacStreamCodec& codec)
{
    srs_error_t err = srs_success;
    
    while (!stream->empty()) {
        int adts_header_start = stream->pos();
        
        // decode the ADTS.
        // @see ISO_IEC_13818-7-AAC-2004.pdf, page 26
        //      6.2 Audio Data Transport Stream, ADTS
        // byte_alignment()
        
        // adts_fixed_header:
        //      12bits syncword,
        //      16bits left.
        // adts_variable_header:
        //      28bits
        //      12+16+28=56bits
        // adts_error_check:
        //      16bits if protection_absent
        //      56+16=72bits
        // if protection_absent:
        //      require(7bytes)=56bits
        // else
        //      require(9bytes)=72bits
        if (!stream->require(7)) {
            return srs_error_new(ERROR_AAC_ADTS_HEADER, "requires 7 only %d bytes", stream->left());
        }
        
        // for aac, the frame must be ADTS format.
        if (!srs_aac_startswith_adts(stream)) {
            return srs_error_new(ERROR_AAC_REQUIRED_ADTS, "not adts");
        }
        
        // syncword 12 bslbf
        stream->read_1bytes();
        // 4bits left.
        // adts_fixed_header(), 1.A.2.2.1 Fixed Header of ADTS
        // ID 1 bslbf
        // layer 2 uimsbf
        // protection_absent 1 bslbf
        int8_t pav = (stream->read_1bytes() & 0x0f);
        int8_t id = (pav >> 3) & 0x01;
        /*int8_t layer = (pav >> 1) & 0x03;*/
        int8_t protection_absent = pav & 0x01;
        
        /**
         * ID: MPEG identifier, set to '1' if the audio data in the ADTS stream are MPEG-2 AAC (See ISO/IEC 13818-7)
         * and set to '0' if the audio data are MPEG-4. See also ISO/IEC 11172-3, subclause 2.4.2.3.
         */
        if (id != 0x01) {
            // well, some system always use 0, but actually is aac format.
            // for example, houjian vod ts always set the aac id to 0, actually 1.
            // we just ignore it, and alwyas use 1(aac) to demux.
            id = 0x01;
        }
        
        int16_t sfiv = stream->read_2bytes();
        // profile 2 uimsbf
        // sampling_frequency_index 4 uimsbf
        // private_bit 1 bslbf
        // channel_configuration 3 uimsbf
        // original/copy 1 bslbf
        // home 1 bslbf
        int8_t profile = (sfiv >> 14) & 0x03;
        int8_t sampling_frequency_index = (sfiv >> 10) & 0x0f;
        /*int8_t private_bit = (sfiv >> 9) & 0x01;*/
        int8_t channel_configuration = (sfiv >> 6) & 0x07;
        /*int8_t original = (sfiv >> 5) & 0x01;*/
        /*int8_t home = (sfiv >> 4) & 0x01;*/
        //int8_t Emphasis; @remark, Emphasis is removed
        // 4bits left.
        // adts_variable_header(), 1.A.2.2.2 Variable Header of ADTS
        // copyright_identification_bit 1 bslbf
        // copyright_identification_start 1 bslbf
        /*int8_t fh_copyright_identification_bit = (fh1 >> 3) & 0x01;*/
        /*int8_t fh_copyright_identification_start = (fh1 >> 2) & 0x01;*/
        // frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
        // use the left 2bits as the 13 and 12 bit,
        // the frame_length is 13bits, so we move 13-2=11.
        int16_t frame_length = (sfiv << 11) & 0x1800;
        
        int32_t abfv = stream->read_3bytes();
        // frame_length 13 bslbf: consume the first 13-2=11bits
        // the fh2 is 24bits, so we move right 24-11=13.
        frame_length |= (abfv >> 13) & 0x07ff;
        // adts_buffer_fullness 11 bslbf
        /*int16_t fh_adts_buffer_fullness = (abfv >> 2) & 0x7ff;*/
        // number_of_raw_data_blocks_in_frame 2 uimsbf
        /*int16_t number_of_raw_data_blocks_in_frame = abfv & 0x03;*/
        // adts_error_check(), 1.A.2.2.3 Error detection
        if (!protection_absent) {
            if (!stream->require(2)) {
                return srs_error_new(ERROR_AAC_ADTS_HEADER, "requires 2 only %d bytes", stream->left());
            }
            // crc_check 16 Rpchof
            /*int16_t crc_check = */stream->read_2bytes();
        }
        
        // TODO: check the sampling_frequency_index
        // TODO: check the channel_configuration
        
        // raw_data_blocks
        int adts_header_size = stream->pos() - adts_header_start;
        int raw_data_size = frame_length - adts_header_size;
        if (!stream->require(raw_data_size)) {
            return srs_error_new(ERROR_AAC_ADTS_HEADER, "requires %d only %d bytes", raw_data_size, stream->left());
        }
        
        // the codec info.
        codec.protection_absent = protection_absent;
        codec.aac_object = srs_aac_ts2rtmp((SrsAacProfile)profile);
        codec.sampling_frequency_index = sampling_frequency_index;
        codec.channel_configuration = channel_configuration;
        codec.frame_length = frame_length;
        
        // The aac sampleing rate defined in srs_aac_srates.
        // TODO: FIXME: maybe need to resample audio.
        codec.sound_format = 10; // AAC
        if (sampling_frequency_index <= 0x0c && sampling_frequency_index > 0x0a) {
            codec.sound_rate = SrsAudioSampleRate5512;
        } else if (sampling_frequency_index <= 0x0a && sampling_frequency_index > 0x07) {
            codec.sound_rate = SrsAudioSampleRate11025;
        } else if (sampling_frequency_index <= 0x07 && sampling_frequency_index > 0x04) {
            codec.sound_rate = SrsAudioSampleRate22050;
        } else if (sampling_frequency_index <= 0x04) {
            codec.sound_rate = SrsAudioSampleRate44100;
        } else {
            codec.sound_rate = SrsAudioSampleRate44100;
            srs_warn("adts invalid sample rate for flv, rate=%#x", sampling_frequency_index);
        }
        codec.sound_type = srs_max(0, srs_min(1, channel_configuration - 1));
        // TODO: FIXME: finger it out the sound size by adts.
        codec.sound_size = 1; // 0(8bits) or 1(16bits).
        
        // frame data.
        *pframe = stream->data() + stream->pos();
        *pnb_frame = raw_data_size;
        stream->skip(raw_data_size);
        
        break;
    }
    
    return err;
}

srs_error_t SrsRawAacStream::mux_sequence_header(SrsRawAacStreamCodec* codec, string& sh)
{
    srs_error_t err = srs_success;

    // only support aac profile 1-4.
    if (codec->aac_object == SrsAacObjectTypeReserved) {
        return srs_error_new(ERROR_AAC_DATA_INVALID, "invalid aac object");
    }
    
    SrsAacObjectType audioObjectType = codec->aac_object;
    char channelConfiguration = codec->channel_configuration;

    // Here we are generating AAC sequence header, the ASC structure,
    // because we have already parsed the sampling rate from AAC codec,
    // which is more precise than the sound_rate defined by RTMP.
    //
    // For example, AAC sampling_frequency_index is 3(48000HZ) or 4(44100HZ),
    // the sound_rate is always 3(44100HZ), if we covert sound_rate to
    // sampling_frequency_index, we may make mistake.
    uint8_t samplingFrequencyIndex = (uint8_t)codec->sampling_frequency_index;
    if (samplingFrequencyIndex >= SrsAacSampleRateUnset) {
        switch (codec->sound_rate) {
            case SrsAudioSampleRate5512:
                samplingFrequencyIndex = 0x0c; break;
            case SrsAudioSampleRate11025:
                samplingFrequencyIndex = 0x0a; break;
            case SrsAudioSampleRate22050:
                samplingFrequencyIndex = 0x07; break;
            case SrsAudioSampleRate44100:
                samplingFrequencyIndex = 0x04; break;
            default:
                break;
        }
    }
    if (samplingFrequencyIndex >= SrsAacSampleRateUnset) {
        return srs_error_new(ERROR_AAC_DATA_INVALID, "invalid sample index %d", samplingFrequencyIndex);
    }

    char chs[2];
    // @see ISO_IEC_14496-3-AAC-2001.pdf
    // AudioSpecificConfig (), page 33
    // 1.6.2.1 AudioSpecificConfig
    // audioObjectType; 5 bslbf
    chs[0] = (audioObjectType << 3) & 0xf8;
    // 3bits left.
    
    // samplingFrequencyIndex; 4 bslbf
    chs[0] |= (samplingFrequencyIndex >> 1) & 0x07;
    chs[1] = (samplingFrequencyIndex << 7) & 0x80;
    // 7bits left.
    
    // channelConfiguration; 4 bslbf
    chs[1] |= (channelConfiguration << 3) & 0x78;
    // 3bits left.
    
    // GASpecificConfig(), page 451
    // 4.4.1 Decoder configuration (GASpecificConfig)
    // frameLengthFlag; 1 bslbf
    // dependsOnCoreCoder; 1 bslbf
    // extensionFlag; 1 bslbf
    sh = string((char*)chs, sizeof(chs));
    
    return err;
}

srs_error_t SrsRawAacStream::mux_aac2flv(char* frame, int nb_frame, SrsRawAacStreamCodec* codec, uint32_t dts, char** flv, int* nb_flv)
{
    srs_error_t err = srs_success;
    
    char sound_format = codec->sound_format;
    char sound_type = codec->sound_type;
    char sound_size = codec->sound_size;
    char sound_rate = codec->sound_rate;
    char aac_packet_type = codec->aac_packet_type;
    
    // for audio frame, there is 1 or 2 bytes header:
    //      1bytes, SoundFormat|SoundRate|SoundSize|SoundType
    //      1bytes, AACPacketType for SoundFormat == 10, 0 is sequence header.
    int size = nb_frame + 1;
    if (sound_format == SrsAudioCodecIdAAC) {
        size += 1;
    }
    char* data = new char[size];
    char* p = data;
    
    uint8_t audio_header = sound_type & 0x01;
    audio_header |= (sound_size << 1) & 0x02;
    audio_header |= (sound_rate << 2) & 0x0c;
    audio_header |= (sound_format << 4) & 0xf0;
    
    *p++ = audio_header;
    
    if (sound_format == SrsAudioCodecIdAAC) {
        *p++ = aac_packet_type;
    }
    
    memcpy(p, frame, nb_frame);
    
    *flv = data;
    *nb_flv = size;
    
    return err;
}

