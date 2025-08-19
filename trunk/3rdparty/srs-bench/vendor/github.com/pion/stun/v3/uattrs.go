// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package stun

import "errors"

// UnknownAttributes represents UNKNOWN-ATTRIBUTES attribute.
//
// RFC 5389 Section 15.9
type UnknownAttributes []AttrType

func (a UnknownAttributes) String() string {
	s := ""
	if len(a) == 0 {
		return "<nil>"
	}
	last := len(a) - 1
	for i, t := range a {
		s += t.String()
		if i != last {
			s += ", "
		}
	}
	return s
}

// type size is 16 bit.
const attrTypeSize = 4

// AddTo adds UNKNOWN-ATTRIBUTES attribute to message.
func (a UnknownAttributes) AddTo(m *Message) error {
	v := make([]byte, 0, attrTypeSize*20) // 20 should be enough
	// If len(a.Types) > 20, there will be allocations.
	for i, t := range a {
		v = append(v, 0, 0, 0, 0) // 4 times by 0 (16 bits)
		first := attrTypeSize * i
		last := first + attrTypeSize
		bin.PutUint16(v[first:last], t.Value())
	}
	m.Add(AttrUnknownAttributes, v)
	return nil
}

// ErrBadUnknownAttrsSize means that UNKNOWN-ATTRIBUTES attribute value
// has invalid length.
var ErrBadUnknownAttrsSize = errors.New("bad UNKNOWN-ATTRIBUTES size")

// GetFrom parses UNKNOWN-ATTRIBUTES from message.
func (a *UnknownAttributes) GetFrom(m *Message) error {
	v, err := m.Get(AttrUnknownAttributes)
	if err != nil {
		return err
	}
	if len(v)%attrTypeSize != 0 {
		return ErrBadUnknownAttrsSize
	}
	*a = (*a)[:0]
	first := 0
	for first < len(v) {
		last := first + attrTypeSize
		*a = append(*a, AttrType(bin.Uint16(v[first:last])))
		first = last
	}
	return nil
}
