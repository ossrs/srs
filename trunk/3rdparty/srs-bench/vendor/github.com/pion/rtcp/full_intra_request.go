// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

import (
	"encoding/binary"
	"fmt"
)

// A FIREntry is a (SSRC, seqno) pair, as carried by FullIntraRequest.
type FIREntry struct {
	SSRC           uint32
	SequenceNumber uint8
}

// The FullIntraRequest packet is used to reliably request an Intra frame
// in a video stream.  See RFC 5104 Section 3.5.1.  This is not for loss
// recovery, which should use PictureLossIndication (PLI) instead.
type FullIntraRequest struct {
	SenderSSRC uint32
	MediaSSRC  uint32

	FIR []FIREntry
}

const (
	firOffset = 8
)

var _ Packet = (*FullIntraRequest)(nil)

// Marshal encodes the FullIntraRequest
func (p FullIntraRequest) Marshal() ([]byte, error) {
	rawPacket := make([]byte, firOffset+(len(p.FIR)*8))
	binary.BigEndian.PutUint32(rawPacket, p.SenderSSRC)
	binary.BigEndian.PutUint32(rawPacket[4:], p.MediaSSRC)
	for i, fir := range p.FIR {
		binary.BigEndian.PutUint32(rawPacket[firOffset+8*i:], fir.SSRC)
		rawPacket[firOffset+8*i+4] = fir.SequenceNumber
	}
	h := p.Header()
	hData, err := h.Marshal()
	if err != nil {
		return nil, err
	}

	return append(hData, rawPacket...), nil
}

// Unmarshal decodes the TransportLayerNack
func (p *FullIntraRequest) Unmarshal(rawPacket []byte) error {
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

	if h.Type != TypePayloadSpecificFeedback || h.Count != FormatFIR {
		return errWrongType
	}

	// The FCI field MUST contain one or more FIR entries
	if 4*h.Length-firOffset <= 0 || (4*h.Length)%8 != 0 {
		return errBadLength
	}

	p.SenderSSRC = binary.BigEndian.Uint32(rawPacket[headerLength:])
	p.MediaSSRC = binary.BigEndian.Uint32(rawPacket[headerLength+ssrcLength:])
	for i := headerLength + firOffset; i < (headerLength + int(h.Length*4)); i += 8 {
		p.FIR = append(p.FIR, FIREntry{
			binary.BigEndian.Uint32(rawPacket[i:]),
			rawPacket[i+4],
		})
	}
	return nil
}

// Header returns the Header associated with this packet.
func (p *FullIntraRequest) Header() Header {
	return Header{
		Count:  FormatFIR,
		Type:   TypePayloadSpecificFeedback,
		Length: uint16((p.MarshalSize() / 4) - 1),
	}
}

// MarshalSize returns the size of the packet once marshaled
func (p *FullIntraRequest) MarshalSize() int {
	return headerLength + firOffset + len(p.FIR)*8
}

func (p *FullIntraRequest) String() string {
	out := fmt.Sprintf("FullIntraRequest %x %x",
		p.SenderSSRC, p.MediaSSRC)
	for _, e := range p.FIR {
		out += fmt.Sprintf(" (%x %v)", e.SSRC, e.SequenceNumber)
	}
	return out
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (p *FullIntraRequest) DestinationSSRC() []uint32 {
	ssrcs := make([]uint32, 0, len(p.FIR))
	for _, entry := range p.FIR {
		ssrcs = append(ssrcs, entry.SSRC)
	}
	return ssrcs
}
