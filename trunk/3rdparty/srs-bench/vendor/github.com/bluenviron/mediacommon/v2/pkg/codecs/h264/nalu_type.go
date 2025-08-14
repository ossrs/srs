package h264

import (
	"fmt"
)

// NALUType is the type of a NALU.
// Specification: ITU-T Rec. H.264, Table 7-1
type NALUType uint8

// NALU types.
const (
	NALUTypeNonIDR                        NALUType = 1
	NALUTypeDataPartitionA                NALUType = 2
	NALUTypeDataPartitionB                NALUType = 3
	NALUTypeDataPartitionC                NALUType = 4
	NALUTypeIDR                           NALUType = 5
	NALUTypeSEI                           NALUType = 6
	NALUTypeSPS                           NALUType = 7
	NALUTypePPS                           NALUType = 8
	NALUTypeAccessUnitDelimiter           NALUType = 9
	NALUTypeEndOfSequence                 NALUType = 10
	NALUTypeEndOfStream                   NALUType = 11
	NALUTypeFillerData                    NALUType = 12
	NALUTypeSPSExtension                  NALUType = 13
	NALUTypePrefix                        NALUType = 14
	NALUTypeSubsetSPS                     NALUType = 15
	NALUTypeReserved16                    NALUType = 16
	NALUTypeReserved17                    NALUType = 17
	NALUTypeReserved18                    NALUType = 18
	NALUTypeSliceLayerWithoutPartitioning NALUType = 19
	NALUTypeSliceExtension                NALUType = 20
	NALUTypeSliceExtensionDepth           NALUType = 21
	NALUTypeReserved22                    NALUType = 22
	NALUTypeReserved23                    NALUType = 23

	// additional NALU types for RTP/H264
	NALUTypeSTAPA  NALUType = 24
	NALUTypeSTAPB  NALUType = 25
	NALUTypeMTAP16 NALUType = 26
	NALUTypeMTAP24 NALUType = 27
	NALUTypeFUA    NALUType = 28
	NALUTypeFUB    NALUType = 29
)

var naluTypeLabels = map[NALUType]string{
	NALUTypeNonIDR:                        "NonIDR",
	NALUTypeDataPartitionA:                "DataPartitionA",
	NALUTypeDataPartitionB:                "DataPartitionB",
	NALUTypeDataPartitionC:                "DataPartitionC",
	NALUTypeIDR:                           "IDR",
	NALUTypeSEI:                           "SEI",
	NALUTypeSPS:                           "SPS",
	NALUTypePPS:                           "PPS",
	NALUTypeAccessUnitDelimiter:           "AccessUnitDelimiter",
	NALUTypeEndOfSequence:                 "EndOfSequence",
	NALUTypeEndOfStream:                   "EndOfStream",
	NALUTypeFillerData:                    "FillerData",
	NALUTypeSPSExtension:                  "SPSExtension",
	NALUTypePrefix:                        "Prefix",
	NALUTypeSubsetSPS:                     "SubsetSPS",
	NALUTypeReserved16:                    "Reserved16",
	NALUTypeReserved17:                    "Reserved17",
	NALUTypeReserved18:                    "Reserved18",
	NALUTypeSliceLayerWithoutPartitioning: "SliceLayerWithoutPartitioning",
	NALUTypeSliceExtension:                "SliceExtension",
	NALUTypeSliceExtensionDepth:           "SliceExtensionDepth",
	NALUTypeReserved22:                    "Reserved22",
	NALUTypeReserved23:                    "Reserved23",
	NALUTypeSTAPA:                         "STAP-A",
	NALUTypeSTAPB:                         "STAP-B",
	NALUTypeMTAP16:                        "MTAP-16",
	NALUTypeMTAP24:                        "MTAP-24",
	NALUTypeFUA:                           "FU-A",
	NALUTypeFUB:                           "FU-B",
}

// String implements fmt.Stringer.
func (nt NALUType) String() string {
	if l, ok := naluTypeLabels[nt]; ok {
		return l
	}
	return fmt.Sprintf("unknown (%d)", nt)
}
