package vp9

import (
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/bits"
)

// Header_ColorConfig is the color_config member of an header.
type Header_ColorConfig struct { //nolint:revive
	TenOrTwelveBit bool
	BitDepth       uint8
	ColorSpace     uint8
	ColorRange     bool
	SubsamplingX   bool
	SubsamplingY   bool
}

func (c *Header_ColorConfig) unmarshal(profile uint8, buf []byte, pos *int) error {
	if profile >= 2 {
		var err error
		c.TenOrTwelveBit, err = bits.ReadFlag(buf, pos)
		if err != nil {
			return err
		}

		if c.TenOrTwelveBit {
			c.BitDepth = 12
		} else {
			c.BitDepth = 10
		}
	} else {
		c.BitDepth = 8
	}

	tmp, err := bits.ReadBits(buf, pos, 3)
	if err != nil {
		return err
	}
	c.ColorSpace = uint8(tmp)

	if c.ColorSpace != 7 {
		var err error
		c.ColorRange, err = bits.ReadFlag(buf, pos)
		if err != nil {
			return err
		}

		if profile == 1 || profile == 3 {
			err := bits.HasSpace(buf, *pos, 3)
			if err != nil {
				return err
			}

			c.SubsamplingX = bits.ReadFlagUnsafe(buf, pos)
			c.SubsamplingY = bits.ReadFlagUnsafe(buf, pos)
			*pos++
		} else {
			c.SubsamplingX = true
			c.SubsamplingY = true
		}
	} else {
		c.ColorRange = true

		if profile == 1 || profile == 3 {
			c.SubsamplingX = false
			c.SubsamplingY = false

			err := bits.HasSpace(buf, *pos, 1)
			if err != nil {
				return err
			}
			*pos++
		}
	}

	return nil
}

// Header_FrameSize is the frame_size member of an header.
type Header_FrameSize struct { //nolint:revive
	FrameWidthMinus1  uint16
	FrameHeightMinus1 uint16
}

func (s *Header_FrameSize) unmarshal(buf []byte, pos *int) error {
	err := bits.HasSpace(buf, *pos, 32)
	if err != nil {
		return err
	}

	s.FrameWidthMinus1 = uint16(bits.ReadBitsUnsafe(buf, pos, 16))
	s.FrameHeightMinus1 = uint16(bits.ReadBitsUnsafe(buf, pos, 16))
	return nil
}

// Header is a VP9 Frame header.
// Specification:
// https://storage.googleapis.com/downloads.webmproject.org/docs/vp9/vp9-bitstream-specification-v0.6-20160331-draft.pdf
type Header struct {
	Profile            uint8
	ShowExistingFrame  bool
	FrameToShowMapIdx  uint8
	NonKeyFrame        bool
	ShowFrame          bool
	ErrorResilientMode bool
	ColorConfig        *Header_ColorConfig
	FrameSize          *Header_FrameSize
}

// Unmarshal decodes a Header.
func (h *Header) Unmarshal(buf []byte) error {
	pos := 0

	err := bits.HasSpace(buf, pos, 4)
	if err != nil {
		return err
	}

	frameMarker := bits.ReadBitsUnsafe(buf, &pos, 2)
	if frameMarker != 2 {
		return fmt.Errorf("invalid frame marker")
	}

	profileLowBit := uint8(bits.ReadBitsUnsafe(buf, &pos, 1))
	profileHighBit := uint8(bits.ReadBitsUnsafe(buf, &pos, 1))
	h.Profile = profileHighBit<<1 + profileLowBit

	if h.Profile == 3 {
		err = bits.HasSpace(buf, pos, 1)
		if err != nil {
			return err
		}
		pos++
	}

	h.ShowExistingFrame, err = bits.ReadFlag(buf, &pos)
	if err != nil {
		return err
	}

	if h.ShowExistingFrame {
		var tmp uint64
		tmp, err = bits.ReadBits(buf, &pos, 3)
		if err != nil {
			return err
		}
		h.FrameToShowMapIdx = uint8(tmp)
		return nil
	}

	err = bits.HasSpace(buf, pos, 3)
	if err != nil {
		return err
	}

	h.NonKeyFrame = bits.ReadFlagUnsafe(buf, &pos)
	h.ShowFrame = bits.ReadFlagUnsafe(buf, &pos)
	h.ErrorResilientMode = bits.ReadFlagUnsafe(buf, &pos)

	if !h.NonKeyFrame {
		err := bits.HasSpace(buf, pos, 24)
		if err != nil {
			return err
		}

		frameSyncByte0 := uint8(bits.ReadBitsUnsafe(buf, &pos, 8))
		if frameSyncByte0 != 0x49 {
			return fmt.Errorf("wrong frame_sync_byte_0")
		}

		frameSyncByte1 := uint8(bits.ReadBitsUnsafe(buf, &pos, 8))
		if frameSyncByte1 != 0x83 {
			return fmt.Errorf("wrong frame_sync_byte_1")
		}

		frameSyncByte2 := uint8(bits.ReadBitsUnsafe(buf, &pos, 8))
		if frameSyncByte2 != 0x42 {
			return fmt.Errorf("wrong frame_sync_byte_2")
		}

		h.ColorConfig = &Header_ColorConfig{}
		err = h.ColorConfig.unmarshal(h.Profile, buf, &pos)
		if err != nil {
			return err
		}

		h.FrameSize = &Header_FrameSize{}
		err = h.FrameSize.unmarshal(buf, &pos)
		if err != nil {
			return err
		}
	}

	return nil
}

// Width returns the video width.
func (h Header) Width() int {
	if h.FrameSize == nil {
		return 0
	}
	return int(h.FrameSize.FrameWidthMinus1) + 1
}

// Height returns the video height.
func (h Header) Height() int {
	if h.FrameSize == nil {
		return 0
	}
	return int(h.FrameSize.FrameHeightMinus1) + 1
}

// ChromaSubsampling returns the chroma subsampling format, in ISO-BMFF/vpcC format.
func (h Header) ChromaSubsampling() uint8 {
	if h.ColorConfig == nil {
		return 1
	}
	switch {
	case !h.ColorConfig.SubsamplingX && !h.ColorConfig.SubsamplingY:
		return 3 // 4:4:4
	case h.ColorConfig.SubsamplingX && !h.ColorConfig.SubsamplingY:
		return 2 // 4:2:2
	default:
		return 1 // 4:2:0 colocated with luma
	}
}
