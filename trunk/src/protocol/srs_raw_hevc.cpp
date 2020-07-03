#include "srs_raw_hevc.hpp"
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <string.h>
using namespace std;

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define BIT(ptr, off) (((ptr)[(off) / 8] >> (7 - ((off) % 8))) & 0x01)

struct mpeg4_hevc_t
{
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
	struct
	{
		uint8_t array_completeness;
		uint8_t type; // nalu type
		uint16_t bytes;
		uint8_t* data;
	} nalu[64];

	uint8_t array_completeness;
	uint8_t data[4 * 1024];
	int off;
};

static uint8_t mpeg4_h264_read_ue(const uint8_t* data, int bytes, int* offset)
{
	int bit, i;
	int leadingZeroBits = -1;

	for (bit = 0; !bit && *offset / 8 < bytes; ++leadingZeroBits)
	{
		bit = (data[*offset / 8] >> (7 - (*offset % 8))) & 0x01;
		++*offset;
	}

	bit = 0;
	assert(leadingZeroBits < 32);
	for (i = 0; i < leadingZeroBits && *offset / 8 < bytes; i++)
	{
		bit = (bit << 1) | ((data[*offset / 8] >> (7 - (*offset % 8))) & 0x01);
		++*offset;
	}

	return (uint8_t)((1 << leadingZeroBits) - 1 + bit);
}

static int hevc_rbsp_decode(const uint8_t* nalu, int bytes, uint8_t* sodb)
{
	int i, j;
	for (j = i = 0; i < bytes; i++)
	{
		if (i + 2 < bytes && 0 == nalu[i] && 0 == nalu[i + 1] && 0x03 == nalu[i + 2])
		{
			sodb[j++] = nalu[i];
			sodb[j++] = nalu[i + 1];
			i += 2;
		}
		else
		{
			sodb[j++] = nalu[i];
		}
	}
	return j;
}

static int hevc_profile_tier_level(const uint8_t* nalu, int bytes, uint8_t maxNumSubLayersMinus1, struct mpeg4_hevc_t* hevc)
{
	int n;
	uint8_t i;
	uint8_t sub_layer_profile_present_flag[8];
	uint8_t sub_layer_level_present_flag[8];

	if (bytes < 12)
		return -1;

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
	if (maxNumSubLayersMinus1 < 1)
		return 12;

	if (bytes < 14)
		return -1; // error

	for (i = 0; i < maxNumSubLayersMinus1; i++)
	{
		sub_layer_profile_present_flag[i] = BIT(nalu, 12 * 8 + i * 2);
		sub_layer_level_present_flag[i] = BIT(nalu, 12 * 8 + i * 2 + 1);
	}

	n = 12 + 2;
	for (i = 0; i < maxNumSubLayersMinus1; i++)
	{
		if(sub_layer_profile_present_flag[i])
			n += 11;
		if (sub_layer_level_present_flag[i])
			n += 1;
	}

	return bytes < n ? n : -1;
}

static uint8_t hevc_vps_id(const uint8_t* rbsp, int bytes, struct mpeg4_hevc_t* hevc, uint8_t* ptr)
{
	int sodb;
	uint8_t vps;
	uint8_t vps_max_sub_layers_minus1;
	uint8_t vps_temporal_id_nesting_flag;

	sodb = hevc_rbsp_decode(rbsp, bytes, ptr);
	if (sodb < 16 + 2)
		return 0xFF;

	vps = ptr[2] >> 4;  // 2-nalu type
	vps_max_sub_layers_minus1 = (ptr[3] >> 1) & 0x07;
	vps_temporal_id_nesting_flag = ptr[3] & 0x01;
	hevc->numTemporalLayers = MAX(hevc->numTemporalLayers, vps_max_sub_layers_minus1 + 1);
	hevc->temporalIdNested = (hevc->temporalIdNested || vps_temporal_id_nesting_flag) ? 1 : 0;
	hevc_profile_tier_level(ptr + 6, sodb - 6, vps_max_sub_layers_minus1, hevc);

	return vps;
}

static uint8_t hevc_sps_id(const uint8_t* rbsp, int bytes, struct mpeg4_hevc_t* hevc, uint8_t* ptr, uint8_t* vps)
{
	int n;
	int sodb;
	uint8_t sps;
	uint8_t sps_max_sub_layers_minus1;
	uint8_t sps_temporal_id_nesting_flag;
	uint8_t conformance_window_flag;

	sodb = hevc_rbsp_decode(rbsp, bytes, ptr);
	if (sodb < 12+3)
		return 0xFF;

	*vps = ptr[2] >> 4;  // 2-nalu type
	sps_max_sub_layers_minus1 = (ptr[2] >> 1) & 0x07;
	sps_temporal_id_nesting_flag = ptr[2] & 0x01;
    (void)sps_temporal_id_nesting_flag;
	n = hevc_profile_tier_level(ptr + 3, sodb - 3, sps_max_sub_layers_minus1, hevc);
	if (n <= 0)
		return 0xFF;

	n = (n + 3) * 8;
	sps = mpeg4_h264_read_ue(ptr, sodb, &n);
	hevc->chromaFormat = mpeg4_h264_read_ue(ptr, sodb, &n);
	if (3 == hevc->chromaFormat)
		n++;
	mpeg4_h264_read_ue(ptr, sodb, &n); // pic_width_in_luma_samples
	mpeg4_h264_read_ue(ptr, sodb, &n); // pic_height_in_luma_samples
	conformance_window_flag = BIT(ptr, n); n++; // conformance_window_flag
	if (conformance_window_flag)
	{
		mpeg4_h264_read_ue(ptr, sodb, &n); // conf_win_left_offset
		mpeg4_h264_read_ue(ptr, sodb, &n); // conf_win_right_offset
		mpeg4_h264_read_ue(ptr, sodb, &n); // conf_win_top_offset
		mpeg4_h264_read_ue(ptr, sodb, &n); // conf_win_bottom_offset
	}
	hevc->bitDepthLumaMinus8 = mpeg4_h264_read_ue(ptr, sodb, &n);
	hevc->bitDepthChromaMinus8 = mpeg4_h264_read_ue(ptr, sodb, &n);

	// TODO: vui_parameters
	//mp4->hevc->min_spatial_segmentation_idc; // min_spatial_segmentation_idc
	return sps;
}

static uint8_t hevc_pps_id(const uint8_t* rbsp, int bytes, struct mpeg4_hevc_t* hevc, uint8_t* ptr, uint8_t* sps)
{
	// TODO:
	//hevc->parallelismType; // entropy_coding_sync_enabled_flag
	(void)hevc;

	int sodb;
	int offset = 2 * 8;  // 2-nalu type
	sodb = hevc_rbsp_decode(rbsp, bytes, ptr);
	if (sodb < 3)
		return 0xFF;
	*sps = mpeg4_h264_read_ue(ptr, sodb, &offset);
	return mpeg4_h264_read_ue(ptr, sodb, &offset);
}

static void mpeg4_hevc_remove(struct mpeg4_hevc_t* hevc, uint8_t* ptr, int bytes, const uint8_t* end)
{
	uint8_t i;
	assert(ptr >= hevc->data && ptr + bytes <= end && end <= hevc->data + sizeof(hevc->data));
	memmove(ptr, ptr + bytes, end - ptr - bytes);

	for (i = 0; i < hevc->numOfArrays; i++)
	{
		if (hevc->nalu[i].data > ptr)
			hevc->nalu[i].data -= bytes;
	}
}

static int mpeg4_hevc_update2(struct mpeg4_hevc_t* hevc, int i, const uint8_t* nalu, int bytes)
{
	if (bytes == hevc->nalu[i].bytes && 0 == memcmp(nalu, hevc->nalu[i].data, bytes))
		return 0; // do nothing

	if (bytes > hevc->nalu[i].bytes && hevc->off + (bytes - hevc->nalu[i].bytes) > (int)sizeof(hevc->data))
	{
		assert(0);
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
	assert(hevc->numOfArrays < (int)sizeof(hevc->nalu) / (int)sizeof(hevc->nalu[0]));
	if (hevc->numOfArrays >= (int)sizeof(hevc->nalu) / (int)sizeof(hevc->nalu[0])
		|| hevc->off + bytes > (int)sizeof(hevc->data))
	{
		assert(0);
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
	int i;
	uint8_t vpsid;

	if (bytes < 3)
	{
		assert(0);
		return -1; // invalid length
	}

	vpsid = hevc_vps_id(nalu, bytes, hevc, hevc->data + hevc->off);
	for (i = 0; i < hevc->numOfArrays; i++)
	{
		if (NAL_UNIT_VPS == hevc->nalu[i].type && vpsid == hevc_vps_id(hevc->nalu[i].data, hevc->nalu[i].bytes, hevc, hevc->data + hevc->off))
			return mpeg4_hevc_update2(hevc, i, nalu, bytes);
	}

	return mpeg4_hevc_add(hevc, NAL_UNIT_VPS, nalu, bytes);
}

static int h265_sps_copy(struct mpeg4_hevc_t* hevc, const uint8_t* nalu, int bytes)
{
	int i;
	uint8_t spsid;
	uint8_t vpsid, vpsid2;

	if (bytes < 13 + 2)
	{
		assert(0);
		return -1; // invalid length
	}

	spsid = hevc_sps_id(nalu, bytes, hevc, hevc->data + hevc->off, &vpsid);
	for (i = 0; i < hevc->numOfArrays; i++)
	{
		if (NAL_UNIT_SPS == hevc->nalu[i].type && spsid == hevc_sps_id(hevc->nalu[i].data, hevc->nalu[i].bytes, hevc, hevc->data + hevc->off, &vpsid2) && vpsid == vpsid2)
			return mpeg4_hevc_update2(hevc, i, nalu, bytes);
	}

	return mpeg4_hevc_add(hevc, NAL_UNIT_SPS, nalu, bytes);
}

static int h265_pps_copy(struct mpeg4_hevc_t* hevc, const uint8_t* nalu, int bytes)
{
	int i;
	uint8_t ppsid;
	uint8_t spsid, spsid2;

	if (bytes < 1 + 2)
	{
		assert(0);
		return -1; // invalid length
	}

	ppsid = hevc_pps_id(nalu, bytes, hevc, hevc->data + hevc->off, &spsid);
	for (i = 0; i < hevc->numOfArrays; i++)
	{
		if (NAL_UNIT_PPS == hevc->nalu[i].type && ppsid == hevc_pps_id(hevc->nalu[i].data, hevc->nalu[i].bytes, hevc, hevc->data + hevc->off, &spsid2) && spsid == spsid2)
			return mpeg4_hevc_update2(hevc, i, nalu, bytes);
	}

	return mpeg4_hevc_add(hevc, NAL_UNIT_PPS, nalu, bytes);
}

SrsRawHEVCStream::SrsRawHEVCStream() {

}

SrsRawHEVCStream::~SrsRawHEVCStream() {

}

srs_error_t SrsRawHEVCStream::annexb_demux(SrsBuffer* stream, char** pframe, int* pnb_frame)
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

// whether the frame is sps or pps or vps.
bool SrsRawHEVCStream::is_sps(char* frame, int nb_frame)
{
    srs_assert(nb_frame > 0);
    uint8_t nal_unit_type = HEVC_NALU_TYPE(frame[0]);
    
    return nal_unit_type == NAL_UNIT_SPS;
}

bool SrsRawHEVCStream::is_pps(char* frame, int nb_frame) {
    srs_assert(nb_frame > 0);
    uint8_t nal_unit_type = HEVC_NALU_TYPE(frame[0]);
    
    return nal_unit_type == NAL_UNIT_PPS;
}

bool SrsRawHEVCStream::is_vps(char* frame, int nb_frame) {
    srs_assert(nb_frame > 0);
    uint8_t nal_unit_type = HEVC_NALU_TYPE(frame[0]);
    
    return nal_unit_type == NAL_UNIT_VPS;
}

srs_error_t SrsRawHEVCStream::sps_demux(char* frame, int nb_frame, std::string& sps)
{
    srs_error_t err = srs_success;
    
    // atleast 1bytes for SPS to decode the type, profile, constrain and level.
    if (nb_frame < 4) {
        return err;
    }

    sps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::pps_demux(char* frame, int nb_frame, std::string& pps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_PPS, "no pps");
    }

    pps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::vps_demux(char* frame, int nb_frame, std::string& vps)
{
    srs_error_t err = srs_success;

    if (nb_frame <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_VPS, "no vps");
    }

    vps = string(frame, nb_frame);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_sequence_header(std::string sps, std::string pps, std::string vps,
                                        uint32_t dts, uint32_t pts, std::string& hvcC)
{
    srs_error_t err = srs_success;
    uint8_t temp8bits = 0;
    struct mpeg4_hevc_t hevc_info;

    memset(&hevc_info, 0, sizeof(hevc_info));

    if (h265_vps_copy(&hevc_info, (uint8_t*)vps.data(), (int)vps.length()) < 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_VPS, "decode vps error");
    }

    if (h265_sps_copy(&hevc_info, (uint8_t*)sps.data(), (int)sps.length()) < 0) {
        return srs_error_new(ERROR_STREAM_CASTER_HEVC_SPS, "decode sps error");
    }

    if (h265_pps_copy(&hevc_info, (uint8_t*)pps.data(), (int)pps.length()) < 0) {
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


    //use simple mode: nalu size + nalu data
    int nb_packet = 23 + 5 + (int)sps.length() + 5 + (int)pps.length() + 5 + (int)vps.length();
    char* packet = new char[nb_packet];
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

    stream.write_1bytes(0xfc|(hevc_info.parallelismType & 0x03));
    stream.write_1bytes(0xfc|(hevc_info.chromaFormat & 0x03));

    stream.write_1bytes(0xf8|(hevc_info.bitDepthLumaMinus8 & 0x07));

    stream.write_1bytes(0xf8|(hevc_info.bitDepthChromaMinus8 & 0x07));

    stream.write_2bytes(hevc_info.avgFrameRate);

    hevc_info.lengthSizeMinusOne = 3;
    temp8bits = 0;
    temp8bits |= (hevc_info.constantFrameRate << 6) | 0xc0;
    temp8bits |= (hevc_info.numTemporalLayers << 3) | 0x38;
    temp8bits |= (hevc_info.temporalIdNested << 2) | 0x04;
    temp8bits |= hevc_info.lengthSizeMinusOne & 0x03;

    stream.write_1bytes(temp8bits);
    
    uint8_t numOfArrays = 3;//vps,sps,pps
    stream.write_1bytes(numOfArrays);

    uint8_t array_completeness = 0;//1bit
    //uint8_t reserved = 0;//1bit
    uint8_t nal_unit_type = 0;//6bits;

    //vps
    nal_unit_type = ((array_completeness << 7) & 0x80) | (NAL_UNIT_VPS & 0x3f);
    stream.write_1bytes(nal_unit_type);

    uint16_t namNalus = 1;
    stream.write_2bytes(namNalus);

    uint16_t nalUnitLength = vps.length();
    stream.write_2bytes(nalUnitLength);

    stream.write_string(vps);

    //sps
    nal_unit_type = ((array_completeness << 7) & 0x80) | (NAL_UNIT_SPS & 0x3f);
    stream.write_1bytes(nal_unit_type);

    namNalus = 1;
    stream.write_2bytes(namNalus);

    
    nalUnitLength = sps.length();
    stream.write_2bytes(nalUnitLength);

    stream.write_string(sps);

    //pps
    nal_unit_type = ((array_completeness << 7) & 0x80) | (NAL_UNIT_PPS & 0x3f);
    stream.write_1bytes(nal_unit_type);

    namNalus = 1;
    stream.write_2bytes(namNalus);

    nalUnitLength = pps.length();
    stream.write_2bytes(nalUnitLength);
    stream.write_string(pps);

    hvcC = string(packet, nb_packet);

    return err;
}

srs_error_t SrsRawHEVCStream::mux_ipb_frame(char* frame, int nb_frame, std::string& ibp)
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

srs_error_t SrsRawHEVCStream::mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char** flv, int* nb_flv)
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
    *p++ = (frame_type << 4) | SrsVideoCodecIdHEVC;
    
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
    
    // hevc raw data.
    memcpy(p, video.data(), video.length());
    
    *flv = data;
    *nb_flv = size;
    
    return err;
}