// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

type unmarshalCache struct {
	sessionAttributes []Attribute
	mediaAttributes   []Attribute
}

func (c *unmarshalCache) reset() {
	c.sessionAttributes = c.sessionAttributes[:0]
	c.mediaAttributes = c.mediaAttributes[:0]
}

func (c *unmarshalCache) getSessionAttribute() *Attribute {
	c.sessionAttributes = append(c.sessionAttributes, Attribute{})

	return &c.sessionAttributes[len(c.sessionAttributes)-1]
}

func (c *unmarshalCache) cloneSessionAttributes() []Attribute {
	if len(c.sessionAttributes) == 0 {
		return nil
	}
	s := make([]Attribute, len(c.sessionAttributes))
	copy(s, c.sessionAttributes)
	c.sessionAttributes = c.sessionAttributes[:0]

	return s
}

func (c *unmarshalCache) getMediaAttribute() *Attribute {
	c.mediaAttributes = append(c.mediaAttributes, Attribute{})

	return &c.mediaAttributes[len(c.mediaAttributes)-1]
}

func (c *unmarshalCache) cloneMediaAttributes() []Attribute {
	if len(c.mediaAttributes) == 0 {
		return nil
	}
	s := make([]Attribute, len(c.mediaAttributes))
	copy(s, c.mediaAttributes)
	c.mediaAttributes = c.mediaAttributes[:0]

	return s
}
