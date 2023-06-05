package sctp

import (
	"errors"
	"fmt"
)

/*
chunkShutdownComplete represents an SCTP Chunk of type chunkShutdownComplete

0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 14   |Reserved     |T|      Length = 4               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type chunkShutdownComplete struct {
	chunkHeader
}

// Shutdown complete chunk errors
var (
	ErrChunkTypeNotShutdownComplete = errors.New("ChunkType is not of type SHUTDOWN-COMPLETE")
)

func (c *chunkShutdownComplete) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if c.typ != ctShutdownComplete {
		return fmt.Errorf("%w: actually is %s", ErrChunkTypeNotShutdownComplete, c.typ.String())
	}

	return nil
}

func (c *chunkShutdownComplete) marshal() ([]byte, error) {
	c.typ = ctShutdownComplete
	return c.chunkHeader.marshal()
}

func (c *chunkShutdownComplete) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkShutdownComplete printable
func (c *chunkShutdownComplete) String() string {
	return c.chunkHeader.String()
}
