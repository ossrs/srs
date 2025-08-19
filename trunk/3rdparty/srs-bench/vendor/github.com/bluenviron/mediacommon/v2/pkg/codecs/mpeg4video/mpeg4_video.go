// Package mpeg4video contains utilities to work with MPEG-4 part 2 video codecs.
package mpeg4video

const (
	// MaxFrameSize is the maximum size of a frame.
	MaxFrameSize = 1 * 1024 * 1024
)

// StartCode is a MPEG-4 Video start code.
// Specification: ISO 14496-2, Table 6-3
type StartCode uint8

// start codes.
const (
	VideoObjectStartCodeFirst      StartCode = 0x00
	VideoObjectStartCodeLast       StartCode = 0x1F
	VisualObjectSequenceStartCode  StartCode = 0xB0
	VideoObjectLayerStartCodeFirst StartCode = 0x20
	VideoObjectLayerStartCodeLast  StartCode = 0x2F
	UserDataStartCode              StartCode = 0xB2
	GroupOfVOPStartCode            StartCode = 0xB3
	VisualObjectStartCode          StartCode = 0xB5
	VOPStartCode                   StartCode = 0xB6
)
