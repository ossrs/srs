package sctp

import (
	"errors"
	"fmt"
)

/*
chunkShutdownAck represents an SCTP Chunk of type chunkShutdownAck

0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 8    | Chunk  Flags  |      Length = 4               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type chunkShutdownAck struct {
	chunkHeader
}

// Shutdown ack chunk errors
var (
	ErrChunkTypeNotShutdownAck = errors.New("ChunkType is not of type SHUTDOWN-ACK")
)

func (c *chunkShutdownAck) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if c.typ != ctShutdownAck {
		return fmt.Errorf("%w: actually is %s", ErrChunkTypeNotShutdownAck, c.typ.String())
	}

	return nil
}

func (c *chunkShutdownAck) marshal() ([]byte, error) {
	c.typ = ctShutdownAck
	return c.chunkHeader.marshal()
}

func (c *chunkShutdownAck) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkShutdownAck printable
func (c *chunkShutdownAck) String() string {
	return c.chunkHeader.String()
}
