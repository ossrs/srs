// Package h264 contains utilities to work with the H264 codec.
package h264

const (
	// MaxAccessUnitSize is the maximum size of an access unit.
	// With a 50 Mbps 2160p60 H264 video, the maximum size does not seem to exceed 8 MiB.
	MaxAccessUnitSize = 8 * 1024 * 1024

	// MaxNALUsPerAccessUnit is the maximum number of NALUs per access unit.
	// with x264, tune=zerolatency and 4K resolution, NALU count is lower than 25.
	MaxNALUsPerAccessUnit = 25
)
