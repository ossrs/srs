// Package h265 contains utilities to work with the H265 codec.
package h265

const (
	// MaxAccessUnitSize is the maximum size of an access unit.
	// With a 50 Mbps 2160p60 H265 video, the maximum size does not seem to exceed 8 MiB.
	MaxAccessUnitSize = 8 * 1024 * 1024

	// MaxNALUsPerAccessUnit is the maximum number of NALUs per access unit.
	MaxNALUsPerAccessUnit = 21
)
