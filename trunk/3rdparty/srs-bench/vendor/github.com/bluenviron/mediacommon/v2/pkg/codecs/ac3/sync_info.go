package ac3

import (
	"fmt"
)

// ATSC, AC-3, Table 5.18
var frameSizes = [][]int{
	{64, 69, 96},
	{64, 70, 96},
	{80, 87, 120},
	{80, 88, 120},
	{96, 104, 144},
	{96, 105, 144},
	{112, 121, 168},
	{112, 122, 168},
	{128, 139, 192},
	{128, 140, 192},
	{160, 174, 240},
	{160, 175, 240},
	{192, 208, 288},
	{192, 209, 288},
	{224, 243, 336},
	{224, 244, 336},
	{256, 278, 384},
	{256, 279, 384},
	{320, 348, 480},
	{320, 349, 480},
	{384, 417, 576},
	{384, 418, 576},
	{448, 487, 672},
	{448, 488, 672},
	{512, 557, 768},
	{512, 558, 768},
	{640, 696, 960},
	{640, 697, 960},
	{768, 835, 1152},
	{768, 836, 1152},
	{896, 975, 1344},
	{896, 976, 1344},
	{1024, 1114, 1536},
	{1024, 1115, 1536},
	{1152, 1253, 1728},
	{1152, 1254, 1728},
	{1280, 1393, 1920},
	{1280, 1394, 1920},
}

// SyncInfo is a synchronization information.
// Specification: ATSC, AC-3, Table 5.1
type SyncInfo struct {
	Fscod      uint8
	Frmsizecod uint8
}

// Unmarshal decodes a SyncInfo.
func (s *SyncInfo) Unmarshal(frame []byte) error {
	if len(frame) < 5 {
		return fmt.Errorf("not enough bits")
	}

	if frame[0] != 0x0B || frame[1] != 0x77 {
		return fmt.Errorf("invalid sync word")
	}

	s.Fscod = frame[4] >> 6
	if s.Fscod >= 3 {
		return fmt.Errorf("invalid fscod")
	}

	s.Frmsizecod = frame[4] & 0x3f
	if s.Frmsizecod >= 38 {
		return fmt.Errorf("invalid frmsizecod")
	}

	return nil
}

// FrameSize returns the frame size.
func (s SyncInfo) FrameSize() int {
	return frameSizes[s.Frmsizecod][s.Fscod] * 2
}

// SampleRate returns the frame sample rate.
func (s SyncInfo) SampleRate() int {
	switch s.Fscod {
	case 0:
		return 48000
	case 1:
		return 44100
	default:
		return 32000
	}
}
