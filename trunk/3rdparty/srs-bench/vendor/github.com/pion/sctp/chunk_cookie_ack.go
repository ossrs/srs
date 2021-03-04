package sctp

import (
	"github.com/pkg/errors"
)

/*
chunkCookieAck represents an SCTP Chunk of type chunkCookieAck

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 11   |Chunk  Flags   |     Length = 4                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type chunkCookieAck struct {
	chunkHeader
}

func (c *chunkCookieAck) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if c.typ != ctCookieAck {
		return errors.Errorf("ChunkType is not of type COOKIEACK, actually is %s", c.typ.String())
	}

	return nil
}

func (c *chunkCookieAck) marshal() ([]byte, error) {
	c.chunkHeader.typ = ctCookieAck
	return c.chunkHeader.marshal()
}

func (c *chunkCookieAck) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkCookieAck printable
func (c *chunkCookieAck) String() string {
	return c.chunkHeader.String()
}
