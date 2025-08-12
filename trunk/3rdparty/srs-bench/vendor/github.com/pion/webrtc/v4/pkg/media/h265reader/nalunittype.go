// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package h265reader

import "strconv"

// NalUnitType is the type of a NAL unit in H.265/HEVC.
type NalUnitType uint8

// Enums for H.265/HEVC NAL unit types.
const (
	// VCL NAL unit types.
	NalUnitTypeTrailN   NalUnitType = 0  // Coded slice segment of a non-TSA, non-STSA trailing picture
	NalUnitTypeTrailR   NalUnitType = 1  // Coded slice segment of a non-TSA, non-STSA trailing picture
	NalUnitTypeTsaN     NalUnitType = 2  // Coded slice segment of a TSA picture
	NalUnitTypeTsaR     NalUnitType = 3  // Coded slice segment of a TSA picture
	NalUnitTypeStsaN    NalUnitType = 4  // Coded slice segment of an STSA picture
	NalUnitTypeStsaR    NalUnitType = 5  // Coded slice segment of an STSA picture
	NalUnitTypeRadlN    NalUnitType = 6  // Coded slice segment of a RADL picture
	NalUnitTypeRadlR    NalUnitType = 7  // Coded slice segment of a RADL picture
	NalUnitTypeRaslN    NalUnitType = 8  // Coded slice segment of a RASL picture
	NalUnitTypeRaslR    NalUnitType = 9  // Coded slice segment of a RASL picture
	NalUnitTypeBlaWLp   NalUnitType = 16 // Coded slice segment of a BLA picture
	NalUnitTypeBlaWRadl NalUnitType = 17 // Coded slice segment of a BLA picture
	NalUnitTypeBlaNLp   NalUnitType = 18 // Coded slice segment of a BLA picture
	NalUnitTypeIdrWRadl NalUnitType = 19 // Coded slice segment of an IDR picture
	NalUnitTypeIdrNLp   NalUnitType = 20 // Coded slice segment of an IDR picture
	NalUnitTypeCraNut   NalUnitType = 21 // Coded slice segment of a CRA picture

	// Non-VCL NAL unit types.
	NalUnitTypeVps       NalUnitType = 32 // Video parameter set
	NalUnitTypeSps       NalUnitType = 33 // Sequence parameter set
	NalUnitTypePps       NalUnitType = 34 // Picture parameter set
	NalUnitTypeAud       NalUnitType = 35 // Access unit delimiter
	NalUnitTypeEos       NalUnitType = 36 // End of sequence
	NalUnitTypeEob       NalUnitType = 37 // End of bitstream
	NalUnitTypeFd        NalUnitType = 38 // Filler data
	NalUnitTypePrefixSei NalUnitType = 39 // Supplemental enhancement information
	NalUnitTypeSuffixSei NalUnitType = 40 // Supplemental enhancement information

	// Reserved.
	NalUnitTypeReserved41 NalUnitType = 41
	NalUnitTypeReserved47 NalUnitType = 47
	NalUnitTypeUnspec48   NalUnitType = 48
	NalUnitTypeUnspec63   NalUnitType = 63
)

func (n *NalUnitType) String() string { //nolint:cyclop
	var str string
	switch *n {
	case NalUnitTypeTrailN:
		str = "TrailN"
	case NalUnitTypeTrailR:
		str = "TrailR"
	case NalUnitTypeTsaN:
		str = "TsaN"
	case NalUnitTypeTsaR:
		str = "TsaR"
	case NalUnitTypeStsaN:
		str = "StsaN"
	case NalUnitTypeStsaR:
		str = "StsaR"
	case NalUnitTypeRadlN:
		str = "RadlN"
	case NalUnitTypeRadlR:
		str = "RadlR"
	case NalUnitTypeRaslN:
		str = "RaslN"
	case NalUnitTypeRaslR:
		str = "RaslR"
	case NalUnitTypeBlaWLp:
		str = "BlaWLp"
	case NalUnitTypeBlaWRadl:
		str = "BlaWRadl"
	case NalUnitTypeBlaNLp:
		str = "BlaNLp"
	case NalUnitTypeIdrWRadl:
		str = "IdrWRadl"
	case NalUnitTypeIdrNLp:
		str = "IdrNLp"
	case NalUnitTypeCraNut:
		str = "CraNut"
	case NalUnitTypeVps:
		str = "VPS"
	case NalUnitTypeSps:
		str = "SPS"
	case NalUnitTypePps:
		str = "PPS"
	case NalUnitTypeAud:
		str = "AUD"
	case NalUnitTypeEos:
		str = "EOS"
	case NalUnitTypeEob:
		str = "EOB"
	case NalUnitTypeFd:
		str = "FD"
	case NalUnitTypePrefixSei:
		str = "PrefixSEI"
	case NalUnitTypeSuffixSei:
		str = "SuffixSEI"
	default:
		switch {
		case *n >= NalUnitTypeReserved41 && *n <= NalUnitTypeReserved47:
			str = "Reserved"
		case *n >= NalUnitTypeUnspec48 && *n <= NalUnitTypeUnspec63:
			str = "Unspecified"
		default:
			str = "Unknown"
		}
	}
	str = str + "(" + strconv.FormatInt(int64(*n), 10) + ")"

	return str
}
