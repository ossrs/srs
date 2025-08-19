// Package av1 contains utilities to work with the AV1 codec.
package av1

const (
	// MaxTemporalUnitSize is the maximum size of a temporal unit.
	MaxTemporalUnitSize = 3 * 1024 * 1024

	// MaxOBUsPerTemporalUnit is the maximum number of OBUs per temporal unit.
	MaxOBUsPerTemporalUnit = 10
)
