// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtp

import (
	"encoding/binary"
	"errors"
	"fmt"
	"strings"

	"github.com/pion/rtp/codecs/av1/obu"
)

var (
	// ErrVLATooShort is returned when payload is too short.
	ErrVLATooShort = errors.New("VLA payload too short")
	// ErrVLAInvalidStreamCount is returned when RTP stream count is invalid.
	ErrVLAInvalidStreamCount = errors.New("invalid RTP stream count in VLA")
	// ErrVLAInvalidStreamID is returned when RTP stream ID is invalid.
	ErrVLAInvalidStreamID = errors.New("invalid RTP stream ID in VLA")
	// ErrVLAInvalidSpatialID is returned when spatial ID is invalid.
	ErrVLAInvalidSpatialID = errors.New("invalid spatial ID in VLA")
	// ErrVLADuplicateSpatialID is returned when spatial ID is invalid.
	ErrVLADuplicateSpatialID = errors.New("duplicate spatial ID in VLA")
	// ErrVLAInvalidTemporalLayer is returned when temporal layer is invalid.
	ErrVLAInvalidTemporalLayer = errors.New("invalid temporal layer in VLA")
)

// SpatialLayer is a spatial layer in VLA.
type SpatialLayer struct {
	RTPStreamID    int
	SpatialID      int
	TargetBitrates []int // target bitrates per temporal layer

	// Following members are valid only when HasResolutionAndFramerate is true
	Width     int
	Height    int
	Framerate int
}

// VLA is a Video Layer Allocation (VLA) extension.
// See https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/video-layers-allocation00
type VLA struct {
	RTPStreamID               int // 0-origin RTP stream ID (RID) this allocation is sent on (0..3)
	RTPStreamCount            int // Number of RTP streams (1..4)
	ActiveSpatialLayer        []SpatialLayer
	HasResolutionAndFramerate bool
}

type vlaMarshalingContext struct {
	slMBs                 [4]uint8
	sls                   [4][4]*SpatialLayer
	commonSLBM            uint8
	encodedTargetBitrates [][]byte
	requiredLen           int
}

func (v VLA) preprocessForMashaling(ctx *vlaMarshalingContext) error {
	for i := 0; i < len(v.ActiveSpatialLayer); i++ {
		sl := v.ActiveSpatialLayer[i]
		if sl.RTPStreamID < 0 || sl.RTPStreamID >= v.RTPStreamCount {
			return fmt.Errorf("invalid RTP streamID %d:%w", sl.RTPStreamID, ErrVLAInvalidStreamID)
		}
		if sl.SpatialID < 0 || sl.SpatialID >= 4 {
			return fmt.Errorf("invalid spatial ID %d: %w", sl.SpatialID, ErrVLAInvalidSpatialID)
		}
		if len(sl.TargetBitrates) == 0 || len(sl.TargetBitrates) > 4 {
			return fmt.Errorf("invalid temporal layer count %d: %w", len(sl.TargetBitrates), ErrVLAInvalidTemporalLayer)
		}
		ctx.slMBs[sl.RTPStreamID] |= 1 << sl.SpatialID
		if ctx.sls[sl.RTPStreamID][sl.SpatialID] != nil {
			return fmt.Errorf("duplicate spatial layer: %w", ErrVLADuplicateSpatialID)
		}
		ctx.sls[sl.RTPStreamID][sl.SpatialID] = &sl
	}

	return nil
}

func (v VLA) encodeTargetBitrates(ctx *vlaMarshalingContext) {
	for rtpStreamID := 0; rtpStreamID < v.RTPStreamCount; rtpStreamID++ {
		for spatialID := 0; spatialID < 4; spatialID++ {
			if sl := ctx.sls[rtpStreamID][spatialID]; sl != nil {
				for _, kbps := range sl.TargetBitrates {
					leb128 := obu.WriteToLeb128(uint(kbps)) // nolint: gosec
					ctx.encodedTargetBitrates = append(ctx.encodedTargetBitrates, leb128)
					ctx.requiredLen += len(leb128)
				}
			}
		}
	}
}

func (v VLA) analyzeVLAForMarshaling() (*vlaMarshalingContext, error) {
	// Validate RTPStreamCount
	if v.RTPStreamCount <= 0 || v.RTPStreamCount > 4 {
		return nil, ErrVLAInvalidStreamCount
	}
	// Validate RTPStreamID
	if v.RTPStreamID < 0 || v.RTPStreamID >= v.RTPStreamCount {
		return nil, ErrVLAInvalidStreamID
	}

	ctx := &vlaMarshalingContext{}
	err := v.preprocessForMashaling(ctx)
	if err != nil {
		return nil, err
	}

	ctx.commonSLBM = commonSLBMValues(ctx.slMBs[:])

	// RID, NS, sl_bm fields
	if ctx.commonSLBM != 0 {
		ctx.requiredLen = 1
	} else {
		ctx.requiredLen = 3
	}

	// #tl fields
	ctx.requiredLen += (len(v.ActiveSpatialLayer)-1)/4 + 1

	v.encodeTargetBitrates(ctx)

	if v.HasResolutionAndFramerate {
		ctx.requiredLen += len(v.ActiveSpatialLayer) * 5
	}

	return ctx, nil
}

// Marshal encodes VLA into a byte slice.
func (v VLA) Marshal() ([]byte, error) { // nolint: cyclop
	ctx, err := v.analyzeVLAForMarshaling()
	if err != nil {
		return nil, err
	}

	payload := make([]byte, ctx.requiredLen)
	offset := 0

	// RID, NS, sl_bm fields
	payload[offset] = byte(v.RTPStreamID<<6) | byte(v.RTPStreamCount-1)<<4 | ctx.commonSLBM

	if ctx.commonSLBM == 0 {
		offset++
		for streamID := 0; streamID < v.RTPStreamCount; streamID++ {
			if streamID%2 == 0 {
				payload[offset+streamID/2] |= ctx.slMBs[streamID] << 4
			} else {
				payload[offset+streamID/2] |= ctx.slMBs[streamID]
			}
		}
		offset += (v.RTPStreamCount - 1) / 2
	}

	// #tl fields
	offset++
	var temporalLayerIndex int
	for rtpStreamID := 0; rtpStreamID < v.RTPStreamCount; rtpStreamID++ {
		for spatialID := 0; spatialID < 4; spatialID++ {
			if sl := ctx.sls[rtpStreamID][spatialID]; sl != nil {
				if temporalLayerIndex >= 4 {
					temporalLayerIndex = 0
					offset++
				}
				payload[offset] |= byte(len(sl.TargetBitrates)-1) << (2 * (3 - temporalLayerIndex))
				temporalLayerIndex++
			}
		}
	}

	// Target bitrate fields
	offset++
	for _, encodedKbps := range ctx.encodedTargetBitrates {
		encodedSize := len(encodedKbps)
		copy(payload[offset:], encodedKbps)
		offset += encodedSize
	}

	// Resolution & framerate fields
	if v.HasResolutionAndFramerate {
		for _, sl := range v.ActiveSpatialLayer {
			binary.BigEndian.PutUint16(payload[offset+0:], uint16(sl.Width-1))  // nolint: gosec
			binary.BigEndian.PutUint16(payload[offset+2:], uint16(sl.Height-1)) // nolint: gosec
			payload[offset+4] = byte(sl.Framerate)
			offset += 5
		}
	}

	return payload, nil
}

func commonSLBMValues(slMBs []uint8) uint8 {
	var common uint8
	for i := 0; i < len(slMBs); i++ {
		if slMBs[i] == 0 {
			continue
		}
		if common == 0 {
			common = slMBs[i]

			continue
		}
		if slMBs[i] != common {
			return 0
		}
	}

	return common
}

type vlaUnmarshalingContext struct {
	payload   []byte
	offset    int
	slBMField uint8
	slBMs     [4]uint8
}

func (ctx *vlaUnmarshalingContext) checkRemainingLen(requiredLen int) bool {
	return len(ctx.payload)-ctx.offset >= requiredLen
}

func (v *VLA) unmarshalSpatialLayers(ctx *vlaUnmarshalingContext) error {
	if !ctx.checkRemainingLen(1) {
		return fmt.Errorf("failed to unmarshal VLA (offset=%d): %w", ctx.offset, ErrVLATooShort)
	}
	v.RTPStreamID = int(ctx.payload[ctx.offset] >> 6 & 0b11)
	v.RTPStreamCount = int(ctx.payload[ctx.offset]>>4&0b11) + 1

	// sl_bm fields
	ctx.slBMField = ctx.payload[ctx.offset] & 0b1111
	ctx.offset++

	if ctx.slBMField != 0 {
		for streamID := 0; streamID < v.RTPStreamCount; streamID++ {
			ctx.slBMs[streamID] = ctx.slBMField
		}
	} else {
		if !ctx.checkRemainingLen((v.RTPStreamCount-1)/2 + 1) {
			return fmt.Errorf("failed to unmarshal VLA (offset=%d): %w", ctx.offset, ErrVLATooShort)
		}
		// slX_bm fields
		for streamID := 0; streamID < v.RTPStreamCount; streamID++ {
			var bm uint8
			if streamID%2 == 0 {
				bm = ctx.payload[ctx.offset+streamID/2] >> 4 & 0b1111
			} else {
				bm = ctx.payload[ctx.offset+streamID/2] & 0b1111
			}
			ctx.slBMs[streamID] = bm
		}
		ctx.offset += 1 + (v.RTPStreamCount-1)/2
	}

	return nil
}

func (v *VLA) unmarshalTemporalLayers(ctx *vlaUnmarshalingContext) error { // nolint: cyclop
	if !ctx.checkRemainingLen(1) {
		return fmt.Errorf("failed to unmarshal VLA (offset=%d): %w", ctx.offset, ErrVLATooShort)
	}

	var temporalLayerIndex int
	for streamID := 0; streamID < v.RTPStreamCount; streamID++ {
		for spatialID := 0; spatialID < 4; spatialID++ {
			if ctx.slBMs[streamID]&(1<<spatialID) == 0 {
				continue
			}
			if temporalLayerIndex >= 4 {
				temporalLayerIndex = 0
				ctx.offset++
				if !ctx.checkRemainingLen(1) {
					return fmt.Errorf("failed to unmarshal VLA (offset=%d): %w", ctx.offset, ErrVLATooShort)
				}
			}
			tlCount := int(ctx.payload[ctx.offset]>>(2*(3-temporalLayerIndex))&0b11) + 1
			temporalLayerIndex++
			sl := SpatialLayer{
				RTPStreamID:    streamID,
				SpatialID:      spatialID,
				TargetBitrates: make([]int, tlCount),
			}
			v.ActiveSpatialLayer = append(v.ActiveSpatialLayer, sl)
		}
	}
	ctx.offset++

	// target bitrates
	for i, sl := range v.ActiveSpatialLayer {
		for j := range sl.TargetBitrates {
			kbps, n, err := obu.ReadLeb128(ctx.payload[ctx.offset:])
			if err != nil {
				return err
			}

			in := int(n) // nolint: gosec

			if !ctx.checkRemainingLen(in) {
				return fmt.Errorf("failed to unmarshal VLA (offset=%d): %w", ctx.offset, ErrVLATooShort)
			}
			v.ActiveSpatialLayer[i].TargetBitrates[j] = int(kbps) // nolint: gosec
			ctx.offset += in
		}
	}

	return nil
}

func (v *VLA) unmarshalResolutionAndFramerate(ctx *vlaUnmarshalingContext) error {
	if !ctx.checkRemainingLen(len(v.ActiveSpatialLayer) * 5) {
		return fmt.Errorf("failed to unmarshal VLA (offset=%d): %w", ctx.offset, ErrVLATooShort)
	}

	v.HasResolutionAndFramerate = true

	for i := range v.ActiveSpatialLayer {
		v.ActiveSpatialLayer[i].Width = int(binary.BigEndian.Uint16(ctx.payload[ctx.offset+0:])) + 1
		v.ActiveSpatialLayer[i].Height = int(binary.BigEndian.Uint16(ctx.payload[ctx.offset+2:])) + 1
		v.ActiveSpatialLayer[i].Framerate = int(ctx.payload[ctx.offset+4])
		ctx.offset += 5
	}

	return nil
}

// Unmarshal decodes VLA from a byte slice.
func (v *VLA) Unmarshal(payload []byte) (int, error) {
	ctx := &vlaUnmarshalingContext{
		payload: payload,
	}

	err := v.unmarshalSpatialLayers(ctx)
	if err != nil {
		return ctx.offset, err
	}

	// #tl fields (build the list ActiveSpatialLayer at the same time)
	err = v.unmarshalTemporalLayers(ctx)
	if err != nil {
		return ctx.offset, err
	}

	if len(ctx.payload) == ctx.offset {
		return ctx.offset, nil
	}

	// resolution & framerate (optional)
	err = v.unmarshalResolutionAndFramerate(ctx)
	if err != nil {
		return ctx.offset, err
	}

	return ctx.offset, nil
}

// String makes VLA printable.
func (v VLA) String() string {
	out := fmt.Sprintf("RID:%d,RTPStreamCount:%d", v.RTPStreamID, v.RTPStreamCount)
	var slOut []string
	for _, sl := range v.ActiveSpatialLayer {
		out2 := fmt.Sprintf("RTPStreamID:%d", sl.RTPStreamID)
		out2 += fmt.Sprintf(",TargetBitrates:%v", sl.TargetBitrates)
		if v.HasResolutionAndFramerate {
			out2 += fmt.Sprintf(",Resolution:(%d,%d)", sl.Width, sl.Height)
			out2 += fmt.Sprintf(",Framerate:%d", sl.Framerate)
		}
		slOut = append(slOut, out2)
	}
	out += fmt.Sprintf(",ActiveSpatialLayers:{%s}", strings.Join(slOut, ","))

	return out
}
