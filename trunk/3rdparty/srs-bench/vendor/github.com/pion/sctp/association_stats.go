// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

import (
	"sync/atomic"
)

type associationStats struct {
	nPacketsReceived uint64
	nPacketsSent     uint64
	nDATAs           uint64
	nSACKsReceived   uint64
	nSACKsSent       uint64
	nT3Timeouts      uint64
	nAckTimeouts     uint64
	nFastRetrans     uint64
}

func (s *associationStats) incPacketsReceived() {
	atomic.AddUint64(&s.nPacketsReceived, 1)
}

func (s *associationStats) getNumPacketsReceived() uint64 {
	return atomic.LoadUint64(&s.nPacketsReceived)
}

func (s *associationStats) incPacketsSent() {
	atomic.AddUint64(&s.nPacketsSent, 1)
}

func (s *associationStats) getNumPacketsSent() uint64 {
	return atomic.LoadUint64(&s.nPacketsSent)
}

func (s *associationStats) incDATAs() {
	atomic.AddUint64(&s.nDATAs, 1)
}

func (s *associationStats) getNumDATAs() uint64 {
	return atomic.LoadUint64(&s.nDATAs)
}

func (s *associationStats) incSACKsReceived() {
	atomic.AddUint64(&s.nSACKsReceived, 1)
}

func (s *associationStats) getNumSACKsReceived() uint64 {
	return atomic.LoadUint64(&s.nSACKsReceived)
}

func (s *associationStats) incSACKsSent() {
	atomic.AddUint64(&s.nSACKsSent, 1)
}

func (s *associationStats) getNumSACKsSent() uint64 {
	return atomic.LoadUint64(&s.nSACKsSent)
}

func (s *associationStats) incT3Timeouts() {
	atomic.AddUint64(&s.nT3Timeouts, 1)
}

func (s *associationStats) getNumT3Timeouts() uint64 {
	return atomic.LoadUint64(&s.nT3Timeouts)
}

func (s *associationStats) incAckTimeouts() {
	atomic.AddUint64(&s.nAckTimeouts, 1)
}

func (s *associationStats) getNumAckTimeouts() uint64 {
	return atomic.LoadUint64(&s.nAckTimeouts)
}

func (s *associationStats) incFastRetrans() {
	atomic.AddUint64(&s.nFastRetrans, 1)
}

func (s *associationStats) getNumFastRetrans() uint64 {
	return atomic.LoadUint64(&s.nFastRetrans)
}

func (s *associationStats) reset() {
	atomic.StoreUint64(&s.nPacketsReceived, 0)
	atomic.StoreUint64(&s.nPacketsSent, 0)
	atomic.StoreUint64(&s.nDATAs, 0)
	atomic.StoreUint64(&s.nSACKsReceived, 0)
	atomic.StoreUint64(&s.nSACKsSent, 0)
	atomic.StoreUint64(&s.nT3Timeouts, 0)
	atomic.StoreUint64(&s.nAckTimeouts, 0)
	atomic.StoreUint64(&s.nFastRetrans, 0)
}
