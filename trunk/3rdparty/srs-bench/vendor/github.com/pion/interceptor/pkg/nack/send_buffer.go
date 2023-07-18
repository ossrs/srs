// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"fmt"
	"sync"
)

const (
	uint16SizeHalf = 1 << 15
)

type sendBuffer struct {
	packets   []*retainablePacket
	size      uint16
	lastAdded uint16
	started   bool

	m sync.RWMutex
}

func newSendBuffer(size uint16) (*sendBuffer, error) {
	allowedSizes := make([]uint16, 0)
	correctSize := false
	for i := 0; i < 16; i++ {
		if size == 1<<i {
			correctSize = true
			break
		}
		allowedSizes = append(allowedSizes, 1<<i)
	}

	if !correctSize {
		return nil, fmt.Errorf("%w: %d is not a valid size, allowed sizes: %v", ErrInvalidSize, size, allowedSizes)
	}

	return &sendBuffer{
		packets: make([]*retainablePacket, size),
		size:    size,
	}, nil
}

func (s *sendBuffer) add(packet *retainablePacket) {
	s.m.Lock()
	defer s.m.Unlock()

	seq := packet.Header().SequenceNumber
	if !s.started {
		s.packets[seq%s.size] = packet
		s.lastAdded = seq
		s.started = true
		return
	}

	diff := seq - s.lastAdded
	if diff == 0 {
		return
	} else if diff < uint16SizeHalf {
		for i := s.lastAdded + 1; i != seq; i++ {
			idx := i % s.size
			prevPacket := s.packets[idx]
			if prevPacket != nil {
				prevPacket.Release()
			}
			s.packets[idx] = nil
		}
	}

	idx := seq % s.size
	prevPacket := s.packets[idx]
	if prevPacket != nil {
		prevPacket.Release()
	}
	s.packets[idx] = packet
	s.lastAdded = seq
}

func (s *sendBuffer) get(seq uint16) *retainablePacket {
	s.m.RLock()
	defer s.m.RUnlock()

	diff := s.lastAdded - seq
	if diff >= uint16SizeHalf {
		return nil
	}

	if diff >= s.size {
		return nil
	}

	pkt := s.packets[seq%s.size]
	if pkt != nil {
		if pkt.Header().SequenceNumber != seq {
			return nil
		}
		// already released
		if err := pkt.Retain(); err != nil {
			return nil
		}
	}
	return pkt
}
