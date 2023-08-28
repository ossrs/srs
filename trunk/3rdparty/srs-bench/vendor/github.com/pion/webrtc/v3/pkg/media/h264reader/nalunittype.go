// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package h264reader

import "strconv"

// NalUnitType is the type of a NAL
type NalUnitType uint8

// Enums for NalUnitTypes
const (
	NalUnitTypeUnspecified              NalUnitType = 0  // Unspecified
	NalUnitTypeCodedSliceNonIdr         NalUnitType = 1  // Coded slice of a non-IDR picture
	NalUnitTypeCodedSliceDataPartitionA NalUnitType = 2  // Coded slice data partition A
	NalUnitTypeCodedSliceDataPartitionB NalUnitType = 3  // Coded slice data partition B
	NalUnitTypeCodedSliceDataPartitionC NalUnitType = 4  // Coded slice data partition C
	NalUnitTypeCodedSliceIdr            NalUnitType = 5  // Coded slice of an IDR picture
	NalUnitTypeSEI                      NalUnitType = 6  // Supplemental enhancement information (SEI)
	NalUnitTypeSPS                      NalUnitType = 7  // Sequence parameter set
	NalUnitTypePPS                      NalUnitType = 8  // Picture parameter set
	NalUnitTypeAUD                      NalUnitType = 9  // Access unit delimiter
	NalUnitTypeEndOfSequence            NalUnitType = 10 // End of sequence
	NalUnitTypeEndOfStream              NalUnitType = 11 // End of stream
	NalUnitTypeFiller                   NalUnitType = 12 // Filler data
	NalUnitTypeSpsExt                   NalUnitType = 13 // Sequence parameter set extension
	NalUnitTypeCodedSliceAux            NalUnitType = 19 // Coded slice of an auxiliary coded picture without partitioning
	// 14..18                                            // Reserved
	// 20..23                                            // Reserved
	// 24..31                                            // Unspecified
)

func (n *NalUnitType) String() string {
	var str string
	switch *n {
	case NalUnitTypeUnspecified:
		str = "Unspecified"
	case NalUnitTypeCodedSliceNonIdr:
		str = "CodedSliceNonIdr"
	case NalUnitTypeCodedSliceDataPartitionA:
		str = "CodedSliceDataPartitionA"
	case NalUnitTypeCodedSliceDataPartitionB:
		str = "CodedSliceDataPartitionB"
	case NalUnitTypeCodedSliceDataPartitionC:
		str = "CodedSliceDataPartitionC"
	case NalUnitTypeCodedSliceIdr:
		str = "CodedSliceIdr"
	case NalUnitTypeSEI:
		str = "SEI"
	case NalUnitTypeSPS:
		str = "SPS"
	case NalUnitTypePPS:
		str = "PPS"
	case NalUnitTypeAUD:
		str = "AUD"
	case NalUnitTypeEndOfSequence:
		str = "EndOfSequence"
	case NalUnitTypeEndOfStream:
		str = "EndOfStream"
	case NalUnitTypeFiller:
		str = "Filler"
	case NalUnitTypeSpsExt:
		str = "SpsExt"
	case NalUnitTypeCodedSliceAux:
		str = "NalUnitTypeCodedSliceAux"
	default:
		str = "Unknown"
	}
	str = str + "(" + strconv.FormatInt(int64(*n), 10) + ")"
	return str
}
