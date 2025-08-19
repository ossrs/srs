// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtcp

import (
	"encoding/binary"
	"fmt"
	"math"
)

// PacketBitmap shouldn't be used like a normal integral,
// so it's type is masked here. Access it with PacketList().
type PacketBitmap uint16

// NackPair is a wire-representation of a collection of
// Lost RTP packets
type NackPair struct {
	// ID of lost packets
	PacketID uint16

	// Bitmask of following lost packets
	LostPackets PacketBitmap
}

// The TransportLayerNack packet informs the encoder about the loss of a transport packet
// IETF RFC 4585, Section 6.2.1
// https://tools.ietf.org/html/rfc4585#section-6.2.1
type TransportLayerNack struct {
	// SSRC of sender
	SenderSSRC uint32

	// SSRC of the media source
	MediaSSRC uint32

	Nacks []NackPair
}

// NackPairsFromSequenceNumbers generates a slice of NackPair from a list of SequenceNumbers
// This handles generating the proper values for PacketID/LostPackets
func NackPairsFromSequenceNumbers(sequenceNumbers []uint16) (pairs []NackPair) {
	if len(sequenceNumbers) == 0 {
		return []NackPair{}
	}

	nackPair := &NackPair{PacketID: sequenceNumbers[0]}
	for i := 1; i < len(sequenceNumbers); i++ {
		m := sequenceNumbers[i]

		if m-nackPair.PacketID > 16 {
			pairs = append(pairs, *nackPair)
			nackPair = &NackPair{PacketID: m}
			continue
		}

		nackPair.LostPackets |= 1 << (m - nackPair.PacketID - 1)
	}
	pairs = append(pairs, *nackPair)
	return
}

// Range calls f sequentially for each sequence number covered by n.
// If f returns false, Range stops the iteration.
func (n *NackPair) Range(f func(seqno uint16) bool) {
	more := f(n.PacketID)
	if !more {
		return
	}

	b := n.LostPackets
	for i := uint16(0); b != 0; i++ {
		if (b & (1 << i)) != 0 {
			b &^= (1 << i)
			more = f(n.PacketID + i + 1)
			if !more {
				return
			}
		}
	}
}

// PacketList returns a list of Nack'd packets that's referenced by a NackPair
func (n *NackPair) PacketList() []uint16 {
	out := make([]uint16, 0, 17)
	n.Range(func(seqno uint16) bool {
		out = append(out, seqno)
		return true
	})
	return out
}

const (
	tlnLength  = 2
	nackOffset = 8
)

// Marshal encodes the TransportLayerNack in binary
func (p TransportLayerNack) Marshal() ([]byte, error) {
	if len(p.Nacks)+tlnLength > math.MaxUint8 {
		return nil, errTooManyReports
	}

	rawPacket := make([]byte, nackOffset+(len(p.Nacks)*4))
	binary.BigEndian.PutUint32(rawPacket, p.SenderSSRC)
	binary.BigEndian.PutUint32(rawPacket[4:], p.MediaSSRC)
	for i := 0; i < len(p.Nacks); i++ {
		binary.BigEndian.PutUint16(rawPacket[nackOffset+(4*i):], p.Nacks[i].PacketID)
		binary.BigEndian.PutUint16(rawPacket[nackOffset+(4*i)+2:], uint16(p.Nacks[i].LostPackets))
	}
	h := p.Header()
	hData, err := h.Marshal()
	if err != nil {
		return nil, err
	}

	return append(hData, rawPacket...), nil
}

// Unmarshal decodes the TransportLayerNack from binary
func (p *TransportLayerNack) Unmarshal(rawPacket []byte) error {
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

	if h.Type != TypeTransportSpecificFeedback || h.Count != FormatTLN {
		return errWrongType
	}

	// The FCI field MUST contain at least one and MAY contain more than one Generic NACK
	if 4*h.Length <= nackOffset {
		return errBadLength
	}

	p.SenderSSRC = binary.BigEndian.Uint32(rawPacket[headerLength:])
	p.MediaSSRC = binary.BigEndian.Uint32(rawPacket[headerLength+ssrcLength:])
	for i := headerLength + nackOffset; i < (headerLength + int(h.Length*4)); i += 4 {
		p.Nacks = append(p.Nacks, NackPair{
			binary.BigEndian.Uint16(rawPacket[i:]),
			PacketBitmap(binary.BigEndian.Uint16(rawPacket[i+2:])),
		})
	}
	return nil
}

// MarshalSize returns the size of the packet once marshaled
func (p *TransportLayerNack) MarshalSize() int {
	return headerLength + nackOffset + (len(p.Nacks) * 4)
}

// Header returns the Header associated with this packet.
func (p *TransportLayerNack) Header() Header {
	return Header{
		Count:  FormatTLN,
		Type:   TypeTransportSpecificFeedback,
		Length: uint16((p.MarshalSize() / 4) - 1),
	}
}

func (p TransportLayerNack) String() string {
	out := fmt.Sprintf("TransportLayerNack from %x\n", p.SenderSSRC)
	out += fmt.Sprintf("\tMedia Ssrc %x\n", p.MediaSSRC)
	out += "\tID\tLostPackets\n"
	for _, i := range p.Nacks {
		out += fmt.Sprintf("\t%d\t%b\n", i.PacketID, i.LostPackets)
	}
	return out
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (p *TransportLayerNack) DestinationSSRC() []uint32 {
	return []uint32{p.MediaSSRC}
}
