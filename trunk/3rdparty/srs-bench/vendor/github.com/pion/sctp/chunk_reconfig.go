// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"errors"
	"fmt"
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

// Reconfigure chunk errors.
var (
	ErrChunkParseParamTypeFailed        = errors.New("failed to parse param type")
	ErrChunkMarshalParamAReconfigFailed = errors.New("unable to marshal parameter A for reconfig")
	ErrChunkMarshalParamBReconfigFailed = errors.New("unable to marshal parameter B for reconfig")
)

func (c *chunkReconfig) unmarshal(raw []byte) error {
	if err := c.chunkHeader.unmarshal(raw); err != nil {
		return err
	}
	pType, err := parseParamType(c.raw)
	if err != nil {
		return fmt.Errorf("%w: %v", ErrChunkParseParamTypeFailed, err) //nolint:errorlint
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
			return fmt.Errorf("%w: %v", ErrChunkParseParamTypeFailed, err) //nolint:errorlint
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
		return nil, fmt.Errorf("%w: %v", ErrChunkMarshalParamAReconfigFailed, err) //nolint:errorlint
	}
	if c.paramB != nil {
		// Pad param A
		out = padByte(out, getPadding(len(out)))

		outB, err := c.paramB.marshal()
		if err != nil {
			return nil, fmt.Errorf("%w: %v", ErrChunkMarshalParamBReconfigFailed, err) //nolint:errorlint
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

// String makes chunkReconfig printable.
func (c *chunkReconfig) String() string {
	res := fmt.Sprintf("Param A:\n %s", c.paramA)
	if c.paramB != nil {
		res += fmt.Sprintf("Param B:\n %s", c.paramB)
	}

	return res
}
