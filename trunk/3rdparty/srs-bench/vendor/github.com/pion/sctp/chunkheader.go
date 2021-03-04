package sctp

import (
	"encoding/binary"

	"github.com/pkg/errors"
)

/*
chunkHeader represents a SCTP Chunk header, defined in https://tools.ietf.org/html/rfc4960#section-3.2
The figure below illustrates the field format for the chunks to be
transmitted in the SCTP packet.  Each chunk is formatted with a Chunk
Type field, a chunk-specific Flag field, a Chunk Length field, and a
Value field.

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Chunk Type  | Chunk  Flags  |        Chunk Length           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                          Chunk Value                          |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type chunkHeader struct {
	typ   chunkType
	flags byte
	raw   []byte
}

const (
	chunkHeaderSize = 4
)

func (c *chunkHeader) unmarshal(raw []byte) error {
	if len(raw) < chunkHeaderSize {
		return errors.Errorf("raw only %d bytes, %d is the minimum length for a SCTP chunk", len(raw), chunkHeaderSize)
	}

	c.typ = chunkType(raw[0])
	c.flags = raw[1]
	length := binary.BigEndian.Uint16(raw[2:])

	// Length includes Chunk header
	valueLength := int(length - chunkHeaderSize)
	lengthAfterValue := len(raw) - (chunkHeaderSize + valueLength)

	if lengthAfterValue < 0 {
		return errors.Errorf("Not enough data left in SCTP packet to satisfy requested length remain %d req %d ", valueLength, len(raw)-chunkHeaderSize)
	} else if lengthAfterValue < 4 {
		// https://tools.ietf.org/html/rfc4960#section-3.2
		// The Chunk Length field does not count any chunk padding.
		// Chunks (including Type, Length, and Value fields) are padded out
		// by the sender with all zero bytes to be a multiple of 4 bytes
		// long.  This padding MUST NOT be more than 3 bytes in total.  The
		// Chunk Length value does not include terminating padding of the
		// chunk.  However, it does include padding of any variable-length
		// parameter except the last parameter in the chunk.  The receiver
		// MUST ignore the padding.
		for i := lengthAfterValue; i > 0; i-- {
			paddingOffset := chunkHeaderSize + valueLength + (i - 1)
			if raw[paddingOffset] != 0 {
				return errors.Errorf("Chunk padding is non-zero at offset %d ", paddingOffset)
			}
		}
	}

	c.raw = raw[chunkHeaderSize : chunkHeaderSize+valueLength]
	return nil
}

func (c *chunkHeader) marshal() ([]byte, error) {
	raw := make([]byte, 4+len(c.raw))

	raw[0] = uint8(c.typ)
	raw[1] = c.flags
	binary.BigEndian.PutUint16(raw[2:], uint16(len(c.raw)+chunkHeaderSize))
	copy(raw[4:], c.raw)
	return raw, nil
}

func (c *chunkHeader) valueLength() int {
	return len(c.raw)
}

// String makes chunkHeader printable
func (c chunkHeader) String() string {
	return c.typ.String()
}
