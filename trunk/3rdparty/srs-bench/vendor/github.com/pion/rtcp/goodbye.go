package rtcp

import (
	"encoding/binary"
	"fmt"
)

// The Goodbye packet indicates that one or more sources are no longer active.
type Goodbye struct {
	// The SSRC/CSRC identifiers that are no longer active
	Sources []uint32
	// Optional text indicating the reason for leaving, e.g., "camera malfunction" or "RTP loop detected"
	Reason string
}

// Marshal encodes the Goodbye packet in binary
func (g Goodbye) Marshal() ([]byte, error) {
	/*
	 *        0                   1                   2                   3
	 *        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *       |V=2|P|    SC   |   PT=BYE=203  |             length            |
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *       |                           SSRC/CSRC                           |
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *       :                              ...                              :
	 *       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * (opt) |     length    |               reason for leaving            ...
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	rawPacket := make([]byte, g.len())
	packetBody := rawPacket[headerLength:]

	if len(g.Sources) > countMax {
		return nil, errTooManySources
	}

	for i, s := range g.Sources {
		binary.BigEndian.PutUint32(packetBody[i*ssrcLength:], s)
	}

	if g.Reason != "" {
		reason := []byte(g.Reason)

		if len(reason) > sdesMaxOctetCount {
			return nil, errReasonTooLong
		}

		reasonOffset := len(g.Sources) * ssrcLength
		packetBody[reasonOffset] = uint8(len(reason))
		copy(packetBody[reasonOffset+1:], reason)
	}

	hData, err := g.Header().Marshal()
	if err != nil {
		return nil, err
	}
	copy(rawPacket, hData)

	return rawPacket, nil
}

// Unmarshal decodes the Goodbye packet from binary
func (g *Goodbye) Unmarshal(rawPacket []byte) error {
	/*
	 *        0                   1                   2                   3
	 *        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *       |V=2|P|    SC   |   PT=BYE=203  |             length            |
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *       |                           SSRC/CSRC                           |
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *       :                              ...                              :
	 *       +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
	 * (opt) |     length    |               reason for leaving            ...
	 *       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 */

	var header Header
	if err := header.Unmarshal(rawPacket); err != nil {
		return err
	}

	if header.Type != TypeGoodbye {
		return errWrongType
	}

	if getPadding(len(rawPacket)) != 0 {
		return errPacketTooShort
	}

	g.Sources = make([]uint32, header.Count)

	reasonOffset := int(headerLength + header.Count*ssrcLength)
	if reasonOffset > len(rawPacket) {
		return errPacketTooShort
	}

	for i := 0; i < int(header.Count); i++ {
		offset := headerLength + i*ssrcLength

		g.Sources[i] = binary.BigEndian.Uint32(rawPacket[offset:])
	}

	if reasonOffset < len(rawPacket) {
		reasonLen := int(rawPacket[reasonOffset])
		reasonEnd := reasonOffset + 1 + reasonLen

		if reasonEnd > len(rawPacket) {
			return errPacketTooShort
		}

		g.Reason = string(rawPacket[reasonOffset+1 : reasonEnd])
	}

	return nil
}

// Header returns the Header associated with this packet.
func (g *Goodbye) Header() Header {
	return Header{
		Padding: false,
		Count:   uint8(len(g.Sources)),
		Type:    TypeGoodbye,
		Length:  uint16((g.len() / 4) - 1),
	}
}

func (g *Goodbye) len() int {
	srcsLength := len(g.Sources) * ssrcLength
	reasonLength := len(g.Reason) + 1

	l := headerLength + srcsLength + reasonLength

	// align to 32-bit boundary
	return l + getPadding(l)
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (g *Goodbye) DestinationSSRC() []uint32 {
	out := make([]uint32, len(g.Sources))
	copy(out, g.Sources)
	return out
}

func (g Goodbye) String() string {
	out := "Goodbye\n"
	for i, s := range g.Sources {
		out += fmt.Sprintf("\tSource %d: %x\n", i, s)
	}
	out += fmt.Sprintf("\tReason: %s\n", g.Reason)

	return out
}
