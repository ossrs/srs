package rtcp

import (
	"encoding/binary"
	"fmt"
)

// The RapidResynchronizationRequest packet informs the encoder about the loss of an undefined amount of coded video data belonging to one or more pictures
type RapidResynchronizationRequest struct {
	// SSRC of sender
	SenderSSRC uint32

	// SSRC of the media source
	MediaSSRC uint32
}

var _ Packet = (*RapidResynchronizationRequest)(nil) // assert is a Packet

const (
	rrrLength       = 2
	rrrHeaderLength = ssrcLength * 2
	rrrMediaOffset  = 4
)

// Marshal encodes the RapidResynchronizationRequest in binary
func (p RapidResynchronizationRequest) Marshal() ([]byte, error) {
	/*
	 * RRR does not require parameters.  Therefore, the length field MUST be
	 * 2, and there MUST NOT be any Feedback Control Information.
	 *
	 * The semantics of this FB message is independent of the payload type.
	 */
	rawPacket := make([]byte, p.len())
	packetBody := rawPacket[headerLength:]

	binary.BigEndian.PutUint32(packetBody, p.SenderSSRC)
	binary.BigEndian.PutUint32(packetBody[rrrMediaOffset:], p.MediaSSRC)

	hData, err := p.Header().Marshal()
	if err != nil {
		return nil, err
	}
	copy(rawPacket, hData)

	return rawPacket, nil
}

// Unmarshal decodes the RapidResynchronizationRequest from binary
func (p *RapidResynchronizationRequest) Unmarshal(rawPacket []byte) error {
	if len(rawPacket) < (headerLength + (ssrcLength * 2)) {
		return errPacketTooShort
	}

	var h Header
	if err := h.Unmarshal(rawPacket); err != nil {
		return err
	}

	if h.Type != TypeTransportSpecificFeedback || h.Count != FormatRRR {
		return errWrongType
	}

	p.SenderSSRC = binary.BigEndian.Uint32(rawPacket[headerLength:])
	p.MediaSSRC = binary.BigEndian.Uint32(rawPacket[headerLength+ssrcLength:])
	return nil
}

func (p *RapidResynchronizationRequest) len() int {
	return headerLength + rrrHeaderLength
}

// Header returns the Header associated with this packet.
func (p *RapidResynchronizationRequest) Header() Header {
	return Header{
		Count:  FormatRRR,
		Type:   TypeTransportSpecificFeedback,
		Length: rrrLength,
	}
}

// DestinationSSRC returns an array of SSRC values that this packet refers to.
func (p *RapidResynchronizationRequest) DestinationSSRC() []uint32 {
	return []uint32{p.MediaSSRC}
}

func (p *RapidResynchronizationRequest) String() string {
	return fmt.Sprintf("RapidResynchronizationRequest %x %x", p.SenderSSRC, p.MediaSSRC)
}
