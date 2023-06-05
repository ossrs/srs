// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"encoding/binary"

	"github.com/pion/stun"
)

// tiebreaker is common helper for ICE-{CONTROLLED,CONTROLLING}
// and represents the so-called tiebreaker number.
type tiebreaker uint64

const tiebreakerSize = 8 // 64 bit

// AddToAs adds tiebreaker value to m as t attribute.
func (a tiebreaker) AddToAs(m *stun.Message, t stun.AttrType) error {
	v := make([]byte, tiebreakerSize)
	binary.BigEndian.PutUint64(v, uint64(a))
	m.Add(t, v)
	return nil
}

// GetFromAs decodes tiebreaker value in message getting it as for t type.
func (a *tiebreaker) GetFromAs(m *stun.Message, t stun.AttrType) error {
	v, err := m.Get(t)
	if err != nil {
		return err
	}
	if err = stun.CheckSize(t, len(v), tiebreakerSize); err != nil {
		return err
	}
	*a = tiebreaker(binary.BigEndian.Uint64(v))
	return nil
}

// AttrControlled represents ICE-CONTROLLED attribute.
type AttrControlled uint64

// AddTo adds ICE-CONTROLLED to message.
func (c AttrControlled) AddTo(m *stun.Message) error {
	return tiebreaker(c).AddToAs(m, stun.AttrICEControlled)
}

// GetFrom decodes ICE-CONTROLLED from message.
func (c *AttrControlled) GetFrom(m *stun.Message) error {
	return (*tiebreaker)(c).GetFromAs(m, stun.AttrICEControlled)
}

// AttrControlling represents ICE-CONTROLLING attribute.
type AttrControlling uint64

// AddTo adds ICE-CONTROLLING to message.
func (c AttrControlling) AddTo(m *stun.Message) error {
	return tiebreaker(c).AddToAs(m, stun.AttrICEControlling)
}

// GetFrom decodes ICE-CONTROLLING from message.
func (c *AttrControlling) GetFrom(m *stun.Message) error {
	return (*tiebreaker)(c).GetFromAs(m, stun.AttrICEControlling)
}

// AttrControl is helper that wraps ICE-{CONTROLLED,CONTROLLING}.
type AttrControl struct {
	Role       Role
	Tiebreaker uint64
}

// AddTo adds ICE-CONTROLLED or ICE-CONTROLLING attribute depending on Role.
func (c AttrControl) AddTo(m *stun.Message) error {
	if c.Role == Controlling {
		return tiebreaker(c.Tiebreaker).AddToAs(m, stun.AttrICEControlling)
	}
	return tiebreaker(c.Tiebreaker).AddToAs(m, stun.AttrICEControlled)
}

// GetFrom decodes Role and Tiebreaker value from message.
func (c *AttrControl) GetFrom(m *stun.Message) error {
	if m.Contains(stun.AttrICEControlling) {
		c.Role = Controlling
		return (*tiebreaker)(&c.Tiebreaker).GetFromAs(m, stun.AttrICEControlling)
	}
	if m.Contains(stun.AttrICEControlled) {
		c.Role = Controlled
		return (*tiebreaker)(&c.Tiebreaker).GetFromAs(m, stun.AttrICEControlled)
	}
	return stun.ErrAttributeNotFound
}
