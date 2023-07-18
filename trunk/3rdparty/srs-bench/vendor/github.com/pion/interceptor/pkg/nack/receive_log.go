// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"fmt"
	"sync"
)

type receiveLog struct {
	packets         []uint64
	size            uint16
	end             uint16
	started         bool
	lastConsecutive uint16
	m               sync.RWMutex
}

func newReceiveLog(size uint16) (*receiveLog, error) {
	allowedSizes := make([]uint16, 0)
	correctSize := false
	for i := 6; i < 16; i++ {
		if size == 1<<i {
			correctSize = true
			break
		}
		allowedSizes = append(allowedSizes, 1<<i)
	}

	if !correctSize {
		return nil, fmt.Errorf("%w: %d is not a valid size, allowed sizes: %v", ErrInvalidSize, size, allowedSizes)
	}

	return &receiveLog{
		packets: make([]uint64, size/64),
		size:    size,
	}, nil
}

func (s *receiveLog) add(seq uint16) {
	s.m.Lock()
	defer s.m.Unlock()

	if !s.started {
		s.setReceived(seq)
		s.end = seq
		s.started = true
		s.lastConsecutive = seq
		return
	}

	diff := seq - s.end
	switch {
	case diff == 0:
		return
	case diff < uint16SizeHalf:
		// this means a positive diff, in other words seq > end (with counting for rollovers)
		for i := s.end + 1; i != seq; i++ {
			// clear packets between end and seq (these may contain packets from a "size" ago)
			s.delReceived(i)
		}
		s.end = seq

		if s.lastConsecutive+1 == seq {
			s.lastConsecutive = seq
		} else if seq-s.lastConsecutive > s.size {
			s.lastConsecutive = seq - s.size
			s.fixLastConsecutive() // there might be valid packets at the beginning of the buffer now
		}
	case s.lastConsecutive+1 == seq:
		// negative diff, seq < end (with counting for rollovers)
		s.lastConsecutive = seq
		s.fixLastConsecutive() // there might be other valid packets after seq
	}

	s.setReceived(seq)
}

func (s *receiveLog) get(seq uint16) bool {
	s.m.RLock()
	defer s.m.RUnlock()

	diff := s.end - seq
	if diff >= uint16SizeHalf {
		return false
	}

	if diff >= s.size {
		return false
	}

	return s.getReceived(seq)
}

func (s *receiveLog) missingSeqNumbers(skipLastN uint16) []uint16 {
	s.m.RLock()
	defer s.m.RUnlock()

	until := s.end - skipLastN
	if until-s.lastConsecutive >= uint16SizeHalf {
		// until < s.lastConsecutive (counting for rollover)
		return nil
	}

	missingPacketSeqNums := make([]uint16, 0)
	for i := s.lastConsecutive + 1; i != until+1; i++ {
		if !s.getReceived(i) {
			missingPacketSeqNums = append(missingPacketSeqNums, i)
		}
	}

	return missingPacketSeqNums
}

func (s *receiveLog) setReceived(seq uint16) {
	pos := seq % s.size
	s.packets[pos/64] |= 1 << (pos % 64)
}

func (s *receiveLog) delReceived(seq uint16) {
	pos := seq % s.size
	s.packets[pos/64] &^= 1 << (pos % 64)
}

func (s *receiveLog) getReceived(seq uint16) bool {
	pos := seq % s.size
	return (s.packets[pos/64] & (1 << (pos % 64))) != 0
}

func (s *receiveLog) fixLastConsecutive() {
	i := s.lastConsecutive + 1
	for ; i != s.end+1 && s.getReceived(i); i++ { //nolint:revive
		// find all consecutive packets
	}

	s.lastConsecutive = i - 1
}
