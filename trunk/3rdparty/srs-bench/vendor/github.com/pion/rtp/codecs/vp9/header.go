// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package vp9 contains a VP9 header parser.
package vp9

import (
	"errors"
)

var (
	errInvalidFrameMarker  = errors.New("invalid frame marker")
	errWrongFrameSyncByte0 = errors.New("wrong frame_sync_byte_0")
	errWrongFrameSyncByte1 = errors.New("wrong frame_sync_byte_1")
	errWrongFrameSyncByte2 = errors.New("wrong frame_sync_byte_2")
)

// HeaderColorConfig is the color_config member of an header.
type HeaderColorConfig struct {
	TenOrTwelveBit bool
	BitDepth       uint8
	ColorSpace     uint8
	ColorRange     bool
	SubsamplingX   bool
	SubsamplingY   bool
}

func (c *HeaderColorConfig) unmarshal(profile uint8, buf []byte, pos *int) error { // nolint:cyclop
	if profile >= 2 {
		var err error
		c.TenOrTwelveBit, err = readFlag(buf, pos)
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

	tmp, err := readBits(buf, pos, 3)
	if err != nil {
		return err
	}
	c.ColorSpace = uint8(tmp) // nolint: gosec // G115, no overflow we read 3 bits

	if c.ColorSpace != 7 { // nolint: nestif
		var err error
		c.ColorRange, err = readFlag(buf, pos)
		if err != nil {
			return err
		}

		if profile == 1 || profile == 3 {
			err := hasSpace(buf, *pos, 3)
			if err != nil {
				return err
			}

			c.SubsamplingX = readFlagUnsafe(buf, pos)
			c.SubsamplingY = readFlagUnsafe(buf, pos)
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

			err := hasSpace(buf, *pos, 1)
			if err != nil {
				return err
			}
			*pos++
		}
	}

	return nil
}

// HeaderFrameSize is the frame_size member of an header.
type HeaderFrameSize struct {
	FrameWidthMinus1  uint16
	FrameHeightMinus1 uint16
}

func (s *HeaderFrameSize) unmarshal(buf []byte, pos *int) error {
	err := hasSpace(buf, *pos, 32)
	if err != nil {
		return err
	}

	s.FrameWidthMinus1 = uint16(readBitsUnsafe(buf, pos, 16))  // nolint: gosec // G115 no overflow, we read 16 bits
	s.FrameHeightMinus1 = uint16(readBitsUnsafe(buf, pos, 16)) // nolint: gosec // G115

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
	ColorConfig        *HeaderColorConfig
	FrameSize          *HeaderFrameSize
}

// Unmarshal decodes a Header.
func (h *Header) Unmarshal(buf []byte) error { //nolint:cyclop
	pos := 0

	err := hasSpace(buf, pos, 4)
	if err != nil {
		return err
	}

	frameMarker := readBitsUnsafe(buf, &pos, 2)
	if frameMarker != 2 {
		return errInvalidFrameMarker
	}

	profileLowBit := uint8(readBitsUnsafe(buf, &pos, 1))  // nolint: gosec // no overflow, we read 1 bit
	profileHighBit := uint8(readBitsUnsafe(buf, &pos, 1)) // nolint: gosec // G115
	h.Profile = profileHighBit<<1 + profileLowBit

	if h.Profile == 3 {
		err = hasSpace(buf, pos, 1)
		if err != nil {
			return err
		}
		pos++
	}

	h.ShowExistingFrame, err = readFlag(buf, &pos)
	if err != nil {
		return err
	}

	if h.ShowExistingFrame {
		var tmp uint64
		tmp, err = readBits(buf, &pos, 3)
		if err != nil {
			return err
		}
		h.FrameToShowMapIdx = uint8(tmp) // nolint: gosec // no overflow, we read 3 bits

		return nil
	}

	err = hasSpace(buf, pos, 3)
	if err != nil {
		return err
	}

	h.NonKeyFrame = readFlagUnsafe(buf, &pos)
	h.ShowFrame = readFlagUnsafe(buf, &pos)
	h.ErrorResilientMode = readFlagUnsafe(buf, &pos)

	if !h.NonKeyFrame { // nolint: nestif
		err := hasSpace(buf, pos, 24)
		if err != nil {
			return err
		}

		frameSyncByte0 := uint8(readBitsUnsafe(buf, &pos, 8)) // nolint: gosec // no overflow, we read 8 bits
		if frameSyncByte0 != 0x49 {
			return errWrongFrameSyncByte0
		}

		frameSyncByte1 := uint8(readBitsUnsafe(buf, &pos, 8)) // nolint: gosec // no overflow, we read 8 bits
		if frameSyncByte1 != 0x83 {
			return errWrongFrameSyncByte1
		}

		frameSyncByte2 := uint8(readBitsUnsafe(buf, &pos, 8)) // nolint: gosec // no overflow, we read 8 bits
		if frameSyncByte2 != 0x42 {
			return errWrongFrameSyncByte2
		}

		h.ColorConfig = &HeaderColorConfig{}
		err = h.ColorConfig.unmarshal(h.Profile, buf, &pos)
		if err != nil {
			return err
		}

		h.FrameSize = &HeaderFrameSize{}
		err = h.FrameSize.unmarshal(buf, &pos)
		if err != nil {
			return err
		}
	}

	return nil
}

// Width returns the video width.
func (h Header) Width() uint16 {
	if h.FrameSize == nil {
		return 0
	}

	return h.FrameSize.FrameWidthMinus1 + 1
}

// Height returns the video height.
func (h Header) Height() uint16 {
	if h.FrameSize == nil {
		return 0
	}

	return h.FrameSize.FrameHeightMinus1 + 1
}
