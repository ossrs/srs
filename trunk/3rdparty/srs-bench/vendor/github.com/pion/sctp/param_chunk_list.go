// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

type paramChunkList struct {
	paramHeader
	chunkTypes []chunkType
}

func (c *paramChunkList) marshal() ([]byte, error) {
	c.typ = chunkList
	c.raw = make([]byte, len(c.chunkTypes))
	for i, t := range c.chunkTypes {
		c.raw[i] = byte(t)
	}

	return c.paramHeader.marshal()
}

func (c *paramChunkList) unmarshal(raw []byte) (param, error) {
	err := c.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	for _, t := range c.raw {
		c.chunkTypes = append(c.chunkTypes, chunkType(t))
	}

	return c, nil
}
