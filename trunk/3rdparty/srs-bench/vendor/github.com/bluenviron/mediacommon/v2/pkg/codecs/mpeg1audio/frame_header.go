package mpeg1audio

import (
	"fmt"
)

// http://www.mp3-tech.org/programmer/frame_header.html
var bitrates = [][][]int{
	// MPEG-1
	{
		// layer 1
		{},
		// layer 2
		{
			32000,
			48000,
			56000,
			64000,
			80000,
			96000,
			112000,
			128000,
			160000,
			192000,
			224000,
			256000,
			320000,
			384000,
		},
		// layer 3
		{
			32000,
			40000,
			48000,
			56000,
			64000,
			80000,
			96000,
			112000,
			128000,
			160000,
			192000,
			224000,
			256000,
			320000,
		},
	},
	// MPEG-2
	{
		// layer 1
		{},
		// layer 2
		{
			8000,
			16000,
			24000,
			32000,
			40000,
			48000,
			56000,
			64000,
			80000,
			96000,
			112000,
			128000,
			144000,
			160000,
		},
		// layer 3
		{
			8000,
			16000,
			24000,
			32000,
			40000,
			48000,
			56000,
			64000,
			80000,
			96000,
			112000,
			128000,
			144000,
			160000,
		},
	},
}

var sampleRates = [][]int{
	// MPEG-1
	{
		44100,
		48000,
		32000,
	},
	// MPEG-2
	{
		22050,
		24000,
		16000,
	},
}

var samplesPerFrame = [][]int{
	// MPEG-1
	{
		384,
		1152,
		1152,
	},
	// MPEG-2
	{
		384,
		1152,
		576,
	},
}

// ChannelMode is a channel mode of a MPEG-1/2 audio frame.
type ChannelMode int

// standard channel modes.
const (
	ChannelModeStereo      ChannelMode = 0
	ChannelModeJointStereo ChannelMode = 1
	ChannelModeDualChannel ChannelMode = 2
	ChannelModeMono        ChannelMode = 3
)

// FrameHeader is the header of a MPEG-1/2 audio frame.
// Specification: ISO 11172-3, 2.4.1.3
type FrameHeader struct {
	MPEG2       bool
	Layer       uint8
	Bitrate     int
	SampleRate  int
	Padding     bool
	ChannelMode ChannelMode
}

// Unmarshal decodes a FrameHeader.
func (h *FrameHeader) Unmarshal(buf []byte) error {
	if len(buf) < 5 {
		return fmt.Errorf("not enough bytes")
	}

	syncWord := uint16(buf[0])<<4 | uint16(buf[1])>>4
	if syncWord != 0x0FFF {
		return fmt.Errorf("sync word not found: %x", syncWord)
	}

	h.MPEG2 = ((buf[1] >> 3) & 0x01) == 0

	var mpegIndex int
	if h.MPEG2 {
		mpegIndex = 1
	} else {
		mpegIndex = 0
	}

	h.Layer = 4 - ((buf[1] >> 1) & 0b11)
	if h.Layer <= 1 || h.Layer >= 4 {
		return fmt.Errorf("unsupported MPEG layer: %v", h.Layer)
	}

	bitrateIndex := (buf[2] >> 4)
	if bitrateIndex == 0 || bitrateIndex >= 15 {
		return fmt.Errorf("invalid bitrate")
	}
	h.Bitrate = bitrates[mpegIndex][h.Layer-1][bitrateIndex-1]

	sampleRateIndex := (buf[2] >> 2) & 0b11
	if sampleRateIndex >= 3 {
		return fmt.Errorf("invalid sample rate")
	}
	h.SampleRate = sampleRates[mpegIndex][sampleRateIndex]

	h.Padding = ((buf[2] >> 1) & 0b1) != 0
	h.ChannelMode = ChannelMode(buf[3] >> 6)

	return nil
}

// FrameLen returns the length of the frame associated with the header.
func (h FrameHeader) FrameLen() int {
	if h.Padding {
		return (144 * h.Bitrate / h.SampleRate) + 1
	}
	return (144 * h.Bitrate / h.SampleRate)
}

// SampleCount returns the number of samples contained into the frame.
func (h FrameHeader) SampleCount() int {
	var mpegIndex int
	if h.MPEG2 {
		mpegIndex = 1
	} else {
		mpegIndex = 0
	}

	return samplesPerFrame[mpegIndex][h.Layer-1]
}
