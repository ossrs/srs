package handshake

import (
	"encoding/binary"

	"github.com/pion/dtls/v2/internal/util"
)

// HeaderLength msg_len for Handshake messages assumes an extra
// 12 bytes for sequence, fragment and version information vs TLS
const HeaderLength = 12

// Header is the static first 12 bytes of each RecordLayer
// of type Handshake. These fields allow us to support message loss, reordering, and
// message fragmentation,
//
// https://tools.ietf.org/html/rfc6347#section-4.2.2
type Header struct {
	Type            Type
	Length          uint32 // uint24 in spec
	MessageSequence uint16
	FragmentOffset  uint32 // uint24 in spec
	FragmentLength  uint32 // uint24 in spec
}

// Marshal encodes the Header
func (h *Header) Marshal() ([]byte, error) {
	out := make([]byte, HeaderLength)

	out[0] = byte(h.Type)
	util.PutBigEndianUint24(out[1:], h.Length)
	binary.BigEndian.PutUint16(out[4:], h.MessageSequence)
	util.PutBigEndianUint24(out[6:], h.FragmentOffset)
	util.PutBigEndianUint24(out[9:], h.FragmentLength)
	return out, nil
}

// Unmarshal populates the header from encoded data
func (h *Header) Unmarshal(data []byte) error {
	if len(data) < HeaderLength {
		return errBufferTooSmall
	}

	h.Type = Type(data[0])
	h.Length = util.BigEndianUint24(data[1:])
	h.MessageSequence = binary.BigEndian.Uint16(data[4:])
	h.FragmentOffset = util.BigEndianUint24(data[6:])
	h.FragmentLength = util.BigEndianUint24(data[9:])
	return nil
}
