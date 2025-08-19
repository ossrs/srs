package h265

import (
	"fmt"
)

// NALUType is the type of a NALU.
// Specification: ITU-T Rec. H.265, Table 7-1
type NALUType uint8

// NALU types.
const (
	NALUType_TRAIL_N        NALUType = 0  //nolint:revive
	NALUType_TRAIL_R        NALUType = 1  //nolint:revive
	NALUType_TSA_N          NALUType = 2  //nolint:revive
	NALUType_TSA_R          NALUType = 3  //nolint:revive
	NALUType_STSA_N         NALUType = 4  //nolint:revive
	NALUType_STSA_R         NALUType = 5  //nolint:revive
	NALUType_RADL_N         NALUType = 6  //nolint:revive
	NALUType_RADL_R         NALUType = 7  //nolint:revive
	NALUType_RASL_N         NALUType = 8  //nolint:revive
	NALUType_RASL_R         NALUType = 9  //nolint:revive
	NALUType_RSV_VCL_N10    NALUType = 10 //nolint:revive
	NALUType_RSV_VCL_N12    NALUType = 12 //nolint:revive
	NALUType_RSV_VCL_N14    NALUType = 14 //nolint:revive
	NALUType_RSV_VCL_R11    NALUType = 11 //nolint:revive
	NALUType_RSV_VCL_R13    NALUType = 13 //nolint:revive
	NALUType_RSV_VCL_R15    NALUType = 15 //nolint:revive
	NALUType_BLA_W_LP       NALUType = 16 //nolint:revive
	NALUType_BLA_W_RADL     NALUType = 17 //nolint:revive
	NALUType_BLA_N_LP       NALUType = 18 //nolint:revive
	NALUType_IDR_W_RADL     NALUType = 19 //nolint:revive
	NALUType_IDR_N_LP       NALUType = 20 //nolint:revive
	NALUType_CRA_NUT        NALUType = 21 //nolint:revive
	NALUType_RSV_IRAP_VCL22 NALUType = 22 //nolint:revive
	NALUType_RSV_IRAP_VCL23 NALUType = 23 //nolint:revive
	NALUType_VPS_NUT        NALUType = 32 //nolint:revive
	NALUType_SPS_NUT        NALUType = 33 //nolint:revive
	NALUType_PPS_NUT        NALUType = 34 //nolint:revive
	NALUType_AUD_NUT        NALUType = 35 //nolint:revive
	NALUType_EOS_NUT        NALUType = 36 //nolint:revive
	NALUType_EOB_NUT        NALUType = 37 //nolint:revive
	NALUType_FD_NUT         NALUType = 38 //nolint:revive
	NALUType_PREFIX_SEI_NUT NALUType = 39 //nolint:revive
	NALUType_SUFFIX_SEI_NUT NALUType = 40 //nolint:revive

	// additional NALU types for RTP/H265
	NALUType_AggregationUnit   NALUType = 48 //nolint:revive
	NALUType_FragmentationUnit NALUType = 49 //nolint:revive
	NALUType_PACI              NALUType = 50 //nolint:revive
)

var naluTypeLabels = map[NALUType]string{
	NALUType_TRAIL_N:        "TRAIL_N",
	NALUType_TRAIL_R:        "TRAIL_R",
	NALUType_TSA_N:          "TSA_N",
	NALUType_TSA_R:          "TSA_R",
	NALUType_STSA_N:         "STSA_N",
	NALUType_STSA_R:         "STSA_R:",
	NALUType_RADL_N:         "RADL_N",
	NALUType_RADL_R:         "RADL_R",
	NALUType_RASL_N:         "RASL_N",
	NALUType_RASL_R:         "RASL_R",
	NALUType_RSV_VCL_N10:    "RSV_VCL_N10",
	NALUType_RSV_VCL_N12:    "RSV_VCL_N12",
	NALUType_RSV_VCL_N14:    "RSV_VCL_N14",
	NALUType_RSV_VCL_R11:    "RSV_VCL_R11",
	NALUType_RSV_VCL_R13:    "RSV_VCL_R13",
	NALUType_RSV_VCL_R15:    "RSV_VCL_R15",
	NALUType_BLA_W_LP:       "BLA_W_LP",
	NALUType_BLA_W_RADL:     "BLA_W_RADL",
	NALUType_BLA_N_LP:       "BLA_N_LP",
	NALUType_IDR_W_RADL:     "IDR_W_RADL",
	NALUType_IDR_N_LP:       "IDR_N_LP",
	NALUType_CRA_NUT:        "CRA_NUT",
	NALUType_RSV_IRAP_VCL22: "RSV_IRAP_VCL22",
	NALUType_RSV_IRAP_VCL23: "RSV_IRAP_VCL23",
	NALUType_VPS_NUT:        "VPS_NUT",
	NALUType_SPS_NUT:        "SPS_NUT",
	NALUType_PPS_NUT:        "PPS_NUT",
	NALUType_AUD_NUT:        "AUD_NUT",
	NALUType_EOS_NUT:        "EOS_NUT",
	NALUType_EOB_NUT:        "EOB_NUT",
	NALUType_FD_NUT:         "FD_NUT",
	NALUType_PREFIX_SEI_NUT: "PrefixSEINUT",
	NALUType_SUFFIX_SEI_NUT: "SuffixSEINUT",

	// additional NALU types for RTP/H265
	NALUType_AggregationUnit:   "AggregationUnit",
	NALUType_FragmentationUnit: "FragmentationUnit",
	NALUType_PACI:              "PACI",
}

// String implements fmt.Stringer.
func (nt NALUType) String() string {
	if l, ok := naluTypeLabels[nt]; ok {
		return l
	}
	return fmt.Sprintf("unknown (%d)", nt)
}
