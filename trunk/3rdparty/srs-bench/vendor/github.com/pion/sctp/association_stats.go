package sctp

import (
	"sync/atomic"
)

type associationStats struct {
	nDATAs       uint64
	nSACKs       uint64
	nT3Timeouts  uint64
	nAckTimeouts uint64
	nFastRetrans uint64
}

func (s *associationStats) incDATAs() {
	atomic.AddUint64(&s.nDATAs, 1)
}

func (s *associationStats) getNumDATAs() uint64 {
	return atomic.LoadUint64(&s.nDATAs)
}

func (s *associationStats) incSACKs() {
	atomic.AddUint64(&s.nSACKs, 1)
}

func (s *associationStats) getNumSACKs() uint64 {
	return atomic.LoadUint64(&s.nSACKs)
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
	atomic.StoreUint64(&s.nDATAs, 0)
	atomic.StoreUint64(&s.nSACKs, 0)
	atomic.StoreUint64(&s.nT3Timeouts, 0)
	atomic.StoreUint64(&s.nAckTimeouts, 0)
	atomic.StoreUint64(&s.nFastRetrans, 0)
}
