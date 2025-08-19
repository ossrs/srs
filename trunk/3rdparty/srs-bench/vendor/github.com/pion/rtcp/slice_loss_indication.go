// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

import (
	"encoding/binary"
	"fmt"
	"math"
)

// SLIEntry represents a single entry to the SLI packet's
// list of lost slices.
type SLIEntry struct {
	// ID of first lost slice
	First uint16

	// Number of lost slices
	Number uint16

	// ID of related picture
	Picture uint8
}

// The SliceLossIndication packet informs the encoder about the loss of a picture slice
type SliceLossIndication struct {
	// SSRC of sender
	SenderSSRC uint32

	// SSRC of the media source
	MediaSSRC uint32

	SLI []SLIEntry
}

const (
	sliLength = 2
	sliOffset = 8
)

// Marshal encodes the SliceLossIndication in binary
func (p SliceLossIndication) Marshal() ([]byte, error) {
	if len(p.SLI)+sliLength > math.MaxUint8 {
		return nil, errTooManyReports
	}

	rawPacket := make([]byte, sliOffset+(len(p.SLI)*4))
	binary.BigEndian.PutUint32(rawPacket, p.SenderSSRC)
	binary.BigEndian.PutUint32(rawPacket[4:], p.MediaSSRC)
	for i, s := range p.SLI {
		sli := ((uint32(s.First) & 0x1FFF) << 19) |
			((uint32(s.Number) & 0x1FFF) << 6) |
			(uint32(s.Picture) & 0x3F)
		binary.BigEndian.PutUint32(rawPacket[sliOffset+(4*i):], sli)
	}
	hData, err := p.Header().Marshal()
	if err != nil {
		return nil, err
	}

	return append(hData, rawPacket...), nil
}

// Unmarshal decodes the SliceLossIndication from binary
func (p *SliceLossIndication) Unmarshal(rawPacket []byte) error {
	if len(rawPacket) < (headerLength + ssrcLength) {
		return errPacketTooShort
	}

	var h Header
	if err := h.Unmarshal(rawPacket); err != nil {
		return err
	}

	if len(rawPacket) < (headerLength + int(4*h.Length)) {
		return errPacketTooShort
	}

	if h.Type != TypeTransportSpecificFeedback || h.Count != FormatSLI {
		return errWrongType
	}

	p.SenderSSRC = binary.BigEndian.Uint32(rawPacket[headerLength:])
	p.MediaSSRC = binary.BigEndian.Uint32(rawPacket[headerLength+ssrcLength:])
	for i := headerLength + sliOffset; i < (headerLength + int(h.Length*4)); i += 4 {
		sli := binary.BigEndian.Uint32(rawPacket[i:])
		p.SLI = append(p.SLI, SLIEntry{
			First:   uint16((sli >> 19) & 0x1FFF),
			Number:  uint16((sli >> 6) & 0x1FFF),
			Picture: uint8(sli & 0x3F),
		})
	}
	return nil
}

// MarshalSize returns the size of the packet once marshaled
func (p *SliceLossIndication) MarshalSize() int {
	return headerLength + sliOffset + (len(p.SLI) * 4)
}

// Header returns the Header associated with this packet.
func (p *SliceLossIndication) Header() Header {
	return Header{
		Count:  FormatSLI,
		Type:   TypeTransportSpecificFeedback,
		Length: uint16((p.MarshalSize() / 4) - 1),
	}
}

func (p *SliceLossIndication) String() string {
	return fmt.Sprintf("SliceLossIndication %x %x %+v", p.SenderSSRC, p.MediaSSRC, p.SLI)
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (p *SliceLossIndication) DestinationSSRC() []uint32 {
	return []uint32{p.MediaSSRC}
}
