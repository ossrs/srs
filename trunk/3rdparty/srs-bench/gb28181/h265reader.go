// Package gb28181 provides GB28181 protocol support
package gb28181

import (
	"io"

	"github.com/pion/webrtc/v4/pkg/media/h265reader"
)

// Type aliases for compatibility with existing code
type H265Reader = h265reader.H265Reader
type NAL = h265reader.NAL
type NalUnitType = h265reader.NalUnitType

// NAL unit type constants for compatibility with existing code
const (
	NaluTypeSliceTrailN = h265reader.NalUnitTypeTrailN
	NaluTypeSliceTrailR = h265reader.NalUnitTypeTrailR
	NaluTypeSliceTsaN   = h265reader.NalUnitTypeTsaN
	NaluTypeSliceTsaR   = h265reader.NalUnitTypeTsaR
	NaluTypeSliceStsaN  = h265reader.NalUnitTypeStsaN
	NaluTypeSliceStsaR  = h265reader.NalUnitTypeStsaR
	NaluTypeSliceRadlN  = h265reader.NalUnitTypeRadlN
	NaluTypeSliceRadlR  = h265reader.NalUnitTypeRadlR
	NaluTypeSliceRaslN  = h265reader.NalUnitTypeRaslN
	NaluTypeSliceRaslR  = h265reader.NalUnitTypeRaslR

	NaluTypeSliceBlaWlp       = h265reader.NalUnitTypeBlaWLp
	NaluTypeSliceBlaWradl     = h265reader.NalUnitTypeBlaWRadl
	NaluTypeSliceBlaNlp       = h265reader.NalUnitTypeBlaNLp
	NaluTypeSliceIdr          = h265reader.NalUnitTypeIdrWRadl
	NaluTypeSliceIdrNlp       = h265reader.NalUnitTypeIdrNLp
	NaluTypeSliceCranut       = h265reader.NalUnitTypeCraNut
	NaluTypeSliceRsvIrapVcl22 = h265reader.NalUnitTypeReserved41 // Approximate mapping
	NaluTypeSliceRsvIrapVcl23 = h265reader.NalUnitTypeReserved47 // Approximate mapping

	NaluTypeVps       = h265reader.NalUnitTypeVps
	NaluTypeSps       = h265reader.NalUnitTypeSps
	NaluTypePps       = h265reader.NalUnitTypePps
	NaluTypeAud       = h265reader.NalUnitTypeAud
	NaluTypeSei       = h265reader.NalUnitTypePrefixSei
	NaluTypeSeiSuffix = h265reader.NalUnitTypeSuffixSei

	NaluTypeUnspecified = h265reader.NalUnitTypeUnspec48
)

// NewReader creates new H265Reader using Pion's implementation
func NewReader(in io.Reader) (*H265Reader, error) {
	return h265reader.NewReader(in)
}
