package sctp

import (
	"encoding/binary"
	"errors"
	"fmt"
)

/*
chunkShutdown represents an SCTP Chunk of type chunkShutdown

0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 7    | Chunk  Flags  |      Length = 8               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Cumulative TSN Ack                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type chunkShutdown struct {
	chunkHeader
	cumulativeTSNAck uint32
}

const (
	cumulativeTSNAckLength = 4
)

// Shutdown chunk errors
var (
	ErrInvalidChunkSize     = errors.New("invalid chunk size")
	ErrChunkTypeNotShutdown = errors.New("ChunkType is not of type SHUTDOWN")
)

func (c *chunkShutdown) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if c.typ != ctShutdown {
		return fmt.Errorf("%w: actually is %s", ErrChunkTypeNotShutdown, c.typ.String())
	}

	if len(c.raw) != cumulativeTSNAckLength {
		return ErrInvalidChunkSize
	}

	c.cumulativeTSNAck = binary.BigEndian.Uint32(c.raw[0:])

	return nil
}

func (c *chunkShutdown) marshal() ([]byte, error) {
	out := make([]byte, cumulativeTSNAckLength)
	binary.BigEndian.PutUint32(out[0:], c.cumulativeTSNAck)

	c.typ = ctShutdown
	c.raw = out
	return c.chunkHeader.marshal()
}

func (c *chunkShutdown) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkShutdown printable
func (c *chunkShutdown) String() string {
	return c.chunkHeader.String()
}
