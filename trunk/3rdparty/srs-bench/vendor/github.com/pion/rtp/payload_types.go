// SPDX-FileCopyrightText: 2024 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtp

// https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml
// https://en.wikipedia.org/wiki/RTP_payload_formats

// Audio Payload Types as defined in https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml
const (
	// PayloadTypePCMU is a payload type for ITU-T G.711 PCM μ-Law audio 64 kbit/s (RFC 3551).
	PayloadTypePCMU = 0
	// PayloadTypeGSM is a payload type for European GSM Full Rate audio 13 kbit/s (GSM 06.10).
	PayloadTypeGSM = 3
	// PayloadTypeG723 is a payload type for ITU-T G.723.1 audio (RFC 3551).
	PayloadTypeG723 = 4
	// PayloadTypeDVI4_8000 is a payload type for IMA ADPCM audio 32 kbit/s (RFC 3551).
	PayloadTypeDVI4_8000 = 5
	// PayloadTypeDVI4_16000 is a payload type for IMA ADPCM audio 64 kbit/s (RFC 3551).
	PayloadTypeDVI4_16000 = 6
	// PayloadTypeLPC is a payload type for Experimental Linear Predictive Coding audio 5.6 kbit/s (RFC 3551).
	PayloadTypeLPC = 7
	// PayloadTypePCMA is a payload type for ITU-T G.711 PCM A-Law audio 64 kbit/s (RFC 3551).
	PayloadTypePCMA = 8
	// PayloadTypeG722 is a payload type for ITU-T G.722 audio 64 kbit/s (RFC 3551).
	PayloadTypeG722 = 9
	// PayloadTypeL16Stereo is a payload type for Linear PCM 16-bit Stereo audio 1411.2 kbit/s, uncompressed (RFC 3551).
	PayloadTypeL16Stereo = 10
	// PayloadTypeL16Mono is a payload type for Linear PCM 16-bit audio 705.6 kbit/s, uncompressed (RFC 3551).
	PayloadTypeL16Mono = 11
	// PayloadTypeQCELP is a payload type for Qualcomm Code Excited Linear Prediction (RFC 2658, RFC 3551).
	PayloadTypeQCELP = 12
	// PayloadTypeCN is a payload type for Comfort noise (RFC 3389).
	PayloadTypeCN = 13
	// PayloadTypeMPA is a payload type for MPEG-1 or MPEG-2 audio only (RFC 3551, RFC 2250).
	PayloadTypeMPA = 14
	// PayloadTypeG728 is a payload type for ITU-T G.728 audio 16 kbit/s (RFC 3551).
	PayloadTypeG728 = 15
	// PayloadTypeDVI4_11025 is a payload type for IMA ADPCM audio 44.1 kbit/s (RFC 3551).
	PayloadTypeDVI4_11025 = 16
	// PayloadTypeDVI4_22050 is a payload type for IMA ADPCM audio 88.2 kbit/s (RFC 3551).
	PayloadTypeDVI4_22050 = 17
	// PayloadTypeG729 is a payload type for ITU-T G.729 and G.729a audio 8 kbit/s (RFC 3551, RFC 3555).
	PayloadTypeG729 = 18
)

// Video Payload Types as defined in https://www.iana.org/assignments/rtp-parameters/rtp-parameters.xhtml
const (
	// PayloadTypeCELLB is a payload type for Sun CellB video (RFC 2029).
	PayloadTypeCELLB = 25
	// PayloadTypeJPEG is a payload type for JPEG video (RFC 2435).
	PayloadTypeJPEG = 26
	// PayloadTypeNV is a payload type for Xerox PARC's Network Video (nv, RFC 3551).
	PayloadTypeNV = 28
	// PayloadTypeH261 is a payload type for ITU-T H.261 video (RFC 4587).
	PayloadTypeH261 = 31
	// PayloadTypeMPV is a payload type for MPEG-1 and MPEG-2 video (RFC 2250).
	PayloadTypeMPV = 32
	// PayloadTypeMP2T is a payload type for MPEG-2 transport stream (RFC 2250).
	PayloadTypeMP2T = 33
	// PayloadTypeH263 is a payload type for H.263 video, first version (1996, RFC 3551, RFC 2190).
	PayloadTypeH263 = 34
)

const (
	// PayloadTypeFirstDynamic is a first non-static payload type.
	PayloadTypeFirstDynamic = 35
)
