// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"math/rand"
	"time"
)

// LossFilter is a wrapper around NICs, that drops some of the packets passed to
// onInboundChunk
type LossFilter struct {
	NIC
	chance int
}

// NewLossFilter creates a new LossFilter that drops every packet with a
// probability of chance/100. Every packet that is not dropped is passed on to
// the given NIC.
func NewLossFilter(nic NIC, chance int) (*LossFilter, error) {
	f := &LossFilter{
		NIC:    nic,
		chance: chance,
	}
	rand.Seed(time.Now().UTC().UnixNano())
	return f, nil
}

func (f *LossFilter) onInboundChunk(c Chunk) {
	if rand.Intn(100) < f.chance { //nolint:gosec
		return
	}

	f.NIC.onInboundChunk(c)
}
