package sctp

import (
	"fmt"

	"github.com/pkg/errors"
)

// https://tools.ietf.org/html/rfc6525#section-3.1
// chunkReconfig represents an SCTP Chunk used to reconfigure streams.
//
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// | Type = 130    |  Chunk Flags  |      Chunk Length             |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// \                                                               \
// /                  Re-configuration Parameter                   /
// \                                                               \
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// \                                                               \
// /             Re-configuration Parameter (optional)             /
// \                                                               \
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

type chunkReconfig struct {
	chunkHeader
	paramA param
	paramB param
}

func (c *chunkReconfig) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}
	pType, err := parseParamType(c.raw)
	if err != nil {
		return errors.Wrap(err, "failed to parse param type")
	}
	a, err := buildParam(pType, c.raw)
	if err != nil {
		return err
	}
	c.paramA = a

	padding := getPadding(a.length())
	offset := a.length() + padding
	if len(c.raw) > offset {
		pType, err := parseParamType(c.raw[offset:])
		if err != nil {
			return errors.Wrap(err, "failed to parse param type")
		}
		b, err := buildParam(pType, c.raw[offset:])
		if err != nil {
			return err
		}
		c.paramB = b
	}

	return nil
}

func (c *chunkReconfig) marshal() ([]byte, error) {
	out, err := c.paramA.marshal()
	if err != nil {
		return nil, errors.Wrap(err, "Unable to marshal parameter A for reconfig")
	}
	if c.paramB != nil {
		// Pad param A
		out = padByte(out, getPadding(len(out)))

		outB, err := c.paramB.marshal()
		if err != nil {
			return nil, errors.Wrap(err, "Unable to marshal parameter B for reconfig")
		}

		out = append(out, outB...)
	}

	c.typ = ctReconfig
	c.raw = out
	return c.chunkHeader.marshal()
}

func (c *chunkReconfig) check() (abort bool, err error) {
	// nolint:godox
	// TODO: check allowed combinations:
	// https://tools.ietf.org/html/rfc6525#section-3.1
	return true, nil
}

// String makes chunkReconfig printable
func (c *chunkReconfig) String() string {
	res := fmt.Sprintf("Param A:\n %s", c.paramA)
	if c.paramB != nil {
		res += fmt.Sprintf("Param B:\n %s", c.paramB)
	}
	return res
}
