package sctp

import (
	"github.com/pkg/errors"
)

/*
CookieEcho represents an SCTP Chunk of type CookieEcho

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 10   |Chunk  Flags   |         Length                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Cookie                                    |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

*/
type chunkCookieEcho struct {
	chunkHeader
	cookie []byte
}

func (c *chunkCookieEcho) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if c.typ != ctCookieEcho {
		return errors.Errorf("ChunkType is not of type COOKIEECHO, actually is %s", c.typ.String())
	}
	c.cookie = c.raw

	return nil
}

func (c *chunkCookieEcho) marshal() ([]byte, error) {
	c.chunkHeader.typ = ctCookieEcho
	c.chunkHeader.raw = c.cookie
	return c.chunkHeader.marshal()
}

func (c *chunkCookieEcho) check() (abort bool, err error) {
	return false, nil
}
