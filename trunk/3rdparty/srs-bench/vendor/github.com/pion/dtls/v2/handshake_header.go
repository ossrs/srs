package dtls

import (
	"encoding/binary"
)

// msg_len for Handshake messages assumes an extra 12 bytes for
// sequence, fragment and version information
const handshakeHeaderLength = 12

type handshakeHeader struct {
	handshakeType   handshakeType
	length          uint32 // uint24 in spec
	messageSequence uint16
	fragmentOffset  uint32 // uint24 in spec
	fragmentLength  uint32 // uint24 in spec
}

func (h *handshakeHeader) Marshal() ([]byte, error) {
	out := make([]byte, handshakeMessageHeaderLength)

	out[0] = byte(h.handshakeType)
	putBigEndianUint24(out[1:], h.length)
	binary.BigEndian.PutUint16(out[4:], h.messageSequence)
	putBigEndianUint24(out[6:], h.fragmentOffset)
	putBigEndianUint24(out[9:], h.fragmentLength)
	return out, nil
}

func (h *handshakeHeader) Unmarshal(data []byte) error {
	if len(data) < handshakeHeaderLength {
		return errBufferTooSmall
	}

	h.handshakeType = handshakeType(data[0])
	h.length = bigEndianUint24(data[1:])
	h.messageSequence = binary.BigEndian.Uint16(data[4:])
	h.fragmentOffset = bigEndianUint24(data[6:])
	h.fragmentLength = bigEndianUint24(data[9:])
	return nil
}
